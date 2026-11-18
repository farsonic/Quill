/* sigline.c - WP19's signature line (see wordart.h): Word's Insert > Signature Line. The line is an
   in-line picture of what Word draws for an unsigned line (a cross, the line to sign on, the
   suggested signer's name and title under it), which Quill makes itself as PNG; its setup (signer,
   title, e-mail, instructions, comments allowed, date shown) is kept with it and written to .docx as
   the VML shape with o:signatureline that Word writes. Written for Quill from the VML schema
   ([MS-OI29500], [MS-ODRAWXML] CT_SignatureLine). This file belongs to WP19. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "wordart.h"
#include "image.h"
#include "pdf.h"
#include "xml.h"

FontSet *sig_fonts;

struct SigLine *sig_copy(const struct SigLine *s)
{
  struct SigLine *c;
  if (!s || !(c = (struct SigLine *)malloc(sizeof *c))) return NULL;
  *c = *s;
  return c;
}
void sig_free(struct SigLine *s) { free(s); }

/* an id for a new line, GUID-shaped ("{8-4-4-4-12}"), different each time and for each setup */
static void new_id(struct SigLine *s)
{
  static unsigned long serial = 0x5A17;
  unsigned long h = 2166136261UL;
  const char *p;
  for (p = s->signer; *p; p++) h = (h ^ (u8)*p) * 16777619UL;
  for (p = s->title; *p; p++) h = (h ^ (u8)*p) * 16777619UL;
  serial = serial * 1103515245UL + 12345UL + h;
  sprintf(s->id, "{%08lX-%04lX-4%03lX-A%03lX-%04lX%08lX}", (h ^ serial) & 0xFFFFFFFFUL, (serial >> 7) & 0xFFFF, (h >> 5) & 0xFFF, (serial >> 3) & 0xFFF,
          (h >> 16) & 0xFFFF, (serial * 2654435761UL) & 0xFFFFFFFFUL);
}

/* ------------------------------------------------------------------ the picture */
#define SIG_W 3840                  /* 192 x 96 points, as Word makes them */
#define SIG_H 1920
#define SIG_DPI 150
static void put_text(u8 *rgb, u32 w, u32 h, FaceSize *z, const char *s, i32 x, i32 base, u32 colour)
{
  WaPath p;
  WaRaster r;
  TextFx fx;
  u32 y, k;
  if (!z || !s[0]) return;
  wa_path_init(&p);
  memset(&fx, 0, sizeof fx);
  if (wa_path_text(&p, z, s, (u32)strlen(s), x, base) && wa_render(&p, &fx, colour, SIG_DPI, 1440, WL_LETTERS, &r)) {
    for (y = 0; y < (u32)r.h; y++)
      for (k = 0; k < (u32)r.w; k++) {
        long X = r.ox + (long)k, Y = r.oy + (long)y;
        const u8 *q = r.px + ((size_t)y * r.w + k) * 4;
        u8 *d;
        int c;
        if (X < 0 || Y < 0 || X >= (long)w || Y >= (long)h || !q[3]) continue;
        d = rgb + ((size_t)Y * w + X) * 3;
        for (c = 0; c < 3; c++) d[c] = (u8)((q[c] * q[3] + d[c] * (255 - q[3])) / 255);
      }
    wa_raster_free(&r);
  }
  wa_path_free(&p);
}
int sig_make(Image *im, const struct SigLine *s, FontSet *fs)
{
  u32 w = (u32)((long)SIG_W * SIG_DPI / 1440), h = (u32)((long)SIG_H * SIG_DPI / 1440), x, y;
  u8 *rgb;
  Buf png;
  struct SigLine *c;
  FaceSize *big, *small;
  if (!s || !fs || !(rgb = (u8 *)malloc((size_t)w * h * 3))) return 0;
  memset(rgb, 255, (size_t)w * h * 3);
  big = fonts_size(fs, "Calibri", 0, 0, 56);
  small = fonts_size(fs, "Calibri", 0, 0, 22);
  /* the cross above the line's left end, the line, the signer's name and title under it */
  put_text(rgb, w, h, big, "X", 200, 1120, 0x000000);
  for (y = 1170 * SIG_DPI / 1440; y < 1170 * SIG_DPI / 1440 + 2; y++)
    for (x = 180 * SIG_DPI / 1440; x < (u32)(3660L * SIG_DPI / 1440); x++) { u8 *d = rgb + ((size_t)y * w + x) * 3; d[0] = d[1] = d[2] = 0x40; }
  put_text(rgb, w, h, small, s->signer, 200, 1480, 0x000000);
  put_text(rgb, w, h, small, s->title, 200, 1760, 0x000000);
  buf_init(&png);
  if (!png_encode(&png, rgb, w, h, SIG_DPI) || png.oom || !(c = sig_copy(s))) { free(rgb); buf_free(&png); return 0; }
  free(rgb);
  if (!c->id[0]) new_id(c);
  if (!im->shared) free(im->data);
  sig_free(im->sig);
  if (im->kind != OBJ_PICTURE || !im->cx) {
    img_defaults(im);
    im->cx = SIG_W; im->cy = SIG_H;
  }
  im->data = (u8 *)png.data; im->len = (u32)png.len; im->shared = 0; im->type = IMG_PNG; im->pw = w; im->ph = h;
  im->sig = c;
  if (!im->alt[0]) strcpy(im->alt, "Signature Line");
  return 1;
}

/* ------------------------------------------------------------------ .docx */
/* a VML length ("192pt", "2in", "96.5pt") in twips; 0 if none */
static i32 vml_len(const char *style, const char *key)
{
  const char *p = style ? strstr(style, key) : NULL;
  long v = 0, frac = 0, div = 1;
  if (!p) return 0;
  p += strlen(key);
  while (*p == ' ') p++;
  while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
  if (*p == '.') { p++; while (*p >= '0' && *p <= '9' && div < 1000) { frac = frac * 10 + (*p++ - '0'); div *= 10; } }
  if (!strncmp(p, "in", 2)) return (i32)(v * 1440 + frac * 1440 / div);
  if (!strncmp(p, "cm", 2)) return (i32)(v * 567 + frac * 567 / div);
  if (!strncmp(p, "mm", 2)) return (i32)((v * 567 + frac * 567 / div) / 10);
  if (!strncmp(p, "px", 2)) return (i32)(v * 15 + frac * 15 / div);
  return (i32)(v * 20 + frac * 20 / div);      /* points */
}
static void copy_attr(Xml *x, const char *name, char *out, size_t cap)
{
  const char *v = xml_attr(x, name);
  size_t n;
  if (!v) return;
  n = strlen(v);
  if (n >= cap) n = cap - 1;
  memcpy(out, v, n); out[n] = 0;
}
static int truth(const char *v, int dflt) { return v ? (*v == 't' || *v == 'T' || *v == '1' || *v == 'o') && strcmp(v, "off") : dflt; }
int sig_parse_vml(const char *raw, size_t len, struct SigLine *s, char *relid, size_t rcap, i32 *cx, i32 *cy)
{
  Xml x;
  int is = 0;
  memset(s, 0, sizeof *s);
  s->showdate = 1;
  if (rcap) relid[0] = 0;
  *cx = *cy = 0;
  xml_init(&x, raw, len);
  while (xml_next(&x) != XT_EOF) {
    if (x.type != XT_START) continue;
    if (!strcmp(x.name, "shape")) {
      const char *st = xml_attr(&x, "style");
      i32 w = vml_len(st, "width:"), h = vml_len(st, "height:");
      if (w > 0 && h > 0) { *cx = w; *cy = h; }
    } else if (!strcmp(x.name, "imagedata")) copy_attr(&x, "id", relid, rcap);
    else if (!strcmp(x.name, "signatureline")) {
      is = truth(xml_attr(&x, "issignatureline"), 1);
      copy_attr(&x, "suggestedsigner", s->signer, sizeof s->signer);
      copy_attr(&x, "suggestedsigner2", s->title, sizeof s->title);
      copy_attr(&x, "suggestedsigneremail", s->email, sizeof s->email);
      copy_attr(&x, "signinginstructions", s->instr, sizeof s->instr);
      copy_attr(&x, "id", s->id, sizeof s->id);
      s->comments = (u8)truth(xml_attr(&x, "allowcomments"), 0);
      s->showdate = (u8)truth(xml_attr(&x, "showsigndate"), 1);
    }
  }
  xml_free(&x);
  return is;
}
static void attr(Buf *o, const char *name, const char *v)
{
  if (!v[0]) return;
  buf_printf(o, " %s=\"", name); xml_escape(o, v, strlen(v)); buf_adds(o, "\"");
}
void sig_write_vml(Buf *o, const Image *im, const char *rpr, size_t rl, int media, u32 id)
{
  const struct SigLine *s = im->sig;
  buf_adds(o, "<w:r>"); buf_add(o, rpr, rl);
  buf_adds(o, "<w:pict xmlns:v=\"urn:schemas-microsoft-com:vml\" xmlns:o=\"urn:schemas-microsoft-com:office:office\">"
              "<v:shapetype id=\"_x0000_t75\" coordsize=\"21600,21600\" o:spt=\"75\" o:preferrelative=\"t\" path=\"m@4@5l@4@11@9@11@9@5xe\" filled=\"f\" stroked=\"f\">"
              "<v:stroke joinstyle=\"miter\"/><v:formulas><v:f eqn=\"if lineDrawn pixelLineWidth 0\"/><v:f eqn=\"sum @0 1 0\"/><v:f eqn=\"sum 0 0 @1\"/>"
              "<v:f eqn=\"prod @2 1 2\"/><v:f eqn=\"prod @3 21600 pixelWidth\"/><v:f eqn=\"prod @3 21600 pixelHeight\"/><v:f eqn=\"sum @0 0 1\"/>"
              "<v:f eqn=\"prod @6 1 2\"/><v:f eqn=\"prod @7 21600 pixelWidth\"/><v:f eqn=\"sum @8 21600 0\"/><v:f eqn=\"prod @7 21600 pixelHeight\"/>"
              "<v:f eqn=\"sum @10 21600 0\"/></v:formulas><v:path o:extrusionok=\"f\" gradientshapeok=\"t\" o:connecttype=\"rect\"/>"
              "<o:lock v:ext=\"edit\" aspectratio=\"t\"/></v:shapetype>");
  buf_printf(o, "<v:shape id=\"_x0000_i%lu\" type=\"#_x0000_t75\"", (unsigned long)(1024 + id));
  attr(o, "alt", im->alt[0] ? im->alt : "Signature Line");
  buf_printf(o, " style=\"width:%ld.%02ldpt;height:%ld.%02ldpt\">", (long)im->cx / 20, (long)(im->cx % 20) * 5, (long)im->cy / 20, (long)(im->cy % 20) * 5);
  if (media > 0) buf_printf(o, "<v:imagedata r:id=\"rIdImg%d\" o:title=\"\"/>", media);
  buf_adds(o, "<o:lock v:ext=\"edit\" ungrouping=\"t\" rotation=\"t\" cropping=\"t\" verticies=\"t\" text=\"t\" grouping=\"t\"/>");
  buf_adds(o, "<o:signatureline v:ext=\"edit\"");
  attr(o, "id", s->id);
  buf_adds(o, " provid=\"{00000000-0000-0000-0000-000000000000}\"");
  attr(o, "o:suggestedsigner", s->signer);
  attr(o, "o:suggestedsigner2", s->title);
  attr(o, "o:suggestedsigneremail", s->email);
  if (s->instr[0]) { attr(o, "o:signinginstructions", s->instr); buf_adds(o, " signinginstructionsset=\"t\""); }
  if (s->comments) buf_adds(o, " allowcomments=\"t\"");
  if (!s->showdate) buf_adds(o, " showsigndate=\"f\"");
  buf_adds(o, " issignatureline=\"t\"/></v:shape></w:pict></w:r>");
}
