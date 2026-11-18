/* mac/include/stdlib.h - memory, numbers and sorting for Quill on the classic Mac OS (mac/libc.c). */
#ifndef QM_STDLIB_H
#define QM_STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 0x7fff
void *malloc(size_t n);
void *calloc(size_t n, size_t size);
void *realloc(void *p, size_t n);
void  free(void *p);
int   atoi(const char *s);
long  atol(const char *s);
double atof(const char *s);
long  strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
long long strtoll(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);
double strtod(const char *s, char **end);
void  qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
int   abs(int x);
long  labs(long x);
int   rand(void);
void  srand(unsigned seed);
char *getenv(const char *name);
void  exit(int code);
void  abort(void);
#endif
