/*
 * docx_intl.c - WP27's .docx properties (see docx_intl.h): read in docx.c's parse_rpr, parse_ppr and
 * parse_sect, written in w_rpr, w_ppr_body and w_sectpr, in the places the schema's order wants.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "docx_intl.h"
#include "intl.h"

static int on_off(Xml *x)
{
  const char *v = xml_attr(x, "val");
  return !v || !(!strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "off") || !strcmp(v, "none"));
}
static int num_attr(Xml *x, const char *name, long *out)
{
  const char *v = xml_attr(x, name);
  char *e;
  long k;
  if (!v || !*v) return 0;
  k = strtol(v, &e, 10);
  if (e == v) return 0;
  *out = k;
  return 1;
}
static const char *const ems[EM_N] = { "none", "dot", "comma", "circle", "underDot" };
static const char *const brks[EB_N] = { "none", "round", "square", "angle", "curly" };

int wp27_read_rpr(Xml *x, Doc *d, unsigned *set, CharFmt *cf)
{
  const char *n = x->name, *v = xml_attr(x, "val");
  long k;
  if (!strcmp(n, "rFonts")) {
    const char *ea = xml_attr(x, "eastAsia"), *cs = xml_attr(x, "cs"), *hint = xml_attr(x, "hint"), *as = xml_attr(x, "ascii");
    if (ea && *ea) { *set |= CM_EA; cf->fontea = (u8)(doc_font(d, ea) + 1); cf->intl_mask |= IM_FONTEA; }
    if (cs && *cs) {
      *set |= CM_CS; cf->intl_mask |= IM_FONTCS;
      cf->fontcs = (u8)(as && str_ieq(as, cs) ? 0 : doc_font(d, cs) + 1);    /* (the same as the text's: its own) */
    }
    if (hint) { *set |= CM_CS; cf->intl_mask |= IM_HINT; cf->hint = (u8)(!strcmp(hint, "eastAsia") ? HINT_EA : !strcmp(hint, "cs") ? HINT_CS : HINT_DEFAULT); }
    return 0;
  }
  if (!strcmp(n, "rtl")) { *set |= CM_CS; cf->intl_mask |= IM_RTL; cf->rtl = (u8)on_off(x); return 1; }
  if (!strcmp(n, "cs")) { *set |= CM_CS; cf->intl_mask |= IM_CSFL; if (on_off(x)) cf->cs |= CS_ALL; else cf->cs &= (u8)~CS_ALL; return 1; }
  if (!strcmp(n, "bCs") || !strcmp(n, "iCs")) {
    int b = n[0] == 'b', on = on_off(x);
    unsigned flag = b ? CF_BOLD : CF_ITALIC;
    u8 own = (u8)(b ? CS_OWNB : CS_OWNI), bit = (u8)(b ? CS_BOLD : CS_ITALIC);
    /* the same as w:b (w:i) in this rPr: doubled, as Word writes it, not a value of its own */
    if ((*set & flag) && !!(cf->flags & flag) == on) { cf->cs &= (u8)~(own | bit); return 1; }
    *set |= CM_CS; cf->intl_mask |= IM_CSFL;
    cf->cs = (u8)((cf->cs & ~bit) | own | (on ? bit : 0));
    return 1;
  }
  if (!strcmp(n, "szCs")) {
    if (v && (k = atol(v)) > 0 && k < 3277 && !((*set & CM_SIZE) && cf->size == k)) { *set |= CM_CS; cf->intl_mask |= IM_SIZECS; cf->sizecs = (u16)k; }
    return 1;
  }
  if (!strcmp(n, "em")) {
    int e;
    for (e = 0; e < EM_N && !(v && !strcmp(v, ems[e])); e++) ;
    *set |= CM_EA; cf->intl_mask |= IM_EM; cf->em = (u8)(e < EM_N ? e : EM_NONE);
    return 1;
  }
  if (!strcmp(n, "eastAsianLayout")) {
    const char *c = xml_attr(x, "combine"), *vt = xml_attr(x, "vert"), *vc = xml_attr(x, "vertCompress"), *br = xml_attr(x, "combineBrackets");
    int e = 0, b;
    if (c && (*c == '1' || *c == 't' || !strcmp(c, "on"))) e |= EAL_COMBINE;
    if (vt && (*vt == '1' || *vt == 't' || !strcmp(vt, "on"))) e |= EAL_VERT;
    if (vc && (*vc == '1' || *vc == 't' || !strcmp(vc, "on"))) e |= EAL_VCOMPRESS;
    for (b = 0; b < EB_N && !(br && !strcmp(br, brks[b])); b++) ;
    if (b < EB_N) e |= b << 4;
    *set |= CM_EA; cf->intl_mask |= IM_EAL; cf->eal = (u8)e;
    cf->ealid = (u16)(num_attr(x, "id", &k) && k >= 0 && k < 65536 ? k : 0);
    return 1;
  }
  return 0;
}
unsigned wp27_rpr_diff(const CharFmt *f, const CharFmt *b)
{
  unsigned m = 0;
  if (f->rtl != b->rtl || f->cs != b->cs || f->fontcs != b->fontcs || f->sizecs != b->sizecs || f->hint != b->hint) m |= CM_CS;
  if (f->fontea != b->fontea || f->em != b->em || f->eal != b->eal || f->ealid != b->ealid) m |= CM_EA;
  return m;
}
static void w_font_attr(Buf *b, Doc *d, const char *attr, int k)
{
  if (k < 0 || k >= d->nfonts) return;
  buf_printf(b, " w:%s=\"", attr);
  xml_escape(b, d->fonts[k], strlen(d->fonts[k]));
  buf_adds(b, "\"");
}
void wp27_write_rfonts(Buf *b, Doc *d, const CharFmt *f, const CharFmt *base, unsigned diff, int full)
{
  static const char *const hints[3] = { "default", "eastAsia", "cs" };
  int fontd = (diff & CM_FONT) && f->font < d->nfonts;
  int own = full ? (f->fontcs || f->fontea || f->hint) : (f->fontcs != base->fontcs || f->fontea != base->fontea || f->hint != base->hint);
  if (!fontd && !own) return;
  buf_adds(b, "<w:rFonts");
  if (f->font < d->nfonts) { w_font_attr(b, d, "ascii", f->font); w_font_attr(b, d, "hAnsi", f->font); }
  if (f->fontea) w_font_attr(b, d, "eastAsia", f->fontea - 1);
  w_font_attr(b, d, "cs", f->fontcs ? f->fontcs - 1 : f->font);
  if (f->hint) buf_printf(b, " w:hint=\"%s\"", hints[f->hint < 3 ? f->hint : 0]);
  buf_adds(b, "/>");
}
void wp27_write_bi(Buf *b, const CharFmt *f, unsigned diff)
{
  int cb = f->cs & CS_OWNB ? (f->cs & CS_BOLD) != 0 : (f->flags & CF_BOLD) != 0;
  int ci = f->cs & CS_OWNI ? (f->cs & CS_ITALIC) != 0 : (f->flags & CF_ITALIC) != 0;
  if (diff & CM_BOLD) buf_adds(b, f->flags & CF_BOLD ? "<w:b/>" : "<w:b w:val=\"0\"/>");
  if ((diff & CM_BOLD) || ((diff & CM_CS) && (f->cs & CS_OWNB))) buf_adds(b, cb ? "<w:bCs/>" : "<w:bCs w:val=\"0\"/>");
  if (diff & CM_ITALIC) buf_adds(b, f->flags & CF_ITALIC ? "<w:i/>" : "<w:i w:val=\"0\"/>");
  if ((diff & CM_ITALIC) || ((diff & CM_CS) && (f->cs & CS_OWNI))) buf_adds(b, ci ? "<w:iCs/>" : "<w:iCs w:val=\"0\"/>");
}
void wp27_write_sz(Buf *b, const CharFmt *f, unsigned diff)
{
  if (diff & CM_SIZE) buf_printf(b, "<w:sz w:val=\"%u\"/>", (unsigned)f->size);
  if ((diff & CM_SIZE) || ((diff & CM_CS) && f->sizecs)) buf_printf(b, "<w:szCs w:val=\"%u\"/>", (unsigned)(f->sizecs ? f->sizecs : f->size));
}
void wp27_write_rpr(Buf *b, const CharFmt *f, const CharFmt *base, int full, int part)
{
  if (part == 0) {
    if (full ? f->rtl : f->rtl != base->rtl) buf_adds(b, f->rtl ? "<w:rtl/>" : "<w:rtl w:val=\"0\"/>");
    if (full ? (f->cs & CS_ALL) : ((f->cs ^ base->cs) & CS_ALL)) buf_adds(b, f->cs & CS_ALL ? "<w:cs/>" : "<w:cs w:val=\"0\"/>");
    if (full ? f->em : f->em != base->em) buf_printf(b, "<w:em w:val=\"%s\"/>", ems[f->em < EM_N ? f->em : 0]);
  } else if (full ? f->eal != 0 : (f->eal != base->eal || (f->eal && f->ealid != base->ealid))) {
    buf_printf(b, "<w:eastAsianLayout w:id=\"%u\"", (unsigned)f->ealid);
    if (f->eal & EAL_COMBINE) {
      buf_adds(b, " w:combine=\"1\"");
      if (EAL_BRACKETS(f)) buf_printf(b, " w:combineBrackets=\"%s\"", brks[EAL_BRACKETS(f) < EB_N ? EAL_BRACKETS(f) : 0]);
    } else if (base->eal & EAL_COMBINE) buf_adds(b, " w:combine=\"0\"");
    if (f->eal & EAL_VERT) { buf_adds(b, " w:vert=\"1\""); if (f->eal & EAL_VCOMPRESS) buf_adds(b, " w:vertCompress=\"1\""); }
    else if (base->eal & EAL_VERT) buf_adds(b, " w:vert=\"0\"");
    buf_adds(b, "/>");
  }
}
/* ParaFmt.asian bits and their elements, in the schema's order (w:bidi comes between them and w:snapToGrid) */
static const struct { const char *name; u8 bit; u8 inv; } asian[] = {
  { "kinsoku", AS_NOKINSOKU, 1 }, { "wordWrap", AS_NOWORDWRAP, 1 }, { "overflowPunct", AS_NOOVERFLOW, 1 },
  { "topLinePunct", AS_TOPLINE, 0 }, { "autoSpaceDE", AS_NOSPACEDE, 1 }, { "autoSpaceDN", AS_NOSPACEDN, 1 } };
static const char *const taligns[5] = { "auto", "top", "center", "baseline", "bottom" };
int wp27_read_ppr(Xml *x, unsigned *set, ParaFmt *pf)
{
  const char *n = x->name, *v;
  size_t i;
  if (!strcmp(n, "bidi")) { *set |= PM_BIDI; pf->bidi = (u8)on_off(x); return 1; }
  if (!strcmp(n, "snapToGrid")) { *set |= PM_ASIAN; if (on_off(x)) pf->asian &= (u8)~AS_NOSNAP; else pf->asian |= AS_NOSNAP; return 1; }
  if (!strcmp(n, "textAlignment")) {
    int k;
    v = xml_attr(x, "val");
    for (k = 0; k < 5 && !(v && !strcmp(v, taligns[k])); k++) ;
    *set |= PM_ASIAN; pf->talign = (u8)(k < 5 ? k : 0);
    return 1;
  }
  for (i = 0; i < sizeof asian / sizeof asian[0]; i++)
    if (!strcmp(n, asian[i].name)) {
      int on = on_off(x);
      *set |= PM_ASIAN;
      if (on != asian[i].inv) pf->asian |= asian[i].bit; else pf->asian &= (u8)~asian[i].bit;
      return 1;
    }
  return 0;
}
void wp27_write_ppr(Buf *b, const ParaFmt *p, const ParaFmt *base, int full, int part)
{
  size_t i;
  if (part == 0) {
    for (i = 0; i < sizeof asian / sizeof asian[0]; i++) {
      int set = (p->asian & asian[i].bit) != 0;
      if (full ? set : set != ((base->asian & asian[i].bit) != 0)) {
        int on = set != asian[i].inv;
        buf_printf(b, on ? "<w:%s/>" : "<w:%s w:val=\"0\"/>", asian[i].name);
      }
    }
    if (full ? p->bidi : p->bidi != base->bidi) buf_adds(b, p->bidi ? "<w:bidi/>" : "<w:bidi w:val=\"0\"/>");
    if (full ? (p->asian & AS_NOSNAP) : ((p->asian ^ base->asian) & AS_NOSNAP)) buf_adds(b, p->asian & AS_NOSNAP ? "<w:snapToGrid w:val=\"0\"/>" : "<w:snapToGrid/>");
  } else if (full ? p->talign : p->talign != base->talign) buf_printf(b, "<w:textAlignment w:val=\"%s\"/>", taligns[p->talign < 5 ? p->talign : 0]);
}
static const char *const tdirs[STD_N] = { "lrTb", "tbRl", "btLr", "lrTbV", "tbRlV", "tbLrV" };
static const char *const grids[4] = { "default", "lines", "linesAndChars", "snapToChars" };
int wp27_read_sect(Xml *x, SectFmt *sf)
{
  const char *n = x->name, *v = xml_attr(x, "val");
  long k;
  int i;
  if (!strcmp(n, "bidi")) sf->bidi = (u8)on_off(x);
  else if (!strcmp(n, "textDirection")) {
    for (i = 0; i < STD_N && !(v && !strcmp(v, tdirs[i])); i++) ;
    /* (the older names: tb-rl and the like) */
    if (i == STD_N && v) i = !strcmp(v, "tbRl") || !strcmp(v, "rl") ? STD_TBRL : !strcmp(v, "btLr") ? STD_BTLR : STD_LRTB;
    sf->tdir = (u8)(i < STD_N ? i : STD_LRTB);
  } else if (!strcmp(n, "docGrid")) {
    const char *t = xml_attr(x, "type");
    for (i = 0; i < 4 && !(t && !strcmp(t, grids[i])); i++) ;
    sf->grid = (u8)(i < 4 ? i : GRID_NONE);
    sf->linepitch = num_attr(x, "linePitch", &k) && k > 0 && k < 32000 ? (i32)k : 0;
    sf->charspace = num_attr(x, "charSpace", &k) && k > -2000000 && k < 2000000 ? (i32)k : 0;
    if (sf->grid == GRID_NONE && sf->linepitch == 360) sf->linepitch = sf->charspace = 0;   /* (no grid, the default pitch: nothing to keep) */
  } else return 0;
  xml_skip(x);
  return 1;
}
void wp27_write_sect(Buf *o, const SectFmt *s, int part)
{
  if (!s) return;
  if (part == 0) {
    if (s->tdir && s->tdir < STD_N) buf_printf(o, "<w:textDirection w:val=\"%s\"/>", tdirs[s->tdir]);
    if (s->bidi) buf_adds(o, "<w:bidi/>");
  } else if (s->grid || s->linepitch) {
    buf_adds(o, "<w:docGrid");
    if (s->grid) buf_printf(o, " w:type=\"%s\"", grids[s->grid & 3]);
    if (s->linepitch) buf_printf(o, " w:linePitch=\"%ld\"", (long)s->linepitch);
    if (s->charspace) buf_printf(o, " w:charSpace=\"%ld\"", (long)s->charspace);
    buf_adds(o, "/>");
  }
}
static const char *const cscs[3] = { "doNotCompress", "compressPunctuation", "compressPunctuationAndJapaneseKana" };
int wp27_read_setting(Doc *d, Xml *x)
{
  const char *n = x->name, *v = xml_attr(x, "val");
  IntlSettings *s;
  if (strcmp(n, "characterSpacingControl") && strcmp(n, "strictFirstAndLastChars") && strcmp(n, "noLineBreaksAfter") && strcmp(n, "noLineBreaksBefore")) return 0;
  if (!(s = intl_settings(d))) return 1;
  if (n[0] == 'c') { int k; for (k = 0; k < 3 && !(v && !strcmp(v, cscs[k])); k++) ; s->cspacing = (u8)(k < 3 ? k : 0); s->cspset = 1; }
  else if (n[0] == 's') s->strict = (u8)on_off(x);
  else {
    const char *l = xml_attr(x, "lang");
    char *dst = n[12] == 'A' ? s->noafter : s->nobefore;
    size_t cap = n[12] == 'A' ? sizeof s->noafter : sizeof s->nobefore;
    snprintf(dst, cap, "%s", v ? v : "");
    if (l) snprintf(s->lang, sizeof s->lang, "%s", l);
  }
  return 1;
}
void wp27_write_settings(Doc *d, Buf *b, int where)
{
  IntlSettings *s = d->intl;
  if (!s || where != 4) return;
  /* (in slot 4, after w:evenAndOddHeaders; w:characterSpacingControl belongs before WP22a's
     w:printTwoOnOne, so with that one it is left out: doNotCompress, the default, is what Word then uses) */
  if (s->cspset && !d->twoonone) buf_printf(b, "<w:characterSpacingControl w:val=\"%s\"/>", cscs[s->cspacing < 3 ? s->cspacing : 0]);
  if (s->strict) buf_adds(b, "<w:strictFirstAndLastChars/>");
  if (s->noafter[0]) {
    buf_printf(b, "<w:noLineBreaksAfter w:lang=\"%s\" w:val=\"", s->lang[0] ? s->lang : "ja-JP");
    xml_escape(b, s->noafter, strlen(s->noafter)); buf_adds(b, "\"/>");
  }
  if (s->nobefore[0]) {
    buf_printf(b, "<w:noLineBreaksBefore w:lang=\"%s\" w:val=\"", s->lang[0] ? s->lang : "ja-JP");
    xml_escape(b, s->nobefore, strlen(s->nobefore)); buf_adds(b, "\"/>");
  }
}
