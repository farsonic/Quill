/*
 * ink.h - WP20: freeform shapes and ink (the Draw tab). A freeform's outline is a list of paths of
 * points (DrawingML's a:custGeom), kept in Image.ff and shared, counted, between the entries copies
 * of the object make (a change makes a new one). Ink is a freeform drawn with a pen, pencil or
 * highlighter: its points smoothed, written to .docx as a wps:wsp with a:custGeom named the way
 * Word's ink and WordCraft's are ("Ink Pen 3 #12": the pen and its place in the drawing order),
 * which is also how Word's own freeform shapes come in. Word's native ink (w14:contentPart naming an
 * InkML part) is kept as it was written, and drawn from the part's traces.
 *
 * Also here: the smoothing of a stroke drawn with the mouse, Ink to Shape (recognising lines,
 * arrows, rectangles, ellipses, triangles and other polygons), hitting and rubbing out strokes, the
 * lasso, and a drawing canvas's pieces (a canvas is a group with Image.canvas set). Integer
 * arithmetic only, like the rest of the core. This file belongs to WP20.
 */
#ifndef QUILL_INK_H
#define QUILL_INK_H

#include "doc.h"
#include "xml.h"

#define SH_FREEFORM 200          /* Image.shape of a freeform: its outline is Image.ff (past doc.h's SH_ and objfx.h's SHX_) */
enum { INK_NONE, INK_PEN, INK_PENCIL, INK_HIGHLIGHTER };   /* Freeform.tool (INK_NONE: a freeform shape, not ink) */
enum { CANVAS_NO, CANVAS_GROUP, CANVAS_BG };   /* Image.canvas: a drawing canvas (a group), or the shape that is its background (its first object) */
#define FF_MAXPTS 4000           /* points a path keeps */
#define FF_MAXPATHS 256

typedef struct { i32 *xy; u32 n; u8 closed; } FfPath;
typedef struct Freeform {
  u32 refs;                 /* entries sharing it */
  i32 w, h;                 /* the paths' own space, stretched over the object's cx x cy (> 0) */
  FfPath *p; u32 np;
  u8 tool;                  /* INK_ */
  u8 alpha;                 /* the outline's opacity, per cent (0: opaque) */
  u32 order;                /* ink: its place in the order strokes were drawn (Ink Replay), 0 unknown */
  char *raw;                /* Word's own ink: its w14:contentPart as written (NULL: none) */
  char *rawpart;            /* the InkML part it names ("word/ink/ink1.xml") */
} Freeform;

/* ---- freeforms */
Freeform *ff_new(i32 w, i32 h);            /* empty, refs 1; NULL if out of memory */
void ff_ref(Freeform *f);                  /* NULL is fine */
void ff_unref(Freeform *f);                /* freed with the last reference; NULL is fine */
int  ff_add_path(Freeform *f, const i32 *xy, u32 n, int closed);   /* copied; 0 if out of memory */
Freeform *ff_copy(const Freeform *f);      /* a new one (refs 1) with the same paths and settings */
/* path k's point i in the object's own cx x cy (twips) */
void ff_point(const Freeform *f, i32 cx, i32 cy, u32 k, u32 i, i32 *u, i32 *v);
int  ff_is_ink(const Image *im);           /* a stroke of ink (not a freeform shape) */
u32  ink_next_order(const Doc *d);         /* one after the highest drawing order in the document */

/* ---- drawing with the mouse */
/* a stroke's points (n pairs) smoothed: jitter averaged out, points closer than tol to the line
   through their neighbours dropped, then the corners rounded; into out (cap pairs). How many */
u32  ink_smooth(const i32 *in, u32 n, i32 *out, u32 cap, i32 tol);
/* a new ink stroke (or, tool INK_NONE, an open freeform) through pts (n pairs, twips on the page):
   im gets its settings (floating in front of the text; a highlighter's behind it, where the text
   shows through it), its size padded by half the line; *x, *y its box's top left on the page.
   width: eighths of a point. 0 if out of memory or too few points */
int  ink_make(Image *im, int tool, u32 color, int width, const i32 *pts, u32 n, i32 *x, i32 *y);

/* ---- Ink to Shape */
typedef struct {
  int shape;                /* SH_LINE, SH_ARROW, SH_RECT, SH_ELLIPSE, SH_TRIANGLE, SH_RTRIANGLE, SH_DIAMOND, or SH_FREEFORM (a polygon) */
  i32 x, y, w, h;           /* its box, in the stroke's coordinates */
  u8 flip;                  /* 1 across, 2 upside down (a line's or arrow's direction, a triangle's) */
  i32 poly[16]; int npoly;  /* SH_FREEFORM: its corners (3-8), from the box's top left */
} InkShape;
/* the shape pts (n pairs) draw, if they draw one: 1 and *r */
int  ink_recognise(const i32 *pts, u32 n, InkShape *r);
/* the ink stroke im as the shape it looks like (out: a new object, its box moved by *dx, *dy from
   the stroke's); 0 if it isn't a pen or pencil stroke, or doesn't look like a shape */
int  ink_to_shape(const Image *im, Image *out, i32 *dx, i32 *dy);
const char *ink_shape_name(int shape);     /* "Rectangle"... (English, for messages) */

/* ---- hitting, rubbing out, the lasso (u, v: a point in the object's own cx x cy, twips) */
int  ff_hit(const Image *im, i32 u, i32 v, i32 r);   /* within r of a stroke (half its width added) */
/* the paths with the points within r of (u, v) taken out (paths split there): a new freeform, or NULL
   if nothing is left; *hit whether anything was near */
Freeform *ff_erase(const Image *im, i32 u, i32 v, i32 r, int *hit);
int  poly_inside(const i32 *poly, u32 n, i32 x, i32 y);    /* even-odd rule */
/* object im with its box's top left at bx, by on the page is in the loop poly (n pairs, page
   twips): most of a stroke's points, or a shape's centre */
int  ink_in_lasso(const Image *im, i32 bx, i32 by, const i32 *poly, u32 n);

/* ---- drawing canvas */
/* a new, empty canvas cx x cy twips (in line with the text): a group whose first object is its
   background (no fill, no outline). 0 if out of memory */
int  canvas_make(Image *im, i32 cx, i32 cy);
/* object kid added to canvas c (a copy, at x, y in it; the canvas's space grows to hold it): 0 if out of memory */
int  canvas_add(Image *c, const Image *kid, i32 x, i32 y);

/* ---- .docx: a:custGeom, the ink's name, Word's InkML */
/* x just past an a:custGeom's START: its paths, read to its END, in a space ew x eh (the shape's
   size, EMU, for paths that give none); NULL if it has no points */
Freeform *ff_read_custgeom(Xml *x, i32 ew, i32 eh);
void ff_write_custgeom(Buf *o, const Freeform *f);
/* a shape's name: "Ink Pen 3 #12" for ink (pen, id, drawing order), else NULL */
const char *ink_name(const Freeform *f, u32 id, char *out, size_t cap);
/* what a name says: the pen (INK_NONE: not ink) and drawing order */
void ink_parse_name(const char *name, u8 *tool, u32 *order);
/* the traces of an InkML part (its brush's colour and width, eighths of a point, if it gives them)
   as paths stretched over cx x cy twips: into f; 0 if it has none */
int  ff_from_inkml(Freeform *f, const char *xml, size_t n, i32 cx, i32 cy, u32 *color, int *width);
/* PDF: the freeform part pt (its centre at cx, cy in PDF space, twips, y up) drawn into b */
void ff_pdf(Buf *b, const ObjPart *pt, i32 cx, i32 cy);

#endif
