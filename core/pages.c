/* pages.c - see pages.h (WP22a) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pages.h"
#include "image.h"
#include "hyph.h"

/* ------------------------------------------------------------------ */
/* the page as laid out */
int pages_multi(const Doc *d)
{
  return d->bookfold ? MP_BOOKFOLD : d->twoonone ? MP_TWOONONE : d->mirror ? MP_MIRROR : MP_NORMAL;
}
void pages_set_multi(Doc *d, int mp)
{
  d->mirror = (u8)(mp == MP_MIRROR); d->twoonone = (u8)(mp == MP_TWOONONE); d->bookfold = (u8)(mp == MP_BOOKFOLD);
  if (mp == MP_BOOKFOLD || mp == MP_MIRROR) d->gutterattop = 0;       /* (Word offers only the left then) */
}
/* nothing about the document changes its pages from their page setups */
static int plain(const Doc *d, const PageSetup *src, int rtl)
{
  return !d->mirror && !d->twoonone && !d->bookfold && !src->gutter && !rtl;
}
/* see pages.h. A page is odd or even by its number (Doc.pgstart counting from it) */
void pages_effective(const Doc *d, const PageSetup *src, int rtl, u32 page, PageSetup *out)
{
  int even;
  *out = *src;
  if (d->edit.kind || plain(d, src, rtl)) return;
  even = ((page + (d->pgstart > 1 ? d->pgstart : 1)) & 1) == 0;
  if (d->bookfold) {
    i32 lng = src->width > src->height ? src->width : src->height, sht = src->width + src->height - lng;
    out->width = lng / 2; out->height = sht;
  } else if (d->twoonone) {
    if (src->width <= src->height) out->height = src->height / 2;
    else out->width = src->width / 2;
  }
  if (src->gutter > 0) {
    if (d->gutterattop && !d->mirror && !d->bookfold) out->top += src->gutter;
    else if (rtl) out->right += src->gutter;
    else out->left += src->gutter;
  }
  if ((d->mirror || d->bookfold) && even) { i32 t = out->left; out->left = out->right; out->right = t; }
  /* margins that leave no room are not believed */
  if (out->left + out->right > out->width - 720) { out->left = out->right = out->width / 8; }
  if (out->top + out->bottom > out->height - 720) { out->top = out->bottom = out->height / 8; }
}
const PageSetup *pages_eff(Layout *L, const PageSetup *src, const SectFmt *s, u32 page)
{
  Doc *d = L->d;
  int k, idx = -1, rtl = s ? s->rtlgutter : 0;
  u32 need;
  if (!src || L->webw > 0 || d->edit.kind || plain(d, src, rtl)) return src;
  if (src == &d->page) idx = 0;
  else for (k = 0; k < d->nsects; k++) if (src == &d->sects[k].page) { idx = k + 1; break; }
  if (idx < 0) return src;
  need = (u32)(idx + 1) * 4;
  if (need > L->nepg) {
    u32 nc = need + 16;
    PageSetup *n = (PageSetup *)realloc(L->epg, nc * sizeof(PageSetup));
    if (!n) return src;
    L->epg = n; L->nepg = nc;
  }
  {
    /* (a slot each for odd and even pages, with and without a right-hand gutter) */
    PageSetup *e = &L->epg[idx * 4 + (rtl ? 2 : 0) + ((page + (d->pgstart > 1 ? d->pgstart : 1)) & 1)];
    pages_effective(d, src, rtl, page, e);
    return e;
  }
}
void pages_layout_free(Layout *L) { free(L->epg); L->epg = NULL; L->nepg = 0; }

/* ------------------------------------------------------------------ */
/* columns */
int pages_cols(const SectFmt *s, i32 body, i32 *x, i32 *w)
{
  int n = !s || s->cols < 1 ? 1 : s->cols > MAX_SECT_COLS ? MAX_SECT_COLS : s->cols, k;
  if (n > 1 && s->colunequal) {
    /* their own widths, scaled to the text area if they add up to more or less */
    i32 sum = 0, at = 0;
    for (k = 0; k < n; k++) sum += s->colw[k] + (k + 1 < n ? s->colsp[k] : 0);
    if (sum > 0) {
      int narrow = 0;
      for (k = 0; k < n; k++) {
        x[k] = (i32)((long long)at * body / sum);
        w[k] = (i32)((long long)s->colw[k] * body / sum);
        if (w[k] < 720) narrow = 1;
        at += s->colw[k] + (k + 1 < n ? s->colsp[k] : 0);
      }
      if (!narrow) return n;
    }
  }
  if (n > 1) {
    i32 gap = s->colgap, cw = (body - (n - 1) * gap) / n;
    if (cw >= 720) {
      for (k = 0; k < n; k++) { x[k] = k * (cw + gap); w[k] = cw; }
      return n;
    }
  }
  x[0] = 0; w[0] = body;
  return 1;
}
void pages_col_set(SectFmt *s, int n, const i32 *w, const i32 *sp, int equal, i32 gap)
{
  int k;
  if (n < 1) n = 1;
  if (n > MAX_SECT_COLS) n = MAX_SECT_COLS;
  s->cols = (u8)n;
  s->colunequal = (u8)(!equal && n > 1);
  if (gap >= 0) s->colgap = gap;
  memset(s->colw, 0, sizeof s->colw); memset(s->colsp, 0, sizeof s->colsp);
  if (s->colunequal) for (k = 0; k < n; k++) { s->colw[k] = w[k]; s->colsp[k] = k + 1 < n ? sp[k] : 0; }
}
/* Word's Left and Right: two columns, one a third of the room less the gap... the narrow one about
   half the wide one's width (Word: 1.83" and 4.17" with a 0.5" gap on Letter) */
void pages_col_preset(SectFmt *s, int preset, i32 body)
{
  i32 w[2], sp[2] = { 720, 0 };
  if (preset <= CP_THREE) { pages_col_set(s, preset + 1, NULL, NULL, 1, 720); return; }
  w[0] = (body - 720) * 3 / 10; w[1] = body - 720 - w[0];
  if (preset == CP_RIGHT) { i32 t = w[0]; w[0] = w[1]; w[1] = t; }
  pages_col_set(s, 2, w, sp, 0, 720);
}
int pages_col_preset_of(const SectFmt *s, i32 body)
{
  i32 x[MAX_SECT_COLS], w[MAX_SECT_COLS];
  int n = pages_cols(s, body, x, w);
  if (!s->colunequal || n == 1) return n <= 3 ? n - 1 : -1;
  if (n == 2 && w[0] * 3 < w[1] * 2) return CP_LEFT;
  if (n == 2 && w[1] * 3 < w[0] * 2) return CP_RIGHT;
  return -1;
}

/* ------------------------------------------------------------------ */
/* page borders */
/* Word's line kinds (ST_Border, without the art ones): drawn as lines from the side nearest the
   text outwards: W a line the border's width, T a thin one, H half the width (two tones), gaps: s
   small, m medium, l large, G the width; and how they are broken (DK_) */
enum { DK_SOLID, DK_DOT, DK_DASH, DK_DASHSMALL, DK_DOTDASH, DK_DOTDOTDASH, DK_WAVE };
typedef struct { const char *name; const char *parts; u8 dash, pb, tone; } LineKind;
static const LineKind kinds[] = {
  { "single", "W", DK_SOLID, PB_SINGLE, 0 }, { "thick", "W", DK_SOLID, PB_THICK, 0 },
  { "double", "WGW", DK_SOLID, PB_DOUBLE, 0 }, { "dotted", "W", DK_DOT, PB_DOTTED, 0 },
  { "dashed", "W", DK_DASH, PB_DASHED, 0 }, { "dotDash", "W", DK_DOTDASH, PB_DASHED, 0 },
  { "dotDotDash", "W", DK_DOTDOTDASH, PB_DASHED, 0 }, { "triple", "WGWGW", DK_SOLID, PB_DOUBLE, 0 },
  { "thinThickSmallGap", "TsW", DK_SOLID, PB_DOUBLE, 0 }, { "thickThinSmallGap", "WsT", DK_SOLID, PB_DOUBLE, 0 },
  { "thinThickThinSmallGap", "TsWsT", DK_SOLID, PB_DOUBLE, 0 }, { "thinThickMediumGap", "TmW", DK_SOLID, PB_DOUBLE, 0 },
  { "thickThinMediumGap", "WmT", DK_SOLID, PB_DOUBLE, 0 }, { "thinThickThinMediumGap", "TmWmT", DK_SOLID, PB_DOUBLE, 0 },
  { "thinThickLargeGap", "TlW", DK_SOLID, PB_DOUBLE, 0 }, { "thickThinLargeGap", "WlT", DK_SOLID, PB_DOUBLE, 0 },
  { "thinThickThinLargeGap", "TlWlT", DK_SOLID, PB_DOUBLE, 0 }, { "wave", "W", DK_WAVE, PB_SINGLE, 0 },
  { "doubleWave", "WGW", DK_WAVE, PB_DOUBLE, 0 }, { "dashSmallGap", "W", DK_DASHSMALL, PB_DASHED, 0 },
  { "dashDotStroked", "W", DK_DOTDASH, PB_DASHED, 0 }, { "threeDEmboss", "HH", DK_SOLID, PB_THICK, 1 },
  { "threeDEngrave", "HH", DK_SOLID, PB_THICK, 2 }, { "outset", "HH", DK_SOLID, PB_THICK, 1 },
  { "inset", "HH", DK_SOLID, PB_THICK, 2 },
};
#define NKINDS ((int)(sizeof kinds / sizeof kinds[0]))
const char *const pages_pb_vals[] = { "single", "dotted", "dashed", "dashSmallGap", "dotDash", "dotDotDash", "double", "triple",
  "thinThickSmallGap", "thickThinSmallGap", "thinThickThinSmallGap", "thinThickMediumGap", "thickThinMediumGap",
  "thinThickThinMediumGap", "thinThickLargeGap", "thickThinLargeGap", "thinThickThinLargeGap", "wave", "doubleWave",
  "dashDotStroked", "threeDEmboss", "threeDEngrave", "outset", "inset", "thick" };
const int pages_pb_nvals = (int)(sizeof pages_pb_vals / sizeof pages_pb_vals[0]);
const char *const pages_pb_arts[] = { "basicBlackDots", "basicBlackSquares", "basicThinLines", "basicWideOutline", "checkedBarBlack",
  "dotDash", "triangles", "zigZag", "apples", "hearts", "stars", "weavingBraid" };
const int pages_pb_narts = (int)(sizeof pages_pb_arts / sizeof pages_pb_arts[0]);

static const LineKind *kind_of(const char *val)
{
  int k;
  if (!val || !val[0]) return &kinds[0];
  for (k = 0; k < NKINDS; k++) if (!strcmp(kinds[k].name, val)) return &kinds[k];
  return NULL;                          /* an art border */
}
int pages_pb_art(const char *val) { return val && val[0] && strcmp(val, "none") && strcmp(val, "nil") && !kind_of(val); }
int pages_pb_style(const char *val)
{
  const LineKind *k;
  if (!val || !strcmp(val, "none") || !strcmp(val, "nil")) return PB_NONE;
  k = kind_of(val);
  return k ? k->pb : PB_SINGLE;
}
static const char *pb_name(int style)
{
  static const char *const n[6] = { "none", "single", "double", "thick", "dotted", "dashed" };
  return n[style >= 0 && style < 6 ? style : 1];
}
void pages_pb_side(const PageBorder *pb, int k, PBSide *out)
{
  memset(out, 0, sizeof *out);
  if (pb->style == PB_NONE) return;
  if (pb->own) { *out = pb->side[k & 3]; if (out->style != PB_NONE && !out->val[0]) strcpy(out->val, pb_name(out->style)); return; }
  out->style = pb->style; out->width = pb->width; out->color = pb->color; out->space = pb->space;
  strcpy(out->val, pb_name(pb->style));
}
int pages_pb_mask(const PageBorder *pb)
{
  int k, m = 0;
  for (k = 0; k < 4; k++) { PBSide s; pages_pb_side(pb, k, &s); if (s.style != PB_NONE) m |= 1 << k; }
  return m;
}
void pages_pb_set(PageBorder *pb, const char *val, int width, u32 color, i32 space, int mask, int frompage)
{
  int k, st = pages_pb_style(val);
  u8 disp = pb->display, back = pb->back;
  memset(pb, 0, sizeof *pb);
  pb->display = disp; pb->back = back;
  if (st == PB_NONE || !(mask & 15)) return;
  pb->style = (u8)st; pb->width = (u8)(width < 1 ? 4 : width > 255 ? 255 : width); pb->color = color; pb->space = space; pb->frompage = (u8)frompage;
  pb->own = 1;
  for (k = 0; k < 4; k++) {
    PBSide *s = &pb->side[k];
    if (!(mask & (1 << k))) continue;
    s->style = (u8)st; s->width = pb->width; s->color = color; s->space = space;
    strncpy(s->val, val, sizeof s->val - 1);
  }
  /* one kind all round in a kind the old fields name: kept as those alone (as 0.14 wrote it) */
  if ((mask & 15) == 15 && !strcmp(val, pb_name(st))) pb->own = 0, memset(pb->side, 0, sizeof pb->side);
}
const PageBorder *pages_sect_border(const Doc *d, const SectFmt *s, int last)
{
  return !last && s && s->pbown ? &s->pb : &d->border;
}
const PageBorder *pages_page_border(Layout *L, u32 page)
{
  const PageBorder *pb;
  const PageInfo *pi;
  if (page >= L->npages || page >= L->pinfocap || L->webw > 0) return L->d->border.style != PB_NONE && L->webw <= 0 ? &L->d->border : NULL;
  pi = &L->pinfo[page];
  pb = pages_sect_border(L->d, pi->sec, pi->last);
  if (pb->style == PB_NONE) return NULL;
  if (pb->display == PBD_FIRST && page != pi->first) return NULL;
  if (pb->display == PBD_NOTFIRST && page == pi->first) return NULL;
  return pb;
}
/* rectangles collected */
typedef struct { PBRect *r; int n, cap; } Rects;
static void add_rect(Rects *R, i32 x0, i32 y0, i32 x1, i32 y1, u32 col)
{
  if (x1 < x0) { i32 t = x0; x0 = x1; x1 = t; }
  if (y1 < y0) { i32 t = y0; y0 = y1; y1 = t; }
  if (x1 - x0 < 1 || y1 - y0 < 1) return;
  if (R->n >= R->cap) {
    int nc = R->cap ? R->cap * 2 : 64;
    PBRect *n;
    if (nc > 16384 || !(n = (PBRect *)realloc(R->r, (size_t)nc * sizeof(PBRect)))) return;
    R->r = n; R->cap = nc;
  }
  R->r[R->n].x0 = x0; R->r[R->n].y0 = y0; R->r[R->n].x1 = x1; R->r[R->n].y1 = y1; R->r[R->n].color = col;
  R->n++;
}
/* one line along a side: from a to b along it (x for top and bottom, y for left and right), across
   from c0 to c1, broken as dash says (w: the border's width, for the pattern's sizes) */
static void dash_line(Rects *R, int horiz, i32 a, i32 b, i32 c0, i32 c1, int dash, i32 w, u32 col)
{
  i32 pat[6], np = 0, at = a, i = 0, len;
  if (w < 10) w = 10;
  switch (dash) {
  case DK_DOT: pat[0] = w < 20 ? 20 : w; pat[1] = pat[0]; np = 2; break;
  case DK_DASH: pat[0] = 3 * w < 80 ? 80 : 3 * w; pat[1] = 2 * w < 50 ? 50 : 2 * w; np = 2; break;
  case DK_DASHSMALL: pat[0] = 3 * w < 80 ? 80 : 3 * w; pat[1] = w < 25 ? 25 : w; np = 2; break;
  case DK_DOTDASH: pat[0] = 3 * w < 80 ? 80 : 3 * w; pat[1] = w < 25 ? 25 : w; pat[2] = pat[1]; pat[3] = pat[1]; np = 4; break;
  case DK_DOTDOTDASH: pat[0] = 3 * w < 80 ? 80 : 3 * w; pat[1] = w < 25 ? 25 : w; pat[2] = pat[1]; pat[3] = pat[1]; pat[4] = pat[1]; pat[5] = pat[1]; np = 6; break;
  case DK_WAVE: {
    /* short pieces stepping up and down across a band twice the width */
    i32 step = 2 * w < 60 ? 60 : 2 * w, half = (c1 - c0) / 2;
    int up = 0;
    for (at = a; at < b; at += step, up = !up) {
      i32 e = at + step > b ? b : at + step, d0 = up ? c0 : c0 + half, d1 = up ? c0 + half : c1;
      if (horiz) add_rect(R, at, d0, e, d1, col); else add_rect(R, d0, at, d1, e, col);
    }
    return; }
  default:
    if (horiz) add_rect(R, a, c0, b, c1, col); else add_rect(R, c0, a, c1, b, col);
    return;
  }
  while (at < b) {
    len = pat[i % np];
    if (!(i & 1)) {
      i32 e = at + len > b ? b : at + len;
      if (horiz) add_rect(R, at, c0, e, c1, col); else add_rect(R, c0, at, c1, e, col);
    }
    at += len; i++;
  }
}
static u32 lighter(u32 c) { return ((((c >> 16) & 255) + 255) / 2) << 16 | ((((c >> 8) & 255) + 255) / 2) << 8 | (((c & 255) + 255) / 2); }
/* a side's lines: offsets (from the edge nearest the text, outwards) and thickness of each, and its
   whole band; returns how many lines */
typedef struct { i32 o, t; u32 col; } Sub;
static int side_lines(const PBSide *s, Sub *sub, i32 *band, int *dash, int *art)
{
  const LineKind *k = kind_of(s->val);
  i32 w = s->width * 5 / 2, t, at = 0;
  u32 col = s->color == COLOR_AUTO ? 0 : s->color;
  int n = 0;
  const char *p;
  *art = 0; *dash = DK_SOLID;
  if (s->style == PB_NONE) { *band = 0; return 0; }
  if (!k) {                                    /* art: a band sz points wide */
    *art = 1; *band = (s->width ? s->width : 12) * 20;
    sub[0].o = 0; sub[0].t = *band; sub[0].col = s->color == COLOR_AUTO ? 0x808080 : col;
    return 1;
  }
  if (w < 5) w = 5;
  t = w / 4 < 8 ? 8 : w / 4;
  *dash = k->dash;
  for (p = k->parts; *p && n < 8; p++) {
    switch (*p) {
    case 'W': sub[n].o = at; sub[n].t = k->dash == DK_WAVE ? 2 * w : w; sub[n].col = col; at += sub[n].t; n++; break;
    case 'T': sub[n].o = at; sub[n].t = t; sub[n].col = col; at += t; n++; break;
    case 'H': sub[n].o = at; sub[n].t = (w + 1) / 2; at += sub[n].t;
              /* two tones: embossed light outside, engraved light inside */
              sub[n].col = (k->tone == 1) == (n == 0) ? col : lighter(col == 0 ? 0x404040 : col); n++; break;
    case 's': at += t; break;
    case 'm': at += w / 2 > t ? w / 2 : t; break;
    case 'l': at += w > t ? w : t; break;
    case 'G': at += w; break;
    }
  }
  *band = at;
  return n;
}
/* an art border's band: squares of the colour along it, a gap between */
static void art_line(Rects *R, int horiz, i32 a, i32 b, i32 c0, i32 c1, u32 col)
{
  i32 sz = c1 - c0, at;
  if (sz < 20) sz = 20;
  for (at = a; at < b; at += sz) {
    i32 e = at + sz * 2 / 3 > b ? b : at + sz * 2 / 3;
    if (horiz) add_rect(R, at, c0 + sz / 6, e, c1 - sz / 6, col); else add_rect(R, c0 + sz / 6, at, c1 - sz / 6, e, col);
  }
}
int pages_border_rects(Layout *L, u32 page, PBRect **out)
{
  const PageBorder *pb = pages_page_border(L, page);
  const PageSetup *pg = lay_page(L, page);
  Doc *d = L->d;
  PBSide s[4];
  Sub sub[4][8];
  int nsub[4], dash[4], art[4], k, j;
  i32 band[4], edge[4];                  /* each side's edge nearest the text: y for top and bottom, x for left and right */
  Rects R;
  *out = NULL;
  if (!pb) return 0;
  memset(&R, 0, sizeof R);
  for (k = 0; k < 4; k++) { pages_pb_side(pb, k, &s[k]); nsub[k] = side_lines(&s[k], sub[k], &band[k], &dash[k], &art[k]); }
  if (pb->frompage) {
    /* space from the page's edge to the border's outside */
    edge[PBS_TOP] = s[PBS_TOP].space + band[PBS_TOP];
    edge[PBS_LEFT] = s[PBS_LEFT].space + band[PBS_LEFT];
    edge[PBS_BOTTOM] = pg->height - s[PBS_BOTTOM].space - band[PBS_BOTTOM];
    edge[PBS_RIGHT] = pg->width - s[PBS_RIGHT].space - band[PBS_RIGHT];
  } else {
    /* space from the text to the border's inside; round the header and footer too unless told not to */
    i32 top = pg->top, bottom = pg->height - pg->bottom;
    Story *h = lay_page_story(L, page, 0), *f = lay_page_story(L, page, 1);
    if (!d->pbnohdr && h && h->np && pg->header < top) top = pg->header;
    if (!d->pbnoftr && f && f->np && pg->height - pg->footer > bottom) bottom = pg->height - pg->footer;
    edge[PBS_TOP] = top - s[PBS_TOP].space;
    edge[PBS_LEFT] = pg->left - s[PBS_LEFT].space;
    edge[PBS_BOTTOM] = bottom + s[PBS_BOTTOM].space;
    edge[PBS_RIGHT] = pg->width - pg->right + s[PBS_RIGHT].space;
  }
  for (k = 0; k < 4; k++)
    for (j = 0; j < nsub[k]; j++) {
      const Sub *u = &sub[k][j];
      int horiz = k == PBS_TOP || k == PBS_BOTTOM;
      int a0 = horiz ? PBS_LEFT : PBS_TOP, a1 = horiz ? PBS_RIGHT : PBS_BOTTOM;
      /* across the corners: as far out along the sides at the ends as this line is out from its own edge */
      i32 o0 = u->o, o1 = u->o + u->t, lo, hi, c0, c1;
      lo = edge[a0] - (nsub[a0] ? (o1 < band[a0] ? o1 : band[a0]) : 0);
      hi = edge[a1] + (nsub[a1] ? (o1 < band[a1] ? o1 : band[a1]) : 0);
      if (k == PBS_TOP || k == PBS_LEFT) { c0 = edge[k] - o1; c1 = edge[k] - o0; }
      else { c0 = edge[k] + o0; c1 = edge[k] + o1; }
      if (art[k]) art_line(&R, horiz, lo, hi, c0, c1, u->col);
      else dash_line(&R, horiz, lo, hi, c0, c1, dash[k], s[k].width * 5 / 2, u->col);
    }
  *out = R.r;
  return R.n;
}

/* ------------------------------------------------------------------ */
/* watermarks */
int pages_wm_on(const Doc *d) { return d->wm.text[0] || d->wm.pic; }
void pages_wm_clear(Doc *d)
{
  free(d->wm.pic);
  d->wm.pic = NULL; d->wm.piclen = 0; d->wm.text[0] = 0;
}
int pages_wm_picture(Doc *d, const u8 *data, u32 len)
{
  u32 w, h;
  int t = img_info(data, len, &w, &h);
  u8 *c;
  if (t == IMG_NONE || !w || !h || !(c = (u8 *)malloc(len))) return 0;
  memcpy(c, data, len);
  free(d->wm.pic);
  d->wm.pic = c; d->wm.piclen = len; d->wm.pictype = (u8)t; d->wm.pw = w; d->wm.ph = h;
  d->wm.text[0] = 0;
  return 1;
}
void pages_wm_box(const Doc *d, const PageSetup *pg, i32 *x, i32 *y, i32 *w, i32 *h)
{
  i32 bw = pg->width - pg->left - pg->right, bh = pg->height - pg->top - pg->bottom;
  long pw = d->wm.pw ? d->wm.pw : 1, ph = d->wm.ph ? d->wm.ph : 1;
  if (d->wm.scale) { *w = (i32)(pw * 15 * d->wm.scale / 100); *h = (i32)(ph * 15 * d->wm.scale / 100); }
  else if (pw * bh > ph * bw) { *w = bw; *h = (i32)(ph * bw / pw); }
  else { *h = bh; *w = (i32)(pw * bh / ph); }
  if (*w < 20) *w = 20;
  if (*h < 20) *h = 20;
  *x = pg->left + (bw - *w) / 2; *y = pg->top + (bh - *h) / 2;
}
/* Word's washout (VML gain 0.3, black level 0.35) as LibreOffice shows it: black becomes 85% white,
   white stays white (the same straight line as core/pdf.c's /Decode [0.85 1]) */
void pages_washout(u8 *rgb, u32 npix)
{
  u32 i;
  for (i = 0; i < npix * 3; i++) rgb[i] = (u8)(217 + (u32)rgb[i] * 38 / 255);
}

static const u8 *washed;
void pages_wash_mark(const u8 *data) { washed = data; }
int pages_wash_is(const u8 *data) { return data && data == washed; }

/* ------------------------------------------------------------------ */
/* hyphenation */
i32 pages_hyph_zone(const Doc *d) { return d->hyzone > 0 ? d->hyzone : 360; }
int pages_hyph_allowed(const Doc *d, const char *word, u32 len, const Line *l, u32 nl, const Box *b)
{
  u32 k, run = 0;
  if (d->hynocaps) {
    int caps = 1, any = 0;
    for (k = 0; k < len; k++) if ((word[k] >= 'a' && word[k] <= 'z')) caps = 0; else if (word[k] >= 'A' && word[k] <= 'Z') any = 1;
    if (caps && any) return 0;
  }
  if (d->hylimit) {
    /* lines just before ending in an automatic hyphen (a box marked to show one) */
    for (k = nl; k-- > 0; ) {
      const Line *ln = &l[k];
      if (!ln->nbox || b[ln->box0 + ln->nbox - 1].kind != BX_TEXT || b[ln->box0 + ln->nbox - 1].nspace != 1) break;
      run++;
    }
    if (run >= d->hylimit) return 0;
  }
  return 1;
}
void pages_hyph_changed(Doc *d)
{
  u32 i;
  /* (versions are stamps from a counter: a value it hasn't reached yet, or one it has passed, is
     never one a later edit gives) */
  for (i = 0; i < d->np; i++) d->p[i]->ver = ~d->p[i]->ver;
  d->ver++;
}
int pages_next_manual(Layout *L, Pos from, Pos *at, u32 *len, u8 *brk, u32 *suggest)
{
  u32 k;
  Doc *d = L->d;
  for (k = 0; k + 1 < L->nplaced; k++) {
    const Placed *pc = &L->placed[k];
    ParaLay *pl = &L->pl[pc->para];
    const Line *ln, *nx;
    const Box *b;
    const char *t;
    u32 lead = 0, core, j;
    i32 room;
    FaceSize *z;
    if (pc->para < from.para || pc->line + 1 >= pl->nlines) continue;
    ln = &pl->lines[pc->line]; nx = &pl->lines[pc->line + 1];
    if (!nx->nbox) continue;
    b = &pl->boxes[nx->box0];
    if (b->kind != BX_TEXT || (pc->para == from.para && b->off < from.off)) continue;
    /* the line before ends at a space, with room for part of this word */
    if (!ln->nbox || pl->boxes[ln->box0 + ln->nbox - 1].kind != BX_SPACE) continue;
    room = pc->w - ln->left - pl->p->pf.right - ln->width;
    if (room < pages_hyph_zone(d)) continue;
    t = pl->p->t + b->off;
    while (lead < b->len && (t[lead] == '"' || t[lead] == '(' || t[lead] == '\'')) lead++;
    for (core = lead; core < b->len && ((t[core] | 32) >= 'a' && (t[core] | 32) <= 'z'); core++) ;
    if (core - lead < 5 || core - lead > 38) continue;
    memset(brk, 0, 40);
    if (!hyph_word(t + lead, core - lead, brk)) continue;
    if (!(z = lay_face(L, &b->f))) continue;
    {
      /* the space at the line's end, then as much of the word as fits with a hyphen */
      i32 avail = room - (i32)(fonts_text(z, " ", 1) / QFINE) - (i32)(fonts_text(z, "-", 1) / QFINE);
      u32 best = 0;
      for (j = core - lead; j-- > 1; ) if (brk[j] && (i32)(fonts_text(z, t, lead + j) / QFINE) <= avail) { best = j; break; }
      if (!best) continue;
      at->para = pc->para; at->off = b->off + lead; *len = core - lead; *suggest = best;
      return 1;
    }
  }
  return 0;
}

/* ------------------------------------------------------------------ */
/* .docx */
static int num(Xml *x, const char *a, i32 *v)
{
  const char *s = xml_attr(x, a);
  char *e;
  long n;
  if (!s || !*s) return 0;
  n = strtol(s, &e, 10);
  if (e == s) return 0;
  *v = (i32)n;
  return 1;
}
static int onoff(Xml *x)
{
  const char *v = xml_attr(x, "val");
  return !v || !strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "on");
}
static u32 color(const char *s)
{
  u32 c = 0;
  int i;
  if (!s || strlen(s) != 6) return COLOR_AUTO;
  for (i = 0; i < 6; i++) {
    char ch = s[i];
    int v = ch >= '0' && ch <= '9' ? ch - '0' : (ch | 32) >= 'a' && (ch | 32) <= 'f' ? (ch | 32) - 'a' + 10 : -1;
    if (v < 0) return COLOR_AUTO;
    c = c << 4 | (u32)v;
  }
  return c;
}
/* a w:pgBorders (x at its START) into pb, read to its END */
static void read_pgborders(Xml *x, PageBorder *pb)
{
  const char *of = xml_attr(x, "offsetFrom"), *disp = xml_attr(x, "display"), *z = xml_attr(x, "zOrder");
  int d0 = x->depth, k, first = -1, same = 1;
  memset(pb, 0, sizeof *pb);
  pb->frompage = (u8)(of && !strcmp(of, "page"));
  pb->display = (u8)(disp && !strcmp(disp, "firstPage") ? PBD_FIRST : disp && !strcmp(disp, "notFirstPage") ? PBD_NOTFIRST : PBD_ALL);
  pb->back = (u8)(z && !strcmp(z, "back"));
  if (!x->pending_end)
    while (xml_next(x) != XT_EOF) {
      const char *v, *n = x->name;
      i32 t;
      PBSide *s;
      if (x->type == XT_END && x->depth < d0) break;
      if (x->type != XT_START) continue;
      k = !strcmp(n, "top") ? PBS_TOP : !strcmp(n, "left") || !strcmp(n, "start") ? PBS_LEFT : !strcmp(n, "bottom") ? PBS_BOTTOM :
          !strcmp(n, "right") || !strcmp(n, "end") ? PBS_RIGHT : -1;
      v = xml_attr(x, "val");
      if (k < 0 || !v || !strcmp(v, "none") || !strcmp(v, "nil")) { xml_skip(x); continue; }
      s = &pb->side[k];
      s->style = (u8)pages_pb_style(v);
      strncpy(s->val, v, sizeof s->val - 1);
      s->width = (u8)(num(x, "sz", &t) && t > 0 ? (t > 255 ? 255 : t) : 4);
      s->space = num(x, "space", &t) && t >= 0 && t <= 31 ? t * 20 : (pb->frompage ? 480 : 80);
      s->color = color(xml_attr(x, "color"));
      s->shadow = (u8)((v = xml_attr(x, "shadow")) && (!strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "on")));
      s->frame = (u8)((v = xml_attr(x, "frame")) && (!strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "on")));
      xml_skip(x);
    }
  else xml_skip(x);
  /* the old fields: the first side's; sides all alike in a kind they name need no side[] */
  for (k = 0; k < 4; k++) {
    PBSide *s = &pb->side[k];
    if (s->style == PB_NONE) { same = 0; continue; }
    if (first < 0) { first = k; pb->style = s->style; pb->width = s->width; pb->color = s->color; pb->space = s->space; }
    else if (s->width != pb->side[first].width || s->color != pb->side[first].color || s->space != pb->side[first].space || strcmp(s->val, pb->side[first].val)) same = 0;
    if (s->shadow || s->frame) same = 0;
  }
  if (first < 0) { memset(pb->side, 0, sizeof pb->side); pb->style = PB_NONE; return; }
  pb->own = (u8)!(same && !strcmp(pb->side[first].val, pb_name(pb->side[first].style)));
  if (!pb->own) memset(pb->side, 0, sizeof pb->side);
}
int pages_read_sect(Xml *x, Doc *d, SectFmt *sf, PageSetup *pg, int body)
{
  const char *n = x->name;
  i32 k;
  if (!strcmp(n, "cols")) {
    const char *sep = xml_attr(x, "sep"), *eq = xml_attr(x, "equalWidth");
    int d0 = x->depth, nc = 0;
    i32 w[MAX_SECT_COLS], sp[MAX_SECT_COLS];
    if (num(x, "num", &k) && k >= 1) sf->cols = (u8)(k > MAX_SECT_COLS ? MAX_SECT_COLS : k);
    if (num(x, "space", &k) && k >= 0 && k < 7200) sf->colgap = k;
    sf->colline = (u8)(sep && (!strcmp(sep, "1") || !strcmp(sep, "true") || !strcmp(sep, "on")));
    if (!x->pending_end)
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d0) break;
        if (x->type != XT_START) continue;
        if (!strcmp(x->name, "col") && nc < MAX_SECT_COLS) {
          w[nc] = num(x, "w", &k) && k > 0 ? k : 1440;
          sp[nc] = num(x, "space", &k) && k >= 0 ? k : 0;
          nc++;
        }
        xml_skip(x);
      }
    else xml_skip(x);
    if (eq && (!strcmp(eq, "0") || !strcmp(eq, "false") || !strcmp(eq, "off")) && nc > 1) {
      sf->cols = (u8)nc;
      pages_col_set(sf, nc, w, sp, 0, -1);
    }
    return 1;
  }
  if (!strcmp(n, "vAlign")) {
    const char *v = xml_attr(x, "val");
    if (v) sf->valign = (u8)(!strcmp(v, "center") ? VA_CENTER : !strcmp(v, "bottom") ? VA_BOTTOM : !strcmp(v, "both") ? VA_JUSTIFY : VA_TOP);
    xml_skip(x);
    return 1;
  }
  if (!strcmp(n, "pgBorders")) {
    PageBorder pb;
    read_pgborders(x, &pb);
    if (body) d->border = pb;
    else { sf->pb = pb; sf->pbown = 2; }              /* (2: read; pages_read_done sorts it out) */
    return 1;
  }
  if (!strcmp(n, "pgMar")) {
    if (num(x, "top", &k)) pg->top = k < 0 ? -k : k;
    if (num(x, "bottom", &k)) pg->bottom = k < 0 ? -k : k;
    if (num(x, "left", &k) || num(x, "start", &k)) pg->left = k;
    if (num(x, "right", &k) || num(x, "end", &k)) pg->right = k;
    if (num(x, "header", &k) && k >= 0) pg->header = k;
    if (num(x, "footer", &k) && k >= 0) pg->footer = k;
    if (num(x, "gutter", &k) && k >= 0 && k < 14400) pg->gutter = k;
    if (!body) sf->haspage = 1;
    xml_skip(x);
    return 1;
  }
  if (!strcmp(n, "rtlGutter")) { sf->rtlgutter = (u8)onoff(x); xml_skip(x); return 1; }
  return 0;
}
void pages_read_setting(Doc *d, Xml *x)
{
  const char *n = x->name;
  i32 k;
  if (!strcmp(n, "mirrorMargins")) d->mirror = (u8)onoff(x);
  else if (!strcmp(n, "printTwoOnOne")) d->twoonone = (u8)onoff(x);
  else if (!strcmp(n, "bookFoldPrinting")) { if (onoff(x)) d->bookfold = d->bookfold ? d->bookfold : 1; }
  else if (!strcmp(n, "bookFoldRevPrinting")) { if (onoff(x)) d->bookfold = 2; }
  else if (!strcmp(n, "bookFoldPrintingSheets")) { if (num(x, "val", &k) && k >= 0 && k < 32767) d->bfsheets = (u16)k; }
  else if (!strcmp(n, "gutterAtTop")) d->gutterattop = (u8)onoff(x);
  else if (!strcmp(n, "doNotHyphenateCaps")) d->hynocaps = (u8)onoff(x);
  else if (!strcmp(n, "consecutiveHyphenLimit")) { if (num(x, "val", &k) && k >= 0 && k < 256) d->hylimit = (u8)k; }
  else if (!strcmp(n, "hyphenationZone")) { if (num(x, "val", &k) && k > 0 && k < 14400) d->hyzone = k; }
  else if (!strcmp(n, "bordersDoNotSurroundHeader")) d->pbnohdr = (u8)onoff(x);
  else if (!strcmp(n, "bordersDoNotSurroundFooter")) d->pbnoftr = (u8)onoff(x);
}
/* sections' borders as read: one that is the document's needs none of its own; a section without a
   w:pgBorders has none (where the document has one) */
void pages_read_done(Doc *d)
{
  int k, lastk = d->np ? (int)d->p[d->np - 1]->pf.sect - 1 : -1;
  for (k = 0; k < d->nsects; k++) {
    SectFmt *s = &d->sects[k];
    if (k == lastk) s->pbown = 0;           /* (the last section's is the document's) */
    else if (s->pbown == 2) s->pbown = (u8)(memcmp(&s->pb, &d->border, sizeof s->pb) != 0);
    else if (d->border.style != PB_NONE) { memset(&s->pb, 0, sizeof s->pb); s->pbown = 1; }
    else s->pbown = 0;
  }
}
void pages_write_pgmar(Buf *o, const PageSetup *pg)
{
  buf_printf(o, "<w:pgMar w:top=\"%ld\" w:right=\"%ld\" w:bottom=\"%ld\" w:left=\"%ld\" w:header=\"%ld\" w:footer=\"%ld\" w:gutter=\"%ld\"/>",
             (long)pg->top, (long)pg->right, (long)pg->bottom, (long)pg->left, (long)pg->header, (long)pg->footer, (long)(pg->gutter > 0 ? pg->gutter : 0));
}
void pages_write_pgborders(Buf *o, const Doc *d, const SectFmt *s, int last)
{
  static const char *const sides[4] = { "top", "left", "bottom", "right" };
  const PageBorder *pb = pages_sect_border(d, s, last);
  int k;
  if (pb->style == PB_NONE) return;
  buf_printf(o, "<w:pgBorders w:offsetFrom=\"%s\"", pb->frompage ? "page" : "text");
  if (pb->display == PBD_FIRST) buf_adds(o, " w:display=\"firstPage\"");
  else if (pb->display == PBD_NOTFIRST) buf_adds(o, " w:display=\"notFirstPage\"");
  if (pb->back) buf_adds(o, " w:zOrder=\"back\"");
  buf_adds(o, ">");
  for (k = 0; k < 4; k++) {
    PBSide sd;
    char col[8];
    long sp;
    pages_pb_side(pb, k, &sd);
    if (sd.style == PB_NONE) continue;
    sp = sd.space / 20;
    if (sd.color == COLOR_AUTO) strcpy(col, "auto"); else sprintf(col, "%06lX", (unsigned long)(sd.color & 0xFFFFFF));
    buf_printf(o, "<w:%s w:val=\"", sides[k]);
    xml_escape(o, sd.val, strlen(sd.val));
    buf_printf(o, "\" w:sz=\"%u\" w:space=\"%ld\" w:color=\"%s\"%s%s/>", (unsigned)sd.width, sp > 31 ? 31L : sp < 0 ? 0L : sp, col,
               sd.shadow ? " w:shadow=\"1\"" : "", sd.frame ? " w:frame=\"1\"" : "");
  }
  buf_adds(o, "</w:pgBorders>");
}
void pages_write_cols(Buf *o, const SectFmt *s)
{
  int k, n = s->cols < 1 ? 1 : s->cols;
  if (n > 1 && s->colunequal) {
    buf_printf(o, "<w:cols w:num=\"%u\" w:space=\"%ld\" w:equalWidth=\"0\"%s>", (unsigned)n, (long)s->colgap, s->colline ? " w:sep=\"1\"" : "");
    for (k = 0; k < n; k++) {
      buf_printf(o, "<w:col w:w=\"%ld\"", (long)s->colw[k]);
      if (k + 1 < n) buf_printf(o, " w:space=\"%ld\"", (long)s->colsp[k]);
      buf_adds(o, "/>");
    }
    buf_adds(o, "</w:cols>");
    return;
  }
  buf_printf(o, "<w:cols w:num=\"%u\" w:space=\"%ld\"%s/>", (unsigned)n, (long)s->colgap, s->colline && n > 1 ? " w:sep=\"1\"" : "");
}
void pages_write_valign(Buf *o, const SectFmt *s)
{
  static const char *const va[4] = { "top", "center", "bottom", "both" };
  if (s->valign != VA_TOP) buf_printf(o, "<w:vAlign w:val=\"%s\"/>", va[s->valign & 3]);
}
void pages_write_sect_tail(Buf *o, const SectFmt *s)
{
  if (s->rtlgutter) buf_adds(o, "<w:rtlGutter/>");
}
void pages_write_settings(const Doc *d, Buf *b, int where)
{
  if (where == 10) {                         /* (docx.c calls this itself, before the packages' slot 1) */
    if (d->mirror) buf_adds(b, "<w:mirrorMargins/>");
    if (d->pbnohdr) buf_adds(b, "<w:bordersDoNotSurroundHeader/>");
    if (d->pbnoftr) buf_adds(b, "<w:bordersDoNotSurroundFooter/>");
    if (d->gutterattop) buf_adds(b, "<w:gutterAtTop/>");
  } else if (where == 2) {
    if (d->hylimit) buf_printf(b, "<w:consecutiveHyphenLimit w:val=\"%u\"/>", (unsigned)d->hylimit);
    if (d->hyzone > 0) buf_printf(b, "<w:hyphenationZone w:val=\"%ld\"/>", (long)d->hyzone);
    if (d->hynocaps) buf_adds(b, "<w:doNotHyphenateCaps/>");
  } else if (where == 4) {
    if (d->bookfold == 2) buf_adds(b, "<w:bookFoldRevPrinting/>");
    else if (d->bookfold) buf_adds(b, "<w:bookFoldPrinting/>");
    if (d->bookfold && d->bfsheets) buf_printf(b, "<w:bookFoldPrintingSheets w:val=\"%u\"/>", (unsigned)d->bfsheets);
    if (d->twoonone) buf_adds(b, "<w:printTwoOnOne/>");
  }
}
const char *pages_wm_part(const Doc *d, const char **ctype)
{
  if (!d->wm.pic) return "";
  if (ctype) *ctype = d->wm.pictype == IMG_PNG ? "image/png" : "image/jpeg";
  return d->wm.pictype == IMG_PNG ? "word/media/wmark1.png" : "word/media/wmark1.jpeg";
}
/* Word's picture watermark: a VML picture (shape type 75) behind the text, centred on the margins */
void pages_write_wm_picture(Buf *o, const Doc *d)
{
  static int shapes = 3073;
  i32 x, y, w, h;
  pages_wm_box(d, &d->page, &x, &y, &w, &h);
  buf_adds(o, "<w:r><w:rPr><w:noProof/></w:rPr><w:pict>"
    "<v:shapetype id=\"_x0000_t75\" coordsize=\"21600,21600\" o:spt=\"75\" o:preferrelative=\"t\" path=\"m@4@5l@4@11@9@11@9@5xe\" filled=\"f\" stroked=\"f\">"
    "<v:stroke joinstyle=\"miter\"/><v:formulas><v:f eqn=\"if lineDrawn pixelLineWidth 0\"/><v:f eqn=\"sum @0 1 0\"/><v:f eqn=\"sum 0 0 @1\"/>"
    "<v:f eqn=\"prod @2 1 2\"/><v:f eqn=\"prod @3 21600 pixelWidth\"/><v:f eqn=\"prod @3 21600 pixelHeight\"/><v:f eqn=\"sum @0 0 1\"/>"
    "<v:f eqn=\"prod @6 1 2\"/><v:f eqn=\"prod @7 21600 pixelWidth\"/><v:f eqn=\"sum @8 21600 0\"/><v:f eqn=\"prod @7 21600 pixelHeight\"/>"
    "<v:f eqn=\"sum @10 21600 0\"/></v:formulas><v:path o:extrusionok=\"f\" gradientshapeok=\"t\" o:connecttype=\"rect\"/>"
    "<o:lock v:ext=\"edit\" aspectratio=\"t\"/></v:shapetype>");
  buf_printf(o, "<v:shape id=\"WordPictureWatermark%d\" o:spid=\"_x0000_s%d\" type=\"#_x0000_t75\" style=\"position:absolute;margin-left:0;margin-top:0;"
                "width:%ld.%02ldpt;height:%ld.%02ldpt;z-index:-251657216;mso-position-horizontal:center;mso-position-horizontal-relative:margin;"
                "mso-position-vertical:center;mso-position-vertical-relative:margin\" o:allowincell=\"f\">"
                "<v:imagedata r:id=\"" PAGES_WM_RELID "\" o:title=\"\"%s/></v:shape></w:pict></w:r>",
             shapes, shapes, (long)(w / 20), (long)(w % 20 * 5), (long)(h / 20), (long)(h % 20 * 5),
             d->wm.washout ? " gain=\"19661f\" blacklevel=\"22938f\"" : "");
  shapes++;
}
