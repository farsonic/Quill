/* mac/include/time.h - the time of day for Quill on the classic Mac OS (mac/libc.c: the Mac's clock,
   seconds since 1904, as seconds since 1970 in local time; clock() counts ticks). */
#ifndef QM_TIME_H
#define QM_TIME_H
#include <stddef.h>
typedef long time_t;
typedef long clock_t;
#define CLOCKS_PER_SEC 60
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };
time_t time(time_t *t);
clock_t clock(void);
struct tm *localtime(const time_t *t);
struct tm *gmtime(const time_t *t);
time_t mktime(struct tm *tm);
size_t strftime(char *s, size_t n, const char *fmt, const struct tm *tm);
#endif
