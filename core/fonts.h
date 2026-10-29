/*
 * fonts.h - font metrics for layout, read from TrueType files.
 *
 * Layout never asks the screen: widths come from the fonts' own advance widths, so a document
 * breaks its lines the same on any Amiga, at any zoom, and as in Word when the metric-compatible
 * fonts are used (Liberation for Times New Roman, Arial and Courier New; Carlito for Calibri;
 * Caladea for Cambria). Only the tables needed are read (head, hhea, OS/2, hmtx, cmap).
 *
 * Widths are kept per face and size in 1/16 twip ("fine" units) for Latin-1, so measuring text
 * is a table lookup; other characters go through the cmap.
 */
#ifndef QUILL_FONTS_H
#define QUILL_FONTS_H

#include "util.h"

#define QFINE 16                       /* fine units per twip */

typedef struct Face {
  char family[48];
  u8 bold, italic;
  u8 installed;                       /* one of the system's (FONTS:), not one Quill ships */
  u8 index;                           /* its place in a collection (.ttc), else 0 */
  u8 cff;                             /* OpenType with CFF outlines (.otf): 1, CFF2 (variable): 2; TrueType: 0 */
  u16 fstype;                         /* OS/2 fsType: what its licence lets a PDF embed */
  char path[256];
  char amiga[40];                     /* the .font diskfont.library opens it by ("" if none) */
  int loaded, failed;
  u16 upm;
  i32 ascent, descent, linegap;       /* font units: Windows' (GDI) metrics, which Word uses */
  u16 nmetrics, nglyphs;
  u16 *adv;                           /* advance width per glyph */
  /* cmap: format 4 segments (BMP) */
  u16 nseg;
  u16 *seg_end, *seg_start, *seg_off;
  i16 *seg_delta;
  u16 *glyphs; u32 nglyphids;         /* the glyph id array after the segments */
  u16 latin_glyph[256];
} Face;

/* one face at one size: widths of Latin-1 characters and the line metrics, in twips (fine) */
typedef struct FaceSize {
  Face *face;
  u16 size;                           /* half-points */
  i32 ascent, descent, height;        /* twips: ascent and descent of the text, the single line height */
  u32 fine[256];                      /* advance of each Latin-1 character, fine units */
  struct FaceSize *next;
} FaceSize;

#define MAXFACES 256
typedef struct FontSet {
  Face faces[MAXFACES];
  int nfaces;
  FaceSize *sizes;                    /* cache, most recent first */
  char dir[256];
} FontSet;

/* the fonts listed in dir/fonts.list: one per line, "file;Family;bold;italic" (# comments) */
int   fonts_init(FontSet *fs, const char *dir);
/* or add one face by hand (tests, user fonts); returns its index or -1 */
int   fonts_add(FontSet *fs, const char *path, const char *family, int bold, int italic);
void  fonts_free(FontSet *fs);                 /* frees the size cache and every face's tables */
/* a font file's family (the name Word documents use: name ID 1, as UTF-8) and style, for face
   index of a collection (0 otherwise); 0 if it is not a font or has no family name */
int   fonts_probe(const char *path, int index, char *family, int famsize, int *bold, int *italic);
/* how many faces a font file holds (a collection several, else 1); 0 if it isn't a font */
int   fonts_count(const char *path);
/* the families, each once, sorted (pointers into fs); returns how many */
int   fonts_families(FontSet *fs, const char **out, int max);
/* the face layout uses for a family and style (substituted as fonts_size does); NULL if none */
Face *fonts_face(FontSet *fs, const char *family, int bold, int italic);
/* the family layout uses for a family a document asks for (Times New Roman -> Liberation Serif...) */
const char *fonts_substitute(FontSet *fs, const char *family);
/* metrics of family (substituted as needed), bold/italic, at size half-points; never NULL as long as
   any face loads; NULL if none can */
FaceSize *fonts_size(FontSet *fs, const char *family, int bold, int italic, u16 size);
/* the advance of one code point in fine units */
u32   fonts_fine(FaceSize *z, u32 cp);
/* the width of UTF-8 text in fine units */
u32   fonts_text(FaceSize *z, const char *s, u32 n);

/* the table directory of face index of a font file (plain, or a .ttc collection): hdr gets the
   12 bytes before it and the file is left at it; *cff set for CFF outlines. 0 if not a font */
int   sfnt_header(void *fp, int index, u8 hdr[12], int *cff, long fsize);
/* a face as a font file of its own (a face of a collection taken out of it; else the file);
   malloc'd, NULL if it can't be read */
u8   *fonts_face_file(const Face *f, size_t *len);
/* may a PDF carry this face (its licence: not "restricted", not bitmaps only; not CFF2)? */
int   fonts_embeddable(const Face *f);
/* reading a TrueType file into a Face (0 if it is not one, or is damaged) */
int   ttf_load(Face *f);
/* the glyph id of a BMP code point (0, .notdef, if the face has none) */
u16   ttf_glyph(Face *f, u32 cp);

#endif
