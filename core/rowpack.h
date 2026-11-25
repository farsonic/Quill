/*
 * core/rowpack.h - rows of RGB pixels (3 bytes each, as pdf_page_rgb gives a page) run-length packed,
 * each row on its own so that one can be unpacked or compared alone. Octavo, the classic Mac edition,
 * keeps the pages it has drawn this way (mac/qm_view.c): a page of text is mostly runs of white and
 * packs to a tenth of its size or less, so a few pages fit a small memory partition.
 *
 * The packing: for each row, a byte n, then n + 1 pixels as they are (n < 128), or one pixel that
 * repeats n - 126 times (n >= 128: 2 to 129 times). The same pixels always pack the same way, so two
 * rows can be compared packed.
 */
#ifndef QUILL_ROWPACK_H
#define QUILL_ROWPACK_H
#include <stddef.h>
#include "util.h"

typedef struct {
  u8 *data;                 /* the packed rows (NULL: none) */
  u32 *rowat;               /* row y's start in data; rowat[h] the end of the last */
  u32 w, h;
} PackedRGB;

int    rgb_pack(PackedRGB *p, const u8 *rgb, u32 w, u32 h);      /* 0 if out of memory (p then empty) */
void   rgb_unpack_row(const PackedRGB *p, u32 y, u8 *out);       /* w * 3 bytes */
int    rgb_same_row(const PackedRGB *a, const PackedRGB *b, u32 y);   /* (pages of one size) */
size_t rgb_pack_size(const PackedRGB *p);                        /* bytes held */
void   rgb_pack_free(PackedRGB *p);
#endif
