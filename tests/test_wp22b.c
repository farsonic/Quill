/* test_wp22b.c - the native tests of WP22b Footnote and endnote options (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp22b() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/layout.h"
#include "../core/notes.h"
#include "../core/zip.h"

/* a part of a saved .docx as text (malloc'd, NULL if missing) */
static char *part22(const u8 *data, size_t n, const char *name)
{
  ZipReader z;
  size_t len;
  u8 *b, *t;
  if (!zip_open(&z, data, n)) return NULL;
  b = zip_read(&z, zip_find(&z, name), &len);
  zip_close(&z);
  if (!b) return NULL;
  if ((t = (u8 *)realloc(b, len + 1))) { t[len] = 0; b = t; } else { free(b); return NULL; }
  return (char *)b;
}
/* with WP22B_OUT set, d saved there as name (for looking at in LibreOffice) */
static void dump22(Doc *d, const char *name)
{
  const char *dir = getenv("WP22B_OUT");
  char path[400];
  size_t n;
  u8 *data;
  FILE *f;
  if (!dir || !(data = docx_save(d, &n))) return;
  snprintf(path, sizeof path, "%s/%s", dir, name);
  if ((f = fopen(path, "wb"))) { fwrite(data, 1, n, f); fclose(f); }
  free(data);
}
/* the label of the first note box (end: endnote) in paragraph pi */
static void first_label(Layout *L, u32 pi, int end, char *out)
{
  u32 k;
  out[0] = 0;
  for (k = 0; k < L->pl[pi].nboxes; k++)
    if (L->pl[pi].boxes[k].kind == (end ? BX_ENOTE : BX_NOTE)) { lay_note_label(&L->pl[pi], k, out); return; }
}
/* a document of n paragraphs "Paragraph i ..." with a footnote at the end of each paragraph in refs
   (a bit mask), each note's text "Note i." */
static Doc *notes_doc(int n, unsigned refs, int end)
{
  Doc *d = doc_new();
  Pos c = P(0, 0);
  int i, st;
  if (!d) return NULL;
  st = doc_style(d, "Normal");
  for (i = 0; i < n; i++) {
    char t[80], ref[8];
    sprintf(t, "%sParagraph %d of the text, with a few words in it.", i ? "\n" : "", i + 1);
    doc_insert(d, &c, t, (u32)strlen(t), NULL);
    if (refs & (1u << i)) {
      int k = end ? doc_add_endnote(d) : doc_add_note(d);
      sprintf(t, "Note %d.", i + 1);
      story_set_text(d, end ? &d->enotes[k] : &d->notes[k], t, st < 0 ? 0 : st, AL_LEFT);
      if (end) enote_utf8(ref, k); else note_utf8(ref, k);
      doc_insert(d, &c, ref, 4, NULL);
    }
  }
  return d;
}
/* paragraph pi ends a section (starting the next on a new page) */
static void sect_after(Doc *d, u32 pi)
{
  SectFmt s;
  ParaFmt pf;
  sect_default(&s);
  memset(&pf, 0, sizeof pf);
  pf.sect = (u16)doc_add_sect(d, &s);
  doc_para_format(d, pi, pi, PM_SECT, &pf);
}

static void labels(void)
{
  char o[24];
  notes_label(NNF_DECIMAL, 12, o); ok("note labels: 12", !strcmp(o, "12"));
  notes_label(NNF_LOWER_LETTER, 3, o); ok("note labels: c", !strcmp(o, "c"));
  notes_label(NNF_UPPER_LETTER, 28, o); ok("note labels: BB after Z", !strcmp(o, "BB"));
  notes_label(NNF_LOWER_ROMAN, 14, o); ok("note labels: xiv", !strcmp(o, "xiv"));
  notes_label(NNF_UPPER_ROMAN, 9, o); ok("note labels: IX", !strcmp(o, "IX"));
  notes_label(NNF_SYMBOL, 2, o); ok("note labels: a dagger", !strcmp(o, "\xE2\x80\xA0"));
  notes_label(NNF_SYMBOL, 5, o); ok("note labels: ** after the section sign", !strcmp(o, "**"));
  ok("note formats: names both ways", notes_fmt_parse("upperRoman") == NNF_UPPER_ROMAN && !strcmp(notes_fmt_name(NNF_SYMBOL), "chicago") &&
     notes_fmt_parse("decimalEnclosedCircle") == NNF_OTHER);
}
/* Word's notes.docx: the document's format and first number, then numbering on each page */
static void doc_options(void)
{
  char err[200], lab[24];
  unsigned lost;
  Doc *d = doc_load_file("tests/data/notes.docx", &lost, err, sizeof err), *e;
  Layout *L;
  Furn f;
  i32 sepy;
  u32 i, p3 = 0;
  size_t n;
  u8 *data;
  NotePr np;
  if (!d) { ok("note options: notes.docx loads", 0); return; }
  for (i = 0; i < d->np; i++) if (!strncmp(d->p[i]->t, "Paragraph 3.", 12)) p3 = i;
  memset(&np, 0, sizeof np);
  np.set = NPS_FMT | NPS_START; np.fmt = NNF_LOWER_LETTER; np.start = 3;
  ok("note options: applied to the document", notes_apply(d, 0, 0, 1, &np) && d->fnpr.fmt == NNF_LOWER_LETTER && d->fnpr.start == 3);
  L = lay_new(d, &fs); lay_update(L);
  first_label(L, p3, 0, lab);
  ok("note options: the first note is c", !strcmp(lab, "c"));
  ok("note options: its text starts c", lay_notes(L, 0, &f, &sepy) && f.n && !strncmp(f.f[0].p->t, "c The Amiga", 11));
  lay_furniture_free(&f);
  data = docx_save(d, &n);
  {
    char *st = data ? part22(data, n, "word/settings.xml") : NULL;
    ok("note options: in settings.xml before w:compat", st && strstr(st, "<w:footnotePr><w:numFmt w:val=\"lowerLetter\"/><w:numStart w:val=\"3\"/><w:footnote w:id=\"-1\"/>") &&
       strstr(st, "</w:footnotePr><w:compat>"));
    free(st);
  }
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("note options: read back", e && (e->fnpr.set & NPS_FMT) && e->fnpr.fmt == NNF_LOWER_LETTER && e->fnpr.start == 3 && e->nnotes == 3);
  free(data); doc_free(e);
  /* numbered afresh on each page: note 1 on page 1, notes 2 and 3 on page 2 */
  memset(&np, 0, sizeof np);
  np.set = NPS_FMT | NPS_START | NPS_RESTART; np.fmt = NNF_DECIMAL; np.start = 1; np.restart = NRS_PAGE;
  notes_apply(d, 0, 0, 1, &np);
  lay_update(L);
  ok("note options: each page's notes from 1", L->npn == 3 && L->fnn[L->pn[0].note].lab == 1 && L->fnn[L->pn[1].note].lab == 1 && L->fnn[L->pn[2].note].lab == 2);
  dump22(d, "perpage.docx");
  ok("note options: page 2's notes 1 and 2", lay_notes(L, 1, &f, &sepy) && f.n == 2 && !strncmp(f.f[0].p->t, "1 Commodore", 11) && !strncmp(f.f[1].p->t, "2 A second", 10));
  lay_furniture_free(&f);
  /* beneath the text: just under the last line, not at the foot */
  memset(&np, 0, sizeof np);
  np.set = NPS_POS; np.pos = NPOS_BENEATHTEXT;
  notes_apply(d, 0, 0, 1, &np);
  lay_update(L);
  {
    u32 a, b, last = 0;
    i32 bottom;
    lay_page_lines(L, 1, &a, &b);
    for (i = a; i < b; i++) if (L->placed[i].page == 1) last = i;
    bottom = L->placed[last].y + L->pl[L->placed[last].para].lines[L->placed[last].line].height;
    ok("note options: beneath the text", lay_notes(L, 1, &f, &sepy) && f.n == 2 && sepy > bottom && sepy < bottom + 480 && f.f[1].y < d->page.height - d->page.bottom - 1440);
    lay_furniture_free(&f);
  }
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  dump22(d, "beneath.docx");
  ok("note options: position and restart read back", e && e->fnpr.pos == NPOS_BENEATHTEXT && e->fnpr.restart == NRS_PAGE);
  free(data); doc_free(e);
  lay_free(L); doc_free(d);
}
/* a section's own options (sectPr), and numbers starting again each section */
static void sect_options(void)
{
  Doc *d = notes_doc(4, 0xF, 0), *e;
  Layout *L;
  NotePr np;
  char lab[24], err[200];
  unsigned lost;
  size_t n;
  u8 *data;
  if (!d) { ok("section note options: made", 0); return; }
  sect_after(d, 1);                      /* paragraphs 1-2 a section, 3-4 another */
  memset(&np, 0, sizeof np);
  np.set = NPS_FMT | NPS_RESTART; np.fmt = NNF_UPPER_ROMAN; np.restart = NRS_SECTION;
  ok("section note options: this section", notes_apply(d, 3, 0, 0, &np) && !(d->fnpr.set & NPS_FMT));
  L = lay_new(d, &fs); lay_update(L);
  first_label(L, 0, 0, lab); ok("section note options: section 1 as before (1)", !strcmp(lab, "1"));
  first_label(L, 1, 0, lab); ok("section note options: then 2", !strcmp(lab, "2"));
  first_label(L, 2, 0, lab); ok("section note options: section 2 from I", !strcmp(lab, "I"));
  first_label(L, 3, 0, lab); ok("section note options: then II", !strcmp(lab, "II"));
  dump22(d, "sections.docx");
  data = docx_save(d, &n);
  {
    char *doc = data ? part22(data, n, "word/document.xml") : NULL;
    ok("section note options: in the last w:sectPr, before w:pgSz", doc && strstr(doc, "<w:footnotePr><w:numFmt w:val=\"upperRoman\"/><w:numRestart w:val=\"eachSect\"/></w:footnotePr><w:pgSz"));
    free(doc);
  }
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  {
    const SectFmt *s = e ? doc_sect(e, e->np - 1) : NULL;
    ok("section note options: read back", s && (s->fnpr.set & NPS_FMT) && s->fnpr.fmt == NNF_UPPER_ROMAN && s->fnpr.restart == NRS_SECTION && !(doc_sect(e, 0)->fnpr.set & NPS_FMT));
  }
  free(data); doc_free(e);
  /* undo takes the section's settings away again */
  {
    Pos c;
    doc_undo(d, &c);
    ok("section note options: undone", !(doc_sect(d, d->np - 1)->fnpr.set & NPS_FMT));
  }
  lay_free(L); doc_free(d);
}
/* endnotes at each section's end, before the next section's pages */
static void sect_endnotes(void)
{
  Doc *d = notes_doc(4, 0x5, 1), *e;
  Layout *L;
  NotePr np;
  Furn f;
  i32 sepy;
  u32 i, pg2 = 0;
  char err[200], lab[24];
  unsigned lost;
  size_t n;
  u8 *data;
  if (!d) { ok("section endnotes: made", 0); return; }
  sect_after(d, 1);
  memset(&np, 0, sizeof np);
  np.set = NPS_POS | NPS_RESTART; np.pos = NPOS_SECTEND; np.restart = NRS_SECTION;
  notes_apply(d, 0, 1, 1, &np);
  L = lay_new(d, &fs); lay_update(L);
  for (i = 0; i < L->nplaced; i++) if (L->placed[i].para == 2) { pg2 = L->placed[i].page; break; }
  ok("section endnotes: two lots, each with its line", L->nen == 2 && L->nenseps == 2 && L->en[0].page < pg2 && L->en[1].page >= pg2);
  ok("section endnotes: the first after section 1's text", lay_endnotes(L, L->en[0].page, &f, &sepy) && f.n == 1 && !strncmp(f.f[0].p->t, "i Note 1.", 9) && sepy > 0);
  lay_furniture_free(&f);
  first_label(L, 2, 1, lab);
  ok("section endnotes: section 2's numbered from i again", !strcmp(lab, "i"));
  dump22(d, "sectend.docx");
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("section endnotes: endnotePr read back", e && e->enpr.pos == NPOS_SECTEND && e->enpr.restart == NRS_SECTION && e->nenotes == 2);
  free(data); doc_free(e);
  lay_free(L); doc_free(d);
}
/* a footnote too long for the page goes on overleaf under the continuation separator; the document's
   own separator and continuation notice drawn and kept */
static void long_note(void)
{
  Doc *d = notes_doc(3, 0x1, 0), *e;
  Layout *L;
  Furn f;
  i32 sepy;
  char *t, err[200];
  unsigned lost;
  u32 i, l0 = 0, l1 = 0;
  size_t n;
  u8 *data;
  int st, found = 0;
  if (!d) { ok("long footnote: made", 0); return; }
  st = doc_style(d, "Normal");
  t = (char *)malloc(12000);
  t[0] = 0;
  for (i = 0; i < 180; i++) strcat(t, "A long footnote's words going on and on, ");
  story_set_text(d, &d->notes[0], t, st < 0 ? 0 : st, AL_LEFT);
  free(t);
  L = lay_new(d, &fs); lay_update(L);
  for (i = 0; i < L->nncut; i++) if (L->ncut[i].page == 0 && L->ncut[i].l0 == 0) { l1 = L->ncut[i].l1; found++; }
  for (i = 0; i < L->nncut; i++) if (L->ncut[i].page == 1 && L->ncut[i].l0 == l1) { l0 = L->ncut[i].l0; found++; }
  ok("long footnote: split over two pages", found == 2 && l1 > 3 && l0 == l1 && L->placed[0].page == 0 && L->npages >= 2);
  ok("long footnote: page 1 has the short line", lay_notes(L, 0, &f, &sepy) && sepy > 0 && L->sepw == 2880 && !L->notesep_cont && f.f[f.n - 1].y < d->page.height - d->page.bottom);
  lay_furniture_free(&f);
  ok("long footnote: page 2 the continuation separator", lay_notes(L, 1, &f, &sepy) && sepy > 0 && L->notesep_cont && L->sepw == d->page.width - d->page.left - d->page.right &&
     f.n && f.f[0].pl.nlines && f.f[0].pl.lines[0].start > 0);
  lay_furniture_free(&f);
  /* the document's own separator (words, no line) and a continuation notice */
  {
    Story s1, s2;
    s1.p = NULL; s1.np = 0; s2.p = NULL; s2.np = 0;
    story_set_text(d, &s1, "Notes", st < 0 ? 0 : st, AL_LEFT);
    story_set_text(d, &s2, "(continued overleaf)", st < 0 ? 0 : st, AL_RIGHT);
    notes_sep_set(d, 0, NSEP_SEP, 0, &s1);
    notes_sep_set(d, 0, NSEP_NOTICE, 0, &s2);
  }
  lay_update(L);
  dump22(d, "longnote.docx");
  ok("long footnote: the separator's words and the notice drawn", lay_notes(L, 0, &f, &sepy) && sepy < 0 && f.n >= 3 && !strcmp(f.f[0].p->t, "Notes") &&
     !strcmp(f.f[f.n - 1].p->t, "(continued overleaf)") && f.f[f.n - 1].y + 200 <= d->page.height - d->page.bottom + 300);
  lay_furniture_free(&f);
  data = docx_save(d, &n);
  {
    char *fx = data ? part22(data, n, "word/footnotes.xml") : NULL, *sx = data ? part22(data, n, "word/settings.xml") : NULL;
    ok("long footnote: separators written", fx && strstr(fx, "w:type=\"separator\" w:id=\"-1\"") && strstr(fx, ">Notes<") && !strstr(fx, "<w:separator/>") &&
       strstr(fx, "<w:continuationSeparator/>") && strstr(fx, "w:type=\"continuationNotice\" w:id=\"2\"") && sx && strstr(sx, "<w:footnote w:id=\"2\"/>"));
    free(fx); free(sx);
  }
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  {
    const Story *ts = NULL, *tn = NULL;
    int f1 = e ? notes_sep(e, 0, NSEP_SEP, &ts) : -1, f2 = e ? notes_sep(e, 0, NSEP_CONT, NULL) : -1;
    if (e) notes_sep(e, 0, NSEP_NOTICE, &tn);
    ok("long footnote: separators read back", e && f1 == 0 && ts && !strcmp(ts->p[0]->t, "Notes") && f2 == NSF_LONG && tn && !strcmp(tn->p[0]->t, "(continued overleaf)") &&
       e->nnotes == 1);
  }
  free(data); doc_free(e);
  lay_free(L); doc_free(d);
}
/* LibreOffice's notes (its separators are ids 0 and 1) */
static void lo_notes(void)
{
  char err[200];
  unsigned lost;
  Doc *d = doc_load_file("tests/data/notes-lo.docx", &lost, err, sizeof err), *e;
  if (!d) { ok("LibreOffice notes: load", 0); return; }
  ok("LibreOffice notes: its format and separators read", (d->fnpr.set & NPS_FMT) && d->fnpr.fmt == NNF_DECIMAL && notes_sep(d, 0, NSEP_SEP, NULL) == NSF_LINE &&
     notes_sep(d, 0, NSEP_CONT, NULL) == NSF_LONG && !notes_sep_own(d, 0, NSEP_SEP));
  e = docx_again(d, &lost);
  ok("LibreOffice notes: saved and read back", e && same_doc(d, e) && e->nnotes == d->nnotes && (e->fnpr.set & NPS_FMT));
  doc_free(e); doc_free(d);
}

void test_wp22b(void)
{
  labels();
  doc_options();
  sect_options();
  sect_endnotes();
  long_note();
  lo_notes();
}
