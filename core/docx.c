/* docx.c - see docx.h. Reading first (styles, numbering and theme, then the body, notes, comments,
   headers and footers), then writing, then plain text and whole files. Reading is forgiving: an
   attribute that is missing or out of range is ignored rather than failing the load. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "docx.h"
#include "toc.h"
#include "xml.h"
#include "zip.h"
#include "image.h"
#include "tstyle.h"
#include "keep.h"
#include "notes.h"   /* (WP22b) */
#include "info.h"    /* (WP24) */
#include "math.h"     /* WP14a: equations */

/* ------------------------------------------------------------------ */
/* reading: formatting as found (which fields a pPr or rPr sets) */
typedef struct { unsigned set; ParaFmt pf; u16 numid; int has_list; int has_sect; SectFmt sect; u8 dropcap, dropmargin; } PPr;
typedef struct { unsigned set; CharFmt cf; } RPr;

typedef struct {
  char id[40], name[48], based[40], next[40];
  int type;                         /* 0 paragraph, 1 character, 2 other */
  int isdefault;
  PPr p; RPr r;
  int resolved, busy, quick;
  ParaFmt epf; CharFmt ecf;         /* paragraph styles: effective formatting */
  RPr er;                           /* character styles: what the chain sets */
  TableStyle *ts;                   /* table styles: their lines, fills and parts (NULL for others) */
} RawStyle;

typedef struct { u16 id; ListLevel lvl[9]; } AbsNum;

typedef struct {
  Doc *d;
  RawStyle *st; int nst;
  PPr defp; RPr defr;
  char major[48], minor[48];
  u32 clr[12];                      /* the theme's colours: dk1 lt1 dk2 lt2 accent1-6 hlink folHlink */
  AbsNum *abs; int nabs;
  unsigned lost;
  int field;                        /* complex fields: 0 outside, 1 in the code, 2 in the result */
  int depth;                        /* nesting of tables (recursion guard) */
  int compat;                       /* Word's compatibility mode: 15 for Word 2013 and later */
  int story;                        /* reading a header or footer: page fields become FIELD_ characters */
  char instr[600]; int fieldkind;   /* the code of the complex field being read; 1 PAGE, 2 NUMPAGES */
  u16 fieldlink;                    /* a HYPERLINK field's link number: its result's runs are the link */
  u16 bmark; char bmid[16];         /* the bookmark being read (its runs get it) and its w:id; 0: none */
  char hdrid[HF_N][32], ftrid[HF_N][32];   /* the section's header and footer relationships */
  char pend[2 * HF_N][32];          /* those of the last section's w:sectPr read in a paragraph */
  char (*sid)[2 * HF_N][32]; int nsid;   /* each section format's (Doc.sects order), as read */
  int has_bodysect; SectFmt bodysect;      /* the last section's format (the body's w:sectPr) */
  int border_set;                   /* page borders read (the first section's are kept) */
  ZipReader *z;                     /* for pictures: the archive, the relationships of the part being */
  const char *rels; size_t rn;      /* read, and the folder its targets are relative to */
  const char *base;
  i32 *fnid; int *fnidx; int nfn;   /* footnotes: their ids in the file and indexes in Doc.notes */
  i32 *enid; int *enidx; int nen;   /* endnotes the same */
  i32 *cmid; int *cmidx; u8 *cmopen; int ncm;   /* comments the same; whether their range has started */
} Ctx;
static void rel_by_id(const char *rels, size_t n, const char *id, const char *base, char *out, size_t cap);
static void rel_raw(const char *rels, size_t n, const char *id, char *out, size_t cap);
/* a HYPERLINK field code's target: HYPERLINK "url", with \l "name" for a place in the document
   (url#name, or #name alone). 0 if the code isn't a HYPERLINK */
static int hyperlink_field(const char *instr, char *out, size_t cap)
{
  const char *s = instr, *q;
  char anchor[256] = "";
  out[0] = 0;
  while (*s == ' ') s++;
  if (strncmp(s, "HYPERLINK", 9) || (s[9] != ' ' && s[9] != 0)) return 0;
  s += 9;
  while (*s) {
    while (*s == ' ') s++;
    if (s[0] == '\\' && (s[1] == 'l' || s[1] == 'o' || s[1] == 't')) {
      /* a switch with a quoted argument: \l is the place in the document, the others aren't kept */
      char k = s[1];
      s += 2;
      while (*s == ' ') s++;
      if (*s == '"' && (q = strchr(s + 1, '"'))) {
        if (k == 'l') snprintf(anchor, sizeof anchor, "%.*s", (int)(q - s - 1), s + 1);
        s = q + 1;
      }
      continue;
    }
    if (*s == '\\') { s += 2; continue; }
    if (*s == '"' && (q = strchr(s + 1, '"'))) { if (!out[0]) snprintf(out, cap, "%.*s", (int)(q - s - 1), s + 1); s = q + 1; continue; }
    if (*s && !out[0]) { q = s; while (*q && *q != ' ') q++; snprintf(out, cap, "%.*s", (int)(q - s), s); s = q; continue; }
    if (*s) s++;
  }
  if (anchor[0] && strlen(out) + strlen(anchor) + 2 < cap) { strcat(out, "#"); strcat(out, anchor); }
  return out[0] != 0;
}
/* a REF or PAGEREF field's bookmark (into name): 1 for REF (its result, the bookmark's text, is kept
   as text linked to the bookmark), 2 for PAGEREF (the page it is on), 0 for other fields */
static int ref_field(const char *instr, char *name, size_t cap)
{
  const char *s = instr, *q;
  int kind;
  name[0] = 0;
  while (*s == ' ') s++;
  if (!strncmp(s, "PAGEREF", 7) && s[7] == ' ') { kind = 2; s += 7; }
  else if (!strncmp(s, "REF", 3) && s[3] == ' ') { kind = 1; s += 3; }
  else return 0;
  while (*s == ' ') s++;
  if (*s == '"') { s++; q = strchr(s, '"'); if (!q) return 0; }
  else { q = s; while (*q && *q != ' ' && *q != '\\') q++; }
  if (q == s) return 0;
  snprintf(name, cap, "%.*s", (int)(q - s), s);
  return kind;
}
/* what a field code means to Quill: 1 for PAGE (the page number), 2 for NUMPAGES or SECTIONPAGES
   (the page count), 0 for anything else, whose result text is kept as it is */
static int field_kind(const char *instr)
{
  while (*instr == ' ') instr++;
  if (!strncmp(instr, "PAGE", 4) && (instr[4] == ' ' || instr[4] == 0 || instr[4] == '\\')) return 1;
  if (!strncmp(instr, "NUMPAGES", 8) || !strncmp(instr, "SECTIONPAGES", 12)) return 2;
  return 0;
}

/* a WordprocessingML on/off property such as <w:b/>: on when it has no val at all, off for
   "0", "false", "off" or "none" */
static int onoff(Xml *x)
{
  const char *v = xml_attr(x, "val");
  return !v || !(!strcmp(v, "0") || str_ieq(v, "false") || str_ieq(v, "off") || str_ieq(v, "none"));
}
/* parse a decimal integer (leading spaces and a sign allowed) into *out; 0, leaving *out alone, if
   s is NULL or has no digits. Digits past nine are dropped so hostile input cannot overflow. */
static int num(const char *s, i32 *out)
{
  i32 v = 0;
  int neg = 0, any = 0;
  if (!s) return 0;
  while (*s == ' ') s++;
  if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
  while (*s >= '0' && *s <= '9') { if (v < 100000000) v = v * 10 + (*s - '0'); s++; any = 1; }
  if (any) *out = neg ? -v : v;
  return any;
}
/* the current tag's attribute as a number; 0 (and *out untouched) if absent or not a number */
static int attr_num(Xml *x, const char *name, i32 *out) { return num(xml_attr(x, name), out); }
/* copy an attribute into dst (cap bytes, always NUL-terminated); dst is left as it was if absent */
static void copy_attr(Xml *x, const char *name, char *dst, size_t cap)
{
  const char *v = xml_attr(x, name);
  if (!v) return;
  strncpy(dst, v, cap - 1);
  dst[cap - 1] = 0;
}
/* a colour written as six hex digits (RRGGBB) as 0xRRGGBB; COLOR_AUTO for "auto", NULL or
   anything malformed */
static u32 hexcolor(const char *v)
{
  u32 c = 0;
  int i;
  if (!v || str_ieq(v, "auto")) return COLOR_AUTO;
  for (i = 0; i < 6 && v[i]; i++) {
    int ch = v[i], d = (ch >= '0' && ch <= '9') ? ch - '0' : (ch >= 'a' && ch <= 'f') ? ch - 'a' + 10 : (ch >= 'A' && ch <= 'F') ? ch - 'A' + 10 : -1;
    if (d < 0) return COLOR_AUTO;
    c = (c << 4) | (u32)d;
  }
  return i == 6 ? c : COLOR_AUTO;
}
/* the font a theme font attribute (asciiTheme="minorHAnsi" and so on) stands for, from the theme's
   major (headings) and minor (body) Latin fonts; NULL if unknown or the theme had none */
static const char *theme_font(Ctx *c, const char *t)
{
  if (!t) return NULL;
  if (!strncmp(t, "major", 5) && c->major[0]) return c->major;
  if (!strncmp(t, "minor", 5) && c->minor[0]) return c->minor;
  return NULL;
}
/* record that the rPr sets one of the CF_ flags, on or off (off matters: it overrides a style) */
static void set_flag(RPr *r, unsigned bit, int on)
{
  r->set |= bit;
  if (on) r->cf.flags |= (u8)bit; else r->cf.flags &= (u8)~bit;
}
/* Word's named highlight colours */
static const struct { const char *name; u32 rgb; } hlnames[] = {
  { "yellow", 0xFFFF00 }, { "green", 0x00FF00 }, { "cyan", 0x00FFFF }, { "magenta", 0xFF00FF }, { "blue", 0x0000FF }, { "red", 0xFF0000 },
  { "darkBlue", 0x000080 }, { "darkCyan", 0x008080 }, { "darkGreen", 0x008000 }, { "darkMagenta", 0x800080 }, { "darkRed", 0x800000 },
  { "darkYellow", 0x808000 }, { "darkGray", 0x808080 }, { "lightGray", 0xC0C0C0 }, { "black", 0x000000 }, { "white", 0xFFFFFF },
};
/* w:highlight's colour name as HL_ON | 0xRRGGBB; 0 for "none" or a name Word doesn't define */
static u32 highlight_rgb(const char *v)
{
  size_t i;
  for (i = 0; i < sizeof hlnames / sizeof hlnames[0]; i++) if (!strcmp(v, hlnames[i].name)) return HL_ON | hlnames[i].rgb;
  return 0;                                                  /* "none" */
}
/* the reverse, for saving: the name of a highlight, or NULL if it is not one of Word's sixteen
   (then the caller writes it as shading) */
static const char *highlight_name(u32 hl)
{
  size_t i;
  for (i = 0; i < sizeof hlnames / sizeof hlnames[0]; i++) if (hl == (HL_ON | hlnames[i].rgb)) return hlnames[i].name;
  return NULL;
}
/* read a w:rPr (x just past its START) into r, noting in r->set which fields it sets; rstyle, if
   given, gets the character style id (up to 39 bytes). Sizes are in half-points (w:sz), spacing in
   twips, position in half-points, scale in percent (100 is stored as 0, meaning normal). Fonts
   are taken from the ascii slot, falling back to hAnsi and then the theme. Reads up to its END. */
static void parse_rpr(Xml *x, Ctx *c, RPr *r, char *rstyle)
{
  int d0 = x->depth;
  while (xml_next(x) != XT_EOF) {
    const char *n = x->name, *v;
    i32 k;
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    v = xml_attr(x, "val");
    if (!strcmp(n, "rStyle") && rstyle && v) { strncpy(rstyle, v, 39); rstyle[39] = 0; }
    else if (!strcmp(n, "b")) set_flag(r, CF_BOLD, onoff(x));
    else if (!strcmp(n, "i")) set_flag(r, CF_ITALIC, onoff(x));
    else if (!strcmp(n, "u")) {
      int on = onoff(x);
      set_flag(r, CF_UNDERLINE, on);
      r->set |= CM_UL;
      r->cf.ul = (u8)(!on || !v ? UL_SINGLE : !strcmp(v, "double") ? UL_DOUBLE : !strncmp(v, "thick", 5) ? UL_THICK :
                      !strncmp(v, "dotted", 6) || !strncmp(v, "dotDot", 6) ? UL_DOTTED : !strncmp(v, "dash", 4) || !strncmp(v, "dotDash", 7) ? UL_DASHED :
                      !strncmp(v, "wav", 3) ? UL_WAVE : !strcmp(v, "words") ? UL_WORDS : UL_SINGLE);
    }
    else if (!strcmp(n, "strike")) set_flag(r, CF_STRIKE, onoff(x));
    else if (!strcmp(n, "dstrike")) { r->set |= CM_DSTRIKE; if (onoff(x)) r->cf.fx |= FX_DSTRIKE; else r->cf.fx &= (u8)~FX_DSTRIKE; }
    else if (!strcmp(n, "caps")) set_flag(r, CF_CAPS, onoff(x));
    else if (!strcmp(n, "bdr")) { r->set |= CM_CBORDER; if (v && strcmp(v, "none") && strcmp(v, "nil")) r->cf.fx |= FX_BORDER; else r->cf.fx &= (u8)~FX_BORDER; }
    else if (!strcmp(n, "smallCaps")) { r->set |= CM_SMALLCAPS; if (onoff(x)) r->cf.fx |= FX_SMALLCAPS; else r->cf.fx &= (u8)~FX_SMALLCAPS; }
    else if (!strcmp(n, "spacing") && num(v, &k) && k > -32000 && k < 32000) { r->set |= CM_SPACING; r->cf.spacing = (i16)k; }
    else if (!strcmp(n, "w") && num(v, &k) && k > 0 && k <= 600) { r->set |= CM_SCALE; r->cf.scale = (u16)(k == 100 ? 0 : k); }
    else if (!strcmp(n, "position") && num(v, &k) && k > -3000 && k < 3000) { r->set |= CM_POSITION; r->cf.position = (i16)k; }
    else if (!strcmp(n, "vertAlign") && v) {
      set_flag(r, CF_SUPER, !strcmp(v, "superscript"));
      set_flag(r, CF_SUB, !strcmp(v, "subscript"));
    }
    else if (!strcmp(n, "sz") && num(v, &k) && k > 0 && k < 3277) { r->set |= CM_SIZE; r->cf.size = (u16)k; }
    else if (!strcmp(n, "rFonts")) {
      const char *f = xml_attr(x, "ascii");
      if (!f) f = xml_attr(x, "hAnsi");
      if (!f) f = theme_font(c, xml_attr(x, "asciiTheme"));
      if (!f) f = theme_font(c, xml_attr(x, "hAnsiTheme"));
      if (f && *f) { r->set |= CM_FONT; r->cf.font = (u8)doc_font(c->d, f); }
    }
    else if (!strcmp(n, "color") && v) { r->set |= CM_COLOR; r->cf.color = hexcolor(v); }
    else if (!strcmp(n, "highlight") && v) { r->set |= CM_HL; r->cf.hl = highlight_rgb(v); }
    else if (!strcmp(n, "shd")) {
      /* shading behind the text, as highlighting (a highlight wins) */
      const char *fill = xml_attr(x, "fill");
      if (!(r->set & CM_HL) || !r->cf.hl) { r->set |= CM_HL; r->cf.hl = fill && strcmp(fill, "auto") && hexcolor(fill) != COLOR_AUTO ? HL_ON | hexcolor(fill) : 0; }
    }
    xml_skip(x);
  }
}
static void parse_sect(Xml *x, Ctx *c, SectFmt *sf, int body);
static void parse_line(Xml *x, TBorder *b);
/* read a w:pPr (x just past its START) into p, noting in p->set which fields it sets; lengths are
   twips. pstyle gets the paragraph style id. mark is given only for a paragraph in the text (not a
   style): then the paragraph mark's rPr and a section break (w:sectPr) are read too. Indents use
   left/right or the newer start/end names; hanging becomes a negative first-line indent. Only the
   first eight tab stops are kept, sorted by position. */
static void parse_ppr(Xml *x, Ctx *c, PPr *p, char *pstyle, RPr *mark)
{
  int d0 = x->depth;
  while (xml_next(x) != XT_EOF) {
    const char *n = x->name, *v;
    i32 k;
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    v = xml_attr(x, "val");
    if (!strcmp(n, "pStyle") && pstyle && v) { strncpy(pstyle, v, 39); pstyle[39] = 0; }
    else if (!strcmp(n, "jc") && v) {
      p->set |= PM_ALIGN;
      p->pf.align = (u8)(!strcmp(v, "center") ? AL_CENTER : (!strcmp(v, "right") || !strcmp(v, "end")) ? AL_RIGHT :
                         (!strcmp(v, "both") || !strcmp(v, "distribute")) ? AL_JUSTIFY : AL_LEFT);
    }
    else if (!strcmp(n, "ind")) {
      i32 h = 0;
      if (!(p->set & PM_INDENT)) { p->pf.left = p->pf.right = p->pf.first = 0; }
      p->set |= PM_INDENT;
      if (attr_num(x, "left", &k) || attr_num(x, "start", &k)) p->pf.left = k;
      if (attr_num(x, "right", &k) || attr_num(x, "end", &k)) p->pf.right = k;
      if (attr_num(x, "firstLine", &k)) p->pf.first = k;
      if (attr_num(x, "hanging", &h)) p->pf.first = -h;
    }
    else if (!strcmp(n, "spacing")) {
      if (attr_num(x, "before", &k)) { p->set |= PM_SPACING; p->pf.before = k; }
      if (attr_num(x, "after", &k)) { if (!(p->set & PM_SPACING)) p->pf.before = 0; p->set |= PM_SPACING; p->pf.after = k; }
      if (attr_num(x, "line", &k) && k > 0) {
        const char *rule = xml_attr(x, "lineRule");
        p->set |= PM_LINE;
        p->pf.line = k;
        p->pf.linerule = (u8)(rule && !strcmp(rule, "exact") ? LS_EXACT : rule && !strcmp(rule, "atLeast") ? LS_ATLEAST : LS_AUTO);
      }
    }
    else if (!strcmp(n, "keepNext")) { p->set |= PM_KEEP; p->pf.keepnext = (u8)onoff(x); }
    else if (!strcmp(n, "keepLines")) { p->set |= PM_PAGINATION; p->pf.keeplines = (u8)onoff(x); }
    else if (!strcmp(n, "widowControl")) { p->set |= PM_PAGINATION; p->pf.nowidow = (u8)!onoff(x); }
    else if (!strcmp(n, "contextualSpacing")) { p->set |= PM_CONTEXT; p->pf.contextual = (u8)onoff(x); }
    else if (!strcmp(n, "shd")) {
      const char *fill = xml_attr(x, "fill");
      u32 col = fill ? hexcolor(fill) : COLOR_AUTO;
      p->set |= PM_SHADE;
      p->pf.shade = col == COLOR_AUTO ? 0 : HL_ON | col;
    }
    else if (!strcmp(n, "pBdr")) {
      int d1 = x->depth;
      p->set |= PM_BORDER;
      memset(p->pf.bstyle, 0, sizeof p->pf.bstyle); memset(p->pf.bwidth, 0, sizeof p->pf.bwidth); memset(p->pf.bspace, 0, sizeof p->pf.bspace);
      memset(p->pf.bcolor, 0, sizeof p->pf.bcolor);
      while (xml_next(x) != XT_EOF) {
        const char *bn;
        int e;
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type != XT_START) continue;
        bn = x->name;
        e = !strcmp(bn, "top") ? PE_TOP : !strcmp(bn, "bottom") ? PE_BOTTOM : (!strcmp(bn, "left") || !strcmp(bn, "start")) ? PE_LEFT :
            (!strcmp(bn, "right") || !strcmp(bn, "end")) ? PE_RIGHT : !strcmp(bn, "between") ? PE_BETWEEN : -1;
        if (e >= 0) {
          TBorder tb;
          parse_line(x, &tb);
          p->pf.bstyle[e] = tb.style; p->pf.bwidth[e] = tb.width; p->pf.bcolor[e] = tb.color;
          p->pf.bspace[e] = attr_num(x, "space", &k) && k >= 0 && k <= 31 ? (u8)k : 0;
        }
      }
      continue;
    }
    else if (!strcmp(n, "pageBreakBefore")) { p->set |= PM_BREAK; p->pf.pagebreak = (u8)onoff(x); }
    else if (!strcmp(n, "tabs")) {
      int d1 = x->depth, nt = 0;
      p->set |= PM_TABS;
      memset(p->pf.tabs, 0, sizeof p->pf.tabs);
      memset(p->pf.tabkind, 0, sizeof p->pf.tabkind);
      memset(p->pf.tablead, 0, sizeof p->pf.tablead);
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type == XT_START && !strcmp(x->name, "tab")) {
          const char *tv = xml_attr(x, "val"), *ld = xml_attr(x, "leader");
          if (nt < 8 && attr_num(x, "pos", &k) && k > 0 && !(tv && (!strcmp(tv, "clear") || !strcmp(tv, "bar") || !strcmp(tv, "num")))) {
            int j = nt++;
            /* in order of position */
            while (j > 0 && p->pf.tabs[j - 1] > k) { p->pf.tabs[j] = p->pf.tabs[j - 1]; p->pf.tabkind[j] = p->pf.tabkind[j - 1]; p->pf.tablead[j] = p->pf.tablead[j - 1]; j--; }
            p->pf.tabs[j] = k;
            p->pf.tabkind[j] = (u8)(!tv ? TAB_LEFT : (!strcmp(tv, "center") ? TAB_CENTER : (!strcmp(tv, "right") || !strcmp(tv, "end")) ? TAB_RIGHT :
                                    !strcmp(tv, "decimal") ? TAB_DECIMAL : TAB_LEFT));
            p->pf.tablead[j] = (u8)(!ld ? LEAD_NONE : (!strcmp(ld, "dot") || !strcmp(ld, "middleDot")) ? LEAD_DOT : !strcmp(ld, "hyphen") ? LEAD_HYPHEN :
                                    (!strcmp(ld, "underscore") || !strcmp(ld, "heavy")) ? LEAD_LINE : LEAD_NONE);
          }
        }
      }
      continue;                          /* the loop above ended on the END of tabs */
    }
    else if (!strcmp(n, "numPr")) {
      int d1 = x->depth;
      p->has_list = 1;
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type == XT_START && !strcmp(x->name, "ilvl") && attr_num(x, "val", &k) && k >= 0 && k < 9) p->pf.level = (u8)k;
        if (x->type == XT_START && !strcmp(x->name, "numId") && attr_num(x, "val", &k) && k >= 0 && k < 65536) p->numid = (u16)k;
      }
      continue;
    }
    else if (!strcmp(n, "rPr") && mark) { parse_rpr(x, c, mark, NULL); continue; }
    else if (!strcmp(n, "sectPr") && mark) { parse_sect(x, c, &p->sect, 0); p->has_sect = 1; continue; }
    else if (!strcmp(n, "framePr")) {
      const char *dc = xml_attr(x, "dropCap");
      if (dc && (!strcmp(dc, "drop") || !strcmp(dc, "margin"))) {
        p->dropcap = attr_num(x, "lines", &k) && k >= 2 && k <= 10 ? (u8)k : 3;
        p->dropmargin = (u8)(dc[0] == 'm');
      }
    }
    xml_skip(x);
  }
}
/* apply onto dst the paragraph fields p sets (lists are applied separately, by apply_list) */
static void pf_set(ParaFmt *dst, const PPr *p)
{
  pf_apply(dst, p->set & (PM_ALIGN | PM_INDENT | PM_SPACING | PM_LINE | PM_TABS | PM_KEEP | PM_BORDER | PM_SHADE | PM_PAGINATION | PM_CONTEXT), &p->pf);
  if (p->set & PM_BREAK) dst->pagebreak = p->pf.pagebreak;
}
/* apply onto dst the character look fields r sets */
static void cf_set(CharFmt *dst, const RPr *r)
{
  cf_apply(dst, r->set & CM_LOOK, &r->cf);
}
/* the Doc list (1-based index into d->lists) for a w:numId; 0 for none (numId 0 switches a list off) */
static int list_for(Ctx *c, u16 numid)
{
  int i;
  if (!numid) return 0;
  for (i = 0; i < c->d->nlists; i++) if (c->d->lists[i].id == numid) return i + 1;
  return 0;
}
/* a list on a paragraph: the level's indents unless the paragraph sets its own */
static void apply_list(Ctx *c, ParaFmt *pf, const PPr *p)
{
  if (!p->has_list) return;
  pf->list = (u16)list_for(c, p->numid);
  pf->level = p->pf.level;
  if (pf->list && !(p->set & PM_INDENT)) {
    const ListLevel *lv = &c->d->lists[pf->list - 1].lvl[pf->level];
    pf->left = lv->left; pf->first = -lv->hanging;
  }
}

/* ---- styles */
/* the index of the style with this id among those read, of the given type (0 paragraph,
   1 character, -1 any); -1 if there is none */
static int raw_find(Ctx *c, const char *id, int type)
{
  int i;
  for (i = 0; i < c->nst; i++) if ((type < 0 || c->st[i].type == type) && !strcmp(c->st[i].id, id)) return i;
  return -1;
}
/* the formatting a style with no basedOn starts from */
static void base_fmt(Ctx *c, ParaFmt *pf, CharFmt *cf)
{
  /* the specification's defaults (Times New Roman, 10 pt, single, no spacing), then docDefaults */
  memset(pf, 0, sizeof *pf); memset(cf, 0, sizeof *cf);
  pf->line = 240; pf->linerule = LS_AUTO;
  cf->font = (u8)doc_font(c->d, "Times New Roman"); cf->size = 20; cf->color = COLOR_AUTO;
  pf_set(pf, &c->defp); cf_set(cf, &c->defr);
}
/* work out style i's effective formatting (epf, ecf, and er for character styles) by applying it
   on top of the style it is based on, resolving that first. busy breaks basedOn loops, which
   damaged or hand-made files do have: the style in the loop then starts from the defaults. */
static void resolve(Ctx *c, int i)
{
  RawStyle *s = &c->st[i];
  int b;
  if (s->resolved || s->busy) return;
  s->busy = 1;
  b = s->based[0] ? raw_find(c, s->based, s->type) : -1;
  if (b >= 0 && b != i) {
    resolve(c, b);
    s->epf = c->st[b].epf; s->ecf = c->st[b].ecf; s->er = c->st[b].er;
  } else {
    base_fmt(c, &s->epf, &s->ecf);
    memset(&s->er, 0, sizeof s->er);
  }
  pf_set(&s->epf, &s->p);
  if (s->p.has_list) apply_list(c, &s->epf, &s->p);
  cf_set(&s->ecf, &s->r);
  s->er.set |= s->r.set;
  cf_set(&s->er.cf, &s->r);
  s->resolved = 1; s->busy = 0;
}
/* a border element's line: style, width (w:sz, eighths of a point, 4 if unset, at most 96) and
   colour; marks it set even when the style is none, so it can switch off a style's line */
static void parse_line(Xml *x, TBorder *b)
{
  const char *v = xml_attr(x, "val");
  i32 sz;
  b->set = 1;
  b->style = (u8)(!v || !strcmp(v, "nil") || !strcmp(v, "none") ? PB_NONE : !strcmp(v, "double") ? PB_DOUBLE : !strcmp(v, "thick") ? PB_THICK :
                  !strcmp(v, "dotted") ? PB_DOTTED : (!strcmp(v, "dashed") || !strcmp(v, "dashSmallGap") || !strcmp(v, "dotDash") || !strcmp(v, "dotDotDash")) ? PB_DASHED :
                  (!strncmp(v, "thinThick", 9) || !strncmp(v, "thickThin", 9) || !strcmp(v, "triple")) ? PB_DOUBLE : PB_SINGLE);
  b->width = attr_num(x, "sz", &sz) && sz > 0 ? (u8)(sz > 96 ? 96 : sz) : 4;
  b->color = hexcolor(xml_attr(x, "color"));
}
/* border elements (top, left, bottom, right, insideH, insideV) into out (TE_ order) */
static void parse_borders(Xml *x, TBorder *out)
{
  int d0 = x->depth;
  while (xml_next(x) != XT_EOF) {
    const char *n = x->name;
    int e;
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    e = !strcmp(n, "top") ? TE_TOP : (!strcmp(n, "left") || !strcmp(n, "start")) ? TE_LEFT : !strcmp(n, "bottom") ? TE_BOTTOM :
        (!strcmp(n, "right") || !strcmp(n, "end")) ? TE_RIGHT : !strcmp(n, "insideH") ? TE_INSIDEH : !strcmp(n, "insideV") ? TE_INSIDEV : -1;
    if (e >= 0) parse_line(x, &out[e]);
    xml_skip(x);
  }
}
/* what a tblPr says: borders (into b, TE_N of them), cell margins (set bit 2), and, when t is given,
   the table's indent, alignment, layout, look and style */
static void parse_tblpr(Xml *x, unsigned *set, TBorder *b, i32 *marl, i32 *marr, Table *t, char *tstyle, int *gotlook)
{
  int d0 = x->depth;
  while (xml_next(x) != XT_EOF) {
    const char *n = x->name, *v;
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    v = xml_attr(x, "val");
    if (!strcmp(n, "tblBorders")) { parse_borders(x, b); *set |= 1; continue; }
    if (!strcmp(n, "tblCellMar")) {
      int d1 = x->depth;
      while (xml_next(x) != XT_EOF) {
        i32 k;
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type != XT_START) continue;
        if ((!strcmp(x->name, "left") || !strcmp(x->name, "start")) && attr_num(x, "w", &k)) { *marl = k; *set |= 2; }
        if ((!strcmp(x->name, "right") || !strcmp(x->name, "end")) && attr_num(x, "w", &k)) { *marr = k; *set |= 2; }
        if (t && !strcmp(x->name, "top") && attr_num(x, "w", &k) && k >= 0 && k < 2000) t->mart = k;
        if (t && !strcmp(x->name, "bottom") && attr_num(x, "w", &k) && k >= 0 && k < 2000) t->marb = k;
      }
      continue;
    }
    if (t) {
      if (!strcmp(n, "tblInd")) attr_num(x, "w", &t->indent);
      else if (!strcmp(n, "jc") && v) t->jc = (u8)(!strcmp(v, "center") ? AL_CENTER : (!strcmp(v, "right") || !strcmp(v, "end")) ? AL_RIGHT : AL_LEFT);
      else if (!strcmp(n, "tblLayout")) { const char *ty = xml_attr(x, "type"); t->autofit = (u8)!(ty && !strcmp(ty, "fixed")); }
      else if (!strcmp(n, "tblLook")) {
        i32 k;
        const char *a;
        u16 lk = 0;
        if (v) { k = (i32)strtol(v, NULL, 16); lk = (u16)(k & 0x7E0); }
        if ((a = xml_attr(x, "firstRow"))) lk = (u16)(*a == '1' || *a == 't' ? lk | LK_FIRSTROW : lk & ~LK_FIRSTROW);
        if ((a = xml_attr(x, "lastRow"))) lk = (u16)(*a == '1' || *a == 't' ? lk | LK_LASTROW : lk & ~LK_LASTROW);
        if ((a = xml_attr(x, "firstColumn"))) lk = (u16)(*a == '1' || *a == 't' ? lk | LK_FIRSTCOL : lk & ~LK_FIRSTCOL);
        if ((a = xml_attr(x, "lastColumn"))) lk = (u16)(*a == '1' || *a == 't' ? lk | LK_LASTCOL : lk & ~LK_LASTCOL);
        if ((a = xml_attr(x, "noHBand"))) lk = (u16)(*a == '1' || *a == 't' ? lk | LK_NOHBAND : lk & ~LK_NOHBAND);
        if ((a = xml_attr(x, "noVBand"))) lk = (u16)(*a == '1' || *a == 't' ? lk | LK_NOVBAND : lk & ~LK_NOVBAND);
        t->look = lk;
        if (gotlook) *gotlook = 1;
      }
    }
    if (!strcmp(n, "tblStyle") && tstyle) copy_attr(x, "val", tstyle, 40);
    xml_skip(x);
  }
}
/* a table style's tcPr: fill and every cell's edges */
static void parse_style_tcpr(Xml *x, TCond *c)
{
  int d0 = x->depth;
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "shd")) { c->shade_set = 1; c->shade = hexcolor(xml_attr(x, "fill")); c->used = 1; }
    else if (!strcmp(x->name, "tcBorders")) {
      TBorder b[TE_N];
      int k;
      memset(b, 0, sizeof b);
      parse_borders(x, b);
      for (k = 0; k < 4; k++) if (b[k].set) { c->cb[k] = b[k]; c->used = 1; }
      continue;
    }
    xml_skip(x);
  }
}
/* the TC_ index of a w:tblStylePr type (firstRow, band1Horz and so on); -1 for one Quill doesn't know */
static int tcond_kind(const char *v)
{
  static const char *const names[TC_N] = { "wholeTable", "band1Vert", "band2Vert", "band1Horz", "band2Horz", "firstCol", "lastCol",
                                           "firstRow", "lastRow", "neCell", "nwCell", "seCell", "swCell" };
  int k;
  for (k = 0; v && k < TC_N; k++) if (!strcmp(v, names[k])) return k;
  return -1;
}
/* a table style's part (or the whole of it): what its pPr, rPr, tblPr and tcPr say */
static void parse_tcond(Xml *x, Ctx *c, TableStyle *ts, TCond *tc, RawStyle *rs)
{
  const char *n = x->name;
  if (!strcmp(n, "pPr")) {
    PPr pp;
    memset(&pp, 0, sizeof pp);
    parse_ppr(x, c, &pp, NULL, NULL);
    if (pp.set & PM_ALIGN) { tc->align_set = 1; tc->align = pp.pf.align; tc->used = 1; }
    if (rs) rs->p = pp;
  } else if (!strcmp(n, "rPr")) {
    RPr rp;
    memset(&rp, 0, sizeof rp);
    parse_rpr(x, c, &rp, NULL);
    if (rp.set & CM_ALL) { tc->cmask |= rp.set & CM_ALL; cf_apply(&tc->cf, rp.set & CM_ALL, &rp.cf); tc->used = 1; }
    if (rs) rs->r = rp;
  } else if (!strcmp(n, "tblPr")) {
    unsigned set = 0;
    i32 ml = 108, mr = 108;
    parse_tblpr(x, &set, tc->b, &ml, &mr, NULL, NULL, NULL);
    if (set & 1) tc->used = 1;
    if ((set & 2) && rs) { ts->mar_set = 1; ts->marl = ml; ts->marr = mr; }
  } else if (!strcmp(n, "tcPr")) parse_style_tcpr(x, tc);
  else xml_skip(x);
}
/* styles.xml: the document defaults (docDefaults) into c->defp and c->defr, and every style as
   written into c->st, unresolved (basedOn is followed later, by resolve). Table styles also get
   their TableStyle with the whole-table part and each conditional part. A style without an id is
   skipped; one without a name is named after its id. */
static void parse_styles(Ctx *c, const char *xml, size_t n)
{
  Xml x;
  int cap = 0;
  xml_init(&x, xml, n);
  while (xml_next(&x) != XT_EOF) {
    if (x.type != XT_START) continue;
    if (!strcmp(x.name, "rPrDefault")) {
      int d0 = x.depth;
      while (xml_next(&x) != XT_EOF && !(x.type == XT_END && x.depth < d0))
        if (x.type == XT_START && !strcmp(x.name, "rPr")) parse_rpr(&x, c, &c->defr, NULL);
    } else if (!strcmp(x.name, "pPrDefault")) {
      int d0 = x.depth;
      while (xml_next(&x) != XT_EOF && !(x.type == XT_END && x.depth < d0))
        if (x.type == XT_START && !strcmp(x.name, "pPr")) parse_ppr(&x, c, &c->defp, NULL, NULL);
    } else if (!strcmp(x.name, "style")) {
      RawStyle *s;
      const char *t = xml_attr(&x, "type"), *def = xml_attr(&x, "default");
      int d0 = x.depth;
      if (c->nst >= cap) {
        int nc = cap ? cap * 2 : 64;
        RawStyle *ns = (RawStyle *)realloc(c->st, nc * sizeof(RawStyle));
        if (!ns) { xml_skip(&x); continue; }
        c->st = ns; cap = nc;
      }
      s = &c->st[c->nst];
      memset(s, 0, sizeof *s);
      s->type = t && !strcmp(t, "paragraph") ? 0 : t && !strcmp(t, "character") ? 1 : 2;
      s->isdefault = def && (!strcmp(def, "1") || str_ieq(def, "true"));
      copy_attr(&x, "styleId", s->id, sizeof s->id);
      if (!s->id[0]) { xml_skip(&x); continue; }
      if (t && !strcmp(t, "table") && (s->ts = (TableStyle *)calloc(1, sizeof(TableStyle)))) s->ts->c[TC_WHOLE].used = 1;
      while (xml_next(&x) != XT_EOF) {
        if (x.type == XT_END && x.depth < d0) break;
        if (x.type != XT_START) continue;
        if (!strcmp(x.name, "name")) copy_attr(&x, "val", s->name, sizeof s->name);
        else if (!strcmp(x.name, "basedOn")) copy_attr(&x, "val", s->based, sizeof s->based);
        else if (!strcmp(x.name, "next")) copy_attr(&x, "val", s->next, sizeof s->next);
        else if (!strcmp(x.name, "qFormat")) s->quick = 1;
        else if (s->ts && (!strcmp(x.name, "pPr") || !strcmp(x.name, "rPr") || !strcmp(x.name, "tblPr") || !strcmp(x.name, "tcPr"))) {
          parse_tcond(&x, c, s->ts, &s->ts->c[TC_WHOLE], s);
          continue;
        }
        else if (s->ts && !strcmp(x.name, "tblStylePr")) {
          int k = tcond_kind(xml_attr(&x, "type")), d1 = x.depth;
          if (k < 0) { xml_skip(&x); continue; }
          while (xml_next(&x) != XT_EOF) {
            if (x.type == XT_END && x.depth < d1) break;
            if (x.type == XT_START) parse_tcond(&x, c, s->ts, &s->ts->c[k], NULL);
          }
          continue;
        }
        else if (!strcmp(x.name, "pPr")) { parse_ppr(&x, c, &s->p, NULL, NULL); continue; }
        else if (!strcmp(x.name, "rPr")) { parse_rpr(&x, c, &s->r, NULL); continue; }
        xml_skip(&x);
      }
      if (!s->name[0]) strcpy(s->name, s->id);
      if (s->ts) { strcpy(s->ts->id, s->id); strcpy(s->ts->name, s->name); strcpy(s->ts->based, s->based); }
      c->nst++;
    }
  }
  xml_free(&x);
}
/* the document's paragraph and character styles into the Doc (what each sets itself, and what it is
   based on): its default paragraph style becomes style 0 */
static void install_styles(Ctx *c)
{
  Doc *d = c->d;
  int i, k, def = -1;
  for (i = 0; i < c->nst; i++) resolve(c, i);
  for (i = 0; i < c->nst; i++) if (c->st[i].type == 0 && c->st[i].isdefault) { def = i; break; }
  if (def < 0) def = raw_find(c, "Normal", 0);
  base_fmt(c, &d->defpf, &d->defcf);
  if (def < 0) {
    /* no styles part: Normal is the document's defaults; the other built-in styles stay */
    d->styles[0].pmask = d->styles[0].cmask = 0;
    doc_resolve_styles(d);
  }
  for (i = 0; i < c->nst; i++) {
    RawStyle *s = &c->st[i];
    Style ds;
    if (s->type > 1 || (s->type == 1 && s->isdefault)) continue;      /* (the default character style is "none") */
    memset(&ds, 0, sizeof ds);
    strncpy(ds.id, s->id, sizeof ds.id - 1);
    strncpy(ds.name, s->name, sizeof ds.name - 1);
    ds.type = s->type == 0 ? ST_PARA : ST_CHAR;
    ds.based = NO_STYLE; ds.quick = (u8)s->quick;
    ds.pmask = s->type == 0 ? s->p.set & PM_STYLED : 0;
    ds.opf = s->p.pf;
    if (s->type == 0 && s->p.has_list) { ds.pmask |= PM_LIST; ds.opf.list = (u16)list_for(c, s->p.numid); ds.opf.level = s->p.pf.level; }
    ds.cmask = s->r.set & CM_LOOK; ds.ocf = s->r.cf;
    ds.pf = s->epf; ds.cf = s->ecf;
    if (i == def) { ds.builtin = 1; d->styles[0] = ds; continue; }
    k = doc_style(d, s->id);
    if (k >= 0 && d->styles[k].type != ds.type) { strncat(ds.id, ds.type == ST_CHAR ? "Char" : "Para", sizeof ds.id - 1 - strlen(ds.id)); }
    doc_add_style(d, &ds);
  }
  /* bases and next styles by index, now that every style has one */
  for (i = 0; i < c->nst; i++) {
    RawStyle *s = &c->st[i];
    int me, nx, b;
    if (s->type > 1 || (s->type == 1 && s->isdefault)) continue;
    me = i == def ? 0 : doc_style(d, s->id);
    if (me < 0) continue;
    if (me > 0 && s->based[0]) {
      int rb = raw_find(c, s->based, s->type);
      b = rb == def && s->type == 0 ? 0 : doc_style(d, s->based);
      if (b >= 0 && b != me && d->styles[b].type == d->styles[me].type) d->styles[me].based = (u8)b;
    }
    if (s->type != 0) continue;
    nx = s->next[0] ? (raw_find(c, s->next, 0) == def ? 0 : doc_style(d, s->next)) : me;
    d->styles[me].next = (u8)(nx >= 0 && d->styles[nx].type == ST_PARA ? nx : me);
  }
  doc_resolve_styles(d);
  for (i = 0; i < c->nst; i++) if (c->st[i].ts) doc_add_tstyle(d, c->st[i].ts);
}
/* free the styles as read (the table styles have been copied into the Doc by then) */
static void free_raw(Ctx *c)
{
  int i;
  for (i = 0; i < c->nst; i++) free(c->st[i].ts);
  free(c->st);
  c->st = NULL; c->nst = 0;
}

/* ---- numbering */
/* replace a bullet level's text when it is a Symbol or Wingdings character with the Unicode
   character it looks like, so it shows without those fonts (text is UTF-8, cap bytes) */
static void map_bullet(char *text, size_t cap)
{
  /* bullets in symbol fonts use the private use area: show them as the Unicode characters they look like */
  int used;
  u32 cp = utf8_get(text, strlen(text), &used), to = 0;
  if (cp >= 0xF000 && cp <= 0xF0FF) {
    switch (cp & 0xFF) {
    case 0xB7: case 0x9F: case 0x6C: to = 0x2022; break;   /* Symbol/Wingdings bullets */
    case 0xA7: case 0x6E: to = 0x25AA; break;              /* squares */
    case 0xD8: case 0xE0: to = 0x25BA; break;              /* arrows */
    case 0xFC: to = 0x2713; break;                         /* tick */
    case 0x76: to = 0x2756; break;
    default: to = 0x2022;
    }
  } else if (cp == 0xB7) to = 0x2022;
  if (to && cap >= 4) text[utf8_put(text, to)] = 0;
}
/* a w:lvl of an abstract numbering: start number, number format, level text (with its %1 style
   placeholders), indents (twips) and the bullet's font. Unset fields default to decimal from 1. */
static void parse_lvl(Xml *x, Ctx *c, ListLevel *lv)
{
  int d0 = x->depth;
  i32 k;
  lv->start = 1; lv->fmt = NF_DECIMAL; lv->left = 0; lv->hanging = 0;
  while (xml_next(x) != XT_EOF) {
    const char *v;
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    v = xml_attr(x, "val");
    if (!strcmp(x->name, "start") && num(v, &k) && k >= 0 && k < 65536) lv->start = (u16)k;
    else if (!strcmp(x->name, "numFmt") && v) {
      lv->fmt = (u8)(!strcmp(v, "bullet") ? NF_BULLET : !strcmp(v, "lowerLetter") ? NF_LOWER_LETTER : !strcmp(v, "upperLetter") ? NF_UPPER_LETTER :
                     !strcmp(v, "lowerRoman") ? NF_LOWER_ROMAN : !strcmp(v, "upperRoman") ? NF_UPPER_ROMAN : !strcmp(v, "none") ? NF_NONE : NF_DECIMAL);
    }
    else if (!strcmp(x->name, "lvlText") && v) { strncpy(lv->text, v, sizeof lv->text - 1); lv->text[sizeof lv->text - 1] = 0; }
    else if (!strcmp(x->name, "ind")) {
      if (attr_num(x, "left", &k) || attr_num(x, "start", &k)) lv->left = k;
      if (attr_num(x, "hanging", &k)) lv->hanging = k;
      if (attr_num(x, "firstLine", &k)) lv->hanging = -k;
    }
    else if (!strcmp(x->name, "rFonts")) {
      const char *f = xml_attr(x, "ascii");
      if (!f) f = xml_attr(x, "hAnsi");
      if (f) { strncpy(lv->font, f, sizeof lv->font - 1); lv->font[sizeof lv->font - 1] = 0; }
    }
    if (strcmp(x->name, "pPr") && strcmp(x->name, "rPr")) xml_skip(x);   /* descend into pPr and rPr */
  }
  if (lv->fmt == NF_BULLET) map_bullet(lv->text, sizeof lv->text);
}
/* numbering.xml: the abstract numberings first (Word writes them before the w:num elements), then
   each w:num becomes a Doc list carrying its abstract numbering's levels, under the num's id.
   Level overrides (lvlOverride) are not read. */
static void parse_numbering(Ctx *c, const char *xml, size_t n)
{
  Xml x;
  int cap = 0;
  i32 k;
  xml_init(&x, xml, n);
  while (xml_next(&x) != XT_EOF) {
    if (x.type != XT_START) continue;
    if (!strcmp(x.name, "abstractNum") && attr_num(&x, "abstractNumId", &k)) {
      AbsNum *a;
      int d0 = x.depth;
      if (c->nabs >= cap) {
        int nc = cap ? cap * 2 : 16;
        AbsNum *na = (AbsNum *)realloc(c->abs, nc * sizeof(AbsNum));
        if (!na) { xml_skip(&x); continue; }
        c->abs = na; cap = nc;
      }
      a = &c->abs[c->nabs++];
      memset(a, 0, sizeof *a);
      a->id = (u16)k;
      while (xml_next(&x) != XT_EOF) {
        i32 il;
        if (x.type == XT_END && x.depth < d0) break;
        if (x.type == XT_START && !strcmp(x.name, "lvl") && attr_num(&x, "ilvl", &il) && il >= 0 && il < 9) parse_lvl(&x, c, &a->lvl[il]);
        else if (x.type == XT_START) xml_skip(&x);
      }
    } else if (!strcmp(x.name, "num") && attr_num(&x, "numId", &k)) {
      ListDef l;
      int d0 = x.depth, i, found = 0;
      i32 an = -1;
      memset(&l, 0, sizeof l);
      l.id = (u16)k;
      while (xml_next(&x) != XT_EOF) {
        if (x.type == XT_END && x.depth < d0) break;
        if (x.type == XT_START && !strcmp(x.name, "abstractNumId")) attr_num(&x, "val", &an);
      }
      for (i = 0; i < c->nabs; i++) if (c->abs[i].id == an) { memcpy(l.lvl, c->abs[i].lvl, sizeof l.lvl); found = 1; break; }
      if (found) doc_add_list(c->d, &l);
    }
  }
  xml_free(&x);
}
static const char *const clrnames[12] = { "dk1", "lt1", "dk2", "lt2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6", "hlink", "folHlink" };
/* theme1.xml: the twelve scheme colours (c->clr, clrnames order; a system colour by its lastClr)
   and the major and minor Latin fonts that theme font references resolve to */
static void parse_theme(Ctx *c, const char *xml, size_t n)
{
  Xml x;
  int mode = 0, cl = -1, k;
  xml_init(&x, xml, n);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START) {
      for (k = 0; k < 12; k++) if (!strcmp(x.name, clrnames[k])) cl = k;
      if (cl >= 0 && (!strcmp(x.name, "srgbClr") || !strcmp(x.name, "sysClr"))) {
        u32 v = hexcolor(xml_attr(&x, !strcmp(x.name, "sysClr") ? "lastClr" : "val"));
        if (v != COLOR_AUTO) c->clr[cl] = v;
        cl = -1;
      }
    }
    if (x.type == XT_START && !strcmp(x.name, "majorFont")) mode = 1;
    else if (x.type == XT_START && !strcmp(x.name, "minorFont")) mode = 2;
    else if (x.type == XT_END && (!strcmp(x.name, "majorFont") || !strcmp(x.name, "minorFont"))) mode = 0;
    else if (x.type == XT_START && mode && !strcmp(x.name, "latin"))
      copy_attr(&x, "typeface", mode == 1 ? c->major : c->minor, sizeof c->major);
  }
  xml_free(&x);
}

/* ---- the document body */
typedef struct {
  Ctx *c;
  Para *p;                          /* being built (NULL until there is something) */
  PPr ppr; RPr mark;
  char pstyle[40];
  int style;                        /* index in the Doc, or -1 */
  ParaFmt pf; CharFmt pcf;          /* the paragraph's formatting and its style's character formatting */
  CharFmt markcf;
  Para **out; u32 nout, outcap;     /* finished paragraphs */
  Cell cell;                        /* the table cell being read (table 0: none) */
  Cell outer;                       /* in a table nested in a cell: that cell */
  int tstyle;                       /* its table's style (RawStyle index), or -1 */
  int tsnum; u16 tlook; int tnrows, tncols;   /* and as the Doc has it, its look and size (for the style's parts) */
  u8 rev; u16 rid;                  /* inside w:ins or w:del: the runs' tracked change */
  u16 link;                         /* inside w:hyperlink (or a fldSimple HYPERLINK): the runs' link number */
} PCtx;
/* (the work packages' reading hooks, further down) */
static int docx_read_block_ext(Xml *x, PCtx *pc), docx_read_elem_ext(Xml *x, PCtx *pc);

/* add a finished paragraph to pc->out, which takes ownership; 0 if out of memory (p is freed then) */
static int emit(PCtx *pc, Para *p)
{
  if (pc->nout >= pc->outcap) {
    u32 nc = pc->outcap ? pc->outcap * 2 : 64;
    Para **no = (Para **)realloc(pc->out, nc * sizeof(Para *));
    if (!no) { para_free(p); return 0; }
    pc->out = no; pc->outcap = nc;
  }
  pc->out[pc->nout++] = p;
  return 1;
}
/* work out a paragraph's formatting once its pPr has been read: its style's, then (in a table)
   the table style's, then its own pPr and list. Also sets the mark's character format and, when
   the pPr ended a section, adds the section to the Doc and records its header and footer
   relationships for later. */
static void para_begin(PCtx *pc)
{
  Ctx *c = pc->c;
  Doc *d = c->d;
  int rs;
  pc->style = pc->pstyle[0] ? doc_style(d, pc->pstyle) : 0;
  rs = pc->pstyle[0] ? raw_find(c, pc->pstyle, 0) : -1;
  if (pc->style >= 0) { pc->pf = d->styles[pc->style].pf; pc->pcf = d->styles[pc->style].cf; pc->pf.style = (u8)pc->style; }
  else if (rs >= 0) { pc->pf = c->st[rs].epf; pc->pcf = c->st[rs].ecf; pc->pf.style = 0; }   /* a style that didn't fit */
  else { pc->pf = d->styles[0].pf; pc->pcf = d->styles[0].cf; pc->pf.style = 0; }
  /* in a table, its style's paragraph formatting comes before the paragraph's own (for paragraphs in
     the default style, as Word) */
  if (pc->cell.table && pc->tstyle >= 0 && pc->pf.style == 0) {
    int k = pc->tstyle, guard = 0;
    int chain[8], n = 0;
    while (k >= 0 && n < 8 && guard++ < 8) { chain[n++] = k; k = c->st[k].based[0] ? raw_find(c, c->st[k].based, 2) : -1; }
    while (n > 0) { pf_set(&pc->pf, &c->st[chain[--n]].p); cf_set(&pc->pcf, &c->st[chain[n]].r); }
  }
  /* and the style's parts the cell is in (the heading row, a band...) */
  if (pc->cell.table && pc->tsnum && pc->pf.style == 0) {
    CellLook lk;
    tstyle_cell(d, pc->tsnum, pc->tlook, pc->cell.row, pc->cell.col, pc->cell.span, pc->tnrows, pc->tncols, NULL, &lk);
    cf_apply(&pc->pcf, lk.cmask, &lk.cf);
    if (lk.align_set) pc->pf.align = lk.align;
  }
  pf_set(&pc->pf, &pc->ppr);
  apply_list(c, &pc->pf, &pc->ppr);
  pc->markcf = pc->pcf;
  cf_set(&pc->markcf, &pc->mark);
  pc->pf.cell = pc->cell;
  pc->pf.outer = pc->outer;
  /* a section ends with this paragraph (never one in a table: Word doesn't allow it) */
  pc->pf.sect = pc->ppr.has_sect && !pc->cell.table && !c->story ? (u16)doc_add_sect(d, &pc->ppr.sect) : 0;
  if (pc->pf.sect) {
    /* its headers' and footers' relationships, for after the body */
    void *ns = realloc(c->sid, pc->pf.sect * sizeof *c->sid);
    if (ns) {
      c->sid = ns;
      while (c->nsid < pc->pf.sect) memset(c->sid[c->nsid++], 0, sizeof c->sid[0]);
      memcpy(c->sid[pc->pf.sect - 1], c->pend, sizeof c->pend);
    }
  }
}
/* the paragraph being built, created on first use with the formatting para_begin worked out;
   NULL if out of memory */
static Para *cur(PCtx *pc)
{
  if (!pc->p) pc->p = para_new(&pc->pf, &pc->markcf);
  return pc->p;
}
/* append UTF-8 text in format f to the current paragraph; line breaks in the XML are plain
   whitespace, so each becomes a space */
static void run_text(PCtx *pc, const char *s, size_t n, const CharFmt *f)
{
  Para *p = cur(pc);
  size_t i, a = 0;
  if (!p) return;
  for (i = 0; i <= n; i++)                /* newlines in text are layout's whitespace in XML: spaces */
    if (i == n || s[i] == '\n' || s[i] == '\r') {
      if (i > a) para_append(p, s + a, (u32)(i - a), f);
      if (i < n) para_append(p, " ", 1, f);
      a = i + 1;
    }
}
static void parse_children(Xml *x, PCtx *pc);
static int comment_by_id(Ctx *c, i32 id);
/* a header's w:pict: Word's text watermark (a VML text path shape named PowerPlusWaterMarkObject)
   becomes the document's; 1 if it was one (the pict read to its end) */
static int parse_watermark(Xml *x, Ctx *c)
{
  int d0 = x->depth, is = 0;
  Watermark w;
  memset(&w, 0, sizeof w);
  w.color = 0xC0C0C0; w.horizontal = 1;
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "shape")) {
      const char *id = xml_attr(x, "id"), *st = xml_attr(x, "style"), *fc = xml_attr(x, "fillcolor");
      if (id && strstr(id, "WaterMark")) is = 1;
      if (st && strstr(st, "rotation:315")) w.horizontal = 0;
      if (fc && fc[0] == '#') w.color = hexcolor(fc + 1) == COLOR_AUTO ? 0xC0C0C0 : hexcolor(fc + 1);
      else if (fc && !strncmp(fc, "gray", 4)) w.color = 0x808080;
    } else if (!strcmp(x->name, "textpath")) {
      const char *str = xml_attr(x, "string"), *st = xml_attr(x, "style"), *ff;
      if (str) { strncpy(w.text, str, sizeof w.text - 1); w.text[sizeof w.text - 1] = 0; }
      if (st && (ff = strstr(st, "font-family:"))) {
        size_t i = 0;
        ff += 12;
        while (*ff == '"' || *ff == '\'' || *ff == ' ') ff++;
        while (ff[i] && ff[i] != '"' && ff[i] != '\'' && ff[i] != ';' && i < sizeof w.font - 1) { w.font[i] = ff[i]; i++; }
        w.font[i] = 0;
      }
    }
  }
  if (!is) { c->lost |= LOST_PICTURES; return 1; }
  if (w.text[0] && !c->d->wm.text[0]) c->d->wm = w;
  return 1;
}
/* a DrawingML colour element (srgbClr, schemeClr, sysClr, prstClr) with its shade, tint and luminance changes */
/* (x at the colour's START; read to its END.) The modifiers are in 1000ths of a percent:
   shade darkens towards black, tint lightens towards white, lumMod and lumOff scale and offset
   the brightness (done here per channel, close enough to Word's HSL maths for theme colours).
   COLOR_AUTO for a colour it can't work out. */
static u32 dml_color(Xml *x, Ctx *c)
{
  int d0 = x->depth, k;
  const char *n = x->name, *v = xml_attr(x, "val");
  u32 col = COLOR_AUTO, r, g, b;
  i32 shade = 100000, tint = 100000, lmod = 100000, loff = 0, t;
  if (!strcmp(n, "srgbClr")) col = hexcolor(v);
  else if (!strcmp(n, "sysClr")) col = hexcolor(xml_attr(x, "lastClr"));
  else if (!strcmp(n, "schemeClr") && v) {
    const char *m = !strcmp(v, "tx1") ? "dk1" : !strcmp(v, "bg1") ? "lt1" : !strcmp(v, "tx2") ? "dk2" : !strcmp(v, "bg2") ? "lt2" : v;
    for (k = 0; k < 12; k++) if (!strcmp(m, clrnames[k])) col = c->clr[k];
  } else if (!strcmp(n, "prstClr") && v) col = !strcmp(v, "white") ? 0xFFFFFF : !strcmp(v, "red") ? 0xFF0000 : !strcmp(v, "blue") ? 0x0000FF : 0;
  if (x->pending_end) { xml_skip(x); return col; }
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (attr_num(x, "val", &t)) {
      if (!strcmp(x->name, "shade")) shade = t;
      else if (!strcmp(x->name, "tint")) tint = t;
      else if (!strcmp(x->name, "lumMod")) lmod = t;
      else if (!strcmp(x->name, "lumOff")) loff = t;
    }
  }
  if (col == COLOR_AUTO) return col;
  r = (col >> 16) & 255; g = (col >> 8) & 255; b = col & 255;
  if (shade != 100000) { r = (u32)(r * shade / 100000); g = (u32)(g * shade / 100000); b = (u32)(b * shade / 100000); }
  if (tint != 100000) { r = (u32)(255 - (255 - r) * tint / 100000); g = (u32)(255 - (255 - g) * tint / 100000); b = (u32)(255 - (255 - b) * tint / 100000); }
  if (lmod != 100000 || loff) {
    i32 rr = (i32)(r * lmod / 100000) + 255 * loff / 100000, gg = (i32)(g * lmod / 100000) + 255 * loff / 100000, bb = (i32)(b * lmod / 100000) + 255 * loff / 100000;
    r = (u32)(rr < 0 ? 0 : rr > 255 ? 255 : rr); g = (u32)(gg < 0 ? 0 : gg > 255 ? 255 : gg); b = (u32)(bb < 0 ? 0 : bb > 255 ? 255 : bb);
  }
  return r << 16 | g << 8 | b;
}
/* whether an element name is one of the DrawingML colour elements dml_color reads */
static int is_color(const char *n) { return !strcmp(n, "srgbClr") || !strcmp(n, "schemeClr") || !strcmp(n, "sysClr") || !strcmp(n, "prstClr"); }
static Para *parse_para(Xml *x, PCtx *pc);
static void parse_nested(Xml *x, PCtx *pc);
/* a text box's paragraphs (w:txbxContent) */
/* (x just past the START; read to its END.) Returns a malloc'd Story owning the paragraphs, NULL
   if out of memory. Nested tables are flattened into the box's paragraphs; page fields in a text
   box are not treated as a header's, even inside one. */
static Story *parse_txbx(Xml *x, Ctx *c)
{
  PCtx pc;
  Story *st;
  int d0 = x->depth, keep = c->story;
  memset(&pc, 0, sizeof pc);
  pc.c = c; pc.tstyle = -1;
  c->story = 0;
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "p")) { Para *q = parse_para(x, &pc); if (q) emit(&pc, q); }
    else if (!strcmp(x->name, "tbl")) parse_nested(x, &pc);
    else if (strcmp(x->name, "sdt") && strcmp(x->name, "sdtContent")) xml_skip(x);
  }
  c->story = keep;
  if (!(st = (Story *)calloc(1, sizeof(Story)))) { u32 i; for (i = 0; i < pc.nout; i++) para_free(pc.out[i]); free(pc.out); return NULL; }
  st->p = pc.out; st->np = pc.nout;
  return st;
}
static const char *const prst_names[SH_N] = { "rect", "roundRect", "ellipse", "line", "straightConnector1", "triangle", "diamond", "rightArrow", "star5",
  "pentagon", "hexagon", "octagon", "parallelogram", "trapezoid", "rtTriangle", "leftArrow", "upArrow", "downArrow",
  "plus", "heart", "wedgeRectCallout", "chevron", "homePlate", "star4" };
/* a shape, picture or group being read: its settings, and what tells how to finish them */
typedef struct {
  Image im;
  int havefill, haveline, lnfill, inln, insp, instyle, inxfrm, xfrmdone;
  u32 stylefill, styleline;
  char embed[40];
  i32 offx, offy, extx, exty, choffx, choffy, chextx, chexty;   /* its a:xfrm (EMU) */
} DShape;
/* a shape being read starts as a picture with Image's defaults, no wrap distances left and right,
   and its fill and line still to come from its style */
static void dshape_init(DShape *s)
{
  memset(s, 0, sizeof *s);
  img_defaults(&s->im);
  s->im.dl = s->im.dr = 0;
  s->lnfill = -1;
  s->stylefill = s->styleline = COLOR_AUTO;
}
/* the picture a blip names, into im (its own data); 0 if there is none to read */
static int load_blip(Ctx *c, const char *embed, Image *im)
{
  char path[300];
  size_t len = 0;
  u8 *data = NULL;
  u32 w, h;
  int t;
  if (!embed[0] || !c->rels || !c->z) return 0;
  rel_by_id(c->rels, c->rn, embed, c->base, path, sizeof path);
  if (!path[0] || !(data = (u8 *)zip_read(c->z, zip_find(c->z, path), &len))) return 0;
  if ((t = img_info(data, (u32)len, &w, &h)) == IMG_NONE) { free(data); return 0; }
  im->data = data; im->len = (u32)len; im->type = (u8)t; im->pw = w; im->ph = h; im->shared = 0;
  return 1;
}
static void parse_group(Xml *x, PCtx *pc, DShape *g);
/* one element inside a shape (or the drawing round it): 1 if its contents were read here too */
static int dshape_elem(Xml *x, PCtx *pc, DShape *s)
{
  Ctx *c = pc->c;
  const char *n = x->name;
  Image *im = &s->im;
  i32 v;
  int k;
  if (!strcmp(n, "blip") && !s->embed[0]) copy_attr(x, "embed", s->embed, sizeof s->embed);
  else if (!strcmp(n, "srcRect")) {
    if (attr_num(x, "l", &v)) im->crop[0] = v;
    if (attr_num(x, "t", &v)) im->crop[1] = v;
    if (attr_num(x, "r", &v)) im->crop[2] = v;
    if (attr_num(x, "b", &v)) im->crop[3] = v;
  }
  else if (!strcmp(n, "xfrm") && !s->xfrmdone) {
    const char *fh = xml_attr(x, "flipH"), *fv = xml_attr(x, "flipV");
    if (attr_num(x, "rot", &v)) { v = (v / 6000) % 3600; im->rot = (u16)(v < 0 ? v + 3600 : v); }   /* 60000ths of a degree */
    if (fh && (*fh == '1' || *fh == 't')) im->flip |= 1;
    if (fv && (*fv == '1' || *fv == 't')) im->flip |= 2;
    s->inxfrm = 1;
  }
  else if (s->inxfrm && !strcmp(n, "off")) { attr_num(x, "x", &s->offx); attr_num(x, "y", &s->offy); }
  else if (s->inxfrm && !strcmp(n, "ext")) { attr_num(x, "cx", &s->extx); attr_num(x, "cy", &s->exty); }
  else if (s->inxfrm && !strcmp(n, "chOff")) { attr_num(x, "x", &s->choffx); attr_num(x, "y", &s->choffy); }
  else if (s->inxfrm && !strcmp(n, "chExt")) { attr_num(x, "cx", &s->chextx); attr_num(x, "cy", &s->chexty); }
  else if (!strcmp(n, "wsp")) im->kind = OBJ_SHAPE;
  else if (!strcmp(n, "cNvSpPr")) { const char *t = xml_attr(x, "txBox"); if (t && (*t == '1' || *t == 't')) im->kind = OBJ_TEXTBOX; }
  else if (!strcmp(n, "spPr")) s->insp = 1;
  else if (!strcmp(n, "style")) s->instyle = 1;
  else if (!strcmp(n, "prstGeom")) {
    const char *pr = xml_attr(x, "prst");
    for (k = 0; pr && k < SH_N; k++) if (!strcmp(pr, prst_names[k])) im->shape = (u8)k;
    if (pr && (!strcmp(pr, "line") || !strncmp(pr, "straightConnector", 17))) im->shape = (u8)(im->shape == SH_ARROW ? SH_ARROW : SH_LINE);
  }
  else if (!strcmp(n, "ln") && s->insp) {
    s->inln = 1; s->haveline = 1;
    if (attr_num(x, "w", &v)) im->lwidth = (u8)(v / 1587 > 96 ? 96 : v / 1587 < 1 ? 1 : v / 1587);     /* EMU: 12700 a point */
    if (im->lstyle == PB_NONE) im->lstyle = PB_SINGLE;
  }
  else if (!strcmp(n, "tailEnd") || !strcmp(n, "headEnd")) { const char *t = xml_attr(x, "type"); if (t && strcmp(t, "none") && im->shape == SH_LINE) im->shape = SH_ARROW; }
  else if (!strcmp(n, "prstDash") && s->inln) { const char *t = xml_attr(x, "val"); if (t && strcmp(t, "solid")) im->lstyle = !strcmp(t, "dot") || !strcmp(t, "sysDot") ? PB_DOTTED : PB_DASHED; }
  else if (!strcmp(n, "noFill")) { if (s->inln) { s->lnfill = 0; } else if (s->insp) { s->havefill = 1; im->fill = COLOR_AUTO; } }
  else if (!strcmp(n, "solidFill") && (s->insp || s->inln)) {
    int d1 = x->depth, line = s->inln;
    while (xml_next(x) != XT_EOF) {
      if (x->type == XT_END && x->depth < d1) break;
      if (x->type == XT_START && is_color(x->name)) { u32 col = dml_color(x, c); if (line) { im->lcolor = col; s->lnfill = 1; } else { im->fill = col; s->havefill = 1; } }
    }
    return 1;
  }
  else if (s->instyle && (!strcmp(n, "lnRef") || !strcmp(n, "fillRef"))) {
    int d1 = x->depth, line = n[0] == 'l', ref = attr_num(x, "idx", &v) ? (int)v : 1;
    while (xml_next(x) != XT_EOF) {
      if (x->type == XT_END && x->depth < d1) break;
      if (x->type == XT_START && is_color(x->name)) { u32 col = dml_color(x, c); if (ref) { if (line) s->styleline = col; else s->stylefill = col; } }
    }
    return 1;
  }
  else if (!strcmp(n, "txbxContent")) {
    if (im->text) { story_free(im->text); free(im->text); }
    im->text = parse_txbx(x, c);
    if (im->kind == OBJ_SHAPE && im->shape == SH_RECT && !s->havefill) im->kind = OBJ_TEXTBOX;
    return 1;
  }
  else if (!strcmp(n, "wgp") || !strcmp(n, "grpSp")) { parse_group(x, pc, s); return 1; }
  return 0;
}
/* an END inside a shape: leave the element dshape_elem noted we were in (line, shape properties,
   style, transform) */
static void dshape_end(Xml *x, DShape *s)
{
  const char *n = x->name;
  if (!strcmp(n, "ln")) s->inln = 0;
  else if (!strcmp(n, "spPr")) s->insp = 0;
  else if (!strcmp(n, "style")) s->instyle = 0;
  else if (!strcmp(n, "xfrm") && s->inxfrm) { s->inxfrm = 0; s->xfrmdone = 1; }
}
/* the settings a shape's style gives where it gives none itself */
static void dshape_finish(DShape *s)
{
  Image *im = &s->im;
  if (im->kind != OBJ_PICTURE && im->kind != OBJ_GROUP) {
    if (!s->havefill) im->fill = s->stylefill;
    if (!s->haveline && s->styleline != COLOR_AUTO) { im->lstyle = PB_SINGLE; im->lwidth = 6; im->lcolor = s->styleline; }
    else if (s->haveline && s->lnfill < 0 && s->styleline != COLOR_AUTO) im->lcolor = s->styleline;
    if (im->shape == SH_LINE || im->shape == SH_ARROW) im->fill = COLOR_AUTO;
  }
  if (s->lnfill == 0) im->lstyle = PB_NONE;
}
/* a group (wpg:wgp, or wpg:grpSp in one): the objects in it, each placed in the group's child space */
static void parse_group(Xml *x, PCtx *pc, DShape *g)
{
  int d0 = x->depth;
  Image *gi = &g->im;
  gi->kind = OBJ_GROUP;
  while (xml_next(x) != XT_EOF) {
    const char *n = x->name;
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type == XT_END) { dshape_end(x, g); continue; }
    if (x->type != XT_START) continue;
    if (!strcmp(n, "wsp") || !strcmp(n, "pic") || !strcmp(n, "grpSp")) {
      /* one object of the group */
      DShape k;
      Image *nk;
      int d1 = x->depth, isgrp = n[0] == 'g';
      dshape_init(&k);
      k.im.kind = n[0] == 'w' ? OBJ_SHAPE : OBJ_PICTURE;
      if (isgrp) parse_group(x, pc, &k);
      else
        while (xml_next(x) != XT_EOF) {
          if (x->type == XT_END && x->depth < d1) break;
          if (x->type == XT_END) { dshape_end(x, &k); continue; }
          if (x->type == XT_START) dshape_elem(x, pc, &k);
        }
      dshape_finish(&k);
      if (k.im.kind == OBJ_PICTURE && !load_blip(pc->c, k.embed, &k.im)) { img_release(&k.im); pc->c->lost |= LOST_PICTURES; continue; }
      k.im.x = (k.offx - g->choffx) / 635; k.im.y = (k.offy - g->choffy) / 635;
      k.im.cx = (k.extx + 317) / 635; k.im.cy = (k.exty + 317) / 635;
      if (k.im.kind == OBJ_GROUP) { k.im.chw = (k.chextx + 317) / 635; k.im.chh = (k.chexty + 317) / 635; }
      if (!(nk = (Image *)realloc(gi->kids, (gi->nkids + 1) * sizeof(Image)))) { img_release(&k.im); continue; }
      gi->kids = nk;
      gi->kids[gi->nkids++] = k.im;
      continue;
    }
    if (dshape_elem(x, pc, g)) continue;
  }
  gi->chw = (g->chextx + 317) / 635; gi->chh = (g->chexty + 317) / 635;
}
/* wp:wrapPolygon: the outline the text wraps round, 21600ths of the object's box */
static void parse_polygon(Xml *x, Image *im)
{
  int d0 = x->depth;
  i32 px, py, *np;
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START || (strcmp(x->name, "start") && strcmp(x->name, "lineTo"))) continue;
    if (!attr_num(x, "x", &px) || !attr_num(x, "y", &py) || im->npoly >= 4000) continue;
    if (!(np = (i32 *)realloc(im->poly, (im->npoly + 1) * 2 * sizeof(i32)))) break;
    im->poly = np;
    np[2 * im->npoly] = px; np[2 * im->npoly + 1] = py;
    im->npoly++;
  }
  /* the last point back at the first is left out; a box is no outline of its own */
  if (im->npoly > 1 && im->poly[0] == im->poly[2 * im->npoly - 2] && im->poly[1] == im->poly[2 * im->npoly - 1]) im->npoly--;
  if (im->npoly < 3) { free(im->poly); im->poly = NULL; im->npoly = 0; }
}
/* a w:drawing: a picture, shape, text box or group, in line or floating; its reference goes into the text */
static void parse_drawing(Xml *x, PCtx *pc, const CharFmt *f)
{
  Ctx *c = pc->c;
  int d0 = x->depth, anchor = 0, posh = 0, k;
  i32 cx = 0, cy = 0, v;
  char wrapname[24] = "";
  DShape s;
  Image *im = &s.im;
  dshape_init(&s);
  while (xml_next(x) != XT_EOF) {
    const char *n = x->name;
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type == XT_END) { dshape_end(x, &s); continue; }
    if (x->type != XT_START) continue;
    if (!strcmp(n, "anchor") || !strcmp(n, "inline")) {
      anchor = n[0] == 'a';
      if (attr_num(x, "distL", &v)) im->dl = (v + 317) / 635;
      if (attr_num(x, "distR", &v)) im->dr = (v + 317) / 635;
      if (attr_num(x, "distT", &v)) im->dt = (v + 317) / 635;
      if (attr_num(x, "distB", &v)) im->db = (v + 317) / 635;
      if (anchor) { const char *b = xml_attr(x, "behindDoc"); im->wrap = b && (*b == '1' || *b == 't') ? WRAP_BEHIND : WRAP_FRONT; }
      if (anchor && attr_num(x, "relativeHeight", &v) && v >= 0) im->z = (v - 251658240) / 64;     /* (as written below) */
    }
    else if (!strcmp(n, "positionH") || !strcmp(n, "positionV")) {
      const char *r = xml_attr(x, "relativeFrom");
      posh = n[8] == 'H';
      if (posh) im->hrel = (u8)(!r ? POS_COLUMN : !strcmp(r, "page") || !strcmp(r, "leftMargin") || !strcmp(r, "rightMargin") || !strcmp(r, "outsideMargin") || !strcmp(r, "insideMargin") ? POS_PAGE :
                              !strcmp(r, "margin") ? POS_MARGIN : POS_COLUMN);
      else im->vrel = (u8)(!r ? POS_PARA : !strcmp(r, "page") || !strcmp(r, "topMargin") || !strcmp(r, "bottomMargin") ? POS_PAGE : !strcmp(r, "margin") ? POS_MARGIN : POS_PARA);
    }
    else if (!strcmp(n, "posOffset") || !strcmp(n, "align")) {
      int d1 = x->depth, isoff = n[0] == 'p';
      char t[24] = "";
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type == XT_TEXT) snprintf(t, sizeof t, "%.*s", (int)x->text.len, x->text.data);
      }
      if (isoff) { if (num(t, &v)) { if (posh) im->x = v / 635; else im->y = v / 635; } }
      else {
        u8 a = (u8)(!strcmp(t, "center") ? PA_CENTER : (!strcmp(t, "right") || !strcmp(t, "bottom") || !strcmp(t, "outside")) ? PA_END : PA_START);
        if (posh) im->halign = a; else im->valign = a;
      }
      continue;
    }
    else if (!strcmp(n, "extent") && !cx) { attr_num(x, "cx", &cx); attr_num(x, "cy", &cy); }
    else if (!strcmp(n, "wrapPolygon")) { parse_polygon(x, im); continue; }
    else if (!strncmp(n, "wrap", 4) && !wrapname[0] && strlen(n) < sizeof wrapname) strcpy(wrapname, n);
    else if (!strcmp(n, "docPr")) { const char *d = xml_attr(x, "descr"); if (d) { strncpy(im->alt, d, sizeof im->alt - 1); im->alt[sizeof im->alt - 1] = 0; } }
    else if (!strcmp(n, "decorative")) { const char *v = xml_attr(x, "val"); im->decorative = (u8)(!v || *v == '1' || *v == 't'); }   /* (WP24) */
    else dshape_elem(x, pc, &s);
  }
  /* the wrap: none (behind or in front, as behindDoc says), square, tight, through or top and bottom */
  if (!anchor) im->wrap = WRAP_INLINE;
  else if (!strcmp(wrapname, "wrapSquare")) im->wrap = WRAP_SQUARE;
  else if (!strcmp(wrapname, "wrapTight") || !strcmp(wrapname, "wrapThrough")) im->wrap = WRAP_TIGHT;
  else if (!strcmp(wrapname, "wrapTopAndBottom")) im->wrap = WRAP_TOPBOTTOM;
  dshape_finish(&s);
  im->cx = (cx + 317) / 635; im->cy = (cy + 317) / 635;
  if (im->kind == OBJ_GROUP && (!im->chw || !im->chh)) { im->chw = im->cx; im->chh = im->cy; }
  if (im->kind == OBJ_PICTURE && !load_blip(c, s.embed, im)) k = -1;
  else if (im->kind == OBJ_GROUP && !im->nkids) k = -1;
  else k = doc_take_object(c->d, im);           /* the entry takes what was read as it is */
  if (k < 0) img_release(im);
  if (k >= 0) { char t[8]; pic_utf8(t, k); run_text(pc, t, 4, f); }
  else c->lost |= LOST_PICTURES;
}
/* a w:r: its rPr (on top of the paragraph style's character format and any character style), then
   its text, tabs, breaks, drawings, note and comment references and symbols into the current
   paragraph. Complex fields (fldChar begin/separate/end) are followed across runs in c->field: the
   code is collected and left out, and the result kept, except that a PAGE or NUMPAGES field
   becomes Quill's placeholder instead. A page or column break splits the paragraph in two. */
static void parse_run(Xml *x, PCtx *pc)
{
  Ctx *c = pc->c;
  RPr r;
  char rstyle[40] = "";
  CharFmt f;
  int d0 = x->depth;
  memset(&r, 0, sizeof r);
  f = pc->pcf;
  f.rev = pc->rev; f.rid = pc->rid;
  f.link = c->field == 2 && c->fieldlink ? c->fieldlink : pc->link;
  f.bmark = c->bmark;
  while (xml_next(x) != XT_EOF) {
    const char *n = x->name;
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(n, "rPr")) {
      int cs;
      parse_rpr(x, c, &r, rstyle);
      f = pc->pcf;
      if (rstyle[0] && (cs = raw_find(c, rstyle, 1)) >= 0) {
        int k = doc_style(c->d, rstyle);
        cf_set(&f, &c->st[cs].er);
        if (k > 0 && c->d->styles[k].type == ST_CHAR) f.cstyle = (u8)k;
      }
      cf_set(&f, &r);
      f.rev = pc->rev; f.rid = pc->rid;
      f.link = c->field == 2 && c->fieldlink ? c->fieldlink : pc->link;
      f.bmark = c->bmark;
      continue;
    }
    if (!strcmp(n, "fldChar")) {
      const char *t = xml_attr(x, "fldCharType");
      if (t && !strcmp(t, "begin")) { c->field = 1; c->instr[0] = 0; c->fieldkind = 0; c->fieldlink = 0; }
      else if (t && !strcmp(t, "separate")) {
        char url[512];
        c->field = 2;
        /* a HYPERLINK field: what it shows is the link */
        if (hyperlink_field(c->instr, url, sizeof url)) { c->fieldlink = (u16)doc_link(c->d, url); f.link = c->fieldlink; }
        else {
          /* a cross-reference: REF's result is the bookmark's text, linked to it; PAGEREF becomes
             the page-of-a-bookmark placeholder (linked to it, which names it), its result left out */
          char bm[200];
          int rk = ref_field(c->instr, bm, sizeof bm);
          if (rk) {
            snprintf(url, sizeof url, "#%s", bm);
            c->fieldlink = (u16)doc_link(c->d, url);
            f.link = c->fieldlink;
            if (rk == 2) { c->fieldkind = 3; run_text(pc, FIELD_PAGEREF, 3, &f); }
          }
        }
        /* a page number field becomes its placeholder; its result is left out */
        if (!c->fieldkind && (c->fieldkind = field_kind(c->instr))) run_text(pc, c->fieldkind == 1 ? FIELD_PAGE : FIELD_PAGES, 3, &f);
      }
      else if (t && !strcmp(t, "end")) { c->field = 0; c->fieldkind = 0; c->fieldlink = 0; f.link = pc->link; }
    }
    else if (c->field == 1 && !strcmp(n, "instrText")) {
      int d1 = x->depth;
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type == XT_TEXT) { size_t l = strlen(c->instr); snprintf(c->instr + l, sizeof c->instr - l, "%.*s", (int)x->text.len, x->text.data); }
      }
      continue;
    }
    else if (c->field == 1) ;                                  /* field code: left out */
    else if (c->field == 2 && c->fieldkind) ;                  /* a page number's result: the placeholder stands for it */
    else if (!strcmp(n, "t") || (!strcmp(n, "delText") && pc->rev == REV_DEL)) {
      int d1 = x->depth;
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type == XT_TEXT) run_text(pc, x->text.data, x->text.len, &f);
      }
      continue;
    }
    else if (!strcmp(n, "tab") || !strcmp(n, "ptab")) { Para *p = cur(pc); if (p) para_append(p, "\t", 1, &f); }
    else if (!strcmp(n, "noBreakHyphen")) run_text(pc, "\xE2\x80\x91", 3, &f);
    else if (!strcmp(n, "br") || !strcmp(n, "cr")) {
      const char *t = xml_attr(x, "type");
      if (t && (!strcmp(t, "page") || !strcmp(t, "column"))) {
        /* a page (column) break: what follows starts a new paragraph on a new page (column) */
        int col = t[0] == 'c';
        Para *p = pc->p;
        if (p && p->len) {
          p->pf.sect = 0;                    /* the section ends with the last part */
          emit(pc, p);
          if (col) pc->pf.colbreak = 1; else pc->pf.pagebreak = 1;
          pc->pf.dropcap = 0;
          pc->p = para_new(&pc->pf, &f);
          pc->pf.pagebreak = pc->pf.colbreak = 0;
        } else if (cur(pc)) { if (col) pc->p->pf.colbreak = 1; else pc->p->pf.pagebreak = 1; }
      } else if (!t || !strcmp(t, "textWrapping")) { Para *p = cur(pc); if (p) para_append(p, "\v", 1, &f); }
      else run_text(pc, " ", 1, &f);
    }
    else if (!strcmp(n, "drawing")) { parse_drawing(x, pc, &f); continue; }
    else if (!strcmp(n, "Fallback")) { xml_skip(x); continue; }        /* mc:AlternateContent: the drawing (Choice), not its VML copy */
    else if (!strcmp(n, "AlternateContent") || !strcmp(n, "Choice")) continue;
    else if (!strcmp(n, "pict") && c->story && parse_watermark(x, c)) continue;
    else if (!strcmp(n, "pict") || !strcmp(n, "object")) c->lost |= LOST_PICTURES;
    else if (!strcmp(n, "footnoteReference")) {
      i32 id;
      int k, done = 0;
      if (attr_num(x, "id", &id))
        for (k = 0; k < c->nfn; k++) if (c->fnid[k] == id) { char t[8]; CharFmt nf = f; nf.flags &= (u8)~CF_SUPER; note_utf8(t, c->fnidx[k]); run_text(pc, t, 4, &nf); done = 1; break; }   /* Quill raises the number itself */
      if (!done) c->lost |= LOST_NOTES;
    }
    else if (!strcmp(n, "endnoteReference")) {
      i32 id;
      int k, done = 0;
      if (attr_num(x, "id", &id))
        for (k = 0; k < c->nen; k++) if (c->enid[k] == id) { char t[8]; CharFmt nf = f; nf.flags &= (u8)~CF_SUPER; enote_utf8(t, c->enidx[k]); run_text(pc, t, 4, &nf); done = 1; break; }
      if (!done) c->lost |= LOST_NOTES;
    }
    else if (!strcmp(n, "commentReference")) {
      /* a comment with no range: a point at its reference */
      i32 id;
      int k = attr_num(x, "id", &id) ? comment_by_id(c, id) : -1;
      if (k >= 0 && !c->cmopen[k]) { char t[8]; cmark_utf8(t, c->cmidx[k], 0); run_text(pc, t, 4, &f); cmark_utf8(t, c->cmidx[k], 1); run_text(pc, t, 4, &f); c->cmopen[k] = 2; }
      else if (k < 0) c->lost |= LOST_COMMENTS;
    }
    else if (!strcmp(n, "sym")) {
      /* a symbol-font character (hex, often in the private use area): its Unicode look-alike */
      const char *ch = xml_attr(x, "char");
      if (ch) {
        char t[8];
        u32 cp = 0;
        int k;
        for (k = 0; ch[k] && k < 4; k++) {
          int v = ch[k];
          v = (v >= '0' && v <= '9') ? v - '0' : (v >= 'a' && v <= 'f') ? v - 'a' + 10 : (v >= 'A' && v <= 'F') ? v - 'A' + 10 : 0;
          cp = (cp << 4) | (u32)v;
        }
        if (cp < 0x100) cp |= 0xF000;
        t[utf8_put(t, cp)] = 0;
        map_bullet(t, sizeof t);
        run_text(pc, t, strlen(t), &f);
      }
    }
    xml_skip(x);
  }
}
static void parse_table(Xml *x, PCtx *pc);
/* the children of a paragraph, or of something inside one that keeps its contents */
static void parse_children(Xml *x, PCtx *pc)
{
  Ctx *c = pc->c;
  int d0 = x->depth;
  while (xml_next(x) != XT_EOF) {
    const char *n = x->name;
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(n, "pPr")) {
      memset(&pc->ppr, 0, sizeof pc->ppr); memset(&pc->mark, 0, sizeof pc->mark);
      pc->pstyle[0] = 0;
      parse_ppr(x, c, &pc->ppr, pc->pstyle, &pc->mark);
      para_begin(pc);
      if (pc->p) { pc->p->pf = pc->pf; pc->p->r[0].f = pc->markcf; }
      continue;
    }
    if (!strcmp(n, "r")) { parse_run(x, pc); continue; }
    if (!strcmp(n, "fldSimple")) {
      const char *in = xml_attr(x, "instr");
      int k = in ? field_kind(in) : 0;
      if (k) { Para *p = cur(pc); if (p) para_append(p, k == 1 ? FIELD_PAGE : FIELD_PAGES, 3, &p->r[p->nr - 1].f); xml_skip(x); continue; }
      if (in) {
        /* a simple cross-reference: PAGEREF's placeholder, or REF's text linked to its bookmark */
        char bm[200], url[210];
        int rk = ref_field(in, bm, sizeof bm);
        if (rk) {
          u16 ol = pc->link;
          snprintf(url, sizeof url, "#%s", bm);
          pc->link = (u16)doc_link(pc->c->d, url);
          if (rk == 2) {
            Para *p = cur(pc);
            if (p) { CharFmt f = p->r[p->nr - 1].f; f.link = pc->link; f.bmark = pc->c->bmark; para_append(p, FIELD_PAGEREF, 3, &f); }
            xml_skip(x);
          } else parse_children(x, pc);
          pc->link = ol;
          continue;
        }
      }
    }
    if (!strcmp(n, "ins") || !strcmp(n, "del") || !strcmp(n, "moveTo") || !strcmp(n, "moveFrom")) {
      /* tracked changes: the runs inside are inserted or deleted, by someone, at some time */
      u8 orev = pc->rev;
      u16 orid = pc->rid;
      char author[64] = "", date[24] = "";
      copy_attr(x, "author", author, sizeof author); copy_attr(x, "date", date, sizeof date);
      pc->rev = (n[0] == 'i' || !strcmp(n, "moveTo")) ? REV_INS : REV_DEL;
      pc->rid = (u16)doc_add_rev(c->d, author, date);
      parse_children(x, pc);
      pc->rev = orev; pc->rid = orid;
      continue;
    }
    if (!strcmp(n, "hyperlink") || (!strcmp(n, "fldSimple") && !pc->link)) {
      /* a link: to a web or mail address (its relationship's target) or to a bookmark (w:anchor);
         fldSimple HYPERLINK "..." is the old way of writing the same */
      u16 ol = pc->link;
      char url[512] = "", id[64] = "", anchor[256] = "";
      if (n[0] == 'h') {
        copy_attr(x, "id", id, sizeof id); copy_attr(x, "anchor", anchor, sizeof anchor);
        if (id[0] && c->rels) rel_raw(c->rels, c->rn, id, url, sizeof url);
        if (!url[0] && anchor[0]) snprintf(url, sizeof url, "#%s", anchor);
        else if (url[0] && anchor[0] && strlen(url) + strlen(anchor) + 2 < sizeof url) { strcat(url, "#"); strcat(url, anchor); }
      } else {
        const char *in = xml_attr(x, "instr");
        if (in) hyperlink_field(in, url, sizeof url);
      }
      if (url[0]) pc->link = (u16)doc_link(c->d, url);
      parse_children(x, pc);
      pc->link = ol;
      continue;
    }
    if (!strcmp(n, "bookmarkStart")) {
      /* a bookmark: the runs until its end are in it (one at a time; Word's own _GoBack is left out) */
      const char *nm = xml_attr(x, "name"), *id = xml_attr(x, "id");
      if (nm && nm[0] && strcmp(nm, "_GoBack")) { c->bmark = (u16)doc_bmark(c->d, nm); snprintf(c->bmid, sizeof c->bmid, "%s", id ? id : ""); }
      continue;
    }
    if (!strcmp(n, "bookmarkEnd")) {
      const char *id = xml_attr(x, "id");
      if (c->bmark && (!id || !strcmp(id, c->bmid))) c->bmark = 0;
      continue;
    }
    if (!strcmp(n, "smartTag") || !strcmp(n, "sdt") || !strcmp(n, "sdtContent") ||
        !strcmp(n, "fldSimple") || !strcmp(n, "customXml") || !strcmp(n, "bdo") || !strcmp(n, "dir")) {
      parse_children(x, pc);
      continue;
    }
    if (!strcmp(n, "commentRangeStart") || !strcmp(n, "commentRangeEnd")) {
      i32 id;
      int k = attr_num(x, "id", &id) ? comment_by_id(c, id) : -1, end = n[12] == 'E';
      if (k >= 0 && c->cmopen[k] == (end ? 1 : 0)) {
        char t[8];
        Para *p = cur(pc);
        cmark_utf8(t, c->cmidx[k], end);
        if (p) para_append(p, t, 4, &p->r[p->nr - 1].f);
        c->cmopen[k] = (u8)(end ? 2 : 1);
      } else if (k < 0) c->lost |= LOST_COMMENTS;
    }
    if (docx_read_elem_ext(x, pc)) continue;          /* a work package's element */
    xml_skip(x);
  }
}
/* a w:p (x just past its START; read to its END): the finished paragraph, owned by the caller, or
   NULL if out of memory. Formatting starts as the default style's until a pPr says otherwise. A
   page break inside may already have emitted the paragraph's first part to pc->out. */
static Para *parse_para(Xml *x, PCtx *pc)
{
  Para *p;
  memset(&pc->ppr, 0, sizeof pc->ppr); memset(&pc->mark, 0, sizeof pc->mark);
  pc->pstyle[0] = 0;
  pc->p = NULL;
  para_begin(pc);
  parse_children(x, pc);
  p = cur(pc);
  pc->p = NULL;
  return p;
}
/* the body of a cell in a table nested in another one: its paragraphs go into the outer cell */
static void parse_nested(Xml *x, PCtx *pc)
{
  int d0 = x->depth;
  if (!pc->c->story) pc->c->lost |= LOST_TABLES;      /* tables in headers are flattened quietly */
  if (++pc->c->depth > 8) { xml_skip(x); pc->c->depth--; return; }
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "p")) { Para *q = parse_para(x, pc); if (q) emit(pc, q); }
    else if (!strcmp(x->name, "tbl")) parse_nested(x, pc);
    else if (strcmp(x->name, "tr") && strcmp(x->name, "tc") && strcmp(x->name, "sdt") && strcmp(x->name, "sdtContent")) xml_skip(x);
  }
  pc->c->depth--;
}
/* how many rows the table being read has (x just past its w:tbl), without moving x */
static int count_rows(Xml *x)
{
  Xml y;
  int n = 0;
  xml_init(&y, x->s + x->i, x->n - x->i);
  while (xml_next(&y) != XT_EOF) {
    if (y.type == XT_END && y.depth < 0) break;
    if (y.type == XT_START && y.depth == 1 && !strcmp(y.name, "tr")) n++;
  }
  xml_free(&y);
  return n;
}
static void parse_table_body(Xml *x, PCtx *pc);
/* a table: in a cell, a table nested in it (deeper ones are flattened into that one's cells) */
static void parse_table(Xml *x, PCtx *pc)
{
  Cell cell, outer;
  int tstyle, tsnum, tnrows, tncols;
  u16 tlook;
  if (pc->c->story || (pc->cell.table && pc->outer.table)) { parse_nested(x, pc); return; }
  if (!pc->cell.table) { parse_table_body(x, pc); return; }
  cell = pc->cell; outer = pc->outer;
  tstyle = pc->tstyle; tsnum = pc->tsnum; tlook = pc->tlook; tnrows = pc->tnrows; tncols = pc->tncols;
  pc->outer = cell;
  memset(&pc->cell, 0, sizeof pc->cell);
  parse_table_body(x, pc);
  pc->cell = cell; pc->outer = outer;
  pc->tstyle = tstyle; pc->tsnum = tsnum; pc->tlook = tlook; pc->tnrows = tnrows; pc->tncols = tncols;
}
/* a w:tbl's properties, grid and rows (x just past its START; read to its END). The Table is added
   to the Doc at the first row; each cell's paragraphs are emitted with pf.cell set, and once the
   cell is finished its lines and fill (table style, table, then the cell's own tcPr) are copied
   onto every paragraph in it. An empty cell still gets one empty paragraph. Grid widths are twips. */
static void parse_table_body(Xml *x, PCtx *pc)
{
  Ctx *c = pc->c;
  Doc *d = c->d;
  int d0 = x->depth, id = 0, k, maxcol = 0, nrows, gotlook = 0;
  Table t;
  TBorder tb[TE_N];
  char tstyle[40] = "";
  u32 first = pc->nout;
  u16 row = 0;
  int haverows = 0;
  memset(&t, 0, sizeof t);
  memset(tb, 0, sizeof tb);
  t.marl = t.marr = 108;
  t.autofit = 1;                                   /* Word's default without a tblLayout */
  pc->tstyle = -1;
  nrows = count_rows(x);
  /* no style named: the default table style (Word's Table Normal: no lines) */
  for (k = 0; k < c->nst; k++) if (c->st[k].ts && c->st[k].isdefault) { strcpy(tstyle, c->st[k].id); break; }
  while (xml_next(x) != XT_EOF) {
    const char *n = x->name;
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(n, "tblPr")) {
      unsigned set = 0;
      i32 ml = t.marl, mr = t.marr;
      parse_tblpr(x, &set, tb, &ml, &mr, &t, tstyle, &gotlook);
      if (set & 2) { t.marl = ml; t.marr = mr; }
      else {                                       /* the style's margins, if it (or one it's based on) has any */
        int ts = tstyle[0] ? doc_tstyle(d, tstyle) : 0, guard = 0;
        while (ts && guard++ < 8) {
          if (d->tstyles[ts - 1].mar_set) { t.marl = d->tstyles[ts - 1].marl; t.marr = d->tstyles[ts - 1].marr; break; }
          ts = d->tstyles[ts - 1].based[0] ? doc_tstyle(d, d->tstyles[ts - 1].based) : 0;
        }
      }
      continue;
    }
    if (!strcmp(n, "tblGrid")) {
      int d1 = x->depth;
      while (xml_next(x) != XT_EOF) {
        i32 w;
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type == XT_START && !strcmp(x->name, "gridCol") && t.ncols < MAX_COLS) {
          if (!attr_num(x, "w", &w) || w < 0) w = 0;
          t.grid[t.ncols++] = w;
        }
      }
      continue;
    }
    if (!strcmp(n, "tr")) {
      int d1 = x->depth, col = 0;
      i16 rowh = 0;
      u8 header = 0, cantsplit = 0;
      if (!id) {
        if (tstyle[0]) { t.tstyle = (u8)doc_tstyle(d, tstyle); pc->tstyle = raw_find(c, tstyle, 2); }
        if (!gotlook) t.look = LK_DEFAULT;
        if (!(id = doc_add_table(d, &t))) { xml_skip(x); continue; }
        pc->tsnum = t.tstyle; pc->tlook = t.look; pc->tnrows = nrows; pc->tncols = t.ncols;
      }
      haverows = 1;
      while (xml_next(x) != XT_EOF) {
        const char *m = x->name;
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type != XT_START) continue;
        if (!strcmp(m, "trPr")) {
          int d2 = x->depth;
          while (xml_next(x) != XT_EOF) {
            i32 v;
            if (x->type == XT_END && x->depth < d2) break;
            if (x->type != XT_START) continue;
            if (!strcmp(x->name, "trHeight") && attr_num(x, "val", &v) && v > 0 && v < 32000) {
              const char *rule = xml_attr(x, "hRule");
              rowh = (i16)(rule && !strcmp(rule, "exact") ? -v : v);
            }
            else if (!strcmp(x->name, "tblHeader")) header = (u8)onoff(x);
            else if (!strcmp(x->name, "cantSplit")) cantsplit = (u8)onoff(x);
            else if (!strcmp(x->name, "gridBefore") && attr_num(x, "val", &v) && v > 0 && v < MAX_COLS) col += v;
          }
          continue;
        }
        if (!strcmp(m, "tc")) {
          int d2 = x->depth, any = 0, shd = 0;
          Cell cl;
          TBorder cb[TE_N];
          memset(&cl, 0, sizeof cl);
          memset(cb, 0, sizeof cb);
          cl.table = (u16)id; cl.row = row; cl.col = (u8)(col < MAX_COLS ? col : MAX_COLS - 1); cl.span = 1;
          cl.rowh = rowh; cl.header = header; cl.cantsplit = cantsplit; cl.shade = COLOR_AUTO;
          while (xml_next(x) != XT_EOF) {
            const char *e = x->name;
            if (x->type == XT_END && x->depth < d2) break;
            if (x->type != XT_START) continue;
            if (!strcmp(e, "tcPr")) {
              int d3 = x->depth;
              while (xml_next(x) != XT_EOF) {
                i32 v;
                const char *val;
                if (x->type == XT_END && x->depth < d3) break;
                if (x->type != XT_START) continue;
                val = xml_attr(x, "val");
                if (!strcmp(x->name, "gridSpan") && num(val, &v) && v > 1 && v < MAX_COLS) cl.span = (u8)v;
                else if (!strcmp(x->name, "vMerge")) cl.vmerge = (u8)(val && !strcmp(val, "restart") ? VM_START : VM_CONT);
                else if (!strcmp(x->name, "vAlign") && val) cl.valign = (u8)(!strcmp(val, "center") ? 1 : !strcmp(val, "bottom") ? 2 : 0);
                else if (!strcmp(x->name, "shd")) { cl.shade = hexcolor(xml_attr(x, "fill")); shd = 1; }
                else if (!strcmp(x->name, "tcBorders")) { parse_borders(x, cb); continue; }
                xml_skip(x);
              }
              continue;
            }
            if (!strcmp(e, "p")) {
              Para *q;
              pc->cell = cl;
              if ((q = parse_para(x, pc))) { emit(pc, q); any = 1; }
              continue;
            }
            if (!strcmp(e, "tbl")) { pc->cell = cl; parse_table(x, pc); any = 1; continue; }
            if (!strcmp(e, "sdt") || !strcmp(e, "sdtContent") || !strcmp(e, "customXml")) continue;
            xml_skip(x);
          }
          /* the cell's lines and fill: the style's and the table's, then the cell's own */
          {
            CellLook lk;
            u32 q;
            int ncols = t.ncols ? t.ncols : col + cl.span;
            tstyle_cell(d, t.tstyle, t.look, row, cl.col, cl.span, nrows, ncols, tb, &lk);
            for (k = 0; k < 4; k++) {
              if (cb[k].set) {
                if (cb[k].style == PB_NONE) lk.borders &= (u8)~(1 << k);
                else { lk.borders |= (u8)(1 << k); lk.bstyle[k] = cb[k].style; lk.bwidth[k] = cb[k].width; lk.bcolor[k] = cb[k].color; }
              }
              cl.bstyle[k] = lk.bstyle[k]; cl.bwidth[k] = lk.bwidth[k]; cl.bcolor[k] = lk.bcolor[k];
            }
            cl.borders = lk.borders;
            if (!shd) cl.shade = lk.shade;
            if (!any) { Para *e; pc->cell = cl; para_begin(pc); if ((e = para_new(&pc->pf, &pc->markcf))) emit(pc, e); }
            for (q = first; q < pc->nout; q++) {
              Cell *qc = &pc->out[q]->pf.cell, *qo = &pc->out[q]->pf.outer;
              if (qc->table == cl.table && qc->row == cl.row && qc->col == cl.col) *qc = cl;
              if (qo->table == cl.table && qo->row == cl.row && qo->col == cl.col) *qo = cl;   /* a table nested in it */
            }
          }
          col += cl.span;
          if (col > maxcol) maxcol = col;
          continue;
        }
        xml_skip(x);
      }
      row++;
      continue;
    }
    xml_skip(x);
  }
  memset(&pc->cell, 0, sizeof pc->cell);
  pc->tstyle = -1; pc->tsnum = 0;
  if (!haverows || !id) return;
  /* before Word 2013, a table's indent was to its first cell's text, not its edge */
  if (c->compat < 15) d->tables[id - 1].indent -= d->tables[id - 1].marl;
  /* a grid that is missing or too narrow: equal columns */
  {
    Table *tt = &d->tables[id - 1];
    i32 body = d->page.width - d->page.left - d->page.right, sum = 0;
    if (maxcol > MAX_COLS) maxcol = MAX_COLS;
    for (k = 0; k < tt->ncols; k++) sum += tt->grid[k];
    if (tt->ncols < maxcol || sum <= 0) {
      for (k = 0; k < maxcol; k++) tt->grid[k] = body / (maxcol ? maxcol : 1);
      tt->ncols = (u8)maxcol;
    }
  }
}
/* a w:sectPr: its section's format; the body's (body) also gives the page setup, headers and footers */
static void parse_sect(Xml *x, Ctx *c, SectFmt *sf, int body)
{
  int d0 = x->depth;
  i32 k;
  PageSetup *pg = body ? &c->d->page : &sf->page;
  sect_default(sf);
  if (!body) { sf->page = c->d->page; memset(c->pend, 0, sizeof c->pend); }
  while (xml_next(x) != XT_EOF) {
    const char *v;
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    v = xml_attr(x, "val");
    if (!strcmp(x->name, "type") && v)
      sf->start = (u8)(!strcmp(v, "continuous") ? SB_CONT : !strcmp(v, "evenPage") ? SB_EVEN : !strcmp(v, "oddPage") ? SB_ODD : SB_NEXT);
    else if (!strcmp(x->name, "cols")) {
      const char *sep = xml_attr(x, "sep");
      if (attr_num(x, "num", &k) && k >= 1) sf->cols = (u8)(k > MAX_SECT_COLS ? MAX_SECT_COLS : k);
      if (attr_num(x, "space", &k) && k >= 0 && k < 7200) sf->colgap = k;
      sf->colline = (u8)(sep && (!strcmp(sep, "1") || !strcmp(sep, "true") || !strcmp(sep, "on")));
    }
    else if (!strcmp(x->name, "vAlign") && v)
      sf->valign = (u8)(!strcmp(v, "center") ? VA_CENTER : !strcmp(v, "bottom") ? VA_BOTTOM : VA_TOP);
    else if (!strcmp(x->name, "lnNumType")) {
      const char *r = xml_attr(x, "restart");
      i32 by = 1;
      attr_num(x, "countBy", &by);
      if (by >= 1) {
        sf->linenum = (u8)(r && !strcmp(r, "continuous") ? LN_CONT : r && !strcmp(r, "newSection") ? LN_SECTION : LN_PAGE);
        sf->lnby = (u8)(by > 100 ? 100 : by);
      }
      if (attr_num(x, "distance", &k) && k > 0 && k < 2880) sf->lndist = k;
      if (attr_num(x, "start", &k) && k > 0 && k < 32767) sf->lnstart = (u16)(k + 1);      /* (Word counts it from 0) */
    }
    else if (!strcmp(x->name, "pgNumType")) {
      /* page numbers' format and first number (kept for the whole document) */
      const char *f = xml_attr(x, "fmt");
      if (f) c->d->pgfmt = (u8)(!strcmp(f, "lowerRoman") ? NF_LOWER_ROMAN : !strcmp(f, "upperRoman") ? NF_UPPER_ROMAN : !strcmp(f, "lowerLetter") ? NF_LOWER_LETTER :
                                 !strcmp(f, "upperLetter") ? NF_UPPER_LETTER : 0);
      if (attr_num(x, "start", &k) && k >= 0 && k < 32767) c->d->pgstart = (u16)k;
    }
    else if (!strcmp(x->name, "pgBorders")) {
      const char *of = xml_attr(x, "offsetFrom");
      int d1 = x->depth, got = 0;
      PageBorder pb;
      memset(&pb, 0, sizeof pb);
      pb.frompage = (u8)(of && !strcmp(of, "page"));
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type == XT_START && !got && (v = xml_attr(x, "val")) && strcmp(v, "none") && strcmp(v, "nil")) {
          pb.style = (u8)(!strcmp(v, "double") ? PB_DOUBLE : !strcmp(v, "thick") ? PB_THICK : !strcmp(v, "dotted") ? PB_DOTTED :
                          (!strcmp(v, "dashed") || !strcmp(v, "dashSmallGap")) ? PB_DASHED : PB_SINGLE);
          pb.width = (u8)(attr_num(x, "sz", &k) && k > 0 && k <= 96 ? k : 4);
          pb.space = attr_num(x, "space", &k) && k >= 0 && k <= 31 ? k * 20 : (pb.frompage ? 480 : 80);
          pb.color = hexcolor(xml_attr(x, "color"));
          got = 1;
        }
      }
      if (got && !c->border_set) { c->d->border = pb; c->border_set = 1; }
      continue;
    }
    else if (!strcmp(x->name, "pgSz")) {
      if (attr_num(x, "w", &k) && k >= 1440 && k <= 31680) pg->width = k;
      if (attr_num(x, "h", &k) && k >= 1440 && k <= 31680) pg->height = k;
      if (!body) sf->haspage = 1;
    } else if (!strcmp(x->name, "pgMar")) {
      if (attr_num(x, "top", &k)) pg->top = k < 0 ? -k : k;
      if (attr_num(x, "bottom", &k)) pg->bottom = k < 0 ? -k : k;
      if (attr_num(x, "left", &k) || attr_num(x, "start", &k)) pg->left = k;
      if (attr_num(x, "right", &k) || attr_num(x, "end", &k)) pg->right = k;
      if (attr_num(x, "header", &k) && k >= 0) pg->header = k;
      if (attr_num(x, "footer", &k) && k >= 0) pg->footer = k;
      if (!body) sf->haspage = 1;
    } else if (!strcmp(x->name, "headerReference") || !strcmp(x->name, "footerReference")) {
      const char *t = xml_attr(x, "type"), *id = xml_attr(x, "id");
      int w = t && !strcmp(t, "first") ? HF_FIRST : t && !strcmp(t, "even") ? HF_EVEN : HF_DEFAULT;
      if (id) {
        char *dst = body ? (x->name[0] == 'h' ? c->hdrid[w] : c->ftrid[w]) : c->pend[(x->name[0] == 'h' ? 0 : HF_N) + w];
        strncpy(dst, id, 31); dst[31] = 0;
      }
    } else if (!strcmp(x->name, "titlePg")) { if (body) c->d->titlepg = (u8)onoff(x); else sf->titlepg = (u8)onoff(x); }
    else if (!strcmp(x->name, "footnotePr") || !strcmp(x->name, "endnotePr")) { notes_read_pr(x, x->name[0] == 'e' ? &sf->enpr : &sf->fnpr); continue; }   /* (WP22b) */
    xml_skip(x);
  }
  /* margins that leave no room are not believed */
  if (pg->left + pg->right > pg->width - 1440) pg->left = pg->right = 1440;
  if (pg->top + pg->bottom > pg->height - 1440) pg->top = pg->bottom = 1440;
}
/* a header or footer part: its paragraphs (tables flattened) */
static void parse_story(Ctx *c, const char *xml, size_t n, Story *st)
{
  Xml x;
  PCtx pc;
  memset(&pc, 0, sizeof pc);
  pc.c = c; pc.tstyle = -1;
  c->story = 1;
  xml_init(&x, xml, n);
  while (xml_next(&x) != XT_EOF) {
    if (x.type != XT_START) continue;
    if (!strcmp(x.name, "hdr") || !strcmp(x.name, "ftr") || !strcmp(x.name, "sdt") || !strcmp(x.name, "sdtContent") || !strcmp(x.name, "customXml")) continue;
    if (!strcmp(x.name, "p")) { Para *p = parse_para(&x, &pc); if (p) emit(&pc, p); }
    else if (!strcmp(x.name, "tbl")) parse_nested(&x, &pc);
    else xml_skip(&x);
  }
  xml_free(&x);
  c->story = 0;
  story_free(st);
  /* a story of one empty paragraph is no story */
  if (pc.nout == 1 && !pc.out[0]->len) { para_free(pc.out[0]); pc.nout = 0; }
  if (!pc.nout) { free(pc.out); return; }
  st->p = pc.out; st->np = pc.nout;
}
static char *part(ZipReader *z, const char *name, size_t *n);
/* the header or footer part a relationship (id) names, into st */
static void read_hf(Ctx *c, ZipReader *z, const char *rels, size_t rn, const char *base, const char *id, Story *st)
{
  char path[300], rp[340], *xml, *srels;
  size_t n, srn = 0;
  const char *fn;
  if (!id[0]) return;
  rel_by_id(rels, rn, id, base, path, sizeof path);
  if (!path[0] || !(xml = part(z, path, &n))) return;
  /* a header's pictures are in its own relationships */
  fn = strrchr(path, '/');
  snprintf(rp, sizeof rp, "%.*s_rels/%s.rels", fn ? (int)(fn - path + 1) : 0, path, fn ? fn + 1 : path);
  srels = part(z, rp, &srn);
  c->rels = srels; c->rn = srn;
  parse_story(c, xml, n, st);
  free(srels);
  c->rels = NULL;
  free(xml);
}
/* comments.xml: each comment's author, date and paragraphs, before the text marks their ranges */
static void parse_comments(Ctx *c, const char *xml, size_t n)
{
  Xml x;
  int cap = 0;
  xml_init(&x, xml, n);
  while (xml_next(&x) != XT_EOF) {
    i32 id;
    int k, d0;
    char author[64] = "", initials[12] = "", date[24] = "";
    PCtx pc;
    if (x.type != XT_START || strcmp(x.name, "comment")) continue;
    copy_attr(&x, "author", author, sizeof author); copy_attr(&x, "initials", initials, sizeof initials); copy_attr(&x, "date", date, sizeof date);
    if (!attr_num(&x, "id", &id) || (k = doc_add_comment(c->d, author, initials, date)) < 0) { xml_skip(&x); continue; }
    if (c->ncm >= cap) {
      int nc = cap ? cap * 2 : 32;
      i32 *a = (i32 *)realloc(c->cmid, nc * sizeof(i32));
      int *b;
      u8 *o;
      if (!a) { xml_skip(&x); continue; }
      c->cmid = a;
      if (!(b = (int *)realloc(c->cmidx, nc * sizeof(int)))) { xml_skip(&x); continue; }
      c->cmidx = b;
      if (!(o = (u8 *)realloc(c->cmopen, (size_t)nc))) { xml_skip(&x); continue; }
      c->cmopen = o; cap = nc;
    }
    c->cmid[c->ncm] = id; c->cmidx[c->ncm] = k; c->cmopen[c->ncm] = 0; c->ncm++;
    memset(&pc, 0, sizeof pc);
    pc.c = c; pc.tstyle = -1;
    d0 = x.depth;
    while (xml_next(&x) != XT_EOF) {
      if (x.type == XT_END && x.depth < d0) break;
      if (x.type != XT_START) continue;
      if (!strcmp(x.name, "p")) { Para *p = parse_para(&x, &pc); if (p) emit(&pc, p); }
      else xml_skip(&x);
    }
    c->d->comments[k].text.p = pc.out; c->d->comments[k].text.np = pc.nout;
  }
  xml_free(&x);
}
/* the index in c->cmid (and cmidx, cmopen) of the comment with this w:id; -1 if comments.xml had
   no such comment */
static int comment_by_id(Ctx *c, i32 id)
{
  int k;
  for (k = 0; k < c->ncm; k++) if (c->cmid[k] == id) return k;
  return -1;
}
/* (WP22b) a separator note (type: separator, continuationSeparator or continuationNotice) of footnotes.xml
   (end: endnotes.xml), the START just read: which line it holds and its paragraphs, kept for drawing
   and writing back (core/notes.h) */
static void read_note_sep(Ctx *c, Xml *x, const char *type, int end)
{
  int which = !strcmp(type, "separator") ? NSEP_SEP : !strcmp(type, "continuationSeparator") ? NSEP_CONT : !strcmp(type, "continuationNotice") ? NSEP_NOTICE : -1;
  int flags = 0;
  const char *raw;
  size_t n = xml_raw(x, &raw);
  Xml y;
  PCtx pc;
  Story st;
  if (which < 0 || !n) return;
  xml_init(&y, raw, n);
  while (xml_next(&y) != XT_EOF)
    if (y.type == XT_START) { if (!strcmp(y.name, "separator")) flags |= NSF_LINE; else if (!strcmp(y.name, "continuationSeparator")) flags |= NSF_LONG; }
  xml_free(&y);
  memset(&pc, 0, sizeof pc);
  pc.c = c; pc.tstyle = -1;
  xml_init(&y, raw, n);
  while (xml_next(&y) != XT_EOF) {
    if (y.type != XT_START) continue;
    if (!strcmp(y.name, "p")) { Para *p = parse_para(&y, &pc); if (p) emit(&pc, p); }
    else if (strcmp(y.name, "footnote") && strcmp(y.name, "endnote")) xml_skip(&y);
  }
  xml_free(&y);
  st.p = pc.out; st.np = pc.nout;
  notes_sep_set(c->d, end, which, flags, &st);
}
/* footnotes.xml: each note's paragraphs (the separators are left out), before the text refers to them.
   With end set, endnotes.xml the same way. Each note's w:id is recorded so the references in the
   text can find its index in the Doc. */
static void parse_footnotes(Ctx *c, const char *xml, size_t n, int end)
{
  Xml x;
  int cap = 0;
  i32 **ids = end ? &c->enid : &c->fnid;
  int **idx = end ? &c->enidx : &c->fnidx, *cnt = end ? &c->nen : &c->nfn;
  xml_init(&x, xml, n);
  while (xml_next(&x) != XT_EOF) {
    const char *t;
    i32 id;
    int k, d0;
    PCtx pc;
    if (x.type != XT_START || strcmp(x.name, end ? "endnote" : "footnote")) continue;
    t = xml_attr(&x, "type");
    if (t && strcmp(t, "normal")) { read_note_sep(c, &x, t, end); continue; }       /* (WP22b: the document's separators) */
    if (!attr_num(&x, "id", &id) || (k = end ? doc_add_endnote(c->d) : doc_add_note(c->d)) < 0) { xml_skip(&x); continue; }
    if (*cnt >= cap) {
      int nc = cap ? cap * 2 : 32;
      i32 *a = (i32 *)realloc(*ids, nc * sizeof(i32));
      int *b;
      if (!a) { xml_skip(&x); continue; }
      *ids = a;
      if (!(b = (int *)realloc(*idx, nc * sizeof(int)))) { xml_skip(&x); continue; }
      *idx = b; cap = nc;
    }
    (*ids)[*cnt] = id; (*idx)[*cnt] = k; (*cnt)++;
    memset(&pc, 0, sizeof pc);
    pc.c = c; pc.tstyle = -1;
    d0 = x.depth;
    while (xml_next(&x) != XT_EOF) {
      if (x.type == XT_END && x.depth < d0) break;
      if (x.type != XT_START) continue;
      if (!strcmp(x.name, "p")) { Para *p = parse_para(&x, &pc); if (p) emit(&pc, p); }
      else if (!strcmp(x.name, "tbl")) parse_nested(&x, &pc);
      else xml_skip(&x);
    }
    /* the number is drawn by Quill: the space Word puts after it goes */
    if (pc.nout && pc.out[0]->len && pc.out[0]->t[0] == ' ') {
      Para *p0 = pc.out[0], *q = para_new(&p0->pf, &p0->r[0].f);
      u32 r, start = 0;
      if (q) {
        for (r = 0; r < p0->nr; r++) {
          u32 a = start > 1 ? start : 1;
          if (p0->r[r].end > a) para_append(q, p0->t + a, p0->r[r].end - a, &p0->r[r].f);
          start = p0->r[r].end;
        }
        para_free(p0); pc.out[0] = q;
      }
    }
    if (end) { c->d->enotes[k].p = pc.out; c->d->enotes[k].np = pc.nout; }
    else { c->d->notes[k].p = pc.out; c->d->notes[k].np = pc.nout; }
  }
  xml_free(&x);
}
/* a relationship's target by its id, as a path in the package: relative targets get base (the folder
   of the part the relationships belong to) in front. out is empty if there is no such id. */
/* a relationship's Target as written (a hyperlink's address, not a path in the package) */
static void rel_raw(const char *rels, size_t n, const char *id, char *out, size_t cap)
{
  Xml x;
  out[0] = 0;
  xml_init(&x, rels, n);
  while (xml_next(&x) != XT_EOF) {
    const char *i, *tg;
    if (x.type != XT_START || strcmp(x.name, "Relationship")) continue;
    i = xml_attr(&x, "Id"); tg = xml_attr(&x, "Target");
    if (i && tg && !strcmp(i, id)) { snprintf(out, cap, "%s", tg); break; }
  }
  xml_free(&x);
}
static void rel_by_id(const char *rels, size_t n, const char *id, const char *base, char *out, size_t cap)
{
  Xml x;
  out[0] = 0;
  xml_init(&x, rels, n);
  while (xml_next(&x) != XT_EOF) {
    const char *i, *tg;
    if (x.type != XT_START || strcmp(x.name, "Relationship")) continue;
    i = xml_attr(&x, "Id"); tg = xml_attr(&x, "Target");
    if (i && tg && !strcmp(i, id)) { snprintf(out, cap, "%s%s", tg[0] == '/' ? "" : base, tg[0] == '/' ? tg + 1 : tg); break; }
  }
  xml_free(&x);
}
/* Word's drop cap: a framed paragraph holding the letter, before the paragraph it opens. Quill keeps
   one paragraph, its first letter marked as dropped (at the text's size: layout makes it big) */
static Para *join_drop(Para *cap, Para *p, u8 lines, u8 margin)
{
  ParaFmt pf = p->pf;
  CharFmt f = p->r[0].f;
  Para *q;
  u32 r, start = 0;
  pf.dropcap = lines; pf.dropmargin = margin;
  f.font = cap->r[0].f.font;
  if (!(q = para_new(&pf, &p->r[0].f))) { para_free(cap); return p; }
  para_append(q, cap->t, cap->len, &f);
  for (r = 0; r < p->nr; r++) { if (p->r[r].end > start) para_append(q, p->t + start, p->r[r].end - start, &p->r[r].f); start = p->r[r].end; }
  para_normalise(q);
  para_free(cap); para_free(p);
  return q;
}
/* document.xml: the body's paragraphs and tables replace the Doc's paragraphs, and the body's own
   w:sectPr (the last section) is kept in c->bodysect. A drop cap's frame paragraph is held back
   and joined to the paragraph after it. Returns 0 if the body had no paragraphs (the Doc keeps
   the empty one doc_new made). */
static int parse_document(Ctx *c, const char *xml, size_t n)
{
  Xml x;
  PCtx pc;
  int inbody = 0;
  u32 i;
  Para *held = NULL;                 /* a drop cap's frame, waiting for its paragraph */
  u8 hl = 0, hm = 0;
  memset(&pc, 0, sizeof pc);
  pc.c = c;
  xml_init(&x, xml, n);
  while (xml_next(&x) != XT_EOF) {
    if (x.type != XT_START) continue;
    if (!strcmp(x.name, "body")) { inbody = 1; continue; }
    if (!inbody && !strcmp(x.name, "background")) {
      /* the page colour */
      const char *col = xml_attr(&x, "color");
      if (col && strlen(col) == 6 && strcmp(col, "auto")) c->d->pagecolor = HL_ON | (u32)strtoul(col, NULL, 16);
      continue;
    }
    if (!inbody) continue;
    if (!strcmp(x.name, "p")) {
      Para *p = parse_para(&x, &pc);
      if (!p) continue;
      if (held) { p = join_drop(held, p, hl, hm); held = NULL; }
      else if (pc.ppr.dropcap && p->len && p->len <= 8 && !p->pf.sect) { held = p; hl = pc.ppr.dropcap; hm = pc.ppr.dropmargin; continue; }
      emit(&pc, p);
    }
    else if (!strcmp(x.name, "tbl")) { if (held) { emit(&pc, held); held = NULL; } parse_table(&x, &pc); }
    else if (!strcmp(x.name, "sectPr")) { parse_sect(&x, c, &c->bodysect, 1); c->has_bodysect = 1; }
    else if (!strcmp(x.name, "sdt") || !strcmp(x.name, "sdtContent") || !strcmp(x.name, "customXml")) continue;   /* their paragraphs follow */
    else if (!docx_read_block_ext(&x, &pc)) xml_skip(&x);    /* (a work package's, or skipped) */
  }
  xml_free(&x);
  if (held) emit(&pc, held);
  if (!pc.nout) { free(pc.out); return 0; }
  /* the document's paragraphs replace the empty one doc_new made */
  for (i = 0; i < c->d->np; i++) para_free(c->d->p[i]);
  free(c->d->p);
  c->d->p = pc.out; c->d->np = pc.nout; c->d->pcap = pc.outcap;
  /* the last section's format goes on the last paragraph (if it isn't the plain one) */
  if (c->has_bodysect) {
    SectFmt def;
    Para *lp = c->d->p[c->d->np - 1];
    sect_default(&def);
    if (memcmp(&def, &c->bodysect, sizeof def) && !lp->pf.sect && !lp->pf.cell.table) lp->pf.sect = (u16)doc_add_sect(c->d, &c->bodysect);
  }
  return 1;
}
/* docProps/core.xml: the title and author (dc:creator), up to 127 bytes each */
static void parse_core(Doc *d, const char *xml, size_t n)
{
  Xml x;
  char *dst = NULL;
  xml_init(&x, xml, n);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START) dst = !strcmp(x.name, "title") ? d->title : !strcmp(x.name, "creator") ? d->author : NULL;
    else if (x.type == XT_TEXT && dst) { strncpy(dst, x.text.data, 127); dst[127] = 0; dst = NULL; }
    else if (x.type == XT_END) dst = NULL;
  }
  xml_free(&x);
}
/* a relationship's target, resolved against the folder of the part that has it, for the first
   relationship whose Type ends in typesuffix ("/styles" and so on: the namespace in front varies
   between transitional and strict files). out is left alone if there is none. */
static void rel_target(const char *rels, size_t n, const char *typesuffix, const char *base, char *out, size_t cap)
{
  Xml x;
  xml_init(&x, rels, n);
  while (xml_next(&x) != XT_EOF) {
    const char *t, *tg;
    size_t ls;
    if (x.type != XT_START || strcmp(x.name, "Relationship")) continue;
    t = xml_attr(&x, "Type"); tg = xml_attr(&x, "Target");
    if (!t || !tg) continue;
    ls = strlen(typesuffix);
    if (strlen(t) < ls || strcmp(t + strlen(t) - ls, typesuffix)) continue;
    if (tg[0] == '/') snprintf(out, cap, "%s", tg + 1);
    else {
      char b[256];
      snprintf(b, sizeof b, "%s", base);
      while (!strncmp(tg, "../", 3)) {                 /* up a folder */
        char *sl;
        tg += 3;
        b[strlen(b) ? strlen(b) - 1 : 0] = 0;
        sl = strrchr(b, '/');
        if (sl) sl[1] = 0; else b[0] = 0;
      }
      snprintf(out, cap, "%s%s", b, tg);
    }
    break;
  }
  xml_free(&x);
}
/* a part of the package by its path, malloc'd and NUL-terminated (*n its size); NULL if absent or
   damaged */
static char *part(ZipReader *z, const char *name, size_t *n)
{
  return (char *)zip_read(z, zip_find(z, name), n);
}
/* WP14a: an equation in a paragraph (m:oMath; m:oMathPara for a display one): an entry in Doc.maths
   and its reference in the text, in the paragraph's format at the size of its first sized run. 1 if
   x was one (read to its END) */
static int read_math(Xml *x, PCtx *pc)
{
  Ctx *c = pc->c;
  CharFmt f;
  MathEq *e;
  char ref[8];
  int k;
  u16 sz;
  if (strcmp(x->name, "oMath") && strcmp(x->name, "oMathPara")) return 0;
  if (c->field == 1 || !(e = math_read_omml(x))) return 1;
  f = pc->pcf;
  f.rev = pc->rev; f.rid = pc->rid; f.bmark = c->bmark;
  f.link = c->field == 2 && c->fieldlink ? c->fieldlink : pc->link;
  if ((sz = math_size(e))) f.size = sz;
  if ((k = doc_add_math(c->d, e)) < 0) return 1;
  math_utf8(ref, k);
  run_text(pc, ref, 4, &f);
  return 1;
}
/* ------------------------------------------------------------------ */
/* the work packages' reading hooks (WP0). Each package puts the call to its own function on a line
   right after its own marker line; never edit a marker line itself (so two packages' lines don't
   touch and their branches merge cleanly) */
/* a block-level element of the body that Quill doesn't read (w:p, w:tbl and w:sectPr are read): 1 if
   a package took it (and read to its END), 0 to have it skipped */
static int docx_read_block_ext(Xml *x, PCtx *pc)
{
  (void)x; (void)pc;
  /* WP1: */
  /* WP2: */
  /* WP3: */
  /* WP4: */
  /* WP5: */
  /* WP7: */
  /* WP9: */
  /* WP10: */
  /* WP11: */
  /* WP13: */
  /* WP14a: */
  /* WP15a: */
  /* WP15b: */
  /* WP15c: */
  /* WP16: */
  /* WP17: */
  /* WP18: */
  /* WP19: */
  /* WP20: */
  /* WP21a: */
  /* WP21b: */
  /* WP22a: */
  /* WP22b: */
  /* WP23: */
  /* WP24: */
  /* WP27: */
  /* WP31: */
  return 0;
}
/* an element inside a paragraph that Quill doesn't read (runs, links, bookmarks, tracked changes and
   comment marks are read): 1 if a package took it, 0 to have it skipped */
static int docx_read_elem_ext(Xml *x, PCtx *pc)
{
  (void)x; (void)pc;
  /* WP1: */
  /* WP2: */
  /* WP3: */
  /* WP4: */
  /* WP5: */
  /* WP7: */
  /* WP9: */
  /* WP10: */
  /* WP11: */
  /* WP13: */
  /* WP14a: */
  if (read_math(x, pc)) return 1;
  /* WP15a: */
  /* WP15b: */
  /* WP15c: */
  /* WP16: */
  /* WP17: */
  /* WP18: */
  /* WP19: */
  /* WP20: */
  /* WP21a: */
  /* WP21b: */
  /* WP22a: */
  /* WP22b: */
  /* WP23: */
  /* WP24: */
  /* WP27: */
  /* WP31: */
  return 0;
}
/* (WP22b) settings.xml's w:footnotePr and w:endnotePr: the document's note options */
static void read_notes_setting(Ctx *c, Xml *x)
{
  if (!strcmp(x->name, "footnotePr")) notes_read_pr(x, &c->d->fnpr);
  else if (!strcmp(x->name, "endnotePr")) notes_read_pr(x, &c->d->enpr);
}
/* each start tag of word/settings.xml, after Quill's own (compatibility mode, odd and even headers,
   automatic hyphenation) have been looked at */
static void docx_read_setting_ext(Ctx *c, Xml *x)
{
  (void)c; (void)x;
  /* WP1: */
  /* WP2: */
  /* WP3: */
  /* WP4: */
  /* WP5: */
  /* WP7: */
  /* WP9: */
  /* WP10: */
  /* WP11: */
  /* WP13: */
  /* WP14a: */
  if (!strcmp(x->name, "mathPr")) math_read_mathpr(c->d, x);
  /* WP15a: */
  /* WP15b: */
  /* WP15c: */
  /* WP16: */
  /* WP17: */
  /* WP18: */
  /* WP19: */
  /* WP20: */
  /* WP21a: */
  /* WP21b: */
  /* WP22a: */
  /* WP22b: */
  read_notes_setting(c, x);
  /* WP23: */
  /* WP24: */
  /* WP27: */
  /* WP31: */
}

/* the parts Quill doesn't read but keeps verbatim (core/keep.h): a relationship's type (the
   package's own if pkglevel, else the document's) ends in one of these. Digital signatures are left
   out (an edited document's would be wrong), and so is the thumbnail (it would show the old pages).
   A package that comes to read one of these parts takes it out of the kept ones (keep_remove) */
static int keep_type(const char *type, int pkglevel)
{
  static const char *const pkg[] = { "/extended-properties", "/custom-properties" };
  static const char *const doc[] = { "/customXml", "/glossaryDocument", "/people" };
  const char *const *l = pkglevel ? pkg : doc;
  size_t i, n = pkglevel ? sizeof pkg / sizeof pkg[0] : sizeof doc / sizeof doc[0], lt = strlen(type);
  for (i = 0; i < n; i++) if (lt >= strlen(l[i]) && !strcmp(type + lt - strlen(l[i]), l[i])) return 1;
  return 0;
}
/* whether Quill writes a part of this name itself (a kept part must not take its place) */
static int quill_part(const char *name)
{
  static const char *const own[] = { "[Content_Types].xml", "_rels/.rels", "word/document.xml", "word/_rels/document.xml.rels", "word/styles.xml",
    "word/settings.xml", "word/numbering.xml", "word/footnotes.xml", "word/endnotes.xml", "word/comments.xml", "docProps/core.xml" };
  size_t i;
  for (i = 0; i < sizeof own / sizeof own[0]; i++) if (!strcmp(name, own[i])) return 1;
  return !strncmp(name, "word/media/", 11) || !strncmp(name, "word/header", 11) || !strncmp(name, "word/footer", 11) ||
         !strncmp(name, "word/_rels/header", 17) || !strncmp(name, "word/_rels/footer", 17);
}
/* target (a relationship's Target) as a part name, from the folder base ("word/", "" ...) */
static void part_path(const char *base, const char *tg, char *out, size_t cap)
{
  char b[256];
  if (tg[0] == '/') { snprintf(out, cap, "%s", tg + 1); return; }
  snprintf(b, sizeof b, "%s", base);
  while (!strncmp(tg, "../", 3) || !strncmp(tg, "./", 2)) {
    char *sl;
    if (tg[1] == '/') { tg += 2; continue; }
    tg += 3;
    b[strlen(b) ? strlen(b) - 1 : 0] = 0;
    sl = strrchr(b, '/');
    if (sl) sl[1] = 0; else b[0] = 0;
  }
  snprintf(out, cap, "%s%s", b, tg);
}
/* a part's content type from [Content_Types].xml (ct): its Override, else its extension's Default;
   "" if neither */
static void content_type(const char *ct, size_t n, const char *name, char *out, size_t cap)
{
  Xml x;
  const char *ext = strrchr(name, '.');
  char def[200] = "";
  out[0] = 0;
  if (!ct) return;
  xml_init(&x, ct, n);
  while (xml_next(&x) != XT_EOF && !out[0]) {
    const char *pn, *ty;
    if (x.type != XT_START) continue;
    ty = xml_attr(&x, "ContentType");
    if (!ty) continue;
    if (!strcmp(x.name, "Override") && (pn = xml_attr(&x, "PartName")) && pn[0] == '/' && str_ieq(pn + 1, name)) snprintf(out, cap, "%s", ty);
    else if (!strcmp(x.name, "Default") && ext && (pn = xml_attr(&x, "Extension")) && str_ieq(pn, ext + 1)) snprintf(def, sizeof def, "%s", ty);
  }
  xml_free(&x);
  if (!out[0]) snprintf(out, cap, "%s", def);
}
/* keeps part name (and, through its own .rels, the parts it leads to) verbatim; owner, type and id
   as keep_add's. depth stops loops */
static void keep_tree(Doc *d, ZipReader *z, const char *ct, size_t ctn, const char *name, const char *owner,
                      const char *reltype, const char *relid, int depth)
{
  char ty[200], relsname[300], dir[256], tgt[300];
  char *data, *rels;
  size_t n, rn;
  const char *sl;
  if (depth > 4 || quill_part(name) || keep_find(d, name) >= 0 || !(data = part(z, name, &n))) return;
  content_type(ct, ctn, name, ty, sizeof ty);
  keep_add(d, name, ty[0] ? ty : NULL, owner, reltype, relid, data, n);
  free(data);
  /* its own relationships, kept as they are, and the parts inside the package they lead to */
  sl = strrchr(name, '/');
  snprintf(dir, sizeof dir, "%.*s", sl ? (int)(sl - name + 1) : 0, name);
  snprintf(relsname, sizeof relsname, "%s_rels/%s.rels", dir, sl ? sl + 1 : name);
  if (!(rels = part(z, relsname, &rn))) return;
  keep_add(d, relsname, NULL, NULL, NULL, NULL, rels, rn);
  {
    Xml x;
    xml_init(&x, rels, rn);
    while (xml_next(&x) != XT_EOF) {
      const char *t, *mode;
      if (x.type != XT_START || strcmp(x.name, "Relationship") || !(t = xml_attr(&x, "Target"))) continue;
      if ((mode = xml_attr(&x, "TargetMode")) && !strcmp(mode, "External")) continue;
      part_path(dir, t, tgt, sizeof tgt);
      keep_tree(d, z, ct, ctn, tgt, NULL, NULL, NULL, depth + 1);
    }
    xml_free(&x);
  }
  free(rels);
}
/* the relationships in rels whose types keep_type names: their parts kept (owner: the rels' owner) */
static void keep_rels(Doc *d, ZipReader *z, const char *ct, size_t ctn, const char *rels, size_t rn, const char *base, const char *owner)
{
  Xml x;
  char tgt[300], ty[300], id[64];
  if (!rels) return;
  xml_init(&x, rels, rn);
  while (xml_next(&x) != XT_EOF) {
    const char *t, *tg, *mode, *i;
    if (x.type != XT_START || strcmp(x.name, "Relationship")) continue;
    t = xml_attr(&x, "Type"); tg = xml_attr(&x, "Target"); i = xml_attr(&x, "Id"); mode = xml_attr(&x, "TargetMode");
    if (!t || !tg || !i || (mode && !strcmp(mode, "External")) || !keep_type(t, !owner[0])) continue;
    snprintf(ty, sizeof ty, "%s", t); snprintf(id, sizeof id, "%s", i);
    part_path(base, tg, tgt, sizeof tgt);
    keep_tree(d, z, ct, ctn, tgt, owner, ty, id, 0);
  }
  xml_free(&x);
}
/* (WP24) the document's properties: core.xml, app.xml and custom.xml (core/info.h), no longer kept whole */
static void read_info_parts(Doc *d, ZipReader *z, const char *pkgrels, size_t pn)
{
  static const char *const types[3] = { "/core-properties", "/extended-properties", "/custom-properties" };
  int k;
  if (!pkgrels) return;
  for (k = 0; k < 3; k++) {
    char path[300] = "", rels[340];
    char *xml;
    size_t n;
    const char *sl;
    rel_target(pkgrels, pn, types[k], "", path, sizeof path);
    if (!path[0] || !(xml = part(z, path, &n))) continue;
    if (k == 0) info_read_core(d, xml, n); else if (k == 1) info_read_app(d, xml, n); else info_read_custom(d, xml, n);
    free(xml);
    keep_remove(d, path);
    sl = strrchr(path, '/');
    snprintf(rels, sizeof rels, "%.*s_rels/%s.rels", sl ? (int)(sl - path + 1) : 0, path, sl ? sl + 1 : path);
    keep_remove(d, rels);
  }
}
/* after everything Quill reads has been read: the parts kept verbatim, then the packages' own parts.
   docrels: the main part's relationships (base its folder, "word/") */
static void docx_read_parts_ext(Ctx *c, ZipReader *z, const char *docrels, size_t rn, const char *base, const char *mainpart)
{
  size_t ctn = 0, pn = 0;
  char *ct = part(z, "[Content_Types].xml", &ctn), *pkgrels = part(z, "_rels/.rels", &pn);
  keep_rels(c->d, z, ct, ctn, pkgrels, pn, "", "");
  keep_rels(c->d, z, ct, ctn, docrels, rn, base, "word/document.xml");
  (void)mainpart;
  /* WP1: */
  /* WP2: */
  /* WP3: */
  /* WP4: */
  /* WP5: */
  /* WP7: */
  /* WP9: */
  /* WP10: */
  /* WP11: */
  /* WP13: */
  /* WP14a: */
  /* WP15a: */
  /* WP15b: */
  /* WP15c: */
  /* WP16: */
  /* WP17: */
  /* WP18: */
  /* WP19: */
  /* WP20: */
  /* WP21a: */
  /* WP21b: */
  /* WP22a: */
  /* WP22b: */
  /* WP23: */
  /* WP24: */
  read_info_parts(c->d, z, pkgrels, pn);
  /* WP27: */
  /* WP31: */
  free(ct); free(pkgrels);
}

/* the parts are read in the order the text depends on them: the main part is found through
   _rels/.rels (it need not be word/document.xml), then theme, numbering and styles, notes and
   comments (so references in the text can find them), settings, the body, and last the headers and
   footers the sections name. A missing part other than the body is just skipped. The new Doc
   has no undo history and is not marked modified. */
Doc *docx_load(const u8 *data, size_t len, unsigned *lost, char *err, size_t errcap)
{
  ZipReader z;
  Ctx c;
  char main[256] = "word/document.xml", base[256], path[300];
  char *xml, *rels;
  size_t n, rn;
  if (lost) *lost = 0;
  if (!zip_open(&z, data, len)) { snprintf(err, errcap, "This is not a Word document (no zip archive in it)."); return NULL; }
  memset(&c, 0, sizeof c);
  {
    /* Office's own theme, for documents without one */
    static const u32 office[12] = { 0x000000, 0xFFFFFF, 0x44546A, 0xE7E6E6, 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47, 0x0563C1, 0x954F72 };
    memcpy(c.clr, office, sizeof office);
  }
  if (!(c.d = doc_new())) { zip_close(&z); snprintf(err, errcap, "Not enough memory."); return NULL; }
  if ((rels = part(&z, "_rels/.rels", &rn))) { rel_target(rels, rn, "/officeDocument", "", main, sizeof main); free(rels); }
  snprintf(base, sizeof base, "%s", main);
  { char *sl = strrchr(base, '/'); if (sl) sl[1] = 0; else base[0] = 0; }
  snprintf(path, sizeof path, "%s_rels/%s.rels", base, main + strlen(base));
  rels = part(&z, path, &rn);
  /* theme fonts, lists and styles before the text that uses them */
  path[0] = 0;
  if (rels) rel_target(rels, rn, "/theme", base, path, sizeof path);
  if (!path[0]) snprintf(path, sizeof path, "%stheme/theme1.xml", base);
  if ((xml = part(&z, path, &n))) { parse_theme(&c, xml, n); free(xml); }
  path[0] = 0;
  path[0] = 0;
  if (rels) rel_target(rels, rn, "/numbering", base, path, sizeof path);
  if (!path[0]) snprintf(path, sizeof path, "%snumbering.xml", base);
  if ((xml = part(&z, path, &n))) { parse_numbering(&c, xml, n); free(xml); }
  path[0] = 0;
  if (rels) rel_target(rels, rn, "/styles", base, path, sizeof path);
  if (!path[0]) snprintf(path, sizeof path, "%sstyles.xml", base);
  if ((xml = part(&z, path, &n))) { parse_styles(&c, xml, n); free(xml); }
  install_styles(&c);
  path[0] = 0;
  if (rels) rel_target(rels, rn, "/footnotes", base, path, sizeof path);
  if (path[0] && (xml = part(&z, path, &n))) {
    /* footnotes' pictures would be in their own relationships: not read */
    c.z = NULL;
    parse_footnotes(&c, xml, n, 0);
    free(xml);
  }
  path[0] = 0;
  if (rels) rel_target(rels, rn, "/endnotes", base, path, sizeof path);
  if (path[0] && (xml = part(&z, path, &n))) {
    c.z = NULL;
    parse_footnotes(&c, xml, n, 1);
    free(xml);
  }
  path[0] = 0;
  if (rels) rel_target(rels, rn, "/comments", base, path, sizeof path);
  if (path[0] && (xml = part(&z, path, &n))) {
    c.z = NULL;
    parse_comments(&c, xml, n);
    free(xml);
  }
  path[0] = 0;
  if (rels) rel_target(rels, rn, "/settings", base, path, sizeof path);
  if (!path[0]) snprintf(path, sizeof path, "%ssettings.xml", base);
  if ((xml = part(&z, path, &n))) {
    Xml x;
    xml_init(&x, xml, n);
    while (xml_next(&x) != XT_EOF) {
      if (x.type == XT_START && !strcmp(x.name, "compatSetting")) {
        const char *nm = xml_attr(&x, "name");
        i32 v;
        if (nm && !strcmp(nm, "compatibilityMode") && attr_num(&x, "val", &v)) c.compat = (int)v;
      } else if (x.type == XT_START && !strcmp(x.name, "evenAndOddHeaders")) c.d->evenodd = (u8)onoff(&x);
      else if (x.type == XT_START && !strcmp(x.name, "autoHyphenation")) c.d->hyphenate = (u8)onoff(&x);
      if (x.type == XT_START) docx_read_setting_ext(&c, &x);
    }
    xml_free(&x);
    free(xml);
  }
  if (!(xml = part(&z, main, &n))) {
    snprintf(err, errcap, "The document's text (%s) is missing or damaged.", main);
    doc_free(c.d); free(rels); free_raw(&c); free(c.abs); free(c.fnid); free(c.fnidx); free(c.enid); free(c.enidx); free(c.cmid); free(c.cmidx); free(c.cmopen); zip_close(&z);
    return NULL;
  }
  c.z = &z; c.rels = rels; c.rn = rn; c.base = base;
  if (!parse_document(&c, xml, n)) {
    /* an empty body: keep the empty paragraph */
  }
  free(xml);
  /* the headers and footers the sections refer to: a section without a reference of a kind has the
     section before's (Word's "link to previous"); the last section's are the document's, and a
     section whose all come to the same has those */
  if (rels) {
    char fin[2 * HF_N][32], (*res)[2 * HF_N][32] = NULL, prev[2 * HF_N][32];
    int w, n;
    memset(prev, 0, sizeof prev);
    if (c.nsid && (res = malloc(c.nsid * sizeof *res)))
      for (n = 0; n < c.nsid; n++) {
        for (w = 0; w < 2 * HF_N; w++) strcpy(res[n][w], c.sid[n][w][0] ? c.sid[n][w] : prev[w]);
        memcpy(prev, res[n], sizeof prev);
      }
    for (w = 0; w < 2 * HF_N; w++) {
      const char *id = w < HF_N ? c.hdrid[w] : c.ftrid[w - HF_N];
      strcpy(fin[w], id[0] ? id : prev[w]);
    }
    for (w = 0; w < HF_N * 2; w++) read_hf(&c, &z, rels, rn, base, fin[w], w < HF_N ? &c.d->hdr[w] : &c.d->ftr[w - HF_N]);
    for (n = 0; res && n < c.nsid && n < c.d->nsects; n++) {
      SectFmt *sf = &c.d->sects[n];
      for (w = 0; w < 2 * HF_N && !strcmp(res[n][w], fin[w]); w++) ;
      if (w == 2 * HF_N || !(sf->hf = (Story *)calloc(2 * HF_N, sizeof(Story)))) continue;
      for (w = 0; w < HF_N * 2; w++) read_hf(&c, &z, rels, rn, base, res[n][w], &sf->hf[w]);
    }
    free(res);
  }
  /* sections set up as the last one (the document's page setup) have the document's */
  {
    int n;
    for (n = 0; n < c.d->nsects; n++) if (c.d->sects[n].haspage && !memcmp(&c.d->sects[n].page, &c.d->page, sizeof(PageSetup))) c.d->sects[n].haspage = 0;
  }
  free(c.sid);
  if ((xml = part(&z, "docProps/core.xml", &n))) { parse_core(c.d, xml, n); free(xml); }
  docx_read_parts_ext(&c, &z, rels, rn, base, main);
  free(rels);
  free_raw(&c); free(c.abs); free(c.fnid); free(c.fnidx); free(c.enid); free(c.enidx); free(c.cmid); free(c.cmidx); free(c.cmopen);
  zip_close(&z);
  doc_clear_undo(c.d);
  c.d->dirty = 0;
  if (lost) *lost = c.lost;
  return c.d;
}

/* ------------------------------------------------------------------ */
/* writing */
static const char *w_rstyle;           /* the next run's character style (its id), written by w_rpr */
/* append a w:rPr for f: with full, every field that isn't at its plain value (for styles and
   docDefaults), else only what differs from base (the paragraph style's character format), with
   explicit "off" values where f turns off something base has. A pending w_rstyle goes in first.
   Nothing is written if there is nothing to say. Bold and italic are doubled into the complex
   script slots (bCs, iCs) as Word does. */
static void w_rpr(Buf *o, Doc *d, const CharFmt *f, const CharFmt *base, int full)
{
  Buf b;
  unsigned diff = full ? CM_ALL : 0;
  buf_init(&b);
  if (!full) {
    diff = (unsigned)(f->flags ^ base->flags) & 0x7F;
    if (f->font != base->font) diff |= CM_FONT;
    if (f->size != base->size) diff |= CM_SIZE;
    if (f->color != base->color) diff |= CM_COLOR;
    if (f->hl != base->hl) diff |= CM_HL;
    if ((f->fx ^ base->fx) & FX_SMALLCAPS) diff |= CM_SMALLCAPS;
    if ((f->fx ^ base->fx) & FX_DSTRIKE) diff |= CM_DSTRIKE;
    if ((f->fx ^ base->fx) & FX_BORDER) diff |= CM_CBORDER;
    if (f->ul != base->ul && (f->flags & CF_UNDERLINE)) diff |= CM_UL | CM_UNDERLINE;
    if (f->spacing != base->spacing) diff |= CM_SPACING;
    if (f->scale != base->scale) diff |= CM_SCALE;
    if (f->position != base->position) diff |= CM_POSITION;
  }
  if (w_rstyle) {
    buf_adds(&b, "<w:rStyle w:val=\""); xml_escape(&b, w_rstyle, strlen(w_rstyle)); buf_adds(&b, "\"/>");
    w_rstyle = NULL;
  }
  if (!diff && !b.len) return;
  if ((diff & CM_FONT) && f->font < d->nfonts) {
    buf_adds(&b, "<w:rFonts w:ascii=\""); xml_escape(&b, d->fonts[f->font], strlen(d->fonts[f->font]));
    buf_adds(&b, "\" w:hAnsi=\""); xml_escape(&b, d->fonts[f->font], strlen(d->fonts[f->font]));
    buf_adds(&b, "\" w:cs=\""); xml_escape(&b, d->fonts[f->font], strlen(d->fonts[f->font]));
    buf_adds(&b, "\"/>");
  }
  if (diff & CM_BOLD) buf_adds(&b, f->flags & CF_BOLD ? "<w:b/><w:bCs/>" : "<w:b w:val=\"0\"/><w:bCs w:val=\"0\"/>");
  if (diff & CM_ITALIC) buf_adds(&b, f->flags & CF_ITALIC ? "<w:i/><w:iCs/>" : "<w:i w:val=\"0\"/><w:iCs w:val=\"0\"/>");
  if ((diff & CM_CAPS) && (!full || (f->flags & CF_CAPS))) buf_adds(&b, f->flags & CF_CAPS ? "<w:caps/>" : "<w:caps w:val=\"0\"/>");
  if ((diff & CM_STRIKE) && (!full || (f->flags & CF_STRIKE))) buf_adds(&b, f->flags & CF_STRIKE ? "<w:strike/>" : "<w:strike w:val=\"0\"/>");
  if ((diff & CM_COLOR) && (!full || f->color != COLOR_AUTO)) {
    if (f->color == COLOR_AUTO) buf_adds(&b, "<w:color w:val=\"auto\"/>");
    else buf_printf(&b, "<w:color w:val=\"%06lX\"/>", (unsigned long)(f->color & 0xFFFFFF));
  }
  if (diff & CM_SIZE) buf_printf(&b, "<w:sz w:val=\"%u\"/><w:szCs w:val=\"%u\"/>", (unsigned)f->size, (unsigned)f->size);
  if ((diff & CM_HL) && (highlight_name(f->hl) || !full))
    buf_printf(&b, "<w:highlight w:val=\"%s\"/>", highlight_name(f->hl) ? highlight_name(f->hl) : "none");
  if ((diff & CM_UNDERLINE) && (!full || (f->flags & CF_UNDERLINE))) {
    static const char *const uls[UL_N] = { "single", "double", "thick", "dotted", "dash", "wave", "words" };
    buf_printf(&b, "<w:u w:val=\"%s\"/>", f->flags & CF_UNDERLINE ? uls[f->ul < UL_N ? f->ul : 0] : "none");
  }
  if ((diff & CM_SMALLCAPS) && (!full || (f->fx & FX_SMALLCAPS))) buf_adds(&b, f->fx & FX_SMALLCAPS ? "<w:smallCaps/>" : "<w:smallCaps w:val=\"0\"/>");
  if ((diff & CM_DSTRIKE) && (!full || (f->fx & FX_DSTRIKE))) buf_adds(&b, f->fx & FX_DSTRIKE ? "<w:dstrike/>" : "<w:dstrike w:val=\"0\"/>");
  if ((diff & CM_SPACING) && (!full || f->spacing)) buf_printf(&b, "<w:spacing w:val=\"%d\"/>", (int)f->spacing);
  if ((diff & CM_SCALE) && (!full || f->scale)) buf_printf(&b, "<w:w w:val=\"%u\"/>", (unsigned)(f->scale ? f->scale : 100));
  if ((diff & CM_POSITION) && (!full || f->position)) buf_printf(&b, "<w:position w:val=\"%d\"/>", (int)f->position);
  if ((diff & CM_CBORDER) && (!full || (f->fx & FX_BORDER)))
    buf_adds(&b, f->fx & FX_BORDER ? "<w:bdr w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>" : "<w:bdr w:val=\"nil\"/>");
  if ((diff & CM_HL) && f->hl && !highlight_name(f->hl))     /* not one of Word's highlights: shading */
    buf_printf(&b, "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"%06lX\"/>", (unsigned long)(f->hl & 0xFFFFFF));
  if ((diff & (CM_SUPER | CM_SUB)) && (!full || (f->flags & (CF_SUPER | CF_SUB))))
    buf_adds(&b, f->flags & CF_SUPER ? "<w:vertAlign w:val=\"superscript\"/>" : f->flags & CF_SUB ? "<w:vertAlign w:val=\"subscript\"/>" : "<w:vertAlign w:val=\"baseline\"/>");
  if (b.len) { buf_adds(o, "<w:rPr>"); buf_add(o, b.data, b.len); buf_adds(o, "</w:rPr>"); }
  buf_free(&b);
}
/* append the inside of a w:pPr for p (the caller writes the element itself, so it can add
   w:sectPr or w:rPr): with full, everything (for styles), else the style reference and what
   differs from base, the paragraph's style. Lengths are twips. Elements go in the order the
   schema wants: Word can refuse a file with them out of order. */
static void w_ppr_body(Buf *b, Doc *d, const ParaFmt *p, const ParaFmt *base, int full)
{
  static const char *const jc[4] = { "left", "center", "right", "both" };
  int k;
  if (!full && p->style) { buf_adds(b, "<w:pStyle w:val=\""); xml_escape(b, d->styles[p->style].id, strlen(d->styles[p->style].id)); buf_adds(b, "\"/>"); }
  if (full ? p->keepnext : p->keepnext != base->keepnext) buf_adds(b, p->keepnext ? "<w:keepNext/>" : "<w:keepNext w:val=\"0\"/>");
  if (full ? p->keeplines : p->keeplines != base->keeplines) buf_adds(b, p->keeplines ? "<w:keepLines/>" : "<w:keepLines w:val=\"0\"/>");
  if (p->pagebreak && (full || !base->pagebreak)) buf_adds(b, "<w:pageBreakBefore/>");
  if (full ? p->nowidow : p->nowidow != base->nowidow) buf_adds(b, p->nowidow ? "<w:widowControl w:val=\"0\"/>" : "<w:widowControl/>");
  if (p->list && p->list <= d->nlists && (full || p->list != base->list || p->level != base->level))
    buf_printf(b, "<w:numPr><w:ilvl w:val=\"%u\"/><w:numId w:val=\"%u\"/></w:numPr>", (unsigned)p->level, (unsigned)d->lists[p->list - 1].id);
  else if (!p->list && base->list && !full) buf_adds(b, "<w:numPr><w:numId w:val=\"0\"/></w:numPr>");
  if (full ? p->bstyle[0] || p->bstyle[1] || p->bstyle[2] || p->bstyle[3] || p->bstyle[4] :
             (memcmp(p->bstyle, base->bstyle, sizeof p->bstyle) || memcmp(p->bwidth, base->bwidth, sizeof p->bwidth) ||
              memcmp(p->bspace, base->bspace, sizeof p->bspace) || memcmp(p->bcolor, base->bcolor, sizeof p->bcolor))) {
    static const char *const names[PE_N] = { "top", "left", "bottom", "right", "between" };
    static const char *const kinds[] = { "nil", "single", "double", "thick", "dotted", "dashed" };
    static const int order[PE_N] = { PE_TOP, PE_LEFT, PE_BOTTOM, PE_RIGHT, PE_BETWEEN };
    buf_adds(b, "<w:pBdr>");
    for (k = 0; k < PE_N; k++) {
      int e = order[k];
      if (!p->bstyle[e]) { if (base->bstyle[e] && !full) buf_printf(b, "<w:%s w:val=\"nil\"/>", names[e]); continue; }
      buf_printf(b, "<w:%s w:val=\"%s\" w:sz=\"%u\" w:space=\"%u\" w:color=\"", names[e], kinds[p->bstyle[e] <= PB_DASHED ? p->bstyle[e] : 1],
                 (unsigned)(p->bwidth[e] ? p->bwidth[e] : 4), (unsigned)p->bspace[e]);
      if (p->bcolor[e] == COLOR_AUTO) buf_adds(b, "auto\"/>"); else buf_printf(b, "%06lX\"/>", (unsigned long)(p->bcolor[e] & 0xFFFFFF));
    }
    buf_adds(b, "</w:pBdr>");
  }
  if (full ? p->shade != 0 : p->shade != base->shade) {
    if (p->shade) buf_printf(b, "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"%06lX\"/>", (unsigned long)(p->shade & 0xFFFFFF));
    else buf_adds(b, "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"auto\"/>");
  }
  if (full ? p->tabs[0] != 0 : (memcmp(p->tabs, base->tabs, sizeof p->tabs) || memcmp(p->tabkind, base->tabkind, sizeof p->tabkind) ||
                                 memcmp(p->tablead, base->tablead, sizeof p->tablead))) {
    static const char *const kinds[4] = { "left", "center", "right", "decimal" }, *const leads[4] = { "", " w:leader=\"dot\"", " w:leader=\"hyphen\"", " w:leader=\"underscore\"" };
    buf_adds(b, "<w:tabs>");
    for (k = 0; k < 8 && p->tabs[k]; k++)
      buf_printf(b, "<w:tab w:val=\"%s\"%s w:pos=\"%ld\"/>", kinds[p->tabkind[k] & 3], leads[p->tablead[k] & 3], (long)p->tabs[k]);
    if (!p->tabs[0]) for (k = 0; k < 8 && base->tabs[k]; k++) buf_printf(b, "<w:tab w:val=\"clear\" w:pos=\"%ld\"/>", (long)base->tabs[k]);
    buf_adds(b, "</w:tabs>");
  }
  if (full || p->before != base->before || p->after != base->after || p->line != base->line || p->linerule != base->linerule)
    buf_printf(b, "<w:spacing w:before=\"%ld\" w:after=\"%ld\" w:line=\"%ld\" w:lineRule=\"%s\"/>", (long)p->before, (long)p->after,
               (long)p->line, p->linerule == LS_EXACT ? "exact" : p->linerule == LS_ATLEAST ? "atLeast" : "auto");
  if (full || p->left != base->left || p->right != base->right || p->first != base->first) {
    buf_printf(b, "<w:ind w:left=\"%ld\" w:right=\"%ld\"", (long)p->left, (long)p->right);
    if (p->first < 0) buf_printf(b, " w:hanging=\"%ld\"/>", (long)-p->first);
    else buf_printf(b, " w:firstLine=\"%ld\"/>", (long)p->first);
  }
  if (full ? p->contextual : p->contextual != base->contextual) buf_adds(b, p->contextual ? "<w:contextualSpacing/>" : "<w:contextualSpacing w:val=\"0\"/>");
  if (full ? p->align != AL_LEFT : p->align != base->align) buf_printf(b, "<w:jc w:val=\"%s\"/>", jc[p->align & 3]);
}
static int w_deleted;                   /* writing a deletion's runs: w:delText */
/* append text as w:t elements (w:delText inside a deletion), with tabs as w:tab and Quill's line
   breaks (\v) as w:br; xml:space="preserve" where leading or trailing spaces would otherwise be
   dropped */
static void w_text(Buf *o, const char *s, u32 n)
{
  u32 i, a = 0;
  for (i = 0; i <= n; i++) {
    if (i < n && s[i] != '\t' && s[i] != '\v') continue;
    if (i > a) {
      int keep = s[a] == ' ' || s[i - 1] == ' ';
      if (w_deleted) buf_adds(o, "<w:delText xml:space=\"preserve\">");
      else buf_adds(o, keep ? "<w:t xml:space=\"preserve\">" : "<w:t>");
      xml_escape(o, s + a, i - a);
      buf_adds(o, w_deleted ? "</w:delText>" : "</w:t>");
    }
    if (i < n) buf_adds(o, s[i] == '\t' ? "<w:tab/>" : "<w:br/>");
    a = i + 1;
  }
}
static const char *const XMLHDR = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n";
static const char *const WNS = "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
                               "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
                               "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\"";
/* while saving: each picture's media number (0: not used: shapes and text boxes have none, and entries
   sharing a file share its number), and a counter for drawing ids */
/* the pictures saved, each file once (by its data), numbered for the media folder */
static u32 save_drawing;                 /* drawings so far (their ids) */
typedef struct { const u8 *data; u32 len; u8 type; } Media;
static Media *save_ml; static int save_nml;
/* the media number (1-based) of a picture's data in save_ml, matched by pointer so entries that
   share one file share it; 0 if it isn't there */
static int media_find(const Image *im)
{
  int k;
  if (!im->data) return 0;
  for (k = 0; k < save_nml; k++) if (save_ml[k].data == im->data) return k + 1;
  return 0;
}
/* add a picture's data to save_ml unless it's already there (a group adds each of its pictures);
   if memory runs out the picture is simply left out of the file */
static void media_add(const Image *im)
{
  Media *nm;
  int k;
  if (im->kind == OBJ_GROUP) { for (k = 0; k < im->nkids; k++) media_add(&im->kids[k]); return; }
  if (im->kind != OBJ_PICTURE || !im->data || media_find(im)) return;
  if (!(nm = (Media *)realloc(save_ml, (save_nml + 1) * sizeof(Media)))) return;
  save_ml = nm;
  save_ml[save_nml].data = im->data; save_ml[save_nml].len = im->len; save_ml[save_nml].type = im->type;
  save_nml++;
}
/* an object can be saved: it isn't a picture whose file is missing */
static int obj_saved(const Image *im) { return im->kind != OBJ_PICTURE || media_find(im) > 0; }
static int *save_notes;                 /* each footnote's id in footnotes.xml (0: not referred to) */
static int *save_comments;              /* each comment's id + 1 in comments.xml (0: its marks are gone) */
static int *save_enotes;                /* each endnote's id in endnotes.xml (0: not referred to) */
/* a border line, as element name (top, left, insideH...): width in eighths of a point. Off writes
   val="nil", which removes a line a style would give. */
static void w_line(Buf *o, const char *name, int on, u8 style, u8 width, u32 color)
{
  static const char *const kinds[] = { "nil", "single", "double", "thick", "dotted", "dashed" };
  if (!on) { buf_printf(o, "<w:%s w:val=\"nil\"/>", name); return; }
  if (style > PB_DASHED || !style) style = PB_SINGLE;          /* a cell's 0 is a single line */
  buf_printf(o, "<w:%s w:val=\"%s\" w:sz=\"%u\" w:space=\"0\" w:color=\"", name, kinds[style], (unsigned)(width ? width : 4));
  if (color == COLOR_AUTO) buf_adds(o, "auto\"/>"); else buf_printf(o, "%06lX\"/>", (unsigned long)(color & 0xFFFFFF));
}
/* the character fields mask names (a table style part's rPr) */
/* a table style part sets only some fields, which a CharFmt can't say by itself, so it is written
   against a made-up base that differs from f in exactly the masked fields */
static void w_rpr_mask(Buf *o, Doc *d, unsigned mask, const CharFmt *f)
{
  CharFmt base = *f;
  base.flags ^= (u8)(mask & 0x7F);
  if (mask & CM_FONT) base.font++;
  if (mask & CM_SIZE) base.size++;
  if (mask & CM_COLOR) base.color ^= 1;
  if (mask & CM_HL) base.hl ^= 1;
  w_rpr(o, d, f, &base, 0);
}
/* the contents of table style part k (TC_WHOLE for the style itself): its alignment, character
   format, table lines, cell margins (whole table only), cell edges and fill */
static void w_tcond(Buf *o, Doc *d, const TableStyle *ts, int k)
{
  static const char *const edges[TE_N] = { "top", "left", "bottom", "right", "insideH", "insideV" };
  const TCond *c = &ts->c[k];
  int e, any = 0;
  static const char *const jc[4] = { "left", "center", "right", "both" };
  if (c->align_set) buf_printf(o, "<w:pPr><w:jc w:val=\"%s\"/></w:pPr>", jc[c->align & 3]);
  if (c->cmask) w_rpr_mask(o, d, c->cmask, &c->cf);
  for (e = 0; e < TE_N; e++) if (c->b[e].set) any = 1;
  if (any || (k == TC_WHOLE && ts->mar_set)) {
    buf_adds(o, "<w:tblPr>");
    if (any) {
      buf_adds(o, "<w:tblBorders>");
      for (e = 0; e < TE_N; e++) if (c->b[e].set) w_line(o, edges[e], c->b[e].style != PB_NONE, c->b[e].style, c->b[e].width, c->b[e].color);
      buf_adds(o, "</w:tblBorders>");
    }
    if (k == TC_WHOLE && ts->mar_set)
      buf_printf(o, "<w:tblCellMar><w:left w:w=\"%ld\" w:type=\"dxa\"/><w:right w:w=\"%ld\" w:type=\"dxa\"/></w:tblCellMar>", (long)ts->marl, (long)ts->marr);
    buf_adds(o, "</w:tblPr>");
  }
  any = c->shade_set;
  for (e = 0; e < 4; e++) if (c->cb[e].set) any = 1;
  if (any) {
    buf_adds(o, "<w:tcPr>");
    if (c->cb[0].set || c->cb[1].set || c->cb[2].set || c->cb[3].set) {
      buf_adds(o, "<w:tcBorders>");
      for (e = 0; e < 4; e++) if (c->cb[e].set) w_line(o, edges[e], c->cb[e].style != PB_NONE, c->cb[e].style, c->cb[e].width, c->cb[e].color);
      buf_adds(o, "</w:tcBorders>");
    }
    if (c->shade_set) {
      if (c->shade == COLOR_AUTO) buf_adds(o, "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"auto\"/>");
      else buf_printf(o, "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"%06lX\"/>", (unsigned long)(c->shade & 0xFFFFFF));
    }
    buf_adds(o, "</w:tcPr>");
  }
}
/* append every table style as a w:style of type table, with a w:tblStylePr for each part in use */
static void w_tstyles(Buf *o, Doc *d)
{
  static const char *const names[TC_N] = { "wholeTable", "band1Vert", "band2Vert", "band1Horz", "band2Horz", "firstCol", "lastCol",
                                           "firstRow", "lastRow", "neCell", "nwCell", "seCell", "swCell" };
  int i, k;
  for (i = 0; i < d->ntstyles; i++) {
    const TableStyle *ts = &d->tstyles[i];
    buf_adds(o, "<w:style w:type=\"table\" w:styleId=\""); xml_escape(o, ts->id, strlen(ts->id));
    buf_adds(o, "\"><w:name w:val=\""); xml_escape(o, ts->name, strlen(ts->name)); buf_adds(o, "\"/>");
    if (ts->based[0]) { buf_adds(o, "<w:basedOn w:val=\""); xml_escape(o, ts->based, strlen(ts->based)); buf_adds(o, "\"/>"); }
    w_tcond(o, d, ts, TC_WHOLE);
    for (k = 1; k < TC_N; k++) {
      if (!ts->c[k].used) continue;
      buf_printf(o, "<w:tblStylePr w:type=\"%s\">", names[k]);
      w_tcond(o, d, ts, k);
      buf_adds(o, "</w:tblStylePr>");
    }
    buf_adds(o, "</w:style>");
  }
}
/* open a table: w:tbl with its tblPr (style, width as the grid's sum, alignment, indent, layout, cell
   margins, look) and tblGrid; lengths in twips (dxa) */
static void w_tbl_start(Buf *o, Doc *d, const Table *t)
{
  static const char *const jc[3] = { "left", "center", "right" };
  i32 sum = 0;
  int k;
  for (k = 0; k < t->ncols; k++) sum += t->grid[k];
  buf_adds(o, "<w:tbl><w:tblPr>");
  if (t->tstyle && t->tstyle <= d->ntstyles) {
    const char *id = d->tstyles[t->tstyle - 1].id;
    buf_adds(o, "<w:tblStyle w:val=\""); xml_escape(o, id, strlen(id)); buf_adds(o, "\"/>");
  }
  buf_printf(o, "<w:tblW w:w=\"%ld\" w:type=\"dxa\"/>", (long)sum);
  if (t->jc && t->jc < 3) buf_printf(o, "<w:jc w:val=\"%s\"/>", jc[t->jc]);
  buf_printf(o, "<w:tblInd w:w=\"%ld\" w:type=\"dxa\"/><w:tblLayout w:type=\"%s\"/><w:tblCellMar>", (long)t->indent, t->autofit ? "autofit" : "fixed");
  if (t->mart) buf_printf(o, "<w:top w:w=\"%ld\" w:type=\"dxa\"/>", (long)t->mart);
  buf_printf(o, "<w:left w:w=\"%ld\" w:type=\"dxa\"/>", (long)t->marl);
  if (t->marb) buf_printf(o, "<w:bottom w:w=\"%ld\" w:type=\"dxa\"/>", (long)t->marb);
  buf_printf(o, "<w:right w:w=\"%ld\" w:type=\"dxa\"/></w:tblCellMar>", (long)t->marr);
  buf_printf(o, "<w:tblLook w:val=\"%04X\" w:firstRow=\"%d\" w:lastRow=\"%d\" w:firstColumn=\"%d\" w:lastColumn=\"%d\" w:noHBand=\"%d\" w:noVBand=\"%d\"/>",
             (unsigned)t->look, !!(t->look & LK_FIRSTROW), !!(t->look & LK_LASTROW), !!(t->look & LK_FIRSTCOL), !!(t->look & LK_LASTCOL),
             !!(t->look & LK_NOHBAND), !!(t->look & LK_NOVBAND));
  buf_adds(o, "</w:tblPr><w:tblGrid>");
  for (k = 0; k < t->ncols; k++) buf_printf(o, "<w:gridCol w:w=\"%ld\"/>", (long)t->grid[k]);
  buf_adds(o, "</w:tblGrid>");
}
/* open a row, with the row properties its cells carry: height (negative means exact), heading
   row, can't split */
static void w_tr_start(Buf *o, const Cell *c)
{
  buf_adds(o, "<w:tr>");
  if (c->rowh || c->header || c->cantsplit) {
    buf_adds(o, "<w:trPr>");
    if (c->cantsplit) buf_adds(o, "<w:cantSplit/>");
    if (c->rowh) buf_printf(o, "<w:trHeight w:val=\"%d\" w:hRule=\"%s\"/>", c->rowh < 0 ? -c->rowh : c->rowh, c->rowh < 0 ? "exact" : "atLeast");
    if (c->header) buf_adds(o, "<w:tblHeader/>");
    buf_adds(o, "</w:trPr>");
  }
}
/* open a cell and write its tcPr: width (the grid columns it spans, twips), span, vertical merge,
   all four edges (explicitly, so the table style's lines don't come back), fill and vertical
   alignment */
static void w_tc_start(Buf *o, const Table *t, const Cell *c)
{
  static const char *const edges[4] = { "top", "left", "bottom", "right" };
  i32 w = 0;
  int k;
  for (k = c->col; k < c->col + c->span && k < t->ncols; k++) w += t->grid[k];
  buf_printf(o, "<w:tc><w:tcPr><w:tcW w:w=\"%ld\" w:type=\"dxa\"/>", (long)w);
  if (c->span > 1) buf_printf(o, "<w:gridSpan w:val=\"%u\"/>", (unsigned)c->span);
  if (c->vmerge) buf_adds(o, c->vmerge == VM_START ? "<w:vMerge w:val=\"restart\"/>" : "<w:vMerge/>");
  buf_adds(o, "<w:tcBorders>");
  for (k = 0; k < 4; k++) w_line(o, edges[k], c->borders & (1 << k), c->bstyle[k], c->bwidth[k], c->bcolor[k]);
  buf_adds(o, "</w:tcBorders>");
  if (c->shade != COLOR_AUTO) buf_printf(o, "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"%06lX\"/>", (unsigned long)(c->shade & 0xFFFFFF));
  if (c->valign) buf_adds(o, c->valign == 1 ? "<w:vAlign w:val=\"center\"/>" : "<w:vAlign w:val=\"bottom\"/>");
  buf_adds(o, "</w:tcPr>");
}
/* the cells a paragraph is in, outermost first; how many */
static int cell_path(Doc *d, const ParaFmt *pf, const Cell **v)
{
  int n = 0;
  if (!pf) return 0;
  if (pf->outer.table && pf->outer.table <= d->ntables) v[n++] = &pf->outer;
  if (pf->cell.table && pf->cell.table <= d->ntables) v[n++] = &pf->cell;
  return n;
}
/* whether two cell references are the same cell (the other fields, such as its lines, may differ) */
static int same_c(const Cell *a, const Cell *b) { return a->table == b->table && a->row == b->row && a->col == b->col; }
/* table structure around paragraph i: the starts of tables, rows and cells before it (as far as the
   paragraph before isn't in them already), the ends after it */
static void w_table_open(Buf *o, Doc *d, u32 i)
{
  const Cell *pv[2], *cv[2];
  int np = cell_path(d, i ? &d->p[i - 1]->pf : NULL, pv), nc = cell_path(d, &d->p[i]->pf, cv), k = 0, l;
  while (k < np && k < nc && same_c(pv[k], cv[k])) k++;
  for (l = k; l < nc; l++) {
    const Table *t = &d->tables[cv[l]->table - 1];
    int tblopen = l == k && l < np && pv[l]->table == cv[l]->table;
    int rowopen = tblopen && pv[l]->row == cv[l]->row;
    if (!tblopen) w_tbl_start(o, d, t);
    if (!rowopen) w_tr_start(o, cv[l]);
    w_tc_start(o, t, cv[l]);
  }
}
/* close the cells, rows and tables paragraph i is in that the next paragraph isn't, innermost
   first. Word needs a paragraph after a nested table before its cell ends, and after a table at
   the end of the document, so empty ones are written there. */
static void w_table_close(Buf *o, Doc *d, u32 i)
{
  const Cell *cv[2], *nv[2];
  int nc = cell_path(d, &d->p[i]->pf, cv), nn = cell_path(d, i + 1 < d->np ? &d->p[i + 1]->pf : NULL, nv), k = 0, l;
  while (k < nc && k < nn && same_c(cv[k], nv[k])) k++;
  for (l = nc - 1; l >= k; l--) {
    int sametbl = l == k && l < nn && nv[l]->table == cv[l]->table;
    if (l < nc - 1) buf_adds(o, "<w:p/>");        /* a cell ends with a paragraph, not a table */
    buf_adds(o, "</w:tc>");
    if (sametbl && nv[l]->row == cv[l]->row) continue;
    buf_adds(o, "</w:tr>");
    if (sametbl) continue;
    buf_adds(o, "</w:tbl>");
  }
  if (nc && i + 1 >= d->np) buf_adds(o, "<w:p/>");  /* a document ends with a paragraph */
}
static void w_para_ex(Buf *o, Doc *d, Para *p, const char *lead, const char *tail);
/* an object's outline (a:ln), or none */
static void w_outline(Buf *o, const Image *im)
{
  if (im->lstyle == PB_NONE || !im->lwidth) { buf_adds(o, "<a:ln><a:noFill/></a:ln>"); return; }
  buf_printf(o, "<a:ln w=\"%ld\"><a:solidFill><a:srgbClr val=\"%06lX\"/></a:solidFill>", (long)im->lwidth * 1587, (unsigned long)(im->lcolor & 0xFFFFFF));
  if (im->lstyle == PB_DOTTED) buf_adds(o, "<a:prstDash val=\"sysDot\"/>");
  else if (im->lstyle == PB_DASHED) buf_adds(o, "<a:prstDash val=\"dash\"/>");
  if (im->kind != OBJ_PICTURE && im->shape == SH_ARROW) buf_adds(o, "<a:tailEnd type=\"triangle\"/>");
  buf_adds(o, "</a:ln>");
}
/* an object's a:xfrm: turned and flipped, at off (EMU; 0 for one in the text), its size; a group's child space too */
static void w_xfrm(Buf *o, const Image *im, long offx, long offy, long cx, long cy)
{
  buf_adds(o, "<a:xfrm");
  if (im->rot) buf_printf(o, " rot=\"%ld\"", (long)im->rot * 6000L);
  if (im->flip & 1) buf_adds(o, " flipH=\"1\"");
  if (im->flip & 2) buf_adds(o, " flipV=\"1\"");
  buf_printf(o, "><a:off x=\"%ld\" y=\"%ld\"/><a:ext cx=\"%ld\" cy=\"%ld\"/>", offx, offy, cx, cy);
  if (im->kind == OBJ_GROUP) buf_printf(o, "<a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"%ld\" cy=\"%ld\"/>", (long)im->chw * 635, (long)im->chh * 635);
  buf_adds(o, "</a:xfrm>");
}
static void w_para_ex(Buf *o, Doc *d, Para *p, const char *lead, const char *tail);
/* an object's drawing: a picture (pic:pic), shape or text box (wps:wsp) or group (wpg:wgp, or wpg:grpSp in
   one); child: in a group, at offx, offy of its space */
/* *id counts the drawing ids (every object, and each child of a group, needs its own). Sizes go
   from twips to EMU (635 a twip). A picture must already be in save_ml, since its relationship id
   is rIdImg plus its media number. */
static void w_obj(Buf *o, Doc *d, const Image *im, int child, long offx, long offy, u32 *id)
{
  long cx = (long)im->cx * 635, cy = (long)im->cy * 635;
  u32 me = ++*id;
  int k;
  if (im->kind == OBJ_PICTURE) {
    int m = media_find(im);
    buf_printf(o, "<pic:pic xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
                  "<pic:nvPicPr><pic:cNvPr id=\"%lu\" name=\"image%d\"/><pic:cNvPicPr/></pic:nvPicPr>"
                  "<pic:blipFill><a:blip r:embed=\"rIdImg%d\"/>", (unsigned long)me, m, m);
    if (im->crop[0] || im->crop[1] || im->crop[2] || im->crop[3])
      buf_printf(o, "<a:srcRect l=\"%ld\" t=\"%ld\" r=\"%ld\" b=\"%ld\"/>", (long)im->crop[0], (long)im->crop[1], (long)im->crop[2], (long)im->crop[3]);
    buf_adds(o, "<a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr>");
    w_xfrm(o, im, offx, offy, cx, cy);
    buf_adds(o, "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>");
    if (im->lstyle != PB_NONE && im->lwidth) w_outline(o, im);
    buf_adds(o, "</pic:spPr></pic:pic>");
  } else if (im->kind == OBJ_GROUP) {
    if (child) buf_printf(o, "<wpg:grpSp><wpg:cNvPr id=\"%lu\" name=\"Group %lu\"/><wpg:cNvGrpSpPr/><wpg:grpSpPr>", (unsigned long)me, (unsigned long)me);
    else buf_adds(o, "<wpg:wgp xmlns:wpg=\"http://schemas.microsoft.com/office/word/2010/wordprocessingGroup\"><wpg:cNvGrpSpPr/><wpg:grpSpPr>");
    w_xfrm(o, im, offx, offy, cx, cy);
    buf_adds(o, "</wpg:grpSpPr>");
    for (k = 0; k < im->nkids; k++)
      if (obj_saved(&im->kids[k])) w_obj(o, d, &im->kids[k], 1, (long)im->kids[k].x * 635, (long)im->kids[k].y * 635, id);
    buf_adds(o, child ? "</wpg:grpSp>" : "</wpg:wgp>");
  } else {
    buf_adds(o, "<wps:wsp xmlns:wps=\"http://schemas.microsoft.com/office/word/2010/wordprocessingShape\">");
    if (child) buf_printf(o, "<wps:cNvPr id=\"%lu\" name=\"Shape %lu\"/>", (unsigned long)me, (unsigned long)me);
    buf_adds(o, im->kind == OBJ_TEXTBOX ? "<wps:cNvSpPr txBox=\"1\"/><wps:spPr>" : "<wps:cNvSpPr/><wps:spPr>");
    w_xfrm(o, im, offx, offy, cx, cy);
    buf_printf(o, "<a:prstGeom prst=\"%s\"><a:avLst/></a:prstGeom>", prst_names[im->shape < SH_N ? im->shape : 0]);
    if (im->fill == COLOR_AUTO) buf_adds(o, "<a:noFill/>"); else buf_printf(o, "<a:solidFill><a:srgbClr val=\"%06lX\"/></a:solidFill>", (unsigned long)(im->fill & 0xFFFFFF));
    w_outline(o, im);
    buf_adds(o, "</wps:spPr>");
    if (im->text && im->text->np) {
      u32 i;
      buf_adds(o, "<wps:txbx><w:txbxContent>");
      for (i = 0; i < im->text->np; i++) w_para_ex(o, d, im->text->p[i], NULL, NULL);
      buf_adds(o, "</w:txbxContent></wps:txbx>");
    }
    buf_adds(o, "<wps:bodyPr rot=\"0\" vert=\"horz\" wrap=\"square\" lIns=\"91440\" tIns=\"45720\" rIns=\"91440\" bIns=\"45720\" anchor=\"t\"><a:noAutofit/></wps:bodyPr></wps:wsp>");
  }
}
/* a picture, shape or text box (Doc image k) as a run: in line (wp:inline) or floating (wp:anchor),
   with its position, wrap and wrap outline. rpr (rl bytes) is the run's w:rPr. Twips go to EMU,
   635 a twip; floating objects are stacked in the order they are written. */
static void w_picture(Buf *o, Doc *d, const char *rpr, size_t rl, int k)
{
  static const char *const hrels[4] = { "column", "page", "margin", "column" }, *const vrels[4] = { "paragraph", "page", "margin", "paragraph" };
  static const char *const haligns[4] = { "", "left", "center", "right" }, *const valigns[4] = { "", "top", "center", "bottom" };
  const Image *im = &d->images[k];
  long cx = (long)im->cx * 635, cy = (long)im->cy * 635;
  u32 id = ++save_drawing;
  int fl = im->wrap != WRAP_INLINE, i;
  buf_adds(o, "<w:r>"); buf_add(o, rpr, rl);
  buf_printf(o, "<w:drawing><wp:%s distT=\"%ld\" distB=\"%ld\" distL=\"%ld\" distR=\"%ld\"", fl ? "anchor" : "inline",
             (long)im->dt * 635, (long)im->db * 635, (long)im->dl * 635, (long)im->dr * 635);
  if (fl) {
    buf_printf(o, " simplePos=\"0\" relativeHeight=\"%lu\" behindDoc=\"%d\" locked=\"0\" layoutInCell=\"1\" allowOverlap=\"1\"><wp:simplePos x=\"0\" y=\"0\"/>",
               (unsigned long)(251658240L + (long)(im->z < -3900000 ? -3900000 : im->z > 29000000 ? 29000000 : im->z) * 64 + (id & 63)), im->wrap == WRAP_BEHIND);
    buf_printf(o, "<wp:positionH relativeFrom=\"%s\">", hrels[im->hrel & 3]);
    if (im->halign) buf_printf(o, "<wp:align>%s</wp:align>", haligns[im->halign & 3]); else buf_printf(o, "<wp:posOffset>%ld</wp:posOffset>", (long)im->x * 635);
    buf_printf(o, "</wp:positionH><wp:positionV relativeFrom=\"%s\">", vrels[im->vrel & 3]);
    if (im->valign) buf_printf(o, "<wp:align>%s</wp:align>", valigns[im->valign & 3]); else buf_printf(o, "<wp:posOffset>%ld</wp:posOffset>", (long)im->y * 635);
    buf_adds(o, "</wp:positionV>");
  } else buf_adds(o, ">");
  buf_printf(o, "<wp:extent cx=\"%ld\" cy=\"%ld\"/><wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/>", cx, cy);
  if (fl) {
    switch (im->wrap) {
    case WRAP_SQUARE: buf_adds(o, "<wp:wrapSquare wrapText=\"bothSides\"/>"); break;
    case WRAP_TIGHT:
      buf_adds(o, "<wp:wrapTight wrapText=\"bothSides\"><wp:wrapPolygon edited=\"0\">");
      if (im->npoly >= 3) {
        for (i = 0; i <= im->npoly; i++)
          buf_printf(o, "<wp:%s x=\"%ld\" y=\"%ld\"/>", i ? "lineTo" : "start", (long)im->poly[2 * (i % im->npoly)], (long)im->poly[2 * (i % im->npoly) + 1]);
      } else buf_adds(o, "<wp:start x=\"0\" y=\"0\"/><wp:lineTo x=\"0\" y=\"21600\"/><wp:lineTo x=\"21600\" y=\"21600\"/><wp:lineTo x=\"21600\" y=\"0\"/><wp:lineTo x=\"0\" y=\"0\"/>");
      buf_adds(o, "</wp:wrapPolygon></wp:wrapTight>");
      break;
    case WRAP_TOPBOTTOM: buf_adds(o, "<wp:wrapTopAndBottom/>"); break;
    default: buf_adds(o, "<wp:wrapNone/>");
    }
  }
  buf_printf(o, "<wp:docPr id=\"%lu\" name=\"%s %lu\"", (unsigned long)id, im->kind == OBJ_PICTURE ? "Picture" : im->kind == OBJ_TEXTBOX ? "Text Box" : im->kind == OBJ_GROUP ? "Group" : "Shape",
             (unsigned long)id);
  if (im->alt[0]) { buf_adds(o, " descr=\""); xml_escape(o, im->alt, strlen(im->alt)); buf_adds(o, "\""); }
  buf_adds(o, im->decorative ? "><a:extLst xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\"><a:ext uri=\"{C183D7F6-B498-43B3-948B-1728B52AA6E4}\">"   /* (WP24: decorative) */
              "<adec:decorative xmlns:adec=\"http://schemas.microsoft.com/office/drawing/2017/decorative\" val=\"1\"/></a:ext></a:extLst></wp:docPr>" : "/>");
  buf_adds(o, "<wp:cNvGraphicFramePr/><a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">");
  buf_printf(o, "<a:graphicData uri=\"%s\">", im->kind == OBJ_PICTURE ? "http://schemas.openxmlformats.org/drawingml/2006/picture" :
             im->kind == OBJ_GROUP ? "http://schemas.microsoft.com/office/word/2010/wordprocessingGroup" : "http://schemas.microsoft.com/office/word/2010/wordprocessingShape");
  w_obj(o, d, im, 0, 0, 0, &save_drawing);
  buf_printf(o, "</a:graphicData></a:graphic></wp:%s></w:drawing></w:r>", fl ? "anchor" : "inline");
}
/* runs of text in one format (rpr: its w:rPr, rl bytes), split wherever a 4-byte reference
   appears: pictures become drawings, notes footnote or endnote references, comment marks their
   range elements, and the page number and page count placeholders fldSimple fields (with "1" as
   their result, which Word updates). References to things not being saved are dropped. */
static u16 w_runlink;                 /* the link of the run being written (a PAGEREF field's bookmark) */
int (*docx_pageref)(void *ctx, const char *bookmark);   /* see docx.h */
void *docx_pageref_ctx;
static void w_run(Buf *o, Doc *d, const char *rpr, size_t rl, const char *s, u32 n)
{
  u32 i, a = 0;
  for (i = 0; i <= n; i++) {
    int field = i + 2 < n + 0 && IS_FIELD(s + i);
    int pic = i + 4 <= n && IS_PIC(s + i), note = i + 4 <= n && IS_NOTE(s + i), mark = i + 4 <= n && IS_MARK(s + i);
    if (i + 4 <= n && IS_MATH(s + i)) {          /* WP14a: an equation, as OMML (between the runs) */
      const MathEq *e = doc_math(d, math_index(s + i));
      if (i > a) { buf_adds(o, "<w:r>"); buf_add(o, rpr, rl); w_text(o, s + a, i - a); buf_adds(o, "</w:r>"); }
      if (e) math_write_omml(o, e, rpr, rl);
      i += 3; a = i + 1;
      continue;
    }
    if (i < n && !field && !pic && !note && !mark && !(i + 4 <= n && IS_ENOTE(s + i))) continue;
    if (i > a) { buf_adds(o, "<w:r>"); buf_add(o, rpr, rl); w_text(o, s + a, i - a); buf_adds(o, "</w:r>"); }
    if (pic) {
      int k = pic_index(s + i);
      if (k < d->nimages && obj_saved(&d->images[k])) w_picture(o, d, rpr, rl, k);
      i += 3; a = i + 1;
      continue;
    }
    if (mark) {
      /* a comment's range: its start, or its end and the reference Word shows the comment at */
      int k = note_index(s + i);
      if (k < d->ncomments && save_comments && save_comments[k]) {
        if (IS_CSTART(s + i)) buf_printf(o, "<w:commentRangeStart w:id=\"%d\"/>", save_comments[k] - 1);
        else buf_printf(o, "<w:commentRangeEnd w:id=\"%d\"/><w:r><w:commentReference w:id=\"%d\"/></w:r>", save_comments[k] - 1, save_comments[k] - 1);
      }
      i += 3; a = i + 1;
      continue;
    }
    if (i + 4 <= n && IS_ENOTE(s + i)) {
      int k = note_index(s + i);
      if (k < d->nenotes && save_enotes && save_enotes[k])
        buf_printf(o, "<w:r><w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr><w:endnoteReference w:id=\"%d\"/></w:r>", save_enotes[k]);
      i += 3; a = i + 1;
      continue;
    }
    if (note) {
      int k = note_index(s + i);
      if (k < d->nnotes && save_notes && save_notes[k])
        buf_printf(o, "<w:r><w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr><w:footnoteReference w:id=\"%d\"/></w:r>", save_notes[k]);
      i += 3; a = i + 1;
      continue;
    }
    if (field && (u8)s[i + 2] == 0x82) {
      /* the page a bookmark is on: PAGEREF, with the page it is on now if the caller can say */
      const char *u = doc_link_url(d, w_runlink), *bm = u[0] == '#' ? u + 1 : u;
      int pg = docx_pageref && bm[0] ? docx_pageref(docx_pageref_ctx, bm) : 0;
      buf_adds(o, "<w:fldSimple w:instr=\" PAGEREF ");
      xml_escape(o, bm, strlen(bm));
      buf_printf(o, " \\h \"><w:r>");
      buf_add(o, rpr, rl);
      buf_printf(o, "<w:t>%d</w:t></w:r></w:fldSimple>", pg > 0 ? pg : 1);
      i += 2;
    } else if (field) {
      buf_adds(o, (u8)s[i + 2] == 0x80 ? "<w:fldSimple w:instr=\" PAGE \"><w:r>" : "<w:fldSimple w:instr=\" NUMPAGES \"><w:r>");
      buf_add(o, rpr, rl);
      buf_adds(o, "<w:t>1</w:t></w:r></w:fldSimple>");
      i += 2;
    }
    a = i + 1;
  }
}
static void w_para_ex(Buf *o, Doc *d, Para *p, const char *lead, const char *tail);
static void w_sectpr(Buf *o, Doc *d, const SectFmt *s, int g, int last);
static int w_inbody;                  /* writing the body's paragraphs */
static u16 w_bmark;                   /* the bookmark open in the body (its runs are being written), 0 none */
/* the body's runs move from bookmark w_bmark to k: the one open ended, the new one started (Word
   numbers them by w:id; Quill uses the bookmark's number) */
static void w_bmark_to(Buf *o, Doc *d, u16 k)
{
  if (k == w_bmark) return;
  if (w_bmark) buf_printf(o, "<w:bookmarkEnd w:id=\"%u\"/>", (unsigned)w_bmark);
  if (k && k <= d->nbmarks) {
    buf_printf(o, "<w:bookmarkStart w:id=\"%u\" w:name=\"", (unsigned)k);
    xml_escape(o, d->bmarks[k - 1], strlen(d->bmarks[k - 1]));
    buf_adds(o, "\"/>");
  }
  w_bmark = k;
}
/* append paragraph p as a w:p: its pPr against its style (with a w:sectPr when a section ends here,
   in the body), then its runs, each with an rPr against the style and any character style, and
   wrapped in w:ins or w:del when it is a tracked change. lead: runs that open the paragraph (a
   footnote's number, the start of a TOC field); tail: runs that close it. In the body a drop cap is
   written Word's way, as a framed paragraph holding the letter (at the dropped size, about 1.67
   times the text's per extra line) followed by the rest. */
/* a hyperlink round runs opened (open) or closed. In the main document it is Word's w:hyperlink,
   whose address is a relationship (rIdLk + its number, written with the others) or a w:anchor for a
   place in the document; elsewhere (headers, notes, comments, whose parts have no relationships of
   their own here) it is the older fldSimple HYPERLINK field, which Word reads just the same */
static void w_link(Buf *o, Doc *d, u16 link, int open)
{
  const char *u = doc_link_url(d, link);
  if (!link || !u[0]) return;
  if (!open) { buf_adds(o, w_inbody ? "</w:hyperlink>" : "</w:fldSimple>"); return; }
  if (w_inbody) {
    if (u[0] == '#') { buf_adds(o, "<w:hyperlink w:anchor=\""); xml_escape(o, u + 1, strlen(u + 1)); buf_adds(o, "\" w:history=\"1\">"); }
    else buf_printf(o, "<w:hyperlink r:id=\"rIdLk%u\" w:history=\"1\">", (unsigned)link);
    return;
  }
  {
    const char *hash = strchr(u, '#');
    size_t n = hash ? (size_t)(hash - u) : strlen(u);
    buf_adds(o, "<w:fldSimple w:instr=\" HYPERLINK ");
    if (n) { buf_adds(o, "&quot;"); xml_escape(o, u, n); buf_adds(o, "&quot; "); }
    if (hash) { buf_adds(o, "\\l &quot;"); xml_escape(o, hash + 1, strlen(hash + 1)); buf_adds(o, "&quot; "); }
    buf_adds(o, "\">");
  }
}
static void w_para_ex(Buf *o, Doc *d, Para *p, const char *lead, const char *tail)
{
  const Style *st = &d->styles[p->pf.style < d->nstyles ? p->pf.style : 0];
  Buf pp;
  u32 k, start = 0;
  u16 link = 0;                         /* the link the runs written are in (w_link opens and closes it) */
  if (w_inbody && p->pf.dropcap && p->len && !in_table(&p->pf)) {
    /* a drop cap: Word's framed paragraph with the letter, then the rest */
    u32 n = (u32)utf8_next(p->t, p->len, 0), r;
    CharFmt cf = p->r[0].f;
    ParaFmt pf = p->pf;
    Para *rest;
    Buf rp;
    long lines = p->pf.dropcap, lh = lines * cf.size * 117 / 10;
    cf.size = (u16)(cf.size * (1000 + (lines - 1) * 1671) / 1000);
    buf_init(&rp);
    w_rpr(&rp, d, &cf, &st->cf, 0);
    buf_adds(o, "<w:p><w:pPr>");
    if (p->pf.style) { buf_adds(o, "<w:pStyle w:val=\""); xml_escape(o, d->styles[p->pf.style].id, strlen(d->styles[p->pf.style].id)); buf_adds(o, "\"/>"); }
    buf_printf(o, "<w:keepNext/><w:framePr w:dropCap=\"%s\" w:lines=\"%ld\" w:wrap=\"around\" w:vAnchor=\"text\" w:hAnchor=\"text\"/>"
                  "<w:spacing w:before=\"0\" w:after=\"0\" w:line=\"%ld\" w:lineRule=\"exact\"/><w:textAlignment w:val=\"baseline\"/></w:pPr>",
               p->pf.dropmargin ? "margin" : "drop", lines, lh);
    w_run(o, d, rp.data ? rp.data : "", rp.len, p->t, n);
    buf_adds(o, "</w:p>");
    buf_free(&rp);
    pf.dropcap = pf.dropmargin = 0;
    if (!(rest = para_new(&pf, &p->r[0].f))) return;
    for (r = 0, start = 0; r < p->nr; r++) {
      u32 a = start > n ? start : n;
      if (p->r[r].end > a) para_append(rest, p->t + a, p->r[r].end - a, &p->r[r].f);
      start = p->r[r].end;
    }
    w_para_ex(o, d, rest, lead, tail);
    para_free(rest);
    return;
  }
  buf_init(&pp);
  w_ppr_body(&pp, d, &p->pf, &st->pf, 0);
  if (!p->len) w_rpr(&pp, d, &p->r[0].f, &st->cf, 0);       /* an empty paragraph's mark keeps its formatting */
  if (w_inbody && p->pf.sect && p->pf.sect <= d->nsects && p != d->p[d->np - 1] && !in_table(&p->pf)) {
    w_sectpr(&pp, d, &d->sects[p->pf.sect - 1], p->pf.sect, 0);    /* a section ends here */
  }
  buf_adds(o, "<w:p>");
  if (pp.len) { buf_adds(o, "<w:pPr>"); buf_add(o, pp.data, pp.len); buf_adds(o, "</w:pPr>"); }
  buf_free(&pp);
  if (lead) buf_adds(o, lead);
  if (w_inbody && p->pf.colbreak) buf_adds(o, "<w:r><w:br w:type=\"column\"/></w:r>");
  for (k = 0; k < p->nr; k++) {
    if (p->r[k].end > start) {
      Buf r;
      const CharFmt *f = &p->r[k].f;
      if (f->link != link) { w_link(o, d, link, 0); w_link(o, d, f->link, 1); link = f->link; }
      if (w_inbody) w_bmark_to(o, d, f->bmark);
      w_runlink = f->link;
      buf_init(&r);
      {
        /* a character style: named, and what it sets taken as given */
        CharFmt rb = st->cf;
        if (f->cstyle && f->cstyle < d->nstyles && d->styles[f->cstyle].type == ST_CHAR) {
          cf_apply(&rb, d->styles[f->cstyle].ecmask, &d->styles[f->cstyle].cf);
          w_rstyle = d->styles[f->cstyle].id;
        }
        w_rpr(&r, d, f, &rb, 0);
      }
      if (f->rev) {
        /* a tracked change round the run */
        static int revid = 1000;
        const char *au = f->rid < d->nrevs ? d->revs[f->rid].author : "", *dt = f->rid < d->nrevs ? d->revs[f->rid].date : "";
        buf_printf(o, f->rev == REV_INS ? "<w:ins w:id=\"%d\" w:author=\"" : "<w:del w:id=\"%d\" w:author=\"", revid++);
        xml_escape(o, au, strlen(au));
        buf_adds(o, "\"");
        if (*dt) { buf_adds(o, " w:date=\""); xml_escape(o, dt, strlen(dt)); buf_adds(o, "\""); }
        buf_adds(o, ">");
        w_deleted = f->rev == REV_DEL;
      }
      w_run(o, d, r.data ? r.data : "", r.len, p->t + start, p->r[k].end - start);
      if (f->rev) { buf_adds(o, f->rev == REV_INS ? "</w:ins>" : "</w:del>"); w_deleted = 0; }
      buf_free(&r);
    }
    start = p->r[k].end;
  }
  w_link(o, d, link, 0);
  if (tail) buf_adds(o, tail);
  buf_adds(o, "</w:p>");
}
/* Word's text watermark: a VML text path shape behind the text, centred on the page */
/* (a run, to go in a header's paragraph.) The shape is sized from the text body's width, in
   points, and the shape type is Word's own WordArt one (136), copied as Word writes it. */
static void w_watermark(Buf *o, Doc *d)
{
  static int shapes = 1025;
  i32 body = d->page.width - d->page.left - d->page.right;
  long n = (long)strlen(d->wm.text), wpt, hpt;
  char col[8];
  const char *font = d->wm.font[0] ? d->wm.font : "Calibri";
  if (n < 1) n = 1;
  wpt = (long)(d->wm.horizontal ? body : body * 13 / 10) / 20;
  if (wpt > 470) wpt = 470;
  hpt = wpt * 20 / (11 * n) ;                 /* letters about as tall as wide */
  if (hpt > wpt / 2) hpt = wpt / 2;
  sprintf(col, "%06lX", (unsigned long)(d->wm.color & 0xFFFFFF));
  buf_adds(o, "<w:r><w:rPr><w:noProof/></w:rPr><w:pict>"
    "<v:shapetype id=\"_x0000_t136\" coordsize=\"21600,21600\" o:spt=\"136\" adj=\"10800\" path=\"m@7,l@8,m@5,21600l@6,21600e\"><v:formulas>"
    "<v:f eqn=\"sum #0 0 10800\"/><v:f eqn=\"prod #0 2 1\"/><v:f eqn=\"sum 21600 0 @1\"/><v:f eqn=\"sum 0 0 @2\"/><v:f eqn=\"sum 21600 0 @3\"/>"
    "<v:f eqn=\"if @0 @3 0\"/><v:f eqn=\"if @0 21600 @1\"/><v:f eqn=\"if @0 0 @2\"/><v:f eqn=\"if @0 @4 21600\"/><v:f eqn=\"mid @5 @6\"/>"
    "<v:f eqn=\"mid @8 @5\"/><v:f eqn=\"mid @7 @8\"/><v:f eqn=\"mid @6 @7\"/><v:f eqn=\"sum @6 0 @5\"/></v:formulas>"
    "<v:path textpathok=\"t\" o:connecttype=\"custom\" o:connectlocs=\"@9,0;@10,10800;@11,21600;@12,10800\" o:connectangles=\"270,180,90,0\"/>"
    "<v:textpath on=\"t\" fitshape=\"t\"/><v:handles><v:h position=\"#0,bottomRight\" xrange=\"6629,14971\"/></v:handles><o:lock v:ext=\"edit\" text=\"t\" shapetype=\"t\"/></v:shapetype>");
  buf_printf(o, "<v:shape id=\"PowerPlusWaterMarkObject%d\" o:spid=\"_x0000_s%d\" type=\"#_x0000_t136\" style=\"position:absolute;margin-left:0;margin-top:0;"
                "width:%ldpt;height:%ldpt;%sz-index:-251654144;mso-position-horizontal:center;mso-position-horizontal-relative:margin;"
                "mso-position-vertical:center;mso-position-vertical-relative:margin\" o:allowincell=\"f\" fillcolor=\"#%s\" stroked=\"f\">"
                "<v:fill opacity=\".5\"/><v:textpath style=\"font-family:&quot;", shapes, shapes, wpt, hpt, d->wm.horizontal ? "" : "rotation:315;", col);
  shapes++;
  xml_escape(o, font, strlen(font));
  buf_adds(o, "&quot;;font-size:1pt\" string=\"");
  xml_escape(o, d->wm.text, strlen(d->wm.text));
  buf_adds(o, "\"/><w10:wrap anchorx=\"margin\" anchory=\"margin\"/></v:shape></w:pict></w:r>");
}
/* a header or footer part (wm: with the watermark) */
/* (returns the XML malloc'd, *len its size; NULL if out of memory) */
static char *w_story(Doc *d, const Story *st, int footer, int wm, size_t *len)
{
  Buf o;
  u32 i;
  buf_init(&o);
  buf_adds(&o, XMLHDR);
  buf_adds(&o, footer ? "<w:ftr " : "<w:hdr "); buf_adds(&o, WNS);
  buf_adds(&o, " xmlns:v=\"urn:schemas-microsoft-com:vml\" xmlns:o=\"urn:schemas-microsoft-com:office:office\" xmlns:w10=\"urn:schemas-microsoft-com:office:word\">");
  {
    /* the watermark goes in as a run of the first paragraph (or of one of its own) */
    Buf w;
    buf_init(&w);
    if (wm) w_watermark(&w, d);
    for (i = 0; i < st->np; i++) w_para_ex(&o, d, st->p[i], i == 0 && w.len ? w.data : NULL, NULL);
    if (!st->np && w.len) { buf_adds(&o, "<w:p><w:pPr><w:pStyle w:val=\"Header\"/></w:pPr>"); buf_add(&o, w.data, w.len); buf_adds(&o, "</w:p>"); }
    else if (!st->np) buf_adds(&o, "<w:p/>");                 /* an empty one (a section's, where the one before has text) */
    if (w.oom) o.oom = 1;
    buf_free(&w);
  }
  buf_adds(&o, footer ? "</w:ftr>" : "</w:hdr>");
  if (o.oom) { buf_free(&o); return NULL; }
  *len = o.len;
  return buf_take(&o);
}
static const char *const hf_type[HF_N] = { "default", "first", "even" };
/* (WP22b) the id the continuation notice gets in footnotes.xml (end: endnotes.xml): the one after the
   notes', 0 if the document has no notice of its own */
static int notice_id(Doc *d, int end)
{
  int k, top = 0, n = end ? d->nenotes : d->nnotes, *ids = end ? save_enotes : save_notes;
  if (!ids || !notes_sep_own(d, end, NSEP_NOTICE)) return 0;
  for (k = 0; k < n; k++) if (ids[k] > top) top = ids[k];
  return top + 1;
}
/* (WP22b) the separator notes at the start of footnotes.xml (end: endnotes.xml): the document's own
   paragraphs with the line they hold, else Word's */
static void w_note_seps(Buf *o, Doc *d, int end)
{
  static const char *const types[NSEP_N] = { "separator", "continuationSeparator", "continuationNotice" };
  const char *el = end ? "endnote" : "footnote";
  int w;
  for (w = 0; w < NSEP_N; w++) {
    int flags = notes_sep(d, end, w, NULL), id = w == NSEP_SEP ? -1 : w == NSEP_CONT ? 0 : notice_id(d, end);
    const Story *st = d->nseps && d->nseps->have[end][w] ? &d->nseps->text[end][w] : NULL;
    const char *pre = flags & NSF_LINE ? "<w:r><w:separator/></w:r>" : flags & NSF_LONG ? "<w:r><w:continuationSeparator/></w:r>" : NULL;
    u32 i;
    if (w == NSEP_NOTICE && !id) continue;
    buf_printf(o, "<w:%s w:type=\"%s\" w:id=\"%d\">", el, types[w], id);
    if (st && st->np) for (i = 0; i < st->np; i++) w_para_ex(o, d, st->p[i], i ? NULL : pre, NULL);
    else {
      buf_adds(o, "<w:p><w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr>");
      if (pre) buf_adds(o, pre);
      buf_adds(o, "</w:p>");
    }
    buf_printf(o, "</w:%s>", el);
  }
}
/* (WP22b) settings.xml's w:footnotePr and w:endnotePr: the document's options and the special notes' ids */
static void w_notes_settings(Buf *st, Doc *d)
{
  int e;
  for (e = 0; e < 2; e++) {
    char inner[200];
    int nid = notice_id(d, e);
    const char *el = e ? "endnote" : "footnote";
    snprintf(inner, sizeof inner, "<w:%s w:id=\"-1\"/><w:%s w:id=\"0\"/>", el, el);
    if (nid) snprintf(inner + strlen(inner), sizeof inner - strlen(inner), "<w:%s w:id=\"%d\"/>", el, nid);
    notes_write_pr(st, e ? &d->enpr : &d->fnpr, e, e ? "endnotePr" : "footnotePr", (e ? save_enotes : save_notes) ? inner : NULL);
  }
}
/* footnotes.xml: Word's two separator notes (ids -1 and 0), then each footnote that is referred to,
   under the id save_notes gave it, its first paragraph opened by the number (footnoteRef) and a
   space. Malloc'd, *len its size; NULL if out of memory. */
static char *w_footnotes(Doc *d, size_t *len)
{
  Buf o;
  int k;
  u32 i;
  buf_init(&o);
  buf_adds(&o, XMLHDR);
  buf_adds(&o, "<w:footnotes "); buf_adds(&o, WNS); buf_adds(&o, ">");
  w_note_seps(&o, d, 0);                 /* (WP22b: the separators, the document's own or Word's) */
  for (k = 0; k < d->nnotes; k++) {
    if (!save_notes[k]) continue;
    buf_printf(&o, "<w:footnote w:id=\"%d\">", save_notes[k]);
    for (i = 0; i < d->notes[k].np; i++)
      w_para_ex(&o, d, d->notes[k].p[i], i ? NULL : "<w:r><w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr><w:footnoteRef/></w:r><w:r><w:t xml:space=\"preserve\"> </w:t></w:r>", NULL);
    if (!d->notes[k].np) buf_adds(&o, "<w:p><w:r><w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr><w:footnoteRef/></w:r></w:p>");
    buf_adds(&o, "</w:footnote>");
  }
  buf_adds(&o, "</w:footnotes>");
  if (o.oom) { buf_free(&o); return NULL; }
  *len = o.len;
  return buf_take(&o);
}
/* endnotes.xml, the same way with save_enotes */
static char *w_endnotes(Doc *d, size_t *len)
{
  Buf o;
  int k;
  u32 i;
  buf_init(&o);
  buf_adds(&o, XMLHDR);
  buf_adds(&o, "<w:endnotes "); buf_adds(&o, WNS); buf_adds(&o, ">");
  w_note_seps(&o, d, 1);                 /* (WP22b) */
  for (k = 0; k < d->nenotes; k++) {
    if (!save_enotes[k]) continue;
    buf_printf(&o, "<w:endnote w:id=\"%d\">", save_enotes[k]);
    for (i = 0; i < d->enotes[k].np; i++)
      w_para_ex(&o, d, d->enotes[k].p[i], i ? NULL : "<w:r><w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr><w:endnoteRef/></w:r><w:r><w:t xml:space=\"preserve\"> </w:t></w:r>", NULL);
    if (!d->enotes[k].np) buf_adds(&o, "<w:p><w:r><w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr><w:endnoteRef/></w:r></w:p>");
    buf_adds(&o, "</w:endnote>");
  }
  buf_adds(&o, "</w:endnotes>");
  if (o.oom) { buf_free(&o); return NULL; }
  *len = o.len;
  return buf_take(&o);
}
/* comments.xml: each comment whose marks are still in the text, under the id save_comments gave it
   (less one), with its author, date and initials; malloc'd, NULL if out of memory */
static char *w_comments(Doc *d, size_t *len)
{
  Buf o;
  int k;
  u32 i;
  buf_init(&o);
  buf_adds(&o, XMLHDR);
  buf_adds(&o, "<w:comments "); buf_adds(&o, WNS); buf_adds(&o, ">");
  for (k = 0; k < d->ncomments; k++) {
    Comment *c = &d->comments[k];
    if (!save_comments[k]) continue;
    buf_printf(&o, "<w:comment w:id=\"%d\" w:author=\"", save_comments[k] - 1);
    xml_escape(&o, c->author, strlen(c->author));
    buf_adds(&o, "\"");
    if (c->date[0]) { buf_adds(&o, " w:date=\""); xml_escape(&o, c->date, strlen(c->date)); buf_adds(&o, "\""); }
    if (c->initials[0]) { buf_adds(&o, " w:initials=\""); xml_escape(&o, c->initials, strlen(c->initials)); buf_adds(&o, "\""); }
    buf_adds(&o, ">");
    for (i = 0; i < c->text.np; i++) w_para_ex(&o, d, c->text.p[i], i ? NULL : "<w:r><w:annotationRef/></w:r>", NULL);
    if (!c->text.np) buf_adds(&o, "<w:p><w:r><w:annotationRef/></w:r></w:p>");
    buf_adds(&o, "</w:comment>");
  }
  buf_adds(&o, "</w:comments>");
  if (o.oom) { buf_free(&o); return NULL; }
  *len = o.len;
  return buf_take(&o);
}
/* headers and footers are written in sets: 0 the document's, n section format n's own (Doc.sects[n - 1].hf,
   when a section uses them): part k (HF_N headers, then HF_N footers) of set g is header/footer
   g * HF_N + k % HF_N + 1, relationship rIdH or rIdF g * HF_N + k % HF_N */
static u8 *save_sused;
static u8 save_slot[2 * HF_N];          /* a kind some set has: every set has a part of it (else the kind would
                                           be the section before's, Word's "link to previous") */
/* part k of set g, and whether set g is written at all (set 0 always is; a section's only when
   some section uses its own) */
static const Story *set_story(Doc *d, int g, int k) { return g ? &d->sects[g - 1].hf[k] : k < HF_N ? &d->hdr[k] : &d->ftr[k - HF_N]; }
static int set_used(Doc *d, int g) { return !g || (g <= d->nsects && save_sused && save_sused[g]); }
/* part k of set g is written: it has text, or (a header) it carries the watermark */
static int part_written(Doc *d, int g, int k)
{
  int tp;
  if (!set_used(d, g)) return 0;
  tp = g ? d->sects[g - 1].titlepg : d->titlepg;
  if (set_story(d, g, k)->np || save_slot[k]) return 1;
  return k < HF_N && d->wm.text[0] && (k == HF_DEFAULT || (k == HF_FIRST && tp) || (k == HF_EVEN && d->evenodd));
}
/* a section's w:sectPr: g its headers' and footers' set; last: the last section (whose page setup and
   first page are the document's) */
static void w_sectpr(Buf *o, Doc *d, const SectFmt *s, int g, int last)
{
  const PageSetup *pg = sect_page(d, s, last);
  static const char *const types[4] = { "nextPage", "continuous", "evenPage", "oddPage" };
  static const char *const restart[4] = { "newPage", "newPage", "newSection", "continuous" };
  static const char *const pbs[6] = { "none", "single", "double", "thick", "dotted", "dashed" };
  static const char *const va[3] = { "top", "center", "bottom" };
  int k;
  buf_adds(o, "<w:sectPr>");
  if (!set_used(d, g)) g = 0;
  for (k = 0; k < HF_N; k++) if (part_written(d, g, k)) buf_printf(o, "<w:headerReference w:type=\"%s\" r:id=\"rIdH%lu\"/>", hf_type[k], (unsigned long)(g * HF_N + k));
  for (k = 0; k < HF_N; k++) if (part_written(d, g, HF_N + k)) buf_printf(o, "<w:footerReference w:type=\"%s\" r:id=\"rIdF%lu\"/>", hf_type[k], (unsigned long)(g * HF_N + k));
  notes_write_pr(o, &s->fnpr, 0, "footnotePr", NULL); notes_write_pr(o, &s->enpr, 1, "endnotePr", NULL);   /* (WP22b) */
  if (s->start != SB_NEXT) buf_printf(o, "<w:type w:val=\"%s\"/>", types[s->start & 3]);
  buf_printf(o, "<w:pgSz w:w=\"%ld\" w:h=\"%ld\"%s/>", (long)pg->width, (long)pg->height,
             pg->width > pg->height ? " w:orient=\"landscape\"" : "");
  buf_printf(o, "<w:pgMar w:top=\"%ld\" w:right=\"%ld\" w:bottom=\"%ld\" w:left=\"%ld\" w:header=\"%ld\" w:footer=\"%ld\" w:gutter=\"0\"/>",
             (long)pg->top, (long)pg->right, (long)pg->bottom, (long)pg->left, (long)pg->header, (long)pg->footer);
  if (d->border.style != PB_NONE) {
    static const char *const sides[4] = { "top", "left", "bottom", "right" };
    char col[8];
    long sp = d->border.space / 20;
    if (d->border.color == COLOR_AUTO) strcpy(col, "auto"); else sprintf(col, "%06lX", (unsigned long)(d->border.color & 0xFFFFFF));
    buf_printf(o, "<w:pgBorders w:offsetFrom=\"%s\">", d->border.frompage ? "page" : "text");
    for (k = 0; k < 4; k++)
      buf_printf(o, "<w:%s w:val=\"%s\" w:sz=\"%u\" w:space=\"%ld\" w:color=\"%s\"/>", sides[k], pbs[d->border.style % 6], (unsigned)d->border.width, sp > 31 ? 31L : sp, col);
    buf_adds(o, "</w:pgBorders>");
  }
  if (s->linenum != LN_NONE) {
    buf_printf(o, "<w:lnNumType w:countBy=\"%u\" w:distance=\"%ld\" w:restart=\"%s\"", (unsigned)s->lnby, (long)s->lndist, restart[s->linenum & 3]);
    if (s->lnstart > 1) buf_printf(o, " w:start=\"%u\"", (unsigned)(s->lnstart - 1));
    buf_adds(o, "/>");
  }
  if (d->pgfmt > NF_DECIMAL || d->pgstart > 1) {
    static const char *const fmts[7] = { "decimal", "decimal", "lowerLetter", "upperLetter", "lowerRoman", "upperRoman", "decimal" };
    buf_printf(o, "<w:pgNumType w:fmt=\"%s\"", fmts[d->pgfmt < 7 ? d->pgfmt : 0]);
    if (d->pgstart > 1) buf_printf(o, " w:start=\"%u\"", (unsigned)d->pgstart);
    buf_adds(o, "/>");
  }
  buf_printf(o, "<w:cols w:num=\"%u\" w:space=\"%ld\"%s/>", (unsigned)s->cols, (long)s->colgap, s->colline && s->cols > 1 ? " w:sep=\"1\"" : "");
  if (s->valign != VA_TOP) buf_printf(o, "<w:vAlign w:val=\"%s\"/>", va[s->valign % 3]);
  if (sect_titlepg(d, s, last)) buf_adds(o, "<w:titlePg/>");
  buf_adds(o, "</w:sectPr>");
}
/* document.xml: the body's paragraphs with the tables round them, a table of contents wrapped in a
   TOC field, and the last section's w:sectPr. Malloc'd, *len its size; NULL if out of memory. */
static char *w_document(Doc *d, size_t *len)
{
  Buf o;
  u32 i;
  buf_init(&o);
  buf_adds(&o, XMLHDR);
  buf_adds(&o, "<w:document "); buf_adds(&o, WNS); buf_adds(&o, ">");
  if (d->pagecolor) buf_printf(&o, "<w:background w:color=\"%06lX\"/>", (unsigned long)(d->pagecolor & 0xFFFFFF));   /* (the page colour) */
  buf_adds(&o, "<w:body>");
  w_inbody = 1; w_bmark = 0;
  for (i = 0; i < d->np; i++) {
    /* a table of contents: Word's TOC field round its entries, so Word can update it */
    int toc = toc_para(d, d->p[i]), first = toc && !(i > 0 && toc_para(d, d->p[i - 1])), last = toc && !(i + 1 < d->np && toc_para(d, d->p[i + 1]));
    w_table_open(&o, d, i);
    w_para_ex(&o, d, d->p[i], first ? "<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText xml:space=\"preserve\"> TOC \\o \"1-3\" \\h \\z \\u </w:instrText></w:r><w:r><w:fldChar w:fldCharType=\"separate\"/></w:r>" : NULL,
              last ? "<w:r><w:fldChar w:fldCharType=\"end\"/></w:r>" : NULL);
    w_table_close(&o, d, i);
  }
  w_bmark_to(&o, d, 0);
  w_inbody = 0;
  w_sectpr(&o, d, doc_sect(d, d->np - 1), 0, 1);   /* the last section: the body's */
  buf_adds(&o, "</w:body></w:document>");
  if (o.oom) { buf_free(&o); return NULL; }
  *len = o.len;
  return buf_take(&o);
}
/* styles.xml: the document defaults written in full, then each paragraph and character style as
   what it changes from the style it is based on (style 0 is the default paragraph style), then
   the table styles. Malloc'd, *len its size; NULL if out of memory. */
static char *w_styles(Doc *d, size_t *len)
{
  Buf o, pp;
  int i;
  ParaFmt zp; CharFmt zc;
  memset(&zp, 0, sizeof zp); memset(&zc, 0, sizeof zc);
  buf_init(&o);
  buf_adds(&o, XMLHDR);
  buf_adds(&o, "<w:styles "); buf_adds(&o, WNS); buf_adds(&o, ">");
  buf_adds(&o, "<w:docDefaults><w:rPrDefault>");
  w_rpr(&o, d, &d->defcf, &zc, 1);
  buf_adds(&o, "</w:rPrDefault><w:pPrDefault>");
  buf_init(&pp);
  {
    ParaFmt df = d->defpf;
    df.style = 0;
    w_ppr_body(&pp, d, &df, &zp, 1);
  }
  if (pp.len) { buf_adds(&o, "<w:pPr>"); buf_add(&o, pp.data, pp.len); buf_adds(&o, "</w:pPr>"); }
  buf_free(&pp);
  buf_adds(&o, "</w:pPrDefault></w:docDefaults>");
  /* each style as based on another, with what it sets itself (as far as it differs from its base) */
  for (i = 0; i < d->nstyles; i++) {
    Style *s = &d->styles[i];
    const ParaFmt *bp = &d->defpf;
    const CharFmt *bc = &d->defcf;
    int ch = s->type == ST_CHAR;
    if (s->type != ST_PARA && !ch) continue;
    if (s->based < d->nstyles && d->styles[s->based].type == s->type && s->based != i) { bp = &d->styles[s->based].pf; bc = &d->styles[s->based].cf; }
    else if (ch) bc = &d->defcf;
    buf_adds(&o, ch ? "<w:style w:type=\"character\"" : "<w:style w:type=\"paragraph\"");
    if (i == 0) buf_adds(&o, " w:default=\"1\"");
    buf_adds(&o, " w:styleId=\""); xml_escape(&o, s->id, strlen(s->id));
    buf_adds(&o, "\"><w:name w:val=\""); xml_escape(&o, s->name, strlen(s->name)); buf_adds(&o, "\"/>");
    if (s->based < d->nstyles && s->based != i && d->styles[s->based].type == s->type) {
      buf_adds(&o, "<w:basedOn w:val=\""); xml_escape(&o, d->styles[s->based].id, strlen(d->styles[s->based].id)); buf_adds(&o, "\"/>");
    }
    if (!ch && s->next < d->nstyles && s->next != i && d->styles[s->next].type == ST_PARA) { buf_adds(&o, "<w:next w:val=\""); xml_escape(&o, d->styles[s->next].id, strlen(d->styles[s->next].id)); buf_adds(&o, "\"/>"); }
    if (s->quick) buf_adds(&o, "<w:qFormat/>");
    if (!ch) {
      ParaFmt sp = s->pf, b0 = *bp;
      sp.style = 0; b0.style = 0;
      buf_init(&pp);
      w_ppr_body(&pp, d, &sp, &b0, 0);
      if (pp.len) { buf_adds(&o, "<w:pPr>"); buf_add(&o, pp.data, pp.len); buf_adds(&o, "</w:pPr>"); }
      buf_free(&pp);
    }
    w_rpr(&o, d, &s->cf, bc, 0);
    buf_adds(&o, "</w:style>");
  }
  w_tstyles(&o, d);
  buf_adds(&o, "</w:styles>");
  if (o.oom) { buf_free(&o); return NULL; }
  *len = o.len;
  return buf_take(&o);
}
/* numbering.xml: one abstract numbering per list (numbered by position) and a w:num for each with
   the list's own id, which the paragraphs' numPr refer to. Malloc'd; NULL if out of memory. */
static char *w_numbering(Doc *d, size_t *len)
{
  static const char *const fmts[] = { "bullet", "decimal", "lowerLetter", "upperLetter", "lowerRoman", "upperRoman", "none" };
  Buf o;
  int i, k;
  buf_init(&o);
  buf_adds(&o, XMLHDR);
  buf_adds(&o, "<w:numbering "); buf_adds(&o, WNS); buf_adds(&o, ">");
  for (i = 0; i < d->nlists; i++) {
    buf_printf(&o, "<w:abstractNum w:abstractNumId=\"%d\"><w:multiLevelType w:val=\"hybridMultilevel\"/>", i);
    for (k = 0; k < 9; k++) {
      ListLevel *v = &d->lists[i].lvl[k];
      buf_printf(&o, "<w:lvl w:ilvl=\"%d\"><w:start w:val=\"%u\"/><w:numFmt w:val=\"%s\"/><w:lvlText w:val=\"", k, (unsigned)v->start, fmts[v->fmt <= NF_NONE ? v->fmt : 1]);
      xml_escape(&o, v->text, strlen(v->text));
      buf_printf(&o, "\"/><w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\"%ld\" w:hanging=\"%ld\"/></w:pPr>", (long)v->left, (long)v->hanging);
      if (v->font[0] && v->fmt != NF_BULLET) { buf_adds(&o, "<w:rPr><w:rFonts w:ascii=\""); xml_escape(&o, v->font, strlen(v->font)); buf_adds(&o, "\" w:hAnsi=\""); xml_escape(&o, v->font, strlen(v->font)); buf_adds(&o, "\"/></w:rPr>"); }
      buf_adds(&o, "</w:lvl>");
    }
    buf_adds(&o, "</w:abstractNum>");
  }
  for (i = 0; i < d->nlists; i++) buf_printf(&o, "<w:num w:numId=\"%u\"><w:abstractNumId w:val=\"%d\"/></w:num>", (unsigned)d->lists[i].id, i);
  buf_adds(&o, "</w:numbering>");
  if (o.oom) { buf_free(&o); return NULL; }
  *len = o.len;
  return buf_take(&o);
}
/* the pictures a story refers to, numbered for the media folder */
static void mark_media(Doc *d, Para **pp, u32 np)
{
  u32 i, k;
  for (i = 0; i < np; i++)
    for (k = 0; k + 4 <= pp[i]->len; k++)
      if (IS_PIC(pp[i]->t + k)) { int n = pic_index(pp[i]->t + k); if (n < d->nimages) media_add(&d->images[n]); k += 3; }
}
/* first works out what goes in the package (the pictures used, which sections have headers and
   footers of their own, which notes and comments are still referred to, numbered in text order),
   then writes the parts and zips them. That state lives in the save_ statics, so this is not
   re-entrant. Always writes Word 2013 compatibility mode. The result is malloc'd (caller frees). */
/* ---- the work packages' writing hooks (WP0): as the reading ones, each package's call goes on a
   line right after its own marker line */
/* the parts, content types and relationships the hooks add, written by docx_save */
struct DocxOut {
  Buf ct, pkgrels, docrels;             /* <Override .../> and <Relationship .../> entries */
  struct { char *name; u8 *data; size_t len; } *parts;
  int nparts, oom;
};
int docx_out_part(DocxOut *o, const char *name, const char *ctype, const char *owner, const char *reltype, const char *relid,
                  const void *data, size_t len)
{
  void *np;
  int i;
  for (i = 0; i < o->nparts; i++) if (!strcmp(o->parts[i].name, name)) return 1;     /* (there already) */
  if (!(np = realloc(o->parts, (size_t)(o->nparts + 1) * sizeof o->parts[0]))) { o->oom = 1; return 0; }
  o->parts = np;
  if (!(o->parts[o->nparts].name = str_dup(name)) || !(o->parts[o->nparts].data = (u8 *)malloc(len ? len : 1))) {
    free(o->parts[o->nparts].name); o->oom = 1; return 0;
  }
  if (len) memcpy(o->parts[o->nparts].data, data, len);
  o->parts[o->nparts].len = len;
  o->nparts++;
  if (ctype) {
    buf_adds(&o->ct, "<Override PartName=\"/"); xml_escape(&o->ct, name, strlen(name));
    buf_adds(&o->ct, "\" ContentType=\""); xml_escape(&o->ct, ctype, strlen(ctype)); buf_adds(&o->ct, "\"/>");
  }
  if (owner && reltype && relid) {
    Buf *r = owner[0] ? &o->docrels : &o->pkgrels;
    const char *tg = name;
    if (owner[0] && !strncmp(name, "word/", 5)) tg = name + 5;          /* (the document's targets are from word/) */
    buf_adds(r, "<Relationship Id=\""); xml_escape(r, relid, strlen(relid));
    buf_adds(r, "\" Type=\""); xml_escape(r, reltype, strlen(reltype));
    buf_adds(r, owner[0] && tg == name ? "\" Target=\"/" : "\" Target=\""); xml_escape(r, tg, strlen(tg)); buf_adds(r, "\"/>");
  }
  return 1;
}
/* (WP24) docProps/app.xml and (with custom properties) docProps/custom.xml */
static void write_info_parts(Doc *d, DocxOut *o)
{
  size_t n;
  char *x;
  if ((x = info_app_xml(d, &n))) {
    docx_out_part(o, "docProps/app.xml", "application/vnd.openxmlformats-officedocument.extended-properties+xml", "",
                  "http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties", "rIdApp", x, n);
    free(x);
  } else o->oom = 1;
  if ((x = info_custom_xml(d, &n))) {
    docx_out_part(o, "docProps/custom.xml", "application/vnd.openxmlformats-officedocument.custom-properties+xml", "",
                  "http://schemas.openxmlformats.org/officeDocument/2006/relationships/custom-properties", "rIdCustom", x, n);
    free(x);
  }
}
/* the kept parts (core/keep.h), then each package's own */
static void docx_write_parts_ext(Doc *d, DocxOut *o)
{
  int i;
  for (i = 0; i < d->nkept; i++) {
    const KeptPart *k = &d->kept[i];
    docx_out_part(o, k->name, k->ctype, k->owner, k->reltype, k->relid, k->data, k->len);
  }
  /* WP1: */
  /* WP2: */
  /* WP3: */
  /* WP4: */
  /* WP5: */
  /* WP7: */
  /* WP9: */
  /* WP10: */
  /* WP11: */
  /* WP13: */
  /* WP14a: */
  /* WP15a: */
  /* WP15b: */
  /* WP15c: */
  /* WP16: */
  /* WP17: */
  /* WP18: */
  /* WP19: */
  /* WP20: */
  /* WP21a: */
  /* WP21b: */
  /* WP22a: */
  /* WP22b: */
  /* WP23: */
  /* WP24: */
  write_info_parts(d, o);
  /* WP27: */
  /* WP31: */
}
/* word/settings.xml's elements of the packages, in the schema's order (CT_Settings): where 0 first
   (w:writeProtection, w:view, w:zoom ...), 1 after w:displayBackgroundShape and before
   w:defaultTabStop (w:trackRevisions, w:documentProtection ...), 2 after w:autoHyphenation (hyphenation
   zone and limits; w:evenAndOddHeaders comes after), 3 after w:compat (w:docVars, w:rsids, m:mathPr,
   w:attachedTemplate, w:mailMerge ...) */
static void docx_write_settings_ext(Doc *d, Buf *b, int where)
{
  (void)d; (void)b; (void)where;
  /* WP1: */
  /* WP2: */
  /* WP3: */
  /* WP4: */
  /* WP5: */
  /* WP7: */
  /* WP9: */
  /* WP10: */
  /* WP11: */
  /* WP13: */
  /* WP14a: */
  if (where == 3) math_write_mathpr(d, b);
  /* WP15a: */
  /* WP15b: */
  /* WP15c: */
  /* WP16: */
  /* WP17: */
  /* WP18: */
  /* WP19: */
  /* WP20: */
  /* WP21a: */
  /* WP21b: */
  /* WP22a: */
  /* WP22b: */
  /* WP23: */
  /* WP24: */
  /* WP27: */
  /* WP31: */
}

u8 *docx_save(Doc *d, size_t *len)
{
  ZipWriter z;
  char *docxml, *stxml, *numxml = NULL;
  size_t dl, sl, nl = 0;
  Buf ct, core;
  int ok;
  u8 *out;
  DocxOut ext;
  /* the pictures the text refers to, numbered for the media folder */
  free(save_ml); save_ml = NULL; save_nml = 0; save_drawing = 0;
  if (d->nimages) {
    int w, n;
    mark_media(d, d->p, d->np);
    for (w = 0; w < HF_N; w++) { mark_media(d, d->hdr[w].p, d->hdr[w].np); mark_media(d, d->ftr[w].p, d->ftr[w].np); }
    for (n = 0; n < d->nsects; n++)
      if (d->sects[n].hf) for (w = 0; w < 2 * HF_N; w++) mark_media(d, d->sects[n].hf[w].p, d->sects[n].hf[w].np);
    /* and pictures in text boxes, notes and comments */
    for (w = 0; w < d->nimages; w++) if (d->images[w].text) mark_media(d, d->images[w].text->p, d->images[w].text->np);
    for (w = 0; w < d->nnotes; w++) mark_media(d, d->notes[w].p, d->notes[w].np);
    for (w = 0; w < d->nenotes; w++) mark_media(d, d->enotes[w].p, d->enotes[w].np);
    for (w = 0; w < d->ncomments; w++) mark_media(d, d->comments[w].text.p, d->comments[w].text.np);
  }
  /* the sections with headers and footers of their own that are used (one ends somewhere but at the end) */
  free(save_sused); save_sused = NULL;
  if (d->nsects && (save_sused = (u8 *)calloc((size_t)d->nsects + 1, 1))) {
    u32 i;
    for (i = 0; i + 1 < d->np; i++) if (d->p[i]->pf.sect && d->p[i]->pf.sect <= d->nsects && d->sects[d->p[i]->pf.sect - 1].hf) save_sused[d->p[i]->pf.sect] = 1;
  }
  memset(save_slot, 0, sizeof save_slot);
  {
    int g, k;
    for (g = 1; g <= d->nsects; g++)
      if (set_used(d, g)) for (k = 0; k < 2 * HF_N; k++) if (set_story(d, g, k)->np) save_slot[k] = 1;
    if (save_slot[0] || save_slot[1] || save_slot[2] || save_slot[3] || save_slot[4] || save_slot[5])
      for (k = 0; k < 2 * HF_N; k++) if (set_story(d, 0, k)->np) save_slot[k] = 1;
  }
  free(save_notes); save_notes = NULL;
  if (d->nnotes && (save_notes = (int *)calloc((size_t)d->nnotes, sizeof(int)))) {
    u32 i, k;
    int nn = 0;
    for (i = 0; i < d->np; i++)
      for (k = 0; k + 4 <= d->p[i]->len; k++)
        if (IS_NOTE(d->p[i]->t + k)) { int m = note_index(d->p[i]->t + k); if (m < d->nnotes && !save_notes[m]) save_notes[m] = ++nn; k += 3; }
    if (!nn) { free(save_notes); save_notes = NULL; }
  }
  free(save_enotes); save_enotes = NULL;
  if (d->nenotes && (save_enotes = (int *)calloc((size_t)d->nenotes, sizeof(int)))) {
    u32 i, k;
    int nn = 0;
    for (i = 0; i < d->np; i++)
      for (k = 0; k + 4 <= d->p[i]->len; k++)
        if (IS_ENOTE(d->p[i]->t + k)) { int m = note_index(d->p[i]->t + k); if (m < d->nenotes && !save_enotes[m]) save_enotes[m] = ++nn; k += 3; }
    if (!nn) { free(save_enotes); save_enotes = NULL; }
  }
  free(save_comments); save_comments = NULL;
  if (d->ncomments && (save_comments = (int *)calloc((size_t)d->ncomments, sizeof(int)))) {
    u32 i, k;
    int nn = 0;
    for (i = 0; i < d->np; i++)
      for (k = 0; k + 4 <= d->p[i]->len; k++)
        if (IS_CSTART(d->p[i]->t + k)) { int m = note_index(d->p[i]->t + k); if (m < d->ncomments && !save_comments[m]) save_comments[m] = ++nn; k += 3; }
    if (!nn) { free(save_comments); save_comments = NULL; }
  }
  if (!(docxml = w_document(d, &dl))) return NULL;
  if (!(stxml = w_styles(d, &sl))) { free(docxml); return NULL; }
  if (d->nlists && !(numxml = w_numbering(d, &nl))) { free(docxml); free(stxml); return NULL; }
  /* the parts kept from the file read and the work packages' own (docx_write_parts_ext) */
  memset(&ext, 0, sizeof ext);
  buf_init(&ext.ct); buf_init(&ext.pkgrels); buf_init(&ext.docrels);
  docx_write_parts_ext(d, &ext);
  buf_init(&ct); buf_init(&core);
  buf_adds(&ct, XMLHDR);
  buf_adds(&ct, "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
                "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
                "<Override PartName=\"/word/settings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/>");
  if (numxml) buf_adds(&ct, "<Override PartName=\"/word/numbering.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/>");
  {
    int k, g;
    for (g = 0; g <= d->nsects; g++)
      for (k = 0; k < 2 * HF_N; k++)
        if (part_written(d, g, k))
          buf_printf(&ct, "<Override PartName=\"/word/%s%d.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.%s+xml\"/>",
                     k < HF_N ? "header" : "footer", g * HF_N + k % HF_N + 1, k < HF_N ? "header" : "footer");
  }
  if (save_notes) buf_adds(&ct, "<Override PartName=\"/word/footnotes.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footnotes+xml\"/>");
  if (save_enotes) buf_adds(&ct, "<Override PartName=\"/word/endnotes.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.endnotes+xml\"/>");
  if (save_comments) buf_adds(&ct, "<Override PartName=\"/word/comments.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.comments+xml\"/>");
  if (save_nml) buf_adds(&ct, "<Default Extension=\"png\" ContentType=\"image/png\"/><Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>");
  buf_add(&ct, ext.ct.data, ext.ct.len);
  buf_adds(&ct, "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/></Types>");
  info_write_core(d, &core);             /* (WP24: every core property, core/info.h) */
  zip_begin(&z);
  ok = zip_add(&z, "[Content_Types].xml", ct.data, ct.len);
  {
    static const char *const pkgrels = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
      "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
      "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
      "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>";
    Buf pr;
    static const char *const docrels = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
      "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
      "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
      "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings\" Target=\"settings.xml\"/>";
    static const char *const numrel =
      "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering\" Target=\"numbering.xml\"/>";
    Buf wr;
    buf_init(&wr);
    buf_adds(&wr, docrels);
    if (numxml) buf_adds(&wr, numrel);
    {
      int k, g;
      for (g = 0; g <= d->nsects; g++)
        for (k = 0; k < 2 * HF_N; k++)
          if (part_written(d, g, k))
            buf_printf(&wr, "<Relationship Id=\"rId%c%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/%s\" Target=\"%s%d.xml\"/>",
                       k < HF_N ? 'H' : 'F', g * HF_N + k % HF_N, k < HF_N ? "header" : "footer", k < HF_N ? "header" : "footer", g * HF_N + k % HF_N + 1);
    }
    if (save_notes) buf_adds(&wr, "<Relationship Id=\"rIdFn\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footnotes\" Target=\"footnotes.xml\"/>");
    if (save_enotes) buf_adds(&wr, "<Relationship Id=\"rIdEn\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/endnotes\" Target=\"endnotes.xml\"/>");
    if (save_comments) buf_adds(&wr, "<Relationship Id=\"rIdCm\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments\" Target=\"comments.xml\"/>");
    {
      /* hyperlinks' addresses (those to places in the document need none) */
      int k;
      for (k = 0; k < d->nlinks; k++) {
        if (d->links[k][0] == '#') continue;
        buf_printf(&wr, "<Relationship Id=\"rIdLk%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" Target=\"", k + 1);
        xml_escape(&wr, d->links[k], strlen(d->links[k]));
        buf_adds(&wr, "\" TargetMode=\"External\"/>");
      }
    }
    {
      int k;
      for (k = 0; k < save_nml; k++)
        buf_printf(&wr, "<Relationship Id=\"rIdImg%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"media/image%d.%s\"/>",
                   k + 1, k + 1, save_ml[k].type == IMG_PNG ? "png" : "jpeg");
    }
    buf_add(&wr, ext.docrels.data, ext.docrels.len);
    buf_adds(&wr, "</Relationships>");
    buf_init(&pr);
    buf_adds(&pr, pkgrels); buf_add(&pr, ext.pkgrels.data, ext.pkgrels.len); buf_adds(&pr, "</Relationships>");
    ok = ok && !pr.oom && zip_add(&z, "_rels/.rels", pr.data, pr.len);
    buf_free(&pr);
    ok = ok && !wr.oom && zip_add(&z, "word/_rels/document.xml.rels", wr.data, wr.len);
    buf_free(&wr);
  }
  ok = ok && zip_add(&z, "word/document.xml", docxml, dl);
  ok = ok && zip_add(&z, "word/styles.xml", stxml, sl);
  {
    /* Word 2013 mode: table indents are to the table's edge, as Quill keeps them. The work packages'
       settings go in at their places in the schema's order (docx_write_settings_ext) */
    Buf st;
    int k;
    buf_init(&st);
    buf_adds(&st, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
      "<w:settings xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">");
    docx_write_settings_ext(d, &st, 0);
    if (d->pagecolor) buf_adds(&st, "<w:displayBackgroundShape/>");
    docx_write_settings_ext(d, &st, 1);
    buf_adds(&st, "<w:defaultTabStop w:val=\"720\"/>");
    if (d->hyphenate) buf_adds(&st, "<w:autoHyphenation/>");
    docx_write_settings_ext(d, &st, 2);
    if (d->evenodd) buf_adds(&st, "<w:evenAndOddHeaders/>");
    w_notes_settings(&st, d);           /* (WP22b: w:footnotePr, w:endnotePr) */
    buf_adds(&st, "<w:compat><w:compatSetting w:name=\"compatibilityMode\" w:uri=\"http://schemas.microsoft.com/office/word\" w:val=\"15\"/></w:compat>");
    docx_write_settings_ext(d, &st, 3);
    buf_adds(&st, "</w:settings>");
    ok = ok && !st.oom && zip_add(&z, "word/settings.xml", st.data, st.len);
    buf_free(&st);
    for (k = 0; k < (d->nsects + 1) * HF_N * 2 && ok; k++) {
      int g = k / (2 * HF_N), pk = k % (2 * HF_N);
      const Story *st;
      char name[40], *x;
      size_t xl;
      if (!part_written(d, g, pk)) continue;
      st = set_story(d, g, pk);
      sprintf(name, "word/%s%d.xml", pk < HF_N ? "header" : "footer", g * HF_N + pk % HF_N + 1);
      if (!(x = w_story(d, st, pk >= HF_N, pk < HF_N && d->wm.text[0], &xl))) { ok = 0; break; }
      ok = zip_add(&z, name, x, xl);
      free(x);
      if (ok && save_nml) {
        /* a header's pictures: the same media, through its own relationships */
        Buf hr;
        int i;
        buf_init(&hr);
        buf_adds(&hr, XMLHDR);
        buf_adds(&hr, "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
        for (i = 0; i < save_nml; i++)
          buf_printf(&hr, "<Relationship Id=\"rIdImg%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"media/image%d.%s\"/>",
                     i + 1, i + 1, save_ml[i].type == IMG_PNG ? "png" : "jpeg");
        buf_adds(&hr, "</Relationships>");
        sprintf(name, "word/_rels/%s%d.xml.rels", pk < HF_N ? "header" : "footer", g * HF_N + pk % HF_N + 1);
        ok = !hr.oom && zip_add(&z, name, hr.data, hr.len);
        buf_free(&hr);
      }
    }
    if (save_notes && ok) {
      char *fx;
      size_t fl;
      if (!(fx = w_footnotes(d, &fl))) ok = 0;
      else { ok = zip_add(&z, "word/footnotes.xml", fx, fl); free(fx); }
    }
    if (save_enotes && ok) {
      char *ex;
      size_t el;
      if (!(ex = w_endnotes(d, &el))) ok = 0;
      else { ok = zip_add(&z, "word/endnotes.xml", ex, el); free(ex); }
    }
    if (save_comments && ok) {
      char *cx;
      size_t cl;
      if (!(cx = w_comments(d, &cl))) ok = 0;
      else { ok = zip_add(&z, "word/comments.xml", cx, cl); free(cx); }
    }
    for (k = 0; k < save_nml && ok; k++) {
      char name[40];
      sprintf(name, "word/media/image%d.%s", k + 1, save_ml[k].type == IMG_PNG ? "png" : "jpeg");
      ok = zip_add(&z, name, save_ml[k].data, save_ml[k].len);
    }
  }
  if (numxml) ok = ok && zip_add(&z, "word/numbering.xml", numxml, nl);
  ok = ok && zip_add(&z, "docProps/core.xml", core.data, core.len);
  {
    /* the kept parts and the packages' */
    int k;
    for (k = 0; k < ext.nparts; k++) { if (ok) ok = zip_add(&z, ext.parts[k].name, ext.parts[k].data, ext.parts[k].len); free(ext.parts[k].name); free(ext.parts[k].data); }
    free(ext.parts);
    ok = ok && !ext.oom && !ext.ct.oom && !ext.pkgrels.oom && !ext.docrels.oom;
    buf_free(&ext.ct); buf_free(&ext.pkgrels); buf_free(&ext.docrels);
  }
  ok = ok && zip_finish(&z) && !ct.oom && !core.oom;
  free(docxml); free(stxml); free(numxml); buf_free(&ct); buf_free(&core);
  free(save_ml); save_ml = NULL; save_nml = 0;
  free(save_notes); save_notes = NULL;
  free(save_comments); save_comments = NULL;
  free(save_enotes); save_enotes = NULL;
  if (!ok) { zip_free(&z); return NULL; }
  *len = z.out.len;
  out = (u8 *)buf_take(&z.out);
  buf_free(&z.cdir);
  return out;
}

/* ------------------------------------------------------------------ */
/* plain text */
/* whether s[0..n) decodes as UTF-8 throughout (utf8_get consumes a single byte only for a bad
   sequence) */
static int valid_utf8(const u8 *s, size_t n)
{
  size_t i = 0;
  while (i < n) {
    int used;
    if (s[i] < 0x80) { i++; continue; }
    utf8_get((const char *)s + i, n - i, &used);
    if (used == 1) return 0;
    i += used;
  }
  return 1;
}
/* a leading UTF-8 byte order mark is dropped; anything that isn't valid UTF-8 is taken as Latin-1,
   which is what Amiga editors write. The new Doc has no undo history and is not marked modified;
   NULL if out of memory. */
Doc *txt_load(const u8 *data, size_t len)
{
  Doc *d = doc_new();
  Buf u;
  Pos c;
  size_t i;
  if (!d) return NULL;
  buf_init(&u);
  if (len >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) { data += 3; len -= 3; }
  if (valid_utf8(data, len)) buf_add(&u, data, len);
  else for (i = 0; i < len; i++) { char t[4]; buf_add(&u, t, (size_t)utf8_put(t, data[i])); }   /* Latin-1 */
  if (u.oom) { buf_free(&u); doc_free(d); return NULL; }
  c = doc_start(d);
  if (u.len && !doc_insert(d, &c, u.data, (u32)u.len, NULL)) { buf_free(&u); doc_free(d); return NULL; }
  buf_free(&u);
  doc_clear_undo(d);
  d->dirty = 0;
  return d;
}
/* pictures, note references and comment marks are left out and line breaks become newlines;
   characters with no Latin-1 form are handled by utf8_to_latin1. Malloc'd; NULL if out of memory. */
u8 *txt_save(Doc *d, int latin1, size_t *len)
{
  u32 n;
  char *t = doc_text(d, doc_start(d), doc_end_pos(d), &n);
  if (!t) return NULL;
  {
    u32 i, o = 0;
    for (i = 0; i < n; i++) {
      if (i + 4 <= n && IS_MATH(t + i)) { i += 3; continue; }     /* WP14a: equations too */
      if (i + 4 <= n && (IS_PIC(t + i) || IS_MARK(t + i) || IS_NOTE(t + i))) { i += 3; continue; }     /* pictures, notes and comment marks are left out */
      t[o++] = t[i] == '\v' ? '\n' : t[i];
    }
    n = o; t[n] = 0;
  }
  if (latin1) {
    char *o = (char *)malloc(n * 3 + 2);
    if (!o) { free(t); return NULL; }
    *len = utf8_to_latin1(t, n, o, n * 3 + 2);
    free(t);
    return (u8 *)o;
  }
  *len = n;
  return (u8 *)t;
}

/* ------------------------------------------------------------------ */
/* files */
/* the whole file, malloc'd with a NUL after it (*len without it); NULL if it can't be opened or read
   or is over 256 MB */
u8 *file_read(const char *path, size_t *len)
{
  FILE *fp = fopen(path, "rb");
  long n;
  u8 *b;
  if (!fp) return NULL;
  if (fseek(fp, 0, SEEK_END) || (n = ftell(fp)) < 0 || n > 256L * 1024 * 1024 || fseek(fp, 0, SEEK_SET)) { fclose(fp); return NULL; }
  if (!(b = (u8 *)malloc((size_t)n + 1))) { fclose(fp); return NULL; }
  if (fread(b, 1, (size_t)n, fp) != (size_t)n) { free(b); fclose(fp); return NULL; }
  fclose(fp);
  b[n] = 0;
  *len = (size_t)n;
  return b;
}
/* write (replacing) a whole file; 0 if it can't be opened or a write or the close fails (a full
   disk often only shows at the close) */
int file_write(const char *path, const void *data, size_t len)
{
  FILE *fp = fopen(path, "wb");
  int ok;
  if (!fp) return 0;
  ok = fwrite(data, 1, len, fp) == len;
  if (fclose(fp)) ok = 0;
  return ok;
}
/* ---- the file formats (docx.h DocFormat). The rows are tried in this order when a file is read: first
   by their sniff of its bytes, then by its name's ending, and plain text (DF_TEXT) when nothing
   matches. WP28a-d fill in their rows in their own files (core/rtf.c, odt.c, html.c, markdown.c,
   docbin.c); a row with neither load nor save is left out. ODT is a zip too, so it comes before .docx */
static int sniff_zip(const u8 *b, size_t n) { return n >= 4 && b[0] == 'P' && b[1] == 'K' && b[2] == 3 && b[3] == 4; }
static Doc *load_txt(const u8 *b, size_t n, unsigned *lost, char *err, size_t errcap)
{
  Doc *d = txt_load(b, n);
  (void)lost;
  if (!d) snprintf(err, errcap, "Not enough memory to open the file.");
  return d;
}
static u8 *save_txt(Doc *d, size_t *len) { return txt_save(d, 1, len); }      /* (Latin-1, for Amiga editors) */
static Doc *load_docx(const u8 *b, size_t n, unsigned *lost, char *err, size_t errcap) { return docx_load(b, n, lost, err, errcap); }
static const DocFormat fmt_docx = { "Word Document", "docx", sniff_zip, load_docx, docx_save, DF_NATIVE };
static const DocFormat fmt_txt = { "Plain Text", "txt|text|asc", NULL, load_txt, save_txt, DF_TEXT };
extern const DocFormat fmt_odt, fmt_doc, fmt_rtf, fmt_html, fmt_markdown;
const DocFormat *const doc_formats[] = { &fmt_odt, &fmt_docx, &fmt_doc, &fmt_rtf, &fmt_html, &fmt_markdown, &fmt_txt };
const int doc_nformats = (int)(sizeof doc_formats / sizeof doc_formats[0]);
/* whether path ends in "." and one of exts (ext|ext|...), ignoring case (Amiga names are case-insensitive) */
static int ext_in(const char *path, const char *exts)
{
  const char *dot = strrchr(path, '.'), *e = exts;
  size_t n;
  if (!dot || strchr(dot, '/') || strchr(dot, ':')) return 0;
  dot++; n = strlen(dot);
  while (*e) {
    const char *bar = strchr(e, '|');
    size_t k = bar ? (size_t)(bar - e) : strlen(e);
    if (k == n) {
      size_t j;
      for (j = 0; j < k && (e[j] | 0x20) == (dot[j] | 0x20); j++) ;
      if (j == k) return 1;
    }
    e += k + (bar ? 1 : 0);
  }
  return 0;
}
const DocFormat *doc_format_for_name(const char *path, int saving)
{
  int i;
  for (i = 0; i < doc_nformats; i++) {
    const DocFormat *f = doc_formats[i];
    if ((saving ? f->save != NULL : f->load != NULL) && ext_in(path, f->exts)) return f;
  }
  return NULL;
}
void doc_format_pattern(int saving, char *out, size_t cap)
{
  int i;
  size_t o;
  if (cap < 8) { if (cap) out[0] = 0; return; }
  strcpy(out, "#?.(");
  o = 4;
  for (i = 0; i < doc_nformats; i++) {
    const DocFormat *f = doc_formats[i];
    size_t k = strlen(f->exts);
    if (!(saving ? f->save != NULL : f->load != NULL)) continue;
    if (o + k + 3 >= cap) break;
    if (o > 4) out[o++] = '|';
    memcpy(out + o, f->exts, k); o += k;
  }
  out[o++] = ')'; out[o] = 0;
}
/* the first format whose sniff knows the bytes, else the one the name's ending names, else plain text */
Doc *doc_load_file(const char *path, unsigned *lost, char *err, size_t errcap)
{
  size_t n;
  u8 *data = file_read(path, &n);
  const DocFormat *f = NULL;
  Doc *d;
  int i;
  if (lost) *lost = 0;
  if (!data) { snprintf(err, errcap, "Can't read %s.", path); return NULL; }
  for (i = 0; i < doc_nformats && !f; i++) if (doc_formats[i]->load && doc_formats[i]->sniff && doc_formats[i]->sniff(data, n)) f = doc_formats[i];
  for (i = 0; i < doc_nformats && !f; i++) if (doc_formats[i]->load && !doc_formats[i]->sniff && ext_in(path, doc_formats[i]->exts)) f = doc_formats[i];
  if (!f) f = &fmt_txt;
  err[0] = 0;
  d = f->load(data, n, lost, err, errcap);
  if (!d && !err[0]) snprintf(err, errcap, "Quill couldn't read %s.", path);
  free(data);
  return d;
}
/* in the format its name's ending names (case ignored), otherwise Latin-1 plain text; marks the
   document unmodified on success. 0 if out of memory or the file can't be written. */
int doc_save_file(Doc *d, const char *path)
{
  size_t n;
  const DocFormat *f = doc_format_for_name(path, 1);
  u8 *data = (f ? f : &fmt_txt)->save(d, &n);
  int ok;
  if (!data) return 0;
  ok = file_write(path, data, n);
  free(data);
  if (ok) d->dirty = 0;
  return ok;
}
