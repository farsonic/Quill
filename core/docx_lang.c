/* docx_lang.c - WP31's run properties in .docx (see proflang.h): the language (w:lang with its w:val,
   w:eastAsia and w:bidi) and "do not check spelling or grammar" (w:noProof). core/docx.c calls these
   from parse_rpr and w_rpr (and so for runs, styles and the document defaults alike). This file
   belongs to WP31. */
#include <string.h>
#include "proflang.h"
#include "xml.h"
#include "docx_lang.h"

int wp31_read_rpr(Xml *x, unsigned *set, CharFmt *cf)
{
  if (!strcmp(x->name, "lang")) {
    const char *v = xml_attr(x, "val"), *ea = xml_attr(x, "eastAsia"), *bi = xml_attr(x, "bidi");
    *set |= CM_LANG;
    cf->lang = plang_find(v); cf->langea = plang_find(ea); cf->langbidi = plang_find(bi);
    return 1;
  }
  if (!strcmp(x->name, "noProof")) {
    const char *v = xml_attr(x, "val");
    *set |= CM_NOPROOF;
    cf->noproof = (u8)(!v || !(!strcmp(v, "0") || str_ieq(v, "false") || str_ieq(v, "off")));
    return 1;
  }
  return 0;
}
unsigned wp31_rpr_diff(const CharFmt *f, const CharFmt *base)
{
  unsigned d = 0;
  if (f->lang != base->lang || f->langea != base->langea || f->langbidi != base->langbidi) d |= CM_LANG;
  if (f->noproof != base->noproof) d |= CM_NOPROOF;
  return d;
}
static void attr(Buf *b, const char *name, u16 id)
{
  const char *t = plang_tag(id);
  if (!*t) return;
  buf_printf(b, " w:%s=\"", name); xml_escape(b, t, strlen(t)); buf_adds(b, "\"");
}
/* part 0: w:noProof (after the effects, before w:vanish's neighbours); part 1: w:lang (after w:vertAlign) */
void wp31_write_rpr(Buf *b, const CharFmt *f, const CharFmt *base, unsigned diff, int full, int part)
{
  if (!full) diff = wp31_rpr_diff(f, base);
  if (part == 0 && (full ? f->noproof != 0 : (diff & CM_NOPROOF) != 0))
    buf_adds(b, f->noproof ? "<w:noProof/>" : "<w:noProof w:val=\"0\"/>");
  if (part == 1 && (full ? (f->lang || f->langea || f->langbidi) : (diff & CM_LANG) != 0) && (f->lang || f->langea || f->langbidi)) {
    buf_adds(b, "<w:lang");
    attr(b, "val", f->lang); attr(b, "eastAsia", f->langea); attr(b, "bidi", f->langbidi);
    buf_adds(b, "/>");
  }
}
