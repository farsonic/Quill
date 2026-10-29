/* pdf.c - see pdf.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pdf.h"
#include "zip.h"
#include "docx.h"
#include "image.h"

/* Windows-1252's 0x80-0x9F (0: not used) */
static const u16 cp1252_hi[32] = {
  0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
  0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178 };
/* the code point of a Windows-1252 byte; 0 for the five unused codes */
static u32 code_cp(int c) { return c >= 0x80 && c < 0xA0 ? cp1252_hi[c - 0x80] : (u32)c; }
/* the Windows-1252 byte for a code point (the fonts' /WinAnsiEncoding), with a few look-alikes
   for characters it lacks; '?' for anything else */
static int cp_code(u32 cp)
{
  int k;
  if (cp < 0x80 || (cp >= 0xA0 && cp < 0x100)) return (int)cp;
  for (k = 0; k < 32; k++) if (cp1252_hi[k] && cp1252_hi[k] == cp) return 0x80 + k;
  if (cp == 0x2011) return '-';                      /* non-breaking hyphen */
  if (cp == 0x25AA || cp == 0x25BA) return 0x95;     /* other bullets */
  return '?';
}

typedef struct { Face *face; } PFont;
typedef struct {
  Layout *L;
  Buf o;
  u32 *off; u32 nobj, objcap;
  PFont fonts[64]; int nfonts;
  const Image *imgs[256]; int nimgs;      /* pictures used (each file once), as /Im0... */
  u32 page;                               /* the page being written */
  /* hyperlinks' clickable areas, in points from the page's bottom left (link annotations, written
     as objects of their own after the pages) */
  struct PLink { u32 page; i32 x0, y0, x1, y1; u16 link; } *links; int nlinks, linkcap;
} Pdf;
/* a link's area (twips, as the content stream draws) noted for the page being written; a box
   just right of the last area of the same link on the same line joins it */
static void link_area(Pdf *p, u16 link, i32 x0, i32 y0, i32 x1, i32 y1)
{
  struct PLink *l;
  const char *u = doc_link_url(p->L->d, link);
  if (!u[0] || u[0] == '#') return;           /* (places in the document: no bookmarks in Quill's PDFs yet) */
  x0 /= 20; y0 /= 20; x1 = (x1 + 19) / 20; y1 = (y1 + 19) / 20;
  if (p->nlinks) {
    l = &p->links[p->nlinks - 1];
    if (l->page == p->page && l->link == link && l->y0 == y0 && l->y1 == y1 && x0 <= l->x1 + 2 && x0 >= l->x0) { if (x1 > l->x1) l->x1 = x1; return; }
  }
  if (p->nlinks == p->linkcap) {
    int nc = p->linkcap ? p->linkcap * 2 : 16;
    struct PLink *nl = (struct PLink *)realloc(p->links, (size_t)nc * sizeof *nl);
    if (!nl) return;
    p->links = nl; p->linkcap = nc;
  }
  l = &p->links[p->nlinks++];
  l->page = p->page; l->link = link; l->x0 = x0; l->y0 = y0; l->x1 = x1; l->y1 = y1;
}
/* the /Im number for a picture, adding it on first use; pictures sharing the same data are
   written once. -1 if there are already 256 (the picture is then left out) */
static int img_ref(Pdf *p, const Image *im)
{
  int i;
  for (i = 0; i < p->nimgs; i++) if (p->imgs[i]->data == im->data) return i;
  if (p->nimgs >= 256) return -1;
  p->imgs[p->nimgs] = im;
  return p->nimgs++;
}

/* the /F number for a face, adding it on first use. Past 64 faces the text falls back to
   font 0 rather than going missing */
static int font_index(Pdf *p, Face *f)
{
  int i;
  for (i = 0; i < p->nfonts; i++) if (p->fonts[i].face == f) return i;
  if (p->nfonts >= 64) return 0;
  p->fonts[p->nfonts].face = f;
  return p->nfonts++;
}
/* begins object n, recording its byte offset for the xref table */
static void obj_start(Pdf *p, u32 n)
{
  if (n < p->objcap) p->off[n] = (u32)p->o.len;
  buf_printf(&p->o, "%lu 0 obj\n", (unsigned long)n);
}
/* a stream object, deflated */
static void stream(Pdf *p, u32 n, const char *dict, const u8 *data, size_t len)
{
  size_t clen = 0;
  u8 *c = zdeflate(data, len, &clen);
  obj_start(p, n);
  if (c) {
    /* zdeflate makes raw deflate; FlateDecode wants the zlib wrapper around it */
    u32 a = 1, b = 0;
    size_t i;
    for (i = 0; i < len; i++) { a = (a + data[i]) % 65521; b = (b + a) % 65521; }
    buf_printf(&p->o, "<<%s /Filter /FlateDecode /Length %lu>>\nstream\n", dict, (unsigned long)(clen + 6));
    buf_addc(&p->o, 0x78); buf_addc(&p->o, 0x01);
    buf_add(&p->o, c, clen);
    buf_addc(&p->o, (char)(b >> 8)); buf_addc(&p->o, (char)b); buf_addc(&p->o, (char)(a >> 8)); buf_addc(&p->o, (char)a);
    free(c);
  } else {
    buf_printf(&p->o, "<<%s /Length %lu>>\nstream\n", dict, (unsigned long)len);
    buf_add(&p->o, data, len);
  }
  buf_adds(&p->o, "\nendstream\nendobj\n");
}
/* a PDF literal string, escaped: brackets and backslashes get a backslash, bytes outside
   printable ASCII an octal escape */
static void pdf_string(Buf *b, const char *s)
{
  buf_addc(b, '(');
  for (; *s; s++) {
    u8 c = (u8)*s;
    if (c == '(' || c == ')' || c == '\\') { buf_addc(b, '\\'); buf_addc(b, (char)c); }
    else if (c < 32 || c > 126) buf_printf(b, "\\%03o", c);
    else buf_addc(b, (char)c);
  }
  buf_addc(b, ')');
}
/* sets a 0xRRGGBB colour with op ("rg" fill, "RG" stroke) as three decimals, without floating
   point; the automatic colour prints black */
static void colour(Buf *b, u32 c, const char *op)
{
  if (c == COLOR_AUTO) c = 0;
  buf_printf(b, "%d.%03d %d.%03d %d.%03d %s\n", (int)((c >> 16) & 255) / 255, (int)(((c >> 16) & 255) * 1000 / 255) % 1000,
             (int)((c >> 8) & 255) / 255, (int)(((c >> 8) & 255) * 1000 / 255) % 1000,
             (int)(c & 255) / 255, (int)((c & 255) * 1000 / 255) % 1000, op);
}
/* an underline or strike line of a kind (UL_) from x, w wide, at y (its middle), for text size twips */
static void deco_line(Buf *b, int kind, i32 x, i32 y, i32 w, i32 size)
{
  i32 t = size / 18 + 1;
  switch (kind) {
  case UL_DOUBLE: buf_printf(b, "%ld %ld %ld %ld re f %ld %ld %ld %ld re f\n", (long)x, (long)(y + t), (long)w, (long)t, (long)x, (long)(y - t * 2), (long)w, (long)t); break;
  case UL_THICK: buf_printf(b, "%ld %ld %ld %ld re f\n", (long)x, (long)(y - t), (long)w, (long)(t * 2)); break;
  case UL_DOTTED: case UL_DASHED:
    buf_printf(b, "%ld w [%ld %ld] 0 d %ld %ld m %ld %ld l S [] 0 d\n", (long)t, (long)(kind == UL_DOTTED ? t : t * 4), (long)(kind == UL_DOTTED ? t * 2 : t * 3),
               (long)x, (long)y, (long)(x + w), (long)y);
    break;
  case UL_WAVE: {
    i32 step = t * 3, i;
    buf_printf(b, "%ld w %ld %ld m", (long)t, (long)x, (long)y);
    for (i = step; i <= w; i += step) buf_printf(b, " %ld %ld l", (long)(x + i), (long)(y + ((i / step) & 1 ? t : -t)));
    buf_adds(b, " S\n");
    break; }
  default: buf_printf(b, "%ld %ld %ld %ld re f\n", (long)x, (long)(y - t / 2), (long)w, (long)t);
  }
}
/* one run of text (a box or a label) at x, baseline y (twips, PDF's y upwards) */
static void text(Pdf *p, Buf *b, const CharFmt *f, const char *s, u32 n, i32 x, i32 y, i32 w)
{
  FaceSize *z = lay_face(p->L, f), *zs = NULL;
  char enc[512];
  u8 low[512];
  u32 i = 0, k = 0, j;
  i32 size = f->size * 10, ssize;              /* twips */
  int small = (f->fx & FX_SMALLCAPS) && !(f->flags & CF_CAPS), synth_b, synth_i;
  if (!z) return;
  if (f->flags & (CF_SUPER | CF_SUB)) {
    if (f->flags & CF_SUPER) y += size / 3; else y -= size / 8;
    size = (size * 2 + 2) / 3;
  }
  y += f->position * 10;
  ssize = (size * 4 + 2) / 5;
  if (small) { CharFmt t = *f; t.size = (u16)((f->size * 4 + 2) / 5); zs = lay_face(p->L, &t); }
  while (i < n && k + 1 < sizeof enc) {
    int used;
    u32 cp = utf8_get(s + i, n - i, &used);
    low[k] = 0;
    if ((f->flags & CF_CAPS) && cp >= 'a' && cp <= 'z') cp -= 32;
    if (small && ((cp >= 'a' && cp <= 'z') || (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7))) { cp -= 32; low[k] = 1; }
    if (cp == '\t' || cp == '\v') cp = ' ';
    enc[k++] = (char)cp_code(cp);
    i += (u32)used;
  }
  enc[k] = 0;
  /* tracked changes as Word prints them: insertions underlined, deletions struck through, in colour */
  {
    static const u32 revc[] = { 0xC00000, 0x1F4FC8, 0x008040, 0x8A2BB0, 0xB85C00, 0x007A87 };   /* as the screen shows them */
    colour(b, f->rev ? revc[f->rid % 6] : f->color, "rg");
    if ((f->flags & (CF_UNDERLINE | CF_STRIKE)) || f->rev || (f->fx & FX_DSTRIKE)) colour(b, f->rev ? revc[f->rid % 6] : f->color, "RG");
  }
  /* a family without a bold or italic face: the regular one, stroked (bold) or slanted (italic) */
  synth_b = (f->flags & CF_BOLD) && !z->face->bold;
  synth_i = (f->flags & CF_ITALIC) && !z->face->italic;
  if (synth_b || synth_i) buf_adds(b, "q ");
  if (synth_b) { colour(b, f->rev ? 0xC00000 : f->color, "RG"); buf_printf(b, "%ld w ", (long)(size / 30 + 1)); }
  buf_printf(b, "BT /F%d %ld Tf ", font_index(p, z->face), (long)size);
  if (synth_b) buf_adds(b, "2 Tr ");
  if (f->spacing) buf_printf(b, "%ld Tc ", (long)f->spacing);
  if (f->scale && f->scale != 100) buf_printf(b, "%u Tz ", (unsigned)f->scale);
  if (synth_i) buf_printf(b, "1 0 0.21 1 %ld %ld Tm ", (long)x, (long)y);
  else buf_printf(b, "%ld %ld Td ", (long)x, (long)y);
  /* small capitals: the lower case runs smaller */
  for (j = 0; j < k; ) {
    u32 e = j;
    char seg[512];
    while (e < k && low[e] == low[j]) e++;
    memcpy(seg, enc + j, e - j); seg[e - j] = 0;
    if (small) buf_printf(b, "/F%d %ld Tf ", font_index(p, (low[j] && zs ? zs : z)->face), (long)(low[j] ? ssize : size));
    pdf_string(b, seg);
    buf_adds(b, " Tj ");
    j = e;
  }
  buf_adds(b, "ET\n");
  if (synth_b || synth_i) buf_adds(b, "Q\n");
  if (f->spacing || (f->scale && f->scale != 100)) buf_adds(b, "BT 0 Tc 100 Tz ET\n");
  if ((f->flags & CF_UNDERLINE) || f->rev == REV_INS) deco_line(b, f->rev == REV_INS ? UL_SINGLE : f->ul, x, y - size / 9, w, size);
  if ((f->flags & CF_STRIKE) || f->rev == REV_DEL) deco_line(b, UL_SINGLE, x, y + size / 4, w, size);
  if (f->fx & FX_DSTRIKE) deco_line(b, UL_DOUBLE, x, y + size / 4, w, size);
}
static void pdf_object(Pdf *p, Buf *b, int k, i32 x, i32 y);
/* one line: its list label and boxes; lx: the line's left edge, base: its baseline (PDF y) */
static void pdf_line(Pdf *p, Buf *b, ParaLay *pl, u32 line, i32 lx, i32 base)
{
  Line *ln = &pl->lines[line];
  u32 k;
  if (line == 0 && pl->label[0]) {
    CharFmt f = pl->p->r[0].f;
    f.flags &= (u8)~(CF_UNDERLINE | CF_STRIKE);
    text(p, b, &f, pl->label, (u32)strlen(pl->label), lx, base, pl->labelw);
  }
  /* highlighting behind the text first (trailing spaces left out) */
  for (k = ln->box0; k < ln->box0 + ln->nbox; k++) {
    Box *bx = &pl->boxes[k];
    i32 size = bx->f.size * 10;
    if (!bx->f.hl || bx->kind == BX_BREAK || bx->kind == BX_PIC || (bx->kind == BX_SPACE && k + 1 == ln->box0 + ln->nbox)) continue;
    colour(b, bx->f.hl & 0xFFFFFF, "rg");
    buf_printf(b, "%ld %ld %ld %ld re f\n", (long)(lx + bx->x), (long)(base - size / 4), (long)bx->w, (long)(size * 23 / 20));
  }
  /* character borders: one box round each stretch of bordered boxes (a trailing space left out) */
  for (k = ln->box0; k < ln->box0 + ln->nbox; k++) {
    u32 e = k;
    i32 size = 0, x1;
    if (!(pl->boxes[k].f.fx & FX_BORDER)) continue;
    while (e < ln->box0 + ln->nbox && (pl->boxes[e].f.fx & FX_BORDER)) { if (pl->boxes[e].f.size * 10 > size) size = pl->boxes[e].f.size * 10; e++; }
    x1 = e == ln->box0 + ln->nbox && e - 1 > k && pl->boxes[e - 1].kind == BX_SPACE ? pl->boxes[e - 1].x : pl->boxes[e - 1].x + pl->boxes[e - 1].w;
    colour(b, pl->boxes[k].f.color, "RG");
    buf_printf(b, "10 w %ld %ld %ld %ld re S\n", (long)(lx + pl->boxes[k].x), (long)(base - size * 3 / 10), (long)(x1 - pl->boxes[k].x), (long)(size * 12 / 10));
    k = e - 1;
  }
  for (k = ln->box0; k < ln->box0 + ln->nbox; k++) {
    Box *bx = &pl->boxes[k];
    if (bx->f.link && (bx->kind == BX_TEXT || bx->kind == BX_SPACE) && bx->w > 0) {
      i32 size = bx->f.size * 10;
      link_area(p, bx->f.link, lx + bx->x, base - size / 4, lx + bx->x + bx->w, base + size * 9 / 10);
    }
    if (bx->kind == BX_TEXT) {
      FaceSize *z = bx->nspace ? lay_face(p->L, &bx->f) : NULL;
      i32 hw = z ? (i32)((fonts_text(z, "-", 1) + QFINE / 2) / QFINE) : 0;
      text(p, b, &bx->f, pl->p->t + bx->off, bx->len, lx + bx->x, base, bx->w - hw);
      if (z) text(p, b, &bx->f, "-", 1, lx + bx->x + bx->w - hw, base, hw);      /* hyphenated here */
    }
    else if (bx->kind == BX_DROP) text(p, b, &bx->f, pl->p->t + bx->off, bx->len, lx + bx->x, base - lay_drop_dy(pl, bx), bx->w);
    else if (bx->kind == BX_TAB && bx->nspace && bx->w > 0) {
      /* the tab's leader: dots, hyphens or a line up to the stop */
      CharFmt f = bx->f;
      FaceSize *z;
      f.flags &= (u8)~(CF_UNDERLINE | CF_STRIKE);
      if (bx->nspace == LEAD_LINE) {
        i32 size = f.size * 10;
        colour(b, f.color, "rg");
        buf_printf(b, "%ld %ld %ld %ld re f\n", (long)(lx + bx->x), (long)(base - size / 9), (long)bx->w, (long)(size / 18 + 1));
      } else if ((z = lay_face(p->L, &f))) {
        const char *ch = bx->nspace == LEAD_DOT ? "." : "-";
        i32 cw = (i32)(fonts_text(z, ch, 1) / QFINE), n, i0;
        char dots[200];
        if (cw > 0) {
          n = (bx->w - cw / 2) / cw;
          if (n > (i32)sizeof dots - 1) n = (i32)sizeof dots - 1;
          for (i0 = 0; i0 < n; i0++) dots[i0] = *ch;
          dots[n > 0 ? n : 0] = 0;
          /* ending at the stop, as Word lines its dots up */
          if (n > 0) text(p, b, &f, dots, (u32)n, lx + bx->x + bx->w - n * cw, base, n * cw);
        }
      }
    }
    else if (bx->kind == BX_NOTE || bx->kind == BX_ENOTE) {
      char num[24];
      lay_note_label(pl, k, num);
      text(p, b, &bx->f, num, (u32)strlen(num), lx + bx->x, base, bx->w);
    }
    else if (bx->kind == BX_FIELD) {
      char num[16];
      lay_field_text(p->L, pl->p, bx, p->page, num);
      text(p, b, &bx->f, num, (u32)strlen(num), lx + bx->x, base, bx->w);
    }
    else if (bx->kind == BX_PIC) {
      int k = pic_index(pl->p->t + bx->off);
      Doc *d = p->L->d;
      if (k < d->nimages) pdf_object(p, b, k, lx + bx->x, base);       /* its bottom on the baseline */
    }
    else if (bx->kind == BX_SPACE && (bx->f.flags & CF_UNDERLINE) && bx->f.ul != UL_WORDS && k + 1 < ln->box0 + ln->nbox) {
      i32 size = bx->f.size * 10;
      colour(b, bx->f.color, "rg");
      colour(b, bx->f.color, "RG");
      deco_line(b, bx->f.ul, lx + bx->x, base + bx->f.position * 10 - size / 9, bx->w, size);
    }
  }
}
/* Word draws the watermark half transparent: half way to white */
static u32 wm_colour(u32 c)
{
  return ((((c >> 16) & 255) + 255) / 2) << 16 | ((((c >> 8) & 255) + 255) / 2) << 8 | (((c & 255) + 255) / 2);
}
/* the watermark: big light words across the text area (diagonal, rising), behind everything */
static void watermark(Pdf *p, Buf *b)
{
  Doc *d = p->L->d;
  const PageSetup *pg = lay_page(p->L, p->page);
  i32 H = pg->height, bw = pg->width - pg->left - pg->right, bh = pg->height - pg->top - pg->bottom;
  FaceSize *z = fonts_size(p->L->fs, d->wm.font[0] ? d->wm.font : "Calibri", 0, 0, 144);
  char enc[80];
  u32 i = 0, k = 0, n = (u32)strlen(d->wm.text);
  long w72, want, size, w, cx, cy, x0, y0;
  if (!z) return;
  while (i < n && k + 1 < sizeof enc) { int used; u32 cp = utf8_get(d->wm.text + i, n - i, &used); enc[k++] = (char)cp_code(cp); i += (u32)used; }
  enc[k] = 0;
  w72 = (long)(fonts_text(z, d->wm.text, n) / QFINE);
  if (w72 <= 0) return;
  want = d->wm.horizontal ? bw * 9 / 10 : (bw + bh) * 47 / 100;    /* most of the diagonal (near enough its length) */
  size = 144L * want / w72;
  if (size > 4000) size = 4000;
  if (!(z = fonts_size(p->L->fs, d->wm.font[0] ? d->wm.font : "Calibri", 0, 0, (u16)size))) return;
  w = (long)(fonts_text(z, d->wm.text, n) / QFINE);
  cx = pg->left + bw / 2; cy = H - (pg->top + bh / 2);
  colour(b, wm_colour(d->wm.color), "rg");
  if (d->wm.horizontal) { x0 = cx - w / 2; y0 = cy - size * 10 * 7 / 20; buf_printf(b, "BT /F%d %ld Tf %ld %ld Td ", font_index(p, z->face), size * 10, x0, y0); }
  else {
    /* along the diagonal: start half the width back, half the capital's height down (rotated) */
    long hh = size * 10 * 7 / 20;
    x0 = cx - (w / 2) * 7071 / 10000 + hh * 7071 / 10000;
    y0 = cy - (w / 2) * 7071 / 10000 - hh * 7071 / 10000;
    buf_printf(b, "BT /F%d %ld Tf 0.7071 0.7071 -0.7071 0.7071 %ld %ld Tm ", font_index(p, z->face), size * 10, x0, y0);
  }
  pdf_string(b, enc);
  buf_adds(b, " Tj ET\n");
}
/* the page border: a rectangle round the text (or in from the page's edge) */
static void page_border(Pdf *p, Buf *b)
{
  Doc *d = p->L->d;
  const PageSetup *pg = lay_page(p->L, p->page);
  PageBorder *pb = &d->border;
  i32 H = pg->height, lw = pb->width * 5 / 2, x0, y0, x1, y1, i, n = pb->style == PB_DOUBLE ? 2 : 1;
  if (pb->style == PB_NONE) return;
  if (pb->style == PB_THICK) lw *= 3;
  if (lw < 5) lw = 5;
  if (pb->frompage) { x0 = pb->space; y0 = pb->space; x1 = pg->width - pb->space; y1 = pg->height - pb->space; }
  else { x0 = pg->left - pb->space; y0 = pg->top - pb->space; x1 = pg->width - pg->right + pb->space; y1 = pg->height - pg->bottom + pb->space; }
  colour(b, pb->color, "RG");
  buf_printf(b, "%ld w 0 J ", (long)lw);
  if (pb->style == PB_DOTTED) buf_printf(b, "[%ld %ld] 0 d ", (long)lw, (long)lw * 2);
  else if (pb->style == PB_DASHED) buf_printf(b, "[%ld %ld] 0 d ", (long)lw * 6, (long)lw * 4);
  for (i = 0; i < n; i++) {
    i32 o = i * lw * 3;
    buf_printf(b, "%ld %ld %ld %ld re S\n", (long)(x0 + o), (long)(H - y1 + o), (long)(x1 - x0 - 2 * o), (long)(y1 - y0 - 2 * o));
  }
  buf_adds(b, "[] 0 d\n");
}
/* one edge (E_) of a table cell, from (x0, y0) to (x1, y1) in PDF space */
static void cell_edge(Buf *b, const CellBox *c, int e, i32 x0, i32 y0, i32 x1, i32 y1)
{
  u8 st;
  i32 lw, k, n;
  if (!(c->borders & (1 << e))) return;
  st = c->bstyle[e] ? c->bstyle[e] : PB_SINGLE;
  lw = (c->bwidth[e] ? c->bwidth[e] : 4) * 5 / 2;
  if (st == PB_THICK && lw < 15) lw = 15;
  if (lw < 5) lw = 5;
  n = st == PB_DOUBLE ? 2 : 1;
  colour(b, c->bcolor[e], "RG");
  buf_printf(b, "%ld w %d J ", (long)lw, st == PB_DOTTED ? 1 : 2);
  if (st == PB_DOTTED) buf_printf(b, "[0 %ld] 0 d ", (long)lw * 2);
  else if (st == PB_DASHED) buf_printf(b, "[%ld %ld] 0 d ", (long)lw * 4, (long)lw * 3);
  for (k = 0; k < n; k++) {
    i32 o = n == 2 ? (k ? lw * 3 / 2 : -lw * 3 / 2) : 0;
    if (y0 == y1) buf_printf(b, "%ld %ld m %ld %ld l S\n", (long)x0, (long)(y0 + o), (long)x1, (long)(y1 + o));
    else buf_printf(b, "%ld %ld m %ld %ld l S\n", (long)(x0 + o), (long)y0, (long)(x1 + o), (long)y1);
  }
  if (st == PB_DOTTED || st == PB_DASHED) buf_adds(b, "[] 0 d\n");
}
/* an outline's dashes (PB_ kind) for a line lw wide */
static void dash(Buf *b, int st, i32 lw)
{
  if (st == PB_DOTTED) buf_printf(b, "[0 %ld] 0 d 1 J ", (long)lw * 2);
  else if (st == PB_DASHED) buf_printf(b, "[%ld %ld] 0 d 0 J ", (long)lw * 4, (long)lw * 3);
  else buf_adds(b, "[] 0 d 0 J ");
}
/* a number of TRIG_ONEths, as PDF writes it */
static void fix4(Buf *b, i32 v)
{
  long a = v < 0 ? -(long)v : v, w = a / TRIG_ONE, f = (a % TRIG_ONE) * 10000 / TRIG_ONE;
  buf_printf(b, "%s%ld.%04ld ", v < 0 ? "-" : "", w, f);
}
/* one simple object (pt: as obj_parts gives it) with its centre at cx, cy in PDF space */
static void pdf_leaf(Pdf *p, Buf *b, const ObjPart *pt, i32 cx, i32 cy)
{
  const Image *im = pt->im;
  i32 w = pt->cx, h = pt->cy, lw = im->lwidth * 5 / 2, c = icos(pt->rot), sn = isin(pt->rot);
  int fx = pt->flip & 1 ? -1 : 1, fy = pt->flip & 2 ? -1 : 1, ri;
  int outline = im->lstyle != PB_NONE && im->lwidth;
  if (lw < 5) lw = 5;
  /* the object's own space: its centre at the origin, y up, turned (clockwise on the page) and flipped */
  buf_printf(b, "q 1 0 0 1 %ld %ld cm ", (long)cx, (long)cy);
  fix4(b, c); fix4(b, -sn); fix4(b, sn); fix4(b, c);
  buf_printf(b, "0 0 cm %d 0 0 %d 0 0 cm\n", fx, fy);
  if (im->kind == OBJ_PICTURE) {
    if (im->data && (ri = img_ref(p, im)) >= 0) {
      /* cropped: the whole picture bigger, the box clipping it */
      long cl = im->crop[0], ct = im->crop[1], cr = im->crop[2], cb = im->crop[3], fw, fh;
      if (cl + cr >= 99000) cl = cr = 0;
      if (ct + cb >= 99000) ct = cb = 0;
      fw = (long)w * 100000 / (100000 - cl - cr); fh = (long)h * 100000 / (100000 - ct - cb);
      if (cl || ct || cr || cb) buf_printf(b, "%ld %ld %ld %ld re W n\n", (long)(-w / 2), (long)(-h / 2), (long)w, (long)h);
      buf_printf(b, "q %ld 0 0 %ld %ld %ld cm /Im%d Do Q\n", fw, fh, (long)(-w / 2 - fw * cl / 100000), (long)(-h / 2 - fh * cb / 100000), ri);
    }
    if (outline) {
      colour(b, im->lcolor, "RG");
      buf_printf(b, "%ld w ", (long)lw);
      dash(b, im->lstyle, lw);
      buf_printf(b, "%ld %ld %ld %ld re S\n", (long)(-w / 2), (long)(-h / 2), (long)w, (long)h);
    }
  } else {
    i32 xy[2 * SHAPE_MAXPTS];
    int closed, i;
    u32 n = shape_points(im->kind == OBJ_TEXTBOX ? SH_RECT : im->shape, w, h, xy, SHAPE_MAXPTS, &closed);
    int fill = closed && im->fill != COLOR_AUTO;
    if (fill || outline) {
      for (i = 0; i < (int)n; i++) buf_printf(b, "%ld %ld %s ", (long)(xy[2 * i] - w / 2), (long)(h / 2 - xy[2 * i + 1]), i ? "l" : "m");
      if (closed) buf_adds(b, "h ");
      if (fill) colour(b, im->fill, "rg");
      if (outline) { colour(b, im->lcolor, "RG"); buf_printf(b, "%ld w 1 j ", (long)lw); dash(b, im->lstyle, lw); }
      buf_adds(b, fill && outline ? "B\n" : fill ? "f\n" : "S\n");
      if (im->shape == SH_ARROW && im->kind == OBJ_SHAPE && outline) {
        i32 ah[6];
        shape_arrowhead(w, h, lw, ah);
        colour(b, im->lcolor, "rg");
        buf_printf(b, "%ld %ld m %ld %ld l %ld %ld l h f\n", (long)(ah[0] - w / 2), (long)(h / 2 - ah[1]), (long)(ah[2] - w / 2), (long)(h / 2 - ah[3]),
                   (long)(ah[4] - w / 2), (long)(h / 2 - ah[5]));
      }
    }
  }
  buf_adds(b, "Q\n");
  /* a text box's (or shape's) text, upright in its box */
  if (im->text) {
    Furn f;
    u32 j, l;
    if (lay_textbox(p->L, im, w, &f)) {
      i32 top = cy + h / 2, x0 = cx - w / 2;
      buf_printf(b, "q %ld %ld %ld %ld re W n\n", (long)x0, (long)(cy - h / 2), (long)w, (long)h);
      for (j = 0; j < f.n; j++) {
        i32 ty = f.f[j].y;
        for (l = 0; l < f.f[j].pl.nlines; l++) {
          Line *ln = &f.f[j].pl.lines[l];
          pdf_line(p, b, &f.f[j].pl, l, x0 + ln->left, top - (ty + ln->ascent));
          ty += ln->height;
        }
      }
      buf_adds(b, "Q\n");
    }
    lay_furniture_free(&f);
  }
}
/* object k (picture, shape, text box or group) with the bottom left of its (turned) box at x, y in PDF space */
static void pdf_object(Pdf *p, Buf *b, int k, i32 x, i32 y)
{
  const Image *im = &p->L->d->images[k];
  ObjPart parts[64];
  int n = obj_parts(im, parts, 64), i;
  i32 H = OBJ_H(im);
  for (i = 0; i < n; i++) pdf_leaf(p, b, &parts[i], x + parts[i].x, y + H - parts[i].y);
}
/* the floating objects on a page (or in a header): those behind the text, or the rest */
static void pdf_floats(Pdf *p, Buf *b, const FloatBox *fb, u32 n, u32 page, int behind)
{
  const PageSetup *pg = lay_page(p->L, p->page);
  i32 H = pg->height;
  u32 i, *ord = lay_stack_order(p->L, fb, n);       /* (bottom first) */
  for (i = 0; i < n; i++) {
    u32 j = ord ? ord[i] : i;
    if (fb[j].page == page && (fb[j].wrap == WRAP_BEHIND) == behind) pdf_object(p, b, fb[j].img, fb[j].x, H - fb[j].y - fb[j].h);
  }
  free(ord);
}
/* the content stream of one page, painted back to front: watermark, page border, objects
   behind the text, paragraph shading and borders, column lines, table cells, the text, headers,
   footers and notes, then the floating objects in front. After the opening 1/20 scale every
   coordinate is in twips, with y counting up from the foot of the page as PDF does */
static void page_content(Pdf *p, u32 page, Buf *b)
{
  Layout *L = p->L;
  Doc *d = L->d;
  const PageSetup *pg = lay_page(L, page);
  i32 H = pg->height;
  u32 a, e, i;
  p->page = page;
  buf_adds(b, "0.05 0 0 0.05 0 0 cm\n");
  if (d->pagecolor) {
    /* the page colour, under everything */
    colour(b, d->pagecolor & 0xFFFFFF, "rg");
    buf_printf(b, "0 0 %ld %ld re f\n", (long)pg->width, (long)H);
  }
  if (d->wm.text[0]) watermark(p, b);
  page_border(p, b);
  /* objects behind the text: the headers' first */
  {
    int w;
    for (w = 0; w < 2; w++) {
      Furn f;
      if (!lay_furniture(L, page, w, &f)) continue;
      pdf_floats(p, b, f.fb, f.nfb, page, 1);
      lay_furniture_free(&f);
    }
  }
  pdf_floats(p, b, L->floats, L->nfloats, page, 1);
  /* paragraphs' shading, then their borders */
  for (i = 0; i < L->npbox; i++) {
    const ParaBox *x = &L->pbox[i];
    const ParaFmt *pf = &L->pl[x->para].p->pf;
    if (x->page != page || !pf->shade) continue;
    colour(b, pf->shade & 0xFFFFFF, "rg");
    buf_printf(b, "%ld %ld %ld %ld re f\n", (long)x->x0, (long)(H - x->y1), (long)(x->x1 - x->x0), (long)(x->y1 - x->y0));
  }
  for (i = 0; i < L->npbox; i++) {
    const ParaBox *x = &L->pbox[i];
    const ParaFmt *pf = &L->pl[x->para].p->pf;
    int e;
    if (x->page != page) continue;
    for (e = 0; e < PE_N; e++) {
      i32 lw = (pf->bwidth[e] ? pf->bwidth[e] : 4) * 5 / 2, x0, y0, x1, y1, o, n2, kk;
      u8 st = pf->bstyle[e];
      if (!st) continue;
      if ((e == PE_TOP && !(x->lines & PB_TOPLINE)) || (e == PE_BOTTOM && !(x->lines & PB_BOTTOMLINE)) || (e == PE_BETWEEN && !(x->lines & PB_BETWEENLINE))) continue;
      if (st == PB_THICK && lw < 15) lw = 15;
      if (lw < 5) lw = 5;
      n2 = st == PB_DOUBLE ? 2 : 1;
      /* each line inside the box's edge */
      switch (e) {
      case PE_TOP: x0 = x->x0; x1 = x->x1; y0 = y1 = H - x->y0 - lw / 2; break;
      case PE_BOTTOM: x0 = x->x0; x1 = x->x1; y0 = y1 = H - x->y1 + lw / 2 + (n2 == 2 ? lw * 2 : 0); break;
      case PE_BETWEEN: x0 = x->x0; x1 = x->x1; y0 = y1 = H - x->ybetween - lw / 2; break;
      case PE_LEFT: x0 = x1 = x->x0 + lw / 2; y0 = H - x->y0; y1 = H - x->y1; break;
      default: x0 = x1 = x->x1 - lw / 2 - (n2 == 2 ? lw * 2 : 0); y0 = H - x->y0; y1 = H - x->y1;
      }
      colour(b, pf->bcolor[e], "RG");
      buf_printf(b, "%ld w %d J ", (long)lw, st == PB_DOTTED ? 1 : 0);
      if (st == PB_DOTTED) buf_printf(b, "[0 %ld] 0 d ", (long)lw * 2);
      else if (st == PB_DASHED) buf_printf(b, "[%ld %ld] 0 d ", (long)lw * 4, (long)lw * 3);
      for (kk = 0; kk < n2; kk++) {
        o = kk * lw * 2;
        if (y0 == y1) buf_printf(b, "%ld %ld m %ld %ld l S\n", (long)x0, (long)(y0 - o), (long)x1, (long)(y1 - o));
        else buf_printf(b, "%ld %ld m %ld %ld l S\n", (long)(x0 + o), (long)y0, (long)(x1 + o), (long)y1);
      }
      if (st == PB_DOTTED || st == PB_DASHED) buf_adds(b, "[] 0 d\n");
    }
  }
  /* lines between columns */
  for (i = 0; i < L->nseps; i++)
    if (L->seps[i].page == page)
      buf_printf(b, "0 0 0 RG 10 w %ld %ld m %ld %ld l S\n", (long)(pg->left + L->seps[i].x), (long)(H - L->seps[i].y0), (long)(pg->left + L->seps[i].x), (long)(H - L->seps[i].y1));
  /* table cells: fills, then lines */
  for (i = 0; i < 2 * L->ncells; i++) {             /* a nested table's fills over its cell's */
    CellBox *c = &L->cells[i % L->ncells];
    if ((u32)c->depth != i / L->ncells || c->page != page || c->shade == COLOR_AUTO) continue;
    colour(b, c->shade, "rg");
    buf_printf(b, "%ld %ld %ld %ld re f\n", (long)c->x, (long)(H - c->y - c->h), (long)c->w, (long)c->h);
  }
  for (i = 0; i < L->ncells; i++) {
    CellBox *c = &L->cells[i];
    i32 x0 = c->x, x1 = c->x + c->w, y0 = H - c->y, y1 = H - c->y - c->h;
    if (c->page != page) continue;
    cell_edge(b, c, E_TOP, x0, y0, x1, y0);
    cell_edge(b, c, E_BOTTOM, x0, y1, x1, y1);
    cell_edge(b, c, E_LEFT, x0, y0, x0, y1);
    cell_edge(b, c, E_RIGHT, x1, y0, x1, y1);
  }
  lay_page_lines(L, page, &a, &e);
  for (i = a; i < e; i++) {
    Placed *pc = &L->placed[i];
    ParaLay *pl = &L->pl[pc->para];
    Line *ln = &pl->lines[pc->line];
    i32 lx = pg->left + pc->x + ln->left, base = H - (pc->y + ln->ascent);
    if (pc->page != page) continue;
    if (pc->turn) {
      /* WP21b: turned text in a table cell, drawn through a turned frame (the line laid out across) */
      if (pc->turn == TD_DOWN) buf_printf(b, "q 0 -1 1 0 %ld %ld cm\n", (long)(pg->left + pc->x + ln->height), (long)(H - pc->y - pc->tshift));
      else buf_printf(b, "q 0 1 -1 0 %ld %ld cm\n", (long)(pg->left + pc->x), (long)(H - pc->y - pc->tlen + pc->tshift));
      pdf_line(p, b, pl, pc->line, ln->left, -ln->ascent);
      buf_adds(b, "Q\n");
      continue;
    }
    pdf_line(p, b, pl, pc->line, lx, base);
    if (pc->lnum) {
      /* its line number, its right edge lnd left of the column */
      CharFmt f = d->styles[0].cf;
      FaceSize *z = lay_face(L, &f);
      char num[16];
      i32 nw;
      sprintf(num, "%lu", (unsigned long)pc->lnum);
      nw = z ? (i32)(fonts_text(z, num, (u32)strlen(num)) / QFINE) : 0;
      f.flags = 0; f.color = COLOR_AUTO; f.rev = 0; f.hl = 0;
      text(p, b, &f, num, (u32)strlen(num), pg->left + pc->x - pc->lnd - nw, base, nw);
    }
  }
  /* the header and footer, and the footnotes under a short line */
  {
    int w;
    for (w = 0; w < 4; w++) {
      Furn f;
      u32 j, k;
      i32 sepy = -1;
      if (w < 2 ? !lay_furniture(L, page, w, &f) : w == 2 ? !lay_notes(L, page, &f, &sepy) : !lay_endnotes(L, page, &f, &sepy)) continue;
      if (w >= 2 && sepy >= 0) buf_printf(b, "0 0 0 RG 10 w %ld %ld m %ld %ld l S\n", (long)pg->left, (long)(H - sepy), (long)(pg->left + 2880), (long)(H - sepy));
      for (j = 0; j < f.n; j++) {
        i32 y = f.f[j].y;
        for (k = 0; k < f.f[j].pl.nlines; k++) {
          Line *ln = &f.f[j].pl.lines[k];
          pdf_line(p, b, &f.f[j].pl, k, pg->left + ln->left, H - (y + ln->ascent));
          y += ln->height;
        }
      }
      if (w < 2) pdf_floats(p, b, f.fb, f.nfb, page, 0);
      lay_furniture_free(&f);
    }
  }
  pdf_floats(p, b, L->floats, L->nfloats, page, 0);
}
/* a PostScript name for the face (the family without spaces or PDF delimiters, plus -Bold
   and so on); o needs room for 52 bytes */
static void ps_name(char *o, const Face *f)
{
  const char *s;
  size_t n = 0;
  for (s = f->family; *s && n < 40; s++) if (*s != ' ' && *s > 32 && *s < 127 && !strchr("()<>[]{}/%#", *s)) o[n++] = *s;
  o[n] = 0;
  if (f->bold || f->italic) strcat(o, f->bold && f->italic ? "-BoldItalic" : f->bold ? "-Bold" : "-Italic");
}
/* the pages' content goes first, into separate buffers, since only then are the fonts and
   pictures known and the object numbers fixed: 1 catalog, 2 page tree, 3 info, three per font
   (font, descriptor, file), one per picture, then two per page (page, content). Faces whose
   licence forbids embedding get widths and a descriptor only. The result is malloc'd */
u8 *pdf_export(Layout *L, size_t *len)
{
  Pdf p;
  Doc *d = L->d;
  u32 i, k, pg0, xref;
  Buf *pages;
  if (!lay_update(L)) return NULL;
  memset(&p, 0, sizeof p);
  p.L = L;
  buf_init(&p.o);
  /* the pages' content first: it tells which fonts are used */
  if (!(pages = (Buf *)calloc(L->npages ? L->npages : 1, sizeof(Buf)))) return NULL;
  for (k = 0; k < L->npages; k++) { buf_init(&pages[k]); page_content(&p, k, &pages[k]); }
  pg0 = 4 + 3 * (u32)p.nfonts + (u32)p.nimgs;
  p.objcap = pg0 + 2 * L->npages + (u32)p.nlinks + 1;     /* (the link annotations come after the pages) */
  if (!(p.off = (u32 *)calloc(p.objcap, sizeof(u32)))) { for (k = 0; k < L->npages; k++) buf_free(&pages[k]); free(pages); return NULL; }
  {
    /* 1.4, or 1.6 for a whole OpenType font (CFF outlines) embedded */
    int v6 = 0;
    for (i = 0; i < (u32)p.nfonts; i++) if (p.fonts[i].face->cff && fonts_embeddable(p.fonts[i].face)) v6 = 1;
    buf_adds(&p.o, v6 ? "%PDF-1.6\n%\xE2\xE3\xCF\xD3\n" : "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");
  }
  obj_start(&p, 1); buf_adds(&p.o, "<</Type /Catalog /Pages 2 0 R>>\nendobj\n");
  obj_start(&p, 2);
  buf_adds(&p.o, "<</Type /Pages /Kids [");
  for (k = 0; k < L->npages; k++) buf_printf(&p.o, "%lu 0 R ", (unsigned long)(pg0 + 2 * k));
  buf_printf(&p.o, "] /Count %lu>>\nendobj\n", (unsigned long)L->npages);
  obj_start(&p, 3);
  buf_adds(&p.o, "<</Producer (Quill for the Amiga)");
  if (d->title[0]) { char t[200]; utf8_to_latin1(d->title, (u32)strlen(d->title), t, sizeof t); buf_adds(&p.o, " /Title "); pdf_string(&p.o, t); }
  if (d->author[0]) { char t[200]; utf8_to_latin1(d->author, (u32)strlen(d->author), t, sizeof t); buf_adds(&p.o, " /Author "); pdf_string(&p.o, t); }
  buf_adds(&p.o, ">>\nendobj\n");
  for (i = 0; i < (u32)p.nfonts; i++) {
    Face *f = p.fonts[i].face;
    char name[64];
    size_t flen = 0;
    /* the face as a file of its own (out of a collection), if its licence lets a PDF carry it */
    u8 *file = fonts_embeddable(f) ? fonts_face_file(f, &flen) : NULL;
    int c;
    i32 upm = f->upm ? f->upm : 1000;
    ps_name(name, f);
    obj_start(&p, 4 + 3 * i);
    buf_printf(&p.o, "<</Type /Font /Subtype /%s /BaseFont /%s /FirstChar 32 /LastChar 255 /Encoding /WinAnsiEncoding /FontDescriptor %lu 0 R /Widths [",
               f->cff ? "Type1" : "TrueType", name, (unsigned long)(5 + 3 * i));
    for (c = 32; c < 256; c++) {
      u32 cp = code_cp(c);
      u16 g = cp ? ttf_glyph(f, cp) : 0;
      u16 adv = f->nmetrics ? f->adv[g < f->nmetrics ? g : f->nmetrics - 1] : 500;
      buf_printf(&p.o, "%ld ", (long)adv * 1000 / upm);
    }
    buf_adds(&p.o, "]>>\nendobj\n");
    obj_start(&p, 5 + 3 * i);
    buf_printf(&p.o, "<</Type /FontDescriptor /FontName /%s /Flags %d /FontBBox [-600 %ld 2000 %ld] /ItalicAngle %d /Ascent %ld /Descent %ld /CapHeight %ld /StemV %d",
               name, 32 | (f->italic ? 64 : 0), (long)(-f->descent * 1000 / upm), (long)(f->ascent * 1000 / upm), f->italic ? -12 : 0,
               (long)(f->ascent * 1000 / upm), (long)(-f->descent * 1000 / upm), (long)(f->ascent * 700 / upm), f->bold ? 140 : 80);
    if (file) buf_printf(&p.o, " /FontFile%s %lu 0 R", f->cff ? "3" : "2", (unsigned long)(6 + 3 * i));
    buf_adds(&p.o, ">>\nendobj\n");
    if (file) {
      char dict[64];
      if (f->cff) strcpy(dict, "/Subtype /OpenType");          /* (a whole OpenType font: PDF 1.6) */
      else sprintf(dict, "/Length1 %lu", (unsigned long)flen);
      stream(&p, 6 + 3 * i, dict, file, flen);
      free(file);
    } else { obj_start(&p, 6 + 3 * i); buf_adds(&p.o, "null\nendobj\n"); }
  }
  /* the pictures: JPEG as it is, PNG as deflated RGB at up to 300 dpi */
  for (i = 0; i < (u32)p.nimgs; i++) {
    const Image *im = p.imgs[i];
    u32 on = 4 + 3 * (u32)p.nfonts + i;
    char dict[200];
    int dec = 0, nc = im->type == IMG_JPEG ? img_jpeg_components(im->data, im->len, &dec) : 3;
    if (im->type == IMG_JPEG && (nc == 1 || nc == 3)) {
      obj_start(&p, on);
      buf_printf(&p.o, "<</Type /XObject /Subtype /Image /Width %lu /Height %lu /ColorSpace /%s /BitsPerComponent 8 /Filter /DCTDecode /Length %lu>>\nstream\n",
                 (unsigned long)im->pw, (unsigned long)im->ph, nc == 1 ? "DeviceGray" : "DeviceRGB", (unsigned long)im->len);
      buf_add(&p.o, im->data, im->len);
      buf_adds(&p.o, "\nendstream\nendobj\n");
    } else {
      u32 w = im->pw, h = im->ph, mw = (u32)(im->cx / 5 + 1), mh = (u32)(im->cy / 5 + 1);
      u8 *rgb;
      if (w > mw) { h = (u32)((unsigned long)h * mw / w); w = mw; }
      if (h > mh) { w = (u32)((unsigned long)w * mh / h); h = mh; }
      if (!w) w = 1;
      if (!h) h = 1;
      if ((rgb = img_decode(im->data, im->len, w, h))) {
        sprintf(dict, "/Type /XObject /Subtype /Image /Width %lu /Height %lu /ColorSpace /DeviceRGB /BitsPerComponent 8", (unsigned long)w, (unsigned long)h);
        stream(&p, on, dict, rgb, (size_t)w * h * 3);
        free(rgb);
      } else {                                /* one grey pixel in its place */
        static const u8 grey[3] = { 200, 200, 200 };
        stream(&p, on, "/Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceRGB /BitsPerComponent 8", grey, 3);
      }
    }
  }
  for (k = 0; k < L->npages; k++) {
    obj_start(&p, pg0 + 2 * k);
    buf_printf(&p.o, "<</Type /Page /Parent 2 0 R /MediaBox [0 0 %ld.%02ld %ld.%02ld] /Contents %lu 0 R /Resources <</Font <<",
               (long)(lay_page(L, k)->width / 20), (long)(lay_page(L, k)->width % 20 * 5), (long)(lay_page(L, k)->height / 20), (long)(lay_page(L, k)->height % 20 * 5),
               (unsigned long)(pg0 + 2 * k + 1));
    for (i = 0; i < (u32)p.nfonts; i++) buf_printf(&p.o, "/F%lu %lu 0 R ", (unsigned long)i, (unsigned long)(4 + 3 * i));
    buf_adds(&p.o, ">>");
    if (p.nimgs) {
      buf_adds(&p.o, " /XObject <<");
      for (i = 0; i < (u32)p.nimgs; i++) buf_printf(&p.o, "/Im%lu %lu 0 R ", (unsigned long)i, (unsigned long)(4 + 3 * p.nfonts + i));
      buf_adds(&p.o, ">>");
    }
    buf_adds(&p.o, " >>");
    {
      /* its links' annotations */
      int j, any = 0;
      for (j = 0; j < p.nlinks; j++) if (p.links[j].page == k) { buf_adds(&p.o, any ? " " : " /Annots ["); buf_printf(&p.o, "%lu 0 R", (unsigned long)(pg0 + 2 * L->npages + j)); any = 1; }
      if (any) buf_adds(&p.o, "]");
    }
    buf_adds(&p.o, " >>\nendobj\n");
    stream(&p, pg0 + 2 * k + 1, "", (const u8 *)pages[k].data, pages[k].len);
    buf_free(&pages[k]);
  }
  free(pages);
  for (i = 0; i < (u32)p.nlinks; i++) {
    /* a link: a rectangle that opens its address (no border drawn: the text is styled already) */
    struct PLink *l = &p.links[i];
    obj_start(&p, pg0 + 2 * L->npages + i);
    buf_printf(&p.o, "<</Type /Annot /Subtype /Link /Rect [%ld %ld %ld %ld] /Border [0 0 0] /A <</S /URI /URI ", (long)l->x0, (long)l->y0, (long)l->x1, (long)l->y1);
    pdf_string(&p.o, doc_link_url(d, l->link));
    buf_adds(&p.o, ">> >>\nendobj\n");
  }
  free(p.links);
  xref = (u32)p.o.len;
  buf_printf(&p.o, "xref\n0 %lu\n0000000000 65535 f \n", (unsigned long)p.objcap);
  for (i = 1; i < p.objcap; i++) buf_printf(&p.o, "%010lu 00000 n \n", (unsigned long)p.off[i]);
  buf_printf(&p.o, "trailer\n<</Size %lu /Root 1 0 R /Info 3 0 R>>\nstartxref\n%lu\n%%%%EOF\n", (unsigned long)p.objcap, (unsigned long)xref);
  free(p.off);
  if (p.o.oom) { buf_free(&p.o); return NULL; }
  *len = p.o.len;
  return (u8 *)buf_take(&p.o);
}
/* exports and writes to path; 0 if out of memory or the file couldn't be written */
int pdf_save_file(Layout *L, const char *path)
{
  size_t n;
  u8 *data = pdf_export(L, &n);
  int ok;
  if (!data) return 0;
  ok = file_write(path, data, n);
  free(data);
  return ok;
}
