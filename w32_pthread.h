#ifndef __W32_PTHREAD_H__
#define __W32_PTHREAD_H__

#if _WIN32

#include <windows.h>
#include <process.h>
#include <time.h>
typedef struct pthread_t {
    void *handle;
    void *(*func)(void* arg);
    void *arg;
    void *ret;
} pthread_t;

typedef CRITICAL_SECTION pthread_mutex_t;

static inline DWORD WINAPI win32thread_worker(void *arg) {
    pthread_t *h = arg;
    h->ret = h->func(h->arg);
    return 0;
}

static inline int pthread_create(pthread_t *thread, const void *unused_attr,
                                    void *(*start_routine)(void*), void *arg) {
    thread->func   = start_routine;
    thread->arg    = arg;
#if 0
    thread->handle = (void*)CreateThread(NULL, 0, win32thread_worker, thread,
                                           0, NULL);
#else
    thread->handle = (void*)_beginthreadex(NULL, 0, win32thread_worker, thread,
                                           0, NULL);
#endif
    return !thread->handle;
}

static inline int pthread_join(pthread_t thread, void **value_ptr) {
    DWORD ret = WaitForSingleObject(thread.handle, INFINITE);
    if (ret != WAIT_OBJECT_0) {
        if (ret == WAIT_ABANDONED)
            return EINVAL;
        else
            return EDEADLK;
    }
    if (value_ptr)
        *value_ptr = thread.ret;
    CloseHandle(thread.handle);
    return 0;
}

static inline int pthread_mutex_init(pthread_mutex_t *m, void* attr) {
    InitializeCriticalSection(m);
    return 0;
}

static inline int pthread_mutex_destroy(pthread_mutex_t *m) {
    DeleteCriticalSection(m);
    return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t *m) {
    EnterCriticalSection(m);
    return 0;
}

static inline int pthread_mutex_unlock(pthread_mutex_t *m) {
    LeaveCriticalSection(m);
    return 0;
}

#endif /* _WIN32 */

#endif
