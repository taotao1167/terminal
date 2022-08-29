#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#if defined(_WIN32)
#include "w32_semaphore.h"
#else
#include <semaphore.h>
#endif
#include "pi_list.h"

#define MY_MALLOC(x) malloc((x))
#define MY_FREE(x) free((x))
#define MY_REALLOC(x, y) realloc((x), (y))

int pi_list_init(PI_LIST *list, size_t limit, void (* free_payload)(void *payload)) {
    if (list == NULL) {
        return -1;
    }
    memset(list, 0x00, sizeof(PI_LIST));
    list->limit = limit;
    list->free_payload = free_payload;
    pthread_mutex_init(&(list->lock), NULL);
    return 0;
}

int pi_list_destroy(PI_LIST *list) {
    PI_LISTE *p_cur = NULL, *p_next = NULL;

    if (list == NULL) {
        return -1;
    }
    pthread_mutex_lock(&(list->lock));
    for (p_cur = list->head; p_cur != NULL; p_cur = p_next) {
        p_next = p_cur->next;
        if (list->free_payload != NULL) {
            list->free_payload(p_cur->payload);
        }
        free(p_cur);
    }
    memset(list, 0x00, sizeof(PI_LIST));
    pthread_mutex_unlock(&(list->lock));
    pthread_mutex_destroy(&(list->lock));
    return 0;
}

int pi_list_push(PI_LIST *list, void *payload) {
    int ret = -1;
    PI_LISTE *p_new = NULL;

    if (list == NULL) {
        return -1;
    }
    pthread_mutex_lock(&(list->lock));
    if (list->limit && list->cnt >= list->limit) {
        goto func_end;
    }
    p_new = (PI_LISTE *)MY_MALLOC(sizeof(PI_LISTE));
    if (p_new == NULL) {
        goto func_end;
    }
    memset(p_new, 0x00, sizeof(PI_LISTE));
    p_new->payload = payload;
    if (list->tail != NULL) {
        p_new->prev = list->tail;
        list->tail->next = p_new;
        list->tail = p_new;
    } else {
        list->tail = list->head = p_new;
    }
    list->cnt++;
    ret = 0;
func_end:
    pthread_mutex_unlock(&(list->lock));
    return ret;
}

int pi_list_insert_after(PI_LIST *list, PI_LISTE *prev, void *payload) {
    int ret = -1;
    PI_LISTE *p_new = NULL;

    if (list == NULL) {
        return -1;
    }
    pthread_mutex_lock(&(list->lock));
    if (list->limit && list->cnt >= list->limit) {
        goto func_end;
    }
    p_new = (PI_LISTE *)MY_MALLOC(sizeof(PI_LISTE));
    if (p_new == NULL) {
        goto func_end;
    }
    memset(p_new, 0x00, sizeof(PI_LISTE));
    p_new->payload = payload;
    if (prev == NULL) {
        p_new->next = list->head;
        if (list->head != NULL) {
            list->head->prev = p_new;
        }
        list->head = p_new;
        if (list->tail == NULL) {
            list->tail = p_new;
        }
    } else {
        p_new->next = prev->next;
        p_new->prev = prev;
        if (list->tail == prev) {
            list->tail = p_new;
        }
        if (prev->next != NULL) {
            prev->next->prev = p_new;
        }
        prev->next = p_new;
    }
    list->cnt++;
    ret = 0;
func_end:
    pthread_mutex_unlock(&(list->lock));
    return ret;
}

int pi_list_remove(PI_LIST *list, PI_LISTE *entry) {
    if (list == NULL || entry == NULL) {
        return -1;
    }
    pthread_mutex_lock(&(list->lock));
    if (entry->prev != NULL) {
        entry->prev->next = entry->next;
    } else {
        list->head = entry->next;
    }
    if (entry->next != NULL) {
        entry->next->prev = entry->prev;
    } else {
        list->tail = entry->prev;
    }
    if (list->free_payload != NULL) {
        list->free_payload(entry->payload);
    }
    free(entry);
    list->cnt--;
    pthread_mutex_unlock(&(list->lock));
    return 0;
}

#if 0
int main() {
    PI_LIST list;
    PI_LISTE *entry;
    int i = 0;

    pi_list_init(&list, 0, NULL);
    for (i = 10; i < 25; i++) {
        pi_list_push(&list, (void *)((uintptr_t)i));
    }
    for (i = 100; i < 125; i++) {
        pi_list_insert_after(&list, NULL, (void *)((uintptr_t)i));
    }
    pthread_mutex_lock(&(list.lock));
    for (entry = list.head; entry != NULL; entry = entry->next) {
        printf("%p\n", entry->payload);
    }
    pthread_mutex_unlock(&(list.lock));
    pi_list_destroy(&list);
    return 0;
}
#endif

