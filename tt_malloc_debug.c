#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#ifdef __linux__
#include <execinfo.h>
#elif __wasm__
#include "wasm_wrap.h"
#endif
#ifndef __TT_RBTREE_H__
#include "tt_rbtree.h"
#endif
#ifndef __TT_MALLOC_DEBUG_H__
#include "tt_malloc_debug.h"
#endif

#define debug_printf(fmt, ...)
// #define debug_printf(fmt, ...) printf(fmt, ##__VA_ARGS__)
// #define error_printf(fmt, ...)
// #define error_printf(fmt, ...)  printf("\x1b[1;31m[leakcheck] " fmt "\x1b[0m", ##__VA_ARGS__)
#define error_printf(fmt, ...)  printf("[leakcheck] " fmt, ##__VA_ARGS__)

#if 1
#define RAM_PREFIX "\x55\x55\x55\x55\x55\x55\x55\x55\xff\xff\xff\xff\xff\xff\xff\xff\x2a\x2a\x2a\x2a\x2a\x2a\x2a\x2a\x55\x55\x55\x55\x55\x55\x55\x55"
#define SIZE_PREFIX 32 /* 0: don't use prefix */
#define RAM_SUFFIX "\x55\x55\x55\x55\x55\x55\x55\x55\x2a\x2a\x2a\x2a\x2a\x2a\x2a\x2a\xff\xff\xff\xff\xff\xff\xff\xff\x55\x55\x55\x55\x55\x55\x55\x55"
#define SIZE_SUFFIX 32
#else
#define RAM_PREFIX "\x55\x55\x55\x55"
#define SIZE_PREFIX 4
#define RAM_SUFFIX "\x55\x55\x55\x55"
#define SIZE_SUFFIX 4
#endif

static rbt g_ram_rbt;
static pthread_mutex_t g_ram_rbt_lock;
static size_t g_max_used = 0;
static size_t g_ram_used = 0;

#ifdef  __cplusplus
extern "C" {
#endif

static int64_t gettime_usec() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static int compare_handler(rbt_data key1, rbt_data key2) {
    return (uintptr_t)key1.ptr - (uintptr_t)key2.ptr;
}

#if __linux__
static int get_backtrace(void **buffer, int size) {
    return backtrace(buffer, size);
}
static void show_backtrace(void **trace, int trace_cnt) {
    int i = 0;
    char **traces = NULL;

    traces = backtrace_symbols(trace, trace_cnt);
    if (traces != NULL) {
        for (i = 0; i < trace_cnt; i++) {
            printf("    %s\n", traces[i]);
        }
        BASE_FREE(traces);
    }
    printf("\n");
}
#elif __wasm__
static int get_backtrace(void **buffer, int size) {
    return web_backtrace((char **)&buffer[0]);
}
static void show_backtrace(void **trace, int trace_cnt) {
    printf("%s\n", (char *)trace[0]);
}
#else
static int get_backtrace(void **buffer, int size) {
    return 0;
}
static void show_backtrace(void **trace, int trace_cnt) {
}
#endif

static void print_handler(rbt_node *node) {
	RAM_RECORD *record = NULL;

    record = (RAM_RECORD *)(node->value.ptr);
    printf("size:%zu, ptr:%p\n", record->size, (void *)((uintptr_t)record->ptr + SIZE_PREFIX));
    show_backtrace(record->trace, record->trace_cnt);
}

static void free_handler(rbt_node *node) {
    // BASE_FREE(node->value.ptr);
}

static int append_record(void *ptr, size_t size) {
	RAM_RECORD *p_new = NULL;
    rbt_data key, value;

	p_new = (RAM_RECORD *)BASE_MALLOC(sizeof(RAM_RECORD));
	if (p_new == NULL) {
		return -1;
	}
	memset(p_new, 0x00, sizeof(RAM_RECORD));
    p_new->m_time = p_new->c_time = gettime_usec();
	p_new->ptr = ptr;
	p_new->size = size;
    p_new->trace_cnt = get_backtrace(p_new->trace, MAX_TRACE);
    key.ptr = ptr;
    value.ptr = p_new;
    tt_rbt_insert(&g_ram_rbt, key, value);
	return 0;
}

void *NEW_MALLOC(size_t size) {
	void *ptr = NULL, *ret_ptr = NULL;
    void *trace[MAX_TRACE] = {NULL};
    int trace_cnt = 0;

    pthread_mutex_lock(&g_ram_rbt_lock);
	ptr = BASE_MALLOC(size + SIZE_PREFIX + SIZE_SUFFIX);
	if (ptr == NULL) {
        error_printf("malloc %zu failed.\n", size);
        trace_cnt = get_backtrace(trace, MAX_TRACE);
        show_backtrace(trace, trace_cnt);
        ret_ptr = NULL;
        goto func_end;
	}
    ret_ptr = ptr ? ptr + SIZE_PREFIX : NULL;
    if (SIZE_PREFIX > 0) {
        memcpy(ptr, RAM_PREFIX, SIZE_PREFIX);
    }
    memcpy(ptr + SIZE_PREFIX + size, RAM_SUFFIX, SIZE_SUFFIX);
#if ADD_POISON
    memset(ptr + SIZE_PREFIX, 'P', size);
#endif
    append_record(ptr, size);
    debug_printf("malloc %zu Bytes success, ret %p.\n", size, (void *)((uintptr_t)ptr + SIZE_PREFIX));
    g_ram_used += size;
    if (g_ram_used > g_max_used) {
        g_max_used = g_ram_used;
    }
func_end:
    pthread_mutex_unlock(&g_ram_rbt_lock);
    return ret_ptr;
}

void NEW_FREE(void *ptr) {
    rbt_node *node = NULL;
	RAM_RECORD *target = NULL;
	void *org_ptr = NULL;
    rbt_data key;
    void *trace[MAX_TRACE] = {NULL};
    int trace_cnt = 0;

    org_ptr = ptr ? ptr - SIZE_PREFIX : NULL;
	pthread_mutex_lock(&g_ram_rbt_lock);
    key.ptr = org_ptr;
    node = tt_rbt_search(g_ram_rbt, key);
	if (node != NULL) {
        target = (RAM_RECORD *)(node->value.ptr);
		if (SIZE_PREFIX > 0 && 0 != memcmp(org_ptr, RAM_PREFIX, SIZE_PREFIX)) {
			error_printf("prefix: free overflow heap %p\n", ptr);
            trace_cnt = get_backtrace(trace, MAX_TRACE);
            show_backtrace(trace, trace_cnt);
			error_printf("heap %p malloced at\n", ptr);
            show_backtrace(target->trace, target->trace_cnt);
		}
		if (0 != memcmp((void *)((uintptr_t)ptr + target->size), RAM_SUFFIX, SIZE_SUFFIX)) {
			error_printf("suffix: free overflow heap %p\n", ptr);
            trace_cnt = get_backtrace(trace, MAX_TRACE);
            show_backtrace(trace, trace_cnt);
			error_printf("heap %p malloced at\n", ptr);
            show_backtrace(target->trace, target->trace_cnt);
		}
		debug_printf("free heap %p(%zu Bytes).\n", ptr, target->size);
#if ADD_POISON
        memset(org_ptr, '*', target->size + SIZE_PREFIX + SIZE_SUFFIX);
#endif
		BASE_FREE(org_ptr);
        g_ram_used -= target->size;
		tt_rbt_delete_node(&g_ram_rbt, node);
	} else {
		error_printf("free invalid addr %p.\n", ptr);
        trace_cnt = get_backtrace(trace, MAX_TRACE);
        show_backtrace(trace, trace_cnt);
		// BASE_FREE(ptr); /* TODO need check target ? */
	}
	pthread_mutex_unlock(&g_ram_rbt_lock);
}

void *NEW_REALLOC(void *ptr, size_t size) {
    rbt_node *node = NULL;
	RAM_RECORD *target = NULL;
	void *ptr_new = NULL, *org_ptr = NULL, *ret_ptr = NULL;
    size_t old_size = 0;
    rbt_data key;
    void *trace[MAX_TRACE] = {NULL};
    int trace_cnt = 0;

    org_ptr = ptr ? ptr - SIZE_PREFIX : NULL;
	pthread_mutex_lock(&g_ram_rbt_lock);
    key.ptr = org_ptr;
    node = tt_rbt_search(g_ram_rbt, key);
	if (node != NULL) {
        target = (RAM_RECORD *)(node->value.ptr);
		if (SIZE_PREFIX > 0 && 0 != memcmp(org_ptr, RAM_PREFIX, SIZE_PREFIX)) {
			error_printf("prefix: realloc overflow heap %p\n", ptr);
            trace_cnt = get_backtrace(trace, MAX_TRACE);
            show_backtrace(trace, trace_cnt);
			error_printf("heap %p malloced at\n", ptr);
            show_backtrace(target->trace, target->trace_cnt);
		}
		if (0 != memcmp((void *)((uintptr_t)ptr + target->size), RAM_SUFFIX, SIZE_SUFFIX)) {
			error_printf("suffix: realloc overflow heap %p\n", ptr);
            trace_cnt = get_backtrace(trace, MAX_TRACE);
            show_backtrace(trace, trace_cnt);
			error_printf("heap %p malloced at\n", ptr);
            show_backtrace(target->trace, target->trace_cnt);
		}
		debug_printf("realloc heap %p(%zu Bytes).\n", ptr, target->size);
        g_ram_used -= target->size;
        old_size = target->size;
        tt_rbt_delete_node(&g_ram_rbt, node);
	} else {
        if (ptr != NULL) {
            error_printf("realloc invalid addr %p.\n", ptr);
            trace_cnt = get_backtrace(trace, MAX_TRACE);
            show_backtrace(trace, trace_cnt);
            ret_ptr = BASE_REALLOC(ptr, size); /* pass through */
            goto func_end;
        }
	}

    if (size > 0) {
        ptr_new = BASE_MALLOC(size + SIZE_PREFIX + SIZE_SUFFIX); /* use memalign */
        if (ptr_new != NULL) {
#if ADD_POISON
            memset(ptr_new + SIZE_PREFIX, 'P', size);
#endif
            if (ptr != NULL && node != NULL) {
                memcpy(ptr_new + SIZE_PREFIX, ptr, old_size);
            }
            if (SIZE_PREFIX > 0){
                memcpy(ptr_new, RAM_PREFIX, SIZE_PREFIX);
            }
            memcpy(ptr_new + SIZE_PREFIX + size, RAM_SUFFIX, SIZE_SUFFIX);
        }
    }
    if (ptr != NULL) {
        if (node != NULL) {
#if ADD_POISON
            memset(org_ptr, '*', old_size + SIZE_PREFIX + SIZE_SUFFIX);
#endif
            BASE_FREE(org_ptr); /* TODO need check target ? */
        }
    }
    ret_ptr = ptr_new ? (void *)((uintptr_t)ptr_new + SIZE_PREFIX) : NULL;
	if (size != 0 && ret_ptr == NULL) {
		error_printf("realloc %zu failed.\n", size);
        trace_cnt = get_backtrace(trace, MAX_TRACE);
        show_backtrace(trace, trace_cnt);
	} else {
        debug_printf("realloc heap %p(%zu Bytes) -> %p(%zu Bytes).\n", ptr, node ? old_size : 0, ret_ptr, size);
		append_record(ptr_new, size);
        g_ram_used += size;
        if (g_ram_used > g_max_used) {
            g_max_used = g_ram_used;
        }
	}
func_end:
	pthread_mutex_unlock(&g_ram_rbt_lock);
    return ret_ptr;
}

void show_ram() {
    printf("=== max alloc %zu Bytes, %zu Bytes not free ===\n", g_max_used, g_ram_used);
	pthread_mutex_lock(&g_ram_rbt_lock);
    tt_rbt_print(g_ram_rbt);
	pthread_mutex_unlock(&g_ram_rbt_lock);
}

int leak_check_init() {
    debug_printf("%s\n", __func__);
    pthread_mutex_init(&g_ram_rbt_lock, NULL);
    return tt_rbt_init(&g_ram_rbt, compare_handler, print_handler, free_handler);
}
void leak_check_destroy()  {
    debug_printf("%s\n", __func__);
    show_ram();
    g_max_used = 0;
    tt_rbt_destroy(&g_ram_rbt);
    pthread_mutex_destroy(&g_ram_rbt_lock);
}

char *NEW_STRDUP(const char *s) {
    char *ptr = NULL;
    size_t len = 0;
    if (s == NULL) {
        return NULL;
    }
    len = strlen(s) + 1;
    ptr = NEW_MALLOC(len);
    if (ptr == NULL) {
        return NULL;
    }
    memcpy(ptr, s, len);
    ptr[len] = '\0';
    return ptr;
}

char *NEW_STRNDUP(const char *s, size_t len) {
    char *ret = NULL, *end = NULL;

    if (s == NULL) {
        return NULL;
    }
    end = memchr(s, 0, len);
    if (end != NULL) {
        len = end - s;
    }
    ret = NEW_MALLOC(len + 1);
    if (ret == NULL)
        return NULL;

    memcpy(ret, s, len);
    ret[len] = '\0';
    return ret;
}
#ifdef  __cplusplus
}
#endif

#if 0 /* test api */

int main() {
	char *p = NULL;
    int i = 0;

#if 1 /* normal */
	p = (char *)malloc(5);
    printf("malloc ret %p\n", p);
	memset(p, 0, 5);
    printf("free %p\n", p);
    free(p);
#endif

#if 1 /* write overflow at end */
	p = (char *)malloc(5);
    printf("malloc ret %p\n", p);
	memset(p, 0, 6);
    printf("free %p\n", p);
    free(p);
#endif

#if 1 /* write overflow at begin */
	p = (char *)malloc(5);
    printf("malloc ret %p\n", p);
	memset(p - 1, 0, 5);
    printf("free %p\n", p);
    free(p);
#endif

#if 1 /* free invalid pointer */
	p = (char *)malloc(5);
    printf("malloc ret %p\n", p);
	memset(p, 0, 5);
    printf("free %p\n", p);
    free(p + 1);
#endif

    for (i = 0; i < 10; i++) {
        malloc(20);
    }
    show_ram();
    return 0;
}

#endif
