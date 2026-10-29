/*
 * pdf_intl.h - WP27 in PDFs, included by pdf.c (it uses pdf.c's text and colour): right-to-left and
 * shaped text as its glyphs in visual order, two lines in one, phonetic guides over their base text,
 * emphasis marks.
 */
#include "intl.h"
#include "bidi.h"

/* the text of box bx as two lines in one: its first half over its second, at half the size, in
   brackets if it has them */
static void wp27_pdf_combine(Pdf *p, Buf *b, ParaLay *pl, const Box *bx, i32 lx, i32 base)
{
  static const char *const br[EB_N][2] = { { "", "" }, { "(", ")" }, { "[", "]" }, { "<", ">" }, { "{", "}" } };
  const char *t = pl->p->t;
  CharFmt hf = bx->f;
  FaceSize *z = lay_face(p->L, &bx->f), *hz;
  u32 n = 0, k = 0, o, mid, end = bx->off + bx->len;
  i32 size = bx->f.size * 10, x = lx + bx->x, bw = 0, w1, w2;
  int brk = EAL_BRACKETS(&bx->f);
  for (o = bx->off; o < end; o = (u32)utf8_next(t, end, o)) n++;
  for (o = bx->off; o < end && k < (n + 1) / 2; o = (u32)utf8_next(t, end, o)) k++;
  mid = o;
  hf.size = (u16)((bx->f.size + 1) / 2);
  hf.eal = 0;
  if (!z || !(hz = lay_face(p->L, &hf))) return;
  if (brk && brk < EB_N) {
    bw = (i32)(fonts_text(z, br[brk][0], 1) / QFINE);
    text(p, b, &bx->f, br[brk][0], 1, x, base, bw);
    text(p, b, &bx->f, br[brk][1], 1, x + bx->w - (i32)(fonts_text(z, br[brk][1], 1) / QFINE), base, (i32)(fonts_text(z, br[brk][1], 1) / QFINE));
  }
  w1 = (i32)(fonts_text(hz, t + bx->off, mid - bx->off) / QFINE);
  w2 = (i32)(fonts_text(hz, t + mid, end - mid) / QFINE);
  text(p, b, &hf, t + bx->off, mid - bx->off, x + bw, base + size * 2 / 5, w1);
  text(p, b, &hf, t + mid, end - mid, x + bw, base - size / 10, w2);
}
static int wp27_vert;                     /* drawing a line of vertical text (in a frame turned a quarter clockwise) */
/* in vertical text: the box's wide (East Asian) characters each turned back upright round the middle of
   its em square, the others along the line; 1 if it had any */
static int wp27_pdf_upright(Pdf *p, Buf *b, ParaLay *pl, const Box *bx, i32 lx, i32 base)
{
  const char *t = pl->p->t;
  u32 o = bx->off, end = bx->off + bx->len;
  int any = 0;
  FaceSize *z = lay_face(p->L, &bx->f);
  i32 size = bx->f.size * 10;
  if (!z) return 0;
  for (o = bx->off; o < end; ) { int used; if (bidi_wide(utf8_get(t + o, end - o, &used))) { any = 1; break; } o += (u32)used; }
  if (!any) return 0;
  for (o = bx->off; o < end; ) {
    int used;
    u32 cp = utf8_get(t + o, end - o, &used);
    u32 nx = o + (u32)used;
    i32 x0 = lx + lay_text_x(p->L, bx, t, o), x1 = lx + lay_text_x(p->L, bx, t, nx);
    if (bidi_wide(cp)) {
      i32 cx = (x0 + x1) / 2, cy = base + size * 38 / 100;
      buf_printf(b, "q 0 1 -1 0 %ld %ld cm\n", (long)(cx + cy), (long)(cy - cx));
      text(p, b, &bx->f, t + o, (u32)used, x0, base, x1 - x0);
      buf_adds(b, "Q\n");
    } else text(p, b, &bx->f, t + o, (u32)used, x0, base, x1 - x0);
    o = nx;
  }
  return 1;
}
/* a text box WP27 draws itself: 1 if it was one */
static int wp27_pdf_box(Pdf *p, Buf *b, ParaLay *pl, const Box *bx, i32 lx, i32 base)
{
  char v[2048];
  u32 n;
  if (bx->kind != BX_TEXT) return 0;
  if (bx->f.eal & EAL_COMBINE) { wp27_pdf_combine(p, b, pl, bx, lx, base); return 1; }
  if (wp27_vert && !bx->vis && wp27_pdf_upright(p, b, pl, bx, lx, base)) return 1;
  if (!bx->vis || !(n = lay_visual(p->L, bx, pl->p->t, v, sizeof v))) return 0;
  text(p, b, &bx->f, v, n, lx + bx->x, base, bx->w);
  return 1;
}
/* after a line's boxes: phonetic guides centred over their base text, emphasis marks over each character */
static void wp27_pdf_marks(Pdf *p, Buf *b, ParaLay *pl, Line *ln, i32 lx, i32 base)
{
  u32 k, e;
  for (k = ln->box0; k < ln->box0 + ln->nbox; k = e) {
    const Box *bx = &pl->boxes[k];
    const Ruby *r = bx->f.ruby ? intl_ruby(p->L->d, bx->f.ruby) : NULL;
    i32 x0 = bx->x, x1 = bx->x + bx->w;
    e = k + 1;
    if (bx->f.em && bx->kind == BX_TEXT) {
      /* a dot (comma, circle) over each character, or under it */
      u32 o;
      i32 size = bx->f.size * 10, rr = size / 9 + 4;
      colour(b, bx->f.color, "rg");
      colour(b, bx->f.color, "RG");
      for (o = bx->off; o < (u32)(bx->off + bx->len); o = (u32)utf8_next(pl->p->t, bx->off + bx->len, o)) {
        u32 nx = (u32)utf8_next(pl->p->t, bx->off + bx->len, o);
        i32 cx = lx + (lay_text_x(p->L, bx, pl->p->t, o) + lay_text_x(p->L, bx, pl->p->t, nx)) / 2;
        i32 cy = bx->f.em == EM_UNDERDOT ? base - size / 4 : base + size + rr;
        if (pl->p->t[o] == ' ') continue;
        if (bx->f.em == EM_CIRCLE) buf_printf(b, "%ld w %ld %ld %ld %ld re S\n", (long)(size / 60 + 2), (long)(cx - rr), (long)(cy - rr), (long)(2 * rr), (long)(2 * rr));
        else if (bx->f.em == EM_COMMA) buf_printf(b, "%ld %ld m %ld %ld l %ld %ld l h f\n", (long)(cx - rr / 2), (long)(cy + rr), (long)(cx + rr / 2), (long)(cy + rr), (long)(cx - rr), (long)(cy - rr));
        else buf_printf(b, "%ld %ld %ld %ld re f\n", (long)(cx - rr / 2), (long)(cy - rr / 2), (long)rr, (long)rr);
      }
    }
    if (!r) continue;
    /* the guide's base: the boxes after this one with the same guide */
    while (e < ln->box0 + ln->nbox && pl->boxes[e].f.ruby == bx->f.ruby) {
      if (pl->boxes[e].x < x0) x0 = pl->boxes[e].x;
      if (pl->boxes[e].x + pl->boxes[e].w > x1) x1 = pl->boxes[e].x + pl->boxes[e].w;
      e++;
    }
    {
      CharFmt rf;
      FaceSize *rz;
      i32 tw, gx, gy;
      u32 len = (u32)strlen(r->text);
      lay_ruby_look(p->L, bx, r, &rf);
      if (!(rz = lay_face(p->L, &rf)) || !len) continue;
      tw = (i32)(fonts_text(rz, r->text, len) / QFINE);
      gx = r->align == RA_LEFT ? x0 : r->align == RA_RIGHT ? x1 - tw : (x0 + x1 - tw) / 2;
      gy = base + (r->raise ? r->raise * 10 : bx->f.size * 10);
      if ((r->align == RA_DLETTER || r->align == RA_DSPACE) && tw < x1 - x0) {
        /* distributed: the guide's characters spread over the base (with space: half a gap at each end) */
        u32 o = 0, nc = 0, k2 = 0;
        i32 at, gap;
        for (o = 0; o < len; o = (u32)utf8_next(r->text, len, o)) nc++;
        gap = r->align == RA_DSPACE || nc < 2 ? (x1 - x0 - tw) / (i32)nc : (x1 - x0 - tw) / (i32)(nc - 1);
        at = x0 + (r->align == RA_DSPACE || nc < 2 ? gap / 2 : 0);
        for (o = 0; o < len; k2++) {
          u32 nx = (u32)utf8_next(r->text, len, o);
          i32 cw = (i32)(fonts_text(rz, r->text + o, nx - o) / QFINE);
          text(p, b, &rf, r->text + o, nx - o, lx + at, gy, cw);
          at += cw + gap; o = nx;
        }
      } else text(p, b, &rf, r->text, len, lx + gx, gy, tw);
    }
  }
}
