/* doctheme.c - see doctheme.h (WP13). Office's colour schemes, font schemes and themes are given by
   their names and values as Word lists them; the effects are Quill's own short fmtSchemes in the
   spirit of Word's (shadows, glow, reflection, bevels), as Quill doesn't draw theme effects itself. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doctheme.h"
#include "xml.h"
#include "zip.h"

/* ---- Office's presets */
static const struct { const char *name; u32 c[12]; } colorsets[] = {
  { "Office", { 0x000000, 0xFFFFFF, 0x0E2841, 0xE8E8E8, 0x156082, 0xE97132, 0x196B24, 0x0F9ED5, 0xA02B93, 0x4EA72E, 0x467886, 0x96607D } },
  { "Office 2013 - 2022", { 0x000000, 0xFFFFFF, 0x44546A, 0xE7E6E6, 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47, 0x0563C1, 0x954F72 } },
  { "Office 2007 - 2010", { 0x000000, 0xFFFFFF, 0x1F497D, 0xEEECE1, 0x4F81BD, 0xC0504D, 0x9BBB59, 0x8064A2, 0x4BACC6, 0xF79646, 0x0000FF, 0x800080 } },
  { "Grayscale", { 0x000000, 0xFFFFFF, 0x000000, 0xF8F8F8, 0xDDDDDD, 0xB2B2B2, 0x969696, 0x808080, 0x5F5F5F, 0x4D4D4D, 0x5F5F5F, 0x919191 } },
  { "Blue Warm", { 0x000000, 0xFFFFFF, 0x242852, 0xACCBF9, 0x4A66AC, 0x629DD1, 0x297FD5, 0x7F8FA9, 0x5AA2AE, 0x9D90A0, 0x9454C3, 0x3EBBF0 } },
  { "Blue", { 0x000000, 0xFFFFFF, 0x17406D, 0xDBEFF9, 0x0F6FC6, 0x009DD9, 0x0BD0D9, 0x10CF9B, 0x7CCA62, 0xA5C249, 0xF49100, 0x85DFD0 } },
  { "Blue II", { 0x000000, 0xFFFFFF, 0x335B74, 0xDFE3E5, 0x1CADE4, 0x2683C6, 0x27CED7, 0x42BA97, 0x3E8853, 0x62A39F, 0x6B9F25, 0xB26B02 } },
  { "Blue Green", { 0x000000, 0xFFFFFF, 0x373545, 0xCEDBE6, 0x3494BA, 0x58B6C0, 0x75BDA7, 0x7A8C8E, 0x84ACB6, 0x2683C6, 0x6B9F25, 0x9F6715 } },
  { "Green", { 0x000000, 0xFFFFFF, 0x455F51, 0xE3DED1, 0x549E39, 0x8AB833, 0xC0CF3A, 0x029676, 0x4AB5C4, 0x0989B1, 0x6B9F25, 0xBA6906 } },
  { "Yellow", { 0x000000, 0xFFFFFF, 0x39302A, 0xE5DEDB, 0xFFCA08, 0xF8931D, 0xCE8D3E, 0xEC7016, 0xE64823, 0x9C6A6A, 0x2998E3, 0x7F723D } },
  { "Orange", { 0x000000, 0xFFFFFF, 0x637052, 0xCCDDEA, 0xE48312, 0xBD582C, 0x865640, 0x9B8357, 0xC2BC80, 0x94A088, 0x2998E3, 0x8C8C8C } },
  { "Red", { 0x000000, 0xFFFFFF, 0x323232, 0xE5C243, 0xA5300F, 0xD55816, 0xE19825, 0xB19C7D, 0x7F5F52, 0xB27D49, 0x6B9F25, 0xB26B02 } },
  { "Violet", { 0x000000, 0xFFFFFF, 0x373545, 0xDCD8DC, 0xAD84C6, 0x8784C7, 0x5D739A, 0x6997AF, 0x84ACB6, 0x6F8183, 0x69A020, 0x8C8C8C } },
  { "Median", { 0x000000, 0xFFFFFF, 0x775F55, 0xEBDDC3, 0x94B6D2, 0xDD8047, 0xA5AB81, 0xD8B25C, 0x7BA79D, 0x968C8C, 0xF7B615, 0x704404 } },
  { "Paper", { 0x000000, 0xFFFFFF, 0x444D26, 0xFEFAC9, 0xA5B592, 0xF3A447, 0xE7BC29, 0xD092A7, 0x9C85C0, 0x809EC2, 0x8E58B6, 0x7F6F6F } },
  { "Marquee", { 0x000000, 0xFFFFFF, 0x5E5E5E, 0xDDDDDD, 0x418AB3, 0xA6B727, 0xF69200, 0x838383, 0xFEC306, 0xDF5327, 0xF59E00, 0xB2B2B2 } },
  { "Slipstream", { 0x000000, 0xFFFFFF, 0x212745, 0xB4DCFA, 0x4E67C8, 0x5ECCF3, 0xA7EA52, 0x5DCEAF, 0xFF8021, 0xF14124, 0x56C7AA, 0x59A8D1 } },
  { "Aspect", { 0x000000, 0xFFFFFF, 0x323232, 0xE3DED1, 0xF07F09, 0x9F2936, 0x1B587C, 0x4E8542, 0x604878, 0xC19859, 0x6B9F25, 0xB26B02 } },
  { "Facet", { 0x000000, 0xFFFFFF, 0x2C3C43, 0xEBEBEB, 0x90C226, 0x54A021, 0xE6B91E, 0xE76618, 0xC42F1A, 0x918655, 0x99CA3C, 0xB9D181 } },
  { "Ion", { 0x000000, 0xFFFFFF, 0x1E5155, 0xEBEBEB, 0xB01513, 0xEA6312, 0xE6B729, 0x6AAC90, 0x54849A, 0x9E5E9B, 0x58C1BA, 0x9DFFCB } },
  { "Wisp", { 0x000000, 0xFFFFFF, 0x4E3B30, 0xFBEEC9, 0xA53010, 0xDE7E18, 0x9F8351, 0x728653, 0x92AA4C, 0x6AAC91, 0xFB4A18, 0xFB9318 } },
};
#define NCOLORSETS ((int)(sizeof colorsets / sizeof colorsets[0]))
static const struct { const char *name, *major, *minor; } fontsets[] = {
  { "Office", "Aptos Display", "Aptos" }, { "Office 2013 - 2022", "Calibri Light", "Calibri" }, { "Office 2007 - 2010", "Cambria", "Calibri" },
  { "Calibri", "Calibri", "Calibri" }, { "Arial", "Arial", "Arial" }, { "Corbel", "Corbel", "Corbel" }, { "Candara", "Candara", "Candara" },
  { "Franklin Gothic", "Franklin Gothic Medium", "Franklin Gothic Book" }, { "Century Gothic", "Century Gothic", "Century Gothic" },
  { "Tw Cen MT", "Tw Cen MT", "Tw Cen MT" }, { "Cambria", "Cambria", "Cambria" }, { "Garamond-TrebuchetMS", "Garamond", "Trebuchet MS" },
  { "Times New Roman", "Times New Roman", "Times New Roman" }, { "Times New Roman-Arial", "Times New Roman", "Arial" },
  { "Arial-Times New Roman", "Arial", "Times New Roman" }, { "Trebuchet MS", "Trebuchet MS", "Trebuchet MS" },
  { "Trebuchet MS-Georgia", "Trebuchet MS", "Georgia" }, { "Georgia", "Georgia", "Georgia" }, { "Verdana", "Verdana", "Verdana" },
  { "Gill Sans MT", "Gill Sans MT", "Gill Sans MT" },
};
#define NFONTSETS ((int)(sizeof fontsets / sizeof fontsets[0]))
/* effects: each of the three line widths (EMU) and effect styles (EK_), and whether the fills are gradients */
enum { EK_NONE, EK_SHADOW, EK_SOFTSHADOW, EK_BIGSHADOW, EK_TOPSHADOW, EK_GLOW, EK_REFLECT, EK_SOFTEDGE, EK_BEVEL, EK_INSET };
static const struct { const char *name; long ln[3]; u8 eff[3]; u8 grad; } effects[] = {
  { "Office", { 6350, 12700, 19050 }, { EK_NONE, EK_NONE, EK_SHADOW }, 1 },
  { "Office 2007 - 2010", { 9525, 25400, 38100 }, { EK_SHADOW, EK_SHADOW, EK_BEVEL }, 1 },
  { "Subtle Solids", { 6350, 12700, 19050 }, { EK_NONE, EK_NONE, EK_NONE }, 0 },
  { "Banded Edge", { 12700, 19050, 25400 }, { EK_NONE, EK_SHADOW, EK_BEVEL }, 0 },
  { "Smokey Glass", { 9525, 12700, 19050 }, { EK_NONE, EK_SOFTSHADOW, EK_REFLECT }, 1 },
  { "Glow Edge", { 9525, 12700, 19050 }, { EK_NONE, EK_GLOW, EK_GLOW }, 1 },
  { "Frosted Glass", { 6350, 9525, 12700 }, { EK_NONE, EK_SOFTEDGE, EK_REFLECT }, 1 },
  { "Top Shadow", { 6350, 12700, 19050 }, { EK_TOPSHADOW, EK_TOPSHADOW, EK_TOPSHADOW }, 0 },
  { "Inset", { 6350, 12700, 19050 }, { EK_NONE, EK_INSET, EK_INSET }, 1 },
  { "Reflection", { 6350, 12700, 19050 }, { EK_NONE, EK_NONE, EK_REFLECT }, 1 },
  { "Extreme Shadow", { 12700, 19050, 25400 }, { EK_BIGSHADOW, EK_BIGSHADOW, EK_BIGSHADOW }, 0 },
  { "Glossy", { 9525, 12700, 19050 }, { EK_NONE, EK_BEVEL, EK_BEVEL }, 1 },
};
#define NEFFECTS ((int)(sizeof effects / sizeof effects[0]))
static const struct { const char *name; u8 colors, fonts, effects; } othemes[] = {
  { "Office", 0, 0, 0 }, { "Office 2013 - 2022", 1, 1, 0 }, { "Office 2007 - 2010", 2, 2, 1 },
  { "Facet", 18, 15, 0 }, { "Integral", 6, 9, 2 }, { "Ion", 19, 8, 4 }, { "Retrospect", 10, 5, 3 }, { "Wisp", 20, 8, 6 },
  { "Slipstream", 16, 18, 5 }, { "Median", 13, 17, 7 },
};
#define NTHEMES ((int)(sizeof othemes / sizeof othemes[0]))

int theme_ncolorsets(void) { return NCOLORSETS; }
const char *theme_colorset_name(int i) { return i >= 0 && i < NCOLORSETS ? colorsets[i].name : ""; }
const u32 *theme_colorset(int i) { return colorsets[i >= 0 && i < NCOLORSETS ? i : 1].c; }
int theme_nfontsets(void) { return NFONTSETS; }
const char *theme_fontset_name(int i) { return i >= 0 && i < NFONTSETS ? fontsets[i].name : ""; }
void theme_fontset(int i, const char **major, const char **minor)
{
  if (i < 0 || i >= NFONTSETS) i = 1;
  if (major) *major = fontsets[i].major;
  if (minor) *minor = fontsets[i].minor;
}
int theme_neffects(void) { return NEFFECTS; }
const char *theme_effect_name(int i) { return i >= 0 && i < NEFFECTS ? effects[i].name : ""; }
int theme_nthemes(void) { return NTHEMES; }
const char *theme_office_name(int i) { return i >= 0 && i < NTHEMES ? othemes[i].name : ""; }
void theme_office(int i, int *colors, int *fonts, int *eff)
{
  if (i < 0 || i >= NTHEMES) i = 1;
  if (colors) *colors = othemes[i].colors;
  if (fonts) *fonts = othemes[i].fonts;
  if (eff) *eff = othemes[i].effects;
}

void theme_init_office(DocTheme *t)
{
  memset(t, 0, sizeof *t);
  strcpy(t->name, "Office Theme");
  strcpy(t->cname, "Office"); memcpy(t->clr, colorsets[1].c, sizeof t->clr);
  strcpy(t->fname, "Office"); strcpy(t->major, "Calibri Light"); strcpy(t->minor, "Calibri");
  strcpy(t->ename, "Office"); t->effects = 0;
}
const DocTheme *theme_of(const Doc *d)
{
  static DocTheme office;
  static int made;
  if (d && d->theme) return d->theme;
  if (!made) { theme_init_office(&office); made = 1; }
  return &office;
}
DocTheme *theme_get(Doc *d)
{
  if (!d->theme && (d->theme = (DocTheme *)malloc(sizeof(DocTheme)))) theme_init_office(d->theme);
  return d->theme;
}
void theme_free(Doc *d)
{
  if (!d || !d->theme) return;
  free(d->theme->xml); free(d->theme->keyraw);
  free(d->theme);
  d->theme = NULL;
}

/* ---- colours */
static const char *const wnames[THC_N] = { "dark1", "light1", "dark2", "light2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6",
  "hyperlink", "followedHyperlink", "background1", "text1", "background2", "text2" };
static const char *const dnames[THC_N] = { "dk1", "lt1", "dk2", "lt2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6",
  "hlink", "folHlink", "bg1", "tx1", "bg2", "tx2" };
static const char *const labels[THC_N] = { "Dark 1", "Light 1", "Dark 2", "Light 2", "Accent 1", "Accent 2", "Accent 3", "Accent 4", "Accent 5", "Accent 6",
  "Hyperlink", "Followed Hyperlink", "Background 1", "Text 1", "Background 2", "Text 2" };
const char *thc_word_name(int c) { return c >= 0 && c < THC_N ? wnames[c] : "text1"; }
const char *thc_dml_name(int c) { return c >= 0 && c < THC_N ? dnames[c] : "tx1"; }
const char *thc_label(int c) { return c >= 0 && c < THC_N ? labels[c] : ""; }
int thc_from_word(const char *s) { int i; if (s) for (i = 0; i < THC_N; i++) if (!strcmp(s, wnames[i])) return i; return -1; }
int thc_from_dml(const char *s) { int i; if (s) for (i = 0; i < THC_N; i++) if (!strcmp(s, dnames[i])) return i; return -1; }
static int hexbyte(const char *s)
{
  int v = 0, i;
  if (!s) return -1;
  for (i = 0; i < 2; i++) {
    int c = s[i], d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (d < 0) return -1;
    v = v * 16 + d;
  }
  return s[2] ? -1 : v;
}
u32 thr_from_word(const char *color, const char *tint, const char *shade)
{
  int c = thc_from_word(color), v;
  if (c < 0) return 0;
  if ((v = hexbyte(shade)) >= 0 && v < 255) return THR(c, THM_SHADE, v, 0);
  if ((v = hexbyte(tint)) >= 0 && v < 255) return THR(c, THM_TINT, v, 0);
  return THR(c, THM_NONE, 0, 0);
}
void thr_word_attrs(Buf *b, u32 ref, const char *pfx)
{
  int c = THR_COLOR(ref), m = THR_MOD(ref);
  if (c < 0 || c >= THC_N) return;
  buf_printf(b, " w:%s%s=\"%s\"", pfx, strcmp(pfx, "theme") ? "" : "Color", wnames[c]);
  if (m == THM_TINT) buf_printf(b, " w:%sTint=\"%02X\"", pfx, (unsigned)THR_A(ref));
  else if (m == THM_SHADE) buf_printf(b, " w:%sShade=\"%02X\"", pfx, (unsigned)THR_A(ref));
}
void thr_dml(Buf *b, u32 ref)
{
  int c = THR_COLOR(ref), m = THR_MOD(ref);
  if (c < 0 || c >= THC_N) return;
  buf_printf(b, "<a:schemeClr val=\"%s\"", dnames[c]);
  switch (m) {
  case THM_LUM:
    buf_printf(b, "><a:lumMod val=\"%d\"/>", THR_A(ref) * 1000);
    if (THR_B(ref)) buf_printf(b, "<a:lumOff val=\"%d\"/>", THR_B(ref) * 1000);
    buf_adds(b, "</a:schemeClr>");
    break;
  case THM_DTINT: buf_printf(b, "><a:tint val=\"%d\"/></a:schemeClr>", THR_A(ref) * 1000); break;
  case THM_DSHADE: buf_printf(b, "><a:shade val=\"%d\"/></a:schemeClr>", THR_A(ref) * 1000); break;
  case THM_TINT: buf_printf(b, "><a:lumMod val=\"%d\"/><a:lumOff val=\"%d\"/></a:schemeClr>", (THR_A(ref) * 100 + 127) / 255 * 1000, 100000 - (THR_A(ref) * 100 + 127) / 255 * 1000); break;
  case THM_SHADE: buf_printf(b, "><a:lumMod val=\"%d\"/></a:schemeClr>", (THR_A(ref) * 100 + 127) / 255 * 1000); break;
  default: buf_adds(b, "/>");
  }
}
void rgb_hsl(u32 rgb, int *h, int *s, int *l)
{
  long r = (long)((rgb >> 16) & 255), g = (long)((rgb >> 8) & 255), b = (long)(rgb & 255);
  long mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b), d = mx - mn, hh;
  *l = (int)((mx + mn) * 1000 / 510);
  if (!d) { *h = 0; *s = 0; return; }
  *s = (int)(*l < 500 ? d * 1000 / (mx + mn) : d * 1000 / (510 - mx - mn));
  if (mx == r) hh = 600 * (g - b) / d;
  else if (mx == g) hh = 600 * (b - r) / d + 1200;
  else hh = 600 * (r - g) / d + 2400;
  if (hh < 0) hh += 3600;
  *h = (int)(hh % 3600);
}
static long hue2(long p, long q, long t)
{
  if (t < 0) t += 3600;
  if (t >= 3600) t -= 3600;
  if (t < 600) return p + (q - p) * t / 600;
  if (t < 1800) return q;
  if (t < 2400) return p + (q - p) * (2400 - t) / 600;
  return p;
}
u32 hsl_rgb(int h, int s, int l)
{
  long q, p, r, g, b;
  if (l < 0) l = 0;
  if (l > 1000) l = 1000;
  if (s < 0) s = 0;
  if (s > 1000) s = 1000;
  if (!s) { r = g = b = ((long)l * 255 + 500) / 1000; return (u32)(r << 16 | g << 8 | b); }
  q = l < 500 ? (long)l * (1000 + s) / 1000 : (long)l + s - (long)l * s / 1000;
  p = 2L * l - q;
  r = (hue2(p, q, h + 1200) * 255 + 500) / 1000; g = (hue2(p, q, h) * 255 + 500) / 1000; b = (hue2(p, q, h - 1200) * 255 + 500) / 1000;
  return (u32)((r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b));
}
u32 theme_rgb(const DocTheme *t, u32 ref)
{
  int c = THR_COLOR(ref), m = THR_MOD(ref), a = THR_A(ref), h, s, l;
  u32 col, r, g, b;
  if (c < 0 || c >= THC_N) return 0;
  if (c >= THC_BG1) c = c == THC_BG1 ? THC_LT1 : c == THC_TX1 ? THC_DK1 : c == THC_BG2 ? THC_LT2 : THC_DK2;
  col = t->clr[c];
  if (m == THM_NONE) return col;
  r = (col >> 16) & 255; g = (col >> 8) & 255; b = col & 255;
  if (m == THM_DSHADE) return (r * a / 100) << 16 | (g * a / 100) << 8 | (b * a / 100);
  if (m == THM_DTINT) return (255 - (255 - r) * a / 100) << 16 | (255 - (255 - g) * a / 100) << 8 | (255 - (255 - b) * a / 100);
  rgb_hsl(col, &h, &s, &l);
  if (m == THM_SHADE) l = l * a / 255;
  else if (m == THM_TINT) l = 1000 - (1000 - l) * a / 255;
  else if (m == THM_LUM) l = l * a / 100 + THR_B(ref) * 10;
  return hsl_rgb(h, s, l);
}
int theme_ref_ok(const Doc *d, u32 ref, u32 rgb)
{
  u32 t;
  int k;
  if (!ref || THR_COLOR(ref) < 0 || THR_COLOR(ref) >= THC_N || rgb == COLOR_AUTO) return 0;
  t = theme_rgb(theme_of(d), ref);
  for (k = 0; k < 24; k += 8) {
    int x = (int)((t >> k) & 255) - (int)((rgb >> k) & 255);
    if (x > 6 || x < -6) return 0;
  }
  return 1;
}
const char *theme_font_name(const DocTheme *t, u8 which)
{
  return which == THF_MAJOR ? t->major : which == THF_MINOR ? t->minor : NULL;
}
int theme_font_ok(const Doc *d, u8 which, int font)
{
  const char *n = theme_font_name(theme_of(d), which);
  return n && n[0] && font >= 0 && font < d->nfonts && str_ieq(d->fonts[font], n);
}

/* ---- theme1.xml */
static const char *const snames[12] = { "dk1", "lt1", "dk2", "lt2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6", "hlink", "folHlink" };
static u32 hex6(const char *v)
{
  u32 c = 0;
  int i;
  if (!v) return COLOR_AUTO;
  for (i = 0; i < 6; i++) {
    int ch = v[i], dd = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
    if (dd < 0) return COLOR_AUTO;
    c = c << 4 | (u32)dd;
  }
  return c;
}
static void cpy(char *dst, const char *src, size_t cap) { if (src) { strncpy(dst, src, cap - 1); dst[cap - 1] = 0; } }
int theme_parse(DocTheme *t, const char *xml, size_t n)
{
  Xml x;
  int mode = 0, cl = -1, k;
  char *copy = (char *)malloc(n + 1);
  if (!copy) return 0;
  memcpy(copy, xml, n); copy[n] = 0;
  free(t->xml);
  t->xml = copy; t->len = n;
  t->effects = -1;
  t->newcolors = t->newfonts = t->neweffects = t->newname = 0;
  xml_init(&x, copy, n);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START) {
      if (!strcmp(x.name, "theme")) cpy(t->name, xml_attr(&x, "name"), sizeof t->name);
      else if (!strcmp(x.name, "clrScheme")) cpy(t->cname, xml_attr(&x, "name"), sizeof t->cname);
      else if (!strcmp(x.name, "fontScheme")) cpy(t->fname, xml_attr(&x, "name"), sizeof t->fname);
      else if (!strcmp(x.name, "fmtScheme")) cpy(t->ename, xml_attr(&x, "name"), sizeof t->ename);
      else if (!strcmp(x.name, "extraClrSchemeLst") || !strcmp(x.name, "objectDefaults")) { xml_skip(&x); continue; }
      for (k = 0; k < 12; k++) if (!strcmp(x.name, snames[k])) cl = k;
      if (cl >= 0 && (!strcmp(x.name, "srgbClr") || !strcmp(x.name, "sysClr"))) {
        u32 v = hex6(xml_attr(&x, !strcmp(x.name, "sysClr") ? "lastClr" : "val"));
        if (v != COLOR_AUTO) t->clr[cl] = v;
        cl = -1;
      }
      if (!strcmp(x.name, "majorFont")) mode = 1;
      else if (!strcmp(x.name, "minorFont")) mode = 2;
      else if (mode && !strcmp(x.name, "latin")) cpy(mode == 1 ? t->major : t->minor, xml_attr(&x, "typeface"), sizeof t->major);
    } else if (x.type == XT_END && (!strcmp(x.name, "majorFont") || !strcmp(x.name, "minorFont"))) mode = 0;
  }
  xml_free(&x);
  return 1;
}
static void esc(Buf *b, const char *s) { xml_escape(b, s, strlen(s)); }
static void w_clrscheme(Buf *b, const DocTheme *t)
{
  int k;
  buf_adds(b, "<a:clrScheme name=\""); esc(b, t->cname[0] ? t->cname : "Custom"); buf_adds(b, "\">");
  for (k = 0; k < 12; k++) {
    if (k == 0 && t->clr[0] == 0) buf_adds(b, "<a:dk1><a:sysClr val=\"windowText\" lastClr=\"000000\"/></a:dk1>");
    else if (k == 1 && t->clr[1] == 0xFFFFFF) buf_adds(b, "<a:lt1><a:sysClr val=\"window\" lastClr=\"FFFFFF\"/></a:lt1>");
    else buf_printf(b, "<a:%s><a:srgbClr val=\"%06lX\"/></a:%s>", snames[k], (unsigned long)(t->clr[k] & 0xFFFFFF), snames[k]);
  }
  buf_adds(b, "</a:clrScheme>");
}
static void w_fontscheme(Buf *b, const DocTheme *t)
{
  buf_adds(b, "<a:fontScheme name=\""); esc(b, t->fname[0] ? t->fname : "Custom");
  buf_adds(b, "\"><a:majorFont><a:latin typeface=\""); esc(b, t->major);
  buf_adds(b, "\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:majorFont><a:minorFont><a:latin typeface=\""); esc(b, t->minor);
  buf_adds(b, "\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:minorFont></a:fontScheme>");
}
static void w_grad(Buf *b, int strong)
{
  static const int tints[2][3] = { { 67000, 73000, 81000 }, { 94000, 100000, 78000 } };
  int k;
  buf_adds(b, "<a:gradFill rotWithShape=\"1\"><a:gsLst>");
  for (k = 0; k < 3; k++)
    buf_printf(b, "<a:gs pos=\"%d\"><a:schemeClr val=\"phClr\"><a:%s val=\"%d\"/><a:satMod val=\"%d\"/></a:schemeClr></a:gs>",
               k * 50000, strong && k == 2 ? "shade" : "tint", tints[strong][k], 103000 + k * 3000);
  buf_adds(b, "</a:gsLst><a:lin ang=\"5400000\" scaled=\"0\"/></a:gradFill>");
}
static void w_effect(Buf *b, int kind)
{
  buf_adds(b, "<a:effectStyle>");
  switch (kind) {
  case EK_SHADOW: buf_adds(b, "<a:effectLst><a:outerShdw blurRad=\"57150\" dist=\"19050\" dir=\"5400000\" algn=\"ctr\" rotWithShape=\"0\"><a:srgbClr val=\"000000\"><a:alpha val=\"63000\"/></a:srgbClr></a:outerShdw></a:effectLst>"); break;
  case EK_SOFTSHADOW: buf_adds(b, "<a:effectLst><a:outerShdw blurRad=\"101600\" dist=\"38100\" dir=\"5400000\" algn=\"t\" rotWithShape=\"0\"><a:srgbClr val=\"000000\"><a:alpha val=\"35000\"/></a:srgbClr></a:outerShdw></a:effectLst>"); break;
  case EK_BIGSHADOW: buf_adds(b, "<a:effectLst><a:outerShdw blurRad=\"152400\" dist=\"76200\" dir=\"2700000\" algn=\"tl\" rotWithShape=\"0\"><a:srgbClr val=\"000000\"><a:alpha val=\"60000\"/></a:srgbClr></a:outerShdw></a:effectLst>"); break;
  case EK_TOPSHADOW: buf_adds(b, "<a:effectLst><a:outerShdw blurRad=\"63500\" dist=\"25400\" dir=\"16200000\" algn=\"b\" rotWithShape=\"0\"><a:srgbClr val=\"000000\"><a:alpha val=\"40000\"/></a:srgbClr></a:outerShdw></a:effectLst>"); break;
  case EK_GLOW: buf_adds(b, "<a:effectLst><a:glow rad=\"63500\"><a:schemeClr val=\"phClr\"><a:satMod val=\"175000\"/><a:alpha val=\"40000\"/></a:schemeClr></a:glow></a:effectLst>"); break;
  case EK_REFLECT: buf_adds(b, "<a:effectLst><a:reflection blurRad=\"6350\" stA=\"52000\" endA=\"300\" endPos=\"35000\" dir=\"5400000\" sy=\"-100000\" algn=\"bl\" rotWithShape=\"0\"/></a:effectLst>"); break;
  case EK_SOFTEDGE: buf_adds(b, "<a:effectLst><a:softEdge rad=\"31750\"/></a:effectLst>"); break;
  case EK_BEVEL:
    buf_adds(b, "<a:effectLst><a:outerShdw blurRad=\"40000\" dist=\"23000\" dir=\"5400000\" rotWithShape=\"0\"><a:srgbClr val=\"000000\"><a:alpha val=\"35000\"/></a:srgbClr></a:outerShdw></a:effectLst>"
                "<a:scene3d><a:camera prst=\"orthographicFront\"><a:rot lat=\"0\" lon=\"0\" rev=\"0\"/></a:camera><a:lightRig rig=\"threePt\" dir=\"t\"><a:rot lat=\"0\" lon=\"0\" rev=\"1200000\"/></a:lightRig></a:scene3d>"
                "<a:sp3d><a:bevelT w=\"63500\" h=\"25400\"/></a:sp3d>");
    break;
  case EK_INSET: buf_adds(b, "<a:effectLst><a:innerShdw blurRad=\"63500\" dist=\"25400\" dir=\"13500000\"><a:srgbClr val=\"000000\"><a:alpha val=\"50000\"/></a:srgbClr></a:innerShdw></a:effectLst>"); break;
  default: buf_adds(b, "<a:effectLst/>");
  }
  buf_adds(b, "</a:effectStyle>");
}
static void w_fmtscheme(Buf *b, int e)
{
  int k;
  if (e < 0 || e >= NEFFECTS) e = 0;
  buf_adds(b, "<a:fmtScheme name=\""); esc(b, effects[e].name); buf_adds(b, "\"><a:fillStyleLst><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>");
  if (effects[e].grad) { w_grad(b, 0); w_grad(b, 1); }
  else buf_adds(b, "<a:solidFill><a:schemeClr val=\"phClr\"><a:tint val=\"50000\"/></a:schemeClr></a:solidFill><a:solidFill><a:schemeClr val=\"phClr\"><a:shade val=\"80000\"/></a:schemeClr></a:solidFill>");
  buf_adds(b, "</a:fillStyleLst><a:lnStyleLst>");
  for (k = 0; k < 3; k++)
    buf_printf(b, "<a:ln w=\"%ld\" cap=\"flat\" cmpd=\"sng\" algn=\"ctr\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/><a:miter lim=\"800000\"/></a:ln>", effects[e].ln[k]);
  buf_adds(b, "</a:lnStyleLst><a:effectStyleLst>");
  for (k = 0; k < 3; k++) w_effect(b, effects[e].eff[k]);
  buf_adds(b, "</a:effectStyleLst><a:bgFillStyleLst><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
              "<a:solidFill><a:schemeClr val=\"phClr\"><a:tint val=\"95000\"/><a:satMod val=\"170000\"/></a:schemeClr></a:solidFill>");
  w_grad(b, 1);
  buf_adds(b, "</a:bgFillStyleLst></a:fmtScheme>");
}
/* the element named tag (with the a: prefix) in s: where it starts and ends (just past its closing
   tag); 0 if it isn't there in that form */
static int find_elem(const char *s, const char *tag, size_t *a, size_t *z)
{
  char open[48], close[48];
  const char *p, *q;
  sprintf(open, "<a:%s", tag); sprintf(close, "</a:%s>", tag);
  for (p = strstr(s, open); p && p[strlen(open)] != ' ' && p[strlen(open)] != '>'; p = strstr(p + 1, open)) ;
  if (!p || !(q = strstr(p, close))) return 0;
  *a = (size_t)(p - s); *z = (size_t)(q - s) + strlen(close);
  return 1;
}
/* t's theme1.xml written out whole */
static void w_theme_new(Buf *b, const DocTheme *t)
{
  buf_adds(b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" name=\"");
  esc(b, t->name[0] ? t->name : "Office Theme");
  buf_adds(b, "\"><a:themeElements>");
  w_clrscheme(b, t); w_fontscheme(b, t); w_fmtscheme(b, t->effects);
  buf_adds(b, "</a:themeElements><a:objectDefaults/><a:extraClrSchemeLst/></a:theme>");
}
char *theme_xml(const DocTheme *t, size_t *len)
{
  Buf b;
  buf_init(&b);
  if (!t->xml || (!t->newcolors && !t->newfonts && !t->neweffects && !t->newname)) {
    if (t->xml) buf_add(&b, t->xml, t->len); else w_theme_new(&b, t);
  } else {
    /* the theme read, with the parts changed written afresh (in document order) */
    size_t pos = 0, a, z;
    const char *s = t->xml;
    size_t ca = 0, cz = 0, fa = 0, fz = 0, ea = 0, ez = 0;
    int hc = find_elem(s, "clrScheme", &ca, &cz), hf = find_elem(s, "fontScheme", &fa, &fz), he = find_elem(s, "fmtScheme", &ea, &ez);
    if ((t->newcolors && !hc) || (t->newfonts && !hf) || (t->neweffects && !he) || !(ca < fa && fa < ea)) w_theme_new(&b, t);
    else {
      if (t->newname && (a = (size_t)(strstr(s, "<a:theme") ? strstr(s, "<a:theme") - s : 0)) < ca) {
        const char *nm = strstr(s + a, " name=\"");
        if (nm && (size_t)(nm - s) < ca) {
          const char *e = strchr(nm + 7, '"');
          if (e) { buf_add(&b, s, (size_t)(nm - s) + 7); esc(&b, t->name); pos = (size_t)(e - s); }
        }
      }
      buf_add(&b, s + pos, ca - pos);
      if (t->newcolors) w_clrscheme(&b, t); else buf_add(&b, s + ca, cz - ca);
      buf_add(&b, s + cz, fa - cz);
      if (t->newfonts) w_fontscheme(&b, t); else buf_add(&b, s + fa, fz - fa);
      buf_add(&b, s + fz, ea - fz);
      if (t->neweffects) w_fmtscheme(&b, t->effects); else buf_add(&b, s + ea, ez - ea);
      z = t->len;
      buf_add(&b, s + ez, z - ez);
    }
  }
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return buf_take(&b);
}
int theme_colorset_of(const Doc *d)
{
  const DocTheme *t = theme_of(d);
  int i;
  for (i = 0; i < NCOLORSETS; i++) if (!memcmp(t->clr + 2, colorsets[i].c + 2, 10 * sizeof(u32))) return i;
  return -1;
}
int theme_fontset_of(const Doc *d)
{
  const DocTheme *t = theme_of(d);
  int i;
  for (i = 0; i < NFONTSETS; i++) if (str_ieq(t->major, fontsets[i].major) && str_ieq(t->minor, fontsets[i].minor)) return i;
  return -1;
}

/* ---- applying a changed theme to the document */
typedef struct { DocTheme old; } Was;
/* a character format brought to the theme as it is now (it was right for the old one); 1 if it changed */
static int cf_retheme(Doc *d, CharFmt *c, const DocTheme *old)
{
  int ch = 0;
  const DocTheme *t = theme_of(d);
  if (c->thcolor && c->color != COLOR_AUTO) {
    Doc tmp;
    tmp.theme = (DocTheme *)old;
    if (theme_ref_ok(&tmp, c->thcolor, c->color)) { u32 v = theme_rgb(t, c->thcolor); if (v != c->color) { c->color = v; ch = 1; } }
  }
  if (c->thfont && c->font < d->nfonts) {
    const char *on = theme_font_name(old, c->thfont), *nn = theme_font_name(t, c->thfont);
    if (on && nn && nn[0] && str_ieq(d->fonts[c->font], on) && !str_ieq(on, nn)) { c->font = (u8)doc_font(d, nn); ch = 1; }
  }
  return ch;
}
static int col_retheme(Doc *d, u32 ref, u32 *col, const DocTheme *old, int hlon)
{
  Doc tmp;
  u32 c = hlon ? (*col & 0xFFFFFF) : *col, v;
  if (!ref || (hlon && !*col) || c == COLOR_AUTO) return 0;
  tmp.theme = (DocTheme *)old;
  if (!theme_ref_ok(&tmp, ref, c)) return 0;
  v = theme_rgb(theme_of(d), ref);
  if (v == c) return 0;
  *col = hlon ? (HL_ON | v) : v;
  return 1;
}
static int pf_retheme(Doc *d, ParaFmt *p, const DocTheme *old)
{
  int ch = 0, k;
  ch |= col_retheme(d, p->thshade, &p->shade, old, 1);
  for (k = 0; k < 5; k++) ch |= col_retheme(d, p->thbcolor[k], &p->bcolor[k], old, 0);
  ch |= col_retheme(d, p->cell.thshade, &p->cell.shade, old, 0);
  ch |= col_retheme(d, p->outer.thshade, &p->outer.shade, old, 0);
  return ch;
}
static int img_retheme(Doc *d, Image *im, const DocTheme *old)
{
  int ch = 0, k;
  ch |= col_retheme(d, im->thfill, &im->fill, old, 0);
  ch |= col_retheme(d, im->thline, &im->lcolor, old, 0);
  for (k = 0; k < im->nkids; k++) ch |= img_retheme(d, &im->kids[k], old);
  return ch;
}
/* the styles and defaults (inside doc_restyle) */
static void styles_retheme(Doc *d, void *arg)
{
  const DocTheme *old = &((Was *)arg)->old;
  int k;
  cf_retheme(d, &d->defcf, old);
  pf_retheme(d, &d->defpf, old);
  for (k = 0; k < d->nstyles; k++) {
    Style *s = &d->styles[k];
    if (s->type != ST_PARA && s->type != ST_CHAR) continue;
    cf_retheme(d, &s->ocf, old);
    pf_retheme(d, &s->opf, old);
  }
}
/* a story's paragraphs (not in the undo history), changed in place */
static void story_retheme(Doc *d, Story *st, const DocTheme *old)
{
  u32 i, r;
  if (!st) return;
  for (i = 0; i < st->np; i++) {
    Para *p = st->p[i];
    int ch = pf_retheme(d, &p->pf, old);
    for (r = 0; r < p->nr; r++) ch |= cf_retheme(d, &p->r[r].f, old);
    if (ch) { para_normalise(p); para_touch(p); }
  }
}
/* everything that uses the theme, from old to the document's theme now */
static int doc_retheme(Doc *d, const DocTheme *old)
{
  Was w;
  u32 i, r;
  int k, ok;
  Pos at;
  at.para = 0; at.off = 0;
  w.old = *old;
  doc_begin(d, at);
  ok = doc_restyle(d, styles_retheme, &w);
  /* the body's own formatting (undoable) */
  for (i = 0; ok && i < d->np; i++) {
    Para *p = d->p[i];
    ParaFmt pf = p->pf;
    u32 guard = 0;
    if (pf_retheme(d, &pf, old)) doc_para_format(d, i, i, PM_SHADE | PM_BORDER | PM_CELL, &pf);
    for (r = 0; r < d->p[i]->nr; r++) {
      CharFmt c = d->p[i]->r[r].f;
      if (cf_retheme(d, &c, old)) {
        Pos a, b;
        a.para = b.para = i; a.off = r ? d->p[i]->r[r - 1].end : 0; b.off = d->p[i]->r[r].end;
        if (a.off < b.off) doc_format(d, a, b, CM_COLOR | CM_FONT, &c);
        else { d->p[i]->r[r].f = c; para_touch(d->p[i]); }
        if (++guard < 4 * d->p[i]->nr + 8) r = (u32)-1;   /* (the runs may have merged: from the start again; a changed run now fits) */
      }
    }
    /* objects referred to in it */
    for (r = 0; r + 4 <= d->p[i]->len; r++) {
      const char *t = d->p[i]->t + r;
      if (IS_PIC(t)) {
        int n = pic_index(t);
        Image im;
        if (n < d->nimages && img_clone(&im, &d->images[n], 0)) {
          Pos a;
          a.para = i; a.off = r;
          if (img_retheme(d, &im, old)) doc_set_object(d, a, &im);
          img_release(&im);
        }
        r += 3;
      }
    }
  }
  /* headers, footers, notes, comments and text boxes (outside the history) */
  for (k = 0; k < HF_N; k++) { story_retheme(d, &d->hdr[k], old); story_retheme(d, &d->ftr[k], old); }
  for (k = 0; k < d->nsects; k++) if (d->sects[k].hf) { int j; for (j = 0; j < 2 * HF_N; j++) story_retheme(d, &d->sects[k].hf[j], old); }
  for (k = 0; k < d->nnotes; k++) story_retheme(d, &d->notes[k], old);
  for (k = 0; k < d->nenotes; k++) story_retheme(d, &d->enotes[k], old);
  for (k = 0; k < d->ncomments; k++) story_retheme(d, &d->comments[k].text, old);
  for (k = 0; k < d->nimages; k++) if (d->images[k].text) story_retheme(d, d->images[k].text, old);
  doc_end(d, at);
  d->ver++; d->dirty = 1;
  return ok;
}
int doc_theme_set_colors(Doc *d, const u32 clr[12], const char *name)
{
  DocTheme old = *theme_of(d), *t = theme_get(d);
  if (!t) return 0;
  memcpy(t->clr, clr, sizeof t->clr);
  cpy(t->cname, name ? name : "Custom", sizeof t->cname);
  t->newcolors = 1;
  return doc_retheme(d, &old);
}
int doc_theme_set_fonts(Doc *d, const char *major, const char *minor, const char *name)
{
  DocTheme old = *theme_of(d), *t = theme_get(d);
  if (!t) return 0;
  cpy(t->major, major, sizeof t->major); cpy(t->minor, minor, sizeof t->minor);
  cpy(t->fname, name ? name : "Custom", sizeof t->fname);
  t->newfonts = 1;
  return doc_retheme(d, &old);
}
int doc_theme_set_effects(Doc *d, int e)
{
  DocTheme *t = theme_get(d);
  if (!t || e < 0 || e >= NEFFECTS) return 0;
  t->effects = e; t->neweffects = 1;
  cpy(t->ename, effects[e].name, sizeof t->ename);
  d->dirty = 1; d->ver++;
  return 1;
}
int doc_theme_office(Doc *d, int i)
{
  DocTheme old, *t;
  int c, f, e;
  if (i < 0 || i >= NTHEMES) return 0;
  old = *theme_of(d);
  if (!(t = theme_get(d))) return 0;
  theme_office(i, &c, &f, &e);
  memcpy(t->clr, colorsets[c].c, sizeof t->clr); cpy(t->cname, colorsets[c].name, sizeof t->cname);
  cpy(t->major, fontsets[f].major, sizeof t->major); cpy(t->minor, fontsets[f].minor, sizeof t->minor); cpy(t->fname, fontsets[f].name, sizeof t->fname);
  t->effects = e; cpy(t->ename, effects[e].name, sizeof t->ename);
  sprintf(t->name, "%.50s", othemes[i].name);
  if (!strcmp(othemes[i].name, "Office") || !strncmp(othemes[i].name, "Office ", 7)) strcpy(t->name, "Office Theme");
  t->newcolors = t->newfonts = t->neweffects = t->newname = 1;
  return doc_retheme(d, &old);
}
int doc_theme_load(Doc *d, const u8 *data, size_t len)
{
  DocTheme old, nt, *t;
  char *xml = NULL;
  size_t n = 0;
  int ok;
  memset(&nt, 0, sizeof nt);
  if (len >= 4 && data[0] == 'P' && data[1] == 'K') {
    ZipReader z;
    int i, best = -1;
    if (!zip_open(&z, data, len)) return 0;
    for (i = 0; i < z.n; i++) {
      const char *nm = z.e[i].name, *sl = strrchr(nm, '/');
      if (sl && !strncmp(sl + 1, "theme", 5) && strstr(nm, ".xml") && !strstr(nm, "_rels") && !strstr(nm, "Manager")) {
        if (best < 0 || strstr(nm, "theme1.xml")) best = i;
      }
    }
    if (best >= 0) xml = (char *)zip_read(&z, best, &n);
    zip_close(&z);
  } else if (len > 10 && memchr(data, '<', len < 200 ? len : 200)) {
    if ((xml = (char *)malloc(len + 1))) { memcpy(xml, data, len); xml[len] = 0; n = len; }
  }
  if (!xml) return 0;
  theme_init_office(&nt);
  nt.cname[0] = nt.fname[0] = nt.ename[0] = 0;
  ok = strstr(xml, "clrScheme") != NULL && theme_parse(&nt, xml, n);
  free(xml);
  if (!ok) { free(nt.xml); return 0; }
  old = *theme_of(d);
  if (!(t = theme_get(d))) { free(nt.xml); return 0; }
  free(t->xml);
  nt.keyraw = t->keyraw; nt.keylen = t->keylen;
  *t = nt;
  return doc_retheme(d, &old);
}
u8 *theme_thmx(const Doc *d, size_t *len)
{
  ZipWriter z;
  char *x;
  size_t n;
  int ok;
  static const char ct[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
    "<Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/theme/theme/theme1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\"/></Types>";
  static const char rels[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" "
    "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme\" Target=\"theme/theme/theme1.xml\"/></Relationships>";
  if (!(x = theme_xml(theme_of(d), &n))) return NULL;
  zip_begin(&z);
  ok = zip_add(&z, "[Content_Types].xml", ct, sizeof ct - 1) && zip_add(&z, "_rels/.rels", rels, sizeof rels - 1) &&
       zip_add(&z, "theme/theme/theme1.xml", x, n) && zip_finish(&z);
  free(x);
  if (!ok) { zip_free(&z); return NULL; }
  *len = z.out.len;
  { u8 *out = (u8 *)buf_take(&z.out); zip_free(&z); return out; }
}

/* ---- the styles' shortcut keys (customizations.xml) */
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int b64val(int c) { const char *p = c ? strchr(b64, c) : NULL; return p ? (int)(p - b64) : -1; }
/* a style name from an acd's argValue (base64 of 2 bytes, then UTF-16LE), as UTF-8 */
static void arg_name(const char *v, char *out, size_t cap)
{
  u8 raw[200];
  int n = 0, acc = 0, bits = 0, k;
  size_t o = 0;
  for (; *v && n < (int)sizeof raw; v++) {
    int x = b64val((u8)*v);
    if (x < 0) continue;
    acc = (acc << 6 | x) & 0xFFFFFF; bits += 6;
    if (bits >= 8) { bits -= 8; raw[n++] = (u8)(acc >> bits); }
  }
  for (k = 2; k + 1 < n && o + 5 < cap; k += 2) {
    u32 cp = (u32)raw[k] | (u32)raw[k + 1] << 8;
    if (!cp) break;
    o += (size_t)utf8_put(out + o, cp);
  }
  out[o] = 0;
}
static void name_arg(Buf *b, const char *name)
{
  u8 raw[200];
  int n = 2, k;
  size_t i = 0, len = strlen(name);
  raw[0] = 2; raw[1] = 0;
  while (i < len && n + 2 < (int)sizeof raw) {
    int used;
    u32 cp = utf8_get(name + i, len - i, &used);
    i += (size_t)used;
    if (cp > 0xFFFF) cp = '?';
    raw[n++] = (u8)cp; raw[n++] = (u8)(cp >> 8);
  }
  for (k = 0; k < n; k += 3) {
    u32 v = (u32)raw[k] << 16 | (k + 1 < n ? (u32)raw[k + 1] << 8 : 0) | (k + 2 < n ? raw[k + 2] : 0);
    buf_addc(b, b64[v >> 18 & 63]); buf_addc(b, b64[v >> 12 & 63]);
    buf_addc(b, k + 1 < n ? b64[v >> 6 & 63] : '='); buf_addc(b, k + 2 < n ? b64[v & 63] : '=');
  }
}
void stylekeys_read(Doc *d, const char *xml, size_t n)
{
  Xml x;
  Buf other;
  struct { char name[24], style[64]; int fci65; } acd[64];
  struct { u16 kcm; char acd[24]; const char *raw; size_t len; } km[128];
  int nacd = 0, nkm = 0, i, j;
  DocTheme *t;
  /* first the commands (acds), then the keys that name them */
  xml_init(&x, xml, n);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START && !strcmp(x.name, "acd") && nacd < 64 && xml_attr(&x, "argValue")) {
      const char *f = xml_attr(&x, "fciIndexBasedOn"), *nm = xml_attr(&x, "acdName");
      cpy(acd[nacd].name, nm, sizeof acd[nacd].name);
      arg_name(xml_attr(&x, "argValue"), acd[nacd].style, sizeof acd[nacd].style);
      acd[nacd].fci65 = f && !strcmp(f, "0065");
      nacd++;
    }
  }
  xml_free(&x);
  xml_init(&x, xml, n);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START && !strcmp(x.name, "keymap") && nkm < 128) {
      const char *k = xml_attr(&x, "kcmPrimary");
      int d0 = x.depth;
      km[nkm].kcm = (u16)(k ? strtoul(k, NULL, 16) : 0); km[nkm].acd[0] = 0;
      km[nkm].raw = xml + x.tok;
      while (xml_next(&x) != XT_EOF && !(x.type == XT_END && x.depth < d0))
        if (x.type == XT_START && !strcmp(x.name, "acd")) cpy(km[nkm].acd, xml_attr(&x, "acdName"), sizeof km[nkm].acd);
      km[nkm].len = (size_t)(xml + x.i - km[nkm].raw);
      nkm++;
    }
  }
  xml_free(&x);
  /* style keys become the styles'; everything else is kept as it was */
  buf_init(&other);
  for (i = 0; i < nkm; i++) {
    int used = 0;
    for (j = 0; j < nacd && km[i].acd[0]; j++)
      if (!strcmp(acd[j].name, km[i].acd) && acd[j].fci65) {
        int s = doc_style_by_name(d, acd[j].style);
        if (s >= 0 && km[i].kcm) { d->styles[s].x.key = km[i].kcm; used = 1; }
      }
    if (!used) { buf_adds(&other, "<!--k-->"); buf_add(&other, km[i].raw, km[i].len); }
  }
  /* the acds the kept keys use, and the rest of the customizations (toolbars...) as they are */
  xml_init(&x, xml, n);
  while (xml_next(&x) != XT_EOF) {
    if (x.type != XT_START) continue;
    if (!strcmp(x.name, "tcg") || !strcmp(x.name, "keymaps") || !strcmp(x.name, "acds")) continue;
    if (!strcmp(x.name, "keymap")) { xml_skip(&x); continue; }
    if (!strcmp(x.name, "acd")) {
      const char *nm = xml_attr(&x, "acdName"), *st;
      size_t l;
      int keep = 0;
      for (i = 0; i < nkm && nm; i++) if (!strcmp(km[i].acd, nm)) { for (j = 0; j < nacd; j++) if (!strcmp(acd[j].name, nm) && !acd[j].fci65) keep = 1; }
      for (j = 0; j < nacd && nm; j++) if (!strcmp(acd[j].name, nm) && acd[j].fci65) { int s = doc_style_by_name(d, acd[j].style); if (s < 0) keep = 1; }
      l = xml_raw(&x, &st);
      if (keep) { buf_adds(&other, "<!--a-->"); buf_add(&other, st, l); }
      continue;
    }
    { const char *st; size_t l = xml_raw(&x, &st); buf_adds(&other, "<!--o-->"); buf_add(&other, st, l); }
  }
  xml_free(&x);
  if (other.len && !other.oom && (t = theme_get(d))) { free(t->keyraw); t->keylen = other.len; t->keyraw = buf_take(&other); }
  buf_free(&other);
}
/* the kept pieces of one kind ('k' keymaps, 'a' acds, 'o' others) */
static void kept_kind(Buf *b, const char *raw, size_t n, char kind)
{
  const char *p = raw, *e = raw + n;
  char tag[9];
  sprintf(tag, "<!--%c-->", kind);
  while (p < e) {
    const char *q = strstr(p, "<!--");
    const char *nx;
    if (!q || q >= e) break;
    nx = strstr(q + 8, "<!--");
    if (!nx || nx > e) nx = e;
    if (!strncmp(q, tag, 8)) buf_add(b, q + 8, (size_t)(nx - q - 8));
    p = nx;
  }
}
char *stylekeys_xml(Doc *d, size_t *len)
{
  const DocTheme *t = d->theme;
  Buf b;
  int k, any = 0;
  for (k = 0; k < d->nstyles; k++) if ((d->styles[k].type == ST_PARA || d->styles[k].type == ST_CHAR) && d->styles[k].x.key) any = 1;
  if (!any && !(t && t->keyraw)) return NULL;
  buf_init(&b);
  buf_adds(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<wne:tcg xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
               "xmlns:wne=\"http://schemas.microsoft.com/office/word/2006/wordml\"><wne:keymaps>");
  if (t && t->keyraw) kept_kind(&b, t->keyraw, t->keylen, 'k');
  for (k = 0; k < d->nstyles; k++)
    if ((d->styles[k].type == ST_PARA || d->styles[k].type == ST_CHAR) && d->styles[k].x.key)
      buf_printf(&b, "<wne:keymap wne:kcmPrimary=\"%04X\"><wne:acd wne:acdName=\"acdQ%d\"/></wne:keymap>", (unsigned)d->styles[k].x.key, k);
  buf_adds(&b, "</wne:keymaps>");
  if (t && t->keyraw) kept_kind(&b, t->keyraw, t->keylen, 'o');
  buf_adds(&b, "<wne:acds>");
  if (t && t->keyraw) kept_kind(&b, t->keyraw, t->keylen, 'a');
  for (k = 0; k < d->nstyles; k++)
    if ((d->styles[k].type == ST_PARA || d->styles[k].type == ST_CHAR) && d->styles[k].x.key) {
      char nm[64];
      strcpy(nm, d->styles[k].name);
      if (nm[0] >= 'a' && nm[0] <= 'z' && d->styles[k].builtin) nm[0] = (char)(nm[0] - 32);
      buf_adds(&b, "<wne:acd wne:argValue=\""); name_arg(&b, nm);
      buf_printf(&b, "\" wne:acdName=\"acdQ%d\" wne:fciIndexBasedOn=\"0065\"/>", k);
    }
  buf_adds(&b, "</wne:acds></wne:tcg>");
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return buf_take(&b);
}
void stylekey_name(u16 key, char *out, size_t cap)
{
  char k[12];
  int c = key & 255;
  if (!key) { if (cap) out[0] = 0; return; }
  if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) sprintf(k, "%c", c);
  else if (c >= 0x70 && c <= 0x7B) sprintf(k, "F%d", c - 0x6F);
  else sprintf(k, "#%02X", c);
  snprintf(out, cap, "%s%s%s%s", key & 0x200 ? "Ctrl+" : "", key & 0x400 ? "Alt+" : "", key & 0x100 ? "Shift+" : "", k);
}
