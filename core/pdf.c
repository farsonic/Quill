/* pdf.c - see pdf.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pdf.h"
#include "field.h"
#include "pages.h"
#include "zip.h"
#include "docx.h"
#include "image.h"
#include "objfx.h"     /* WP17/WP18 */
#include "chart.h"                 /* (WP15a) charts and SmartArt */
#include "charfx.h"
#include "toc.h"
#include "raster.h"

PdfOpts pdf_options = { 0, 0, 1, "", "", 150, PIMG_PNG };
#include "math.h"     /* WP14a: equations */
#include "ink.h"      /* WP20 */

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

/* a face used: a Type 0 font of its glyphs (TrueType or CFF outlines, embedded as a subset), or a
   simple font in Windows-1252 when its licence keeps it out of the file. used: the glyphs drawn
   (a bit each), uni: the character each shows (for the ToUnicode CMap); cid: a CID-keyed CFF
   font's CID of each glyph (the code the text uses), with the font file read early for it */
enum { PF_SIMPLE, PF_TT, PF_CFF };
typedef struct {
  Face *face;
  u8 kind;
  u8 *used; u32 *uni;
  u16 *cid; u32 ncid;
  u8 *file; size_t flen;
} PFont;
/* a place to jump to: a page and a point on it (twips, PDF's y up from the page's foot); set: known */
typedef struct { u32 page; i32 x, y; u8 set; } PDest;
/* what a link does: open its address (LK_URI, target: Doc.links number), go to a bookmark (LK_NAME,
   the link's "#name"), a paragraph's start (LK_PARA: a table of contents entry's heading), a note
   (LK_NOTE, LK_ENOTE: its number) or back from a note to its reference (LK_NOTEBACK, LK_ENOTEBACK) */
enum { LK_URI, LK_NAME, LK_PARA, LK_NOTE, LK_ENOTE, LK_NOTEBACK, LK_ENOTEBACK };
typedef struct {
  Layout *L;
  const PdfOpts *opt;
  int raster;                             /* drawing a picture of a page (core/raster.c): glyph ids as codes */
  Buf o;
  u32 *off; u32 nobj, objcap;
  PFont fonts[64]; int nfonts;
  const Image *imgs[256]; int nimgs;      /* pictures used (each file once), as /Im0... */
  u32 page;                               /* the page being written */
  /* links' clickable areas, in points from the page's bottom left (link annotations, written as
     objects of their own after the pages) */
  struct PLink { u32 page; i32 x0, y0, x1, y1; u8 kind; u32 target; } *links; int nlinks, linkcap;
  PDest *fnote, *fref, *enote, *eref;     /* each footnote's and endnote's text and reference */
  int *tocto;                             /* each paragraph of a table of contents: its heading's paragraph, -1 */
  Image wmimg;                            /* (WP22a) the picture watermark, as a picture to draw */
  struct PRich { u8 *rgb, *a; i32 w, h; } *rich; int nrich;   /* WP17/WP18: objects drawn by objfx_render, as /R0... */
} Pdf;
/* a link's area (twips, as the content stream draws) noted for the page being written; a box
   just right of the last area of the same link on the same line joins it */
static void link_area(Pdf *p, int kind, u32 target, i32 x0, i32 y0, i32 x1, i32 y1)
{
  struct PLink *l;
  if (p->raster) return;
  if (kind == LK_URI) {
    const char *u = doc_link_url(p->L->d, (int)target);
    if (!u[0]) return;
    if (u[0] == '#') kind = LK_NAME;          /* a place in the document */
  }
  x0 /= 20; y0 /= 20; x1 = (x1 + 19) / 20; y1 = (y1 + 19) / 20;
  if (p->nlinks) {
    l = &p->links[p->nlinks - 1];
    if (l->page == p->page && l->kind == kind && l->target == target && l->y0 == y0 && l->y1 == y1 && x0 <= l->x1 + 2 && x0 >= l->x0) { if (x1 > l->x1) l->x1 = x1; return; }
  }
  if (p->nlinks == p->linkcap) {
    int nc = p->linkcap ? p->linkcap * 2 : 16;
    struct PLink *nl = (struct PLink *)realloc(p->links, (size_t)nc * sizeof *nl);
    if (!nl) return;
    p->links = nl; p->linkcap = nc;
  }
  l = &p->links[p->nlinks++];
  l->page = p->page; l->kind = (u8)kind; l->target = target; l->x0 = x0; l->y0 = y0; l->x1 = x1; l->y1 = y1;
}
/* a place noted (the first time) */
static void dest_note(PDest *d, u32 page, i32 x, i32 y)
{
  if (d && !d->set) { d->page = page; d->x = x; d->y = y; d->set = 1; }
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
  PFont *pf;
  for (i = 0; i < p->nfonts; i++) if (p->fonts[i].face == f) return i;
  if (p->nfonts >= 64) return 0;
  pf = &p->fonts[p->nfonts];
  memset(pf, 0, sizeof *pf);
  pf->face = f;
  if (fonts_embeddable(f) && f->nglyphs && f->adv) {
    pf->kind = f->cff ? PF_CFF : PF_TT;
    pf->used = (u8 *)calloc(((u32)f->nglyphs + 8) / 8, 1);
    pf->uni = (u32 *)calloc(f->nglyphs, sizeof(u32));
    if (pf->kind == PF_CFF && !p->raster && pf->used && pf->uni) {
      /* a CID-keyed font's text gives CIDs: its charset says each glyph's */
      if ((pf->file = fonts_face_file(f, &pf->flen))) cff_cid_map(pf->file, pf->flen, &pf->cid, &pf->ncid);
      else pf->kind = PF_SIMPLE;
    }
    if (!pf->used || !pf->uni) { free(pf->used); free(pf->uni); free(pf->file); free(pf->cid); pf->used = NULL; pf->uni = NULL; pf->file = NULL; pf->cid = NULL; pf->kind = PF_SIMPLE; }
  }
  return p->nfonts++;
}
static void fonts_release(Pdf *p)
{
  int i;
  for (i = 0; i < p->nfonts; i++) { free(p->fonts[i].used); free(p->fonts[i].uni); free(p->fonts[i].cid); free(p->fonts[i].file); }
  p->nfonts = 0;
}
/* the glyph a Type 0 font shows a character with (noted as used), the character it stands for in
   *shown: the font's own, else a look-alike (a symbol font's private-use one, Windows-1252's
   stand-ins), else a question mark */
static u16 font_glyph(PFont *pf, u32 cp, u32 *shown)
{
  Face *f = pf->face;
  u16 g = ttf_glyph(f, cp);
  *shown = cp;
  if (!g && cp < 0x100) g = ttf_glyph(f, 0xF000 + cp);
  if (!g && cp_code(cp) != '?') { *shown = code_cp(cp_code(cp)); g = ttf_glyph(f, *shown); }
  if (!g && cp != '?') { *shown = '?'; g = ttf_glyph(f, '?'); }
  if (g >= f->nglyphs) g = 0;
  pf->used[g >> 3] |= (u8)(1 << (g & 7));
  if (!pf->uni[g] && g) pf->uni[g] = *shown;
  return g;
}
/* the code the text uses for a glyph: its id, or a CID-keyed font's CID */
static u32 glyph_code(const PFont *pf, u16 g) { return pf->cid && g < pf->ncid ? pf->cid[g] : g; }
/* characters shown in font fi: a hex string of 2-byte glyph codes, or (a simple font) a literal
   string in Windows-1252, then Tj */
static void pdf_string(Buf *b, const char *s);
static void show(Pdf *p, Buf *b, int fi, const u32 *cps, u32 n)
{
  PFont *pf = &p->fonts[fi];
  u32 i;
  if (pf->kind == PF_SIMPLE) {
    char enc[520];
    for (i = 0; i < n && i + 1 < sizeof enc; i++) enc[i] = (char)cp_code(cps[i]);
    enc[i] = 0;
    pdf_string(b, enc);
  } else {
    buf_addc(b, '<');
    for (i = 0; i < n; i++) { u32 sh; buf_printf(b, "%04lX", (unsigned long)glyph_code(pf, font_glyph(pf, cps[i], &sh))); }
    buf_addc(b, '>');
  }
  buf_adds(b, " Tj ");
}
/* begins object n, recording its byte offset for the xref table */
static void obj_start(Pdf *p, u32 n)
{
  if (n >= p->objcap) {
    u32 nc = n + 64, *no = (u32 *)realloc(p->off, nc * sizeof(u32));
    if (!no) { p->o.oom = 1; return; }
    memset(no + p->objcap, 0, (nc - p->objcap) * sizeof(u32));
    p->off = no; p->objcap = nc;
  }
  if (n >= p->nobj) p->nobj = n + 1;
  p->off[n] = (u32)p->o.len;
  buf_printf(&p->o, "%lu 0 obj\n", (unsigned long)n);
}
/* a new object number */
static u32 obj_new(Pdf *p) { return p->nobj++; }
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
  u32 cps[512];
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
  while (i < n && k < sizeof cps / sizeof cps[0]) {
    int used;
    u32 cp = utf8_get(s + i, n - i, &used);
    low[k] = 0;
    if ((f->flags & CF_CAPS) && cp >= 'a' && cp <= 'z') cp -= 32;
    if (small && ((cp >= 'a' && cp <= 'z') || (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7))) { cp -= 32; low[k] = 1; }
    if (cp == '\t' || cp == '\v') cp = ' ';
    if (cp == 0xAD) { i += (u32)used; continue; }        /* (WP10) an optional hyphen shows only where a line breaks */
    if (cp == 0x2011) cp = '-';                          /* (WP10) a nonbreaking hyphen */
    cps[k++] = cp;
    i += (u32)used;
  }
  /* tracked changes as Word prints them: insertions underlined, deletions struck through, in colour */
  {
    static const u32 revc[] = { 0xC00000, 0x1F4FC8, 0x008040, 0x8A2BB0, 0xB85C00, 0x007A87 };   /* as the screen shows them */
    colour(b, f->rev ? revc[f->rid % 6] : f->color, "rg");
    if ((f->flags & (CF_UNDERLINE | CF_STRIKE)) || f->rev || (f->fx & FX_DSTRIKE)) colour(b, f->rev ? revc[f->rid % 6] : f->color, "RG");
  }
  /* a family without a bold or italic face: the regular one, stroked (bold) or slanted (italic) */
  synth_b = (f->flags & CF_BOLD) && !z->face->bold;
  synth_i = (f->flags & CF_ITALIC) && !z->face->italic;
  {
    /* (WP9) shadow, emboss and engrave: a grey copy behind, offset; outline: hollow letters (stroked,
       filled with white) */
    int pass, npass = 1;
    i32 off = size / 24 + 4, dx[2] = { 0, 0 }, dy[2] = { 0, 0 };
    u32 pc[2] = { 0, 0 };
    if (f->fx2 & (FX2_SHADOW | FX2_EMBOSS | FX2_ENGRAVE)) {
      npass = 2; pc[0] = 0x808080;
      dx[0] = f->fx2 & FX2_ENGRAVE ? -off : off; dy[0] = f->fx2 & FX2_ENGRAVE ? off : -off;
    }
    for (pass = 0; pass < npass; pass++) {
      int last = pass == npass - 1, hollow = last && (f->fx2 & (FX2_OUTLINE | FX2_EMBOSS | FX2_ENGRAVE));
      i32 px = x + dx[pass], py = y + dy[pass];
      if (!last) colour(b, pc[pass], "rg");
      else if (npass > 1) colour(b, f->rev ? 0xC00000 : f->color, "rg");
      if (synth_b || synth_i || hollow) buf_adds(b, "q ");
      if (hollow) {
        /* the letters' outline in the text's colour, white inside (emboss and engrave: white letters, a grey edge) */
        colour(b, f->fx2 & FX2_OUTLINE ? (f->rev ? 0xC00000 : f->color) : 0x808080, "RG");
        buf_printf(b, "%ld w ", (long)(size / 40 + 2));
        colour(b, 0xFFFFFF, "rg");
      }
      if (synth_b && !hollow) { colour(b, f->rev ? 0xC00000 : f->color, "RG"); buf_printf(b, "%ld w ", (long)(size / 30 + 1)); }
      buf_printf(b, "BT /F%d %ld Tf ", font_index(p, z->face), (long)size);
      if ((synth_b && last) || hollow) buf_adds(b, "2 Tr ");
      if (f->spacing) buf_printf(b, "%ld Tc ", (long)f->spacing);
      if (f->scale && f->scale != 100) buf_printf(b, "%u Tz ", (unsigned)f->scale);
      if (synth_i) buf_printf(b, "1 0 0.21 1 %ld %ld Tm ", (long)px, (long)py);
      else buf_printf(b, "%ld %ld Td ", (long)px, (long)py);
      /* small capitals: the lower case runs smaller */
      for (j = 0; j < k; ) {
        u32 e = j;
        int fi = font_index(p, z->face);
        while (e < k && low[e] == low[j]) e++;
        if (small) { fi = font_index(p, (low[j] && zs ? zs : z)->face); buf_printf(b, "/F%d %ld Tf ", fi, (long)(low[j] ? ssize : size)); }
        show(p, b, fi, cps + j, e - j);
        j = e;
      }
      buf_adds(b, (synth_b && last) || hollow ? "0 Tr ET\n" : "ET\n");
      if (synth_b || synth_i || hollow) buf_adds(b, "Q\n");
    }
  }
  if (f->spacing || (f->scale && f->scale != 100)) buf_adds(b, "BT 0 Tc 100 Tz ET\n");
  if (f->ulcolor && (f->flags & CF_UNDERLINE) && !f->rev) { colour(b, f->ulcolor & 0xFFFFFF, "rg"); colour(b, f->ulcolor & 0xFFFFFF, "RG"); }   /* (WP9) */
  if ((f->flags & CF_UNDERLINE) || f->rev == REV_INS) deco_line(b, f->rev == REV_INS ? UL_SINGLE : f->ul, x, y - size / 9, w, size);
  if (f->ulcolor && (f->flags & CF_UNDERLINE) && !f->rev) { colour(b, f->color, "rg"); colour(b, f->color, "RG"); }
  if ((f->flags & CF_STRIKE) || f->rev == REV_DEL) deco_line(b, UL_SINGLE, x, y + size / 4, w, size);
  if (f->fx & FX_DSTRIKE) deco_line(b, UL_DOUBLE, x, y + size / 4, w, size);
}
static void pdf_object(Pdf *p, Buf *b, int k, i32 x, i32 y);
/* (WP9) enclosed characters: the circle, square, triangle or diamond round the box, the text in
   its middle (shrunk to seven tenths unless the symbol is enlarged) */
static void wp9_pdf_enclosed(Pdf *p, Buf *b, const Box *bx, const char *t, i32 lx, i32 base)
{
  CharFmt f = bx->f;
  FaceSize *z;
  i32 size = f.size * 10, w = bx->w, x0 = lx + bx->x, cx = x0 + w / 2, h = size * (f.fx2 & FX2_ENLARGE ? 3 : 2) / 2 - size / 5,
      cy = base + size * 3 / 10, tw, r = w / 2 < h / 2 ? w / 2 : h / 2;
  int shape = (int)ENCLOSURE(&f);
  f.fx2 &= (u16)~(FX2_ENCLOSE | FX2_ENLARGE);
  if (!(bx->f.fx2 & FX2_ENLARGE)) f.size = (u16)((f.size * 7 + 5) / 10);
  z = lay_face(p->L, &f);
  tw = z ? (i32)((fonts_text(z, t + bx->off, bx->len) + QFINE / 2) / QFINE) : w;
  text(p, b, &f, t + bx->off, bx->len, cx - tw / 2, base + (bx->f.fx2 & FX2_ENLARGE ? 0 : size / 12), tw);
  colour(b, bx->f.color, "RG");
  buf_printf(b, "%ld w ", (long)(size / 20 + 4));
  if (shape == EN_CIRCLE) {
    /* four Bezier quarters (0.552 of the radius for the control points) */
    i32 k = r * 552 / 1000;
    buf_printf(b, "%ld %ld m %ld %ld %ld %ld %ld %ld c %ld %ld %ld %ld %ld %ld c %ld %ld %ld %ld %ld %ld c %ld %ld %ld %ld %ld %ld c S\n",
               (long)(cx + r), (long)cy, (long)(cx + r), (long)(cy + k), (long)(cx + k), (long)(cy + r), (long)cx, (long)(cy + r),
               (long)(cx - k), (long)(cy + r), (long)(cx - r), (long)(cy + k), (long)(cx - r), (long)cy,
               (long)(cx - r), (long)(cy - k), (long)(cx - k), (long)(cy - r), (long)cx, (long)(cy - r),
               (long)(cx + k), (long)(cy - r), (long)(cx + r), (long)(cy - k), (long)(cx + r), (long)cy);
  } else if (shape == EN_SQUARE) buf_printf(b, "%ld %ld %ld %ld re S\n", (long)(cx - r), (long)(cy - r), (long)(2 * r), (long)(2 * r));
  else if (shape == EN_TRIANGLE) buf_printf(b, "%ld %ld m %ld %ld l %ld %ld l h S\n", (long)(cx - r * 6 / 5), (long)(cy - r * 4 / 5), (long)(cx + r * 6 / 5), (long)(cy - r * 4 / 5), (long)cx, (long)(cy + r * 6 / 5));
  else buf_printf(b, "%ld %ld m %ld %ld l %ld %ld l %ld %ld l h S\n", (long)(cx - r * 6 / 5), (long)cy, (long)cx, (long)(cy + r * 6 / 5), (long)(cx + r * 6 / 5), (long)cy, (long)cx, (long)(cy - r * 6 / 5));
}
/* WP14a: an equation (t: its reference, in format f), its origin at x on baseline y (PDF's y up):
   its glyphs as text where Windows-1252 has them as they are, else as their outlines (filled); its
   rules and drawn shapes filled */
static void pdf_math(Pdf *p, Buf *b, const char *t, const CharFmt *f, i32 x, i32 y)
{
  const MLay *ml = math_layout(p->L->fs, p->L->d, math_index(t), f);
  i32 *seg = NULL;
  u32 i;
  int j;
  if (!ml) return;
  for (i = 0; i < ml->n; i++) {
    const MItem *it = &ml->it[i];
    colour(b, it->color == COLOR_AUTO ? f->color : it->color, "rg");
    if (it->kind == MI_RECT) buf_printf(b, "%ld %ld %ld %ld re f\n", (long)(x + it->x), (long)(y - it->y - it->h), (long)it->w, (long)it->h);
    else if (it->kind == MI_POLY) {
      for (j = 0; j < it->h; j++)
        buf_printf(b, "%ld %ld %c ", (long)(x + ml->pt[it->w + 2 * j]), (long)(y - ml->pt[it->w + 2 * j + 1]), j ? 'l' : 'm');
      buf_adds(b, "h f\n");
    } else if (it->kind == MI_GLYPH && it->face < ml->nfaces) {
      Face *fc = ml->faces[it->face];
      int code = cp_code(it->cp), n;
      if (!it->flags && (code != '?' || it->cp == '?') && ttf_glyph(fc, code_cp(code)) == it->glyph) {
        char one[2];
        one[0] = (char)code; one[1] = 0;
        buf_printf(b, "BT /F%d %ld Tf %ld %ld Td ", font_index(p, fc), (long)it->size, (long)(x + it->x), (long)(y - it->y));
        pdf_string(b, one);
        buf_adds(b, " Tj ET\n");
        continue;
      }
      /* the outline */
      if (!seg && !(seg = (i32 *)malloc(4 * 4000 * sizeof(i32)))) continue;
      n = math_glyph_segments(fc, it->glyph, seg, 4000);
      if (n > 0) {
        i32 upm = fc->upm ? fc->upm : 2048, adv = fc->nmetrics ? fc->adv[it->glyph < fc->nmetrics ? it->glyph : fc->nmetrics - 1] : upm / 2, lo = seg[1], hi = seg[1];
        i32 px = 0x7FFFFFFF, py = 0;
        for (j = 0; j < n; j++) { if (seg[4 * j + 1] < lo) lo = seg[4 * j + 1]; if (seg[4 * j + 1] > hi) hi = seg[4 * j + 1]; }
        for (j = 0; j < n; j++) {
          int e;
          for (e = 0; e < 2; e++) {
            i32 u = seg[4 * j + 2 * e], v = seg[4 * j + 2 * e + 1], X, Y;
            if (it->flags & MIF_FLIPX) u = adv - u;
            if (it->flags & MIF_FLIPY) v = lo + hi - v;
            X = x + it->x + (i32)(((long long)u * it->size + upm / 2) / upm);
            Y = y - it->y + (i32)(((long long)v * it->size + (v >= 0 ? upm / 2 : -upm / 2)) / upm);
            if (e == 0 && (X != px || Y != py)) buf_printf(b, "%ld %ld m ", (long)X, (long)Y);
            else if (e == 1) buf_printf(b, "%ld %ld l ", (long)X, (long)Y);
            if (e == 1) { px = X; py = Y; }
          }
        }
        buf_adds(b, "f\n");
      }
    }
  }
  free(seg);
}
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
  /* highlighting behind the text first (trailing spaces left out); character shading under it (WP9) */
  for (k = ln->box0; k < ln->box0 + ln->nbox; k++) {
    Box *bx = &pl->boxes[k];
    i32 size = bx->f.size * 10;
    if (bx->f.shade && !bx->f.hl && bx->w > 0 && bx->kind != BX_BREAK && bx->kind != BX_PIC && !(bx->kind == BX_SPACE && k + 1 == ln->box0 + ln->nbox)) {
      colour(b, bx->f.shade & 0xFFFFFF, "rg");
      buf_printf(b, "%ld %ld %ld %ld re f\n", (long)(lx + bx->x), (long)(base - size / 4), (long)bx->w, (long)(size * 23 / 20));
    }
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
    if (LAY_HIDDEN(p->L, bx)) continue;                     /* (WP9: hidden text isn't printed) */
    if (ENCLOSURE(&bx->f) && bx->kind == BX_TEXT) { wp9_pdf_enclosed(p, b, bx, pl->p->t, lx, base); continue; }   /* (WP9) */
    if (bx->f.link && (bx->kind == BX_TEXT || bx->kind == BX_SPACE || bx->kind == BX_FIELD) && bx->w > 0) {
      i32 size = bx->f.size * 10;
      link_area(p, LK_URI, bx->f.link, lx + bx->x, base - size / 4, lx + bx->x + bx->w, base + size * 9 / 10);
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
      int ni = note_index(pl->p->t + bx->off), en = bx->kind == BX_ENOTE;
      Doc *d = p->L->d;
      lay_note_label(pl, k, num);
      text(p, b, &bx->f, num, (u32)strlen(num), lx + bx->x, base, bx->w);
      if (ni >= 0 && ni < (en ? d->nenotes : d->nnotes)) {
        /* a link to the note, and the place its number links back to */
        i32 size = bx->f.size * 10;
        PDest *ref = en ? p->eref : p->fref;
        link_area(p, en ? LK_ENOTE : LK_NOTE, (u32)ni, lx + bx->x, base - size / 4, lx + bx->x + bx->w, base + size * 9 / 10);
        dest_note(ref ? &ref[ni] : NULL, p->page, lx + bx->x, base + ln->ascent);
      }
    }
    else if (bx->kind == BX_FIELD) {
      char num[LAY_FIELD_TEXT];
      lay_field_text(p->L, pl->p, bx, p->page, num);
      text(p, b, &bx->f, num, (u32)strlen(num), lx + bx->x, base, bx->w);
    }
    else if (bx->kind == BX_MATH) pdf_math(p, b, pl->p->t + bx->off, &bx->f, lx + bx->x, base);     /* WP14a */
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
  u32 cps[80];
  u32 i = 0, k = 0, n = (u32)strlen(d->wm.text);
  long w72, want, size, w, cx, cy, x0, y0;
  if (!z) return;
  while (i < n && k < sizeof cps / sizeof cps[0]) { int used; cps[k++] = utf8_get(d->wm.text + i, n - i, &used); i += (u32)used; }
  w72 = (long)(fonts_text(z, d->wm.text, n) / QFINE);
  if (w72 <= 0) return;
  want = d->wm.horizontal ? bw * 9 / 10 : (bw + bh) * 47 / 100;    /* most of the diagonal (near enough its length) */
  size = 144L * want / w72;
  if (size > 4000) size = 4000;
  if (!(z = fonts_size(p->L->fs, d->wm.font[0] ? d->wm.font : "Calibri", 0, 0, (u16)size))) return;
  w = (long)(fonts_text(z, d->wm.text, n) / QFINE);
  cx = pg->left + bw / 2; cy = H - (pg->top + bh / 2);
  colour(b, d->wm.opaque ? d->wm.color : wm_colour(d->wm.color), "rg");      /* (WP22a: or solid) */
  if (d->wm.horizontal) { x0 = cx - w / 2; y0 = cy - size * 10 * 7 / 20; buf_printf(b, "BT /F%d %ld Tf %ld %ld Td ", font_index(p, z->face), size * 10, x0, y0); }
  else {
    /* along the diagonal: start half the width back, half the capital's height down (rotated) */
    long hh = size * 10 * 7 / 20;
    x0 = cx - (w / 2) * 7071 / 10000 + hh * 7071 / 10000;
    y0 = cy - (w / 2) * 7071 / 10000 - hh * 7071 / 10000;
    buf_printf(b, "BT /F%d %ld Tf 0.7071 0.7071 -0.7071 0.7071 %ld %ld Tm ", font_index(p, z->face), size * 10, x0, y0);
  }
  show(p, b, font_index(p, z->face), cps, k);
  buf_adds(b, "ET\n");
}
/* the page border (WP22a: the page's section's, each side its own kind: core/pages.c's rectangles) */
static void page_border(Pdf *p, Buf *b)
{
  PBRect *r = NULL;
  const PageSetup *pg = lay_page(p->L, p->page);
  int n = pages_border_rects(p->L, p->page, &r), i;
  u32 last = 0xFFFFFFFF;
  for (i = 0; i < n; i++) {
    if (r[i].color != last) { colour(b, r[i].color, "rg"); last = r[i].color; }
    buf_printf(b, "%ld %ld %ld %ld re f\n", (long)r[i].x0, (long)(pg->height - r[i].y1), (long)(r[i].x1 - r[i].x0), (long)(r[i].y1 - r[i].y0));
  }
  free(r);
}
/* (WP22a) the picture watermark, centred on the text area, behind everything (washed out: the
   picture's samples mapped to its pale end, by /Decode or, decoded, pages_washout) */
static void wm_picture(Pdf *p, Buf *b)
{
  Doc *d = p->L->d;
  const PageSetup *pg = lay_page(p->L, p->page);
  i32 x, y, w, h;
  int ri;
  pages_wm_box(d, pg, &x, &y, &w, &h);
  memset(&p->wmimg, 0, sizeof p->wmimg);
  p->wmimg.data = d->wm.pic; p->wmimg.len = d->wm.piclen; p->wmimg.type = d->wm.pictype; p->wmimg.pw = d->wm.pw; p->wmimg.ph = d->wm.ph;
  p->wmimg.cx = w; p->wmimg.cy = h; p->wmimg.shared = 1;
  pages_wash_mark(d->wm.washout ? d->wm.pic : NULL);
  if ((ri = img_ref(p, &p->wmimg)) < 0) return;
  buf_printf(b, "q %ld 0 0 %ld %ld %ld cm /Im%d Do Q\n", (long)w, (long)h, (long)x, (long)(pg->height - y - h), ri);
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
/* WP17/WP18: an object with adjustments, a gradient or picture fill or effects drawn by objfx_render
   at up to 150 dpi, as an image with a soft mask (its centre at cx, cy); 0 if it is not one (or a page
   is being drawn as a picture, whose drawing knows no soft masks: then it is drawn plainly) */
static int pdf_rich(Pdf *p, Buf *b, const ObjPart *pt, i32 cx, i32 cy)
{
  Image t = *pt->im;
  ObjRaster r;
  struct PRich *nr;
  long num = 150, den = 1440, px;
  i32 k;
  t.cx = pt->cx; t.cy = pt->cy; t.rot = pt->rot; t.flip = pt->flip;
  if (p->raster || !objfx_rich(&t)) return 0;
  px = ((long)t.cx + 2 * objfx_margin(&t)) * num / den * (((long)t.cy + 2 * objfx_margin(&t)) * num / den);
  while (px > 2500000L && num > 20) { num = num * 2 / 3; px = px * 4 / 9; }
  if (!objfx_render(&t, num, den, &r)) return 0;
  if (!(nr = (struct PRich *)realloc(p->rich, (size_t)(p->nrich + 1) * sizeof *nr))) { objraster_free(&r); return 1; }
  p->rich = nr; nr += p->nrich;
  nr->w = r.w; nr->h = r.h;
  nr->rgb = (u8 *)malloc((size_t)r.w * r.h * 3); nr->a = (u8 *)malloc((size_t)r.w * r.h);
  if (!nr->rgb || !nr->a) { free(nr->rgb); free(nr->a); objraster_free(&r); return 1; }
  for (k = 0; k < r.w * r.h; k++) { memcpy(nr->rgb + k * 3, r.px + k * 4, 3); nr->a[k] = r.px[k * 4 + 3]; }
  buf_printf(b, "q %ld 0 0 %ld %ld %ld cm /R%d Do Q\n", (long)r.w * den / num, (long)r.h * den / num, (long)(cx - obj_w(&t) / 2 + (long)r.ox * den / num),
             (long)(cy + obj_h(&t) / 2 - (long)r.oy * den / num - (long)r.h * den / num), p->nrich);
  p->nrich++;
  objraster_free(&r);
  return 1;
}
/* (WP15a) a chart or SmartArt (its pack's primitives, pixels at 96 dpi: 15 twips each) in the w x h box
   whose bottom left is x0, y0 in PDF space */
static void pdf_pack(Pdf *p, Buf *b, const Image *im, i32 x0, i32 y0, i32 w, i32 h)
{
  Doc *d = p->L->d;
  const char *fam = d->defcf.font < d->nfonts ? d->fonts[d->defcf.font] : "Calibri";
  const PrimList *l = pack_prims(im->pack, w, h, p->L->fs, fam);
  int i, k, font = d->defcf.font < d->nfonts ? d->defcf.font : 0;
  for (k = 0; k < d->nfonts; k++) if (im->pack->font[0] && str_ieq(d->fonts[k], im->pack->font)) font = k;
#define PX(v) ((long)x0 + (long)(v) * 15)
#define PY(v) ((long)y0 + h - (long)(v) * 15)
  if (!l) return;
  buf_printf(b, "q %ld %ld %ld %ld re W n 1 j\n", (long)x0, (long)y0, (long)w, (long)h);
  for (i = 0; i < l->n; i++) {
    const Prim *q = &l->p[i];
    long lw = (long)(q->width ? q->width : 1) * 15;
    switch (q->t) {
    case PR_RECT:
      if (q->fill) { colour(b, q->fill & 0xFFFFFF, "rg"); buf_printf(b, "%ld %ld %ld %ld re f\n", PX(q->x), PY(q->y + q->h), (long)q->w * 15, (long)q->h * 15); }
      if (q->line) { colour(b, q->line & 0xFFFFFF, "RG"); buf_printf(b, "%ld w %ld %ld %ld %ld re S\n", lw, PX(q->x), PY(q->y + q->h), (long)q->w * 15, (long)q->h * 15); }
      break;
    case PR_LINE: case PR_POLY:
      if (q->np < 2 || (!q->fill && !q->line)) break;
      for (k = 0; k < q->np; k++) buf_printf(b, "%ld %ld %s ", PX(q->pts[2 * k]), PY(q->pts[2 * k + 1]), k ? "l" : "m");
      if (q->t == PR_POLY) buf_adds(b, "h ");
      if (q->t == PR_POLY && q->fill) colour(b, q->fill & 0xFFFFFF, "rg");
      if (q->line) { colour(b, q->line & 0xFFFFFF, "RG"); buf_printf(b, "%ld w ", lw); }
      buf_adds(b, q->t == PR_LINE ? "S\n" : q->fill && q->line ? "B\n" : q->fill ? "f\n" : "S\n");
      break;
    case PR_ELLIPSE: {
      /* four Bezier quarters; 0.5523 of the radius out along the tangents */
      long rx = (long)q->w * 15 / 2, ry = (long)q->h * 15 / 2, cx = PX(q->x) + rx, cy = PY(q->y) - ry, kx = rx * 5523 / 10000, ky = ry * 5523 / 10000;
      if (!q->fill && !q->line) break;
      buf_printf(b, "%ld %ld m %ld %ld %ld %ld %ld %ld c %ld %ld %ld %ld %ld %ld c %ld %ld %ld %ld %ld %ld c %ld %ld %ld %ld %ld %ld c h ",
                 cx + rx, cy, cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry, cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy,
                 cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry, cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
      if (q->fill) colour(b, q->fill & 0xFFFFFF, "rg");
      if (q->line) { colour(b, q->line & 0xFFFFFF, "RG"); buf_printf(b, "%ld w ", lw); }
      buf_adds(b, q->fill && q->line ? "B\n" : q->fill ? "f\n" : "S\n");
      break; }
    case PR_TEXT: {
      CharFmt f;
      FaceSize *z;
      long tw, asc, ht, bx, top;
      if (!q->text) break;
      memset(&f, 0, sizeof f);
      f.font = (u8)font;
      f.size = (u16)(q->size * 3 / 2 > 0 ? q->size * 3 / 2 : 1);
      f.color = q->line & 0xFFFFFF;
      f.flags = (u8)((q->bold ? CF_BOLD : 0) | (q->italic ? CF_ITALIC : 0));
      if (!(z = lay_face(p->L, &f))) break;
      tw = (long)fonts_text(z, q->text, (u32)strlen(q->text)) / QFINE;
      asc = z->ascent; ht = z->ascent + z->descent;
      if (!q->vert) {
        bx = PX(q->x) - (q->align == 1 ? tw / 2 : q->align == 2 ? tw : 0);
        top = PY(q->y) + (q->valign == 1 ? ht / 2 : q->valign == 2 ? ht : 0);
        text(p, b, &f, q->text, (u32)strlen(q->text), (i32)bx, (i32)(top - asc), (i32)tw);
      } else {
        /* reading upwards: its box ht wide and tw high, the baseline asc in from its left */
        long left = PX(q->x) - (q->align == 1 ? ht / 2 : q->align == 2 ? ht : 0), bottom = PY(q->y) - (q->valign == 1 ? tw / 2 : q->valign == 2 ? tw : 0);
        buf_printf(b, "q 0 1 -1 0 %ld %ld cm\n", left + asc, bottom);
        text(p, b, &f, q->text, (u32)strlen(q->text), 0, 0, (i32)tw);
        buf_adds(b, "Q\n");
      }
      break; }
    }
  }
  buf_adds(b, "Q\n");
  if (im->lstyle != PB_NONE && im->lwidth) {
    i32 olw = im->lwidth * 5 / 2;
    colour(b, im->lcolor, "RG");
    buf_printf(b, "%ld w ", (long)(olw < 5 ? 5 : olw));
    dash(b, im->lstyle, olw);
    buf_printf(b, "%ld %ld %ld %ld re S [] 0 d\n", (long)x0, (long)y0, (long)w, (long)h);
  }
#undef PX
#undef PY
}
/* one simple object (pt: as obj_parts gives it) with its centre at cx, cy in PDF space */
static void pdf_leaf(Pdf *p, Buf *b, const ObjPart *pt, i32 cx, i32 cy)
{
  const Image *im = pt->im;
  if (im->pack) { pdf_pack(p, b, im, cx - pt->cx / 2, cy - pt->cy / 2, pt->cx, pt->cy); return; }   /* (WP15a) */
  if (im->ff && im->kind == OBJ_SHAPE) { ff_pdf(b, pt, cx, cy); return; }     /* WP20: freeforms and ink */
  i32 w = pt->cx, h = pt->cy, lw = im->lwidth * 5 / 2, c = icos(pt->rot), sn = isin(pt->rot);
  int fx = pt->flip & 1 ? -1 : 1, fy = pt->flip & 2 ? -1 : 1, ri;
  int outline = im->lstyle != PB_NONE && im->lwidth;
  if (lw < 5) lw = 5;
  if (pdf_rich(p, b, pt, cx, cy)) goto text;      /* WP17/WP18 */
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
text:
  /* a text box's (or shape's) text, turned with it (WP18; and a quarter more for text across its height) */
  if (im->text || im->fx.link) {
    Furn f;
    u32 j, l;
    if (lay_textbox(p->L, im, w, &f)) {
      int a = (pt->rot + (im->fx.vert == TV_VERT ? 900 : im->fx.vert == TV_VERT270 ? 2700 : 0)) % 3600;
      i32 tw = im->fx.vert ? h : w, th = im->fx.vert ? w : h, top = cy + th / 2, x0 = cx - tw / 2;
      if (a) {
        buf_printf(b, "q 1 0 0 1 %ld %ld cm ", (long)cx, (long)cy);
        fix4(b, icos(a)); fix4(b, -isin(a)); fix4(b, isin(a)); fix4(b, icos(a));
        buf_printf(b, "0 0 cm 1 0 0 1 %ld %ld cm\n", (long)-cx, (long)-cy);
      }
      buf_printf(b, "q %ld %ld %ld %ld re W n\n", (long)x0, (long)(cy - th / 2), (long)tw, (long)th);
      for (j = 0; j < f.n; j++) {
        i32 ty = f.f[j].y;
        for (l = 0; l < f.f[j].pl.nlines; l++) {
          Line *ln = &f.f[j].pl.lines[l];
          pdf_line(p, b, &f.f[j].pl, l, x0 + ln->left, top - (ty + ln->ascent));
          ty += ln->height;
        }
      }
      buf_adds(b, a ? "Q Q\n" : "Q\n");
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
static void note_marks(Pdf *p, Furn *f, u32 page, int end, i32 H, i32 left);
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
  else if (d->wm.pic) wm_picture(p, b);       /* (WP22a) */
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
    if (p->tocto && pc->para < d->np && p->tocto[pc->para] >= 0 && ln->width > 0)      /* a table of contents entry: to its heading */
      link_area(p, LK_PARA, (u32)p->tocto[pc->para], lx, H - pc->y - ln->height, lx + ln->width, H - pc->y);
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
      if (w >= 2) note_marks(p, &f, page, w == 3, H, pg->left);
      if (w >= 2 && sepy >= 0) buf_printf(b, "0 0 0 RG 10 w %ld %ld m %ld %ld l S\n", (long)pg->left, (long)(H - sepy), (long)(pg->left + L->sepw), (long)(H - sepy));   /* (WP22b: L->sepw) */
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
/* a PDF text string from UTF-8: a literal for plain ASCII, else UTF-16BE with its byte order mark */
static void pdf_text(Buf *b, const char *s)
{
  const char *q;
  size_t n = strlen(s), i = 0;
  for (q = s; *q && (u8)*q >= 32 && (u8)*q < 127; q++) ;
  if (!*q) { pdf_string(b, s); return; }
  buf_adds(b, "<FEFF");
  while (i < n) {
    int used;
    u32 cp = utf8_get(s + i, n - i, &used);
    if (cp > 0xFFFF) { cp -= 0x10000; buf_printf(b, "%04lX%04lX", (unsigned long)(0xD800 + (cp >> 10)), (unsigned long)(0xDC00 + (cp & 0x3FF))); }
    else buf_printf(b, "%04lX", (unsigned long)cp);
    i += (size_t)used;
  }
  buf_addc(b, '>');
}
/* the outline level of a heading paragraph (a "heading N" style, as the table of contents takes
   them), 0 for any other */
static int heading_level(const Doc *d, const Para *pp)
{
  const Style *st = &d->styles[pp->pf.style < d->nstyles ? pp->pf.style : 0];
  if (!strncmp(st->name, "heading ", 8) || !strncmp(st->name, "Heading ", 8)) return st->name[8] >= '1' && st->name[8] <= '9' ? st->name[8] - '0' : 1;
  return 0;
}
/* a paragraph's words (n bytes of t) for an outline title or to match a table of contents entry:
   tabs and breaks as spaces, pictures, notes, marks and fields left out, no spaces at the ends */
static void plain_text(Buf *b, const char *t, u32 n)
{
  u32 i = 0;
  while (i < n) {
    int used;
    u32 cp = utf8_get(t + i, n - i, &used);
    if (cp == '\t' || cp == '\v' || cp == ' ') { if (b->len && b->data[b->len - 1] != ' ') buf_addc(b, ' '); }
    else if (!(cp >= 0xE000 && cp <= 0xE0FF) && cp < 0xF0000) buf_add(b, t + i, (u32)used);
    i += (u32)used;
  }
  while (b->len && b->data[b->len - 1] == ' ') b->data[--b->len] = 0;
}
/* Quill's own table of contents has no links in it: each entry (the text before its page number)
   goes to the first heading after the last one matched with the same words. Entries with links of
   their own (Word's) keep them */
static void toc_targets(Pdf *p)
{
  Doc *d = p->L->d;
  u32 i, h, cur = 0, r;
  for (i = 0; i < d->np && !toc_para(d, d->p[i]); i++) ;
  if (i == d->np || !(p->tocto = (int *)malloc(d->np * sizeof(int)))) return;
  for (i = 0; i < d->np; i++) p->tocto[i] = -1;
  for (i = 0; i < d->np; i++) {
    Para *tp = d->p[i];
    Buf e;
    const char *tab;
    int linked = 0;
    if (!toc_para(d, tp)) continue;
    for (r = 0; r < tp->nr; r++) if (tp->r[r].f.link) linked = 1;
    if (linked) continue;
    buf_init(&e);
    tab = strrchr(tp->t, '\t');
    plain_text(&e, tp->t, tab ? (u32)(tab - tp->t) : tp->len);
    for (h = cur; e.len && h < d->np; h++) {
      Buf t;
      int same;
      if (!heading_level(d, d->p[h]) || toc_para(d, d->p[h])) continue;
      buf_init(&t);
      plain_text(&t, d->p[h]->t, d->p[h]->len);
      same = t.len == e.len && !memcmp(t.data, e.data, e.len);
      buf_free(&t);
      if (same) { p->tocto[i] = (int)h; cur = h + 1; break; }
    }
    buf_free(&e);
  }
}
/* the start of a note's text on the page being written (fp: its first paragraph): noted as where
   its reference goes, and its number made a link back to the reference */
static void note_start(Pdf *p, FPara *fp, int kind, u32 n, PDest *dst, i32 H, i32 left)
{
  Line *ln;
  Box *bx;
  i32 base, lx, size;
  if (!fp->pl.nlines) return;
  ln = &fp->pl.lines[0];
  lx = left + ln->left; base = H - (fp->y + ln->ascent);
  dest_note(dst, p->page, lx, H - fp->y);
  if (!ln->nbox) return;
  bx = &fp->pl.boxes[ln->box0];
  size = bx->f.size * 10;
  link_area(p, kind, n, lx + bx->x, base - size / 4, lx + bx->x + bx->w, base + size * 9 / 10);
}
/* the notes laid out at the foot of a page (end: the endnotes after the text): which paragraph
   starts which note, as lay_notes and lay_endnotes put them one after another */
static void note_marks(Pdf *p, Furn *f, u32 page, int end, i32 H, i32 left)
{
  Layout *L = p->L;
  Doc *d = L->d;
  u32 j = 0, i;
  if (p->raster) return;
  if (!end) {
    for (i = 0; i < L->npn && j < f->n; i++) {
      u32 n = L->pn[i].note;
      if (L->pn[i].page != page || n >= (u32)d->nnotes) continue;
      note_start(p, &f->f[j], LK_NOTEBACK, n, p->fnote ? &p->fnote[n] : NULL, H, left);
      j += d->notes[n].np ? d->notes[n].np : 1;
    }
  } else {
    for (i = 0; i < L->nen && j < f->n; i++) {
      u32 n = L->en[i].note;
      if (L->en[i].page != page) continue;
      if (L->en[i].para == 0 && n < (u32)d->nenotes) note_start(p, &f->f[j], LK_ENOTEBACK, n, p->enote ? &p->enote[n] : NULL, H, left);
      j++;
    }
  }
}
/* where a position in the body is: its line's top on its page */
static int dest_pos(Pdf *p, Pos pos, PDest *out)
{
  Layout *L = p->L;
  u32 page;
  i32 x, y, h;
  if (pos.para >= L->npl || pos.para >= L->d->np) return 0;
  lay_caret(L, pos, &page, &x, &y, &h);
  if (page >= L->npages) return 0;
  out->page = page; out->x = x; out->y = lay_page(L, page)->height - y; out->set = 1;
  return 1;
}
/* the PDF objects of the pages exported (layout page k: pgobj[k - first]) */
typedef struct { u32 first, last; u32 *pgobj; } PRange;
static u32 range_obj(const PRange *r, u32 page) { return page >= r->first && page <= r->last ? r->pgobj[page - r->first] : 0; }
/* a destination array: the page, its top a little above the place (points) */
static int dest_write(Buf *b, const PRange *r, const PDest *ds)
{
  u32 po;
  long top;
  if (!ds || !ds->set || !(po = range_obj(r, ds->page))) return 0;
  top = (ds->y + 119) / 20 + 6;
  buf_printf(b, "[%lu 0 R /XYZ null %ld null]", (unsigned long)po, top);
  return 1;
}
/* where a link goes (not for web addresses): 0 if nowhere in the pages exported */
static int link_dest(Pdf *p, const struct PLink *l, PDest *out)
{
  Doc *d = p->L->d;
  PDest *arr = NULL;
  memset(out, 0, sizeof *out);
  switch (l->kind) {
  case LK_NAME: {
    Pos a, b;
    const char *u = doc_link_url(d, (int)l->target);
    if (u[0] != '#' || !doc_bmark_find(d, u + 1, &a, &b)) return 0;
    return dest_pos(p, a, out); }
  case LK_PARA: { Pos a; a.para = l->target; a.off = 0; return dest_pos(p, a, out); }
  case LK_NOTE: arr = p->fnote; break;
  case LK_ENOTE: arr = p->enote; break;
  case LK_NOTEBACK: arr = p->fref; break;
  case LK_ENOTEBACK: arr = p->eref; break;
  default: return 0;
  }
  if (!arr) return 0;
  *out = arr[l->target];
  return out->set;
}
/* the document's outline from its headings (body paragraphs in heading styles), nested by level,
   every level open; written as objects from first on. Returns how many entries (0: none written) */
typedef struct { u32 para; int level; PDest d; int parent, first, last, prev, next; u32 count; } OItem;
static u32 outline_items(Pdf *p, const PRange *r, OItem **out)
{
  Doc *d = p->L->d;
  OItem *it;
  u32 i, n = 0, j;
  int stack[10], sp = 0;
  *out = NULL;
  for (i = 0; i < d->np; i++) if (heading_level(d, d->p[i]) && d->p[i]->len && !toc_para(d, d->p[i])) n++;
  if (!n || !(it = (OItem *)calloc(n, sizeof(OItem)))) return 0;
  n = 0;
  for (i = 0; i < d->np; i++) {
    Pos a;
    int lv = heading_level(d, d->p[i]);
    if (!lv || !d->p[i]->len || toc_para(d, d->p[i])) continue;
    a.para = i; a.off = 0;
    if (!dest_pos(p, a, &it[n].d) || !range_obj(r, it[n].d.page)) continue;
    it[n].para = i; it[n].level = lv;
    it[n].first = it[n].last = it[n].prev = it[n].next = -1;
    while (sp && it[stack[sp - 1]].level >= lv) sp--;
    it[n].parent = sp ? stack[sp - 1] : -1;
    if (sp < 10) stack[sp++] = (int)n;
    n++;
  }
  for (i = 0; i < n; i++) {
    int par = it[i].parent;
    /* the last child of the parent so far: the latest entry before i with the same parent */
    for (j = i; j-- > 0;) if (it[j].parent == par) { it[j].next = (int)i; it[i].prev = (int)j; break; }
    if (par >= 0) { if (it[par].first < 0) it[par].first = (int)i; it[par].last = (int)i; }
    for (j = i + 1; j < n && it[j].level > it[i].level; j++) ;
    it[i].count = j - i - 1;
  }
  if (!n) { free(it); return 0; }
  *out = it;
  return n;
}
/* a font's objects: a simple font (named only, or a whole file when a subset can't be made), or a
   Type 0 font with its CIDFont, descriptor, subset and ToUnicode CMap. Returns the font's object */
typedef struct { u32 code, uni; u16 g; } GUse;
static int guse_cmp(const void *a, const void *b) { u32 x = ((const GUse *)a)->code, y = ((const GUse *)b)->code; return x < y ? -1 : x > y; }
static u32 font_write(Pdf *p, int i)
{
  PFont *pf = &p->fonts[i];
  Face *f = pf->face;
  char name[64];
  i32 upm = f->upm ? f->upm : 1000;
  u32 fo = obj_new(p), desc, ffo = 0;
  ps_name(name, f);
  if (pf->kind == PF_SIMPLE) {
    size_t flen = 0;
    u8 *file = fonts_embeddable(f) ? fonts_face_file(f, &flen) : NULL;
    int c;
    desc = obj_new(p);
    if (file) ffo = obj_new(p);
    obj_start(p, fo);
    buf_printf(&p->o, "<</Type /Font /Subtype /%s /BaseFont /%s /FirstChar 32 /LastChar 255 /Encoding /WinAnsiEncoding /FontDescriptor %lu 0 R /Widths [",
               f->cff ? "Type1" : "TrueType", name, (unsigned long)desc);
    for (c = 32; c < 256; c++) {
      u32 cp = code_cp(c);
      u16 g = cp ? ttf_glyph(f, cp) : 0;
      u16 adv = f->nmetrics ? f->adv[g < f->nmetrics ? g : f->nmetrics - 1] : 500;
      buf_printf(&p->o, "%ld ", (long)adv * 1000 / upm);
    }
    buf_adds(&p->o, "]>>\nendobj\n");
    obj_start(p, desc);
    buf_printf(&p->o, "<</Type /FontDescriptor /FontName /%s /Flags %d /FontBBox [-600 %ld 2000 %ld] /ItalicAngle %d /Ascent %ld /Descent %ld /CapHeight %ld /StemV %d",
               name, 32 | (f->italic ? 64 : 0), (long)(-f->descent * 1000 / upm), (long)(f->ascent * 1000 / upm), f->italic ? -12 : 0,
               (long)(f->ascent * 1000 / upm), (long)(-f->descent * 1000 / upm), (long)(f->ascent * 700 / upm), f->bold ? 140 : 80);
    if (file) buf_printf(&p->o, " /FontFile%s %lu 0 R", f->cff ? "3" : "2", (unsigned long)ffo);
    buf_adds(&p->o, ">>\nendobj\n");
    if (file) {
      char dict[64];
      if (f->cff) strcpy(dict, "/Subtype /OpenType");          /* (a whole OpenType font: PDF 1.6) */
      else sprintf(dict, "/Length1 %lu", (unsigned long)flen);
      stream(p, ffo, dict, file, flen);
      free(file);
    }
    return fo;
  }
  {
    u32 cidf = obj_new(p), tu, g, n = 0, k, h = 2166136261UL;
    GUse *gu;
    char tag[8];
    size_t flen = pf->flen, slen = 0;
    u8 *file = pf->file ? pf->file : fonts_face_file(f, &flen), *sub;
    Buf cm;
    desc = obj_new(p); ffo = obj_new(p); tu = obj_new(p);
    pf->used[0] |= 1;
    for (g = 0; g < f->nglyphs; g++) if (pf->used[g >> 3] & (1 << (g & 7))) n++;
    if (!(gu = (GUse *)malloc((n ? n : 1) * sizeof(GUse)))) { p->o.oom = 1; return fo; }
    for (g = 0, n = 0; g < f->nglyphs; g++)
      if (pf->used[g >> 3] & (1 << (g & 7))) { gu[n].g = (u16)g; gu[n].code = glyph_code(pf, (u16)g); gu[n].uni = pf->uni[g]; n++; }
    if (pf->cid) qsort(gu, n, sizeof(GUse), guse_cmp);
    /* the subset's tag: six capitals from the glyphs it holds */
    for (g = 0; g < ((u32)f->nglyphs + 7) / 8; g++) h = (h ^ pf->used[g]) * 16777619UL;
    for (g = 0; f->family[g]; g++) h = (h ^ (u8)f->family[g]) * 16777619UL;
    for (k = 0; k < 6; k++) { tag[k] = (char)('A' + h % 26); h /= 26; }
    tag[6] = 0;
    obj_start(p, fo);
    buf_printf(&p->o, "<</Type /Font /Subtype /Type0 /BaseFont /%s+%s /Encoding /Identity-H /DescendantFonts [%lu 0 R] /ToUnicode %lu 0 R>>\nendobj\n",
               tag, name, (unsigned long)cidf, (unsigned long)tu);
    obj_start(p, cidf);
    buf_printf(&p->o, "<</Type /Font /Subtype /%s /BaseFont /%s+%s /CIDSystemInfo <</Registry (Adobe) /Ordering (Identity) /Supplement 0>> /FontDescriptor %lu 0 R /DW %ld /W [",
               pf->kind == PF_CFF ? "CIDFontType0" : "CIDFontType2", tag, name, (unsigned long)desc, (long)f->adv[0] * 1000 / upm);
    for (k = 0; k < n; ) {
      /* runs of consecutive codes: c [w1 w2 ...] */
      u32 e = k + 1;
      while (e < n && gu[e].code == gu[e - 1].code + 1) e++;
      buf_printf(&p->o, "%lu [", (unsigned long)gu[k].code);
      for (; k < e; k++) buf_printf(&p->o, "%ld ", (long)f->adv[gu[k].g] * 1000 / upm);
      buf_adds(&p->o, "] ");
    }
    buf_printf(&p->o, "]%s>>\nendobj\n", pf->kind == PF_TT ? " /CIDToGIDMap /Identity" : "");
    obj_start(p, desc);
    buf_printf(&p->o, "<</Type /FontDescriptor /FontName /%s+%s /Flags %d /FontBBox [-600 %ld 2000 %ld] /ItalicAngle %d /Ascent %ld /Descent %ld /CapHeight %ld /StemV %d",
               tag, name, 4 | (f->italic ? 64 : 0), (long)(-f->descent * 1000 / upm), (long)(f->ascent * 1000 / upm), f->italic ? -12 : 0,
               (long)(f->ascent * 1000 / upm), (long)(-f->descent * 1000 / upm), (long)(f->ascent * 700 / upm), f->bold ? 140 : 80);
    buf_printf(&p->o, " /FontFile%s %lu 0 R>>\nendobj\n", pf->kind == PF_CFF ? "3" : "2", (unsigned long)ffo);
    sub = file ? font_subset(file, flen, pf->used, f->nglyphs, &slen) : NULL;
    if (!sub && file) { sub = file; slen = flen; file = NULL; }       /* the whole font, if it can't be cut down */
    if (sub) {
      char dict[64];
      if (pf->kind == PF_CFF) strcpy(dict, "/Subtype /OpenType");
      else sprintf(dict, "/Length1 %lu", (unsigned long)slen);
      stream(p, ffo, dict, sub, slen);
    } else { obj_start(p, ffo); buf_adds(&p->o, "null\nendobj\n"); }
    if (sub != pf->file) free(sub);
    if (file != pf->file) free(file);
    /* the ToUnicode CMap: each code's character, a hundred to a block */
    buf_init(&cm);
    buf_adds(&cm, "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n/CIDSystemInfo <</Registry (Adobe) /Ordering (UCS) /Supplement 0>> def\n"
                  "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n");
    for (k = 0; k < n; ) {
      u32 e, c = 0, j;
      for (e = k; e < n && c < 100; e++) if (gu[e].uni) c++;
      if (c) {
        buf_printf(&cm, "%lu beginbfchar\n", (unsigned long)c);
        for (j = k; j < e; j++) {
          u32 u = gu[j].uni;
          if (!u) continue;
          if (u > 0xFFFF) { u -= 0x10000; buf_printf(&cm, "<%04lX> <%04lX%04lX>\n", (unsigned long)gu[j].code, (unsigned long)(0xD800 + (u >> 10)), (unsigned long)(0xDC00 + (u & 0x3FF))); }
          else buf_printf(&cm, "<%04lX> <%04lX>\n", (unsigned long)gu[j].code, (unsigned long)u);
        }
        buf_adds(&cm, "endbfchar\n");
      }
      k = e;
    }
    buf_adds(&cm, "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n");
    if (cm.oom) p->o.oom = 1;
    stream(p, tu, "", (const u8 *)(cm.data ? cm.data : ""), cm.len);
    buf_free(&cm);
    free(gu);
  }
  return fo;
}
static int name_cmp(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
/* the pages' content goes first, into separate buffers, since only then are the fonts and
   pictures known. Then the fonts, pictures, pages with their link annotations, the outline, the
   named destinations, the catalog (1), page tree (2) and document information (3). The result is
   malloc'd */
static u8 *pdf_export_laid(Layout *L, const PdfOpts *o, size_t *len);
/* (WP9) hidden text is left out of the PDF: the layout without it while the PDF is made, then as it was */
u8 *pdf_export_opts(Layout *L, const PdfOpts *o, size_t *len)
{
  u8 shown = L->showhidden, *r;
  int codes = field_codes_any(L->d);       /* (WP1) fields show their results in a PDF, not their codes */
  L->showhidden = 0;
  if (codes) field_show_codes(L->d, 0);
  r = pdf_export_laid(L, o, len);
  if (codes) field_show_codes(L->d, codes > 1);
  if (shown || codes) { L->showhidden = shown; lay_update(L); }
  return r;
}
static u8 *pdf_export_laid(Layout *L, const PdfOpts *o, size_t *len)
{
  Pdf p;
  Doc *d = L->d;
  u32 i, k, xref, npg, *fobj = NULL, *imobj = NULL, *annobj = NULL, outl = 0, nout = 0, names = 0, richobj = 0;
  Buf *pages;
  PRange r;
  PDest *ldest = NULL;
  OItem *it = NULL;
  int v6 = 0, ok = 0;
  if (!o) o = &pdf_options;
  if (!lay_update(L) || !L->npages) return NULL;
  memset(&p, 0, sizeof p);
  p.L = L; p.opt = o;
  buf_init(&p.o);
  r.first = o->first ? o->first - 1 : 0;
  r.last = o->last && o->last <= L->npages ? o->last - 1 : L->npages - 1;
  if (r.first > r.last) r.first = r.last;
  npg = r.last - r.first + 1;
  if (d->nnotes) { p.fnote = (PDest *)calloc((size_t)d->nnotes, sizeof(PDest)); p.fref = (PDest *)calloc((size_t)d->nnotes, sizeof(PDest)); }
  if (d->nenotes) { p.enote = (PDest *)calloc((size_t)d->nenotes, sizeof(PDest)); p.eref = (PDest *)calloc((size_t)d->nenotes, sizeof(PDest)); }
  toc_targets(&p);
  /* the pages' content first: it tells which fonts are used */
  r.pgobj = (u32 *)calloc(npg, sizeof(u32));
  if (!(pages = (Buf *)calloc(npg, sizeof(Buf))) || !r.pgobj) goto fail;
  for (k = 0; k < npg; k++) { buf_init(&pages[k]); page_content(&p, r.first + k, &pages[k]); }
  p.nobj = 4;
  /* 1.4, or 1.6 for an OpenType font (CFF outlines) embedded */
  for (i = 0; i < (u32)p.nfonts; i++) if (p.fonts[i].face->cff && fonts_embeddable(p.fonts[i].face)) v6 = 1;
  buf_adds(&p.o, v6 ? "%PDF-1.6\n%\xE2\xE3\xCF\xD3\n" : "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");
  if (!(fobj = (u32 *)calloc((size_t)p.nfonts + 1, sizeof(u32))) || !(imobj = (u32 *)calloc((size_t)p.nimgs + 1, sizeof(u32)))) goto fail;
  for (i = 0; i < (u32)p.nfonts; i++) fobj[i] = font_write(&p, (int)i);
  /* the pictures: JPEG as it is, PNG as deflated RGB at up to 300 dpi */
  for (i = 0; i < (u32)p.nimgs; i++) {
    const Image *im = p.imgs[i];
    u32 on = imobj[i] = obj_new(&p);
    char dict[200];
    int dec = 0, nc = im->type == IMG_JPEG ? img_jpeg_components(im->data, im->len, &dec) : 3;
    if (im->type == IMG_JPEG && (nc == 1 || nc == 3)) {
      obj_start(&p, on);
      const char *dec = !pages_wash_is(im->data) ? "" : nc == 1 ? "/Decode [0.85 1] " : "/Decode [0.85 1 0.85 1 0.85 1] ";   /* (WP22a: washed out) */
      buf_printf(&p.o, "<</Type /XObject /Subtype /Image /Width %lu /Height %lu /ColorSpace /%s /BitsPerComponent 8 %s/Filter /DCTDecode /Length %lu>>\nstream\n",
                 (unsigned long)im->pw, (unsigned long)im->ph, nc == 1 ? "DeviceGray" : "DeviceRGB", dec, (unsigned long)im->len);
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
        if (pages_wash_is(im->data)) pages_washout(rgb, w * h);      /* (WP22a) */
        sprintf(dict, "/Type /XObject /Subtype /Image /Width %lu /Height %lu /ColorSpace /DeviceRGB /BitsPerComponent 8", (unsigned long)w, (unsigned long)h);
        stream(&p, on, dict, rgb, (size_t)w * h * 3);
        free(rgb);
      } else {                                /* one grey pixel in its place */
        static const u8 grey[3] = { 200, 200, 200 };
        stream(&p, on, "/Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceRGB /BitsPerComponent 8", grey, 3);
      }
    }
  }
  /* WP17/WP18: the drawn objects, each with its transparency as a soft mask (two objects each, from richobj) */
  for (i = 0; i < (u32)p.nrich; i++) {
    u32 on = obj_new(&p), sm = obj_new(&p);
    char dict[200];
    if (!i) richobj = on;
    sprintf(dict, "/Type /XObject /Subtype /Image /Width %ld /Height %ld /ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask %lu 0 R",
            (long)p.rich[i].w, (long)p.rich[i].h, (unsigned long)sm);
    stream(&p, on, dict, p.rich[i].rgb, (size_t)p.rich[i].w * p.rich[i].h * 3);
    sprintf(dict, "/Type /XObject /Subtype /Image /Width %ld /Height %ld /ColorSpace /DeviceGray /BitsPerComponent 8", (long)p.rich[i].w, (long)p.rich[i].h);
    stream(&p, sm, dict, p.rich[i].a, (size_t)p.rich[i].w * p.rich[i].h);
    free(p.rich[i].rgb); free(p.rich[i].a); p.rich[i].rgb = p.rich[i].a = NULL;
  }
  for (k = 0; k < npg; k++) r.pgobj[k] = obj_new(&p);
  /* the links that go somewhere, each an annotation */
  if (p.nlinks && (!(ldest = (PDest *)calloc((size_t)p.nlinks, sizeof(PDest))) || !(annobj = (u32 *)calloc((size_t)p.nlinks, sizeof(u32))))) goto fail;
  for (i = 0; i < (u32)p.nlinks; i++)
    if (p.links[i].kind == LK_URI || (link_dest(&p, &p.links[i], &ldest[i]) && range_obj(&r, ldest[i].page))) annobj[i] = obj_new(&p);
  for (k = 0; k < npg; k++) {
    u32 page = r.first + k, co = obj_new(&p);
    int j, any = 0;
    obj_start(&p, r.pgobj[k]);
    buf_printf(&p.o, "<</Type /Page /Parent 2 0 R /MediaBox [0 0 %ld.%02ld %ld.%02ld] /Contents %lu 0 R /Resources <</Font <<",
               (long)(lay_page(L, page)->width / 20), (long)(lay_page(L, page)->width % 20 * 5), (long)(lay_page(L, page)->height / 20), (long)(lay_page(L, page)->height % 20 * 5),
               (unsigned long)co);
    for (i = 0; i < (u32)p.nfonts; i++) buf_printf(&p.o, "/F%lu %lu 0 R ", (unsigned long)i, (unsigned long)fobj[i]);
    buf_adds(&p.o, ">>");
    if (p.nimgs || p.nrich) {
      buf_adds(&p.o, " /XObject <<");
      for (i = 0; i < (u32)p.nimgs; i++) buf_printf(&p.o, "/Im%lu %lu 0 R ", (unsigned long)i, (unsigned long)imobj[i]);
      for (i = 0; i < (u32)p.nrich; i++) buf_printf(&p.o, "/R%lu %lu 0 R ", (unsigned long)i, (unsigned long)(richobj + 2 * i));     /* (WP17/WP18) */
      buf_adds(&p.o, ">>");
    }
    buf_adds(&p.o, " >>");
    /* its links' annotations */
    for (j = 0; j < p.nlinks; j++) if (p.links[j].page == page && annobj[j]) { buf_adds(&p.o, any ? " " : " /Annots ["); buf_printf(&p.o, "%lu 0 R", (unsigned long)annobj[j]); any = 1; }
    if (any) buf_adds(&p.o, "]");
    buf_adds(&p.o, " >>\nendobj\n");
    stream(&p, co, "", (const u8 *)pages[k].data, pages[k].len);
    buf_free(&pages[k]);
  }
  for (i = 0; i < (u32)p.nlinks; i++) {
    /* a link: a rectangle that opens its address or goes to its place (no border drawn: the text
       is styled already) */
    struct PLink *l = &p.links[i];
    if (!annobj[i]) continue;
    obj_start(&p, annobj[i]);
    buf_printf(&p.o, "<</Type /Annot /Subtype /Link /Rect [%ld %ld %ld %ld] /Border [0 0 0] ", (long)l->x0, (long)l->y0, (long)l->x1, (long)l->y1);
    if (l->kind == LK_URI) { buf_adds(&p.o, "/A <</S /URI /URI "); pdf_string(&p.o, doc_link_url(d, (int)l->target)); buf_adds(&p.o, ">>"); }
    else { buf_adds(&p.o, "/Dest "); dest_write(&p.o, &r, &ldest[i]); }
    buf_adds(&p.o, " >>\nendobj\n");
  }
  /* the outline: the headings, nested */
  if (o->outline && (nout = outline_items(&p, &r, &it))) {
    outl = obj_new(&p);
    for (i = 0; i < nout; i++) obj_new(&p);
    obj_start(&p, outl);
    {
      u32 top = 0, firsttop = 0, lasttop = 0;
      for (i = 0; i < nout; i++) if (it[i].parent < 0) { if (!top++) firsttop = i; lasttop = i; }
      buf_printf(&p.o, "<</Type /Outlines /First %lu 0 R /Last %lu 0 R /Count %lu>>\nendobj\n", (unsigned long)(outl + 1 + firsttop), (unsigned long)(outl + 1 + lasttop), (unsigned long)nout);
    }
    for (i = 0; i < nout; i++) {
      Buf t;
      buf_init(&t);
      plain_text(&t, d->p[it[i].para]->t, d->p[it[i].para]->len);
      obj_start(&p, outl + 1 + i);
      buf_adds(&p.o, "<</Title ");
      pdf_text(&p.o, t.data ? t.data : "");
      buf_free(&t);
      buf_printf(&p.o, " /Parent %lu 0 R", (unsigned long)(it[i].parent < 0 ? outl : outl + 1 + (u32)it[i].parent));
      if (it[i].prev >= 0) buf_printf(&p.o, " /Prev %lu 0 R", (unsigned long)(outl + 1 + (u32)it[i].prev));
      if (it[i].next >= 0) buf_printf(&p.o, " /Next %lu 0 R", (unsigned long)(outl + 1 + (u32)it[i].next));
      if (it[i].first >= 0) buf_printf(&p.o, " /First %lu 0 R /Last %lu 0 R /Count %lu", (unsigned long)(outl + 1 + (u32)it[i].first), (unsigned long)(outl + 1 + (u32)it[i].last), (unsigned long)it[i].count);
      buf_adds(&p.o, " /Dest ");
      dest_write(&p.o, &r, &it[i].d);
      buf_adds(&p.o, ">>\nendobj\n");
    }
  }
  /* the bookmarks (not the hidden ones Word makes) as named destinations, for links into the file */
  if (d->nbmarks) {
    const char **nm = (const char **)malloc((size_t)d->nbmarks * sizeof(char *));
    int nn = 0, j;
    for (j = 0; nm && j < d->nbmarks; j++) {
      Pos a, b;
      PDest ds;
      if (d->bmarks[j][0] && d->bmarks[j][0] != '_' && doc_bmark_find(d, d->bmarks[j], &a, &b) && dest_pos(&p, a, &ds) && range_obj(&r, ds.page)) nm[nn++] = d->bmarks[j];
    }
    if (nn) {
      qsort(nm, (size_t)nn, sizeof(char *), name_cmp);
      names = obj_new(&p);
      obj_start(&p, names);
      buf_adds(&p.o, "<</Names [");
      for (j = 0; j < nn; j++) {
        Pos a, b;
        PDest ds;
        if (j && !strcmp(nm[j], nm[j - 1])) continue;
        doc_bmark_find(d, nm[j], &a, &b); dest_pos(&p, a, &ds);
        pdf_string(&p.o, nm[j]); buf_addc(&p.o, ' ');
        dest_write(&p.o, &r, &ds);
        buf_addc(&p.o, ' ');
      }
      buf_adds(&p.o, "]>>\nendobj\n");
    }
    free(nm);
  }
  obj_start(&p, 1);
  buf_adds(&p.o, "<</Type /Catalog /Pages 2 0 R");
  if (outl) buf_printf(&p.o, " /Outlines %lu 0 R /PageMode /UseOutlines", (unsigned long)outl);
  if (names) buf_printf(&p.o, " /Names <</Dests %lu 0 R>>", (unsigned long)names);
  buf_adds(&p.o, ">>\nendobj\n");
  obj_start(&p, 2);
  buf_adds(&p.o, "<</Type /Pages /Kids [");
  for (k = 0; k < npg; k++) buf_printf(&p.o, "%lu 0 R ", (unsigned long)r.pgobj[k]);
  buf_printf(&p.o, "] /Count %lu>>\nendobj\n", (unsigned long)npg);
  obj_start(&p, 3);
  buf_adds(&p.o, "<</Producer (Quill for the Amiga) /Creator (Quill)");
  if (d->title[0]) { buf_adds(&p.o, " /Title "); pdf_text(&p.o, d->title); }
  if (d->author[0]) { buf_adds(&p.o, " /Author "); pdf_text(&p.o, d->author); }
  if (o->subject[0]) { buf_adds(&p.o, " /Subject "); pdf_text(&p.o, o->subject); }
  if (o->keywords[0]) { buf_adds(&p.o, " /Keywords "); pdf_text(&p.o, o->keywords); }
  buf_adds(&p.o, ">>\nendobj\n");
  xref = (u32)p.o.len;
  buf_printf(&p.o, "xref\n0 %lu\n0000000000 65535 f \n", (unsigned long)p.nobj);
  for (i = 1; i < p.nobj; i++) buf_printf(&p.o, "%010lu 00000 n \n", (unsigned long)(i < p.objcap ? p.off[i] : 0));
  buf_printf(&p.o, "trailer\n<</Size %lu /Root 1 0 R /Info 3 0 R>>\nstartxref\n%lu\n%%%%EOF\n", (unsigned long)p.nobj, (unsigned long)xref);
  ok = !p.o.oom;
fail:
  if (pages) { for (k = 0; k < npg; k++) buf_free(&pages[k]); free(pages); }
  free(r.pgobj); free(fobj); free(imobj); free(annobj); free(ldest); free(it);
  for (i = 0; i < (u32)p.nrich; i++) { free(p.rich[i].rgb); free(p.rich[i].a); }
  free(p.rich);
  free(p.links); free(p.off); free(p.fnote); free(p.fref); free(p.enote); free(p.eref); free(p.tocto);
  fonts_release(&p);
  if (!ok) { buf_free(&p.o); return NULL; }
  *len = p.o.len;
  return (u8 *)buf_take(&p.o);
}
u8 *pdf_export(Layout *L, size_t *len) { return pdf_export_opts(L, NULL, len); }
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
/* see pdf.h: the page's content as the PDF would have it, drawn by core/raster.c */
u8 *pdf_page_rgb(Layout *L, u32 page, int dpi, u32 *w, u32 *h)
{
  Pdf p;
  Buf b;
  RFont rf[64];
  int i;
  u8 *rgb = NULL;
  if (!lay_update(L) || page >= L->npages) return NULL;
  memset(&p, 0, sizeof p);
  p.L = L; p.opt = &pdf_options; p.raster = 1;
  buf_init(&b);
  page_content(&p, page, &b);
  for (i = 0; i < p.nfonts; i++) { rf[i].face = p.fonts[i].face; rf[i].simple = p.fonts[i].kind == PF_SIMPLE; }
  if (!b.oom) rgb = raster_page(b.data ? b.data : "", b.len, lay_page(L, page)->width, lay_page(L, page)->height, dpi, rf, p.nfonts, p.imgs, p.nimgs, w, h);
  buf_free(&b);
  free(p.links);
  fonts_release(&p);
  return rgb;
}
