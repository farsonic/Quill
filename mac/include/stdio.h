/* mac/include/stdio.h - the C library's files and formatted printing, as Quill uses them on the
   classic Mac OS (mac/libc.c). Files are the File Manager's, named by Mac paths ("Disk:Folder:file");
   stdout and stderr go to the debug log. GPL-2.0-only, as Quill. */
#ifndef QM_STDIO_H
#define QM_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct qm_file FILE;
typedef long fpos_t;
#define EOF      (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define BUFSIZ   1024
extern FILE *stdin, *stdout, *stderr;
FILE  *fopen(const char *path, const char *mode);
int    fclose(FILE *f);
size_t fread(void *p, size_t size, size_t n, FILE *f);
size_t fwrite(const void *p, size_t size, size_t n, FILE *f);
int    fseek(FILE *f, long off, int whence);
long   ftell(FILE *f);
int    fgetc(FILE *f);
int    getc(FILE *f);
int    fputc(int c, FILE *f);
int    putc(int c, FILE *f);
char  *fgets(char *s, int n, FILE *f);
int    fputs(const char *s, FILE *f);
int    puts(const char *s);
int    putchar(int c);
int    fflush(FILE *f);
int    feof(FILE *f);
int    ferror(FILE *f);
void   rewind(FILE *f);
int    remove(const char *path);
int    rename(const char *from, const char *to);
int    printf(const char *fmt, ...);
int    fprintf(FILE *f, const char *fmt, ...);
int    vfprintf(FILE *f, const char *fmt, va_list ap);
int    sprintf(char *s, const char *fmt, ...);
int    snprintf(char *s, size_t n, const char *fmt, ...);
int    vsnprintf(char *s, size_t n, const char *fmt, va_list ap);
int    vsprintf(char *s, const char *fmt, va_list ap);
int    sscanf(const char *s, const char *fmt, ...);
#endif
