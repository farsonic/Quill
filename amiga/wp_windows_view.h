/*
 * wp_windows_view.h - WP25's part of the page view, included by view.c (after wp_outline_view.h) so it
 * can use the view's own data and helpers: a document shown in more than one view (New Window, Split)
 * laid out again when another view changed it; the working pane of a split window; Side to Side page
 * movement and Multiple Pages (a number of pages fitted in the view); Read Mode's options (column width:
 * the text reflowed into a column of that width, page colour, text size, line spacing). view.c calls in
 * from a few places: ww_sync (before drawing, events, commands and timer ticks), ww_laid (relayout),
 * ww_prepare (relayout), ww_cols (cols), ww_fit (fit_zoom), ww_left (page_left), ww_pens (get_pens),
 * ww_key (key), ww_click and ww_passive (handle_event, update_toolbar), ww_mode (the view mode set),
 * ww_zoomed (the zoom set), ww_prop (update_prop), ww_command (do_command) and ww_free (OM_DISPOSE).
 * This file belongs to WP25.
 */
#include "wp_windows.h"

static void relayout(struct Data *d, Object *obj);
static void update_toolbar(struct Data *d);
static void update_prop(struct Data *d);
static void scroll_to(struct Data *d, Object *obj, LONG y);
static void close_fonts(struct Data *d);
static void free_pens(struct Data *d, Object *obj);
static void get_pens(struct Data *d, Object *obj);
static void drop_plain(struct Data *d);
static void make_visible(struct Data *d, Object *obj);

typedef struct {
  u32 seenver; int seenok;      /* the document's version when last laid out here */
  int side;                     /* Side to Side: the pages in one row */
  int many, rows;               /* Multiple Pages: columns and rows fitted in the view (0: off) */
  struct WwRead rd;             /* Read Mode's options */
  int reading;                  /* in Read Mode (reflowed: the view mode inside is web layout) */
  int darkset, darkwas;         /* the dark page put on for an inverse page colour, and what it was */
  int passive;                  /* a split window's other pane is the working one */
  Object *twin;                 /* the other view of a split window */
  Object *syncwith;             /* Synchronous Scrolling: the view scrolled along with this one */
  LONG synced;                  /* this one's scroll position when last passed on */
} WwState;
int wp25_paper;                 /* (wp_windows.h) */
static const ULONG paper_rgb[4] = { 0xFFFFFF, 0xCCE8CF, 0xFAF0DC, 0xE8EAED };
static Object *ww_clicked;      /* the view last clicked into of a split window (for main.c) */

static WwState *ww(struct Data *d)
{
  if (!d->wp25 && (d->wp25 = calloc(1, sizeof(WwState)))) ((WwState *)d->wp25)->rd.col = 1;
  return (WwState *)d->wp25;
}
static void ww_free(struct Data *d)
{
  if (ww_clicked == d->obj) ww_clicked = NULL;
  free(d->wp25);
  d->wp25 = NULL;
}
static int ww_passive(struct Data *d) { return d->wp25 && ((WwState *)d->wp25)->passive; }
/* reflowed Read Mode: the text in a column (web layout inside) */
static int ww_reflow(struct Data *d) { WwState *s = (WwState *)d->wp25; return s && s->reading && s->rd.col != 1; }
static int ww_reading(struct Data *d) { WwState *s = (WwState *)d->wp25; return s && s->reading; }
/* (relayout) the version laid out */
static void ww_laid(struct Data *d)
{
  WwState *s = ww(d);
  if (s && d->doc) { s->seenver = d->doc->ver; s->seenok = 1; }
}
/* another view (New Window, Split) changed the document: laid out again here, the caret kept in it,
   the window marked changed; drawn again (redraw 1), when the user pauses (2: in the middle of
   another object's drawing), or not (0: a drawing is about to follow) */
static void ww_sync(struct Data *d, Object *obj, int redraw)
{
  WwState *s = (WwState *)d->wp25;
  if (s && s->syncwith && d->scrolly != s->synced) {
    /* Synchronous Scrolling: the other view moves as far as this one did */
    LONG by = d->scrolly - s->synced;
    s->synced = d->scrolly;
    DoMethod(s->syncwith, MUIM_Quill_Command, CMD_WW_SYNCBY, (ULONG)by);
  }
  if (!s || !s->seenok || !d->doc || !d->lay || d->stale || d->doc->ver == s->seenver) return;
  relayout(d, obj);
  d->caret = pos_clamp(d->doc, d->caret);
  d->anchor = pos_clamp(d->doc, d->anchor);
  d->words_stale = 1; d->outline_stale = 1;
  if (d->doc->dirty && !d->changed) { d->changed = 1; set(obj, MUIA_Quill_Changed, TRUE); }
  if (redraw == 2) { d->redraw_later = 1; d->idle = 0; }
  else if (redraw && d->bw && _win(obj)) { update_prop(d); if (!ww_passive(d)) update_toolbar(d); MUI_Redraw(obj, MADF_DRAWOBJECT); }
}
/* (cols) pages in a row: all of them Side to Side, the columns of Multiple Pages; 0 as usual */
static int ww_cols(struct Data *d)
{
  WwState *s = (WwState *)d->wp25;
  if (!s || !paged(d) || d->vmode == VM_READ) return 0;
  if (s->many > 0) return s->many;
  if (s->side && d->lay) return d->lay->npages ? (int)d->lay->npages : 1;
  return 0;
}
/* (fit_zoom, after it) Multiple Pages: rows x columns as big as fit; Side to Side at a fitting zoom: a
   page's height; reflowed Read Mode: its text size */
static void ww_fit(struct Data *d, LONG w)
{
  WwState *s = (WwState *)d->wp25;
  LONG mh = d->obj ? _mheight(d->obj) : 0;
  if (!s) return;
  if (ww_reflow(d)) {
    static const LONG sizes[4] = { 85, 100, 125, 150 };
    d->zoom = sizes[(s->rd.size + 1) & 3];
    return;
  }
  if (!paged(d) || d->vmode == VM_READ || mh <= 4 * GAP) return;
  if (s->many > 0) {
    LONG zw = (w - (s->many + 1) * GAP) * 2000L / (s->many * (d->cellw ? d->cellw : 11906));
    LONG zh = (mh - (s->rows + 1) * GAP) * 2000L / ((s->rows > 0 ? s->rows : 1) * (d->cellh ? d->cellh : 16838));
    d->zoom = zw < zh ? zw : zh;
  } else if (s->side && d->zoomset <= 0) d->zoom = (mh - 2 * GAP) * 2000L / (d->cellh ? d->cellh : 16838);
  else return;
  if (d->zoom < 10) d->zoom = 10;
  if (d->zoom > 400) d->zoom = 400;
}
/* (page_left) reflowed Read Mode: the column in the middle of the view; -1 as usual */
static LONG ww_left(struct Data *d, LONG w, LONG rw)
{
  if (!ww_reflow(d) || w <= rw) return -1;
  return (w - rw) / 2;
}
/* (relayout, after the width is set) reflowed Read Mode lays the text out in its column, with its line
   spacing; other views without (paragraphs laid out afresh when that changes) */
static void ww_prepare(struct Data *d)
{
  WwState *s = (WwState *)d->wp25;
  Layout *L = d->lay;
  u16 pct;
  u32 i;
  if (!L) return;
  if (ww_reflow(d)) L->webw = s->rd.col == 0 ? 5040 : 9360;      /* 3.5 and 6.5 inches */
  pct = (u16)(s && s->reading && s->rd.space ? 150 : 0);
  if (pct != L->linepct) { L->linepct = pct; for (i = 0; i < L->npl; i++) L->pl[i].laid = 0; }
}
/* (get_pens, after it) Read Mode's sepia page */
static void ww_pens(struct Data *d, Object *obj)
{
  WwState *s = (WwState *)d->wp25;
  if (wp25_paper > 0 && wp25_paper < 4 && !d->darkpage && !(d->doc && d->doc->pagecolor)) {
    /* the screen paper colour (the document's own page colour comes first) */
    ULONG c = paper_rgb[wp25_paper];
    d->pen_page = best(obj, (c >> 16) & 255, (c >> 8) & 255, c & 255);
  }
  if (!s || !s->reading || s->rd.color != 1) return;
  d->pen_page = best(obj, 0xF4, 0xEC, 0xD8);
  d->pen_text = best(obj, 0x5B, 0x46, 0x36);
  d->pen_desk = best(obj, 0xD8, 0xCC, 0xB4);
}
/* the pens again (the page colour changed) */
static void ww_repen(struct Data *d, Object *obj)
{
  if (!_win(obj)) return;
  free_pens(d, obj); get_pens(d, obj); close_fonts(d);
}
/* the inverse page: the dark page put on while reading (and back as it was after) */
static void ww_dark(struct Data *d, Object *obj)
{
  WwState *s = (WwState *)d->wp25;
  int want = s && s->reading && s->rd.color == 2;
  if (!s) return;
  if (want && !s->darkset) { s->darkwas = d->darkpage; d->darkpage = 1; s->darkset = 1; }
  else if (!want && s->darkset) { d->darkpage = s->darkwas; s->darkset = 0; }
}
/* (the view mode set: d->vmode just changed) Read Mode reflowed is web layout inside; its colours */
static void ww_mode(struct Data *d, Object *obj, int init)
{
  WwState *s = d->vmode == VM_READ ? ww(d) : (WwState *)d->wp25;
  if (!s) return;
  s->reading = d->vmode == VM_READ;
  if (s->reading && s->rd.col != 1) d->vmode = VM_WEB;
  ww_dark(d, obj);
  if (!init) ww_repen(d, obj);
}
/* (the zoom set) a zoom of its own ends Multiple Pages */
static void ww_zoomed(struct Data *d)
{
  WwState *s = (WwState *)d->wp25;
  if (s && d->zoomset != -3) s->many = 0;
}
/* (update_prop) a split window's other pane keeps its own scroll bar's size up to date */
static void ww_prop(struct Data *d)
{
  WwState *s = (WwState *)d->wp25;
  if (s && s->twin && d->tb && d->tb->prop && d->obj && _win(d->obj) && _mheight(d->obj) > 0)
    SetAttrs(d->tb->prop, MUIA_NoNotify, TRUE, MUIA_Prop_Visible, _mheight(d->obj), TAG_DONE);
}
/* (key, first) reflowed Read Mode's keys: no editing; the cursor keys and the wheel turn the screens,
   Esc goes back to Print Layout. Side to Side: the wheel goes sideways. 1 if used */
static int ww_key(struct Data *d, Object *obj, struct IntuiMessage *im)
{
  WwState *s = (WwState *)d->wp25;
  UWORD code = im->Code;
  if (!s) return 0;
  if (ww_reflow(d)) {
    LONG vh = _mheight(obj);
    switch (code) {
    case 0x45: set(obj, MUIA_Quill_ViewMode, VM_PRINT); return 1;
    case 0x4C: case 0x4F: case 0x48: case 0x41: scroll_to(d, obj, d->scrolly - vh + 16); return 1;
    case 0x4D: case 0x4E: case 0x49: case 0x40: scroll_to(d, obj, d->scrolly + vh - 16); return 1;
    case 0x70: scroll_to(d, obj, 0); return 1;
    case 0x71: scroll_to(d, obj, d->doch); return 1;
    case 0x7A: case 0x7B: scroll_to(d, obj, d->scrolly + (code == 0x7A ? -60 : 60)); return 1;
    }
    return 1;
  }
  if (s->side && paged(d) && (code == 0x7A || code == 0x7B)) {
    caret_hide(d, obj);
    d->scrollx += code == 0x7A ? -80 : 80;
    page_left(d, _mwidth(obj));
    update_prop(d);
    MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 1;
  }
  return 0;
}
/* (handle_event) a click in a split window's pane that isn't the working one makes it that */
static void ww_click(struct Data *d, Object *obj, struct IntuiMessage *im)
{
  WwState *s = (WwState *)d->wp25;
  if (!s || !s->twin || !s->passive || im->Class != IDCMP_MOUSEBUTTONS || im->Code != SELECTDOWN) return;
  if (im->MouseX < _mleft(obj) || im->MouseX > _mright(obj) || im->MouseY < _mtop(obj) || im->MouseY > _mbottom(obj)) return;
  s->passive = 0;
  DoMethod(s->twin, MUIM_Quill_Command, CMD_WW_PASSIVE, 1);
  ww_clicked = obj;
  DoMethod(_app(obj), MUIM_Application_ReturnID, ID_WW_FOCUS);
}
/* the view's WP25 commands (CMD_WW_); 1 if it was one */
static int ww_command(struct Data *d, Object *obj, struct MUIP_Quill_Command *m)
{
  WwState *s;
  if (!IS_WW_CMD(m->cmd)) return 0;
  if (!(s = ww(d))) return 1;
  switch (m->cmd) {
  case CMD_WW_INFO: {
    struct WwInfo *wi = (struct WwInfo *)m->arg;
    wi->side = s->side; wi->many = s->many; wi->rows = s->rows; wi->rd = s->rd; wi->passive = s->passive;
    return 1; }
  case CMD_WW_TWIN: s->twin = (Object *)m->arg; if (!s->twin) s->passive = 0; return 1;
  case CMD_WW_PASSIVE: if (m->arg && _win(obj)) caret_hide(d, obj); s->passive = (int)m->arg; return 1;
  case CMD_WW_PAPER: ww_repen(d, obj); if (_win(obj) && d->bw) { caret_hide(d, obj); MUI_Redraw(obj, MADF_DRAWOBJECT); } return 1;
  case CMD_WW_SYNC: s->syncwith = (Object *)m->arg; s->synced = d->scrolly; return 1;
  case CMD_WW_SYNCBY:
    if (_win(obj) && d->bw) { caret_hide(d, obj); scroll_to(d, obj, d->scrolly + (LONG)m->arg); update_prop(d); }
    s->synced = d->scrolly;
    return 1;
  case CMD_WW_SIDE: s->side = (int)m->arg; s->many = 0; break;
  case CMD_WW_MANY:
    s->many = (int)(m->arg & 255); s->rows = (int)((m->arg >> 8) & 255);
    if (s->rows < 1) s->rows = 1;
    if (s->many) { s->side = 0; d->zoomset = -3; if (d->vmode != VM_PRINT) { d->vmode = VM_PRINT; s->reading = 0; } }
    else if (d->zoomset == -3) d->zoomset = 0;
    break;
  case CMD_WW_READ: {
    int was = s->reading;
    s->rd = *(const struct WwRead *)m->arg;
    if (s->rd.col < 0 || s->rd.col > 2) s->rd.col = 1;
    if (was) {
      d->vmode = s->rd.col != 1 ? VM_WEB : VM_READ;
      ww_dark(d, obj);
      ww_repen(d, obj);
      if (d->lay) { lay_free(d->lay); d->lay = NULL; }      /* (laid out afresh: width and line spacing) */
      drop_plain(d);
      d->scrollx = 0;
    }
    break; }
  }
  if (_win(obj) && d->bw) {
    caret_hide(d, obj);
    relayout(d, obj);
    close_fonts(d);
    make_visible(d, obj);
    update_prop(d);
    update_toolbar(d);
    MUI_Redraw(obj, MADF_DRAWOBJECT);
  }
  return 1;
}
/* (ctx_build) Read Mode's right-button menu: back to Print Layout, and its options (wp_windows.c's
   ctxmenu hook) */
static void ctx_item(Object *menu, const char *title, ULONG id, int enabled);
static Object *ww_readmenu(struct Data *d)
{
  WpCtx wc;
  if (!(d->ctxmenu = MenuObject, MUIA_Menu_Title, (ULONG)tr("Read Mode"), End)) return NULL;
  ctx_item(d->ctxmenu, tr("Print Layout"), WP_CTX_ID(ID_VPRINT), 1);
  wp_ctx(d, &wc, WPC_TEXT);
  wp_ctxmenu(d->ctxmenu, &wc);
  DoMethod(d->ctxstrip, MUIM_Family_AddTail, (ULONG)d->ctxmenu);
  return d->ctxstrip;
}
/* (wphooks.h's packages) the state of the view the last WpCtx was made for */
void wp25_view_info(struct WwInfo *wi)
{
  WwState *s = ctx_view ? (WwState *)ctx_view->wp25 : NULL;
  memset(wi, 0, sizeof *wi);
  wi->rd.col = 1;
  if (s) { wi->side = s->side; wi->many = s->many; wi->rows = s->rows; wi->rd = s->rd; wi->passive = s->passive; }
}
Object *wp25_clicked_view(void) { return ww_clicked; }
