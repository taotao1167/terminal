#ifndef __W32_SEMAPHORE_H__
#define __W32_SEMAPHORE_H__

#if _WIN32
#include <windows.h>
#include <time.h>
typedef HANDLE sem_t;

static inline int sem_init(sem_t *sem, int pshared, unsigned long value) {
    *sem = CreateSemaphore(NULL, value, 0x7fffffff, NULL);
    return 0;
}

static inline int sem_destroy(sem_t *sem) {
    CloseHandle(*sem);
    return 0;
}

static inline int sem_post(sem_t *sem) {
    if (ReleaseSemaphore(*sem, 1, NULL) == 0) {
        return -1;
    }
    return 0;
}

static inline int sem_wait(sem_t *sem) {
    WaitForSingleObject(*sem, INFINITE);
    return 0;
}

static inline int sem_trywait(sem_t *sem) {
    if(WaitForSingleObject(*sem, 0) != WAIT_OBJECT_0) {
        return -1;
    }
    return 0;
}

static inline int sem_timedwait(sem_t *sem, const struct timespec *tmout) {
    if(WaitForSingleObject(*sem, (tmout->tv_sec * 1000) + (tmout->tv_nsec / 1000000)) != WAIT_OBJECT_0) {
        return -1;
    }
    return 0;
}

#endif /* _WIN32 */

#endif
