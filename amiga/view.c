/*
 * view.c - Quill's page view, a MUI custom class (a subclass of Area).
 *
 * Pages are drawn as layout placed them: each box (a word in one format) at its own position,
 * in the Amiga bitmap font made from the TrueType font layout measured it with, so the screen
 * shows the page as it will print. Drawing goes to a back buffer (a bitmap like the window's,
 * with a layer of its own for clipping) and is copied into the window in one go: no flicker.
 * The caret is drawn over that with COMPLEMENT, so blinking is two XORs.
 *
 * Coordinates: the layout works in twips on a page; tx() and tw() convert at the zoom. View pixels
 * count from the view's top left (the back buffer's), window pixels add _mleft/_mtop, and
 * "from the top of everything" means view pixels before scrolly is taken off.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <exec/memory.h>
#include <graphics/gfxmacros.h>
#include <graphics/text.h>
#include <graphics/rpattr.h>
#include <devices/inputevent.h>
#include <devices/printer.h>
#include <graphics/view.h>
#include <intuition/intuitionbase.h>
#include <graphics/gfxbase.h>
#include <libraries/diskfont.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/diskfont.h>
#include <proto/keymap.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "quill.h"
#include "../core/table.h"
#include "../core/image.h"
#include "../core/toc.h"
#include "../core/chart.h"                 /* (WP15a) charts and SmartArt */
#include "wp_math.h"     /* WP14a: equations */
#include "../core/math.h"
#include "../core/spell.h"
#include "../core/field.h"
#include "../core/pages.h"
#include "../core/review.h"
#include "ids.h"
#include <libraries/gadtools.h>        /* NM_BARLABEL: a line between menu items */
#include "wphooks.h"
#include "wp_charpara.h"                /* (WP9, WP10) */
#include "theme.h"
#include "lang.h"
#include "wp_tables.h"                 /* WP21a/WP21b: the table tools and turned text */
#include "wp_objects.h"            /* WP17/WP18 */
#include "wp_wordart.h"            /* WP19 */
#include "../core/wordart.h"
#include "../core/objfx.h"
#include "wp_protect.h"                 /* (WP7) Restrict Editing */

/* (not in the public MUI 3.8 headers) */
#ifndef MUIM_GoActive
#define MUIM_GoActive   0x8042491aUL
#define MUIM_GoInactive 0x80422c0cUL
#endif
#define GAP 16                          /* pixels around and between pages */
#define ROT_HANDLE 16                   /* the turning handle this far above an object's frame */
#define NFONTS 48
#define NCOLS 24

struct Data {
  Doc *doc;
  Layout *lay;
  FontSet *fs;
  struct Toolbar *tb;
  LONG zoom, zoomset;                   /* zoomset 0: fit the width */
  LONG scrollx;                         /* pixels scrolled right, when the page is wider than the view */
  int hshown;                           /* the horizontal scroll bar is shown */
  LONG scrolly, doch;                   /* pixels */
  Pos caret, anchor;
  int pending; CharFmt pendfmt;         /* formatting for the next typing (bold toggled with no selection) */
  i32 wantx;                            /* page x (twips) up and down keep to */
  int caret_on, caret_shown, dragging, clicks;
  int hoverlink;                        /* the link under the pointer (Doc.links + 1), 0 none: shown on the status bar */
  ULONG secs, micros;
  LONG cx, cy, ch;                      /* the caret drawn: window pixels */
  int changed;                          /* the document changed (notified for the title) */
  int words_stale; ULONG words;
  int idle;                             /* blink ticks since the last edit: the word count waits for a pause */
  int stale;                            /* edited, not laid out yet: more keys were waiting */
  struct MUI_EventHandlerNode ehn;
  struct MUI_InputHandlerNode ihn;
  struct MUI_InputHandlerNode bgn;      /* laying out the rest of a long document */
  int handlers, bg_on;
  int intable;                          /* the caret is in a table (notified) */
  int outline_stale;                    /* edited since the outline was last notified */
  struct { char name[24]; UWORD ysize; struct TextFont *tf; } fc[NFONTS];
  int nfc;
  struct { ULONG rgb; LONG pen; } col[NCOLS];
  int ncol;
  LONG pen_page, pen_desk, pen_shadow, pen_text, pen_sel, pen_caret, pen_marks, pen_comment, pen_squiggle;
  ULONG pens_pagecolor;                 /* the page colour pen_page was got for (another document's, or changed: got again) */
  int marks;                            /* formatting marks shown */
  int vmode;                            /* VM_PRINT, VM_WEB, VM_DRAFT, VM_READ (quill.h) */
  int grid;                             /* gridlines over the text area */
  int tgrid;                            /* table gridlines: dotted where cells have no lines */
  int blk; u16 blk_t; int blk_r0, blk_r1, blk_c0, blk_c1;   /* the selection is a block of cells (as of the last draw) */
  int coldrag; u16 coldrag_t; int coldrag_edge; LONG coldrag_x0, coldrag_start;  /* dragging a table's column edge on the page */
  Fragment *frag;                       /* the last copy, with its formatting (pasted while the clipboard still has its text) */
  int odrag;                            /* dragging an object: 1-8 a handle (OH_), 9 moving it, 10 turning it */
  int odrag_a0, odrag_rot, odrag_snap;  /* turning: the pointer's direction from the centre at the start, the angle now; Shift held */
  LONG odrag_p[8];                      /* turning: the turned frame shown (XOR) */
  Pos msel[16]; int nmsel;              /* floating objects chosen together (Shift-click), for grouping */
  int odrag_k; FloatBox odrag_fb;       /* the object, and its box as it was (wrap WRAP_INLINE: in the text) */
  LONG odrag_mx, odrag_my, odrag_r[4]; int odrag_on;   /* where the drag started; the outline shown (XOR) */
  int darkpage;                         /* the page dark (View > Dark Mode): colours' lightness inverted */
  LONG pen_grid;
  UBYTE *wm_mask; LONG wm_w, wm_h, wm_zoom; char wm_text[64]; int wm_horiz;   /* the watermark, turned, as a 1-bit mask (made for a zoom) */
  Layout *plain;                        /* in web layout: the document on pages too (PDF export, page numbers), made when asked */
  int shown;                            /* between MUIM_Show and MUIM_Hide (hidden, it keeps its event handler) */
  struct { WORD x0, y0, x1, y1; int k; int kind; Pos a, b; } balloon[24]; int nballoon;   /* the balloons drawn (view coordinates): a comment's
                                           (kind RVI_COMMENT, k its index) or a change's (WP5, a..b what it is about) */
  u32 cver, cpara; int copen;           /* comments open at the start of paragraph cpara (as of document version cver) */
  int brush;                            /* the format painter: the next selection gets brushfmt */
  CharFmt brushfmt;
  struct BitMap *bm; struct Layer_Info *li; struct Layer *ly; LONG bw, bh;
  int back_ok;                          /* the buffer holds the last full drawing (scrolling may shift it) */
  Object *ctxstrip, *ctxmenu;           /* the right-button menu: its strip (kept), and the menu built in it for this click */
  Pos ctx_a, ctx_b;                     /* the misspelt word the menu was built for (ctx_a == ctx_b: none) */
  char ctx_word[128];                   /* that word (UTF-8) */
  char ctx_lab[8][64];                  /* its suggestions as the menu shows them (Latin-1) */
  char ctx_sug[8][128];                 /* and as they go into the text (UTF-8) */
  int ctx_n;
  ULONG ctx_pick;                        /* MUIA_Quill_CtxPick */
  int drawn_sel;                        /* there was a selection when the buffer was last drawn in full */
  int redraw_later;                     /* scrolled by blitting: draw it all once the user pauses (caret-dependent touches) */
  LONG band0, band1;                    /* draw_page draws only the lines between these view y's (a scroll's new strip) */
  Object *obj;                          /* the view itself (for the toolbar's notifications) */
  const PageSetup *pg;                  /* the page being drawn's page setup (sections may have their own) */
  LONG cellw, cellh; ULONG cellver; u32 cellpages;   /* the biggest page (each page's place in the rows is that wide) */
  i32 *rowt; u32 nrowt; int rowcols; ULONG rowver; u32 rowpages;   /* each row of pages' top, twips (gaps not counted): as high as its tallest */
  i32 wm_bw, wm_bh;                     /* the text area the watermark's mask was made for */
  char slabel[100];                     /* a story edited in the text's place: what it is, shown on its pages */
  struct QStoryReq sreq;                /* the story a double-click asked for */
  /* pictures: a cube of pens they are dithered to, and the last few as pens at their shown size */
  LONG cube[216]; int cubelv, ncube;
  struct { const u8 *data; LONG w, h; u16 rot; u8 flip; long crop; UBYTE *px; LONG *span; } pic[8];   /* span: each row's first and last pixel shown */
  int npic;
  /* WP22a: the picture watermark as pens at its shown size */
  UBYTE *wmp_px; LONG wmp_w, wmp_h; const u8 *wmp_data; int wmp_wash;
  void *wp12;                           /* WP12's own state (wp_find_view.h) */
  /* WP21a/WP21b: a table tool's stroke being dragged (Draw Table, Eraser, Border Painter): where it
     started and where the pointer is (window pixels); the caret lying across turned text, cw wide */
  int tdrag, tdrag_on; LONG tdrag_mx, tdrag_my, tdrag_x, tdrag_y;
  int cturn; LONG cw;
  void *wp11;                           /* WP11's own state (wp_outline_view.h) */
  void *wp25;                           /* WP25's own state (wp_windows_view.h) */
  void *wp14b;                          /* WP14b's own state (wp_equation_view.h): the equation being edited */
};

static struct MUI_CustomClass *cls;
extern FILE *qlog;
/* the time in ticks (1/50 s) since midnight, from the DOS clock: for timing drawing in the log */
static LONG ticks50(void) { struct DateStamp ds; DateStamp(&ds); return ds.ds_Minute * 3000 + ds.ds_Tick; }

/* ------------------------------------------------------------------ */
/* geometry: twips <-> pixels at the zoom (a percentage: at 100% a pixel is a point, 20 twips, so
   the page shows at 72 dpi; printing sets the zoom from the printer's dpi) */
static LONG tx(struct Data *d, i32 t) { return (LONG)((t * d->zoom) / 2000); }
/* and back, pixels to twips (a zoom of 0, before the first layout, taken as 100%) */
static i32 tw(struct Data *d, LONG px) { return (i32)((px * 2000) / (d->zoom ? d->zoom : 100)); }
/* pages in the rows each have the room of the biggest (sections may have pages of their own size),
   smaller ones centred across it */
static void cell_size(struct Data *d)
{
  Layout *L = d->lay;
  u32 k;
  if (d->cellw && L && d->cellver == d->doc->ver && d->cellpages == L->npages) return;
  d->cellw = d->doc->page.width; d->cellh = d->doc->page.height;
  if (L)
    for (k = 0; k < L->npages; k++) {
      const PageSetup *g = lay_page(L, k);
      if (g->width > d->cellw) d->cellw = g->width;
      if (g->height > d->cellh) d->cellh = g->height;
    }
  d->cellver = d->doc->ver; d->cellpages = L ? L->npages : 0;
}
/* the room each page has in a row (pixels), and page_of: page k's setup (the document's own until
   there is a layout) */
static LONG page_w(struct Data *d) { cell_size(d); return tx(d, d->cellw); }
static LONG page_h(struct Data *d) { cell_size(d); return tx(d, d->cellh); }
static const PageSetup *page_of(struct Data *d, u32 k) { return d->lay ? lay_page(d->lay, k) : &d->doc->page; }
/* the page setup of paragraph pi's section */
static const PageSetup *para_page(Doc *doc, u32 pi) { return sect_page(doc, doc_sect(doc, pi), doc_last_sect(doc, pi)); }
/* The view modes place pages differently (all in pixels, from the top left of the scrolled area):
   print layout and read mode put whole pages in rows (as many side by side as fit); draft runs
   the pages' text areas together, a dotted line where a page ends; web layout is one long page as
   wide as the window, no margins shown */
#define WEB_PAD 12                      /* web layout and draft: the gap left of the text */
static int paged(struct Data *d) { return d->vmode == VM_PRINT || d->vmode == VM_READ; }
/* draft: the height (pixels, at least 8) of each page's slice of the strip, its text area */
static LONG text_h(struct Data *d) { LONG h = tx(d, d->doc->page.height - d->doc->page.top - d->doc->page.bottom); return h > 8 ? h : 8; }
static LONG col_w(struct Data *d);
static int metric(struct Data *d);
/* (WP25, wp_windows_view.h) Side to Side, Multiple Pages, reflowed Read Mode */
static int ww_cols(struct Data *d);
static void ww_fit(struct Data *d, LONG w);
static LONG ww_left(struct Data *d, LONG w, LONG rw);
static void ww_pens(struct Data *d, Object *obj);
/* pages in a row */
static int cols(struct Data *d)
{
  LONG w, n;
  if (!paged(d) || !d->obj) return 1;
  { int wc = ww_cols(d); if (wc > 0) return wc; }      /* WP25 */
  w = _mwidth(d->obj) - col_w(d);
  n = (w - GAP) / (page_w(d) + GAP);
  if (d->lay && n > (LONG)d->lay->npages) n = (LONG)d->lay->npages;
  return n < 1 ? 1 : (int)n;
}
/* the rows' tops (made again when the pages or how many fit in a row change) */
static i32 row_twips(struct Data *d, u32 r)
{
  Layout *L = d->lay;
  int n = cols(d);
  u32 nr, i;
  if (!L) return 0;
  nr = (L->npages + (u32)n - 1) / (u32)n;
  if (!d->rowt || d->rowver != d->doc->ver || d->rowpages != L->npages || d->rowcols != n) {
    i32 *t = (i32 *)realloc(d->rowt, (nr + 1) * sizeof(i32));
    if (!t) return (i32)r * (d->cellh ? d->cellh : 16838);
    d->rowt = t; d->nrowt = nr; d->rowcols = n; d->rowver = d->doc->ver; d->rowpages = L->npages;
    t[0] = 0;
    for (i = 0; i < nr; i++) {
      i32 h = 0;
      u32 k;
      for (k = i * (u32)n; k < (i + 1) * (u32)n && k < L->npages; k++) if (lay_page(L, k)->height > h) h = lay_page(L, k)->height;
      t[i + 1] = t[i] + h;
    }
  }
  return d->rowt[r <= d->nrowt ? r : d->nrowt];
}
/* row r's top in pixels from the top of everything: the gaps above it and the rows' heights */
static LONG row_top(struct Data *d, u32 r) { return GAP + (LONG)r * GAP + tx(d, row_twips(d, r)); }
/* the row a point (y from the top of everything) is in */
static u32 row_at(struct Data *d, LONG y)
{
  u32 r = 0, nr;
  if (!d->lay) return 0;
  row_twips(d, 0);
  nr = d->nrowt;
  while (r + 1 < nr && y >= row_top(d, r + 1) - GAP / 2) r++;
  return r;
}
/* y of page k's top edge, in pixels from the top of everything (subtract scrolly for the view).
   Draft and web layout: a top that puts the text area where it belongs, the margin above it being
   left off the strip (so this may be negative) */
static LONG page_top(struct Data *d, u32 k)
{
  if (d->vmode == VM_DRAFT) return GAP + (LONG)k * (text_h(d) + 2) - tx(d, page_of(d, k)->top);
  if (d->vmode == VM_WEB) return WEB_PAD - tx(d, d->doc->page.top);
  return row_top(d, k / (u32)cols(d));
}
/* the height of everything */
static LONG doc_height(struct Data *d)
{
  Layout *L = d->lay;
  if (!L) return 0;
  if (d->vmode == VM_DRAFT) return 2 * GAP + (LONG)L->npages * (text_h(d) + 2);
  if (d->vmode == VM_WEB) {
    i32 bottom = d->doc->page.top;
    if (L->nplaced) {
      Placed *pc = &L->placed[L->nplaced - 1];
      bottom = pc->y + (pc->para < L->npl && pc->line < L->pl[pc->para].nlines ? L->pl[pc->para].lines[pc->line].height : 0);
    }
    if (L->nen) bottom += 4000;                       /* the endnotes after it */
    return 2 * WEB_PAD + tx(d, bottom - d->doc->page.top) + GAP;
  }
  return row_top(d, (L->npages + (u32)cols(d) - 1) / (u32)cols(d));
}
/* the column beside the page for the comments' balloons (when there are comments and they are shown) */
static LONG col_w(struct Data *d)
{
  if (!d->doc || !d->tb || !(d->tb->uibits & RBIT(RB_SHOWCOMMENTS)) || d->vmode != VM_PRINT || !rv_any_margin(d->doc)) return 0;   /* (WP5: changes' balloons too) */
  return d->obj && _mwidth(d->obj) > 500 ? 170 : 130;
}
/* the width of a row of pages (with the comments' column) */
static LONG row_w(struct Data *d)
{
  int n = cols(d);
  if (d->vmode == VM_WEB) return d->lay ? tx(d, d->lay->webw) + 2 * WEB_PAD : 0;
  if (d->vmode == VM_DRAFT) return tx(d, d->doc->page.width - d->doc->page.left - d->doc->page.right) + 2 * WEB_PAD;
  return n * page_w(d) + (n - 1) * GAP + col_w(d);
}
/* the left edge of the row of pages in a view w pixels wide: centred when it fits, else GAP in from
   the left less the sideways scroll. Keeps scrollx within what the width allows, as a side effect */
static LONG page_left(struct Data *d, LONG w)
{
  LONG pw = row_w(d), max = pw + 2 * GAP - w;
  { LONG c = ww_left(d, w, pw); if (c >= 0) { d->scrollx = 0; return c; } }     /* WP25: reflowed Read Mode, in the middle */
  if (paged(d) && w > pw + 2 * GAP) return (w - pw) / 2;
  if (!paged(d) && w >= pw) max = 0;
  if (d->scrollx > max) d->scrollx = max;
  if (d->scrollx < 0) d->scrollx = 0;
  return (paged(d) ? GAP : 0) - d->scrollx;
}
/* x of page k's left edge (draft and web layout: so the text starts just in from the left) */
static LONG page_x(struct Data *d, u32 k, LONG w)
{
  LONG l = page_left(d, w);
  if (!paged(d)) return l + WEB_PAD - tx(d, page_of(d, k)->left);
  return l + (LONG)(k % (u32)cols(d)) * (page_w(d) + GAP) + (page_w(d) - tx(d, page_of(d, k)->width)) / 2;
}

/* d->zoom for a view w pixels wide, from the zoom asked for (zoomset: above 0 a percentage, 0 the
   page's width, -1 a whole page, -2 two pages side by side) and the view mode; 25% to 400% */
static void fit_zoom(struct Data *d, LONG w)
{
  cell_size(d);
  w -= col_w(d);
  if (d->vmode == VM_DRAFT && d->zoomset == 0) {       /* draft at page width: the text's width in the view */
    i32 body = d->doc->page.width - d->doc->page.left - d->doc->page.right;
    d->zoom = (LONG)((w - 2 * WEB_PAD - 4) * 2000L / (body > 1440 ? body : 1440));
  }
  else if (d->vmode == VM_WEB || d->vmode == VM_DRAFT) d->zoom = d->zoomset > 0 ? d->zoomset : 100;
  else if (d->zoomset > 0 && d->vmode != VM_READ) d->zoom = d->zoomset;
  else if ((d->zoomset == -2 || d->vmode == VM_READ) && d->obj && _mheight(d->obj) > 4 * GAP) {
    /* two pages side by side (Multiple Pages, and read mode), as big as fits */
    LONG zw = (w - 3 * GAP) * 1000L / (d->cellw ? d->cellw : 11906);
    LONG zh = (_mheight(d->obj) - 2 * GAP) * 2000L / (d->cellh ? d->cellh : 16838);
    d->zoom = zw < zh ? zw : zh;
  }
  else if (d->zoomset < 0 && d->obj && _mheight(d->obj) > 4 * GAP) {       /* one page: its width and height in the view */
    LONG zw = (w - 2 * GAP) * 2000L / (d->cellw ? d->cellw : 11906);
    LONG zh = (_mheight(d->obj) - 2 * GAP) * 2000L / (d->cellh ? d->cellh : 16838);
    d->zoom = zw < zh ? zw : zh;
  }
  else if (w > 4 * GAP) d->zoom = (LONG)((w - 2 * GAP) * 2000L / (d->cellw ? d->cellw : 11906));
  if (d->zoom < 25) d->zoom = 25;
  if (d->zoom > 400) d->zoom = 400;
  ww_fit(d, w);                                  /* WP25: Multiple Pages, Side to Side, Read Mode's text size */
}

/* ------------------------------------------------------------------ */
/* fonts and colours */
/* the screen font family for a layout family: rendered from the same TrueType font where Quill
   ships one, so the screen matches the layout; layfam: the family the screen font was made from */
static const char *amiga_family(const char *layfam, const char **srcfam)
{
  if (strstr(layfam, "Carlito")) { *srcfam = "Carlito"; return "QuillSansC"; }
  if (strstr(layfam, "Caladea")) { *srcfam = "Caladea"; return "QuillSerifC"; }
  if (strstr(layfam, "Mono") || strstr(layfam, "Courier")) { *srcfam = "Liberation Mono"; return "QuillMono"; }
  if ((strstr(layfam, "Serif") && !strstr(layfam, "Sans")) || strstr(layfam, "Times") || strstr(layfam, "Georgia")) { *srcfam = "Liberation Serif"; return "QuillSerif"; }
  *srcfam = "Liberation Sans";
  return "QuillSans";
}
/* the Amiga font to draw format f in at this zoom, from a small cache (emptied when full, and by
   close_fonts when the zoom changes). An installed TrueType face is rendered at the exact size; else
   one of the bitmap fonts made from Quill's own TrueType fonts, its height chosen so its line box
   matches the layout's. NULL if no font opens: callers fall back to the system font */
static struct TextFont *screen_font(struct Data *d, const CharFmt *f)
{
  FaceSize *z = lay_face(d->lay, f), *sz;
  struct TextAttr ta;
  char name[40];
  const char *src, *fam = amiga_family(z ? z->face->family : "Liberation Serif", &src);
  LONG em;
  UWORD ys;
  int i;
  if (z && z->face->installed) {
    /* an installed TrueType font: at the size exactly (in quarter pixels) */
    LONG ppem64 = ((LONG)z->size * d->zoom * 8) / 25;
    sprintf(name, "@%lx", (unsigned long)z->face);
    ys = (UWORD)((ppem64 + 8) / 16);
    for (i = 0; i < d->nfc; i++) if (d->fc[i].ysize == ys && !strcmp(d->fc[i].name, name)) return d->fc[i].tf;
    if (d->nfc >= NFONTS) {
      for (i = 0; i < d->nfc; i++) if (d->fc[i].tf) theme_close_font(d->fc[i].tf);
      d->nfc = 0;
    }
    i = d->nfc++;
    strcpy(d->fc[i].name, name);
    d->fc[i].ysize = ys;
    d->fc[i].tf = theme_open_face(z->face, ppem64);
    return d->fc[i].tf;
  }
  /* rounded down: a screen font no wider than the layout keeps the spaces between words */
  em = ((LONG)(z ? z->size : f->size) * d->zoom) / 200;
  if (em < 6) em = 6;
  /* the Amiga font's height is its own line box (as mkamigafonts.py made it) */
  sz = fonts_size(d->fs, src, f->flags & CF_BOLD, f->flags & CF_ITALIC, 24);
  if (!strcmp(src, "Caladea")) ys = (UWORD)((em * 1300 + 500) / 1000);    /* its own metrics (layout uses Cambria's) */
  else if (sz) ys = (UWORD)((em * (sz->face->ascent + sz->face->descent) + sz->face->upm / 2) / sz->face->upm);
  else ys = (UWORD)(em + em / 8);
  sprintf(name, "%s%s%s.font", fam, f->flags & CF_BOLD ? (f->flags & CF_ITALIC ? "BI" : "B") : "", (f->flags & (CF_BOLD | CF_ITALIC)) == CF_ITALIC ? "I" : "");
  for (i = 0; i < d->nfc; i++) if (d->fc[i].ysize == ys && !strcmp(d->fc[i].name, name)) return d->fc[i].tf;
  if (d->nfc >= NFONTS) {                     /* full: start again */
    for (i = 0; i < d->nfc; i++) if (d->fc[i].tf) theme_close_font(d->fc[i].tf);
    d->nfc = 0;
  }
  ta.ta_Name = (STRPTR)name; ta.ta_YSize = ys; ta.ta_Style = 0; ta.ta_Flags = 0;
  i = d->nfc++;
  strcpy(d->fc[i].name, name);
  d->fc[i].ysize = ys;
  (void)ta;
  d->fc[i].tf = theme_open_font(name, ys);
  return d->fc[i].tf;
}
/* close all the cached screen fonts: after a zoom or view mode change their sizes are wrong, and
   around printing (another resolution) */
static void close_fonts(struct Data *d)
{
  int i;
  for (i = 0; i < d->nfc; i++) if (d->fc[i].tf) theme_close_font(d->fc[i].tf);
  d->nfc = 0;
}
/* the dark page (as WordCraft's): a colour's lightness inverted, keeping its hue, black lifted to
   the paper's grey; a text background dimmed instead, so light text on it can be read */
#define DARK_PAPER 0x33
/* a text or line colour (0xRRGGBB) as the page shows it: unchanged unless the page is dark */
static ULONG ink(struct Data *d, ULONG c)
{
  LONG r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255, mx, mn, dd;
  if (!d->darkpage) return c;
  mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
  mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
  dd = 255 - mx - mn;
#define INV(v) ((v) + dd < 0 ? 0 : (v) + dd > 255 ? 255 : (v) + dd)
#define LIFT(v) (DARK_PAPER + (v) * (255 - DARK_PAPER) / 255)
  r = LIFT(INV(r)); g = LIFT(INV(g)); b = LIFT(INV(b));
#undef INV
#undef LIFT
  return ((ULONG)r << 16) | ((ULONG)g << 8) | (ULONG)b;
}
/* a background colour (highlight, shading) on the dark page: each channel squeezed into 0x10-0x4A */
static ULONG ink_bg(struct Data *d, ULONG c)
{
  if (!d->darkpage) return c;
  return ((0x10 + ((c >> 16) & 255) * 0x3A / 255) << 16) | ((0x10 + ((c >> 8) & 255) * 0x3A / 255) << 8) | (0x10 + (c & 255) * 0x3A / 255);
}
/* a pen for a text colour (COLOR_AUTO or 0: the text pen). On a screen with a palette the pens are
   shared with other programs (ObtainBestPen finds the nearest free or matching one), so each colour
   is obtained once and cached, NCOLS at most (then the text pen); free_pens gives them back */
static LONG pen_for(struct Data *d, Object *obj, ULONG rgb)
{
  int i;
  if (rgb == COLOR_AUTO || rgb == 0) return d->pen_text;
  rgb = ink(d, rgb);
  if (!rgb) rgb = 0x010101;
  for (i = 0; i < d->ncol; i++) if (d->col[i].rgb == rgb) return d->col[i].pen;
  if (d->ncol >= NCOLS) return d->pen_text;
  d->col[d->ncol].rgb = rgb;
  d->col[d->ncol].pen = ObtainBestPen(_screen(obj)->ViewPort.ColorMap, ((rgb >> 16) & 255) * 0x01010101UL, ((rgb >> 8) & 255) * 0x01010101UL,
                                      (rgb & 255) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
  return d->col[d->ncol++].pen;
}
/* a background colour's pen: on the dark page, dimmed (cached with the text colours, by a key of its own) */
static LONG pen_bg(struct Data *d, Object *obj, ULONG rgb)
{
  int i;
  ULONG c = d->darkpage ? ink_bg(d, rgb) : rgb, key = c | 0x80000000UL;
  if (!c) c = 0x010101;
  for (i = 0; i < d->ncol; i++) if (d->col[i].rgb == key) return d->col[i].pen;
  if (d->ncol >= NCOLS) return d->pen_page;
  d->col[d->ncol].rgb = key;
  d->col[d->ncol].pen = ObtainBestPen(_screen(obj)->ViewPort.ColorMap, ((c >> 16) & 255) * 0x01010101UL, ((c >> 8) & 255) * 0x01010101UL,
                                      (c & 255) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
  return d->col[d->ncol++].pen;
}
/* the screen's nearest pen to r, g, b (0-255 each); ReleasePen it when done. ObtainBestPen takes
   32-bit colour components, hence the * 0x01010101 */
static LONG best(Object *obj, ULONG r, ULONG g, ULONG b)
{
  return ObtainBestPen(_screen(obj)->ViewPort.ColorMap, r * 0x01010101UL, g * 0x01010101UL, b * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
}
/* the view's own pens (desk, page, text, selection, marks...) for the theme and the dark page: at
   Setup, and again (after free_pens) when the dark page is turned on or off. Empties the colour cache */
static void get_pens(struct Data *d, Object *obj)
{
  int dk = d->darkpage;
  /* the desk is the interface's canvas (light or dark theme); the page's own colours follow the dark page */
  d->pen_desk = theme_is_dark() ? best(obj, 0x14, 0x14, 0x14) : best(obj, 230, 230, 230);
  d->pen_shadow = theme_is_dark() ? best(obj, 0, 0, 0) : best(obj, 190, 190, 190);
  if (d->doc && d->doc->pagecolor && !dk) {
    /* the document's page colour (Design > Page Color) */
    ULONG c = d->doc->pagecolor;
    d->pen_page = best(obj, (c >> 16) & 255, (c >> 8) & 255, c & 255);
  } else d->pen_page = dk ? best(obj, DARK_PAPER, DARK_PAPER, DARK_PAPER) : best(obj, 255, 255, 255);
  d->pens_pagecolor = d->doc ? d->doc->pagecolor : 0;
  d->pen_marks = dk ? best(obj, 140, 160, 230) : best(obj, 110, 130, 200);
  d->pen_comment = dk ? best(obj, 0x4A, 0x44, 0x2A) : best(obj, 250, 232, 170);     /* a comment's range */
  d->pen_squiggle = best(obj, 220, 30, 30);
  d->pen_text = dk ? best(obj, 255, 255, 255) : best(obj, 0, 0, 0);
  d->pen_sel = dk ? best(obj, 0x4A, 0x5A, 0x8E) : best(obj, 170, 200, 240);
  d->pen_grid = dk ? best(obj, 0x45, 0x4C, 0x5C) : best(obj, 0xD5, 0xDD, 0xEE);
  d->ncol = 0;
  ww_pens(d, obj);                               /* WP25: Read Mode's sepia page */
}
/* drop the cached pictures: their pixels are pen numbers, wrong once the pens change */
static void free_pics(struct Data *d)
{
  int i;
  for (i = 0; i < d->npic; i++) { FreeVec(d->pic[i].px); FreeVec(d->pic[i].span); }
  d->npic = 0;
  if (d->wmp_px) { FreeVec(d->wmp_px); d->wmp_px = NULL; }      /* (WP22a) */
}
/* give back every pen obtained (get_pens, pen_for, pen_bg, the cube) and the pictures made with them.
   Needs the screen, so only between Setup and Cleanup */
static void free_pens(struct Data *d, Object *obj)
{
  struct ColorMap *cm = _screen(obj)->ViewPort.ColorMap;
  int i;
  ReleasePen(cm, d->pen_page); ReleasePen(cm, d->pen_desk); ReleasePen(cm, d->pen_shadow); ReleasePen(cm, d->pen_marks); ReleasePen(cm, d->pen_comment); ReleasePen(cm, d->pen_squiggle);
  ReleasePen(cm, d->pen_text); ReleasePen(cm, d->pen_sel); ReleasePen(cm, d->pen_grid);
  for (i = 0; i < d->ncube; i++) ReleasePen(cm, d->cube[i]);
  d->ncube = 0;
  wp18_flush();                         /* (WP18: drawings made with those pens) */
  wp19_flush();                         /* (WP19) */
  free_pics(d);
  for (i = 0; i < d->ncol; i++) ReleasePen(cm, d->col[i].pen);
  d->ncol = 0;
}

/* ------------------------------------------------------------------ */
/* the back buffer */
/* free the back buffer. WaitBlit first: the blitter works on its own, and may still be copying it */
static void back_free(struct Data *d)
{
  if (d->ly) { DeleteLayer(0, d->ly); d->ly = NULL; }
  if (d->li) { DisposeLayerInfo(d->li); d->li = NULL; }
  if (d->bm) { WaitBlit(); FreeBitMap(d->bm); d->bm = NULL; }
  d->bw = d->bh = 0;
  d->back_ok = 0;
}
/* the back buffer's RastPort, w x h pixels, made (or made again at a new size) like the window's
   bitmap, so ClipBlit copies it fast (on a graphics card, in its memory). It is given a layer rather
   than drawn as a bare bitmap because graphics.library only clips drawing through a layer: without
   one, anything drawn past the edge writes over memory. The layer also takes the clip region a
   blitted scroll installs. NULL without memory: MUIM_Draw then draws straight into the window */
static struct RastPort *back_rp(struct Data *d, Object *obj, LONG w, LONG h)
{
  struct BitMap *wbm = _rp(obj)->BitMap;
  if (d->ly && d->bw == w && d->bh == h) return d->ly->rp;
  back_free(d);
  if (w <= 0 || h <= 0) return NULL;
  if (!(d->bm = AllocBitMap((ULONG)w, (ULONG)h, GetBitMapAttr(wbm, BMA_DEPTH), 0, wbm))) return NULL;
  if (!(d->li = NewLayerInfo()) || !(d->ly = CreateUpfrontLayer(d->li, d->bm, 0, 0, w - 1, h - 1, LAYERSIMPLE, NULL))) { back_free(d); return NULL; }
  /* AAText finds the screen of a layer's text by its window: the buffer is drawn in the window's
     colours (nothing else looks at the window of a layer that isn't the screen's) */
  if (theme_aatext()) d->ly->Window = (APTR)_window(obj);
  d->bw = w; d->bh = h;
  return d->ly->rp;
}

/* ------------------------------------------------------------------ */
/* drawing */
/* the selection runs from anchor (where it began) to caret (the end that moves): sel_lo and sel_hi
   give them in document order, has_sel whether anything is selected */
static Pos sel_lo(struct Data *d) { return pos_cmp(d->anchor, d->caret) <= 0 ? d->anchor : d->caret; }
static Pos sel_hi(struct Data *d) { return pos_cmp(d->anchor, d->caret) <= 0 ? d->caret : d->anchor; }
static int has_sel(struct Data *d) { return pos_cmp(d->anchor, d->caret) != 0; }
static int sel_object(struct Data *d, Pos *at);
/* what the work packages' hooks are told (wphooks.h WpCtx): the document, caret, selection, table,
   view mode, the object selected, and where (WPC_) */
static struct Data *ctx_view;          /* (WP11) the view the last WpCtx was made for */
static int shown_mode(struct Data *d);  /* (WP11, WP25) the view mode as the outside sees it: VM_OUTLINE, VM_READ reflowed */
static void wp_ctx(struct Data *d, WpCtx *c, int where)
{
  memset(c, 0, sizeof *c);
  c->doc = d->doc; c->caret = d->caret; c->anchor = d->anchor; c->hassel = has_sel(d);
  c->intable = d->doc && d->caret.para < d->doc->np && in_table(&d->doc->p[d->caret.para]->pf);
  c->vmode = shown_mode(d); c->obj = d->doc ? sel_object(d, NULL) : -1; c->where = where;
  ctx_view = d;
}

/* x (twips, from the line's start) of byte off on a line */
static i32 line_x(Layout *L, ParaLay *pl, Line *ln, u32 off)
{
  u32 k;
  i32 x = 0;
  for (k = ln->box0; k < ln->box0 + ln->nbox; k++) {
    Box *b = &pl->boxes[k];
    if (off >= b->off && off <= b->off + b->len) return lay_text_x(L, b, pl->p->t, off);
    x = b->x + b->w;
  }
  return x;
}
#include "wp_find_view.h"               /* WP12: block and similar selections, Extend Selection, Select Objects, Highlight All */
#include "wp_outline_view.h"            /* WP11: Outline view and the outline commands */
#include "wp_windows_view.h"            /* WP25: views shared by windows, Split, page movement, Read Mode's options */
static int shown_mode(struct Data *d) { return ov_on(d) ? VM_OUTLINE : ww_reading(d) ? VM_READ : d->vmode; }
#include "wp_proof.h"                  /* WP8, WP31: grammar marks, AutoCorrect while typing, languages */
#include "../core/proflang.h"
/* an underline or strike line of a kind (UL_) from x, w pixels wide, at y */
static void deco_line(struct RastPort *rp, int kind, LONG x, LONG y, LONG w)
{
  LONG i;
  if (w <= 0) return;
  switch (kind) {
  case UL_DOUBLE: Move(rp, x, y - 1); Draw(rp, x + w - 1, y - 1); Move(rp, x, y + 1); Draw(rp, x + w - 1, y + 1); break;
  case UL_THICK: RectFill(rp, x, y, x + w - 1, y + 1); break;
  case UL_DOTTED: for (i = 0; i < w; i += 2) WritePixel(rp, x + i, y); break;
  case UL_DASHED: for (i = 0; i < w; i += 6) { Move(rp, x + i, y); Draw(rp, x + (i + 3 < w ? i + 3 : w - 1), y); } break;
  case UL_WAVE:
    Move(rp, x, y);
    for (i = 2; i < w; i += 2) Draw(rp, x + i, y + ((i / 2) & 1 ? 1 : -1));
    break;
  default: Move(rp, x, y); Draw(rp, x + w - 1, y);
  }
}
/* a lower-case Latin-1 letter, whose capital is 32 below it (not the division sign, nor y with
   diaeresis, whose capital isn't in Latin-1) */
static int lower_l1(u8 c) { return (c >= 'a' && c <= 'z') || (c >= 0xE0 && c != 0xF7 && c != 0xFF); }
/* box b's text (t: the paragraph's UTF-8, shown as Latin-1 for the Amiga font) at x, on baseline base,
   in its colour and screen font: capitals, small capitals, raised and lowered text, underlines and
   strikes. Expects JAM1 (only the letters' pixels set, what's behind showing through) */
static void get_cube(struct Data *d, Object *obj);     /* (WP19) */
static void draw_text(struct Data *d, Object *obj, struct RastPort *rp, const Box *b, const char *t, LONG x, LONG base)
{
  char buf[512];
  struct TextFont *tf = screen_font(d, &b->f), *tfs = NULL;
  size_t n;
  ULONG soft = 0;
  int small = (b->f.fx & FX_SMALLCAPS) && !(b->f.flags & CF_CAPS), pass, npass = 1, special = 0;
  u8 low[512], skip[512];
  LONG dx[3] = { 0, 0, 0 }, dy[3] = { 0, 0, 0 };
  ULONG pens[3];
  if (WA_RICH(&b->f)) {                 /* (WP19) WordArt: drawn from the letters' outlines with its effects */
    get_cube(d, obj);
    if (wp19_draw_run(rp, d->lay, b, t, x, base, d->zoom, d->cube, d->cubelv, d->band0, d->band1)) return;
  }
  {
    /* (WP10) an optional hyphen isn't drawn (with formatting marks: a not sign), a nonbreaking hyphen
       is a hyphen, a nonbreaking space shows as a degree sign with the marks */
    u32 o = b->off, end = b->off + b->len;
    size_t i = 0;
    while (o < end && i < sizeof buf - 1) {
      int used;
      u32 c = utf8_get(t + o, end - o, &used);
      skip[i] = 0;
      if (c == 0xAD) { skip[i] = (u8)!d->marks; c = 0xAC; special = 1; }
      else if (c == 0x2011) c = '-';
      else if (c == 0xA0 && d->marks) { c = 0xB0; special = 1; }
      buf[i++] = (char)(c < 256 ? c : latin1_of(c) >= 0 ? latin1_of(c) : '?');
      o += (u32)used;
    }
    n = i;
  }
  u8 rvm = 0;
  ULONG tpen;
  {
    size_t i;
    for (i = 0; i < n; i++) {
      low[i] = (u8)(small && lower_l1((u8)buf[i]));
      if (((b->f.flags & CF_CAPS) || small) && lower_l1((u8)buf[i])) buf[i] = (char)(buf[i] - 32);
    }
  }
  if (small) { CharFmt sf = b->f; sf.size = (u16)((b->f.size * 4 + 2) / 5); tfs = screen_font(d, &sf); }
  if (tf) {
    /* an installed family without a bold or italic face: made bold or slanted */
    FaceSize *z = lay_face(d->lay, &b->f);
    SetFont(rp, tf);
    if (z && z->face->installed) soft = (b->f.flags & CF_BOLD && !z->face->bold ? FSF_BOLD : 0) | (b->f.flags & CF_ITALIC && !z->face->italic ? FSF_ITALIC : 0);
  } else {
    /* no Quill fonts: the system font, with algorithmic styles */
    SetFont(rp, ((struct GfxBase *)GfxBase)->DefaultFont);
    soft = (b->f.flags & CF_BOLD ? FSF_BOLD : 0) | (b->f.flags & CF_ITALIC ? FSF_ITALIC : 0);
  }
  {
    /* (WP5) a tracked change in its author's colour (or the Track Changes Options'), with its mark */
    u32 rc;
    rv_ink(d->doc, &b->f, &rc, &rvm);
    if (rvm == RVO_BOLD) soft |= FSF_BOLD;
    if (rvm == RVO_ITALIC) soft |= FSF_ITALIC;
    tpen = rc != COLOR_AUTO ? pen_for(d, obj, rc) : pen_for(d, obj, b->f.color);
  }
  SetSoftStyle(rp, soft, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
  pens[0] = tpen;
  if (b->f.flags & CF_SUPER) base -= tx(d, b->f.size * 10 / 3);
  else if (b->f.flags & CF_SUB) base += tx(d, b->f.size * 10 / 6);
  base -= tx(d, b->f.position * 10);
  /* (WP9) the effects, as passes drawn one over the other: shadow a grey copy down and right; emboss
     (engrave) white letters with a grey copy down and right (up and left); outline the letters drawn
     round about in the text's colour, then white inside */
  if (b->f.fx2 & FX2_RELIEF) {
    ULONG grey = pen_for(d, obj, 0x808080), white = pen_for(d, obj, 0xFFFFFF);
    LONG o = 1 + d->zoom / 200;
    npass = 0;
    if (b->f.fx2 & (FX2_SHADOW | FX2_EMBOSS | FX2_ENGRAVE)) {
      int up = (b->f.fx2 & FX2_ENGRAVE) && !(b->f.fx2 & FX2_SHADOW);
      dx[npass] = up ? -o : o; dy[npass] = up ? -o : o; pens[npass] = grey; npass++;
    }
    if (b->f.fx2 & FX2_OUTLINE) {
      /* the letters one pixel round about (as four passes folded into one: see below), then white inside */
      dx[npass] = 0; dy[npass] = 0; pens[npass] = 0xFFFFFFFFUL; npass++;
      dx[npass] = 0; dy[npass] = 0; pens[npass] = white; npass++;
    } else {
      dx[npass] = 0; dy[npass] = 0;
      pens[npass] = (b->f.fx2 & (FX2_EMBOSS | FX2_ENGRAVE)) ? white : tpen;
      npass++;
    }
  }
  for (pass = 0; pass < npass; pass++) {
    LONG want = tx(d, b->w), got, slack = want / 24 + 1;
    int each = small || b->f.spacing || (b->f.scale && b->f.scale != 100) || special, k4, rounds = pens[pass] == 0xFFFFFFFFUL ? 4 : 1;
    for (k4 = 0; k4 < rounds; k4++) {
      /* (an outline's round: the letters in the text's colour a pixel to each side) */
      LONG ox = x + dx[pass] + (rounds == 4 ? (k4 == 0 ? -1 : k4 == 1 ? 1 : 0) : 0), ob = base + dy[pass] + (rounds == 4 ? (k4 == 2 ? -1 : k4 == 3 ? 1 : 0) : 0);
      SetAPen(rp, rounds == 4 ? tpen : pens[pass]);
      got = TextLength(rp, (STRPTR)buf, (ULONG)n);
      if (b->kind != BX_TEXT || (!each && (n != b->len || (got - want <= slack && want - got <= slack)))) {
        Move(rp, ox, ob);
        Text(rp, (STRPTR)buf, (ULONG)n);
      } else {
        /* the screen font is wider or narrower than the layout measured (or the text is spaced, scaled
           or in small capitals): each character at its laid-out place, so words never run into each other */
        u32 o = b->off, end = b->off + b->len;
        size_t k = 0;
        while (o < end && k < n) {
          u32 nx = (u32)utf8_next(t, end, o);
          if (!skip[k]) {
            int mark = d->marks && ((u8)buf[k] == 0xAC || (u8)buf[k] == 0xB0) && special;
            if (small) SetFont(rp, low[k] && tfs ? tfs : tf ? tf : ((struct GfxBase *)GfxBase)->DefaultFont);
            if (mark) SetAPen(rp, d->pen_marks);
            Move(rp, ox + tx(d, lay_text_x(d->lay, b, t, o) - b->x), ob);
            Text(rp, (STRPTR)buf + k, 1);
            if (mark) SetAPen(rp, rounds == 4 ? tpen : pens[pass]);
          }
          o = nx; k++;
        }
      }
    }
  }
  SetAPen(rp, tpen);
  if ((b->f.flags & (CF_UNDERLINE | CF_STRIKE)) || rvm || (b->f.fx & FX_DSTRIKE)) {
    /* a tracked change's mark (underlined, struck through ... as the Track Changes Options say), in its colour */
    LONG w = tx(d, b->w), sy = base - tx(d, b->f.size * 10 / 4);
    if (b->f.ulcolor && (b->f.flags & CF_UNDERLINE)) SetAPen(rp, pen_for(d, obj, b->f.ulcolor & 0xFFFFFF));   /* (WP9) */
    if (b->f.flags & CF_UNDERLINE) deco_line(rp, b->f.ul, x, base + 1 + d->zoom / 150, w);
    SetAPen(rp, tpen);
    if (rvm == RVO_UNDERLINE || rvm == RVO_DUNDERLINE) deco_line(rp, rvm == RVO_DUNDERLINE ? UL_DOUBLE : UL_SINGLE, x, base + 1 + d->zoom / 150 + (rvm == RVO_DUNDERLINE), w);
    if ((b->f.flags & CF_STRIKE) || rvm == RVO_STRIKE) deco_line(rp, UL_SINGLE, x, sy, w);
    if ((b->f.fx & FX_DSTRIKE) || rvm == RVO_DSTRIKE) deco_line(rp, UL_DOUBLE, x, sy, w);
  }
  if ((b->f.fx2 & FX2_HIDDEN) && d->marks) {
    /* (WP9) hidden text shown with the formatting marks: a dotted line under it */
    SetAPen(rp, d->pen_marks);
    deco_line(rp, UL_DOTTED, x, base + 2 + d->zoom / 100, tx(d, b->w));
  }
}
/* (WP9) enclosed characters: the circle, square, triangle or diamond round the box, the text in
   the middle (at seven tenths of its size unless the symbol is enlarged) */
static void draw_enclosed(struct Data *d, Object *obj, struct RastPort *rp, const Box *b, const char *t, LONG x, LONG base)
{
  Box tb = *b;
  struct TextFont *tf;
  char buf[64];
  size_t n = utf8_to_latin1(t + b->off, b->len, buf, sizeof buf);
  LONG w = tx(d, b->w), sz = tx(d, b->f.size * 10), h = sz * (b->f.fx2 & FX2_ENLARGE ? 3 : 2) / 2 + sz / 10, r, cx = x + w / 2, cy = base - sz * 3 / 10, tw;
  int shape = (int)ENCLOSURE(&b->f);
  tb.f.fx2 &= (u16)~(FX2_ENCLOSE | FX2_ENLARGE);
  if (!(b->f.fx2 & FX2_ENLARGE)) tb.f.size = (u16)((b->f.size * 7 + 5) / 10);
  tf = screen_font(d, &tb.f);
  SetFont(rp, tf ? tf : ((struct GfxBase *)GfxBase)->DefaultFont);
  tw = TextLength(rp, (STRPTR)buf, (ULONG)n);
  tb.kind = BX_TEXT; tb.w = tw * 2000 / (d->zoom ? d->zoom : 100);       /* (drawn as one piece, its own width) */
  {
    /* draw_text with the box's text, centred */
    Box db = tb;
    db.f.spacing = 0; db.f.scale = 0;
    draw_text(d, obj, rp, &db, t, cx - tw / 2, base - (b->f.fx2 & FX2_ENLARGE ? 0 : sz / 12));
  }
  r = (w < h ? w : h) / 2;
  if (r < 3) r = 3;
  SetAPen(rp, pen_for(d, obj, b->f.color));
  if (shape == EN_CIRCLE) DrawEllipse(rp, cx, cy, r, r);
  else if (shape == EN_SQUARE) { Move(rp, cx - r, cy - r); Draw(rp, cx + r, cy - r); Draw(rp, cx + r, cy + r); Draw(rp, cx - r, cy + r); Draw(rp, cx - r, cy - r); }
  else if (shape == EN_TRIANGLE) { Move(rp, cx - r * 6 / 5, cy + r * 4 / 5); Draw(rp, cx + r * 6 / 5, cy + r * 4 / 5); Draw(rp, cx, cy - r * 6 / 5); Draw(rp, cx - r * 6 / 5, cy + r * 4 / 5); }
  else { Move(rp, cx - r * 6 / 5, cy); Draw(rp, cx, cy - r * 6 / 5); Draw(rp, cx + r * 6 / 5, cy); Draw(rp, cx, cy + r * 6 / 5); Draw(rp, cx - r * 6 / 5, cy); }
}
/* ---- pictures */
static const UBYTE bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
/* the cube of pens pictures are dithered to (with the 4x4 ordered dither above): 6 levels a channel
   on an 8-bit screen, 3 or 2 on shallower ones. Obtained once, kept until free_pens */
static void get_cube(struct Data *d, Object *obj)
{
  LONG depth = GetBitMapAttr(_screen(obj)->RastPort.BitMap, BMA_DEPTH);
  int r, g, b, lv;
  if (d->ncube) return;
  lv = depth >= 8 ? 6 : depth >= 5 ? 3 : 2;            /* 216, 27 or 8 colours */
  d->cubelv = lv;
  for (r = 0; r < lv; r++) for (g = 0; g < lv; g++) for (b = 0; b < lv; b++)
    d->cube[d->ncube++] = best(obj, (ULONG)(r * 255 / (lv - 1)), (ULONG)(g * 255 / (lv - 1)), (ULONG)(b * 255 / (lv - 1)));
}
/* a picture (im: its settings, cx x cy pixels unturned) as pens in its turned box (*w x *h): from the
   cache, else decoded (cropped, turned and flipped) and dithered; *span: each row's first and last
   pixel inside it. NULL if it can't be */
static UBYTE *pic_pens(struct Data *d, Object *obj, const Image *im, LONG uw, LONG uh, LONG *w, LONG *h, LONG **span)
{
  UBYTE *rgb, *px;
  LONG *sp, x, y, fw, fh, cx0, cy0;
  long cl = im->crop[0], ct = im->crop[1], cr = im->crop[2], cb = im->crop[3], c = icos(im->rot), sn = isin(im->rot);
  int i, lv;
  Image t = *im;
  t.cx = uw; t.cy = uh;
  *w = obj_w(&t); *h = obj_h(&t);
  if (*w < 1) *w = 1;
  if (*h < 1) *h = 1;
  for (i = 0; i < d->npic; i++)
    if (d->pic[i].data == im->data && d->pic[i].w == uw && d->pic[i].h == uh && d->pic[i].rot == im->rot && d->pic[i].flip == im->flip && d->pic[i].crop == cl + 3 * ct + 7 * cr + 11 * cb) {
      *span = d->pic[i].span;
      return d->pic[i].px;
    }
  if (uw <= 0 || uh <= 0 || *w * *h > 4000000 || !im->data) return NULL;
  get_cube(d, obj);
  lv = d->cubelv;
  /* the whole picture at the size that makes its cropped part uw x uh */
  if (cl + cr >= 99000) cl = cr = 0;
  if (ct + cb >= 99000) ct = cb = 0;
  fw = (LONG)(uw * 100000L / (100000L - cl - cr)); fh = (LONG)(uh * 100000L / (100000L - ct - cb));
  if (fw < uw) fw = uw;
  if (fh < uh) fh = uh;
  if (fw * fh > 4000000) return NULL;
  cx0 = (LONG)(fw * cl / 100000L); cy0 = (LONG)(fh * ct / 100000L);
  if (cx0 + uw > fw) cx0 = fw - uw;
  if (cy0 + uh > fh) cy0 = fh - uh;
  if (!(rgb = img_decode(im->data, im->len, (u32)fw, (u32)fh))) return NULL;
  px = (UBYTE *)AllocVec((ULONG)(*w * *h), MEMF_ANY);
  sp = (LONG *)AllocVec((ULONG)(*h * 2 * sizeof(LONG)), MEMF_ANY);
  if (!px || !sp) { if (px) FreeVec(px); if (sp) FreeVec(sp); free(rgb); return NULL; }
  for (y = 0; y < *h; y++) {
    /* the unturned picture's pixel each one shows: across the row in steps of (cos, -sin) */
    long dy = 2 * y + 1 - *h, dx = 1 - *w;
    long u = (long)uw * TRIG_ONE + dx * c + dy * sn, v = (long)uh * TRIG_ONE + dy * c - dx * sn;    /* (doubled) */
    sp[2 * y] = *w; sp[2 * y + 1] = -1;
    for (x = 0; x < *w; x++, u += 2 * c, v -= 2 * sn) {
      LONG uu = (LONG)(u >> 15), vv = (LONG)(v >> 15);
      const UBYTE *cc;
      int tt, q[3], j;
      if (uu < 0 || uu >= uw || vv < 0 || vv >= uh) continue;
      if (sp[2 * y] > x) sp[2 * y] = x;
      sp[2 * y + 1] = x;
      if (im->flip & 1) uu = uw - 1 - uu;
      if (im->flip & 2) vv = uh - 1 - vv;
      cc = rgb + ((vv + cy0) * fw + uu + cx0) * 3;
      tt = bayer[y & 3][x & 3] * 255 / 16;
      for (j = 0; j < 3; j++) { q[j] = (cc[j] * (lv - 1) + tt) / 255; if (q[j] > lv - 1) q[j] = lv - 1; }
      px[y * *w + x] = (UBYTE)d->cube[(q[0] * lv + q[1]) * lv + q[2]];
    }
  }
  free(rgb);
  if (d->npic >= 8) { FreeVec(d->pic[0].px); FreeVec(d->pic[0].span); memmove(d->pic, d->pic + 1, 7 * sizeof d->pic[0]); d->npic--; }
  d->pic[d->npic].data = im->data; d->pic[d->npic].w = uw; d->pic[d->npic].h = uh; d->pic[d->npic].rot = im->rot; d->pic[d->npic].flip = im->flip;
  d->pic[d->npic].crop = im->crop[0] + 3 * im->crop[1] + 7 * im->crop[2] + 11 * im->crop[3];
  d->pic[d->npic].px = px; d->pic[d->npic].span = sp;
  d->npic++;
  *span = sp;
  return px;
}
/* the selected object's Doc.images index (the selection just its reference), or -1 */
static int sel_object(struct Data *d, Pos *at)
{
  Pos lo = sel_lo(d), hi = sel_hi(d);
  if (!has_sel(d) || lo.para != hi.para || hi.off - lo.off != 4 || lo.para >= d->doc->np || !IS_PIC(d->doc->p[lo.para]->t + lo.off)) return -1;
  if (at) *at = lo;
  return pic_index(d->doc->p[lo.para]->t + lo.off);
}
#include "wp_draw_view.h"               /* WP20: freeforms and ink drawn, drawing with the mouse, the eraser, the lasso, Ink Replay */
/* a polygon (xy: n points, pixels x 16) filled, row by row */
static void fill_poly(struct RastPort *rp, const LONG *xy, int n)
{
  LONG y0 = 0x7FFFFFFF, y1 = -0x7FFFFFFF, y, xs[16];
  int i, j, m;
  for (i = 0; i < n; i++) { if (xy[2 * i + 1] < y0) y0 = xy[2 * i + 1]; if (xy[2 * i + 1] > y1) y1 = xy[2 * i + 1]; }
  for (y = (y0 >> 4); y <= (y1 >> 4); y++) {
    LONG sy = y * 16 + 8;
    for (i = 0, m = 0; i < n && m < 16; i++) {
      LONG ax = xy[2 * i], ay = xy[2 * i + 1], bx = xy[2 * ((i + 1) % n)], by = xy[2 * ((i + 1) % n) + 1];
      if ((ay <= sy) != (by <= sy)) xs[m++] = ax + (sy - ay) * (bx - ax) / (by - ay);
    }
    for (i = 1; i < m; i++) for (j = i; j > 0 && xs[j - 1] > xs[j]; j--) { LONG t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
    for (i = 0; i + 1 < m; i += 2) {
      LONG a = (xs[i] + 8) >> 4, b = ((xs[i + 1] + 8) >> 4) - 1;
      if (b >= a) RectFill(rp, a, y, b, y);
    }
  }
}
/* a line t pixels thick (points in pixels x 16) */
static void thick_line(struct RastPort *rp, LONG ax, LONG ay, LONG bx, LONG by, int t)
{
  int i, steep = (by > ay ? by - ay : ay - by) > (bx > ax ? bx - ax : ax - bx);
  for (i = 0; i < t; i++) {
    LONG o = i - (t - 1) / 2;
    if (steep) { Move(rp, (ax >> 4) + o, ay >> 4); Draw(rp, (bx >> 4) + o, by >> 4); }
    else { Move(rp, ax >> 4, (ay >> 4) + o); Draw(rp, bx >> 4, (by >> 4) + o); }
  }
}
static void draw_line(struct Data *d, Object *obj, struct RastPort *rp, ParaLay *pl, Placed *pc, LONG px, LONG py, Pos slo, Pos shi, int sel);
/* WP18: a text box's text (f, laid out by lay_textbox) turned with its box t, and a quarter more for
   text running down (src->fx.vert): drawn upright into a bitmap of its own twice, on two different
   pens, so the pixels that come out the same both times are the text's; those are then copied turned
   round the box's centre (cx, cy twips on the page at pl, pt) */
static void draw_text_turned(struct Data *d, Object *obj, struct RastPort *rp, const Image *src, const Image *t, i32 cx, i32 cy, LONG pl, LONG pt, Furn *f)
{
  int a = (t->rot + (src->fx.vert == TV_VERT ? 900 : src->fx.vert == TV_VERT270 ? 2700 : 0)) % 3600, pass, depth, k;
  i32 tw = src->fx.vert ? t->cy : t->cx, th = src->fx.vert ? t->cx : t->cy;
  LONG W = tx(d, tw) + 2, H = tx(d, th) + 2, x, y, c = icos(a), sn = isin(a), R, ox, oy, bw, bh, bpr;
  struct BitMap *bm = NULL;
  struct Layer_Info *li = NULL;
  struct Layer *ly = NULL;
  UBYTE *buf[2] = { NULL, NULL }, *row = NULL;
  Pos none;
  u32 j, ln;
  if (W < 2 || H < 2 || W * H > 600000) return;
  /* a bitmap of our own as the printing code makes them (planar when it can be: its planes then read
     directly, else pixel by pixel), with a layer to clip the text to it */
  depth = (int)GetBitMapAttr(rp->BitMap, BMA_DEPTH);
  if (depth > 8) depth = 8;
  if ((bm = AllocBitMap((ULONG)W, (ULONG)H, (ULONG)depth, BMF_CLEAR, NULL)) && (li = NewLayerInfo()) &&
      (ly = CreateUpfrontLayer(li, bm, 0, 0, W - 1, H - 1, LAYERSIMPLE, NULL)) &&
      (buf[0] = (UBYTE *)AllocVec((ULONG)(W * H), MEMF_ANY)) && (buf[1] = (UBYTE *)AllocVec((ULONG)(W * H), MEMF_ANY)) &&
      (row = (UBYTE *)AllocVec((ULONG)(2 * (W + H) + 16), MEMF_ANY))) {
    LONG b0 = d->band0, b1 = d->band1;
    int planar = (GetBitMapAttr(bm, BMA_FLAGS) & BMF_STANDARD) != 0;
    bpr = bm->BytesPerRow;
    none.para = 0xFFFFFFFFUL; none.off = 0;
    d->band0 = 0; d->band1 = 0;                 /* (inline pictures in the box: drawn whole) */
    for (pass = 0; pass < 2; pass++) {
      SetAPen(ly->rp, (ULONG)(pass ? d->pen_shadow : d->pen_page));
      SetDrMd(ly->rp, JAM1);
      RectFill(ly->rp, 0, 0, W - 1, H - 1);
      for (j = 0; j < f->n; j++) {
        Placed fp;
        memset(&fp, 0, sizeof fp);
        fp.para = 0xFFFFFFFFUL; fp.x = -d->pg->left; fp.y = f->f[j].y;
        for (ln = 0; ln < f->f[j].pl.nlines; ln++) {
          fp.line = ln;
          if (fp.y + f->f[j].pl.lines[ln].height <= th + 20) draw_line(d, obj, ly->rp, &f->f[j].pl, &fp, 0, 0, none, none, 0);
          fp.y += f->f[j].pl.lines[ln].height;
        }
      }
      WaitBlit();
      for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
          UBYTE v = 0, m = (UBYTE)(0x80 >> (x & 7));
          LONG o = y * bpr + (x >> 3);
          if (planar) { for (k = 0; k < depth; k++) if (((UBYTE *)bm->Planes[k])[o] & m) v |= (UBYTE)(1 << k); }
          else v = (UBYTE)ReadPixel(ly->rp, x, y);
          buf[pass][y * W + x] = v;
        }
    }
    d->band0 = b0; d->band1 = b1;
    /* each pixel of the turned frame from the upright one: across in steps of (cos, -sin) */
    R = W + H;
    ox = pl + tx(d, cx) - R / 2; oy = pt + tx(d, cy) - R / 2;
    bw = GetBitMapAttr(rp->BitMap, BMA_WIDTH); bh = GetBitMapAttr(rp->BitMap, BMA_HEIGHT);
    for (y = 0; y < R; y++) {
      LONG yy = oy + y, run = -1;
      if (yy < 0 || yy >= bh || yy < d->band0 || (d->band1 && yy > d->band1)) continue;
      for (x = 0; x <= R; x++) {
        int on = 0;
        if (x < R) {
          long dx = 2 * x + 1 - R, dy = 2 * y + 1 - R, u = (dx * c + dy * sn) / TRIG_ONE, v = (dy * c - dx * sn) / TRIG_ONE;   /* (doubled) */
          LONG su = (LONG)((u + W) >> 1), sv = (LONG)((v + H) >> 1), xx = ox + x;
          if (su >= 0 && sv >= 0 && su < W && sv < H && xx >= 0 && xx < bw) {
            LONG o = sv * W + su;
            if (buf[0][o] == buf[1][o]) { on = 1; row[x] = buf[0][o]; }
          }
        }
        if (on && run < 0) run = x;
        if (!on && run >= 0) { WriteChunkyPixels(rp, (ULONG)(ox + run), (ULONG)yy, (ULONG)(ox + x - 1), (ULONG)yy, row + run, R); run = -1; }
      }
    }
  }
  if (row) FreeVec(row);
  if (buf[0]) FreeVec(buf[0]);
  if (buf[1]) FreeVec(buf[1]);
  if (ly) DeleteLayer(0, ly);
  if (li) DisposeLayerInfo(li);
  if (bm) { WaitBlit(); FreeBitMap(bm); }
}
/* ---- (WP15a) charts and SmartArt: their pack's primitives (pixels at 96 dpi, 15 twips each) */
/* a polygon (xy: n points, pixels x 16) filled row by row, with room for many crossings (area charts) */
static void pk_fill(struct RastPort *rp, const LONG *xy, int n)
{
  LONG y0 = 0x7FFFFFFF, y1 = -0x7FFFFFFF, y, xs[64];
  int i, j, m;
  for (i = 0; i < n; i++) { if (xy[2 * i + 1] < y0) y0 = xy[2 * i + 1]; if (xy[2 * i + 1] > y1) y1 = xy[2 * i + 1]; }
  for (y = (y0 >> 4); y <= (y1 >> 4); y++) {
    LONG sy = y * 16 + 8;
    for (i = 0, m = 0; i < n && m < 64; i++) {
      LONG ax = xy[2 * i], ay = xy[2 * i + 1], bx = xy[2 * ((i + 1) % n)], by = xy[2 * ((i + 1) % n) + 1];
      if ((ay <= sy) != (by <= sy)) xs[m++] = ax + (sy - ay) * (bx - ax) / (by - ay);
    }
    for (i = 1; i < m; i++) for (j = i; j > 0 && xs[j - 1] > xs[j]; j--) { LONG t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
    for (i = 0; i + 1 < m; i += 2) {
      LONG a = (xs[i] + 8) >> 4, b = ((xs[i + 1] + 8) >> 4) - 1;
      if (b >= a) RectFill(rp, a, y, b, y);
    }
  }
}
/* text turned to read upwards, its box's top left at x, y (drawn through a one-plane bitmap; Ledger's chartview.c) */
static void pk_text_up(struct RastPort *rp, struct TextFont *tf, const char *t, LONG x, LONG y, LONG pen)
{
  struct RastPort trp;
  struct BitMap *bm;
  LONG w, h, i, j;
  InitRastPort(&trp);
  SetFont(&trp, tf);
  w = TextLength(&trp, (STRPTR)t, (ULONG)strlen(t));
  h = tf->tf_YSize;
  if (w <= 0 || w > 1000 || !(bm = AllocBitMap((ULONG)w, (ULONG)h, 1, BMF_CLEAR, NULL))) return;
  trp.BitMap = bm;
  SetAPen(&trp, 1); SetDrMd(&trp, JAM1);
  Move(&trp, 0, tf->tf_Baseline);
  Text(&trp, (STRPTR)t, (ULONG)strlen(t));
  SetAPen(rp, pen);
  for (j = 0; j < h; j++)
    for (i = 0; i < w; i++)
      if (ReadPixel(&trp, i, j)) WritePixel(rp, x + j, y + w - 1 - i);
  FreeBitMap(bm);
}
/* object im (a chart or SmartArt) w x h twips with its top left at pixel x, y */
static void draw_pack(struct Data *d, Object *obj, struct RastPort *rp, const Image *im, LONG x, LONG y, i32 w, i32 h)
{
  Doc *doc = d->doc;
  const char *fam = doc->defcf.font < doc->nfonts ? doc->fonts[doc->defcf.font] : "Calibri";
  const PrimList *l = pack_prims(im->pack, w, h, d->fs, fam);
  int i, k, font = doc->defcf.font < doc->nfonts ? doc->defcf.font : 0;
  LONG xy[2 * 200];
#define SX(v) (x + tx(d, (i32)(v) * 15))
#define SY(v) (y + tx(d, (i32)(v) * 15))
  if (!l) return;
  for (k = 0; k < doc->nfonts; k++) if (im->pack->font[0] && str_ieq(doc->fonts[k], im->pack->font)) font = k;
  SetDrMd(rp, JAM1);
  for (i = 0; i < l->n; i++) {
    const Prim *p = &l->p[i];
    int th = (int)tx(d, (i32)(p->width ? p->width : 1) * 15);
    if (th < 1) th = 1;
    if (th > 8) th = 8;
    switch (p->t) {
    case PR_RECT: {
      LONG a = SX(p->x), b = SY(p->y), c = SX(p->x + p->w) - 1, e = SY(p->y + p->h) - 1;
      if (c < a) c = a;
      if (e < b) e = b;
      if (p->fill) { SetAPen(rp, pen_bg(d, obj, (p->fill & 0xFFFFFF) ? p->fill & 0xFFFFFF : 0x010101)); RectFill(rp, a, b, c, e); }
      if (p->line) { SetAPen(rp, pen_for(d, obj, p->line & 0xFFFFFF)); Move(rp, a, b); Draw(rp, c, b); Draw(rp, c, e); Draw(rp, a, e); Draw(rp, a, b); }
      break; }
    case PR_LINE: case PR_POLY: {
      int n = p->np < 200 ? p->np : 200;
      for (k = 0; k < n; k++) { xy[2 * k] = SX(p->pts[2 * k]) * 16 + 8; xy[2 * k + 1] = SY(p->pts[2 * k + 1]) * 16 + 8; }
      if (p->t == PR_POLY && p->fill && n >= 3) { SetAPen(rp, pen_bg(d, obj, (p->fill & 0xFFFFFF) ? p->fill & 0xFFFFFF : 0x010101)); pk_fill(rp, xy, n); }
      if (p->line && n >= 2) {
        SetAPen(rp, pen_for(d, obj, p->line & 0xFFFFFF));
        for (k = 0; k + 1 < n + (p->t == PR_POLY); k++) thick_line(rp, xy[2 * k], xy[2 * k + 1], xy[2 * ((k + 1) % n)], xy[2 * ((k + 1) % n) + 1], p->t == PR_LINE ? th : 1);
      }
      break; }
    case PR_ELLIPSE: {
      LONG rx = tx(d, (i32)p->w * 15) / 2, ry = tx(d, (i32)p->h * 15) / 2, cx = SX(p->x) + rx, cy = SY(p->y) + ry;
      if (p->fill) {
        for (k = 0; k < 32; k++) { xy[2 * k] = cx * 16 + (LONG)((long)rx * 16 * icos(k * 1125 / 10) / TRIG_ONE); xy[2 * k + 1] = cy * 16 + (LONG)((long)ry * 16 * isin(k * 1125 / 10) / TRIG_ONE); }
        SetAPen(rp, pen_bg(d, obj, (p->fill & 0xFFFFFF) ? p->fill & 0xFFFFFF : 0x010101));
        if (rx > 0 && ry > 0) pk_fill(rp, xy, 32); else WritePixel(rp, cx, cy);
      }
      if (p->line) { SetAPen(rp, pen_for(d, obj, p->line & 0xFFFFFF)); DrawEllipse(rp, cx, cy, rx > 0 ? rx : 1, ry > 0 ? ry : 1); }
      break; }
    case PR_TEXT: {
      CharFmt f;
      struct TextFont *tf;
      char t[300];
      LONG tw, th2, ax, ay;
      if (!p->text) break;
      memset(&f, 0, sizeof f);
      f.font = (u8)font; f.size = (u16)(p->size * 3 / 2 > 0 ? p->size * 3 / 2 : 1);
      f.flags = (u8)((p->bold ? CF_BOLD : 0) | (p->italic ? CF_ITALIC : 0));
      utf8_to_latin1(p->text, strlen(p->text), t, sizeof t);
      tf = screen_font(d, &f);
      SetFont(rp, tf ? tf : ((struct GfxBase *)GfxBase)->DefaultFont);
      SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
      tw = TextLength(rp, (STRPTR)t, (ULONG)strlen(t)); th2 = rp->TxHeight;
      if (p->vert) {
        ax = SX(p->x) - (p->align == 1 ? th2 / 2 : p->align == 2 ? th2 : 0);
        ay = SY(p->y) - (p->valign == 1 ? tw / 2 : p->valign == 2 ? tw : 0);
        pk_text_up(rp, tf ? tf : ((struct GfxBase *)GfxBase)->DefaultFont, t, ax, ay, pen_for(d, obj, p->line & 0xFFFFFF));
        break;
      }
      ax = SX(p->x) - (p->align == 1 ? tw / 2 : p->align == 2 ? tw : 0);
      ay = SY(p->y) - (p->valign == 1 ? th2 / 2 : p->valign == 2 ? th2 : 0);
      SetAPen(rp, pen_for(d, obj, p->line & 0xFFFFFF));
      Move(rp, ax, ay + rp->TxBaseline);
      Text(rp, (STRPTR)t, (ULONG)strlen(t));
      break; }
    }
  }
  if (im->lstyle != PB_NONE && im->lwidth) {
    LONG a = x, b = y, c = x + tx(d, w) - 1, e = y + tx(d, h) - 1;
    SetAPen(rp, pen_for(d, obj, im->lcolor));
    Move(rp, a, b); Draw(rp, c, b); Draw(rp, c, e); Draw(rp, a, e); Draw(rp, a, b);
  }
#undef SX
#undef SY
}
/* a simple object (pt: as obj_parts gives it) with its centre at cx, cy twips on the page at (pl, pt) */
static void draw_leaf(struct Data *d, Object *obj, struct RastPort *rp, const ObjPart *part, LONG pl, LONG pt, i32 cx, i32 cy)
{
  const Image *src = part->im;
  Image t = *src;
  LONG bw, bh;
  int outline, th;
  i32 bx, by;
  t.cx = part->cx; t.cy = part->cy; t.rot = part->rot; t.flip = part->flip;
  bx = cx - OBJ_W(&t) / 2; by = cy - OBJ_H(&t) / 2;          /* its box's top left */
  if (src->pack) { draw_pack(d, obj, rp, src, pl + tx(d, cx - part->cx / 2), pt + tx(d, cy - part->cy / 2), part->cx, part->cy); return; }   /* (WP15a) */
  if (dv_leaf(d, obj, rp, part, pl, pt, cx, cy)) return;      /* WP20: a freeform or ink */
  outline = src->lstyle != PB_NONE && src->lwidth;
  th = (int)tx(d, src->lwidth * 5 / 2);
  if (th < 1) th = 1;
  if (th > 8) th = 8;
  bw = GetBitMapAttr(rp->BitMap, BMA_WIDTH); bh = GetBitMapAttr(rp->BitMap, BMA_HEIGHT);
  if (objfx_rich(&t)) {                 /* WP17/WP18: adjustments, gradients, picture fills, effects */
    get_cube(d, obj);
    if (wp18_draw_rich(rp, &t, d->zoom, pl + tx(d, bx), pt + tx(d, by), d->cube, d->cubelv, d->band0, d->band1)) goto text;
  }
  if (src->kind == OBJ_PICTURE) {
    LONG w, h, *span, row, x = pl + tx(d, cx), y = pt + tx(d, cy), uw = tx(d, t.cx), uh = tx(d, t.cy);
    UBYTE *px;
    if (uw < 1) uw = 1;
    if (uh < 1) uh = 1;
    if ((px = pic_pens(d, obj, &t, uw, uh, &w, &h, &span))) {
      x -= w / 2; y -= h / 2;
      for (row = 0; row < h; row++) {
        LONG a = x + span[2 * row], b = x + span[2 * row + 1], yy = y + row;
        if (yy < 0 || yy >= bh || span[2 * row + 1] < span[2 * row]) continue;
        if (yy < d->band0 || (d->band1 && yy > d->band1)) continue;    /* (WriteChunkyPixels isn't clipped by the layer's region on every system) */
        if (a < 0) a = 0;
        if (b >= bw) b = bw - 1;
        if (b >= a) WriteChunkyPixels(rp, (ULONG)a, (ULONG)yy, (ULONG)b, (ULONG)yy, px + row * w + (a - x), w);
      }
    } else {                                     /* can't be shown: a crossed box */
      LONG x0 = pl + tx(d, bx), y0 = pt + tx(d, by), x1 = x0 + tx(d, OBJ_W(&t)) - 1, y1 = y0 + tx(d, OBJ_H(&t)) - 1;
      SetAPen(rp, d->pen_shadow);
      Move(rp, x0, y0); Draw(rp, x1, y0); Draw(rp, x1, y1); Draw(rp, x0, y1); Draw(rp, x0, y0); Draw(rp, x1, y1);
      Move(rp, x1, y0); Draw(rp, x0, y1);
    }
    if (outline) {
      /* its edge, turned with it */
      static const BYTE cu[4] = { 0, 1, 1, 0 }, cv[4] = { 0, 0, 1, 1 };
      LONG xy[8];
      int i2;
      for (i2 = 0; i2 < 4; i2++) {
        i32 X, Y;
        obj_point(&t, cu[i2] * t.cx, cv[i2] * t.cy, &X, &Y);
        xy[2 * i2] = (pl + tx(d, bx)) * 16 + (LONG)((long)X * 16 * d->zoom / 2000); xy[2 * i2 + 1] = (pt + tx(d, by)) * 16 + (LONG)((long)Y * 16 * d->zoom / 2000);
      }
      SetAPen(rp, pen_for(d, obj, src->lcolor));
      for (i2 = 0; i2 < 4; i2++) thick_line(rp, xy[2 * i2], xy[2 * i2 + 1], xy[2 * ((i2 + 1) & 3)], xy[2 * ((i2 + 1) & 3) + 1], th);
    }
  } else {
    i32 pts[2 * SHAPE_MAXPTS];
    LONG xy[2 * SHAPE_MAXPTS], x = pl + tx(d, bx), y = pt + tx(d, by);
    int closed, i;
    u32 n = shape_points(src->kind == OBJ_TEXTBOX ? SH_RECT : src->shape, t.cx, t.cy, pts, SHAPE_MAXPTS, &closed);
    /* into the box: flipped, turned, in pixels x 16 */
    for (i = 0; i < (int)n; i++) {
      i32 X, Y;
      obj_point(&t, pts[2 * i], pts[2 * i + 1], &X, &Y);
      xy[2 * i] = x * 16 + (LONG)((long)X * 16 * d->zoom / 2000); xy[2 * i + 1] = y * 16 + (LONG)((long)Y * 16 * d->zoom / 2000);
    }
    if (closed && src->fill != COLOR_AUTO) { SetAPen(rp, pen_bg(d, obj, src->fill == 0 ? 0x010101 : src->fill)); fill_poly(rp, xy, (int)n); }
    if (outline) {
      SetAPen(rp, pen_for(d, obj, src->lcolor));
      for (i = 0; i + 1 < (int)n + closed; i++) thick_line(rp, xy[2 * i], xy[2 * i + 1], xy[2 * ((i + 1) % n)], xy[2 * ((i + 1) % n) + 1], th);
      if (src->kind == OBJ_SHAPE && src->shape == SH_ARROW) {
        i32 ah[6];
        LONG hx[6];
        shape_arrowhead(t.cx, t.cy, src->lwidth * 5 / 2, ah);
        for (i = 0; i < 3; i++) {
          i32 X, Y;
          obj_point(&t, ah[2 * i], ah[2 * i + 1], &X, &Y);
          hx[2 * i] = x * 16 + (LONG)((long)X * 16 * d->zoom / 2000); hx[2 * i + 1] = y * 16 + (LONG)((long)Y * 16 * d->zoom / 2000);
        }
        fill_poly(rp, hx, 3);
      }
    }
  }
text:
  /* a text box's (or shape's) text, upright in its box (WP18: or turned with it, or running down) */
  if ((src->text && src->text->np) || src->fx.link) {
    Furn f;
    u32 j, ln;
    int laid = lay_textbox(d->lay, src, t.cx, &f);
    if (laid && src->warp) get_cube(d, obj);
    if (laid && src->warp && wp19_draw_warped(rp, d->lay, src, &t, cx, cy, pl, pt, d->zoom, d->cube, d->cubelv, d->band0, d->band1)) ;   /* (WP19) bent */
    else if (laid && (t.rot || src->fx.vert)) draw_text_turned(d, obj, rp, src, &t, cx, cy, pl, pt, &f);
    else if (f.n) {
      Pos none;
      i32 tx0 = cx - t.cx / 2, ty0 = cy - t.cy / 2;
      none.para = 0xFFFFFFFFUL; none.off = 0;
      for (j = 0; j < f.n; j++) {
        Placed fp;
        memset(&fp, 0, sizeof fp);
        fp.para = 0xFFFFFFFFUL; fp.x = tx0 - d->pg->left; fp.y = ty0 + f.f[j].y;
        for (ln = 0; ln < f.f[j].pl.nlines; ln++) {
          fp.line = ln;
          if (fp.y + f.f[j].pl.lines[ln].height <= ty0 + t.cy + 20) draw_line(d, obj, rp, &f.f[j].pl, &fp, pl, pt, none, none, 0);
          fp.y += f.f[j].pl.lines[ln].height;
        }
      }
    }
    lay_furniture_free(&f);
  }
}
/* object k with its (turned) box's top left at bx, by twips on the page at (pl, pt); sel: with handles
   (2: one of several chosen, a dotted frame) */
static void draw_object(struct Data *d, Object *obj, struct RastPort *rp, int k, LONG pl, LONG pt, i32 bx, i32 by, int sel)
{
  Image *im;
  ObjPart parts[64];
  LONG x, y, w, h;
  int n, i;
  if (k < 0 || k >= d->doc->nimages) return;
  im = &d->doc->images[k];
  n = obj_parts(im, parts, 64);
  for (i = 0; i < n; i++) draw_leaf(d, obj, rp, &parts[i], pl, pt, bx + parts[i].x, by + parts[i].y);
  x = pl + tx(d, bx); y = pt + tx(d, by); w = tx(d, OBJ_W(im)); h = tx(d, OBJ_H(im));
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (sel == 2) {
    LONG xx;
    SetAPen(rp, d->pen_sel);
    for (xx = x - 1; xx <= x + w; xx += 2) { WritePixel(rp, xx, y - 1); WritePixel(rp, xx, y + h); }
    for (xx = y - 1; xx <= y + h; xx += 2) { WritePixel(rp, x - 1, xx); WritePixel(rp, x + w, xx); }
  } else if (sel) {
    /* its frame and eight handles, and a round one above it for turning it (a float) */
    static const BYTE hxs[8] = { 0, 1, 2, 2, 2, 1, 0, 0 }, hys[8] = { 0, 0, 0, 1, 2, 2, 2, 1 };
    SetAPen(rp, d->pen_sel);
    Move(rp, x - 1, y - 1); Draw(rp, x + w, y - 1); Draw(rp, x + w, y + h); Draw(rp, x - 1, y + h); Draw(rp, x - 1, y - 1);
    for (i = 0; i < 8; i++) {
      LONG cx = x + hxs[i] * (w - 1) / 2, cy = y + hys[i] * (h - 1) / 2;
      if (wp17_cropping(k)) {
        /* WP17: cropping: black bars on the frame at the corners (two) and the middles (one) */
        LONG sx = hxs[i] == 2 ? -1 : 1, sy = hys[i] == 2 ? -1 : 1, a0, a1, b0, b1;
        SetAPen(rp, d->pen_text);
        if (hys[i] != 1) {
          a0 = hxs[i] == 1 ? cx - 6 : cx; a1 = hxs[i] == 1 ? cx + 6 : cx + sx * 9; b0 = cy; b1 = cy - sy * 2;
          RectFill(rp, a0 < a1 ? a0 : a1, b0 < b1 ? b0 : b1, a0 < a1 ? a1 : a0, b0 < b1 ? b1 : b0);
        }
        if (hxs[i] != 1) {
          b0 = hys[i] == 1 ? cy - 6 : cy; b1 = hys[i] == 1 ? cy + 6 : cy + sy * 9; a0 = cx; a1 = cx - sx * 2;
          RectFill(rp, a0 < a1 ? a0 : a1, b0 < b1 ? b0 : b1, a0 < a1 ? a1 : a0, b0 < b1 ? b1 : b0);
        }
        continue;
      }
      SetAPen(rp, d->pen_text);
      RectFill(rp, cx - 3, cy - 3, cx + 3, cy + 3);
      SetAPen(rp, d->pen_page);
      RectFill(rp, cx - 2, cy - 2, cx + 2, cy + 2);
    }
    if (im->wrap != WRAP_INLINE && !OBJ_IS_GRAPHIC(im->kind)) {
      LONG cx = x + (w - 1) / 2, cy = y - ROT_HANDLE;
      SetAPen(rp, d->pen_text);
      Move(rp, cx, y - 1); Draw(rp, cx, cy + 4);
      DrawEllipse(rp, cx, cy, 4, 4);
      SetAPen(rp, d->pen_page);
      DrawEllipse(rp, cx, cy, 3, 3);
      SetAPen(rp, d->pen_text);
      DrawEllipse(rp, cx, cy, 2, 2);
    }
  }
}
/* how many comments are open at the start of paragraph para: found once, then counted forward */
static int comments_open(struct Data *d, u32 para)
{
  Doc *doc = d->doc;
  if (!doc->ncomments || para >= doc->np) return 0;
  if (d->cver != doc->ver || para < d->cpara || para > d->cpara + 400) {
    Pos p;
    p.para = para; p.off = 0;
    d->copen = doc_comment_at(doc, p, NULL, NULL) >= 0;
    d->cpara = para; d->cver = doc->ver;
  }
  while (d->cpara < para) {
    Para *q = doc->p[d->cpara];
    const char *f = q->len >= 4 ? (const char *)memchr(q->t, (char)0xF3, q->len) : NULL;
    while (f && f + 4 <= q->t + q->len) {
      if (IS_CSTART(f)) d->copen++; else if (IS_CEND(f) && d->copen > 0) d->copen--;
      f = (const char *)memchr(f + 1, (char)0xF3, (size_t)(q->t + q->len - (f + 1)));
    }
    d->cpara++;
  }
  return d->copen;
}
/* fields' results on grey, as Word shades them (WP1, core/field.h field_shading): every field, or the
   one the caret is in (found once per change of caret or document) */
static void field_shade(struct Data *d, Object *obj, struct RastPort *rp, ParaLay *pl, Placed *pc, Line *ln, LONG lx, LONG top)
{
  static const Doc *sdoc;
  static ULONG sver;
  static Pos scaret, ss, se;
  static int sk = -1;
  Doc *doc = d->doc;
  int body = pc->para < doc->np && pl->p == doc->p[pc->para], depth = 0;
  u32 k;
  if (field_shading == FS_SELECTED) {
    if (!body) return;
    if (sdoc != doc || sver != doc->ver || pos_cmp(scaret, d->caret)) {
      sdoc = doc; sver = doc->ver; scaret = d->caret;
      sk = field_at(doc, d->caret, &ss, &se);
    }
    if (sk < 0 || pc->para < ss.para || pc->para > se.para) return;
  } else {
    if (body) depth = field_depth_before(doc, pc->para);
    for (k = pl->lines[0].box0; k < ln->box0 && k < pl->nboxes; k++) {
      Box *b = &pl->boxes[k];
      if ((b->kind == BX_MARK || b->kind == BX_FIELD) && b->off + 4 <= pl->p->len && IS_FMARK(pl->p->t + b->off)) { if (IS_FSTART(pl->p->t + b->off)) depth++; else if (depth > 0) depth--; }
    }
  }
  SetAPen(rp, pen_bg(d, obj, 0xD9D9D9));
  for (k = ln->box0; k < ln->box0 + ln->nbox; k++) {
    Box *b = &pl->boxes[k];
    int in;
    if ((b->kind == BX_MARK || b->kind == BX_FIELD) && b->off + 4 <= pl->p->len && IS_FMARK(pl->p->t + b->off)) {
      if (IS_FSTART(pl->p->t + b->off)) depth++; else if (depth > 0) depth--;
      continue;
    }
    if (b->w <= 0 || b->kind == BX_FIELD) continue;
    if (field_shading == FS_SELECTED) {
      Pos at;
      at.para = pc->para; at.off = b->off;
      in = pos_cmp(ss, at) < 0 && pos_cmp(at, se) < 0;
    } else in = depth > 0;
    if (in) RectFill(rp, lx + tx(d, b->x), top, lx + tx(d, b->x + b->w) - 1, top + tx(d, ln->height) - 1);
  }
}
/* WP14a: equations' colours as pens (for wp14a_draw) */
static struct Data *mpen_d; static Object *mpen_obj;
static LONG math_pen(ULONG rgb) { return pen_for(mpen_d, mpen_obj, rgb); }
#include "wp_equation_view.h"           /* WP14b: editing equations in place */
/* one laid-out line (pl: its paragraph's layout, pc: where it was placed) on the page whose top left
   is at (px, py). Back to front: change bar, comment shading, highlights, the selection (sel: slo to
   shi), then the text, note numbers, fields, inline pictures, leaders and formatting marks. Headers,
   footers, notes and text boxes use it too, with pc->para 0xFFFFFFFF (no paragraph of the body) */
/* WP21b: a line of turned text in a table cell: its text drawn across into a scratch bitmap, then put
   on the page turned, pixel by pixel (pixels left at pen 0 are what's behind); the selection behind it */
static void draw_turned(struct Data *d, Object *obj, struct RastPort *rp, ParaLay *pl, Placed *pc, LONG px, LONG py, Pos slo, Pos shi, int sel)
{
  Line *ln = &pl->lines[pc->line];
  LONG W = tx(d, pc->tlen) + 1, H = tx(d, ln->height) + 1, bw = tx(d, pc->w), bh = tx(d, pc->tlen), u, v;
  LONG bx = px + tx(d, d->pg->left + pc->x), by = py + tx(d, pc->y), sh = tx(d, pc->tshift + ln->left);
  struct BitMap *bm;
  struct RastPort trp;
  u32 k;
  if (W < 2 || H < 2 || W > 4000 || H > 400) return;
  if (sel && pc->para >= slo.para && pc->para <= shi.para) {
    u32 s0 = pc->para == slo.para ? slo.off : 0, e0 = pc->para == shi.para ? shi.off : pl->p->len;
    if (e0 > s0) {
      LONG a = sh + tx(d, line_x(d->lay, pl, ln, s0)), b = sh + tx(d, line_x(d->lay, pl, ln, e0));
      SetAPen(rp, d->pen_sel);
      if (pc->turn == TD_DOWN) RectFill(rp, bx, by + a, bx + bw - 1, by + b - 1);
      else RectFill(rp, bx, by + bh - b, bx + bw - 1, by + bh - a - 1);
    }
  }
  /* (eight planes of its own whatever the screen: a pixel read back is the pen it was drawn in) */
  if (!(bm = AllocBitMap((ULONG)W, (ULONG)H, 8, BMF_CLEAR, NULL))) return;
  InitRastPort(&trp);
  trp.BitMap = bm;
  SetDrMd(&trp, JAM1);
  for (k = ln->box0; k < ln->box0 + ln->nbox; k++) {
    Box *b = &pl->boxes[k], nb = *b;
    char num[24];
    if (LAY_HIDDEN(d->lay, b)) continue;                   /* (WP9: hidden text, formatting marks off) */
    if (b->kind == BX_TEXT) draw_text(d, obj, &trp, b, pl->p->t, sh + tx(d, b->x), tx(d, ln->ascent));
    else if (b->kind == BX_NOTE || b->kind == BX_ENOTE || b->kind == BX_FIELD) {
      if (b->kind == BX_FIELD) lay_field_text(d->lay, pl->p, b, pc->page, num); else lay_note_label(pl, k, num);
      nb.kind = BX_TEXT; nb.off = 0; nb.len = (u16)strlen(num);
      draw_text(d, obj, &trp, &nb, num, sh + tx(d, b->x), tx(d, ln->ascent));
    }
  }
  for (v = 0; v < H && v < bw + 1; v++)
    for (u = 0; u < W; u++) {
      LONG c = ReadPixel(&trp, u, v);
      if (c <= 0) continue;
      SetAPen(rp, (ULONG)c);
      if (pc->turn == TD_DOWN) WritePixel(rp, bx + bw - 1 - v, by + u);
      else WritePixel(rp, bx + v, by + bh - 1 - u);
    }
  WaitBlit();
  FreeBitMap(bm);
}
static void draw_line(struct Data *d, Object *obj, struct RastPort *rp, ParaLay *pl, Placed *pc, LONG px, LONG py, Pos slo, Pos shi, int sel)
{
  Layout *L = d->lay;
  Doc *doc = d->doc;
  Line *ln = &pl->lines[pc->line];
  if (pc->turn) { draw_turned(d, obj, rp, pl, pc, px, py, slo, shi, sel); return; }     /* WP21b */
  LONG lx = px + tx(d, d->pg->left + pc->x + ln->left), top = py + tx(d, pc->y), base = py + tx(d, pc->y + ln->ascent);
  u32 k;
  /* a changed line has a bar in the margin (WP5: where the Track Changes Options say; red in Simple Markup) */
  if (d->doc->nrevs && rv_opts.lines != RVL_NONE) {
    for (k = ln->box0; k < ln->box0 + ln->nbox; k++) if (pl->boxes[k].rvmark) break;
    if (k < ln->box0 + ln->nbox || (pc->para != 0xFFFFFFFFUL && rv_show_para(doc, &pl->p->pf))) {
      int right = rv_opts.lines == RVL_RIGHT || (rv_opts.lines == RVL_OUTSIDE && d->doc->evenodd && (pc->page & 1));
      LONG bx = right ? px + tx(d, d->pg->width - d->pg->right) + 5 : px + tx(d, d->pg->left) - 6;
      SetAPen(rp, pen_for(d, obj, rv_mode(doc) == RVM_SIMPLE ? 0xC00000 : rv_opts.linecolor));
      RectFill(rp, bx, top, bx, top + tx(d, ln->height) - 1);
    }
  }
  /* comments' ranges shaded */
  if (d->doc->ncomments && pc->para < d->doc->np && rv_show_comments(d->doc)) {
    int open = comments_open(d, pc->para);
    for (k = pl->lines[0].box0; k < ln->box0 && k < pl->nboxes; k++)      /* the marks on lines before this one */
      if (pl->boxes[k].kind == BX_MARK) { if (IS_CSTART(pl->p->t + pl->boxes[k].off)) open++; else if (open > 0 && IS_CEND(pl->p->t + pl->boxes[k].off)) open--; }
    for (k = ln->box0; k < ln->box0 + ln->nbox; k++) {
      Box *b = &pl->boxes[k];
      if (b->kind == BX_MARK) { if (IS_CSTART(pl->p->t + b->off)) open++; else if (open > 0 && IS_CEND(pl->p->t + b->off)) open--; continue; }
      if (open > 0 && b->w > 0 && !(b->kind == BX_SPACE && k + 1 == ln->box0 + ln->nbox)) {
        SetAPen(rp, d->pen_comment);
        RectFill(rp, lx + tx(d, b->x), top, lx + tx(d, b->x + b->w) - 1, top + tx(d, ln->height) - 1);
      }
    }
  }
  if (doc->nfields && field_shading != FS_NEVER) field_shade(d, obj, rp, pl, pc, ln, lx, top);     /* fields on grey (WP1) */
  /* (WP7) the regions Restrict Editing lets the user edit, shaded */
  if (wp7_shade(d->doc))
    for (k = ln->box0; k < ln->box0 + ln->nbox; k++) {
      Box *b = &pl->boxes[k];
      if (b->w > 0 && b->f.perm && prot_perm_mine(d->doc, b->f.perm)) {
        SetAPen(rp, pen_bg(d, obj, 0xFFF4B0));
        RectFill(rp, lx + tx(d, b->x), top, lx + tx(d, b->x + b->w) - 1, top + tx(d, ln->height) - 1);
      }
    }
  /* highlighting behind the text (not trailing spaces); page fields on grey, as Word shades fields */
  for (k = ln->box0; k < ln->box0 + ln->nbox; k++) {
    Box *b = &pl->boxes[k];
    LONG x0, x1, sz;
    if (b->kind == BX_FIELD && b->w > 0) {
      SetAPen(rp, pen_bg(d, obj, 0xD9D9D9));
      RectFill(rp, lx + tx(d, b->x), top, lx + tx(d, b->x + b->w) - 1, top + tx(d, ln->height) - 1);
      continue;
    }
    if (!(b->f.hl || b->f.shade) || b->kind == BX_BREAK || b->kind == BX_PIC || (b->kind == BX_SPACE && k + 1 == ln->box0 + ln->nbox)) continue;
    x0 = lx + tx(d, b->x); x1 = lx + tx(d, b->x + b->w); sz = tx(d, b->f.size * 10);
    if (!b->f.hl) {        /* (WP9) character shading, the highlight over it */
      SetAPen(rp, pen_bg(d, obj, (b->f.shade & 0xFFFFFF) ? (b->f.shade & 0xFFFFFF) : 0x010101));
      if (x1 > x0) RectFill(rp, x0, base - sz + sz / 8, x1 - 1, base + sz / 4);
      continue;
    }
    SetAPen(rp, pen_bg(d, obj, (b->f.hl & 0xFFFFFF) ? (b->f.hl & 0xFFFFFF) : 0x010101));
    if (x1 > x0) RectFill(rp, x0, base - sz + sz / 8, x1 - 1, base + sz / 4);
  }
  fv_draw_line(d, obj, rp, pl, pc, ln, lx, top, sel);     /* WP12 */
  /* the selection behind the text (a block of cells is shown by its cells instead) */
  if (sel && !fv_hidesel(d) && pc->para >= slo.para && pc->para <= shi.para && !(d->blk && cell_in(&pl->p->pf, d->blk_t))) {
    u32 s = pc->para == slo.para ? slo.off : 0, e = pc->para == shi.para ? shi.off : pl->p->len;
    u32 ls = ln->start, le = pc->line + 1 < pl->nlines ? pl->lines[pc->line + 1].start : pl->p->len;
    if (s < ls) s = ls;
    if (e > le) e = le;
    if (e > s || (pc->para < shi.para && pc->line + 1 == pl->nlines)) {
      LONG x0 = lx + tx(d, line_x(L, pl, ln, s)), x1 = lx + tx(d, line_x(L, pl, ln, e));
      if (pc->para < shi.para && pc->line + 1 == pl->nlines) x1 += tx(d, 120);     /* the paragraph mark */
      if (x1 > x0) { SetAPen(rp, d->pen_sel); RectFill(rp, x0, top, x1 - 1, top + tx(d, ln->height) - 1); }
    }
  }
  SetDrMd(rp, JAM1);
  if (pc->line == 0 && pl->label[0]) {
    Box lb;
    memset(&lb, 0, sizeof lb);
    lb.f = pl->p->r[0].f; lb.f.flags &= ~(CF_UNDERLINE | CF_STRIKE);
    lb.kind = BX_TEXT; lb.off = 0; lb.len = (u16)strlen(pl->label);
    draw_text(d, obj, rp, &lb, pl->label, lx, base);
  }
  if (pc->lnum) {
    /* the line number, its right edge pc->lnd left of the column, in the Normal style's font */
    struct TextFont *tf = screen_font(d, &doc->styles[0].cf);
    char num[12];
    LONG nx = px + tx(d, d->pg->left + pc->x - pc->lnd);
    sprintf(num, "%lu", (unsigned long)pc->lnum);
    if (tf) SetFont(rp, tf);
    SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    SetAPen(rp, d->pen_text); SetDrMd(rp, JAM1);
    Move(rp, nx - TextLength(rp, (STRPTR)num, (ULONG)strlen(num)), base); Text(rp, (STRPTR)num, (ULONG)strlen(num));
  }
  for (k = ln->box0; k < ln->box0 + ln->nbox; k++) {
    Box *b = &pl->boxes[k];
    if (LAY_HIDDEN(L, b)) continue;                       /* (WP9: hidden text, formatting marks off) */
    if (b->kind == BX_TEXT && ENCLOSURE(&b->f)) { draw_enclosed(d, obj, rp, b, pl->p->t, lx + tx(d, b->x), base); continue; }   /* (WP9) */
    if (b->kind == BX_DROP) draw_text(d, obj, rp, b, pl->p->t, lx + tx(d, b->x), base + tx(d, lay_drop_dy(pl, b)));
    if (b->kind == BX_TEXT && b->nspace) {
      /* hyphenated at the line's end: the text, then the hyphen */
      FaceSize *z = lay_face(L, &b->f);
      Box tb = *b, hb = *b;
      i32 hw = z ? (i32)((fonts_text(z, "-", 1) + QFINE / 2) / QFINE) : 0;
      tb.w -= hw; tb.nspace = 0;
      hb.off = 0; hb.len = 1; hb.w = hw; hb.x = b->x + tb.w;
      draw_text(d, obj, rp, &tb, pl->p->t, lx + tx(d, tb.x), base);
      draw_text(d, obj, rp, &hb, "-", lx + tx(d, hb.x), base);
    }
    else if (b->kind == BX_TEXT) {
      draw_text(d, obj, rp, b, pl->p->t, lx + tx(d, b->x), base);
      Spell *lsp = d->tb && proof_live_spelling ? proof_spell((Spell *)d->tb->spell, &b->f) : NULL;   /* (WP31: the run's language's dictionary, none if not checked) */
      if (lsp && !b->f.rev && !(k > 0 && (pl->boxes[k - 1].kind == BX_DROP || (pl->boxes[k - 1].kind == BX_TEXT && pl->boxes[k - 1].nspace)))) {   /* not the rest of a hyphenated word, nor the word a drop cap opens */
        /* words not in the dictionary: a red wavy line (not the word being typed) */
        u32 ws, we, o = b->off, end = b->off + b->len;
        while (o < end && spell_word_at(pl->p->t, end, o, &ws, &we)) {
          if (!(pc->para == d->caret.para && we == d->caret.off) && !spell_ok(lsp, pl->p->t + ws, we - ws)) {
            LONG x0 = lx + tx(d, lay_text_x(L, b, pl->p->t, ws)), x1 = lx + tx(d, lay_text_x(L, b, pl->p->t, we)), xx, wy = base + 2;
            SetAPen(rp, d->pen_squiggle);
            for (xx = x0; xx < x1; xx++) WritePixel(rp, xx, wy + ((xx >> 1) & 1));
          }
          o = we;
        }
      }
    }
    else if (b->kind == BX_NOTE || b->kind == BX_ENOTE) {
      Box nb = *b;
      char num[24];
      lay_note_label(pl, k, num);
      nb.kind = BX_TEXT; nb.off = 0; nb.len = (u16)strlen(num);
      draw_text(d, obj, rp, &nb, num, lx + tx(d, b->x), base);
    }
    else if (b->kind == BX_FIELD) {
      /* a page field: its number, on grey (as Word shades fields) */
      Box nb = *b;
      char num[LAY_FIELD_TEXT];
      lay_field_text(L, pl->p, b, pc->page, num);
      nb.kind = BX_TEXT; nb.off = 0; nb.len = (u16)strlen(num);
      draw_text(d, obj, rp, &nb, num, lx + tx(d, b->x), base);
    }
    else if (b->kind == BX_MATH) ev_draw_math(d, obj, rp, L, pl, pc, b, lx + tx(d, b->x), base);   /* WP14a (drawn), WP14b (edited) */
    else if (b->kind == BX_PIC) {
      Pos at;
      int pk = pic_index(pl->p->t + b->off);
      at.para = pc->para; at.off = b->off;
      if (pk < doc->nimages)          /* its bottom on the baseline */
        draw_object(d, obj, rp, pk, px, py, d->pg->left + pc->x + ln->left + b->x, pc->y + ln->ascent - OBJ_H(&doc->images[pk]),
                    sel && pos_cmp(at, slo) == 0 && shi.para == at.para && shi.off == at.off + 4);
    }
    else if (b->kind == BX_SPACE && (b->f.flags & (CF_UNDERLINE | CF_STRIKE)) && k + 1 < ln->box0 + ln->nbox) {
      LONG x = lx + tx(d, b->x), w = tx(d, b->w);        /* underlined spaces between words */
      SetAPen(rp, pen_for(d, obj, b->f.color));
      if ((b->f.flags & CF_UNDERLINE) && b->f.ul != UL_WORDS) deco_line(rp, b->f.ul, x, base - tx(d, b->f.position * 10) + 1 + d->zoom / 150, w);
    }
    if ((b->kind == BX_TEXT || b->kind == BX_SPACE) && !b->f.rev && d->tb) {
      /* (WP8) grammar issues: a blue double line under them, style issues a dotted gold one (not the one being typed) */
      const GramIssue *gi;
      int ng = wp8_issues(pl->p, &gi), g;
      for (g = 0; g < ng; g++) {
        u32 ga = gi[g].a > b->off ? gi[g].a : b->off, gb = gi[g].b < (u32)(b->off + b->len) ? gi[g].b : (u32)(b->off + b->len);
        LONG x0, x1, xx, wy = base + 2;
        if (ga >= gb || (pc->para == d->caret.para && gi[g].b == d->caret.off)) continue;
        x0 = lx + tx(d, b->kind == BX_SPACE ? b->x : lay_text_x(L, b, pl->p->t, ga));
        x1 = lx + tx(d, b->kind == BX_SPACE ? b->x + b->w : lay_text_x(L, b, pl->p->t, gb));
        SetAPen(rp, pen_for(d, obj, gi[g].kind >= GR_PASSIVE ? 0xC09000 : 0x2050E0));
        if (gi[g].kind >= GR_PASSIVE) { for (xx = x0; xx < x1; xx += 2) WritePixel(rp, xx, wy); }
        else if (x1 > x0) { Move(rp, x0, wy); Draw(rp, x1 - 1, wy); Move(rp, x0, wy + 2); Draw(rp, x1 - 1, wy + 2); }
      }
    }
    if ((b->f.fx & FX_BORDER) && b->w > 0 && (b->kind == BX_TEXT || b->kind == BX_SPACE || b->kind == BX_TAB || b->kind == BX_FIELD)) {
      /* a character border: one box round a stretch of bordered text, so its sides only where the stretch starts and ends */
      LONG x = lx + tx(d, b->x), w = tx(d, b->w), y0 = base - tx(d, b->f.size * 9), y1 = base + tx(d, b->f.size * 3);
      int first = k == ln->box0 || !(pl->boxes[k - 1].f.fx & FX_BORDER), last = k + 1 == ln->box0 + ln->nbox || !(pl->boxes[k + 1].f.fx & FX_BORDER);
      if (!(last && b->kind == BX_SPACE && !first)) {
        SetAPen(rp, pen_for(d, obj, b->f.color));
        Move(rp, x, y0); Draw(rp, x + w - 1, y0); Move(rp, x, y1); Draw(rp, x + w - 1, y1);
        if (first) { Move(rp, x - 1, y0); Draw(rp, x - 1, y1); }
        if (last) { Move(rp, x + w, y0); Draw(rp, x + w, y1); }
      }
    }
    if (b->kind == BX_TAB && b->nspace && b->w > 0) {
      /* the tab's leader up to its stop */
      LONG x = lx + tx(d, b->x), w = tx(d, b->w), step = tx(d, b->f.size * 10 / 4), i;
      SetAPen(rp, pen_for(d, obj, b->f.color));
      if (step < 3) step = 3;
      if (b->nspace == LEAD_LINE) { Move(rp, x, base + 1); Draw(rp, x + w - 1, base + 1); }
      else for (i = x + w - step / 2 - 1; i > x + step / 2; i -= step) {
        if (b->nspace == LEAD_DOT) WritePixel(rp, i, base - 1);
        else { Move(rp, i - step / 3, base - tx(d, b->f.size * 10 / 4)); Draw(rp, i, base - tx(d, b->f.size * 10 / 4)); }
      }
    }
    if (d->marks) {
      /* formatting marks: a dot for each space, an arrow for a tab, a bent arrow for a line break */
      LONG x = lx + tx(d, b->x), w = tx(d, b->w), my = base - tx(d, b->f.size * 10 / 4);
      SetAPen(rp, d->pen_marks);
      if (b->kind == BX_SPACE) {
        int i;
        for (i = 0; i < b->nspace; i++) { LONG cx = x + w * (2 * i + 1) / (2 * b->nspace); RectFill(rp, cx, my, cx + (d->zoom > 120), my + (d->zoom > 120)); }
      } else if (b->kind == BX_TAB) {
        LONG ax = x + (w > 12 ? w / 2 + 4 : w), a0 = ax - (w > 12 ? 8 : w);
        if (a0 < x) a0 = x;
        Move(rp, a0, my); Draw(rp, ax, my); Move(rp, ax - 2, my - 2); Draw(rp, ax, my); Draw(rp, ax - 2, my + 2);
      } else if (b->kind == BX_BREAK) {
        LONG bx0 = x + 2, by = my;
        Move(rp, bx0 + 6, by - 4); Draw(rp, bx0 + 6, by); Draw(rp, bx0, by); Move(rp, bx0 + 2, by - 2); Draw(rp, bx0, by); Draw(rp, bx0 + 2, by + 2);
      }
    }
  }
  if (d->marks && pc->line + 1 == pl->nlines && (pl->nboxes == 0 || pl->boxes[pl->nboxes - 1].kind != BX_BREAK || ln->nbox == 0)) {
    /* the paragraph mark after its last line */
    LONG x = lx + tx(d, ln->nbox ? pl->boxes[ln->box0 + ln->nbox - 1].x + pl->boxes[ln->box0 + ln->nbox - 1].w : 0) + 1;
    struct TextFont *tf = screen_font(d, &pl->p->r[pl->p->nr - 1].f);
    SetFont(rp, tf ? tf : ((struct GfxBase *)GfxBase)->DefaultFont);
    SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    SetAPen(rp, d->pen_marks);
    Move(rp, x, base);
    Text(rp, (STRPTR)"\266", 1);
    if (pc->para != 0xFFFFFFFFUL && pl->p->pf.sect && pc->para + 1 < doc->np) {
      /* a section ends here: a dotted line after the mark, saying how the next one starts */
      static const char *const kinds[4] = { "Section Break (Next Page)", "Section Break (Continuous)", "Section Break (Even Page)", "Section Break (Odd Page)" };
      const char *k = tr(kinds[doc_sect(doc, pc->para + 1)->start & 3]);
      LONG x1 = px + tx(d, d->pg->left + pc->x + pc->w), tl, mx, xx;
      x += TextLength(rp, (STRPTR)"\266", 1) + 2;
      SetFont(rp, theme_small());
      tl = TextLength(rp, (STRPTR)k, (ULONG)strlen(k));
      mx = x + (x1 - x - tl) / 2;
      for (xx = x; xx < x1; xx += 2) if (xx < mx - 3 || xx > mx + tl + 3) WritePixel(rp, xx, base - 3);
      if (x1 - x > tl + 8) { Move(rp, mx, base - 3 - theme_small()->tf_YSize / 2 + theme_small()->tf_Baseline); Text(rp, (STRPTR)k, (ULONG)strlen(k)); }
    }
  }
  if (d->marks && pc->line == 0 && pc->para != 0xFFFFFFFFUL && pl->p->pf.colbreak) {
    /* a column break before the paragraph: a dotted line above it */
    LONG xx, x0 = px + tx(d, d->pg->left + pc->x), y = top - 2;
    SetAPen(rp, d->pen_marks);
    for (xx = x0; xx < x0 + tx(d, pc->w); xx += 2) WritePixel(rp, xx, y);
  }
}
/* (WP5) up to max lines of Latin-1 text s wrapped to w pixels in rp's font: each line's start and length */
static int wrap_text(struct RastPort *rp, const char *s, LONG w, int max, const char **st, ULONG *len)
{
  int n = 0;
  while (*s && n < max) {
    struct TextExtent te;
    ULONG fit = TextFit(rp, (STRPTR)s, (ULONG)strlen(s), &te, NULL, 1, (ULONG)(w > 8 ? w : 8), 100);
    if (!fit) break;
    if (s[fit] && fit > 1) { ULONG f2 = fit; while (f2 > 1 && s[f2] != ' ') f2--; if (f2 > 1) fit = f2; }
    st[n] = s; len[n] = fit; n++;
    s += fit; while (*s == ' ') s++;
  }
  return n;
}
/* the balloons for page k in the column right of the page, each joined to where it is anchored by a
   dotted line (WP5: core/review.h rv_page_items): a comment with its replies (a resolved thread dimmed,
   to its first line), a deletion shown in a balloon, a formatting change */
static void draw_balloons(struct Data *d, Object *obj, struct RastPort *rp, u32 k, LONG pl, LONG pt, LONG h)
{
  Layout *L = d->lay;
  Doc *doc = d->doc;
  RvItem it[32];
  int n, i;
  LONG bx = pl + page_w(d) + 8, bw = col_w(d) - 16, next = pt + 4, fh = theme_font()->tf_YSize;
  int cur = doc_comment_at(doc, d->caret, NULL, NULL);
  if (cur >= 0) cur = rv_thread_root(doc, cur);
  n = rv_page_items(L, k, it, 32);
  for (i = 0; i < n; i++) {
    /* the balloon's rows: segments of text (an author in bold, then their words, wrapped) */
    static char seg[6][260];
    const char *rs[16];
    ULONG rl[16];
    u8 rbold[16], rind[16];
    int nseg = 0, nr = 0, j, dim = 0, reps[8], nrep = 0;
    static char u[600];
    LONG ax = pl + tx(d, it[i].x), ay = pt + tx(d, it[i].y), y, hgt, by;
    u32 accent;
    SetFont(rp, theme_font());
    if (it[i].kind == RVI_COMMENT) {
      Comment *c = &doc->comments[it[i].k];
      char *txt;
      int done = c->done, maxl = done ? 1 : 4;
      dim = done;
      accent = rv_comment_color(doc, c->author);
      /* the author (and Resolved), the words; then each reply's */
      utf8_to_latin1(c->author[0] ? c->author : "Comment", (u32)strlen(c->author[0] ? c->author : "Comment"), seg[nseg], 200);
      if (done) { strcat(seg[nseg], " - "); strcat(seg[nseg], tr("Resolved")); }
      rs[nr] = seg[nseg]; rl[nr] = (ULONG)strlen(seg[nseg]); rbold[nr] = 1; rind[nr] = 0; nr++; nseg++;
      txt = story_text(&c->text);
      utf8_to_latin1(txt ? txt : "", txt ? (u32)strlen(txt) : 0, seg[nseg], sizeof seg[0]);
      free(txt);
      j = wrap_text(rp, seg[nseg], bw - 10, maxl, rs + nr, rl + nr);
      while (j-- > 0) { rbold[nr] = 0; rind[nr] = 0; nr++; }
      nseg++;
      nrep = done ? 0 : rv_replies(doc, it[i].k, reps, 8);
      for (j = 0; j < nrep && j < 2 && nseg + 1 < 6 && nr + 3 < 16; j++) {
        Comment *rc = &doc->comments[reps[j]];
        int m;
        utf8_to_latin1(rc->author, (u32)strlen(rc->author), seg[nseg], 200);
        rs[nr] = seg[nseg]; rl[nr] = (ULONG)strlen(seg[nseg]); rbold[nr] = 1; rind[nr] = 1; nr++; nseg++;
        txt = story_text(&rc->text);
        utf8_to_latin1(txt ? txt : "", txt ? (u32)strlen(txt) : 0, seg[nseg], sizeof seg[0]);
        free(txt);
        m = wrap_text(rp, seg[nseg], bw - 18, 2, rs + nr, rl + nr);
        while (m-- > 0) { rbold[nr] = 0; rind[nr] = 1; nr++; }
        nseg++;
      }
      if (nrep > 2 && nseg < 6 && nr < 16) {
        sprintf(seg[nseg], tr("%d more replies"), nrep - 2);
        rs[nr] = seg[nseg]; rl[nr] = (ULONG)strlen(seg[nseg]); rbold[nr] = 0; rind[nr] = 1; nr++; nseg++;
      }
    } else {
      const char *au = it[i].k < doc->nrevs ? doc->revs[it[i].k].author : "";
      accent = rv_author_color(doc, it[i].k);
      utf8_to_latin1(au[0] ? au : "?", (u32)strlen(au[0] ? au : "?"), seg[0], 200);
      rs[nr] = seg[0]; rl[nr] = (ULONG)strlen(seg[0]); rbold[nr] = 1; rind[nr] = 0; nr++;
      rv_item_text(doc, &it[i], u, sizeof u, tr);
      utf8_to_latin1(u, (u32)strlen(u), seg[1], sizeof seg[1]);
      j = wrap_text(rp, seg[1], bw - 10, 3, rs + nr, rl + nr);
      while (j-- > 0) { rbold[nr] = 0; rind[nr] = 0; nr++; }
    }
    hgt = 6 + nr * fh + 4;
    y = ay - fh > next ? ay - fh : next;
    if (y > h || y + hgt < 0) { next = y + hgt + 4; continue; }
    /* the dotted line from the anchor to the balloon */
    SetAPen(rp, it[i].kind == RVI_COMMENT ? d->pen_marks : pen_for(d, obj, accent));
    for (by = ax; by < bx; by += 3) WritePixel(rp, by, ay);
    if (y + 8 != ay) for (by = (ay < y + 8 ? ay : y + 8); by <= (ay < y + 8 ? y + 8 : ay); by += 3) WritePixel(rp, bx - 1, by);
    SetAPen(rp, it[i].kind == RVI_COMMENT && it[i].k == cur ? d->pen_comment : d->pen_page);
    RectFill(rp, bx, y, bx + bw - 1, y + hgt - 1);
    SetAPen(rp, it[i].kind == RVI_COMMENT && it[i].k == cur ? d->pen_sel : d->pen_shadow);
    Move(rp, bx, y); Draw(rp, bx + bw - 1, y); Draw(rp, bx + bw - 1, y + hgt - 1); Draw(rp, bx, y + hgt - 1); Draw(rp, bx, y);
    /* the author's colour down its left edge */
    SetAPen(rp, pen_for(d, obj, accent));
    RectFill(rp, bx + 1, y + 1, bx + 2, y + hgt - 2);
    SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    SetDrMd(rp, JAM1);
    for (j = 0; j < nr; j++) {
      struct TextExtent te;
      struct TextFont *f = rbold[j] ? theme_bold() : theme_font();
      LONG tx0 = bx + 6 + (rind[j] ? 8 : 0);
      ULONG fit;
      SetFont(rp, f);
      SetAPen(rp, dim ? d->pen_marks : d->pen_text);
      fit = TextFit(rp, (STRPTR)rs[j], rl[j], &te, NULL, 1, (ULONG)(bx + bw - 4 - tx0 > 4 ? bx + bw - 4 - tx0 : 4), 100);
      Move(rp, tx0, y + 3 + j * fh + f->tf_Baseline); Text(rp, (STRPTR)rs[j], fit);
    }
    if (d->nballoon < 24) {
      d->balloon[d->nballoon].x0 = (WORD)bx; d->balloon[d->nballoon].y0 = (WORD)y; d->balloon[d->nballoon].x1 = (WORD)(bx + bw - 1);
      d->balloon[d->nballoon].y1 = (WORD)(y + hgt - 1); d->balloon[d->nballoon].k = it[i].k;
      d->balloon[d->nballoon].kind = it[i].kind; d->balloon[d->nballoon].a = it[i].a; d->balloon[d->nballoon].b = it[i].b;
      d->nballoon++;
    }
    next = y + hgt + 4;
  }
}
/* the watermark's mask: its words drawn with the biggest screen font, then scaled and turned (diagonal:
   rising 45 degrees) into a 1-bit mask the size they take on the page at this zoom */
static void wm_build(struct Data *d)
{
  Doc *doc = d->doc;
  i32 bw = d->pg->width - d->pg->left - d->pg->right, bh = d->pg->height - d->pg->top - d->pg->bottom;
  LONG want = tx(d, doc->wm.horizontal ? bw * 9 / 10 : (bw + bh) * 47 / 100), sw, sh, W, H, X, Y, scale, bpr, mbpr;
  struct TextAttr ta;
  struct TextFont *tf;
  struct BitMap bm;
  struct RastPort trp;
  char lat[80];
  size_t n;
  free(d->wm_mask); d->wm_mask = NULL;
  strcpy(d->wm_text, doc->wm.text); d->wm_zoom = d->zoom; d->wm_horiz = doc->wm.horizontal; d->wm_bw = bw; d->wm_bh = bh;
  n = utf8_to_latin1(doc->wm.text, (u32)strlen(doc->wm.text), lat, sizeof lat);
  if (!n || want < 8) return;
  ta.ta_Name = (STRPTR)"QuillSans.font"; ta.ta_YSize = 80; ta.ta_Style = 0; ta.ta_Flags = 0;
  if (!(tf = theme_open_font("QuillSans.font", 80))) return;
  memset(&trp, 0, sizeof trp);
  InitRastPort(&trp);
  SetFont(&trp, tf);
  sw = TextLength(&trp, (STRPTR)lat, (ULONG)n) + 4; sh = tf->tf_YSize + 4;
  InitBitMap(&bm, 1, sw, sh);
  if (!(bm.Planes[0] = (PLANEPTR)AllocRaster(sw, sh))) { theme_close_font(tf); return; }
  trp.BitMap = &bm;
  SetRast(&trp, 0); SetAPen(&trp, 1); SetDrMd(&trp, JAM1);
  Move(&trp, 2, 2 + tf->tf_Baseline); Text(&trp, (STRPTR)lat, (ULONG)n);
  scale = want * 1024 / (sw - 4);                       /* 1024ths: the page's pixels to the font's */
  /* the letters' own height, not the font's line: about 0.75 of it */
  if (doc->wm.horizontal) { W = want; H = sh * scale / 1024; }
  else W = H = ((sw + sh) * scale / 1024) * 724 / 1024;
  bpr = bm.BytesPerRow; mbpr = (W + 7) / 8;
  if (W > 0 && H > 0 && W < 4000 && H < 4000 && (d->wm_mask = (UBYTE *)calloc((size_t)mbpr * H, 1))) {
    UBYTE *src = (UBYTE *)bm.Planes[0];
    for (Y = 0; Y < H; Y++)
      for (X = 0; X < W; X++) {
        LONG u = X - W / 2, v = Y - H / 2, x, y;
        if (doc->wm.horizontal) { x = u; y = v; }
        else { x = (u * 724 - v * 724) / 1024; y = (u * 724 + v * 724) / 1024; }       /* turned back 45 degrees */
        x = x * 1024 / scale + sw / 2; y = y * 1024 / scale + sh / 2;
        if (x >= 0 && x < sw && y >= 0 && y < sh && (src[y * bpr + (x >> 3)] & (0x80 >> (x & 7)))) d->wm_mask[Y * mbpr + (X >> 3)] |= (UBYTE)(0x80 >> (X & 7));
      }
    d->wm_w = W; d->wm_h = H;
  }
  FreeRaster(bm.Planes[0], sw, sh);
  theme_close_font(tf);
}
/* Word draws it half transparent: half way to white */
static ULONG wm_colour(ULONG c)
{
  return ((((c >> 16) & 255) + 255) / 2) << 16 | ((((c >> 8) & 255) + 255) / 2) << 8 | (((c & 255) + 255) / 2);
}
/* the watermark centred on the text area of the page at (pl, pt) (h: rp's height), from its mask,
   made again when the zoom, the text or the page's size changes */
static void draw_watermark(struct Data *d, Object *obj, struct RastPort *rp, LONG pl, LONG pt, LONG h)
{
  Doc *doc = d->doc;
  LONG cx, cy, x0, y0, Y, mbpr;
  if (d->wm_zoom != d->zoom || strcmp(d->wm_text, doc->wm.text) || d->wm_horiz != doc->wm.horizontal ||
      d->wm_bw != d->pg->width - d->pg->left - d->pg->right || d->wm_bh != d->pg->height - d->pg->top - d->pg->bottom) wm_build(d);
  if (!d->wm_mask) return;
  cx = pl + tx(d, d->pg->left + (d->pg->width - d->pg->left - d->pg->right) / 2);
  cy = pt + tx(d, d->pg->top + (d->pg->height - d->pg->top - d->pg->bottom) / 2);
  x0 = cx - d->wm_w / 2; y0 = cy - d->wm_h / 2; mbpr = (d->wm_w + 7) / 8;
  SetAPen(rp, pen_bg(d, obj, doc->wm.opaque ? doc->wm.color : wm_colour(doc->wm.color)));
  for (Y = 0; Y < d->wm_h; Y++) {
    UBYTE *row = d->wm_mask + Y * mbpr;
    LONG X = 0, yy = y0 + Y;
    if (yy < 0 || yy > h) continue;
    while (X < d->wm_w) {                        /* each run of set pixels, one fill */
      LONG a;
      while (X < d->wm_w && !(row[X >> 3] & (0x80 >> (X & 7)))) { if (!row[X >> 3] && !(X & 7)) X += 8; else X++; }
      a = X;
      while (X < d->wm_w && (row[X >> 3] & (0x80 >> (X & 7)))) X++;
      if (X > a) RectFill(rp, x0 + a, yy, x0 + X - 1, yy);
    }
  }
}
/* (WP22a) the picture watermark, centred on the text area of the page at (pl, pt) (h: rp's height):
   decoded at its shown size (washed out if it is), dithered to the cube's pens and kept */
static void draw_wm_picture(struct Data *d, Object *obj, struct RastPort *rp, LONG pl, LONG pt, LONG h)
{
  Doc *doc = d->doc;
  i32 bx, by, bw, bh;
  LONG w, ht, x, y, row, rw;
  get_cube(d, obj);
  pages_wm_box(doc, d->pg, &bx, &by, &bw, &bh);
  w = tx(d, bw); ht = tx(d, bh);
  if (w < 2 || ht < 2 || w * ht > 3000000) return;
  if (!d->wmp_px || d->wmp_w != w || d->wmp_h != ht || d->wmp_data != doc->wm.pic || d->wmp_wash != doc->wm.washout) {
    u8 *rgb;
    int lv = d->cubelv;
    if (d->wmp_px) { FreeVec(d->wmp_px); d->wmp_px = NULL; }
    if (!(rgb = img_decode(doc->wm.pic, doc->wm.piclen, (u32)w, (u32)ht))) return;
    if (doc->wm.washout) pages_washout(rgb, (u32)(w * ht));
    if ((d->wmp_px = (UBYTE *)AllocVec((ULONG)(w * ht), MEMF_ANY))) {
      for (y = 0; y < ht; y++)
        for (x = 0; x < w; x++) {
          const u8 *c = rgb + (y * w + x) * 3;
          int tt = bayer[y & 3][x & 3] * 255 / 16, q[3], j;
          for (j = 0; j < 3; j++) { q[j] = (c[j] * (lv - 1) + tt) / 255; if (q[j] > lv - 1) q[j] = lv - 1; }
          d->wmp_px[y * w + x] = (UBYTE)d->cube[(q[0] * lv + q[1]) * lv + q[2]];
        }
      d->wmp_w = w; d->wmp_h = ht; d->wmp_data = doc->wm.pic; d->wmp_wash = doc->wm.washout;
    }
    free(rgb);
    if (!d->wmp_px) return;
  }
  x = pl + tx(d, bx); y = pt + tx(d, by);
  rw = GetBitMapAttr(rp->BitMap, BMA_WIDTH);
  for (row = 0; row < ht; row++) {
    LONG yy = y + row, a = x, b = x + w - 1;
    if (yy < 0 || yy > h || yy >= GetBitMapAttr(rp->BitMap, BMA_HEIGHT)) continue;
    if (yy < d->band0 || (d->band1 && yy > d->band1)) continue;
    if (a < 0) a = 0;
    if (b >= rw) b = rw - 1;
    if (b >= a) WriteChunkyPixels(rp, (ULONG)a, (ULONG)yy, (ULONG)b, (ULONG)yy, d->wmp_px + row * w + (a - x), w);
  }
}
/* the page border (WP22a: the page's section's, each side its own kind, as core/pages.c's rectangles) */
static void draw_border(struct Data *d, Object *obj, struct RastPort *rp, LONG pl, LONG pt, u32 page)
{
  PBRect *r = NULL;
  int n = d->lay ? pages_border_rects(d->lay, page, &r) : 0, i;
  for (i = 0; i < n; i++) {
    LONG x0 = pl + tx(d, r[i].x0), y0 = pt + tx(d, r[i].y0), x1 = pl + tx(d, r[i].x1) - 1, y1 = pt + tx(d, r[i].y1) - 1;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    SetAPen(rp, pen_for(d, obj, r[i].color));
    RectFill(rp, x0, y0, x1, y1);
  }
  free(r);
}
/* one edge (E_) of a table cell, from (x0, y0) to (x1, y1): its line, or a table gridline if it has none */
static void cell_edge(struct Data *d, Object *obj, struct RastPort *rp, const CellBox *c, int e, LONG x0, LONG y0, LONG x1, LONG y1)
{
  int horiz = y0 == y1;
  LONG lw, gap, i, j, n;
  u8 st;
  if (!(c->borders & (1 << e))) {
    if (!d->tgrid) return;
    SetAPen(rp, d->pen_grid); SetDrPt(rp, 0xCCCC);
    Move(rp, x0, y0); Draw(rp, x1, y1);
    SetDrPt(rp, 0xFFFF);
    return;
  }
  st = c->bstyle[e] ? c->bstyle[e] : PB_SINGLE;
  lw = tx(d, (c->bwidth[e] ? c->bwidth[e] : 4) * 5 / 2);
  if (st == PB_THICK && lw < 2) lw = 2;
  if (lw < 1) lw = 1;
  n = st == PB_DOUBLE ? 2 : 1;
  gap = st == PB_DOUBLE ? (lw + 1 > 2 ? lw + 1 : 2) : 0;
  SetAPen(rp, pen_for(d, obj, c->bcolor[e]));
  SetDrPt(rp, st == PB_DOTTED ? 0xAAAA : st == PB_DASHED ? 0xF0F0 : 0xFFFF);
  for (j = 0; j < n; j++)
    for (i = 0; i < lw; i++) {
      LONG o = i - (lw - 1) / 2 + (n == 2 ? (j ? gap / 2 + 1 : -(gap / 2 + 1)) : 0);
      if (horiz) { Move(rp, x0, y0 + o); Draw(rp, x1, y1 + o); }
      else { Move(rp, x0 + o, y0); Draw(rp, x1 + o, y1); }
    }
  SetDrPt(rp, 0xFFFF);
}
/* paragraphs' shading and borders on page k */
static void draw_pboxes(struct Data *d, Object *obj, struct RastPort *rp, u32 k, LONG pl, LONG pt, LONG h)
{
  Layout *L = d->lay;
  u32 i;
  for (i = 0; i < L->npbox; i++) {
    const ParaBox *x = &L->pbox[i];
    const ParaFmt *pf = &L->pl[x->para].p->pf;
    LONG x0 = pl + tx(d, x->x0), x1 = pl + tx(d, x->x1) - 1, y0 = pt + tx(d, x->y0), y1 = pt + tx(d, x->y1) - 1;
    int e;
    if (x->page != k || y1 < 0 || y0 > h) continue;
    if (pf->shade) { SetAPen(rp, pen_bg(d, obj, (pf->shade & 0xFFFFFF) ? pf->shade & 0xFFFFFF : 0x010101)); RectFill(rp, x0, y0 < 0 ? 0 : y0, x1, y1 > h ? h : y1); }
    for (e = 0; e < PE_N; e++) {
      u8 st = pf->bstyle[e];
      LONG lw, j, m, n, gap, ly;
      if (!st || (e == PE_TOP && !(x->lines & PB_TOPLINE)) || (e == PE_BOTTOM && !(x->lines & PB_BOTTOMLINE)) || (e == PE_BETWEEN && !(x->lines & PB_BETWEENLINE))) continue;
      lw = tx(d, (pf->bwidth[e] ? pf->bwidth[e] : 4) * 5 / 2);
      if (st == PB_THICK && lw < 2) lw = 2;
      if (lw < 1) lw = 1;
      n = st == PB_DOUBLE ? 2 : 1;
      gap = n == 2 ? lw + 1 : 0;
      SetAPen(rp, pen_for(d, obj, pf->bcolor[e]));
      SetDrPt(rp, st == PB_DOTTED ? 0xAAAA : st == PB_DASHED ? 0xF0F0 : 0xFFFF);
      ly = e == PE_BETWEEN ? pt + tx(d, x->ybetween) : 0;
      for (j = 0; j < n; j++)
        for (m = 0; m < lw; m++) {
          LONG o = j * (lw + gap) + m;          /* inward from the box's edge */
          switch (e) {
          case PE_TOP: Move(rp, x0, y0 + o); Draw(rp, x1, y0 + o); break;
          case PE_BOTTOM: Move(rp, x0, y1 - o); Draw(rp, x1, y1 - o); break;
          case PE_BETWEEN: Move(rp, x0, ly + o); Draw(rp, x1, ly + o); break;
          case PE_LEFT: Move(rp, x0 + o, y0); Draw(rp, x0 + o, y1); break;
          default: Move(rp, x1 - o, y0); Draw(rp, x1 - o, y1);
          }
        }
      SetDrPt(rp, 0xFFFF);
    }
  }
}
/* the floating objects on page k (fb: n of them): those behind the text, or the rest */
static void draw_floats(struct Data *d, Object *obj, struct RastPort *rp, const FloatBox *fb, u32 n, u32 k, LONG pl, LONG pt, LONG h, int behind)
{
  int selk = fb == d->lay->floats ? sel_object(d, NULL) : -1, pass;
  u32 j, i, *ord = lay_stack_order(d->lay, fb, n);     /* bottom first */
  /* the selected one last, over the others */
  for (pass = 0; pass < 2; pass++)
    for (j = 0; j < n; j++)
      if ((i = ord ? ord[j] : j), fb[i].page == k && (fb[i].wrap == WRAP_BEHIND) == behind && (fb[i].img == selk) == pass && pt + tx(d, fb[i].y + fb[i].h) >= 0 && pt + tx(d, fb[i].y) <= h) {
        int chosen = 0, j;
        if (fb == d->lay->floats)
          for (j = 0; j < d->nmsel; j++) if (d->msel[j].para == fb[i].para && d->msel[j].off < d->doc->p[fb[i].para]->len && pic_index(d->doc->p[fb[i].para]->t + d->msel[j].off) == fb[i].img) chosen = 1;
        draw_object(d, obj, rp, fb[i].img, pl, pt, fb[i].x, fb[i].y, pass ? 1 : chosen ? 2 : 0);
      }
  free(ord);
}
/* one page whose top left corner is at (pl, pt) in rp; h: the height of rp (lines outside are left out).
   Back to front: watermark, border, column lines, cells' shading and lines, gridlines, paragraph
   borders, objects behind the text, the lines, balloons, header, footer and notes, objects in front.
   sel, slo, shi: the selection shown (none when printing). Sets d->pg, which the helpers read */
static void draw_page(struct Data *d, Object *obj, struct RastPort *rp, u32 k, LONG pl, LONG pt, LONG h, int sel, Pos slo, Pos shi)
{
  u32 k0 = k;                           /* the page */
  Layout *L = d->lay;
  u32 a, b, i;
  d->pg = lay_page(L, k);
  if (d->doc->wm.text[0] && d->vmode != VM_WEB && d->vmode != VM_DRAFT) draw_watermark(d, obj, rp, pl, pt, h);
  else if (d->doc->wm.pic && d->vmode != VM_WEB && d->vmode != VM_DRAFT) draw_wm_picture(d, obj, rp, pl, pt, h);      /* (WP22a) */
  if (d->vmode != VM_WEB && d->vmode != VM_DRAFT) draw_border(d, obj, rp, pl, pt, k);
  /* lines between columns */
  SetAPen(rp, d->pen_text);
  for (i = 0; i < L->nseps; i++)
    if (L->seps[i].page == k) {
      LONG x = pl + tx(d, d->pg->left + L->seps[i].x);
      Move(rp, x, pt + tx(d, L->seps[i].y0)); Draw(rp, x, pt + tx(d, L->seps[i].y1));
    }
  /* table cells: their fills (a nested table's over its cell's), then their lines */
  for (i = 0; i < 2 * L->ncells; i++) {
    CellBox *c = &L->cells[i % L->ncells];
    LONG y0 = pt + tx(d, c->y), y1 = pt + tx(d, c->y + c->h);
    if ((u32)c->depth != i / L->ncells || c->page != k || y1 < 0 || y0 > h || c->shade == COLOR_AUTO) continue;
    SetAPen(rp, pen_bg(d, obj, c->shade == 0 ? 0x010101 : c->shade));
    RectFill(rp, pl + tx(d, c->x), y0, pl + tx(d, c->x + c->w), y1);
  }
  if (sel && d->blk)
    for (i = 0; i < L->ncells; i++) {
      CellBox *c = &L->cells[i];
      if (c->page != k || c->table != d->blk_t || c->row < d->blk_r0 || c->row > d->blk_r1 || c->col < d->blk_c0 || c->col + c->span - 1 > d->blk_c1) continue;
      SetAPen(rp, d->pen_sel);
      RectFill(rp, pl + tx(d, c->x) + 1, pt + tx(d, c->y) + 1, pl + tx(d, c->x + c->w) - 1, pt + tx(d, c->y + c->h) - 1);
    }
  SetAPen(rp, d->pen_text);
  for (i = 0; i < L->ncells; i++) {
    CellBox *c = &L->cells[i];
    LONG x0 = pl + tx(d, c->x), x1 = pl + tx(d, c->x + c->w), y0 = pt + tx(d, c->y), y1 = pt + tx(d, c->y + c->h);
    if (c->page != k || y1 < 0 || y0 > h) continue;
    cell_edge(d, obj, rp, c, E_TOP, x0, y0, x1, y0);
    cell_edge(d, obj, rp, c, E_BOTTOM, x0, y1, x1, y1);
    cell_edge(d, obj, rp, c, E_LEFT, x0, y0, x0, y1);
    cell_edge(d, obj, rp, c, E_RIGHT, x1, y0, x1, y1);
  }
  if (d->grid) {
    /* gridlines: every half centimetre (quarter inch) over the text area */
    const PageSetup *g = d->pg;
    i32 step = metric(d) ? 283 : 360, x, y, right = g->width - g->right, bottom = d->vmode == VM_WEB ? 0x3FFFFFFF : g->height - g->bottom;
    LONG gx0 = pl + tx(d, g->left), gx1 = pl + tx(d, d->vmode == VM_WEB && L->webw > 0 ? g->left + L->webw : right);
    LONG gy0 = pt + tx(d, g->top), gy1 = bottom >= 0x3FFFFFFF ? h : pt + tx(d, bottom);
    if (gy0 < 0) gy0 = 0;
    if (gy1 > h) gy1 = h;
    SetAPen(rp, d->pen_grid);
    if (tx(d, step) >= 3 && gy1 > gy0) {
      for (x = g->left; pl + tx(d, x) <= gx1; x += step) { Move(rp, pl + tx(d, x), gy0); Draw(rp, pl + tx(d, x), gy1); }
      for (y = g->top; pt + tx(d, y) <= gy1; y += step) if (pt + tx(d, y) >= gy0) { Move(rp, gx0, pt + tx(d, y)); Draw(rp, gx1, pt + tx(d, y)); }
    }
  }
  draw_pboxes(d, obj, rp, k, pl, pt, h);
  /* objects behind the text: the header's and footer's, then the page's */
  if (d->vmode != VM_DRAFT) {
    int w;
    for (w = 0; w < 2; w++) {
      Furn f;
      if (!lay_furniture(L, k0, w, &f)) continue;
      draw_floats(d, obj, rp, f.fb, f.nfb, k0, pl, pt, h, 1);
      lay_furniture_free(&f);
    }
  }
  draw_floats(d, obj, rp, L->floats, L->nfloats, k, pl, pt, h, 1);
  lay_page_lines(L, k, &a, &b);
  for (i = a; i < b; i++) {
    Placed *pc = &L->placed[i];
    LONG ly = pt + tx(d, pc->y);
    if (pc->page != k || ly > h || (d->band1 && ly > d->band1)) continue;     /* (band1 0: no band set yet) */
    if (ly + tx(d, L->pl[pc->para].lines[pc->line].height) < (d->band0 > 0 ? d->band0 : 0)) continue;
    draw_line(d, obj, rp, &L->pl[pc->para], pc, pl, pt, slo, shi, sel);
  }
  if (col_w(d)) draw_balloons(d, obj, rp, k, pl, pt, h);
  /* the header and footer, and the footnotes under a short line */
  {
    int w;
    for (w = 0; w < 4; w++) {
      Furn f;
      u32 j, ln;
      i32 sepy = -1;
      /* headers, footers, footnotes, and after the text the endnotes (draft: only those) */
      if (w < 3 && d->vmode == VM_DRAFT) continue;
      if (w < 2 ? !lay_furniture(L, k0, w, &f) : w == 2 ? !lay_notes(L, k0, &f, &sepy) : !lay_endnotes(L, k0, &f, &sepy)) continue;
      if (w >= 2 && sepy >= 0) {
        SetAPen(rp, d->pen_text);
        Move(rp, pl + tx(d, d->pg->left), pt + tx(d, sepy)); Draw(rp, pl + tx(d, d->pg->left + L->sepw), pt + tx(d, sepy));   /* (WP22b: L->sepw) */
      }
      for (j = 0; j < f.n; j++) {
        Placed fp;
        memset(&fp, 0, sizeof fp);
        fp.para = 0xFFFFFFFFUL; fp.page = k0; fp.y = f.f[j].y;
        for (ln = 0; ln < f.f[j].pl.nlines; ln++) {
          fp.line = ln;
          draw_line(d, obj, rp, &f.f[j].pl, &fp, pl, pt, slo, shi, 0);
          fp.y += f.f[j].pl.lines[ln].height;
        }
      }
      if (w < 2) draw_floats(d, obj, rp, f.fb, f.nfb, k0, pl, pt, h, 0);
      lay_furniture_free(&f);
    }
  }
  draw_floats(d, obj, rp, L->floats, L->nfloats, k, pl, pt, h, 0);
}
/* everything in the view (w x h pixels, view coordinates) into rp: the desk, then each page in sight
   with its shadow. Also decides whether the selection is a block of cells (d->blk), which draw_page
   and draw_line read */
static void draw_all(struct Data *d, Object *obj, struct RastPort *rp, LONG w, LONG h)
{
  if (d->doc && d->doc->pagecolor != d->pens_pagecolor) { free_pens(d, obj); get_pens(d, obj); }
  Layout *L = d->lay;
  LONG pw = page_w(d), ph = page_h(d), pl = page_left(d, w);
  u32 k;
  Pos slo = sel_lo(d), shi = sel_hi(d);
  int sel = has_sel(d);
  d->blk = sel && tbl_block(d->doc, d->anchor, d->caret, &d->blk_t, &d->blk_r0, &d->blk_r1, &d->blk_c0, &d->blk_c1);
  SetDrMd(rp, JAM1);
  SetAPen(rp, d->pen_desk);
  RectFill(rp, 0, 0, w - 1, h - 1);
  d->nballoon = 0;
  if (!L) return;
  if (!paged(d)) {
    /* draft and web layout: one sheet the width of the view; draft marks where pages end */
    LONG top = (d->vmode == VM_DRAFT ? GAP : 0) - d->scrolly;
    SetAPen(rp, d->pen_page);
    RectFill(rp, 0, top > 0 ? top : 0, w - 1, h - 1);
    for (k = 0; k < L->npages; k++) {
      LONG pt = page_top(d, k) - d->scrolly, py = pt + tx(d, d->doc->page.top);
      if (py + text_h(d) + 2 < 0) continue;
      if (py > h) break;
      if (d->vmode == VM_DRAFT && k) {
        LONG x;
        SetAPen(rp, d->pen_shadow);
        for (x = 0; x < w; x += 4) WritePixel(rp, x, py - 1);
      }
      draw_page(d, obj, rp, k, page_x(d, k, w), pt, h, sel, slo, shi);
      ov_draw(d, obj, rp, k, page_x(d, k, w), pt, h);       /* WP11: the outline symbols */
    }
    (void)pw; (void)ph; (void)pl;
    return;
  }
  for (k = 0; k < L->npages; k++) {
    LONG pt = page_top(d, k) - d->scrolly, px = page_x(d, k, w);
    if (pt + page_h(d) < 0) continue;
    if (pt > h) break;
    pw = tx(d, page_of(d, k)->width); ph = tx(d, page_of(d, k)->height);
    SetAPen(rp, d->pen_shadow);
    RectFill(rp, px + 1, pt + 1, px + pw, pt + ph);
    SetAPen(rp, d->pen_page);
    RectFill(rp, px, pt, px + pw - 1, pt + ph - 1);
    draw_page(d, obj, rp, k, px, pt, h, sel, slo, shi);
    if (d->slabel[0]) {
      /* a story edited in the text's place: a dashed line above its text, and a tab saying what it is */
      const PageSetup *g = page_of(d, k);
      struct TextFont *tf = theme_small();
      LONG x0 = px + tx(d, g->left), x1 = px + tx(d, g->width - g->right), y = pt + tx(d, g->top) - 3, xx;
      LONG tw_;
      SetFont(rp, tf);
      tw_ = TextLength(rp, (STRPTR)d->slabel, (ULONG)strlen(d->slabel));
      SetAPen(rp, tpen(T_ACCENT));
      for (xx = x0; xx < x1; xx += 4) RectFill(rp, xx, y, xx + 1, y);
      RectFill(rp, x0, y - tf->tf_YSize - 3, x0 + tw_ + 8, y - 1);
      SetAPen(rp, tpen(T_ONACCENT)); SetDrMd(rp, JAM1);
      Move(rp, x0 + 4, y - tf->tf_YSize - 2 + tf->tf_Baseline); Text(rp, (STRPTR)d->slabel, (ULONG)strlen(d->slabel));
    }
  }
}
/* the caret, XORed on the window */
static void caret_xor(struct Data *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG x0 = d->cx, y0 = d->cy, y1 = d->cy + d->ch - 1;
  if (d->cturn) {
    /* WP21b: across turned text */
    LONG x1 = x0 + d->cw - 1;
    if (y0 < _mtop(obj) || y0 + 1 > _mbottom(obj)) return;
    if (x0 < _mleft(obj)) x0 = _mleft(obj);
    if (x1 > _mright(obj)) x1 = _mright(obj);
    if (x1 < x0) return;
    SetDrMd(rp, COMPLEMENT);
    RectFill(rp, x0, y0, x1, y0 + 1);
    SetDrMd(rp, JAM1);
    return;
  }
  if (x0 < _mleft(obj) || x0 + 1 > _mright(obj)) return;
  if (y0 < _mtop(obj)) y0 = _mtop(obj);
  if (y1 > _mbottom(obj)) y1 = _mbottom(obj);
  if (y1 < y0) return;
  SetDrMd(rp, COMPLEMENT);
  RectFill(rp, x0, y0, x0 + 1, y1);
  SetDrMd(rp, JAM1);
}
/* where the caret goes, in window pixels (cx, cy and its height ch), from the layout */
static void caret_place(struct Data *d, Object *obj)
{
  u32 page;
  i32 x, y, h;
  if (ev_caret(d, obj)) return;          /* WP14b: inside an equation */
  lay_caret(d->lay, d->caret, &page, &x, &y, &h);
  d->cx = _mleft(obj) + page_x(d, page, _mwidth(obj)) + tx(d, x);
  d->cy = _mtop(obj) + page_top(d, page) - d->scrolly + tx(d, y);
  d->ch = tx(d, h);
  if (d->ch < 4) d->ch = 4;
  d->cturn = lay_caret_turn; d->cw = tx(d, lay_caret_w);      /* WP21b: turned text */
  if (d->cw < 4) d->cw = 4;
}
/* the caret is XORed on the window, so taking it away is drawing it again: caret_shown must be true
   to the screen, or a stray bar is left. Anything drawing into the window hides it first; caret_show
   places and draws it, only when nothing is selected */
static void caret_hide(struct Data *d, Object *obj)
{
  if (d->caret_shown) { caret_xor(d, obj); d->caret_shown = 0; }
}
static void caret_show(struct Data *d, Object *obj)
{
  if (!d->caret_shown && !has_sel(d) && !ww_passive(d) && !ev_hidecaret(d)) { caret_place(d, obj); caret_xor(d, obj); d->caret_shown = 1; }   /* (WP25: not in a split window's other pane; WP14b: not with a selection in an equation) */
}

/* ------------------------------------------------------------------ */
/* keeping things up to date */
/* the scroll bars' size and position (NoNotify: they don't scroll the view back) */
static void update_prop(struct Data *d)
{
  if (d->tb && d->tb->prop && d->lay)
    SetAttrs(d->tb->prop, MUIA_NoNotify, TRUE, MUIA_Prop_Entries, d->doch, MUIA_Prop_First, d->scrolly, TAG_DONE);
  if (d->tb && d->tb->hprop && d->lay && d->obj && _win(d->obj)) {
    LONG w = _mwidth(d->obj), full = row_w(d) + (paged(d) ? 2 * GAP : 0);
    int need = full > w;
    page_left(d, w);
    SetAttrs(d->tb->hprop, MUIA_NoNotify, TRUE, MUIA_Prop_Entries, full, MUIA_Prop_Visible, w, MUIA_Prop_First, d->scrollx, TAG_DONE);
    /* shown only when the page is wider than the view (changed later: not in the middle of drawing) */
    if (need != d->hshown) {
      d->hshown = need;
      DoMethod(_app(d->obj), MUIM_Application_PushMethod, (ULONG)d->tb->hprop, 3, MUIM_Set, MUIA_ShowMe, (ULONG)need);
    }
  }
  ww_prop(d);                                    /* WP25: a split window's other pane's scroll bar */
}
/* Scrolling with the blitter. Most of what a scroll shows was on screen already, so instead of
   drawing the whole view again, the off-screen buffer's picture is moved by dy with ScrollRaster
   (done by the blitter on the Amiga's own chips, and by the card's blitter under Picasso96 or
   CyberGraphX), and only the strip that comes into view is drawn, clipped to it with a region on
   the buffer's layer and with draw_page skipping lines outside it. The buffer then goes to the
   window in one blit. A jump of most of a screen, or a buffer that may not match the document
   (typing not drawn yet), is drawn in full instead: returns 0 then and the caller redraws */
static void draw_all(struct Data *d, Object *obj, struct RastPort *rp, LONG w, LONG h);
static void update_vruler(struct Data *d);
/* (with DEBUG=: how long blitted scrolls and full drawings take, summed, for comparing them) */
static LONG time_blit, time_full, n_blit, n_full;
/* one more timing (t ticks; full: a full drawing, else a blitted scroll), logged every tenth */
static void time_note(int full, LONG t)
{
  if (!qlog) return;
  if (full) { time_full += t; n_full++; } else { time_blit += t; n_blit++; }
  if ((n_full + n_blit) % 10 == 0)
    fprintf(qlog, "drawing: %ld blitted scrolls in %ld ticks, %ld full drawings in %ld ticks (1/50 s)\n", (long)n_blit, (long)time_blit, (long)n_full, (long)time_full), fflush(qlog);
}
/* scroll by dy pixels through the back buffer, as above: 1 if done, 0 if the caller must redraw.
   scrolly is left as it was for the caller to set */
static int scroll_blit(struct Data *d, Object *obj, LONG dy)
{
  LONG w = _mwidth(obj), h = _mheight(obj), y0, y1, t0 = ticks50();
  struct Region *rgn;
  struct Rectangle r;
  struct RastPort *rp;
  if (!d->back_ok || !d->ly || d->bw != w || d->bh != h || d->stale || !d->lay || !_win(obj) || !d->shown) return 0;
  if (dy >= h * 3 / 4 || -dy >= h * 3 / 4) return 0;
  if (!(rgn = NewRegion())) return 0;
  rp = d->ly->rp;
  caret_hide(d, obj);
  ScrollRaster(rp, 0, dy, 0, 0, w - 1, h - 1);       /* dy > 0: the picture moves up, a strip opens at the bottom */
  if (dy > 0) { y0 = h - dy; y1 = h - 1; } else { y0 = 0; y1 = -dy - 1; }
  r.MinX = 0; r.MinY = (WORD)y0; r.MaxX = (WORD)(w - 1); r.MaxY = (WORD)y1;
  OrRectRegion(rgn, &r);
  InstallClipRegion(d->ly, rgn);
  d->scrolly += dy;
  d->band0 = y0; d->band1 = y1;
  draw_all(d, obj, rp, w, h);
  d->band0 = 0; d->band1 = 0x7FFFFFFF;
  InstallClipRegion(d->ly, NULL);
  DisposeRegion(rgn);
  d->scrolly -= dy;                     /* (the caller sets it, with the scroll bar) */
  update_vruler(d);
  ClipBlit(rp, 0, 0, _rp(obj), _mleft(obj), _mtop(obj), w, h, 0xC0);
  d->caret_on = 1;
  d->scrolly += dy; caret_show(d, obj); d->scrolly -= dy;     /* (where the caret is now) */
  time_note(0, ticks50() - t0);
  return 1;
}
/* scroll so y (pixels from the top of everything) is the view's top, kept within the document:
   blitted when it can be, else redrawn; updates the scroll bars */
static void scroll_to(struct Data *d, Object *obj, LONG y)
{
  LONG h = _mheight(obj), max = d->doch - h;
  if (y > max) y = max;
  if (y < 0) y = 0;
  if (y == d->scrolly) return;
  if (scroll_blit(d, obj, y - d->scrolly)) { d->scrolly = y; update_prop(d); return; }
  d->scrolly = y;
  update_prop(d);
  MUI_Redraw(obj, MADF_DRAWOBJECT);
}
static int obj_box(struct Data *d, Pos at, int k, FloatBox *box);
/* scroll (up and down, and sideways) so the caret, or a selected floating object, is in sight with a
   margin. Only sets scrolly, scrollx and the scroll bars: the caller redraws */
static void make_visible(struct Data *d, Object *obj)
{
  u32 page;
  i32 x, y, h;
  LONG top, bot, vh = _mheight(obj);
  if (!d->lay || vh <= 0) return;
  lay_caret(d->lay, d->caret, &page, &x, &y, &h);
  {
    /* a floating object selected: its box shown, not its anchor in the text */
    Pos at;
    FloatBox fb;
    int k = sel_object(d, &at);
    if (k >= 0 && obj_box(d, at, k, &fb) && fb.wrap != WRAP_INLINE) { page = fb.page; y = fb.y; h = fb.h; }
  }
  top = page_top(d, page) + tx(d, y);
  bot = top + tx(d, h);
  if (bot - top > vh - 16 && top >= d->scrolly + 8 && top < d->scrolly + vh - 8) ;      /* taller than the view and its top in sight */
  else if (top < d->scrolly + 8) d->scrolly = top - vh / 3;
  else if (bot > d->scrolly + vh - 8) d->scrolly = bot - vh * 2 / 3;
  if (d->scrolly > d->doch - vh) d->scrolly = d->doch - vh;
  if (d->scrolly < 0) d->scrolly = 0;
  {
    /* sideways too, when the page is wider than the view */
    LONG vw = _mwidth(obj), cx = page_x(d, page, vw) + tx(d, x);
    if (cx < 8) d->scrollx -= 8 - cx + vw / 4;
    else if (cx > vw - 8) d->scrollx += cx - (vw - 8) + vw / 4;
    page_left(d, vw);
  }
  update_prop(d);
}
static int pic_near(struct Data *d, Pos *at);
static void select_word(struct Data *d);
/* centimetres (else inches): as chosen, or as the page suggests (Letter: inches) */
static int metric(struct Data *d) { return d->tb && d->tb->units >= 0 ? d->tb->units : d->doc->page.width != 12240; }
/* the vertical ruler: the caret's page, where it is in the view now */
static void update_vruler(struct Data *d)
{
  struct MUIP_Ruler_SetV v;
  u32 page;
  i32 x, y, h;
  Object *obj = d->obj;
  if (!d->tb || !d->tb->vruler || !d->lay || !obj || !_win(obj)) return;
  lay_caret(d->lay, d->caret, &page, &x, &y, &h);
  memset(&v, 0, sizeof v);
  v.MethodID = MUIM_Ruler_SetV;
  v.y0 = _mtop(obj) + page_top(d, page) - d->scrolly;
  v.zoom = paged(d) ? d->zoom : 0;                 /* draft and web layout: no pages to measure */
  v.pageh = page_of(d, page)->height; v.mtop = page_of(d, page)->top; v.mbottom = page_of(d, page)->bottom;
  v.metric = metric(d);
  DoMethodA(d->tb->vruler, (Msg)&v);
}
/* tell the rest of the window about the caret's place and format: the toolbar's buttons, the status
   line or bar, the navigation pane, the rulers and the ribbon. Called after every move and edit;
   attributes are set with MUIA_NoNotify so their notifications don't send commands back here */
static void update_toolbar(struct Data *d)
{
  struct Toolbar *tb = d->tb;
  Doc *doc = d->doc;
  Para *p;
  CharFmt f;
  int i;
  char st[160];
  if (!tb || !doc || ww_passive(d)) return;     /* (WP25: a split window's other pane works the tools) */
  if (has_sel(d)) { Pos a = sel_lo(d); p = doc->p[a.para]; f = *para_fmt_at(p, a.off); }
  else { p = doc->p[d->caret.para]; f = d->pending ? d->pendfmt : *para_fmt_before(p, d->caret.off); }
  if (tb->bold) SetAttrs(tb->bold, MUIA_NoNotify, TRUE, MUIA_Selected, (f.flags & CF_BOLD) != 0, TAG_DONE);
  if (tb->italic) SetAttrs(tb->italic, MUIA_NoNotify, TRUE, MUIA_Selected, (f.flags & CF_ITALIC) != 0, TAG_DONE);
  if (tb->underline) SetAttrs(tb->underline, MUIA_NoNotify, TRUE, MUIA_Selected, (f.flags & CF_UNDERLINE) != 0, TAG_DONE);
  if (tb->strike) SetAttrs(tb->strike, MUIA_NoNotify, TRUE, MUIA_Selected, (f.flags & CF_STRIKE) != 0, TAG_DONE);
  if (tb->super) SetAttrs(tb->super, MUIA_NoNotify, TRUE, MUIA_Selected, (f.flags & CF_SUPER) != 0, TAG_DONE);
  if (tb->sub) SetAttrs(tb->sub, MUIA_NoNotify, TRUE, MUIA_Selected, (f.flags & CF_SUB) != 0, TAG_DONE);
  for (i = 0; i < 4; i++) if (tb->align[i]) SetAttrs(tb->align[i], MUIA_NoNotify, TRUE, MUIA_Selected, p->pf.align == i, TAG_DONE);
  if (tb->bullets) SetAttrs(tb->bullets, MUIA_NoNotify, TRUE, MUIA_Selected, p->pf.list && doc->lists[p->pf.list - 1].lvl[p->pf.level].fmt == NF_BULLET, TAG_DONE);
  if (tb->numbers) SetAttrs(tb->numbers, MUIA_NoNotify, TRUE, MUIA_Selected, p->pf.list && doc->lists[p->pf.list - 1].lvl[p->pf.level].fmt != NF_BULLET, TAG_DONE);
  if (tb->size) {
    int best = -1;
    for (i = 0; i < tb->nsizes; i++) if (tb->sizes[i] == f.size) best = i;
    if (best >= 0) SetAttrs(tb->size, MUIA_NoNotify, TRUE, MUIA_Cycle_Active, best, TAG_DONE);
  }
  if (tb->font) {
    for (i = 0; i < tb->nfonts; i++) if (f.font < doc->nfonts && str_ieq(tb->fonts[i], doc->fonts[f.font])) break;
    SetAttrs(tb->font, MUIA_NoNotify, TRUE, MUIA_Cycle_Active, i, TAG_DONE);      /* i == nfonts: "(other)" */
  }
  if (tb->style) {
    for (i = 0; i < tb->nstyle_map; i++) if (tb->style_map[i] == p->pf.style) { SetAttrs(tb->style, MUIA_NoNotify, TRUE, MUIA_Cycle_Active, i, TAG_DONE); break; }
  }
  if (tb->status && d->lay) {
    u32 page, k;
    i32 x, y, h;
    char fam[48];
    static char statusbuf[200];
    lay_caret(d->lay, d->caret, &page, &x, &y, &h);
    /* counting a long document's words takes a while on a 68040: only when typing pauses (see Blink) */
    if (d->words_stale && d->idle >= 2) { d->words = doc_count_words(doc); d->words_stale = 0; }
    utf8_to_latin1(f.font < doc->nfonts ? doc->fonts[f.font] : "?", strlen(f.font < doc->nfonts ? doc->fonts[f.font] : "?"), fam, sizeof fam);
    k = (u32)f.size;
    {
      char words[24];
      if (d->words_stale && !d->words) strcpy(words, "...");             /* not counted yet */
      else sprintf(words, "%lu", (unsigned long)d->words);
      sprintf(st, "Page %lu of %lu%s   %s words   %s %lu%s pt%s   %ld%%", (unsigned long)page + 1, (unsigned long)d->lay->npages,
              d->lay->complete ? "" : "+", words, fam, (unsigned long)(k / 2), k & 1 ? ".5" : "",
              in_table(&p->pf) ? "   Table" : "", (long)d->zoom);
    }
    strcpy(statusbuf, st);
    SetAttrs(tb->status, MUIA_NoNotify, TRUE, MUIA_Text_Contents, (ULONG)statusbuf, TAG_DONE);
  }
  if ((tb->statusbar || tb->nav) && d->lay) {
    u32 page;
    i32 x, y, h;
    lay_caret(d->lay, d->caret, &page, &x, &y, &h);
    if (tb->statusbar) {
      struct MUIP_Status_Set ss;
      /* counting a long document's words takes a while on a 68040: only when typing pauses (see Blink) */
      if (d->words_stale && d->idle >= 2) { d->words = doc_count_words(doc); d->words_stale = 0; }
      memset(&ss, 0, sizeof ss);
      ss.MethodID = MUIM_Status_Set;
      ss.page = page; ss.pages = d->lay->npages; ss.complete = d->lay->complete;
      ss.words = d->words_stale && !d->words ? ~0UL : d->words;
      ss.zoom = d->zoom; ss.vmode = shown_mode(d);
      DoMethodA(tb->statusbar, (Msg)&ss);
    }
    if (tb->nav) {
      struct MUIP_Nav_Update nu;
      memset(&nu, 0, sizeof nu);
      nu.MethodID = MUIM_Nav_Update;
      nu.doc = doc; nu.caretpara = d->caret.para; nu.page = page; nu.npages = d->lay->npages;
      DoMethodA(tb->nav, (Msg)&nu);
    }
  }
  if (tb->ruler && d->lay && d->obj && _win(d->obj)) {
    struct MUIP_Ruler_Set r;
    Object *obj = d->obj;
    u32 rpage;
    i32 rx_, ry_, rh_;
    LONG pl;
    const PageSetup *rpg;
    lay_caret(d->lay, d->caret, &rpage, &rx_, &ry_, &rh_);
    rpg = page_of(d, rpage);
    pl = _mleft(obj) + page_x(d, rpage, _mwidth(obj));
    Table *t = doc_table(doc, &p->pf);
    memset(&r, 0, sizeof r);
    r.MethodID = MUIM_Ruler_Set;
    r.zoom = d->zoom; r.page0 = pl; r.pagew = tx(d, page_of(d, rpage)->width);
    r.mleft = rpg->left; r.mright = rpg->right;
    r.width = d->vmode == VM_WEB && d->lay->webw > 0 ? d->lay->webw : rpg->width - rpg->left - rpg->right;
    if (!paged(d)) { r.page0 = pl + tx(d, rpg->left); r.pagew = tx(d, r.width); r.mleft = r.mright = 0; }
    r.x0 = pl + tx(d, rpg->left);
    if (t && paged(d) && d->caret.para < d->lay->npl && d->lay->pl[d->caret.para].placed0 < d->lay->nplaced) {
      /* in a table: the cell's text area as laid out (nested and centred tables too), and the table's column edges */
      Placed *pc = &d->lay->placed[d->lay->pl[d->caret.para].placed0];
      i32 cx = 0;
      int c;
      r.x0 = pl + tx(d, rpg->left + pc->x);
      r.width = pc->w;
      for (c = 0; c < p->pf.cell.col && c < t->ncols; c++) cx += t->grid[c];
      r.tblx0 = r.x0 - tx(d, cx + t->marl);
      r.ncolx = t->ncols + 1; r.table = p->pf.cell.table;
      r.colx[0] = 0;
      for (c = 0; c < t->ncols && c < 64; c++) r.colx[c + 1] = r.colx[c] + t->grid[c];
    } else if (paged(d) && d->caret.para < d->lay->npl && d->lay->pl[d->caret.para].placed0 < d->lay->nplaced) {
      /* in a section's column: that column */
      ParaLay *cpl = &d->lay->pl[d->caret.para];
      u32 li = 0;
      Placed *pc;
      while (li + 1 < cpl->nlines && d->caret.off >= cpl->lines[li + 1].start) li++;
      pc = &d->lay->placed[cpl->placed0 + li < d->lay->nplaced ? cpl->placed0 + li : cpl->placed0];
      if (pc->w < r.width) { r.x0 = pl + tx(d, rpg->left + pc->x); r.width = pc->w; }
    }
    r.left = p->pf.left; r.first = p->pf.first; r.right = p->pf.right; r.tabs = p->pf.tabs; r.tabkind = p->pf.tabkind; r.tablead = p->pf.tablead;
    r.metric = metric(d);
    r.view = obj;
    DoMethodA(tb->ruler, (Msg)&r);
  }
  if (tb->ribbon) {
    struct MUIP_Ribbon_State rs;
    Pos at;
    memset(&rs, 0, sizeof rs);
    rs.MethodID = MUIM_Ribbon_State;
    rs.on = tb->uibits;
    rs.on2 = tb->uibits2;
    if (f.flags & CF_BOLD) rs.on |= RBIT(RB_BOLD);
    if (f.flags & CF_ITALIC) rs.on |= RBIT(RB_ITALIC);
    if (f.flags & CF_UNDERLINE) rs.on |= RBIT(RB_UNDERLINE);
    if (f.flags & CF_STRIKE) rs.on |= RBIT(RB_STRIKE);
    if (f.flags & CF_SUB) rs.on |= RBIT(RB_SUB);
    if (f.flags & CF_SUPER) rs.on |= RBIT(RB_SUPER);
    if (p->pf.list) rs.on |= RBIT(doc->lists[p->pf.list - 1].lvl[p->pf.level].fmt == NF_BULLET ? RB_BULLETS : RB_NUMBERS);
    if (p->pf.align <= AL_JUSTIFY) rs.on |= RBIT(RB_ALIGN0 + p->pf.align);     /* (Distributed: WP10's own state) */
    if (in_table(&p->pf)) {
      Table *ct = doc_table(doc, &p->pf);
      rs.on |= RBIT(RB_INTABLE) | RBIT(RB_VALIGN0 + (p->pf.cell.valign % 3));
      if (p->pf.cell.header) rs.on |= RBIT(RB_HEADROW);
      if (ct) {
        int k;
        if (ct->look & LK_FIRSTROW) rs.on2 |= RBIT(RB_LK_FIRSTROW - 32);
        if (ct->look & LK_LASTROW) rs.on2 |= RBIT(RB_LK_LASTROW - 32);
        if (!(ct->look & LK_NOHBAND)) rs.on2 |= RBIT(RB_LK_BANDROWS - 32);
        if (ct->look & LK_FIRSTCOL) rs.on2 |= RBIT(RB_LK_FIRSTCOL - 32);
        if (ct->look & LK_LASTCOL) rs.on2 |= RBIT(RB_LK_LASTCOL - 32);
        if (!(ct->look & LK_NOVBAND)) rs.on2 |= RBIT(RB_LK_BANDCOLS - 32);
        if (ct->tstyle) rs.on2 |= RBIT(RB_TSTYLED - 32);
        rs.rowh = p->pf.cell.rowh;
        for (k = p->pf.cell.col; k < p->pf.cell.col + p->pf.cell.span && k < ct->ncols; k++) rs.colw += ct->grid[k];
      }
    }
    if (d->tgrid) rs.on2 |= RBIT(RB_TGRID - 32);
    if (has_sel(d)) rs.on |= RBIT(RB_SELECTION);
    if (d->marks) rs.on |= RBIT(RB_MARKS);
    if (d->brush) rs.on |= RBIT(RB_BRUSH);
    if (d->grid) rs.on |= RBIT(RB_GRID);
    if (d->darkpage) rs.on |= RBIT(RB_DARKPAGE);
    if (doc->ncomments) rs.on |= RBIT(RB_COMMENTS);
    if (doc->nrevs) rs.on |= RBIT(RB_CHANGES);
    if (doc_can_undo(doc)) rs.on |= RBIT(RB_CANUNDO);
    if (doc_can_redo(doc)) rs.on2 |= RBIT(RB_CANREDO - 32);
    if (pic_near(d, &at)) rs.on |= RBIT(RB_PICTURE);
    {
      int ok = sel_object(d, NULL);
      if (ok >= 0 && ok < doc->nimages) rs.on2 |= RBIT((doc->images[ok].kind == OBJ_PICTURE || OBJ_IS_GRAPHIC(doc->images[ok].kind) ? RB_PICSEL : RB_SHAPESEL) - 32);   /* (WP15a: charts as pictures) */
      if (ok >= 0 && ok < doc->nimages && doc->images[ok].kind == OBJ_GROUP) rs.on2 |= RBIT(RB_GROUPSEL - 32);
      if (d->nmsel >= 2) rs.on2 |= RBIT(RB_MULTISEL - 32);
      if (d->nmsel >= 2 || (ok >= 0 && ok < doc->nimages && doc->images[ok].wrap != WRAP_INLINE)) rs.on2 |= RBIT(RB_FLOATSEL - 32);
      if (ok >= 0 && ok < doc->nimages && doc->images[ok].kind != OBJ_PICTURE && doc->images[ok].kind != OBJ_GROUP && !OBJ_IS_GRAPHIC(doc->images[ok].kind)) rs.on2 |= RBIT(RB_ONESHAPE - 32);
    }
    rs.font = f.font < doc->nfonts ? doc->fonts[f.font] : "";
    rs.size = f.size;
    rs.style = p->pf.style;
    rs.left = p->pf.left; rs.right = p->pf.right; rs.before = p->pf.before; rs.after = p->pf.after;
    rs.metric = metric(d);
    rs.doc = doc; rs.fs = d->fs; rs.vmode = shown_mode(d);
    {
      int i;
      u32 sig = (u32)doc->nstyles;
      for (i = 0; i < doc->nstyles; i++) {
        const Style *st = &doc->styles[i];
        sig = sig * 31 + st->type + st->quick * 7 + st->cf.font * 131 + st->cf.size * 17 + st->cf.flags * 3 + st->cf.color + st->cf.fx * 5 + (u32)st->name[0] * 11;
      }
      rs.stylesig = sig;
    }
    { WpCtx wc; wp_ctx(d, &wc, WPC_TEXT); wp_state(rs.wpon, &wc); }    /* the work packages' own states (wphooks.h) */
    DoMethodA(tb->ribbon, (Msg)&rs);
  }
  if (d->obj && (in_table(&p->pf) != 0) != d->intable) { d->intable = in_table(&p->pf) != 0; set(d->obj, MUIA_Quill_InTable, d->intable); }
}
/* laying out the rest of a long document in the background, a little at a time */
static void bg_start(struct Data *d, Object *obj)
{
  if (!d->bg_on && d->handlers && d->lay && !d->lay->complete) {
    DoMethod(_app(obj), MUIM_Application_AddInputHandler, (ULONG)&d->bgn);
    d->bg_on = 1;
  }
}
/* (stopped when the layout is complete, and at Cleanup) */
static void bg_stop(struct Data *d, Object *obj)
{
  if (d->bg_on) { DoMethod(_app(obj), MUIM_Application_RemInputHandler, (ULONG)&d->bgn); d->bg_on = 0; }
}
#define FIRST_PARAS 40                  /* laid out before the first page shows */
/* lay out again after an edit or a change of width, zoom or view mode (zoom fitted first); a new
   document gets its first FIRST_PARAS paragraphs now, the rest in the background */
static void relayout(struct Data *d, Object *obj)
{
  LONG w = _mwidth(obj);
  if (!d->doc) return;
  if (!d->lay) d->lay = lay_new(d->doc, d->fs);
  if (!d->lay) return;
  fit_zoom(d, w > 0 ? w : 640);
  /* web layout: the text as wide as the window */
  d->lay->webw = d->vmode == VM_WEB ? tw(d, (w > 0 ? w : 640) - 2 * WEB_PAD - 4) : 0;
  if (d->vmode == VM_WEB && d->lay->webw < 1440) d->lay->webw = 1440;
  ov_prepare(d);                                 /* WP11: Outline view */
  ww_prepare(d);                                 /* WP25: reflowed Read Mode */
  /* a document still being laid out continues from where it got to; after that, edits re-lay out
     only what changed */
  {
    LONG t0 = qlog ? ticks50() : 0;
    int ok_;
    d->lay->showhidden = (u8)(d->marks != 0);                  /* (WP9: hidden text shown with the formatting marks) */
    ok_ = lay_update_part(d->lay, d->lay->complete ? 0 : FIRST_PARAS);
    ww_laid(d);                                  /* WP25: the version laid out */
    if (qlog) { fprintf(qlog, "[ok %d, free %lu] ", ok_, (unsigned long)AvailMem(MEMF_ANY)); fprintf(qlog, "layout: %lu laid, %lu pages%s, %ld ticks (mode %d, web %ld, zoom %ld)\n", (unsigned long)d->lay->laid, (unsigned long)d->lay->npages, d->lay->complete ? "" : " so far", (long)(ticks50() - t0), d->vmode, (long)d->lay->webw, (long)d->zoom); fflush(qlog); }
  }
  d->doch = doc_height(d);
  if (!d->lay->complete) bg_start(d, obj);
}
/* lay out at least as far as paragraph para (a jump far ahead in a document still being laid out) */
static void ensure_laid(struct Data *d, Object *obj, u32 para)
{
  Layout *L = d->lay;
  if (!L) return;
  while (!L->complete && (para >= L->npl || !L->pl[para].laid)) if (!lay_update_part(L, 200)) break;
  d->doch = doc_height(d);
}
/* MUIM_Quill_Background, every 20 ms while a document is still being laid out: a few more paragraphs,
   and a redraw if pages in view were still coming. Waits while typing is behind or the mouse drags */
static void background(struct Data *d, Object *obj)
{
  Layout *L = d->lay;
  u32 had;
  LONG vh = _mheight(obj);
  if (!L || L->complete || d->stale || d->dragging) { if (!L || L->complete) bg_stop(d, obj); return; }
  had = L->npages;
  lay_update_part(L, 25);
  d->doch = doc_height(d);
  update_prop(d);
  /* the pages on screen were still being laid out: draw them again */
  if (page_top(d, had ? had - 1 : 0) < d->scrolly + vh) { caret_hide(d, obj); MUI_Redraw(obj, MADF_DRAWOBJECT); }
  if (L->complete || had != L->npages) update_toolbar(d);
  if (L->complete) bg_stop(d, obj);
}
/* after an edit or a move: layout, scrolling, toolbar, redraw */
static int scroll_blit(struct Data *d, Object *obj, LONG dy);
/* edited: the document changed (laid out again, marked changed for the title). Then the cheapest way
   to the screen: the caret alone, a blitted scroll, or a full MUI_Redraw (which calls MUIM_Draw).
   The first two leave a full drawing for when the user pauses (redraw_later) */
static void refresh(struct Data *d, Object *obj, int edited)
{
  LONG was = d->scrolly, wasx = d->scrollx;
  if (fv_quiet(d)) return;                     /* WP12: a command done for each of several ranges */
  if (edited) {
    d->nmsel = 0;                                /* (the chosen objects' places may have moved) */
    relayout(d, obj);
    d->words_stale = 1;
    d->outline_stale = 1;
    d->idle = 0;
    if (!d->changed) { d->changed = 1; set(obj, MUIA_Quill_Changed, TRUE); }
  }
  d->caret = pos_clamp(d->doc, d->caret);
  d->anchor = pos_clamp(d->doc, d->anchor);
  ov_caret(d);                                 /* WP11: not in a paragraph a collapsed outline hides */
  ensure_laid(d, obj, d->caret.para);
  make_visible(d, obj);
  update_toolbar(d);
  if (!edited && !has_sel(d) && !d->drawn_sel && d->scrolly == was && d->scrollx == wasx && d->back_ok && d->bw == _mwidth(obj) && d->bh == _mheight(obj) && !d->stale) {
    /* only the caret moved, within the view: the picture is right as it is, so just the caret
       (it is XORed on the window, not part of the buffer) */
    caret_hide(d, obj);
    d->caret_on = 1;
    caret_show(d, obj);
    update_vruler(d);
    d->redraw_later = 1; d->idle = 0;
    return;
  }
  if (!edited && !has_sel(d) && !d->drawn_sel && d->scrolly != was && d->scrollx == wasx) {
    /* only the caret moved, taking the view with it: the blitter moves the picture (see scroll_blit) */
    LONG now = d->scrolly;
    d->scrolly = was;
    if (scroll_blit(d, obj, now - was)) { d->scrolly = now; update_prop(d); d->redraw_later = 1; d->idle = 0; return; }
    d->scrolly = now;
  }
  MUI_Redraw(obj, MADF_DRAWOBJECT);
}

/* ------------------------------------------------------------------ */
/* commands */
/* start a pending format (what the next typing gets: Bold pressed with nothing selected) from the
   format at the caret */
static void set_pending(struct Data *d)
{
  if (!d->pending) { d->pendfmt = *para_fmt_before(d->doc->p[d->caret.para], d->caret.off); d->pending = 1; }
}
/* the range a character format applies to: the selection, or the word the caret is in */
static int format_range(struct Data *d, Pos *a, Pos *b)
{
  Para *p = d->doc->p[d->caret.para];
  if (has_sel(d)) { *a = sel_lo(d); *b = sel_hi(d); return 1; }
  if (d->caret.off > 0 && d->caret.off < p->len && p->t[d->caret.off] != ' ' && p->t[d->caret.off - 1] != ' ') {
    *a = pos_word_left(d->doc, d->caret);
    *b = d->caret;
    while (b->off < p->len && p->t[b->off] != ' ' && p->t[b->off] != '\t') b->off = (u32)utf8_next(p->t, p->len, b->off);
    return 1;
  }
  return 0;
}
/* a selection over several cells is a block of them (Word's cell selection): formatting, deleting and
   copying take each cell in the block */
#define MAXBLK 512
static u32 blk_first[MAXBLK], blk_end[MAXBLK];
/* the block's cells as paragraph ranges in blk_first and blk_end (end excluded): how many, 0 if the
   selection isn't a block. Static scratch arrays, good until the next call */
static int sel_cells(struct Data *d) { return has_sel(d) ? tbl_block_cells(d->doc, d->anchor, d->caret, blk_first, blk_end, MAXBLK) : 0; }
/* the selected paragraphs as ranges (first and last of each): the selection's, or each cell's of a block */
static int sel_ranges(struct Data *d, u32 *first, u32 *last, int max)
{
  int n = sel_cells(d), k;
  if (n) { for (k = 0; k < n && k < max; k++) { first[k] = blk_first[k]; last[k] = blk_end[k] - 1; } return n < max ? n : max; }
  first[0] = sel_lo(d).para; last[0] = sel_hi(d).para;
  return 1;
}
/* paragraph formatting on the selected paragraphs (one undo step) */
static void sel_para_format(struct Data *d, unsigned mask, const ParaFmt *pf)
{
  static u32 f[MAXBLK], l[MAXBLK];
  int n = sel_ranges(d, f, l, MAXBLK), k;
  doc_begin(d->doc, d->caret);
  for (k = 0; k < n; k++) doc_para_format(d->doc, f[k], l[k], mask, pf);
  doc_end(d->doc, d->caret);
}
/* a paragraph style on the selected paragraphs (one undo step) */
static void sel_apply_style(struct Data *d, int style)
{
  static u32 f[MAXBLK], l[MAXBLK];
  int n = sel_ranges(d, f, l, MAXBLK), k;
  doc_begin(d->doc, d->caret);
  for (k = 0; k < n; k++) doc_apply_style(d->doc, f[k], l[k], style);
  doc_end(d->doc, d->caret);
}
/* character formatting: the CM_ fields in mask set from v, or the CF_ flags in toggle flipped, on the
   selection, each cell of a block, or the caret's word. With none of those it becomes the pending
   format for the next typing */
static void char_format(struct Data *d, unsigned mask, const CharFmt *v, unsigned toggle)
{
  Pos a, b;
  int n = sel_cells(d);
  if (n) {
    /* a block of cells: each cell's text (a toggle as the first cell turns out) */
    int k;
    CharFmt w;
    doc_begin(d->doc, d->caret);
    for (k = 0; k < n; k++) {
      a.para = blk_first[k]; a.off = 0; b.para = blk_end[k] - 1; b.off = d->doc->p[b.para]->len;
      if (toggle && k == 0) {
        doc_toggle(d->doc, a, b, toggle);
        memset(&w, 0, sizeof w); w.flags = (u8)(para_fmt_at(d->doc->p[a.para], 0)->flags & toggle);
      } else if (toggle) doc_format(d->doc, a, b, toggle, &w);
      else doc_format(d->doc, a, b, mask, v);
    }
    doc_end(d->doc, d->caret);
    return;
  }
  if (format_range(d, &a, &b)) {
    if (toggle) doc_toggle(d->doc, a, b, toggle);
    else doc_format(d->doc, a, b, mask, v);
  } else {
    set_pending(d);
    if (toggle) d->pendfmt.flags ^= (u8)toggle;
    else {
      cf_apply(&d->pendfmt, mask & ~(CM_LINK | CM_BMARK | CM_REV), v);      /* (every field, WP9's too) */
    }
  }
}
/* bullets (numbered 0) or numbering on the selected paragraphs, or off if the first is already that
   kind of list */
static void list_toggle(struct Data *d, int numbered)
{
  Doc *doc = d->doc;
  Pos a = sel_lo(d), b = sel_hi(d);
  Para *p = doc->p[a.para];
  ParaFmt pf = p->pf;
  int on = p->pf.list && ((doc->lists[p->pf.list - 1].lvl[0].fmt == NF_BULLET) != numbered);
  if (on) {                                /* off: back to the style's indents */
    const Style *st = &doc->styles[p->pf.style < doc->nstyles ? p->pf.style : 0];
    pf.list = 0; pf.level = 0; pf.left = st->pf.left; pf.first = st->pf.first;
  } else {
    /* continue a list of the same kind just above, else a new one */
    int l = 0;
    if (a.para > 0) {
      Para *q = doc->p[a.para - 1];
      if (q->pf.list && (doc->lists[q->pf.list - 1].lvl[0].fmt != NF_BULLET) == numbered) l = q->pf.list;
    }
    if (!l) l = doc_new_list(doc, numbered);
    if (!l) return;
    pf.list = (u16)l; pf.level = 0;
    pf.left = doc->lists[l - 1].lvl[0].left; pf.first = -doc->lists[l - 1].lvl[0].hanging;
  }
  doc_para_format(doc, a.para, b.para, PM_LIST | PM_INDENT, &pf);
}
/* the selected paragraphs in (dir 1) or out (-1): a list item a level deeper or up, others by half
   an inch (720 twips). One undo step */
static void indent(struct Data *d, int dir)
{
  Doc *doc = d->doc;
  u32 i, a = sel_lo(d).para, b = sel_hi(d).para;
  doc_begin(doc, d->caret);
  for (i = a; i <= b; i++) {
    ParaFmt pf = doc->p[i]->pf;
    if (pf.list) {
      int lv = pf.level + dir;
      if (lv < 0 || lv > 8) continue;
      pf.level = (u8)lv;
      pf.left = doc->lists[pf.list - 1].lvl[lv].left; pf.first = -doc->lists[pf.list - 1].lvl[lv].hanging;
      doc_para_format(doc, i, i, PM_LIST | PM_INDENT, &pf);
    } else {
      pf.left += dir * 720;
      if (pf.left < 0) pf.left = 0;
      doc_para_format(doc, i, i, PM_INDENT, &pf);
    }
  }
  doc_end(doc, d->caret);
}
/* the selection to the clipboard as Latin-1 text (a block of cells: tabs between cells, a newline
   after each row), and kept as a Fragment with its formatting for pasting back here */
static void copy_sel(struct Data *d)
{
  u32 n;
  char *t, *l;
  if (!has_sel(d)) return;
  {
    int nc = sel_cells(d), k;
    if (nc) {
      /* a block of cells: rows of cells' text, tabs between them */
      Buf o;
      u16 bt = d->doc->p[d->anchor.para]->pf.cell.table;
      buf_init(&o);
      for (k = 0; k < nc; k++) {
        Pos s, e;
        u32 m;
        char *ct;
        if (k) {
          const Cell *x = cell_in(&d->doc->p[blk_first[k - 1]]->pf, bt), *y = cell_in(&d->doc->p[blk_first[k]]->pf, bt);
          buf_adds(&o, x && y && x->row == y->row ? "\t" : "\n");
        }
        s.para = blk_first[k]; s.off = 0; e.para = blk_end[k] - 1; e.off = d->doc->p[e.para]->len;
        if ((ct = doc_text(d->doc, s, e, &m))) { buf_add(&o, ct, m); free(ct); }
      }
      buf_adds(&o, "\n");
      if (!o.oom && (l = (char *)malloc(o.len * 3 + 4))) { size_t mm = utf8_to_latin1(o.data, (u32)o.len, l, o.len * 3 + 4); clip_write(l, (long)mm); free(l); }
      buf_free(&o);
      return;
    }
  }
  if (!(t = doc_text(d->doc, sel_lo(d), sel_hi(d), &n))) return;
  if ((l = (char *)malloc(n * 3 + 4))) { size_t m = utf8_to_latin1(t, n, l, n * 3 + 4); clip_write(l, (long)m); free(l); }
  free(t);
  /* and kept with its formatting, for pasting here */
  fragment_free(d->frag);
  d->frag = doc_fragment(d->doc, sel_lo(d), sel_hi(d));
  wp10_copied(d->doc, sel_lo(d), sel_hi(d));          /* (WP10: the Clipboard pane keeps it) */
}
/* a range that can be deleted by joining paragraphs: not across table cells */
static int flat_range(Doc *doc, Pos a, Pos b)
{
  u32 i;
  if (a.para == b.para || same_cell(&doc->p[a.para]->pf, &doc->p[b.para]->pf)) {
    if (!in_table(&doc->p[a.para]->pf)) for (i = a.para; i <= b.para; i++) if (in_table(&doc->p[i]->pf)) return 0;
    return 1;
  }
  return 0;
}
/* tracking changes: deletions are marked, insertions carry the author's mark */
static int tracking(struct Data *d) { return (d->tb && (d->tb->uibits & RBIT(RB_TRACK))) || prot_forces_tracking(d->doc); }   /* (WP7: tracked changes only) */
/* the revision (author and date, from the options) new changes are marked with, added if new; vdelete:
   a deletion within a paragraph or across plain ones, only marked while tracking */
static int rev_id(struct Data *d) { return doc_add_rev(d->doc, d->tb && d->tb->author ? d->tb->author : prot_user, d->tb && d->tb->revdate ? d->tb->revdate : ""); }
static void vdelete(struct Data *d, Pos a, Pos b)
{
  if (tracking(d)) doc_track_delete(d->doc, a, b, rev_id(d));
  else doc_delete(d->doc, a, b);
}
/* delete a to b without breaking tables (across cells each paragraph's part is emptied instead of
   paragraphs being joined; one undo step); marked instead while tracking */
static void delete_range(struct Data *d, Pos a, Pos b)
{
  Doc *doc = d->doc;
  u32 i;
  if (tracking(d)) { doc_track_delete(doc, a, b, rev_id(d)); return; }
  if (flat_range(doc, a, b)) { doc_delete(doc, a, b); return; }
  /* across cells (or into a table): each paragraph's part is emptied and the cells stay */
  doc_begin(doc, a);
  for (i = b.para + 1; i-- > a.para; ) {
    Para *p = doc->p[i];
    Pos s, e;
    s.para = e.para = i;
    s.off = i == a.para ? a.off : 0; e.off = i == b.para ? b.off : p->len;
    if (e.off > s.off) doc_delete(doc, s, e);
  }
  doc_end(doc, a);
}
/* delete the selection (a block of cells: what is in them); the caret goes where it began */
static void delete_sel(struct Data *d)
{
  Pos a = sel_lo(d), b = sel_hi(d);
  int n = sel_cells(d), k;
  if (!has_sel(d)) return;
  if (n) {                                 /* a block of cells: what's in each goes, the cells stay */
    doc_begin(d->doc, d->caret);
    for (k = n - 1; k >= 0; k--) {
      Pos s, e;
      s.para = blk_first[k]; s.off = 0; e.para = blk_end[k] - 1; e.off = d->doc->p[e.para]->len;
      if (pos_cmp(s, e)) delete_range(d, s, e);
    }
    doc_end(d->doc, d->caret);
    d->caret.para = blk_first[0]; d->caret.off = 0;
    d->caret = d->anchor = pos_clamp(d->doc, d->caret);
    return;
  }
  delete_range(d, a, b);
  d->caret = d->anchor = a;
}
/* select the contents of the cell paragraph pi is in (Tab into a cell, as Word) */
static void select_cell(struct Data *d, u32 pi)
{
  u32 e = cell_end(d->doc, pi) - 1;
  d->anchor.para = cell_first(d->doc, pi); d->anchor.off = 0;
  d->caret.para = e; d->caret.off = d->doc->p[e]->len;
}
/* type n bytes of UTF-8 at the caret, replacing the selection (in one undo step with it), in the
   pending format if any; the caret ends after it */
/* text typed just after a link isn't part of it (as in Word): the format before the caret without
   the link, and without the Hyperlink style's look (the paragraph style's colour and underline back) */
static const CharFmt *past_link(struct Data *d, const CharFmt *near, CharFmt *plain)
{
  Pos la, lb;
  const CharFmt *pc;
  int hs;
  if (!near->link || !doc_link_at(d->doc, d->caret, &la, &lb) || lb.off != d->caret.off) return near;
  *plain = *near;
  plain->link = 0;
  hs = doc_style(d->doc, "Hyperlink");
  if (hs > 0 && plain->cstyle == hs) {
    pc = &d->doc->styles[d->doc->p[d->caret.para]->pf.style < d->doc->nstyles ? d->doc->p[d->caret.para]->pf.style : 0].cf;
    plain->cstyle = 0; plain->color = pc->color; plain->ul = pc->ul;
    plain->flags = (u8)((plain->flags & ~CF_UNDERLINE) | (pc->flags & CF_UNDERLINE));
  }
  return plain;
}
static void insert_text(struct Data *d, const char *s, u32 n)
{
  int grouped = has_sel(d);
  const CharFmt *near;
  CharFmt plain;
  if (grouped) { doc_begin(d->doc, d->caret); delete_sel(d); }
  near = para_fmt_before(d->doc->p[d->caret.para], d->caret.off);
  if (!d->pending && near->link && (near = past_link(d, near, &plain)) == &plain) { d->pendfmt = plain; d->pending = 1; }
  if (tracking(d) || near->rev || (d->pending && d->pendfmt.rev)) {
    /* the new text is the author's insertion when tracking, and never part of a neighbouring change otherwise */
    CharFmt f = d->pending ? d->pendfmt : *near;
    if (tracking(d)) { f.rev = REV_INS; f.rid = (u16)rev_id(d); f.frev = 0; } else { f.rev = REV_NONE; f.rid = 0; }
    f.move = 0;                                   /* (WP5: new text is no formatting change, nor a move) */
    doc_insert(d->doc, &d->caret, s, n, &f);
  } else doc_insert(d->doc, &d->caret, s, n, d->pending ? &d->pendfmt : NULL);
  if (grouped) doc_end(d->doc, d->caret);
  d->anchor = d->caret;
  d->pending = 0;
}
/* the last copy's text, as the clipboard has it (Latin-1): still the same? */
static int frag_current(struct Data *d, const char *clip, long n)
{
  u32 fn;
  const char *ft;
  char *l;
  int same;
  if (!d->frag) return 0;
  ft = fragment_text(d->frag, &fn);
  if (!(l = (char *)malloc(fn * 3 + 4))) return 0;
  same = (long)utf8_to_latin1(ft, fn, l, fn * 3 + 4) == n && !memcmp(l, clip, (size_t)n);
  free(l);
  return same;
}
/* paste the clipboard: our own last copy with its formatting (mode: PASTE_), anything else as plain
   text, Latin-1 made UTF-8. A beep if the clipboard has no text */
static void paste_as(struct Data *d, int mode)
{
  long n, i, o = 0;
  char *t = clip_read(&n), *u;
  if (!t) { DisplayBeep(NULL); return; }
  if (frag_current(d, t, n)) {
    /* copied here: with its formatting (one undo step with deleting the selection) */
    Pos at;
    doc_begin(d->doc, d->caret);
    if (has_sel(d)) delete_sel(d);
    at = d->caret;
    if (doc_paste(d->doc, &at, d->frag, mode)) { d->caret = d->anchor = at; }
    doc_end(d->doc, d->caret);
    d->pending = 0;
    free(t);
    return;
  }
  if ((u = (char *)malloc((size_t)n * 2 + 1))) {               /* Latin-1 -> UTF-8 */
    for (i = 0; i < n; i++) o += utf8_put(u + o, (u8)t[i]);
    insert_text(d, u, (u32)o);
    free(u);
  }
  free(t);
}
/* paste keeping the formatting; find: the next match of what after the selection (FIND_BACK: before
   it) selected, 0 if there is none */
static void paste(struct Data *d) { paste_as(d, PASTE_KEEP); }
static int find(struct Data *d, const char *what, unsigned flags)
{
  Pos a, b, from = (flags & FIND_BACK) ? sel_lo(d) : sel_hi(d);
  if (!doc_find(d->doc, from, what, flags, &a, &b)) return 0;
  d->anchor = a; d->caret = b;
  return 1;
}
/* does the selection match what (as doc_find would)? */
static int sel_matches(struct Data *d, const char *what, unsigned flags)
{
  Pos a, b, lo = sel_lo(d);
  return has_sel(d) && doc_find(d->doc, lo, what, flags & ~FIND_BACK, &a, &b) && !pos_cmp(a, lo) && !pos_cmp(b, sel_hi(d));
}
/* the width text has where paragraph pi is: its cell's, or the page's, less the indents */
static i32 column_width(Doc *doc, u32 pi)
{
  Para *p = doc->p[pi];
  Table *t = doc_table(doc, &p->pf);
  i32 w = para_page(doc, pi)->width - para_page(doc, pi)->left - para_page(doc, pi)->right;
  if (t) {
    int c;
    w = -t->marl - t->marr;
    for (c = p->pf.cell.col; c < p->pf.cell.col + p->pf.cell.span && c < t->ncols; c++) w += t->grid[c];
  }
  w -= p->pf.left + p->pf.right;
  return w < 720 ? 720 : w;
}
/* the picture selected, or just before or after the caret: its position, or -1 */
static int pic_near(struct Data *d, Pos *at)
{
  Para *p = d->doc->p[d->caret.para];
  Pos lo = sel_lo(d), hi = sel_hi(d);
  if (has_sel(d) && lo.para == hi.para && hi.off - lo.off == 4 && IS_PIC(d->doc->p[lo.para]->t + lo.off)) { *at = lo; return 1; }
  if (has_sel(d)) return 0;
  *at = d->caret;
  if (d->caret.off >= 4 && IS_PIC(p->t + d->caret.off - 4)) { at->off -= 4; return 1; }
  if (d->caret.off + 4 <= p->len && IS_PIC(p->t + d->caret.off)) return 1;
  return 0;
}
/* the document laid out on pages, up to date: the view's own layout, or in web layout a second one */
static Layout *paged_layout(struct Data *d)
{
  if (!d->doc) return NULL;
  if (d->vmode != VM_WEB) {
    if (!d->lay) d->lay = lay_new(d->doc, d->fs);
    if (d->lay) lay_update(d->lay);
    return d->lay;
  }
  if (!d->plain) d->plain = lay_new(d->doc, d->fs);
  if (d->plain) lay_update(d->plain);
  return d->plain;
}
/* forget the second layout (the document or the view mode changed) */
static void drop_plain(struct Data *d) { if (d->plain) { lay_free(d->plain); d->plain = NULL; } }
/* table helpers for the commands: the first paragraph of the cell at row r covering grid column c of
   table t (whose paragraphs are a..b), or b if none; the last paragraph of a row */
static u32 cell_para_at(Doc *doc, u16 t, u32 a, u32 b, int r, int c)
{
  u32 i;
  for (i = a; i < b; i++) {
    const Cell *x = cell_in(&doc->p[i]->pf, t);
    if (x && x->row == r && c >= x->col && c < x->col + x->span && (i == a || !cell_in(&doc->p[i - 1]->pf, t) || cell_in(&doc->p[i - 1]->pf, t)->col != x->col ||
                                                                     cell_in(&doc->p[i - 1]->pf, t)->row != x->row)) return i;
  }
  return b;
}
static u32 row_last(Doc *doc, u16 t, u32 a, u32 b, int r)
{
  u32 i, last = b;
  for (i = a; i < b; i++) { const Cell *x = cell_in(&doc->p[i]->pf, t); if (x && x->row == r) last = i; }
  return last;
}
/* the rows and grid columns the selection covers in the caret's table (the caret's cell alone if
   nothing is selected) */
static void sel_rect(struct Data *d, int *r0, int *r1, int *c0, int *c1)
{
  u16 t;
  const Cell *c = &d->doc->p[d->caret.para]->pf.cell;
  if (!has_sel(d) || !tbl_block(d->doc, d->anchor, d->caret, &t, r0, r1, c0, c1)) { *r0 = *r1 = c->row; *c0 = c->col; *c1 = c->col + c->span - 1; }
}
/* the width a table in the caret's place may have: the text area's, or the cell's it is nested in */
static i32 table_room(Doc *doc, u32 pi)
{
  const ParaFmt *pf = &doc->p[pi]->pf;
  if (is_nested(pf)) {
    Table *o = &doc->tables[pf->outer.table - 1];
    i32 w = -o->marl - o->marr;
    int c;
    for (c = pf->outer.col; c < pf->outer.col + pf->outer.span && c < o->ncols; c++) w += o->grid[c];
    return w > 720 ? w : 720;
  }
  return para_page(doc, pi)->width - para_page(doc, pi)->left - para_page(doc, pi)->right;
}
/* MUIM_Quill_Command: the menus, ribbon, panes and dialogs send every command here (m->cmd a CMD_,
   m->arg a value or a pointer to the command's struct). A command changes the document, then one
   refresh at the end lays out and redraws; those that change nothing set edited to 0 */
static ULONG do_command(struct IClass *cl, Object *obj, struct MUIP_Quill_Command *m)
{
  struct Data *d = INST_DATA(cl, obj);
  Doc *doc = d->doc;
  CharFmt v;
  int edited = 1;
  if (!doc) return 0;
  ww_sync(d, obj, 1);                          /* WP25: changed in another view */
  if (ww_command(d, obj, m)) return 0;         /* WP25's commands */
  if (ov_command(d, obj, m)) return 0;         /* WP11's commands */
  if (ev_command(d, obj, m)) return 0;         /* WP14b's commands, and editing commands inside an equation */
  if (fv_command(d, obj, m)) return 0;         /* WP12's commands, and commands while several ranges are selected */
  caret_hide(d, obj);
  memset(&v, 0, sizeof v);
  doc->rvtrack = (u16)(tracking(d) ? rev_id(d) + 1 : 0);       /* (WP5: formatting changes tracked too) */
  if (qlog) {
    fprintf(qlog, "command %lu arg %ld: caret %lu.%lu anchor %lu.%lu, flags before %d\n", (unsigned long)m->cmd, (long)m->arg,
            (unsigned long)d->caret.para, (unsigned long)d->caret.off, (unsigned long)d->anchor.para, (unsigned long)d->anchor.off,
            para_fmt_at(doc->p[sel_lo(d).para], sel_lo(d).off)->flags);
    fflush(qlog);
  }
  switch (m->cmd) {
  case CMD_BOLD: char_format(d, 0, NULL, CF_BOLD); break;
  case CMD_ITALIC: char_format(d, 0, NULL, CF_ITALIC); break;
  case CMD_UNDERLINE: char_format(d, 0, NULL, CF_UNDERLINE); break;
  case CMD_STRIKE: char_format(d, 0, NULL, CF_STRIKE); break;
  case CMD_CBORDER: {   /* on, or off if the selection's start (or the caret) already has one */
    CharFmt v;
    Pos a = sel_lo(d);
    const CharFmt *f = has_sel(d) ? para_fmt_at(doc->p[a.para], a.off) : d->pending ? &d->pendfmt : para_fmt_before(doc->p[a.para], a.off);
    memset(&v, 0, sizeof v);
    v.fx = (u8)(f->fx & FX_BORDER ? 0 : FX_BORDER);
    char_format(d, CM_CBORDER, &v, 0);
    break; }
  case CMD_SUPER: char_format(d, 0, NULL, CF_SUPER); break;
  case CMD_SUB: char_format(d, 0, NULL, CF_SUB); break;
  case CMD_CAPS: char_format(d, 0, NULL, CF_CAPS); break;
  case CMD_SIZE: v.size = (u16)m->arg; char_format(d, CM_SIZE, &v, 0); break;
  case CMD_FONT: v.font = (u8)doc_font(doc, (const char *)m->arg); char_format(d, CM_FONT, &v, 0); break;
  case CMD_ALIGN: { ParaFmt pf; memset(&pf, 0, sizeof pf); pf.align = (u8)m->arg; sel_para_format(d, PM_ALIGN, &pf); break; }
  case CMD_SPACING: { ParaFmt pf; memset(&pf, 0, sizeof pf); pf.line = m->arg; pf.linerule = LS_AUTO; sel_para_format(d, PM_LINE, &pf); break; }
  case CMD_STYLE:
    if ((int)m->arg >= 0 && (int)m->arg < doc->nstyles && doc->styles[m->arg].type == ST_CHAR) {      /* a character style from the gallery */
      Pos a = sel_lo(d), b = sel_hi(d);
      if (!has_sel(d)) { select_word(d); a = sel_lo(d); b = sel_hi(d); }
      if (!pos_cmp(a, b) || !doc_char_style(doc, a, b, (int)m->arg)) { DisplayBeep(NULL); edited = 0; }
      break;
    }
    sel_apply_style(d, (int)m->arg); d->pending = 0; break;
  case CMD_BULLETS: list_toggle(d, 0); break;
  case CMD_NUMBERS: list_toggle(d, 1); break;
  case CMD_INDENT: indent(d, 1); break;
  case CMD_OUTDENT: indent(d, -1); break;
  case CMD_UNDO: if (!doc_undo(doc, &d->caret)) DisplayBeep(NULL); d->anchor = d->caret; break;
  case CMD_REDO: if (!doc_redo(doc, &d->caret)) DisplayBeep(NULL); d->anchor = d->caret; break;
  case CMD_COPY: copy_sel(d); edited = 0; break;
  case CMD_CUT: copy_sel(d); delete_sel(d); break;
  case CMD_PASTE: paste(d); break;
  case CMD_DELETE: delete_sel(d); break;
  case CMD_SELALL: d->anchor = doc_start(doc); d->caret = doc_end_pos(doc); edited = 0; break;
  case CMD_FIND: if (!find(d, (const char *)m->arg, 0)) DisplayBeep(NULL); edited = 0; break;
  case CMD_FINDX: { struct QFind *q = (struct QFind *)m->arg; q->found = find(d, q->what, q->flags); if (!q->found) DisplayBeep(NULL); edited = 0; break; }
  case CMD_REPLACE1: {
    struct QFind *q = (struct QFind *)m->arg;
    if (sel_matches(d, q->what, q->flags)) insert_text(d, q->with, (u32)strlen(q->with));
    else edited = 0;
    q->found = find(d, q->what, q->flags & ~FIND_BACK);
    break; }
  case CMD_REPLACEALL: {
    struct QFind *q = (struct QFind *)m->arg;
    q->count = doc_replace_all(doc, q->what, q->with, q->flags & ~FIND_BACK);
    if (!q->count) { DisplayBeep(NULL); edited = 0; }
    d->caret = d->anchor = pos_clamp(doc, d->caret);
    break; }
  case CMD_COLUMNS: case CMD_LINENUM: case CMD_PAGEALIGN: case CMD_LINEOPTS: {
    /* section formats: a new one for each section changed, on the paragraph that ends it (undoable) */
    u32 a = 0, b = doc->np - 1, i;
    if (m->cmd == CMD_COLUMNS) { a = b = doc_sect_end(doc, d->caret.para); }
    if (in_table(&doc->p[doc_sect_end(doc, doc->np - 1)]->pf) && b == doc->np - 1) { DisplayBeep(NULL); edited = 0; break; }
    doc_begin(doc, d->caret);
    for (i = a; i <= b; i++) {
      SectFmt s;
      ParaFmt pf;
      if (!doc->p[i]->pf.sect && i != doc->np - 1) continue;
      if (in_table(&doc->p[i]->pf)) continue;
      s = *doc_sect(doc, i);
      if (m->cmd == CMD_COLUMNS) { if (m->arg) s.cols = (u8)m->arg; else s.colline = (u8)!s.colline; }
      else if (m->cmd == CMD_LINENUM) s.linenum = (u8)m->arg;
      else if (m->cmd == CMD_LINEOPTS) {
        const struct QLineNum *q = (const struct QLineNum *)m->arg;
        s.linenum = q->linenum; s.lnby = q->by; s.lnstart = q->start; s.lndist = q->dist;
      }
      else s.valign = (u8)m->arg;
      memset(&pf, 0, sizeof pf);
      if (!(pf.sect = (u16)doc_add_sect(doc, &s))) break;
      doc_para_format(doc, i, i, PM_SECT, &pf);
    }
    doc_end(doc, d->caret);
    break; }
  case CMD_SECTBREAK: {
    /* the paragraph before the caret ends a section like the one it is in; the one after starts as asked */
    SectFmt s, t;
    ParaFmt pf;
    u32 e;
    if (in_table(&doc->p[d->caret.para]->pf)) { DisplayBeep(NULL); edited = 0; break; }
    doc_begin(doc, d->caret);
    delete_sel(d);
    s = *doc_sect(doc, d->caret.para);
    s.titlepg = (u8)sect_titlepg(doc, &s, doc_last_sect(doc, d->caret.para));     /* (Word copies the section's settings) */
    doc_insert(doc, &d->caret, "\n", 1, NULL);
    memset(&pf, 0, sizeof pf);
    if ((pf.sect = (u16)doc_add_sect(doc, &s))) doc_para_format(doc, d->caret.para - 1, d->caret.para - 1, PM_SECT, &pf);
    e = doc_sect_end(doc, d->caret.para);
    t = s; t.start = (u8)m->arg;
    if (!in_table(&doc->p[e]->pf) && (pf.sect = (u16)doc_add_sect(doc, &t))) doc_para_format(doc, e, e, PM_SECT, &pf);
    doc_end(doc, d->caret);
    d->anchor = d->caret;
    break; }
  case CMD_HYPHENATE: doc->hyphenate = (u8)(m->arg != 0); doc->dirty = 1; break;
  case CMD_DROPCAP: {
    ParaFmt pf;
    if (in_table(&doc->p[d->caret.para]->pf)) { DisplayBeep(NULL); edited = 0; break; }
    memset(&pf, 0, sizeof pf);
    pf.dropcap = (u8)((m->arg & 255) ? ((m->arg >> 8) & 255 ? (m->arg >> 8) & 255 : 3) : 0); pf.dropmargin = (u8)((m->arg & 255) == 2);
    doc_para_format(doc, d->caret.para, d->caret.para, PM_DROP, &pf);
    break; }
  case CMD_DESIGN: doc->dirty = 1; break;
  case CMD_COLBREAK:
  case CMD_PAGEBREAK: {
    ParaFmt pf;
    doc_begin(doc, d->caret);
    delete_sel(d);
    doc_insert(doc, &d->caret, "\n", 1, NULL);
    memset(&pf, 0, sizeof pf);
    if (m->cmd == CMD_COLBREAK) pf.colbreak = 1; else pf.pagebreak = 1;
    doc_para_format(doc, d->caret.para, d->caret.para, PM_BREAK, &pf);
    doc_end(doc, d->caret);
    d->anchor = d->caret;
    break; }
  case CMD_TOP: d->caret = d->anchor = doc_start(doc); d->scrolly = 0; edited = 0; break;
  case CMD_END: d->caret = d->anchor = doc_end_pos(doc); edited = 0; break;
  case CMD_LINEBREAK: insert_text(d, "\v", 1); break;
  case CMD_COLOR: v.color = (u32)m->arg; char_format(d, CM_COLOR, &v, 0); break;
  case CMD_HIGHLIGHT: v.hl = (u32)m->arg == COLOR_AUTO ? 0 : HL_ON | ((u32)m->arg & 0xFFFFFF); char_format(d, CM_HL, &v, 0); break;
  case CMD_GROW: case CMD_CLEARFMT: case CMD_CASE: {
    Pos a, b;
    if (!format_range(d, &a, &b)) { edited = 0; DisplayBeep(NULL); break; }
    if (m->cmd == CMD_GROW) doc_grow(doc, a, b, (int)m->arg);
    else if (m->cmd == CMD_CLEARFMT) doc_clear_format(doc, a, b);
    else doc_change_case(doc, a, b, (int)m->arg);
    break; }
  case CMD_MARKS: d->marks = !d->marks; edited = 0; relayout(d, obj); d->back_ok = 0; break;   /* (hidden text takes room while marks show: WP9) */
  case CMD_BRUSH:
    /* the format painter: the formatting at the caret goes to the next text selected (again: off) */
    d->brush = !d->brush;
    d->brushfmt = has_sel(d) ? *para_fmt_at(doc->p[sel_lo(d).para], sel_lo(d).off) : *para_fmt_before(doc->p[d->caret.para], d->caret.off);
    edited = 0;
    break;
  case CMD_TOC: {
    /* page numbers move as the table grows: done again (one undo step) until they settle */
    int again = 0, n = 0;
    Pos at = sel_lo(d);
    Layout *pl;
    doc_begin(doc, d->caret);
    do {
      if (!(pl = paged_layout(d)) || !doc_toc(doc, pl, &at, &again)) { if (!n) DisplayBeep(NULL); break; }
    } while (again && ++n < 3);
    doc_end(doc, d->caret);
    d->caret = d->anchor = pos_clamp(doc, d->caret);
    break; }
  case CMD_NEXTNOTE: {
    /* from the caret on, wrapping round once */
    u32 pi = d->caret.para, off = d->caret.off + (d->caret.off + 4 <= doc->p[d->caret.para]->len && IS_NOTE(doc->p[d->caret.para]->t + d->caret.off) ? 4 : 0), n;
    int found = 0;
    for (n = 0; n <= doc->np && !found; n++, pi = (pi + 1) % doc->np, off = 0) {
      Para *p = doc->p[pi];
      u32 k;
      for (k = off; k + 4 <= p->len; k++) if (IS_NOTE(p->t + k)) { d->caret.para = pi; d->caret.off = k; d->anchor = d->caret; found = 1; break; }
    }
    if (!found) DisplayBeep(NULL);
    edited = 0;
    break; }
  case CMD_MERGE:
    if (!tbl_merge(doc, sel_lo(d), sel_hi(d))) { DisplayBeep(NULL); edited = 0; }
    else { d->caret = d->anchor = pos_clamp(doc, sel_lo(d)); }
    break;
  case CMD_SPLIT: { Pos c = d->caret; if (!tbl_split(doc, &c)) { DisplayBeep(NULL); edited = 0; } else d->caret = d->anchor = c; break; }
  case CMD_VALIGN: case CMD_SHADE: {
    Pos a = sel_lo(d), b = sel_hi(d);
    if (!tbl_cell_format(doc, a, b, m->cmd == CMD_SHADE, (u32)m->arg)) { DisplayBeep(NULL); edited = 0; }
    else { d->anchor = pos_clamp(doc, a); d->caret = pos_clamp(doc, b); }
    break; }
  case CMD_COMMENT: {
    const struct QComment *q = (const struct QComment *)m->arg;
    if (q->edit >= 0 && q->edit < doc->ncomments) {
      /* its text changed (not in the undo history, as a footnote's) */
      int st = doc_style(doc, "CommentText");
      story_set_text(doc, &doc->comments[q->edit].text, q->text, st < 0 ? 0 : st, AL_LEFT);
      doc->dirty = 1; doc->ver++;
    } else {
      Pos a, b;
      if (!format_range(d, &a, &b)) a = b = d->caret;
      if (doc_comment(doc, a, b, q->text, q->author, q->initials, q->date) < 0) { DisplayBeep(NULL); edited = 0; break; }
      d->caret = d->anchor = pos_clamp(doc, b);
      if (doc->ncomments == 1) close_fonts(d);              /* the first: room for the balloons */
    }
    break; }
  case CMD_DELCOMMENT: {
    int k = doc_comment_at(doc, has_sel(d) ? sel_lo(d) : d->caret, NULL, NULL);
    if (k < 0 && has_sel(d)) { Pos q = sel_lo(d); q.off += 4 <= doc->p[q.para]->len - q.off ? 4 : 0; k = doc_comment_at(doc, q, NULL, NULL); }
    if (k < 0 || !doc_delete_comment(doc, k)) { DisplayBeep(NULL); edited = 0; break; }
    d->caret = d->anchor = pos_clamp(doc, d->caret);
    break; }
  case CMD_NEXTCOMMENT: {
    /* the next (arg > 0) or previous comment's range selected */
    u32 pi = d->caret.para, n;
    int dir = (int)m->arg, found = 0;
    Pos at = d->caret;
    for (n = 0; n <= doc->np && !found; n++) {
      Para *q = doc->p[pi];
      if (dir > 0) {
        u32 k0 = pi == d->caret.para && n == 0 ? d->caret.off : 0, k;
        for (k = k0; k + 4 <= q->len; k++) if (IS_CSTART(q->t + k) && !(pi == d->caret.para && n == 0 && k + 4 == d->caret.off)) { at.para = pi; at.off = k; found = 1; break; }
        pi = (pi + 1) % doc->np;
      } else {
        u32 k = pi == d->caret.para && n == 0 ? d->caret.off : q->len;
        while (k >= 4) {
          k = (u32)utf8_prev(q->t, k);
          if (IS_CSTART(q->t + k) && !(pi == d->caret.para && n == 0 && k + 4 >= d->caret.off)) { at.para = pi; at.off = k; found = 1; break; }
        }
        pi = pi ? pi - 1 : doc->np - 1;
      }
    }
    if (found) {
      Pos e;
      int k = note_index(doc->p[at.para]->t + at.off);
      if (!doc_comment_mark(doc, k, 1, &e)) e = at;
      at.off += 4;
      d->anchor = at; d->caret = pos_cmp(e, at) > 0 ? e : at;
    } else DisplayBeep(NULL);
    edited = 0;
    break; }
  case CMD_RESOLVE: {
    /* arg: bit 0 accept (else reject), bits 1-2: 0 this change, 1 all, 2 this and on to the next */
    int accept = (int)(m->arg & 1), how = (int)(m->arg >> 1);
    Pos a, b;
    if (how == 1) { a = doc_start(doc); b = doc_end_pos(doc); }
    else if (has_sel(d)) { a = sel_lo(d); b = sel_hi(d); }
    else {
      /* the change at the caret, or the next one */
      Para *q = doc->p[d->caret.para];
      u32 r, rs = 0;
      a = b = d->caret;
      for (r = 0; r < q->nr; r++) {
        if (rv_is_change(&q->r[r].f) && rs <= d->caret.off && q->r[r].end >= d->caret.off && q->r[r].end > rs) { a.off = rs; b.off = q->r[r].end; break; }
        rs = q->r[r].end;
      }
      if (r == q->nr && !doc_next_change(doc, d->caret, 1, &a, &b)) { DisplayBeep(NULL); edited = 0; break; }
    }
    if (!doc_resolve(doc, a, b, accept)) { DisplayBeep(NULL); edited = 0; break; }
    d->caret = d->anchor = pos_clamp(doc, a);
    if (how == 2 && doc_next_change(doc, d->caret, 1, &a, &b)) { d->anchor = a; d->caret = b; }
    break; }
  case CMD_NEXTCHANGE: {
    Pos a, b;
    if (doc_next_change(doc, (int)m->arg > 0 ? sel_hi(d) : sel_lo(d), (int)m->arg, &a, &b)) { d->anchor = a; d->caret = b; }
    else DisplayBeep(NULL);
    edited = 0;
    break; }
  case CMD_SORT:
    if (!doc_sort(doc, sel_lo(d).para, sel_hi(d).para)) { DisplayBeep(NULL); edited = 0; }
    else { d->anchor.para = sel_lo(d).para; d->anchor.off = 0; d->caret = d->anchor; }
    break;
  case CMD_TEXT: case CMD_REPLACE: insert_text(d, (const char *)m->arg, (u32)strlen((const char *)m->arg)); break;
  case CMD_LINK: case CMD_UNLINK: {
    /* on the selection, or on the whole link the caret is in */
    const struct QLink *q = (const struct QLink *)m->arg;
    Pos a = sel_lo(d), b = sel_hi(d), la, lb, end;
    int ok;
    if (!has_sel(d) && doc_link_at(doc, d->caret, &la, &lb)) { a = la; b = lb; }
    doc_begin(doc, a);
    if (m->cmd == CMD_LINK && q && q->text && q->text[0] && pos_cmp(a, b) && a.para == b.para) {
      /* the text to show changed: it replaces what was there */
      u32 n = b.off - a.off;
      if (strlen(q->text) != n || memcmp(doc->p[a.para]->t + a.off, q->text, n)) { doc_delete(doc, a, b); b = a; }
    }
    ok = m->cmd == CMD_LINK ? (q && doc_set_link(doc, a, b, q->url, q->text, &end)) : doc_set_link(doc, a, b, NULL, NULL, &end);
    doc_end(doc, ok ? end : a);
    if (!ok) { DisplayBeep(NULL); edited = 0; break; }
    d->anchor = d->caret = end; d->pending = 0;
    break; }
  case CMD_BLANKPAGE: {
    /* a page break, an empty Normal paragraph, another page break; the caret on the empty page.
       At a paragraph's start one split is enough: the empty part before it is the blank page */
    ParaFmt pf;
    u32 blank;
    int start, k;
    if (in_table(&doc->p[d->caret.para]->pf)) { DisplayBeep(NULL); edited = 0; break; }
    doc_begin(doc, d->caret);
    delete_sel(d);
    start = d->caret.off == 0;
    doc_insert(doc, &d->caret, "\n\n", start ? 1 : 2, NULL);
    blank = d->caret.para - 1;
    if ((k = doc_style(doc, "Normal")) >= 0) doc_apply_style(doc, blank, blank, k);
    memset(&pf, 0, sizeof pf); pf.pagebreak = 1;
    doc_para_format(doc, blank, d->caret.para, PM_BREAK, &pf);
    d->caret.para = blank; d->caret.off = 0; d->anchor = d->caret;
    doc_end(doc, d->caret);
    break; }
  case CMD_TOCREMOVE:
    doc_begin(doc, d->caret);
    if (!doc_toc_remove(doc, &d->caret)) { DisplayBeep(NULL); edited = 0; }
    doc_end(doc, d->caret);
    d->caret = d->anchor = pos_clamp(doc, d->caret);
    break;
  case CMD_SELUNIT: {
    /* the sentence round the caret (to just after its full stop and the spaces after that), or the
       whole paragraph */
    Para *p = doc->p[d->caret.para];
    u32 a = d->caret.off, b = d->caret.off;
    if (m->arg) { a = 0; b = p->len; }
    else {
      while (a > 0 && !((p->t[a - 1] == ' ' || p->t[a - 1] == '\t') && a >= 2 && strchr(".!?", p->t[a - 2]))) a--;
      while (b < p->len && !strchr(".!?", p->t[b])) b++;
      while (b < p->len && strchr(".!?\"')", p->t[b])) b++;
      while (b < p->len && p->t[b] == ' ') b++;
    }
    d->anchor.para = d->caret.para; d->anchor.off = a; d->caret.off = b;
    edited = 0;
    break; }
  case CMD_TABLE: {
    int rows = (int)((m->arg >> 8) & 0xFFFF), cols = (int)(m->arg & 255);
    doc_begin(doc, d->caret);
    delete_sel(d);
    if (!tbl_insert(doc, &d->caret, rows, cols, in_table(&doc->p[d->caret.para]->pf) ? column_width(doc, d->caret.para) : table_room(doc, d->caret.para))) DisplayBeep(NULL);
    doc_end(doc, d->caret);
    d->anchor = d->caret;
    break; }
  case CMD_ROW_ABOVE: case CMD_ROW_BELOW: case CMD_COL_LEFT: case CMD_COL_RIGHT: case CMD_DEL_ROW: case CMD_DEL_COL: case CMD_DEL_TABLE: {
    int r;
    d->caret = sel_lo(d);
    switch (m->cmd) {
    case CMD_ROW_ABOVE: r = tbl_row_insert(doc, &d->caret, 0); break;
    case CMD_ROW_BELOW: r = tbl_row_insert(doc, &d->caret, 1); break;
    case CMD_COL_LEFT: r = tbl_col_insert(doc, &d->caret, 0); break;
    case CMD_COL_RIGHT: r = tbl_col_insert(doc, &d->caret, 1); break;
    case CMD_DEL_ROW: r = tbl_row_delete(doc, &d->caret); break;
    case CMD_DEL_COL: r = tbl_col_delete(doc, &d->caret); break;
    default: r = tbl_delete(doc, &d->caret);
    }
    if (!r) DisplayBeep(NULL);
    d->anchor = d->caret;
    break; }
  case CMD_GOTO: {
    u32 page;
    i32 x, y, h;
    d->caret.para = (u32)m->arg; d->caret.off = 0;
    d->caret = d->anchor = pos_clamp(doc, d->caret);
    ensure_laid(d, obj, d->caret.para);
    lay_caret(d->lay, d->caret, &page, &x, &y, &h);
    d->scrolly = page_top(d, page) + tx(d, y) - 8;
    if (d->scrolly > d->doch - _mheight(obj)) d->scrolly = d->doch - _mheight(obj);
    if (d->scrolly < 0) d->scrolly = 0;
    update_prop(d);
    edited = 0;
    break; }
  case CMD_INDENTS: {
    const i32 *iv = (const i32 *)m->arg;
    ParaFmt pf;
    memset(&pf, 0, sizeof pf);
    pf.left = iv[0]; pf.first = iv[1]; pf.right = iv[2];
    doc_para_format(doc, sel_lo(d).para, sel_hi(d).para, PM_INDENT, &pf);
    break; }
  case CMD_REFRESH: edited = 0; break;
  case CMD_SELECT: {
    const Pos *ab = (const Pos *)m->arg;
    d->anchor = pos_clamp(doc, ab[0]); d->caret = pos_clamp(doc, ab[1]);
    ensure_laid(d, obj, d->caret.para);
    edited = 0;
    break; }
  case CMD_PARAFMT: {
    const struct QParaFmt *q = (const struct QParaFmt *)m->arg;
    sel_para_format(d, q->mask, &q->pf);
    break; }
  case CMD_TABS: {
    const struct QTabs *q = (const struct QTabs *)m->arg;
    ParaFmt pf;
    memset(&pf, 0, sizeof pf);
    memcpy(pf.tabs, q->pos, sizeof pf.tabs); memcpy(pf.tabkind, q->kind, sizeof pf.tabkind); memcpy(pf.tablead, q->lead, sizeof pf.tablead);
    doc_para_format(doc, sel_lo(d).para, sel_hi(d).para, PM_TABS, &pf);
    break; }
  case CMD_PAGESETUP: close_fonts(d); break;
  case CMD_STORY: {
    const struct QStoryView *sv = (const struct QStoryView *)m->arg;
    if (d->lay) { lay_free(d->lay); d->lay = NULL; }
    drop_plain(d);
    close_fonts(d);
    strncpy(d->slabel, sv && sv->label ? sv->label : "", sizeof d->slabel - 1);
    d->caret = d->anchor = pos_clamp(doc, sv ? sv->caret : doc_start(doc));
    d->pending = 0; d->wantx = -1; d->words_stale = 1; d->outline_stale = 1;
    relayout(d, obj);
    if (sv && sv->scroll >= 0) d->scrolly = sv->scroll;
    update_prop(d);
    edited = 0;
    break; }
  case CMD_PICTURE: {
    Image *im;
    char t[8];
    i32 w;
    if (m->arg < 0 || m->arg >= doc->nimages) { edited = 0; break; }
    im = &doc->images[m->arg];
    w = column_width(doc, sel_lo(d).para);
    if (im->cx > w) { im->cy = (i32)((long)im->cy * w / im->cx); im->cx = w; }    /* not wider than the column */
    pic_utf8(t, (int)m->arg);
    insert_text(d, t, 4);
    break; }
  case CMD_GROUP: {
    /* the objects chosen together (Shift-click) made one group */
    i32 box[4 * 16];
    Pos at[16];
    int n = 0, j, g;
    for (j = 0; j < d->nmsel; j++) {
      FloatBox fb;
      Para *p = d->msel[j].para < doc->np ? doc->p[d->msel[j].para] : NULL;
      int k = p && d->msel[j].off + 4 <= p->len && IS_PIC(p->t + d->msel[j].off) ? pic_index(p->t + d->msel[j].off) : -1;
      if (k < 0 || !obj_box(d, d->msel[j], k, &fb) || fb.wrap == WRAP_INLINE) continue;
      at[n] = d->msel[j]; box[4 * n] = fb.x; box[4 * n + 1] = fb.y; box[4 * n + 2] = fb.w; box[4 * n + 3] = fb.h;
      n++;
    }
    if (n < 2 || (g = doc_group(doc, at, box, n)) < 0) { DisplayBeep(NULL); edited = 0; break; }
    d->nmsel = 0;
    {
      /* the group selected (its reference where the first one was) */
      u32 i, o;
      for (i = 0; i < doc->np; i++) for (o = 0; o + 4 <= doc->p[i]->len; o++) if (IS_PIC(doc->p[i]->t + o) && pic_index(doc->p[i]->t + o) == g) { d->anchor.para = d->caret.para = i; d->anchor.off = o; d->caret.off = o + 4; }
    }
    break; }
  case CMD_RESTACK: {
    /* the selected floating object up or down the stack of those on its page */
    Pos at;
    int k = sel_object(d, &at);
    u32 i;
    if (k < 0 || !d->lay) { DisplayBeep(NULL); edited = 0; break; }
    for (i = 0; i < d->lay->nfloats && !(d->lay->floats[i].img == k && d->lay->floats[i].para == at.para); i++) ;
    if (i == d->lay->nfloats || !lay_restack(d->lay, i, (int)m->arg)) { DisplayBeep(NULL); edited = 0; break; }
    d->anchor = at; d->caret = at; d->caret.off += 4;
    break; }
  case CMD_BOOKMARK: {
    /* the selection, or with none the caret's paragraph, made the bookmark */
    Pos a = sel_lo(d), b = sel_hi(d);
    const char *nm = (const char *)m->arg;
    if (!has_sel(d)) { a.off = 0; b = a; b.off = doc->p[a.para]->len; }
    if (!nm || !nm[0] || !doc_set_bmark(doc, a, b, nm)) { DisplayBeep(NULL); edited = 0; }
    break; }
  case CMD_UNBOOKMARK:
    if (!m->arg || !doc_remove_bmark(doc, (const char *)m->arg)) { DisplayBeep(NULL); edited = 0; }
    break;
  case CMD_GOTOBM: {
    Pos a, b;
    edited = 0;
    if (!m->arg || !doc_bmark_find(doc, (const char *)m->arg, &a, &b)) { DisplayBeep(NULL); break; }
    d->anchor = a; d->caret = b;
    ensure_laid(d, obj, b.para);
    break; }
  case CMD_XREF: {
    /* the bookmark's text (one paragraph's worth) or the page-reference field, linked to the
       bookmark (so Ctrl-click goes there), in the caret's formatting; one undo step with the hidden
       bookmark a heading gets */
    const struct QXref *q = (const struct QXref *)m->arg;
    char name[64], url[80];
    Pos a, b;
    CharFmt f;
    if (!q) { edited = 0; break; }
    doc_begin(doc, d->caret);
    delete_sel(d);
    if (q->para >= 0 && (u32)q->para < doc->np) {
      /* a heading: its own hidden bookmark (the one there, or a new one) */
      Pos h;
      int k;
      h.para = (u32)q->para; h.off = 0;
      k = doc_bmark_at(doc, h, &a, &b);
      if (k && doc_bmark_name(doc, k)[0] == '_') strcpy(name, doc_bmark_name(doc, k));
      else {
        int n = 1;
        do sprintf(name, "_Ref%d", n++); while (doc_bmark_find(doc, name, NULL, NULL));
        b = h; b.off = doc->p[h.para]->len;
        if (!doc_set_bmark(doc, h, b, name)) name[0] = 0;
      }
    } else if (q->name) { strncpy(name, q->name, sizeof name - 1); name[sizeof name - 1] = 0; }
    else name[0] = 0;
    if (!name[0] || !doc_bmark_find(doc, name, &a, &b)) { doc_end(doc, d->caret); DisplayBeep(NULL); edited = 0; break; }
    sprintf(url, "#%.70s", name);
    f = *para_fmt_before(doc->p[d->caret.para], d->caret.off);
    f.link = (u16)doc_link(doc, url);
    f.bmark = 0;
    if (q->kind == 1) doc_insert(doc, &d->caret, FIELD_PAGEREF, 3, &f);
    else {
      /* its text, up to its paragraph's end, without objects, notes or fields */
      char t[256];
      u32 i, n = 0, e = a.para == b.para ? b.off : doc->p[a.para]->len;
      const char *s = doc->p[a.para]->t;
      for (i = a.off; i < e && n < sizeof t - 4; ) {
        if (i + 4 <= e && (IS_PIC(s + i) || IS_NOTE(s + i) || IS_MARK(s + i) || IS_ENOTE(s + i))) { i += 4; continue; }
        if (i + 3 <= e && IS_FIELD(s + i)) { i += 3; continue; }
        if (s[i] == '\t' || s[i] == '\v') { t[n++] = ' '; i++; continue; }
        t[n++] = s[i++];
      }
      if (n) doc_insert(doc, &d->caret, t, n, &f);
    }
    doc_end(doc, d->caret);
    d->anchor = d->caret;
    break; }
  case CMD_OBJALIGN: {
    /* one object: aligned to the margins (as Word, by its alignment settings, so it stays there);
       several chosen together and on one page: moved to line up with each other, or spaced evenly */
    Pos at[16];
    FloatBox fb[16];
    int n = 0, j, k, how = (int)m->arg;
    if (d->nmsel >= 2) {
      for (j = 0; j < d->nmsel && n < 16; j++) {
        Para *p = d->msel[j].para < doc->np ? doc->p[d->msel[j].para] : NULL;
        k = p && d->msel[j].off + 4 <= p->len && IS_PIC(p->t + d->msel[j].off) ? pic_index(p->t + d->msel[j].off) : -1;
        if (k < 0 || !obj_box(d, d->msel[j], k, &fb[n]) || fb[n].wrap == WRAP_INLINE || (n && fb[n].page != fb[0].page)) continue;
        at[n++] = d->msel[j];
      }
    } else if ((k = sel_object(d, &at[0])) >= 0 && obj_box(d, at[0], k, &fb[0]) && fb[0].wrap != WRAP_INLINE) n = 1;
    if (!n || how < 0 || how > 7 || (how >= 6 && n < 3) || (n == 1 && how >= 6)) { DisplayBeep(NULL); edited = 0; break; }
    doc_begin(doc, d->caret);
    if (n == 1) {
      Image im = doc->images[fb[0].img];
      if (how < 3) { im.hrel = POS_MARGIN; im.halign = (u8)(PA_START + how); }
      else { im.vrel = POS_MARGIN; im.valign = (u8)(PA_START + how - 3); }
      doc_set_object(doc, at[0], &im);
    } else {
      i32 x0 = fb[0].x, y0 = fb[0].y, x1 = fb[0].x + fb[0].w, y1 = fb[0].y + fb[0].h, room = 0, gap, pos;
      int ord[16], a, b;
      for (j = 1; j < n; j++) {
        if (fb[j].x < x0) x0 = fb[j].x;
        if (fb[j].y < y0) y0 = fb[j].y;
        if (fb[j].x + fb[j].w > x1) x1 = fb[j].x + fb[j].w;
        if (fb[j].y + fb[j].h > y1) y1 = fb[j].y + fb[j].h;
      }
      for (j = 0; j < n; j++) ord[j] = j;
      if (how >= 6)        /* in order across (or down), by where they start */
        for (a = 1; a < n; a++) for (b = a; b > 0 && (how == 6 ? fb[ord[b]].x < fb[ord[b - 1]].x : fb[ord[b]].y < fb[ord[b - 1]].y); b--) { int t = ord[b]; ord[b] = ord[b - 1]; ord[b - 1] = t; }
      for (j = 0; j < n; j++) room += how == 6 ? fb[j].w : fb[j].h;
      gap = how == 6 ? (x1 - x0 - room) / (n - 1) : (y1 - y0 - room) / (n - 1);
      pos = how == 6 ? x0 : y0;
      for (a = 0; a < n; a++) {
        FloatBox *f = &fb[ord[a]];
        i32 nx = f->x, ny = f->y;
        Image im = doc->images[f->img];
        switch (how) {
        case 0: nx = x0; break;
        case 1: nx = (x0 + x1 - f->w) / 2; break;
        case 2: nx = x1 - f->w; break;
        case 3: ny = y0; break;
        case 4: ny = (y0 + y1 - f->h) / 2; break;
        case 5: ny = y1 - f->h; break;
        case 6: nx = pos; pos += f->w + gap; break;
        case 7: ny = pos; pos += f->h + gap; break;
        }
        if (nx == f->x && ny == f->y) continue;
        lay_float_moved(d->lay, f, &im, nx, ny);
        doc_set_object(doc, at[ord[a]], &im);
      }
    }
    doc_end(doc, d->caret);
    break; }
  case CMD_UNGROUP: {
    Pos at;
    FloatBox fb;
    int k = sel_object(d, &at);
    if (k < 0 || doc->images[k].kind != OBJ_GROUP || !obj_box(d, at, k, &fb) || fb.wrap == WRAP_INLINE || !doc_ungroup(doc, at, fb.x, fb.y)) { DisplayBeep(NULL); edited = 0; break; }
    d->anchor = at; d->caret = at; d->caret.off += 4;          /* the first of them selected */
    break; }
  case CMD_OBJSET: {
    Pos at;
    if (sel_object(d, &at) < 0 || !m->arg || doc_set_object(doc, at, (const Image *)m->arg) < 0) { DisplayBeep(NULL); edited = 0; break; }
    d->anchor = at; d->caret = at; d->caret.off += 4;
    break; }
  case CMD_OBJWRAP: {
    Pos at;
    int k = sel_object(d, &at);
    Image im;
    FloatBox fb;
    if (k < 0 || k >= doc->nimages || m->arg < WRAP_INLINE || m->arg > WRAP_FRONT) { DisplayBeep(NULL); edited = 0; break; }
    im = doc->images[k];
    if (im.wrap == WRAP_INLINE && m->arg != WRAP_INLINE && obj_box(d, at, k, &fb)) {
      /* floated where it was in the line: across from the margin, down from its paragraph */
      ParaLay *pl = &d->lay->pl[at.para];
      i32 py = pl->nlines && pl->placed0 < d->lay->nplaced ? d->lay->placed[pl->placed0].y : fb.y;
      im.hrel = POS_MARGIN; im.halign = PA_NONE; im.x = fb.x - page_of(d, fb.page)->left;
      im.vrel = POS_PARA; im.valign = PA_NONE; im.y = fb.y - py;
      if (im.y < 0) im.y = 0;
    }
    im.wrap = (u8)m->arg;
    if (im.wrap == WRAP_TIGHT && im.kind == OBJ_PICTURE && !im.npoly && im.data) {
      /* tight round a picture: round what it shows (the entry made takes its own copy) */
      int np = img_outline(im.data, im.len, &im.poly);
      im.npoly = (u16)np;
      if (doc_set_object(doc, at, &im) < 0) edited = 0;
      free(im.poly);
      if (!edited) break;
    } else if (doc_set_object(doc, at, &im) < 0) { edited = 0; break; }
    d->anchor = at; d->caret = at; d->caret.off += 4;
    break; }
  case CMD_SHAPE: {
    int sh = (int)m->arg, k = doc_add_shape(doc, sh < 0 ? -1 : sh, sh < 0 ? 2880 : 1440, sh < 0 || sh == SH_LINE || sh == SH_ARROW ? 1440 / (sh < 0 ? 1 : 2) : 1440);
    char t[8];
    if (k < 0) { edited = 0; break; }
    /* at the paragraph's top left in its column, in front of the text (a text box: square) */
    doc->images[k].hrel = POS_COLUMN; doc->images[k].x = 0; doc->images[k].vrel = POS_PARA; doc->images[k].y = 0;
    pic_utf8(t, k);
    d->caret = d->anchor = sel_hi(d);           /* after the selection (another object, say), not over it */
    insert_text(d, t, 4);
    d->anchor = d->caret; d->anchor.off -= 4;
    break; }
  case CMD_SETSTYLE: {
    struct QStyle *q = (struct QStyle *)m->arg;
    int k = doc_set_style(doc, q->k, &q->s);
    if (k < 0) { DisplayBeep(NULL); edited = 0; break; }
    q->k = k;
    break; }
  case CMD_DELSTYLE: if (!doc_delete_style(doc, (int)m->arg)) { DisplayBeep(NULL); edited = 0; } break;
  case CMD_CHARSTYLE: {
    Pos a = sel_lo(d), b = sel_hi(d);
    if (!has_sel(d)) { select_word(d); a = sel_lo(d); b = sel_hi(d); }
    if (!pos_cmp(a, b) || !doc_char_style(doc, a, b, (int)m->arg)) { DisplayBeep(NULL); edited = 0; }
    break; }
  case CMD_CHARFMT: {
    const struct QCharFmt *q = (const struct QCharFmt *)m->arg;
    char_format(d, q->mask, &q->cf, 0);
    break; }
  case CMD_LISTPRESET: {
    int l = doc_list_preset(doc, (int)m->arg);
    if (!l || !doc_list_apply(doc, sel_lo(d).para, sel_hi(d).para, l)) { DisplayBeep(NULL); edited = 0; }
    break; }
  case CMD_LISTDEF: {
    int l = doc_add_list(doc, (const ListDef *)m->arg);
    if (!l || !doc_list_apply(doc, sel_lo(d).para, sel_hi(d).para, l)) { DisplayBeep(NULL); edited = 0; }
    break; }
  case CMD_LISTRESTART: if (!doc_list_restart(doc, d->caret.para, (int)m->arg)) { DisplayBeep(NULL); edited = 0; } break;
  case CMD_LISTCONTINUE: if (!doc_list_continue(doc, d->caret.para)) { DisplayBeep(NULL); edited = 0; } break;
  case CMD_HEADNUM: if (!doc_heading_numbering(doc, (int)m->arg)) { DisplayBeep(NULL); edited = 0; } break;
  case CMD_DOCLOOK: {
    int kind = (int)(m->arg >> 8), v = (int)(m->arg & 255), ok;
    ok = kind == 0 ? doc_style_set(doc, v) : kind == 1 ? doc_theme_fonts(doc, v) : kind == 2 ? doc_para_spacing(doc, v) :
         kind == 3 ? doc_color_set(doc, v) : doc_theme(doc, v);
    if (!ok) { DisplayBeep(NULL); edited = 0; }
    break; }
  case CMD_PAGECOLOR:   /* not in the undo history, like the page setup */
    if (doc->pagecolor == (u32)m->arg) { edited = 0; break; }
    doc->pagecolor = (u32)m->arg;
    doc->dirty = 1;
    break;
  case CMD_PASTEAS: paste_as(d, (int)m->arg); break;
  case CMD_PICSIZE: {
    Pos at, end;
    Image *im;
    u8 *copy;
    int k, nk;
    char t[8];
    CharFmt f;
    i32 w;
    if (!pic_near(d, &at) || (k = pic_index(doc->p[at.para]->t + at.off)) >= doc->nimages) { DisplayBeep(NULL); edited = 0; break; }
    im = &doc->images[k];
    w = column_width(doc, at.para) * (i32)m->arg / 100;
    if (w < 60) w = 60;
    /* a new entry at the new size takes the old one's place in the text: an edit like any other, so undoable */
    if (!(copy = (u8 *)malloc(im->len))) { edited = 0; break; }
    memcpy(copy, im->data, im->len);
    nk = doc_add_image(doc, copy, im->len, w, (i32)((long)w * doc->images[k].ph / (doc->images[k].pw ? doc->images[k].pw : 1)));
    if (nk < 0) { edited = 0; break; }
    f = *para_fmt_at(doc->p[at.para], at.off);
    end = at; end.off += 4;
    pic_utf8(t, nk);
    doc_begin(doc, at);
    doc_delete(doc, at, end);
    doc_insert(doc, &at, t, 4, &f);
    doc_end(doc, at);
    d->anchor = at; d->anchor.off -= 4; d->caret = at;          /* the picture stays selected */
    break; }
  case CMD_HEADERROW: {
    Para *cp = doc->p[d->caret.para];
    u32 a, b, i;
    u8 on;
    if (!in_table(&cp->pf)) { DisplayBeep(NULL); edited = 0; break; }
    on = (u8)!cp->pf.cell.header;
    a = tbl_first(doc, d->caret.para); b = tbl_end(doc, d->caret.para);
    doc_begin(doc, d->caret);
    for (i = a; i < b; i++)
      if (doc->p[i]->pf.cell.row == cp->pf.cell.row) { ParaFmt pf = doc->p[i]->pf; pf.cell.header = on; doc_para_format(doc, i, i, PM_CELL, &pf); }
    doc_end(doc, d->caret);
    break; }
  case CMD_TSTYLE: case CMD_TLOOK: {
    Table *t = doc_table(doc, &doc->p[d->caret.para]->pf);
    Pos c = d->caret;
    u16 look;
    int ts;
    if (!t) { DisplayBeep(NULL); edited = 0; break; }
    look = t->look; ts = t->tstyle;
    if (m->cmd == CMD_TSTYLE) ts = (int)m->arg; else look ^= (u16)m->arg;
    if (!tbl_apply_style(doc, &c, ts, look)) { DisplayBeep(NULL); edited = 0; break; }
    d->caret = c; if (!has_sel(d)) d->anchor = c;
    break; }
  case CMD_TBORDERS: {
    const struct QBorders *q = (const struct QBorders *)m->arg;
    Pos a = has_sel(d) ? d->anchor : d->caret, b = d->caret;
    if (!in_table(&doc->p[d->caret.para]->pf) || !tbl_borders(doc, a, b, q->which, q->style, q->width, q->color)) { DisplayBeep(NULL); edited = 0; }
    break; }
  case CMD_TSELECT: {
    u16 t = doc->p[d->caret.para]->pf.cell.table;
    const Cell *c = &doc->p[d->caret.para]->pf.cell;
    u32 a, b, i;
    int r, rows = 0;
    if (!t) { DisplayBeep(NULL); edited = 0; break; }
    a = tbl_first(doc, d->caret.para); b = tbl_end(doc, d->caret.para);
    for (i = a; i < b; i++) { const Cell *x = cell_in(&doc->p[i]->pf, t); if (x && x->row + 1 > rows) rows = x->row + 1; }
    switch ((int)m->arg) {
    case 0: select_cell(d, d->caret.para); break;
    case 1: {                                   /* the column: from its top cell to its bottom one */
      u32 top = cell_para_at(doc, t, a, b, 0, c->col), bot = b;
      for (r = rows - 1; r >= 0 && bot >= b; r--) bot = cell_para_at(doc, t, a, b, r, c->col);
      if (top >= b || bot >= b) { DisplayBeep(NULL); break; }
      d->anchor.para = top; d->anchor.off = 0;
      d->caret.para = cell_end(doc, bot) - 1; d->caret.off = doc->p[d->caret.para]->len;
      break; }
    case 2: {
      u32 first = cell_para_at(doc, t, a, b, c->row, 0), last = row_last(doc, t, a, b, c->row);
      if (first >= b) first = d->caret.para;
      d->anchor.para = first; d->anchor.off = 0;
      d->caret.para = last < b ? last : d->caret.para; d->caret.off = doc->p[d->caret.para]->len;
      break; }
    default:
      d->anchor.para = a; d->anchor.off = 0; d->caret.para = b - 1; d->caret.off = doc->p[b - 1]->len;
    }
    edited = 0;
    break; }
  case CMD_TGRID: d->tgrid = !d->tgrid; edited = 0; break;
  case CMD_TPROPS: {
    const struct QTableProps *q = (const struct QTableProps *)m->arg;
    Table *t = doc_table(doc, &doc->p[d->caret.para]->pf), nt;
    Pos c = d->caret, a = has_sel(d) ? sel_lo(d) : d->caret, b = has_sel(d) ? sel_hi(d) : d->caret;
    if (!t) { DisplayBeep(NULL); edited = 0; break; }
    nt = *t;
    nt.jc = q->jc; nt.indent = q->indent; nt.autofit = q->autofit;
    nt.marl = q->marl; nt.marr = q->marr; nt.mart = q->mart; nt.marb = q->marb;
    doc_begin(doc, c);
    tbl_set_table(doc, &c, &nt);
    a.para = a.para >= tbl_first(doc, c.para) ? a.para : c.para; b.para = b.para < tbl_end(doc, c.para) ? b.para : c.para;
    tbl_cell_format(doc, a, b, TF_ROWH, (u32)(i32)q->rowh);
    tbl_cell_format(doc, a, b, TF_CANTSPLIT, q->cantsplit);
    tbl_cell_format(doc, a, b, TF_HEADER, q->header);
    doc_end(doc, c);
    d->caret = pos_clamp(doc, d->caret); d->anchor = pos_clamp(doc, d->anchor);
    break; }
  case CMD_SPLITCELLS: {
    Pos c = d->caret;
    if (!tbl_split_cells(doc, &c, (int)((m->arg >> 8) & 255), (int)(m->arg & 255))) { DisplayBeep(NULL); edited = 0; break; }
    d->caret = d->anchor = c;
    break; }
  case CMD_SPLITTABLE: {
    Pos c = d->caret;
    if (!tbl_split_table(doc, &c)) { DisplayBeep(NULL); edited = 0; break; }
    d->caret = d->anchor = c;
    break; }
  case CMD_AUTOFIT: {
    Table *t = doc_table(doc, &doc->p[d->caret.para]->pf), nt;
    Layout *L = paged_layout(d);
    Pos c = d->caret;
    i32 grid[MAX_COLS];
    int n;
    if (!t || !L) { DisplayBeep(NULL); edited = 0; break; }
    nt = *t;
    if (m->arg == 2) nt.autofit = 0;
    else {
      if (!(n = lay_autofit(L, tbl_first(doc, d->caret.para), m->arg == 1, table_room(doc, d->caret.para), grid))) { DisplayBeep(NULL); edited = 0; break; }
      memcpy(nt.grid, grid, (size_t)n * sizeof(i32));
      nt.autofit = m->arg == 0; nt.indent = 0;
    }
    if (!tbl_set_table(doc, &c, &nt)) { DisplayBeep(NULL); edited = 0; break; }
    d->caret = c; d->anchor = pos_clamp(doc, d->anchor);
    break; }
  case CMD_ROWH: {
    Pos a = has_sel(d) ? sel_lo(d) : d->caret, b = has_sel(d) ? sel_hi(d) : d->caret;
    if (!tbl_cell_format(doc, a, b, TF_ROWH, (u32)m->arg)) { DisplayBeep(NULL); edited = 0; }
    break; }
  case CMD_COLW: case CMD_DISTCOLS: {
    Table *t = doc_table(doc, &doc->p[d->caret.para]->pf), nt;
    Pos c = d->caret;
    int r0, r1, c0, c1, k;
    i32 sum = 0;
    if (!t) { DisplayBeep(NULL); edited = 0; break; }
    sel_rect(d, &r0, &r1, &c0, &c1);
    nt = *t;
    if (m->cmd == CMD_DISTCOLS && c0 == c1) { c0 = 0; c1 = t->ncols - 1; }    /* nothing selected across: every column */
    for (k = c0; k <= c1 && k < t->ncols; k++) sum += t->grid[k];
    {
      /* each selected column that wide (one merged cell alone: its columns share the width) */
      const Cell *cc = &doc->p[d->caret.para]->pf.cell;
      int per = r0 == r1 && c0 == cc->col && c1 == cc->col + cc->span - 1 ? cc->span : 1;
      for (k = c0; k <= c1 && k < t->ncols; k++) nt.grid[k] = m->cmd == CMD_COLW ? (i32)m->arg / per : sum / (c1 - c0 + 1);
    }
    for (k = 0; k < nt.ncols; k++) if (nt.grid[k] < 240) nt.grid[k] = 240;
    if (!tbl_set_table(doc, &c, &nt)) { DisplayBeep(NULL); edited = 0; break; }
    d->caret = c; d->anchor = pos_clamp(doc, d->anchor);
    break; }
  case CMD_DISTROWS: {
    Layout *L = paged_layout(d);
    u16 t = doc->p[d->caret.para]->pf.cell.table;
    int r0, r1, c0, c1, r, nr = 0;
    i32 total = 0;
    u32 i, a, b;
    Pos pa, pb;
    if (!t || !L) { DisplayBeep(NULL); edited = 0; break; }
    sel_rect(d, &r0, &r1, &c0, &c1);
    a = tbl_first(doc, d->caret.para); b = tbl_end(doc, d->caret.para);
    if (r0 == r1) { r0 = 0; for (i = a; i < b; i++) { const Cell *x = cell_in(&doc->p[i]->pf, t); if (x && x->row > r1) r1 = x->row; } }
    /* each row's height as laid out: its tallest cell box that is just that row's */
    for (r = r0; r <= r1; r++) {
      i32 h = 0;
      for (i = 0; i < L->ncells; i++) if (L->cells[i].table == t && L->cells[i].row == r && L->cells[i].h > h) h = L->cells[i].h;
      total += h; nr++;
    }
    if (!nr) { edited = 0; break; }
    pa.para = cell_para_at(doc, t, a, b, r0, 0); pa.off = 0;
    pb.para = row_last(doc, t, a, b, r1); pb.off = 0;
    if (pa.para >= b || pb.para >= b || !tbl_cell_format(doc, pa, pb, TF_ROWH, (u32)(i32)(total / nr))) { DisplayBeep(NULL); edited = 0; }
    break; }
  case CMD_TSORT: {
    const struct QSort *q = (const struct QSort *)m->arg;
    Pos c = d->caret;
    if (!tbl_sort(doc, &c, q->col, q->desc, q->numeric, q->header)) { DisplayBeep(NULL); edited = 0; break; }
    d->caret = d->anchor = c;
    break; }
  case CMD_TOTEXT: {
    Pos c = d->caret;
    if (!tbl_to_text(doc, &c, (int)m->arg)) { DisplayBeep(NULL); edited = 0; break; }
    d->caret = d->anchor = c;
    break; }
  case CMD_TOTABLE: {
    Pos c = d->caret;
    if (!tbl_from_text(doc, sel_lo(d).para, sel_hi(d).para, (int)m->arg, para_page(doc, sel_lo(d).para)->width - para_page(doc, sel_lo(d).para)->left - para_page(doc, sel_lo(d).para)->right, &c)) { DisplayBeep(NULL); edited = 0; break; }
    d->caret = d->anchor = c;
    break; }
  case CMD_FORMULA: {
    struct QFormula *q = (struct QFormula *)m->arg;
    if (!(q->ok = tbl_formula(doc, d->caret, q->expr, q->result, sizeof q->result))) { edited = 0; break; }
    insert_text(d, q->result, (u32)strlen(q->result));
    break; }
  case CMD_COLEDGE: {
    const struct QColEdge *q = (const struct QColEdge *)m->arg;
    Table *t = doc_table(doc, &doc->p[d->caret.para]->pf), nt;
    Pos c = d->caret;
    i32 left = 0;
    int k = q->edge;
    if (!t || doc->p[d->caret.para]->pf.cell.table != q->table || k < 1 || k > t->ncols) { edited = 0; break; }
    nt = *t;
    for (left = 0, k = 0; k < q->edge - 1; k++) left += t->grid[k];
    k = q->edge;
    /* the column left of the edge takes the new width; the one right of it gives or takes the difference (the last edge: the table's width changes) */
    {
      i32 w = q->x - left, diff;
      if (w < 240) w = 240;
      diff = w - t->grid[k - 1];
      if (k < t->ncols) { if (t->grid[k] - diff < 240) diff = t->grid[k] - 240; nt.grid[k] = t->grid[k] - diff; }
      nt.grid[k - 1] = t->grid[k - 1] + diff;
    }
    if (!tbl_set_table(doc, &c, &nt)) { edited = 0; break; }
    d->caret = c; d->anchor = pos_clamp(doc, d->anchor);
    break; }
  default: edited = 0;
  }
  if (qlog) { fprintf(qlog, "  flags after %d\n", para_fmt_at(doc->p[sel_lo(d).para], sel_lo(d).off)->flags); fflush(qlog); }
  refresh(d, obj, edited);
  return 0;
}

/* ------------------------------------------------------------------ */
/* keys and the mouse */
/* the page under the pointer, and the point on it (twips) */
static u32 point_page(struct Data *d, Object *obj, LONG mx, LONG my, i32 *px, i32 *py)
{
  LONG y = my - _mtop(obj) + d->scrolly, x = mx - _mleft(obj), w = _mwidth(obj);
  u32 page;
  if (d->vmode == VM_WEB) page = 0;
  else if (d->vmode == VM_DRAFT) page = y < GAP ? 0 : (u32)((y - GAP) / (text_h(d) + 2));
  else {
    /* the row, then the page in it nearest the pointer */
    LONG row = (LONG)row_at(d, y), c = (x - page_left(d, w) + GAP / 2) / (page_w(d) + GAP);
    if (c < 0) c = 0;
    if (c >= cols(d)) c = cols(d) - 1;
    page = (u32)(row * cols(d) + c);
  }
  if (page >= d->lay->npages) page = d->lay->npages - 1;
  *px = tw(d, x - page_x(d, page, w)); *py = tw(d, y - page_top(d, page));
  return page;
}
/* a header, footer or footnote at a point on a page: its SK_ kind (0: none), the note's index */
static int story_at(struct Data *d, u32 page, i32 px, i32 py, int *index)
{
  Layout *L = d->lay;
  const PageSetup *pg = lay_page(L, page);
  Furn f;
  i32 sepy = -1;
  *index = 0;
  if (d->doc->edit.kind || !paged(d)) return 0;
  if (lay_notes(L, page, &f, &sepy)) {
    int found = -1;
    if (py >= sepy) {
      u32 i, j = 0, k;
      for (i = 0; i < L->npn && found < 0; i++) {
        u32 n = L->pn[i].note;
        if (L->pn[i].page != page || n >= (u32)d->doc->nnotes) continue;
        for (k = 0; k < d->doc->notes[n].np && j < f.n; k++, j++) {
          i32 h = f.f[j].p->pf.after;
          u32 ln;
          for (ln = 0; ln < f.f[j].pl.nlines; ln++) h += f.f[j].pl.lines[ln].height;
          if (py < f.f[j].y + h) { found = (int)n; break; }
        }
        if (found < 0) found = (int)n;          /* (the last one, if below them all) */
      }
    }
    lay_furniture_free(&f);
    if (found >= 0) { *index = found; return SK_NOTE; }
  }
  if (py < pg->top) return SK_HEADER;
  if (py > pg->height - pg->bottom) return SK_FOOTER;
  return 0;
}
/* the text position nearest a point in window pixels */
static Pos point_pos(struct Data *d, Object *obj, LONG mx, LONG my)
{
  i32 px, py;
  u32 page = point_page(d, obj, mx, my, &px, &py);
  return lay_hit(d->lay, page, px, py);
}
/* the link at the caret for main.c's Link window and commands: its address and text, or (no link)
   an empty address and the selected text, if it is short and on one line. Static: good until the
   next call */
static const struct QLink *link_info(struct Data *d)
{
  static char url[512], text[256];
  static struct QLink q;
  Pos a, b;
  int k;
  url[0] = text[0] = 0;
  q.url = url; q.text = text;
  if (!d->doc) return &q;
  if ((k = doc_link_at(d->doc, has_sel(d) ? sel_lo(d) : d->caret, &a, &b))) {
    strncpy(url, doc_link_url(d->doc, k), sizeof url - 1); url[sizeof url - 1] = 0;
  } else { a = sel_lo(d); b = sel_hi(d); }
  if (a.para == b.para && b.off > a.off && b.off - a.off < sizeof text) {
    memcpy(text, d->doc->p[a.para]->t + a.off, b.off - a.off); text[b.off - a.off] = 0;
  }
  return &q;
}
/* ------------------------------------------------------------------ */
/* The right mouse button over the page: a context menu (MUI's MUIM_ContextMenuBuild, asked for by
   MUIA_ContextMenu in OM_NEW). Over a word the spelling check underlines, it offers the word's
   suggestions, Ignore All, Add to Dictionary and the Spelling window; anywhere else Cut, Copy and
   Paste. The strip object is made once and kept (MUIA_ContextMenu points at it); each click
   replaces the one menu inside it. Item user data: 1-8 a suggestion, then the CTX_ values */
enum { CTX_IGNORE = 20, CTX_ADD, CTX_SPELLING, CTX_CUT, CTX_COPY, CTX_PASTE, CTX_SELALL };
/* user data for items that stand for something main.c does: a command id (ReturnID), or an entry of
   one of the ribbon's lists (main.c reads it back through MUIA_Quill_CtxPick) */
#define CTX_ID(id)        WP_CTX_ID(id)            /* (wphooks.h: the work packages use the same) */
#define CTX_LIST(id, v)   WP_CTX_LIST(id, v)
/* one item added to a menu (title NM_BARLABEL: a separator line), its user data id, greyed if not enabled */
static void ctx_item(Object *menu, const char *title, ULONG id, int enabled)
{
  Object *it = MenuitemObject, MUIA_Menuitem_Title, (ULONG)title, MUIA_UserData, id,
                 MUIA_Menuitem_Enabled, enabled, End;
  if (it) DoMethod(menu, MUIM_Family_AddTail, (ULONG)it);
}
static int obj_hit(struct Data *d, Object *obj, LONG mx, LONG my, Pos *at, FloatBox *box);
/* an item with a submenu of a ribbon list's entries (titles[i] -> CTX_LIST(id, i)) */
static void ctx_sub(Object *menu, const char *title, ULONG id, const char *const *titles, int n)
{
  Object *it = MenuitemObject, MUIA_Menuitem_Title, (ULONG)title, End;
  int i;
  if (!it) return;
  for (i = 0; i < n; i++) {
    Object *s = MenuitemObject, MUIA_Menuitem_Title, (ULONG)tr(titles[i]), MUIA_UserData, CTX_LIST(id, i), End;
    if (s) DoMethod(it, MUIM_Family_AddTail, (ULONG)s);
  }
  DoMethod(menu, MUIM_Family_AddTail, (ULONG)it);
}
/* the menu for a picture, shape, text box or group under the pointer (selected first, as a left
   click would): what its ribbon tab offers, the common things first */
static Object *ctx_object(struct Data *d, Object *obj, int k, Pos at)
{
  static const char *const wraps[] = { "In Line with Text", "Square", "Tight", "Top and Bottom", "Behind Text", "In Front of Text", "More Layout Options..." };
  static const char *const rots[] = { "Rotate Right 90\260", "Rotate Left 90\260", "Flip Vertical", "Flip Horizontal", "More Rotation Options..." };
  const Image *im = &d->doc->images[k];
  int pic = im->kind == OBJ_PICTURE, grp = im->kind == OBJ_GROUP, chosen = 0, j;
  int gfx = OBJ_IS_GRAPHIC(im->kind);       /* (WP15a) a chart or SmartArt: as a picture, without crop or rotation */
  for (j = 0; j < d->nmsel; j++) if (!pos_cmp(d->msel[j], at)) chosen = 1;
  if (!chosen) d->nmsel = 0;            /* (right-clicking one of the Shift-clicked objects keeps them, for Group) */
  caret_hide(d, obj);
  d->anchor = at; d->caret = at; d->caret.off += 4; d->pending = 0;
  refresh(d, obj, 0);
  if (!(d->ctxmenu = MenuObject, MUIA_Menu_Title, (ULONG)tr(gfx ? (im->kind == OBJ_CHART ? "Chart" : "SmartArt") : pic ? "Picture" : grp ? "Group" : "Shape"), End)) return NULL;
  ctx_item(d->ctxmenu, tr("Cut"), CTX_CUT, 1);
  ctx_item(d->ctxmenu, tr("Copy"), CTX_COPY, 1);
  ctx_item(d->ctxmenu, tr("Paste"), CTX_PASTE, 1);
  ctx_item(d->ctxmenu, (const char *)NM_BARLABEL, 0, 1);
  if (!pic && !grp && !gfx) ctx_item(d->ctxmenu, tr("Edit Text"), CTX_ID(ID_EDITTEXT), 1);
  ctx_item(d->ctxmenu, tr(pic || gfx ? "Format Picture..." : "Format Shape..."), CTX_ID(ID_OBJLAYOUT), 1);
  ctx_sub(d->ctxmenu, tr("Wrap Text"), ID_OBJWRAPDD, wraps, 7);
  if (!gfx) ctx_sub(d->ctxmenu, tr("Rotate"), ID_OBJROTDD, rots, 5);
  ctx_item(d->ctxmenu, tr("Size..."), CTX_ID(ID_OBJSIZE), 1);
  if (pic) ctx_item(d->ctxmenu, tr("Crop..."), CTX_ID(ID_OBJCROP), 1);
  if (d->nmsel >= 2 || grp) {
    ctx_item(d->ctxmenu, (const char *)NM_BARLABEL, 0, 1);
    if (d->nmsel >= 2) ctx_item(d->ctxmenu, tr("Group"), CTX_ID(ID_GROUP), 1);
    if (grp) ctx_item(d->ctxmenu, tr("Ungroup"), CTX_ID(ID_UNGROUP), 1);
  }
  ctx_item(d->ctxmenu, (const char *)NM_BARLABEL, 0, 1);
  ctx_item(d->ctxmenu, tr("Alt Text..."), CTX_ID(ID_ALTTEXT), 1);
  { WpCtx wc; wp_ctx(d, &wc, WPC_OBJECT); wc.obj = k; wp_ctxmenu(d->ctxmenu, &wc); }     /* the work packages' items */
  DoMethod(d->ctxstrip, MUIM_Family_AddTail, (ULONG)d->ctxmenu);
  return d->ctxstrip;
}
/* MUIM_ContextMenuBuild: the right-button menu for what is under the pointer at mx, my. An object
   (picture, shape, group) gets ctx_object's menu; a misspelled word its suggestions first; a link its
   link items; text and tables the rest. The strip is built again each time, into the same object */
static Object *ctx_build(struct Data *d, Object *obj, LONG mx, LONG my)
{
  Spell *sp = d->tb ? (Spell *)d->tb->spell : NULL;
  Pos p;
  Para *q;
  u32 ws, we;
  int i;
  if (!d->lay || !d->doc || !d->ctxstrip) return NULL;
  if (d->ctxmenu) { DoMethod(d->ctxstrip, MUIM_Family_Remove, (ULONG)d->ctxmenu); MUI_DisposeObject(d->ctxmenu); d->ctxmenu = NULL; }
  if (shown_mode(d) == VM_READ) return ww_readmenu(d);     /* (WP25: Read Mode's own menu) */
  {
    /* a picture or shape under the pointer has its own menu */
    Pos at;
    FloatBox fb;
    int k = obj_hit(d, obj, mx, my, &at, &fb);
    if (k >= 0 && k < d->doc->nimages) return ctx_object(d, obj, k, at);
  }
  p = point_pos(d, obj, mx, my);
  if (ev_rclick(d, obj, mx, my)) p = d->caret;   /* WP14b: in an equation, the caret there */
  if (p.para >= d->doc->np) return NULL;
  /* a click outside the selection puts the caret there first, as a left click would */
  if (!has_sel(d) || pos_cmp(p, sel_lo(d)) < 0 || pos_cmp(p, sel_hi(d)) > 0) {
    caret_hide(d, obj);
    d->anchor = d->caret = p;
    d->pending = 0;
    refresh(d, obj, 0);
  }
  d->ctx_a = d->ctx_b = p; d->ctx_n = 0; d->ctx_word[0] = 0;
  q = d->doc->p[p.para];
  /* the word under the pointer (found from the paragraph's start, as spell_word_at only looks
     forward), if the spelling check doesn't know it */
  {
    u32 k = 0;
    ws = we = 0;
    while (spell_word_at(q->t, q->len, k, &ws, &we) && we < p.off) k = we;
  }
  if (sp && we > ws) sp = proof_live_spelling ? proof_spell_at(sp, q, ws) : NULL;     /* (WP31: the word's language) */
  if (sp && sp->n && we > ws && ws <= p.off && we >= p.off &&
      we - ws < sizeof d->ctx_word && !spell_ok(sp, q->t + ws, we - ws)) {
    char *sug[8];
    int n;
    memcpy(d->ctx_word, q->t + ws, we - ws); d->ctx_word[we - ws] = 0;
    d->ctx_a.off = ws; d->ctx_b.off = we;
    n = spell_suggest(sp, d->ctx_word, we - ws, sug, 8);
    for (i = 0; i < n; i++) {
      utf8_to_latin1(sug[i], (u32)strlen(sug[i]), d->ctx_lab[i], sizeof d->ctx_lab[i]);
      strncpy(d->ctx_sug[i], sug[i], sizeof d->ctx_sug[i] - 1); d->ctx_sug[i][sizeof d->ctx_sug[i] - 1] = 0;
      free(sug[i]);
    }
    d->ctx_n = n;
    if (!(d->ctxmenu = MenuObject, MUIA_Menu_Title, (ULONG)tr("Spelling"), End)) return NULL;
    for (i = 0; i < d->ctx_n; i++) ctx_item(d->ctxmenu, d->ctx_lab[i], (ULONG)(i + 1), 1);
    if (!d->ctx_n) ctx_item(d->ctxmenu, tr("(no suggestions)"), 0, 0);
    ctx_item(d->ctxmenu, (const char *)NM_BARLABEL, 0, 1);
    ctx_item(d->ctxmenu, tr("Ignore All"), CTX_IGNORE, 1);
    ctx_item(d->ctxmenu, tr("Add to Dictionary"), CTX_ADD, 1);
    ctx_item(d->ctxmenu, (const char *)NM_BARLABEL, 0, 1);
    ctx_item(d->ctxmenu, tr("Spelling..."), CTX_SPELLING, 1);
    ctx_item(d->ctxmenu, (const char *)NM_BARLABEL, 0, 1);
    ctx_item(d->ctxmenu, tr("Cut"), CTX_CUT, has_sel(d));
    ctx_item(d->ctxmenu, tr("Copy"), CTX_COPY, has_sel(d));
    ctx_item(d->ctxmenu, tr("Paste"), CTX_PASTE, 1);
  } else {
    /* text: the clipboard, then the formatting windows, then a comment; in a table, its own commands too */
    static const char *const ins[] = { "Insert Rows Above", "Insert Rows Below", "Insert Columns to the Left", "Insert Columns to the Right" };
    static const ULONG insid[] = { ID_ROW_ABOVE, ID_ROW_BELOW, ID_COL_LEFT, ID_COL_RIGHT };
    static const char *const dels[] = { "Delete Rows", "Delete Columns", "Delete Table" };
    static const ULONG delid[] = { ID_DEL_ROW, ID_DEL_COL, ID_DEL_TABLE };
    if (!(d->ctxmenu = MenuObject, MUIA_Menu_Title, (ULONG)tr("Edit"), End)) return NULL;
    if (doc_link_at(d->doc, p, NULL, NULL)) {
      /* on a link: what can be done with it first */
      ctx_item(d->ctxmenu, tr("Open Link"), CTX_ID(ID_LINKOPEN), 1);
      ctx_item(d->ctxmenu, tr("Edit Link..."), CTX_ID(ID_LINK), 1);
      ctx_item(d->ctxmenu, tr("Copy Link Address"), CTX_ID(ID_LINKCOPY), 1);
      ctx_item(d->ctxmenu, tr("Remove Link"), CTX_ID(ID_UNLINK), 1);
      ctx_item(d->ctxmenu, (const char *)NM_BARLABEL, 0, 1);
    } else ctx_item(d->ctxmenu, tr("Link..."), CTX_ID(ID_LINK), 1);
    ctx_item(d->ctxmenu, tr("Cut"), CTX_CUT, has_sel(d));
    ctx_item(d->ctxmenu, tr("Copy"), CTX_COPY, has_sel(d));
    ctx_item(d->ctxmenu, tr("Paste"), CTX_PASTE, 1);
    ctx_item(d->ctxmenu, (const char *)NM_BARLABEL, 0, 1);
    ctx_item(d->ctxmenu, tr("Font..."), CTX_ID(ID_FONTDLG), 1);
    ctx_item(d->ctxmenu, tr("Paragraph..."), CTX_ID(ID_PARADLG), 1);
    if (in_table(&q->pf)) {
      Object *sub;
      ctx_item(d->ctxmenu, (const char *)NM_BARLABEL, 0, 1);
      for (i = 0; i < 2; i++) {
        int j, n = i ? 3 : 4;
        if (!(sub = MenuitemObject, MUIA_Menuitem_Title, (ULONG)tr(i ? "Delete" : "Insert"), End)) continue;
        for (j = 0; j < n; j++) {
          Object *it = MenuitemObject, MUIA_Menuitem_Title, (ULONG)tr(i ? dels[j] : ins[j]), MUIA_UserData, CTX_ID(i ? delid[j] : insid[j]), End;
          if (it) DoMethod(sub, MUIM_Family_AddTail, (ULONG)it);
        }
        DoMethod(d->ctxmenu, MUIM_Family_AddTail, (ULONG)sub);
      }
      ctx_item(d->ctxmenu, tr("Merge Cells"), CTX_ID(ID_MERGE), has_sel(d));
      ctx_item(d->ctxmenu, tr("Split Cells..."), CTX_ID(ID_SPLIT), 1);
      ctx_item(d->ctxmenu, tr("Table Properties..."), CTX_ID(ID_TPROPS), 1);
    }
    ctx_item(d->ctxmenu, (const char *)NM_BARLABEL, 0, 1);
    ctx_item(d->ctxmenu, tr("New Comment"), CTX_ID(ID_COMMENT), 1);
    ctx_item(d->ctxmenu, tr("Select All"), CTX_SELALL, 1);
  }
  {
    /* the work packages' items (wphooks.h), told what was clicked */
    WpCtx wc;
    wp_ctx(d, &wc, d->ctx_word[0] ? WPC_SPELLING : doc_link_at(d->doc, p, NULL, NULL) ? WPC_LINK : WPC_TEXT);
    wp_ctxmenu(d->ctxmenu, &wc);
  }
  DoMethod(d->ctxstrip, MUIM_Family_AddTail, (ULONG)d->ctxmenu);
  return d->ctxstrip;
}
/* a context menu item chosen */
static void ctx_choice(struct Data *d, Object *obj, Object *item)
{
  ULONG id = 0;
  if (!item) return;
  get(item, MUIA_UserData, &id);
  if (id & 0x40000000UL) { d->ctx_pick = id & 0x3FFFFFFFUL; DoMethod(_app(obj), MUIM_Application_ReturnID, ID_CTXPICK); return; }
  if (id & 0x20000000UL) { DoMethod(_app(obj), MUIM_Application_ReturnID, id & 0xFFFFUL); return; }
  if (id >= 1 && id <= (ULONG)d->ctx_n && pos_cmp(d->ctx_a, d->ctx_b)) {
    /* the misspelt word replaced by the suggestion (one undo step, tracked if tracking is on) */
    caret_hide(d, obj);
    d->anchor = d->ctx_a; d->caret = d->ctx_b;
    DoMethod(obj, MUIM_Quill_Command, CMD_REPLACE, (ULONG)d->ctx_sug[id - 1]);
    return;
  }
  switch (id) {
  case CTX_IGNORE: case CTX_ADD:
    if (d->ctx_word[0] && d->tb && d->tb->spell) {
      Spell *ws = d->ctx_a.para < d->doc->np ? proof_spell_at((Spell *)d->tb->spell, d->doc->p[d->ctx_a.para], d->ctx_a.off) : NULL;   /* (WP31) */
      spell_add(ws ? ws : (Spell *)d->tb->spell, d->ctx_word, (u32)strlen(d->ctx_word));
      if (id == CTX_ADD) {
        /* kept for next time, as the Spelling window's Add does (main.c reads it at startup) */
        FILE *f = fopen("PROGDIR:Quill.words", "a");
        if (f) { fprintf(f, "%s\n", d->ctx_word); fclose(f); }
      }
      MUI_Redraw(obj, MADF_DRAWOBJECT);           /* its underlines gone, everywhere */
    }
    break;
  case CTX_SPELLING: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_SPELL); break;
  case CTX_CUT: DoMethod(obj, MUIM_Quill_Command, CMD_CUT, 0); break;
  case CTX_COPY: DoMethod(obj, MUIM_Quill_Command, CMD_COPY, 0); break;
  case CTX_PASTE: DoMethod(obj, MUIM_Quill_Command, CMD_PASTE, 0); break;
  case CTX_SELALL: DoMethod(obj, MUIM_Quill_Command, CMD_SELALL, 0); break;
  }
}
/* object img's reference in paragraph pi: its offset, -1 if it has none */
static LONG obj_ref(Doc *doc, u32 pi, int img)
{
  u32 k;
  if (pi >= doc->np) return -1;
  for (k = 0; k + 4 <= doc->p[pi]->len; k++) if (IS_PIC(doc->p[pi]->t + k) && pic_index(doc->p[pi]->t + k) == img) return (LONG)k;
  return -1;
}
/* the box of the object whose reference is at (Doc.images k): floating, or a picture in a line (wrap
   WRAP_INLINE); 0 if it isn't laid out */
static int obj_box(struct Data *d, Pos at, int k, FloatBox *box)
{
  Layout *L = d->lay;
  ParaLay *pl;
  u32 i, j;
  if (!L || at.para >= L->npl) return 0;
  for (i = 0; i < L->nfloats; i++) if (L->floats[i].img == k && L->floats[i].para == at.para) { *box = L->floats[i]; return 1; }
  pl = &L->pl[at.para];
  if (!pl->nlines || pl->placed0 + pl->nlines > L->nplaced) return 0;
  for (i = 0; i < pl->nlines; i++) {
    Line *ln = &pl->lines[i];
    Placed *pc = &L->placed[pl->placed0 + i];
    for (j = ln->box0; j < ln->box0 + ln->nbox; j++)
      if (pl->boxes[j].kind == BX_PIC && pl->boxes[j].off == at.off && k < d->doc->nimages) {
        Image *im = &d->doc->images[k];
        box->page = pc->page; box->x = page_of(d, pc->page)->left + pc->x + ln->left + pl->boxes[j].x; box->y = pc->y + ln->ascent - OBJ_H(im);
        box->w = OBJ_W(im); box->h = OBJ_H(im); box->img = k; box->para = at.para; box->wrap = WRAP_INLINE;
        return 1;
      }
  }
  return 0;
}
/* the object under the pointer (floats in front, then pictures in lines, then floats behind): its
   index, its reference and box; -1 if none */
static int obj_hit(struct Data *d, Object *obj, LONG mx, LONG my, Pos *at, FloatBox *box)
{
  Layout *L = d->lay;
  i32 px, py;
  u32 page = point_page(d, obj, mx, my, &px, &py), i, a, b;
  int pass, selk = sel_object(d, at);
  /* the selected one first (drawn over the others) */
  if (selk >= 0 && obj_box(d, *at, selk, box) && box->page == page && px >= box->x && px < box->x + box->w && py >= box->y && py < box->y + box->h) return selk;
  for (pass = 0; pass < 3; pass++) {
    if (pass == 1) {
      lay_page_lines(L, page, &a, &b);
      for (i = a; i < b; i++) {
        Placed *pc = &L->placed[i];
        ParaLay *pl = &L->pl[pc->para];
        Line *ln = &pl->lines[pc->line];
        u32 j;
        if (pc->page != page || py < pc->y || py >= pc->y + ln->height) continue;
        for (j = ln->box0; j < ln->box0 + ln->nbox; j++)
          if (pl->boxes[j].kind == BX_PIC) {
            Pos p;
            p.para = pc->para; p.off = pl->boxes[j].off;
            if (obj_box(d, p, pic_index(pl->p->t + p.off), box) && px >= box->x && px < box->x + box->w && py >= box->y && py < box->y + box->h) { *at = p; return box->img; }
          }
      }
      continue;
    }
    {
      /* from the top of the stack down */
      u32 *ord = lay_stack_order(L, L->floats, L->nfloats), j;
      for (j = L->nfloats; j-- > 0; ) {
        FloatBox *f = &L->floats[ord ? ord[j] : j];
        LONG off;
        if (f->page != page || (f->wrap == WRAP_BEHIND) != (pass == 2) || px < f->x || px >= f->x + f->w || py < f->y || py >= f->y + f->h) continue;
        if ((off = obj_ref(d->doc, f->para, f->img)) < 0) continue;
        at->para = f->para; at->off = (u32)off; *box = *f;
        free(ord);
        return f->img;
      }
      free(ord);
    }
  }
  return -1;
}
/* the handles: across 0 left, 1 middle, 2 right; down the same */
static const BYTE oh_x[8] = { 0, 1, 2, 2, 2, 1, 0, 0 }, oh_y[8] = { 0, 0, 0, 1, 2, 2, 2, 1 };
/* box b's top left on the screen */
static void box_screen(struct Data *d, Object *obj, const FloatBox *b, LONG *x, LONG *y)
{
  *x = _mleft(obj) + page_x(d, b->page, _mwidth(obj)) + tx(d, b->x);
  *y = _mtop(obj) + page_top(d, b->page) - d->scrolly + tx(d, b->y);
}
/* the selected object's handle under the pointer (1-8, 10 the turning one), 0 if none */
static int handle_at(struct Data *d, Object *obj, LONG mx, LONG my, FloatBox *box)
{
  Pos at;
  int k = sel_object(d, &at), i;
  LONG x, y, w, h;
  if (k < 0 || !obj_box(d, at, k, box)) return 0;
  box_screen(d, obj, box, &x, &y);
  w = tx(d, box->w); h = tx(d, box->h);
  if (box->wrap != WRAP_INLINE && !OBJ_IS_GRAPHIC(d->doc->images[k].kind) && mx >= x + (w - 1) / 2 - 5 && mx <= x + (w - 1) / 2 + 5 && my >= y - ROT_HANDLE - 5 && my <= y - ROT_HANDLE + 5) return 10;   /* (WP15a: charts don't turn) */
  for (i = 0; i < 8; i++) {
    LONG cx = x + oh_x[i] * (w - 1) / 2, cy = y + oh_y[i] * (h - 1) / 2;
    if (mx >= cx - 4 && mx <= cx + 4 && my >= cy - 4 && my <= cy + 4) return i + 1;
  }
  return 0;
}
/* the dragged object's box (twips on its page) with the pointer at mx, my */
static void drag_rect(struct Data *d, LONG mx, LONG my, i32 *l, i32 *t, i32 *r, i32 *b)
{
  FloatBox *f = &d->odrag_fb;
  i32 dx = tw(d, mx - d->odrag_mx), dy = tw(d, my - d->odrag_my);
  *l = f->x; *t = f->y; *r = f->x + f->w; *b = f->y + f->h;
  if (d->odrag == 9) { *l += dx; *r += dx; *t += dy; *b += dy; return; }
  {
    int hx = oh_x[d->odrag - 1], hy = oh_y[d->odrag - 1];
    if (hx == 0) *l += dx; else if (hx == 2) *r += dx;
    if (hy == 0) *t += dy; else if (hy == 2) *b += dy;
    if (*r - *l < 120) { if (hx == 0) *l = *r - 120; else *r = *l + 120; }
    if (*b - *t < 120) { if (hy == 0) *t = *b - 120; else *b = *t + 120; }
    /* a picture's corner keeps its shape */
    if (hx != 1 && hy != 1 && d->odrag_k < d->doc->nimages && d->doc->images[d->odrag_k].kind == OBJ_PICTURE && f->w > 0 && !wp17_cropping(d->odrag_k)) {
      i32 nh = (i32)((long)(*r - *l) * f->h / f->w);
      if (hy == 0) *t = *b - nh; else *b = *t + nh;
    }
  }
}
/* the dragged outline drawn, or taken away (XOR), within the view */
static void drag_outline(struct Data *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG x0 = d->odrag_r[0], y0 = d->odrag_r[1], x1 = d->odrag_r[2], y1 = d->odrag_r[3];
  LONG L = _mleft(obj), T = _mtop(obj), R = _mright(obj), B = _mbottom(obj);
  SetDrMd(rp, COMPLEMENT);
  if (d->odrag == 10) {
    /* turning: its frame at the angle now (clipped to the view) */
    APTR clip = (APTR)MUI_AddClipping(muiRenderInfo(obj), L, T, R - L + 1, B - T + 1);
    int i;
    for (i = 0; i < 4; i++) { Move(rp, d->odrag_p[2 * i], d->odrag_p[2 * i + 1]); Draw(rp, d->odrag_p[2 * ((i + 1) & 3)], d->odrag_p[2 * ((i + 1) & 3) + 1]); }
    MUI_RemoveClipping(muiRenderInfo(obj), (struct Region *)clip);
    SetDrMd(rp, JAM1);
    return;
  }
  if (y0 >= T && y0 <= B) { Move(rp, x0 < L ? L : x0, y0); Draw(rp, x1 > R ? R : x1, y0); }
  if (y1 >= T && y1 <= B && y1 != y0) { Move(rp, x0 < L ? L : x0, y1); Draw(rp, x1 > R ? R : x1, y1); }
  if (x0 >= L && x0 <= R && y1 - y0 > 1) { Move(rp, x0, (y0 < T ? T : y0 + 1)); Draw(rp, x0, y1 > B ? B : y1 - 1); }
  if (x1 >= L && x1 <= R && x1 != x0 && y1 - y0 > 1) { Move(rp, x1, (y0 < T ? T : y0 + 1)); Draw(rp, x1, y1 > B ? B : y1 - 1); }
  SetDrMd(rp, JAM1);
}
/* turning: the angle with the pointer at mx, my (Shift: in steps of 15 degrees) */
static int drag_angle(struct Data *d, Object *obj, LONG mx, LONG my, int snap)
{
  LONG x, y;
  int a;
  box_screen(d, obj, &d->odrag_fb, &x, &y);
  x += tx(d, d->odrag_fb.w) / 2; y += tx(d, d->odrag_fb.h) / 2;
  a = (d->doc->images[d->odrag_k].rot + iatan2(my - y, mx - x) - d->odrag_a0 + 7200) % 3600;
  if (snap) a = (a + 75) / 150 * 150 % 3600;
  return a;
}
/* the dragged object's outline (or turned frame) moved to the pointer at mx, my: the old one XORed
   away, the new one XORed on, so nothing under it is drawn again until it is let go */
static void drag_show(struct Data *d, Object *obj, LONG mx, LONG my)
{
  i32 l, t, r, b;
  LONG ox, oy;
  FloatBox f = d->odrag_fb;
  if (d->odrag_on) drag_outline(d, obj);
  if (d->odrag == 10) {
    /* the frame turned round the box's centre */
    static const BYTE cu[4] = { 0, 1, 1, 0 }, cv[4] = { 0, 0, 1, 1 };
    Image t2 = d->doc->images[d->odrag_k];
    LONG cx, cy;
    int i;
    d->odrag_rot = drag_angle(d, obj, mx, my, d->odrag_snap);
    t2.rot = (u16)d->odrag_rot;
    box_screen(d, obj, &f, &cx, &cy);
    cx += tx(d, f.w) / 2; cy += tx(d, f.h) / 2;
    for (i = 0; i < 4; i++) {
      i32 X, Y;
      obj_point(&t2, cu[i] * t2.cx, cv[i] * t2.cy, &X, &Y);
      d->odrag_p[2 * i] = cx + tx(d, X - OBJ_W(&t2) / 2); d->odrag_p[2 * i + 1] = cy + tx(d, Y - OBJ_H(&t2) / 2);
    }
    drag_outline(d, obj);
    d->odrag_on = 1;
    return;
  }
  drag_rect(d, mx, my, &l, &t, &r, &b);
  f.x = 0; f.y = 0;
  box_screen(d, obj, &f, &ox, &oy);
  d->odrag_r[0] = ox + tx(d, l); d->odrag_r[1] = oy + tx(d, t); d->odrag_r[2] = ox + tx(d, r) - 1; d->odrag_r[3] = oy + tx(d, b) - 1;
  drag_outline(d, obj);
  d->odrag_on = 1;
}
/* a table cell's right edge under the pointer (within two pixels): its table, the grid edge, and the
   table's left edge (twips from the page's); 0 if none */
static int edge_at(struct Data *d, Object *obj, LONG mx, LONG my, u16 *table, int *edge, i32 *tleft)
{
  Layout *L = d->lay;
  LONG y = my - _mtop(obj) + d->scrolly, x = mx - _mleft(obj), w = _mwidth(obj);
  u32 page, i;
  i32 px, py, slack = tw(d, 2) + 1;
  if (!paged(d) || !L || !L->ncells) return 0;
  {
    LONG row = (LONG)row_at(d, y), c = (x - page_left(d, w) + GAP / 2) / (page_w(d) + GAP);
    if (c < 0) c = 0;
    if (c >= cols(d)) c = cols(d) - 1;
    page = (u32)(row * cols(d) + c);
  }
  if (page >= L->npages) return 0;
  px = tw(d, x - page_x(d, page, w)); py = tw(d, y - page_top(d, page));
  for (i = 0; i < L->ncells; i++) {
    CellBox *c = &L->cells[i];
    i32 ex = c->x + c->w;
    if (c->page != page || py < c->y || py > c->y + c->h || px < ex - slack || px > ex + slack || !c->table || c->table > d->doc->ntables) continue;
    {
      Table *t = &d->doc->tables[c->table - 1];
      int k;
      i32 left = c->x;
      for (k = 0; k < c->col && k < t->ncols; k++) left -= t->grid[k];
      *table = c->table; *edge = c->col + c->span; *tleft = left;
      return *edge >= 1 && *edge <= t->ncols;
    }
  }
  return 0;
}
/* WP21a/WP21b: a table tool's stroke, drawn or taken away (XOR): a box for Draw Table (a line when it
   is thin, as it is then one), a line for the others; clipped to the view */
static void tdrag_show(struct Data *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG x0 = d->tdrag_mx, y0 = d->tdrag_my, x1 = d->tdrag_x, y1 = d->tdrag_y, t, ax, ay;
  LONG L = _mleft(obj), T = _mtop(obj), R = _mright(obj), B = _mbottom(obj);
  APTR clip = MUI_AddClipping((struct MUI_RenderInfo *)(APTR)muiRenderInfo(obj), (WORD)L, (WORD)T, (WORD)(R - L + 1), (WORD)(B - T + 1));
  SetDrMd(rp, COMPLEMENT);
  ax = x1 > x0 ? x1 - x0 : x0 - x1; ay = y1 > y0 ? y1 - y0 : y0 - y1;
  if (wp21_tool() == 1 && ax > 6 && ay > 6 && ax < 3 * ay && ay < 3 * ax) {
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    Move(rp, x0, y0); Draw(rp, x1, y0); Draw(rp, x1, y1); Draw(rp, x0, y1); Draw(rp, x0, y0 + 1);
  } else if (x0 != x1 || y0 != y1) { Move(rp, x0, y0); Draw(rp, x1, y1); }
  else RectFill(rp, x0 - 1, y0 - 1, x0 + 1, y0 + 1);
  SetDrMd(rp, JAM1);
  MUI_RemoveClipping((struct MUI_RenderInfo *)(APTR)muiRenderInfo(obj), clip);
}
/* the line showing where a dragged column edge goes (drawn and taken away by XOR) */
static void coldrag_line(struct Data *d, Object *obj, LONG x)
{
  struct RastPort *rp = _rp(obj);
  SetDrMd(rp, COMPLEMENT);
  Move(rp, x, _mtop(obj)); Draw(rp, x, _mbottom(obj));
  SetDrMd(rp, JAM1);
}
/* the caret to p, the selection extended (Shift) or not; a pending format is forgotten */
static void move_to(struct Data *d, Pos p, int extend)
{
  d->caret = p;
  if (!extend) d->anchor = p;
  d->pending = 0;
}
/* a raw key: moving, editing keys, typing. Codes are the Amiga's raw ones (0x4C-0x4F the cursor keys,
   0x41 Backspace, 0x46 Del, 0x44/0x43 Return and Enter, 0x42 Tab, 0x45 Esc); others go through the
   keymap (MapRawKey) to Latin-1 text. 1 if the key was used. When more input is already waiting,
   layout and drawing are put off (stale) so a slow redraw doesn't hold up a fast typist */
static int key(struct Data *d, Object *obj, struct IntuiMessage *im)
{
  Doc *doc = d->doc;
  UWORD code = im->Code, q = im->Qualifier;
  int shift = (q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0, alt = (q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) != 0;
  int ctrl = (q & IEQUALIFIER_CONTROL) != 0;
  int vert = 0, edited = 0;
  i32 keepx = d->wantx;
  if (code & IECODE_UP_PREFIX) {
    /* the last key's release: if typing ran ahead of the screen, catch up now */
    if (d->stale && _window(obj) && IsMsgPortEmpty(_window(obj)->UserPort)) { caret_hide(d, obj); refresh(d, obj, 1); d->stale = 0; }
    return 0;
  }
  if (q & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND)) return 0;      /* menu shortcuts */
  if (ww_key(d, obj, im)) return 1;             /* WP25: reflowed Read Mode, Side to Side's wheel */
  if (d->vmode == VM_READ) {
    /* read mode: no editing; the keys turn the pages, Esc goes back to print layout */
    LONG vh = _mheight(obj);
    switch (code) {
    case 0x45: set(obj, MUIA_Quill_ViewMode, VM_PRINT); return 1;
    case 0x4C: case 0x4F: case 0x48: case 0x41: scroll_to(d, obj, d->scrolly - vh + 16); return 1;
    case 0x4D: case 0x4E: case 0x49: case 0x40: scroll_to(d, obj, d->scrolly + vh - 16); return 1;
    case 0x70: scroll_to(d, obj, 0); return 1;
    case 0x71: scroll_to(d, obj, d->doch); return 1;
    }
    return 1;
  }
  if (ev_key(d, obj, im)) return 1;             /* WP14b: keys inside an equation, and into one */
  {
    /* a work package's key (wphooks.h): its command, run by main.c's loop */
    WpCtx wc;
    ULONG wid;
    wp_ctx(d, &wc, WPC_TEXT);
    if ((wid = wp_key(code, q, &wc))) { DoMethod(_app(obj), MUIM_Application_ReturnID, wid); return 1; }
  }
  if (fv_key(d, obj, im, &shift)) return 1;    /* WP12: Extend Selection, column selection */
  caret_hide(d, obj);
  if (d->stale && ((code >= 0x4C && code <= 0x4F) || code == 0x70 || code == 0x71 || code == 0x48 || code == 0x49)) { relayout(d, obj); d->stale = 0; }   /* the cursor keys need the layout */
  switch (code) {
  /* a PC keyboard's Home, End, Page Up and Page Down (as AROS and most adapters send them) */
  case 0x70: move_to(d, ctrl ? doc_start(doc) : lay_line_start(d->lay, d->caret), shift); break;
  case 0x71: move_to(d, ctrl ? doc_end_pos(doc) : lay_line_end(d->lay, d->caret), shift); break;
  case 0x48: case 0x49: {
    int n = (int)(_mheight(obj) * 2000L / d->zoom / 276), i;
    Pos p = d->caret;
    if (keepx < 0) { u32 pg; i32 x, y, h; lay_caret(d->lay, p, &pg, &x, &y, &h); keepx = x; }
    for (i = 0; i < n; i++) p = lay_updown(d->lay, p, keepx, code == 0x48 ? -1 : 1);
    move_to(d, p, shift);
    vert = 1;
    break; }
  case 0x4F: move_to(d, ctrl ? lay_line_start(d->lay, d->caret) : alt ? pos_word_left(doc, d->caret) : (!shift && has_sel(d)) ? sel_lo(d) : pos_left(doc, d->caret), shift); break;
  case 0x4E: move_to(d, ctrl ? lay_line_end(d->lay, d->caret) : alt ? pos_word_right(doc, d->caret) : (!shift && has_sel(d)) ? sel_hi(d) : pos_right(doc, d->caret), shift); break;
  case 0x4C: case 0x4D: {
    int dir = code == 0x4C ? -1 : 1;
    if (ctrl && alt) { move_to(d, dir < 0 ? doc_start(doc) : doc_end_pos(doc), shift); break; }
    if (alt) {                                            /* a paragraph */
      Pos p = d->caret;
      if (dir < 0) { if (p.off) p.off = 0; else if (p.para) p.para--; p.off = 0; }
      else { if (p.para + 1 < doc->np) { p.para++; p.off = 0; } else p.off = doc->p[p.para]->len; }
      move_to(d, p, shift);
      break;
    }
    {
      int n = ctrl ? (int)(_mheight(obj) * 2000L / d->zoom / 276) : 1, i;     /* Ctrl: a screenful */
      Pos p = d->caret;
      if (keepx < 0) { u32 pg; i32 x, y, h; lay_caret(d->lay, p, &pg, &x, &y, &h); keepx = x; }
      for (i = 0; i < n; i++) p = lay_updown(d->lay, p, keepx, dir);
      move_to(d, p, shift);
      vert = 1;
    }
    break; }
  case 0x41:                                              /* Backspace */
    if (has_sel(d)) delete_sel(d);
    else if (d->caret.off == 0 && d->caret.para && !same_cell(&doc->p[d->caret.para - 1]->pf, &doc->p[d->caret.para]->pf)) {
      /* at the start of a cell, or just after a table: cells are not joined. An empty paragraph after
         a table goes, otherwise the caret just moves back */
      Para *p = doc->p[d->caret.para];
      Pos a = pos_left(doc, d->caret);
      if (!in_table(&p->pf) && !p->len && !p->pf.list && d->caret.para + 1 < doc->np) doc_delete(doc, a, d->caret);
      d->caret = d->anchor = a;
    }
    else if (alt) { Pos a = pos_word_left(doc, d->caret); if (a.para != d->caret.para) a.off = 0, a.para = d->caret.para; vdelete(d, a, d->caret); d->caret = d->anchor = a; }
    else if (d->caret.off || d->caret.para) {
      Para *p = doc->p[d->caret.para];
      if (d->caret.off == 0 && p->pf.list) {              /* Backspace at a list item's start ends the list there */
        ParaFmt pf = p->pf; pf.list = 0; pf.level = 0; pf.left = doc->styles[pf.style].pf.left; pf.first = doc->styles[pf.style].pf.first;
        doc_para_format(doc, d->caret.para, d->caret.para, PM_LIST | PM_INDENT, &pf);
      } else if (tracking(d) && d->caret.off == 0) { d->caret = d->anchor = pos_left(doc, d->caret); }      /* paragraphs are not joined while tracking */
      else { Pos a = pos_left(doc, d->caret); vdelete(d, a, d->caret); d->caret = d->anchor = a; }
    }
    edited = 1; break;
  case 0x46:                                              /* Del */
    if (has_sel(d)) delete_sel(d);
    else {
      Pos b = alt ? pos_word_right(doc, d->caret) : pos_right(doc, d->caret);
      if (b.para != d->caret.para && !same_cell(&doc->p[d->caret.para]->pf, &doc->p[b.para]->pf)) b = d->caret;   /* not into the next cell */
      if (pos_cmp(b, d->caret)) {
        u32 len = doc->p[d->caret.para]->len;
        if (tracking(d) && b.para != d->caret.para) b = d->caret;
        vdelete(d, d->caret, b);
        if (tracking(d) && doc->p[d->caret.para]->len == len) d->caret = d->anchor = b;    /* marked, not gone: past it */
      }
    }
    edited = 1; break;
  case 0x44: case 0x43: {                                 /* Return */
    Para *p = doc->p[d->caret.para];
    if (shift) insert_text(d, "\v", 1);                  /* Shift-Return: a line break */
    else if (p->pf.list && !p->len) {                     /* Return in an empty list item ends the list */
      ParaFmt pf = p->pf; pf.list = 0; pf.level = 0; pf.left = doc->styles[pf.style].pf.left; pf.first = doc->styles[pf.style].pf.first;
      doc_para_format(doc, d->caret.para, d->caret.para, PM_LIST | PM_INDENT, &pf);
    } else { insert_text(d, "\n", 1); wp8_typed(d->doc, &d->caret, "\n", 1, tracking(d) ? rev_id(d) : -1); d->anchor = d->caret; }   /* (WP8) */
    edited = 1; break; }
  case 0x42:                                              /* Tab: in a list item's start, a level deeper */
    if (doc->p[d->caret.para]->pf.list && d->caret.off == 0 && !has_sel(d)) indent(d, shift ? -1 : 1);
    else if (in_table(&doc->p[d->caret.para]->pf) && !alt) {
      /* in a table: the next cell (a new row after the last), or the one before */
      Pos p = d->caret;
      if (shift) { if (tbl_next_cell(doc, &p, -1)) select_cell(d, p.para); edited = 0; break; }
      if (tbl_next_cell(doc, &p, 1)) { select_cell(d, p.para); edited = 0; break; }
      d->caret = d->anchor = p;
      if (tbl_row_insert(doc, &d->caret, 1)) d->anchor = d->caret;
    }
    else insert_text(d, "\t", 1);
    edited = 1; break;
  case 0x45:                                                       /* Esc */
    if (has_sel(d)) d->anchor = d->caret;
    else if (doc->edit.kind) { set(obj, MUIA_Quill_EditStory, 0x100); return 1; }    /* done with the story */
    break;
  case 0x7A: case 0x7B: scroll_to(d, obj, d->scrolly + (code == 0x7A ? -60 : 60)); return 1;   /* wheel */
  default: {
    struct InputEvent ie;
    char buf[8], u[32];
    LONG n, i, o = 0;
    memset(&ie, 0, sizeof ie);
    ie.ie_Class = IECLASS_RAWKEY; ie.ie_Code = code; ie.ie_Qualifier = q & ~IEQUALIFIER_CONTROL;
    ie.ie_EventAddress = (APTR *)*((ULONG *)im->IAddress);
    n = MapRawKey(&ie, (STRPTR)buf, sizeof buf, NULL);
    /* function keys, Help and the like map to CSI (0x9B) sequences: not text. Nor are control codes */
    if (n > 0 && (u8)buf[0] == 0x9B) n = 0;
    for (i = 0; i < n; i++) if ((u8)buf[i] >= 32 && (u8)buf[i] != 127 && !((u8)buf[i] >= 0x80 && (u8)buf[i] < 0xA0)) o += utf8_put(u + o, (u8)buf[i]);
    if (!o) { if (!d->caret_shown) caret_show(d, obj); return 0; }
    insert_text(d, u, (u32)o);
    wp8_typed(d->doc, &d->caret, u, (u32)o, tracking(d) ? rev_id(d) : -1); d->anchor = d->caret;   /* (WP8: AutoCorrect, AutoFormat) */
    edited = 1;
  }
  }
  if (!vert) d->wantx = -1; else d->wantx = keepx;
  /* more keys waiting (a fast typist on a slow Amiga): take them first, lay out and draw once */
  if (edited && _window(obj) && !IsMsgPortEmpty(_window(obj)->UserPort)) { d->stale = 1; return 1; }
  refresh(d, obj, edited || d->stale);
  d->stale = 0;
  return 1;
}
/* select the caret's word and the spaces after it (a double-click) */
static void select_word(struct Data *d)
{
  Para *p = d->doc->p[d->caret.para];
  Pos a = d->caret, b = d->caret;
  while (a.off > 0 && p->t[a.off - 1] != ' ' && p->t[a.off - 1] != '\t') a.off = (u32)utf8_prev(p->t, a.off);
  while (b.off < p->len && p->t[b.off] != ' ' && p->t[b.off] != '\t') b.off = (u32)utf8_next(p->t, p->len, b.off);
  while (b.off < p->len && p->t[b.off] == ' ') b.off++;                /* and the space after it, as Word */
  d->anchor = a; d->caret = b;
}
/* the dragged object let go at mx, my: moved or sized there (one undo step) */
static void obj_drop(struct Data *d, Object *obj, LONG mx, LONG my)
{
  Image m;
  Pos at;
  i32 l, t, r, b;
  FloatBox *f = &d->odrag_fb;
  if (sel_object(d, &at) != d->odrag_k || d->odrag_k >= d->doc->nimages) return;
  m = d->doc->images[d->odrag_k];
  if (d->odrag == 10) {
    /* turned round its centre */
    if (d->odrag_rot == m.rot) return;
    m.rot = (u16)d->odrag_rot;
    l = f->x + f->w / 2 - OBJ_W(&m) / 2; t = f->y + f->h / 2 - OBJ_H(&m) / 2;
    lay_float_moved(d->lay, f, &m, l, t);
    DoMethod(obj, MUIM_Quill_Command, CMD_OBJSET, (ULONG)&m);
    return;
  }
  drag_rect(d, mx, my, &l, &t, &r, &b);
  if (l == f->x && t == f->y && r == f->x + f->w && b == f->y + f->h) return;
  if (d->odrag != 9 && wp17_cropping(d->odrag_k) && f->w > 0 && f->h > 0) {
    /* WP17: cropped by its handles, the picture staying where it was */
    wp17_crop_drop(&m, f, l, t, r, b);
    if (f->wrap != WRAP_INLINE && (l != f->x || t != f->y)) lay_float_moved(d->lay, f, &m, l, t);
    DoMethod(obj, MUIM_Quill_Command, CMD_OBJSET, (ULONG)&m);
    return;
  }
  if (d->odrag != 9 && f->w > 0 && f->h > 0) {
    /* the box stretched: the object stretched along its own sides as much as they lie across and down */
    long c = icos(m.rot), sn = isin(m.rot), c2 = c * c / TRIG_ONE, s2 = sn * sn / TRIG_ONE;
    long sx = (long)(r - l) * TRIG_ONE / f->w, sy = (long)(b - t) * TRIG_ONE / f->h;
    m.cx = (i32)((long long)m.cx * ((sx * c2 + sy * s2) / TRIG_ONE) / TRIG_ONE);
    m.cy = (i32)((long long)m.cy * ((sx * s2 + sy * c2) / TRIG_ONE) / TRIG_ONE);
    if (m.cx < 60) m.cx = 60;
    if (m.cy < 60) m.cy = 60;
  }
  if (f->wrap != WRAP_INLINE && (l != f->x || t != f->y)) lay_float_moved(d->lay, f, &m, l, t);
  DoMethod(obj, MUIM_Quill_Command, CMD_OBJSET, (ULONG)&m);
}
/* MUIM_HandleEvent: keys and the mouse from the window's event handler (d->ehn); keys are left alone
   while another object has them (key_grab). Mouse moves and ticks are asked for only during a drag
   (the handler removed and added again with more IDCMP flags), as each move would wake us. A click
   tries a comment balloon, an object or its handle, a table's column edge, a header, footer or note
   (double-click), then the text. Returns MUI_EventHandlerRC_Eat for what it used */
static void hover(struct Data *d, Object *obj, WORD x, WORD y);
static ULONG handle_event(struct IClass *cl, Object *obj, struct MUIP_HandleEvent *m)
{
  struct Data *d = INST_DATA(cl, obj);
  struct IntuiMessage *im = m->imsg;
  if (!im || !d->doc || !d->lay || !d->shown) return 0;
  ww_sync(d, obj, 1);                            /* WP25: changed in another view */
  ww_click(d, obj, im);                          /* WP25: the working pane of a split window */
  if (im->Class == IDCMP_RAWKEY) return key_grab || ww_passive(d) || !key(d, obj, im) ? 0 : MUI_EventHandlerRC_Eat;
  { ULONG r = ev_mouse(d, obj, im); if (r) return r; }    /* WP14b: dragging in an equation */
  { ULONG r = fv_mouse(d, obj, im); if (r) return r; }    /* WP12: Select Objects */
  { ULONG r = dv_mouse(d, obj, im); if (r) return r; }    /* WP20: drawing, the eraser, the lasso, Ink Replay */
  { ULONG r = ov_mouse(d, obj, im); if (r) return r; }    /* WP11: the outline symbols */
  if (im->Class == IDCMP_MOUSEBUTTONS) {
    if (im->Code == SELECTDOWN && im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj) && im->MouseY >= _mtop(obj) && im->MouseY <= _mbottom(obj)) {
      Pos p;
      int i;
      /* a comment's balloon: its range selected */
      for (i = 0; i < d->nballoon; i++) {
        LONG x = im->MouseX - _mleft(obj), y = im->MouseY - _mtop(obj);
        Pos a, b;
        if (x < d->balloon[i].x0 || x > d->balloon[i].x1 || y < d->balloon[i].y0 || y > d->balloon[i].y1) continue;
        if (d->balloon[i].kind != RVI_COMMENT) {
          /* (WP5) a change's balloon: what it is about selected */
          caret_hide(d, obj);
          d->anchor = pos_clamp(d->doc, d->balloon[i].a); d->caret = pos_clamp(d->doc, d->balloon[i].b);
          refresh(d, obj, 0);
          return MUI_EventHandlerRC_Eat;
        }
        if (DoubleClick(d->secs, d->micros, im->Seconds, im->Micros)) {
          /* double-clicked: its text edited */
          d->secs = 0;
          d->sreq.kind = SK_COMMENT; d->sreq.index = d->balloon[i].k; d->sreq.page = 0;
          set(obj, MUIA_Quill_EditStory, SK_COMMENT);
          return MUI_EventHandlerRC_Eat;
        }
        d->secs = im->Seconds; d->micros = im->Micros;
        if (doc_comment_mark(d->doc, d->balloon[i].k, 0, &a)) {
          if (!doc_comment_mark(d->doc, d->balloon[i].k, 1, &b)) b = a;
          caret_hide(d, obj);
          a.off += 4;
          d->anchor = a; d->caret = pos_cmp(b, a) > 0 ? b : a;
          refresh(d, obj, 0);
        }
        return MUI_EventHandlerRC_Eat;
      }
      if (wp21_tool() && paged(d) && !d->doc->edit.kind) {
        /* WP21a/WP21b: a table tool (Draw Table, Eraser, Border Painter): a stroke dragged out */
        caret_hide(d, obj);
        d->tdrag = 1; d->tdrag_on = 0; d->tdrag_mx = d->tdrag_x = im->MouseX; d->tdrag_my = d->tdrag_y = im->MouseY;
        tdrag_show(d, obj);
        DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
        d->ehn.ehn_Events |= IDCMP_MOUSEMOVE;
        DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
        set(_win(obj), MUIA_Window_ActiveObject, (ULONG)obj);
        return MUI_EventHandlerRC_Eat;
      }
      {
        /* an object: selected; by its box moved (if it floats), by a handle sized */
        FloatBox fb;
        Pos at;
        int hnd = handle_at(d, obj, im->MouseX, im->MouseY, &fb), shift = (im->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
        if (!hnd && (im->Qualifier & IEQUALIFIER_CONTROL) && d->lay) {
          /* Ctrl-click on a link opens it (main.c asks for the link at the caret, put there first) */
          Pos lp = point_pos(d, obj, im->MouseX, im->MouseY);
          if (doc_link_at(d->doc, lp, NULL, NULL)) {
            caret_hide(d, obj);
            d->anchor = d->caret = lp; d->pending = 0;
            refresh(d, obj, 0);
            DoMethod(_app(obj), MUIM_Application_ReturnID, ID_LINKOPEN);
            return MUI_EventHandlerRC_Eat;
          }
        }
        if (!hnd && shift && obj_hit(d, obj, im->MouseX, im->MouseY, &at, &fb) >= 0 && fb.wrap != WRAP_INLINE) {
          /* Shift-click: another floating object chosen with the one selected (to group them) */
          Pos cur;
          FloatBox cb;
          int k = sel_object(d, &cur), j;
          caret_hide(d, obj);
          if (k >= 0 && !d->nmsel && obj_box(d, cur, k, &cb) && cb.wrap != WRAP_INLINE) d->msel[d->nmsel++] = cur;
          for (j = 0; j < d->nmsel && pos_cmp(d->msel[j], at); j++) ;
          if (j == d->nmsel && d->nmsel < 16) d->msel[d->nmsel++] = at;
          if (k < 0) { d->anchor = at; d->caret = at; d->caret.off += 4; }
          refresh(d, obj, 0);
          return MUI_EventHandlerRC_Eat;
        }
        d->nmsel = 0;
        if (hnd || obj_hit(d, obj, im->MouseX, im->MouseY, &at, &fb) >= 0) {
          caret_hide(d, obj);
          if (!hnd) {
            int again = sel_object(d, NULL) == fb.img && DoubleClick(d->secs, d->micros, im->Seconds, im->Micros);
            d->secs = im->Seconds; d->micros = im->Micros;
            d->anchor = at; d->caret = at; d->caret.off += 4; d->pending = 0;
            refresh(d, obj, 0);
            if (again) { set(obj, MUIA_Quill_EditObject, fb.img); return MUI_EventHandlerRC_Eat; }    /* double-clicked: its dialog */
          }
          d->odrag = hnd ? hnd : fb.wrap == WRAP_INLINE ? 0 : 9;
          if (d->odrag) {
            d->odrag_k = fb.img; d->odrag_fb = fb; d->odrag_mx = im->MouseX; d->odrag_my = im->MouseY; d->odrag_on = 0;
            if (d->odrag == 10) {
              LONG x, y;
              box_screen(d, obj, &fb, &x, &y);
              d->odrag_a0 = iatan2(im->MouseY - (y + tx(d, fb.h) / 2), im->MouseX - (x + tx(d, fb.w) / 2));
              d->odrag_rot = d->doc->images[fb.img].rot;
            }
            DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
            d->ehn.ehn_Events |= IDCMP_MOUSEMOVE;
            DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
          }
          set(_win(obj), MUIA_Window_ActiveObject, (ULONG)obj);
          return MUI_EventHandlerRC_Eat;
        }
      }
      {
        /* a table's column edge: dragged sideways */
        u16 t; int e; i32 tl;
        if (edge_at(d, obj, im->MouseX, im->MouseY, &t, &e, &tl)) {
          caret_hide(d, obj);
          d->coldrag = 1; d->coldrag_t = t; d->coldrag_edge = e; d->coldrag_x0 = d->coldrag_start = im->MouseX;
          coldrag_line(d, obj, im->MouseX);
          DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
          d->ehn.ehn_Events |= IDCMP_MOUSEMOVE;
          DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
          return MUI_EventHandlerRC_Eat;
        }
      }
      if (DoubleClick(d->secs, d->micros, im->Seconds, im->Micros) && !d->doc->edit.kind) {
        /* double-clicked in a header, footer or footnote: it is edited */
        i32 px, py;
        int idx, kind;
        u32 pg = point_page(d, obj, im->MouseX, im->MouseY, &px, &py);
        if ((kind = story_at(d, pg, px, py, &idx))) {
          d->secs = 0;
          d->sreq.kind = kind; d->sreq.index = idx; d->sreq.page = pg;
          set(obj, MUIA_Quill_EditStory, kind);
          return MUI_EventHandlerRC_Eat;
        }
      }
      if (ev_click(d, obj, im)) return MUI_EventHandlerRC_Eat;      /* WP14b: a click into an equation */
      p = point_pos(d, obj, im->MouseX, im->MouseY);
      int shift = (im->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
      fv_click(d, im, &shift);                 /* WP12 */
      caret_hide(d, obj);
      if (DoubleClick(d->secs, d->micros, im->Seconds, im->Micros) && !shift) d->clicks++; else d->clicks = 1;
      d->secs = im->Seconds; d->micros = im->Micros;
      move_to(d, p, shift);
      if (d->clicks == 2) select_word(d);
      else if (d->clicks >= 3) { d->anchor.para = d->caret.para; d->anchor.off = 0; d->caret.off = d->doc->p[d->caret.para]->len; }
      d->wantx = -1;
      d->dragging = 1;
      DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
      d->ehn.ehn_Events |= IDCMP_MOUSEMOVE | IDCMP_INTUITICKS;
      DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
      set(_win(obj), MUIA_Window_ActiveObject, (ULONG)obj);
      refresh(d, obj, 0);
      return MUI_EventHandlerRC_Eat;
    }
    if (im->Code == SELECTUP && d->tdrag) {
      /* WP21a/WP21b: the stroke done: its ends on the page where it began, to the tool */
      i32 x0, y0, x1, y1;
      u32 page;
      tdrag_show(d, obj);
      d->tdrag = 0;
      DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
      DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
      page = point_page(d, obj, d->tdrag_mx, d->tdrag_my, &x0, &y0);
      x1 = x0 + tw(d, im->MouseX - d->tdrag_mx); y1 = y0 + tw(d, im->MouseY - d->tdrag_my);
      if (wp21_stroke(d->doc, d->lay, page, x0, y0, x1, y1, &d->caret)) { d->anchor = d->caret; d->pending = 0; refresh(d, obj, 1); }
      else { DisplayBeep(NULL); refresh(d, obj, 0); }
      return MUI_EventHandlerRC_Eat;
    }
    if (im->Code == SELECTUP && d->odrag) {
      if (d->odrag_on) drag_outline(d, obj);
      DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
      DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
      if (d->odrag_on) obj_drop(d, obj, im->MouseX, im->MouseY);
      d->odrag = 0; d->odrag_on = 0;
      return MUI_EventHandlerRC_Eat;
    }
    if (im->Code == SELECTUP && d->coldrag) {
      /* the edge where it was let go: the caret into that table, the columns changed */
      LONG dx = im->MouseX - d->coldrag_start;
      Table *t = d->coldrag_t <= d->doc->ntables ? &d->doc->tables[d->coldrag_t - 1] : NULL;
      u32 i;
      coldrag_line(d, obj, d->coldrag_x0);
      d->coldrag = 0;
      DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
      DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
      if (t && dx) {
        struct QColEdge q;
        int k;
        q.table = d->coldrag_t; q.edge = d->coldrag_edge; q.x = 0;
        for (k = 0; k < q.edge && k < t->ncols; k++) q.x += t->grid[k];
        q.x += tw(d, dx);
        if (d->doc->p[d->caret.para]->pf.cell.table != q.table)
          for (i = 0; i < d->doc->np; i++) if (d->doc->p[i]->pf.cell.table == q.table) { d->caret.para = i; d->caret.off = 0; d->anchor = d->caret; break; }
        DoMethod(obj, MUIM_Quill_Command, CMD_COLEDGE, (ULONG)&q);
      } else refresh(d, obj, 0);
      return MUI_EventHandlerRC_Eat;
    }
    if (im->Code == SELECTUP && d->dragging) {
      d->dragging = 0;
      DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
      d->ehn.ehn_Events &= ~IDCMP_INTUITICKS;
      DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
      if (d->brush && has_sel(d)) {
        /* the format painter: the selection gets the formatting copied */
        d->brush = 0;
        caret_hide(d, obj);
        doc_format(d->doc, sel_lo(d), sel_hi(d), CM_ALL, &d->brushfmt);
        refresh(d, obj, 1);
      }
      return MUI_EventHandlerRC_Eat;
    }
  }
  if (im->Class == IDCMP_MOUSEMOVE && d->odrag) {
    d->odrag_snap = (im->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
    /* (a few pixels first, so a click doesn't nudge it) */
    if (d->odrag_on || im->MouseX - d->odrag_mx > 2 || d->odrag_mx - im->MouseX > 2 || im->MouseY - d->odrag_my > 2 || d->odrag_my - im->MouseY > 2)
      drag_show(d, obj, im->MouseX, im->MouseY);
    return MUI_EventHandlerRC_Eat;
  }
  if (im->Class == IDCMP_MOUSEMOVE && d->tdrag) {
    if (im->MouseX != d->tdrag_x || im->MouseY != d->tdrag_y) { tdrag_show(d, obj); d->tdrag_x = im->MouseX; d->tdrag_y = im->MouseY; tdrag_show(d, obj); }
    return MUI_EventHandlerRC_Eat;
  }
  if (im->Class == IDCMP_MOUSEMOVE && d->coldrag) {
    if (im->MouseX != d->coldrag_x0) { coldrag_line(d, obj, d->coldrag_x0); d->coldrag_x0 = im->MouseX; coldrag_line(d, obj, d->coldrag_x0); }
    return MUI_EventHandlerRC_Eat;
  }
  if ((im->Class == IDCMP_MOUSEMOVE || im->Class == IDCMP_INTUITICKS) && d->dragging) {
    Pos p;
    if (im->MouseY < _mtop(obj)) scroll_to(d, obj, d->scrolly - 20);         /* drag past an edge: scroll */
    else if (im->MouseY > _mbottom(obj)) scroll_to(d, obj, d->scrolly + 20);
    p = point_pos(d, obj, im->MouseX, im->MouseY);
    if (pos_cmp(p, d->caret)) {
      caret_hide(d, obj);
      if (d->clicks == 2) {                       /* extending by words */
        Pos a = d->anchor;
        d->caret = p;
        if (pos_cmp(p, a) > 0) { Para *q = d->doc->p[p.para]; while (d->caret.off < q->len && q->t[d->caret.off] != ' ') d->caret.off++; }
      } else d->caret = p;
      refresh(d, obj, 0);
    }
    return MUI_EventHandlerRC_Eat;
  }
  if (im->Class == IDCMP_MOUSEMOVE) hover(d, obj, im->MouseX, im->MouseY);
  return 0;
}

/* the pointer moved over the page (no button held): over a link, its address goes on the status
   bar with how to open it, and comes off again when the pointer leaves. (Not a bubble: MUI puts
   those under the pointer, where they'd catch the right button meant for the link's menu.) Only
   a change of link is sent, so the bar isn't redrawn on every move */
static void hover(struct Data *d, Object *obj, WORD x, WORD y)
{
  int k = 0;
  if (!d->tb || !d->tb->statusbar) return;
  if (x >= _mleft(obj) && x <= _mright(obj) && y >= _mtop(obj) && y <= _mbottom(obj) && d->doc->nlinks)
    k = doc_link_at(d->doc, point_pos(d, obj, x, y), NULL, NULL);
  if (k == d->hoverlink) return;
  d->hoverlink = k;
  if (k) {
    char lat[200], s[200];
    const char *u = doc_link_url(d->doc, k);
    utf8_to_latin1(u, (u32)strlen(u), lat, sizeof lat);
    sprintf(s, "%.120s - %.70s", lat, tr("Ctrl-click to open"));
    DoMethod(d->tb->statusbar, MUIM_Status_Hint, (ULONG)s);
  } else DoMethod(d->tb->statusbar, MUIM_Status_Hint, (ULONG)"");
}

/* ------------------------------------------------------------------ */
/* printing: each page drawn in bands at the printer's resolution with the same code as the screen,
   and sent to printer.device as a graphic dump (which TurboPrint and the like take over) */
#define BAND 96
/* MUIM_Quill_Print: pages from..to at m->dpi (150 if 0), in print layout and light colours whatever the
   view shows (its zoom, layout, mode and pens are swapped and put back). Each page goes through a
   bitmap BAND rows high, with a layer of its own like the back buffer, so a whole page never needs
   to be in memory. NULL when done, else a message for the user (static) */
static const char *print_pages(struct Data *d, Object *obj, struct MUIP_Quill_Print *m)
{
  static char err[80];
  struct MsgPort *mp = NULL;
  struct IODRPReq *io = NULL;
  struct BitMap *bm = NULL;
  struct Layer_Info *li = NULL;
  struct Layer *ly = NULL;
  struct RastPort *rp;
  LONG oz = d->zoom, w, h, k, y0, from = m->from, to = m->to, depth;
  const char *res = NULL;
  int opened = 0;
  Pos none;
  Layout *shown = d->lay;
  int keepmode = d->vmode, keepdark = d->darkpage, keepgrid = d->grid;
  if (!d->lay || !d->doc || !_win(obj)) return "Nothing to print.";
  /* the pages as they are on paper: in light colours, whatever the view shows */
  d->lay = paged_layout(d);
  d->vmode = VM_PRINT; d->grid = 0;
  if (d->darkpage) { d->darkpage = 0; free_pens(d, obj); get_pens(d, obj); }
  if (from < 0) from = 0;
  if (to < from || to >= (LONG)d->lay->npages) to = (LONG)d->lay->npages - 1;
  d->zoom = (m->dpi > 0 ? m->dpi : 150) * 2000L / 1440;
  w = page_w(d); h = page_h(d);
  depth = GetBitMapAttr(_screen(obj)->RastPort.BitMap, BMA_DEPTH);
  if (depth > 8) depth = 8;
  memset(&none, 0, sizeof none);
  if (!(bm = AllocBitMap((ULONG)w, BAND, (ULONG)depth, BMF_CLEAR, NULL)) || !(li = NewLayerInfo()) ||
      !(ly = CreateUpfrontLayer(li, bm, 0, 0, w - 1, BAND - 1, LAYERSIMPLE, NULL))) { res = "Not enough memory to print."; goto done; }
  rp = ly->rp;
  if (!(mp = CreateMsgPort()) || !(io = (struct IODRPReq *)CreateIORequest(mp, sizeof(struct IODRPReq)))) { res = "Not enough memory to print."; goto done; }
  if (OpenDevice((STRPTR)"printer.device", 0, (struct IORequest *)io, 0)) { res = "printer.device couldn't be opened: check Prefs/Printer."; goto done; }
  opened = 1;
  close_fonts(d);
  for (k = from; k <= to && !res; k++) {
    for (y0 = 0; y0 < h && !res; y0 += BAND) {
      LONG bh = h - y0 < BAND ? h - y0 : BAND;
      BYTE e;
      SetDrMd(rp, JAM1);
      SetAPen(rp, d->pen_page);
      RectFill(rp, 0, 0, w - 1, BAND - 1);
      draw_page(d, obj, rp, (u32)k, 0, -y0, bh, 0, none, none);
      io->io_Command = PRD_DUMPRPORT;
      io->io_RastPort = rp;
      io->io_ColorMap = _screen(obj)->ViewPort.ColorMap;
      io->io_Modes = HIRES | LACE;
      io->io_SrcX = 0; io->io_SrcY = 0; io->io_SrcWidth = (UWORD)w; io->io_SrcHeight = (UWORD)bh;
      io->io_DestCols = 0; io->io_DestRows = 0;
      /* the bands of a page fill the paper's width and follow each other; the page's last ends it */
      io->io_Special = SPECIAL_FULLCOLS | SPECIAL_ASPECT | SPECIAL_TRUSTME | (y0 + bh < h ? SPECIAL_NOFORMFEED : 0);
      if ((e = DoIO((struct IORequest *)io)) != 0) { sprintf(err, "The printer reported error %d.", (int)e); res = err; }
    }
  }
done:
  if (opened) CloseDevice((struct IORequest *)io);
  if (io) DeleteIORequest((struct IORequest *)io);
  if (mp) DeleteMsgPort(mp);
  if (ly) DeleteLayer(0, ly);
  if (li) DisposeLayerInfo(li);
  if (bm) { WaitBlit(); FreeBitMap(bm); }
  d->zoom = oz;
  d->lay = shown; d->vmode = keepmode; d->grid = keepgrid;
  if (keepdark) { d->darkpage = 1; free_pens(d, obj); get_pens(d, obj); }
  close_fonts(d);
  MUI_Redraw(obj, MADF_DRAWOBJECT);
  return res;
}

/* ------------------------------------------------------------------ */
/* the class */
/* show another document (the caller owns it): the old layout dropped, the caret at the start;
   shown: laid out and drawn now, else at the first MUIM_Draw */
static void set_doc(struct Data *d, Object *obj, Doc *doc, int shown)
{
  if (d->lay) { lay_free(d->lay); d->lay = NULL; }
  drop_plain(d);
  d->doc = doc;
  d->caret = d->anchor = doc_start(doc);
  d->scrolly = 0; d->pending = 0; d->wantx = -1; d->changed = 0; d->words_stale = 1; d->caret_shown = 0;
  if (shown) { relayout(d, obj); update_prop(d); update_toolbar(d); MUI_Redraw(obj, MADF_DRAWOBJECT); }
}
/* OM_SET, and OM_NEW's tags (init 1: no window yet, so nothing is laid out or drawn) */
static ULONG mSet(struct IClass *cl, Object *obj, struct opSet *msg, int init)
{
  struct Data *d = INST_DATA(cl, obj);
  struct TagItem *tags = msg->ops_AttrList, *t;
  while ((t = NextTagItem(&tags))) {
    switch (t->ti_Tag) {
    case MUIA_Quill_Fonts: d->fs = (FontSet *)t->ti_Data; break;
    case MUIA_Quill_Toolbar: d->tb = (struct Toolbar *)t->ti_Data; break;
    case MUIA_Quill_Doc: set_doc(d, obj, (Doc *)t->ti_Data, !init && _win(obj) && d->bw); break;
    case MUIA_Quill_ViewMode:
      if (t->ti_Data <= VM_OUTLINE && (int)t->ti_Data != shown_mode(d)) {
        ov_switch(d, t->ti_Data == VM_OUTLINE);          /* WP11: Outline view is web layout with the outline on */
        d->vmode = t->ti_Data == VM_OUTLINE ? VM_WEB : (int)t->ti_Data;
        ww_mode(d, obj, init);                           /* WP25: Read Mode reflowed (web layout inside), its page colour */
        d->scrollx = 0;
        drop_plain(d);
        if (!init && d->doc && d->bw) { caret_hide(d, obj); relayout(d, obj); close_fonts(d); make_visible(d, obj); update_prop(d); update_toolbar(d); MUI_Redraw(obj, MADF_DRAWOBJECT); }
      }
      break;
    case MUIA_Quill_Grid:
      d->grid = (int)t->ti_Data;
      if (!init && d->bw) { caret_hide(d, obj); MUI_Redraw(obj, MADF_DRAWOBJECT); }
      break;
    case MUIA_Quill_DarkPage:
      if ((int)t->ti_Data != d->darkpage) {
        d->darkpage = (int)t->ti_Data;
        /* new pens for the page (pictures keep theirs) */
        if (!init && _win(obj)) { free_pens(d, obj); get_pens(d, obj); close_fonts(d); if (d->bw) { caret_hide(d, obj); MUI_Redraw(obj, MADF_DRAWOBJECT); } }
      }
      break;
    case MUIA_Quill_Zoom:
      d->zoomset = (LONG)t->ti_Data;
      ww_zoomed(d);                                      /* WP25: a zoom of its own ends Multiple Pages */
      if (!init && d->doc && d->bw) { caret_hide(d, obj); if (d->lay) { relayout(d, obj); close_fonts(d); make_visible(d, obj); update_toolbar(d); MUI_Redraw(obj, MADF_DRAWOBJECT); } }
      break;
    case MUIA_Quill_Scroll: if (!init && d->bw) { caret_hide(d, obj); scroll_to(d, obj, (LONG)t->ti_Data); } break;
    case MUIA_Quill_ScrollX:
      if (!init && d->bw && d->scrollx != (LONG)t->ti_Data) { caret_hide(d, obj); d->scrollx = (LONG)t->ti_Data; update_toolbar(d); MUI_Redraw(obj, MADF_DRAWOBJECT); }
      break;
    case MUIA_Quill_Changed: d->changed = (int)t->ti_Data; break;
    case MUIA_Quill_Caret:
      if (d->doc && t->ti_Data) {
        d->caret = d->anchor = pos_clamp(d->doc, *(Pos *)t->ti_Data);
        if (!init && d->bw) { caret_hide(d, obj); refresh(d, obj, 0); }
      }
      break;
    }
  }
  return 0;
}
/* the class's dispatcher: MUI calls it for every method sent to a view. BOOPSI passes the class in
   a0, the object in a2 and the message in a1, which vbcc's __reg asks for */
static ULONG dispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct Data *d;
  switch (msg->MethodID) {
  case OM_NEW: {
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->zoom = 100; d->wantx = -1;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;   /* (moves: the link under the pointer) */
    d->ihn.ihn_Object = obj; d->ihn.ihn_Flags = MUIIHNF_TIMER; d->ihn.ihn_Method = MUIM_Quill_Blink;
    d->ihn.ihn_stuff.ihn_timer.ihn_millis = 500;
    d->bgn.ihn_Object = obj; d->bgn.ihn_Flags = MUIIHNF_TIMER; d->bgn.ihn_Method = MUIM_Quill_Background;
    d->bgn.ihn_stuff.ihn_timer.ihn_millis = 20;
    d->obj = obj;
    mSet(cl, obj, (struct opSet *)msg, 1);
    /* the right-button menu's strip, its menu made afresh at each click (ctx_build). It starts with
       a real menu in it: MUI turns the strip into Intuition menus when the window opens, and an
       empty one isn't safe there */
    d->ctxstrip = MenustripObject,
      MUIA_Family_Child, d->ctxmenu = MenuObject, MUIA_Menu_Title, (ULONG)"Edit",
        MUIA_Family_Child, MenuitemObject, MUIA_Menuitem_Title, (ULONG)"Paste", MUIA_UserData, CTX_PASTE, End,
      End,
    End;
    if (d->ctxstrip) set(obj, MUIA_ContextMenu, (ULONG)d->ctxstrip);
    return (ULONG)obj;
  }
  case OM_DISPOSE:
    d = INST_DATA(cl, obj);
    if (d->ctxstrip) { set(obj, MUIA_ContextMenu, 0); MUI_DisposeObject(d->ctxstrip); d->ctxstrip = d->ctxmenu = NULL; }
    if (d->lay) lay_free(d->lay);
    free(d->wm_mask);
    free(d->rowt);
    drop_plain(d);
    fv_free(d);                         /* WP12 */
    ov_free(d);                         /* WP11 */
    ww_free(d);                         /* WP25 */
    ev_free(d);                         /* WP14b */
    break;
  case OM_SET:
    mSet(cl, obj, (struct opSet *)msg, 0);
    break;
  case OM_GET: {
    struct opGet *g = (struct opGet *)msg;
    d = INST_DATA(cl, obj);
    if (g->opg_AttrID == MUIA_Quill_Changed) { *g->opg_Storage = (ULONG)d->changed; return TRUE; }
    if (g->opg_AttrID == MUIA_Quill_CtxPick) { *g->opg_Storage = d->ctx_pick; return TRUE; }
    if (g->opg_AttrID == MUIA_Quill_LinkInfo) { *g->opg_Storage = (ULONG)link_info(d); return TRUE; }
    if (g->opg_AttrID == MUIA_Quill_Zoom) { *g->opg_Storage = (ULONG)d->zoom; return TRUE; }
    if (g->opg_AttrID == MUIA_Quill_Caret) { *g->opg_Storage = (ULONG)&d->caret; return TRUE; }
    if (g->opg_AttrID == MUIA_Quill_Anchor) { *g->opg_Storage = (ULONG)&d->anchor; return TRUE; }
    if (g->opg_AttrID == MUIA_Quill_InTable) { *g->opg_Storage = (ULONG)d->intable; return TRUE; }
    if (g->opg_AttrID == MUIA_Quill_Object) { *g->opg_Storage = (ULONG)(LONG)sel_object(d, NULL); return TRUE; }
    if (g->opg_AttrID == MUIA_Quill_Outline) { *g->opg_Storage = 0; return TRUE; }
    if (g->opg_AttrID == MUIA_Quill_EditStory) { *g->opg_Storage = (ULONG)&d->sreq; return TRUE; }
    if (g->opg_AttrID == MUIA_Quill_Scroll) { *g->opg_Storage = (ULONG)d->scrolly; return TRUE; }
    if (g->opg_AttrID == MUIA_Quill_LayoutNow) { ww_sync(d, obj, 2); *g->opg_Storage = (ULONG)d->lay; return TRUE; }   /* (WP25: up to date) */
    if (g->opg_AttrID == MUIA_Quill_Layout) { ww_sync(d, obj, 2); *g->opg_Storage = (ULONG)paged_layout(d); return TRUE; }
    if (g->opg_AttrID == MUIA_Quill_ViewMode) { *g->opg_Storage = (ULONG)shown_mode(d); return TRUE; }   /* (WP11: VM_OUTLINE too) */
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 120; m->MinMaxInfo->DefWidth += 640; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += 80; m->MinMaxInfo->DefHeight += 400; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    d = INST_DATA(cl, obj);
    if (d->handlers) return TRUE;      /* (set up already: MUI may send Setup twice) */
    get_pens(d, obj);
    theme_setup(obj);
    theme_handler(obj, &d->ehn);
    DoMethod(_app(obj), MUIM_Application_AddInputHandler, (ULONG)&d->ihn);
    d->handlers = 1;
    bg_start(d, obj);
    return TRUE;
  case MUIM_Cleanup:
    d = INST_DATA(cl, obj);
    if (d->handlers) {                 /* (only what Setup did: MUI may send Cleanup without it) */
      bg_stop(d, obj);
      DoMethod(_app(obj), MUIM_Application_RemInputHandler, (ULONG)&d->ihn);
      d->handlers = 0;
      close_fonts(d);
      free_pens(d, obj);
      theme_cleanup(obj);
    }
    back_free(d);
    break;
  case MUIM_Show: ((struct Data *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide:
    d = INST_DATA(cl, obj);
    d->shown = 0;
    back_free(d);
    d->caret_shown = 0;
    break;
  case MUIM_Draw: {
    struct MUIP_Draw *m = (struct MUIP_Draw *)msg;
    struct RastPort *rp;
    LONG w, h;
    DoSuperMethodA(cl, obj, msg);
    if (!(m->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) return 0;
    d = INST_DATA(cl, obj);
    w = _mwidth(obj); h = _mheight(obj);
    ww_sync(d, obj, 0);                          /* WP25: changed in another view */
    if (d->doc && (!d->lay || ((d->zoomset <= 0 || d->vmode == VM_WEB) && d->bw != w))) {
      LONG oz = d->zoom;
      relayout(d, obj);
      if (oz != d->zoom) close_fonts(d);
      update_prop(d);
      update_toolbar(d);
    }
    d->caret_shown = 0;
    update_vruler(d);
    d->band0 = 0; d->band1 = 0x7FFFFFFF;
    d->drawn_sel = has_sel(d);
    d->redraw_later = 0;
    if ((rp = back_rp(d, obj, w, h))) {
      LONG t0 = ticks50();
      draw_all(d, obj, rp, w, h);
      ClipBlit(rp, 0, 0, _rp(obj), _mleft(obj), _mtop(obj), w, h, 0xC0);
      d->back_ok = 1;
      time_note(1, ticks50() - t0);
    } else {
      d->back_ok = 0;
      /* no memory for the buffer: straight into the window, clipped to the view */
      APTR clip = (APTR)MUI_AddClipping(muiRenderInfo(obj), _mleft(obj), _mtop(obj), w, h);
      draw_all(d, obj, _rp(obj), w, h);
      MUI_RemoveClipping(muiRenderInfo(obj), (struct Region *)clip);
    }
    d->caret_on = 1;
    caret_show(d, obj);
    return 0;
  }
  case MUIM_ContextMenuBuild: {
    struct MUIP_ContextMenuBuild *m = (struct MUIP_ContextMenuBuild *)msg;
    return (ULONG)ctx_build(INST_DATA(cl, obj), obj, m->mx, m->my);
  }
  case MUIM_ContextMenuChoice:
    ctx_choice(INST_DATA(cl, obj), obj, ((struct MUIP_ContextMenuChoice *)msg)->item);
    return 0;
  case MUIM_GoActive: case MUIM_GoInactive:
    return 0;                                  /* no focus frame round the page: keys come through the event handler */
  case MUIM_HandleEvent:
    return handle_event(cl, obj, (struct MUIP_HandleEvent *)msg);
  case MUIM_Quill_Blink:
    d = INST_DATA(cl, obj);
    ww_sync(d, obj, 1);                          /* WP25: changed in another view */
    if (d->idle < 100) d->idle++;
    if (d->stale && d->bw) { caret_hide(d, obj); refresh(d, obj, 1); d->stale = 0; }   /* a backstop for deferred typing */
    if (d->words_stale && d->idle == 2 && d->bw) update_toolbar(d);
    if (d->outline_stale && d->idle == 3) { d->outline_stale = 0; set(obj, MUIA_Quill_Outline, TRUE); }
#ifndef QUILL_NOLATER
    if (d->redraw_later && d->idle == 2 && d->bw && !d->dragging) MUI_Redraw(obj, MADF_DRAWOBJECT);
#endif
    if (d->bw && !d->dragging) {
      if (d->caret_shown) caret_hide(d, obj); else caret_show(d, obj);
    }
    return 0;
  case MUIM_Quill_Command:
    return do_command(cl, obj, (struct MUIP_Quill_Command *)msg);
  case MUIM_Quill_Background:
    background(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Quill_Print:
    return (ULONG)print_pages(INST_DATA(cl, obj), obj, (struct MUIP_Quill_Print *)msg);
  case MUIM_Quill_Relayout:
    d = INST_DATA(cl, obj);
    caret_hide(d, obj);
    refresh(d, obj, 1);
    return 0;
  }
  return DoSuperMethodA(cl, obj, msg);
}
/* the class (a subclass of Area), made the first time it is asked for; view_class_free at exit */
struct MUI_CustomClass *view_class(void)
{
  if (!cls) cls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct Data), (APTR)dispatch);
  return cls;
}
void view_class_free(void)
{
  if (cls) { MUI_DeleteCustomClass(cls); cls = NULL; }
}
