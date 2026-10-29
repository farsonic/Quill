/*
 * intl.c - WP27's document model: phonetic guides, East Asian settings, the complex script and
 * East Asian look of text, and the editing commands of right-to-left and Asian text. See intl.h.
 */
#include <stdlib.h>
#include <string.h>
#include "intl.h"
#include "bidi.h"

const Ruby *intl_ruby(const Doc *d, u16 k)
{
  return k && k <= d->nrubies ? &d->rubies[k - 1] : NULL;
}
u16 intl_add_ruby(Doc *d, Ruby *r)
{
  Ruby *n;
  if (d->nrubies >= 65000 || !(n = (Ruby *)realloc(d->rubies, (size_t)(d->nrubies + 1) * sizeof(Ruby)))) {
    free(r->rtraw); free(r->rtsrc); r->rtraw = r->rtsrc = NULL;
    return 0;
  }
  d->rubies = n;
  n[d->nrubies] = *r;
  r->rtraw = r->rtsrc = NULL;
  return (u16)++d->nrubies;
}
IntlSettings *intl_settings(Doc *d)
{
  if (!d->intl) d->intl = (IntlSettings *)calloc(1, sizeof(IntlSettings));
  return d->intl;
}
void intl_free(Doc *d)
{
  int i;
  for (i = 0; i < d->nrubies; i++) { free(d->rubies[i].rtraw); free(d->rubies[i].rtsrc); }
  free(d->rubies); d->rubies = NULL; d->nrubies = 0;
  free(d->intl); d->intl = NULL;
}
void intl_look(const Doc *d, CharFmt *f, int scr)
{
  if (scr == SCR_CS || ((f->rtl || (f->cs & CS_ALL)) && scr != SCR_EA)) {
    int b = f->cs & CS_OWNB ? (f->cs & CS_BOLD) != 0 : (f->flags & CF_BOLD) != 0;
    int i = f->cs & CS_OWNI ? (f->cs & CS_ITALIC) != 0 : (f->flags & CF_ITALIC) != 0;
    if (f->fontcs && f->fontcs <= d->nfonts) f->font = (u8)(f->fontcs - 1);
    if (f->sizecs) f->size = f->sizecs;
    f->flags = (u8)((f->flags & ~(CF_BOLD | CF_ITALIC)) | (b ? CF_BOLD : 0) | (i ? CF_ITALIC : 0));
  } else if (scr == SCR_EA && f->fontea && f->fontea <= d->nfonts) f->font = (u8)(f->fontea - 1);
}
int intl_set_dir(Doc *d, u32 pa, u32 pb, int rtl)
{
  ParaFmt v;
  memset(&v, 0, sizeof v);
  v.bidi = (u8)(rtl != 0);
  return doc_para_format(d, pa, pb, PM_BIDI, &v);
}
static int set_masked(Doc *d, Pos a, Pos b, unsigned mask, CharFmt *v, u8 im)
{
  v->intl_mask = im;
  return doc_format(d, a, b, mask, v);
}
int intl_set_rtl(Doc *d, Pos a, Pos b, int rtl)
{
  CharFmt v;
  memset(&v, 0, sizeof v);
  v.rtl = (u8)(rtl != 0);
  return set_masked(d, a, b, CM_CS, &v, IM_RTL);
}
int intl_set_ruby(Doc *d, Pos a, Pos b, const Ruby *r)
{
  CharFmt v;
  memset(&v, 0, sizeof v);
  if (r) {
    Ruby c = *r;
    c.rtraw = c.rtsrc = NULL;
    if (!(v.ruby = intl_add_ruby(d, &c))) return 0;
  }
  return doc_format(d, a, b, CM_RUBY, &v);
}
int intl_set_eal(Doc *d, Pos a, Pos b, int eal)
{
  static u16 ids = 100;
  CharFmt v;
  memset(&v, 0, sizeof v);
  v.eal = (u8)eal;
  v.ealid = (u16)(eal ? ids++ : 0);
  return set_masked(d, a, b, CM_EA, &v, IM_EAL);
}
int intl_set_em(Doc *d, Pos a, Pos b, int em)
{
  CharFmt v;
  memset(&v, 0, sizeof v);
  v.em = (u8)(em >= 0 && em < EM_N ? em : 0);
  return set_masked(d, a, b, CM_EA, &v, IM_EM);
}
int intl_set_font(Doc *d, Pos a, Pos b, int ea, const char *family)
{
  CharFmt v;
  int k = family && family[0] ? doc_font(d, family) : -1;
  memset(&v, 0, sizeof v);
  if (ea) v.fontea = (u8)(k + 1); else v.fontcs = (u8)(k + 1);
  return set_masked(d, a, b, ea ? CM_EA : CM_CS, &v, (u8)(ea ? IM_FONTEA : IM_FONTCS));
}
const SectFmt *intl_sect_of(const Doc *d, u32 i)
{
  u32 j;
  for (j = i; j < d->np; j++) if (d->p[j]->pf.sect) return d->p[j]->pf.sect <= (u32)d->nsects ? &d->sects[d->p[j]->pf.sect - 1] : NULL;
  return NULL;
}
void intl_grid(const SectFmt *s, i32 *linepitch, i32 *charpitch, u16 size)
{
  *linepitch = *charpitch = 0;
  if (!s || s->grid == GRID_NONE) return;
  if (s->linepitch > 0) *linepitch = s->linepitch;
  if (s->grid == GRID_LINESCHARS || s->grid == GRID_SNAPCHARS) {
    /* Word: the pitch is the size in points plus charSpace 4096ths of a point */
    i32 p = (i32)(size ? size : 21) * 10 + (i32)((long)s->charspace * 20 / 4096);
    *charpitch = p > 40 ? p : 40;
  }
}
int intl_sect_apply(Doc *d, u32 para, int whole, void (*fn)(SectFmt *s, void *arg), void *arg)
{
  u32 i, from, to;
  int done = 0;
  Pos c;
  if (para >= d->np) return 0;
  c.para = para; c.off = 0;
  from = whole ? 0 : doc_sect_end(d, para);
  to = whole ? d->np - 1 : from;
  doc_begin(d, c);
  for (i = from; i <= to; i++) {
    SectFmt s;
    ParaFmt pf;
    if (!d->p[i]->pf.sect && i != d->np - 1) continue;
    if (in_table(&d->p[i]->pf)) continue;
    s = *doc_sect(d, i);
    fn(&s, arg);
    memset(&pf, 0, sizeof pf);
    if (!(pf.sect = (u16)doc_add_sect(d, &s))) break;
    if (doc_para_format(d, i, i, PM_SECT, &pf)) done = 1;
  }
  doc_end(d, c);
  return done;
}
