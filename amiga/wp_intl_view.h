/*
 * wp_intl_view.h - WP27 on the page view, included by view.c (it uses view.c's Data, tx, pen_for,
 * screen_font): text the Amiga's Latin-1 fonts can't show (Hebrew, Arabic, CJK...) drawn glyph by glyph
 * from the TrueType face layout measured it with (core/ttraster.c, the faces' outlines as math uses
 * them); right-to-left and shaped text in its visual order; two lines in one; phonetic guides and
 * emphasis marks; selections in mixed-direction lines box by box; the cursor keys moving by what the
 * eye sees in right-to-left text. A face without a glyph draws its .notdef (an empty box).
 */
#include "../core/bidi.h"
#include "../core/intl.h"
#include "../core/math.h"
#include "../core/ttraster.h"

/* glyphs as pixels (coverage at least half), made once and kept for the session (the last NIG) */
#define NIG 384
typedef struct { Face *f; u16 g; LONG ppem; TTBitmap bm; } IntlGlyph;
static IntlGlyph ig[NIG];
static int nig, ignext;
static Face *ighinted[16]; static int nighinted;
static TTBitmap *intl_glyph(Face *f, u16 g, LONG ppem64)
{
  int i;
  TTRaster *r;
  IntlGlyph *p;
  for (i = 0; i < nig; i++) if (ig[i].f == f && ig[i].g == g && ig[i].ppem == ppem64) return &ig[i].bm;
  if (!(r = math_raster(f))) return NULL;
  for (i = 0; i < nighinted && ighinted[i] != f; i++) ;
  if (i == nighinted && nighinted < 16) { ttr_hint(r, f->latin_glyph); ighinted[nighinted++] = f; }
  if (nig < NIG) p = &ig[nig++];
  else { p = &ig[ignext]; free(p->bm.cov); ignext = (ignext + 1) % NIG; }
  memset(p, 0, sizeof *p);
  p->f = f; p->g = g; p->ppem = ppem64;
  if (!ttr_glyph(r, g, ppem64, &p->bm)) memset(&p->bm, 0, sizeof p->bm);
  else ttr_dropout(&p->bm, 128);
  return &p->bm;
}
static int wp27_turned;                   /* drawing a line of vertical text (turned a quarter clockwise after) */
/* one glyph of face f at ppem64 (26.6), its origin at x on baseline base, in the current pen; rot: turned a
   quarter anticlockwise round the middle of its em square adv pixels wide (upright once the line is turned) */
static void intl_put(struct RastPort *rp, Face *f, u32 cp, LONG ppem64, LONG x, LONG base, int bold, int rot, LONG adv)
{
  TTBitmap *bm;
  int row, c0;
  if (ppem64 < 64) return;
  if (ppem64 > 64 * 300) ppem64 = 64 * 300;
  if (!(bm = intl_glyph(f, ttf_glyph(f, cp), ppem64)) || !bm->cov) return;
  if (rot) {
    LONG cx = x + adv / 2, cy = base - ppem64 * 38 / 6400;
    for (row = 0; row < bm->h; row++)
      for (c0 = 0; c0 < bm->w; c0++)
        if (bm->cov[row * bm->w + c0] >= 128) {
          LONG gx = x + bm->left + c0, gy = base - bm->top + row;
          WritePixel(rp, cx + (gy - cy), cy - (gx - cx));
        }
    return;
  }
  for (row = 0; row < bm->h; row++) {
    const u8 *cv = bm->cov + row * bm->w;
    LONG yy = base - bm->top + row;
    for (c0 = 0; c0 < bm->w; ) {
      int c1;
      if (cv[c0] < 128) { c0++; continue; }
      for (c1 = c0; c1 + 1 < bm->w && cv[c1 + 1] >= 128; c1++) ;
      RectFill(rp, x + bm->left + c0, yy, x + bm->left + c1 + bold, yy);
      c0 = c1 + 1;
    }
  }
}
/* text (UTF-8, as shown: left to right) in format f from pixel x on baseline base, each character at its
   laid-out place; the ones Latin-1 has in the screen font, the others as TrueType glyphs */
static void intl_text(struct Data *d, Object *obj, struct RastPort *rp, const CharFmt *f, const char *s, u32 n, LONG x, LONG base)
{
  FaceSize *z = lay_face(d->lay, f);
  struct TextFont *tf = screen_font(d, f);
  LONG ppem64 = ((LONG)f->size * d->zoom * 8) / 25, acc = 0;
  u32 i = 0;
  int bold = (f->flags & CF_BOLD) && z && !z->face->bold;
  if (!z) return;
  if (f->flags & (CF_SUPER | CF_SUB)) { base += f->flags & CF_SUPER ? -tx(d, f->size * 10 / 3) : tx(d, f->size * 10 / 6); ppem64 = ppem64 * 2 / 3; }
  base -= tx(d, f->position * 10);
  SetFont(rp, tf ? tf : ((struct GfxBase *)GfxBase)->DefaultFont);
  SetSoftStyle(rp, (ULONG)((f->flags & CF_BOLD ? FSF_BOLD : 0) | (f->flags & CF_ITALIC ? FSF_ITALIC : 0)), FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
  while (i < n) {
    int used;
    u32 cp = utf8_get(s + i, n - i, &used);
    LONG px = x + tx(d, (i32)(acc / QFINE));
    i += (u32)used;
    if (cp == BIDI_GONE || bidi_invisible(cp)) continue;
    if (cp < 0x100 && cp >= 0x20) { char c = (char)cp; Move(rp, px, base); Text(rp, (STRPTR)&c, 1); }
    else intl_put(rp, z->face, cp, ppem64, px, base, bold, wp27_turned && bidi_wide(cp), tx(d, (i32)(fonts_fine(z, cp) / QFINE)));
    {
      long w = (long)fonts_fine(z, cp);
      if (f->scale && f->scale != 100) w = w * f->scale / 100;
      acc += w + (long)f->spacing * QFINE;
    }
  }
}
/* a box shows characters the screen font hasn't (beyond Latin-1, no stand-in) */
static int intl_foreign(const char *t, u32 off, u32 len)
{
  u32 o = off, end = off + len;
  while (o < end) {
    int used;
    u32 cp = utf8_get(t + o, end - o, &used);
    if (cp > 0xFF && cp < 0xF0000 && latin1_of(cp) < 0) return 1;
    o += (u32)used;
  }
  return 0;
}
/* box b (text t) drawn by WP27 at pixel x, baseline base: 1 if it was one of its boxes (right to left,
   shaped, beyond Latin-1, two lines in one) */
static int wp27_draw_box(struct Data *d, Object *obj, struct RastPort *rp, const Box *b, const char *t, LONG x, LONG base)
{
  char v[2048];
  u32 n;
  ULONG pen;
  if (b->kind != BX_TEXT || ENCLOSURE(&b->f)) return 0;
  if (!(b->f.eal & EAL_COMBINE) && !b->vis && !intl_foreign(t, b->off, b->len)) return 0;
  pen = (ULONG)pen_for(d, obj, b->f.color);
  SetAPen(rp, pen); SetDrMd(rp, JAM1);
  if (b->f.eal & EAL_COMBINE) {
    /* two lines in one: the first half over the second, at half size, in brackets */
    static const char *const br[EB_N][2] = { { "", "" }, { "(", ")" }, { "[", "]" }, { "<", ">" }, { "{", "}" } };
    CharFmt hf = b->f;
    u32 k = 0, cnt = 0, o, mid, end = b->off + b->len;
    LONG sz = tx(d, b->f.size * 10), bw = 0;
    int brk = EAL_BRACKETS(&b->f);
    for (o = b->off; o < end; o = (u32)utf8_next(t, end, o)) cnt++;
    for (o = b->off; o < end && k < (cnt + 1) / 2; o = (u32)utf8_next(t, end, o)) k++;
    mid = o;
    hf.size = (u16)((b->f.size + 1) / 2); hf.eal = 0;
    if (brk && brk < EB_N) {
      FaceSize *z = lay_face(d->lay, &b->f);
      bw = z ? tx(d, (i32)(fonts_text(z, br[brk][0], 1) / QFINE)) : 0;
      intl_text(d, obj, rp, &b->f, br[brk][0], 1, x, base);
      intl_text(d, obj, rp, &b->f, br[brk][1], 1, x + tx(d, b->w) - bw, base);
    }
    intl_text(d, obj, rp, &hf, t + b->off, mid - b->off, x + bw, base - sz * 2 / 5);
    intl_text(d, obj, rp, &hf, t + mid, end - mid, x + bw, base + sz / 10);
    return 1;
  }
  if (b->vis && (n = lay_visual(d->lay, b, t, v, sizeof v))) intl_text(d, obj, rp, &b->f, v, n, x, base);
  else intl_text(d, obj, rp, &b->f, t + b->off, b->len, x, base);
  if (b->f.flags & (CF_UNDERLINE | CF_STRIKE)) {
    LONG w = tx(d, b->w);
    if (b->f.flags & CF_UNDERLINE) { Move(rp, x, base + 1 + d->zoom / 150); Draw(rp, x + w - 1, base + 1 + d->zoom / 150); }
    if (b->f.flags & CF_STRIKE) { LONG sy = base - tx(d, b->f.size * 10 / 4); Move(rp, x, sy); Draw(rp, x + w - 1, sy); }
  }
  return 1;
}
/* after a line's boxes: phonetic guides centred over their base text, emphasis marks; lx the line's
   left, base its baseline (pixels) */
static void wp27_draw_marks(struct Data *d, Object *obj, struct RastPort *rp, ParaLay *pl, Line *ln, LONG lx, LONG base)
{
  u32 k, e;
  for (k = ln->box0; k < ln->box0 + ln->nbox; k = e) {
    const Box *b = &pl->boxes[k];
    const Ruby *r = b->f.ruby ? intl_ruby(d->doc, b->f.ruby) : NULL;
    i32 x0 = b->x, x1 = b->x + b->w;
    e = k + 1;
    if (b->f.em && b->kind == BX_TEXT) {
      u32 o, end = b->off + b->len;
      LONG rr = tx(d, b->f.size * 10 / 9) + 1;
      SetAPen(rp, pen_for(d, obj, b->f.color));
      for (o = b->off; o < end; o = (u32)utf8_next(pl->p->t, end, o)) {
        u32 nx = (u32)utf8_next(pl->p->t, end, o);
        LONG cx = lx + tx(d, (lay_text_x(d->lay, b, pl->p->t, o) + lay_text_x(d->lay, b, pl->p->t, nx)) / 2);
        LONG cy = b->f.em == EM_UNDERDOT ? base + tx(d, b->f.size * 10 / 4) : base - tx(d, b->f.size * 10) - rr;
        if (pl->p->t[o] == ' ') continue;
        if (b->f.em == EM_CIRCLE) DrawEllipse(rp, cx, cy, rr, rr);
        else RectFill(rp, cx - rr / 2, cy - rr / 2, cx + rr / 2, cy + rr / 2);
      }
    }
    if (!r) continue;
    while (e < ln->box0 + ln->nbox && pl->boxes[e].f.ruby == b->f.ruby) {
      if (pl->boxes[e].x < x0) x0 = pl->boxes[e].x;
      if (pl->boxes[e].x + pl->boxes[e].w > x1) x1 = pl->boxes[e].x + pl->boxes[e].w;
      e++;
    }
    {
      CharFmt rf;
      FaceSize *rz;
      i32 tw, gx;
      u32 len = (u32)strlen(r->text);
      lay_ruby_look(d->lay, b, r, &rf);
      if (!(rz = lay_face(d->lay, &rf)) || !len) continue;
      tw = (i32)(fonts_text(rz, r->text, len) / QFINE);
      gx = r->align == RA_LEFT ? x0 : r->align == RA_RIGHT ? x1 - tw : (x0 + x1 - tw) / 2;
      SetAPen(rp, pen_for(d, obj, b->f.color));
      if ((r->align == RA_DLETTER || r->align == RA_DSPACE) && tw < x1 - x0) {
        /* distributed: the guide's characters spread over the base */
        u32 o, nc = 0;
        i32 at, gap;
        for (o = 0; o < len; o = (u32)utf8_next(r->text, len, o)) nc++;
        gap = r->align == RA_DSPACE || nc < 2 ? (x1 - x0 - tw) / (i32)nc : (x1 - x0 - tw) / (i32)(nc - 1);
        at = x0 + (r->align == RA_DSPACE || nc < 2 ? gap / 2 : 0);
        for (o = 0; o < len; ) {
          u32 nx = (u32)utf8_next(r->text, len, o);
          intl_text(d, obj, rp, &rf, r->text + o, nx - o, lx + tx(d, at), base - tx(d, r->raise ? r->raise * 10 : b->f.size * 10));
          at += (i32)(fonts_text(rz, r->text + o, nx - o) / QFINE) + gap; o = nx;
        }
      } else intl_text(d, obj, rp, &rf, r->text, len, lx + tx(d, gx), base - tx(d, r->raise ? r->raise * 10 : b->f.size * 10));
    }
  }
}
/* the selection s..e (bytes) on a mixed-direction line, box by box (each piece where its characters
   are); 1 if the line was one */
static int wp27_draw_sel(struct Data *d, struct RastPort *rp, ParaLay *pl, Line *ln, LONG lx, LONG top, u32 s, u32 e)
{
  u32 k;
  if (!pl->rtl) {
    for (k = ln->box0; k < ln->box0 + ln->nbox && !pl->boxes[k].lvl; k++) ;
    if (k == ln->box0 + ln->nbox) return 0;
  }
  SetAPen(rp, d->pen_sel);
  for (k = ln->box0; k < ln->box0 + ln->nbox; k++) {
    const Box *b = &pl->boxes[k];
    u32 a = s > b->off ? s : b->off, z = e < (u32)(b->off + b->len) ? e : (u32)(b->off + b->len);
    LONG x0, x1, t;
    if (a >= z && !(b->len == 0 && s <= b->off && e > b->off)) continue;
    x0 = lx + tx(d, lay_text_x(d->lay, b, pl->p->t, a)); x1 = lx + tx(d, lay_text_x(d->lay, b, pl->p->t, z));
    if (x1 < x0) { t = x0; x0 = x1; x1 = t; }
    if (x1 > x0) RectFill(rp, x0, top, x1 - 1, top + tx(d, ln->height) - 1);
  }
  return 1;
}
/* the cursor keys in right-to-left text: Left goes on through the text (to the left on the screen), Right
   back; the key code to act on */
static UWORD wp27_arrow(struct Data *d, UWORD code)
{
  ParaLay *pl;
  u32 k, li, off;
  int rtl;
  if ((code != 0x4E && code != 0x4F) || !d->lay || d->caret.para >= d->lay->npl) return code;
  pl = &d->lay->pl[d->caret.para];
  if (!pl->nlines) return code;
  rtl = pl->rtl & 1;
  off = d->caret.off;
  for (li = 0; li + 1 < pl->nlines && off >= pl->lines[li + 1].start; li++) ;
  for (k = pl->lines[li].box0; k < pl->lines[li].box0 + pl->lines[li].nbox; k++) {
    const Box *b = &pl->boxes[k];
    if (b->kind == BX_TEXT && off >= b->off && off < (u32)(b->off + b->len)) { rtl = b->lvl & 1; break; }
  }
  return rtl ? (UWORD)(code == 0x4F ? 0x4E : 0x4F) : code;
}
