/*
 * odtw.c - writing OpenDocument Text (docs/wordcraft-gap.md WP28b; reading is core/odt.c). This file
 * belongs to WP28b: only that package edits it.
 *
 * Quill's document as an .odt package (mimetype stored first, META-INF/manifest.xml, content.xml,
 * styles.xml, meta.xml, Pictures/, a formula object per equation) or as Flat ODT (one XML file,
 * pictures as base64, equations inline), ODF 1.3 as LibreOffice reads it:
 *  - styles.xml: the fonts, the default style (Quill's document defaults), every paragraph and
 *    character style with what it sets itself and the style it is based on (Normal is Standard),
 *    the lists' styles, the note and line numbering configuration, a page layout and master page
 *    for the document (its page setup, headers and footers: first page, even pages) and for each
 *    section with a setup or headers and footers of its own;
 *  - content.xml: paragraphs and headings with automatic styles for their own formatting, text spans
 *    for their characters' (on top of a character style), lists (nested by level, continued across
 *    interruptions), tables (column widths, merged cells as spans and covered cells, borders,
 *    shading, padding, row heights, heading rows, a table in a cell), pictures (in line or floating,
 *    with wrapping, position, distances, cropping, mirroring, border, alt text), text boxes, shapes
 *    and groups, equations (MathML formula objects), footnotes and endnotes, comments over their
 *    ranges, bookmarks, links, page number, page count and page reference fields, tracked
 *    insertions and deletions, sections (text:section with columns; a new page by its master page);
 *  - meta.xml: title, author and the other properties.
 * Left out: charts and SmartArt (a dashed box in their place), section starts on even or odd pages
 * (a new page), formatting revisions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "odtx.h"
#include "fmtio.h"
#include "image.h"
#include "notes.h"
#include "math.h"
#include "mathml.h"
#include "info.h"
#include "zip.h"
#include "xml.h"
#include "tstyle.h"

/* paragraph attributes (as odt.c's) */
enum { A_ALIGN = 1, A_LEFT = 2, A_RIGHT = 4, A_FIRST = 8, A_BEFORE = 16, A_AFTER = 32, A_LINE = 64, A_TABS = 128, A_KEEPNEXT = 256,
       A_KEEPLINES = 512, A_WIDOW = 1024, A_PAGEBRK = 2048, A_COLBRK = 4096, A_BORDER = 8192, A_SHADE = 16384, A_CONTEXT = 32768,
       A_DROP = 65536, A_ALL = 131071 };

/* automatic styles of one kind in one part: their properties (the key) and names */
typedef struct { char *key; } AutoEnt;
typedef struct { AutoEnt *e; int n, cap; const char *prefix; const char *family; } AutoSet;

typedef struct { const u8 *data; u32 len; char path[48]; } Pic;
typedef struct { char *xml; } MObj;

typedef struct {
  Doc *d;
  int flat;
  int part;                 /* 1 content.xml, 0 styles.xml: where automatic styles go */
  AutoSet ap[2], at[2], ag[2], ac[2], ar[2], acol[2], atab[2], asec[2];
  Buf *o;                   /* where the text is being written */
  Pic *pics; int npics;
  MObj *objs; int nobjs;
  Buf chg; int nchg;        /* text:tracked-changes' regions */
  int nframes, ntables, nsections, nlists;
  int nfoot, nend;          /* notes' numbers so far */
  int *listlast;            /* each Quill list's last text:list block (its xml:id number), 0 none */
  u16 bmark;                /* the bookmark open in the text written so far */
  int sp;                   /* a space (or the paragraph's start) just written */
  int ins;                  /* an insertion open: its region's number (0 none) */
  int insrid;               /* and its revision + 1 */
  int del_rid;              /* (writing a deletion's text) */
  char pmaster[24];         /* a master page the next paragraph (or table) starts */
  int pgnum;                /* a page number the next paragraph starts at (0 none) */
  int notelead;             /* the next paragraph is a note's first: a space opens it */
  int oom;
} W;

static void w_blocks(W *w, Para **p, u32 a, u32 b, int depth);
static int w_pgnum;         /* (a page number w_pprops writes, 0 none) */
static int w_mcentre;       /* (w_pprops marks the alignment as display equations' centring) */
static void w_paras_story(W *w, const Story *s);

/* ---- automatic styles */
static const char *auto_name(W *w, AutoSet *s, const char *key, char *name, size_t cap)
{
  int i;
  for (i = 0; i < s->n; i++) if (!strcmp(s->e[i].key, key)) break;
  if (i == s->n) {
    if (s->n >= s->cap) {
      int nc = s->cap ? s->cap * 2 : 32;
      AutoEnt *ne = (AutoEnt *)realloc(s->e, (size_t)nc * sizeof(AutoEnt));
      if (!ne) { w->oom = 1; snprintf(name, cap, "%s1", s->prefix); return name; }
      s->e = ne; s->cap = nc;
    }
    if (!(s->e[s->n].key = str_dup(key))) { w->oom = 1; snprintf(name, cap, "%s1", s->prefix); return name; }
    s->n++;
  }
  snprintf(name, cap, "%s%d", s->prefix, i + 1);
  return name;
}
/* an automatic style's definition: <style:style style:name=... style:family=... (key)> where the key
   is the rest of its start tag and its content */
static void auto_write(Buf *b, const AutoSet *s)
{
  int i;
  for (i = 0; i < s->n; i++) {
    const char *k = s->e[i].key;
    buf_printf(b, "<style:style style:name=\"%s%d\" style:family=\"%s\"", s->prefix, i + 1, s->family);
    buf_adds(b, k);
    buf_adds(b, "</style:style>");
  }
}
static void auto_free(AutoSet *s)
{
  int i;
  for (i = 0; i < s->n; i++) free(s->e[i].key);
  free(s->e);
  s->e = NULL; s->n = s->cap = 0;
}

/* ---- properties */
static void attr_len(Buf *b, const char *name, i32 tw) { buf_printf(b, " %s=\"", name); odt_put_len(b, tw); buf_addc(b, '"'); }
static void attr_esc(Buf *b, const char *name, const char *v) { buf_printf(b, " %s=\"", name); xml_escape(b, v, strlen(v)); buf_addc(b, '"'); }
static void put_rgb(Buf *b, u32 c) { buf_printf(b, "#%06lx", (unsigned long)(c & 0xFFFFFF)); }
static u32 pa_of(unsigned pm)
{
  u32 a = 0;
  if (pm & PM_ALIGN) a |= A_ALIGN;
  if (pm & PM_INDENT) a |= A_LEFT | A_RIGHT | A_FIRST;
  if (pm & PM_SPACING) a |= A_BEFORE | A_AFTER;
  if (pm & PM_LINE) a |= A_LINE;
  if (pm & PM_TABS) a |= A_TABS;
  if (pm & PM_KEEP) a |= A_KEEPNEXT;
  if (pm & PM_BREAK) a |= A_PAGEBRK | A_COLBRK;
  if (pm & PM_BORDER) a |= A_BORDER;
  if (pm & PM_SHADE) a |= A_SHADE;
  if (pm & PM_PAGINATION) a |= A_KEEPLINES | A_WIDOW;
  if (pm & PM_CONTEXT) a |= A_CONTEXT;
  return a;
}
/* the attributes where p differs from base */
static u32 pa_diff(const ParaFmt *p, const ParaFmt *b)
{
  u32 a = 0;
  int k;
  if (p->align != b->align) a |= A_ALIGN;
  if (p->left != b->left) a |= A_LEFT;
  if (p->right != b->right) a |= A_RIGHT;
  if (p->first != b->first) a |= A_FIRST;
  if (p->before != b->before) a |= A_BEFORE;
  if (p->after != b->after) a |= A_AFTER;
  if (p->line != b->line || p->linerule != b->linerule) a |= A_LINE;
  if (memcmp(p->tabs, b->tabs, sizeof p->tabs) || memcmp(p->tabkind, b->tabkind, sizeof p->tabkind) || memcmp(p->tablead, b->tablead, sizeof p->tablead)) a |= A_TABS;
  if (p->keepnext != b->keepnext) a |= A_KEEPNEXT;
  if (p->keeplines != b->keeplines) a |= A_KEEPLINES;
  if (p->nowidow != b->nowidow) a |= A_WIDOW;
  if (p->pagebreak != b->pagebreak || p->colbreak != b->colbreak) a |= A_PAGEBRK | A_COLBRK;
  for (k = 0; k < 4; k++) if (p->bstyle[k] != b->bstyle[k] || (p->bstyle[k] != PB_NONE && (p->bwidth[k] != b->bwidth[k] || p->bcolor[k] != b->bcolor[k] || p->bspace[k] != b->bspace[k]))) a |= A_BORDER;
  if (p->shade != b->shade) a |= A_SHADE;
  if (p->contextual != b->contextual) a |= A_CONTEXT;
  if (p->dropcap != b->dropcap) a |= A_DROP;
  return a;
}
static void put_pct(Buf *b, long v100)
{
  /* hundredths of a percent */
  if (v100 % 100) buf_printf(b, "%ld.%02ld%%", v100 / 100, v100 % 100);
  else buf_printf(b, "%ld%%", v100 / 100);
}
/* style:paragraph-properties for the attributes a names (nothing if none) */
static void w_pprops(Buf *b, const ParaFmt *p, u32 a)
{
  int k, ntabs = 0;
  static const char *const edge[4] = { "top", "left", "bottom", "right" };
  if (!a) return;
  buf_adds(b, "<style:paragraph-properties");
  if (a & A_ALIGN) {
    buf_printf(b, " fo:text-align=\"%s\"", p->align == AL_CENTER ? "center" : p->align == AL_RIGHT ? "end" : p->align >= AL_JUSTIFY ? "justify" : "start");
    if (p->align == AL_DISTRIBUTE) buf_adds(b, " fo:text-align-last=\"justify\"");
    if (p->align == AL_JUSTIFY) buf_adds(b, " style:justify-single-word=\"false\"");
  }
  if (a & A_LEFT) attr_len(b, "fo:margin-left", p->left);
  if (a & A_RIGHT) attr_len(b, "fo:margin-right", p->right);
  if (a & A_FIRST) { attr_len(b, "fo:text-indent", p->first); buf_adds(b, " style:auto-text-indent=\"false\""); }
  if (a & A_BEFORE) attr_len(b, "fo:margin-top", p->before);
  if (a & A_AFTER) attr_len(b, "fo:margin-bottom", p->after);
  if (a & A_LINE) {
    if (p->linerule == LS_EXACT) attr_len(b, "fo:line-height", p->line);
    else if (p->linerule == LS_ATLEAST) attr_len(b, "style:line-height-at-least", p->line);
    else { buf_adds(b, " fo:line-height=\""); put_pct(b, ((long)p->line * 10000 + 120) / 240); buf_addc(b, '"'); }
  }
  if (a & A_KEEPNEXT) buf_printf(b, " fo:keep-with-next=\"%s\"", p->keepnext ? "always" : "auto");
  if (a & A_KEEPLINES) buf_printf(b, " fo:keep-together=\"%s\"", p->keeplines ? "always" : "auto");
  if (a & A_WIDOW) buf_printf(b, " fo:widows=\"%d\" fo:orphans=\"%d\"", p->nowidow ? 0 : 2, p->nowidow ? 0 : 2);
  if (a & (A_PAGEBRK | A_COLBRK)) buf_printf(b, " fo:break-before=\"%s\"", p->pagebreak ? "page" : p->colbreak ? "column" : "auto");
  if (a & A_BORDER) {
    for (k = 0; k < 4; k++) {
      buf_printf(b, " fo:border-%s=\"", edge[k]);
      odt_put_border(b, p->bstyle[k], p->bwidth[k], p->bcolor[k]);
      buf_addc(b, '"');
      if (p->bstyle[k] != PB_NONE) { buf_printf(b, " fo:padding-%s=\"", edge[k]); odt_put_len(b, (i32)p->bspace[k] * 20); buf_addc(b, '"'); }
    }
  }
  if (a & A_SHADE) { buf_adds(b, " fo:background-color=\""); if (p->shade & HL_ON) put_rgb(b, p->shade); else buf_adds(b, "transparent"); buf_addc(b, '"'); }
  if (a & A_CONTEXT) buf_printf(b, " style:contextual-spacing=\"%s\"", p->contextual ? "true" : "false");
  if (w_pgnum) buf_printf(b, " style:page-number=\"%d\"", w_pgnum);
  if (w_mcentre) buf_adds(b, " quill:math-centred=\"true\"");
  if (a & A_TABS) for (k = 0; k < 8 && p->tabs[k]; k++) ntabs++;
  if (((a & A_TABS)) || ((a & A_DROP) && p->dropcap)) {
    buf_addc(b, '>');
    if (a & A_TABS) {
      buf_adds(b, "<style:tab-stops>");
      for (k = 0; k < ntabs; k++) {
        buf_adds(b, "<style:tab-stop");
        attr_len(b, "style:position", p->tabs[k]);
        if (p->tabkind[k] == TAB_CENTER) buf_adds(b, " style:type=\"center\"");
        else if (p->tabkind[k] == TAB_RIGHT) buf_adds(b, " style:type=\"right\"");
        else if (p->tabkind[k] == TAB_DECIMAL) buf_adds(b, " style:type=\"char\" style:char=\".\"");
        if (p->tablead[k] == LEAD_DOT) buf_adds(b, " style:leader-style=\"dotted\" style:leader-text=\".\"");
        else if (p->tablead[k] == LEAD_HYPHEN) buf_adds(b, " style:leader-style=\"dash\" style:leader-text=\"-\"");
        else if (p->tablead[k] == LEAD_LINE) buf_adds(b, " style:leader-style=\"solid\" style:leader-text=\"_\"");
        buf_adds(b, "/>");
      }
      buf_adds(b, "</style:tab-stops>");
    }
    if ((a & A_DROP) && p->dropcap) buf_printf(b, "<style:drop-cap style:lines=\"%d\" style:length=\"1\" style:distance=\"0in\"/>", p->dropcap);
    buf_adds(b, "</style:paragraph-properties>");
  } else buf_adds(b, "/>");
}
/* the character fields where f differs from base (CM_ flags Quill writes) */
static unsigned cm_diff(const CharFmt *f, const CharFmt *b)
{
  unsigned m = 0;
  if ((f->flags ^ b->flags) & CF_BOLD) m |= CM_BOLD;
  if ((f->flags ^ b->flags) & CF_ITALIC) m |= CM_ITALIC;
  if (((f->flags ^ b->flags) & CF_UNDERLINE) || ((f->flags & CF_UNDERLINE) && f->ul != b->ul)) m |= CM_UNDERLINE | CM_UL;
  if (((f->flags ^ b->flags) & CF_STRIKE) || ((f->fx ^ b->fx) & FX_DSTRIKE)) m |= CM_STRIKE | CM_DSTRIKE;
  if (((f->flags ^ b->flags) & (CF_SUPER | CF_SUB)) || f->position != b->position) m |= CM_SUPER | CM_SUB | CM_POSITION;
  if ((f->flags ^ b->flags) & CF_CAPS) m |= CM_CAPS;
  if ((f->fx ^ b->fx) & FX_SMALLCAPS) m |= CM_SMALLCAPS;
  if ((f->fx ^ b->fx) & FX_BORDER) m |= CM_CBORDER;
  if (f->font != b->font) m |= CM_FONT;
  if (f->size != b->size) m |= CM_SIZE;
  if (f->color != b->color) m |= CM_COLOR;
  if (f->hl != b->hl || f->shade != b->shade) m |= CM_HL;
  if (f->spacing != b->spacing) m |= CM_SPACING;
  if (f->scale != b->scale) m |= CM_SCALE;
  if ((f->fx2 ^ b->fx2) & (FX2_HIDDEN | FX2_RELIEF)) m |= CM_FX2;
  if (f->ulcolor != b->ulcolor && (f->flags & CF_UNDERLINE)) m |= CM_ULCOLOR;
  return m;
}
static void w_tprops(Buf *b, Doc *d, const CharFmt *f, unsigned m)
{
  if (!m) return;
  buf_adds(b, "<style:text-properties");
  if (m & CM_FONT) {
    const char *fam = f->font < d->nfonts ? d->fonts[f->font] : "Times New Roman";
    attr_esc(b, "style:font-name", fam); attr_esc(b, "style:font-name-complex", fam);
  }
  if (m & CM_SIZE) {
    buf_printf(b, " fo:font-size=\"%d%spt\"", f->size / 2, f->size & 1 ? ".5" : "");
    buf_printf(b, " style:font-size-complex=\"%d%spt\"", f->size / 2, f->size & 1 ? ".5" : "");
  }
  if (m & CM_BOLD) buf_printf(b, " fo:font-weight=\"%s\" style:font-weight-complex=\"%s\"", f->flags & CF_BOLD ? "bold" : "normal", f->flags & CF_BOLD ? "bold" : "normal");
  if (m & CM_ITALIC) buf_printf(b, " fo:font-style=\"%s\" style:font-style-complex=\"%s\"", f->flags & CF_ITALIC ? "italic" : "normal", f->flags & CF_ITALIC ? "italic" : "normal");
  if (m & (CM_UNDERLINE | CM_UL)) {
    if (!(f->flags & CF_UNDERLINE)) buf_adds(b, " style:text-underline-style=\"none\"");
    else {
      buf_printf(b, " style:text-underline-style=\"%s\"", f->ul == UL_DOTTED ? "dotted" : f->ul == UL_DASHED ? "dash" : f->ul == UL_WAVE ? "wave" : "solid");
      buf_printf(b, " style:text-underline-width=\"%s\"", f->ul == UL_THICK ? "bold" : "auto");
      if (f->ul == UL_DOUBLE) buf_adds(b, " style:text-underline-type=\"double\"");
      if (f->ul == UL_WORDS) buf_adds(b, " style:text-underline-mode=\"skip-white-space\"");
      if (f->ulcolor & HL_ON) { buf_adds(b, " style:text-underline-color=\""); put_rgb(b, f->ulcolor); buf_addc(b, '"'); }
      else buf_adds(b, " style:text-underline-color=\"font-color\"");
    }
  } else if ((m & CM_ULCOLOR) && (f->flags & CF_UNDERLINE)) {
    buf_adds(b, " style:text-underline-color=\"");
    if (f->ulcolor & HL_ON) put_rgb(b, f->ulcolor); else buf_adds(b, "font-color");
    buf_addc(b, '"');
  }
  if (m & (CM_STRIKE | CM_DSTRIKE)) {
    if (f->fx & FX_DSTRIKE) buf_adds(b, " style:text-line-through-style=\"solid\" style:text-line-through-type=\"double\"");
    else if (f->flags & CF_STRIKE) buf_adds(b, " style:text-line-through-style=\"solid\" style:text-line-through-type=\"single\"");
    else buf_adds(b, " style:text-line-through-style=\"none\"");
  }
  if (m & (CM_SUPER | CM_SUB | CM_POSITION)) {
    if (f->flags & CF_SUPER) buf_adds(b, " style:text-position=\"super 58%\"");
    else if (f->flags & CF_SUB) buf_adds(b, " style:text-position=\"sub 58%\"");
    else buf_printf(b, " style:text-position=\"%d%% 100%%\"", f->size ? f->position * 100 / f->size : 0);
  }
  if (m & CM_CAPS) buf_printf(b, " fo:text-transform=\"%s\"", f->flags & CF_CAPS ? "uppercase" : "none");
  if (m & CM_SMALLCAPS) buf_printf(b, " fo:font-variant=\"%s\"", f->fx & FX_SMALLCAPS ? "small-caps" : "normal");
  if (m & CM_COLOR) {
    if (f->color == COLOR_AUTO) buf_adds(b, " style:use-window-font-color=\"true\"");
    else { buf_adds(b, " fo:color=\""); put_rgb(b, f->color); buf_addc(b, '"'); }
  }
  if (m & (CM_HL | CM_SHADE)) {
    u32 c = f->hl & HL_ON ? f->hl : f->shade;
    buf_adds(b, " fo:background-color=\"");
    if (c & HL_ON) put_rgb(b, c); else buf_adds(b, "transparent");
    buf_addc(b, '"');
  }
  if (m & CM_SPACING) { if (f->spacing) attr_len(b, "fo:letter-spacing", f->spacing); else buf_adds(b, " fo:letter-spacing=\"normal\""); }
  if (m & CM_SCALE) buf_printf(b, " style:text-scale=\"%d%%\"", f->scale ? f->scale : 100);
  if (m & CM_FX2) {
    buf_printf(b, " fo:text-shadow=\"%s\"", f->fx2 & FX2_SHADOW ? "1pt 1pt" : "none");
    buf_printf(b, " style:text-outline=\"%s\"", f->fx2 & FX2_OUTLINE ? "true" : "false");
    buf_printf(b, " style:font-relief=\"%s\"", f->fx2 & FX2_EMBOSS ? "embossed" : f->fx2 & FX2_ENGRAVE ? "engraved" : "none");
    if (f->fx2 & FX2_HIDDEN) buf_adds(b, " text:display=\"none\"");
  }
  if (m & CM_CBORDER) buf_printf(b, " fo:border=\"%s\"", f->fx & FX_BORDER ? "0.5pt solid #000000" : "none");
  buf_adds(b, "/>");
}
static unsigned cm_of_style(unsigned cmask)
{
  unsigned m = cmask & CM_LOOK;
  if (m & CM_SHADE) m |= CM_HL;
  return m;
}

/* ---- names */
static void style_ref(Doc *d, int k, char *out, size_t cap)
{
  char id[48];
  size_t i, o = 0;
  if (k <= 0 || k >= d->nstyles) { snprintf(out, cap, "Standard"); return; }
  /* its id (its name without spaces if it has none) */
  for (i = 0; d->styles[k].name[i] && o + 1 < sizeof id; i++) if (d->styles[k].name[i] != ' ') id[o++] = d->styles[k].name[i];
  id[o] = 0;
  odt_escape(d->styles[k].id[0] ? d->styles[k].id : id, out, cap);
}
static const char *font_attr_quote(const char *fam) { return strchr(fam, ' ') ? "'" : ""; }

/* ---- text */
static void w_esc_text(W *w, const char *s, size_t n)
{
  /* characters: XML escaped; control characters other than those Quill means are dropped */
  size_t i = 0, a = 0;
  Buf *o = w->o;
  for (i = 0; i < n; i++) {
    u8 c = (u8)s[i];
    if (c < 0x20 || c == '&' || c == '<' || c == '>') {
      if (i > a) buf_add(o, s + a, i - a);
      if (c == '&') buf_adds(o, "&amp;"); else if (c == '<') buf_adds(o, "&lt;"); else if (c == '>') buf_adds(o, "&gt;");
      a = i + 1;
    }
  }
  if (i > a) buf_add(o, s + a, i - a);
}
static void close_bmark(W *w)
{
  if (!w->bmark) return;
  buf_adds(w->o, "<text:bookmark-end text:name=\"");
  xml_escape(w->o, doc_bmark_name(w->d, w->bmark), strlen(doc_bmark_name(w->d, w->bmark)));
  buf_adds(w->o, "\"/>");
  w->bmark = 0;
}
static void bmark_to(W *w, u16 k)
{
  if (k == w->bmark) return;
  close_bmark(w);
  if (k && doc_bmark_name(w->d, k)[0]) {
    buf_adds(w->o, "<text:bookmark-start text:name=\"");
    xml_escape(w->o, doc_bmark_name(w->d, k), strlen(doc_bmark_name(w->d, k)));
    buf_adds(w->o, "\"/>");
    w->bmark = k;
  }
}

/* ---- objects */
static int pic_path(W *w, const Image *im, char *out, size_t cap)
{
  int i;
  Pic *np;
  for (i = 0; i < w->npics; i++) if (w->pics[i].len == im->len && (w->pics[i].data == im->data || !memcmp(w->pics[i].data, im->data, im->len))) { snprintf(out, cap, "%s", w->pics[i].path); return 1; }
  if (!(np = (Pic *)realloc(w->pics, (size_t)(w->npics + 1) * sizeof(Pic)))) { w->oom = 1; return 0; }
  w->pics = np;
  np = &w->pics[w->npics];
  np->data = im->data; np->len = im->len;
  snprintf(np->path, sizeof np->path, "Pictures/%d.%s", w->npics + 1, im->type == IMG_JPEG ? "jpg" : "png");
  w->npics++;
  snprintf(out, cap, "%s", np->path);
  return 1;
}
static const char *shape_type(int k)
{
  switch (k) {
  case SH_ROUNDRECT: return "round-rectangle";
  case SH_ELLIPSE: return "ellipse";
  case SH_TRIANGLE: return "isosceles-triangle";
  case SH_RTRIANGLE: return "right-triangle";
  case SH_DIAMOND: return "diamond";
  case SH_RARROW: return "right-arrow";
  case SH_LARROW: return "left-arrow";
  case SH_UARROW: return "up-arrow";
  case SH_DARROW: return "down-arrow";
  case SH_STAR: return "star5";
  case SH_STAR4: return "star4";
  case SH_PENTAGON: return "pentagon";
  case SH_HEXAGON: return "hexagon";
  case SH_OCTAGON: return "octagon";
  case SH_PARALLELOGRAM: return "parallelogram";
  case SH_TRAPEZOID: return "trapezoid";
  case SH_PLUS: return "cross";
  case SH_HEART: return "heart";
  case SH_CALLOUT: return "rectangular-callout";
  case SH_CHEVRON: return "chevron";
  case SH_HOMEPLATE: return "pentagon-right";
  default: return "rectangle";
  }
}
/* the graphic style of an object: its key (the rest of the style element) */
static void graphic_key(W *w, const Image *im, Buf *k, int kind)
{
  /* kind 0 a picture or text box frame, 1 a shape */
  buf_adds(k, " style:parent-style-name=\"");
  buf_adds(k, kind ? "Graphics" : im->kind == OBJ_TEXTBOX ? "Frame" : "Graphics");
  buf_adds(k, "\"><style:graphic-properties");
  if (im->wrap == WRAP_INLINE) buf_adds(k, " style:vertical-pos=\"top\" style:vertical-rel=\"baseline\"");
  else {
    static const char *const hp[] = { "from-left", "left", "center", "right" };
    static const char *const vp[] = { "from-top", "top", "middle", "bottom" };
    const char *wr = im->wrap == WRAP_TOPBOTTOM ? "none" : im->wrap == WRAP_BEHIND || im->wrap == WRAP_FRONT ? "run-through" : "parallel";
    buf_printf(k, " style:wrap=\"%s\"", wr);
    if (im->wrap == WRAP_BEHIND || im->wrap == WRAP_FRONT) buf_printf(k, " style:run-through=\"%s\"", im->wrap == WRAP_BEHIND ? "background" : "foreground");
    if (im->wrap == WRAP_TIGHT) buf_adds(k, " style:wrap-contour=\"true\" style:wrap-contour-mode=\"outside\"");
    else buf_adds(k, " style:wrap-contour=\"false\"");
    buf_printf(k, " style:horizontal-pos=\"%s\" style:horizontal-rel=\"%s\"", hp[im->halign < 4 ? im->halign : 0],
               im->hrel == POS_PAGE ? "page" : im->hrel == POS_MARGIN ? "page-content" : "paragraph");
    buf_printf(k, " style:vertical-pos=\"%s\" style:vertical-rel=\"%s\"", vp[im->valign < 4 ? im->valign : 0],
               im->vrel == POS_PAGE ? "page" : im->vrel == POS_MARGIN ? "page-content" : "paragraph");
  }
  attr_len(k, "fo:margin-left", im->dl); attr_len(k, "fo:margin-right", im->dr);
  attr_len(k, "fo:margin-top", im->dt); attr_len(k, "fo:margin-bottom", im->db);
  if (kind) {
    if (im->lstyle == PB_NONE) buf_adds(k, " draw:stroke=\"none\"");
    else {
      buf_printf(k, " draw:stroke=\"%s\" svg:stroke-color=\"", im->lstyle == PB_DASHED || im->lstyle == PB_DOTTED ? "dash" : "solid");
      put_rgb(k, im->lcolor);
      buf_addc(k, '"');
      attr_len(k, "svg:stroke-width", (i32)(im->lwidth ? im->lwidth : 4) * 5 / 2);
    }
    if (im->fill == COLOR_AUTO) buf_adds(k, " draw:fill=\"none\"");
    else { buf_adds(k, " draw:fill=\"solid\" draw:fill-color=\""); put_rgb(k, im->fill); buf_addc(k, '"'); }
  } else {
    buf_adds(k, " fo:border=\"");
    odt_put_border(k, im->lstyle, im->lwidth, im->lcolor);
    buf_addc(k, '"');
    if (im->fill == COLOR_AUTO) buf_adds(k, " fo:background-color=\"transparent\" draw:fill=\"none\"");
    else { buf_adds(k, " fo:background-color=\""); put_rgb(k, im->fill); buf_adds(k, "\" draw:fill=\"solid\" draw:fill-color=\""); put_rgb(k, im->fill); buf_addc(k, '"'); }
    if (im->kind == OBJ_TEXTBOX) buf_adds(k, " fo:padding=\"0.05in\"");
    else buf_adds(k, " fo:padding=\"0in\"");
    if (im->flip) buf_printf(k, " style:mirror=\"%s\"", im->flip == 3 ? "horizontal vertical" : im->flip == 1 ? "horizontal" : "vertical");
    if (im->kind == OBJ_PICTURE && im->pw && im->ph && (im->crop[0] || im->crop[1] || im->crop[2] || im->crop[3])) {
      i32 nw = (i32)im->pw * 15, nh = (i32)im->ph * 15;
      buf_adds(k, " fo:clip=\"rect(");
      odt_put_len(k, (i32)((long long)im->crop[1] * nh / 100000)); buf_adds(k, ", ");
      odt_put_len(k, (i32)((long long)im->crop[2] * nw / 100000)); buf_adds(k, ", ");
      odt_put_len(k, (i32)((long long)im->crop[3] * nh / 100000)); buf_adds(k, ", ");
      odt_put_len(k, (i32)((long long)im->crop[0] * nw / 100000)); buf_adds(k, ")\"");
    }
  }
  if (im->decorative) buf_adds(k, " loext:decorative=\"true\"");
  buf_adds(k, "/>");
}
/* where an object goes: its anchor and position (and its size) as attributes */
static void place_attrs(W *w, const Image *im, int child, long offx, long offy, long cx, long cy)
{
  Buf *o = w->o;
  if (!child) {
    buf_printf(o, " text:anchor-type=\"%s\"", im->wrap == WRAP_INLINE ? "as-char" : im->hrel == POS_PAGE && im->vrel == POS_PAGE ? "char" : "char");
  }
  if (im->rot && im->wrap != WRAP_INLINE) {
    /* turned: rotate round its centre (ODF turns counter-clockwise round the top left, then moves it) */
    long x = (child ? offx : (long)im->x), y = (child ? offy : (long)im->y);
    int ang = 3600 - im->rot;
    long c = icos(ang), s = isin(ang);
    /* R(-a) in ODF's y-down space: x' = x cos + y sin, y' = -x sin + y cos (a counter-clockwise) */
    long hx = cx / 2, hy = cy / 2;
    long rx = (hx * c + hy * s) / TRIG_ONE, ry = (-hx * s + hy * c) / TRIG_ONE;
    long tx = x + hx - rx, ty = y + hy - ry;
    long rad = (long)ang * 31416L / 1800L;       /* radians x 10000 */
    attr_len(o, "svg:width", (i32)cx); attr_len(o, "svg:height", (i32)cy);
    buf_printf(o, " draw:transform=\"rotate (%s%ld.%04ld) translate (", rad < 0 ? "-" : "", labs(rad) / 10000, labs(rad) % 10000);
    odt_put_len(o, (i32)tx); buf_addc(o, ' '); odt_put_len(o, (i32)ty); buf_adds(o, ")\"");
    return;
  }
  if (child || im->wrap != WRAP_INLINE) { attr_len(o, "svg:x", (i32)(child ? offx : im->x)); attr_len(o, "svg:y", (i32)(child ? offy : im->y)); }
  attr_len(o, "svg:width", (i32)cx); attr_len(o, "svg:height", (i32)cy);
}
static void w_alt(W *w, const Image *im)
{
  if (!im->alt[0]) return;
  buf_adds(w->o, "<svg:desc>");
  xml_escape(w->o, im->alt, strlen(im->alt));
  buf_adds(w->o, "</svg:desc>");
}
static void w_object(W *w, const Image *im, int child, long offx, long offy, long cx, long cy);
static void w_shape(W *w, const Image *im, int child, long offx, long offy, long cx, long cy)
{
  Buf k;
  char gs[24];
  int z = im->z < 0 ? 0 : im->z;
  buf_init(&k);
  graphic_key(w, im, &k, 1);
  auto_name(w, &w->ag[w->part], k.data ? k.data : "", gs, sizeof gs);
  buf_free(&k);
  w->nframes++;
  if (im->shape == SH_LINE) {
    long x1 = child ? offx : im->wrap == WRAP_INLINE ? 0 : im->x, y1 = child ? offy : im->wrap == WRAP_INLINE ? 0 : im->y, x2 = x1 + cx, y2 = y1 + cy;
    if (im->flip & 1) { long t = x1; x1 = x2; x2 = t; }
    if (im->flip & 2) { long t = y1; y1 = y2; y2 = t; }
    buf_printf(w->o, "<draw:line draw:style-name=\"%s\" draw:name=\"Shape%d\" draw:z-index=\"%d\"", gs, w->nframes, z);
    if (!child) buf_printf(w->o, " text:anchor-type=\"%s\"", im->wrap == WRAP_INLINE ? "as-char" : "char");
    attr_len(w->o, "svg:x1", (i32)x1); attr_len(w->o, "svg:y1", (i32)y1); attr_len(w->o, "svg:x2", (i32)x2); attr_len(w->o, "svg:y2", (i32)y2);
    buf_adds(w->o, ">");
    w_alt(w, im);
    buf_adds(w->o, "<text:p/></draw:line>");
    return;
  }
  buf_printf(w->o, "<draw:custom-shape draw:style-name=\"%s\" draw:name=\"Shape%d\" draw:z-index=\"%d\"", gs, w->nframes, z);
  place_attrs(w, im, child, offx, offy, cx, cy);
  buf_adds(w->o, ">");
  w_alt(w, im);
  if (im->text && im->text->np) w_paras_story(w, im->text);
  buf_printf(w->o, "<draw:enhanced-geometry svg:viewBox=\"0 0 21600 21600\" draw:type=\"%s\"%s%s/></draw:custom-shape>", shape_type(im->shape),
             im->flip & 1 ? " draw:mirror-horizontal=\"true\"" : "", im->flip & 2 ? " draw:mirror-vertical=\"true\"" : "");
}
/* an equation's size as LibreOffice would make it, guessed from its content (em: the text's size) */
static void math_guess(const MNode *n, int em, long *wd, long *ht)
{
  u16 i;
  long a, b, c, d2;
  *wd = 0; *ht = em;
  if (!n) return;
  switch (n->kind) {
  case MK_RUN: {
    const char *t = n->text ? n->text : "";
    size_t k = 0, len = strlen(t);
    long chars = 0;
    while (k < len) { k = utf8_next(t, len, k); chars++; }
    *wd = chars * em * 3 / 5;
    return;
  }
  case MK_FRAC:
    math_guess(n->nkid > 0 ? n->kid[0] : NULL, em * 4 / 5, &a, &b);
    math_guess(n->nkid > 1 ? n->kid[1] : NULL, em * 4 / 5, &c, &d2);
    *wd = (a > c ? a : c) + em / 4; *ht = b + d2 + em / 5;
    return;
  default:
    for (i = 0; i < n->nkid; i++) {
      int sub = n->kind == MK_SSUP || n->kind == MK_SSUB || n->kind == MK_SSUBSUP || n->kind == MK_NARY || n->kind == MK_LIMLOW || n->kind == MK_LIMUPP;
      math_guess(n->kid[i], sub && i > 0 ? em * 7 / 10 : em, &a, &b);
      if (n->kind == MK_MATRIX && n->cols) { if (i % n->cols == 0 && i) *ht += em * 6 / 5; }
      *wd += a;
      if (b > *ht) *ht = b;
    }
    if (n->kind == MK_NARY) { *wd += em; *ht += em * 3 / 5; }
    if (n->kind == MK_DELIM) *wd += em * (n->nkid + 1) / 2;
    if (n->kind == MK_RAD) { *wd += em * 3 / 4; *ht += em / 5; }
    if (n->kind == MK_SSUP || n->kind == MK_SSUB || n->kind == MK_SSUBSUP) *ht += em * 2 / 5;
    if (n->kind == MK_MATRIX && n->rows > 1) *ht += em * 6 / 5 * 0;
    return;
  }
}
static void w_math(W *w, int k, const CharFmt *f)
{
  MathEq *e = doc_math(w->d, k);
  long wd = 0, ht = 0, a, b;
  int em = (f && f->size ? f->size : 24) * 10;          /* twips */
  u16 i;
  char gs[24];
  Buf key;
  if (!e) return;
  for (i = 0; i < e->nline; i++) { math_guess(e->line[i], em, &a, &b); if (a > wd) wd = a; ht += b; }
  if (wd < em / 2) wd = em / 2;
  wd += em / 5; ht += em / 3;
  buf_init(&key);
  buf_adds(&key, " style:parent-style-name=\"Formula\"><style:graphic-properties style:vertical-pos=\"middle\" style:vertical-rel=\"text\" fo:margin-left=\"0in\" fo:margin-right=\"0in\" draw:ole-draw-aspect=\"1\"/>");
  auto_name(w, &w->ag[w->part], key.data ? key.data : "", gs, sizeof gs);
  buf_free(&key);
  w->nframes++;
  /* (in a package no size: LibreOffice sizes formula objects itself, and would scale the formula into a guessed
     one; inline in a Flat ODT it needs one: a guess) */
  buf_printf(w->o, "<draw:frame draw:style-name=\"%s\" draw:name=\"Formula%d\" text:anchor-type=\"as-char\" draw:z-index=\"0\"", gs, w->nframes);
  if (w->flat) { attr_len(w->o, "svg:width", (i32)wd); attr_len(w->o, "svg:height", (i32)ht); }
  buf_adds(w->o, ">");
  if (w->flat) {
    buf_adds(w->o, "<draw:object>");
    mathml_write(w->o, e);
    buf_adds(w->o, "</draw:object>");
  } else {
    MObj *no = (MObj *)realloc(w->objs, (size_t)(w->nobjs + 1) * sizeof(MObj));
    Buf m;
    if (!no) { w->oom = 1; return; }
    w->objs = no;
    buf_init(&m);
    buf_adds(&m, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    mathml_write(&m, e);
    w->objs[w->nobjs].xml = buf_take(&m);
    w->nobjs++;
    buf_printf(w->o, "<draw:object xlink:href=\"./Object %d\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/>", w->nobjs);
  }
  buf_adds(w->o, "</draw:frame>");
}
static void w_object(W *w, const Image *im, int child, long offx, long offy, long cx, long cy)
{
  Buf k;
  char gs[24], path[48];
  int z = im->z < 0 ? 0 : im->z;
  if (im->kind == OBJ_GROUP) {
    int i;
    Buf g;
    char gn[24];
    buf_init(&g);
    graphic_key(w, im, &g, 1);
    auto_name(w, &w->ag[w->part], g.data ? g.data : "", gn, sizeof gn);
    buf_free(&g);
    buf_printf(w->o, "<draw:g draw:style-name=\"%s\" draw:z-index=\"%d\"", gn, z);
    if (!child) buf_printf(w->o, " text:anchor-type=\"%s\"", im->wrap == WRAP_INLINE ? "as-char" : "char");
    buf_adds(w->o, ">");
    for (i = 0; i < im->nkids; i++) {
      const Image *c = &im->kids[i];
      long sx = im->chw ? (long)((long long)c->x * cx / im->chw) : c->x, sy = im->chh ? (long)((long long)c->y * cy / im->chh) : c->y;
      long scx = im->chw ? (long)((long long)c->cx * cx / im->chw) : c->cx, scy = im->chh ? (long)((long long)c->cy * cy / im->chh) : c->cy;
      long bx = child ? offx : im->wrap == WRAP_INLINE ? 0 : im->x, by = child ? offy : im->wrap == WRAP_INLINE ? 0 : im->y;
      w_object(w, c, 1, bx + sx, by + sy, scx, scy);
    }
    buf_adds(w->o, "</draw:g>");
    return;
  }
  if (im->kind == OBJ_SHAPE || OBJ_IS_GRAPHIC(im->kind)) {
    if (OBJ_IS_GRAPHIC(im->kind)) {
      /* a chart or SmartArt: a dashed box in its place */
      Image box = *im;
      box.kind = OBJ_SHAPE; box.shape = SH_RECT; box.lstyle = PB_DASHED; box.lwidth = 4; box.lcolor = 0x808080; box.fill = COLOR_AUTO; box.text = NULL;
      w_shape(w, &box, child, offx, offy, cx, cy);
      return;
    }
    w_shape(w, im, child, offx, offy, cx, cy);
    return;
  }
  buf_init(&k);
  graphic_key(w, im, &k, 0);
  auto_name(w, &w->ag[w->part], k.data ? k.data : "", gs, sizeof gs);
  buf_free(&k);
  w->nframes++;
  buf_printf(w->o, "<draw:frame draw:style-name=\"%s\" draw:name=\"%s%d\" draw:z-index=\"%d\"", gs, im->kind == OBJ_TEXTBOX ? "Frame" : "Image", w->nframes, z);
  place_attrs(w, im, child, offx, offy, cx, cy);
  buf_adds(w->o, ">");
  if (im->kind == OBJ_TEXTBOX) {
    buf_adds(w->o, "<draw:text-box>");
    if (im->text && im->text->np) w_paras_story(w, im->text); else buf_adds(w->o, "<text:p/>");
    buf_adds(w->o, "</draw:text-box>");
  } else if (im->data) {
    const char *mime = im->type == IMG_JPEG ? "image/jpeg" : "image/png";
    if (w->flat) {
      buf_printf(w->o, "<draw:image draw:mime-type=\"%s\"><office:binary-data>", mime);
      b64_encode(w->o, im->data, im->len);
      buf_adds(w->o, "</office:binary-data></draw:image>");
    } else if (pic_path(w, im, path, sizeof path))
      buf_printf(w->o, "<draw:image xlink:href=\"%s\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\" draw:mime-type=\"%s\"/>", path, mime);
  }
  w_alt(w, im);
  buf_adds(w->o, "</draw:frame>");
}

/* ---- notes and comments */
static void w_note(W *w, int k, int end)
{
  Story *s = end ? (k < w->d->nenotes ? &w->d->enotes[k] : NULL) : (k < w->d->nnotes ? &w->d->notes[k] : NULL);
  int num = end ? ++w->nend : ++w->nfoot;
  char cit[24];
  int sp = w->sp;
  u16 bm = w->bmark;
  int ins = w->ins;
  if (!s) return;
  {
    NotePr pr;
    notes_eff(w->d, NULL, end, &pr);
    notes_label(pr.fmt, (u32)num, cit);
  }
  buf_printf(w->o, "<text:note text:id=\"%s%d\" text:note-class=\"%s\"><text:note-citation>", end ? "edn" : "ftn", num, end ? "endnote" : "footnote");
  xml_escape(w->o, cit, strlen(cit));
  buf_adds(w->o, "</text:note-citation><text:note-body>");
  w->bmark = 0; w->ins = 0;
  w->notelead = 1;          /* (a space after the number, as Word's notes have: read back, it goes) */
  if (s->np) w_blocks(w, s->p, 0, s->np, 9); else buf_adds(w->o, "<text:p><text:s/></text:p>");
  w->notelead = 0;
  close_bmark(w);
  buf_adds(w->o, "</text:note-body></text:note>");
  w->sp = sp; w->bmark = bm; w->ins = ins;
}
static void w_annotation(W *w, int k)
{
  Comment *c = k < w->d->ncomments ? &w->d->comments[k] : NULL;
  int sp = w->sp, ins = w->ins;
  u16 bm = w->bmark;
  if (!c) return;
  buf_printf(w->o, "<office:annotation office:name=\"__Annotation__%d\"%s>", k, c->done ? " loext:resolved=\"true\"" : "");
  buf_adds(w->o, "<dc:creator>"); xml_escape(w->o, c->author, strlen(c->author)); buf_adds(w->o, "</dc:creator>");
  if (c->date[0]) {
    char date[32];
    snprintf(date, sizeof date, "%s", c->date);
    if (date[0] && date[strlen(date) - 1] == 'Z') date[strlen(date) - 1] = 0;
    buf_adds(w->o, "<dc:date>"); xml_escape(w->o, date, strlen(date)); buf_adds(w->o, "</dc:date>");
  }
  if (c->initials[0]) { buf_adds(w->o, "<meta:creator-initials>"); xml_escape(w->o, c->initials, strlen(c->initials)); buf_adds(w->o, "</meta:creator-initials>"); }
  w->bmark = 0; w->ins = 0;
  if (c->text.np) w_blocks(w, c->text.p, 0, c->text.np, 9); else buf_adds(w->o, "<text:p/>");
  close_bmark(w);
  buf_adds(w->o, "</office:annotation>");
  w->sp = sp; w->bmark = bm; w->ins = ins;
}

/* ---- paragraphs */
/* a stretch of a run's text: characters, spaces, tabs, breaks, fields and objects */
static void w_chars(W *w, const Para *p, u32 a, u32 b, const CharFmt *f)
{
  u32 i = a, s = a;
  while (i < b) {
    u8 c = (u8)p->t[i];
    if (c == ' ') {
      u32 n = 0;
      if (i > s) w_esc_text(w, p->t + s, i - s);
      while (i + n < b && p->t[i + n] == ' ') n++;
      /* one space after text is written as it is; more, at the start, or at the end, as text:s */
      if (!w->sp && i + n < p->len) { buf_addc(w->o, ' '); n--; i++; w->sp = 1; }
      if (n == 1) buf_adds(w->o, "<text:s/>");
      else if (n > 1) buf_printf(w->o, "<text:s text:c=\"%lu\"/>", (unsigned long)n);
      i += n; s = i;
      w->sp = 1;
      continue;
    }
    if (c == '\t' || c == '\v' || c == 0xEE || c == 0xF3 || c == 0xF4) {
      int used = 1;
      if (i > s) w_esc_text(w, p->t + s, i - s);
      if (c == '\t') buf_adds(w->o, "<text:tab/>");
      else if (c == '\v') buf_adds(w->o, "<text:line-break/>");
      else if (i + 3 <= p->len && IS_FIELD(p->t + i)) {
        used = 3;
        if (p->t[i + 2] == '\x80') buf_adds(w->o, "<text:page-number text:select-page=\"current\">1</text:page-number>");
        else if (p->t[i + 2] == '\x81') buf_adds(w->o, "<text:page-count>1</text:page-count>");
        else {
          const char *url = doc_link_url(w->d, f->link);
          buf_adds(w->o, "<text:bookmark-ref text:reference-format=\"page\" text:ref-name=\"");
          if (url[0] == '#') xml_escape(w->o, url + 1, strlen(url + 1));
          buf_adds(w->o, "\">1</text:bookmark-ref>");
        }
      } else if (i + 4 <= p->len && IS_PIC(p->t + i)) {
        int k = pic_index(p->t + i);
        used = 4;
        if (k >= 0 && k < w->d->nimages) { const Image *im = &w->d->images[k]; w_object(w, im, 0, 0, 0, im->cx, im->cy); }
      } else if (i + 4 <= p->len && IS_NOTE(p->t + i)) { used = 4; w_note(w, note_index(p->t + i), 0); }
      else if (i + 4 <= p->len && IS_ENOTE(p->t + i)) { used = 4; w_note(w, note_index(p->t + i), 1); }
      else if (i + 4 <= p->len && IS_CSTART(p->t + i)) {
        int k = (int)(utf8_get(p->t + i, 4, &used) - 0xF8000);
        w_annotation(w, k);
      } else if (i + 4 <= p->len && IS_CEND(p->t + i)) {
        int k = (int)(utf8_get(p->t + i, 4, &used) - 0xFC000);
        buf_printf(w->o, "<office:annotation-end office:name=\"__Annotation__%d\"/>", k);
      } else if (i + 4 <= p->len && IS_MATH(p->t + i)) { used = 4; w_math(w, math_index(p->t + i), f); }
      else {
        /* any other character with these lead bytes */
        utf8_get(p->t + i, p->len - i, &used);
        w_esc_text(w, p->t + i, (size_t)used);
      }
      i += (u32)used; s = i;
      w->sp = 0;
      continue;
    }
    i++;
    w->sp = 0;
  }
  if (i > s) w_esc_text(w, p->t + s, i - s);
}
/* a run's text in its span (an automatic text style for what differs from base, inside its character
   style's span) */
static void w_span(W *w, const Para *p, u32 a, u32 b, const CharFmt *f, const CharFmt *pbase)
{
  CharFmt base = *pbase;
  unsigned m;
  char cs[96], ts[24];
  cs[0] = ts[0] = 0;
  if (f->cstyle && f->cstyle < w->d->nstyles && w->d->styles[f->cstyle].type == ST_CHAR) {
    cf_apply(&base, w->d->styles[f->cstyle].ecmask & CM_LOOK, &w->d->styles[f->cstyle].cf);
    style_ref(w->d, f->cstyle, cs, sizeof cs);
  }
  m = cm_diff(f, &base);
  if (m) {
    Buf k;
    buf_init(&k);
    buf_addc(&k, '>');
    w_tprops(&k, w->d, f, m);
    auto_name(w, &w->at[w->part], k.data ? k.data : "", ts, sizeof ts);
    buf_free(&k);
  }
  if (cs[0]) buf_printf(w->o, "<text:span text:style-name=\"%s\">", cs);
  if (ts[0]) buf_printf(w->o, "<text:span text:style-name=\"%s\">", ts);
  w_chars(w, p, a, b, f);
  if (ts[0]) buf_adds(w->o, "</text:span>");
  if (cs[0]) buf_adds(w->o, "</text:span>");
}
static void change_info(Buf *b, Doc *d, int rid)
{
  const char *au = rid >= 0 && rid < d->nrevs ? d->revs[rid].author : "Unknown", *dt = rid >= 0 && rid < d->nrevs ? d->revs[rid].date : "";
  char date[32];
  snprintf(date, sizeof date, "%s", dt[0] ? dt : "1970-01-01T00:00:00Z");
  if (date[0] && date[strlen(date) - 1] == 'Z') date[strlen(date) - 1] = 0;
  buf_adds(b, "<office:change-info><dc:creator>");
  xml_escape(b, au, strlen(au));
  buf_adds(b, "</dc:creator><dc:date>");
  xml_escape(b, date, strlen(date));
  buf_adds(b, "</dc:date></office:change-info>");
}
static void ins_close(W *w)
{
  if (!w->ins) return;
  buf_printf(w->o, "<text:change-end text:change-id=\"ct%d\"/>", w->ins);
  w->ins = 0;
}
/* the runs of a paragraph */
static void w_runs(W *w, const Para *p, const CharFmt *base)
{
  u32 k = 0, start = 0;
  int link = 0;
  int track = w->part == 1 && !w->del_rid;
  while (k < p->nr) {
    const Run *r = &p->r[k];
    u32 a = start, b = r->end;
    const CharFmt *f = &r->f;
    start = r->end;
    k++;
    if (b <= a) continue;
    if (track && f->rev == REV_DEL) {
      /* deleted text: into its region, a text:change where it was */
      Buf *save = w->o;
      Buf del;
      u32 kk = k - 1, st = a;
      int rid = f->rid, sp = w->sp;
      ins_close(w);
      if (link) { buf_adds(w->o, "</text:a>"); link = 0; }
      bmark_to(w, f->bmark);
      w->nchg++;
      buf_printf(w->o, "<text:change text:change-id=\"ct%d\"/>", w->nchg);
      buf_init(&del);
      buf_printf(&del, "<text:changed-region xml:id=\"ct%d\" text:id=\"ct%d\"><text:deletion>", w->nchg, w->nchg);
      change_info(&del, w->d, rid);
      buf_adds(&del, "<text:p>");
      w->o = &del; w->del_rid = rid + 1; w->sp = 1;
      /* this run and the deleted ones by the same author after it */
      while (kk < p->nr && p->r[kk].f.rev == REV_DEL && p->r[kk].f.rid == rid) {
        u32 e = p->r[kk].end;
        if (e > st) w_span(w, p, st, e, &p->r[kk].f, base);
        st = e;
        kk++;
      }
      w->o = save; w->del_rid = 0; w->sp = sp;
      buf_adds(&del, "</text:p></text:deletion></text:changed-region>");
      buf_add(&w->chg, del.data, del.len);
      buf_free(&del);
      k = kk; start = st;
      continue;
    }
    if (track) {
      /* an insertion: a region from its first run to its last */
      int want = f->rev == REV_INS ? f->rid + 1 : 0;
      if (w->ins && w->insrid != want) ins_close(w);
      if (want && !w->ins) {
        w->nchg++;
        buf_printf(&w->chg, "<text:changed-region xml:id=\"ct%d\" text:id=\"ct%d\"><text:insertion>", w->nchg, w->nchg);
        change_info(&w->chg, w->d, f->rid);
        buf_adds(&w->chg, "</text:insertion></text:changed-region>");
        buf_printf(w->o, "<text:change-start text:change-id=\"ct%d\"/>", w->nchg);
        w->ins = w->nchg; w->insrid = want;
      }
    }
    if (f->link != link) {
      if (link) buf_adds(w->o, "</text:a>");
      link = f->link;
      if (link) {
        const char *url = doc_link_url(w->d, link);
        buf_adds(w->o, "<text:a xlink:type=\"simple\" xlink:href=\"");
        xml_escape(w->o, url, strlen(url));
        buf_adds(w->o, "\">");
      }
    }
    bmark_to(w, f->bmark);
    w_span(w, p, a, b, f, base);
  }
  if (link) buf_adds(w->o, "</text:a>");
  ins_close(w);
}

/* a paragraph (or heading): its automatic style for what differs from its style */
static void w_para(W *w, const Para *p, const char *master, int inlist)
{
  char mbuf[24];
  int mcentre = 0;
  Doc *d = w->d;
  int st = p->pf.style < d->nstyles && d->styles[p->pf.style].type == ST_PARA ? p->pf.style : 0;
  const Style *s = &d->styles[st];
  u32 a = pa_diff(&p->pf, &s->pf);
  int lvl = fmt_heading_level(d, st);
  char sn[96], an[24];
  const CharFmt *base = &s->cf;
  style_ref(d, st, sn, sizeof sn);
  if (inlist && p->pf.list && p->pf.list <= d->nlists) {
    /* in a list: the indents the level gives need not be said */
    const ListLevel *lv = &d->lists[p->pf.list - 1].lvl[p->pf.level < 9 ? p->pf.level : 8];
    if (p->pf.left == lv->left && p->pf.first == -lv->hanging) a &= ~(u32)(A_LEFT | A_FIRST);
    else a |= A_LEFT | A_FIRST;
  }
  an[0] = 0;
  if (!master && w->pmaster[0]) { snprintf(mbuf, sizeof mbuf, "%s", w->pmaster); master = mbuf; w->pmaster[0] = 0; }
  if (w->pgnum && !a) a = A_ALIGN;
  {
    /* a paragraph of display equations: centred as Quill shows them (marked, so it reads back as it was) */
    u32 j;
    int eq = 0, other = 0;
    for (j = 0; j < p->len; ) {
      if (j + 4 <= p->len && IS_MATH(p->t + j)) { MathEq *e = doc_math(d, math_index(p->t + j)); if (e && e->display) eq = 1; else other = 1; j += 4; continue; }
      if (p->t[j] != ' ') { other = 1; break; }
      j++;
    }
    if (eq && !other && p->pf.align == AL_LEFT) { mcentre = 1; a |= A_ALIGN; }
  }
  if (a || master) {
    Buf k;
    buf_init(&k);
    buf_printf(&k, " style:parent-style-name=\"%s\"", sn);
    if (master) { buf_adds(&k, " style:master-page-name=\""); xml_escape(&k, master, strlen(master)); buf_addc(&k, '"'); }
    buf_addc(&k, '>');
    w_pgnum = w->pgnum; w->pgnum = 0;
    if (w_pgnum && !a) a = A_ALIGN;
    if (mcentre) { ParaFmt c = p->pf; c.align = AL_CENTER; w_mcentre = 1; w_pprops(&k, &c, a); w_mcentre = 0; }
    else w_pprops(&k, &p->pf, a);
    w_pgnum = 0;
    auto_name(w, &w->ap[w->part], k.data ? k.data : "", an, sizeof an);
    buf_free(&k);
  }
  if (lvl) buf_printf(w->o, "<text:h text:style-name=\"%s\" text:outline-level=\"%d\">", an[0] ? an : sn, lvl);
  else buf_printf(w->o, "<text:p text:style-name=\"%s\">", an[0] ? an : sn);
  w->sp = 1;
  if (w->notelead) { buf_adds(w->o, "<text:s/>"); w->notelead = 0; }
  w_runs(w, p, base);
  buf_adds(w->o, lvl ? "</text:h>" : "</text:p>");
}

/* ---- lists */
/* paragraphs a.. of one list at this level of tables, as a text:list (its levels nested); returns
   where it ended */
static u32 w_list(W *w, Para **p, u32 a, u32 b, int depth, int (*key)(const ParaFmt *, int))
{
  int list = p[a]->pf.list, open = 0, lv, i;
  char ln[24];
  u32 j;
  snprintf(ln, sizeof ln, "L%d", list);
  w->nlists++;
  for (j = a; j < b && p[j]->pf.list == list && !key(&p[j]->pf, depth); j++) {
    lv = p[j]->pf.level > 8 ? 8 : p[j]->pf.level;
    if (open == 0) {
      buf_printf(w->o, "<text:list xml:id=\"list%d\" text:style-name=\"%s\"", w->nlists, ln);
      if (w->listlast && list <= w->d->nlists && w->listlast[list - 1]) buf_printf(w->o, " text:continue-list=\"list%d\"", w->listlast[list - 1]);
      buf_adds(w->o, "><text:list-item>");
      open = 1;
    } else if (lv + 1 <= open) {
      while (open > lv + 1) { buf_adds(w->o, "</text:list-item></text:list>"); open--; }
      buf_adds(w->o, "</text:list-item><text:list-item>");
    }
    while (open < lv + 1) { buf_adds(w->o, "<text:list><text:list-item>"); open++; }
    w_para(w, p[j], NULL, 1);
  }
  for (i = 0; i < open; i++) buf_adds(w->o, "</text:list-item></text:list>");
  if (w->listlast && list <= w->d->nlists) w->listlast[list - 1] = w->nlists;
  return j;
}

/* ---- tables */
/* the table a paragraph is in at this depth (0 none): the outermost at depth 0, a nested one inside a cell */
static int table_key(const ParaFmt *pf, int depth)
{
  if (depth == 0) return top_cell(pf)->table;
  if (depth == 1) return pf->outer.table ? pf->cell.table : 0;
  return 0;
}
static const Cell *cell_at(const ParaFmt *pf, int depth) { return depth == 0 ? top_cell(pf) : &pf->cell; }
static void cell_key(const Table *t, const Cell *c, int qlook, CellLook *cl, Buf *k)
{
  static const char *const edge[4] = { "top", "left", "bottom", "right" };
  int e;
  buf_adds(k, "><style:table-cell-properties");
  buf_printf(k, " style:vertical-align=\"%s\"", c->valign == 1 ? "middle" : c->valign == 2 ? "bottom" : "top");
  attr_len(k, "fo:padding-left", t->marl); attr_len(k, "fo:padding-right", t->marr);
  attr_len(k, "fo:padding-top", t->mart); attr_len(k, "fo:padding-bottom", t->marb);
  for (e = 0; e < 4; e++) {
    int on = qlook ? (cl->borders >> e) & 1 : (c->borders >> e) & 1;
    u8 bs = qlook ? cl->bstyle[e] : c->bstyle[e], bw = qlook ? cl->bwidth[e] : c->bwidth[e];
    u32 bc = qlook ? cl->bcolor[e] : c->bcolor[e];
    buf_printf(k, " fo:border-%s=\"", edge[e]);
    if (on) odt_put_border(k, bs ? bs : PB_SINGLE, bw ? bw : 4, bc); else buf_adds(k, "none");
    buf_addc(k, '"');
  }
  {
    u32 sh = qlook ? cl->shade : c->shade;
    if (sh != COLOR_AUTO) { buf_adds(k, " fo:background-color=\""); put_rgb(k, sh); buf_addc(k, '"'); }
    else buf_adds(k, " fo:background-color=\"transparent\"");
  }
  if (qlook) buf_adds(k, " quill:look=\"style\"");
  buf_adds(k, "/>");
}
static u32 w_table(W *w, Para **p, u32 a, u32 b, int depth)
{
  Doc *d = w->d;
  int tn = table_key(&p[a]->pf, depth), nrows = 0, r, c, k;
  const Table *t = tn && tn <= d->ntables ? &d->tables[tn - 1] : NULL;
  u32 e, i;
  char tsn[24];
  i32 width = 0;
  Buf key;
  int hdr_open = 0;
  for (e = a; e < b && table_key(&p[e]->pf, depth) == tn; e++) { const Cell *cc = cell_at(&p[e]->pf, depth); if (cc->row + 1 > nrows) nrows = cc->row + 1; }
  if (!t) { for (i = a; i < e; i++) w_para(w, p[i], NULL, 0); return e; }
  w->ntables++;
  snprintf(tsn, sizeof tsn, "Table%d", w->ntables);
  for (k = 0; k < t->ncols; k++) width += t->grid[k];
  /* the table's style */
  buf_init(&key);
  if (w->pmaster[0]) { buf_adds(&key, " style:master-page-name=\""); buf_adds(&key, w->pmaster); buf_addc(&key, '"'); w->pmaster[0] = 0; }
  buf_adds(&key, "><style:table-properties");
  attr_len(&key, "style:width", width);
  if (t->jc == AL_CENTER) buf_adds(&key, " table:align=\"center\"");
  else if (t->jc == AL_RIGHT) buf_adds(&key, " table:align=\"right\"");
  else { buf_adds(&key, " table:align=\"margins\""); attr_len(&key, "fo:margin-left", t->indent); attr_len(&key, "fo:margin-right", fmt_text_width(d) - width - t->indent); }
  if (p[a]->pf.pagebreak) buf_adds(&key, " fo:break-before=\"page\"");
  if (t->tstyle && t->tstyle <= d->ntstyles) { buf_adds(&key, " quill:table-style=\""); xml_escape(&key, d->tstyles[t->tstyle - 1].id, strlen(d->tstyles[t->tstyle - 1].id)); buf_addc(&key, '"'); }
  if (t->look != LK_DEFAULT) buf_printf(&key, " quill:table-look=\"%04x\"", t->look);
  buf_adds(&key, "/>");
  {
    char an[24];
    auto_name(w, &w->atab[w->part], key.data ? key.data : "", an, sizeof an);
    buf_printf(w->o, "<table:table table:name=\"%s\" table:style-name=\"%s\">", tsn, an);
  }
  buf_free(&key);
  for (k = 0; k < t->ncols; k++) {
    char cn[24];
    buf_init(&key);
    buf_adds(&key, "><style:table-column-properties");
    attr_len(&key, "style:column-width", t->grid[k]);
    buf_adds(&key, "/>");
    auto_name(w, &w->acol[w->part], key.data ? key.data : "", cn, sizeof cn);
    buf_free(&key);
    buf_printf(w->o, "<table:table-column table:style-name=\"%s\"/>", cn);
  }
  for (r = 0; r < nrows; r++) {
    /* the row's cells in column order */
    const Cell *rc = NULL;
    char rn[24];
    for (i = a; i < e; i++) if (cell_at(&p[i]->pf, depth)->row == r) { rc = cell_at(&p[i]->pf, depth); break; }
    if (rc && rc->header && !hdr_open && r == 0) { buf_adds(w->o, "<table:table-header-rows>"); hdr_open = 1; }
    if (hdr_open && (!rc || !rc->header)) { buf_adds(w->o, "</table:table-header-rows>"); hdr_open = 0; }
    buf_init(&key);
    buf_adds(&key, "><style:table-row-properties");
    if (rc && rc->rowh > 0) attr_len(&key, "style:min-row-height", rc->rowh);
    else if (rc && rc->rowh < 0) attr_len(&key, "style:row-height", -rc->rowh);
    buf_printf(&key, " fo:keep-together=\"%s\"/>", rc && rc->cantsplit ? "always" : "auto");
    auto_name(w, &w->ar[w->part], key.data ? key.data : "", rn, sizeof rn);
    buf_free(&key);
    buf_printf(w->o, "<table:table-row table:style-name=\"%s\">", rn);
    for (c = 0; c < t->ncols; ) {
      /* the cell starting at column c */
      u32 x = e, y;
      const Cell *cc = NULL;
      for (i = a; i < e; i++) { const Cell *q = cell_at(&p[i]->pf, depth); if (q->row == r && q->col == c) { x = i; cc = q; break; } }
      if (!cc) { buf_adds(w->o, "<table:covered-table-cell/>"); c++; continue; }
      for (y = x; y < e && cell_at(&p[y]->pf, depth)->row == r && cell_at(&p[y]->pf, depth)->col == c; y++) continue;
      {
        int span = cc->span ? cc->span : 1, rows = 1, rr;
        if (c + span > t->ncols) span = t->ncols - c;
        if (span < 1) span = 1;
        if (cc->vmerge == VM_CONT) {
          /* covered by the cell above: its own paragraph kept inside (hidden, as ODF allows) */
          buf_adds(w->o, "<table:covered-table-cell>");
          w_blocks(w, p, x, y, depth + 1);
          buf_adds(w->o, "</table:covered-table-cell>");
          for (k = 1; k < span; k++) buf_adds(w->o, "<table:covered-table-cell/>");
          c += span;
          continue;
        }
        if (cc->vmerge == VM_START) {
          /* the rows below that continue it */
          for (rr = r + 1; rr < nrows; rr++) {
            int found = 0;
            for (i = a; i < e; i++) { const Cell *q = cell_at(&p[i]->pf, depth); if (q->row == rr && q->col == c) { found = q->vmerge == VM_CONT; break; } }
            if (!found) break;
            rows++;
          }
        }
        {
          CellLook cl;
          int qlook = t->tstyle && !cc->borders && cc->shade == COLOR_AUTO;
          char cn[24];
          memset(&cl, 0, sizeof cl);
          if (qlook) tstyle_cell(d, t->tstyle, t->look, r, c, span, nrows, t->ncols, NULL, &cl);
          buf_init(&key);
          cell_key(t, cc, qlook, &cl, &key);
          auto_name(w, &w->ac[w->part], key.data ? key.data : "", cn, sizeof cn);
          buf_free(&key);
          buf_printf(w->o, "<table:table-cell table:style-name=\"%s\" office:value-type=\"string\"", cn);
          if (span > 1) buf_printf(w->o, " table:number-columns-spanned=\"%d\"", span);
          if (rows > 1) buf_printf(w->o, " table:number-rows-spanned=\"%d\"", rows);
          buf_adds(w->o, ">");
          w_blocks(w, p, x, y, depth + 1);
          buf_adds(w->o, "</table:table-cell>");
          for (k = 1; k < span; k++) buf_adds(w->o, "<table:covered-table-cell/>");
        }
        c += span;
      }
    }
    buf_adds(w->o, "</table:table-row>");
  }
  if (hdr_open) buf_adds(w->o, "</table:table-header-rows>");
  buf_adds(w->o, "</table:table>");
  return e;
}

/* paragraphs a..b: tables, lists and paragraphs (depth: 0 the body or a story, 1 inside a table's
   cell, 2 inside a nested one's) */
static void w_blocks(W *w, Para **p, u32 a, u32 b, int depth)
{
  u32 i = a;
  int dk = depth > 2 ? 9 : depth;
  while (i < b) {
    if (dk < 2 && table_key(&p[i]->pf, dk)) { i = w_table(w, p, i, b, dk); continue; }
    if (p[i]->pf.list && p[i]->pf.list <= w->d->nlists) { i = w_list(w, p, i, b, dk, table_key); continue; }
    w_para(w, p[i], NULL, 0);
    i++;
  }
}
static void w_paras_story(W *w, const Story *s)
{
  int sp = w->sp;
  w_blocks(w, s->p, 0, s->np, 9);
  w->sp = sp;
}

/* ---- sections and master pages */
static int sect_own(const Doc *d, const SectFmt *s)
{
  return s && (s->haspage || s->hf || s->titlepg != d->titlepg);
}
static void master_name(const Doc *d, int k, int last, char *out, size_t cap)
{
  /* section format k (1...; 0 the document's); the last section's are the document's */
  if (!last && k > 0 && k <= d->nsects && sect_own(d, &d->sects[k - 1])) snprintf(out, cap, "Sect%d", k);
  else snprintf(out, cap, "Standard");
}
static void w_body(W *w)
{
  Doc *d = w->d;
  u32 a = 0, i;
  int n = 0;
  char cur[24];
  strcpy(cur, "Standard");
  for (i = 0; i < d->np; i++) {
    int last = i + 1 >= d->np;
    if (d->p[i]->pf.sect || last) {
      int k = d->p[i]->pf.sect && d->p[i]->pf.sect <= d->nsects ? d->p[i]->pf.sect : 0;
      const SectFmt *sf = k ? &d->sects[k - 1] : NULL;
      int start = sf ? sf->start : SB_NEXT, cols = sf ? sf->cols : 1, wrap;
      char mn[24];
      master_name(d, k, last, mn, sizeof mn);
      /* a section starts its master page (a new page); a continuous one too when its headers differ (ODF
         changes them only with a page) */
      if (n == 0) { if (strcmp(mn, "Standard")) snprintf(w->pmaster, sizeof w->pmaster, "%s", mn); }
      else if (start != SB_CONT || strcmp(mn, cur)) snprintf(w->pmaster, sizeof w->pmaster, "%s", mn);
      snprintf(cur, sizeof cur, "%s", mn);
      wrap = cols > 1 || (n > 0 && start == SB_CONT);
      if (wrap) {
        Buf key;
        char sn[24];
        buf_init(&key);
        buf_adds(&key, "><style:section-properties><style:columns");
        buf_printf(&key, " fo:column-count=\"%d\"", cols > 1 ? cols : 1);
        attr_len(&key, "fo:column-gap", sf ? sf->colgap : 720);
        buf_adds(&key, ">");
        if (cols > 1 && sf && sf->colline) buf_adds(&key, "<style:column-sep style:style=\"solid\" style:width=\"0.0071in\" style:color=\"#000000\" style:height=\"100%\" style:vertical-align=\"top\"/>");
        buf_adds(&key, "</style:columns></style:section-properties>");
        auto_name(w, &w->asec[1], key.data ? key.data : "", sn, sizeof sn);
        buf_free(&key);
        w->nsections++;
        buf_printf(w->o, "<text:section text:style-name=\"%s\" text:name=\"Section%d\">", sn, w->nsections);
      }
      w_blocks(w, d->p, a, i + 1, 0);
      if (wrap) buf_adds(w->o, "</text:section>");
      close_bmark(w);
      a = i + 1;
      n++;
    }
  }
  close_bmark(w);
}
static int story_has(const Story *s) { return s && s->np > 0; }
static void w_layout(W *w, Buf *b, const char *name, const PageSetup *pg, int hdr, int ftr)
{
  Doc *d = w->d;
  static const char *const nf[] = { "1", "1", "a", "A", "i", "I", "", "1", "1", "1", "1" };
  buf_printf(b, "<style:page-layout style:name=\"%s\"><style:page-layout-properties", name);
  attr_len(b, "fo:page-width", pg->width); attr_len(b, "fo:page-height", pg->height);
  buf_printf(b, " style:num-format=\"%s\" style:print-orientation=\"%s\"", d->pgfmt < 11 ? nf[d->pgfmt] : "1", pg->width > pg->height ? "landscape" : "portrait");
  attr_len(b, "fo:margin-top", hdr && pg->header < pg->top ? pg->header : pg->top);
  attr_len(b, "fo:margin-bottom", ftr && pg->footer < pg->bottom ? pg->footer : pg->bottom);
  attr_len(b, "fo:margin-left", pg->left); attr_len(b, "fo:margin-right", pg->right);
  if (d->border.style != PB_NONE) {
    buf_adds(b, " fo:border=\""); odt_put_border(b, d->border.style, d->border.width, d->border.color); buf_addc(b, '"');
    attr_len(b, "fo:padding", d->border.space);
  }
  if (d->pagecolor & HL_ON) { buf_adds(b, " fo:background-color=\""); put_rgb(b, d->pagecolor); buf_addc(b, '"'); }
  buf_adds(b, " style:writing-mode=\"lr-tb\"/>");
  buf_adds(b, "<style:header-style>");
  if (hdr || pg->header < pg->top) {
    /* (without a header too: the distance comes back from it) */
    i32 h = pg->top - pg->header;
    if (h < 0) h = 0;
    buf_adds(b, "<style:header-footer-properties");
    attr_len(b, "fo:min-height", h); attr_len(b, "fo:margin-bottom", h / 2 < 283 ? h / 2 : 283);
    buf_adds(b, " style:dynamic-spacing=\"true\"/>");
  }
  buf_adds(b, "</style:header-style><style:footer-style>");
  if (ftr || pg->footer < pg->bottom) {
    i32 h = pg->bottom - pg->footer;
    if (h < 0) h = 0;
    buf_adds(b, "<style:header-footer-properties");
    attr_len(b, "fo:min-height", h); attr_len(b, "fo:margin-top", h / 2 < 283 ? h / 2 : 283);
    buf_adds(b, " style:dynamic-spacing=\"true\"/>");
  }
  buf_adds(b, "</style:footer-style></style:page-layout>");
}
static void w_hfpart(W *w, const char *el, const Story *s, int force)
{
  if (!story_has(s) && !force) return;
  buf_printf(w->o, "<style:%s>", el);
  if (story_has(s)) w_blocks(w, s->p, 0, s->np, 9); else buf_adds(w->o, "<text:p/>");
  close_bmark(w);
  buf_printf(w->o, "</style:%s>", el);
}
/* a master page (name, its layout's name) with these headers and footers */
static void w_master(W *w, Buf *layouts, const char *name, const PageSetup *pg, Story *hdr, Story *ftr, int titlepg)
{
  Doc *d = w->d;
  int h = story_has(&hdr[HF_DEFAULT]) || (titlepg && story_has(&hdr[HF_FIRST])) || (d->evenodd && story_has(&hdr[HF_EVEN]));
  int f = story_has(&ftr[HF_DEFAULT]) || (titlepg && story_has(&ftr[HF_FIRST])) || (d->evenodd && story_has(&ftr[HF_EVEN]));
  char ln[40];
  snprintf(ln, sizeof ln, "pm_%s", name);
  w_layout(w, layouts, ln, pg, h, f);
  buf_printf(w->o, "<style:master-page style:name=\"%s\" style:page-layout-name=\"%s\">", name, ln);
  if (h) {
    w_hfpart(w, "header", &hdr[HF_DEFAULT], 1);
    if (d->evenodd) w_hfpart(w, "header-left", &hdr[HF_EVEN], 1);
    if (titlepg) w_hfpart(w, "header-first", &hdr[HF_FIRST], 1);
  }
  if (f) {
    w_hfpart(w, "footer", &ftr[HF_DEFAULT], 1);
    if (d->evenodd) w_hfpart(w, "footer-left", &ftr[HF_EVEN], 1);
    if (titlepg) w_hfpart(w, "footer-first", &ftr[HF_FIRST], 1);
  }
  buf_adds(w->o, "</style:master-page>");
}

/* ---- styles.xml's common part */
static void w_fonts(W *w, Buf *b)
{
  int k;
  buf_adds(b, "<office:font-face-decls>");
  for (k = 0; k < w->d->nfonts; k++) {
    const char *f = w->d->fonts[k], *q = font_attr_quote(f);
    buf_adds(b, "<style:font-face");
    attr_esc(b, "style:name", f);
    buf_printf(b, " svg:font-family=\"%s", q);
    xml_escape(b, f, strlen(f));
    buf_printf(b, "%s\"/>", q);
  }
  buf_adds(b, "</office:font-face-decls>");
}
static void w_level_text(Buf *b, const ListLevel *lv)
{
  /* prefix, the levels shown and suffix from Quill's "%1.%2." */
  const char *t = lv->text, *first = strchr(t, '%'), *last = NULL, *q;
  int count = 0;
  for (q = t; *q; q++) if (q[0] == '%' && q[1] >= '1' && q[1] <= '9') { last = q; count++; q++; }
  if (!first || !last) {
    buf_adds(b, " style:num-prefix=\"\" style:num-suffix=\"");
    xml_escape(b, t, strlen(t));
    buf_addc(b, '"');
    return;
  }
  buf_adds(b, " style:num-prefix=\""); xml_escape(b, t, (size_t)(first - t)); buf_addc(b, '"');
  buf_adds(b, " style:num-suffix=\""); xml_escape(b, last + 2, strlen(last + 2)); buf_addc(b, '"');
  if (count > 1) buf_printf(b, " text:display-levels=\"%d\"", count);
  buf_adds(b, " loext:num-list-format=\"");
  for (q = t; *q; q++) {
    if (q[0] == '%' && q[1] >= '1' && q[1] <= '9') { buf_printf(b, "%%%c%%", q[1]); q++; }
    else xml_escape(b, q, 1);
  }
  buf_addc(b, '"');
}
static void w_list_styles(W *w, Buf *b)
{
  int k, l;
  for (k = 0; k < w->d->nlists; k++) {
    const ListDef *ld = &w->d->lists[k];
    buf_printf(b, "<text:list-style style:name=\"L%d\">", k + 1);
    for (l = 0; l < 9; l++) {
      const ListLevel *lv = &ld->lvl[l];
      int bullet = lv->fmt == NF_BULLET;
      static const char *const nf[] = { "", "1", "a", "A", "i", "I", "", "01, 02, 03, ...", "1st", "One", "First" };
      if (bullet) {
        buf_printf(b, "<text:list-level-style-bullet text:level=\"%d\" text:bullet-char=\"", l + 1);
        xml_escape(b, lv->text[0] ? lv->text : "\xE2\x80\xA2", lv->text[0] ? (size_t)utf8_next(lv->text, strlen(lv->text), 0) : 3);
        buf_addc(b, '"');
      } else {
        buf_printf(b, "<text:list-level-style-number text:level=\"%d\" style:num-format=\"%s\"", l + 1, lv->fmt < 11 ? nf[lv->fmt] : "1");
        w_level_text(b, lv);
        if (lv->start != 1) buf_printf(b, " text:start-value=\"%d\"", lv->start);
      }
      buf_adds(b, "><style:list-level-properties text:list-level-position-and-space-mode=\"label-alignment\"><style:list-level-label-alignment text:label-followed-by=\"listtab\"");
      attr_len(b, "text:list-tab-stop-position", lv->left);
      attr_len(b, "fo:text-indent", -lv->hanging);
      attr_len(b, "fo:margin-left", lv->left);
      buf_adds(b, "/></style:list-level-properties>");
      if (lv->font[0]) { buf_adds(b, "<style:text-properties"); attr_esc(b, "style:font-name", lv->font); buf_adds(b, "/>"); }
      buf_adds(b, bullet ? "</text:list-level-style-bullet>" : "</text:list-level-style-number>");
    }
    buf_adds(b, "</text:list-style>");
  }
}
static void w_common_styles(W *w, Buf *b)
{
  Doc *d = w->d;
  int k;
  NotePr pr;
  static const char *const nnf[] = { "1", "a", "A", "i", "I", "1", "1" };
  buf_adds(b, "<style:default-style style:family=\"paragraph\">");
  w_pprops(b, &d->defpf, A_ALIGN | A_LEFT | A_RIGHT | A_FIRST | A_BEFORE | A_AFTER | A_LINE | A_KEEPNEXT | A_KEEPLINES | A_WIDOW);
  w_tprops(b, d, &d->defcf, CM_FONT | CM_SIZE | CM_COLOR | (d->defcf.flags & CF_BOLD ? CM_BOLD : 0) | (d->defcf.flags & CF_ITALIC ? CM_ITALIC : 0));
  buf_adds(b, "</style:default-style>");
  buf_adds(b, "<style:style style:name=\"Graphics\" style:family=\"graphic\"/><style:style style:name=\"Frame\" style:family=\"graphic\"/>"
              "<style:style style:name=\"Formula\" style:family=\"graphic\"/>");
  for (k = 0; k < d->nstyles; k++) {
    const Style *s = &d->styles[k];
    char nm[96], ref[96];
    if (s->type == ST_GONE) continue;
    if (k == 0) snprintf(nm, sizeof nm, "Standard"); else style_ref(d, k, nm, sizeof nm);
    buf_printf(b, "<style:style style:name=\"%s\"", nm);
    attr_esc(b, "style:display-name", s->name);
    buf_printf(b, " style:family=\"%s\"", s->type == ST_CHAR ? "text" : "paragraph");
    if (k > 0 && s->based != NO_STYLE && s->based < d->nstyles && d->styles[s->based].type == s->type) { style_ref(d, s->based, ref, sizeof ref); buf_printf(b, " style:parent-style-name=\"%s\"", ref); }
    if (s->type == ST_PARA && s->next != k && s->next < d->nstyles && d->styles[s->next].type == ST_PARA) { style_ref(d, s->next, ref, sizeof ref); buf_printf(b, " style:next-style-name=\"%s\"", ref); }
    if (s->type == ST_PARA && (s->pmask & PM_LIST) && s->opf.list && s->opf.list <= d->nlists) buf_printf(b, " style:list-style-name=\"L%d\"", s->opf.list);
    if (s->type == ST_PARA && fmt_heading_level(d, k)) buf_printf(b, " style:default-outline-level=\"%d\" style:class=\"text\"", fmt_heading_level(d, k));
    buf_addc(b, '>');
    if (s->type == ST_PARA) w_pprops(b, &s->opf, pa_of(s->pmask));
    w_tprops(b, d, &s->ocf, cm_of_style(s->cmask));
    buf_adds(b, "</style:style>");
  }
  w_list_styles(w, b);
  notes_eff(d, NULL, 0, &pr);
  buf_printf(b, "<text:notes-configuration text:note-class=\"footnote\" style:num-format=\"%s\" text:start-value=\"%d\" text:footnotes-position=\"%s\" text:start-numbering-at=\"%s\"/>",
             nnf[pr.fmt < 7 ? pr.fmt : 0], pr.start > 0 ? pr.start - 1 : 0, pr.pos == NPOS_DOCEND || pr.pos == NPOS_SECTEND ? "document" : "page",
             pr.restart == NRS_PAGE ? "page" : pr.restart == NRS_SECTION ? "chapter" : "document");
  notes_eff(d, NULL, 1, &pr);
  buf_printf(b, "<text:notes-configuration text:note-class=\"endnote\" style:num-format=\"%s\" text:start-value=\"%d\"/>", nnf[pr.fmt < 7 ? pr.fmt : 0], pr.start > 0 ? pr.start - 1 : 0);
  {
    const SectFmt *ls = d->np ? doc_sect(d, d->np - 1) : NULL;
    int any = ls && ls->linenum != LN_NONE;
    for (k = 0; !any && k < d->nsects; k++) if (d->sects[k].linenum != LN_NONE) { ls = &d->sects[k]; any = 1; }
    if (any) {
      buf_printf(b, "<text:linenumbering-configuration text:number-lines=\"true\" style:num-format=\"1\" text:number-position=\"left\" text:increment=\"%d\" text:restart-on-page=\"%s\"",
                 ls->lnby ? ls->lnby : 1, ls->linenum == LN_PAGE ? "true" : "false");
      attr_len(b, "text:offset", ls->lndist ? ls->lndist : 360);
      buf_adds(b, "/>");
    }
  }
}

/* ---- meta.xml */
static void w_meta_inner(W *w, Buf *b)
{
  Doc *d = w->d;
  const char *v;
  buf_adds(b, "<office:meta><meta:generator>Quill</meta:generator>");
  if (d->title[0]) { buf_adds(b, "<dc:title>"); xml_escape(b, d->title, strlen(d->title)); buf_adds(b, "</dc:title>"); }
  if ((v = info_str(d, IP_SUBJECT))[0]) { buf_adds(b, "<dc:subject>"); xml_escape(b, v, strlen(v)); buf_adds(b, "</dc:subject>"); }
  if ((v = info_str(d, IP_DESCRIPTION))[0]) { buf_adds(b, "<dc:description>"); xml_escape(b, v, strlen(v)); buf_adds(b, "</dc:description>"); }
  if (d->author[0]) { buf_adds(b, "<meta:initial-creator>"); xml_escape(b, d->author, strlen(d->author)); buf_adds(b, "</meta:initial-creator>"); }
  v = info_str(d, IP_LASTBY);
  if (v[0] || d->author[0]) { buf_adds(b, "<dc:creator>"); xml_escape(b, v[0] ? v : d->author, strlen(v[0] ? v : d->author)); buf_adds(b, "</dc:creator>"); }
  if ((v = info_str(d, IP_KEYWORDS))[0]) {
    /* one meta:keyword each */
    const char *s = v;
    while (*s) {
      const char *c = strchr(s, ',');
      size_t n = c ? (size_t)(c - s) : strlen(s);
      while (n && *s == ' ') { s++; n--; }
      if (n) { buf_adds(b, "<meta:keyword>"); xml_escape(b, s, n); buf_adds(b, "</meta:keyword>"); }
      s += n; if (*s == ',') s++;
    }
  }
  if ((v = info_str(d, IP_LANGUAGE))[0]) { buf_adds(b, "<dc:language>"); xml_escape(b, v, strlen(v)); buf_adds(b, "</dc:language>"); }
  if ((v = info_str(d, IP_CREATED))[0]) { buf_adds(b, "<meta:creation-date>"); xml_escape(b, v, strlen(v)); buf_adds(b, "</meta:creation-date>"); }
  if ((v = info_str(d, IP_MODIFIED))[0]) { buf_adds(b, "<dc:date>"); xml_escape(b, v, strlen(v)); buf_adds(b, "</dc:date>"); }
  if ((v = info_str(d, IP_REVISION))[0]) { buf_adds(b, "<meta:editing-cycles>"); xml_escape(b, v, strlen(v)); buf_adds(b, "</meta:editing-cycles>"); }
  if (d->info) {
    int k;
    for (k = 0; k < d->info->ncust; k++) {
      const CustProp *c = &d->info->cust[k];
      const char *ty = c->type == CP_NUMBER ? "float" : c->type == CP_DATE ? "date" : c->type == CP_BOOL ? "boolean" : "string";
      if (!c->name || c->type == CP_OTHER) continue;
      buf_adds(b, "<meta:user-defined meta:name=\"");
      xml_escape(b, c->name, strlen(c->name));
      buf_printf(b, "\" meta:value-type=\"%s\">", ty);
      if (c->type == CP_BOOL) buf_adds(b, c->value && c->value[0] == '1' ? "true" : "false");
      else if (c->value) xml_escape(b, c->value, strlen(c->value));
      buf_adds(b, "</meta:user-defined>");
    }
  }
  buf_adds(b, "</office:meta>");
}

/* ---- the package */
#define NS " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\"" \
  " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\"" \
  " xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\"" \
  " xmlns:xlink=\"http://www.w3.org/1999/xlink\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\"" \
  " xmlns:number=\"urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0\" xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\"" \
  " xmlns:math=\"http://www.w3.org/1998/Math/MathML\" xmlns:loext=\"urn:org:documentfoundation:names:experimental:office:xmlns:loext:1.0\"" \
  " xmlns:quill=\"urn:quill:odt:1\""
static void autos_all(W *w, Buf *b, int part)
{
  auto_write(b, &w->atab[part]); auto_write(b, &w->acol[part]); auto_write(b, &w->ar[part]); auto_write(b, &w->ac[part]);
  auto_write(b, &w->ap[part]); auto_write(b, &w->at[part]); auto_write(b, &w->ag[part]); auto_write(b, &w->asec[part]);
}
/* a stored entry (the mimetype must be first and uncompressed) */
static void zip_stored(ZipWriter *z, const char *name, const char *data)
{
  size_t n = strlen(data), nl = strlen(name);
  u32 crc = crc32_update(0, (const u8 *)data, n), off = (u32)z->out.len;
  u8 h[46];
  int i;
  /* local header */
  static const u8 lh[] = { 'P', 'K', 3, 4, 10, 0, 0, 0, 0, 0 };
  memcpy(h, lh, 10);
  h[10] = (u8)z->dostime; h[11] = (u8)(z->dostime >> 8); h[12] = (u8)z->dosdate; h[13] = (u8)(z->dosdate >> 8);
  for (i = 0; i < 4; i++) { h[14 + i] = (u8)(crc >> (8 * i)); h[18 + i] = (u8)(n >> (8 * i)); h[22 + i] = (u8)(n >> (8 * i)); }
  h[26] = (u8)nl; h[27] = 0; h[28] = 0; h[29] = 0;
  buf_add(&z->out, h, 30); buf_add(&z->out, name, nl); buf_add(&z->out, data, n);
  /* its directory record */
  memset(h, 0, sizeof h);
  h[0] = 'P'; h[1] = 'K'; h[2] = 1; h[3] = 2; h[4] = 20; h[6] = 10;
  h[12] = (u8)z->dostime; h[13] = (u8)(z->dostime >> 8); h[14] = (u8)z->dosdate; h[15] = (u8)(z->dosdate >> 8);
  for (i = 0; i < 4; i++) { h[16 + i] = (u8)(crc >> (8 * i)); h[20 + i] = (u8)(n >> (8 * i)); h[24 + i] = (u8)(n >> (8 * i)); h[42 + i] = (u8)(off >> (8 * i)); }
  h[28] = (u8)nl;
  buf_add(&z->cdir, h, 46); buf_add(&z->cdir, name, nl);
  z->n++;
}
u8 *odt_write(Doc *d, int flat, size_t *len)
{
  W w;
  Buf body, masters, layouts, content, styles, meta, out;
  int k;
  memset(&w, 0, sizeof w);
  w.d = d; w.flat = flat;
  {
    static const char *const pre[2][8] = { { "MP", "MT", "Mfr", "Mce", "Mro", "Mco", "MTable", "MSect" }, { "P", "T", "fr", "ce", "ro", "co", "Tab", "Sect" } };
    for (k = 0; k < 2; k++) {
      w.ap[k].prefix = pre[k][0]; w.ap[k].family = "paragraph";
      w.at[k].prefix = pre[k][1]; w.at[k].family = "text";
      w.ag[k].prefix = pre[k][2]; w.ag[k].family = "graphic";
      w.ac[k].prefix = pre[k][3]; w.ac[k].family = "table-cell";
      w.ar[k].prefix = pre[k][4]; w.ar[k].family = "table-row";
      w.acol[k].prefix = pre[k][5]; w.acol[k].family = "table-column";
      w.atab[k].prefix = pre[k][6]; w.atab[k].family = "table";
      w.asec[k].prefix = pre[k][7]; w.asec[k].family = "section";
    }
  }
  if (d->nlists) w.listlast = (int *)calloc((size_t)d->nlists, sizeof(int));
  buf_init(&body); buf_init(&masters); buf_init(&layouts); buf_init(&content); buf_init(&styles); buf_init(&meta); buf_init(&out); buf_init(&w.chg);
  /* the body (content.xml's automatic styles collect as it is written) */
  w.part = 1; w.o = &body;
  if (d->pgstart > 1) w.pgnum = d->pgstart;
  w_body(&w);
  /* the master pages (styles.xml's) */
  w.part = 0; w.o = &masters;
  {
    w_master(&w, &layouts, "Standard", &d->page, d->hdr, d->ftr, d->titlepg);
    for (k = 0; k < d->nsects; k++) {
      const SectFmt *s = &d->sects[k];
      char mn[24];
      u32 j;
      int used = 0;
      if (!sect_own(d, s)) continue;
      /* (only those of sections that end before the last: the last's are the document's) */
      for (j = 0; j + 1 < d->np && !used; j++) if (d->p[j]->pf.sect == k + 1) used = 1;
      if (!used) continue;
      master_name(d, k + 1, 0, mn, sizeof mn);
      w_master(&w, &layouts, mn, s->haspage ? &s->page : &d->page, s->hf ? s->hf : d->hdr, s->hf ? s->hf + HF_N : d->ftr, s->titlepg);
    }
  }
  /* styles.xml */
  if (!flat) {
    buf_adds(&styles, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-styles" NS " office:version=\"1.3\">");
    w_fonts(&w, &styles);
    buf_adds(&styles, "<office:styles>");
    w_common_styles(&w, &styles);
    buf_adds(&styles, "</office:styles><office:automatic-styles>");
    buf_add(&styles, layouts.data, layouts.len);
    autos_all(&w, &styles, 0);
    buf_adds(&styles, "</office:automatic-styles><office:master-styles>");
    buf_add(&styles, masters.data, masters.len);
    buf_adds(&styles, "</office:master-styles></office:document-styles>");
    /* content.xml */
    buf_adds(&content, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-content" NS " office:version=\"1.3\">");
    w_fonts(&w, &content);
    buf_adds(&content, "<office:automatic-styles>");
    autos_all(&w, &content, 1);
    buf_adds(&content, "</office:automatic-styles><office:body><office:text>");
    if (w.chg.len) { buf_adds(&content, "<text:tracked-changes>"); buf_add(&content, w.chg.data, w.chg.len); buf_adds(&content, "</text:tracked-changes>"); }
    buf_add(&content, body.data, body.len);
    buf_adds(&content, "</office:text></office:body></office:document-content>");
    buf_adds(&meta, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-meta" NS " office:version=\"1.3\">");
    w_meta_inner(&w, &meta);
    buf_adds(&meta, "</office:document-meta>");
    {
      ZipWriter z;
      Buf man;
      buf_init(&man);
      buf_adds(&man, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.3\">"
                     "<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.3\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>"
                     "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
                     "<manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>"
                     "<manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>");
      for (k = 0; k < w.npics; k++)
        buf_printf(&man, "<manifest:file-entry manifest:full-path=\"%s\" manifest:media-type=\"%s\"/>", w.pics[k].path, strstr(w.pics[k].path, ".jpg") ? "image/jpeg" : "image/png");
      for (k = 0; k < w.nobjs; k++)
        buf_printf(&man, "<manifest:file-entry manifest:full-path=\"Object %d/\" manifest:version=\"1.3\" manifest:media-type=\"application/vnd.oasis.opendocument.formula\"/>"
                         "<manifest:file-entry manifest:full-path=\"Object %d/content.xml\" manifest:media-type=\"text/xml\"/>", k + 1, k + 1);
      buf_adds(&man, "</manifest:manifest>");
      zip_begin(&z);
      zip_stored(&z, "mimetype", "application/vnd.oasis.opendocument.text");
      if (!zip_add(&z, "content.xml", content.data, content.len) || !zip_add(&z, "styles.xml", styles.data, styles.len) ||
          !zip_add(&z, "meta.xml", meta.data, meta.len)) w.oom = 1;
      for (k = 0; k < w.npics && !w.oom; k++) if (!zip_add(&z, w.pics[k].path, w.pics[k].data, w.pics[k].len)) w.oom = 1;
      for (k = 0; k < w.nobjs && !w.oom; k++) {
        char nm[48];
        snprintf(nm, sizeof nm, "Object %d/content.xml", k + 1);
        if (!zip_add(&z, nm, w.objs[k].xml, strlen(w.objs[k].xml))) w.oom = 1;
      }
      if (!w.oom && !zip_add(&z, "META-INF/manifest.xml", man.data, man.len)) w.oom = 1;
      if (!w.oom && !zip_finish(&z)) w.oom = 1;
      buf_free(&man);
      if (!w.oom && !z.out.oom && !z.cdir.oom) { *len = z.out.len; out.data = buf_take(&z.out); out.len = *len; }
      zip_free(&z);
    }
  } else {
    buf_adds(&out, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document" NS " office:version=\"1.3\" office:mimetype=\"application/vnd.oasis.opendocument.text\">");
    w_meta_inner(&w, &out);
    w_fonts(&w, &out);
    buf_adds(&out, "<office:styles>");
    w_common_styles(&w, &out);
    buf_adds(&out, "</office:styles><office:automatic-styles>");
    buf_add(&out, layouts.data, layouts.len);
    autos_all(&w, &out, 0);
    autos_all(&w, &out, 1);
    buf_adds(&out, "</office:automatic-styles><office:master-styles>");
    buf_add(&out, masters.data, masters.len);
    buf_adds(&out, "</office:master-styles><office:body><office:text>");
    if (w.chg.len) { buf_adds(&out, "<text:tracked-changes>"); buf_add(&out, w.chg.data, w.chg.len); buf_adds(&out, "</text:tracked-changes>"); }
    buf_add(&out, body.data, body.len);
    buf_adds(&out, "</office:text></office:body></office:document>\n");
    if (out.oom) w.oom = 1;
    *len = out.len;
  }
  if (body.oom || masters.oom || layouts.oom || content.oom || styles.oom || meta.oom || w.chg.oom) w.oom = 1;
  buf_free(&body); buf_free(&masters); buf_free(&layouts); buf_free(&content); buf_free(&styles); buf_free(&meta); buf_free(&w.chg);
  for (k = 0; k < 2; k++) { auto_free(&w.ap[k]); auto_free(&w.at[k]); auto_free(&w.ag[k]); auto_free(&w.ac[k]); auto_free(&w.ar[k]); auto_free(&w.acol[k]); auto_free(&w.atab[k]); auto_free(&w.asec[k]); }
  for (k = 0; k < w.nobjs; k++) free(w.objs[k].xml);
  free(w.objs); free(w.pics); free(w.listlast);
  if (w.oom || !out.data) { buf_free(&out); return NULL; }
  return (u8 *)out.data;
}
