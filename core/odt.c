/*
 * odt.c - OpenDocument Text (docs/wordcraft-gap.md WP28b). This file belongs to WP28b: only that
 * package edits it. Reading is here, writing in core/odtw.c (odtx.h has what they share).
 *
 * Read (OASIS ODF 1.3 Part 3, as LibreOffice and other programs write it): .odt packages (and .ott
 * templates) and Flat ODT (.fodt, one XML file, pictures as base64); content.xml, styles.xml and
 * meta.xml. Paragraph and character styles, common and automatic, with their parents and next
 * styles and the default style (only the styles the text uses, with their ancestors, are made);
 * fonts (font-face declarations); paragraph properties (alignment, indents, spacing, line spacing,
 * tabs with leaders, keeping, widow control, breaks, borders and padding, shading, contextual
 * spacing, drop caps) and text properties (font, size, absolute or relative, bold, italic, the
 * underlines and their colour, strike and double strike, super/subscript and raised text, caps,
 * small caps, colour, highlight, letter spacing, scale, shadow, outline, relief, hidden, a box);
 * headings (text:h); lists (text:list, list styles with bullets and numbers, their prefixes,
 * suffixes and levels shown, LibreOffice's num-list-format, start values, continued and restarted
 * lists, the indents a level gives); tables (column widths, absolute or relative, spans across
 * columns and rows, covered cells, borders, padding, shading, vertical alignment, row heights,
 * rows kept on one page, heading rows, alignment and indent; one table inside a cell; deeper ones
 * and tables in headers, footers and notes are flattened); pictures (Pictures/ in the package or
 * base64 in a Flat ODT; PNG and JPEG, others a box of their size) anchored as characters or to the
 * paragraph, character or page, with wrapping, position, distances, cropping, mirroring, border
 * and alt text; text boxes (frames with draw:text-box) and shapes (rectangles, ellipses, lines and
 * LibreOffice's custom shapes, with fill, line and text; groups); headers and footers from the
 * master pages (default, left = even, first, and a first page master followed by another);
 * footnotes and endnotes with their configuration; bookmarks; links; fields (page number, page
 * count and a bookmark's page become Quill's own; date, time, author, title and the others keep
 * what they show); tracked changes (insertions and deletions, with author and date; deleted
 * paragraphs split as they show); comments (office:annotation, over a range with
 * office:annotation-end); sections (text:section) with columns, and sections by master page (page
 * size, margins, orientation, headers and footers); page borders and colour; line numbering;
 * equations (formula objects: their MathML becomes Quill's equations, core/mathml.c); the title,
 * author and other properties of meta.xml.
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

/* ================================================================== */
/* what reading and writing share (odtx.h) */

i32 odt_len(const char *s, int *ok)
{
  long whole = 0, frac = 0, fd = 1, per = 0;
  int neg = 0, any = 0;
  long long t;
  *ok = 0;
  if (!s) return 0;
  while (*s == ' ') s++;
  if (*s == '-' || *s == '+') neg = *s++ == '-';
  while (*s >= '0' && *s <= '9') { whole = whole * 10 + (*s++ - '0'); any = 1; if (whole > 10000000) whole = 10000000; }
  if (*s == '.') { s++; while (*s >= '0' && *s <= '9') { if (fd < 1000000) { frac = frac * 10 + (*s - '0'); fd *= 10; } s++; any = 1; } }
  if (!any) return 0;
  /* twips per unit, times 1000 */
  if ((s[0] | 0x20) == 'i' && (s[1] | 0x20) == 'n') per = 1440000;
  else if ((s[0] | 0x20) == 'c' && (s[1] | 0x20) == 'm') per = 566929;
  else if ((s[0] | 0x20) == 'm' && (s[1] | 0x20) == 'm') per = 56693;
  else if ((s[0] | 0x20) == 'p' && (s[1] | 0x20) == 't') per = 20000;
  else if ((s[0] | 0x20) == 'p' && (s[1] | 0x20) == 'c') per = 240000;
  else if ((s[0] | 0x20) == 'p' && (s[1] | 0x20) == 'x') per = 15000;
  else if (!s[0]) per = 1000;               /* (a bare number: twips) */
  else return 0;
  t = ((long long)whole * per * fd + (long long)frac * per + 500 * fd) / (1000 * (long long)fd);
  *ok = 1;
  return (i32)(neg ? -t : t);
}
void odt_put_len(Buf *b, i32 tw)
{
  long v = tw < 0 ? -(long)tw : (long)tw;
  v = (v * 10000 + 720) / 1440;
  buf_printf(b, "%s%ld.%04ldin", tw < 0 && v ? "-" : "", v / 10000, v % 10000);
}
static u32 hexrgb(const char *s)
{
  u32 v = 0;
  int i;
  if (!s || s[0] != '#') return COLOR_AUTO;
  for (i = 1; i <= 6; i++) {
    int c = s[i];
    if (c >= '0' && c <= '9') c -= '0'; else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f') c = (c | 0x20) - 'a' + 10; else return COLOR_AUTO;
    v = v * 16 + (u32)c;
  }
  return v;
}
int odt_border(const char *s, u8 *style, u8 *width, u32 *color)
{
  char tok[32];
  int ok;
  *style = PB_SINGLE; *width = 4; *color = COLOR_AUTO;
  if (!s) return 0;
  while (*s) {
    size_t n = 0;
    while (*s == ' ') s++;
    while (s[n] && s[n] != ' ' && n + 1 < sizeof tok) { tok[n] = s[n]; n++; }
    tok[n] = 0;
    s += n;
    while (*s && *s != ' ') s++;
    if (!n) break;
    if (!strcmp(tok, "none") || !strcmp(tok, "hidden")) { *style = PB_NONE; return 1; }
    else if (tok[0] == '#') *color = hexrgb(tok);
    else if (!strcmp(tok, "double")) *style = PB_DOUBLE;
    else if (!strcmp(tok, "dotted")) *style = PB_DOTTED;
    else if (!strcmp(tok, "dashed")) *style = PB_DASHED;
    else if (!strcmp(tok, "solid") || !strcmp(tok, "groove") || !strcmp(tok, "ridge") || !strcmp(tok, "inset") || !strcmp(tok, "outset")) ;
    else if (!strcmp(tok, "thin")) *width = 2;
    else if (!strcmp(tok, "medium")) *width = 8;
    else if (!strcmp(tok, "thick")) *width = 16;
    else {
      i32 tw = odt_len(tok, &ok);
      if (ok) { long e = ((long)tw * 2 + 2) / 5; *width = (u8)(e < 1 ? 1 : e > 96 ? 96 : e); }    /* eighths of a point */
    }
  }
  if (*style == PB_DOUBLE && *width < 6) *width = 6;
  return 1;
}
void odt_put_border(Buf *b, u8 style, u8 width, u32 color)
{
  long tw;
  if (style == PB_NONE) { buf_adds(b, "none"); return; }
  if (!width) width = 4;
  if (style == PB_THICK && width < 12) width = 12;
  tw = (long)width * 5 / 2;                 /* eighths of a point in twips */
  buf_printf(b, "%ld.%02ldpt %s #%06lx", tw / 20, tw % 20 * 5, style == PB_DOUBLE ? "double" : style == PB_DOTTED ? "dotted" : style == PB_DASHED ? "dashed" : "solid",
             (unsigned long)(color == COLOR_AUTO ? 0 : color & 0xFFFFFF));
}
void odt_unescape(const char *s, char *out, size_t cap)
{
  size_t o = 0;
  while (*s && o + 4 < cap) {
    if (s[0] == '_' && s[1] && s[2] && s[3] == '_' && ((s[1] >= '0' && s[1] <= '9') || ((s[1] | 0x20) >= 'a' && (s[1] | 0x20) <= 'f'))) {
      unsigned v;
      char h[3];
      h[0] = s[1]; h[1] = s[2]; h[2] = 0;
      if (sscanf(h, "%x", &v) == 1) { o += (size_t)utf8_put(out + o, v); s += 4; continue; }
    }
    out[o++] = *s++;
  }
  out[o] = 0;
}
void odt_escape(const char *s, char *out, size_t cap)
{
  size_t o = 0;
  for (; *s && o + 5 < cap; s++) {
    u8 c = (u8)*s;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9' && o) || c == '-' || c == '.' || c >= 0x80) out[o++] = (char)c;
    else { sprintf(out + o, "_%02x_", c); o += 4; }
  }
  out[o] = 0;
}

/* ================================================================== */
/* reading */

enum { F_PARA, F_TEXT, F_TABLE, F_COL, F_ROW, F_CELL, F_GRAPHIC, F_SECTION, F_OTHER };
/* paragraph attributes a style sets */
enum { A_ALIGN = 1, A_LEFT = 2, A_RIGHT = 4, A_FIRST = 8, A_BEFORE = 16, A_AFTER = 32, A_LINE = 64, A_TABS = 128, A_KEEPNEXT = 256,
       A_KEEPLINES = 512, A_WIDOW = 1024, A_PAGEBRK = 2048, A_COLBRK = 4096, A_BORDER = 8192, A_SHADE = 16384, A_CONTEXT = 32768,
       A_DROP = 65536, A_BRKAFTER = 131072 };
/* graphic properties set */
enum { G_WRAP = 1, G_HPOS = 2, G_VPOS = 4, G_MARGIN = 8, G_FILL = 16, G_STROKE = 32, G_SCOLOR = 64, G_SWIDTH = 128, G_MIRROR = 256,
       G_CLIP = 512, G_BORDER = 1024, G_DECOR = 2048, G_PADDING = 4096 };

typedef struct {
  char name[64], disp[64], parent[64], next[64], list[64], master[64];
  u32 hash;
  u8 fam, autom, part;
  int qs;                   /* the Quill style it became (common paragraph and text styles), -1 not yet */
  u8 busy;
  /* paragraph */
  u32 pa; ParaFmt pf; int pgnum; u8 brkafter;
  /* text */
  unsigned cm; CharFmt cf; u16 sizepct; i16 pospct; u8 haspos;
  /* table, column, row, cell */
  i32 width, colw, relw; u8 talign, hasalign; i32 tindent; u8 hasindent;
  i32 rowh; u8 rowexact, cantsplit, hasrowh;
  Cell cell; u8 bset, shadeset, valignset; i32 pad[4]; u8 padset;
  u8 qlook;                 /* (Quill's own) the cell's look comes from its table's style */
  char tstyle[40]; u16 tlook;
  /* graphic */
  u32 ga; u8 wrap, contour, run;     /* run: 1 behind the text, 2 in front */
  char hpos[16], hrel[24], vpos[16], vrel[24];
  i32 gmar[4]; u8 fillon; u32 fill; u8 stroke; u32 scolor; i32 swidth; u8 mirror; i32 clip[4];
  u8 bstyle, bwidth; u32 bcolor; u8 decorative;
  /* section */
  u8 cols, colline; i32 colgap;
} OStyle;

typedef struct { char name[64]; ListDef def; int last; } OList;
typedef struct { char name[64]; PageSetup page; i32 hmin, fmin; u8 hasborder; PageBorder border; u32 bg; u8 cols, colline; i32 colgap; int numfmt; } OLayout;
typedef struct {
  char name[64], layout[64], next[64];
  const char *xml; size_t n;            /* (into styles.xml) the element, read once the styles are */
  Story hf[2 * HF_N];                   /* headers HF_ then footers */
  u8 has[2 * HF_N];
  u8 read;
} OMaster;
typedef struct { char id[64]; u8 type; int rid; char *raw; size_t rawn; } OChange;    /* type 0 insertion, 1 deletion, 2 format */
typedef struct { char name[80]; int k; } OAnno;

/* a paragraph being built */
typedef struct {
  Para *p;
  ParaList *l;
  ParaFmt pf;
  int ws;                   /* white space just written (collapse what follows) */
} PB;

/* where blocks go: the cell, the list */
typedef struct {
  const Cell *cell, *outer;     /* table cells the paragraphs are in (NULL none) */
  int list, level;              /* the list and its level (list 0: none) */
  int listnew;                  /* a list whose number is made at its first paragraph: its style's OList index + 1 */
  int listcont;                 /* (with listnew) continue the list before of that style */
  char listid[64];              /* its xml:id, for lists that continue it */
  int restart;                  /* the list item's start value (0 none) */
  int itemdone;                 /* the item's first paragraph (the one numbered) has been read: the others are only indented */
  int flat;                     /* tables come in as their text (headers, footers, notes) */
  int story;                    /* 0 the body */
} Blk;

typedef struct {
  Doc *d;
  ZipReader *z;
  const char *flatxml; size_t flatn;   /* a Flat ODT: the whole file */
  OStyle *st; int nst, stcap;
  OList *ol; int nol, olcap;
  OLayout *pl; int npl;
  OMaster *mp; int nmp;
  struct { char name[64], family[48]; } *ff; int nff;
  OChange *chg; int nchg;
  OAnno *an; int nan;
  struct { char id[64]; int list; } *lid; int nlid;    /* lists' xml:ids and their Quill lists */
  int part;                 /* 0 styles.xml, 1 content.xml: whose automatic styles names refer to */
  unsigned lost;
  int oom;
  PB *pb;                   /* the paragraph being built */
  int ins_rid;              /* an insertion open: its revision + 1 */
  u16 bmk[16]; int nbmk;    /* bookmarks open (the innermost is the text's) */
  char bmkname[16][80];
  /* sections */
  int sect_start;           /* how the section being read began: SB_ */
  char sect_master[64];     /* its master page */
  u8 sect_cols, sect_colline; i32 sect_colgap;
  int sect_paras;           /* paragraphs in it so far */
  int *sect_mp; int nsect_mp;   /* each section closed: its master page (an OMaster index, -1 none) */
  int first_para_done;
  int pending_break;        /* a break after the paragraph before (fo:break-after): PB_ for the next */
  Image *pend_obj; int npend_obj;      /* page-anchored objects waiting for a paragraph */
  int in_deletion;          /* reading a deletion's text */
  int depth;                /* nesting of stories (notes in text boxes...) */
  u8 linenum, lnby; i32 lndist;   /* line numbering (text:linenumbering-configuration): every section's */
  int flat;                 /* reading a Flat ODT */
} R;

static void blocks(Xml *x, R *r, ParaList *l, Blk *b);
static void inline_children(Xml *x, R *r, CharFmt cf);

static u32 hash_name(const char *s) { u32 h = 5381; while (*s) h = h * 33 + (u8)*s++; return h; }
static void cpy(char *dst, size_t cap, const char *src)
{
  size_t n;
  if (!src) src = "";
  n = strlen(src);
  if (n >= cap) n = cap - 1;
  memcpy(dst, src, n); dst[n] = 0;
}
static int attr_is(Xml *x, const char *name, const char *v) { const char *a = xml_attr(x, name); return a && !strcmp(a, v); }
static i32 attr_len(Xml *x, const char *name, int *ok) { return odt_len(xml_attr(x, name), ok); }
static void skip(Xml *x) { xml_skip(x); }

/* ---- styles */
static int find_style(R *r, const char *name, int fam)
{
  u32 h;
  int i;
  if (!name || !name[0]) return -1;
  h = hash_name(name);
  for (i = 0; i < r->nst; i++) if (r->st[i].hash == h && r->st[i].autom && r->st[i].part == r->part && r->st[i].fam == fam && !strcmp(r->st[i].name, name)) return i;
  for (i = 0; i < r->nst; i++) if (r->st[i].hash == h && !r->st[i].autom && r->st[i].fam == fam && !strcmp(r->st[i].name, name)) return i;
  return -1;
}
static const char *font_family(R *r, const char *face)
{
  int i;
  for (i = 0; i < r->nff; i++) if (!strcmp(r->ff[i].name, face)) return r->ff[i].family;
  return face;
}
static int fam_of(const char *s)
{
  if (!s) return F_OTHER;
  if (!strcmp(s, "paragraph")) return F_PARA;
  if (!strcmp(s, "text")) return F_TEXT;
  if (!strcmp(s, "table")) return F_TABLE;
  if (!strcmp(s, "table-column")) return F_COL;
  if (!strcmp(s, "table-row")) return F_ROW;
  if (!strcmp(s, "table-cell")) return F_CELL;
  if (!strcmp(s, "graphic")) return F_GRAPHIC;
  if (!strcmp(s, "section")) return F_SECTION;
  return F_OTHER;
}

/* a percentage ("58%"); def if s isn't one */
static int pct(const char *s, int def)
{
  int v = 0, any = 0, neg = 0;
  if (!s) return def;
  while (*s == ' ') s++;
  if (*s == '-') { neg = 1; s++; }
  while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); any = 1; if (v > 100000) v = 100000; }
  if (*s == '.') { s++; while (*s >= '0' && *s <= '9') s++; }
  if (!any || *s != '%') return def;
  return neg ? -v : v;
}

static void read_tabs(Xml *x, OStyle *s)
{
  int d0 = x->depth, n = 0;
  s->pa |= A_TABS;
  memset(s->pf.tabs, 0, sizeof s->pf.tabs); memset(s->pf.tabkind, 0, sizeof s->pf.tabkind); memset(s->pf.tablead, 0, sizeof s->pf.tablead);
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type == XT_START && !strcmp(x->name, "tab-stop") && n < 8) {
      int ok;
      i32 pos = attr_len(x, "position", &ok);
      const char *ty = xml_attr(x, "type"), *lt = xml_attr(x, "leader-text"), *ls = xml_attr(x, "leader-style");
      if (ok && pos > 0) {
        s->pf.tabs[n] = pos;
        s->pf.tabkind[n] = (u8)(ty && !strcmp(ty, "center") ? TAB_CENTER : ty && !strcmp(ty, "right") ? TAB_RIGHT : ty && !strcmp(ty, "char") ? TAB_DECIMAL : TAB_LEFT);
        if (lt && lt[0] && lt[0] != ' ') s->pf.tablead[n] = (u8)(lt[0] == '.' ? LEAD_DOT : lt[0] == '-' ? LEAD_HYPHEN : lt[0] == '_' ? LEAD_LINE : LEAD_DOT);
        else if (ls && strcmp(ls, "none")) s->pf.tablead[n] = (u8)(!strcmp(ls, "dotted") ? LEAD_DOT : !strcmp(ls, "solid") ? LEAD_LINE : LEAD_HYPHEN);
        n++;
      }
    }
  }
}
static void border_attrs(Xml *x, u8 *bs, u8 *bw, u32 *bc, int *set, int nedges)
{
  /* fo:border, then each edge (PE_ order: top, left, bottom, right) */
  static const char *const edge[4] = { "border-top", "border-left", "border-bottom", "border-right" };
  const char *all = xml_attr(x, "border");
  int k;
  if (all) for (k = 0; k < nedges; k++) { odt_border(all, &bs[k], &bw[k], &bc[k]); *set = 1; }
  for (k = 0; k < nedges; k++) { const char *v = xml_attr(x, edge[k]); if (v) { odt_border(v, &bs[k], &bw[k], &bc[k]); *set = 1; } }
}
static void padding_attrs(Xml *x, i32 *pad, u8 *set)
{
  static const char *const edge[4] = { "padding-top", "padding-left", "padding-bottom", "padding-right" };
  int ok, k;
  i32 v = attr_len(x, "padding", &ok);
  if (ok) for (k = 0; k < 4; k++) { pad[k] = v; *set |= (u8)(1 << k); }
  for (k = 0; k < 4; k++) { v = attr_len(x, edge[k], &ok); if (ok) { pad[k] = v; *set |= (u8)(1 << k); } }
}
static void para_props(Xml *x, OStyle *s)
{
  const char *v;
  int ok, bset = 0;
  i32 n;
  if ((v = xml_attr(x, "text-align")) && !attr_is(x, "math-centred", "true")) {
    const char *last = xml_attr(x, "text-align-last");
    s->pa |= A_ALIGN;
    s->pf.align = (u8)(!strcmp(v, "center") ? AL_CENTER : !strcmp(v, "end") || !strcmp(v, "right") ? AL_RIGHT :
                       !strcmp(v, "justify") ? (last && !strcmp(last, "justify") ? AL_DISTRIBUTE : AL_JUSTIFY) : AL_LEFT);
  }
  n = attr_len(x, "margin-left", &ok); if (ok) { s->pf.left = n; s->pa |= A_LEFT; }
  n = attr_len(x, "margin-right", &ok); if (ok) { s->pf.right = n; s->pa |= A_RIGHT; }
  n = attr_len(x, "text-indent", &ok); if (ok) { s->pf.first = n; s->pa |= A_FIRST; }
  n = attr_len(x, "margin-top", &ok); if (ok) { s->pf.before = n; s->pa |= A_BEFORE; }
  n = attr_len(x, "margin-bottom", &ok); if (ok) { s->pf.after = n; s->pa |= A_AFTER; }
  if ((v = xml_attr(x, "line-height")) && strcmp(v, "normal")) {
    int p = pct(v, -1);
    if (p >= 0) { s->pf.line = p * 240 / 100; s->pf.linerule = LS_AUTO; s->pa |= A_LINE; }
    else if ((n = odt_len(v, &ok)), ok) { s->pf.line = n; s->pf.linerule = LS_EXACT; s->pa |= A_LINE; }
  } else if (v) { s->pf.line = 240; s->pf.linerule = LS_AUTO; s->pa |= A_LINE; }
  n = attr_len(x, "line-height-at-least", &ok); if (ok) { s->pf.line = n; s->pf.linerule = LS_ATLEAST; s->pa |= A_LINE; }
  if ((v = xml_attr(x, "keep-with-next"))) { s->pf.keepnext = (u8)!strcmp(v, "always"); s->pa |= A_KEEPNEXT; }
  if ((v = xml_attr(x, "keep-together"))) { s->pf.keeplines = (u8)!strcmp(v, "always"); s->pa |= A_KEEPLINES; }
  if ((v = xml_attr(x, "widows")) || (v = xml_attr(x, "orphans"))) { s->pf.nowidow = (u8)!strcmp(v, "0"); s->pa |= A_WIDOW; }
  if ((v = xml_attr(x, "break-before"))) {
    s->pf.pagebreak = (u8)!strcmp(v, "page"); s->pf.colbreak = (u8)!strcmp(v, "column");
    s->pa |= A_PAGEBRK | A_COLBRK;
  }
  if ((v = xml_attr(x, "break-after")) && (!strcmp(v, "page") || !strcmp(v, "column"))) { s->pa |= A_BRKAFTER; s->brkafter = (u8)(v[0] == 'p' ? 1 : 2); }
  border_attrs(x, s->pf.bstyle, s->pf.bwidth, s->pf.bcolor, &bset, 4);
  if (bset) {
    i32 pad[4];
    u8 ps = 0;
    int k;
    s->pa |= A_BORDER;
    memset(pad, 0, sizeof pad);
    padding_attrs(x, pad, &ps);
    for (k = 0; k < 4; k++) { long pt = pad[k] / 20; s->pf.bspace[k] = (u8)(pt < 0 ? 0 : pt > 31 ? 31 : pt); }
  }
  if ((v = xml_attr(x, "background-color"))) { u32 c = hexrgb(v); s->pf.shade = c == COLOR_AUTO ? 0 : HL_ON | c; s->pa |= A_SHADE; }
  if ((v = xml_attr(x, "contextual-spacing"))) { s->pf.contextual = (u8)!strcmp(v, "true"); s->pa |= A_CONTEXT; }
  if ((v = xml_attr(x, "page-number"))) { s->pgnum = !strcmp(v, "auto") ? 0 : atoi(v); }
  if (x->pending_end) { xml_next(x); return; }
  {
    int d0 = x->depth;
    while (xml_next(x) != XT_EOF) {
      if (x->type == XT_END && x->depth < d0) break;
      if (x->type != XT_START) continue;
      if (!strcmp(x->name, "tab-stops")) read_tabs(x, s);
      else if (!strcmp(x->name, "drop-cap")) {
        const char *ln = xml_attr(x, "lines");
        int lines = ln ? atoi(ln) : 1;
        s->pa |= A_DROP;
        s->pf.dropcap = (u8)(lines > 1 ? (lines > 10 ? 10 : lines) : 0);
        s->pf.dropmargin = 0;
        skip(x);
      } else skip(x);
    }
  }
}
static void text_props(Xml *x, R *r, OStyle *s)
{
  const char *v;
  int ok;
  i32 n;
  CharFmt *f = &s->cf;
  if ((v = xml_attr(x, "font-name"))) { f->font = (u8)doc_font(r->d, font_family(r, v)); s->cm |= CM_FONT; }
  else if ((v = xml_attr(x, "font-family"))) {
    char fam[48];
    size_t k = 0;
    while (*v == '\'' || *v == '"' || *v == ' ') v++;
    while (v[k] && v[k] != '\'' && v[k] != '"' && v[k] != ',' && k + 1 < sizeof fam) { fam[k] = v[k]; k++; }
    fam[k] = 0;
    if (fam[0]) { f->font = (u8)doc_font(r->d, fam); s->cm |= CM_FONT; }
  }
  if ((v = xml_attr(x, "font-size"))) {
    int p = pct(v, -1);
    if (p > 0) { s->sizepct = (u16)p; s->cm |= CM_SIZE; }
    else if ((n = odt_len(v, &ok)), ok && n > 0) { f->size = (u16)((n + 5) / 10); s->cm |= CM_SIZE; s->sizepct = 0; }
  }
  if ((v = xml_attr(x, "font-weight"))) { s->cm |= CM_BOLD; if (!strcmp(v, "bold") || atoi(v) >= 600) f->flags |= CF_BOLD; else f->flags &= (u8)~CF_BOLD; }
  if ((v = xml_attr(x, "font-style"))) { s->cm |= CM_ITALIC; if (!strcmp(v, "italic") || !strcmp(v, "oblique")) f->flags |= CF_ITALIC; else f->flags &= (u8)~CF_ITALIC; }
  if ((v = xml_attr(x, "text-underline-style"))) {
    s->cm |= CM_UNDERLINE | CM_UL;
    if (!strcmp(v, "none")) { f->flags &= (u8)~CF_UNDERLINE; f->ul = UL_SINGLE; }
    else {
      const char *ty = xml_attr(x, "text-underline-type"), *w = xml_attr(x, "text-underline-width"), *m = xml_attr(x, "text-underline-mode");
      f->flags |= CF_UNDERLINE;
      f->ul = UL_SINGLE;
      if (!strcmp(v, "dotted")) f->ul = UL_DOTTED;
      else if (strstr(v, "dash")) f->ul = UL_DASHED;
      else if (!strcmp(v, "wave")) f->ul = UL_WAVE;
      if (ty && !strcmp(ty, "double")) f->ul = UL_DOUBLE;
      if (w && (!strcmp(w, "bold") || !strcmp(w, "thick")) && f->ul == UL_SINGLE) f->ul = UL_THICK;
      if (m && !strcmp(m, "skip-white-space") && f->ul == UL_SINGLE) f->ul = UL_WORDS;
    }
  }
  if ((v = xml_attr(x, "text-underline-color"))) { u32 c = hexrgb(v); f->ulcolor = c == COLOR_AUTO ? 0 : HL_ON | c; s->cm |= CM_ULCOLOR; }
  if ((v = xml_attr(x, "text-line-through-style"))) {
    const char *ty = xml_attr(x, "text-line-through-type");
    s->cm |= CM_STRIKE | CM_DSTRIKE;
    f->flags &= (u8)~CF_STRIKE; f->fx &= (u8)~FX_DSTRIKE;
    if (strcmp(v, "none")) { if (ty && !strcmp(ty, "double")) f->fx |= FX_DSTRIKE; else f->flags |= CF_STRIKE; }
  }
  if ((v = xml_attr(x, "text-position"))) {
    int a, b = 100;
    const char *sp;
    s->cm |= CM_SUPER | CM_SUB | CM_POSITION;
    f->flags &= (u8)~(CF_SUPER | CF_SUB); s->haspos = 1; s->pospct = 0;
    if (!strncmp(v, "super", 5)) a = 33; else if (!strncmp(v, "sub", 3)) a = -33; else a = pct(v, 0);
    if ((sp = strchr(v, ' '))) b = pct(sp + 1, 100);
    if (b < 100 && a > 0) f->flags |= CF_SUPER;
    else if (b < 100 && a < 0) f->flags |= CF_SUB;
    else s->pospct = (i16)a;
  }
  if ((v = xml_attr(x, "font-variant"))) { s->cm |= CM_SMALLCAPS; if (!strcmp(v, "small-caps")) f->fx |= FX_SMALLCAPS; else f->fx &= (u8)~FX_SMALLCAPS; }
  if ((v = xml_attr(x, "text-transform"))) { s->cm |= CM_CAPS; if (!strcmp(v, "uppercase")) f->flags |= CF_CAPS; else f->flags &= (u8)~CF_CAPS; }
  if ((v = xml_attr(x, "color"))) { f->color = hexrgb(v); s->cm |= CM_COLOR; }
  if ((v = xml_attr(x, "use-window-font-color")) && !strcmp(v, "true")) { f->color = COLOR_AUTO; s->cm |= CM_COLOR; }
  if ((v = xml_attr(x, "background-color"))) { u32 c = hexrgb(v); f->hl = c == COLOR_AUTO ? 0 : HL_ON | c; s->cm |= CM_HL; }
  if ((v = xml_attr(x, "letter-spacing"))) { n = odt_len(v, &ok); f->spacing = (i16)(ok ? n : 0); s->cm |= CM_SPACING; }
  if ((v = xml_attr(x, "text-scale"))) { int p = pct(v, 100); f->scale = (u16)(p == 100 ? 0 : p); s->cm |= CM_SCALE; }
  if ((v = xml_attr(x, "text-shadow"))) { s->cm |= CM_FX2; f->fx2mask |= FX2_SHADOW; if (strcmp(v, "none")) f->fx2 |= FX2_SHADOW; else f->fx2 &= (u16)~FX2_SHADOW; }
  if ((v = xml_attr(x, "text-outline"))) { s->cm |= CM_FX2; f->fx2mask |= FX2_OUTLINE; if (!strcmp(v, "true")) f->fx2 |= FX2_OUTLINE; else f->fx2 &= (u16)~FX2_OUTLINE; }
  if ((v = xml_attr(x, "font-relief"))) {
    s->cm |= CM_FX2; f->fx2mask |= FX2_EMBOSS | FX2_ENGRAVE;
    f->fx2 &= (u16)~(FX2_EMBOSS | FX2_ENGRAVE);
    if (!strcmp(v, "embossed")) f->fx2 |= FX2_EMBOSS; else if (!strcmp(v, "engraved")) f->fx2 |= FX2_ENGRAVE;
  }
  if ((v = xml_attr(x, "display"))) { s->cm |= CM_FX2; f->fx2mask |= FX2_HIDDEN; if (!strcmp(v, "none")) f->fx2 |= FX2_HIDDEN; else f->fx2 &= (u16)~FX2_HIDDEN; }
  if ((v = xml_attr(x, "border"))) { u8 bs, bw; u32 bc; odt_border(v, &bs, &bw, &bc); s->cm |= CM_CBORDER; if (bs != PB_NONE) f->fx |= FX_BORDER; else f->fx &= (u8)~FX_BORDER; }
  skip(x);
}
static void table_props(Xml *x, OStyle *s)
{
  int ok;
  i32 n;
  const char *v;
  n = attr_len(x, "width", &ok); if (ok) s->width = n;
  n = attr_len(x, "margin-left", &ok); if (ok) { s->tindent = n; s->hasindent = 1; }
  if ((v = xml_attr(x, "align"))) { s->hasalign = 1; s->talign = (u8)(!strcmp(v, "center") ? AL_CENTER : !strcmp(v, "right") ? AL_RIGHT : AL_LEFT); }
  if ((v = xml_attr(x, "break-before"))) { s->pf.pagebreak = (u8)!strcmp(v, "page"); s->pa |= A_PAGEBRK; }
  if ((v = xml_attr(x, "table-style"))) cpy(s->tstyle, sizeof s->tstyle, v);
  if ((v = xml_attr(x, "table-look"))) s->tlook = (u16)strtol(v, NULL, 16);
  skip(x);
}
static void cell_props(Xml *x, OStyle *s)
{
  const char *v;
  int bset = 0, k;
  u8 bs[4], bw[4];
  u32 bc[4];
  memset(bs, 0, sizeof bs);
  border_attrs(x, bs, bw, bc, &bset, 4);
  if (bset) {
    s->bset = 1;
    s->cell.borders = 0;
    for (k = 0; k < 4; k++) {
      /* (PE_ order is E_ order: top, left, bottom, right) */
      if (bs[k] != PB_NONE) { s->cell.borders |= (u8)(1 << k); s->cell.bstyle[k] = bs[k] == PB_SINGLE ? 0 : bs[k]; s->cell.bwidth[k] = bw[k] == 4 ? 0 : bw[k]; s->cell.bcolor[k] = bc[k]; }
      else { s->cell.bstyle[k] = 0; s->cell.bwidth[k] = 0; s->cell.bcolor[k] = COLOR_AUTO; }
    }
  }
  if ((v = xml_attr(x, "background-color"))) { s->cell.shade = hexrgb(v); s->shadeset = 1; }
  if ((v = xml_attr(x, "vertical-align"))) { s->cell.valign = (u8)(!strcmp(v, "middle") ? 1 : !strcmp(v, "bottom") ? 2 : 0); s->valignset = 1; }
  padding_attrs(x, s->pad, &s->padset);
  if ((v = xml_attr(x, "look")) && !strcmp(v, "style")) s->qlook = 1;
  skip(x);
}
static void graphic_props(Xml *x, OStyle *s)
{
  const char *v;
  int ok, k;
  i32 n;
  static const char *const mar[4] = { "margin-left", "margin-right", "margin-top", "margin-bottom" };
  if ((v = xml_attr(x, "wrap"))) {
    const char *rt = xml_attr(x, "run-through");
    s->ga |= G_WRAP;
    s->run = 0;
    if (!strcmp(v, "none")) s->wrap = WRAP_TOPBOTTOM;
    else if (!strcmp(v, "run-through")) { s->wrap = WRAP_FRONT; s->run = (u8)(rt && !strcmp(rt, "background") ? 1 : 2); }
    else s->wrap = WRAP_SQUARE;
  }
  if ((v = xml_attr(x, "wrap-contour"))) s->contour = (u8)!strcmp(v, "true");
  if ((v = xml_attr(x, "horizontal-pos"))) { cpy(s->hpos, sizeof s->hpos, v); s->ga |= G_HPOS; }
  if ((v = xml_attr(x, "horizontal-rel"))) { cpy(s->hrel, sizeof s->hrel, v); s->ga |= G_HPOS; }
  if ((v = xml_attr(x, "vertical-pos"))) { cpy(s->vpos, sizeof s->vpos, v); s->ga |= G_VPOS; }
  if ((v = xml_attr(x, "vertical-rel"))) { cpy(s->vrel, sizeof s->vrel, v); s->ga |= G_VPOS; }
  for (k = 0; k < 4; k++) { n = attr_len(x, mar[k], &ok); if (ok) { s->gmar[k] = n; s->ga |= G_MARGIN; } }
  if ((v = xml_attr(x, "fill"))) { s->fillon = (u8)!strcmp(v, "solid"); s->ga |= G_FILL; if (!s->fillon) s->fill = COLOR_AUTO; }
  if ((v = xml_attr(x, "fill-color")) && (s->fillon || !(s->ga & G_FILL))) { s->fill = hexrgb(v); if (!(s->ga & G_FILL)) { s->fillon = 1; s->ga |= G_FILL; } }
  if ((v = xml_attr(x, "background-color")) && strcmp(v, "transparent") && !(s->ga & G_FILL)) { s->fill = hexrgb(v); s->fillon = 1; s->ga |= G_FILL; }
  if ((v = xml_attr(x, "stroke"))) { s->stroke = (u8)(!strcmp(v, "none") ? PB_NONE : !strcmp(v, "dash") ? PB_DASHED : PB_SINGLE); s->ga |= G_STROKE; }
  if ((v = xml_attr(x, "stroke-color"))) { s->scolor = hexrgb(v); s->ga |= G_SCOLOR; }
  n = attr_len(x, "stroke-width", &ok); if (ok) { s->swidth = n; s->ga |= G_SWIDTH; }
  if ((v = xml_attr(x, "border"))) { odt_border(v, &s->bstyle, &s->bwidth, &s->bcolor); s->ga |= G_BORDER; }
  if ((v = xml_attr(x, "mirror"))) { s->mirror = (u8)((strstr(v, "horizontal") ? 1 : 0) | (strstr(v, "vertical") ? 2 : 0)); s->ga |= G_MIRROR; }
  if ((v = xml_attr(x, "clip")) && !strncmp(v, "rect(", 5)) {
    /* top, right, bottom, left */
    const char *p = v + 5;
    int i;
    for (i = 0; i < 4 && *p; i++) {
      s->clip[i] = odt_len(p, &ok);
      while (*p && *p != ',' && *p != ' ' && *p != ')') p++;
      while (*p == ',' || *p == ' ') p++;
    }
    s->ga |= G_CLIP;
  }
  if ((v = xml_attr(x, "decorative"))) { s->decorative = (u8)!strcmp(v, "true"); s->ga |= G_DECOR; }
  skip(x);
}
static void section_props(Xml *x, OStyle *s)
{
  int d0 = x->depth;
  if (x->pending_end) { xml_next(x); return; }
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type == XT_START && !strcmp(x->name, "columns")) {
      const char *c = xml_attr(x, "column-count");
      int ok, d1 = x->depth;
      i32 g = attr_len(x, "column-gap", &ok);
      s->cols = (u8)(c ? atoi(c) : 1);
      if (s->cols < 1) s->cols = 1;
      if (s->cols > MAX_SECT_COLS) s->cols = MAX_SECT_COLS;
      s->colgap = ok ? g : 720;
      if (x->pending_end) { xml_next(x); continue; }
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type == XT_START && !strcmp(x->name, "column-sep")) { const char *st = xml_attr(x, "style"); s->colline = (u8)(!st || strcmp(st, "none")); skip(x); }
        else if (x->type == XT_START && !strcmp(x->name, "column") && !ok) {
          /* (gaps given per column: the first's right and the second's left space) */
          int ok2;
          i32 e = attr_len(x, "end-indent", &ok2);
          if (ok2) s->colgap = e * 2;
          skip(x);
        } else if (x->type == XT_START) skip(x);
      }
    } else if (x->type == XT_START) skip(x);
  }
}
/* a style element (x at its START): its properties read into a new entry */
static void read_style(Xml *x, R *r, int autom, int isdefault)
{
  OStyle *s;
  int d0 = x->depth;
  const char *v;
  if (r->nst >= r->stcap) {
    int nc = r->stcap ? r->stcap * 2 : 128;
    OStyle *ns = (OStyle *)realloc(r->st, (size_t)nc * sizeof(OStyle));
    if (!ns) { r->oom = 1; skip(x); return; }
    r->st = ns; r->stcap = nc;
  }
  s = &r->st[r->nst];
  memset(s, 0, sizeof *s);
  s->qs = -1; s->pgnum = -1;
  s->cf.color = COLOR_AUTO;
  s->cell.shade = COLOR_AUTO;
  s->fill = COLOR_AUTO;
  s->autom = (u8)autom; s->part = (u8)r->part;
  s->fam = (u8)fam_of(xml_attr(x, "family"));
  if (isdefault) cpy(s->name, sizeof s->name, s->fam == F_PARA ? "\001default-para" : s->fam == F_GRAPHIC ? "\001default-graphic" : s->fam == F_TABLE ? "\001default-table" : "\001default");
  else cpy(s->name, sizeof s->name, xml_attr(x, "name"));
  if (!s->name[0]) { skip(x); return; }
  s->hash = hash_name(s->name);
  cpy(s->disp, sizeof s->disp, xml_attr(x, "display-name"));
  cpy(s->parent, sizeof s->parent, xml_attr(x, "parent-style-name"));
  cpy(s->next, sizeof s->next, xml_attr(x, "next-style-name"));
  cpy(s->list, sizeof s->list, xml_attr(x, "list-style-name"));
  if ((v = xml_attr(x, "master-page-name"))) { cpy(s->master, sizeof s->master, v); if (!v[0]) cpy(s->master, sizeof s->master, "\001none"); }
  r->nst++;
  if (x->pending_end) { xml_next(x); return; }
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    s = &r->st[r->nst - 1];
    if (!strcmp(x->name, "paragraph-properties")) para_props(x, s);
    else if (!strcmp(x->name, "text-properties")) text_props(x, r, s);
    else if (!strcmp(x->name, "table-properties")) table_props(x, s);
    else if (!strcmp(x->name, "table-column-properties")) {
      int ok;
      i32 n = attr_len(x, "column-width", &ok);
      const char *rel = xml_attr(x, "rel-column-width");
      if (ok) s->colw = n;
      if (rel) s->relw = atoi(rel);
      skip(x);
    } else if (!strcmp(x->name, "table-row-properties")) {
      int ok;
      i32 n = attr_len(x, "row-height", &ok);
      const char *kt = xml_attr(x, "keep-together");
      if (ok) { s->rowh = n; s->rowexact = 1; s->hasrowh = 1; }
      n = attr_len(x, "min-row-height", &ok);
      if (ok) { s->rowh = n; s->rowexact = 0; s->hasrowh = 1; }
      if (kt) s->cantsplit = (u8)!strcmp(kt, "always");
      skip(x);
    } else if (!strcmp(x->name, "table-cell-properties")) cell_props(x, s);
    else if (!strcmp(x->name, "graphic-properties")) graphic_props(x, s);
    else if (!strcmp(x->name, "section-properties")) section_props(x, s);
    else skip(x);
  }
}

/* ---- list styles */
static int find_list(R *r, const char *name)
{
  int i;
  if (!name || !name[0]) return -1;
  for (i = 0; i < r->nol; i++) if (!strcmp(r->ol[i].name, name)) return i;
  return -1;
}
static int nf_of(const char *f)
{
  if (!f || !f[0]) return NF_NONE;
  if (!strcmp(f, "1")) return NF_DECIMAL;
  if (!strcmp(f, "a")) return NF_LOWER_LETTER;
  if (!strcmp(f, "A")) return NF_UPPER_LETTER;
  if (!strcmp(f, "i")) return NF_LOWER_ROMAN;
  if (!strcmp(f, "I")) return NF_UPPER_ROMAN;
  if (!strcmp(f, "01, 02, 03, ...") || !strcmp(f, "01")) return NF_DECIMAL_ZERO;
  if (!strcmp(f, "1st")) return NF_ORDINAL;
  if (!strcmp(f, "One")) return NF_CARDINAL_TEXT;
  if (!strcmp(f, "First")) return NF_ORDINAL_TEXT;
  return NF_DECIMAL;
}
/* a bullet from a symbol font's private area as the Unicode character it shows */
static u32 bullet_cp(u32 c)
{
  if (c >= 0xF000 && c <= 0xF0FF) c &= 0xFF;
  switch (c) {
  case 0xB7: case 0x6C: case 0x9F: return 0x2022;
  case 0xA7: case 0x6E: return 0x25AA;
  case 0xD8: case 0xE0: return 0x27A2;
  case 0xFC: return 0x2713;
  case 0x71: return 0x25A1;
  default: return c < 0x20 ? 0x2022 : c;
  }
}
static void read_list_style(Xml *x, R *r, int outline)
{
  OList *ol;
  int d0 = x->depth, k;
  if (r->nol >= r->olcap) {
    int nc = r->olcap ? r->olcap * 2 : 32;
    OList *nl = (OList *)realloc(r->ol, (size_t)nc * sizeof(OList));
    if (!nl) { r->oom = 1; skip(x); return; }
    r->ol = nl; r->olcap = nc;
  }
  ol = &r->ol[r->nol++];
  memset(ol, 0, sizeof *ol);
  cpy(ol->name, sizeof ol->name, outline ? "\001outline" : xml_attr(x, "name"));
  for (k = 0; k < 9; k++) {
    ListLevel *lv = &ol->def.lvl[k];
    lv->fmt = NF_BULLET; lv->start = 1; strcpy(lv->text, "\xE2\x80\xA2");
    lv->left = 720 * (k + 1); lv->hanging = 360;
  }
  if (x->pending_end) { xml_next(x); return; }
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "list-level-style-bullet") || !strcmp(x->name, "list-level-style-number") || !strcmp(x->name, "outline-level-style") ||
        !strcmp(x->name, "list-level-style-image")) {
      const char *lvs = xml_attr(x, "level");
      int lvl = lvs ? atoi(lvs) - 1 : 0, d1 = x->depth, isnum = strcmp(x->name, "list-level-style-bullet") && strcmp(x->name, "list-level-style-image");
      ListLevel tmp, *lv = lvl >= 0 && lvl < 9 ? &ol->def.lvl[lvl] : &tmp;
      i32 ml = 0, ti = 0, sb = 0, mlw = 0;
      int mode_align = 0, haveml = 0;
      if (isnum) {
        const char *fmt = xml_attr(x, "num-format"), *pre = xml_attr(x, "num-prefix"), *suf = xml_attr(x, "num-suffix"),
                   *dl = xml_attr(x, "display-levels"), *sv = xml_attr(x, "start-value"), *lf = xml_attr(x, "num-list-format");
        int show = dl ? atoi(dl) : 1, j;
        size_t o = 0;
        lv->fmt = (u8)nf_of(fmt);
        lv->start = (u16)(sv ? atoi(sv) : 1);
        lv->text[0] = 0;
        if (lf && lf[0]) {
          /* LibreOffice's own: "%1%.%2%." */
          const char *p = lf;
          while (*p && o + 3 < sizeof lv->text) {
            if (p[0] == '%' && p[1] >= '1' && p[1] <= '9' && p[2] == '%') { lv->text[o++] = '%'; lv->text[o++] = p[1]; p += 3; }
            else lv->text[o++] = *p++;
          }
          lv->text[o] = 0;
        } else {
          if (pre) { size_t n = strlen(pre); if (n > 8) n = 8; memcpy(lv->text, pre, n); o = n; }
          if (lv->fmt != NF_NONE) {
            if (show < 1) show = 1;
            if (show > lvl + 1) show = lvl + 1;
            for (j = lvl + 1 - show; j <= lvl && o + 4 < sizeof lv->text; j++) {
              lv->text[o++] = '%'; lv->text[o++] = (char)('1' + j);
              if (j < lvl) lv->text[o++] = '.';
            }
          }
          if (suf) { size_t n = strlen(suf); if (o + n + 1 > sizeof lv->text) n = sizeof lv->text - o - 1; memcpy(lv->text + o, suf, n); o += n; }
          lv->text[o] = 0;
        }
        if (outline && lv->fmt == NF_NONE && !lv->text[0]) lv->fmt = NF_NONE;
      } else {
        const char *bc = xml_attr(x, "bullet-char");
        lv->fmt = NF_BULLET;
        if (bc && bc[0]) {
          int used;
          u32 c = bullet_cp(utf8_get(bc, strlen(bc), &used));
          lv->text[utf8_put(lv->text, c)] = 0;
        }
      }
      if (x->pending_end) { xml_next(x); continue; }
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type != XT_START) continue;
        if (!strcmp(x->name, "list-level-properties")) {
          const char *m = xml_attr(x, "list-level-position-and-space-mode");
          int ok;
          i32 n;
          mode_align = m && !strcmp(m, "label-alignment");
          n = attr_len(x, "space-before", &ok); if (ok) sb = n;
          n = attr_len(x, "min-label-width", &ok); if (ok) mlw = n;
          if (x->pending_end) { xml_next(x); continue; }
          {
            int d2 = x->depth;
            while (xml_next(x) != XT_EOF) {
              if (x->type == XT_END && x->depth < d2) break;
              if (x->type == XT_START && !strcmp(x->name, "list-level-label-alignment")) {
                n = attr_len(x, "margin-left", &ok); if (ok) { ml = n; haveml = 1; }
                n = attr_len(x, "text-indent", &ok); if (ok) ti = n;
                skip(x);
              } else if (x->type == XT_START) skip(x);
            }
          }
        } else if (!strcmp(x->name, "text-properties")) {
          const char *fn = xml_attr(x, "font-name");
          if (fn) cpy(lv->font, sizeof lv->font, font_family(r, fn));
          skip(x);
        } else skip(x);
      }
      if (mode_align && haveml) { lv->left = ml; lv->hanging = -ti; }
      else if (!mode_align && (sb || mlw)) { lv->left = sb + mlw; lv->hanging = mlw; }
      if (lv->hanging < 0) lv->hanging = 0;
    } else skip(x);
  }
}

/* ---- Quill styles made from the file's */
static void apply_pa(ParaFmt *dst, const OStyle *s)
{
  u32 a = s->pa;
  int k;
  if (a & A_ALIGN) dst->align = s->pf.align;
  if (a & A_LEFT) dst->left = s->pf.left;
  if (a & A_RIGHT) dst->right = s->pf.right;
  if (a & A_FIRST) dst->first = s->pf.first;
  if (a & A_BEFORE) dst->before = s->pf.before;
  if (a & A_AFTER) dst->after = s->pf.after;
  if (a & A_LINE) { dst->line = s->pf.line; dst->linerule = s->pf.linerule; }
  if (a & A_TABS) { memcpy(dst->tabs, s->pf.tabs, sizeof dst->tabs); memcpy(dst->tabkind, s->pf.tabkind, sizeof dst->tabkind); memcpy(dst->tablead, s->pf.tablead, sizeof dst->tablead); }
  if (a & A_KEEPNEXT) dst->keepnext = s->pf.keepnext;
  if (a & A_KEEPLINES) dst->keeplines = s->pf.keeplines;
  if (a & A_WIDOW) dst->nowidow = s->pf.nowidow;
  if (a & A_PAGEBRK) dst->pagebreak = s->pf.pagebreak;
  if (a & A_COLBRK) dst->colbreak = s->pf.colbreak;
  if (a & A_BORDER) for (k = 0; k < 4; k++) { dst->bstyle[k] = s->pf.bstyle[k]; dst->bwidth[k] = s->pf.bwidth[k]; dst->bcolor[k] = s->pf.bcolor[k]; dst->bspace[k] = s->pf.bspace[k]; }
  if (a & A_SHADE) dst->shade = s->pf.shade;
  if (a & A_CONTEXT) dst->contextual = s->pf.contextual;
  if (a & A_DROP) { dst->dropcap = s->pf.dropcap; dst->dropmargin = 0; }
}
static unsigned pm_of(u32 a)
{
  unsigned m = 0;
  if (a & A_ALIGN) m |= PM_ALIGN;
  if (a & (A_LEFT | A_RIGHT | A_FIRST)) m |= PM_INDENT;
  if (a & (A_BEFORE | A_AFTER)) m |= PM_SPACING;
  if (a & A_LINE) m |= PM_LINE;
  if (a & A_TABS) m |= PM_TABS;
  if (a & A_KEEPNEXT) m |= PM_KEEP;
  if (a & (A_PAGEBRK | A_COLBRK)) m |= PM_BREAK;
  if (a & A_BORDER) m |= PM_BORDER;
  if (a & A_SHADE) m |= PM_SHADE;
  if (a & (A_KEEPLINES | A_WIDOW)) m |= PM_PAGINATION;
  if (a & A_CONTEXT) m |= PM_CONTEXT;
  return m;
}
static void apply_cm(CharFmt *dst, const OStyle *s)
{
  unsigned m = s->cm;
  CharFmt v = s->cf;
  if (s->sizepct) v.size = (u16)((dst->size * s->sizepct + 50) / 100);
  if (s->haspos) v.position = (i16)(s->pospct * (int)(v.size ? v.size : dst->size) / 100);
  if (!(m & CM_FX2)) v.fx2mask = 0;
  cf_apply(dst, m & CM_LOOK, &v);
}
/* Quill's id for a style: its name with the _xx_ escapes undone and spaces taken out */
static void quill_id(const char *name, char *id, size_t cap)
{
  char u[128];
  size_t i, o = 0;
  odt_unescape(name, u, sizeof u);
  for (i = 0; u[i] && o + 1 < cap; i++) if (u[i] != ' ') id[o++] = u[i];
  id[o] = 0;
}
static int is_standard(const OStyle *s)
{
  return s->fam == F_PARA && (!strcmp(s->name, "Standard") || !strcmp(s->name, "Normal") || !strcmp(s->disp, "Default Paragraph Style"));
}
/* the Quill style a common paragraph or text style becomes (made with the ones it is based on) */
static int qstyle(R *r, int i)
{
  OStyle *s = &r->st[i];
  Style ds;
  int base = -1, k, type;
  if (s->qs >= 0) return s->qs;
  if (s->busy) return s->fam == F_TEXT ? 0 : 0;
  s->busy = 1;
  type = s->fam == F_TEXT ? ST_CHAR : ST_PARA;
  if (s->parent[0]) {
    int pi = find_style(r, s->parent, s->fam);
    if (pi >= 0 && pi != i && !r->st[pi].autom) base = qstyle(r, pi);
    s = &r->st[i];
  }
  memset(&ds, 0, sizeof ds);
  if (is_standard(s)) {
    /* the default paragraph style is Quill's Normal */
    ds = r->d->styles[0];
    ds.based = NO_STYLE;
    ds.opf = r->d->defpf; ds.ocf = r->d->defcf;
    apply_pa(&ds.opf, s); ds.pmask = pm_of(s->pa) & PM_STYLED;
    apply_cm(&ds.ocf, s); ds.cmask = s->cm & CM_LOOK;
    if (s->disp[0] && strcmp(s->disp, "Default Paragraph Style")) cpy(ds.name, sizeof ds.name, s->disp);
    r->d->styles[0] = ds;
    doc_resolve_styles(r->d);
    s->qs = 0; s->busy = 0;
    return 0;
  }
  quill_id(s->name, ds.id, sizeof ds.id);
  if (s->disp[0]) cpy(ds.name, sizeof ds.name, s->disp);
  else { char u[128]; odt_unescape(s->name, u, sizeof u); cpy(ds.name, sizeof ds.name, u); }
  ds.type = (u8)type;
  ds.based = (u8)(base >= 0 && r->d->styles[base].type == type ? base : NO_STYLE);
  ds.next = 0; ds.quick = 1;
  /* its own settings measured on what it is based on */
  if (ds.based != NO_STYLE) { ds.opf = r->d->styles[base].pf; ds.ocf = r->d->styles[base].cf; }
  else { ds.opf = r->d->defpf; ds.ocf = r->d->defcf; }
  ds.opf.list = 0; ds.opf.level = 0;
  if (type == ST_PARA) { apply_pa(&ds.opf, s); ds.pmask = pm_of(s->pa) & PM_STYLED; }
  apply_cm(&ds.ocf, s); ds.cmask = s->cm & CM_LOOK;
  k = doc_style(r->d, ds.id);
  if (k >= 0 && (r->d->styles[k].type != type || k == 0)) { size_t n = strlen(ds.id); cpy(ds.id + n, sizeof ds.id - n, type == ST_CHAR ? "Char" : "Para"); }
  else if (k >= 0) { ds.builtin = r->d->styles[k].builtin; }
  k = doc_add_style(r->d, &ds);
  if (k < 0) k = type == ST_CHAR ? 0 : 0;
  else { r->d->styles[k].next = (u8)k; doc_resolve_styles(r->d); }
  s = &r->st[i];
  s->qs = k; s->busy = 0;
  return k;
}

/* a paragraph's formatting from its style (common or automatic): pf (pf.style its Quill style) and the
   character formatting its text starts from; *own: the automatic style's own attributes; *si its entry */
static void para_style(R *r, const char *name, ParaFmt *pf, CharFmt *cf, u32 *own, int *si)
{
  int i = find_style(r, name, F_PARA), q = 0;
  OStyle *s = i >= 0 ? &r->st[i] : NULL;
  *own = 0; *si = i;
  if (s && s->autom) {
    int pi = s->parent[0] ? find_style(r, s->parent, F_PARA) : -1;
    if (pi < 0) { int di = find_style(r, "Standard", F_PARA); pi = di; }
    if (pi >= 0 && !r->st[pi].autom) q = qstyle(r, pi);
  } else if (s) q = qstyle(r, i);
  else { int di = find_style(r, "Standard", F_PARA); if (di >= 0) q = qstyle(r, di); }
  s = i >= 0 ? &r->st[i] : NULL;
  *pf = r->d->styles[q].pf;
  pf->style = (u8)q;
  pf->list = 0; pf->level = 0;
  *cf = r->d->styles[q].cf;
  if (s && s->autom) {
    apply_pa(pf, s); *own = s->pa;
    apply_cm(cf, s);
  }
  cf->rev = REV_NONE; cf->rid = 0; cf->link = 0; cf->bmark = 0; cf->cstyle = 0;
}
/* a text style (text:span) applied to cf */
static void span_style(R *r, const char *name, CharFmt *cf)
{
  int i = find_style(r, name, F_TEXT);
  OStyle *s;
  if (i < 0) return;
  s = &r->st[i];
  if (!s->autom) {
    int q = qstyle(r, i);
    if (q > 0 && r->d->styles[q].type == ST_CHAR) {
      cf_apply(cf, r->d->styles[q].ecmask & CM_LOOK, &r->d->styles[q].cf);
      cf->cstyle = (u8)q;
    }
    return;
  }
  if (s->parent[0]) {
    int pi = find_style(r, s->parent, F_TEXT);
    if (pi >= 0 && !r->st[pi].autom) {
      int q = qstyle(r, pi);
      s = &r->st[i];
      if (q > 0 && r->d->styles[q].type == ST_CHAR) { cf_apply(cf, r->d->styles[q].ecmask & CM_LOOK, &r->d->styles[q].cf); cf->cstyle = (u8)q; }
    }
  }
  apply_cm(cf, s);
}
/* a graphic style with the ones it is based on: what they set, the nearest winning */
static void graphic_style(R *r, const char *name, OStyle *out)
{
  int chain[8], n = 0, i = find_style(r, name, F_GRAPHIC), k;
  memset(out, 0, sizeof *out);
  out->fill = COLOR_AUTO;
  while (i >= 0 && n < 8) {
    for (k = 0; k < n && chain[k] != i; k++) ;
    if (k < n) break;
    chain[n++] = i;
    i = r->st[i].parent[0] ? find_style(r, r->st[i].parent, F_GRAPHIC) : -1;
  }
  i = find_style(r, "\001default-graphic", F_GRAPHIC);
  if (i >= 0 && n < 8) chain[n++] = i;
  while (n-- > 0) {
    const OStyle *s = &r->st[chain[n]];
    if (s->ga & G_WRAP) { out->wrap = s->wrap; out->run = s->run; }
    if (s->contour) out->contour = 1;
    if (s->ga & G_HPOS) { if (s->hpos[0]) cpy(out->hpos, sizeof out->hpos, s->hpos); if (s->hrel[0]) cpy(out->hrel, sizeof out->hrel, s->hrel); }
    if (s->ga & G_VPOS) { if (s->vpos[0]) cpy(out->vpos, sizeof out->vpos, s->vpos); if (s->vrel[0]) cpy(out->vrel, sizeof out->vrel, s->vrel); }
    if (s->ga & G_MARGIN) memcpy(out->gmar, s->gmar, sizeof out->gmar);
    if (s->ga & G_FILL) { out->fillon = s->fillon; out->fill = s->fill; }
    if (s->ga & G_STROKE) out->stroke = s->stroke;
    if (s->ga & G_SCOLOR) out->scolor = s->scolor;
    if (s->ga & G_SWIDTH) out->swidth = s->swidth;
    if (s->ga & G_BORDER) { out->bstyle = s->bstyle; out->bwidth = s->bwidth; out->bcolor = s->bcolor; }
    if (s->ga & G_MIRROR) out->mirror = s->mirror;
    if (s->ga & G_CLIP) memcpy(out->clip, s->clip, sizeof out->clip);
    if (s->ga & G_DECOR) out->decorative = s->decorative;
    out->ga |= s->ga;
  }
}

/* ---- paragraphs being built */
static void pb_open(R *r, PB *pb, ParaList *l, const ParaFmt *pf, const CharFmt *cf)
{
  memset(pb, 0, sizeof *pb);
  pb->l = l; pb->pf = *pf; pb->ws = 1;
  if (!(pb->p = para_new(pf, cf))) r->oom = 1;
}
static void pb_close(R *r, PB *pb)
{
  if (!pb->p) return;
  para_normalise(pb->p);
  pb->p->pf = pb->pf;
  if (!pl_add(pb->l, pb->p)) r->oom = 1;
  pb->p = NULL;
}
static CharFmt text_cf(R *r, CharFmt cf)
{
  if (r->nbmk) cf.bmark = r->bmk[r->nbmk - 1];
  if (r->in_deletion) { cf.rev = REV_DEL; cf.rid = (u16)(r->in_deletion - 1); }
  else if (r->ins_rid) { cf.rev = REV_INS; cf.rid = (u16)(r->ins_rid - 1); }
  return cf;
}
static void put(R *r, const char *s, size_t n, const CharFmt *cf)
{
  CharFmt f;
  if (!r->pb || !r->pb->p || !n) return;
  f = text_cf(r, *cf);
  if (!para_append(r->pb->p, s, (u32)n, &f)) r->oom = 1;
}
/* character data: white space collapsed as ODF says */
static void put_text(R *r, const char *s, size_t n, const CharFmt *cf)
{
  size_t i = 0, a;
  PB *pb = r->pb;
  if (!pb) return;
  while (i < n) {
    if (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') {
      while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
      if (!pb->ws) { put(r, " ", 1, cf); pb->ws = 1; }
      continue;
    }
    a = i;
    while (i < n && s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') i++;
    put(r, s + a, i - a, cf);
    pb->ws = 0;
  }
}
static void put_special(R *r, const char *s, size_t n, const CharFmt *cf) { put(r, s, n, cf); if (r->pb) r->pb->ws = 0; }

/* ---- objects */
static u8 *part_data(R *r, const char *href, size_t *n)
{
  char name[256];
  int k;
  if (!r->z || !href) return NULL;
  if (href[0] == '.' && href[1] == '/') href += 2;
  odt_unescape("", name, 1);
  /* (%20 and other escapes in a relative IRI) */
  {
    size_t o = 0;
    while (*href && o + 1 < sizeof name) {
      if (href[0] == '%' && href[1] && href[2]) { unsigned v; char h[3]; h[0] = href[1]; h[1] = href[2]; h[2] = 0; if (sscanf(h, "%x", &v) == 1) { name[o++] = (char)v; href += 3; continue; } }
      name[o++] = *href++;
    }
    name[o] = 0;
  }
  if ((k = zip_find(r->z, name)) < 0) return NULL;
  return zip_read(r->z, k, n);
}
static int shape_kind(const char *t)
{
  static const struct { const char *n; int k; } m[] = {
    { "rectangle", SH_RECT }, { "round-rectangle", SH_ROUNDRECT }, { "ellipse", SH_ELLIPSE }, { "circle", SH_ELLIPSE },
    { "isosceles-triangle", SH_TRIANGLE }, { "right-triangle", SH_RTRIANGLE }, { "diamond", SH_DIAMOND }, { "right-arrow", SH_RARROW },
    { "left-arrow", SH_LARROW }, { "up-arrow", SH_UARROW }, { "down-arrow", SH_DARROW }, { "star5", SH_STAR }, { "star4", SH_STAR4 },
    { "pentagon", SH_PENTAGON }, { "hexagon", SH_HEXAGON }, { "octagon", SH_OCTAGON }, { "parallelogram", SH_PARALLELOGRAM },
    { "trapezoid", SH_TRAPEZOID }, { "cross", SH_PLUS }, { "heart", SH_HEART }, { "rectangular-callout", SH_CALLOUT },
    { "round-rectangular-callout", SH_CALLOUT }, { "chevron", SH_CHEVRON }, { "pentagon-right", SH_HOMEPLATE }, { "mso-spt100", SH_RECT },
    { "ooxml-rect", SH_RECT }, { "ooxml-ellipse", SH_ELLIPSE }, { "ooxml-roundRect", SH_ROUNDRECT }, { "ooxml-triangle", SH_TRIANGLE },
    { "ooxml-rightArrow", SH_RARROW }, { "ooxml-star5", SH_STAR }, { "ooxml-diamond", SH_DIAMOND }, { NULL, 0 } };
  int i;
  if (!t) return SH_RECT;
  for (i = 0; m[i].n; i++) if (!strcmp(m[i].n, t)) return m[i].k;
  return SH_RECT;
}
/* a frame's or shape's placing and look from its attributes and graphic style */
static void obj_place(Xml *x, R *r, Image *im, int *pageanchor)
{
  OStyle g;
  const char *anchor = xml_attr(x, "anchor-type"), *v;
  int ok;
  i32 n;
  graphic_style(r, xml_attr(x, "style-name"), &g);
  *pageanchor = anchor && !strcmp(anchor, "page");
  if (!anchor || !strcmp(anchor, "as-char")) im->wrap = WRAP_INLINE;
  else {
    im->wrap = (g.ga & G_WRAP) ? g.wrap : WRAP_SQUARE;
    if (im->wrap == WRAP_FRONT && g.run == 1) im->wrap = WRAP_BEHIND;
    if (im->wrap == WRAP_SQUARE && g.contour) im->wrap = WRAP_TIGHT;
    im->hrel = (u8)(!strcmp(g.hrel, "page") || (!strncmp(g.hrel, "page-", 5) && strcmp(g.hrel, "page-content")) ? POS_PAGE :
                    !strcmp(g.hrel, "page-content") ? POS_MARGIN : POS_COLUMN);
    if (*pageanchor && !g.hrel[0]) im->hrel = POS_PAGE;
    im->vrel = (u8)(!strcmp(g.vrel, "page") ? POS_PAGE : !strcmp(g.vrel, "page-content") ? POS_MARGIN : POS_PARA);
    if (*pageanchor && !g.vrel[0]) im->vrel = POS_PAGE;
    im->halign = (u8)(!strcmp(g.hpos, "left") || !strcmp(g.hpos, "inside") ? PA_START : !strcmp(g.hpos, "center") ? PA_CENTER :
                      !strcmp(g.hpos, "right") || !strcmp(g.hpos, "outside") ? PA_END : PA_NONE);
    im->valign = (u8)(!strcmp(g.vpos, "top") ? PA_START : !strcmp(g.vpos, "middle") || !strcmp(g.vpos, "center") ? PA_CENTER : !strcmp(g.vpos, "bottom") ? PA_END : PA_NONE);
    n = attr_len(x, "x", &ok); if (ok) im->x = n;
    n = attr_len(x, "y", &ok); if (ok) im->y = n;
  }
  if (g.ga & G_MARGIN) { im->dl = g.gmar[0]; im->dr = g.gmar[1]; im->dt = g.gmar[2]; im->db = g.gmar[3]; }
  if (g.ga & G_MIRROR) im->flip = g.mirror;
  if ((g.ga & G_FILL) && g.fillon && g.fill != COLOR_AUTO) im->fill = g.fill;
  if (g.ga & G_BORDER) { im->lstyle = g.bstyle; im->lwidth = g.bwidth; im->lcolor = g.bcolor == COLOR_AUTO ? 0 : g.bcolor; }
  if (g.decorative) im->decorative = 1;
  if ((v = xml_attr(x, "z-index"))) im->z = atoi(v);
  n = attr_len(x, "width", &ok); if (ok) im->cx = n;
  n = attr_len(x, "height", &ok); if (ok) im->cy = n;
  else { n = attr_len(x, "min-height", &ok); if (ok) im->cy = n; }
  if (im->kind != OBJ_PICTURE) {
    /* shapes: lines and fill as the style's stroke says */
    if (g.ga & G_STROKE) im->lstyle = g.stroke;
    else if (!(g.ga & G_BORDER)) im->lstyle = PB_SINGLE;
    if (im->lstyle != PB_NONE) {
      long w = g.ga & G_SWIDTH ? ((long)g.swidth * 2 + 2) / 5 : 4;
      im->lwidth = (u8)(w < 1 ? 1 : w > 96 ? 96 : w);
      if (g.ga & G_SCOLOR) im->lcolor = g.scolor == COLOR_AUTO ? 0 : g.scolor;
      if (g.ga & G_BORDER) { im->lwidth = g.bwidth; im->lcolor = g.bcolor == COLOR_AUTO ? 0 : g.bcolor; }
    }
  }
  if ((v = xml_attr(x, "transform")) && strstr(v, "rotate")) {
    /* rotate (radians, counter-clockwise) round the top left, then translate: Quill's turn (clockwise round
       the centre) and box. Integers only (68k maths): micro-radians */
    const char *p = strstr(v, "rotate") + 6, *t;
    long ur = 0, frac = 0, fd = 1, ang;
    int neg = 0;
    while (*p == ' ' || *p == '(') p++;
    if (*p == '-') { neg = 1; p++; }
    while (*p >= '0' && *p <= '9') { ur = ur * 10 + (*p++ - '0'); if (ur > 100) ur = 100; }
    if (*p == '.') { p++; while (*p >= '0' && *p <= '9') { if (fd < 1000000) { frac = frac * 10 + (*p - '0'); fd *= 10; } p++; } }
    ur = ur * 1000000 + frac * (1000000 / fd);
    if (neg) ur = -ur;
    ang = ur * 18 / 31416;                      /* tenths of a degree, counter-clockwise */
    ang %= 3600; if (ang < 0) ang += 3600;
    im->rot = (u16)((3600 - ang) % 3600);
    if ((t = strstr(v, "translate"))) {
      i32 tx, ty;
      int ok1, ok2;
      t += 9;
      while (*t == ' ' || *t == '(') t++;
      tx = odt_len(t, &ok1);
      while (*t && *t != ' ' && *t != ',') t++;
      while (*t == ' ' || *t == ',') t++;
      ty = odt_len(t, &ok2);
      if (ok1 && ok2) {
        long hx = im->cx / 2, hy = im->cy / 2, c = icos((int)ang), sn = isin((int)ang);
        long rx = (hx * c + hy * sn) / TRIG_ONE, ry = (-hx * sn + hy * c) / TRIG_ONE;
        im->x = (i32)(tx + rx - hx); im->y = (i32)(ty + ry - hy);
      }
    }
  }
}
/* a story from the paragraphs inside x (a text box, a shape's text) */
static Story *inner_story(Xml *x, R *r)
{
  ParaList l;
  Blk b;
  Story *st;
  PB *save = r->pb;
  int ins = r->ins_rid;
  memset(&l, 0, sizeof l);
  memset(&b, 0, sizeof b);
  b.flat = 1; b.story = 1;
  r->ins_rid = 0;
  r->depth++;
  blocks(x, r, &l, &b);
  r->depth--;
  r->pb = save; r->ins_rid = ins;
  if (!l.np) { pl_free(&l); return NULL; }
  if (!(st = (Story *)calloc(1, sizeof(Story)))) { pl_free(&l); r->oom = 1; return NULL; }
  pl_to_story(&l, st);
  return st;
}
/* the object's reference into the text (or, page-anchored outside a paragraph, kept for the next one) */
static void obj_put(R *r, Image *im, const CharFmt *cf)
{
  int k;
  char ref[8];
  if (!r->pb || !r->pb->p) {
    Image *np = (Image *)realloc(r->pend_obj, (size_t)(r->npend_obj + 1) * sizeof(Image));
    if (!np) { img_release(im); r->oom = 1; return; }
    r->pend_obj = np; r->pend_obj[r->npend_obj++] = *im;
    return;
  }
  if ((k = doc_take_object(r->d, im)) < 0) { img_release(im); r->oom = 1; return; }
  pic_utf8(ref, k);
  put_special(r, ref, 4, cf);
}
static void put_math(R *r, MathEq *e, const CharFmt *cf)
{
  int k;
  char ref[8];
  if (!e) return;
  if ((k = doc_add_math(r->d, e)) < 0) { r->oom = 1; return; }
  math_utf8(ref, k);
  put_special(r, ref, 4, cf);
}
/* a picture's bytes from a draw:image (its href, or base64 inside it) */
static u8 *image_bytes(Xml *x, R *r, size_t *n)
{
  const char *href = xml_attr(x, "href");
  u8 *data = NULL;
  int d0 = x->depth;
  if (href && href[0]) data = part_data(r, href, n);
  if (x->pending_end) { xml_next(x); return data; }
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type == XT_START && !strcmp(x->name, "binary-data") && !data) {
      Buf t;
      int d1 = x->depth;
      buf_init(&t);
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type == XT_TEXT) buf_add(&t, x->text.data, x->text.len);
      }
      if (t.data) { u32 len; data = b64_decode(t.data, t.len, &len); *n = len; }
      buf_free(&t);
    } else if (x->type == XT_START) skip(x);
  }
  return data;
}
/* draw:frame (x at its START) */
static void read_frame(Xml *x, R *r, const CharFmt *cf)
{
  Image im;
  int pageanchor = 0, d0 = x->depth, done = 0, isbox = 0;
  u8 *data = NULL;
  size_t n = 0;
  MathEq *eq = NULL;
  Story *text = NULL;
  char alt[160], title[160], gsn[64];
  img_defaults(&im);
  alt[0] = title[0] = 0;
  cpy(gsn, sizeof gsn, xml_attr(x, "style-name"));
  obj_place(x, r, &im, &pageanchor);
  if (!x->pending_end) while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "image") && !data && !eq) {
      u8 *b = image_bytes(x, r, &n);
      u32 w, h;
      if (b && img_info(b, (u32)n, &w, &h) != IMG_NONE) data = b;
      else { free(b); if (!done) done = 2; }        /* (another kind of picture: a box, unless a later one is PNG or JPEG) */
    } else if (!strcmp(x->name, "object") && !eq) {
      const char *href = xml_attr(x, "href");
      if (href && href[0]) {
        char path[300];
        size_t cn;
        u8 *c;
        snprintf(path, sizeof path, "%s/content.xml", href);
        if ((c = part_data(r, path, &cn))) { eq = mathml_parse((const char *)c, cn); free(c); }
        skip(x);
      } else {
        /* (Flat ODT: the object inline) */
        int d1 = x->depth;
        if (!x->pending_end) while (xml_next(x) != XT_EOF) {
          if (x->type == XT_END && x->depth < d1) break;
          if (x->type == XT_START && !strcmp(x->name, "math") && !eq) eq = mathml_read(x);
          else if (x->type == XT_START && strcmp(x->name, "document") && strcmp(x->name, "body") && strcmp(x->name, "formula")) skip(x);
        } else xml_next(x);
      }
      if (!eq) done = 3;
    } else if (!strcmp(x->name, "text-box")) {
      isbox = 1;
      text = inner_story(x, r);
    } else if (!strcmp(x->name, "title") || !strcmp(x->name, "desc")) {
      Buf t;
      int d1 = x->depth;
      buf_init(&t);
      if (!x->pending_end) while (xml_next(x) != XT_EOF) { if (x->type == XT_END && x->depth < d1) break; if (x->type == XT_TEXT) buf_add(&t, x->text.data, x->text.len); }
      else xml_next(x);
      if (t.data) cpy(!strcmp(x->name, "title") ? title : alt, 160, t.data);
      buf_free(&t);
    } else if (!strcmp(x->name, "contour-polygon")) {
      skip(x);
    } else skip(x);
  }
  if (eq) {
    /* an equation: Quill's own, in the line (display ones take a line of their own) */
    if (data) free(data);
    if (r->pb && r->pb->p) put_math(r, eq, cf); else math_free(eq);
    return;
  }
  if (!alt[0] && title[0]) cpy(alt, sizeof alt, title);
  cpy(im.alt, sizeof im.alt, alt);
  if (isbox) {
    im.kind = OBJ_TEXTBOX; im.shape = SH_RECT; im.text = text;
    obj_put(r, &im, cf);
    return;
  }
  if (data) {
    u32 w, h;
    int t = img_info(data, (u32)n, &w, &h);
    im.data = data; im.len = (u32)n; im.type = (u8)t; im.pw = w; im.ph = h;
    if (im.cx <= 0) im.cx = (i32)w * 15;
    if (im.cy <= 0) im.cy = (i32)h * 15;
    /* cropping: what fo:clip cuts off, as thousandths of a percent of the picture (its natural size at 96 dpi) */
    {
      OStyle g;
      graphic_style(r, gsn, &g);
      if ((g.ga & G_CLIP) && w && h) {
        i32 nw = (i32)w * 15, nh = (i32)h * 15;
        im.crop[1] = (i32)((long long)g.clip[0] * 100000 / nh);
        im.crop[2] = (i32)((long long)g.clip[1] * 100000 / nw);
        im.crop[3] = (i32)((long long)g.clip[2] * 100000 / nh);
        im.crop[0] = (i32)((long long)g.clip[3] * 100000 / nw);
      }
    }
    obj_put(r, &im, cf);
    return;
  }
  if (done >= 2) {
    /* a picture Quill can't show (SVG, a metafile, a chart...): a dashed box of its size */
    im.kind = OBJ_SHAPE; im.shape = SH_RECT; im.lstyle = PB_DASHED; im.lwidth = 4; im.lcolor = 0x808080;
    if (im.cx <= 0) im.cx = 1440;
    if (im.cy <= 0) im.cy = 1440;
    r->lost |= LOST_PICTURES;
    obj_put(r, &im, cf);
  }
}
/* a shape (draw:custom-shape, rect, ellipse, line, polygon, path) or a group (draw:g) */
static int read_shape(Xml *x, R *r, Image *im)
{
  int pageanchor = 0, d0 = x->depth, ok;
  char el[32];
  ParaList l;
  cpy(el, sizeof el, x->name);
  img_defaults(im);
  im->kind = OBJ_SHAPE;
  obj_place(x, r, im, &pageanchor);
  if (!strcmp(el, "line")) {
    i32 x1 = attr_len(x, "x1", &ok), y1 = attr_len(x, "y1", &ok), x2 = attr_len(x, "x2", &ok), y2 = attr_len(x, "y2", &ok);
    im->shape = SH_LINE;
    im->x = x1 < x2 ? x1 : x2; im->y = y1 < y2 ? y1 : y2;
    im->cx = x2 > x1 ? x2 - x1 : x1 - x2; im->cy = y2 > y1 ? y2 - y1 : y1 - y2;
    if ((x2 < x1) != (y2 < y1)) im->flip = 1;
  } else if (!strcmp(el, "ellipse") || !strcmp(el, "circle")) im->shape = SH_ELLIPSE;
  else if (!strcmp(el, "rect")) { const char *cr = xml_attr(x, "corner-radius"); im->shape = cr && odt_len(cr, &ok) > 0 ? SH_ROUNDRECT : SH_RECT; }
  else im->shape = SH_RECT;
  if (!strcmp(el, "g")) {
    /* a group: its shapes, placed in the group's box */
    Image kids[64];
    int nk = 0, i;
    i32 minx = 0x7FFFFFFF, miny = 0x7FFFFFFF, maxx = -0x7FFFFFFF, maxy = -0x7FFFFFFF;
    im->kind = OBJ_GROUP;
    if (!x->pending_end) while (xml_next(x) != XT_EOF) {
      if (x->type == XT_END && x->depth < d0) break;
      if (x->type != XT_START) continue;
      if (nk < 64 && (!strcmp(x->name, "custom-shape") || !strcmp(x->name, "rect") || !strcmp(x->name, "ellipse") || !strcmp(x->name, "circle") ||
                      !strcmp(x->name, "line") || !strcmp(x->name, "polygon") || !strcmp(x->name, "path") || !strcmp(x->name, "g") || !strcmp(x->name, "polyline"))) {
        if (read_shape(x, r, &kids[nk])) nk++;
      } else skip(x);
    } else xml_next(x);
    if (!nk) { img_release(im); return 0; }
    for (i = 0; i < nk; i++) {
      if (kids[i].x < minx) minx = kids[i].x;
      if (kids[i].y < miny) miny = kids[i].y;
      if (kids[i].x + kids[i].cx > maxx) maxx = kids[i].x + kids[i].cx;
      if (kids[i].y + kids[i].cy > maxy) maxy = kids[i].y + kids[i].cy;
    }
    if (!(im->kids = (Image *)malloc((size_t)nk * sizeof(Image)))) { for (i = 0; i < nk; i++) img_release(&kids[i]); r->oom = 1; return 0; }
    for (i = 0; i < nk; i++) { im->kids[i] = kids[i]; im->kids[i].x -= minx; im->kids[i].y -= miny; }
    im->nkids = nk;
    im->cx = im->chw = maxx - minx; im->cy = im->chh = maxy - miny;
    if (im->wrap != WRAP_INLINE) { im->x = minx; im->y = miny; }
    return 1;
  }
  memset(&l, 0, sizeof l);
  if (!x->pending_end) while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "enhanced-geometry")) {
      const char *t = xml_attr(x, "type");
      if (strcmp(el, "line")) im->shape = (u8)shape_kind(t);
      if (attr_is(x, "mirror-horizontal", "true")) im->flip |= 1;
      if (attr_is(x, "mirror-vertical", "true")) im->flip |= 2;
      skip(x);
    } else if (!strcmp(x->name, "p") || !strcmp(x->name, "h")) {
      /* the shape's text, a paragraph at a time */
      PB pb, *save = r->pb;
      ParaFmt pf; CharFmt cf; u32 own; int si;
      r->depth++;
      para_style(r, xml_attr(x, "style-name"), &pf, &cf, &own, &si);
      pb_open(r, &pb, &l, &pf, &cf);
      r->pb = &pb;
      inline_children(x, r, cf);
      pb_close(r, &pb);
      r->depth--;
      r->pb = save;
    } else if (!strcmp(x->name, "title") || !strcmp(x->name, "desc")) {
      Buf t;
      int d1 = x->depth;
      buf_init(&t);
      if (!x->pending_end) while (xml_next(x) != XT_EOF) { if (x->type == XT_END && x->depth < d1) break; if (x->type == XT_TEXT) buf_add(&t, x->text.data, x->text.len); }
      else xml_next(x);
      if (t.data) cpy(im->alt, sizeof im->alt, t.data);
      buf_free(&t);
    } else skip(x);
  } else xml_next(x);
  if (l.np) {
    if ((im->text = (Story *)calloc(1, sizeof(Story)))) pl_to_story(&l, im->text);
    else pl_free(&l);
  }
  if (im->cx <= 0) im->cx = 720;
  if (im->cy <= 0 && im->shape != SH_LINE) im->cy = 720;
  return 1;
}

/* ---- notes, comments, changes */
static void read_note(Xml *x, R *r, const CharFmt *cf)
{
  const char *cls = xml_attr(x, "note-class");
  int end = cls && !strcmp(cls, "endnote"), d0 = x->depth, k;
  char ref[8];
  ParaList l;
  memset(&l, 0, sizeof l);
  if (!x->pending_end) while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "note-body")) {
      Blk b;
      PB *save = r->pb;
      int ins = r->ins_rid, del = r->in_deletion, nb = r->nbmk;
      memset(&b, 0, sizeof b);
      b.flat = 1; b.story = 1;
      r->ins_rid = 0; r->in_deletion = 0; r->nbmk = 0;
      r->depth++;
      blocks(x, r, &l, &b);
      r->depth--;
      r->pb = save; r->ins_rid = ins; r->in_deletion = del; r->nbmk = nb;
    } else skip(x);
  } else xml_next(x);
  if (l.np && l.p[0]->len && l.p[0]->t[0] == ' ') {
    /* the space after the number (Word's, and Quill's when it writes) is the number's, not the text's */
    Para *p = l.p[0];
    u32 j;
    memmove(p->t, p->t + 1, p->len);
    p->len--;
    for (j = 0; j < p->nr; j++) if (p->r[j].end) p->r[j].end--;
    para_normalise(p);
    p->ver++;
  }
  k = end ? doc_add_endnote(r->d) : doc_add_note(r->d);
  if (k < 0) { pl_free(&l); r->oom = 1; return; }
  pl_to_story(&l, end ? &r->d->enotes[k] : &r->d->notes[k]);
  if (end) enote_utf8(ref, k); else note_utf8(ref, k);
  put_special(r, ref, 4, cf);
}
static void read_annotation(Xml *x, R *r, const CharFmt *cf)
{
  char author[64], initials[12], date[24], name[80];
  int d0 = x->depth, k;
  char mark[8];
  ParaList l;
  author[0] = initials[0] = date[0] = 0;
  cpy(name, sizeof name, xml_attr(x, "name"));
  memset(&l, 0, sizeof l);
  if (!x->pending_end) while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "creator") || !strcmp(x->name, "date") || !strcmp(x->name, "creator-initials") || !strcmp(x->name, "sender-initials")) {
      Buf t;
      int d1 = x->depth;
      char el[24];
      cpy(el, sizeof el, x->name);
      buf_init(&t);
      if (!x->pending_end) while (xml_next(x) != XT_EOF) { if (x->type == XT_END && x->depth < d1) break; if (x->type == XT_TEXT) buf_add(&t, x->text.data, x->text.len); }
      else xml_next(x);
      if (t.data) {
        if (!strcmp(el, "creator")) cpy(author, sizeof author, t.data);
        else if (!strcmp(el, "date")) cpy(date, sizeof date, t.data);
        else cpy(initials, sizeof initials, t.data);
      }
      buf_free(&t);
    } else if (!strcmp(x->name, "p") || !strcmp(x->name, "h") || !strcmp(x->name, "list")) {
      /* the comment's text: its paragraphs, one at a time */
      PB pb, *save = r->pb;
      ParaFmt pf; CharFmt pcf; u32 own; int si;
      int ins = r->ins_rid, del = r->in_deletion, nb = r->nbmk;
      if (!strcmp(x->name, "list")) { skip(x); continue; }
      r->ins_rid = 0; r->in_deletion = 0; r->nbmk = 0;
      para_style(r, xml_attr(x, "style-name"), &pf, &pcf, &own, &si);
      pb_open(r, &pb, &l, &pf, &pcf);
      r->pb = &pb;
      r->depth++;
      inline_children(x, r, pcf);
      r->depth--;
      pb_close(r, &pb);
      r->pb = save; r->ins_rid = ins; r->in_deletion = del; r->nbmk = nb;
    } else skip(x);
  } else xml_next(x);
  /* a date as Word keeps it (no fraction of a second) */
  { char *dot = strchr(date, '.'); if (dot) { *dot = 0; } if (date[0] && !strchr(date, 'Z') && strlen(date) + 1 < sizeof date) strcat(date, "Z"); }
  if (!author[0]) cpy(author, sizeof author, "Unknown");
  k = doc_add_comment(r->d, author, initials, date);
  if (k < 0) { pl_free(&l); r->oom = 1; return; }
  if (attr_is(x, "resolved", "true")) r->d->comments[k].done = 1;
  pl_to_story(&l, &r->d->comments[k].text);
  cmark_utf8(mark, k, 0);
  put_special(r, mark, 4, cf);
  if (name[0]) {
    OAnno *na = (OAnno *)realloc(r->an, (size_t)(r->nan + 1) * sizeof(OAnno));
    if (na) { r->an = na; cpy(r->an[r->nan].name, sizeof r->an[r->nan].name, name); r->an[r->nan].k = k; r->nan++; }
  } else {
    cmark_utf8(mark, k, 1);
    put_special(r, mark, 4, cf);
  }
}
static void read_changes(Xml *x, R *r)
{
  int d0 = x->depth;
  if (x->pending_end) { xml_next(x); return; }
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type == XT_START && !strcmp(x->name, "changed-region")) {
      OChange c;
      int d1 = x->depth;
      const char *id = xml_attr(x, "id");
      memset(&c, 0, sizeof c);
      cpy(c.id, sizeof c.id, id);
      c.type = 9;
      if (!x->pending_end) while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type != XT_START) continue;
        if (!strcmp(x->name, "insertion") || !strcmp(x->name, "deletion") || !strcmp(x->name, "format-change")) {
          int d2 = x->depth;
          char author[64], date[24];
          const char *bs = NULL, *be = NULL;
          c.type = (u8)(x->name[0] == 'i' ? 0 : x->name[0] == 'd' ? 1 : 2);
          author[0] = date[0] = 0;
          if (!x->pending_end) while (xml_next(x) != XT_EOF) {
            if (x->type == XT_END && x->depth < d2) break;
            if (x->type != XT_START) continue;
            if (!strcmp(x->name, "change-info")) {
              int d3 = x->depth;
              if (!x->pending_end) while (xml_next(x) != XT_EOF) {
                if (x->type == XT_END && x->depth < d3) break;
                if (x->type == XT_START && (!strcmp(x->name, "creator") || !strcmp(x->name, "date"))) {
                  Buf t;
                  int d4 = x->depth, isc = x->name[0] == 'c';
                  buf_init(&t);
                  if (!x->pending_end) while (xml_next(x) != XT_EOF) { if (x->type == XT_END && x->depth < d4) break; if (x->type == XT_TEXT) buf_add(&t, x->text.data, x->text.len); }
                  else xml_next(x);
                  if (t.data) cpy(isc ? author : date, isc ? sizeof author : sizeof date, t.data);
                  buf_free(&t);
                } else if (x->type == XT_START) skip(x);
              } else xml_next(x);
            } else {
              /* the deleted content: kept as XML, read where text:change puts it */
              const char *st;
              size_t len = xml_raw(x, &st);
              if (!bs) bs = st;
              be = st + len;
            }
          } else xml_next(x);
          { char *dot = strchr(date, '.'); if (dot) *dot = 0; if (date[0] && !strchr(date, 'Z') && strlen(date) + 1 < sizeof date) strcat(date, "Z"); }
          c.rid = doc_add_rev(r->d, author[0] ? author : "Unknown", date);
          if (c.rid < 0) c.rid = 0;
          if (bs && be > bs) { c.rawn = (size_t)(be - bs); c.raw = str_ndup(bs, c.rawn); }
        } else skip(x);
      } else xml_next(x);
      if (c.type != 9) {
        OChange *nc = (OChange *)realloc(r->chg, (size_t)(r->nchg + 1) * sizeof(OChange));
        if (!nc) { free(c.raw); r->oom = 1; continue; }
        r->chg = nc; r->chg[r->nchg++] = c;
      }
    } else if (x->type == XT_START) skip(x);
  }
}
static int find_change(R *r, const char *id)
{
  int i;
  if (!id) return -1;
  for (i = 0; i < r->nchg; i++) if (!strcmp(r->chg[i].id, id)) return i;
  return -1;
}
/* a deletion's text, where text:change puts it: its first paragraph goes on in this one, each other
   one starts a new paragraph (with this one's formatting), as the deletion shows */
static void put_deletion(R *r, OChange *c, const CharFmt *cf)
{
  Xml x;
  int first = 1;
  if (!c->raw || !r->pb || !r->pb->p) return;
  xml_init(&x, c->raw, c->rawn);
  r->in_deletion = c->rid + 1;
  while (xml_next(&x) != XT_EOF) {
    if (x.type != XT_START) continue;
    if (!strcmp(x.name, "p") || !strcmp(x.name, "h")) {
      CharFmt f = *cf;
      if (!first) {
        PB *pb = r->pb;
        ParaFmt pf = pb->pf;
        pb_close(r, pb);
        pf.pagebreak = 0; pf.sect = 0;
        pb_open(r, pb, pb->l, &pf, cf);
      }
      first = 0;
      inline_children(&x, r, f);
    }
  }
  xml_free(&x);
  r->in_deletion = 0;
}

/* ---- fields and text inside paragraphs */
static void bookmark_start(R *r, const char *name)
{
  int k;
  if (!name || !name[0] || r->nbmk >= 16) return;
  if ((k = doc_bmark(r->d, name)) <= 0) return;
  cpy(r->bmkname[r->nbmk], sizeof r->bmkname[0], name);
  r->bmk[r->nbmk++] = (u16)k;
}
static void bookmark_end(R *r, const char *name)
{
  int i, j;
  if (!name) return;
  for (i = r->nbmk - 1; i >= 0; i--) if (!strcmp(r->bmkname[i], name)) {
    for (j = i; j + 1 < r->nbmk; j++) { r->bmk[j] = r->bmk[j + 1]; memcpy(r->bmkname[j], r->bmkname[j + 1], sizeof r->bmkname[0]); }
    r->nbmk--;
    return;
  }
}
/* the text of an element (a field's result), read to its END, into the paragraph as cf */
static void field_text(Xml *x, R *r, CharFmt cf) { inline_children(x, r, cf); }

static void inline_elem(Xml *x, R *r, CharFmt cf)
{
  const char *n = x->name;
  if (!strcmp(n, "s")) {
    const char *c = xml_attr(x, "c");
    int k = c ? atoi(c) : 1;
    char sp[64];
    if (k < 1) k = 1;
    if (k > 1000) k = 1000;
    while (k > 0) { int m = k > 64 ? 64 : k; memset(sp, ' ', (size_t)m); put_special(r, sp, (size_t)m, &cf); k -= m; }
    skip(x);
  } else if (!strcmp(n, "tab")) { put_special(r, "\t", 1, &cf); skip(x); }
  else if (!strcmp(n, "line-break")) { put_special(r, "\v", 1, &cf); skip(x); }
  else if (!strcmp(n, "soft-page-break") || !strcmp(n, "toc-mark") || !strcmp(n, "toc-mark-start") || !strcmp(n, "toc-mark-end") ||
           !strcmp(n, "alphabetical-index-mark") || !strcmp(n, "alphabetical-index-mark-start") || !strcmp(n, "alphabetical-index-mark-end") ||
           !strcmp(n, "user-index-mark") || !strcmp(n, "user-index-mark-start") || !strcmp(n, "user-index-mark-end") ||
           !strcmp(n, "reference-mark") || !strcmp(n, "reference-mark-start") || !strcmp(n, "reference-mark-end") ||
           !strcmp(n, "bibliography-mark") || !strcmp(n, "hidden-text") || !strcmp(n, "hidden-paragraph") || !strcmp(n, "note-citation") ||
           !strcmp(n, "variable-decls") || !strcmp(n, "sequence-decls") || !strcmp(n, "user-field-decls")) skip(x);
  else if (!strcmp(n, "span")) {
    const char *cl = xml_attr(x, "class-names"), *sn = xml_attr(x, "style-name");
    if (cl) {
      /* several styles, in order */
      char one[64];
      while (*cl) {
        size_t k = 0;
        while (*cl == ' ') cl++;
        while (cl[k] && cl[k] != ' ' && k + 1 < sizeof one) { one[k] = cl[k]; k++; }
        one[k] = 0; cl += k;
        if (one[0]) span_style(r, one, &cf);
      }
    }
    if (sn) span_style(r, sn, &cf);
    inline_children(x, r, cf);
  } else if (!strcmp(n, "a")) {
    const char *href = xml_attr(x, "href"), *sn = xml_attr(x, "style-name");
    if (sn) span_style(r, sn, &cf);
    if (href && href[0]) { int k = doc_link(r->d, href); if (k > 0) cf.link = (u16)k; }
    inline_children(x, r, cf);
  } else if (!strcmp(n, "bookmark-start")) { bookmark_start(r, xml_attr(x, "name")); skip(x); }
  else if (!strcmp(n, "bookmark-end")) { bookmark_end(r, xml_attr(x, "name")); skip(x); }
  else if (!strcmp(n, "bookmark")) skip(x);      /* (a point: Quill's bookmarks cover text) */
  else if (!strcmp(n, "note")) read_note(x, r, &cf);
  else if (!strcmp(n, "annotation")) read_annotation(x, r, &cf);
  else if (!strcmp(n, "annotation-end")) {
    const char *nm = xml_attr(x, "name");
    int i;
    for (i = 0; nm && i < r->nan; i++) if (!strcmp(r->an[i].name, nm)) {
      char mark[8];
      cmark_utf8(mark, r->an[i].k, 1);
      put_special(r, mark, 4, &cf);
      r->an[i].name[0] = 0;
      break;
    }
    skip(x);
  } else if (!strcmp(n, "change-start")) {
    int c = find_change(r, xml_attr(x, "change-id"));
    if (c >= 0 && r->chg[c].type == 0) r->ins_rid = r->chg[c].rid + 1;
    skip(x);
  } else if (!strcmp(n, "change-end")) { r->ins_rid = 0; skip(x); }
  else if (!strcmp(n, "change")) {
    int c = find_change(r, xml_attr(x, "change-id"));
    skip(x);
    if (c >= 0 && r->chg[c].type == 1 && !r->in_deletion) put_deletion(r, &r->chg[c], &cf);
  } else if (!strcmp(n, "page-number")) {
    const char *sel = xml_attr(x, "select-page");
    if (sel && strcmp(sel, "current")) field_text(x, r, cf);       /* (the next or previous page's: its number as text) */
    else { skip(x); put_special(r, FIELD_PAGE, 3, &cf); }
  } else if (!strcmp(n, "page-count")) { skip(x); put_special(r, FIELD_PAGES, 3, &cf); }
  else if (!strcmp(n, "bookmark-ref") || !strcmp(n, "reference-ref")) {
    const char *fmt = xml_attr(x, "reference-format"), *rn = xml_attr(x, "ref-name");
    if (fmt && !strcmp(fmt, "page") && rn && !strcmp(n, "bookmark-ref")) {
      char url[96];
      int k;
      snprintf(url, sizeof url, "#%s", rn);
      skip(x);
      if ((k = doc_link(r->d, url)) > 0) cf.link = (u16)k;
      put_special(r, FIELD_PAGEREF, 3, &cf);
    } else field_text(x, r, cf);
  } else if (!strcmp(n, "frame")) read_frame(x, r, &cf);
  else if (!strcmp(n, "custom-shape") || !strcmp(n, "rect") || !strcmp(n, "ellipse") || !strcmp(n, "circle") || !strcmp(n, "line") ||
           !strcmp(n, "polygon") || !strcmp(n, "polyline") || !strcmp(n, "path") || !strcmp(n, "g")) {
    Image im;
    if (read_shape(x, r, &im)) obj_put(r, &im, &cf);
  } else if (!strcmp(n, "a") || !strcmp(n, "ruby-text") || !strcmp(n, "annotation-xml")) skip(x);
  else if (!strcmp(n, "ruby")) {
    int d0 = x->depth;
    if (!x->pending_end) while (xml_next(x) != XT_EOF) {
      if (x->type == XT_END && x->depth < d0) break;
      if (x->type == XT_START && !strcmp(x->name, "ruby-base")) inline_children(x, r, cf);
      else if (x->type == XT_START) skip(x);
    } else xml_next(x);
  } else if (!strcmp(n, "list") || !strcmp(n, "table") || !strcmp(n, "p") || !strcmp(n, "h")) {
    /* (blocks inside a paragraph, from some writers: their text goes on here) */
    inline_children(x, r, cf);
  } else {
    /* date, time, author, title, file name, sequence, chapter, meta... and anything else: what it shows */
    field_text(x, r, cf);
  }
}
static void inline_children(Xml *x, R *r, CharFmt cf)
{
  int d0 = x->depth;
  if (x->pending_end) { xml_next(x); return; }
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type == XT_TEXT) put_text(r, x->text.data, x->text.len, &cf);
    else if (x->type == XT_START) inline_elem(x, r, cf);
  }
}

/* ---- blocks */
static int list_level_of(const Blk *b) { return b->level < 0 ? 0 : b->level > 8 ? 8 : b->level; }
/* the Quill list the paragraphs of a list being opened use (made at its first paragraph) */
static int list_number(R *r, Blk *b, int oli)
{
  OList *o;
  int k;
  if (oli < 0) {
    /* no list style: a plain bulleted list */
    ListDef def;
    int i;
    memset(&def, 0, sizeof def);
    for (i = 0; i < 9; i++) { def.lvl[i].fmt = NF_BULLET; def.lvl[i].start = 1; strcpy(def.lvl[i].text, "\xE2\x80\xA2"); def.lvl[i].left = 720 * (i + 1); def.lvl[i].hanging = 360; }
    return doc_add_list(r->d, &def);
  }
  o = &r->ol[oli];
  if (b->listcont && o->last) return o->last;
  if (b->listcont == 2) {
    /* text:continue-list named a list: that one's */
    int i;
    for (i = 0; i < r->nlid; i++) if (!strcmp(r->lid[i].id, b->listid)) return r->lid[i].list;
  }
  o->def.id = (u16)(r->d->nlists + 1);
  k = doc_add_list(r->d, &o->def);
  if (k) r->ol[oli].last = k;
  return k;
}
static void note_listid(R *r, const char *id, int list)
{
  void *n;
  if (!id || !id[0] || !list) return;
  if (!(n = realloc(r->lid, (size_t)(r->nlid + 1) * sizeof *r->lid))) return;
  r->lid = n;
  cpy(r->lid[r->nlid].id, sizeof r->lid[0].id, id);
  r->lid[r->nlid].list = list;
  r->nlid++;
}
static void close_section(R *r, ParaList *l, int newstart);
/* a paragraph or heading (x at its START) */
static void read_para(Xml *x, R *r, ParaList *l, Blk *b, int heading)
{
  ParaFmt pf;
  CharFmt cf;
  u32 own;
  int si, k;
  PB pb, *save = r->pb;
  const OStyle *s;
  para_style(r, xml_attr(x, "style-name"), &pf, &cf, &own, &si);
  s = si >= 0 ? &r->st[si] : NULL;
  /* a master page on the paragraph (its style or the one that is based on) starts a section */
  if (!b->story && !b->cell && s) {
    const char *m = s->master;
    if (!m[0] && s->autom && s->parent[0]) { int pi = find_style(r, s->parent, F_PARA); if (pi >= 0) m = r->st[pi].master; }
    if (m[0] && strcmp(m, "\001none")) {
      if (r->sect_paras) close_section(r, l, SB_NEXT);
      else if (l->np) r->sect_start = SB_NEXT;      /* (a section just begun, by text:section, starts a page) */
      cpy(r->sect_master, sizeof r->sect_master, m);
      pf.pagebreak = 0;
    }
    if (s->pgnum > 0 && !r->first_para_done) r->d->pgstart = (u16)s->pgnum;
  }
  if (r->pending_break == 1) pf.pagebreak = 1;
  else if (r->pending_break == 2) pf.colbreak = 1;
  r->pending_break = 0;
  if (s && (s->pa & A_BRKAFTER)) r->pending_break = s->brkafter;
  if (heading && !fmt_heading_level(r->d, pf.style)) {
    /* a heading in a style that isn't one: the heading style of its level */
    const char *lv = xml_attr(x, "outline-level");
    int lvl = lv ? atoi(lv) : 1, hs;
    if (lvl >= 1 && lvl <= 9 && pf.style == 0 && (hs = fmt_heading_style(r->d, lvl)) >= 0) {
      pf = r->d->styles[hs].pf; pf.style = (u8)hs; pf.list = 0; cf = r->d->styles[hs].cf;
    }
  }
  if (b->listnew || b->list) {
    if (b->listnew) {
      int oli = b->listnew - 1;
      if (oli < 0 || oli >= r->nol) {
        /* (the list's style from the paragraph's) */
        const char *ln = s ? s->list : "";
        if (!ln[0] && s && s->autom && s->parent[0]) { int pi = find_style(r, s->parent, F_PARA); if (pi >= 0) ln = r->st[pi].list; }
        oli = find_list(r, ln);
      }
      b->list = list_number(r, b, oli);
      note_listid(r, b->listid, b->list);
      b->listnew = 0;
    }
    if (b->restart > 0 && b->list) {
      /* a list item that starts again at a number: a list of its own from here */
      ListDef def = r->d->lists[b->list - 1];
      int nl;
      def.lvl[list_level_of(b)].start = (u16)b->restart;
      def.id = (u16)(r->d->nlists + 1);
      if ((nl = doc_add_list(r->d, &def))) b->list = nl;
      b->restart = 0;
    }
    if (b->list) {
      const ListLevel *lv = &r->d->lists[b->list - 1].lvl[list_level_of(b)];
      if (!b->itemdone) {
        pf.list = (u16)b->list; pf.level = (u8)list_level_of(b);
        if (!(own & (A_LEFT | A_FIRST))) { pf.left = lv->left; pf.first = -lv->hanging; }
      } else if (!(own & (A_LEFT | A_FIRST))) { pf.left = lv->left; pf.first = 0; }
      b->itemdone = 1;
    }
  }
  if (b->cell) pf.cell = *b->cell; else memset(&pf.cell, 0, sizeof pf.cell);
  if (b->outer) pf.outer = *b->outer; else memset(&pf.outer, 0, sizeof pf.outer);
  pf.sect = 0;
  pb_open(r, &pb, l, &pf, &cf);
  r->pb = &pb;
  /* objects anchored to the page before it */
  if (r->npend_obj && !b->story) {
    for (k = 0; k < r->npend_obj; k++) obj_put(r, &r->pend_obj[k], &cf);
    r->npend_obj = 0;
  }
  inline_children(x, r, cf);
  if (pb.p && r->d->nmaths) {
    /* equations: shown as display ones only alone in their paragraph (LibreOffice marks every
       formula object a block) */
    Para *pp = pb.p;
    u32 j;
    int other = 0;
    for (j = 0; j < pp->len; ) {
      if (j + 4 <= pp->len && IS_MATH(pp->t + j)) { j += 4; continue; }
      if (pp->t[j] != ' ') { other = 1; break; }
      j++;
    }
    if (other) for (j = 0; j + 4 <= pp->len; j++) if (IS_MATH(pp->t + j)) { MathEq *e = doc_math(r->d, math_index(pp->t + j)); if (e) e->display = 0; j += 3; }
  }
  pb_close(r, r->pb);
  r->pb = save;
  if (!b->story) { r->sect_paras++; r->first_para_done = 1; }
}

/* ---- tables */
typedef struct {
  int num;                  /* its Table number (0: flattened) */
  int ncols;
  int row;
  u8 *busy;                 /* rows x MAX_COLS: covered by a cell spanning rows from above (its first column + 1) */
  int brows;
  u8 (*spanof)[MAX_COLS];   /* the span of the cell that covers it (per row and column) */
  int header;
} TState;
static int tbusy_grow(TState *t, int rows)
{
  if (rows <= t->brows) return 1;
  {
    int nr = rows + 16;
    u8 *nb = (u8 *)realloc(t->busy, (size_t)nr * MAX_COLS);
    u8 (*ns)[MAX_COLS] = realloc(t->spanof, (size_t)nr * MAX_COLS);
    if (nb) t->busy = nb;
    if (ns) t->spanof = ns;
    if (!nb || !ns) return 0;
    memset(t->busy + (size_t)t->brows * MAX_COLS, 0, (size_t)(nr - t->brows) * MAX_COLS);
    memset(t->spanof[t->brows], 0, (size_t)(nr - t->brows) * MAX_COLS);
    t->brows = nr;
  }
  return 1;
}
static void read_table(Xml *x, R *r, ParaList *l, Blk *b);
static void read_rows(Xml *x, R *r, ParaList *l, Blk *b, TState *t, int header);
static void read_row(Xml *x, R *r, ParaList *l, Blk *b, TState *t, int header)
{
  int i = find_style(r, xml_attr(x, "style-name"), F_ROW), d0 = x->depth, col = 0;
  i32 rowh = 0;
  u8 cant = 0;
  int rr;
  if (i >= 0 && r->st[i].hasrowh) rowh = r->st[i].rowexact ? -r->st[i].rowh : r->st[i].rowh;
  if (i >= 0) cant = r->st[i].cantsplit;
  if (rowh > 32767) rowh = 32767;
  if (rowh < -32767) rowh = -32767;
  rr = t->row;
  if (!tbusy_grow(t, rr + 2)) { r->oom = 1; skip(x); return; }
  if (x->pending_end) { xml_next(x); t->row++; return; }
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "table-cell") || !strcmp(x->name, "covered-table-cell")) {
      int covered = x->name[0] == 'c';
      const char *cs = xml_attr(x, "number-columns-spanned"), *rs = xml_attr(x, "number-rows-spanned"), *crep = xml_attr(x, "number-columns-repeated");
      int span = cs ? atoi(cs) : 1, rows = rs ? atoi(rs) : 1, nrep = crep ? atoi(crep) : 1, ci = find_style(r, xml_attr(x, "style-name"), F_CELL), j;
      if (span < 1) span = 1;
      if (rows < 1) rows = 1;
      if (nrep < 1) nrep = 1;
      if (nrep > MAX_COLS) nrep = MAX_COLS;
      if (covered) {
        /* a cell under one spanning rows from above: an empty continuation cell of its kind */
        if (col < MAX_COLS && t->busy[(size_t)rr * MAX_COLS + col] && t->num) {
          int first = t->busy[(size_t)rr * MAX_COLS + col] - 1;
          if (first == col) {
            Cell c;
            ParaFmt pf;
            CharFmt cf;
            Para *p;
            const Cell *above = NULL;
            u32 k;
            memset(&c, 0, sizeof c);
            /* the cell it continues: the paragraph above in this column */
            for (k = l->np; k > 0; k--) { const Cell *q = &l->p[k - 1]->pf.cell; if (q->table == t->num && q->col == col && q->row < rr) { above = q; break; } }
            if (above) c = *above;
            c.table = (u16)t->num; c.row = (u16)rr; c.col = (u8)col; c.span = t->spanof[rr][col] ? t->spanof[rr][col] : 1;
            c.vmerge = VM_CONT;
            c.header = (u8)header; c.cantsplit = cant; c.rowh = (i16)rowh;
            {
              /* its own paragraphs, if it has them (Quill writes them), else an empty one */
              Blk cb = *b;
              u32 before = l->np;
              cb.cell = &c; cb.outer = b->cell; cb.list = 0; cb.listnew = 0; cb.level = 0;
              blocks(x, r, l, &cb);
              if (l->np == before) {
                pf = r->d->styles[0].pf; cf = r->d->styles[0].cf;
                pf.list = 0; pf.cell = c;
                if (b->cell) pf.outer = *b->cell; else memset(&pf.outer, 0, sizeof pf.outer);
                if ((p = para_new(&pf, &cf)) && !pl_add(l, p)) r->oom = 1;
                if (!p) r->oom = 1;
              }
            }
            col += nrep;
            continue;
          }
        }
        skip(x);
        col += nrep;               /* (a covered cell takes its grid column, as every cell does) */
        continue;
      }
      for (j = 0; j < nrep; j++) {
        Cell c;
        const OStyle *cs2 = ci >= 0 ? &r->st[ci] : NULL;
        Blk cb = *b;
        u32 before = l->np;
        int k2;
        if (col >= MAX_COLS) col = MAX_COLS - 1;
        memset(&c, 0, sizeof c);
        c.table = (u16)t->num; c.row = (u16)rr; c.col = (u8)col;
        c.span = (u8)(col + span > t->ncols ? (t->ncols - col > 0 ? t->ncols - col : 1) : span);
        c.vmerge = rows > 1 ? VM_START : VM_NONE;
        c.header = (u8)header; c.cantsplit = cant; c.rowh = (i16)rowh;
        c.shade = COLOR_AUTO;
        for (k2 = 0; k2 < 4; k2++) c.bcolor[k2] = COLOR_AUTO;
        if (cs2) {
          if (cs2->bset) { c.borders = cs2->cell.borders; memcpy(c.bstyle, cs2->cell.bstyle, 4); memcpy(c.bwidth, cs2->cell.bwidth, 4); memcpy(c.bcolor, cs2->cell.bcolor, sizeof c.bcolor); }
          if (cs2->shadeset) c.shade = cs2->cell.shade;
          if (cs2->valignset) c.valign = cs2->cell.valign;
          if (cs2->qlook && t->num && r->d->tables[t->num - 1].tstyle) { c.borders = 0; c.shade = COLOR_AUTO; for (k2 = 0; k2 < 4; k2++) { c.bstyle[k2] = 0; c.bwidth[k2] = 0; c.bcolor[k2] = COLOR_AUTO; } }
          if (t->num && rr == 0 && col == 0 && (cs2->padset & 2)) {
            Table *tb = &r->d->tables[t->num - 1];
            tb->marl = cs2->pad[1]; tb->marr = cs2->padset & 8 ? cs2->pad[3] : cs2->pad[1];
            if (cs2->padset & 1) tb->mart = cs2->pad[0];
            if (cs2->padset & 4) tb->marb = cs2->pad[2];
          }
        }
        if (rows > 1 && tbusy_grow(t, rr + rows + 1)) {
          int y, xx;
          for (y = rr + 1; y < rr + rows; y++) for (xx = col; xx < col + c.span && xx < MAX_COLS; xx++) { t->busy[(size_t)y * MAX_COLS + xx] = (u8)(col + 1); t->spanof[y][xx] = c.span; }
        }
        if (t->num) { cb.cell = &c; cb.outer = b->cell; if (b->cell) cb.outer = b->cell; }
        cb.list = 0; cb.listnew = 0; cb.level = 0;
        if (j == 0) blocks(x, r, l, &cb);       /* (a repeated cell: its content once, the copies empty) */
        if (l->np == before) {
          /* an empty cell still has a paragraph */
          ParaFmt pf = r->d->styles[0].pf; CharFmt cf = r->d->styles[0].cf; Para *p;
          pf.list = 0;
          if (t->num) { pf.cell = c; if (b->cell) pf.outer = *b->cell; } else { memset(&pf.cell, 0, sizeof pf.cell); if (b->cell) pf.cell = *b->cell; if (b->outer) pf.outer = *b->outer; }
          if ((p = para_new(&pf, &cf)) && !pl_add(l, p)) r->oom = 1;
        }
        col++;                     /* (the columns it spans come as covered cells) */
      }
    } else skip(x);
  }
  t->row++;
}
static void read_rows(Xml *x, R *r, ParaList *l, Blk *b, TState *t, int header)
{
  int d0 = x->depth;
  if (x->pending_end) { xml_next(x); return; }
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "table-row")) read_row(x, r, l, b, t, header);
    else if (!strcmp(x->name, "table-header-rows")) read_rows(x, r, l, b, t, 1);
    else if (!strcmp(x->name, "table-rows") || !strcmp(x->name, "table-row-group")) read_rows(x, r, l, b, t, header);
    else skip(x);
  }
}
static void read_table(Xml *x, R *r, ParaList *l, Blk *b)
{
  TState t;
  Table tb;
  i32 w[MAX_COLS], rel[MAX_COLS], width = 0;
  int ncols = 0, ts = find_style(r, xml_attr(x, "style-name"), F_TABLE), d0 = x->depth, k, flat;
  const char *tok;
  memset(&t, 0, sizeof t);
  memset(&tb, 0, sizeof tb);
  flat = b->flat || (b->cell && b->outer);
  if (flat && !b->flat) r->lost |= LOST_TABLES;
  if (x->pending_end) { xml_next(x); return; }
  /* columns first (they come before the rows) */
  tok = NULL;
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) { tok = "end"; break; }
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "table-column") || !strcmp(x->name, "table-columns") || !strcmp(x->name, "table-header-columns") || !strcmp(x->name, "table-column-group")) {
      if (!strcmp(x->name, "table-column")) {
        const char *rp = xml_attr(x, "number-columns-repeated");
        int n = rp ? atoi(rp) : 1, ci = find_style(r, xml_attr(x, "style-name"), F_COL);
        if (n < 1) n = 1;
        while (n-- > 0 && ncols < MAX_COLS) { w[ncols] = ci >= 0 ? r->st[ci].colw : 0; rel[ncols] = ci >= 0 ? r->st[ci].relw : 0; ncols++; }
        skip(x);
      }
      /* (groups: their columns are read as they come) */
      continue;
    }
    break;
  }
  if (!ncols) { ncols = 1; w[0] = 0; rel[0] = 0; }
  if (ts >= 0) width = r->st[ts].width;
  if (width <= 0) width = fmt_text_width(r->d);
  {
    /* widths missing: from relative widths, else shared equally */
    long relsum = 0, fixed = 0;
    int missing = 0;
    for (k = 0; k < ncols; k++) { if (w[k] > 0) fixed += w[k]; else missing++; relsum += rel[k]; }
    if (missing) for (k = 0; k < ncols; k++) if (w[k] <= 0) w[k] = relsum > 0 && rel[k] > 0 ? (i32)((long long)width * rel[k] / relsum) : (i32)((width - fixed > 0 ? width - fixed : width) / missing);
    for (k = 0; k < ncols; k++) if (w[k] < 20) w[k] = 20;
  }
  if (!flat) {
    tb.ncols = (u8)ncols;
    for (k = 0; k < ncols; k++) tb.grid[k] = w[k];
    tb.look = LK_DEFAULT;
    if (ts >= 0) {
      const OStyle *s = &r->st[ts];
      tb.jc = s->hasalign ? s->talign : AL_LEFT;
      if (s->hasindent) tb.indent = s->tindent;
      if (s->tstyle[0]) {
        /* (Quill's own: the table's style, one of Quill's gallery) */
        int q = doc_tstyle(r->d, s->tstyle), g;
        for (g = 0; !q && g < tstyle_builtin_count(); g++) if (!strcmp(tstyle_builtin(g)->id, s->tstyle)) q = tstyle_use_builtin(r->d, g);
        tb.tstyle = (u8)q;
      }
      if (s->tlook) tb.look = s->tlook;
    }
    tb.marl = tb.marr = 0;
    if (!(t.num = doc_add_table(r->d, &tb))) { r->oom = 1; skip(x); return; }
  }
  t.ncols = ncols;
  if (!tbusy_grow(&t, 8)) { r->oom = 1; skip(x); return; }
  /* the element the column loop stopped at, then the rest */
  if (!tok) {
    if (!strcmp(x->name, "table-row")) read_row(x, r, l, b, &t, 0);
    else if (!strcmp(x->name, "table-header-rows")) read_rows(x, r, l, b, &t, 1);
    else if (!strcmp(x->name, "table-rows") || !strcmp(x->name, "table-row-group")) read_rows(x, r, l, b, &t, 0);
    else skip(x);
    while (xml_next(x) != XT_EOF) {
      if (x->type == XT_END && x->depth < d0) break;
      if (x->type != XT_START) continue;
      if (!strcmp(x->name, "table-row")) read_row(x, r, l, b, &t, 0);
      else if (!strcmp(x->name, "table-header-rows")) read_rows(x, r, l, b, &t, 1);
      else if (!strcmp(x->name, "table-rows") || !strcmp(x->name, "table-row-group")) read_rows(x, r, l, b, &t, 0);
      else skip(x);
    }
  }
  if (ts >= 0 && (r->st[ts].pa & A_PAGEBRK) && r->st[ts].pf.pagebreak) {
    /* (a page break before the table: on its first paragraph) */
    u32 i;
    for (i = 0; i < l->np; i++) if (t.num && l->p[i]->pf.cell.table == t.num) { l->p[i]->pf.pagebreak = 1; break; }
  }
  free(t.busy); free(t.spanof);
}

/* ---- sections */
static void sect_reset(R *r, int start)
{
  r->sect_start = start;
  r->sect_cols = 0;              /* (0: the master page's) */
  r->sect_colline = 0; r->sect_colgap = 720;
  r->sect_paras = 0;
}
static int find_master(R *r, const char *name)
{
  int i;
  /* (none named: the default one, Standard) */
  if (!name || !name[0]) { for (i = 0; i < r->nmp; i++) if (!strcmp(r->mp[i].name, "Standard")) return i; return r->nmp ? 0 : -1; }
  for (i = 0; i < r->nmp; i++) if (!strcmp(r->mp[i].name, name)) return i;
  return find_master(r, NULL);
}
static int find_layout(R *r, const char *name)
{
  int i;
  for (i = 0; name && i < r->npl; i++) if (!strcmp(r->pl[i].name, name)) return i;
  return -1;
}
/* the section ending with the last paragraph read is done (the next starts as newstart says) */
static void close_section(R *r, ParaList *l, int newstart)
{
  SectFmt f;
  int k, mi, li;
  int *nm;
  if (!l->np) { sect_reset(r, newstart); return; }
  sect_default(&f);
  f.start = (u8)r->sect_start;
  mi = find_master(r, r->sect_master);
  li = mi >= 0 ? find_layout(r, r->mp[mi].layout) : -1;
  if (r->sect_cols) { f.cols = r->sect_cols; f.colgap = r->sect_colgap; f.colline = r->sect_colline; }
  else if (li >= 0 && r->pl[li].cols > 1) { f.cols = r->pl[li].cols; f.colgap = r->pl[li].colgap; f.colline = r->pl[li].colline; }
  /* (its page setup comes from its master page at the end, with the headers' heights) */
  if ((k = doc_add_sect(r->d, &f))) {
    l->p[l->np - 1]->pf.sect = (u16)k;
    if ((nm = (int *)realloc(r->sect_mp, (size_t)k * sizeof(int)))) {
      int j;
      r->sect_mp = nm;
      for (j = r->nsect_mp; j < k; j++) r->sect_mp[j] = -1;
      r->sect_mp[k - 1] = mi;
      r->nsect_mp = k;
    }
  }
  sect_reset(r, newstart);
}
static void read_section(Xml *x, R *r, ParaList *l, Blk *b)
{
  int si = find_style(r, xml_attr(x, "style-name"), F_SECTION);
  u8 cols = 1, colline = 0;
  i32 colgap = 720;
  if (si >= 0 && r->st[si].cols) { cols = r->st[si].cols; colline = r->st[si].colline; colgap = r->st[si].colgap; }
  if (b->story || b->cell || r->depth) { blocks(x, r, l, b); return; }
  /* a section of its own: the text before ends one, this is one, and what follows starts another */
  if (r->sect_paras) close_section(r, l, SB_CONT);
  r->sect_cols = cols; r->sect_colline = colline; r->sect_colgap = colgap;
  blocks(x, r, l, b);
  if (r->sect_paras) close_section(r, l, SB_CONT);
}

static void blocks(Xml *x, R *r, ParaList *l, Blk *b)
{
  int d0 = x->depth;
  if (x->pending_end) { xml_next(x); return; }
  while (xml_next(x) != XT_EOF && !r->oom) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "p")) read_para(x, r, l, b, 0);
    else if (!strcmp(x->name, "h")) read_para(x, r, l, b, 1);
    else if (!strcmp(x->name, "list")) {
      Blk lb = *b;
      const char *sn = xml_attr(x, "style-name"), *cont = xml_attr(x, "continue-numbering"), *cl = xml_attr(x, "continue-list"), *id = xml_attr(x, "id");
      int d1 = x->depth;
      if (b->list || b->listnew) {
        /* a list inside a list item: the next level (its own style can't change the list) */
        lb.level = b->level + 1;
        lb.itemdone = 0;
      } else {
        int oli = find_list(r, sn);
        lb.level = 0;
        lb.listnew = oli >= 0 ? oli + 1 : r->nol + 1;   /* (r->nol + 1: none named; the paragraph's style says) */
        lb.listcont = cl && cl[0] ? 2 : cont && !strcmp(cont, "true") ? 1 : 0;
        cpy(lb.listid, sizeof lb.listid, cl && cl[0] ? cl : "");
        if (lb.listcont == 2) {
          int i;
          for (i = 0; i < r->nlid; i++) if (!strcmp(r->lid[i].id, cl)) { lb.list = r->lid[i].list; lb.listnew = 0; break; }
          if (lb.listnew) lb.listcont = 1;
        }
        if (id) { cpy(lb.listid, sizeof lb.listid, id); if (!lb.listnew) note_listid(r, id, lb.list); }
      }
      if (x->pending_end) { xml_next(x); continue; }
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type != XT_START) continue;
        if (!strcmp(x->name, "list-item") || !strcmp(x->name, "list-header")) {
          const char *sv = xml_attr(x, "start-value");
          Blk ib = lb;
          if (sv && x->name[5] == 'i') ib.restart = atoi(sv);
          ib.itemdone = x->name[5] == 'h';           /* (a header: in the list, no number) */
          blocks(x, r, l, &ib);
          /* what the items found out about the list carries on to the next */
          lb.list = ib.list; lb.listnew = ib.listnew;
        } else skip(x);
      }
      if (!b->list && !b->listnew && lb.list && id) note_listid(r, id, lb.list);
      if (b->listnew && lb.list) { b->list = lb.list; b->listnew = 0; }
    }
    else if (!strcmp(x->name, "table")) read_table(x, r, l, b);
    else if (!strcmp(x->name, "section")) read_section(x, r, l, b);
    else if (!strcmp(x->name, "tracked-changes")) read_changes(x, r);
    else if (!strcmp(x->name, "index-body") || !strcmp(x->name, "table-of-content") || !strcmp(x->name, "alphabetical-index") ||
             !strcmp(x->name, "illustration-index") || !strcmp(x->name, "table-index") || !strcmp(x->name, "object-index") ||
             !strcmp(x->name, "user-index") || !strcmp(x->name, "bibliography") || !strcmp(x->name, "index-title") ||
             !strcmp(x->name, "deletion") || !strcmp(x->name, "numbered-paragraph") || !strcmp(x->name, "text-content")) {
      if (!strcmp(x->name, "deletion")) skip(x);
      else blocks(x, r, l, b);
    } else if (!strcmp(x->name, "frame") || !strcmp(x->name, "custom-shape") || !strcmp(x->name, "rect") || !strcmp(x->name, "ellipse") ||
               !strcmp(x->name, "line") || !strcmp(x->name, "g")) {
      /* an object anchored to the page: it goes with the next paragraph */
      CharFmt cf = r->d->styles[0].cf;
      PB *save = r->pb;
      r->pb = NULL;
      if (!strcmp(x->name, "frame")) read_frame(x, r, &cf);
      else { Image im; if (read_shape(x, r, &im)) obj_put(r, &im, &cf); }
      r->pb = save;
    } else if (!strcmp(x->name, "soft-page-break")) skip(x);
    else skip(x);
  }
}

/* ---- styles.xml and content.xml's style parts */
static void read_font_faces(Xml *x, R *r)
{
  int d0 = x->depth;
  if (x->pending_end) { xml_next(x); return; }
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type == XT_START && !strcmp(x->name, "font-face")) {
      const char *nm = xml_attr(x, "name"), *fam = xml_attr(x, "font-family");
      void *n;
      if (nm && (n = realloc(r->ff, (size_t)(r->nff + 1) * sizeof *r->ff))) {
        char f[48];
        size_t k = 0;
        r->ff = n;
        if (!fam) fam = nm;
        while (*fam == '\'' || *fam == '"') fam++;
        while (fam[k] && fam[k] != '\'' && fam[k] != '"' && k + 1 < sizeof f) { f[k] = fam[k]; k++; }
        f[k] = 0;
        cpy(r->ff[r->nff].name, sizeof r->ff[0].name, nm);
        cpy(r->ff[r->nff].family, sizeof r->ff[0].family, f[0] ? f : nm);
        r->nff++;
      }
      skip(x);
    } else if (x->type == XT_START) skip(x);
  }
}
static void read_notes_config(Xml *x, R *r)
{
  const char *cls = xml_attr(x, "note-class"), *f = xml_attr(x, "num-format"), *sv = xml_attr(x, "start-value"), *at = xml_attr(x, "start-numbering-at"),
             *pos = xml_attr(x, "footnotes-position");
  int end = cls && !strcmp(cls, "endnote");
  NotePr *p = end ? &r->d->enpr : &r->d->fnpr;
  int fmt = !f ? -1 : !strcmp(f, "1") ? NNF_DECIMAL : !strcmp(f, "a") ? NNF_LOWER_LETTER : !strcmp(f, "A") ? NNF_UPPER_LETTER :
            !strcmp(f, "i") ? NNF_LOWER_ROMAN : !strcmp(f, "I") ? NNF_UPPER_ROMAN : NNF_SYMBOL;
  memset(p, 0, sizeof *p);
  if (fmt >= 0 && fmt != (end ? NNF_LOWER_ROMAN : NNF_DECIMAL)) { p->set |= NPS_FMT; p->fmt = (u8)fmt; }
  if (sv && atoi(sv) > 0) { p->set |= NPS_START; p->start = (u16)(atoi(sv) + 1); }
  if (at && !end && strcmp(at, "document")) { p->set |= NPS_RESTART; p->restart = (u8)(!strcmp(at, "page") ? NRS_PAGE : NRS_SECTION); }
  if (pos && !end && !strcmp(pos, "document")) { p->set |= NPS_POS; p->pos = NPOS_DOCEND; }
  skip(x);
}
static void read_page_layout(Xml *x, R *r)
{
  OLayout *pl, *np;
  int d0 = x->depth;
  if (!(np = (OLayout *)realloc(r->pl, (size_t)(r->npl + 1) * sizeof(OLayout)))) { r->oom = 1; skip(x); return; }
  r->pl = np;
  pl = &r->pl[r->npl++];
  memset(pl, 0, sizeof *pl);
  cpy(pl->name, sizeof pl->name, xml_attr(x, "name"));
  pl->page = r->d->page;
  pl->cols = 1; pl->colgap = 720; pl->numfmt = -1;
  pl->hmin = pl->fmin = -1;
  if (x->pending_end) { xml_next(x); return; }
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "page-layout-properties")) {
      int ok, d1 = x->depth, bset = 0;
      i32 n;
      const char *v;
      u8 bs[4], bw[4];
      u32 bc[4];
      n = attr_len(x, "page-width", &ok); if (ok) pl->page.width = n;
      n = attr_len(x, "page-height", &ok); if (ok) pl->page.height = n;
      n = attr_len(x, "margin-top", &ok); if (ok) pl->page.top = n;
      n = attr_len(x, "margin-bottom", &ok); if (ok) pl->page.bottom = n;
      n = attr_len(x, "margin-left", &ok); if (ok) pl->page.left = n;
      n = attr_len(x, "margin-right", &ok); if (ok) pl->page.right = n;
      if ((v = xml_attr(x, "num-format"))) pl->numfmt = nf_of(v);
      if ((v = xml_attr(x, "background-color")) && strcmp(v, "transparent")) pl->bg = HL_ON | hexrgb(v);
      memset(bs, 0, sizeof bs);
      border_attrs(x, bs, bw, bc, &bset, 4);
      if (bset && bs[0] != PB_NONE) {
        i32 pad[4];
        u8 ps = 0;
        memset(pad, 0, sizeof pad);
        padding_attrs(x, pad, &ps);
        pl->hasborder = 1;
        pl->border.style = bs[0]; pl->border.width = bw[0]; pl->border.color = bc[0];
        pl->border.space = pad[0];
      }
      if (x->pending_end) { xml_next(x); continue; }
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type == XT_START && !strcmp(x->name, "columns")) {
          OStyle tmp;
          memset(&tmp, 0, sizeof tmp);
          {
            /* (the same element as a section's) */
            const char *c = xml_attr(x, "column-count");
            int ok2, d2 = x->depth;
            i32 g = attr_len(x, "column-gap", &ok2);
            pl->cols = (u8)(c ? atoi(c) : 1);
            if (pl->cols < 1) pl->cols = 1;
            if (pl->cols > MAX_SECT_COLS) pl->cols = MAX_SECT_COLS;
            pl->colgap = ok2 ? g : 720;
            if (!x->pending_end) while (xml_next(x) != XT_EOF) {
              if (x->type == XT_END && x->depth < d2) break;
              if (x->type == XT_START && !strcmp(x->name, "column-sep")) { const char *st = xml_attr(x, "style"); pl->colline = (u8)(!st || strcmp(st, "none")); skip(x); }
              else if (x->type == XT_START) skip(x);
            } else xml_next(x);
          }
        } else if (x->type == XT_START) skip(x);
      }
    } else if (!strcmp(x->name, "header-style") || !strcmp(x->name, "footer-style")) {
      int hdr = x->name[0] == 'h', d1 = x->depth;
      if (x->pending_end) { xml_next(x); continue; }
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type == XT_START && !strcmp(x->name, "header-footer-properties")) {
          int ok;
          i32 h = attr_len(x, "min-height", &ok);
          if (!ok) h = attr_len(x, "height", &ok);
          if (!ok) h = 0;
          if (hdr) pl->hmin = h; else pl->fmin = h;
          skip(x);
        } else if (x->type == XT_START) skip(x);
      }
    } else skip(x);
  }
}
/* a master page: kept as XML, its headers and footers read once the styles are */
static void read_master(Xml *x, R *r)
{
  OMaster *m, *nm;
  const char *st;
  size_t len;
  if (!(nm = (OMaster *)realloc(r->mp, (size_t)(r->nmp + 1) * sizeof(OMaster)))) { r->oom = 1; skip(x); return; }
  r->mp = nm;
  m = &r->mp[r->nmp++];
  memset(m, 0, sizeof *m);
  cpy(m->name, sizeof m->name, xml_attr(x, "name"));
  cpy(m->layout, sizeof m->layout, xml_attr(x, "page-layout-name"));
  cpy(m->next, sizeof m->next, xml_attr(x, "next-style-name"));
  len = xml_raw(x, &st);
  m->xml = st; m->n = len;
}
static void read_master_stories(R *r, OMaster *m)
{
  Xml x;
  int part = r->part;
  if (m->read || !m->xml) return;
  m->read = 1;
  xml_init(&x, m->xml, m->n);
  r->part = r->flat ? 1 : 0;          /* (a Flat ODT has one set of automatic styles) */
  while (xml_next(&x) != XT_EOF) {
    int k = -1;
    if (x.type != XT_START) continue;
    if (!strcmp(x.name, "header")) k = HF_DEFAULT;
    else if (!strcmp(x.name, "header-first")) k = HF_FIRST;
    else if (!strcmp(x.name, "header-left")) k = HF_EVEN;
    else if (!strcmp(x.name, "footer")) k = HF_N + HF_DEFAULT;
    else if (!strcmp(x.name, "footer-first")) k = HF_N + HF_FIRST;
    else if (!strcmp(x.name, "footer-left")) k = HF_N + HF_EVEN;
    if (k >= 0) {
      ParaList l;
      Blk b;
      memset(&l, 0, sizeof l);
      memset(&b, 0, sizeof b);
      b.flat = 1; b.story = 1;
      if (attr_is(&x, "display", "false")) { skip(&x); continue; }
      m->has[k] = 1;
      r->depth++;
      blocks(&x, r, &l, &b);
      r->depth--;
      pl_to_story(&l, &m->hf[k]);
      if (m->hf[k].np == 1 && !m->hf[k].p[0]->len) story_free(&m->hf[k]);     /* (an empty one: none, though it is there) */
    }
  }
  xml_free(&x);
  r->part = part;
}
/* styles.xml's or content.xml's style sections (x at the root's START), the body (content.xml) too */
static void read_part(Xml *x, R *r, int part, ParaList *body)
{
  int d0 = x->depth;
  r->part = part;
  while (xml_next(x) != XT_EOF && !r->oom) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "font-face-decls")) read_font_faces(x, r);
    else if (!strcmp(x->name, "styles") || !strcmp(x->name, "automatic-styles")) {
      int autom = x->name[0] == 'a', d1 = x->depth;
      r->part = part;
      if (x->pending_end) { xml_next(x); continue; }
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type != XT_START) continue;
        if (!strcmp(x->name, "style")) read_style(x, r, autom, 0);
        else if (!strcmp(x->name, "default-style")) read_style(x, r, 0, 1);
        else if (!strcmp(x->name, "list-style")) read_list_style(x, r, 0);
        else if (!strcmp(x->name, "outline-style")) read_list_style(x, r, 1);
        else if (!strcmp(x->name, "notes-configuration")) read_notes_config(x, r);
        else if (!strcmp(x->name, "page-layout")) read_page_layout(x, r);
        else if (!strcmp(x->name, "linenumbering-configuration")) {
          const char *nl = xml_attr(x, "number-lines"), *inc = xml_attr(x, "increment"), *rs = xml_attr(x, "restart-on-page");
          int ok;
          i32 off = attr_len(x, "offset", &ok);
          if (nl && !strcmp(nl, "true")) {
            /* line numbers: Quill keeps them with each section */
            r->linenum = (u8)(rs && !strcmp(rs, "true") ? LN_PAGE : LN_CONT);
            r->lnby = (u8)(inc && atoi(inc) > 0 ? atoi(inc) : 1);
            r->lndist = ok ? off : 360;
          }
          skip(x);
        } else skip(x);
      }
    } else if (!strcmp(x->name, "master-styles")) {
      int d1 = x->depth;
      if (x->pending_end) { xml_next(x); continue; }
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type == XT_START && !strcmp(x->name, "master-page")) read_master(x, r);
        else if (x->type == XT_START) skip(x);
      }
    } else if (!strcmp(x->name, "body") && body) {
      int d1 = x->depth;
      if (x->pending_end) { xml_next(x); continue; }
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type == XT_START && !strcmp(x->name, "text")) {
          Blk b;
          memset(&b, 0, sizeof b);
          r->part = 1;
          blocks(x, r, body, &b);
        } else if (x->type == XT_START) skip(x);
      }
    } else skip(x);
  }
}

/* ---- meta.xml */
static void read_meta(Xml *x, R *r)
{
  int d0 = x->depth;
  Buf kw;
  char created[40], modified[40];
  buf_init(&kw);
  created[0] = modified[0] = 0;
  if (x->pending_end) { xml_next(x); return; }
  while (xml_next(x) != XT_EOF) {
    char el[32], name[128], type[16];
    Buf t;
    int d1;
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    cpy(el, sizeof el, x->name);
    cpy(name, sizeof name, xml_attr(x, "name"));
    cpy(type, sizeof type, xml_attr(x, "value-type"));
    d1 = x->depth;
    buf_init(&t);
    if (!x->pending_end) while (xml_next(x) != XT_EOF) { if (x->type == XT_END && x->depth < d1) break; if (x->type == XT_TEXT) buf_add(&t, x->text.data, x->text.len); }
    else xml_next(x);
    if (t.data) {
      if (!strcmp(el, "title")) cpy(r->d->title, sizeof r->d->title, t.data);
      else if (!strcmp(el, "initial-creator")) cpy(r->d->author, sizeof r->d->author, t.data);
      else if (!strcmp(el, "creator")) { if (!r->d->author[0]) cpy(r->d->author, sizeof r->d->author, t.data); info_set(r->d, IP_LASTBY, t.data); }
      else if (!strcmp(el, "subject")) info_set(r->d, IP_SUBJECT, t.data);
      else if (!strcmp(el, "description")) info_set(r->d, IP_DESCRIPTION, t.data);
      else if (!strcmp(el, "language")) info_set(r->d, IP_LANGUAGE, t.data);
      else if (!strcmp(el, "keyword")) { if (kw.len) buf_adds(&kw, ", "); buf_adds(&kw, t.data); }
      else if (!strcmp(el, "creation-date")) cpy(created, sizeof created, t.data);
      else if (!strcmp(el, "date")) cpy(modified, sizeof modified, t.data);
      else if (!strcmp(el, "editing-cycles")) info_set(r->d, IP_REVISION, t.data);
      else if (!strcmp(el, "user-defined") && name[0])
        info_cust_set(r->d, name, !strcmp(type, "float") ? CP_NUMBER : !strcmp(type, "date") ? CP_DATE : !strcmp(type, "boolean") ? CP_BOOL : CP_TEXT,
                      !strcmp(type, "boolean") ? (!strcmp(t.data, "true") ? "1" : "0") : t.data);
    }
    buf_free(&t);
  }
  if (kw.data) info_set(r->d, IP_KEYWORDS, kw.data);
  buf_free(&kw);
  {
    /* dates as Word keeps them: to the second, UTC */
    char *v[2]; int k;
    v[0] = created; v[1] = modified;
    for (k = 0; k < 2; k++) if (v[k][0]) {
      char *dot = strchr(v[k], '.');
      if (dot) *dot = 0;
      if (!strchr(v[k], 'Z') && strlen(v[k]) == 19) strcat(v[k], "Z");
      info_set(r->d, k ? IP_MODIFIED : IP_CREATED, v[k]);
    }
  }
}

/* ---- the whole document */
/* a master page's page setup: the layout's, the header and footer heights added to the margins
   they take from */
static void master_page(R *r, int mi, PageSetup *pg, int *li_out)
{
  int li = mi >= 0 ? find_layout(r, r->mp[mi].layout) : -1, k, hh = 0, ff = 0;
  if (mi >= 0) read_master_stories(r, &r->mp[mi]);
  *pg = r->d->page;
  if (li_out) *li_out = li;
  if (li < 0) return;
  *pg = r->pl[li].page;
  for (k = 0; k < HF_N; k++) { if (r->mp[mi].has[k]) hh = 1; if (r->mp[mi].has[HF_N + k]) ff = 1; }
  if (hh && r->pl[li].hmin >= 0) { pg->header = pg->top; pg->top += r->pl[li].hmin; }
  else if (r->pl[li].hmin > 0 && r->pl[li].hmin < pg->top) pg->header = pg->top - r->pl[li].hmin;     /* (no header: Quill's distance) */
  else if (pg->top < pg->header) pg->header = pg->top / 2;
  if (ff && r->pl[li].fmin >= 0) { pg->footer = pg->bottom; pg->bottom += r->pl[li].fmin; }
  else if (r->pl[li].fmin > 0 && r->pl[li].fmin < pg->bottom) pg->footer = pg->bottom - r->pl[li].fmin;
  else if (pg->bottom < pg->footer) pg->footer = pg->bottom / 2;
}
/* a master page's headers and footers (HF_ headers then footers, copies) and whether its first page
   has its own and even pages theirs. A master followed by another (LibreOffice's First Page) is
   the first page of that one */
static void master_hf(R *r, int mi, Story *out, u8 *titlepg, u8 *evenodd)
{
  OMaster *m;
  int k, ni;
  *titlepg = 0; *evenodd = 0;
  memset(out, 0, 2 * HF_N * sizeof(Story));
  if (mi < 0) return;
  m = &r->mp[mi];
  read_master_stories(r, m);
  ni = m->next[0] && strcmp(m->next, m->name) ? find_master(r, m->next) : -1;
  if (ni >= 0 && strcmp(r->mp[ni].name, m->next)) ni = -1;
  if (ni >= 0 && ni != mi) {
    OMaster *n = &r->mp[ni];
    read_master_stories(r, n);
    *titlepg = 1;
    for (k = 0; k < 2; k++) {
      int o = k * HF_N;
      Story *c;
      if (m->has[o + HF_DEFAULT] && (c = story_copy(&m->hf[o + HF_DEFAULT]))) { out[o + HF_FIRST] = *c; free(c); }
      if (n->has[o + HF_DEFAULT] && (c = story_copy(&n->hf[o + HF_DEFAULT]))) { out[o + HF_DEFAULT] = *c; free(c); }
      if (n->has[o + HF_EVEN] && (c = story_copy(&n->hf[o + HF_EVEN]))) { out[o + HF_EVEN] = *c; free(c); *evenodd = 1; }
    }
    return;
  }
  for (k = 0; k < 2 * HF_N; k++) {
    Story *c;
    if (!m->has[k] || !(c = story_copy(&m->hf[k]))) continue;
    out[k] = *c; free(c);
    if (k % HF_N == HF_FIRST) *titlepg = 1;
    if (k % HF_N == HF_EVEN) *evenodd = 1;
  }
  /* an empty first page header of its own still counts */
  if (m->has[HF_FIRST] || m->has[HF_N + HF_FIRST]) *titlepg = 1;
}
static void finish(R *r, ParaList *body)
{
  Doc *d = r->d;
  int mi = find_master(r, r->sect_master), li, k;
  Story hf[2 * HF_N];
  u8 titlepg, evenodd;
  PageSetup pg;
  /* objects still waiting for a paragraph: the last one's */
  if (r->npend_obj) {
    if (body->np) {
      PB pb;
      CharFmt cf = body->p[body->np - 1]->nr ? body->p[body->np - 1]->r[body->p[body->np - 1]->nr - 1].f : d->styles[0].cf;
      memset(&pb, 0, sizeof pb);
      pb.p = body->p[body->np - 1]; pb.l = body; pb.pf = pb.p->pf;
      r->pb = &pb;
      for (k = 0; k < r->npend_obj; k++) obj_put(r, &r->pend_obj[k], &cf);
      r->pb = NULL;
    } else for (k = 0; k < r->npend_obj; k++) img_release(&r->pend_obj[k]);
    r->npend_obj = 0;
  }
  /* the last section's master page is the document's */
  master_page(r, mi, &pg, &li);
  d->page = pg;
  if (li >= 0) {
    OLayout *pl = &r->pl[li];
    if (pl->hasborder) d->border = pl->border;
    if (pl->bg) d->pagecolor = pl->bg;
    if (pl->numfmt >= 0 && pl->numfmt != NF_DECIMAL && pl->numfmt != NF_NONE) d->pgfmt = (u8)pl->numfmt;
  }
  master_hf(r, mi, hf, &titlepg, &evenodd);
  for (k = 0; k < HF_N; k++) {
    story_free(&d->hdr[k]); story_free(&d->ftr[k]);
    d->hdr[k] = hf[k]; d->ftr[k] = hf[HF_N + k];
  }
  d->titlepg = titlepg; d->evenodd = evenodd;
  /* the last section: a format of its own when it isn't plain */
  {
    u8 cols = r->sect_cols ? r->sect_cols : li >= 0 ? r->pl[li].cols : 1;
    if (body->np && r->sect_paras && (cols > 1 || r->linenum || (d->nsects && r->sect_start != SB_NEXT))) {
      SectFmt f;
      sect_default(&f);
      f.start = (u8)(d->nsects ? r->sect_start : SB_NEXT);
      f.cols = cols;
      f.colgap = r->sect_cols ? r->sect_colgap : li >= 0 ? r->pl[li].colgap : 720;
      f.colline = r->sect_cols ? r->sect_colline : li >= 0 ? r->pl[li].colline : 0;
      if ((k = doc_add_sect(d, &f))) body->p[body->np - 1]->pf.sect = (u16)k;
    }
  }
  /* the other sections: their master pages' headers and footers when not the document's */
  for (k = 0; k < d->nsects; k++) {
    SectFmt *s = &d->sects[k];
    int m = k < r->nsect_mp ? r->sect_mp[k] : mi;
    if (r->linenum) { s->linenum = r->linenum; s->lnby = r->lnby; s->lndist = r->lndist; }
    if (m >= 0) { master_page(r, m, &s->page, NULL); s->haspage = 1; }
    if (m >= 0 && m != mi) {
      u8 tp, eo;
      if ((s->hf = (Story *)calloc(2 * HF_N, sizeof(Story)))) { master_hf(r, m, s->hf, &tp, &eo); s->titlepg = tp; }
    } else if (m >= 0) s->titlepg = titlepg;
    if (s->haspage && !memcmp(&s->page, &d->page, sizeof d->page)) s->haspage = 0;
  }
  /* comments whose end never came: they end where they start */
  for (k = 0; k < r->nan; k++) if (r->an[k].name[0]) {
    u32 i, j;
    char s[8], e[8];
    cmark_utf8(s, r->an[k].k, 0); cmark_utf8(e, r->an[k].k, 1);
    for (i = 0; i < body->np; i++) {
      Para *p = body->p[i];
      for (j = 0; j + 4 <= p->len; j++) if (!memcmp(p->t + j, s, 4)) {
        Para *q = para_new(&p->pf, &p->r[0].f);
        u32 a, start = 0;
        if (!q) break;
        for (a = 0; a < p->nr; a++) {
          u32 from = start, to = p->r[a].end;
          if (j + 4 >= from && j + 4 <= to) { para_append(q, p->t + from, j + 4 - from, &p->r[a].f); para_append(q, e, 4, &p->r[a].f); from = j + 4; }
          if (to > from) para_append(q, p->t + from, to - from, &p->r[a].f);
          start = to;
        }
        q->pf = p->pf;
        para_normalise(q);
        body->p[i] = q; para_free(p);
        i = body->np;
        break;
      }
    }
  }
}

static void r_free(R *r)
{
  int k, j;
  for (k = 0; k < r->nmp; k++) for (j = 0; j < 2 * HF_N; j++) story_free(&r->mp[k].hf[j]);
  for (k = 0; k < r->nchg; k++) free(r->chg[k].raw);
  for (k = 0; k < r->npend_obj; k++) img_release(&r->pend_obj[k]);
  free(r->st); free(r->ol); free(r->pl); free(r->mp); free(r->ff); free(r->chg); free(r->an); free(r->lid); free(r->sect_mp); free(r->pend_obj);
}
/* defaults before any style: ODF's (Times New Roman 12 pt, no spacing, single lines) */
static void set_defaults(R *r)
{
  Doc *d = r->d;
  int i = find_style(r, "\001default-para", F_PARA);
  memset(&d->defpf, 0, sizeof d->defpf);
  d->defpf.line = 240; d->defpf.linerule = LS_AUTO;
  memset(&d->defcf, 0, sizeof d->defcf);
  d->defcf.font = (u8)doc_font(d, "Times New Roman"); d->defcf.size = 24; d->defcf.color = COLOR_AUTO;
  if (i >= 0) { apply_pa(&d->defpf, &r->st[i]); apply_cm(&d->defcf, &r->st[i]); }
  d->defpf.list = 0;
  /* Quill's Normal has none of its own until the file's Standard style says */
  d->styles[0].pmask = 0; d->styles[0].cmask = 0;
  d->styles[0].opf = d->defpf; d->styles[0].ocf = d->defcf;
  doc_resolve_styles(d);
}

static Doc *odt_load(const u8 *data, size_t len, unsigned *lost, char *err, size_t errcap)
{
  ZipReader z;
  R *r;
  Doc *d;
  ParaList body;
  int flat = !(len >= 4 && data[0] == 'P' && data[1] == 'K'), k;
  char *xml;
  size_t n;
  Xml x;
  if (lost) *lost = 0;
  if (!flat && !zip_open(&z, data, len)) { snprintf(err, errcap, "This is not an OpenDocument file."); return NULL; }
  if (!(r = (R *)calloc(1, sizeof(R))) || !(d = doc_new())) {
    free(r);
    if (!flat) zip_close(&z);
    snprintf(err, errcap, "Not enough memory to open the file.");
    return NULL;
  }
  memset(&body, 0, sizeof body);
  r->d = d; r->z = flat ? NULL : &z; r->flat = flat;
  r->sect_start = SB_NEXT; r->sect_colgap = 720;
  if (flat) {
    /* one XML file: everything in the order it comes */
    xml_init(&x, (const char *)data, len);
    while (xml_next(&x) != XT_EOF) if (x.type == XT_START && !strcmp(x.name, "document")) break;
    if (x.type != XT_START) { xml_free(&x); r_free(r); free(r); doc_free(d); snprintf(err, errcap, "This is not an OpenDocument file."); return NULL; }
    {
      /* the styles and master pages first (the body needs them), then everything else */
      int d0 = x.depth, defaults = 0;
      r->part = 1;
      while (xml_next(&x) != XT_EOF && !r->oom) {
        if (x.type == XT_END && x.depth < d0) break;
        if (x.type != XT_START) continue;
        if (!strcmp(x.name, "meta")) read_meta(&x, r);
        else if (!strcmp(x.name, "body")) {
          int d1 = x.depth;
          if (!defaults) { set_defaults(r); defaults = 1; }
          for (k = 0; k < r->nmp; k++) r->mp[k].read = 0;
          if (!x.pending_end) while (xml_next(&x) != XT_EOF) {
            if (x.type == XT_END && x.depth < d1) break;
            if (x.type == XT_START && !strcmp(x.name, "text")) { Blk b; memset(&b, 0, sizeof b); r->part = 1; blocks(&x, r, &body, &b); }
            else if (x.type == XT_START) skip(&x);
          } else xml_next(&x);
        } else {
          /* (font-face-decls, styles, automatic-styles, master-styles: read_part's, one element at a time) */
          const char *st;
          size_t sl;
          char el[32];
          Xml y;
          cpy(el, sizeof el, x.name);
          if (strcmp(el, "font-face-decls") && strcmp(el, "styles") && strcmp(el, "automatic-styles") && strcmp(el, "master-styles")) { skip(&x); continue; }
          sl = xml_raw(&x, &st);
          {
            /* wrapped so read_part sees it as a child */
            Buf w;
            buf_init(&w);
            buf_adds(&w, "<w>"); buf_add(&w, st, sl); buf_adds(&w, "</w>");
            if (!w.oom) {
              xml_init(&y, w.data, w.len);
              xml_next(&y);
              read_part(&y, r, 1, NULL);
              xml_free(&y);
              /* master pages point into the wrapped copy: point them into the file instead */
              for (k = 0; k < r->nmp; k++) if (r->mp[k].xml >= w.data && r->mp[k].xml < w.data + w.len) r->mp[k].xml = st + (r->mp[k].xml - w.data - 3);
            }
            buf_free(&w);
          }
        }
      }
      if (!defaults) set_defaults(r);
    }
    xml_free(&x);
  } else {
    if ((k = zip_find(&z, "styles.xml")) >= 0 && (xml = (char *)zip_read(&z, k, &n))) {
      xml_init(&x, xml, n);
      while (xml_next(&x) != XT_EOF) if (x.type == XT_START) { read_part(&x, r, 0, NULL); break; }
      xml_free(&x);
      /* (the master pages' XML points into it: kept until the end) */
      set_defaults(r);
      if ((k = zip_find(&z, "content.xml")) >= 0) {
        char *c = (char *)zip_read(&z, k, &n);
        if (c) {
          xml_init(&x, c, n);
          while (xml_next(&x) != XT_EOF) if (x.type == XT_START) { read_part(&x, r, 1, &body); break; }
          xml_free(&x);
          finish(r, &body);
          free(c);
        }
      }
      free(xml);
      xml = NULL;
    } else {
      set_defaults(r);
      if ((k = zip_find(&z, "content.xml")) >= 0 && (xml = (char *)zip_read(&z, k, &n))) {
        xml_init(&x, xml, n);
        while (xml_next(&x) != XT_EOF) if (x.type == XT_START) { read_part(&x, r, 1, &body); break; }
        xml_free(&x);
        finish(r, &body);
        free(xml);
      } else {
        r_free(r); free(r); doc_free(d); zip_close(&z);
        snprintf(err, errcap, "The document's text (content.xml) is missing or damaged.");
        return NULL;
      }
    }
    if ((k = zip_find(&z, "meta.xml")) >= 0 && (xml = (char *)zip_read(&z, k, &n))) {
      xml_init(&x, xml, n);
      while (xml_next(&x) != XT_EOF) if (x.type == XT_START && !strcmp(x.name, "meta")) { read_meta(&x, r); break; }
      xml_free(&x);
      free(xml);
    }
  }
  if (flat) finish(r, &body);
  if (r->oom) {
    pl_free(&body); r_free(r); free(r); doc_free(d);
    if (!flat) zip_close(&z);
    snprintf(err, errcap, "Not enough memory to open the file.");
    return NULL;
  }
  /* next styles, now that the styles used are made */
  for (k = 0; k < r->nst; k++) {
    OStyle *s = &r->st[k];
    if (s->qs > 0 && s->fam == F_PARA && s->next[0]) {
      int ni = find_style(r, s->next, F_PARA);
      if (ni >= 0 && r->st[ni].qs >= 0) d->styles[s->qs].next = (u8)r->st[ni].qs;
    }
  }
  if (!pl_to_body(d, &body)) { pl_free(&body); r_free(r); free(r); doc_free(d); if (!flat) zip_close(&z); snprintf(err, errcap, "Not enough memory to open the file."); return NULL; }
  if (lost) *lost = r->lost;
  r_free(r); free(r);
  if (!flat) zip_close(&z);
  doc_clear_undo(d);
  d->dirty = 0;
  return d;
}

/* an .odt or .ott package: its mimetype entry, or a content.xml without Word's document */
static int odt_sniff(const u8 *b, size_t n)
{
  if (n >= 4 && b[0] == 'P' && b[1] == 'K' && b[2] == 3 && b[3] == 4) {
    ZipReader z;
    int ok = 0, k;
    if (n > 30 + 8 + 39 && !memcmp(b + 30, "mimetype", 8)) {
      size_t off = 30 + 8 + (size_t)(b[28] | (b[29] << 8));
      if (off + 39 <= n && !memcmp(b + off, "application/vnd.oasis.opendocument.text", 39)) return 1;
      if (off + 28 <= n && !memcmp(b + off, "application/vnd.oasis.opendo", 28)) return 0;     /* (a spreadsheet, a drawing...) */
    }
    if (!zip_open(&z, b, n)) return 0;
    if (zip_find(&z, "content.xml") >= 0 && zip_find(&z, "word/document.xml") < 0) {
      ok = 1;
      if ((k = zip_find(&z, "mimetype")) >= 0) {
        size_t ml;
        u8 *m = zip_read(&z, k, &ml);
        ok = m && ml >= 39 && !memcmp(m, "application/vnd.oasis.opendocument.text", 39);
        free(m);
      }
    }
    zip_close(&z);
    return ok;
  }
  return 0;
}
/* Flat ODT: an office:document element of text near the start */
static int fodt_sniff(const u8 *b, size_t n)
{
  {
    size_t lim = n < 4096 ? n : 4096, i;
    int doc = 0;
    for (i = 0; i + 16 < lim; i++) if (b[i] == '<' && !memcmp(b + i + 1, "office:document", 15) && (b[i + 16] == ' ' || b[i + 16] == '>' || b[i + 16] == '\n' || b[i + 16] == '\r')) { doc = 1; break; }
    if (!doc) return 0;
    for (; i + 17 <= lim; i++) if (!memcmp(b + i, "opendocument.text", 17)) return 1;
    return 0;
  }
}
static u8 *odt_save(Doc *d, size_t *len) { return odt_write(d, 0, len); }
static u8 *fodt_save(Doc *d, size_t *len) { return odt_write(d, 1, len); }

const DocFormat fmt_odt = { "OpenDocument Text", "odt|ott", odt_sniff, odt_load, odt_save, 0 };
const DocFormat fmt_fodt = { "Flat OpenDocument Text", "fodt", fodt_sniff, odt_load, fodt_save, 0 };
