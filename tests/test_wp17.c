#define _GNU_SOURCE
/* test_wp17.c - the native tests of WP17 Pictures (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp17() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/image.h"
#include "../core/objfx.h"
#include "../core/layout.h"
#include "../core/pdf.h"
#include "../core/zip.h"

/* a new document holding the picture file name as its one paragraph's picture; its index */
static Doc *with_picture(const char *name, int *k)
{
  size_t n;
  u8 *data = file_read(name, &n);
  Doc *d = doc_new();
  Pos at = P(0, 0);
  char ref[8];
  *k = -1;
  if (!d || !data) { free(data); return d; }
  *k = doc_add_image(d, data, (u32)n, 1440, 1080);
  if (*k >= 0) { pic_utf8(ref, *k); doc_insert(d, &at, ref, 4, NULL); }
  return d;
}
/* the picture whose reference is first in d's text, or NULL */
static Image *first_pic(Doc *d)
{
  u32 i, j;
  for (i = 0; i < d->np; i++)
    for (j = 0; j + 4 <= d->p[i]->len; j++)
      if (IS_PIC(d->p[i]->t + j)) return &d->images[pic_index(d->p[i]->t + j)];
  return NULL;
}
/* whether a zip holds an entry whose name ends with suffix */
static int has_entry(const u8 *data, size_t n, const char *needle)
{
  return memmem(data, n, needle, strlen(needle)) != NULL;
}

static void formats(void)
{
  static const char *const files[3] = { "tests/data/wp17.gif", "tests/data/wp17.bmp", "tests/data/wp17.iff" };
  static const int types[3] = { IMG_GIF, IMG_BMP, IMG_ILBM };
  int f;
  for (f = 0; f < 3; f++) {
    size_t n;
    u8 *data = file_read(files[f], &n), *rgb = NULL;
    u32 w = 0, h = 0;
    int t = data ? img_info(data, (u32)n, &w, &h) : IMG_NONE;
    char what[100];
    if (t != IMG_NONE) rgb = img_decode(data, (u32)n, 8, 6);
    snprintf(what, sizeof what, "pictures: %s read (its kind, 8 x 6, red left and blue right of white)", files[f] + 11);
    ok(what, t == types[f] && w == 8 && h == 6 && rgb && rgb[(2 * 8 + 2) * 3] > 150 && rgb[(2 * 8 + 2) * 3 + 2] < 100 &&
       rgb[(2 * 8 + 5) * 3 + 2] > 150 && rgb[(2 * 8 + 5) * 3] < 100 && (f == 0 || (rgb[0] > 240 && rgb[1] > 240)));
    free(rgb); free(data);
  }
  {
    /* an ILBM is saved as a PNG, a GIF as itself; both come back */
    int k, k2;
    Doc *d = with_picture("tests/data/wp17.iff", &k), *e;
    size_t n;
    u8 *data;
    unsigned lost = 0;
    char err[100];
    Pos at = P(0, 4);
    char ref[8];
    size_t gl;
    u8 *gif = file_read("tests/data/wp17.gif", &gl);
    k2 = doc_add_image(d, gif, (u32)gl, 600, 450);
    pic_utf8(ref, k2); doc_insert(d, &at, ref, 4, NULL);
    data = docx_save(d, &n);
    e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
    ok("pictures: an ILBM saved as a PNG, a GIF as a GIF ", data && has_entry(data, n, "word/media/image1.png") && has_entry(data, n, "word/media/image2.gif") && e && e->nimages == 2 && e->images[0].type == IMG_PNG && e->images[1].type == IMG_GIF && !(lost & LOST_PICTURES));
    free(data); doc_free(e); doc_free(d);
  }
}
static void adjust(void)
{
  u8 px[6 * 3] = { 200, 100, 50, 10, 20, 30, 255, 255, 255, 0, 0, 0, 128, 64, 32, 90, 180, 60 }, t[18];
  ObjFx f;
  int i, okg = 1;
  memset(&f, 0, sizeof f);
  f.cmode = PCM_GREY; memcpy(t, px, 18); objfx_adjust(t, 6, 1, &f);
  for (i = 0; i < 6; i++) if (t[i * 3] != t[i * 3 + 1] || t[i * 3] != t[i * 3 + 2]) okg = 0;
  ok("adjust: greyscale", okg && t[6] == 255 && t[9] == 0);
  memset(&f, 0, sizeof f); f.bright = 40; memcpy(t, px, 18); objfx_adjust(t, 6, 1, &f);
  ok("adjust: brightness +40%", t[3] == 10 + 102 && t[9] == 102 && t[6] == 255);
  memset(&f, 0, sizeof f); f.contrast = -100; memcpy(t, px, 18); objfx_adjust(t, 6, 1, &f);
  ok("adjust: no contrast at all is mid grey", t[0] == 128 && t[6] == 128 && t[9] == 128);
  memset(&f, 0, sizeof f); f.cmode = PCM_WASHOUT; memcpy(t, px, 18); objfx_adjust(t, 6, 1, &f);
  ok("adjust: washout is light", t[9] >= 200 && t[3] >= 200);
  memset(&f, 0, sizeof f); f.cmode = PCM_SEPIA; memcpy(t, px, 18); objfx_adjust(t, 6, 1, &f);
  ok("adjust: sepia browns (red over blue)", t[12] > t[14] && t[0] > t[2]);
  memset(&f, 0, sizeof f); f.cmode = PCM_DARK; f.recolor = 0x0000FF; memcpy(t, px, 18); objfx_adjust(t, 6, 1, &f);
  ok("adjust: recoloured dark blue", t[6] == 0 && t[7] == 0 && t[8] == 255 && t[9] == 0 && t[11] == 0);
  memset(&f, 0, sizeof f); f.sat = 1; memcpy(t, px, 18); objfx_adjust(t, 6, 1, &f);
  ok("adjust: saturation 0 is grey", abs(t[0] - t[2]) <= 2);
  memset(&f, 0, sizeof f); f.temp = 4700; memcpy(t, px, 18); objfx_adjust(t, 6, 1, &f);
  ok("adjust: a warmer tone (more red, less blue)", t[12] > 128 && t[14] < 32);
  memset(&f, 0, sizeof f); f.cmode = PCM_BW; memcpy(t, px, 18); objfx_adjust(t, 6, 1, &f);
  ok("adjust: black and white", t[15] == 255 && t[3] == 0 && t[6] == 255 && t[0] == 0);
  {
    /* every artistic effect runs on a real picture and changes it */
    u32 w, h, a;
    size_t n;
    u8 *jpg = file_read("tests/data/base.jpg", &n), *rgb;
    int all = 1;
    img_info(jpg, (u32)n, &w, &h);
    for (a = 1; a < ART_N; a++) {
      rgb = img_decode(jpg, (u32)n, 64, 48);
      memset(&f, 0, sizeof f); f.art = (u8)a;
      memcpy(t, rgb + 3 * (20 * 64 + 20), 3);
      objfx_adjust(rgb, 64, 48, &f);
      if (!rgb) all = 0;
      free(rgb);
    }
    memset(&f, 0, sizeof f); f.sharp = 50;
    rgb = img_decode(jpg, (u32)n, 64, 48); objfx_adjust(rgb, 64, 48, &f); free(rgb);
    memset(&f, 0, sizeof f); f.sharp = -50;
    rgb = img_decode(jpg, (u32)n, 64, 48); objfx_adjust(rgb, 64, 48, &f); free(rgb);
    ok("adjust: the artistic effects, sharpen and soften run", all);
    {
      /* the JPEG and PNG writers: what they write reads back as it was */
      Rgba ra;
      size_t jl = 0, pl = 0;
      u8 *j, *p2, *back, *back2;
      long diff = 0;
      int i2;
      rgb = img_decode(jpg, (u32)n, 64, 48);
      rgba_alloc(&ra, 64, 48);
      for (i2 = 0; i2 < 64 * 48; i2++) { memcpy(ra.px + i2 * 4, rgb + i2 * 3, 3); ra.px[i2 * 4 + 3] = 255; }
      j = jpeg_encode(&ra, 90, 0, &jl); p2 = png_encode(&ra, 96, &pl);
      back = j ? img_decode(j, (u32)jl, 64, 48) : NULL; back2 = p2 ? img_decode(p2, (u32)pl, 64, 48) : NULL;
      for (i2 = 0; back && i2 < 64 * 48 * 3; i2++) diff += abs(back[i2] - rgb[i2]);
      ok("jpeg writer: read back close to the original", back && diff / (64 * 48 * 3) < 6);
      ok("png writer: read back exactly", back2 && !memcmp(back2, rgb, 64 * 48 * 3));
      free(j); free(p2); free(back); free(back2); free(rgb); rgba_free(&ra);
    }
    free(jpg);
  }
}
static void render(void)
{
  int k;
  Doc *d = with_picture("tests/data/wp17.bmp", &k);
  Image im = d->images[k];
  ObjRaster r;
  int okr;
  im.fx.alpha = 50;
  okr = objfx_render(&im, 1, 15, &r);       /* (15 twips a pixel: 96 x 72) */
  ok("render: transparency 50% (half the alpha)", okr && r.w == 96 - 2 * r.ox && r.h == 72 - 2 * r.oy && r.px[((r.h / 2) * r.w + r.w / 2) * 4 + 3] >= 120 && r.px[((r.h / 2) * r.w + r.w / 2) * 4 + 3] <= 135);
  objraster_free(&r);
  im.fx.alpha = 0; im.fx.bgkey = 1; im.fx.bgtol = 20; im.fx.bgcolor = objfx_corner_colour(&im);
  okr = objfx_render(&im, 1, 15, &r);
  ok("remove background: the corner colour (white) found and made transparent, the rest kept", im.fx.bgcolor == 0xFFFFFF && okr &&
     r.px[(1 * r.w + 1) * 4 + 3] == 0 && r.px[((r.h / 2) * r.w + r.w / 4) * 4 + 3] == 255);
  objraster_free(&r);
  im.fx.bgkey = 0; im.shape = SH_ELLIPSE;
  okr = objfx_render(&im, 1, 15, &r);
  {
    /* cropping by dragging: the left side in by a quarter of the box, then back out */
    Image c = d->images[k];
    objfx_crop_by(&c, 360, 0, 0, 0);
    ok("crop by dragging: a quarter cut off the left, the picture narrower", c.crop[0] == 25000 && c.cx == 1080 && c.cy == 1080);
    objfx_crop_by(&c, -360, 0, 0, 0);
    ok("crop by dragging: and dragged back out", c.crop[0] == 0 && c.cx == 1440);
    c.flip = 1; objfx_crop_by(&c, 0, 0, 144, 0);
    ok("crop by dragging: a flipped picture's right side is its left", c.crop[0] == 10000 && c.crop[2] == 0);
  }
  ok("picture style: an oval picture (corners clear)", okr && r.px[3] == 0 && r.px[((r.h / 2) * r.w + r.w / 2) * 4 + 3] == 255);
  objraster_free(&r);
  im.shape = SH_RECT; im.fx.shadow = 1; im.fx.shdist = 60; im.fx.shdir = 450; im.fx.shblur = 60; im.fx.shcolor = 0; im.fx.shalpha = 60;
  okr = objfx_render(&im, 1, 15, &r);
  ok("picture style: a drop shadow reaches past the box down and right", okr && r.ox < 0 && r.w > 96 &&
     r.px[3] == 0 && r.px[((72 - r.oy + 2) * r.w + 96 - r.ox + 2) * 4 + 3] > 0);
  objraster_free(&r);
  doc_free(d);
}
static void docx(void)
{
  int k;
  Doc *d = with_picture("tests/data/base.jpg", &k), *e;
  Image *im = &d->images[k], *b;
  size_t n;
  u8 *data;
  unsigned lost = 0;
  char err[100];
  im->fx.bright = 20; im->fx.contrast = -40; im->fx.cmode = PCM_GREY; im->fx.alpha = 30;
  im->shape = SH_ROUNDRECT; im->fx.soft = 90; im->lstyle = PB_SINGLE; im->lwidth = 12; im->lcolor = 0xFFFFFF;
  data = docx_save(d, &n);
  ok("docx: a:lum, a:grayscl, a:alphaModFix, roundRect and softEdge written", data && memmem(data, n, "word/document.xml", 17));
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  b = e ? first_pic(e) : NULL;
  ok("docx: corrections, greyscale, transparency, a picture style read back", b && b->fx.bright == 20 && b->fx.contrast == -40 && b->fx.cmode == PCM_GREY &&
     b->fx.alpha == 30 && b->shape == SH_ROUNDRECT && b->fx.soft == 90 && b->lwidth == 12);
  free(data); doc_free(e);
  memset(&im->fx, 0, sizeof im->fx); im->shape = SH_RECT;
  im->fx.cmode = PCM_SEPIA; im->fx.bgkey = 1; im->fx.bgcolor = 0x123456;
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  b = e ? first_pic(e) : NULL;
  ok("docx: sepia (a:duotone) and a transparent colour (a:clrChange) read back", b && b->fx.cmode == PCM_SEPIA && b->fx.bgkey && b->fx.bgcolor == 0x123456 && !b->fx.bgtol);
  free(data); doc_free(e);
  memset(&im->fx, 0, sizeof im->fx);
  im->fx.cmode = PCM_LIGHT; im->fx.recolor = 0x4472C4; im->fx.art = ART_PENCIL; im->fx.sharp = 25; im->fx.sat = 200; im->fx.temp = 4700;
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  b = e ? first_pic(e) : NULL;
  ok("docx: Word 2010's effects drawn into the picture saved, the original kept as a14:imgLayer", data && memmem(data, n, "word/media/image2.jpeg", 22));
  ok("docx: and read back as the original with its effects (to change again)", b && b->len == im->len && !memcmp(b->data, im->data, im->len) && b->fx.art == ART_PENCIL &&
     b->fx.sharp == 25 && b->fx.sat == 200 && b->fx.temp == 4700 && b->fx.cmode == PCM_LIGHT && b->fx.recolor == 0x4472C4 && !b->fx.raw);
  free(data); doc_free(e);
  {
    /* Word's own: an a14 extension whose layer is an HD Photo Quill can't read is kept as it was */
    Doc *w;
    Image *wi;
    u8 *d2;
    size_t n2;
    memset(&im->fx, 0, sizeof im->fx);
    im->fx.raw = str_dup("<a14:imgProps xmlns:a14=\"http://schemas.microsoft.com/office/drawing/2010/main\"><a14:imgLayer r:embed=\"rIdK9\"><a14:imgEffect><a14:artisticBlur/></a14:imgEffect></a14:imgLayer></a14:imgProps>");
    data = docx_save(d, &n);
    w = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
    wi = w ? first_pic(w) : NULL;
    ok("docx: an a14 extension Quill can't redo (no picture layer it can read) is kept as it was", wi && !wi->fx.art);
    d2 = w ? docx_save(w, &n2) : NULL;
    free(d2);
    free(data); doc_free(w);
    free(im->fx.raw); im->fx.raw = NULL;
  }
  {
    /* compress: at 96 ppi it is smaller, and the cropped areas go */
    u32 cl;
    u8 *c;
    im->crop[0] = 25000; im->crop[2] = 25000;
    c = objfx_compress(im, 96, 1, &cl);
    ok("compress pictures: smaller, cropped areas deleted", c && cl < im->len && cl > 100);
    free(c);
    im->crop[0] = im->crop[2] = 0;
  }
  {
    Layout *L = lay_new(d, &fs);
    u8 *pdf;
    memset(&im->fx, 0, sizeof im->fx); im->fx.alpha = 40;
    pdf = pdf_export(L, &n);
    ok("pdf: a transparent picture as an image with a soft mask", pdf && memmem(pdf, n, "/SMask", 6) && memmem(pdf, n, "/R0", 3));
    free(pdf); lay_free(L);
  }
  doc_free(d);
}
void test_wp17(void)
{
  formats();
  adjust();
  render();
  docx();
}
