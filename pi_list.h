#ifndef __PI_LIST_H__
#define __PI_LIST_H__

#if defined(_WIN32)
#include "w32_pthread.h"
#else
#include <pthread.h>
#endif

typedef struct PI_LISTE {
    void *payload;
    struct PI_LISTE *prev;
    struct PI_LISTE *next;
} PI_LISTE;

typedef struct PI_LIST {
    int inited;
    pthread_mutex_t lock;
    struct PI_LISTE *head;
    struct PI_LISTE *tail;
    size_t cnt;
    size_t limit;
    void (* free_payload)(void *payload);
}PI_LIST;

#ifdef  __cplusplus
extern "C" {
#endif

/* limit: 0 means no limit */
int pi_list_init(PI_LIST *list, size_t limit, void (* free_payload)(void *payload));

int pi_list_destroy(PI_LIST *list);

int pi_list_push(PI_LIST *list, void *payload);

int pi_list_insert_after(PI_LIST *list, PI_LISTE *prev, void *payload);

int pi_list_remove(PI_LIST *list, PI_LISTE *entry);

#ifdef  __cplusplus
}
#endif

#endif
