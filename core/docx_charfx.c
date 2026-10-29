/* docx_charfx.c - WP9's run properties in .docx (see charfx.h): hidden text (w:vanish), outline,
   shadow, emboss, engrave (w:imprint), the underline's colour (w:u w:color), character shading
   (w:shd, apart from the highlight), and run properties kept verbatim (w14 text effects, w:effect,
   w:kern). core/docx.c calls these from parse_rpr and w_rpr. This file belongs to WP9. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "charfx.h"
#include "xml.h"

/* an on/off property: on unless val is 0, false, off or none */
static int on_off(Xml *x)
{
  const char *v = xml_attr(x, "val");
  return !v || !(!strcmp(v, "0") || str_ieq(v, "false") || str_ieq(v, "off") || str_ieq(v, "none"));
}
static u32 hex6(const char *v)
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
/* an fx2 bit read on or off (off matters: it overrides a style) */
static void fx2_set(unsigned *set, CharFmt *cf, u16 bit, int on)
{
  *set |= CM_FX2;
  cf->fx2mask |= bit;
  if (on) cf->fx2 |= bit; else cf->fx2 &= (u16)~bit;
}
/* the elements kept verbatim: Word 2010's w14 text effects and typography, w:effect, w:kern */
static int kept_name(Xml *x)
{
  const char *t = x->s + x->tok + 1;
  return !strncmp(t, "w14:", 4) || !strcmp(x->name, "effect") || !strcmp(x->name, "kern");
}
int wp9_read_rpr(Xml *x, unsigned *set, CharFmt *cf)
{
  const char *n = x->name, *v = xml_attr(x, "val");
  if (!strcmp(n, "vanish")) fx2_set(set, cf, FX2_HIDDEN, on_off(x));
  else if (!strcmp(n, "outline")) fx2_set(set, cf, FX2_OUTLINE, on_off(x));
  else if (!strcmp(n, "shadow")) fx2_set(set, cf, FX2_SHADOW, on_off(x));
  else if (!strcmp(n, "emboss")) fx2_set(set, cf, FX2_EMBOSS, on_off(x));
  else if (!strcmp(n, "imprint")) fx2_set(set, cf, FX2_ENGRAVE, on_off(x));
  else if (!strcmp(n, "u")) {
    /* the underline's kind is docx.c's; its colour is ours */
    const char *c = xml_attr(x, "color");
    u32 col = hex6(c);
    if (c) { *set |= CM_ULCOLOR; cf->ulcolor = col == COLOR_AUTO ? 0 : HL_ON | col; }
    return 0;                                     /* (docx.c reads the rest of it) */
  }
  else if (!strcmp(n, "shd")) {
    const char *fill = xml_attr(x, "fill");
    u32 col = fill ? hex6(fill) : COLOR_AUTO;
    *set |= CM_SHADE;
    /* a pattern (w:val pct25...) over a colour: Word's percentage shadings are grey on white */
    if (col == COLOR_AUTO && v && !strncmp(v, "pct", 3)) {
      int pct = atoi(v + 3);
      u32 g = (u32)(255 - 255 * (pct > 100 ? 100 : pct) / 100);
      col = (g << 16) | (g << 8) | g;
    }
    cf->shade = col == COLOR_AUTO ? 0 : HL_ON | col;
  }
  else if (kept_name(x)) {
    const char *raw;
    size_t len = xml_raw(x, &raw);
    const char *old = rx_text(cf->rx);
    size_t ol = strlen(old);
    char *both = (char *)malloc(ol + len + 1);
    if (both) {
      memcpy(both, old, ol); memcpy(both + ol, raw, len); both[ol + len] = 0;
      cf->rx = rx_add(both, ol + len);
      *set |= CM_RX;
      free(both);
    }
    return 2;                                     /* (read to its end already) */
  }
  else return 0;
  return 1;
}
unsigned wp9_rpr_diff(const CharFmt *f, const CharFmt *base)
{
  unsigned d = 0;
  if (f->fx2 != base->fx2) d |= CM_FX2;
  if (f->ulcolor != base->ulcolor && (f->flags & CF_UNDERLINE)) d |= CM_ULCOLOR;
  if (f->shade != base->shade) d |= CM_SHADE;
  if (f->rx != base->rx) d |= CM_RX;
  return d;
}
/* (w:u's colour attribute, written into docx.c's w:u) */
void wp9_ulcolor_attr(Buf *b, const CharFmt *f)
{
  if (f->ulcolor) buf_printf(b, " w:color=\"%06lX\"", (unsigned long)(f->ulcolor & 0xFFFFFF));
}
/* WP9's elements of a w:rPr: part 0 the effects (after w:dstrike, where the schema has them), part 1
   shading (w:shd, after w:bdr), part 2 the kept ones (last) */
void wp9_write_rpr(Buf *b, const CharFmt *f, const CharFmt *base, unsigned diff, int full, int part)
{
  if (part == 0 && (diff & CM_FX2)) {
    static const struct { u16 bit; const char *name; } fx[5] = {
      { FX2_OUTLINE, "outline" }, { FX2_SHADOW, "shadow" }, { FX2_EMBOSS, "emboss" }, { FX2_ENGRAVE, "imprint" }, { FX2_HIDDEN, "vanish" } };
    int k;
    for (k = 0; k < 5; k++) {
      int on = (f->fx2 & fx[k].bit) != 0, was = (base->fx2 & fx[k].bit) != 0;
      if (full ? on : on != was) buf_printf(b, on ? "<w:%s/>" : "<w:%s w:val=\"0\"/>", fx[k].name);
    }
  }
  if (part == 1 && (diff & CM_SHADE) && (!full || f->shade)) {
    if (f->shade) buf_printf(b, "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"%06lX\"/>", (unsigned long)(f->shade & 0xFFFFFF));
    else buf_adds(b, "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"auto\"/>");
  }
  if (part == 2 && (diff & CM_RX) && f->rx) buf_adds(b, rx_text(f->rx));
}

/* ------------------------------------------------------------------ */
/* enclosed characters as Word writes them */
int wp9_symbol_run(const char *s, size_t n)
{
  int k;
  for (k = EN_CIRCLE; k <= EN_DIAMOND; k++) {
    const char *sym = enclose_symbol(k);
    size_t sl = strlen(sym), i;
    for (i = 0; i + sl <= n; i++) if (!memcmp(s + i, sym, sl)) return 1;
  }
  return 0;
}
/* rpr (a w:rPr element or nothing, rl bytes) with its size and position replaced: size half-points,
   pos half-points (0: none), the two going where the schema puts them (before w:highlight and what
   follows, else at the end) */
static void rpr_sized(Buf *o, const char *rpr, size_t rl, unsigned size, int pos)
{
  static const char *const after[] = { "<w:szCs", "<w:highlight", "<w:u ", "<w:u/", "<w:effect", "<w:bdr", "<w:shd", "<w:fitText", "<w:vertAlign",
                                       "<w:rtl", "<w:cs", "<w:em", "<w:lang", "<w:eastAsianLayout", "<w14:", "</w:rPr>" };
  char *t = str_ndup(rl ? rpr : "<w:rPr></w:rPr>", rl ? rl : 15), *cut;
  size_t k, at;
  const char *rm[3] = { "<w:sz ", "<w:szCs ", "<w:position " };
  char ins[96];
  if (!t) return;
  /* out with the old ones */
  for (k = 0; k < 3; k++)
    while ((cut = strstr(t, rm[k]))) { char *e = strstr(cut, "/>"); if (!e) break; memmove(cut, e + 2, strlen(e + 2) + 1); }
  at = strlen(t);
  for (k = 0; k < sizeof after / sizeof after[0]; k++) { char *f = strstr(t, after[k]); if (f && (size_t)(f - t) < at) at = (size_t)(f - t); }
  if (pos) sprintf(ins, "<w:position w:val=\"%d\"/><w:sz w:val=\"%u\"/><w:szCs w:val=\"%u\"/>", pos, size, size);
  else sprintf(ins, "<w:sz w:val=\"%u\"/><w:szCs w:val=\"%u\"/>", size, size);
  buf_add(o, t, at); buf_adds(o, ins); buf_adds(o, t + at);
  free(t);
}
static void instr_run(Buf *o, const char *rpr, size_t rl, const char *s, size_t n)
{
  buf_adds(o, "<w:r>"); buf_add(o, rpr, rl); buf_adds(o, "<w:instrText xml:space=\"preserve\">"); xml_escape(o, s, n); buf_adds(o, "</w:instrText></w:r>");
}
int wp9_write_enclosed(Buf *o, const CharFmt *f, const char *rpr, size_t rl, const char *s, u32 n)
{
  int shape = (int)ENCLOSURE(f), enlarge = (f->fx2 & FX2_ENLARGE) != 0;
  unsigned base = f->size ? f->size : 24;
  Buf sym, txt;
  char head[32];
  u32 i;
  if (!shape || shape > EN_DIAMOND || !n) return 0;
  for (i = 0; i < n; i++) if ((u8)s[i] < 0x20 || (u8)s[i] >= 0xEE) return 0;     /* (only plain text goes in a field code) */
  buf_init(&sym); buf_init(&txt);
  if (enlarge) { rpr_sized(&sym, rpr, rl, (base * 3 + 1) / 2, -(int)((base + 2) / 5)); buf_add(&txt, rpr, rl); }
  else { buf_add(&sym, rpr, rl); rpr_sized(&txt, rpr, rl, (base * 7 + 5) / 10 ? (base * 7 + 5) / 10 : 1, (int)((base + 5) / 10)); }
  buf_adds(o, "<w:r>"); buf_add(o, rpr, rl); buf_adds(o, "<w:fldChar w:fldCharType=\"begin\"/></w:r>");
  sprintf(head, " eq \\o\\ac(%s", enclose_symbol(shape));
  instr_run(o, sym.data ? sym.data : "", sym.len, head, strlen(head));
  instr_run(o, rpr, rl, ",", 1);
  instr_run(o, txt.data ? txt.data : "", txt.len, s, n);
  instr_run(o, rpr, rl, ")", 1);
  buf_adds(o, "<w:r>"); buf_add(o, rpr, rl); buf_adds(o, "<w:fldChar w:fldCharType=\"end\"/></w:r>");
  buf_free(&sym); buf_free(&txt);
  return 1;
}
int wp10_bullet_font(const char *font)
{
  return font[0] && !str_ieq(font, "Symbol") && strncmp(font, "Wingdings", 9) && strcmp(font, "Webdings");
}
