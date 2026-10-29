/*
 * util.h - small helpers shared by Quill's portable core: growable byte buffers and UTF-8.
 * Portable C99; built natively for the tests and with vbcc for the Amiga.
 */
#ifndef QUILL_UTIL_H
#define QUILL_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* exact widths, so the native tests overflow exactly where the Amiga would */
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t i32;
typedef int16_t i16;

/* a growable byte buffer; data is always NUL-terminated (not counted in len) */
typedef struct { char *data; size_t len, cap; int oom; } Buf;

/* buf_init makes an empty buffer (no allocation yet); buf_free releases it and leaves it empty.
   The add functions do nothing once b->oom is set, so check oom once after a run of writes */
void   buf_init(Buf *b);
void   buf_free(Buf *b);
int    buf_reserve(Buf *b, size_t extra);          /* 0 (and b->oom set) when out of memory */
void   buf_add(Buf *b, const void *p, size_t n);
void   buf_addc(Buf *b, int c);
void   buf_adds(Buf *b, const char *s);
void   buf_printf(Buf *b, const char *fmt, ...);
char  *buf_take(Buf *b);                           /* the data, owned by the caller; the buffer is emptied */

/* UTF-8: decode one code point at s (n bytes available); *used gets its length (at least 1).
   Malformed bytes decode as themselves (Latin-1), so nothing is ever lost or rejected */
u32    utf8_get(const char *s, size_t n, int *used);
int    utf8_put(char *out, u32 cp);                /* encode; returns 1-4 bytes */
size_t utf8_next(const char *s, size_t len, size_t off);   /* the next character boundary */
size_t utf8_prev(const char *s, size_t off);               /* the previous one */
/* text in Latin-1 (Amiga fonts) for UTF-8: typographic marks become their ASCII look-alikes,
   anything else outside Latin-1 becomes '?' */
size_t utf8_to_latin1(const char *s, size_t n, char *out, size_t outcap);
int    latin1_of(u32 cp);                          /* one code point; -1 if it has no stand-in */

/* malloc'd copies (NULL when out of memory); the caller frees */
char  *str_dup(const char *s);
char  *str_ndup(const char *s, size_t n);
int    str_ieq(const char *a, const char *b);      /* ASCII case-insensitive equality */

/* angles in tenths of a degree, without floating point: sine and cosine times TRIG_ONE, and the
   angle of a direction (x right, y down: clockwise from 3 o'clock), 0-3599 */
#define TRIG_ONE 16384
i32    isin(int tenths);
i32    icos(int tenths);
int    iatan2(i32 y, i32 x);

#endif
