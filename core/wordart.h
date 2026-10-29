/*
 * wordart.h - WP19 WordArt (docs/wordcraft-gap.md): Word 2010's text effects on runs (w14:textFill,
 * textOutline, shadow, glow, reflection, a minimal props3d bevel), drawn from the letters' outlines
 * on the screen and in PDFs; Quill's WordArt styles and the Text Fill, Text Outline and Text Effects
 * presets; the Transform warps of a shape's text (a:prstTxWarp), laid out by mapping the glyph
 * outlines; and the signature line (Insert > Signature Line, kept in .docx as Word's VML
 * o:signatureline object). This file belongs to WP19.
 *
 * The effects stay where WP9 keeps a run's w14 properties, CharFmt.rx (core/charfx.h): read once,
 * written back once, unchanged unless edited. wa_get parses them (cached by rx number) for drawing;
 * wa_set writes a changed TextFx back into rx in the schema's order. Integer arithmetic only.
 */
#ifndef QUILL_WORDART_H
#define QUILL_WORDART_H

#include "doc.h"
#include "fonts.h"
#include "layout.h"

/* ---- the effects of a run */
enum { WF_DEFAULT, WF_NONE, WF_SOLID, WF_GRAD };   /* a fill's or line's kind (WF_DEFAULT: not set: the text's colour, no line) */
#define WA_MAXGS 6
typedef struct {
  u8  fill;                 /* WF_ */
  u32 fcol; u8 falpha;      /* solid: 0xRRGGBB, transparency per cent */
  u8  ngs;                  /* gradient: its stops (2..WA_MAXGS), where (per cent), colours, transparencies */
  u8  gpos[WA_MAXGS], galpha[WA_MAXGS];
  u32 gcol[WA_MAXGS];
  u16 gangle;               /* linear: tenths of a degree clockwise from left to right */
  u8  gpath;                /* 0 linear, 1 from the centre (w14:path circle) */
  u8  line;                 /* WF_: the letters' outline (a gradient line drawn in its first colour) */
  u32 lcol; u8 lalpha;
  i32 lw;                   /* twips */
  u8  shadow; u32 shcol; u8 shalpha;
  i32 shblur, shdist;       /* twips */
  u16 shdir;                /* tenths of a degree, clockwise from the right */
  u8  glow; u32 glcol; u8 glalpha;
  i32 glrad;                /* twips */
  u8  refl;                 /* a reflection: opacity from rsta to rena (per cent) over rena's place renpos (per cent of the letters' height) */
  u8  rsta, rena, renpos;
  i32 rblur, rdist;         /* twips */
  u8  bevel;                /* props3d's top bevel (BV_), 0 none: drawn as a light and a dark edge */
} TextFx;
enum { BV_NONE, BV_CIRCLE, BV_RELAXED, BV_SLOPE, BV_ANGLE, BV_N };
/* what wa_set changes */
enum { WA_FILL = 1, WA_LINE = 2, WA_SHADOW = 4, WA_GLOW = 8, WA_REFL = 16, WA_3D = 32, WA_ALL = 63 };

/* the effects rx holds (parsed once, kept), NULL if none of them: the text is drawn as before */
const TextFx *wa_get(u16 rx);
/* f's effects drawn by Quill (wa_get of its rx, not NULL) */
#define WA_RICH(f) ((f)->rx && wa_get((f)->rx))
/* rx with the parts which names (WA_) made fx's (an element left out where fx has none), others kept;
   the w14 elements in the order the schema wants. The number of the result */
u16  wa_set(u16 rx, const TextFx *fx, unsigned which);
/* the default theme's colour of a scheme name ("accent1", "tx1"...), COLOR_AUTO if unknown */
u32  wa_scheme(const char *name);

/* ---- presets */
/* WordArt styles (Insert WordArt, Shape Format's WordArt Quick Styles): Quill's own */
#define WA_NSTYLES 12
extern const char *const wa_style_names[WA_NSTYLES];
void wa_style(int i, TextFx *fx);
/* Text Fill: 0 No Fill, then colours, then gradients; Text Outline: 0 No Outline, colours, then weights;
   the effects lists: shadows, reflections, glows, bevels (each 0 none) */
#define WA_NFILLS 14
#define WA_NLINES 12
#define WA_NSHADOWS 6
#define WA_NREFLS 4
#define WA_NGLOWS 6
#define WA_NBEVELS 5
extern const char *const wa_fill_names[WA_NFILLS], *const wa_line_names[WA_NLINES], *const wa_shadow_names[WA_NSHADOWS],
       *const wa_refl_names[WA_NREFLS], *const wa_glow_names[WA_NGLOWS], *const wa_bevel_names[WA_NBEVELS];
extern const u32 wa_fill_colours[WA_NFILLS], wa_line_colours[WA_NLINES];
/* each changes its part of fx (keeping the line's colour when a weight is picked, and so on) */
void wa_fill_preset(int i, TextFx *fx);
void wa_line_preset(int i, TextFx *fx);
void wa_shadow_preset(int i, TextFx *fx);
void wa_refl_preset(int i, TextFx *fx);
void wa_glow_preset(int i, TextFx *fx);
void wa_bevel_preset(int i, TextFx *fx);

/* ---- warps (Image.warp): a shape's text bent. Every DrawingML preset is kept (its index in Quill's
   table, 0 none); the first WW_N - 1 are drawn bent, the others flat */
enum { WW_NONE, WW_ARCHUP, WW_ARCHDOWN, WW_CIRCLE, WW_WAVE, WW_SLANTUP, WW_SLANTDOWN, WW_INFLATE, WW_DEFLATE,
       WW_CHEVRONUP, WW_CHEVRONDOWN, WW_TRIUP, WW_TRIDOWN, WW_N };
#define WW_ALL 42
extern const char *const wa_warp_labels[WW_N];
const char *wa_warp_prst(int w);          /* its a:prstTxWarp name ("textArchUp"), NULL for 0 or out of range */
int  wa_warp_from_prst(const char *prst); /* its index, 0 if unknown */
/* the point of a w x h frame where the text box's (u, v) goes (u across, v down, each 0..65536 of
   the text's box); aspect: the text's height per 1000 of its width */
void wa_warp_map(int warp, i32 w, i32 h, int aspect, i32 u, i32 v, i32 *x, i32 *y);

/* ---- letters as outlines: segments (ax ay bx by) in eighths of a twip, y down */
#define WA_SUB 8
typedef struct { i32 *seg; int n, cap; i32 x0, y0, x1, y1; int oom; i32 bold; } WaPath;   /* bold: a synthetic bold's stroke (twips), 0 none */
void wa_path_init(WaPath *p);
void wa_path_free(WaPath *p);
/* run text s[0..n) in format f (its caps, small capitals, spacing, scale, raised and lowered text)
   added at x on baseline base (twips); 1 if any letter has an outline */
int  wa_path_run(WaPath *p, Layout *L, const CharFmt *f, const char *s, u32 n, i32 x, i32 base);
/* plain text s[0..n) in face size z at x on baseline base (twips); 1 if any letter has an outline */
int  wa_path_text(WaPath *p, FaceSize *z, const char *s, u32 n, i32 x, i32 base);
/* p bent: its points (within the text box tb: x0 y0 x1 y1 twips) mapped onto the frame w x h by warp,
   then turned by rot tenths of a degree round the frame's centre, which goes to cx, cy (twips);
   long lines cut so they bend */
void wa_path_warp(WaPath *p, int warp, const i32 *tb, i32 w, i32 h, int rot, i32 cx, i32 cy);

/* a laid-out text box's text (f, from lay_textbox) as one path for each box of text: in the text
   box's space (twips from its top left, y down); how many (*out malloc'd), 0 if none */
typedef struct { WaPath p; CharFmt f; } WaPiece;
int  wa_pieces(Layout *L, const Furn *f, WaPiece **out);
void wa_pieces_free(WaPiece *pc, int n);
/* the pieces bent together: their letters' box mapped onto the frame w x h by warp (WW_), turned by
   rot round the frame's centre, which goes to cx, cy */
void wa_pieces_warp(WaPiece *pc, int n, int warp, i32 w, i32 h, int rot, i32 cx, i32 cy);

/* ---- drawing: the effects of p as RGBA (straight alpha), num / den pixels a twip; ox, oy: the
   raster's top left in pixels from the path's origin. layers: WL_SOFT the reflection, shadow and
   glow, WL_LETTERS the fill, outline and bevel. color: the text's own colour. 0 if out of memory */
enum { WL_SOFT = 1, WL_LETTERS = 2, WL_ALL = 3 };
typedef struct { i32 w, h, ox, oy; u8 *px; } WaRaster;
int  wa_render(const WaPath *p, const TextFx *fx, u32 color, long num, long den, int layers, WaRaster *r);
void wa_raster_free(WaRaster *r);
/* PDF: the letters (fill, gradient as strips under a clip, outline) as paths, the origin of p at
   ox, oy in PDF space (y up, twips) */
void wa_pdf_letters(Buf *b, const WaPath *p, const TextFx *fx, u32 color, i32 ox, i32 oy);

/* ---- .docx shapes: a:prstTxWarp read (x an Xml * inside a shape; 1 if taken) and written inside wps:bodyPr */
int  wa_read_shape(void *x, Image *im);
void wa_write_warp(Buf *o, const Image *im);

/* ---- the signature line: Word's setup (Insert > Signature Line) */
struct SigLine {
  char signer[128], title[128], email[128], instr[256];   /* UTF-8 */
  u8 comments, showdate;    /* the signer may add comments; the date shown when signed */
  char id[40];              /* its GUID ("{...}") */
};
struct SigLine *sig_copy(const struct SigLine *s);   /* malloc'd; NULL if s is or out of memory */
void sig_free(struct SigLine *s);
/* im becomes (or is redrawn as) a signature line: an in-line picture 192 x 96 points of what Word
   draws (a cross, the line, the signer's name and title under it), made with fs's fonts as PNG. 0 if
   it couldn't be (im unchanged) */
int  sig_make(Image *im, const struct SigLine *s, FontSet *fs);
/* fonts for signature lines read from files (set by the program); NULL: their pictures as they come */
extern FontSet *sig_fonts;
/* .docx: a w:pict's text (raw, len bytes): 1 if it is a signature line (s filled, relid: its
   v:imagedata's r:id, size: its style's width and height in twips) */
int  sig_parse_vml(const char *raw, size_t len, struct SigLine *s, char *relid, size_t rcap, i32 *cx, i32 *cy);
/* the run holding it (rpr: the run's properties), media: its picture's rIdImg number (0 none) */
void sig_write_vml(Buf *o, const Image *im, const char *rpr, size_t rl, int media, u32 id);

#endif
