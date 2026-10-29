/*
 * objtext.c - see objtext.h (WP18): text direction, anchoring, insets and linked text boxes.
 */
#include <stdlib.h>
#include <string.h>
#include "objtext.h"

i32 tb_inset(const Image *im, int s)
{
  static const i32 dflt[4] = { TB_INSET_X, TB_INSET_Y, TB_INSET_X, TB_INSET_Y };
  if (s < 0 || s > 3) return 0;
  return im->fx.hasins ? im->fx.ins[s] : dflt[s];
}
i32 tb_text_width(const Image *im, i32 cx, i32 cy)
{
  i32 w = im->fx.vert ? cy - tb_inset(im, 1) - tb_inset(im, 3) : cx - tb_inset(im, 0) - tb_inset(im, 2);
  return w < 360 ? 360 : w;
}
/* whether Doc.images[k] is referred to by the text (the body, headers and footers, text boxes) */
static int in_story(const Para *const *pp, u32 np, int k)
{
  u32 i, j;
  for (i = 0; i < np; i++)
    for (j = 0; j + 4 <= pp[i]->len; j++)
      if (IS_PIC(pp[i]->t + j)) { if (pic_index(pp[i]->t + j) == k) return 1; j += 3; }
  return 0;
}
static int referred(const Doc *d, int k)
{
  int w;
  if (in_story((const Para *const *)d->p, d->np, k)) return 1;
  for (w = 0; w < HF_N; w++) if (in_story((const Para *const *)d->hdr[w].p, d->hdr[w].np, k) || in_story((const Para *const *)d->ftr[w].p, d->ftr[w].np, k)) return 1;
  if (d->edit.kind && d->edit.p && in_story((const Para *const *)d->edit.p, d->edit.np, k)) return 1;     /* (the body put aside) */
  return 0;
}
int tb_chain(const Doc *d, int link, int *out, int max)
{
  int k, n = 0, i, j;
  if (!link) return 0;
  /* the newest entry for each place in the chain that the text refers to */
  for (k = d->nimages - 1; k >= 0 && n < max; k--) {
    const Image *im = &d->images[k];
    if (im->fx.link != link || !(im->kind == OBJ_TEXTBOX || im->kind == OBJ_SHAPE)) continue;
    for (i = 0; i < n && d->images[out[i]].fx.seq != im->fx.seq; i++) ;
    if (i < n || !referred(d, k)) continue;
    out[n++] = k;
  }
  /* in order of their places */
  for (i = 1; i < n; i++) for (j = i; j > 0 && d->images[out[j - 1]].fx.seq > d->images[out[j]].fx.seq; j--) { int t = out[j]; out[j] = out[j - 1]; out[j - 1] = t; }
  return n;
}
int tb_new_link(const Doc *d)
{
  int k, m = 0;
  for (k = 0; k < d->nimages; k++) if (d->images[k].fx.link > m) m = d->images[k].fx.link;
  return m + 1;
}
int tb_head(const Doc *d, int k)
{
  static const Doc *cd;
  static u32 cver;
  static int ck = -1, cr = -1;
  int ch[64], n;
  if (k < 0 || k >= d->nimages || !d->images[k].fx.link || !d->images[k].fx.seq) return k;
  if (cd == d && cver == d->ver && ck == k) return cr;
  n = tb_chain(d, d->images[k].fx.link, ch, 64);
  cd = d; cver = d->ver; ck = k;
  cr = n && d->images[ch[0]].fx.seq == 0 ? ch[0] : k;
  return cr;
}
/* the height of laid-out text: the end of its last line (and paragraph spacing) */
static i32 text_height(const Furn *f, i32 top)
{
  i32 h = top;
  u32 j, l;
  for (j = 0; j < f->n; j++) {
    i32 y = f->f[j].y;
    for (l = 0; l < f->f[j].pl.nlines; l++) y += f->f[j].pl.lines[l].height;
    if (f->f[j].pl.nlines && y + f->f[j].p->pf.after > h) h = y + f->f[j].p->pf.after;
  }
  return h;
}
int tb_layout(Layout *L, const Image *im, i32 cx, Furn *f, TbPlain plain)
{
  const Doc *d = L->d;
  const Image *head = im;
  i32 bw = cx > 0 ? cx : im->cx, bh = im->cx ? (i32)((long long)im->cy * bw / im->cx) : im->cy, inner, w, top = tb_inset(im, 1);
  int k = d->images && im >= d->images && im < d->images + d->nimages ? (int)(im - d->images) : -1;
  int ok;
  memset(f, 0, sizeof *f);
  /* a linked box: the chain's text, at the first box's width */
  if (k >= 0 && im->fx.link && im->fx.seq) {
    int h = tb_head(d, k);
    if (h != k) head = &d->images[h];
  }
  w = tb_text_width(head, head == im ? bw : head->cx, head == im ? bh : head->cy);
  if (!(ok = plain(L, head, w, f))) return 0;
  inner = (im->fx.vert ? bw : bh) - tb_inset(im, 1) - tb_inset(im, 3);
  if (k >= 0 && im->fx.link) {
    /* the chain's boxes before this one take the lines that fit them; this one the next */
    int ch[64], n = tb_chain(d, im->fx.link, ch, 64), i;
    i32 from = top, to = top, y0 = top;
    for (i = 0; i < n; i++) {
      const Image *b = &d->images[ch[i]];
      i32 room = (b->fx.vert ? b->cx : b->cy) - tb_inset(b, 1) - tb_inset(b, 3);
      if (b->fx.seq > im->fx.seq) break;
      from = to;
      /* whole lines up to room */
      {
        u32 j, l;
        i32 lim = from + room, end = from;
        for (j = 0; j < f->n; j++) {
          i32 y = f->f[j].y;
          for (l = 0; l < f->f[j].pl.nlines; l++) {
            i32 hgt = f->f[j].pl.lines[l].height;
            if (y >= from && y + hgt <= lim) end = y + hgt;
            else if (y >= from && y + hgt > lim) { j = f->n; break; }
            y += hgt;
          }
        }
        to = end > from ? end : lim;
      }
      if (b->fx.seq == im->fx.seq) break;
    }
    if (im->fx.seq || n > 1) {
      /* keep the lines in from..to, moved up to the top inset */
      u32 j, l;
      for (j = 0; j < f->n; j++) {
        ParaLay *pl = &f->f[j].pl;
        i32 y = f->f[j].y, first = -1, nk = 0;
        Line *keep = pl->lines;
        for (l = 0; l < pl->nlines; l++) {
          i32 hgt = pl->lines[l].height;
          if (y >= from && y + hgt <= to) { if (first < 0) first = y; keep[nk++] = pl->lines[l]; }
          y += hgt;
        }
        pl->nlines = (u32)nk;
        f->f[j].y = first >= 0 ? first - from + y0 : -100000;
      }
    }
  }
  /* anchored in the middle or at the bottom */
  if (im->fx.anchor) {
    i32 used = text_height(f, top) - top, shift = inner - used;
    u32 j;
    if (im->fx.anchor == TA_MIDDLE) shift /= 2;
    if (shift > 0) for (j = 0; j < f->n; j++) f->f[j].y += shift;
  }
  return ok;
}
