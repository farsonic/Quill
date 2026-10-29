/*
 * image.h - pictures: PNG and JPEG, decoded to RGB at the size they are shown.
 *
 * Decoding scales as it goes (averaging the source pixels that fall in each shown one), so a large
 * photograph never needs its full size in memory as RGB: a JPEG is decoded a strip of blocks at a
 * time. PNG is inflated whole first (zip.c's inflate is one-shot). Supported: PNG of every colour
 * type and depth, interlaced or not (transparency is laid over white); JPEG, baseline or progressive
 * (greyscale, YCbCr, RGB, CMYK or YCCK, any sampling). A baseline JPEG with every component in one
 * scan is decoded a strip at a time; the others need their coefficients whole (2 bytes each).
 */
#ifndef QUILL_IMAGE_H
#define QUILL_IMAGE_H

#include "util.h"

enum { IMG_NONE, IMG_PNG, IMG_JPEG };

/* the kind and natural size (pixels) of a picture file; IMG_NONE if it is neither */
int img_info(const u8 *d, u32 n, u32 *w, u32 *h);
/* JPEG only: components (1 grey, 3 YCbCr, 4 CMYK) and whether img_decode can decode it */
int img_jpeg_components(const u8 *d, u32 n, int *decodable);
/* the picture as RGB, ow x oh pixels (3 bytes each, rows top down), malloc'd; NULL if it can't */
u8 *img_decode(const u8 *d, u32 n, u32 ow, u32 oh);
/* the outline of what a picture shows (its background: the colour of its corners, or white where
   transparent), for text to wrap round: points (x, y pairs, 0-21600 across the picture) round it,
   malloc'd into *out; how many, 0 if none (all background, or it can't be decoded) */
int img_outline(const u8 *d, u32 n, i32 **out);

#endif
