/*
 * ttraster.h - TrueType glyphs as pixels, for fonts the system has no engine for.
 *
 * Reads a font's outlines - TrueType's (glyf and loca: simple and composite glyphs; a big font's
 * each from the file as it is wanted, so it costs little memory) or OpenType's CFF ones (Type 2
 * charstrings, with their subroutines; CID-keyed fonts too; CFF2's variable ones at their default
 * design) - scales them to a size in
 * pixels and fills them, four by four samples a pixel, into coverage 0-255.
 * Hinting (ttr_hint) is Quill's own, from the outlines, for any font: the font's alignment zones
 * (baseline, x-height, capitals, ascenders, descenders) and its straight stems are moved onto
 * whole pixels, with the rest of the outline following between them.
 * Integer arithmetic only (26.6 fixed point), as the 68k has no FPU to count on.
 */
#ifndef QUILL_TTRASTER_H
#define QUILL_TTRASTER_H

#include "util.h"

typedef struct TTRaster {
  void *fp;                           /* the file, open while the raster is */
  u32 glyfoff, lglyf;                 /* where its glyf table is, how long */
  u8 *glyf;                           /* the table itself, if not too big to keep (else read as wanted) */
  u8 *loca;
  u32 lloca;
  u8 *gbuf[10]; u32 gcap[10];         /* (a glyph's bytes, a buffer for each level of a composite) */
  int longloca;
  u16 upm, nglyphs;
  i32 *xs, *ys;                       /* (work: the points of a glyph) */
  u8 *on;
  u32 npts, cappts;
  void *lines; u32 caplines;          /* (work, kept from glyph to glyph: the outline's lines, */
  u32 *ends; u32 capends;             /*  the contours' ends, */
  i32 *cross; u32 capcross;           /*  the crossings and the lines crossing a sample row) */
  /* CFF outlines: the table, and where its parts are in it */
  u8 *cff; u32 lcff;
  u32 cs_off, gsub_off, lsub_off;     /* the CharStrings, global and (one font) local Subrs INDEXes */
  u32 fdsel_off;                      /* CID-keyed: FDSelect, and each font DICT's local Subrs */
  u32 fd_lsub[64]; int nfd;
  int cff2;                           /* CFF2 (variable fonts): drawn at their default design */
  i32 fd_vsindex[64];                 /* each font DICT's variation data, */
  int vregions[16], nvdata;           /* and how many regions (deltas a blend has) each has */
  /* hinting: on, and the alignment zones (each its flat edge and its overshoot, font units, y up) */
  int hint;
  i32 zf[6], zo[6]; int nz;
} TTRaster;

typedef struct TTBitmap {
  int w, h;                           /* 0 x 0 for a glyph with nothing to draw (a space) */
  int left, top;                      /* column 0 is left pixels right of the pen; row 0 is top above the baseline */
  u8 *cov;                            /* w * h coverage, 0 (none) to 255 (all), row by row; free() it */
} TTBitmap;

/* a font file for drawing (index: the face in a collection, else 0); 0 if it can't be used.
   The file stays open until ttr_close, which frees everything and is safe on a zeroed raster */
int  ttr_open(TTRaster *r, const char *path, int index);
void ttr_close(TTRaster *r);
/* hinting on, the zones measured from the font's letters (latin: glyph ids of Latin-1) */
void ttr_hint(TTRaster *r, const u16 *latin);
/* glyph g at ppem pixels to the em (26.6 fixed point: 64 is one pixel); 0 on a damaged glyph */
int  ttr_glyph(TTRaster *r, u16 g, i32 ppem64, TTBitmap *bm);
/* for one bit a pixel (set at coverage >= thr): a stroke thinner than a pixel, that would fall
   between pixels and vanish, keeps its strongest pixel (across and down) */
void ttr_dropout(TTBitmap *bm, int thr);

#endif
