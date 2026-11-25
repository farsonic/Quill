/*
 * mac/libc.c - the C library Quill's core needs, on the classic Mac OS. GPL-2.0-only, as Quill.
 *
 * Quill's core (core/) is portable C99 and asks little of the C library: memory, a few string
 * and number functions, qsort, formatted printing into buffers, files read and written whole,
 * and the time. The Mac has none of it, so this file provides it on the Toolbox, as plainly as it
 * can. Riptide's app kit, which Quill's Mac build links (see Makefile.mac), already gives the basic
 * string functions (src/amiga/libc.c) and the printf engine (src/core/printf.c: rt_vsnprintf).
 *
 * Memory. The core makes many small allocations (runs, boxes, strings) and frees most of them
 * soon after. One Memory Manager pointer each would be slow and would fragment the application
 * heap, so small blocks (up to 1 KB) come from free lists of 16 size classes, carved out of 64 KB
 * chunks; bigger ones are Memory Manager pointers of their own. Each block carries its size class
 * in the four bytes before it, so free() and realloc() know what they hold.
 *
 * Files. FILE is a File Manager access path with a 512-byte buffer for reading and writing a
 * character at a time (fgets on fonts.list, for instance). Paths are the Mac's: "Disk:Folder:file",
 * or ":Fonts:fonts.list" from the folder Quill is in.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include "qm_mac.h"

int errno;

/* ---- memory -------------------------------------------------------------------------------- */
#define CHUNK      65536u                   /* bytes asked of the Memory Manager at a time */
#define SMALL_MAX  1024u                    /* bigger blocks are pointers of their own */
#define NCLASS     16
static const unsigned short class_size[NCLASS] = {
	16, 24, 32, 48, 64, 96, 128, 160, 192, 256, 320, 384, 512, 640, 768, 1024
};
#define BIG        0xFFFFu                  /* the class word of a block of its own */
typedef struct freeblk { struct freeblk *next; } freeblk;
static freeblk *free_list[NCLASS];
static char *chunk_at, *chunk_end;          /* what is left of the chunk being carved */
static unsigned long st_chunks, st_big, st_bigpeak;   /* (mac/qm_measure.c) bytes in chunks, in big blocks now, the most */
void libc_stats(unsigned long *chunks, unsigned long *big, unsigned long *bigpeak)
{
	*chunks = st_chunks;
	*big = st_big;
	*bigpeak = st_bigpeak;
}

/* each block: [u16 class][u16 0][the caller's bytes], 4-aligned; a big one also [u32 size] before */
static int class_of(size_t n)
{
	int c = 0;
	while (c < NCLASS && class_size[c] < n)
		c++;
	return c;
}

void *malloc(size_t n)
{
	if (!n)
		n = 1;
	if (n <= SMALL_MAX) {
		int c = class_of(n);
		freeblk *b = free_list[c];
		unsigned need = 4u + class_size[c];
		if (b) {
			free_list[c] = b->next;
			return b;
		}
		if (chunk_at + need > chunk_end) {
			char *p = qm_newptr(CHUNK);
			if (!p)
				return NULL;
			chunk_at = p;
			chunk_end = p + CHUNK;
			st_chunks += CHUNK;
		}
		*(unsigned short *)chunk_at = (unsigned short)c;
		chunk_at += need;
		return chunk_at - class_size[c];
	} else {
		char *p = qm_newptr((long)n + 8);
		if (!p)
			return NULL;
		*(unsigned int *)p = (unsigned int)n;      /* 32 bits, before the class word */
		st_big += n + 8;
		if (st_big > st_bigpeak)
			st_bigpeak = st_big;
		*(unsigned short *)(p + 4) = BIG;
		return p + 8;
	}
}

static size_t block_size(void *p)
{
	unsigned short c = *((unsigned short *)p - 2);
	return c == BIG ? *(unsigned int *)((char *)p - 8) : class_size[c];
}

void free(void *p)
{
	unsigned short c;
	if (!p)
		return;
	c = *((unsigned short *)p - 2);
	if (c == BIG) {
		st_big -= *(unsigned int *)((char *)p - 8) + 8;
		qm_disposeptr((char *)p - 8);
		return;
	}
	((freeblk *)p)->next = free_list[c];
	free_list[c] = p;
}

void *calloc(size_t n, size_t size)
{
	size_t t = n * size;
	void *p;
	if (size && t / size != n)
		return NULL;
	p = malloc(t);
	if (p)
		memset(p, 0, t);
	return p;
}

void *realloc(void *p, size_t n)
{
	void *q;
	size_t have;
	if (!p)
		return malloc(n);
	if (!n) {
		free(p);
		return NULL;
	}
	have = block_size(p);
	if (n <= have && (have <= SMALL_MAX || n > have / 2))
		return p;                       /* it fits, and isn't wasting much */
	q = malloc(n);
	if (!q)
		return NULL;
	memcpy(q, p, have < n ? have : n);
	free(p);
	return q;
}

/* ---- strings (those Riptide's libc does not have) ------------------------------------------ */
void *memchr(const void *s, int c, size_t n)
{
	const unsigned char *p = s;
	for (; n; n--, p++)
		if (*p == (unsigned char)c)
			return (void *)p;
	return NULL;
}
size_t strnlen(const char *s, size_t n)
{
	size_t i = 0;
	while (i < n && s[i])
		i++;
	return i;
}
char *strncat(char *d, const char *s, size_t n)
{
	char *e = d + strlen(d);
	while (n-- && *s)
		*e++ = *s++;
	*e = 0;
	return d;
}
char *strstr(const char *h, const char *n)
{
	size_t k = strlen(n);
	if (!k)
		return (char *)h;
	for (; *h; h++)
		if (*h == *n && !strncmp(h, n, k))
			return (char *)h;
	return NULL;
}
size_t strspn(const char *s, const char *a)
{
	size_t i = 0;
	while (s[i] && strchr(a, s[i]))
		i++;
	return i;
}
size_t strcspn(const char *s, const char *r)
{
	size_t i = 0;
	while (s[i] && !strchr(r, s[i]))
		i++;
	return i;
}
char *strpbrk(const char *s, const char *a)
{
	s += strcspn(s, a);
	return *s ? (char *)s : NULL;
}
char *strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *d = malloc(n);
	if (d)
		memcpy(d, s, n);
	return d;
}
char *strndup(const char *s, size_t n)
{
	size_t k = strnlen(s, n);
	char *d = malloc(k + 1);
	if (d) {
		memcpy(d, s, k);
		d[k] = 0;
	}
	return d;
}
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int strcasecmp(const char *a, const char *b)
{
	while (*a && lower(*a) == lower(*b))
		a++, b++;
	return lower((unsigned char)*a) - lower((unsigned char)*b);
}
int strncasecmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) {
		int d = lower((unsigned char)*a) - lower((unsigned char)*b);
		if (d || !*a)
			return d;
	}
	return 0;
}
char *strerror(int e) { return e == ENOMEM ? "out of memory" : e == ENOENT ? "no such file" : "error"; }

/* ---- numbers ------------------------------------------------------------------------------- */
static int digit(int c, int base)
{
	int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : 99;
	return v < base ? v : -1;
}
unsigned long long strtoull(const char *s, char **end, int base)
{
	const char *p = s;
	unsigned long long v = 0;
	int neg = 0, any = 0, d;
	while (*p == ' ' || (*p >= 9 && *p <= 13))
		p++;
	if (*p == '+' || *p == '-')
		neg = *p++ == '-';
	if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && digit(p[2], 16) >= 0)
		p += 2, base = 16;
	else if (base == 0)
		base = *p == '0' ? 8 : 10;
	while ((d = digit(*p, base)) >= 0)
		v = v * (unsigned)base + (unsigned)d, p++, any = 1;
	if (end)
		*end = (char *)(any ? p : s);
	return neg ? -v : v;
}
long long strtoll(const char *s, char **end, int base) { return (long long)strtoull(s, end, base); }
unsigned long strtoul(const char *s, char **end, int base) { return (unsigned long)strtoull(s, end, base); }
long strtol(const char *s, char **end, int base) { return (long)strtoull(s, end, base); }
int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }
int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
static unsigned long seed = 1;
int rand(void) { seed = seed * 1103515245u + 12345u; return (int)((seed >> 16) & RAND_MAX); }
void srand(unsigned s) { seed = s; }
char *getenv(const char *name) { return NULL; }
void exit(int code) { qm_exit(); }
void abort(void) { qm_exit(); }

/* qsort: a plain heap sort, no recursion (the 68k stack is small) and n log n in the worst case */
static void swap_bytes(char *a, char *b, size_t n)
{
	while (n--) {
		char t = *a;
		*a++ = *b;
		*b++ = t;
	}
}
static void sift(char *base, size_t root, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
	for (;;) {
		size_t child = root * 2 + 1;
		if (child >= n)
			return;
		if (child + 1 < n && cmp(base + child * size, base + (child + 1) * size) < 0)
			child++;
		if (cmp(base + root * size, base + child * size) >= 0)
			return;
		swap_bytes(base + root * size, base + child * size, size);
		root = child;
	}
}
void qsort(void *b, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
	char *base = b;
	size_t i;
	if (n < 2)
		return;
	for (i = n / 2; i-- > 0;)
		sift(base, i, n, size, cmp);
	for (i = n - 1; i > 0; i--) {
		swap_bytes(base, base + i * size, size);
		sift(base, 0, i, size, cmp);
	}
}
void *bsearch(const void *key, const void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
	size_t lo = 0, hi = n;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		int c = cmp(key, (const char *)base + mid * size);
		if (!c)
			return (char *)base + mid * size;
		if (c < 0)
			hi = mid;
		else
			lo = mid + 1;
	}
	return NULL;
}

/* ---- formatted text ------------------------------------------------------------------------ */
int rt_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int vsnprintf(char *s, size_t n, const char *fmt, va_list ap) { return rt_vsnprintf(s, n, fmt, ap); }
int vsprintf(char *s, const char *fmt, va_list ap) { return rt_vsnprintf(s, 0x7FFFFFFF, fmt, ap); }
int snprintf(char *s, size_t n, const char *fmt, ...)
{
	va_list ap;
	int r;
	va_start(ap, fmt);
	r = rt_vsnprintf(s, n, fmt, ap);
	va_end(ap);
	return r;
}
int sprintf(char *s, const char *fmt, ...)
{
	va_list ap;
	int r;
	va_start(ap, fmt);
	r = rt_vsnprintf(s, 0x7FFFFFFF, fmt, ap);
	va_end(ap);
	return r;
}

/* sscanf: what the core asks of it: %d and %x, with literal characters between */
int sscanf(const char *s, const char *fmt, ...)
{
	va_list ap;
	int n = 0;
	va_start(ap, fmt);
	while (*fmt) {
		if (*fmt == '%' && (fmt[1] == 'd' || fmt[1] == 'x')) {
			char *e;
			long v = strtol(s, &e, fmt[1] == 'x' ? 16 : 10);
			if (e == s)
				break;
			*va_arg(ap, int *) = (int)v;
			n++;
			s = e;
			fmt += 2;
		} else if (*fmt == ' ') {
			while (*s == ' ')
				s++;
			fmt++;
		} else if (*fmt == *s) {
			fmt++, s++;
		} else {
			break;
		}
	}
	va_end(ap);
	return n;
}

/* ---- files --------------------------------------------------------------------------------- */
struct qm_file {
	short ref;                 /* the File Manager's access path; -1 for stdout and stderr (the log) */
	char writing, eof, err;
	long pos;                  /* the file's mark at buf[0] */
	unsigned short have, at;   /* bytes in buf, the next one */
	unsigned char buf[512];
};
static struct qm_file std_log = { -1 };
FILE *stdin = NULL, *stdout = &std_log, *stderr = &std_log;

static int flush_out(FILE *f)
{
	if (f->ref < 0 || !f->writing || !f->at)
		return 0;
	if (qm_fwrite(f->ref, f->buf, f->at) != f->at) {
		f->err = 1;
		return EOF;
	}
	f->pos += f->at;
	f->at = 0;
	return 0;
}

FILE *fopen(const char *path, const char *mode)
{
	int write = mode[0] == 'w' || mode[0] == 'a';
	FILE *f;
	short ref;
	if (qm_fopen(path, write, mode[0] == 'a', &ref))
		return NULL;
	f = calloc(1, sizeof *f);
	if (!f) {
		qm_fclose(ref);
		return NULL;
	}
	f->ref = ref;
	f->writing = (char)write;
	f->pos = qm_ftell(ref);
	return f;
}

int fclose(FILE *f)
{
	int r;
	if (!f || f->ref < 0)
		return 0;
	r = flush_out(f);
	qm_fclose(f->ref);
	free(f);
	return r;
}

size_t fread(void *p, size_t size, size_t n, FILE *f)
{
	unsigned char *d = p;
	size_t want = size * n, got = 0;
	if (!f || f->ref < 0 || !want)
		return 0;
	/* first what is buffered, then straight from the file */
	while (got < want && f->at < f->have)
		d[got++] = f->buf[f->at++];
	if (got < want) {
		long k;
		f->pos += f->have;
		f->have = f->at = 0;
		k = qm_fread(f->ref, d + got, (long)(want - got));
		if (k > 0) {
			got += (size_t)k;
			f->pos += k;
		}
		if (got < want)
			f->eof = 1;
	}
	return size ? got / size : 0;
}

size_t fwrite(const void *p, size_t size, size_t n, FILE *f)
{
	size_t want = size * n;
	if (!f || !want)
		return 0;
	if (f->ref < 0) {
		qm_log_bytes(p, (long)want);
		return n;
	}
	if (want > sizeof f->buf - f->at) {
		long k;
		if (flush_out(f))
			return 0;
		k = qm_fwrite(f->ref, p, (long)want);
		if (k > 0)
			f->pos += k;
		return size ? (size_t)(k > 0 ? k : 0) / size : 0;
	}
	memcpy(f->buf + f->at, p, want);
	f->at += (unsigned short)want;
	return n;
}

int fgetc(FILE *f)
{
	if (!f || f->ref < 0)
		return EOF;
	if (f->at >= f->have) {
		long k;
		f->pos += f->have;
		f->at = f->have = 0;
		k = qm_fread(f->ref, f->buf, sizeof f->buf);
		if (k <= 0) {
			f->eof = 1;
			return EOF;
		}
		f->have = (unsigned short)k;
	}
	return f->buf[f->at++];
}
int getc(FILE *f) { return fgetc(f); }
char *fgets(char *s, int n, FILE *f)
{
	int i = 0, c = 0;
	while (i < n - 1 && (c = fgetc(f)) != EOF) {
		s[i++] = (char)c;
		if (c == '\n' || c == '\r')
			break;
	}
	s[i] = 0;
	return i ? s : NULL;
}
int fputc(int c, FILE *f)
{
	unsigned char b = (unsigned char)c;
	return fwrite(&b, 1, 1, f) == 1 ? c : EOF;
}
int putc(int c, FILE *f) { return fputc(c, f); }
int fputs(const char *s, FILE *f) { return fwrite(s, 1, strlen(s), f) ? 0 : EOF; }
int puts(const char *s) { fputs(s, stdout); return fputc('\n', stdout); }
int putchar(int c) { return fputc(c, stdout); }
int fflush(FILE *f) { return f ? flush_out(f) : 0; }
int feof(FILE *f) { return f && f->eof; }
int ferror(FILE *f) { return f && f->err; }
long ftell(FILE *f)
{
	if (!f || f->ref < 0)
		return -1;
	return f->pos + f->at;
}
int fseek(FILE *f, long off, int whence)
{
	long to;
	if (!f || f->ref < 0 || flush_out(f))
		return -1;
	to = whence == SEEK_SET ? off : whence == SEEK_CUR ? f->pos + f->at + off : qm_feof_pos(f->ref) + off;
	if (qm_fseek(f->ref, to))
		return -1;
	f->pos = to;
	f->have = f->at = 0;
	f->eof = 0;
	return 0;
}
void rewind(FILE *f) { fseek(f, 0, SEEK_SET); }
int remove(const char *path) { return qm_fdelete(path); }
int rename(const char *from, const char *to) { return qm_frename(from, to); }

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
	char tmp[512];
	int n = rt_vsnprintf(tmp, sizeof tmp, fmt, ap);
	fwrite(tmp, 1, strlen(tmp), f);
	return n;
}
int fprintf(FILE *f, const char *fmt, ...)
{
	va_list ap;
	int r;
	va_start(ap, fmt);
	r = vfprintf(f, fmt, ap);
	va_end(ap);
	return r;
}
int printf(const char *fmt, ...)
{
	va_list ap;
	int r;
	va_start(ap, fmt);
	r = vfprintf(stdout, fmt, ap);
	va_end(ap);
	return r;
}

/* ---- time ---------------------------------------------------------------------------------- */
/* The Mac's clock counts local seconds from 1 January 1904; time_t counts from 1970. */
#define MAC_TO_UNIX 2082844800UL
time_t time(time_t *t)
{
	time_t v = (time_t)(qm_seconds() - MAC_TO_UNIX);
	if (t)
		*t = v;
	return v;
}
clock_t clock(void) { return (clock_t)qm_ticks(); }

/* days from 1970-01-01 to a civil date and back (Howard Hinnant's algorithms) */
static long days_from_civil(long y, int m, int d)
{
	long era, yoe, doy, doe;
	y -= m <= 2;
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = y - era * 400;
	doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}
static struct tm tm_buf;
struct tm *gmtime(const time_t *t)
{
	long s = *t, days = s / 86400, rem = s % 86400, z, era, doe, yoe, y, doy, mp, d, m;
	if (rem < 0)
		rem += 86400, days--;
	tm_buf.tm_hour = (int)(rem / 3600);
	tm_buf.tm_min = (int)(rem % 3600 / 60);
	tm_buf.tm_sec = (int)(rem % 60);
	tm_buf.tm_wday = (int)((days + 4) % 7 + 7) % 7;             /* 1970-01-01 was a Thursday */
	z = days + 719468;
	era = (z >= 0 ? z : z - 146096) / 146097;
	doe = z - era * 146097;
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	y = yoe + era * 400;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	d = doy - (153 * mp + 2) / 5 + 1;
	m = mp + (mp < 10 ? 3 : -9);
	y += m <= 2;
	tm_buf.tm_year = (int)(y - 1900);
	tm_buf.tm_mon = (int)(m - 1);
	tm_buf.tm_mday = (int)d;
	tm_buf.tm_yday = (int)(days - days_from_civil(y, 1, 1));
	tm_buf.tm_isdst = 0;
	return &tm_buf;
}
struct tm *localtime(const time_t *t) { return gmtime(t); }      /* the Mac's clock is local time already */
time_t mktime(struct tm *tm)
{
	return (time_t)(days_from_civil(tm->tm_year + 1900L, tm->tm_mon + 1, tm->tm_mday) * 86400L +
	                tm->tm_hour * 3600L + tm->tm_min * 60L + tm->tm_sec);
}
size_t strftime(char *s, size_t n, const char *fmt, const struct tm *tm)
{
	return (size_t)snprintf(s, n, "%04d-%02d-%02d %02d:%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
	                        tm->tm_hour, tm->tm_min);
}
