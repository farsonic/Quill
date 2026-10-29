/* notes.c - WP22b, see notes.h */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "notes.h"

/* one field of the options: the section's if it sets it, else the document's, else Word's */
void notes_eff(const Doc *d, const SectFmt *s, int end, NotePr *out)
{
  const NotePr *dp = end ? &d->enpr : &d->fnpr, *sp = s ? (end ? &s->enpr : &s->fnpr) : NULL;
  memset(out, 0, sizeof *out);
  out->pos = (u8)(end ? NPOS_DOCEND : NPOS_PAGEBOTTOM);
  out->fmt = (u8)(end ? NNF_LOWER_ROMAN : NNF_DECIMAL);
  out->start = 1;
  out->restart = NRS_CONT;
  /* the position is the document's (Word sets it for the whole document) */
  if (dp->set & NPS_POS) out->pos = dp->pos;
  else if (sp && (sp->set & NPS_POS)) out->pos = sp->pos;
  if (sp && (sp->set & NPS_FMT)) { out->fmt = sp->fmt; memcpy(out->other, sp->other, sizeof out->other); }
  else if (dp->set & NPS_FMT) { out->fmt = dp->fmt; memcpy(out->other, dp->other, sizeof out->other); }
  if (sp && (sp->set & NPS_START)) out->start = sp->start; else if (dp->set & NPS_START) out->start = dp->start;
  if (sp && (sp->set & NPS_RESTART)) out->restart = sp->restart; else if (dp->set & NPS_RESTART) out->restart = dp->restart;
  /* what can't be: endnotes at a page's foot or restarting each page, footnotes at an end */
  if (end) { if (out->pos != NPOS_SECTEND) out->pos = NPOS_DOCEND; if (out->restart == NRS_PAGE) out->restart = NRS_CONT; }
  else if (out->pos != NPOS_BENEATHTEXT) out->pos = NPOS_PAGEBOTTOM;
  if (out->fmt >= NNF_N) out->fmt = NNF_DECIMAL;
  if (!out->start) out->start = 1;
  out->set = NPS_POS | NPS_FMT | NPS_START | NPS_RESTART;
}

static void roman(char *o, u32 n, int upper)
{
  static const char *const r[13] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
  static const u32 v[13] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
  int i;
  *o = 0;
  if (!n || n > 3999) { sprintf(o, "%lu", (unsigned long)n); return; }
  for (i = 0; i < 13; i++) while (n >= v[i]) { strcat(o, r[i]); n -= v[i]; }
  if (upper) for (; *o; o++) if (*o >= 'a' && *o <= 'z') *o = (char)(*o - 32);
}
void notes_label(int fmt, u32 n, char *out)
{
  switch (fmt) {
  case NNF_LOWER_LETTER: case NNF_UPPER_LETTER: {
    /* a b ... z aa bb ... (Word's way: the letter repeated) */
    u32 rep, k;
    char c;
    if (!n) { strcpy(out, "0"); return; }
    rep = (n - 1) / 26 + 1; c = (char)((fmt == NNF_LOWER_LETTER ? 'a' : 'A') + (n - 1) % 26);
    if (rep > 20) rep = 20;
    for (k = 0; k < rep; k++) out[k] = c;
    out[rep] = 0;
    return; }
  case NNF_LOWER_ROMAN: roman(out, n, 0); return;
  case NNF_UPPER_ROMAN: roman(out, n, 1); return;
  case NNF_SYMBOL: {
    /* *, dagger, double dagger, section sign, then each doubled, tripled... */
    static const char *const sym[4] = { "*", "\xE2\x80\xA0", "\xE2\x80\xA1", "\xC2\xA7" };
    u32 rep, k;
    const char *s;
    if (!n) { strcpy(out, "0"); return; }
    rep = (n - 1) / 4 + 1; s = sym[(n - 1) % 4];
    if (rep * strlen(s) > 21) rep = (u32)(21 / strlen(s));
    out[0] = 0;
    for (k = 0; k < rep; k++) strcat(out, s);
    return; }
  default: sprintf(out, "%lu", (unsigned long)n);
  }
}
static const char *const fmt_names[NNF_N] = { "decimal", "lowerLetter", "upperLetter", "lowerRoman", "upperRoman", "chicago", "decimal" };
const char *notes_fmt_name(int fmt) { return fmt >= 0 && fmt < NNF_N ? fmt_names[fmt] : "decimal"; }
int notes_fmt_parse(const char *name)
{
  int k;
  for (k = 0; k < NNF_OTHER; k++) if (!strcmp(name, fmt_names[k])) return k;
  return NNF_OTHER;
}

/* ---- .docx */
static const char *const pos_names[4] = { "pageBottom", "beneathText", "sectEnd", "docEnd" };
static const char *const restart_names[3] = { "continuous", "eachSect", "eachPage" };
void notes_read_pr(Xml *x, NotePr *np)
{
  int d0 = x->depth, k;
  while (xml_next(x) != XT_EOF) {
    const char *v;
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START || !(v = xml_attr(x, "val"))) continue;
    if (!strcmp(x->name, "pos")) {
      for (k = 0; k < 4; k++) if (!strcmp(v, pos_names[k])) { np->pos = (u8)k; np->set |= NPS_POS; }
    } else if (!strcmp(x->name, "numFmt")) {
      np->fmt = (u8)notes_fmt_parse(v); np->set |= NPS_FMT;
      if (np->fmt == NNF_OTHER) { strncpy(np->other, v, sizeof np->other - 1); np->other[sizeof np->other - 1] = 0; }
      else np->other[0] = 0;
    } else if (!strcmp(x->name, "numStart")) {
      long n = atol(v);
      if (n >= 0 && n < 32767) { np->start = (u16)(n ? n : 1); np->set |= NPS_START; }
    } else if (!strcmp(x->name, "numRestart")) {
      for (k = 0; k < 3; k++) if (!strcmp(v, restart_names[k])) { np->restart = (u8)k; np->set |= NPS_RESTART; }
    }
  }
}
void notes_write_pr(Buf *b, const NotePr *np, int end, const char *elem, const char *inner)
{
  if (!np->set && !inner) return;
  buf_printf(b, "<w:%s>", elem);
  if (np->set & NPS_POS) {
    int p = np->pos & 3;
    if (end && p < NPOS_SECTEND) p = NPOS_DOCEND;
    if (!end && p > NPOS_BENEATHTEXT) p = NPOS_PAGEBOTTOM;
    buf_printf(b, "<w:pos w:val=\"%s\"/>", pos_names[p]);
  }
  if (np->set & NPS_FMT) {
    if (np->fmt == NNF_OTHER && np->other[0]) { buf_adds(b, "<w:numFmt w:val=\""); xml_escape(b, np->other, strlen(np->other)); buf_adds(b, "\"/>"); }
    else buf_printf(b, "<w:numFmt w:val=\"%s\"/>", notes_fmt_name(np->fmt));
  }
  if (np->set & NPS_START) buf_printf(b, "<w:numStart w:val=\"%u\"/>", (unsigned)(np->start ? np->start : 1));
  if (np->set & NPS_RESTART) buf_printf(b, "<w:numRestart w:val=\"%s\"/>", restart_names[(end && np->restart == NRS_PAGE) ? 0 : np->restart % 3]);
  if (inner) buf_adds(b, inner);
  buf_printf(b, "</w:%s>", elem);
}

/* ---- separators */
int notes_sep(const Doc *d, int end, int which, const Story **text)
{
  const NoteSeps *s = d->nseps;
  u32 i;
  end = end != 0;
  if (text) *text = NULL;
  if (which < 0 || which >= NSEP_N) return 0;
  if (!s || !s->have[end][which]) return which == NSEP_SEP ? NSF_LINE : which == NSEP_CONT ? NSF_LONG : 0;
  /* words in it: its paragraphs are drawn */
  for (i = 0; i < s->text[end][which].np; i++)
    if (s->text[end][which].p[i]->len) { if (text) *text = &s->text[end][which]; break; }
  return s->flags[end][which];
}
int notes_sep_set(Doc *d, int end, int which, int flags, Story *text)
{
  end = end != 0;
  if (which < 0 || which >= NSEP_N) { if (text) story_free(text); return 0; }
  if (!d->nseps && !(d->nseps = (NoteSeps *)calloc(1, sizeof(NoteSeps)))) { if (text) story_free(text); return 0; }
  story_free(&d->nseps->text[end][which]);
  if (text) { d->nseps->text[end][which] = *text; text->p = NULL; text->np = 0; }
  d->nseps->flags[end][which] = (u8)flags;
  d->nseps->have[end][which] = 1;
  return 1;
}
int notes_sep_own(const Doc *d, int end, int which)
{
  const Story *t;
  int f = notes_sep(d, end, which, &t);
  if (!d->nseps || !d->nseps->have[end != 0][which]) return 0;
  return t || f != (which == NSEP_SEP ? NSF_LINE : which == NSEP_CONT ? NSF_LONG : 0);
}
void notes_free(Doc *d)
{
  int e, w;
  if (!d->nseps) return;
  for (e = 0; e < 2; e++) for (w = 0; w < NSEP_N; w++) story_free(&d->nseps->text[e][w]);
  free(d->nseps);
  d->nseps = NULL;
}

/* ---- the window's Apply */
/* the fields np sets copied into dst (the position left out: it is the document's) */
static void merge(NotePr *dst, const NotePr *np)
{
  if (np->set & NPS_FMT) { dst->fmt = np->fmt; memcpy(dst->other, np->other, sizeof dst->other); dst->set |= NPS_FMT; }
  if (np->set & NPS_START) { dst->start = np->start; dst->set |= NPS_START; }
  if (np->set & NPS_RESTART) { dst->restart = np->restart; dst->set |= NPS_RESTART; }
}
int notes_apply(Doc *d, u32 para, int end, int whole, const NotePr *np)
{
  NotePr *dp = end ? &d->enpr : &d->fnpr;
  u32 i, k, nsec = 0;
  int ok = 1;
  Pos at;
  if (!d->np) return 0;
  if (para >= d->np) para = d->np - 1;
  at.para = para; at.off = 0;
  for (i = 0; i + 1 < d->np; i++) if (d->p[i]->pf.sect) nsec++;
  if (!nsec) whole = 1;                          /* one section: the document's settings */
  if (np->set & NPS_POS) { dp->pos = np->pos; dp->set |= NPS_POS; }
  if (whole) merge(dp, np);
  doc_begin(d, at);
  for (i = whole ? 0 : doc_sect_end(d, para); i < d->np; i++) {
    SectFmt s;
    ParaFmt pf;
    NotePr *sp;
    unsigned drop;
    if (!d->p[i]->pf.sect && i != d->np - 1) continue;
    if (in_table(&d->p[i]->pf)) { if (whole) continue; merge(dp, np); break; }   /* (no section format in a cell: the document's) */
    s = *doc_sect(d, i);
    sp = end ? &s.enpr : &s.fnpr;
    drop = np->set & (NPS_POS | NPS_FMT | NPS_START | NPS_RESTART);
    if (whole) {
      /* the document's now: the section's own settings of these fields go */
      if (!(sp->set & drop)) continue;
      sp->set = (u8)(sp->set & ~drop);
    } else {
      sp->set = (u8)(sp->set & ~NPS_POS);
      merge(sp, np);
    }
    memset(&pf, 0, sizeof pf);
    if (!(pf.sect = (u16)doc_add_sect(d, &s))) { ok = 0; break; }
    doc_para_format(d, i, i, PM_SECT, &pf);
    if (!whole) break;
  }
  doc_end(d, at);
  /* the references' numbers change: their paragraphs are laid out again */
  for (i = 0; i < d->np; i++)
    for (k = 0; k + 4 <= d->p[i]->len; k++)
      if (end ? IS_ENOTE(d->p[i]->t + k) : IS_NOTE(d->p[i]->t + k)) { d->p[i]->ver++; break; }
  d->ver++;
  d->dirty = 1;
  return ok;
}
