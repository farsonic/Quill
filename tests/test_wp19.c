/* test_wp19.c - the native tests of WP19 WordArt and signature line (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp19().
   With WP19_OUT set to a folder, the documents made are also written there (to look at in Word or
   LibreOffice). */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/wordart.h"
#include "../core/charfx.h"
#include "../core/layout.h"
#include "../core/pdf.h"
#include "../core/zip.h"
#include "../core/image.h"

/* what Word 2016 writes for its "Fill: White; Outline: Blue, Accent color 1; Glow: Blue" style, and
   others: scheme colours with modifiers, transparency, a gradient */
static const char *const WORD_FX =
  "<w14:glow w14:rad=\"63500\"><w14:schemeClr w14:val=\"accent1\"><w14:alpha w14:val=\"60000\"/><w14:satMod w14:val=\"175000\"/></w14:schemeClr></w14:glow>"
  "<w14:textOutline w14:w=\"9525\" w14:cap=\"flat\" w14:cmpd=\"sng\" w14:algn=\"ctr\"><w14:solidFill><w14:schemeClr w14:val=\"accent1\"/></w14:solidFill>"
  "<w14:prstDash w14:val=\"solid\"/><w14:round/></w14:textOutline>"
  "<w14:textFill><w14:solidFill><w14:schemeClr w14:val=\"bg1\"/></w14:solidFill></w14:textFill>";
static const char *const GRAD_FX =
  "<w14:shadow w14:blurRad=\"38100\" w14:dist=\"19050\" w14:dir=\"2700000\" w14:sx=\"100000\" w14:sy=\"100000\" w14:kx=\"0\" w14:ky=\"0\" w14:algn=\"tl\">"
  "<w14:schemeClr w14:val=\"dk1\"><w14:alpha w14:val=\"60000\"/></w14:schemeClr></w14:shadow>"
  "<w14:reflection w14:blurRad=\"6350\" w14:stA=\"55000\" w14:stPos=\"0\" w14:endA=\"300\" w14:endPos=\"45500\" w14:dist=\"0\" w14:dir=\"5400000\" w14:fadeDir=\"5400000\" w14:sx=\"100000\" w14:sy=\"-100000\" w14:kx=\"0\" w14:ky=\"0\" w14:algn=\"bl\"/>"
  "<w14:textFill><w14:gradFill><w14:gsLst><w14:gs w14:pos=\"0\"><w14:srgbClr w14:val=\"FF0000\"/></w14:gs><w14:gs w14:pos=\"100000\"><w14:schemeClr w14:val=\"accent1\"><w14:lumMod w14:val=\"50000\"/></w14:schemeClr></w14:gs></w14:gsLst>"
  "<w14:lin w14:ang=\"5400000\" w14:scaled=\"0\"/></w14:gradFill></w14:textFill>"
  "<w14:props3d w14:extrusionH=\"0\" w14:contourW=\"0\" w14:prstMaterial=\"warmMatte\"><w14:bevelT w14:w=\"38100\" w14:h=\"38100\" w14:prst=\"angle\"/></w14:props3d>"
  "<w14:ligatures w14:val=\"standard\"/>";

static int count(const char *s, size_t n, const char *what)
{
  int c = 0;
  size_t l = strlen(what);
  const char *p = s, *e = s + n;
  while (p < e && (p = (const char *)memmem(p, (size_t)(e - p), what, l))) { c++; p += l; }
  return c;
}
/* a part of a .docx (malloc'd, NUL-terminated) */
static char *part(const u8 *docx, size_t n, const char *name, size_t *len)
{
  ZipReader z;
  char *s = NULL;
  u8 *d;
  *len = 0;
  if (!zip_open(&z, docx, n)) return NULL;
  if ((d = zip_read(&z, zip_find(&z, name), len))) { s = (char *)realloc(d, *len + 1); if (s) s[*len] = 0; else free(d); }
  zip_close(&z);
  return s;
}
static void out(const char *name, const u8 *data, size_t n)
{
  const char *dir = getenv("WP19_OUT");
  char path[512];
  if (!dir || !data) return;
  snprintf(path, sizeof path, "%s/%s", dir, name);
  file_write(path, data, n);
}
/* a one-paragraph story of text, all of it in rx */
static Story *story_of(const char *s, u16 rx, u16 size)
{
  Story *st = (Story *)calloc(1, sizeof(Story));
  ParaFmt pf;
  CharFmt cf;
  Para *p;
  Doc *t = doc_new();
  pf = t->styles[0].pf; cf = t->styles[0].cf;
  doc_free(t);
  pf.align = AL_CENTER;
  cf.rx = rx; cf.size = size; cf.flags |= CF_BOLD;
  p = para_new(&pf, &cf);
  para_append(p, s, (u32)strlen(s), &cf);
  st->p = (Para **)malloc(sizeof(Para *)); st->p[0] = p; st->np = 1;
  return st;
}
static int add_obj(Doc *d, u32 para, int k)
{
  Pos at;
  char ref[8];
  if (k < 0) return k;
  at.para = para; at.off = d->p[para]->len;
  pic_utf8(ref, k);
  doc_insert(d, &at, ref, 4, NULL);
  return k;
}
static Image *nth(Doc *d, int n)
{
  u32 i, j;
  for (i = 0; i < d->np; i++)
    for (j = 0; j + 4 <= d->p[i]->len; j++)
      if (IS_PIC(d->p[i]->t + j) && !n--) return &d->images[pic_index(d->p[i]->t + j)];
  return NULL;
}

static void parsing(void)
{
  u16 rx = rx_add(WORD_FX, strlen(WORD_FX)), g = rx_add(GRAD_FX, strlen(GRAD_FX)), r2, r3;
  const TextFx *fx = wa_get(rx), *gx = wa_get(g);
  TextFx t;
  ok("wordart: Word's style read: white fill, blue outline 0.75 pt, blue glow 5 pt at 60% transparency",
     fx && fx->fill == WF_SOLID && fx->fcol == 0xFFFFFF && fx->line == WF_SOLID && fx->lcol == 0x4472C4 && fx->lw == 15 &&
     fx->glow && fx->glrad == 100 && fx->glcol == 0x4472C4 && fx->glalpha == 60 && !fx->shadow);
  ok("wordart: a gradient (scheme colour darkened), shadow, reflection and bevel read",
     gx && gx->fill == WF_GRAD && gx->ngs == 2 && gx->gcol[0] == 0xFF0000 && gx->gcol[1] == 0x223962 && gx->gangle == 900 &&
     gx->shadow && gx->shdist == 30 && gx->shblur == 60 && gx->shdir == 450 && gx->shalpha == 60 &&
     gx->refl && gx->rsta == 55 && gx->renpos == 45 && gx->bevel == BV_ANGLE);
  ok("wordart: no effects, no WordArt", !wa_get(0) && !wa_get(rx_add("<w14:ligatures w14:val=\"standard\"/>", 35)));
  /* changing only the glow keeps the rest as written, in the schema's order */
  t = *fx; t.glcol = 0xFF0000; t.glrad = 200;
  r2 = wa_set(rx, &t, WA_GLOW);
  ok("wordart: a changed glow written afresh, the outline and fill kept as Word wrote them",
     strstr(rx_text(r2), "<w14:glow w14:rad=\"127000\"><w14:srgbClr w14:val=\"FF0000\"><w14:alpha w14:val=\"60000\"/>") == rx_text(r2) &&
     strstr(rx_text(r2), "<w14:schemeClr w14:val=\"bg1\"/>") && wa_get(r2) && wa_get(r2)->glcol == 0xFF0000 && wa_get(r2)->lw == 15);
  t = *gx; t.glow = 1; t.glcol = 0x00FF00; t.glrad = 100; t.fill = WF_SOLID; t.fcol = 0x123456;
  r3 = wa_set(g, &t, WA_GLOW | WA_FILL);
  {
    const char *s = rx_text(r3), *gl = strstr(s, "<w14:glow"), *sh = strstr(s, "<w14:shadow"), *tf = strstr(s, "<w14:textFill"), *p3 = strstr(s, "<w14:props3d"),
               *lg = strstr(s, "<w14:ligatures");
    ok("wordart: elements kept in the schema's order (glow, shadow, ..., textFill, props3d, ligatures)", gl && sh && tf && p3 && lg && gl < sh && sh < tf && tf < p3 && p3 < lg);
  }
  r3 = wa_set(g, &t, WA_ALL);
  ok("wordart: everything written afresh reads back the same", wa_get(r3) && wa_get(r3)->fill == WF_SOLID && wa_get(r3)->fcol == 0x123456 && wa_get(r3)->refl &&
     wa_get(r3)->renpos == 45 && wa_get(r3)->bevel == BV_ANGLE && strstr(rx_text(r3), "w14:ligatures"));
  memset(&t, 0, sizeof t);
  ok("wordart: all taken off leaves the kept typography alone", !strcmp(rx_text(wa_set(g, &t, WA_ALL)), "<w14:ligatures w14:val=\"standard\"/>"));
  {
    int i, all = 1;
    for (i = 0; i < WA_NSTYLES; i++) {
      TextFx s, *b;
      u16 r;
      wa_style(i, &s);
      r = wa_set(0, &s, WA_ALL);
      b = (TextFx *)wa_get(r);
      if (!b || b->fill != s.fill || b->line != s.line || b->shadow != s.shadow || b->glow != s.glow || b->refl != s.refl || b->bevel != s.bevel) all = 0;
    }
    ok("wordart: every style written and read back", all);
  }
}
static void warps(void)
{
  i32 x, y;
  wa_warp_map(WW_ARCHUP, 4000, 2000, 300, 32768, 0, &x, &y);
  ok("warp: arch up's top middle at the frame's top", x > 1990 && x < 2010 && y < 10);
  wa_warp_map(WW_ARCHUP, 4000, 2000, 300, 0, 65536, &x, &y);
  ok("warp: arch up's left foot inside the left edge, half way down", x > 100 && x <= 1210 && y > 990 && y < 1010);
  wa_warp_map(WW_ARCHDOWN, 4000, 2000, 300, 32768, 65536, &x, &y);
  ok("warp: arch down's middle foot at the bottom", x > 1990 && x < 2010 && y > 1990);
  wa_warp_map(WW_SLANTUP, 4000, 2000, 300, 0, 0, &x, &y);
  {
    i32 x2, y2;
    wa_warp_map(WW_SLANTUP, 4000, 2000, 300, 65536, 0, &x2, &y2);
    ok("warp: slant up rises to the right", y > y2 + 600 && x == 0 && x2 == 4000);
  }
  wa_warp_map(WW_WAVE, 4000, 2000, 300, 16384, 0, &x, &y);
  ok("warp: the wave's crest a quarter across", y < 20);
  ok("warp: names both ways, the others kept by name", wa_warp_from_prst("textCircle") == WW_CIRCLE && !strcmp(wa_warp_prst(WW_WAVE), "textWave1") &&
     wa_warp_from_prst("textCanUp") >= WW_N && !strcmp(wa_warp_prst(wa_warp_from_prst("textCanUp")), "textCanUp") && !wa_warp_from_prst("textBogus"));
}
static void drawing(void)
{
  Doc *d = doc_new();
  Layout *L = lay_new(d, &fs);
  CharFmt f = d->styles[0].cf;
  WaPath p;
  WaRaster r;
  TextFx fx;
  int ok1;
  f.size = 72;
  wa_path_init(&p);
  ok("outlines: the letters of a run", wa_path_run(&p, L, &f, "Wo", 2, 0, 0) && p.n > 20 && p.y0 < -600 && p.y1 >= -WA_SUB && p.x1 > 0);
  memset(&fx, 0, sizeof fx);
  wa_style(2, &fx);      /* white, blue outline, blue glow */
  ok1 = wa_render(&p, &fx, 0, 1, 10, WL_ALL, &r);
  {
    /* the glow shows left of the letters, where they have no ink */
    int glowed = 0, outline = 0;
    i32 y, x;
    for (y = 0; ok1 && y < r.h; y++) for (x = 0; x < r.w; x++) {
      const u8 *q = r.px + ((size_t)y * r.w + x) * 4;
      if (x < -r.ox - 2 && q[3] > 10 && q[2] > q[0]) glowed = 1;
      if (q[3] > 200 && q[2] > 150 && q[0] < 120) outline = 1;
    }
    ok("render: glow round the letters, the blue outline drawn", ok1 && glowed && outline);
  }
  wa_raster_free(&r);
  wa_style(4, &fx);      /* blue gradient, light at the top */
  if (wa_render(&p, &fx, 0, 1, 10, WL_LETTERS, &r)) {
    i32 y, x, top = -1, bot = -1;
    for (y = 0; y < r.h && top < 0; y++) for (x = 0; x < r.w; x++) if (r.px[((size_t)y * r.w + x) * 4 + 3] == 255) { top = r.px[((size_t)y * r.w + x) * 4]; break; }
    for (y = r.h - 1; y >= 0 && bot < 0; y--) for (x = 0; x < r.w; x++) if (r.px[((size_t)y * r.w + x) * 4 + 3] == 255) { bot = r.px[((size_t)y * r.w + x) * 4]; break; }
    ok("render: a gradient from light at the top to dark at the foot", top > bot + 40);
    wa_raster_free(&r);
  } else ok("render: a gradient", 0);
  wa_style(6, &fx);      /* reflection */
  ok("render: a reflection goes below the letters", wa_render(&p, &fx, 0, 1, 10, WL_ALL, &r) && r.oy + r.h > 30);
  wa_raster_free(&r);
  {
    i32 tb[4];
    i32 y0 = p.y0;
    tb[0] = p.x0 / WA_SUB; tb[1] = p.y0 / WA_SUB; tb[2] = p.x1 / WA_SUB; tb[3] = p.y1 / WA_SUB;
    wa_path_warp(&p, WW_ARCHUP, tb, 6000, 3000, 0, 3000, 1500);
    ok("warp: the letters bent onto the frame", !p.oom && p.n > 20 && p.y0 >= -WA_SUB * 4 && p.x0 >= -WA_SUB * 4 && p.x1 <= 6000 * WA_SUB && p.y0 != y0);
  }
  wa_path_free(&p);
  lay_free(L);
  doc_free(d);
}
static void documents(void)
{
  Doc *d = doc_new(), *e;
  u16 rx = rx_add(WORD_FX, strlen(WORD_FX)), g = rx_add(GRAD_FX, strlen(GRAD_FX));
  Pos at;
  CharFmt f = d->styles[0].cf;
  u8 *data, *pdf;
  size_t n, xl, pl;
  unsigned lost = 0;
  char err[100], *xml;
  int k, k2, k3, i;
  Image *im;
  Layout *L;
  TextFx sfx;
  /* runs with Word's effects, one with Quill's style, then WordArt shapes bent, then a signature line */
  at.para = 0; at.off = 0;
  f.size = 56; f.flags |= CF_BOLD; f.rx = rx;
  doc_insert(d, &at, "Glowing ", 8, &f);
  f.rx = g;
  doc_insert(d, &at, "Gradient", 8, &f);
  wa_style(10, &sfx);
  f.rx = wa_set(0, &sfx, WA_ALL);
  doc_insert(d, &at, " Sunset", 7, &f);
  doc_insert(d, &at, "\n", 1, NULL);
  k = add_obj(d, 1, doc_add_shape(d, -1, 5760, 2160));
  im = &d->images[k];
  story_free(im->text); free(im->text);
  wa_style(4, &sfx);
  im->text = story_of("Arched WordArt", wa_set(0, &sfx, WA_ALL), 48);
  im->warp = WW_ARCHUP; im->lstyle = PB_NONE; im->fill = COLOR_AUTO; im->wrap = WRAP_TOPBOTTOM;
  k2 = add_obj(d, 1, doc_add_shape(d, -1, 5760, 1440));
  im = &d->images[k2];
  story_free(im->text); free(im->text);
  wa_style(8, &sfx);
  im->text = story_of("A Wavy Line", wa_set(0, &sfx, WA_ALL), 40);
  im->warp = WW_WAVE; im->lstyle = PB_NONE; im->fill = COLOR_AUTO; im->wrap = WRAP_TOPBOTTOM; im->y = 2400;
  k3 = add_obj(d, 1, doc_add_shape(d, -1, 2880, 2880));
  im = &d->images[k3];
  story_free(im->text); free(im->text);
  im->text = story_of("Round and round we go", 0, 28);
  im->warp = (u8)wa_warp_from_prst("textCanUp"); im->lstyle = PB_NONE; im->fill = COLOR_AUTO;
  {
    struct SigLine sl;
    Image s;
    memset(&sl, 0, sizeof sl);
    strcpy(sl.signer, "Ada Lovelace"); strcpy(sl.title, "Analyst"); strcpy(sl.email, "ada@example.org"); strcpy(sl.instr, "Sign <here> & date");
    sl.comments = 1; sl.showdate = 0;
    img_defaults(&s);
    ok("signature line: drawn as a PNG 192 x 96 points", sig_make(&s, &sl, &fs) && s.type == IMG_PNG && s.data && s.cx == 3840 && s.cy == 1920 && s.sig && s.sig->id[0] == '{');
    doc_insert(d, &at, "\n", 1, NULL);
    add_obj(d, d->np - 1, doc_take_object(d, &s));
  }
  data = docx_save(d, &n);
  out("wordart.docx", data, n);
  xml = data ? part(data, n, "word/document.xml", &xl) : NULL;
  ok("docx: Word's effects written once each, as they came", xml && count(xml, xl, WORD_FX) == 1 && count(xml, xl, GRAD_FX) == 1 && count(xml, xl, "w14:ligatures") == 1);
  ok("docx: the warps written in wps:bodyPr, the one Quill doesn't draw kept", xml && strstr(xml, "<a:prstTxWarp prst=\"textArchUp\"><a:avLst/></a:prstTxWarp><a:noAutofit/></wps:bodyPr>") &&
     strstr(xml, "prst=\"textWave1\"") && strstr(xml, "prst=\"textCanUp\""));
  ok("docx: the signature line as Word's VML", xml && strstr(xml, "<o:signatureline v:ext=\"edit\" id=\"{") && strstr(xml, "o:suggestedsigner=\"Ada Lovelace\"") &&
     strstr(xml, "o:signinginstructions=\"Sign &lt;here&gt; &amp; date\"") && strstr(xml, "allowcomments=\"t\"") && strstr(xml, "showsigndate=\"f\"") &&
     strstr(xml, "<v:imagedata r:id=\"rIdImg") && strstr(xml, "style=\"width:192.00pt;height:96.00pt\""));
  free(xml);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  {
    const CharFmt *a = e ? para_fmt_at(e->p[0], 1) : NULL, *b = e ? para_fmt_at(e->p[0], 10) : NULL;
    ok("docx: the runs' effects read back exactly", a && b && a->rx == rx && b->rx == g && !lost);
  }
  {
    Image *a = e ? nth(e, 0) : NULL, *b = e ? nth(e, 1) : NULL, *c = e ? nth(e, 2) : NULL, *s = e ? nth(e, 3) : NULL;
    ok("docx: the warps read back", a && b && c && a->warp == WW_ARCHUP && b->warp == WW_WAVE && c->warp == wa_warp_from_prst("textCanUp") && a->text &&
       wa_get(para_fmt_at(a->text->p[0], 0)->rx) && wa_get(para_fmt_at(a->text->p[0], 0)->rx)->fill == WF_GRAD);
    ok("docx: the signature line read back with its setup", s && s->sig && !strcmp(s->sig->signer, "Ada Lovelace") && !strcmp(s->sig->title, "Analyst") &&
       !strcmp(s->sig->email, "ada@example.org") && !strcmp(s->sig->instr, "Sign <here> & date") && s->sig->comments && !s->sig->showdate &&
       !strcmp(s->sig->id, nth(d, 3)->sig->id) && s->data && s->type == IMG_PNG && s->cx == 3840);
    if (s) {
      Image c2;
      ok("signature line: copied with its object", img_clone(&c2, s, 0) && c2.sig && c2.sig != s->sig && !strcmp(c2.sig->signer, "Ada Lovelace"));
      img_release(&c2);
    }
  }
  {
    /* a second save: the same document again */
    size_t n2;
    u8 *d2 = e ? docx_save(e, &n2) : NULL;
    char *x2 = d2 ? part(d2, n2, "word/document.xml", &pl) : NULL;
    ok("docx: a second save writes the effects, warps and signature line again", x2 && count(x2, pl, WORD_FX) == 1 && strstr(x2, "textArchUp") && strstr(x2, "o:signatureline"));
    free(x2); free(d2);
  }
  L = lay_new(d, &fs);
  lay_update(L);
  pdf = pdf_export(L, &n);
  out("wordart.pdf", pdf, n);
  ok("pdf: WordArt's soft effects as images with soft masks", pdf && memmem(pdf, n, "/SMask", 6));
  free(pdf);
  {
    u32 w, h;
    u8 *rgb = pdf_page_rgb(L, 0, 50, &w, &h);
    int blue = 0;
    for (i = 0; rgb && i < (int)(w * h); i++) if (rgb[3 * i + 2] > 120 && rgb[3 * i] < 90) blue++;
    ok("page picture: the WordArt drawn", rgb && blue > 20);
    if (rgb && getenv("WP19_OUT")) { Buf b; buf_init(&b); png_encode(&b, rgb, w, h, 50); out("wordart-page.png", (u8 *)b.data, b.len); buf_free(&b); }
    free(rgb);
  }
  lay_free(L);
  free(data); doc_free(e); doc_free(d);
}
void test_wp19(void)
{
  parsing();
  warps();
  drawing();
  documents();
}
