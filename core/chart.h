/*
 * chart.h - charts and SmartArt in documents (WP15a; WP15b and WP15c build on it).
 *
 * A chart or SmartArt in a .docx is a drawing (in line or floating, wrapped like a picture) that
 * refers to parts of its own: a chart part (c:chartSpace, or Excel 2016's cx:chartSpace) with its
 * embedded workbook and colour and style parts; a SmartArt's data, layout, quick style and colour
 * parts and the drawing Word saved from them (dsp:drawing). Quill keeps all of them byte for byte
 * in an ObjPack that the object's Doc.images entry points to (shared, counted, between the entries
 * that copies of it make), and writes them back with their relationships when the document is
 * saved, under their own names where it can.
 *
 * What is shown: a chart drawn from the values cached in its part (column, bar, line, area, pie,
 * doughnut, scatter, bubble, radar, stock and the chartex kinds; a frame with the title for the
 * others), a SmartArt from its saved drawing's shapes and text. Both are laid out as a list of
 * simple primitives (rectangles, lines, polygons, ellipses, text) in pixels at 96 dpi, which the
 * screen and the PDF writer draw.
 *
 * The chart model and its layout follow Ledger's chart.c (same author), which follows Excel's
 * documented behaviour and GridCraft's chart crate; here the series hold their cached values
 * instead of formulas.
 */
#ifndef QUILL_CHART_H
#define QUILL_CHART_H

#include "doc.h"
#include "fonts.h"
#include "zip.h"
#include "xml.h"
#include "docx.h"

/* ------------------------------------------------------------------ drawing primitives */
enum { PR_RECT, PR_LINE, PR_POLY, PR_ELLIPSE, PR_TEXT };
#define PRIM_ON 0x01000000UL          /* fill and line: FILL... | 0xRRGGBB; 0 none */
typedef struct {
  u8 t;                             /* PR_ */
  u8 dash, bold, align, valign, vert;   /* text: align 0 left 1 centre 2 right of x; valign 0 top 1 middle 2 bottom of y; vert: turned to read upwards */
  u8 size;                          /* text: pixels to the em */
  u8 width;                         /* lines: pixels */
  u8 italic;
  u16 elem;                         /* the part of a chart it belongs to (CE_, CE_SERIES + k), CE_NONE */
  u32 fill, line;                   /* PRIM_ON | rgb, 0 none (text: line is its colour) */
  i32 x, y, w, h;                   /* rectangles and ellipses (ellipses: the box), text's anchor (w: its width) */
  int np; i32 *pts;                 /* polylines and polygons: x, y pairs */
  char *text;                       /* UTF-8 */
} Prim;
typedef struct { Prim *p; int n, cap; int oom; } PrimList;
void  prims_init(PrimList *l);
void  prims_free(PrimList *l);
Prim *prim_add(PrimList *l, int t, u16 elem);   /* zeroed; NULL out of memory */
/* text widths for laying out: px pixels to the em */
typedef i32 (*TextWidthFn)(void *ctx, const char *utf8, int px, int bold);

/* ------------------------------------------------------------------ charts */
enum {
  CK_COL, CK_COLSTACK, CK_COL100, CK_BAR, CK_BARSTACK, CK_BAR100, CK_LINE, CK_LINEMARK, CK_LINESTACK,
  CK_PIE, CK_DOUGHNUT, CK_AREA, CK_AREASTACK, CK_AREA100, CK_SCATTER, CK_SCATTERLINE, CK_BUBBLE, CK_RADAR,
  CK_HISTOGRAM, CK_WATERFALL, CK_FUNNEL, CK_TREEMAP, CK_SUNBURST, CK_BOXWHISKER, CK_STOCK, CK_COMBO,
  CK_OTHER,                         /* a kind not drawn (surface, 3-D shapes, map...): a frame with its title */
  CK_N
};
#define CK_SAME 0xFF                /* a series drawn as its chart is */
enum { LG_NONE, LG_BOTTOM, LG_TOP, LG_LEFT, LG_RIGHT };
/* the parts of a chart (hit testing, and the Format pane a later package adds) */
enum { CE_CHART, CE_PLOT, CE_TITLE, CE_LEGEND, CE_XAXIS, CE_YAXIS, CE_N, CE_SERIES = 8 };
#define CE_NONE 0xFFFF
#define CE_NOLINE 0x02000000UL      /* Chart.eline[CE_CHART]: no outline round the chart area */
#define CV_NONE (-1.0e300)          /* an empty point */
#define CV_IS_NONE(v) chart_is_none(v)
int chart_is_none(double v);   /* (told from its bits: see chart.c) */

typedef struct { u8 minset, maxset, majset; double min, max, major; } CAxis;
typedef struct {
  char *name;                       /* the series' name (UTF-8), NULL: "Series n" */
  char **cat; int ncat;             /* its category labels (scatter: unused) */
  double *x; int nx;                /* scatter and bubble: the X values (NULL: 1, 2, 3...) */
  double *v; int nv;                /* the values (CV_NONE where empty) */
  double *size; int nsize;          /* bubble sizes */
  u32 color, line;                  /* fill and line colour, PRIM_ON | rgb (0: automatic) */
  u8 kind;                          /* a combo chart's series: CK_COL, CK_LINE... or CK_SAME */
  u8 secondary, smooth, nomarker;
} CSeries;
typedef struct Chart {
  u8 kind;                          /* CK_ */
  u8 has_title;                     /* a title shown: title, or (NULL/empty) the series name or "Chart Title" */
  char *title;
  u8 legend;                        /* LG_ */
  u8 labels;                        /* data labels (the values) */
  u8 hgrid, vgrid;                  /* major gridlines across (value axis) and up (category axis) */
  u8 style;                         /* chart style 1..8 (the look of bars and lines) */
  u8 palette;                       /* 0 colourful, 1 monochrome, 2..7 one accent's shades */
  u8 hole;                          /* doughnut: the hole, per cent of the diameter (0: 50) */
  char fmt[32];                     /* the values' number format ("General", "0%", "#,##0.00"...) */
  char *xtitle, *ytitle;            /* axis titles, NULL none */
  CAxis va, ha;                     /* the value axis, and scatter's X axis */
  u32 efill[CE_N], eline[CE_N];     /* fill and outline of chart area, plot area, title, legend (0 automatic) */
  u32 accent[6];                    /* the theme's accent colours (0: Office's) */
  char kindname[32];                /* CK_OTHER: what it is ("Surface") */
  char font[48];                    /* its text's font (a:latin typeface; "+mn-lt" the theme's body font), "" none */
  CSeries *s; int ns;
} Chart;

/* set (by the Amiga side) when doubles can't be computed (no mathieeedoubbas.library): charts are then
   read without their values and drawn as frames */
extern int chart_nofp;
Chart *chart_new(int kind);
void   chart_free(Chart *c);
Chart *chart_copy(const Chart *c);
const char *chart_kind_name(int kind);    /* "Clustered Column" */
int    chart_kind_by_name(const char *name);
int    chart_is_cartesian(int kind);      /* has axes */
int    chart_is_pielike(int kind);        /* pie, doughnut, funnel, treemap, sunburst: one colour a point */
CSeries *chart_add_series(Chart *c);      /* a new empty series at the end (zeroed, kind CK_SAME); NULL out of memory */
u32    chart_series_rgb(const Chart *c, int k, int n);   /* series k's colour (of n) */
/* a nice axis: from lo to hi in steps of major (a few of them) */
void   chart_nice_scale(double lo, double hi, int maxticks, double *min, double *max, double *major);
/* a value as format fmt shows it ("General", "0", "0.0", "0%", "#,##0.00", "$#,##0"... the usual
   Excel codes; anything else as General) */
void   chart_fmt_value(double v, const char *fmt, char *out, size_t cap);
/* the chart laid out in w x h pixels (96 dpi). Returns 0 if out of memory */
int    chart_render(const Chart *c, int w, int h, TextWidthFn tw, void *ctx, PrimList *out);
/* a chart part (c:chartSpace, or cx:chartSpace when ex) read from its cached values; theme: the
   document's 12 theme colours (dk1 lt1 dk2 lt2 accent1-6 hlink folHlink) or NULL. NULL if it isn't one */
Chart *chart_read(const char *xml, size_t len, int ex, const u32 *theme);
/* a DrawingML colour element (x at its START, read to its END) as PRIM_ON | rgb, with its shade,
   tint and luminance changes; theme as above. 0 if it isn't a colour */
u32    chart_dml_color(Xml *x, const u32 *theme);

/* ------------------------------------------------------------------ SmartArt's saved drawing */
typedef struct {
  i32 x, y, w, h;                   /* EMU, in the drawing's space */
  i32 tx, ty, tw, th;               /* the text's box (dsp:txXfrm; w 0: the shape's) */
  u16 rot;                          /* tenths of a degree, clockwise */
  u8 flip;                          /* 1 across, 2 upside down */
  u8 shape;                         /* SH_ (doc.h); unknown presets draw as rectangles */
  u8 nofill, noline;
  u32 fill, line;                   /* 0xRRGGBB */
  i32 lw;                           /* line width, EMU */
  char *text;                       /* its text, paragraphs joined by '\n' (UTF-8), or NULL */
  u16 sz;                           /* text size, hundredths of a point (0: 18 pt) */
  u8 bold, italic, algn;            /* algn: 0 left 1 centre 2 right */
  u8 anchor;                        /* 0 top 1 middle 2 bottom */
  u32 tcolor;                       /* 0xRRGGBB */
} SAShape;
typedef struct SmartArt { SAShape *s; int n; i32 ext[2]; } SmartArt;
/* a SmartArt's drawing part (dsp:drawing); theme as for chart_read. NULL if there are no shapes */
SmartArt *smartart_read(const char *xml, size_t len, const u32 *theme);
void   smartart_free(SmartArt *a);
/* the drawing laid out in w x h pixels, scaled to fit as Word does (never above its own size) */
int    smartart_render(const SmartArt *a, int w, int h, TextWidthFn tw, void *ctx, PrimList *out);

/* ------------------------------------------------------------------ the parts kept (chartkeep.c) */
enum { PK_CHART, PK_CHARTEX, PK_SMARTART };
/* what a document-level part is to the object (its relationship from the text) */
enum { RO_NONE, RO_CHART, RO_DM, RO_LO, RO_QS, RO_CS, RO_DRAWING };
typedef struct {
  char *name;                       /* its name in the package as read ("word/charts/chart1.xml") */
  char *ctype;                      /* content type; NULL for a .rels part */
  char *reltype, *relid;            /* the relationship from the document to it (role != RO_NONE) */
  u8 role;                          /* RO_ */
  int owner;                        /* a .rels part: the index of the part whose relationships it holds, else -1 */
  u8 *data; size_t len;
} PackPart;
/* a part several packs share (the document's theme, which the parts' theme colours and fonts refer to) */
typedef struct PackBlob { int refs; u8 *data; size_t len; } PackBlob;
typedef struct ObjPack {
  int refs;                         /* Doc.images entries (and clipboard copies) pointing to it */
  u8 kind;                          /* PK_ */
  PackPart *p; int np;
  Chart *chart;                     /* charts: as read from the part (NULL if it couldn't be) */
  SmartArt *art;                    /* SmartArt: its drawing (NULL if there is none) */
  char font[48];                    /* the font its text is drawn in ("" the document's) */
  PackBlob *theme;                  /* the theme of the document it came from (written with it when the
                                       document being saved has none: Quill writes no theme itself) */
  /* the last layout drawn, for this size and family */
  PrimList cache; i32 ccx, ccy; char cfam[48];
} ObjPack;
ObjPack *pack_new(int kind);              /* refs 1; NULL out of memory */
ObjPack *pack_ref(ObjPack *pk);           /* one more reference (NULL is fine); returns pk */
void     pack_unref(ObjPack *pk);         /* one fewer; freed at none (NULL is fine) */
/* adds a part (data copied); its index, -1 out of memory */
int      pack_add(ObjPack *pk, const char *name, const char *ctype, const char *reltype, const char *relid, int role, int owner,
                  const void *data, size_t len);
int      pack_find(const ObjPack *pk, const char *name);   /* -1 if none */
/* the object drawn cx x cy twips: its primitives in pixels at 96 dpi (one pixel 15 twips), text in
   its own font (pk->font), else family (the document's default font), measured with fs. Laid out again only when the size or font
   changes; NULL out of memory */
const PrimList *pack_prims(ObjPack *pk, i32 cx, i32 cy, FontSet *fs, const char *family);
/* what the object is called ("Chart: Sales", "SmartArt"), for alt text and status lines */
void     pack_title(const ObjPack *pk, char *out, size_t cap);

/* reading and writing a .docx (called from docx.c) */
/* a chart (c:chart or cx:chart r:id) or SmartArt (dgm:relIds) a drawing refers to, from the archive:
   rels, rn the referring part's relationships, base its folder, ct the content types. NULL if its parts
   aren't there */
ObjPack *pack_load_chart(ZipReader *z, const char *ct, size_t ctn, const char *rels, size_t rn, const char *base,
                         const char *rid, int ex, const u32 *theme, const char *minor, const char *major);
ObjPack *pack_load_smartart(ZipReader *z, const char *ct, size_t ctn, const char *rels, size_t rn, const char *base,
                            const char *dm, const char *lo, const char *qs, const char *cs, const u32 *theme, const char *minor);
/* the theme of the document being read (zip z; rels, base: the main part's relationships and folder)
   kept with pk, shared with the packs of d that came with the same one. A theme with relationships of
   its own (pictures) isn't kept */
void pack_set_theme(ObjPack *pk, Doc *d, ZipReader *z, const char *rels, size_t rn, const char *base);
/* saving: every pack the document's text refers to given its part names and relationship ids
   (call before writing any part); then the graphic of image k, the parts, and the relationships
   (each <Relationship .../>) that every part showing objects needs */
void pack_save_begin(Doc *d);
int  pack_save_graphic(Buf *o, const Image *im);   /* the a:graphicData; 0 if im has no pack being saved */
int  pack_save_isex(const Image *im);              /* a chartex: written inside mc:AlternateContent */
void pack_save_fallback(Buf *o, const Image *im);  /* ...whose end this writes, with its fallback: the chart's name as text */
void pack_save_parts(Doc *d, DocxOut *o);
void pack_save_rels(Buf *o);
int  pack_save_has_rels(void);

#endif
