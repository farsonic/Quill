/*
 * cfb.h - OLE compound files ([MS-CFB]: "structured storage", the container of Word 97-2003 .doc files,
 * of encrypted Office 2007+ documents, of embedded OLE objects), read and written. WP7 made it for
 * encrypted .docx files; it is written to serve a .doc reader (WP28d) as well: every stream and storage
 * is listed with its parent, and any stream can be read by index or path.
 *
 * Reading checks every sector number, chain and size (files are hostile input): a damaged chain gives
 * NULL, never a read out of bounds. Version 3 (512-byte sectors) and 4 (4096) files are read; files
 * are written as version 3.
 */
#ifndef QUILL_CFB_H
#define QUILL_CFB_H

#include "util.h"

enum { CFB_EMPTY = 0, CFB_STORAGE = 1, CFB_STREAM = 2, CFB_ROOT = 5 };
#define CFB_NONE 0xFFFFFFFFUL

typedef struct {
  char name[96];            /* UTF-8 (control characters such as "\x06" and "\x05" kept as they are) */
  u8 type;                  /* CFB_ */
  u32 left, right, child;   /* the red-black tree of siblings and the first child: entry numbers, CFB_NONE none */
  u32 start;                /* first sector (mini sector for small streams) */
  u32 size;                 /* bytes (streams over 4 GB are refused) */
  int parent;               /* the storage it is in (index into Cfb.e; -1 for the root) */
  u8 clsid[16];
} CfbEntry;

typedef struct {
  const u8 *data; size_t len;   /* the file (the caller's; must outlive the reader) */
  u32 ssize;                /* sector size: 512 or 4096 */
  u32 cutoff;               /* streams smaller than this are in the mini stream (4096) */
  u32 *fat; u32 nfat;       /* the sector chains */
  u32 *mfat; u32 nmfat;     /* the mini sector chains */
  u8 *mini; u32 nmini;      /* the mini stream (read whole) */
  CfbEntry *e; int n;       /* the directory */
} Cfb;

/* 1 if data looks like a compound file (its signature) */
int  cfb_is(const u8 *data, size_t len);
/* 0 if it isn't one or is damaged */
int  cfb_open(Cfb *c, const u8 *data, size_t len);
void cfb_close(Cfb *c);
/* an entry by path from the root: names separated by '/', compared ignoring case ("\x06DataSpaces/Version");
   its index, -1 if absent */
int  cfb_find(const Cfb *c, const char *path);
/* a child of storage parent (index; 0 the root) by name, -1 if none */
int  cfb_child(const Cfb *c, int parent, const char *name);
/* a stream's bytes (malloc'd, NUL after the end for convenience), *len its size; NULL if damaged or out of memory */
u8  *cfb_read(const Cfb *c, int i, size_t *len);
/* the same by path */
u8  *cfb_read_path(const Cfb *c, const char *path, size_t *len);

/* ---- writing: streams added by path (the storages on the way are made), then the file built */
typedef struct CfbWriter CfbWriter;
CfbWriter *cfb_writer(void);                     /* NULL if out of memory */
/* a stream (copied); 0 if out of memory or the path is bad */
int  cfb_add(CfbWriter *w, const char *path, const void *data, size_t len);
/* the compound file (malloc'd), *len its size; NULL if out of memory. The writer is freed either way */
u8  *cfb_finish(CfbWriter *w, size_t *len);
void cfb_writer_free(CfbWriter *w);              /* (without finishing) */

#endif
