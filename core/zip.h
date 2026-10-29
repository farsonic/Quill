/*
 * zip.h - just enough zip for .docx and .odt: reading entries (stored or deflated) and writing an
 * archive (deflated). Every length and offset in a file is checked: documents are hostile input.
 */
#ifndef QUILL_ZIP_H
#define QUILL_ZIP_H

#include "util.h"

u32  crc32_update(u32 crc, const u8 *p, size_t n);          /* start with 0 */
/* inflate raw deflate data into out (outcap bytes); returns the bytes written, or -1 if the data
   is damaged or does not fit */
long zinflate(const u8 *in, size_t inlen, u8 *out, size_t outcap);
/* deflate (LZ77, fixed Huffman codes); returns a malloc'd buffer, *outlen its size; NULL if out of memory */
u8  *zdeflate(const u8 *in, size_t inlen, size_t *outlen);

/* an entry from the central directory: method 0 stored or 8 deflated, compressed and real sizes,
   CRC-32, and the offset of its local header */
typedef struct { char name[256]; u32 method, csize, usize, crc, local; } ZipEntry;
/* an archive being read: data is the caller's and must outlive it */
typedef struct { const u8 *data; size_t len; ZipEntry *e; int n; } ZipReader;

int  zip_open(ZipReader *z, const u8 *data, size_t len);     /* 0 if it is not a zip */
void zip_close(ZipReader *z);                                 /* frees the entry list only */
int  zip_find(ZipReader *z, const char *name);                 /* -1 if absent (case-insensitive) */
/* an entry's contents, malloc'd and NUL-terminated (for XML); NULL if damaged or too big */
u8  *zip_read(ZipReader *z, int i, size_t *len);

/* an archive being written: the entries go into out as they are added, their directory records
   into cdir until zip_finish */
typedef struct { Buf out; Buf cdir; int n; u16 dostime, dosdate; } ZipWriter;
void zip_begin(ZipWriter *w);       /* stamps the entries with the current time */
/* add a file (deflated, or stored if that is smaller); 0 if out of memory */
int  zip_add(ZipWriter *w, const char *name, const void *data, size_t len);
/* the finished archive (w->out.data, w->out.len); 0 if out of memory */
int  zip_finish(ZipWriter *w);
void zip_free(ZipWriter *w);        /* frees both buffers (buf_take w->out first to keep the archive) */

#endif
