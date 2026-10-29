/*
 * panes.c - the navigation pane and the status bar, as WordCraft has them, drawn by Quill (MUI
 * custom classes) in the theme's colours.
 *
 * The navigation pane: a title with a close button, a search box, and three tabs: Headings (the
 * document's headings, indented by level, the one the caret is under marked), Pages (a small
 * picture of each page; a click goes there) and Results (each match of the search with the text
 * around it). It keeps copies of what it shows, never pointers into the document, so it can be
 * drawn at any time.
 *
 * The status bar: the page, the word count and the language on the left; Focus, the view button
 * and the zoom slider on the right.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/keymap.h>
#include <clib/alib_protos.h>
#include "quill.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "wp_proof.h"   /* (WP31: the language on the bar) */
#include "wp_outline.h"               /* (WP11) CMD_OV_MOVEPART: dragging headings */

/* the interface font's height in pixels: rows and boxes are sized from it */
static int fh(void) { return theme_font()->tf_YSize; }
/* s (Latin-1) at x, y (y the top of the text, not the baseline) in plain tf and pen, cut short
   to fit maxw pixels; returns the width drawn */
static LONG clipped_text(struct RastPort *rp, struct TextFont *tf, LONG x, LONG y, const char *s, LONG maxw, LONG pen)
{
  struct TextExtent te;
  ULONG n;
  SetFont(rp, tf);
  SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
  n = TextFit(rp, (STRPTR)s, (ULONG)strlen(s), &te, NULL, 1, (ULONG)(maxw > 0 ? maxw : 0), 100);
  SetAPen(rp, pen);
  SetDrMd(rp, JAM1);
  Move(rp, x, y + tf->tf_Baseline);
  Text(rp, (STRPTR)s, n);
  return te.te_Width;
}

/* ================================================================== */
/* the navigation pane */
#define MAXHEAD 400
#define MAXRES 200
typedef struct { u32 para; u8 level; char text[60]; } Head;
typedef struct { Pos a, b; char before[40], match[40], after[60]; } Result;
enum { NT_HEADINGS, NT_PAGES, NT_RESULTS };
typedef struct { WORD x0, y0, x1, y1; } NBox;
struct NData {
  int shown;
  Object *view;
  Head *heads; int nheads; u32 headsum;
  Result *res; int nres; int searched;
  int tab;
  char search[64]; int searchlen, active;          /* the search box: its text, and whether typing goes there */
  u32 caretpara, page, npages;
  int first[3];                  /* the first row (or page) shown on each tab */
  int hot;                       /* row under the pointer, or -1 */
  NBox close, box, tabs[3], body;
  struct MUI_EventHandlerNode ehn;
  int drag, dragrow, dropat;     /* (WP11) a heading being dragged (its row in heads), and where it would go (-1 nowhere) */
};
static struct MUI_CustomClass *ncls, *scls;
/* (quill.h) an object typing goes to instead of the page, or NULL: the search box here, or a
   field on the File page */
Object *key_grab;

/* a paragraph's level in the pane (WP11: its outline level, 1-9: the Heading styles' and any
   paragraph given one), 0 for Title, -1 for anything else (not shown in the pane) */
static int heading_level(const Doc *doc, const Para *p)
{
  if (p->pf.outlvl >= 1 && p->pf.outlvl <= 9) return p->pf.outlvl;
  if (str_ieq(doc->styles[p->pf.style < doc->nstyles ? p->pf.style : 0].name, "Title")) return 0;
  return -1;
}
/* the bytes from..to of p's UTF-8 text as Latin-1 in out (cap bytes, 0-terminated), the range
   clamped to the paragraph; control characters (tabs and the like) become spaces */
static void para_latin1(Para *p, u32 from, u32 to, char *out, size_t cap)
{
  char *c;
  if (to > p->len) to = p->len;
  if (from > to) from = to;
  utf8_to_latin1(p->t + from, to - from, out, cap);
  for (c = out; *c; c++) if ((u8)*c < 32) *c = ' ';
}
/* the headings, if they changed (returns whether they did) */
static int nav_headings(struct NData *d, Doc *doc)
{
  u32 i, sum = 0;
  int n = 0;
  for (i = 0; i < doc->np; i++) {
    Para *p = doc->p[i];
    int lv = heading_level(doc, p);
    u32 k;
    if (lv < 0) continue;
    sum = sum * 31 + i;
    for (k = 0; k < p->len && k < 60; k++) sum = sum * 31 + (u8)p->t[k];
    n++;
  }
  if (d->heads && sum == d->headsum && n == d->nheads) return 0;
  d->headsum = sum;
  if (!d->heads && !(d->heads = (Head *)malloc(MAXHEAD * sizeof(Head)))) return 0;
  d->nheads = 0;
  for (i = 0; i < doc->np && d->nheads < MAXHEAD; i++) {
    Para *p = doc->p[i];
    int lv = heading_level(doc, p);
    Head *h;
    if (lv < 0) continue;
    h = &d->heads[d->nheads++];
    h->para = i; h->level = (u8)lv;
    para_latin1(p, 0, p->len, h->text, sizeof h->text);
  }
  return 1;
}
/* the search box's Latin-1 text as UTF-8 for doc_find (cut short to fit cap) */
static void latin1_utf8(const char *s, char *u, size_t cap)
{
  size_t o = 0;
  for (; *s && o + 4 < cap; s++) o += (size_t)utf8_put(u + o, (u8)*s);
  u[o] = 0;
}
/* the search's matches, from the start of the document: up to MAXRES of them, each with some
   text either side copied for the Results tab. doc_find wraps at the end, so a match at or before
   the first means we have been all the way round */
static void nav_search(struct NData *d, Doc *doc)
{
  char u[256];
  Pos at, a, b;
  d->nres = 0;
  d->first[NT_RESULTS] = 0;
  d->searched = d->searchlen > 0;
  if (!d->searchlen || !doc) return;
  if (!d->res && !(d->res = (Result *)malloc(MAXRES * sizeof(Result)))) return;
  latin1_utf8(d->search, u, sizeof u);
  at.para = 0; at.off = 0;
  while (d->nres < MAXRES && doc_find(doc, at, u, 0, &a, &b)) {
    Result *r = &d->res[d->nres];
    Para *p = doc->p[a.para];
    u32 s0 = a.off > 30 ? a.off - 30 : 0;
    if (d->nres && pos_cmp(a, d->res[0].a) <= 0) break;          /* wrapped round */
    while (s0 > 0 && s0 < a.off && ((u8)p->t[s0] & 0xC0) == 0x80) s0++;
    r->a = a; r->b = b;
    para_latin1(p, s0, a.off, r->before, sizeof r->before);
    para_latin1(p, a.off, b.para == a.para ? b.off : p->len, r->match, sizeof r->match);
    para_latin1(p, b.para == a.para ? b.off : p->len, (b.off + 50 < p->len ? b.off + 50 : p->len), r->after, sizeof r->after);
    d->nres++;
    at = b;
    if (pos_cmp(b, a) <= 0) break;
  }
}

/* the boxes: the row height of the Headings tab */
static LONG nrowh(void) { return fh() + 7; }
/* where everything goes (close box, search box, tabs, body), in window pixels from the object's
   inner box; done before each draw and when the pane needs to know how many rows fit */
static void nav_layout(struct NData *d, Object *obj, struct RastPort *rp)
{
  LONG l = _mleft(obj), t = _mtop(obj), r = _mright(obj), x = l + 8, k;
  static const char *const names[3] = { "Headings", "Pages", "Results" };
  d->close.x0 = (WORD)(r - 18); d->close.x1 = (WORD)(r - 4); d->close.y0 = (WORD)(t + 6); d->close.y1 = (WORD)(t + 20);
  d->box.x0 = (WORD)(l + 8); d->box.x1 = (WORD)(r - 8); d->box.y0 = (WORD)(t + 28); d->box.y1 = (WORD)(t + 28 + fh() + 8);
  {
    LONG all = 0, gap;                            /* the gaps narrower when the words are long */
    for (k = 0; k < 3; k++) all += theme_textlen(rp, theme_font(), names[k]);
    gap = (r - 8 - x - all) / 2;
    if (gap > 12) gap = 12;
    if (gap < 4) gap = 4;
    for (k = 0; k < 3; k++) {
      LONG w = theme_textlen(rp, theme_font(), names[k]);
      d->tabs[k].x0 = (WORD)x; d->tabs[k].x1 = (WORD)(x + w - 1); d->tabs[k].y0 = (WORD)(d->box.y1 + 8); d->tabs[k].y1 = (WORD)(d->box.y1 + 8 + fh() + 6);
      x += w + gap;
    }
  }
  d->body.x0 = (WORD)l; d->body.x1 = (WORD)r; d->body.y0 = (WORD)(d->tabs[0].y1 + 6); d->body.y1 = (WORD)_mbottom(obj);
}
/* how many heading rows the body has room for (at least 1) */
static int rows_fit(struct NData *d) { int n = (d->body.y1 - d->body.y0 - 4) / nrowh(); return n < 1 ? 1 : n; }
/* the heading the caret is under (the last one at or before its paragraph), or -1 */
static int current_heading(struct NData *d)
{
  int i, cur = -1;
  for (i = 0; i < d->nheads; i++) if (d->heads[i].para <= d->caretpara) cur = i;
  return cur;
}
/* the thumbnail size: as wide as the pane allows, the page's shape */
static void thumb_size(struct NData *d, Doc *doc, LONG *tw, LONG *th)
{
  LONG w = d->body.x1 - d->body.x0 + 1 - 40;
  if (w > 90) w = 90;
  if (w < 20) w = 20;
  *tw = w;
  *th = doc && doc->page.width ? w * doc->page.height / doc->page.width : w * 1414 / 1000;
}
/* a thin bar at the body's right edge showing which part of a list is in view (nothing when
   it all fits); a click there pages up or down, it isn't dragged */
static void draw_scrollbar(struct NData *d, struct RastPort *rp, int first, int shown, int total)
{
  LONG x = d->body.x1 - 4, y0 = d->body.y0 + 2, y1 = d->body.y1 - 3, len = y1 - y0 + 1, a, b;
  if (total <= shown || len < 10) return;
  a = y0 + len * first / total; b = y0 + len * (first + shown) / total - 1;
  if (b < a + 6) b = a + 6;
  if (b > y1) b = y1;
  theme_fill(rp, x, a, x + 2, b, tpen(T_BORDER2));
}
/* draws the whole pane: title, close box, search box, tabs and the shown tab's list. The
   Pages tab draws each page in miniature from the view's layout, a grey line per line of text,
   scaled by sx (thousandths of a pixel per twip) */
static void nav_draw(struct NData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG l = _mleft(obj), t = _mtop(obj), r = _mright(obj), b = _mbottom(obj), k;
  static const char *const names[3] = { "Headings", "Pages", "Results" };
  nav_layout(d, obj, rp);
  theme_fill(rp, l, t, r, b, tpen(T_PANEL));
  SetAPen(rp, tpen(T_BORDER)); Move(rp, r, t); Draw(rp, r, b);
  theme_text(rp, theme_bold(), l + 8, t + 8, "Navigation", tpen(T_TEXT));
  SetAPen(rp, tpen(d->hot == -2 ? T_TEXT : T_DIM));
  Move(rp, d->close.x0 + 3, d->close.y0 + 3); Draw(rp, d->close.x1 - 3, d->close.y1 - 3);
  Move(rp, d->close.x1 - 3, d->close.y0 + 3); Draw(rp, d->close.x0 + 3, d->close.y1 - 3);
  /* the search box */
  theme_fill(rp, d->box.x0, d->box.y0, d->box.x1, d->box.y1, tpen(T_WHITE));
  theme_rect(rp, d->box.x0, d->box.y0, d->box.x1, d->box.y1, tpen(d->active ? T_ACCENT : T_INPUTBORDER));
  if (d->active) { SetAPen(rp, tpen(T_ACCENT)); Move(rp, d->box.x0, d->box.y1 - 1); Draw(rp, d->box.x1, d->box.y1 - 1); }
  {
    LONG tx = d->box.x0 + 5, ty = d->box.y0 + 4, maxw = d->box.x1 - d->box.x0 - 26, w;
    const char *s = d->search;
    /* the end of a long search stays in view */
    while (*s && theme_textlen(rp, theme_font(), s) > maxw) s++;
    if (d->searchlen) w = clipped_text(rp, theme_font(), tx, ty, s, maxw, tpen(T_TEXT));
    else { w = 0; if (!d->active) clipped_text(rp, theme_font(), tx, ty, tr("Search document"), maxw, tpen(T_DIM)); }
    if (d->active) theme_fill(rp, tx + w + 1, ty - 1, tx + w + 1, ty + fh(), tpen(T_TEXT));
    theme_icon(rp, IC_SEARCH, d->box.x1 - 17, d->box.y0 + (d->box.y1 - d->box.y0 - 13) / 2 + 1, 13, 0);
  }
  /* the tabs */
  for (k = 0; k < 3; k++) {
    NBox *tb = &d->tabs[k];
    theme_text(rp, theme_font(), tb->x0, tb->y0 + 2, names[k], tpen(k == d->tab ? T_ACCENT : T_TEXT));
    if (k == d->tab) theme_fill(rp, tb->x0, tb->y1 - 1, tb->x1, tb->y1, tpen(T_ACCENT));
  }
  SetAPen(rp, tpen(T_BORDER)); Move(rp, l, d->body.y0 - 3); Draw(rp, r - 1, d->body.y0 - 3);
  /* the body */
  if (d->tab == NT_HEADINGS) {
    int i, n = rows_fit(d), cur = current_heading(d);
    if (!d->nheads) clipped_text(rp, theme_font(), l + 8, d->body.y0 + 4, tr("No headings"), r - l - 16, tpen(T_DIM));
    for (i = 0; i < n && d->first[0] + i < d->nheads; i++) {
      Head *h = &d->heads[d->first[0] + i];
      LONG y = d->body.y0 + 2 + i * nrowh(), ind = 8 + (h->level > 1 ? (h->level - 1) * 10 : 0);
      if (d->first[0] + i == cur) {
        theme_fill(rp, l + 1, y, r - 8, y + nrowh() - 2, tpen(T_CHECKED));
        theme_fill(rp, l + 1, y, l + 2, y + nrowh() - 2, tpen(T_ACCENT));
      } else if (d->hot == i) theme_fill(rp, l + 1, y, r - 8, y + nrowh() - 2, tpen(T_NAVHOVER));
      clipped_text(rp, h->level <= 1 ? theme_bold() : theme_font(), l + ind, y + 3, h->text, r - 10 - l - ind, tpen(T_TEXT));
    }
    if (d->drag && d->dropat >= d->first[0] && d->dropat <= d->first[0] + n) {     /* (WP11) where a dragged heading would go */
      LONG y = d->body.y0 + 1 + (d->dropat - d->first[0]) * nrowh();
      theme_fill(rp, l + 4, y - 1, r - 10, y, tpen(T_ACCENT));
    }
    draw_scrollbar(d, rp, d->first[0], n, d->nheads);
  } else if (d->tab == NT_PAGES) {
    Layout *L = NULL;
    Doc *doc;
    LONG tw, th, y;
    u32 pg;
    APTR clip = MUI_AddClipping(muiRenderInfo(obj), d->body.x0, d->body.y0, d->body.x1 - d->body.x0 + 1, d->body.y1 - d->body.y0 + 1);
    if (d->view) get(d->view, MUIA_Quill_LayoutNow, &L);
    doc = L ? L->d : NULL;
    thumb_size(d, doc, &tw, &th);
    y = d->body.y0 + 6;
    for (pg = (u32)d->first[1]; L && pg < L->npages && y < d->body.y1; pg++) {
      LONG x = l + (r - l + 1 - tw) / 2;
      u32 a, e, i;
      char num[12];
      LONG sx = tw * 1000 / (doc->page.width ? doc->page.width : 11906), cx0 = x + (LONG)((doc->page.left * sx) / 1000);
      if (pg == d->page) theme_rect(rp, x - 3, y - 3, x + tw + 2, y + th + 2, tpen(T_ACCENT));
      else if (d->hot == (int)(pg - (u32)d->first[1])) theme_rect(rp, x - 3, y - 3, x + tw + 2, y + th + 2, tpen(T_BORDER2));
      theme_fill(rp, x + 1, y + 1, x + tw, y + th, tpen(T_BORDER2));
      theme_fill(rp, x, y, x + tw - 1, y + th - 1, tpen(T_WHITE));
      /* each line of text as a grey bar */
      lay_page_lines(L, pg, &a, &e);
      SetAPen(rp, tpen(T_BORDER2));
      for (i = a; i < e; i++) {
        Placed *pl = &L->placed[i];
        ParaLay *pp;
        Line *ln;
        LONG ly, lx, lw;
        if (pl->page != pg || pl->para >= L->npl) continue;
        pp = &L->pl[pl->para];
        if (!pp->laid || pl->line >= pp->nlines) continue;
        ln = &pp->lines[pl->line];
        ly = y + (LONG)(((pl->y + ln->height / 2) * sx) / 1000);
        lx = cx0 + (LONG)(((pl->x + ln->left) * sx) / 1000);
        lw = (LONG)((ln->width * sx) / 1000);
        if (lw < 1 || ly >= y + th - 1) continue;
        if (lx + lw > x + tw - 2) lw = x + tw - 2 - lx;
        Move(rp, lx, ly); Draw(rp, lx + lw - 1, ly);
      }
      sprintf(num, "%lu", (unsigned long)pg + 1);
      theme_text(rp, theme_font(), x + (tw - theme_textlen(rp, theme_font(), num)) / 2, y + th + 4, num, tpen(pg == d->page ? T_ACCENT : T_DIM));
      y += th + fh() + 14;
    }
    MUI_RemoveClipping(muiRenderInfo(obj), clip);
    if (L) draw_scrollbar(d, rp, d->first[1], (int)((d->body.y1 - d->body.y0) / (th + fh() + 14)) > 0 ? (int)((d->body.y1 - d->body.y0) / (th + fh() + 14)) : 1, (int)L->npages);
  } else {
    int i, n;
    char msg[48];
    LONG y = d->body.y0 + 4;
    if (!d->searched) clipped_text(rp, theme_font(), l + 8, y, tr("Search for text in the box"), r - l - 16, tpen(T_DIM));
    else {
      sprintf(msg, tr(d->nres >= MAXRES ? "%d+ results" : d->nres == 1 ? "%d result" : "%d results"), d->nres);
      clipped_text(rp, theme_font(), l + 8, y, msg, r - l - 16, tpen(T_DIM));
    }
    y += fh() + 6;
    n = (int)((d->body.y1 - y) / (2 * fh() + 8));
    for (i = 0; i < n && d->first[2] + i < d->nres; i++) {
      Result *rs = &d->res[d->first[2] + i];
      LONG ry = y + i * (2 * fh() + 8), x = l + 8, maxw = r - 12 - l;
      if (d->hot == i) theme_fill(rp, l + 1, ry - 2, r - 8, ry + 2 * fh() + 4, tpen(T_NAVHOVER));
      /* two lines: the end of the text before and the match in bold, then the text after */
      {
        const char *bf = rs->before;
        const char *af = rs->after;
        while (*bf && theme_textlen(rp, theme_font(), bf) > maxw / 2) bf++;
        if (bf != rs->before) { const char *sp = strchr(bf, ' '); if (sp && sp[1]) bf = sp + 1; }   /* from a word's start */
        while (*af == ' ') af++;
        x += clipped_text(rp, theme_font(), x, ry, bf, maxw, tpen(T_DIM));
        clipped_text(rp, theme_bold(), x, ry, rs->match, l + 8 + maxw - x, tpen(T_TEXT));
        clipped_text(rp, theme_font(), l + 8, ry + fh() + 1, af, maxw, tpen(T_DIM));
      }
      SetAPen(rp, tpen(T_BORDER)); Move(rp, l + 8, ry + 2 * fh() + 5); Draw(rp, r - 10, ry + 2 * fh() + 5);
    }
    draw_scrollbar(d, rp, d->first[2], n > 0 ? n : 1, d->nres);
  }
}
/* whether window point x, y is inside b (edges included) */
static int in_nbox(NBox *b, WORD x, WORD y) { return x >= b->x0 && x <= b->x1 && y >= b->y0 && y <= b->y1; }
/* the row (heading, page or result) at y on the shown tab, relative to the first shown, or -1 */
static int nav_row(struct NData *d, Object *obj, WORD x, WORD y)
{
  if (!in_nbox(&d->body, x, y) || x > d->body.x1 - 8) return -1;
  if (d->tab == NT_HEADINGS) {
    int i = (y - d->body.y0 - 2) / nrowh();
    return d->first[0] + i < d->nheads ? i : -1;
  }
  if (d->tab == NT_PAGES) {
    Layout *L = NULL;
    LONG tw, th;
    int i;
    if (d->view) get(d->view, MUIA_Quill_LayoutNow, &L);
    if (!L) return -1;
    thumb_size(d, L->d, &tw, &th);
    i = (int)((y - d->body.y0 - 2) / (th + fh() + 14));
    return d->first[1] + i < (int)L->npages ? i : -1;
  }
  {
    LONG y0 = d->body.y0 + 4 + fh() + 6;
    int i;
    if (y < y0 - 2) return -1;
    i = (int)((y - y0 + 2) / (2 * fh() + 8));
    (void)obj;
    return d->first[2] + i < d->nres ? i : -1;
  }
}
/* the shown tab's list moved by rows (or pages), kept in range, and redrawn */
static void nav_scroll(struct NData *d, Object *obj, int by)
{
  int total = d->tab == NT_HEADINGS ? d->nheads : d->tab == NT_RESULTS ? d->nres : (int)d->npages, *f = &d->first[d->tab];
  *f += by;
  if (*f > total - 1) *f = total - 1;
  if (*f < 0) *f = 0;
  MUI_Redraw(obj, MADF_DRAWOBJECT);
}
/* typing goes to the search box (on) or back to the page; key_grab tells the view's key handler
   to leave keys alone while the box has them */
static void nav_activate(struct NData *d, Object *obj, int on)
{
  if (d->active == on) return;
  d->active = on;
  key_grab = on ? obj : NULL;
  MUI_Redraw(obj, MADF_DRAWOBJECT);
}
/* a click on a row: the view goes to the heading, to the top of the page (the caret put at its
   first line), or selects the search result; the search box lets go of the keys */
static void nav_go(struct NData *d, Object *obj, int row)
{
  if (!d->view || row < 0) return;
  if (d->tab == NT_HEADINGS) DoMethod(d->view, MUIM_Quill_Command, CMD_GOTO, d->heads[d->first[0] + row].para);
  else if (d->tab == NT_PAGES) {
    Layout *L = NULL;
    u32 pg = (u32)(d->first[1] + row), a, e;
    get(d->view, MUIA_Quill_LayoutNow, &L);
    if (!L || pg >= L->npages) return;
    lay_page_lines(L, pg, &a, &e);
    for (; a < e; a++) if (L->placed[a].page == pg) break;
    if (a < e) {
      Pos ab[2];
      ParaLay *pp = &L->pl[L->placed[a].para];
      ab[0].para = ab[1].para = L->placed[a].para;
      ab[0].off = ab[1].off = pp->laid && L->placed[a].line < pp->nlines ? pp->lines[L->placed[a].line].start : 0;
      DoMethod(d->view, MUIM_Quill_Command, CMD_SELECT, (ULONG)ab);
    }
  } else {
    Pos ab[2];
    ab[0] = d->res[d->first[2] + row].a; ab[1] = d->res[d->first[2] + row].b;
    DoMethod(d->view, MUIM_Quill_Command, CMD_SELECT, (ULONG)ab);
  }
  nav_activate(d, obj, 0);
}
/* the document the view shows (through its layout), or NULL with no view or layout yet */
static Doc *view_doc(struct NData *d)
{
  Layout *L = NULL;
  if (d->view) get(d->view, MUIA_Quill_LayoutNow, &L);
  return L ? L->d : NULL;
}
/* the pane's MUIM_HandleEvent: hover, clicks, the mouse wheel and typing in the search box.
   MUI passes every IDCMP event the window gets, not just those over the pane, so it checks
   where the pointer is. Returning MUI_EventHandlerRC_Eat stops other handlers seeing the event;
   0 passes it on. Each key typed searches again and shows Results */
static ULONG nav_events(struct IClass *cl, Object *obj, struct MUIP_HandleEvent *m)
{
  struct NData *d = INST_DATA(cl, obj);
  struct IntuiMessage *im = m->imsg;
  int inside;
  if (!im || !d->shown) return 0;
  inside = im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj) && im->MouseY >= _mtop(obj) && im->MouseY <= _mbottom(obj);
  if (d->drag && im->Class == IDCMP_MOUSEMOVE) {
    /* (WP11) dragging a heading: the gap between rows it would go into */
    int at = d->first[0] + (int)((im->MouseY - d->body.y0 - 2 + nrowh() / 2) / nrowh());
    if (at < d->first[0]) at = d->first[0];
    if (at > d->nheads) at = d->nheads;
    if (!inside || at == d->dragrow || at == d->dragrow + 1) at = -1;
    if (at != d->dropat) { d->dropat = at; MUI_Redraw(obj, MADF_DRAWOBJECT); }
    return MUI_EventHandlerRC_Eat;
  }
  if (d->drag && im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTUP) {
    int at = d->dropat, row = d->dragrow;
    Doc *doc = view_doc(d);
    d->drag = 0; d->dropat = -1;
    if (at >= 0 && doc && d->view && row < d->nheads) {
      u32 ft[2];
      ft[0] = d->heads[row].para;
      ft[1] = at < d->nheads ? d->heads[at].para : doc->np;
      DoMethod(d->view, MUIM_Quill_Command, CMD_OV_MOVEPART, (ULONG)ft);
      DoMethod(_app(obj), MUIM_Application_ReturnID, ID_OUTLINECHG);
    } else nav_go(d, obj, row - d->first[0]);
    MUI_Redraw(obj, MADF_DRAWOBJECT);
    return MUI_EventHandlerRC_Eat;
  }
  if (im->Class == IDCMP_MOUSEMOVE) {
    int h = !inside ? -1 : in_nbox(&d->close, im->MouseX, im->MouseY) ? -2 : nav_row(d, obj, im->MouseX, im->MouseY);
    if (h != d->hot) { d->hot = h; MUI_Redraw(obj, MADF_DRAWOBJECT); }
    return 0;
  }
  if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN) {
    int k;
    if (!inside) { nav_activate(d, obj, 0); return 0; }
    if (in_nbox(&d->close, im->MouseX, im->MouseY)) { DoMethod(_app(obj), MUIM_Application_ReturnID, ID_OUTLINE); return MUI_EventHandlerRC_Eat; }
    if (in_nbox(&d->box, im->MouseX, im->MouseY)) { nav_activate(d, obj, 1); return MUI_EventHandlerRC_Eat; }
    for (k = 0; k < 3; k++)
      if (in_nbox(&d->tabs[k], im->MouseX, im->MouseY)) { d->tab = k; d->hot = -1; MUI_Redraw(obj, MADF_DRAWOBJECT); return MUI_EventHandlerRC_Eat; }
    if (in_nbox(&d->body, im->MouseX, im->MouseY) && im->MouseX > d->body.x1 - 8) {
      /* the scroll bar: a page up or down */
      nav_scroll(d, obj, im->MouseY < (d->body.y0 + d->body.y1) / 2 ? -rows_fit(d) : rows_fit(d));
      return MUI_EventHandlerRC_Eat;
    }
    if (d->tab == NT_HEADINGS && nav_row(d, obj, im->MouseX, im->MouseY) >= 0) {
      /* (WP11) a heading: gone to when let go where it was, else dragged with its part to where it is let go */
      d->drag = 1; d->dragrow = d->first[0] + nav_row(d, obj, im->MouseX, im->MouseY); d->dropat = -1;
      return MUI_EventHandlerRC_Eat;
    }
    nav_go(d, obj, nav_row(d, obj, im->MouseX, im->MouseY));
    return MUI_EventHandlerRC_Eat;
  }
  if (im->Class == IDCMP_RAWKEY) {
    if ((im->Code == 0x7A || im->Code == 0x7B) && inside) { nav_scroll(d, obj, im->Code == 0x7A ? -3 : 3); return MUI_EventHandlerRC_Eat; }   /* the wheel */
    if (!d->active || (im->Code & IECODE_UP_PREFIX)) return 0;
    if (im->Code == 0x45) { nav_activate(d, obj, 0); return MUI_EventHandlerRC_Eat; }                 /* Esc */
    if (im->Code == 0x41) { if (d->searchlen) d->search[--d->searchlen] = 0; }                     /* Backspace */
    else if (im->Code == 0x44 || im->Code == 0x43) { if (d->nres) nav_go(d, obj, 0 - d->first[2]); return MUI_EventHandlerRC_Eat; }   /* Return: the first */
    else {
      struct InputEvent ie;
      char buf[8];
      LONG n;
      memset(&ie, 0, sizeof ie);
      ie.ie_Class = IECLASS_RAWKEY; ie.ie_Code = im->Code; ie.ie_Qualifier = im->Qualifier;
      ie.ie_EventAddress = (APTR *)*((ULONG *)im->IAddress);
      n = MapRawKey(&ie, (STRPTR)buf, sizeof buf, NULL);
      if (n != 1 || (u8)buf[0] < 32 || ((u8)buf[0] >= 127 && (u8)buf[0] < 160)) return MUI_EventHandlerRC_Eat;
      if (d->searchlen < (int)sizeof d->search - 1) { d->search[d->searchlen++] = buf[0]; d->search[d->searchlen] = 0; }
    }
    nav_search(d, view_doc(d));
    d->tab = NT_RESULTS;
    MUI_Redraw(obj, MADF_DRAWOBJECT);
    return MUI_EventHandlerRC_Eat;
  }
  return 0;
}
/* the navigation pane's bubbles: its close box, search box and tabs */
static const char *nav_help(struct NData *d, WORD x, WORD y)
{
  if (in_nbox(&d->close, x, y)) return theme_bubble(tr("Close"), tr("Hides the navigation pane. View > Navigation Pane brings it back."));
  if (in_nbox(&d->box, x, y)) return theme_bubble(tr("Search document"), tr("Type a word and press Return: every place it appears is listed under Results. Click one to go there."));
  if (in_nbox(&d->tabs[NT_HEADINGS], x, y)) return theme_bubble(tr("Headings"), tr("The document's headings as an outline. Click one to jump to it, or drag it to move it with what is under it. Paragraphs with an outline level (the Heading styles) show here."));
  if (in_nbox(&d->tabs[NT_PAGES], x, y)) return theme_bubble(tr("Pages"), tr("A small picture of every page. Click one to go to it."));
  if (in_nbox(&d->tabs[NT_RESULTS], x, y)) return theme_bubble(tr("Results"), tr("The places the search found, each with the words round it."));
  return NULL;
}
/* the navigation pane class's dispatcher: MUI calls it for every method sent to a pane, and
   anything not handled here goes to the superclass (Area). The __reg()s are vbcc's way of taking
   the registers BOOPSI passes class, object and message in. The pane gets its keys and clicks
   through its own event handler (added in MUIM_Setup), not through MUI's gadget input, and makes
   its own bubbles for MUIM_CreateShortHelp */
static ULONG ndispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct NData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->hot = -1;
    d->view = (Object *)GetTagData(MUIA_Nav_View, 0, ((struct opSet *)msg)->ops_AttrList);
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Priority = 1;
    d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY;
    return (ULONG)obj;
  case OM_DISPOSE:
    d = INST_DATA(cl, obj);
    free(d->heads); free(d->res);
    break;
  case OM_SET: {
    struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList, *t;
    d = INST_DATA(cl, obj);
    while ((t = NextTagItem(&tags))) if (t->ti_Tag == MUIA_Nav_View) d->view = (Object *)t->ti_Data;
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 110; m->MinMaxInfo->DefWidth += 140; m->MinMaxInfo->MaxWidth += 170;
    m->MinMaxInfo->MinHeight += 120; m->MinMaxInfo->DefHeight += 300; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct NData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup:
    if (key_grab == obj) key_grab = NULL;
    theme_cleanup(obj);
    break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) nav_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Show: ((struct NData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct NData *)INST_DATA(cl, obj))->shown = 0; nav_activate(INST_DATA(cl, obj), obj, 0); break;
  case MUIM_HandleEvent:
    return nav_events(cl, obj, (struct MUIP_HandleEvent *)msg);
  case MUIM_CreateShortHelp: {
    struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
    return (ULONG)nav_help(INST_DATA(cl, obj), (WORD)m->mx, (WORD)m->my);
  }
  case MUIM_DeleteShortHelp: return 0;
  case MUIM_Nav_Update: {
    struct MUIP_Nav_Update *m = (struct MUIP_Nav_Update *)msg;
    int changed = 0, cur;
    d = INST_DATA(cl, obj);
    if (m->doc && m->headings) {
      changed = nav_headings(d, m->doc);
      if (m->headings == 2 && d->searchlen) { nav_search(d, m->doc); changed = 1; }    /* a new document */
    }
    if (m->caretpara != d->caretpara || m->page != d->page || m->npages != d->npages) changed = 1;
    d->caretpara = m->caretpara; d->page = m->page; d->npages = m->npages;
    /* the caret's heading (or page) in view */
    if (_win(obj)) {
      int n;
      nav_layout(d, obj, _rp(obj));
      n = rows_fit(d);
      cur = current_heading(d);
      if (cur >= 0 && (cur < d->first[0] || cur >= d->first[0] + n)) { d->first[0] = cur - n / 3 > 0 ? cur - n / 3 : 0; changed = 1; }
      if (d->tab == NT_PAGES) {
        Doc *doc = view_doc(d);
        LONG tw, th;
        int per;
        thumb_size(d, doc, &tw, &th);
        per = (int)((d->body.y1 - d->body.y0) / (th + fh() + 14));
        if (per < 1) per = 1;
        if ((int)d->page < d->first[1] || (int)d->page >= d->first[1] + per) { d->first[1] = (int)d->page; changed = 1; }
      }
    }
    if (changed && _win(obj)) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}

/* ================================================================== */
/* the status bar */
struct SData {
  int shown;
  struct MUIP_Status_Set s;
  NBox page, words, focus, viewb[3], minus, plus, slider, pct;
  int hot, drag;
  LONG zoomset;                  /* the zoom chosen on the bar (MUIA_Status_Zoom) */
  char hint[200];                /* MUIM_Status_Hint's line (the address of a link under the pointer), "" none */
  struct MUI_EventHandlerNode ehn;
  NBox langb; char lang[64];     /* (WP31) the language shown, and where */
};
/* the slider: 10% at the left, 100% in the middle, 500% at the right */
/* the slider: 10% at the left, 100% in the middle, 500% at the right (two linear halves).
   zoom_x: the window x of zoom z */
static LONG zoom_x(struct SData *d, LONG z)
{
  LONG w = d->slider.x1 - d->slider.x0, mid = d->slider.x0 + w / 2;
  if (z <= 100) return d->slider.x0 + (z - 10) * (w / 2) / 90;
  return mid + (z - 100) * (w / 2) / 400;
}
/* and the other way: the zoom at window x, 10 to 500, snapping to 100% near the middle */
static LONG x_zoom(struct SData *d, LONG x)
{
  LONG w = d->slider.x1 - d->slider.x0, mid = d->slider.x0 + w / 2, z;
  if (w <= 0) return 100;
  if (x <= mid) z = 10 + (x - d->slider.x0) * 90 / (w / 2 ? w / 2 : 1);
  else z = 100 + (x - mid) * 400 / (w / 2 ? w / 2 : 1);
  if (z > 90 && z < 110) z = 100;            /* sticks at 100% */
  return z < 10 ? 10 : z > 500 ? 500 : z;
}
/* draws the bar and, as it goes, records where each part is (the boxes status_hit tests): the
   left side from the left, the right side from the right edge inwards */
static void status_draw(struct SData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG l = _mleft(obj), t = _mtop(obj), r = _mright(obj), b = _mbottom(obj), x, y = t + (b - t + 1 - fh()) / 2 + 1, w;
  char s[48];
  theme_fill(rp, l, t, r, b, tpen(T_STATUS));
  SetAPen(rp, tpen(T_BORDER)); Move(rp, l, t); Draw(rp, r, t);
  /* the left: page, words, language */
  x = l + 8;
  sprintf(s, tr("Page %lu of %lu%s"), (unsigned long)d->s.page + 1, (unsigned long)d->s.pages, d->s.complete ? "" : "+");
  w = theme_textlen(rp, theme_font(), s);
  d->page.x0 = (WORD)(x - 4); d->page.x1 = (WORD)(x + w + 3); d->page.y0 = (WORD)(t + 1); d->page.y1 = (WORD)b;
  if (d->hot == 1) theme_fill(rp, d->page.x0, d->page.y0, d->page.x1, d->page.y1, tpen(T_HOVER));
  theme_text(rp, theme_font(), x, y, s, tpen(T_DIM));
  x += w + 14;
  if (d->s.words == (ULONG)~0UL) strcpy(s, "... words");
  else sprintf(s, tr(d->s.words == 1 ? "%lu word" : "%lu words"), (unsigned long)d->s.words);
  w = theme_textlen(rp, theme_font(), s);
  d->words.x0 = (WORD)(x - 4); d->words.x1 = (WORD)(x + w + 3); d->words.y0 = (WORD)(t + 1); d->words.y1 = (WORD)b;
  if (d->hot == 2) theme_fill(rp, d->words.x0, d->words.y0, d->words.x1, d->words.y1, tpen(T_HOVER));
  theme_text(rp, theme_font(), x, y, s, tpen(T_DIM));
  x += w + 14;
  /* the right, from the right edge: percent, plus, slider, minus, view, focus */
  {
    LONG rx = r - 6, pw;
    sprintf(s, "%ld%%", (long)d->s.zoom);
    pw = theme_textlen(rp, theme_font(), "500%");
    d->pct.x1 = (WORD)rx; d->pct.x0 = (WORD)(rx - pw); d->pct.y0 = (WORD)(t + 1); d->pct.y1 = (WORD)b;
    if (d->hot == 3) theme_fill(rp, d->pct.x0 - 2, d->pct.y0, d->pct.x1, d->pct.y1, tpen(T_HOVER));
    theme_text(rp, theme_font(), rx - theme_textlen(rp, theme_font(), s), y, s, tpen(T_DIM));
    rx -= pw + 6;
    d->plus.x1 = (WORD)rx; d->plus.x0 = (WORD)(rx - 10); d->plus.y0 = (WORD)(t + 2); d->plus.y1 = (WORD)(b - 1);
    SetAPen(rp, tpen(T_ICON));
    Move(rp, d->plus.x0 + 1, (t + b) / 2); Draw(rp, d->plus.x1 - 1, (t + b) / 2);
    Move(rp, (d->plus.x0 + d->plus.x1) / 2, (t + b) / 2 - 4); Draw(rp, (d->plus.x0 + d->plus.x1) / 2, (t + b) / 2 + 4);
    rx -= 14;
    d->slider.x1 = (WORD)rx; d->slider.x0 = (WORD)(rx - (r - l > 500 ? 100 : 70)); d->slider.y0 = (WORD)(t + 2); d->slider.y1 = (WORD)(b - 1);
    SetAPen(rp, tpen(T_DIM));
    Move(rp, d->slider.x0, (t + b) / 2); Draw(rp, d->slider.x1, (t + b) / 2);
    { LONG mx = (d->slider.x0 + d->slider.x1) / 2; Move(rp, mx, (t + b) / 2 - 3); Draw(rp, mx, (t + b) / 2 + 3); }
    {
      LONG kx = zoom_x(d, d->s.zoom < 10 ? 10 : d->s.zoom > 500 ? 500 : d->s.zoom);
      theme_fill(rp, kx - 2, (t + b) / 2 - 5, kx + 2, (t + b) / 2 + 5, tpen(d->hot == 4 || d->drag ? T_ACCENT : T_ICON));
    }
    rx = d->slider.x0 - 4;
    d->minus.x1 = (WORD)rx; d->minus.x0 = (WORD)(rx - 10); d->minus.y0 = (WORD)(t + 2); d->minus.y1 = (WORD)(b - 1);
    SetAPen(rp, tpen(T_ICON));
    Move(rp, d->minus.x0 + 1, (t + b) / 2); Draw(rp, d->minus.x1 - 1, (t + b) / 2);
    rx = d->minus.x0 - 10;
    {                                          /* the views: read, print, web (right to left: web first) */
      static const UBYTE icons[3] = { IC_READ, IC_PRINTLAYOUT, IC_WEB }, modes[3] = { VM_READ, VM_PRINT, VM_WEB };
      int k;
      for (k = 2; k >= 0; k--) {
        NBox *v = &d->viewb[k];
        v->x1 = (WORD)rx; v->x0 = (WORD)(rx - 18); v->y0 = (WORD)(t + 2); v->y1 = (WORD)(b - 1);
        if (d->s.vmode == modes[k]) { theme_fill(rp, v->x0, v->y0, v->x1, v->y1, tpen(T_CHECKED)); theme_rect(rp, v->x0, v->y0, v->x1, v->y1, tpen(T_ACCENT)); }
        else if (d->hot == 8 + k) theme_fill(rp, v->x0, v->y0, v->x1, v->y1, tpen(T_HOVER));
        theme_icon(rp, icons[k], v->x0 + 2, v->y0 + (v->y1 - v->y0 - 14) / 2, 14, 0);
        rx = v->x0 - 2;
      }
    }
    rx = d->viewb[0].x0 - 6;
    w = theme_textlen(rp, theme_font(), "Focus");
    d->focus.x1 = (WORD)rx; d->focus.x0 = (WORD)(rx - w - 20); d->focus.y0 = (WORD)(t + 2); d->focus.y1 = (WORD)(b - 1);
    if (d->hot == 5) theme_fill(rp, d->focus.x0, d->focus.y0, d->focus.x1, d->focus.y1, tpen(T_HOVER));
    theme_icon(rp, IC_FOCUS, d->focus.x0 + 2, d->focus.y0 + (d->focus.y1 - d->focus.y0 - 14) / 2, 14, 0);
    theme_text(rp, theme_font(), d->focus.x0 + 18, y, "Focus", tpen(T_DIM));
    if (d->hint[0]) {
      /* a hint in place of the language: as much of it as fits before Focus */
      char h[200];
      size_t n = strlen(d->hint);
      strcpy(h, d->hint);
      while (n > 0 && x + theme_textlen(rp, theme_font(), h) >= d->focus.x0 - 10) h[--n] = 0;
      if (n) theme_text(rp, theme_font(), x, y, h, tpen(T_ACCENT));
    }
    /* the language, if there is room (WP31: the caret's; a click opens the Language window) */
    else {
      const char *lg = wp31_status_lang();
      LONG lw = theme_textlen(rp, theme_font(), lg);
      d->langb.x0 = d->langb.x1 = 0;
      if (x + lw < d->focus.x0 - 10) {
        d->langb.x0 = (WORD)(x - 4); d->langb.x1 = (WORD)(x + lw + 3); d->langb.y0 = (WORD)(t + 1); d->langb.y1 = (WORD)b;
        if (d->hot == 11) theme_fill(rp, d->langb.x0, d->langb.y0, d->langb.x1, d->langb.y1, tpen(T_HOVER));
        theme_text(rp, theme_font(), x, y, lg, tpen(T_DIM));
      }
      strncpy(d->lang, lg, sizeof d->lang - 1); d->lang[sizeof d->lang - 1] = 0;
    }
  }
}
/* the part of the bar at x, y: 1 page, 2 words, 3 percent, 4 slider, 5 Focus, 6 minus, 7 plus,
   8-10 the read, print and web view buttons; 0 none. d->hot holds the same numbers */
static int status_hit(struct SData *d, WORD x, WORD y)
{
  if (in_nbox(&d->page, x, y)) return 1;
  if (in_nbox(&d->words, x, y)) return 2;
  if (in_nbox(&d->pct, x, y)) return 3;
  if (in_nbox(&d->slider, x, y)) return 4;
  if (in_nbox(&d->focus, x, y)) return 5;
  if (in_nbox(&d->minus, x, y)) return 6;
  if (in_nbox(&d->plus, x, y)) return 7;
  if (in_nbox(&d->viewb[0], x, y)) return 8;
  if (in_nbox(&d->viewb[1], x, y)) return 9;
  if (in_nbox(&d->viewb[2], x, y)) return 10;
  if (d->langb.x1 > d->langb.x0 && in_nbox(&d->langb, x, y)) return 11;     /* (WP31) */
  return 0;
}
/* a zoom chosen on the bar: kept for MUIA_Status_Zoom and main.c told (ID_ZOOMSET), which sets
   the view's zoom and sends MUIM_Status_Set back */
static void status_zoom(struct SData *d, Object *obj, LONG z)
{
  d->zoomset = z;
  DoMethod(_app(obj), MUIM_Application_ReturnID, ID_ZOOMSET);
}
/* the bar's MUIM_HandleEvent: hover, clicks and dragging the zoom slider (the zoom applied when
   the button is let go). Clicks are mostly passed to main.c as ReturnIDs */
static ULONG status_events(struct IClass *cl, Object *obj, struct MUIP_HandleEvent *m)
{
  struct SData *d = INST_DATA(cl, obj);
  struct IntuiMessage *im = m->imsg;
  int inside, h;
  if (!im || !d->shown) return 0;
  inside = im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj) && im->MouseY >= _mtop(obj) && im->MouseY <= _mbottom(obj);
  if (im->Class == IDCMP_MOUSEMOVE) {
    if (d->drag) { d->s.zoom = x_zoom(d, im->MouseX); MUI_Redraw(obj, MADF_DRAWOBJECT); return MUI_EventHandlerRC_Eat; }
    h = inside ? status_hit(d, im->MouseX, im->MouseY) : 0;
    if (h != d->hot) { d->hot = h; MUI_Redraw(obj, MADF_DRAWOBJECT); }
    return 0;
  }
  if (im->Class != IDCMP_MOUSEBUTTONS) return 0;
  if (im->Code == SELECTUP && d->drag) { d->drag = 0; status_zoom(d, obj, d->s.zoom); return MUI_EventHandlerRC_Eat; }
  if (im->Code != SELECTDOWN || !inside) return 0;
  switch (status_hit(d, im->MouseX, im->MouseY)) {
  case 1: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_FIND); break;
  case 2: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_WORDCOUNT); break;
  case 3: status_zoom(d, obj, 100); break;
  case 4: d->drag = 1; d->s.zoom = x_zoom(d, im->MouseX); MUI_Redraw(obj, MADF_DRAWOBJECT); break;
  case 5: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_FOCUS); break;
  case 6: status_zoom(d, obj, d->s.zoom > 20 ? (d->s.zoom - 10) / 10 * 10 : 10); break;
  case 7: status_zoom(d, obj, d->s.zoom < 490 ? (d->s.zoom + 10) / 10 * 10 : 500); break;
  case 8: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_VREAD); break;
  case 9: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_VPRINT); break;
  case 10: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_VWEB); break;
  case 11: DoMethod(_app(obj), MUIM_Application_ReturnID, WP31_LANGWIN_ID); break;     /* (WP31) */
  default: return 0;
  }
  return MUI_EventHandlerRC_Eat;
}
/* the status bar's bubbles: what each part shows, and what a click on it does */
static const char *status_help(struct SData *d, WORD x, WORD y)
{
  switch (status_hit(d, x, y)) {
  case 1: return theme_bubble(tr("Page"), tr("The page the caret is on, and how many there are. Click to find text in the document."));
  case 2: return theme_bubble(tr("Word Count"), tr("Words in the document (or in the selection). Click for characters, paragraphs and pages too."));
  case 3: return theme_bubble(tr("Zoom"), tr("How big the page is shown. Click to go back to 100%."));
  case 4: return theme_bubble(tr("Zoom"), tr("Drag to zoom in or out. The middle is 100%."));
  case 5: return theme_bubble(tr("Focus"), tr("Hides the ribbon, rulers and pane so only the page is left. Click again to bring them back."));
  case 6: return theme_bubble(tr("Zoom Out"), tr("Shows the page 10% smaller."));
  case 7: return theme_bubble(tr("Zoom In"), tr("Shows the page 10% bigger."));
  case 8: return theme_bubble(tr("Read Mode"), tr("Two pages to read, without the tools (Esc returns)"));
  case 9: return theme_bubble(tr("Print Layout"), tr("The pages as they print"));
  case 10: return theme_bubble(tr("Web Layout"), tr("The text the window's width, without pages"));
  case 11: return theme_bubble(tr("Language"), tr("The language the text at the caret is checked in. Click to set it."));   /* (WP31) */
  }
  return NULL;
}
/* the status bar class's dispatcher (see ndispatch). MUIM_Status_Set redraws only when
   something shown changed, and is ignored while the slider is dragged so the knob doesn't jump */
static ULONG sdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct SData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->s.zoom = 100; d->s.pages = 1; d->s.complete = 1;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    return (ULONG)obj;
  case OM_GET: {
    struct opGet *g = (struct opGet *)msg;
    if (g->opg_AttrID == MUIA_Status_Zoom) { *g->opg_Storage = (ULONG)((struct SData *)INST_DATA(cl, obj))->zoomset; return TRUE; }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    LONG h = fh() + 9;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 200; m->MinMaxInfo->DefWidth += 640; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += h; m->MinMaxInfo->DefHeight += h; m->MinMaxInfo->MaxHeight += h;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct SData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup:
    theme_cleanup(obj);
    break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) status_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Show: ((struct SData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct SData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_HandleEvent:
    return status_events(cl, obj, (struct MUIP_HandleEvent *)msg);
  case MUIM_CreateShortHelp: {
    struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
    return (ULONG)status_help(INST_DATA(cl, obj), (WORD)m->mx, (WORD)m->my);
  }
  case MUIM_DeleteShortHelp: return 0;
  case MUIM_Status_Hint: {
    const char *t = ((struct MUIP_Status_Hint *)msg)->text;
    d = INST_DATA(cl, obj);
    if (!t) t = "";
    if (!strncmp(d->hint, t, sizeof d->hint - 1)) return 0;
    strncpy(d->hint, t, sizeof d->hint - 1); d->hint[sizeof d->hint - 1] = 0;
    if (_win(obj) && d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  }
  case MUIM_Status_Set: {
    struct MUIP_Status_Set *m = (struct MUIP_Status_Set *)msg;
    d = INST_DATA(cl, obj);
    if (d->drag || (!memcmp(&d->s, m, sizeof *m) && !strcmp(d->lang, wp31_status_lang()))) return 0;   /* (WP31: or the language changed) */
    d->s = *m;
    if (_win(obj)) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}

/* ================================================================== */
/* a flat scroll bar (beside the page): takes MUIA_Prop_Entries, _Visible and _First as a Prop does,
   and notifies MUIA_Prop_First as it is dragged */
struct FData { LONG entries, visible, first; int drag, hot, horiz, shown; LONG grab; struct MUI_EventHandlerNode ehn; };
/* along the bar: its start and length, and the pointer's place */
static LONG f0(struct FData *d, Object *obj) { return (d->horiz ? _mleft(obj) : _mtop(obj)) + 2; }
static LONG flen(struct FData *d, Object *obj) { return (d->horiz ? _mwidth(obj) : _mheight(obj)) - 4; }
static struct MUI_CustomClass *fcls;
/* the thumb's start and end along the bar (window pixels), at least 13 long and kept inside */
static void thumb(struct FData *d, Object *obj, LONG *a, LONG *b)
{
  LONG t = f0(d, obj), len = flen(d, obj), e = d->entries > d->visible ? d->entries : d->visible;
  if (e <= 0) e = 1;
  *a = t + len * d->first / e;
  *b = t + len * (d->first + d->visible) / e - 1;
  if (*b < *a + 12) *b = *a + 12;
  if (*b > t + len - 1) { *a -= *b - (t + len - 1); *b = t + len - 1; }
  if (*a < t) *a = t;
}
/* the bar: the canvas colour with a rounded thumb (two overlapping fills), none if everything is
   visible; darker while dragged, lighter under the pointer */
static void flat_draw(struct FData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG a, b, l = _mleft(obj), r = _mright(obj);
  theme_fill(rp, l, _mtop(obj), r, _mbottom(obj), tpen(T_CANVAS));
  if (d->entries <= d->visible) return;
  thumb(d, obj, &a, &b);
  if (d->horiz) {
    LONG t = _mtop(obj), u = _mbottom(obj);
    theme_fill(rp, a + 1, t + 3, b - 1, u - 3, tpen(d->drag ? T_DIM : d->hot ? T_GROUPLABEL : T_BORDER2));
    theme_fill(rp, a, t + 4, b, u - 4, tpen(d->drag ? T_DIM : d->hot ? T_GROUPLABEL : T_BORDER2));
    return;
  }
  theme_fill(rp, l + 3, a + 1, r - 3, b - 1, tpen(d->drag ? T_DIM : d->hot ? T_GROUPLABEL : T_BORDER2));
  theme_fill(rp, l + 4, a, r - 4, b, tpen(d->drag ? T_DIM : d->hot ? T_GROUPLABEL : T_BORDER2));
}
/* MUIA_Prop_First set to f, kept between 0 and entries - visible */
static void flat_set_first(struct FData *d, Object *obj, LONG f)
{
  LONG max = d->entries - d->visible;
  if (f > max) f = max;
  if (f < 0) f = 0;
  if (f == d->first) return;
  SetAttrs(obj, MUIA_Prop_First, f, TAG_DONE);          /* through OM_SET, so notifications go */
}
/* the flat scroll bar's dispatcher: it stands in for MUI's Prop class, keeping the same three
   attributes so main.c can notify on MUIA_Prop_First as it would on a Prop. A click either side of
   the thumb moves nine tenths of a view; d->grab is where on the thumb it was caught */
static ULONG fdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct FData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->entries = d->visible = 1;
    d->horiz = (int)GetTagData(MUIA_FlatScroll_Horiz, FALSE, ((struct opSet *)msg)->ops_AttrList);
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    return (ULONG)obj;
  case OM_SET: {
    struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList, *t;
    int redraw = 0;
    d = INST_DATA(cl, obj);
    while ((t = NextTagItem(&tags))) {
      if (t->ti_Tag == MUIA_Prop_Entries && d->entries != (LONG)t->ti_Data) { d->entries = (LONG)t->ti_Data; redraw = 1; }
      else if (t->ti_Tag == MUIA_Prop_Visible && d->visible != (LONG)t->ti_Data) { d->visible = (LONG)t->ti_Data; redraw = 1; }
      else if (t->ti_Tag == MUIA_Prop_First && d->first != (LONG)t->ti_Data) { d->first = (LONG)t->ti_Data; redraw = 1; }
    }
    if (redraw && _win(obj)) MUI_Redraw(obj, MADF_DRAWOBJECT);
    break;
  }
  case OM_GET: {
    struct opGet *g = (struct opGet *)msg;
    d = INST_DATA(cl, obj);
    if (g->opg_AttrID == MUIA_Prop_Entries) { *g->opg_Storage = (ULONG)d->entries; return TRUE; }
    if (g->opg_AttrID == MUIA_Prop_Visible) { *g->opg_Storage = (ULONG)d->visible; return TRUE; }
    if (g->opg_AttrID == MUIA_Prop_First) { *g->opg_Storage = (ULONG)d->first; return TRUE; }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    if (((struct FData *)INST_DATA(cl, obj))->horiz) {
      m->MinMaxInfo->MinHeight += 12; m->MinMaxInfo->DefHeight += 12; m->MinMaxInfo->MaxHeight += 12;
      m->MinMaxInfo->MinWidth += 30; m->MinMaxInfo->DefWidth += 200; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
      return 0;
    }
    m->MinMaxInfo->MinWidth += 12; m->MinMaxInfo->DefWidth += 12; m->MinMaxInfo->MaxWidth += 12;
    m->MinMaxInfo->MinHeight += 30; m->MinMaxInfo->DefHeight += 200; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct FData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup:
    theme_cleanup(obj);
    break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) flat_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Show: ((struct FData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct FData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_HandleEvent: {
    struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
    LONG a, b, mp;
    int inside;
    d = INST_DATA(cl, obj);
    if (!im || !d->shown || d->entries <= d->visible) return 0;
    inside = im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj) && im->MouseY >= _mtop(obj) && im->MouseY <= _mbottom(obj);
    thumb(d, obj, &a, &b);
    mp = d->horiz ? im->MouseX : im->MouseY;
    if (im->Class == IDCMP_MOUSEMOVE) {
      if (d->drag) {
        LONG len = flen(d, obj);
        flat_set_first(d, obj, (mp - d->grab - f0(d, obj)) * d->entries / (len > 0 ? len : 1));
        return MUI_EventHandlerRC_Eat;
      }
      if ((inside && mp >= a && mp <= b) != d->hot) { d->hot = !d->hot; MUI_Redraw(obj, MADF_DRAWOBJECT); }
      return 0;
    }
    if (im->Class != IDCMP_MOUSEBUTTONS) return 0;
    if (im->Code == SELECTUP && d->drag) { d->drag = 0; MUI_Redraw(obj, MADF_DRAWOBJECT); return MUI_EventHandlerRC_Eat; }
    if (im->Code != SELECTDOWN || !inside) return 0;
    if (mp < a) flat_set_first(d, obj, d->first - d->visible * 9 / 10);
    else if (mp > b) flat_set_first(d, obj, d->first + d->visible * 9 / 10);
    else { d->drag = 1; d->grab = mp - a; MUI_Redraw(obj, MADF_DRAWOBJECT); }
    return MUI_EventHandlerRC_Eat;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
/* the classes, each a subclass of Area made on first call (NULL if MUI couldn't make it): the
   flat scroll bar, */
struct MUI_CustomClass *flatscroll_class(void)
{
  if (!fcls) fcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct FData), (APTR)fdispatch);
  return fcls;
}

/* the navigation pane */
struct MUI_CustomClass *nav_class(void)
{
  if (!ncls) ncls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct NData), (APTR)ndispatch);
  return ncls;
}
/* and the status bar */
struct MUI_CustomClass *status_class(void)
{
  if (!scls) scls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct SData), (APTR)sdispatch);
  return scls;
}
/* at the end, once the window and every object of these classes has been disposed of */
void panes_free(void)
{
  if (ncls) { MUI_DeleteCustomClass(ncls); ncls = NULL; }
  if (scls) { MUI_DeleteCustomClass(scls); scls = NULL; }
  if (fcls) { MUI_DeleteCustomClass(fcls); fcls = NULL; }
}
