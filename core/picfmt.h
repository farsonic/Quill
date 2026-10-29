/*
 * picfmt.h - more picture files for WP17: reading GIF (the first frame), BMP and IFF ILBM (with
 * EHB, HAM6/HAM8, deep and PBM) as RGBA, and writing PNG and baseline JPEG, which .docx files hold
 * (an ILBM is saved as a PNG, a picture with effects drawn in or compressed as either). From
 * Easel, by the same author (see picfmt.c).
 */
#ifndef QUILL_PICFMT_H
#define QUILL_PICFMT_H

#include "util.h"
#include "image.h"

/* a flat picture: w * h * 4 bytes of straight RGBA, rows top down, malloc'd */
typedef struct { i32 w, h; u8 *px; } Rgba;
int  pic_rgba_alloc(Rgba *im, i32 w, i32 h);          /* transparent black; 0 if too big or out of memory */
void pic_rgba_free(Rgba *im);
/* GIF, BMP or ILBM (IMG_, image.h) and its size from the header; IMG_NONE if none of them */
int  picfmt_info(const u8 *d, u32 n, u32 *w, u32 *h);
/* one of them decoded as RGBA (1), or 0 */
int  picfmt_rgba(const u8 *d, u32 n, Rgba *out);
/* 8-bit RGB (RGBA if anything isn't opaque), a pHYs chunk when dpi > 0; malloc'd, NULL if out of memory */
u8  *pic_png_encode(const Rgba *im, int dpi, size_t *len);
/* baseline JPEG, quality 1-100, transparency laid over white; malloc'd, NULL if out of memory */
u8  *pic_jpeg_encode(const Rgba *im, int quality, int dpi, size_t *len);

#endif
