/*
 * objfx.h - WP17 and WP18: what a picture's adjustments (corrections, colour, transparency, artistic
 * effects, a removed background) and an object's fills and effects (gradients, picture fills,
 * shadow, glow, soft edges) do, drawn by Quill itself into an RGBA raster that the screen and PDFs
 * both show; Word's other preset shapes (callouts, flowchart, block arrows, stars...), an icon
 * library drawn in Quill's icon language, and text boxes' text direction, anchoring and linking.
 * The settings live in Image.fx (doc.h's ObjFx); all zero is an object as it always was.
 *
 * Like the rest of the core it is integer arithmetic only (twips, per cents, tenths of a degree).
 */
#ifndef QUILL_OBJFX_H
#define QUILL_OBJFX_H

#include "doc.h"
#include "picfmt.h"

/* ObjFx.cmode: a picture's colour */
enum { PCM_NONE, PCM_GREY, PCM_SEPIA, PCM_WASHOUT, PCM_BW, PCM_DARK, PCM_LIGHT, PCM_N };
/* ObjFx.art: artistic effects (a14:artistic... names in objfx.c) */
enum { ART_NONE, ART_BLUR, ART_PENCIL, ART_LINES, ART_PHOTOCOPY, ART_CUTOUT, ART_GRAIN, ART_GLOWEDGES, ART_MOSAIC, ART_N };
/* ObjFx.filltype, gtype; vert; anchor */
enum { FT_SOLID, FT_GRAD, FT_PIC };
enum { GT_LINEAR, GT_RADIAL, GT_RECT };
enum { TV_HORZ, TV_VERT, TV_VERT270 };
enum { TA_TOP, TA_MIDDLE, TA_BOTTOM };

/* more of Word's preset shapes (Image.shape, past doc.h's SH_N), and an icon (ObjFx.icon) */
enum {
  SHX_FIRST = 32,
  /* basic */
  SHX_HEPTAGON = SHX_FIRST, SHX_DECAGON, SHX_DODECAGON, SHX_TEARDROP, SHX_PIE, SHX_CHORD, SHX_ROUND2SAME, SHX_ROUND2DIAG,
  SHX_SNIP1, SHX_SNIP2DIAG, SHX_PLAQUE, SHX_MOON, SHX_LIGHTNING, SHX_CLOUD, SHX_CORNER, SHX_DIAGSTRIPE, SHX_HALFFRAME,
  SHX_WAVE, SHX_DOUBLEWAVE, SHX_FOLDED,
  /* block arrows */
  SHX_LRARROW, SHX_UDARROW, SHX_QUADARROW, SHX_LRUARROW, SHX_BENTARROW, SHX_UTURN, SHX_LEFTUP, SHX_BENTUP, SHX_NOTCHED,
  SHX_RARROWCALL, SHX_LARROWCALL, SHX_UARROWCALL, SHX_DARROWCALL, SHX_LRARROWCALL,
  /* flowchart */
  SHX_FC_PROCESS, SHX_FC_ALT, SHX_FC_DECISION, SHX_FC_DATA, SHX_FC_PREDEF, SHX_FC_DOCUMENT, SHX_FC_MULTIDOC, SHX_FC_TERMINATOR,
  SHX_FC_PREP, SHX_FC_MANUALIN, SHX_FC_MANUALOP, SHX_FC_CONNECTOR, SHX_FC_OFFPAGE, SHX_FC_CARD, SHX_FC_TAPE, SHX_FC_COLLATE,
  SHX_FC_SORT, SHX_FC_EXTRACT, SHX_FC_MERGE, SHX_FC_DELAY, SHX_FC_DISPLAY, SHX_FC_STORAGE,
  /* callouts */
  SHX_ROUNDCALLOUT, SHX_OVALCALLOUT, SHX_CLOUDCALLOUT,
  /* stars and banners */
  SHX_STAR6, SHX_STAR7, SHX_STAR8, SHX_STAR10, SHX_STAR12, SHX_STAR16, SHX_STAR24, SHX_STAR32, SHX_SEAL1, SHX_SEAL2,
  SHX_ICON,                       /* an icon of the library (ObjFx.icon): written as a:custGeom */
  SHX_END
};
/* the categories of more shapes, for the Insert Shapes window: each one's first and end shape */
enum { SC_BASIC, SC_ARROWS, SC_FLOW, SC_CALLOUTS, SC_STARS, SC_N };
extern const char *const shapex_category[SC_N];
void shapex_range(int cat, int *first, int *end);
const char *shapex_label(int shape);      /* its English name ("Flowchart: Decision") */
const char *shapex_prst(int shape);       /* its a:prstGeom name, NULL if none of these */
int  shapex_from_prst(const char *prst);  /* the shape a name gives (an SH_ of doc.h too), -1 if neither */
/* its outline as layout.h's shape_points gives others' (0 if not one of these) */
u32  shapex_points(int shape, i32 w, i32 h, i32 *xy, u32 max, int *closed);

/* the icon library: Quill's 16 x 16 icon language (theme.c's L R F O E P, with Q a filled polygon
   and "cw" cutting out), one colour; drawn at any size */
enum { IC_CAT_N = 6 };
extern const char *const objicon_category[IC_CAT_N];
int  objicon_count(void);
const char *objicon_name(int i);         /* from 0 */
int  objicon_cat(int i);
const char *objicon_src(int i);
int  objicon_find(const char *name);     /* -1 if none */
/* icon i's coverage at size w x h pixels into mask (255 inside; drawn with max, so it adds) */
void objicon_mask(int i, u8 *mask, i32 w, i32 h);
/* icon i as DrawingML a:custGeom (its paths in a 1600 x 1600 space) */
void objicon_custgeom(Buf *o, int i);

/* ---- pictures */
/* whether the picture's colours are changed (corrections, colour, an effect) */
int  objfx_adjusted(const ObjFx *fx);
/* corrections, colour, saturation, temperature, sharpening and artistic effects on w x h RGB */
void objfx_adjust(u8 *rgb, u32 w, u32 h, const ObjFx *fx);
/* the background as Remove Background finds it: the four corners' colour (a picture's) */
u32  objfx_corner_colour(const Image *im);
/* whether an object needs objfx_render to be drawn: its adjustments, transparency, a fill or
   effect other than a plain colour, a picture's shape that isn't a rectangle, an icon */
int  objfx_rich(const Image *im);

/* an object drawn as RGBA: its turned box is obj_w x obj_h (of im's cx, cy, rot, flip) scaled to
   pixels by num / den (pixels per twip); the raster holds that and the room its shadow and glow
   need, its top left ox, oy pixels from the box's top left (<= 0). Straight alpha. Text is not
   drawn. 0 if it can't be (out of memory, too big) */
typedef struct { i32 w, h, ox, oy; u8 *px; } ObjRaster;
int  objfx_render(const Image *im, long num, long den, ObjRaster *r);
void objraster_free(ObjRaster *r);
/* a picture's part that shows (crop: cropped) at uw x uh pixels as RGB, malloc'd; NULL if it can't be */
u8  *objfx_picture_rgb(const Image *im, i32 uw, i32 uh, int crop);
/* how much an object's effects reach past its box on each side, twips */
i32  objfx_margin(const Image *im);

/* ---- .docx: pictures as files */
/* a picture's file as .docx needs it: its own bytes (key 0), or (key not 0) made afresh: an ILBM
   as PNG, a picture with effects Word can't redo drawn in (key a number that differs with them) */
u32  objfx_media_key(const Image *im);
u32  objfx_orig_key(const Image *im);    /* its unchanged file's (0, or 1 for an ILBM made PNG) */
u8  *objfx_media_bytes(const Image *im, u32 key, u32 *len, u8 *type);   /* malloc'd; NULL if it can't */
const char *objfx_ext(int type);         /* "png", "jpeg", "gif", "bmp" (ILBM: "png") */
/* the picture's file made smaller: cropped areas cut off (delcrop) and at most ppi pixels an inch
   at its shown size, as JPEG (photographs) or PNG; malloc'd, NULL if nothing would be gained */
u8  *objfx_compress(const Image *im, int ppi, int delcrop, u32 *len);

/* ---- .docx: reading (docx.c calls these from its drawing reader) */
typedef u32 (*ObjColourFn)(void *xml, void *ctx);   /* a DrawingML colour element (docx.c's dml_color) */
typedef struct {
  int inblip, ina14, inshd, inglow, ingrad, ings, inbody, inpic, incust, inlum;
  int gpos;                                  /* the gradient stop being read */
  int layerdone;
  char layer[40];                            /* a14:imgLayer's r:embed (the original picture) */
  char *raw;                                 /* the a14 extension as read */
  int hasfx;                                 /* a14 effects read */
  ObjFx a14;                                 /* what they set */
  i32 gangle;
  int pathset;
} ObjRead;
/* one START inside a shape or picture (x an Xml *, as docx.c's DShape reading has it): 1 if it was
   taken (and read to its END), 0 to let docx.c read it as before */
int  objfx_read(void *x, Image *im, ObjRead *s, ObjColourFn colour, void *ctx);
void objfx_read_end(void *x, Image *im, ObjRead *s);
/* after the shape is read: what was found finished off (0 if the original picture named by
   s->layer should be loaded instead of the blip, into im by the caller: then 1 is returned) */
int  objfx_read_finish(Image *im, ObjRead *s);
/* wp:docPr's name: an icon's */
void objfx_read_docpr(void *x, Image *im);

/* ---- .docx: writing */
/* a:blip's children (the r:embed attribute written already, the tag still open), then </a:blip>;
   orig: the media number of its unchanged picture for a14:imgLayer (0 none) */
void objfx_write_blip(Buf *o, const Image *im, int orig);
/* a picture's or shape's geometry (prst: docx.c's name for an SH_, or NULL) */
void objfx_write_geom(Buf *o, const Image *im, const char *prst);
/* a shape's fill (solid, gradient, picture with rIdImg media number m, or none) */
void objfx_write_fill(Buf *o, const Image *im, int m);
/* a:effectLst, if any effect is set */
void objfx_write_effects(Buf *o, const Image *im);
/* wps:bodyPr (text direction, anchoring, insets) */
void objfx_write_bodypr(Buf *o, const Image *im);
/* whether a shape's text box is a linked one that holds no text (wps:linkedTxbx), and that element */
int  objfx_linked_follower(const Image *im);
void objfx_write_linked(Buf *o, const Image *im);
/* the wps:txbx start tag (with its chain id when linked) */
void objfx_write_txbx(Buf *o, const Image *im);
/* an icon's wp:docPr name ("Icon: Name"); NULL for others */
const char *objfx_docpr_name(const Image *im, char *buf, size_t cap);

/* ---- text boxes (objtext.c; tb_layout and the chains in objtext.h) */
/* the object holding the text a linked text box shows (the first of its chain), or k itself */
int  tb_head(const Doc *d, int k);
/* the text width of a text box (its height for turned text), twips */
i32  tb_text_width(const Image *im, i32 cx, i32 cy);
/* the inset of a text box's text on side s (0 left, 1 top, 2 right, 3 bottom), twips */
i32  tb_inset(const Image *im, int s);

#endif
