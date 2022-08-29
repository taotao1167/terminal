#ifndef __TT_MALLOC_DEBUG_H__
#define __TT_MALLOC_DEBUG_H__

#include <time.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADD_POISON 1
#define MAX_TRACE 16

typedef struct ST_RAM_RECORD{
    int64_t c_time;
    int64_t m_time;
	void *ptr;
	size_t size;
	void *trace[MAX_TRACE];
	int trace_cnt;
}RAM_RECORD;

#ifdef USE_AS_LIBRARY
#define LIB_INIT __attribute__((constructor))
#define LIB_DESTROY __attribute__((destructor))
#else
#define LIB_INIT
#define LIB_DESTROY
LIB_INIT  int leak_check_init();
LIB_DESTROY void leak_check_destroy();
#endif

#ifdef __cplusplus
}
#endif

#endif
