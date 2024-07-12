#ifndef __TT_BUFFER_H__
#define __TT_BUFFER_H__

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TTBuffer {
	unsigned char *content; /* point to content */
	size_t used; /* size of used space */
	size_t space; /* size of malloc space */
	int is_malloced; /* mark content is malloced or not */
} TTBuffer;

extern int tt_buffer_init(TTBuffer *buffer);
extern int tt_buffer_free(TTBuffer *buffer);
extern int tt_buffer_empty(TTBuffer *buffer);
extern int tt_buffer_swapto_malloced(TTBuffer *buffer, size_t content_len);
extern int tt_buffer_vprintf(TTBuffer *buffer, const char *format, va_list args);
#if defined(_WIN32)
extern int tt_buffer_printf(TTBuffer *buffer, const char *format, ...);
#else
extern int tt_buffer_printf(TTBuffer *buffer, const char *format, ...) __attribute__((format(printf, 2, 3)));
#endif
extern int tt_buffer_write(TTBuffer *buffer, const void *content, size_t content_len);
extern int tt_buffer_no_copy(TTBuffer *buffer, void *content, size_t used, size_t space, int is_malloced);

#ifdef __cplusplus
}
#endif

#endif
