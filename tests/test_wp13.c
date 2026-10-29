/* test_wp13.c - the native tests of WP13 Styles and themes (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp13() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/doctheme.h"
#include "../core/blocks.h"
#include "../core/zip.h"
#include "../core/keep.h"

static char *part13(const u8 *data, size_t n, const char *name, size_t *len)
{
  ZipReader z;
  size_t l = 0;
  u8 *b;
  if (!zip_open(&z, data, n)) return NULL;
  b = zip_read(&z, zip_find(&z, name), &l);
  zip_close(&z);
  if (len) *len = l;
  return (char *)b;
}
static char *saved_part(Doc *d, const char *name)
{
  size_t n;
  u8 *z = docx_save(d, &n);
  char *p = z ? part13(z, n, name, NULL) : NULL;
  free(z);
  return p;
}
static void dump13(Doc *d, const char *name)
{
  const char *dir = getenv("WP13_OUT");
  char path[400];
  if (!dir) return;
  snprintf(path, sizeof path, "%s/%s", dir, name);
  doc_save_file(d, path);
}
static int near(u32 a, u32 b)
{
  int k;
  for (k = 0; k < 24; k += 8) { int x = (int)((a >> k) & 255) - (int)((b >> k) & 255); if (x > 2 || x < -2) return 0; }
  return 1;
}

/* Word's own lighter and darker shades of Office 2013's accent 1 */
static void colours(void)
{
  DocTheme t;
  theme_init_office(&t);
  ok("theme: accent 1 darker 25% (shade BF)", near(theme_rgb(&t, THR(THC_ACCENT1, THM_SHADE, 0xBF, 0)), 0x2F5496));
  ok("theme: accent 1 darker 50% (shade 80)", near(theme_rgb(&t, THR(THC_ACCENT1, THM_SHADE, 0x80, 0)), 0x203864) || near(theme_rgb(&t, THR(THC_ACCENT1, THM_SHADE, 0x80, 0)), 0x1F3864));
  ok("theme: accent 1 lighter 80% (tint 33)", near(theme_rgb(&t, THR(THC_ACCENT1, THM_TINT, 0x33, 0)), 0xDAE3F3) || near(theme_rgb(&t, THR(THC_ACCENT1, THM_TINT, 0x33, 0)), 0xD9E2F3));
  ok("theme: accent 1 lighter 60% (tint 66)", near(theme_rgb(&t, THR(THC_ACCENT1, THM_TINT, 0x66, 0)), 0xB4C7E7) || near(theme_rgb(&t, THR(THC_ACCENT1, THM_TINT, 0x66, 0)), 0xB4C6E7));
  ok("theme: text 1 is dark 1", theme_rgb(&t, THR(THC_TX1, THM_NONE, 0, 0)) == 0);
  ok("theme: lumMod 75 like shade", near(theme_rgb(&t, THR(THC_ACCENT1, THM_LUM, 75, 0)), 0x2F5496));
  ok("theme: hsl round trip", hsl_rgb(0, 0, 500) == 0x808080 || hsl_rgb(0, 0, 500) == 0x7F7F7F);
  ok("theme: word attribute names", thr_from_word("accent2", NULL, "BF") == THR(THC_ACCENT2, THM_SHADE, 0xBF, 0) && thr_from_word("bogus", NULL, NULL) == 0);
}

/* word.docx: Office 2007's theme; the defaults' theme fonts */
static void read_word(void)
{
  char err[200], *x, *o;
  unsigned lost;
  size_t n;
  Doc *d = doc_load_file("tests/data/word.docx", &lost, err, sizeof err), *e;
  if (!d) { ok("theme: word.docx loads", 0); return; }
  ok("theme: word.docx's theme read", d->theme && !strcmp(d->theme->name, "Office Theme") && d->theme->clr[THC_ACCENT1] == 0x4F81BD &&
     !strcmp(d->theme->minor, "Cambria") && !strcmp(d->theme->major, "Calibri"));
  ok("theme: the defaults' font is the theme's", d->defcf.thfont == THF_MINOR && theme_font_ok(d, d->defcf.thfont, d->defcf.font));
  ok("theme: and so is Normal's text, the title's the heading font", d->styles[0].cf.thfont == THF_MINOR && d->p[0]->r[0].f.thfont == THF_MAJOR);
  /* unchanged: written back byte for byte */
  x = saved_part(d, "word/theme/theme1.xml");
  {
    size_t fn;
    u8 *f = file_read("tests/data/word.docx", &fn);
    o = f ? part13(f, fn, "word/theme/theme1.xml", &n) : NULL;
    free(f);
  }
  ok("theme: written back unchanged", x && o && !strcmp(x, o));
  free(x); free(o);
  x = saved_part(d, "word/styles.xml");
  ok("theme: theme fonts written as such", x && strstr(x, "w:asciiTheme=\"minorHAnsi\""));
  free(x);
  /* the Office 2013 font scheme: the text follows */
  doc_theme_set_fonts(d, "Calibri Light", "Arial", "Test");
  ok("theme: new body font in the defaults", !strcmp(d->fonts[d->defcf.font], "Arial") && !strcmp(d->fonts[d->p[0]->r[0].f.font], "Calibri Light"));
  ok("theme: and in Normal", !strcmp(d->fonts[d->styles[0].cf.font], "Arial"));
  x = saved_part(d, "word/theme/theme1.xml");
  ok("theme: the font scheme written afresh, the rest kept", x && strstr(x, "<a:latin typeface=\"Arial\"/>") && strstr(x, "<a:fontScheme name=\"Test\">") &&
     strstr(x, "4F81BD") && strstr(x, "<a:fmtScheme"));
  free(x);
  e = docx_again(d, &lost);
  ok("theme: read back", e && e->theme && !strcmp(e->theme->minor, "Arial") && !strcmp(e->fonts[e->p[0]->r[0].f.font], "Calibri Light") && e->p[0]->r[0].f.thfont == THF_MAJOR &&
     !strcmp(e->fonts[e->styles[0].cf.font], "Arial"));
  doc_free(e);
  doc_free(d);
}

/* a theme colour on text, a paragraph's shading and border, a cell and a shape */
static void refs(void)
{
  Doc *d = doc_new(), *e;
  CharFmt c;
  ParaFmt pf;
  Pos at = P(0, 0);
  u32 a1 = THR(THC_ACCENT1, THM_SHADE, 0xBF, 0), a2 = THR(THC_ACCENT2, THM_NONE, 0, 0), a6 = THR(THC_ACCENT6, THM_TINT, 0x99, 0);
  int sh;
  char *x;
  unsigned lost;
  doc_insert(d, &at, "Theme colours", 13, NULL);
  memset(&c, 0, sizeof c);
  c.color = theme_rgb(theme_of(d), a1); c.thcolor = a1;
  doc_format(d, P(0, 0), P(0, 5), CM_COLOR, &c);
  memset(&pf, 0, sizeof pf);
  pf.shade = HL_ON | theme_rgb(theme_of(d), a6); pf.thshade = a6;
  pf.bstyle[PE_BOTTOM] = PB_SINGLE; pf.bwidth[PE_BOTTOM] = 8; pf.bcolor[PE_BOTTOM] = theme_rgb(theme_of(d), a2); pf.thbcolor[PE_BOTTOM] = a2;
  doc_para_format(d, 0, 0, PM_SHADE | PM_BORDER, &pf);
  sh = doc_add_shape(d, SH_RECT, 1440, 720);
  if (sh >= 0) { d->images[sh].fill = theme_rgb(theme_of(d), a2); d->images[sh].thfill = a2; d->images[sh].thline = a1; d->images[sh].lcolor = theme_rgb(theme_of(d), a1); d->images[sh].lstyle = PB_SINGLE; d->images[sh].lwidth = 8; }
  if (sh >= 0) { char ref[5]; Pos e1 = doc_end_pos(d); pic_utf8(ref, sh); doc_insert(d, &e1, ref, 4, NULL); }
  x = saved_part(d, "word/document.xml");
  ok("theme refs: run colour written", x && strstr(x, "w:themeColor=\"accent1\" w:themeShade=\"BF\""));
  ok("theme refs: shading written", x && strstr(x, "w:themeFill=\"accent6\" w:themeFillTint=\"99\""));
  ok("theme refs: border written", x && strstr(x, "w:themeColor=\"accent2\"/>"));
  ok("theme refs: shape fill written", x && strstr(x, "<a:solidFill><a:schemeClr val=\"accent2\"/></a:solidFill>"));
  free(x);
  x = saved_part(d, "word/theme/theme1.xml");
  ok("theme: a document without one gets Office's", x && strstr(x, "<a:clrScheme name=\"Office\">") && strstr(x, "4472C4"));
  free(x);
  e = docx_again(d, &lost);
  ok("theme refs: read back", e && e->p[0]->r[0].f.thcolor == a1 && e->p[0]->pf.thshade == a6 && e->p[0]->pf.thbcolor[PE_BOTTOM] == a2);
  ok("theme refs: shape read back", e && e->nimages > 0 && e->images[e->nimages - 1].thfill == a2 &&
     near(theme_rgb(theme_of(e), e->images[e->nimages - 1].thline), theme_rgb(theme_of(e), a1)));
  /* a new colour scheme: everything that uses it changes; undo brings the text back */
  if (e) {
    u32 before = e->p[0]->r[0].f.color;
    int k = 0;
    for (k = 0; k < theme_ncolorsets() && strcmp(theme_colorset_name(k), "Green"); k++) ;
    doc_theme_set_colors(e, theme_colorset(k), theme_colorset_name(k));
    ok("theme colours: text recoloured", e->p[0]->r[0].f.color == theme_rgb(theme_of(e), a1) && e->p[0]->r[0].f.color != before);
    ok("theme colours: shading and border too", (e->p[0]->pf.shade & 0xFFFFFF) == theme_rgb(theme_of(e), a6) && e->p[0]->pf.bcolor[PE_BOTTOM] == 0x8AB833);
    ok("theme colours: shape too", e->images[e->nimages - 1].fill == 0x8AB833 || e->images[pic_index(e->p[0]->t + e->p[0]->len - 4)].fill == 0x8AB833);
    ok("theme colours: the matching set is known", theme_colorset_of(e) == k);
    x = saved_part(e, "word/theme/theme1.xml");
    ok("theme colours: written", x && strstr(x, "<a:clrScheme name=\"Green\">") && strstr(x, "549E39"));
    free(x);
    doc_undo(e, &at);
    ok("theme colours: undone", e->p[0]->r[0].f.color == before);
    /* colour set without a reference: a stale reference is ignored */
    memset(&c, 0, sizeof c); c.color = 0x123456;
    doc_format(e, P(0, 0), P(0, 5), CM_COLOR, &c);
    x = saved_part(e, "word/document.xml");
    ok("theme refs: a plain colour drops the reference", x && strstr(x, "123456") && !strstr(x, "w:themeShade=\"BF\""));
    free(x);
  }
  doc_free(e);
  doc_free(d);
}

/* effects, Office themes, and themes from other files */
static void effects(void)
{
  Doc *d = doc_new(), *e;
  char *x;
  u8 *t;
  size_t n;
  unsigned lost;
  int i, ok2 = 1;
  for (i = 0; i < theme_neffects(); i++) {
    doc_theme_set_effects(d, i);
    x = saved_part(d, "word/theme/theme1.xml");
    if (!x || !strstr(x, theme_effect_name(i)) || !strstr(x, "<a:effectStyleLst>")) ok2 = 0;
    free(x);
  }
  ok("theme effects: every one written", ok2);
  doc_theme_office(d, 0);
  ok("office themes: Office (Aptos)", !strcmp(theme_of(d)->minor, "Aptos") && theme_of(d)->clr[THC_ACCENT1] == 0x156082);
  t = theme_thmx(d, &n);
  e = doc_new();
  ok("themes: a .thmx written and applied", t && doc_theme_load(e, t, n) && !strcmp(theme_of(e)->minor, "Aptos") && theme_of(e)->clr[THC_ACCENT1] == 0x156082);
  free(t);
  {
    size_t fn;
    u8 *f = file_read("tests/data/word.docx", &fn);
    ok("themes: from another document", f && doc_theme_load(e, f, fn) && theme_of(e)->clr[THC_ACCENT1] == 0x4F81BD);
    free(f);
  }
  x = saved_part(e, "word/theme/theme1.xml");
  ok("themes: a loaded theme written as it was", x && strstr(x, "ＭＳ ゴシック"));
  free(x);
  doc_free(e);
  e = docx_again(d, &lost);
  ok("office themes: read back", e && e->theme && !strcmp(e->theme->major, "Aptos Display") && !strcmp(e->theme->ename, "Office"));
  doc_free(e);
  doc_free(d);
}

/* styles: linked styles, priority, hidden, aliases, language, frame, shortcut keys; copying styles */
static void style_x(void)
{
  Doc *d = doc_new(), *e;
  Style s;
  int k, c;
  char *x;
  unsigned lost;
  memset(&s, 0, sizeof s);
  strcpy(s.name, "Note Text"); s.type = ST_PARA; s.based = 0; s.quick = 1;
  s.cmask = CM_ITALIC; s.ocf.flags = CF_ITALIC;
  s.x.priority = 21; s.x.flags = SXF_UNHIDE | SXF_SEMIHIDDEN; strcpy(s.x.lang, "de-DE"); strcpy(s.x.aliases, "NT,Hinweis");
  s.x.frame.set = 1; s.x.frame.w = 2880; s.x.frame.wrap = 1; s.x.frame.hanchor = 1; s.x.frame.vanchor = 0; s.x.frame.xalign = 3; s.x.frame.hspace = 144;
  s.x.key = 0x0600 | '7';
  k = doc_set_style(d, -1, &s);
  memset(&s, 0, sizeof s);
  strcpy(s.name, "Note Text Char"); s.type = ST_CHAR; s.based = NO_STYLE; s.cmask = CM_ITALIC; s.ocf.flags = CF_ITALIC;
  s.x.link = (u8)(k + 1);
  c = doc_set_style(d, -1, &s);
  if (k >= 0 && c >= 0) { s = d->styles[k]; s.x.link = (u8)(c + 1); doc_set_style(d, k, &s); }
  ok("styles: the settings kept by doc_set_style", k > 0 && c > 0 && d->styles[k].x.priority == 21 && d->styles[k].x.link == c + 1 && d->styles[c].x.link == k + 1);
  x = saved_part(d, "word/styles.xml");
  ok("styles: link, priority, hidden written", x && strstr(x, "<w:link w:val=\"NoteTextChar\"/>") && strstr(x, "<w:uiPriority w:val=\"20\"/><w:semiHidden/><w:unhideWhenUsed/>") &&
     strstr(x, "<w:aliases w:val=\"NT,Hinweis\"/>"));
  ok("styles: language and frame written", x && strstr(x, "<w:lang w:val=\"de-DE\"/>") && strstr(x, "<w:framePr w:w=\"2880\" w:wrap=\"around\" w:hSpace=\"144\" w:vAnchor=\"text\" w:hAnchor=\"margin\" w:xAlign=\"right\"/>"));
  free(x);
  x = saved_part(d, "word/customizations.xml");
  ok("styles: shortcut key written", x && strstr(x, "wne:kcmPrimary=\"0637\"") && strstr(x, "wne:fciIndexBasedOn=\"0065\""));
  free(x);
  e = docx_again(d, &lost);
  k = e ? doc_style_by_name(e, "Note Text") : -1;
  c = e ? doc_style_by_name(e, "Note Text Char") : -1;
  ok("styles: read back", k > 0 && c > 0 && e->styles[k].x.priority == 21 && e->styles[k].x.link == c + 1 && e->styles[c].x.link == k + 1 &&
     (e->styles[k].x.flags & (SXF_UNHIDE | SXF_SEMIHIDDEN)) == (SXF_UNHIDE | SXF_SEMIHIDDEN) && !strcmp(e->styles[k].x.lang, "de-DE") &&
     !strcmp(e->styles[k].x.aliases, "NT,Hinweis"));
  ok("styles: frame read back", k > 0 && e->styles[k].x.frame.set && e->styles[k].x.frame.w == 2880 && e->styles[k].x.frame.xalign == 3 && e->styles[k].x.frame.hanchor == 1 && e->styles[k].x.frame.hspace == 144);
  ok("styles: shortcut read back", k > 0 && e->styles[k].x.key == (0x0600 | '7'));
  {
    char nm[24];
    stylekey_name(0x0600 | '7', nm, sizeof nm);
    ok("styles: key named", !strcmp(nm, "Ctrl+Alt+7"));
  }
  /* copying styles from one document to another (Organizer) */
  {
    Doc *f = doc_new();
    const char *names[1] = { "Note Text" };
    int n = doc_copy_styles(f, e, names, 1), j = doc_style_by_name(f, "Note Text");
    ok("styles: copied with Organizer", n == 1 && j > 0 && (f->styles[j].cf.flags & CF_ITALIC) && f->styles[j].x.priority == 21 && f->styles[j].based == 0);
    n = doc_copy_styles(f, e, NULL, 0);
    ok("styles: all copied, links by name", n >= 2 && doc_style_by_name(f, "Note Text Char") > 0 && f->styles[doc_style_by_name(f, "Note Text")].x.link == doc_style_by_name(f, "Note Text Char") + 1);
    doc_free(f);
  }
  /* Styles pane settings kept */
  if (e) {
    Doc *g;
    e->spfilter = 0x3F01; e->spsort = SPS_PRIORITY;
    g = docx_again(e, &lost);
    ok("styles pane settings read back", g && g->spfilter == 0x3F01 && g->spsort == SPS_PRIORITY);
    dump13(g, "styles-x.docx");
    doc_free(g);
  }
  doc_free(e);
  doc_free(d);
}

void test_wp13(void)
{
  colours();
  read_word();
  refs();
  effects();
  style_x();
}
