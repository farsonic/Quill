/*
 * doctheme.h - (WP13) a document's Office theme: the twelve colours of its colour scheme, its heading
 * and body fonts and its effects (word/theme/theme1.xml), and the references to them that text,
 * paragraphs, cells and shapes keep (w:themeColor, w:asciiTheme, a:schemeClr), so a new colour
 * scheme or font scheme changes everything that uses the theme, as in Word. Office's own colour
 * schemes, font schemes, effects and themes are built in (Design > Theme Colors, Theme Fonts,
 * Effects, Office Themes).
 *
 * A reference is a number (THR): which of the theme's colours, and how it is lightened or darkened.
 * The colour itself is kept beside it (CharFmt.color and so on), worked out when it was set; a
 * reference counts only while the colour is still (nearly) what the reference comes to, so code that
 * sets a colour without knowing about themes simply leaves a stale reference that is ignored.
 */
#ifndef QUILL_DOCTHEME_H
#define QUILL_DOCTHEME_H

#include "doc.h"

/* the theme's colours: the scheme's twelve, then the four names text uses for the first four */
enum { THC_DK1, THC_LT1, THC_DK2, THC_LT2, THC_ACCENT1, THC_ACCENT2, THC_ACCENT3, THC_ACCENT4, THC_ACCENT5, THC_ACCENT6,
       THC_HLINK, THC_FOLHLINK, THC_BG1, THC_TX1, THC_BG2, THC_TX2, THC_N };
/* how a reference changes its colour: Word's w:themeTint / w:themeShade (a, 0-255), DrawingML's
   lumMod and lumOff (a, b: per cent), DrawingML's tint and shade (a: per cent) */
enum { THM_NONE, THM_TINT, THM_SHADE, THM_LUM, THM_DTINT, THM_DSHADE };
#define THR(c, m, a, b) ((u32)((c) + 1) | ((u32)(m) << 5) | ((u32)(a) << 8) | ((u32)(b) << 16))
#define THR_COLOR(r) ((int)((r) & 31) - 1)
#define THR_MOD(r)   ((int)(((r) >> 5) & 7))
#define THR_A(r)     ((int)(((r) >> 8) & 255))
#define THR_B(r)     ((int)(((r) >> 16) & 255))
/* a theme font: the heading (major) or body (minor) font */
enum { THF_MAJOR = 1, THF_MINOR = 2 };
/* the Styles pane's order kept in the file (w:stylePaneSortMethod) */
enum { SPS_NONE, SPS_NAME, SPS_PRIORITY, SPS_FONT, SPS_BASEDON, SPS_TYPE, SPS_DEFAULT, SPS_N };

typedef struct DocTheme {
  char name[64];                    /* the theme's name ("Office Theme") */
  char cname[64]; u32 clr[12];      /* the colour scheme: dk1 lt1 dk2 lt2 accent1-6 hlink folHlink */
  char fname[64];                   /* the font scheme */
  char major[48], minor[48];        /* its Latin heading and body fonts */
  char ename[64]; int effects;      /* the effects (fmtScheme): its name; one of Quill's (EF_), -1 the file's own */
  char *xml; size_t len;            /* theme1.xml as it was read (NULL: none; the theme is made afresh) */
  u8 newcolors, newfonts, neweffects, newname;   /* changed since it was read: written afresh */
  char *keyraw; size_t keylen;      /* customizations.xml's other key assignments and commands, kept (Quill writes its styles' keys) */
} DocTheme;

const DocTheme *theme_of(const Doc *d);          /* the document's, or Office's when it has none */
DocTheme *theme_get(Doc *d);                     /* the document's own, Office's made for it if none; NULL if out of memory */
u32  theme_rgb(const DocTheme *t, u32 ref);      /* what a reference comes to (0xRRGGBB) */
/* whether a reference still names this colour (within a few steps: Word rounds its own way); 0 for no reference */
int  theme_ref_ok(const Doc *d, u32 ref, u32 rgb);
/* whether a font reference still names this font (Doc.fonts index) */
int  theme_font_ok(const Doc *d, u8 which, int font);
const char *theme_font_name(const DocTheme *t, u8 which);   /* NULL for a bad which */
/* colour names: Word's (w:themeColor, "accent1", "text1"...), DrawingML's (a:schemeClr, "tx1"...), for people */
const char *thc_word_name(int c);
const char *thc_dml_name(int c);
const char *thc_label(int c);                    /* "Accent 1", "Text 1"... (English, for tr()) */
int  thc_from_word(const char *s);               /* -1 if unknown */
int  thc_from_dml(const char *s);
/* a reference from Word's attributes (themeColor, themeTint, themeShade: NULL if absent); 0 if none */
u32  thr_from_word(const char *color, const char *tint, const char *shade);
/* Word's attributes for a reference: ` w:themeColor="accent1" w:themeShade="BF"` (pfx "theme"), or ` w:themeFill="accent1"
   w:themeFillShade="BF"` (pfx "themeFill") */
void thr_word_attrs(Buf *b, u32 ref, const char *pfx);
/* a DrawingML colour element for a reference */
void thr_dml(Buf *b, u32 ref);
/* RGB and HSL (hue 0-3599, saturation and lightness 0-1000), in integers */
void rgb_hsl(u32 rgb, int *h, int *s, int *l);
u32  hsl_rgb(int h, int s, int l);

/* theme1.xml read into t (its text kept; t's own buffer); 0 if out of memory. Absent names stay as they were */
int  theme_parse(DocTheme *t, const char *xml, size_t n);
/* theme1.xml for t: the one read, with what was changed written afresh, or a new one. Malloc'd, NULL if out of memory */
char *theme_xml(const DocTheme *t, size_t *len);
void theme_init_office(DocTheme *t);             /* Office's theme (2013-2022: Calibri, blue accents) */

/* Office's colour schemes, font schemes, effects and themes */
int  theme_ncolorsets(void);
const char *theme_colorset_name(int i);
const u32 *theme_colorset(int i);                /* its twelve colours */
int  theme_nfontsets(void);
const char *theme_fontset_name(int i);
void theme_fontset(int i, const char **major, const char **minor);
int  theme_neffects(void);
const char *theme_effect_name(int i);
int  theme_nthemes(void);
const char *theme_office_name(int i);
void theme_office(int i, int *colors, int *fonts, int *effects);
/* which preset the document's theme matches (-1 none) */
int  theme_colorset_of(const Doc *d);
int  theme_fontset_of(const Doc *d);

/* the document's theme changed, and everything that uses it with it (styles, defaults, text, paragraph
   shading and borders, cells, shapes): one undo step (the theme itself is not in the history). 0 if
   out of memory */
int  doc_theme_set_colors(Doc *d, const u32 clr[12], const char *name);
int  doc_theme_set_fonts(Doc *d, const char *major, const char *minor, const char *name);
int  doc_theme_set_effects(Doc *d, int effects);
int  doc_theme_office(Doc *d, int theme);        /* an Office theme: its colours, fonts and effects */
/* the theme from another file (a .thmx theme, a .docx/.dotx or a theme1.xml), applied; 0 if it has none */
int  doc_theme_load(Doc *d, const u8 *data, size_t len);
/* the styles' shortcut keys (StyleX.key) from word/customizations.xml (Word's wne:keymaps), the rest of it kept;
   and written back (malloc'd, NULL when there is nothing to write) */
void  stylekeys_read(Doc *d, const char *xml, size_t n);
char *stylekeys_xml(Doc *d, size_t *len);
/* a key as Word writes it (modifiers 1 Shift, 2 Ctrl, 4 Alt << 8 | Windows key code) for people: "Ctrl+Alt+1" */
void  stylekey_name(u16 key, char *out, size_t cap);
/* the document's theme as an Office theme file (.thmx); malloc'd, NULL if out of memory */
u8  *theme_thmx(const Doc *d, size_t *len);

#endif
