/* coretest.c - native tests of Quill's portable core (run by build.sh with ASan and UBSan) */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../core/doc.h"
#include "../core/fonts.h"
#include "../core/layout.h"
#include "../core/docx.h"
#include "../core/zip.h"
#include "../core/table.h"
#include "../core/tstyle.h"
#include "../core/pdf.h"
#include "../core/template.h"
#include "../core/toc.h"
#include "../core/spell.h"
#include "../core/ttraster.h"
#include "../core/hyph.h"
#include "../core/image.h"
#include "testutil.h"

/* (the helpers the work packages' tests share are declared in testutil.h) */
int fails, total;
void ok(const char *what, int cond)
{
  total++;
  if (!cond) { fails++; printf("FAIL %s\n", what); }
}
Pos P(u32 para, u32 off) { Pos p; p.para = para; p.off = off; return p; }
char *all(Doc *d) { return doc_text(d, doc_start(d), doc_end_pos(d), NULL); }
int text_is(Doc *d, const char *want)
{
  char *t = all(d);
  int r = strcmp(t, want) == 0;
  if (!r) printf("  text: [%s] want [%s]\n", t, want);
  free(t);
  return r;
}
/* every paragraph's runs cover its text exactly, in order, with no empty or mergeable neighbours */
int runs_sane(Doc *d)
{
  u32 i, k;
  for (i = 0; i < d->np; i++) {
    Para *p = d->p[i];
    u32 prev = 0;
    if (!p->nr || p->r[p->nr - 1].end != p->len) return 0;
    for (k = 0; k < p->nr; k++) {
      if (p->len && p->r[k].end <= prev) return 0;
      if (k && memcmp(&p->r[k].f, &p->r[k - 1].f, sizeof(CharFmt)) == 0) return 0;
      prev = p->r[k].end;
    }
    if (strlen(p->t) != p->len) return 0;
  }
  return 1;
}

static void typing(void)
{
  Doc *d = doc_new();
  Pos c = doc_start(d);
  const char *s = "Hello world";
  int i;
  for (i = 0; s[i]; i++) doc_insert(d, &c, s + i, 1, NULL);
  ok("typing: text goes in", text_is(d, "Hello world") && c.off == 11);
  doc_insert(d, &c, "\n", 1, NULL);
  doc_insert(d, &c, "Second", 6, NULL);
  ok("typing: Return makes a paragraph", d->np == 2 && text_is(d, "Hello world\nSecond") && c.para == 1 && c.off == 6);
  c = P(0, 5);
  doc_insert(d, &c, "\n", 1, NULL);
  ok("typing: Return in the middle splits", d->np == 3 && text_is(d, "Hello\n world\nSecond") && c.para == 1 && c.off == 0);
  doc_delete(d, P(0, 5), P(1, 0));
  ok("delete: joining paragraphs", d->np == 2 && text_is(d, "Hello world\nSecond"));
  doc_delete(d, P(0, 3), P(1, 2));
  ok("delete: across paragraphs", d->np == 1 && text_is(d, "Helcond") && runs_sane(d));
  doc_free(d);
}
FontSet fs;
/* sections: columns, section and column breaks, line numbers, vertical alignment; saved and read back */
static int set_sect(Doc *d, u32 pi, const SectFmt *s)
{
  ParaFmt pf;
  memset(&pf, 0, sizeof pf);
  pf.sect = (u16)doc_add_sect(d, s);
  return doc_para_format(d, pi, pi, PM_SECT, &pf);
}
static void sections(void)
{
  Doc *d = doc_new(), *e;
  Layout *L;
  Pos c = doc_start(d);
  SectFmt s;
  ParaFmt pf;
  u32 i, k, left = 0, right = 0, onp1 = 0, nums = 0, firstnum = 0;
  i32 body = d->page.width - d->page.left - d->page.right;
  size_t n;
  u8 *data;
  unsigned lost;
  char err[200];
  for (i = 0; i < 30; i++) {
    static const char t[] = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna.";
    doc_insert(d, &c, t, sizeof t - 1, NULL);
    if (i < 29) doc_insert(d, &c, "\n", 1, NULL);
  }
  sect_default(&s); s.cols = 2; s.colline = 1;
  set_sect(d, d->np - 1, &s);
  L = lay_new(d, &fs); lay_update(L);
  for (k = 0; k < L->nplaced; k++) if (L->placed[k].page == 0) { if (L->placed[k].x == 0) left++; else right++; }
  ok("sections: two columns side by side, each under half the width", left > 5 && right > 5 && L->placed[0].w < body / 2 && L->nseps > 0 && L->seps[0].page == 0);
  ok("sections: the second column starts at the top", L->placed[left].x > 0 && L->placed[left].y == L->placed[0].y);
  /* paragraphs 0-4 a one-column section; the rest continue below it in two columns */
  sect_default(&s);
  set_sect(d, 4, &s);
  d->sects[d->p[d->np - 1]->pf.sect - 1].start = SB_CONT;
  lay_update(L);
  {
    Placed *a = &L->placed[L->pl[4].placed0 + L->pl[4].nlines - 1], *b = &L->placed[L->pl[5].placed0];
    ok("sections: a continuous section goes on below, in its columns", L->placed[0].w == body && b->page == 0 && b->y > a->y && b->w < body / 2);
  }
  d->sects[d->p[d->np - 1]->pf.sect - 1].start = SB_NEXT;
  lay_update(L);
  ok("sections: a next-page section starts a page", L->placed[L->pl[5].placed0].page == 1 && L->placed[L->pl[5].placed0].y == L->placed[0].y);
  d->sects[d->p[d->np - 1]->pf.sect - 1].start = SB_ODD;
  lay_update(L);
  ok("sections: an odd-page section skips a page", L->placed[L->pl[5].placed0].page == 2);
  d->sects[d->p[d->np - 1]->pf.sect - 1].start = SB_NEXT;
  /* a column break before paragraph 7 */
  memset(&pf, 0, sizeof pf); pf.colbreak = 1;
  doc_para_format(d, 7, 7, PM_BREAK, &pf);
  d->sects[d->p[d->np - 1]->pf.sect - 1].linenum = LN_PAGE;
  lay_update(L);
  ok("sections: a column break moves to the next column", L->placed[L->pl[7].placed0].x > 0 && L->placed[L->pl[7].placed0].page == L->placed[L->pl[6].placed0].page);
  for (k = 0; k < L->nplaced; k++) {
    if (L->placed[k].lnum) { nums++; if (L->placed[k].page == 1 && !firstnum) firstnum = L->placed[k].lnum; }
    if (L->placed[k].page == 1) onp1++;
  }
  ok("sections: line numbers in the numbered section, starting at 1 on its page", nums > 10 && firstnum == 1 && !L->placed[0].lnum);
  /* Line Numbering Options: from 10, every fifth line shown */
  d->sects[d->p[d->np - 1]->pf.sect - 1].lnstart = 10; d->sects[d->p[d->np - 1]->pf.sect - 1].lnby = 5;
  lay_update(L);
  for (firstnum = 0, nums = 0, k = 0; k < L->nplaced; k++)
    if (L->placed[k].lnum) { if (L->placed[k].lnum % 5) nums = -1000; if (L->placed[k].page == 1 && !firstnum) firstnum = L->placed[k].lnum; }
  ok("sections: line numbers start at 10 and count by 5", firstnum == 10 && nums == 0);
  /* splitting the paragraph that ends a section: the second part ends it */
  c.para = 4; c.off = 5;
  doc_insert(d, &c, "\n", 1, NULL);
  ok("sections: Return keeps the break after the paragraph", !d->p[4]->pf.sect && d->p[5]->pf.sect && doc_sect(d, 4)->cols == 1);
  doc_undo(d, &c);
  /* saved and read back */
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("sections: columns, breaks and line numbers survive saving", e && same_doc(d, e) && e->p[4]->pf.sect && doc_sect(e, 20)->cols == 2 &&
     doc_sect(e, 20)->colline && doc_sect(e, 20)->linenum == LN_PAGE && e->p[7]->pf.colbreak);
  free(data); doc_free(e);
  /* vertical alignment: a short page centred */
  lay_free(L); doc_free(d);
  d = doc_new(); c = doc_start(d);
  doc_insert(d, &c, "Centred", 7, NULL);
  sect_default(&s); s.valign = VA_CENTER;
  set_sect(d, 0, &s);
  L = lay_new(d, &fs); lay_update(L);
  ok("sections: vertical alignment centres a page", L->placed[0].y > d->page.height / 2 - 1440 && L->placed[0].y < d->page.height / 2 + 720);
  lay_free(L); doc_free(d);
}
/* drop caps: the first letter big, the lines beside it indented; saved as Word's framed paragraph */
static void drop_caps(void)
{
  Doc *d = doc_new(), *e;
  Layout *L;
  Pos c = doc_start(d);
  ParaFmt pf;
  ParaLay *pl;
  size_t n;
  u8 *data;
  unsigned lost;
  char err[200];
  static const char t[] = "Once upon a time there was a word processor for the Amiga that set its first letter large, over three lines, "
                          "as books do at the start of a chapter, and the text ran on beside it and then below it to the end.";
  doc_insert(d, &c, t, sizeof t - 1, NULL);
  doc_insert(d, &c, t, sizeof t - 1, NULL);
  memset(&pf, 0, sizeof pf); pf.dropcap = 3;
  doc_para_format(d, 0, 0, PM_DROP, &pf);
  L = lay_new(d, &fs); lay_update(L);
  pl = &L->pl[0];
  ok("drop caps: the letter in a big box of its own", pl->nboxes && pl->boxes[0].kind == BX_DROP && pl->boxes[0].len == 1 && pl->boxes[0].f.size > 3 * d->p[0]->r[0].f.size);
  ok("drop caps: three lines beside it, then the margin again", pl->nlines > 4 && pl->lines[0].left > 0 && pl->lines[2].left == pl->lines[0].left &&
     pl->lines[3].left == 0 && pl->boxes[0].x + pl->lines[0].left >= -2 && pl->boxes[0].x + pl->lines[0].left <= 2 && lay_drop_dy(pl, &pl->boxes[0]) >= 2 * pl->lines[0].height);
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("drop caps: saved as a frame and read back as one", e && e->np == 1 && e->p[0]->pf.dropcap == 3 && !strcmp(e->p[0]->t, d->p[0]->t));
  free(data); doc_free(e);
  {
    /* a one-line paragraph (a title) dropped over two lines: made tall enough, the letter's baseline
       one line down, not two */
    i32 h;
    c = doc_end_pos(d);
    doc_insert(d, &c, "\nTitle", 6, NULL);
    pf.dropcap = 2;
    doc_para_format(d, 1, 1, PM_DROP, &pf);
    lay_update(L);
    pl = &L->pl[1];
    h = pl->nlines == 1 ? pl->lines[0].height / 2 : 0;
    ok("drop caps: a short paragraph made room for the letter, which sits on the last line it covers",
       h > 0 && lay_drop_dy(pl, &pl->boxes[0]) == h);
  }
  lay_free(L); doc_free(d);
}
/* hyphenation: Liang's patterns break words where they may; layout uses them for words at a line's end */
static void hyphenation(void)
{
  u8 brk[40];
  Doc *d;
  Layout *L;
  Pos c;
  u32 k, hy = 0;
  int ok1;
  ok("hyphenation: the British English patterns load", hyph_load("dict/hyph_en_GB.dic"));
  ok1 = hyph_word("hyphenation", 11, brk) == 3 && brk[2] && brk[6] && brk[7];
  ok("hyphenation: hy-phen-a-tion, com-puter, Amiga not at all", ok1 && hyph_word("computer", 8, brk) == 1 && brk[3] &&
     hyph_word("Amiga", 5, brk) == 0 && hyph_word("caf\xc3\xa9s", 6, brk) == 0);
  d = doc_new(); c = doc_start(d);
  for (k = 0; k < 12; k++) {
    static const char t[] = "Internationalisation characteristically demonstrates typographical considerations unquestionably. ";
    doc_insert(d, &c, t, sizeof t - 1, NULL);
  }
  d->hyphenate = 1;
  L = lay_new(d, &fs); lay_update(L);
  for (k = 0; k < L->pl[0].nboxes; k++) if (L->pl[0].boxes[k].kind == BX_TEXT && L->pl[0].boxes[k].nspace) hy++;
  ok("hyphenation: long words hyphenated at line ends", hy > 2);
  d->hyphenate = 0;
  lay_update(L);
  for (hy = 0, k = 0; k < L->pl[0].nboxes; k++) if (L->pl[0].boxes[k].kind == BX_TEXT && L->pl[0].boxes[k].nspace) hy++;
  ok("hyphenation: and not when it is off", hy == 0);
  lay_free(L); doc_free(d);
}
/* web layout: the text as wide as asked, on one long page, page breaks and headers left out */
static void web_layout(void)
{
  char err[200];
  unsigned lost;
  Doc *d = doc_load_file("tests/data/headers.docx", &lost, err, sizeof err);
  Layout *L, *W;
  Furn f;
  u32 i, wide = 0, narrow = 0;
  if (!d) { ok("web layout: loads", 0); return; }
  L = lay_new(d, &fs); lay_update(L);
  W = lay_new(d, &fs); W->webw = 14000; lay_update(W);
  for (i = 0; i < L->npl; i++) narrow += L->pl[i].nlines;
  for (i = 0; i < W->npl; i++) wide += W->pl[i].nlines;
  ok("web layout: one page, fewer lines when wider", L->npages > 1 && W->npages == 1 && wide < narrow && W->complete);
  ok("web layout: no header or footer", !lay_furniture(W, 0, 0, &f) && !lay_furniture(W, 0, 1, &f));
  W->webw = 0; lay_update(W);
  ok("web layout: back to pages", W->npages == L->npages);
  lay_free(L); lay_free(W); doc_free(d);
}
/* spelling: words known (with capitals, possessives and hyphens), suggestions, the next mistake */
static void spelling(void)
{
  static const char words[] = "computer\nfamily\nAmiga\ncaf\xc3\xa9\nthe\nwas\na\nof\nfavourite\nwell\nknown\nform\nfrom\n";
  Spell sp;
  Doc *d = doc_new();
  Pos c = doc_start(d), a, b;
  char *sug[8];
  int n, i, has_from = 0, has_form = 0;
  ok("spelling: a list loaded", spell_load_mem(&sp, words, sizeof words - 1) && sp.n == 13);
  ok("spelling: known, in capitals, possessive, hyphenated, with digits", spell_ok(&sp, "family", 6) && spell_ok(&sp, "Family", 6) && spell_ok(&sp, "AMIGA", 5) &&
     spell_ok(&sp, "Amiga's", 7) && spell_ok(&sp, "well-known", 10) && spell_ok(&sp, "4th", 3) && spell_ok(&sp, "CAF\xc3\x89", 5));
  ok("spelling: not known", !spell_ok(&sp, "famly", 5) && !spell_ok(&sp, "computr", 7) && !spell_ok(&sp, "well-knwon", 10));
  n = spell_suggest(&sp, "fomr", 4, sug, 8);
  for (i = 0; i < n; i++) { if (!strcmp(sug[i], "form")) has_form = 1; free(sug[i]); }
  n = spell_suggest(&sp, "frmo", 4, sug, 8);
  for (i = 0; i < n; i++) { if (!strcmp(sug[i], "from")) has_from = 1; free(sug[i]); }
  ok("spelling: suggestions one edit away", has_from && has_form);
  n = spell_suggest(&sp, "Famly", 5, sug, 8);
  ok("spelling: a capital kept", n >= 1 && !strcmp(sug[0], "Family"));
  for (i = 0; i < n; i++) free(sug[i]);
  doc_insert(d, &c, "The Amiga was a famly of computrs.", 34, NULL);
  ok("spelling: the next mistake, then the one after", spell_next(&sp, d, P(0, 0), &a, &b) && a.off == 16 && b.off == 21 &&
     spell_next(&sp, d, b, &a, &b) && a.off == 25 && b.off == 33);
  spell_add(&sp, "computrs", 8);
  ok("spelling: a word added is known", spell_ok(&sp, "computrs", 8) && spell_next(&sp, d, P(0, 22), &a, &b) && a.off == 16);
  spell_free(&sp); doc_free(d);
  {
    Spell big;
    ok("spelling: the shipped list loads, and knows common words", spell_load(&big, "dict/british-english") && big.n > 60000 &&
       spell_ok(&big, "colour", 6) && spell_ok(&big, "Computers", 9) && !spell_ok(&big, "recieve", 7));
    {
      /* in Quill's order already, so loading it sorts nothing (the words keep the file's order) */
      u32 k;
      for (k = 1; k < big.n && big.word[k - 1] < big.word[k]; k++) ;
      ok("spelling: the shipped list is in order already", big.n && k == big.n);
    }
    n = spell_suggest(&big, "recieve", 7, sug, 8);
    ok("spelling: 'recieve' suggests 'receive'", n >= 1 && !strcmp(sug[0], "receive"));
    for (i = 0; i < n; i++) free(sug[i]);
    {
      /* the likely slip first: a doubled letter, two swapped, one missed out */
      static const char *const typo[][2] = { { "Thiss", "This" }, { "tset", "test" }, { "speling", "spelling" }, { "cheker", "checker" },
                                            { "becuase", "because" }, { "adress", "address" }, { "tomorow", "tomorrow" }, { "teh", "the" } };
      int t, all = 1;
      for (t = 0; t < (int)(sizeof typo / sizeof typo[0]); t++) {
        n = spell_suggest(&big, typo[t][0], (u32)strlen(typo[t][0]), sug, 8);
        if (n < 1 || strcmp(sug[0], typo[t][1])) { all = 0; printf("  (%s: %s first)\n", typo[t][0], n ? sug[0] : "nothing"); }
        for (i = 0; i < n; i++) free(sug[i]);
      }
      ok("spelling: the likeliest word first for common slips", all);
    }
    spell_free(&big);
  }
}
/* endnotes: numbered i, ii... in the order of reference, after the text (on a page of their own if
   need be), saved and read back */
static void endnotes(void)
{
  Doc *d = doc_template(TPL_LETTER), *e;
  Layout *L;
  int a = doc_add_endnote(d), b = doc_add_endnote(d), st = doc_style(d, "Normal");
  char ref[8], err[200];
  Pos c;
  Furn f;
  i32 sepy;
  u32 i, k, last;
  unsigned lost;
  size_t n;
  u8 *data;
  story_set_text(d, &d->enotes[a], "The first endnote.", st, AL_LEFT);
  story_set_text(d, &d->enotes[b], "The second one.", st, AL_LEFT);
  c = P(4, 5); enote_utf8(ref, b); doc_insert(d, &c, ref, 4, NULL);       /* b is referred to first */
  c = P(5, 3); enote_utf8(ref, a); doc_insert(d, &c, ref, 4, NULL);
  L = lay_new(d, &fs); lay_update(L);
  for (k = 0; k < L->pl[4].nboxes && L->pl[4].boxes[k].kind != BX_ENOTE; k++) continue;
  {
    char lab[16];
    lay_note_label(&L->pl[4], k, lab);
    ok("endnotes: the first referred to is i", k < L->pl[4].nboxes && !strcmp(lab, "i") && L->enotenum[b] == 1 && L->enotenum[a] == 2);
  }
  last = L->npages - 1;
  ok("endnotes: after the text, in number order", L->nen == 2 && lay_endnotes(L, last, &f, &sepy) && f.n == 2 && !strncmp(f.f[0].p->t, "i The second", 12) &&
     !strncmp(f.f[1].p->t, "ii The first", 12) && sepy > 0 && f.f[0].y > sepy);
  if (f.n) {
    /* below the last line of text */
    u32 a2, b2;
    i32 bottom = 0;
    lay_page_lines(L, last, &a2, &b2);
    for (i = a2; i < b2; i++) if (L->placed[i].page == last) bottom = L->placed[i].y + L->pl[L->placed[i].para].lines[L->placed[i].line].height;
    ok("endnotes: below the text", sepy > bottom);
  }
  lay_furniture_free(&f);
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("endnotes: saved and read back", e && !(lost & LOST_NOTES) && e->nenotes == 2 && same_doc(d, e) && IS_ENOTE(e->p[4]->t + 5));
  free(data); doc_free(e); lay_free(L); doc_free(d);
}
/* tracked changes: deletions marked (insertions really go), accepted and rejected, saved and read back */
static void tracked(void)
{
  Doc *d = doc_new(), *e;
  Pos c = doc_start(d), a, b;
  CharFmt f;
  int r;
  char err[200];
  unsigned lost;
  size_t n;
  u8 *data;
  doc_insert(d, &c, "keep this text", 14, NULL);
  r = doc_add_rev(d, "Ada", "2026-10-24T10:00:00Z");
  f = *para_fmt_at(d->p[0], 0); f.rev = REV_INS; f.rid = (u16)r;
  c = P(0, 5);
  doc_insert(d, &c, "all ", 4, &f);                       /* "keep all this text" */
  ok("tracked: an insertion carries its mark", para_fmt_at(d->p[0], 5)->rev == REV_INS && para_fmt_at(d->p[0], 9)->rev == REV_NONE);
  doc_track_delete(d, P(0, 7), P(0, 13), r);                /* "keep al[l this] text": "l " goes, "this" is marked */
  ok("tracked: deleting removes inserted text and marks the rest", !strcmp(d->p[0]->t, "keep althis text") && para_fmt_at(d->p[0], 6)->rev == REV_INS &&
     para_fmt_at(d->p[0], 7)->rev == REV_DEL && para_fmt_at(d->p[0], 10)->rev == REV_DEL && para_fmt_at(d->p[0], 11)->rev == REV_NONE);
  ok("tracked: the next change from the start, and the one after", doc_next_change(d, P(0, 0), 1, &a, &b) && a.off == 5 && b.off == 7 &&
     doc_next_change(d, b, 1, &a, &b) && a.off == 7 && b.off == 11);
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("tracked: saved as w:ins and w:del and read back", e && !(lost & LOST_TRACKED) && same_doc(d, e) && para_fmt_at(e->p[0], 5)->rev == REV_INS &&
     para_fmt_at(e->p[0], 8)->rev == REV_DEL && e->nrevs >= 1 && !strcmp(e->revs[para_fmt_at(e->p[0], 8)->rid].author, "Ada"));
  free(data); doc_free(e);
  ok("tracked: reject all: back to the start", doc_resolve(d, P(0, 0), P(0, d->p[0]->len), 0) && !strcmp(d->p[0]->t, "keep this text") && d->p[0]->nr == 1);
  ok("tracked: undo the rejection", doc_undo(d, &c) && !strcmp(d->p[0]->t, "keep althis text"));
  ok("tracked: accept all", doc_resolve(d, P(0, 0), P(0, d->p[0]->len), 1) && !strcmp(d->p[0]->t, "keep al text") && d->p[0]->nr == 1 && !d->p[0]->r[0].f.rev);
  doc_free(d);
}
/* comments: added on a range, found, saved and read back, deleted */
static void comments(void)
{
  Doc *d = doc_new(), *e;
  Pos c = doc_start(d), s, en;
  int k;
  char err[200];
  unsigned lost;
  size_t n;
  u8 *data;
  Layout *L;
  doc_insert(d, &c, "one two three", 13, NULL);
  k = doc_comment(d, P(0, 4), P(0, 7), "Is two right?", "Ada", "AL", "2026-10-24T10:00:00Z");
  ok("comments: added, marks round the range", k == 0 && d->p[0]->len == 21 && IS_CSTART(d->p[0]->t + 4) && IS_CEND(d->p[0]->t + 11) &&
     !strcmp(d->comments[0].text.p[0]->t, "Is two right?"));
  ok("comments: found from inside, not from outside", doc_comment_at(d, P(0, 9), &s, &en) == 0 && s.off == 4 && en.off == 11 &&
     doc_comment_at(d, P(0, 2), NULL, NULL) < 0 && doc_comment_at(d, P(0, 18), NULL, NULL) < 0);
  ok("comments: the caret steps over the marks", pos_right(d, P(0, 3)).off == 8 && pos_left(d, P(0, 8)).off == 3);
  L = lay_new(d, &fs); lay_update(L);
  ok("comments: the marks take no room", L->pl[0].nboxes >= 5 && L->pl[0].boxes[2].kind == BX_MARK && L->pl[0].boxes[2].w == 0);
  lay_free(L);
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("comments: saved and read back (range, author, text)", e && !(lost & LOST_COMMENTS) && e->ncomments == 1 && same_doc(d, e) &&
     !strcmp(e->comments[0].author, "Ada") && !strcmp(e->comments[0].date, "2026-10-24T10:00:00Z") && e->comments[0].text.np == 1 &&
     !strcmp(e->comments[0].text.p[0]->t, "Is two right?"));
  free(data); doc_free(e);
  ok("comments: deleted, and undone", doc_delete_comment(d, 0) && d->p[0]->len == 13 && doc_undo(d, &c) && d->p[0]->len == 21);
  doc_free(d);
}
/* cells merged and split, aligned and shaded */
static void cell_ops(void)
{
  Doc *d = doc_new(), *e;
  Pos c = doc_start(d);
  u32 i, n0;
  char err[200];
  unsigned lost;
  size_t n;
  u8 *data;
  tbl_insert(d, &c, 2, 3, 9000);
  n0 = d->np;
  doc_insert(d, &c, "A", 1, NULL);
  ok("cells: merge two in a row", tbl_merge(d, P(0, 0), P(1, 0)) && d->np == n0 - 1 && d->p[0]->pf.cell.span == 2 && d->p[0]->pf.cell.col == 0 &&
     d->p[1]->pf.cell.col == 2 && d->p[2]->pf.cell.row == 1);
  c = P(0, 0);
  ok("cells: split it again", tbl_split(d, &c) && d->np == n0 && d->p[0]->pf.cell.span == 1 && d->p[1]->pf.cell.col == 1 && !d->p[1]->len && !strcmp(d->p[0]->t, "A"));
  ok("cells: merge undone", doc_undo(d, &c) && d->np == n0 - 1 && doc_undo(d, &c) && d->np == n0);
  ok("cells: a block aligned to the middle", tbl_cell_format(d, P(1, 0), P(4, 0), 0, 1) && d->p[1]->pf.cell.valign == 1 && d->p[4]->pf.cell.valign == 1 &&
     d->p[0]->pf.cell.valign == 0 && d->p[5]->pf.cell.valign == 0 && d->p[3]->pf.cell.valign == 0);
  ok("cells: shaded", tbl_cell_format(d, P(2, 0), P(2, 0), 1, 0xFFF2CC) && d->p[2]->pf.cell.shade == 0xFFF2CC && d->p[1]->pf.cell.shade == COLOR_AUTO);
  tbl_merge(d, P(3, 0), P(5, 0));
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("cells: saved and read back", e && same_doc(d, e) && e->p[3]->pf.cell.span == 3);
  for (i = 0; e && i < e->np; i++) if (e->p[i]->pf.cell.valign != d->p[i]->pf.cell.valign) break;
  ok("cells: alignment read back", e && i == e->np);
  free(data); doc_free(e); doc_free(d);
}
/* the table of contents: made, its page numbers, updated, saved as Word's field and read back */
static void contents(void)
{
  Doc *d = doc_template(TPL_REPORT), *e;
  Layout *L;
  Pos at;
  int again = 0, ntoc = 0;
  u32 i;
  char err[200];
  unsigned lost;
  size_t n;
  u8 *data;
  at.para = 2; at.off = 0;                        /* after the title and subtitle */
  L = lay_new(d, &fs); lay_update(L);
  /* (WP2: the table is a TOC field, its first entry starting with the field's mark; the page numbers
     are placeholders for the pages of hidden _Toc bookmarks on the headings) */
  ok("contents: made", doc_toc(d, L, &at, &again) && !again && !strcmp(d->p[2]->t, "Contents") && toc_para(d, d->p[3]) &&
     (unsigned char)d->p[3]->t[0] == 0xF4 && !strncmp(d->p[3]->t + 4, "Summary\t", 8));
  lay_update(L);
  ok("contents: updated with the pages", doc_toc(d, L, NULL, &again) >= 0 && !again && !strcmp(d->p[3]->t + 4, "Summary\t" FIELD_PAGEREF) &&
     lay_bmark_page(L, doc_link_url(d, para_fmt_at(d->p[3], 12)->link) + 1) == 1);
  for (i = 0; i < d->np; i++) ntoc += toc_para(d, d->p[i]);
  ok("contents: an entry per heading (levels 1-3), indented by level", ntoc == 6 && d->p[6]->pf.left == 220 && d->p[3]->pf.left == 0 &&
     d->p[3]->pf.tabkind[0] == TAB_RIGHT && d->p[3]->pf.tablead[0] == LEAD_DOT);
  {
    Pos h;
    /* a new heading at the end, then update: one more entry, nothing duplicated */
    h = doc_end_pos(d);
    doc_insert(d, &h, "\nAppendix", 9, NULL);
    doc_apply_style(d, d->np - 1, d->np - 1, doc_style(d, "Heading1"));
    lay_update(L);
    doc_toc(d, L, NULL, &again);
    for (ntoc = 0, i = 0; i < d->np; i++) ntoc += toc_para(d, d->p[i]);
    ok("contents: updating adds the new heading", ntoc == 7 && !strcmp(d->p[2]->t, "Contents") && strcmp(d->p[1]->t, "Contents"));
  }
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("contents: saved as a TOC field and read back", e && same_doc(d, e) && toc_para(e, e->p[3]) && data && memmem(data, n, "word/document.xml", 17));
  {
    /* Remove Table of Contents: the entries and their title go in one step, and come back on undo */
    u32 np = d->np;
    Pos c;
    int left = 0;
    at.para = 0;
    ok("contents: removed", doc_toc_remove(d, &at) && d->np == np - 8 && at.para == 2);
    for (i = 0; i < d->np; i++) left += toc_para(d, d->p[i]) || !strcmp(d->p[i]->t, "Contents");
    ok("contents: nothing of it left, the headings kept", !left && doc_toc_remove(d, NULL) == 0 && doc_undo(d, &c) && d->np == np && !strcmp(d->p[2]->t, "Contents"));
  }
  free(data); doc_free(e); lay_free(L); doc_free(d);
}
/* tab stops: right, centred and decimal ones, leaders; read and written */
static void tab_stops(void)
{
  Doc *d = doc_new(), *e;
  Pos c = doc_start(d);
  ParaFmt pf;
  Layout *L;
  ParaLay *pl;
  Box *t1, *a1, *t2, *a2, *t3, *a3;
  char err[200];
  unsigned lost;
  size_t n;
  u8 *data;
  doc_insert(d, &c, "Chapter\tright\tmid\t12.75", 23, NULL);
  memset(&pf, 0, sizeof pf);
  pf.tabs[0] = 2000; pf.tabkind[0] = TAB_RIGHT; pf.tablead[0] = LEAD_DOT;
  pf.tabs[1] = 4000; pf.tabkind[1] = TAB_CENTER;
  pf.tabs[2] = 6000; pf.tabkind[2] = TAB_DECIMAL; pf.tablead[2] = LEAD_HYPHEN;
  doc_para_format(d, 0, 0, PM_TABS, &pf);
  L = lay_new(d, &fs);
  lay_update(L);
  pl = &L->pl[0];
  t1 = &pl->boxes[1]; a1 = &pl->boxes[2]; t2 = &pl->boxes[3]; a2 = &pl->boxes[4]; t3 = &pl->boxes[5]; a3 = &pl->boxes[6];
  ok("tabs: a right stop ends the text there, with its leader", t1->kind == BX_TAB && t1->nspace == LEAD_DOT && abs((int)(a1->x + a1->w - 2000)) <= 2);
  ok("tabs: a centred stop centres it", t2->kind == BX_TAB && abs((int)(a2->x + a2->w / 2 - 4000)) <= 2);
  ok("tabs: a decimal stop lines up the point", t3->kind == BX_TAB && t3->nspace == LEAD_HYPHEN && a3->x < 6000 && a3->x + a3->w > 6000 && a3->x > 5000);
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("tabs: kinds and leaders saved and read back", e && e->p[0]->pf.tabs[1] == 4000 && e->p[0]->pf.tabkind[0] == TAB_RIGHT && e->p[0]->pf.tablead[0] == LEAD_DOT &&
     e->p[0]->pf.tabkind[1] == TAB_CENTER && e->p[0]->pf.tabkind[2] == TAB_DECIMAL && e->p[0]->pf.tablead[2] == LEAD_HYPHEN);
  {
    size_t pn;
    u8 *pdf = pdf_export(L, &pn);
    ok("tabs: the PDF has the leader dots", pdf && pn > 100);
    free(pdf);
  }
  free(data); doc_free(e); lay_free(L); doc_free(d);
}
/* 0.4's formatting: case, grow and shrink, clear, sort, highlight */
static void formatting2(void)
{
  Doc *d = doc_new(), *e;
  Pos c = doc_start(d);
  CharFmt f;
  char err[200];
  unsigned lost;
  size_t n;
  u8 *data;
  { static const char t0[] = "the \xc3\xa9t\xc3\xa9 of don't. another one"; doc_insert(d, &c, t0, (u32)strlen(t0), NULL); }
  doc_change_case(d, P(0, 0), P(0, d->p[0]->len), CASE_TITLE);
  ok("case: capitalise each word (Latin-1 too, not after an apostrophe)", !strcmp(d->p[0]->t, "The \xc3\x89t\xc3\xa9 Of Don't. Another One"));
  doc_change_case(d, P(0, 0), P(0, d->p[0]->len), CASE_UPPER);
  ok("case: upper", !strcmp(d->p[0]->t, "THE \xc3\x89T\xc3\x89 OF DON'T. ANOTHER ONE"));
  doc_change_case(d, P(0, 0), P(0, d->p[0]->len), CASE_SENTENCE);
  ok("case: sentence", !strcmp(d->p[0]->t, "The \xc3\xa9t\xc3\xa9 of don't. Another one"));
  doc_change_case(d, P(0, 0), P(0, 3), CASE_TOGGLE);
  ok("case: toggle, and undo", !strncmp(d->p[0]->t, "tHE ", 4) && doc_undo(d, &c) && !strncmp(d->p[0]->t, "The ", 4));
  doc_grow(d, P(0, 0), P(0, 3), 1);
  ok("grow: 12 pt to 14 pt", para_fmt_at(d->p[0], 0)->size == 28 && para_fmt_at(d->p[0], 4)->size == 24);
  doc_grow(d, P(0, 0), P(0, 3), -1); doc_grow(d, P(0, 0), P(0, 3), -1);
  ok("shrink: to 11 pt", para_fmt_at(d->p[0], 0)->size == 22);
  f = *para_fmt_at(d->p[0], 0);
  f.hl = HL_ON | 0xFFFF00;
  doc_format(d, P(0, 4), P(0, 9), CM_HL | CM_BOLD, &f);
  f.shade = HL_ON | 0x123456;                 /* (WP9: character shading is its own, apart from the highlight) */
  doc_format(d, P(0, 10), P(0, 12), CM_SHADE, &f);
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("highlight: a named one and shading, saved and read back", e && para_fmt_at(e->p[0], 5)->hl == (HL_ON | 0xFFFF00) &&
     para_fmt_at(e->p[0], 10)->shade == (HL_ON | 0x123456) && para_fmt_at(e->p[0], 10)->hl == 0 && para_fmt_at(e->p[0], 13)->hl == 0 && same_doc(d, e));
  free(data); doc_free(e);
  doc_clear_format(d, P(0, 0), P(0, d->p[0]->len));
  ok("clear formatting: back to the style's", d->p[0]->nr == 1 && d->p[0]->r[0].f.size == 24 && !d->p[0]->r[0].f.hl && !(d->p[0]->r[0].f.flags & CF_BOLD));
  c = P(0, d->p[0]->len);
  doc_insert(d, &c, "\npear\nApple\nbanana", 18, NULL);
  ok("sort: A to Z ignoring case", doc_sort(d, 1, 3) && !strcmp(d->p[1]->t, "Apple") && !strcmp(d->p[2]->t, "banana") && !strcmp(d->p[3]->t, "pear") && d->np == 4);
  ok("sort: undone", doc_undo(d, &c) && !strcmp(d->p[1]->t, "pear"));
  doc_free(d);
}
static void formatting(void)
{
  Doc *d = doc_new();
  Pos c = doc_start(d);
  CharFmt f;
  doc_insert(d, &c, "one two three", 13, NULL);
  doc_toggle(d, P(0, 4), P(0, 7), CF_BOLD);
  ok("bold: a word", d->p[0]->nr == 3 && (d->p[0]->r[1].f.flags & CF_BOLD) && d->p[0]->r[1].end == 7 && runs_sane(d));
  doc_toggle(d, P(0, 0), P(0, 13), CF_BOLD);
  ok("bold: a mixed range becomes all bold", d->p[0]->nr == 1 && (d->p[0]->r[0].f.flags & CF_BOLD));
  doc_toggle(d, P(0, 0), P(0, 13), CF_BOLD);
  ok("bold: all bold toggles off", d->p[0]->nr == 1 && !(d->p[0]->r[0].f.flags & CF_BOLD));
  f = d->p[0]->r[0].f; f.flags |= CF_ITALIC;
  c = P(0, 3);
  doc_insert(d, &c, "X", 1, &f);
  ok("insert with a format", d->p[0]->nr == 3 && (d->p[0]->r[1].f.flags & CF_ITALIC) && text_is(d, "oneX two three") && runs_sane(d));
  c = P(0, 4);
  doc_insert(d, &c, "Y", 1, NULL);
  ok("typing continues the format before", d->p[0]->nr == 3 && d->p[0]->r[1].end == 5 && runs_sane(d));
  doc_delete(d, P(0, 3), P(0, 5));
  ok("deleting the italic run merges", d->p[0]->nr == 1 && text_is(d, "one two three"));
  doc_free(d);
}
static void undo(void)
{
  Doc *d = doc_new();
  Pos c = doc_start(d);
  const char *s = "alpha beta gamma";
  int i, n = 0;
  for (i = 0; s[i]; i++) doc_insert(d, &c, s + i, 1, NULL);
  doc_undo(d, &c);
  ok("undo: typing goes back a word", text_is(d, "alpha beta "));
  doc_redo(d, &c);
  ok("redo: it comes back", text_is(d, "alpha beta gamma") && c.off == 16);
  doc_toggle(d, P(0, 0), P(0, 5), CF_BOLD);
  doc_insert(d, &c, "\nnext", 5, NULL);
  doc_delete(d, P(0, 2), P(1, 2));
  ok("before undo", text_is(d, "alxt"));
  doc_undo(d, &c);
  ok("undo: delete across", text_is(d, "alpha beta gamma\nnext") && d->np == 2);
  doc_undo(d, &c);
  ok("undo: paste with a newline", text_is(d, "alpha beta gamma") && d->np == 1);
  doc_undo(d, &c);
  ok("undo: bold", d->p[0]->nr == 1 && !(d->p[0]->r[0].f.flags & CF_BOLD));
  while (doc_undo(d, &c)) n++;
  ok("undo: all the way", text_is(d, "") && n == 3);
  while (doc_redo(d, &c)) ;
  ok("redo: all the way", text_is(d, "alxt") && runs_sane(d));
  doc_begin(d, c);
  c = doc_end_pos(d);
  doc_insert(d, &c, " one", 4, NULL);
  doc_insert(d, &c, " two", 4, NULL);
  doc_end(d, c);
  doc_undo(d, &c);
  ok("undo: a group is one step", text_is(d, "alxt"));
  doc_free(d);
}
static void styles(void)
{
  Doc *d = doc_new();
  Pos c = doc_start(d);
  int h1 = doc_style(d, "Heading1");
  doc_insert(d, &c, "Title here", 10, NULL);
  doc_toggle(d, P(0, 0), P(0, 5), CF_ITALIC);
  doc_apply_style(d, 0, 0, h1);
  ok("style: heading formatting", d->p[0]->pf.style == h1 && (d->p[0]->r[0].f.flags & CF_BOLD) && d->p[0]->r[0].f.size == 32);
  ok("style: direct italic kept", (d->p[0]->r[0].f.flags & CF_ITALIC) && !(d->p[0]->r[1].f.flags & CF_ITALIC));
  c = doc_end_pos(d);
  doc_insert(d, &c, "\n", 1, NULL);
  ok("style: Return after a heading gives Normal", d->np == 2 && d->p[1]->pf.style == 0 && d->p[1]->r[0].f.size == 24 && !(d->p[1]->r[0].f.flags & CF_BOLD));
  doc_free(d);
}
static void utf8(void)
{
  Doc *d = doc_new();
  Pos c = doc_start(d);
  char out[64];
  const char *s = "caf\xC3\xA9 \xE2\x80\x9Cquoted\xE2\x80\x9D";
  doc_insert(d, &c, s, (u32)strlen(s), NULL);
  c = pos_left(d, doc_end_pos(d));
  ok("utf8: left steps over a 3-byte character", c.off == 15);
  c = P(0, 5);
  c = pos_left(d, c);
  ok("utf8: and a 2-byte one", c.off == 3);
  utf8_to_latin1(d->p[0]->t, d->p[0]->len, out, sizeof out);
  ok("utf8: Latin-1 stand-ins", strcmp(out, "caf\xE9 \"quoted\"") == 0);
  ok("utf8: words", doc_count_words(d) == 2);
  doc_free(d);
}
static void hostile(void)
{
  Doc *d = doc_new();
  Pos c = P(99, 99);
  ok("hostile: insert at a bad position fails", !doc_insert(d, &c, "x", 1, NULL));
  ok("hostile: delete clamps", !doc_delete(d, P(5, 5), P(9, 9)));
  c = doc_start(d);
  doc_insert(d, &c, "a\x01\x02\tb\r\nc\x7f", 9, NULL);
  ok("hostile: control characters dropped, CRLF one break", text_is(d, "a\tb\nc") && d->np == 2);
  ok("hostile: undo of nothing", (doc_clear_undo(d), !doc_undo(d, &c)));
  doc_free(d);
}

static void fonts(void)
{
  FaceSize *z, *zb, *zc;
  ok("fonts: the list loads", fonts_init(&fs, "fonts") == 20);
  z = fonts_size(&fs, "Times New Roman", 0, 0, 24);
  ok("fonts: Times New Roman is measured with Liberation Serif", z && !strcmp(z->face->family, "Liberation Serif") && !z->face->bold);
  ok("fonts: H at 12 pt", z && z->fine['H'] == (1479UL * 24 * 10 * 16 + 1024) / 2048);
  ok("fonts: W, space, e acute", z && z->fine['W'] == (1933UL * 3840 + 1024) / 2048 && z->fine[' '] == (512UL * 3840 + 1024) / 2048 && z->fine[0xE9] == z->fine['a']);
  ok("fonts: a curly quote through the cmap", z && fonts_fine(z, 0x201C) == (909UL * 3840 + 1024) / 2048);
  ok("fonts: single line height 13.8 pt, as Word", z && z->height == 276);
  zb = fonts_size(&fs, "Calibri", 1, 0, 22);
  ok("fonts: Calibri bold is Carlito Bold", zb && !strcmp(zb->face->family, "Carlito") && zb->face->bold && zb->fine['W'] == (1856UL * 22 * 160 + 1024) / 2048);
  zc = fonts_size(&fs, "Some Unknown Serif", 0, 1, 24);
  ok("fonts: an unknown serif family looks like a serif", zc && !strcmp(zc->face->family, "Liberation Serif") && zc->face->italic);
  ok("fonts: sizes are cached", fonts_size(&fs, "Times New Roman", 0, 0, 24) == z);
  {
    char fam[48];
    int b, it, n;
    const char *names[16];
    ok("fonts: a file's family and style", fonts_probe("fonts/LiberationSerif-BoldItalic.ttf", 0, fam, sizeof fam, &b, &it) && !strcmp(fam, "Liberation Serif") && b && it);
    ok("fonts: another, regular", fonts_probe("fonts/Carlito-Regular.ttf", 0, fam, sizeof fam, &b, &it) && !strcmp(fam, "Carlito") && !b && !it);
    ok("fonts: not a font", !fonts_probe("dict/README", 0, fam, sizeof fam, &b, &it) && !fonts_probe("no/such.ttf", 0, fam, sizeof fam, &b, &it));
    n = fonts_families(&fs, names, 16);
    ok("fonts: the families, once each, sorted", n == 5 && !strcmp(names[0], "Caladea") && !strcmp(names[1], "Carlito") && !strcmp(names[4], "Liberation Serif"));
  }
}
/* glyphs as pixels (ttraster.c) */
static void raster(void)
{
  TTRaster r;
  TTBitmap o, e, sp, l;
  Face *f = fonts_face(&fs, "Liberation Sans", 0, 0);
  ok("raster: a font opens", f && ttr_open(&r, f->path, 0) && r.upm == 2048);
  ok("raster: O at 40 pixels", f && ttr_glyph(&r, f->latin_glyph['O'], 40 * 64, &o) && o.w >= 26 && o.w <= 32 && o.top >= 27 && o.top <= 29 && o.h >= 28 && o.h <= 31);
  if (o.cov) {
    ok("raster: O has a hole, and its sides are filled", o.cov[(o.h / 2) * o.w + o.w / 2] == 0 && o.cov[(o.h / 2) * o.w + 1] == 255 && o.cov[(o.h / 2) * o.w + o.w - 2] == 255);
    ok("raster: O starts just right of the pen", o.left >= 1 && o.left <= 3);
  }
  ok("raster: e acute (a composite) reaches above the x-height", f && ttr_glyph(&r, f->latin_glyph[0xE9], 40 * 64, &e) && e.top >= 28 && e.top <= 34 && e.w >= 18);
  ok("raster: a space has nothing to draw", f && ttr_glyph(&r, f->latin_glyph[' '], 40 * 64, &sp) && sp.w == 0 && !sp.cov);
  ok("raster: l at 12 pixels is a 1 pixel stem", f && ttr_glyph(&r, f->latin_glyph['l'], 12 * 64, &l) && l.w >= 1 && l.w <= 2 && l.h >= 8 && l.h <= 10);
  ok("raster: a glyph beyond the font fails", !ttr_glyph(&r, 60000, 40 * 64, &sp));
  free(o.cov); free(e.cov); free(l.cov);
  ttr_close(&r);
}
/* hinting: zones and stems onto whole pixels */
static int partial(const TTBitmap *b)
{
  int i, n = 0;
  for (i = 0; i < b->w * b->h; i++) n += b->cov[i] > 0 && b->cov[i] < 255;
  return n;
}
static void hinting(void)
{
  TTRaster r;
  TTBitmap b;
  Face *f = fonts_face(&fs, "Liberation Sans", 0, 0), *c = fonts_face(&fs, "Caladea", 0, 0);
  int px, crisp = 1, soft = 0, tops = 1, row = 0, j, i;
  if (!f || !c || !ttr_open(&r, f->path, 0)) { ok("hinting: a font", 0); return; }
  for (px = 9; px <= 20; px++) if (ttr_glyph(&r, f->latin_glyph['H'], px * 64, &b)) { soft += partial(&b) > 0; free(b.cov); }
  ttr_hint(&r, f->latin_glyph);
  ok("hinting: the zones measured (x-height 1082, capitals 1409, baseline, descenders)", r.hint && r.nz >= 4 && r.zf[0] == 1082 && r.zf[1] == 1409);
  for (px = 9; px <= 20; px++) if (ttr_glyph(&r, f->latin_glyph['H'], px * 64, &b)) { crisp &= partial(&b) == 0; free(b.cov); }
  ok("hinting: H on whole pixels from 9 to 20 pixels (unhinted, it isn't)", crisp && soft > 0);
  for (px = 9; px <= 16; px++)
    if (ttr_glyph(&r, f->latin_glyph['o'], px * 64, &b)) { tops &= b.top == (1082 * px + 1024) / 2048; free(b.cov); }
  ok("hinting: o's top on the x-height, its overshoot flattened", tops);
  ttr_close(&r);
  if (ttr_open(&r, c->path, 0)) {
    ttr_hint(&r, c->latin_glyph);
    if (ttr_glyph(&r, c->latin_glyph['e'], 14 * 64, &b) && b.cov) {
      for (j = 0; j < b.h && !row; j++) {
        int full = 1;
        for (i = 1; i < b.w - 1; i++) full &= b.cov[j * b.w + i] == 255;
        row = full && j > 1 && j < b.h - 2;
      }
      free(b.cov);
    }
    ok("hinting: the bar of e a whole row", row);
    ttr_close(&r);
  }
  {
    /* a stroke thinner than a pixel, between two: one of them set */
    static u8 cov[8] = { 0, 90, 90, 0, 0, 30, 0, 0 };
    TTBitmap d;
    d.w = 8; d.h = 1; d.left = d.top = 0; d.cov = cov;
    ttr_dropout(&d, 128);
    ok("hinting: dropout keeps a thin stroke, not a faint speck", (cov[1] == 255) != (cov[2] == 255) && cov[5] == 30);
  }
}
/* collections (.ttc) and OpenType with CFF outlines (.otf) */
static FontSet fs2;
static void more_fonts(void)
{
  char fam[48];
  int b, it, i;
  Face *f;
  TTRaster r;
  TTBitmap o, e, h;
  u8 *one;
  size_t n = 0;
  ok("ttc: two faces in it", fonts_count("tests/data/pair.ttc") == 2 && fonts_count("fonts/Carlito-Regular.ttf") == 1 && fonts_count("dict/README") == 0);
  ok("ttc: the second face's family and style", fonts_probe("tests/data/pair.ttc", 1, fam, sizeof fam, &b, &it) && !strcmp(fam, "Liberation Sans") && b && !it);
  ok("ttc: the first, and no third", fonts_probe("tests/data/pair.ttc", 0, fam, sizeof fam, &b, &it) && !b && !fonts_probe("tests/data/pair.ttc", 2, fam, sizeof fam, &b, &it));
  memset(&fs2, 0, sizeof fs2);
  i = fonts_add(&fs2, "tests/data/pair.ttc", "Liberation Sans", 1, 0);
  fs2.faces[i].index = 1;
  f = &fs2.faces[i];
  ok("ttc: a face loads from it", ttf_load(f) && !f->cff && f->upm == 2048 && f->adv[f->latin_glyph['W']] == 1933);
  ok("ttc: its glyphs as pixels", ttr_open(&r, f->path, f->index) && ttr_glyph(&r, f->latin_glyph['O'], 40 * 64, &o) && o.w >= 26 && o.w <= 33 && o.top >= 27 && o.top <= 29);
  free(o.cov); ttr_close(&r);
  one = fonts_face_file(f, &n);
  ok("ttc: the face taken out as a font of its own", one && n > 1000 && file_write("/tmp/quill_face.ttf", one, n) &&
     fonts_probe("/tmp/quill_face.ttf", 0, fam, sizeof fam, &b, &it) && !strcmp(fam, "Liberation Sans") && b && fonts_count("/tmp/quill_face.ttf") == 1);
  free(one);
  /* OpenType CFF */
  ok("otf: family and style", fonts_probe("tests/data/inter-sub.otf", 0, fam, sizeof fam, &b, &it) && !strcmp(fam, "Inter") && !b && !it);
  i = fonts_add(&fs2, "tests/data/inter-sub.otf", "Inter", 0, 0);
  f = &fs2.faces[i];
  ok("otf: metrics load", ttf_load(f) && f->cff && f->upm == 2048 && f->adv[f->latin_glyph['O']] == 1566 && fonts_embeddable(f));
  ok("otf: the outlines open", ttr_open(&r, f->path, 0) && r.cff);
  {
    /* the licence: "restricted" isn't embedded; with "print and preview" too it is; bitmaps only isn't */
    u16 was = f->fstype;
    int a, b2, c2;
    f->fstype = 0x0002; a = fonts_embeddable(f);
    f->fstype = 0x0006; b2 = fonts_embeddable(f);
    f->fstype = 0x0208; c2 = fonts_embeddable(f);
    f->fstype = was;
    ok("fonts: what a licence lets a PDF embed", !a && b2 && !c2);
  }
  /* (fontTools: O is 122..1444 by -20..1510 of 2048, H 180..1342 by 0..1490, e acute up to 1558) */
  ok("otf: O at 40 pixels", ttr_glyph(&r, f->latin_glyph['O'], 40 * 64, &o) && o.w >= 26 && o.w <= 28 && o.top >= 29 && o.top <= 30 && o.left >= 2 && o.left <= 3);
  if (o.cov) ok("otf: O has a hole, and filled sides", o.cov[(o.h / 2) * o.w + o.w / 2] == 0 && o.cov[(o.h / 2) * o.w + 1] == 255 && o.cov[(o.h / 2) * o.w + o.w - 2] == 255);
  ok("otf: H stands on the baseline", ttr_glyph(&r, f->latin_glyph['H'], 40 * 64, &h) && h.top == 30 && h.h == 30 && h.w >= 23 && h.w <= 24);
  ok("otf: e acute reaches up", ttr_glyph(&r, f->latin_glyph[0xE9], 40 * 64, &e) && e.top >= 31 && e.top <= 32);
  free(o.cov); free(h.cov); free(e.cov); ttr_close(&r);
  {
    /* a PDF with it: a whole OpenType font embedded (PDF 1.6) */
    Doc *d = doc_new();
    Pos c = doc_start(d);
    Layout *L;
    u8 *pdf;
    doc_insert(d, &c, "Inter in a PDF", 14, NULL);
    {
      CharFmt cf;
      memset(&cf, 0, sizeof cf);
      cf.font = (u8)doc_font(d, "Inter");
      doc_format(d, P(0, 0), P(0, 14), CM_FONT, &cf);
    }
    L = lay_new(d, &fs2); lay_update(L);
    pdf = pdf_export(L, &n);
    ok("otf: a PDF embeds it as OpenType", pdf && !memcmp(pdf, "%PDF-1.6", 8) && memmem(pdf, n, "/FontFile3", 10) && memmem(pdf, n, "/Subtype /OpenType", 18) && memmem(pdf, n, "/Subtype /CIDFontType0 /BaseFont /", 34) && memmem(pdf, n, "+Inter", 6));
    free(pdf); lay_free(L); doc_free(d);
  }
  {
    /* bold and italic of a family without such faces: the regular face, stroked and slanted */
    Doc *d = doc_new();
    Pos c = doc_start(d);
    Layout *L;
    u8 *pdf, *page = NULL;
    CharFmt cf;
    const u8 *q;
    long got = 0;
    doc_insert(d, &c, "Bold and slanted", 16, NULL);
    memset(&cf, 0, sizeof cf);
    cf.font = (u8)doc_font(d, "Inter"); cf.flags = CF_BOLD | CF_ITALIC;
    doc_format(d, P(0, 0), P(0, 16), CM_FONT | CM_BOLD | CM_ITALIC, &cf);
    L = lay_new(d, &fs2); lay_update(L);
    pdf = pdf_export(L, &n);
    /* the page's content: the last stream before the end */
    for (q = pdf; pdf && q && (q = memmem(q, n - (size_t)(q - pdf), "/FlateDecode /Length ", 21)); q++) {
      long len = atol((const char *)q + 21);
      const u8 *st = memmem(q, n - (size_t)(q - pdf), "stream\n", 7);
      free(page);
      page = (u8 *)malloc(65536);
      got = st && page && len > 6 ? zinflate(st + 7 + 2, (size_t)len - 6, page, 65535) : -1;
    }
    if (page && got > 0) page[got] = 0;
    ok("pdf: bold made by stroking, italic by slanting", page && got > 0 && strstr((char *)page, "2 Tr") && strstr((char *)page, "1 0 0.21 1 ") && strstr((char *)page, "q ") && strstr((char *)page, "Q\n"));
    free(page); free(pdf); lay_free(L); doc_free(d);
  }
  {
    /* CFF2: a variable font (weight 400 to 700) drawn at its default design, and a plain one */
    TTBitmap hv, ov;
    i = fonts_add(&fs2, "tests/data/inter-var.otf", "Inter Var", 0, 0);
    f = &fs2.faces[i];
    ok("cff2: family, metrics", fonts_probe("tests/data/inter-var.otf", 0, fam, sizeof fam, &b, &it) && !strcmp(fam, "Inter") && ttf_load(f) && f->cff == 2 && !fonts_embeddable(f));
    ok("cff2: the outlines open", ttr_open(&r, f->path, 0) && r.cff2 && r.nvdata >= 1 && r.vregions[0] >= 1);
    ok("cff2: H at its default weight (as Inter Regular's)", ttr_glyph(&r, f->latin_glyph['H'], 40 * 64, &hv) && hv.top == 30 && hv.h == 30 && hv.w >= 23 && hv.w <= 24);
    ok("cff2: O with its hole", ttr_glyph(&r, f->latin_glyph['O'], 40 * 64, &ov) && ov.w >= 26 && ov.w <= 28 && ov.cov[(ov.h / 2) * ov.w + ov.w / 2] == 0);
    free(hv.cov); free(ov.cov); ttr_close(&r);
    i = fonts_add(&fs2, "tests/data/inter-cff2.otf", "Inter Two", 0, 0);
    f = &fs2.faces[i];
    ok("cff2: a font without variations", ttf_load(f) && f->cff == 2 && ttr_open(&r, f->path, 0) && ttr_glyph(&r, f->latin_glyph['H'], 40 * 64, &hv) && hv.top == 30 && hv.w >= 23 && hv.w <= 24);
    free(hv.cov); ttr_close(&r);
  }
  fonts_free(&fs2);
}
static Doc *filled(int nparas, const char *text)
{
  Doc *d = doc_new();
  Pos c = doc_start(d);
  int i;
  for (i = 0; i < nparas; i++) {
    if (i) doc_insert(d, &c, "\n", 1, NULL);
    doc_insert(d, &c, text, (u32)strlen(text), NULL);
  }
  doc_clear_undo(d);
  return d;
}
static const char *lorem = "The Amiga was a family of personal computers with custom chips for graphics and sound, "
  "a favourite of artists, musicians and game makers through the late eighties and the nineties.";
static void layout(void)
{
  Doc *d = filled(1, lorem);
  Layout *L = lay_new(d, &fs);
  i32 avail = d->page.width - d->page.left - d->page.right;
  u32 k, page; i32 x, y, h;
  Pos c, back;
  ok("layout: updates", lay_update(L) && L->npl == 1 && L->npages == 1);
  ok("layout: the paragraph wraps", L->pl[0].nlines >= 2);
  for (k = 0; k < L->pl[0].nlines; k++) if (L->pl[0].lines[k].width > avail) break;
  ok("layout: every line fits", k == L->pl[0].nlines);
  ok("layout: lines cover the text", L->pl[0].lines[0].start == 0 && L->pl[0].lines[L->pl[0].nlines - 1].end == d->p[0]->len);
  ok("layout: a line breaks after a space", d->p[0]->t[L->pl[0].lines[1].start - 1] == ' ');
  ok("layout: line height", L->pl[0].lines[0].height == 276);
  /* justify: full lines are exactly the width; the last line is not stretched */
  { ParaFmt pf = d->p[0]->pf; pf.align = AL_JUSTIFY; doc_para_format(d, 0, 0, PM_ALIGN, &pf); }
  lay_update(L);
  ok("layout: justified lines fill the width", L->pl[0].lines[0].width == avail && L->pl[0].lines[L->pl[0].nlines - 1].width < avail);
  { Line *l0 = &L->pl[0].lines[0]; Box *lb = &L->pl[0].boxes[l0->box0 + l0->nbox - 2];
    ok("layout: justified: the last word ends at the margin", lb->x + lb->w == avail || L->pl[0].boxes[l0->box0 + l0->nbox - 1].x + L->pl[0].boxes[l0->box0 + l0->nbox - 1].w == avail); }
  { ParaFmt pf = d->p[0]->pf; pf.align = AL_CENTER; doc_para_format(d, 0, 0, PM_ALIGN, &pf); }
  lay_update(L);
  { Line *last = &L->pl[0].lines[L->pl[0].nlines - 1];
    ok("layout: centred", last->left == (avail - last->width) / 2); }
  /* caret and hit testing agree */
  c.para = 0; c.off = 40;
  lay_caret(L, c, &page, &x, &y, &h);
  back = lay_hit(L, page, x + 1, y + h / 2);
  ok("layout: caret and hit test round trip", back.para == 0 && back.off == 40 && h == 276);
  c = lay_updown(L, c, x, 1);
  ok("layout: down a line", c.off > 40 && c.off < d->p[0]->len + 1);
  /* typing re-lays out one paragraph */
  doc_free(d); lay_free(L);
  d = filled(200, lorem);
  L = lay_new(d, &fs);
  lay_update(L);
  ok("layout: 200 paragraphs paginate", L->npages >= 8 && L->laid == 200);
  c.para = 150; c.off = 3;
  doc_insert(d, &c, "x", 1, NULL);
  lay_update(L);
  ok("layout: typing re-lays out only its paragraph", L->laid == 1);
  for (k = 1; k < L->npages; k++) if (L->placed[L->page_first[k]].y != d->page.top) break;
  ok("layout: each page starts at the top margin", k == L->npages);
  { u32 last = L->nplaced - 1; Placed *pc = &L->placed[last];
    ok("layout: nothing below the bottom margin", pc->y + L->pl[pc->para].lines[pc->line].height <= d->page.height - d->page.bottom); }
  /* widows and orphans: no page starts with a paragraph's last line of three or more, nor ends with a first line */
  for (k = 1; k < L->npages; k++) {
    Placed *f = &L->placed[L->page_first[k]], *pv = &L->placed[L->page_first[k] - 1];
    ParaLay *pl = &L->pl[f->para];
    if (pl->nlines >= 3 && f->line == pl->nlines - 1) break;
    if (L->pl[pv->para].nlines >= 2 && pv->line == 0) break;
  }
  ok("layout: no widows or orphans", k == L->npages);
  lay_free(L); doc_free(d);
  /* a heading kept with its paragraph */
  d = filled(60, lorem);
  for (k = 0; k < 60; k += 5) doc_apply_style(d, k, k, doc_style(d, "Heading1"));
  L = lay_new(d, &fs);
  lay_update(L);
  for (k = 0; k + 1 < L->npages; k++) {
    Placed *lastp = &L->placed[L->page_first[k + 1] - 1];
    if (d->p[lastp->para]->pf.keepnext) break;
  }
  ok("layout: no page ends with a heading", k + 1 == L->npages);
  lay_free(L); doc_free(d);
}

static int has_text(Doc *d, u32 pi, const char *want) { return pi < d->np && strstr(d->p[pi]->t, want) != NULL; }
static int flags_at(Doc *d, u32 pi, const char *word)
{
  const char *at = pi < d->np ? strstr(d->p[pi]->t, word) : NULL;
  return at ? para_fmt_at(d->p[pi], (u32)(at - d->p[pi]->t))->flags : -1;
}
static void docx_read(const char *file, int lo)
{
  char err[200], what[200];
  unsigned lost;
  Doc *d = doc_load_file(file, &lost, err, sizeof err);
  int h1, lb, ln, k, cafe;
  sprintf(what, "docx %s: loads", file);
  ok(what, d != NULL);
  if (!d) { printf("  %s\n", err); return; }
  h1 = doc_style_by_name(d, lo ? "Heading 1" : "heading 1");
  ok("docx: Letter paper, 1.25 inch side margins", d->page.width == 12240 && d->page.height == 15840 && d->page.left == 1800);
  ok("docx: title and author", !strcmp(d->title, "Quill test document") && !strcmp(d->author, "Quill tests"));
  ok("docx: heading style, bold, theme font", h1 > 0 && d->p[1]->pf.style == h1 && (d->p[1]->r[0].f.flags & CF_BOLD) && !strcmp(d->fonts[d->p[1]->r[0].f.font], "Calibri"));
  ok("docx: body text in the minor theme font", !strcmp(d->fonts[d->p[2]->r[0].f.font], "Cambria") && d->p[2]->r[0].f.size == 22);
  ok("docx: bold, italic, underline runs", flags_at(d, 2, "bold") == CF_BOLD && flags_at(d, 2, "italic") == CF_ITALIC && flags_at(d, 2, "underlined") == CF_UNDERLINE);
  { const char *r = strstr(d->p[2]->t, "red 14"); CharFmt *f = r ? para_fmt_at(d->p[2], (u32)(r - d->p[2]->t)) : NULL;
    ok("docx: colour and size", f && f->color == 0xC01010 && f->size == 28); }
  cafe = has_text(d, 2, "Caf\xC3\xA9 \xE2\x80\x9Cquoted\xE2\x80\x9D \xE2\x80\x93 dash\tafter");
  ok("docx: accents, curly quotes, dash, tab", cafe);
  ok("docx: centred and justified", d->p[3]->pf.align == AL_CENTER && d->p[4]->pf.align == AL_JUSTIFY);
  for (k = 0; k < (int)d->np && !has_text(d, (u32)k, "First bullet"); k++) ;
  lb = k;
  ok("docx: a bulleted list", lb < (int)d->np && d->p[lb]->pf.list && d->lists[d->p[lb]->pf.list - 1].lvl[0].fmt == NF_BULLET &&
     d->p[lb + 1]->pf.list == d->p[lb]->pf.list && d->p[lb]->pf.left == 360 && d->p[lb]->pf.first == -360);
  ln = lb + 2;
  ok("docx: a numbered list", ln < (int)d->np && d->p[ln]->pf.list && d->lists[d->p[ln]->pf.list - 1].lvl[0].fmt == NF_DECIMAL &&
     !strcmp(d->lists[d->p[ln]->pf.list - 1].lvl[0].text, "%1."));
  for (k = 0; k < (int)d->np && !has_text(d, (u32)k, "r0c0"); k++) ;
  ok("docx: a table, cell by cell", k + 5 < (int)d->np && in_table(&d->p[k]->pf) && d->p[k]->pf.cell.col == 0 && has_text(d, (u32)k + 1, "r0c1") &&
     d->p[k + 1]->pf.cell.col == 1 && d->p[k + 5]->pf.cell.row == 1 && d->p[k + 5]->pf.cell.col == 2 && has_text(d, (u32)k + 5, "r1c2") && !(lost & LOST_TABLES));
  { Table *t = k < (int)d->np ? doc_table(d, &d->p[k]->pf) : NULL;
    ok("docx: the table's grid", t && t->ncols == 3 && t->grid[0] == 2880 && t->grid[2] == 2880 && t->marl == 108);
    ok("docx: the paragraph after the table is outside it", k + 6 < (int)d->np && !in_table(&d->p[k + 6]->pf)); }
  for (k = 0; k < (int)d->np && !has_text(d, (u32)k, "After the break"); k++) ;
  ok("docx: a page break", k < (int)d->np && (d->p[k]->pf.pagebreak || d->p[k - 1]->pf.pagebreak));
  ok("docx: the quote style", has_text(d, d->np - 1, "Last paragraph.") && (d->p[d->np - 1]->r[0].f.flags & CF_ITALIC));
  doc_free(d);
}
/* save and read back: the same text, formatting and paragraph formatting */
int same_doc(Doc *a, Doc *b)
{
  u32 i, k;
  if (a->np != b->np) { printf("  paragraphs %lu vs %lu\n", (unsigned long)a->np, (unsigned long)b->np); return 0; }
  for (i = 0; i < a->np; i++) {
    Para *p = a->p[i], *q = b->p[i];
    if (strcmp(p->t, q->t)) { printf("  paragraph %lu text differs\n", (unsigned long)i); return 0; }
    if (p->pf.align != q->pf.align || p->pf.left != q->pf.left || p->pf.first != q->pf.first || p->pf.before != q->pf.before ||
        p->pf.after != q->pf.after || p->pf.line != q->pf.line || !!p->pf.list != !!q->pf.list || p->pf.pagebreak != q->pf.pagebreak ||
        p->pf.colbreak != q->pf.colbreak || p->pf.dropcap != q->pf.dropcap || p->pf.dropmargin != q->pf.dropmargin ||
        strcmp(a->styles[p->pf.style].name, b->styles[q->pf.style].name)) { printf("  paragraph %lu format differs\n", (unsigned long)i); return 0; }
    if (!!p->pf.cell.table != !!q->pf.cell.table || p->pf.cell.row != q->pf.cell.row || p->pf.cell.col != q->pf.cell.col ||
        p->pf.cell.span != q->pf.cell.span || p->pf.cell.vmerge != q->pf.cell.vmerge || p->pf.cell.borders != q->pf.cell.borders ||
        p->pf.cell.shade != q->pf.cell.shade || p->pf.cell.rowh != q->pf.cell.rowh || !!p->pf.outer.table != !!q->pf.outer.table ||
        p->pf.outer.row != q->pf.outer.row || p->pf.outer.col != q->pf.outer.col || p->pf.outer.span != q->pf.outer.span) { printf("  paragraph %lu cell differs: b %d/%d shade %lx/%lx span %d/%d vm %d/%d\n", (unsigned long)i, p->pf.cell.borders, q->pf.cell.borders, (unsigned long)p->pf.cell.shade, (unsigned long)q->pf.cell.shade, p->pf.cell.span, q->pf.cell.span, p->pf.cell.vmerge, q->pf.cell.vmerge); return 0; }
    {
      const SectFmt *x = doc_sect(a, i), *y = doc_sect(b, i);
      if (!!p->pf.sect != !!q->pf.sect || x->start != y->start || x->cols != y->cols || x->colline != y->colline || x->valign != y->valign ||
          x->linenum != y->linenum || x->lnby != y->lnby || x->lnstart != y->lnstart || x->colgap != y->colgap) { printf("  paragraph %lu section differs\n", (unsigned long)i); return 0; }
    }
    if (p->pf.cell.table) {
      Table *s = doc_table(a, &p->pf), *t = doc_table(b, &q->pf);
      if (!s || !t || s->ncols != t->ncols || memcmp(s->grid, t->grid, s->ncols * sizeof(i32)) || s->marl != t->marl) { printf("  paragraph %lu table differs\n", (unsigned long)i); return 0; }
    }
    for (k = 0; k < p->len; k = (u32)utf8_next(p->t, p->len, k)) {
      CharFmt *f = para_fmt_at(p, k), *g = para_fmt_at(q, k);
      if (f->flags != g->flags || f->size != g->size || f->color != g->color || strcmp(a->fonts[f->font], b->fonts[g->font])) {
        printf("  paragraph %lu character %lu formatting differs\n", (unsigned long)i, (unsigned long)k); return 0;
      }
    }
  }
  return 1;
}
/* d written as a .docx and read back (NULL if either failed); *lost: the reader's LOST_ bits */
Doc *docx_again(Doc *d, unsigned *lost)
{
  char err[200];
  size_t n;
  unsigned l = 0;
  u8 *data = docx_save(d, &n);
  Doc *b = data ? docx_load(data, n, &l, err, sizeof err) : NULL;
  free(data);
  if (lost) *lost = l;
  return b;
}
static void docx_roundtrip(const char *file)
{
  char err[200], what[200];
  unsigned lost;
  Doc *a = doc_load_file(file, &lost, err, sizeof err), *b;
  size_t n;
  u8 *data;
  if (!a) { ok("roundtrip: loads", 0); return; }
  data = docx_save(a, &n);
  b = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  sprintf(what, "docx roundtrip %s: same text and formatting", file);
  ok(what, b && same_doc(a, b) && !(lost & LOST_TABLES));
  if (data) { sprintf(what, "/tmp/quill_rt_%s", strrchr(file, '/') + 1); file_write(what, data, n); }
  free(data); doc_free(a); doc_free(b);
}
static void zipcheck(void)
{
  static u8 src[70000], back[70000];
  size_t n, i;
  u8 *c;
  for (i = 0; i < sizeof src; i++) src[i] = (u8)("<w:p><w:r><w:t>Hello Amiga</w:t></w:r></w:p>"[i % 44] + (i % 977 == 0));
  c = zdeflate(src, sizeof src, &n);
  ok("zip: deflate compresses XML-like data", c && n < sizeof src / 4);
  ok("zip: inflate restores it", c && zinflate(c, n, back, sizeof back) == (long)sizeof src && !memcmp(src, back, sizeof src));
  ok("zip: a damaged stream is refused", c && (c[n / 2] ^= 0x55, zinflate(c, n, back, sizeof back) != (long)sizeof src || memcmp(src, back, sizeof src)));
  free(c);
  { char err[100]; unsigned lost; u8 junk[100]; memset(junk, 'P', sizeof junk); junk[1] = 'K';
    ok("docx: junk is refused politely", docx_load(junk, sizeof junk, &lost, err, sizeof err) == NULL && err[0]); }
}
static void text_files(void)
{
  Doc *d = txt_load((const u8 *)"one\r\ntwo\n\nfour caf\xE9", 20);   /* Latin-1, as Amiga text is */
  size_t n;
  u8 *out;
  ok("txt: lines become paragraphs", d && d->np == 4 && !strcmp(d->p[3]->t, "four caf\xC3\xA9"));
  out = d ? txt_save(d, 1, &n) : NULL;
  ok("txt: saved as Latin-1", out && n == 18 && !memcmp(out, "one\ntwo\n\nfour caf\xE9", 18));
  free(out); doc_free(d);
}

static void lists_and_breaks(void)
{
  char err[200];
  unsigned lost;
  Doc *d = doc_load_file("tests/data/word.docx", &lost, err, sizeof err);
  Layout *L;
  u32 i, lb = 0, after = 0;
  if (!d) { ok("lists: loads", 0); return; }
  L = lay_new(d, &fs);
  lay_update(L);
  for (i = 0; i < d->np; i++) { if (!lb && strstr(d->p[i]->t, "First bullet")) lb = i; if (strstr(d->p[i]->t, "After the break")) after = i; }
  ok("lists: bullet labels", lb && !strcmp(L->pl[lb].label, "\xE2\x80\xA2") && !strcmp(L->pl[lb + 1].label, "\xE2\x80\xA2"));
  ok("lists: numbers count", !strcmp(L->pl[lb + 2].label, "1.") && !strcmp(L->pl[lb + 3].label, "2.") && !strcmp(L->pl[lb + 4].label, "3."));
  { Box *b = &L->pl[lb + 2].boxes[0]; Line *ln = &L->pl[lb + 2].lines[0];
    ok("lists: the text starts at the indent, the label in the hanging space", ln->left + b->x == d->p[lb + 2]->pf.left && L->pl[lb + 2].labelw < 360); }
  ok("breaks: the page break starts page 2", after && L->placed[L->pl[after].placed0].page == 1 && L->placed[L->pl[after - 1].placed0].page == 0);
  /* a new item above renumbers the ones below (their own text did not change) */
  { Pos c; c.para = lb + 2; c.off = d->p[lb + 2]->len; doc_insert(d, &c, "\nInserted", 9, NULL); }
  lay_update(L);
  ok("lists: inserting an item renumbers the rest", !strcmp(L->pl[lb + 3].label, "2.") && !strcmp(L->pl[lb + 5].label, "4."));
  lay_free(L); doc_free(d);
}

static int cell_text(Doc *d, u32 pi, u16 row, u8 col, const char *t)
{
  return pi < d->np && d->p[pi]->pf.cell.row == row && d->p[pi]->pf.cell.col == col && !strcmp(d->p[pi]->t, t);
}
static void tables(void)
{
  Doc *d = doc_new(), *e;
  Layout *L;
  Pos c = doc_start(d);
  i32 body = d->page.width - d->page.left - d->page.right;
  u32 i, page; i32 x, y, h;
  size_t n;
  u8 *data;
  char err[100];
  unsigned lost;
  doc_insert(d, &c, "Before", 6, NULL);
  ok("tables: a 2 x 3 table goes in after the paragraph", tbl_insert(d, &c, 2, 3, body) && d->np == 8 && in_table(&d->p[1]->pf) && !in_table(&d->p[7]->pf) &&
     c.para == 1 && d->p[6]->pf.cell.row == 1 && d->p[6]->pf.cell.col == 2);
  doc_insert(d, &c, "A", 1, NULL);
  ok("tables: Tab goes to the next cell", tbl_next_cell(d, &c, 1) && c.para == 2);
  doc_insert(d, &c, "B", 1, NULL);
  tbl_next_cell(d, &c, 1); tbl_next_cell(d, &c, 1); doc_insert(d, &c, "D", 1, NULL);
  ok("tables: typing in cells", cell_text(d, 1, 0, 0, "A") && cell_text(d, 2, 0, 1, "B") && cell_text(d, 4, 1, 0, "D"));
  ok("tables: copied text has tabs between cells", text_is(d, "Before\nA\tB\t\nD\t\t\n"));
  ok("tables: Shift-Tab goes back", tbl_next_cell(d, &c, -1) && c.para == 3 && !tbl_next_cell(d, &(Pos){1, 0}, -1));
  /* Return in a cell: a new paragraph in the same cell */
  c = P(1, 1); doc_insert(d, &c, "\nA2", 3, NULL);
  ok("tables: Return stays in the cell", d->np == 9 && same_cell(&d->p[1]->pf, &d->p[2]->pf) && cell_end(d, 1) == 3 && cell_first(d, 2) == 1);
  L = lay_new(d, &fs);
  lay_update(L);
  {
    Placed *a = &L->placed[L->pl[1].placed0], *b = &L->placed[L->pl[3].placed0], *dd = &L->placed[L->pl[5].placed0];
    Placed *a2 = &L->placed[L->pl[2].placed0];
    ok("tables: cells side by side", a->y == b->y && b->x > a->x && a->x == 108 && b->x == body / 3 + 108 && a->w == body / 3 - 216);
    ok("tables: the second row below the taller cell", dd->y >= a2->y + L->pl[2].lines[0].height && dd->x == a->x);
    ok("tables: cell boxes with borders", L->ncells == 6 && L->cells[0].borders == BD_ALL && L->cells[0].x == d->page.left && L->cells[1].x == d->page.left + body / 3 &&
       L->cells[0].h == L->cells[1].h);
    ok("tables: the paragraph after is below the table", L->placed[L->pl[8].placed0].y >= L->cells[3].y + L->cells[3].h);
  }
  lay_caret(L, P(3, 1), &page, &x, &y, &h);
  ok("tables: caret in the second cell", x > d->page.left + body / 3);
  ok("tables: clicking in a cell", pos_cmp(lay_hit(L, page, x, y + h / 2), P(3, 1)) == 0);
  ok("tables: down from the first cell's first line goes to its second", lay_updown(L, P(1, 1), d->page.left + 200, 1).para == 2);
  ok("tables: down from cell B goes to the cell below it", lay_updown(L, P(3, 0), d->page.left + body / 3 + 200, 1).para == 6);
  /* rows and columns */
  c = P(3, 0);
  ok("tables: insert a row below", tbl_row_insert(d, &c, 1) && d->np == 12 && d->p[c.para]->pf.cell.row == 1 && d->p[c.para]->pf.cell.col == 0 &&
     d->p[c.para + 3]->pf.cell.row == 2 && cell_text(d, c.para + 3, 2, 0, "D"));
  ok("tables: delete it again", tbl_row_delete(d, &c) && d->np == 9 && cell_text(d, 5, 1, 0, "D"));
  c = P(3, 0);
  ok("tables: insert a column right of B", tbl_col_insert(d, &c, 1) && d->np == 11 && cell_text(d, 4, 0, 2, "") && cell_text(d, 3, 0, 1, "B") &&
     doc_table(d, &d->p[1]->pf)->ncols == 4 && d->p[10]->pf.cell.table == 0 && cell_text(d, 9, 1, 3, ""));
  lay_update(L);
  ok("tables: the table keeps its width", L->cells[3].x + L->cells[3].w <= d->page.left + body && L->cells[3].x + L->cells[3].w > d->page.left + body - 8);
  c = P(4, 0);
  ok("tables: delete that column", tbl_col_delete(d, &c) && d->np == 9 && doc_table(d, &d->p[1]->pf)->ncols == 3 && cell_text(d, 4, 0, 2, ""));
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("tables: saved and read back", e && same_doc(d, e));
  free(data); doc_free(e);
  ok("tables: undo the column operations and the rows", doc_undo(d, &c) && doc_undo(d, &c) && d->np == 9 && doc_table(d, &d->p[1]->pf)->ncols == 3);
  c = P(1, 0);
  ok("tables: delete the table", tbl_delete(d, &c) && d->np == 2 && !in_table(&d->p[1]->pf) && c.para == 1);
  doc_undo(d, &c);
  lay_update(L);
  ok("tables: undo brings it back", d->np == 9 && L->ncells == 6);
  c = P(1, 0);
  ok("tables: a table inside a table nests in the cell", tbl_insert(d, &c, 2, 2, 2000) && is_nested(&d->p[1]->pf) && d->p[1]->pf.outer.table == d->p[5]->pf.cell.table &&
     !is_nested(&d->p[5]->pf) && same_cell(&d->p[5]->pf, &d->p[6]->pf) && doc_undo(d, &c) && d->np == 9);
  /* a table taller than the rest of the page moves on; rows that don't fit go to the next page */
  c = P(8, 0);
  for (i = 0; i < 60; i++) doc_insert(d, &c, "line\n", 5, NULL);
  c = P(d->np - 1, 0);
  tbl_insert(d, &c, 30, 2, body);
  lay_update(L);
  {
    u32 last = d->np - 2;
    ok("tables: rows run onto the next pages", L->placed[L->pl[last].placed0].page >= 2 && L->ncells == 66);
    for (i = 1; i < L->ncells; i++) if (L->cells[i].page == L->cells[i - 1].page && L->cells[i].y + 1 < L->cells[i - 1].y) break;
    ok("tables: on each page the rows go down", i == L->ncells);
  }
  lay_free(L); doc_free(d);
}

/* a .docx made of a document part and a styles part */
static Doc *mini_docx(const char *body, const char *styles)
{
  ZipWriter z;
  Buf doc;
  Doc *d = NULL;
  char err[100];
  unsigned lost;
  buf_init(&doc);
  buf_adds(&doc, "<?xml version=\"1.0\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>");
  buf_adds(&doc, body);
  buf_adds(&doc, "</w:body></w:document>");
  zip_begin(&z);
  zip_add(&z, "word/document.xml", doc.data, doc.len);
  if (styles) {
    Buf st;
    buf_init(&st);
    buf_adds(&st, "<?xml version=\"1.0\"?><w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">");
    buf_adds(&st, styles);
    buf_adds(&st, "</w:styles>");
    zip_add(&z, "word/styles.xml", st.data, st.len);
    buf_free(&st);
  }
  if (zip_finish(&z)) d = docx_load((const u8 *)z.out.data, z.out.len, &lost, err, sizeof err);
  zip_free(&z); buf_free(&doc);
  return d;
}
static u32 find_text(Doc *d, const char *t) { u32 i; for (i = 0; i < d->np && !strstr(d->p[i]->t, t); i++) ; return i; }
static void table_styles(void)
{
  static const char *st =
    "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style>"
    "<w:style w:type=\"table\" w:default=\"1\" w:styleId=\"TableNormal\"><w:name w:val=\"Normal Table\"/>"
    "<w:tblPr><w:tblCellMar><w:left w:w=\"108\" w:type=\"dxa\"/><w:right w:w=\"108\" w:type=\"dxa\"/></w:tblCellMar></w:tblPr></w:style>"
    "<w:style w:type=\"table\" w:styleId=\"Fancy\"><w:name w:val=\"Fancy\"/><w:basedOn w:val=\"TableNormal\"/>"
    "<w:tblPr><w:tblBorders><w:top w:val=\"single\" w:sz=\"8\" w:color=\"4472C4\"/><w:bottom w:val=\"double\" w:sz=\"4\" w:color=\"4472C4\"/>"
    "<w:insideH w:val=\"dotted\" w:sz=\"4\" w:color=\"auto\"/></w:tblBorders></w:tblPr>"
    "<w:tblStylePr w:type=\"firstRow\"><w:rPr><w:b/><w:color w:val=\"FFFFFF\"/></w:rPr><w:tcPr><w:shd w:val=\"clear\" w:fill=\"4472C4\"/></w:tcPr></w:tblStylePr>"
    "<w:tblStylePr w:type=\"band1Horz\"><w:tcPr><w:shd w:val=\"clear\" w:fill=\"D9E2F3\"/></w:tcPr></w:tblStylePr>"
    "<w:tblStylePr w:type=\"lastRow\"><w:rPr><w:i/></w:rPr></w:tblStylePr>"
    "</w:style>";
  static const char *body =
    "<w:tbl><w:tblPr><w:tblStyle w:val=\"Fancy\"/><w:jc w:val=\"center\"/><w:tblLook w:firstRow=\"1\" w:lastRow=\"1\" w:noHBand=\"0\"/>"
    "<w:tblCellMar><w:top w:w=\"60\" w:type=\"dxa\"/></w:tblCellMar></w:tblPr>"
    "<w:tblGrid><w:gridCol w:w=\"2000\"/><w:gridCol w:w=\"2000\"/></w:tblGrid>"
    "<w:tr><w:trPr><w:cantSplit/></w:trPr><w:tc><w:p><w:r><w:t>Head1</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>Head2</w:t></w:r></w:p></w:tc></w:tr>"
    "<w:tr><w:tc><w:p><w:r><w:t>Band1</w:t></w:r></w:p></w:tc><w:tc><w:tcPr><w:shd w:fill=\"FF0000\"/><w:tcBorders><w:right w:val=\"dashed\" w:sz=\"12\" w:color=\"00FF00\"/></w:tcBorders></w:tcPr><w:p><w:r><w:t>Own</w:t></w:r></w:p></w:tc></w:tr>"
    "<w:tr><w:tc><w:p><w:r><w:t>Band2</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>x</w:t></w:r></w:p></w:tc></w:tr>"
    "<w:tr><w:tc><w:p><w:r><w:t>Total</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>y</w:t></w:r></w:p></w:tc></w:tr>"
    "</w:tbl><w:p/>";
  Doc *d = mini_docx(body, st), *e;
  u32 h, b1, own, b2, tot;
  Table *t;
  if (!d) { ok("table styles: a document with a table style reads", 0); return; }
  h = find_text(d, "Head1"); b1 = find_text(d, "Band1"); own = find_text(d, "Own"); b2 = find_text(d, "Band2"); tot = find_text(d, "Total");
  t = doc_table(d, &d->p[h]->pf);
  ok("table styles: the table knows its style, look, alignment and top margin", t && t->tstyle && !strcmp(d->tstyles[t->tstyle - 1].id, "Fancy") &&
     (t->look & LK_FIRSTROW) && (t->look & LK_LASTROW) && t->jc == AL_CENTER && t->mart == 60);
  ok("table styles: the heading row is filled, bold and white", d->p[h]->pf.cell.shade == 0x4472C4 && (d->p[h]->r[0].f.flags & CF_BOLD) && d->p[h]->r[0].f.color == 0xFFFFFF);
  ok("table styles: the first band is filled, the second not", d->p[b1]->pf.cell.shade == 0xD9E2F3 && d->p[b2]->pf.cell.shade == COLOR_AUTO && !(d->p[b1]->r[0].f.flags & CF_BOLD));
  ok("table styles: the total row is italic", (d->p[tot]->r[0].f.flags & CF_ITALIC) && !(d->p[b2]->r[0].f.flags & CF_ITALIC));
  ok("table styles: the table's lines: top single in colour, dotted inside, double at the bottom",
     (d->p[h]->pf.cell.borders & BD_TOP) && d->p[h]->pf.cell.bcolor[E_TOP] == 0x4472C4 && d->p[h]->pf.cell.bwidth[E_TOP] == 8 &&
     (d->p[b1]->pf.cell.borders & BD_TOP) && d->p[b1]->pf.cell.bstyle[E_TOP] == PB_DOTTED &&
     (d->p[tot]->pf.cell.borders & BD_BOTTOM) && d->p[tot]->pf.cell.bstyle[E_BOTTOM] == PB_DOUBLE && !(d->p[h]->pf.cell.borders & BD_LEFT));
  ok("table styles: a cell's own fill and line win", d->p[own]->pf.cell.shade == 0xFF0000 && (d->p[own]->pf.cell.borders & BD_RIGHT) &&
     d->p[own]->pf.cell.bstyle[E_RIGHT] == PB_DASHED && d->p[own]->pf.cell.bcolor[E_RIGHT] == 0x00FF00 && d->p[own]->pf.cell.bwidth[E_RIGHT] == 12);
  ok("table styles: a row that mustn't break", d->p[h]->pf.cell.cantsplit && !d->p[b1]->pf.cell.cantsplit);
  {
    size_t n; u8 *data = docx_save(d, &n); char err[100]; unsigned lost;
    Table *u;
    e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
    u = e ? doc_table(e, &e->p[h]->pf) : NULL;
    ok("table styles: saved and read back the same", e && same_doc(d, e) && u && u->tstyle && !strcmp(e->tstyles[u->tstyle - 1].id, "Fancy") && u->jc == AL_CENTER &&
       u->look == t->look && u->mart == 60 && e->p[h]->pf.cell.cantsplit && e->p[own]->pf.cell.bstyle[E_RIGHT] == PB_DASHED &&
       e->p[b1]->pf.cell.bstyle[E_TOP] == PB_DOTTED && e->tstyles[u->tstyle - 1].c[TC_FIRSTROW].shade == 0x4472C4);
    free(data); doc_free(e);
  }
  {
    Layout *L = lay_new(d, &fs);
    i32 body = d->page.width - d->page.left - d->page.right;
    lay_update(L);
    ok("table styles: a centred table sits in the middle", L->ncells && L->cells[0].x == d->page.left + (body - 4000) / 2);
    ok("table styles: the top margin is above the text", L->placed[L->pl[h].placed0].y == L->cells[0].y + 60);
    lay_free(L);
  }
  doc_free(d);
  /* Quill's own: a new table is a grid; a banded style on it, then without the heading row */
  {
    Doc *q = doc_new();
    Pos c = doc_start(q);
    int k, ts;
    tbl_insert(q, &c, 3, 2, 9000);
    doc_insert(q, &c, "Head", 4, NULL);
    t = doc_table(q, &q->p[0]->pf);
    ok("table styles: a new table has the grid style", t->tstyle && !strcmp(q->tstyles[t->tstyle - 1].id, "TableGrid") && q->p[3]->pf.cell.borders == BD_ALL);
    for (k = 0; k < tstyle_builtin_count() && strcmp(tstyle_builtin(k)->id, "QuillBandedBlue"); k++) ;
    ts = tstyle_use_builtin(q, k);
    c = P(0, 2);
    ok("table styles: a banded style goes on", ts && tbl_apply_style(q, &c, ts, LK_DEFAULT) && c.para == 0 && c.off == 2 &&
       q->p[0]->pf.cell.shade == 0x3B5BDB && (q->p[0]->r[0].f.flags & CF_BOLD) && q->p[0]->r[0].f.color == 0xFFFFFF &&
       q->p[2]->pf.cell.shade != COLOR_AUTO && q->p[4]->pf.cell.shade == COLOR_AUTO && !(q->p[2]->pf.cell.borders & BD_TOP));
    ok("table styles: without the heading row the text is plain again", tbl_apply_style(q, &c, ts, LK_NOVBAND) &&
       !(q->p[0]->r[0].f.flags & CF_BOLD) && q->p[0]->r[0].f.color == q->styles[0].cf.color && q->p[0]->pf.cell.shade != 0x3B5BDB);
    ok("table styles: undo brings the heading back", doc_undo(q, &c) && (q->p[0]->r[0].f.flags & CF_BOLD));
    {
      Table nt = *doc_table(q, &q->p[0]->pf);
      nt.jc = AL_RIGHT;
      ok("table styles: table properties change in one step", tbl_set_table(q, &c, &nt) && doc_table(q, &q->p[0]->pf)->jc == AL_RIGHT &&
         doc_undo(q, &c) && doc_table(q, &q->p[0]->pf)->jc == AL_LEFT);
    }
    doc_free(q);
  }
}
static void row_breaking(void)
{
  Doc *d = doc_new();
  Layout *L = lay_new(d, &fs);
  Pos c = doc_start(d);
  u32 i, k, page_of_first, page_of_last;
  ParaFmt pf;
  for (i = 0; i < 40; i++) doc_insert(d, &c, "filler line\n", 12, NULL);
  tbl_insert(d, &c, 2, 2, 9000);
  doc_insert(d, &c, "Heading", 7, NULL);
  for (i = c.para; i < c.para + 2; i++) { pf = d->p[i]->pf; pf.cell.header = 1; doc_para_format(d, i, i, PM_CELL, &pf); }
  tbl_next_cell(d, &c, 1); tbl_next_cell(d, &c, 1);
  k = c.para;
  for (i = 0; i < 60; i++) doc_insert(d, &c, "a long cell\n", 12, NULL);
  lay_update(L);
  page_of_first = L->placed[L->pl[k].placed0].page;
  page_of_last = L->placed[L->pl[k + 59].placed0].page;
  ok("row breaking: a tall row starts on the page the table is on and runs on", page_of_first == L->placed[L->pl[k - 2].placed0].page && page_of_last > page_of_first);
  {
    u32 a, b, got = 0;
    lay_page_lines(L, page_of_last, &a, &b);
    for (i = a; i < b; i++) if (L->placed[i].page == page_of_last && L->placed[i].para == k - 2 && L->placed[i].y == d->page.top) got = 1;
    ok("row breaking: the heading row again on the page it runs on to", got);
  }
  pf = d->p[k]->pf; pf.cell.cantsplit = 1;
  for (i = k - 2; i < k + 61 && in_table(&d->p[i]->pf); i++) if (d->p[i]->pf.cell.row == 1) { pf = d->p[i]->pf; pf.cell.cantsplit = 1; doc_para_format(d, i, i, PM_CELL, &pf); }
  lay_update(L);
  ok("row breaking: a row that mustn't break starts a new page", L->placed[L->pl[k].placed0].page > L->placed[L->pl[k - 2].placed0].page);
  lay_free(L); doc_free(d);
}

/* the paragraph of the cell at row r, column c of the table paragraph pi is in; -1 if none */
static int cell_para(Doc *d, u32 pi, int r, int c)
{
  u32 i;
  for (i = tbl_first(d, pi); i < tbl_end(d, pi); i++) if (d->p[i]->pf.cell.row == r && d->p[i]->pf.cell.col == c && cell_first(d, i) == i) return (int)i;
  return -1;
}
static void fill_table(Doc *d, int rows, int cols, const char *const *texts)
{
  Pos c = doc_start(d);
  int k;
  tbl_insert(d, &c, rows, cols, 9000);
  for (k = 0; k < rows * cols; k++) {
    if (texts[k][0]) doc_insert(d, &c, texts[k], (u32)strlen(texts[k]), NULL);
    tbl_next_cell(d, &c, 1);
  }
}
static void table_ops(void)
{
  static const char *const t3[] = { "a", "b", "c", "d", "e", "f", "g", "h", "i" };
  Doc *d = doc_new();
  Pos c, e;
  int p;
  char out[40];
  fill_table(d, 3, 3, t3);
  /* a 2 x 2 block merged */
  ok("table ops: a block of 2 x 2 cells merges", tbl_merge(d, P(0, 0), P(4, 0)) && d->p[0]->pf.cell.span == 2 && d->p[0]->pf.cell.vmerge == VM_START &&
     !strcmp(d->p[0]->t, "a") && !strcmp(d->p[1]->t, "b") && !strcmp(d->p[2]->t, "d") && !strcmp(d->p[3]->t, "e") &&
     (p = cell_para(d, 0, 1, 0)) >= 0 && d->p[p]->pf.cell.vmerge == VM_CONT && d->p[p]->pf.cell.span == 2 && d->p[p]->len == 0 &&
     cell_para(d, 0, 1, 2) >= 0 && !strcmp(d->p[cell_para(d, 0, 1, 2)]->t, "f"));
  {
    Layout *L = lay_new(d, &fs);
    lay_update(L);
    ok("table ops: the merged cell is one box over two rows", L->ncells == 6 && L->cells[0].w == 6000 && L->cells[0].h > L->cells[1].h);
    lay_free(L);
  }
  c = P(0, 0);
  ok("table ops: split it back into four", tbl_split_cells(d, &c, 2, 1) && cell_para(d, 0, 0, 1) >= 0 && d->p[0]->pf.cell.span == 1 &&
     d->p[cell_para(d, 0, 1, 0)]->pf.cell.vmerge == VM_NONE && d->p[cell_para(d, 0, 1, 1)]->pf.cell.vmerge == VM_NONE);
  ok("table ops: one undo step each", doc_undo(d, &c) && d->p[0]->pf.cell.span == 2 && doc_undo(d, &c) && cell_para(d, 0, 0, 1) == 1 && d->np == 10);
  /* a cell split into three columns: the grid grows, the column's other cells span */
  c = P(4, 0);
  ok("table ops: a cell split into 3 columns", tbl_split_cells(d, &c, 3, 1) && doc_table(d, &d->p[0]->pf)->ncols == 5 &&
     d->p[cell_para(d, 0, 1, 1)]->pf.cell.span == 1 && !strcmp(d->p[cell_para(d, 0, 1, 1)]->t, "e") && cell_para(d, 0, 1, 3) >= 0 &&
     d->p[cell_para(d, 0, 0, 1)]->pf.cell.span == 3 && d->p[cell_para(d, 0, 2, 1)]->pf.cell.span == 3 && d->p[cell_para(d, 0, 2, 4)]->pf.cell.col == 4);
  doc_undo(d, &c);
  c = P(4, 0);
  ok("table ops: a cell split into 2 rows: the row's other cells reach over both", tbl_split_cells(d, &c, 1, 2) &&
     d->p[cell_para(d, 0, 1, 0)]->pf.cell.vmerge == VM_START && d->p[cell_para(d, 0, 2, 0)]->pf.cell.vmerge == VM_CONT &&
     d->p[cell_para(d, 0, 2, 1)]->pf.cell.vmerge == VM_NONE && !strcmp(d->p[cell_para(d, 0, 3, 0)]->t, "g"));
  doc_undo(d, &c);
  /* split the table */
  c = P(4, 0);
  ok("table ops: split the table at the second row", tbl_split_table(d, &c) && !in_table(&d->p[3]->pf) && in_table(&d->p[4]->pf) &&
     d->p[4]->pf.cell.row == 0 && d->p[4]->pf.cell.table != d->p[0]->pf.cell.table && c.para == 3);
  doc_undo(d, &c);
  /* lines */
  ok("table ops: no lines, then a box round two cells", tbl_borders(d, P(0, 0), P(8, 0), TB_NONE, PB_SINGLE, 4, COLOR_AUTO) && d->p[4]->pf.cell.borders == 0 &&
     tbl_borders(d, P(0, 0), P(1, 0), TB_OUTSIDE, PB_DOUBLE, 6, 0xFF0000) && d->p[0]->pf.cell.borders == (BD_TOP | BD_LEFT | BD_BOTTOM) &&
     d->p[1]->pf.cell.borders == (BD_TOP | BD_RIGHT | BD_BOTTOM) && d->p[0]->pf.cell.bstyle[E_TOP] == PB_DOUBLE && d->p[1]->pf.cell.bcolor[E_RIGHT] == 0xFF0000 &&
     d->p[2]->pf.cell.borders == 0);
  ok("table ops: inside lines only", tbl_borders(d, P(0, 0), P(8, 0), TB_INSIDE_H, PB_SINGLE, 4, COLOR_AUTO) && (d->p[4]->pf.cell.borders & BD_TOP) &&
     (d->p[4]->pf.cell.borders & BD_BOTTOM) && !(d->p[4]->pf.cell.borders & BD_LEFT) && !(d->p[7]->pf.cell.borders & BD_BOTTOM) && (d->p[1]->pf.cell.borders & BD_TOP));
  /* rows */
  ok("table ops: row height, heading row, rows that don't break", tbl_cell_format(d, P(1, 0), P(4, 0), TF_ROWH, (u32)(i32)-500) &&
     d->p[0]->pf.cell.rowh == -500 && d->p[5]->pf.cell.rowh == -500 && d->p[6]->pf.cell.rowh == 0 &&
     tbl_cell_format(d, P(0, 0), P(0, 0), TF_HEADER, 1) && d->p[2]->pf.cell.header && !d->p[3]->pf.cell.header &&
     tbl_cell_format(d, P(8, 0), P(8, 0), TF_CANTSPLIT, 1) && d->p[6]->pf.cell.cantsplit);
  /* formulas */
  doc_free(d);
  {
    static const char *const nums[] = { "Item", "Price", "Qty", "Pens", "1.50", "4", "Ink", "12", "2", "Paper", "3,000", "1", "Total", "", "" };
    d = doc_new();
    fill_table(d, 5, 3, nums);
    c = P(13, 0);
    ok("formulas: SUM(ABOVE)", tbl_formula(d, c, "=SUM(ABOVE)", out, sizeof out) && !strcmp(out, "3013.5"));
    ok("formulas: AVERAGE, MAX, MIN, COUNT", tbl_formula(d, c, "=AVERAGE(ABOVE)", out, sizeof out) && !strcmp(out, "1004.5") &&
       tbl_formula(d, c, "=MAX(B2:B4)", out, sizeof out) && !strcmp(out, "3000") && tbl_formula(d, c, "=min(above)", out, sizeof out) && !strcmp(out, "1.5") &&
       tbl_formula(d, c, "=COUNT(ABOVE)", out, sizeof out) && !strcmp(out, "3"));
    ok("formulas: cells and arithmetic", tbl_formula(d, c, "=B2*C2+B3*C3", out, sizeof out) && !strcmp(out, "30") &&
       tbl_formula(d, c, "=(B2+B3)/3", out, sizeof out) && !strcmp(out, "4.5") && tbl_formula(d, P(5, 0), "=SUM(LEFT)", out, sizeof out) && !strcmp(out, "1.5"));
    ok("formulas: errors", !tbl_formula(d, c, "=SUM(ABOVE", out, sizeof out) && !strcmp(out, "!Syntax Error") &&
       !tbl_formula(d, c, "=1/0", out, sizeof out) && !strcmp(out, "!Zero Divide"));
    /* sorting: by price, as numbers, the heading row stays */
    c = P(4, 0);
    ok("sort: by number, the heading row stays, text after numbers", tbl_sort(d, &c, 1, 1, 1, 1) && !strcmp(d->p[0]->t, "Item") && !strcmp(d->p[3]->t, "Paper") &&
       !strcmp(d->p[6]->t, "Ink") && !strcmp(d->p[9]->t, "Pens") && !strcmp(d->p[12]->t, "Total"));
    ok("sort: by name, Z to A", tbl_sort(d, &c, 0, 1, 0, 1) && !strcmp(d->p[0]->t, "Item") && !strcmp(d->p[3]->t, "Total") && !strcmp(d->p[6]->t, "Pens") &&
       !strcmp(d->p[9]->t, "Paper") && !strcmp(d->p[12]->t, "Ink"));
    ok("sort: A to Z", tbl_sort(d, &c, 0, 0, 0, 1) && !strcmp(d->p[3]->t, "Ink") && !strcmp(d->p[12]->t, "Total"));
    /* autofit */
    {
      Layout *L = lay_new(d, &fs);
      i32 grid[MAX_COLS];
      int n;
      Table nt;
      lay_update(L);
      n = lay_autofit(L, 0, 0, 9000, grid);
      ok("autofit: columns as wide as their text", n == 3 && grid[0] < 3000 && grid[0] > 500 && grid[0] + grid[1] + grid[2] < 9000);
      nt = *doc_table(d, &d->p[0]->pf);
      memcpy(nt.grid, grid, sizeof grid);
      c = P(0, 0);
      ok("autofit: the table takes them", tbl_set_table(d, &c, &nt) && doc_table(d, &d->p[0]->pf)->grid[0] == grid[0]);
      n = lay_autofit(L, 0, 1, 6000, grid);
      ok("autofit: to the window keeps the proportions", n == 3 && grid[0] + grid[1] + grid[2] > 5990 && grid[0] + grid[1] + grid[2] <= 6000);
      lay_free(L);
    }
    /* table to text and back */
    c = P(0, 0);
    ok("convert: table to text, tabs between cells", tbl_to_text(d, &c, '\t') && d->np == 6 && !strcmp(d->p[0]->t, "Item\tPrice\tQty") && !in_table(&d->p[0]->pf));
    e = P(0, 0);
    ok("convert: text to table at tabs", tbl_from_text(d, 0, 4, '\t', 9000, &e) && in_table(&d->p[0]->pf) && doc_table(d, &d->p[0]->pf)->ncols == 3 &&
       d->np == 16 && !strcmp(d->p[1]->t, "Price") && d->p[14]->pf.cell.row == 4 && !in_table(&d->p[15]->pf));
    doc_free(d);
  }
  {
    Doc *q = doc_new();
    Pos c2 = doc_start(q);
    doc_insert(q, &c2, "one, two, three\nfour, five\n", 27, NULL);
    ok("convert: commas, rows of different lengths", tbl_from_text(q, 0, 1, ',', 9000, &c2) && doc_table(q, &q->p[0]->pf)->ncols == 3 &&
       !strcmp(q->p[1]->t, " two") && q->p[5]->len == 0 && q->p[5]->pf.cell.row == 1);
    doc_free(q);
  }
}

static void nested_tables(void)
{
  static const char *body =
    "<w:tbl><w:tblGrid><w:gridCol w:w=\"3000\"/><w:gridCol w:w=\"5000\"/></w:tblGrid>"
    "<w:tr><w:tc><w:p><w:r><w:t>Left</w:t></w:r></w:p></w:tc>"
    "<w:tc><w:p><w:r><w:t>Above</w:t></w:r></w:p>"
      "<w:tbl><w:tblGrid><w:gridCol w:w=\"2000\"/><w:gridCol w:w=\"2000\"/></w:tblGrid>"
      "<w:tr><w:tc><w:p><w:r><w:t>i00</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>i01</w:t></w:r></w:p>"
        "<w:tbl><w:tr><w:tc><w:p><w:r><w:t>deep</w:t></w:r></w:p></w:tc></w:tr></w:tbl><w:p/></w:tc></w:tr>"
      "<w:tr><w:tc><w:p><w:r><w:t>i10</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>i11</w:t></w:r></w:p></w:tc></w:tr></w:tbl>"
    "<w:p><w:r><w:t>Below</w:t></w:r></w:p></w:tc></w:tr>"
    "<w:tr><w:tc><w:p><w:r><w:t>r1</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>r1b</w:t></w:r></w:p></w:tc></w:tr></w:tbl><w:p/>";
  Doc *d = mini_docx(body, NULL), *e;
  u32 left, above, i00, i01, deep, i11, below, r1;
  if (!d) { ok("nested tables: a document with one reads", 0); return; }
  left = find_text(d, "Left"); above = find_text(d, "Above"); i00 = find_text(d, "i00"); i01 = find_text(d, "i01"); deep = find_text(d, "deep");
  i11 = find_text(d, "i11"); below = find_text(d, "Below"); r1 = find_text(d, "r1");
  ok("nested tables: the inner table's cells are in the outer cell", is_nested(&d->p[i00]->pf) && d->p[i00]->pf.outer.table == d->p[left]->pf.cell.table &&
     d->p[i00]->pf.outer.col == 1 && d->p[i00]->pf.outer.row == 0 && d->p[i11]->pf.cell.row == 1 && d->p[i11]->pf.cell.col == 1 &&
     d->p[i00]->pf.cell.table != d->p[left]->pf.cell.table);
  ok("nested tables: the cell's own paragraphs before and after", !is_nested(&d->p[above]->pf) && d->p[above]->pf.cell.col == 1 && !is_nested(&d->p[below]->pf) &&
     d->p[below]->pf.cell.col == 1 && d->p[r1]->pf.cell.row == 1 && !is_nested(&d->p[r1]->pf));
  ok("nested tables: a third level goes into the second's cell", deep < d->np && d->p[deep]->pf.cell.table == d->p[i01]->pf.cell.table && same_cell(&d->p[deep]->pf, &d->p[i01]->pf));
  {
    Layout *L = lay_new(d, &fs);
    u32 k, outerbox = 0, innerbox = 0;
    lay_update(L);
    for (k = 0; k < L->ncells; k++) { if (L->cells[k].depth) innerbox++; else outerbox++; }
    {
      Placed *pa = &L->placed[L->pl[above].placed0], *pi = &L->placed[L->pl[i00].placed0], *pj = &L->placed[L->pl[i01].placed0], *pb = &L->placed[L->pl[below].placed0];
      Placed *pr = &L->placed[L->pl[r1].placed0];
      ok("nested tables: laid out inside the cell, below its first paragraph", innerbox == 4 && outerbox == 4 && pi->y > pa->y && pi->x == pa->x &&     /* (an old file's tables start a margin left) */
         pj->x == pi->x + 2000 && pj->y == pi->y && pb->y > pi->y && pr->y > pb->y);
    }
    lay_free(L);
  }
  {
    size_t n; u8 *data = docx_save(d, &n); char err[100]; unsigned lost;
    e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
    ok("nested tables: saved and read back the same", e && same_doc(d, e) && !(lost & LOST_TABLES));
    free(data); doc_free(e);
  }
  {
    Pos c = P(i00, 0);
    u32 np0 = d->np;
    ok("nested tables: a row goes into the inner table", tbl_row_insert(d, &c, 1) && d->np == np0 + 2 && is_nested(&d->p[c.para]->pf) &&
       d->p[c.para]->pf.cell.row == 1 && d->p[find_text(d, "i11")]->pf.cell.row == 2 && d->p[find_text(d, "r1")]->pf.cell.row == 1);
    doc_undo(d, &c);
    c = P(left, 0);
    ok("nested tables: a column into the outer table keeps the inner one in its cell", tbl_col_insert(d, &c, 1) && is_nested(&d->p[find_text(d, "i00")]->pf) &&
       d->p[find_text(d, "i00")]->pf.outer.col == 2 && d->p[find_text(d, "i00")]->pf.outer.table == d->p[find_text(d, "Left")]->pf.cell.table &&
       d->p[find_text(d, "Above")]->pf.cell.col == 2 && d->p[find_text(d, "i00")]->pf.cell.col == 0);
    doc_undo(d, &c);
    c = P(find_text(d, "r1"), 0);
    ok("nested tables: deleting an outer row with the inner table kept above", tbl_row_delete(d, &c) && find_text(d, "r1") == d->np && is_nested(&d->p[find_text(d, "i00")]->pf));
    doc_undo(d, &c);
    c = P(find_text(d, "i10"), 0);
    ok("nested tables: deleting the inner table leaves the cell's paragraphs", tbl_delete(d, &c) && find_text(d, "i00") == d->np && !is_nested(&d->p[find_text(d, "Below")]->pf) &&
       same_cell(&d->p[find_text(d, "Above")]->pf, &d->p[find_text(d, "Below")]->pf));
    doc_undo(d, &c);
    c = P(find_text(d, "Above"), 5);
    ok("nested tables: no table three deep", tbl_insert(d, &c, 1, 1, 1000) == 1 && !tbl_insert(d, &(Pos){find_text(d, "i00"), 0}, 1, 1, 500));
  }
  doc_free(d);
}

static int zip_entries(const u8 *data, size_t n, const char *prefix)
{
  ZipReader z;
  int i, c = 0;
  if (!zip_open(&z, data, n)) return -1;
  for (i = 0; i < z.n; i++) if (!strncmp(z.e[i].name, prefix, strlen(prefix))) c++;
  zip_close(&z);
  return c;
}
/* the first object reference in paragraph pi: its index, and where it is */
static int obj_in(Doc *d, u32 pi, Pos *at)
{
  u32 k;
  for (k = 0; pi < d->np && k + 4 <= d->p[pi]->len; k++) if (IS_PIC(d->p[pi]->t + k)) { if (at) { at->para = pi; at->off = k; } return pic_index(d->p[pi]->t + k); }
  return -1;
}
static void objects_docx(void)
{
  static const char *body =
    "<w:p><w:r><w:t>Before</w:t></w:r></w:p>"
    "<w:p><w:r><w:drawing><wp:anchor distT=\"0\" distB=\"0\" distL=\"114300\" distR=\"114300\" simplePos=\"0\" relativeHeight=\"1\" behindDoc=\"0\" locked=\"0\" layoutInCell=\"1\" allowOverlap=\"1\">"
      "<wp:simplePos x=\"0\" y=\"0\"/><wp:positionH relativeFrom=\"margin\"><wp:align>right</wp:align></wp:positionH>"
      "<wp:positionV relativeFrom=\"paragraph\"><wp:posOffset>63500</wp:posOffset></wp:positionV><wp:extent cx=\"1270000\" cy=\"635000\"/>"
      "<wp:wrapSquare wrapText=\"bothSides\"/><wp:docPr id=\"1\" name=\"Shape 1\" descr=\"A blue box\"/>"
      "<a:graphic><a:graphicData uri=\"http://schemas.microsoft.com/office/word/2010/wordprocessingShape\"><wps:wsp><wps:cNvSpPr/>"
      "<wps:spPr><a:xfrm rot=\"5400000\"><a:off x=\"0\" y=\"0\"/><a:ext cx=\"1270000\" cy=\"635000\"/></a:xfrm><a:prstGeom prst=\"ellipse\"><a:avLst/></a:prstGeom>"
      "<a:solidFill><a:schemeClr val=\"accent1\"><a:lumMod val=\"50000\"/></a:schemeClr></a:solidFill><a:ln w=\"25400\"><a:solidFill><a:srgbClr val=\"FF0000\"/></a:solidFill><a:prstDash val=\"dash\"/></a:ln></wps:spPr>"
      "<wps:bodyPr/></wps:wsp></a:graphicData></a:graphic></wp:anchor></w:drawing></w:r><w:r><w:t>Text by a shape</w:t></w:r></w:p>"
    "<w:p><w:r><mc:AlternateContent><mc:Choice Requires=\"wps\"><w:drawing><wp:anchor behindDoc=\"1\" distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
      "<wp:positionH relativeFrom=\"page\"><wp:posOffset>1270000</wp:posOffset></wp:positionH><wp:positionV relativeFrom=\"page\"><wp:align>center</wp:align></wp:positionV>"
      "<wp:extent cx=\"2540000\" cy=\"1270000\"/><wp:wrapNone/><wp:docPr id=\"2\" name=\"Text Box 2\"/>"
      "<a:graphic><a:graphicData><wps:wsp><wps:cNvSpPr txBox=\"1\"/><wps:spPr><a:prstGeom prst=\"rect\"/><a:solidFill><a:srgbClr val=\"FFFFCC\"/></a:solidFill></wps:spPr>"
      "<wps:txbx><w:txbxContent><w:p><w:r><w:t>Inside the box</w:t></w:r></w:p><w:p><w:r><w:t>Second line</w:t></w:r></w:p></w:txbxContent></wps:txbx>"
      "<wps:bodyPr/></wps:wsp></a:graphicData></a:graphic></wp:anchor></w:drawing></mc:Choice><mc:Fallback><w:pict><v:shape/></w:pict></mc:Fallback></mc:AlternateContent></w:r></w:p>";
  Doc *d = mini_docx(body, NULL), *e;
  int k1, k2;
  Pos at;
  if (!d) { ok("objects: a document with shapes reads", 0); return; }
  k1 = obj_in(d, find_text(d, "Text by"), NULL);
  k2 = obj_in(d, 2, NULL);
  ok("objects: a floating ellipse, square wrap, at the margin's right", k1 >= 0 && d->images[k1].kind == OBJ_SHAPE && d->images[k1].shape == SH_ELLIPSE &&
     d->images[k1].wrap == WRAP_SQUARE && d->images[k1].hrel == POS_MARGIN && d->images[k1].halign == PA_END && d->images[k1].vrel == POS_PARA &&
     d->images[k1].y == 100 && d->images[k1].cx == 2000 && d->images[k1].cy == 1000 && d->images[k1].dl == 180 && d->images[k1].rot == 900);
  ok("objects: its colours: a darker theme colour, a red dashed outline, its description", k1 >= 0 && d->images[k1].fill == 0x223962 &&
     d->images[k1].lcolor == 0xFF0000 && d->images[k1].lstyle == PB_DASHED && d->images[k1].lwidth == 16 && !strcmp(d->images[k1].alt, "A blue box"));
  ok("objects: a text box behind the text, its paragraphs, the VML copy left out", k2 >= 0 && d->images[k2].kind == OBJ_TEXTBOX && d->images[k2].wrap == WRAP_BEHIND &&
     d->images[k2].hrel == POS_PAGE && d->images[k2].x == 2000 && d->images[k2].valign == PA_CENTER && d->images[k2].text && d->images[k2].text->np == 2 &&
     !strcmp(d->images[k2].text->p[1]->t, "Second line") && d->images[k2].fill == 0xFFFFCC && d->nimages == 2);
  {
    size_t n; u8 *data = docx_save(d, &n); char err[100]; unsigned lost;
    int j1, j2;
    e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
    j1 = e ? obj_in(e, find_text(e, "Text by"), NULL) : -1; j2 = e ? obj_in(e, 2, NULL) : -1;
    ok("objects: saved and read back", e && !(lost & LOST_PICTURES) && j1 >= 0 && j2 >= 0 && e->images[j1].shape == SH_ELLIPSE && e->images[j1].wrap == WRAP_SQUARE &&
       e->images[j1].halign == PA_END && e->images[j1].hrel == POS_MARGIN && e->images[j1].y == 100 && e->images[j1].fill == 0x223962 && e->images[j1].lstyle == PB_DASHED &&
       e->images[j1].rot == 900 && !strcmp(e->images[j1].alt, "A blue box") && e->images[j2].kind == OBJ_TEXTBOX && e->images[j2].wrap == WRAP_BEHIND &&
       e->images[j2].text && e->images[j2].text->np == 2 && !strcmp(e->images[j2].text->p[0]->t, "Inside the box") && e->images[j2].valign == PA_CENTER);
    free(data); doc_free(e);
  }
  /* changing an object: a new entry in its place, one undo step */
  {
    Image im;
    u32 pi = find_text(d, "Text by");
    obj_in(d, pi, &at);
    im = d->images[k1]; im.wrap = WRAP_TOPBOTTOM; im.fill = 0x00FF00;
    ok("objects: changed in one undoable step", doc_set_object(d, at, &im) == 2 && d->images[obj_in(d, pi, NULL)].wrap == WRAP_TOPBOTTOM &&
       doc_undo(d, &at) && obj_in(d, pi, NULL) == k1);
  }
  doc_free(d);
  /* a picture made floating, cropped and outlined, and back */
  {
    char err[200];
    unsigned lost;
    Doc *q = doc_load_file("tests/data/pictures.docx", &lost, err, sizeof err);
    u32 pi;
    int k;
    if (!q) return;
    for (pi = 0; pi < q->np && obj_in(q, pi, &at) < 0; pi++) ;
    k = obj_in(q, pi, &at);
    if (k >= 0) {
      Image im = q->images[k];
      size_t n; u8 *data; int j, nimg;
      im.wrap = WRAP_TIGHT; im.hrel = POS_PAGE; im.x = 1440; im.vrel = POS_PARA; im.y = 0; im.crop[0] = 10000; im.crop[2] = 5000;
      im.lstyle = PB_SINGLE; im.lwidth = 8; im.lcolor = 0x123456; strcpy(im.alt, "A photograph");
      j = doc_set_object(q, at, &im);
      nimg = q->nimages;
      data = docx_save(q, &n);
      e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
      k = e ? obj_in(e, pi, NULL) : -1;
      ok("objects: a picture floating, cropped and outlined, saved and read back (its file once)", j >= 0 && q->images[j].shared && k >= 0 &&
         e->images[k].kind == OBJ_PICTURE && e->images[k].wrap == WRAP_TIGHT && e->images[k].hrel == POS_PAGE && e->images[k].x == 1440 &&
         e->images[k].crop[0] == 10000 && e->images[k].crop[2] == 5000 && e->images[k].lwidth == 8 && e->images[k].lcolor == 0x123456 &&
         !strcmp(e->images[k].alt, "A photograph") && e->images[k].pw == q->images[j].pw && e->nimages == nimg - 1 && zip_entries(data, n, "word/media/") == 3);
      free(data); doc_free(e);
    }
    doc_free(q);
  }
}

static u8 *slurp(const char *name, u32 *n)
{
  FILE *f = fopen(name, "rb");
  u8 *b;
  long l;
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); l = ftell(f); fseek(f, 0, SEEK_SET);
  b = (u8 *)malloc((size_t)l);
  *n = (u32)fread(b, 1, (size_t)l, f);
  fclose(f);
  return b;
}
static u8 *decode_file(const char *name, u32 *w, u32 *h, int *decodable)
{
  u32 n;
  u8 *b = slurp(name, &n), *rgb;
  if (!b) return NULL;
  img_info(b, n, w, h);
  img_jpeg_components(b, n, decodable);
  rgb = img_decode(b, n, *w, *h);
  free(b);
  return rgb;
}
static double mean_diff(const u8 *a, const u8 *b, u32 n)
{
  double t = 0;
  u32 i;
  for (i = 0; i < n; i++) t += a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
  return t / n;
}
static void jpeg_kinds(void)
{
  u32 w, h, w2, h2;
  int dec = 0;
  u8 *base = decode_file("tests/data/base.jpg", &w, &h, &dec), *o;
  ok("jpeg: baseline", base && w == 203 && h == 147 && dec);
  o = decode_file("tests/data/prog.jpg", &w2, &h2, &dec);
  ok("jpeg: progressive, the same as baseline", o && dec && w2 == w && h2 == h && !memcmp(o, base, (size_t)w * h * 3));
  free(o);
  o = decode_file("tests/data/cmyk.jpg", &w2, &h2, &dec);
  ok("jpeg: CMYK (Adobe, progressive)", o && dec && w2 == w && mean_diff(o, base, w * h * 3) < 8);   /* (the baseline subsamples its colour; this doesn't) */
  free(o);
  o = decode_file("tests/data/progrst.jpg", &w2, &h2, &dec);
  ok("jpeg: progressive with restarts", o && w2 == w && mean_diff(o, base, w * h * 3) < 6);
  free(o);
  o = decode_file("tests/data/rst.jpg", &w2, &h2, &dec);
  ok("jpeg: baseline with restarts", o && w2 == w && mean_diff(o, base, w * h * 3) < 6);
  free(o);
  free(base);
}
/* a part of a saved docx as text (malloc'd, NUL-ended), NULL if absent */
static Pos para_pos_t(u32 pi, u32 off) { Pos p; p.para = pi; p.off = off; return p; }
static char *zip_text(const u8 *data, size_t n, const char *name)
{
  ZipReader z;
  int i;
  size_t len;
  u8 *b;
  char *t;
  if (!zip_open(&z, data, n)) return NULL;
  if ((i = zip_find(&z, name)) < 0 || !(b = zip_read(&z, i, &len))) { zip_close(&z); return NULL; }
  zip_close(&z);
  if ((t = (char *)malloc(len + 1))) { memcpy(t, b, len); t[len] = 0; }
  free(b);
  return t;
}
static void style_model(void)
{
  Doc *d = doc_new(), *e;
  Pos c = doc_start(d), a, b;
  int h1 = doc_style(d, "Heading1"), strong = doc_style(d, "Strong"), arial = doc_font(d, "Arial"), cour = doc_font(d, "Courier New"), k, my;
  Style s;
  CharFmt f;
  u8 *data;
  size_t n;
  unsigned lost;
  char err[100], *xml;
  doc_insert(d, &c, "Title here\nBody text and more\nPlain", 34, NULL);
  doc_apply_style(d, 0, 0, h1);
  ok("styles: built-ins based on Normal, setting their own font and size", d->styles[h1].based == 0 && (d->styles[h1].cmask & CM_FONT) &&
     (d->styles[h1].cmask & CM_SIZE) && !(d->styles[h1].cmask & CM_COLOR) && d->styles[0].based == NO_STYLE);
  ok("styles: built-in character styles", strong > 0 && d->styles[strong].type == ST_CHAR && d->styles[strong].ecmask == CM_BOLD);
  /* direct formatting on "more" */
  a.para = 1; a.off = 14; b = a; b.off = 18;
  memset(&f, 0, sizeof f); f.font = (u8)cour;
  doc_format(d, a, b, CM_FONT, &f);
  /* Normal: Arial, red */
  s = d->styles[0];
  s.cmask |= CM_FONT | CM_COLOR; s.ocf.font = (u8)arial; s.ocf.color = 0xC00000;
  k = doc_set_style(d, 0, &s);
  ok("styles: Normal changed: its paragraphs follow", k == 0 && d->p[1]->r[0].f.font == arial && d->p[1]->r[0].f.color == 0xC00000 && d->p[2]->r[0].f.font == arial);
  ok("styles: text's own formatting kept", para_fmt_at(d->p[1], 15)->font == cour && para_fmt_at(d->p[1], 15)->color == 0xC00000);
  ok("styles: based on it: what it sets itself stays, the rest follows", d->styles[h1].cf.font == d->styles[h1].ocf.font && d->styles[h1].cf.color == 0xC00000 &&
     d->p[0]->r[0].f.color == 0xC00000 && d->p[0]->r[0].f.size == d->styles[h1].cf.size);
  doc_undo(d, &c);
  ok("styles: undone in one step: the styles and the text", d->styles[0].cf.font != arial && d->p[1]->r[0].f.font != arial && d->styles[h1].cf.color == COLOR_AUTO &&
     para_fmt_at(d->p[1], 15)->font == cour);
  doc_redo(d, &c);
  ok("styles: and redone", d->styles[0].cf.font == arial && d->p[2]->r[0].f.color == 0xC00000);
  /* a new style from the second paragraph, then its spacing */
  doc_style_from(d, para_pos_t(1, 0), 0, &s);
  strcpy(s.name, "My Body");
  s.pmask |= PM_SPACING; s.opf.before = 240; s.opf.after = 360;
  s.pmask |= PM_ALIGN; s.opf.align = AL_CENTER;
  my = doc_set_style(d, -1, &s);
  doc_apply_style(d, 1, 2, my);
  ok("styles: a new style, applied", my > 0 && !strcmp(d->styles[my].id, "MyBody") && d->p[2]->pf.style == my && d->p[2]->pf.after == 360 &&
     d->p[2]->pf.align == AL_CENTER && d->styles[my].based == 0);
  /* character style: Strong on "Body", then Strong made italic, not bold */
  a.para = 1; a.off = 0; b = a; b.off = 4;
  doc_char_style(d, a, b, strong);
  ok("styles: a character style on some text", para_fmt_at(d->p[1], 1)->cstyle == strong && (para_fmt_at(d->p[1], 1)->flags & CF_BOLD) &&
     !(para_fmt_at(d->p[1], 6)->flags & CF_BOLD));
  s = d->styles[strong];
  s.cmask = CM_ITALIC | CM_BOLD; s.ocf.flags = CF_ITALIC;
  doc_set_style(d, strong, &s);
  ok("styles: the character style changed: its text follows", (para_fmt_at(d->p[1], 1)->flags & (CF_BOLD | CF_ITALIC)) == CF_ITALIC);
  /* saved: based on, own settings only, the character style and the run's rStyle */
  data = docx_save(d, &n);
  xml = data ? zip_text(data, n, "word/styles.xml") : NULL;
  ok("styles: saved based on each other", xml && strstr(xml, "<w:style w:type=\"paragraph\" w:styleId=\"MyBody\"><w:name w:val=\"My Body\"/><w:basedOn w:val=\"Normal\"/>") &&
     strstr(xml, "<w:style w:type=\"character\" w:styleId=\"Strong\">"));
  free(xml);
  xml = data ? zip_text(data, n, "word/document.xml") : NULL;
  ok("styles: a run's character style saved", xml && strstr(xml, "<w:rStyle w:val=\"Strong\"/>"));
  free(xml);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  k = e ? doc_style(e, "Strong") : -1;
  ok("styles: read back based on each other, with their own settings", e && doc_style(e, "MyBody") > 0 && e->styles[doc_style(e, "MyBody")].based == 0 &&
     (e->styles[doc_style(e, "MyBody")].pmask & PM_SPACING) && e->styles[doc_style(e, "Heading1")].based == 0 && e->styles[0].cf.font == doc_font(e, "Arial"));
  ok("styles: and the character style on its text", k > 0 && e->styles[k].type == ST_CHAR && para_fmt_at(e->p[1], 1)->cstyle == k &&
     (para_fmt_at(e->p[1], 1)->flags & (CF_BOLD | CF_ITALIC)) == CF_ITALIC);
  free(data); doc_free(e);
  /* deleting the style: its paragraphs back to Normal; a style based on it keeps its look */
  doc_style_from(d, para_pos_t(1, 6), my, &s);
  strcpy(s.name, "Child"); s.pmask = s.cmask = 0;
  k = doc_set_style(d, -1, &s);
  ok("styles: based on a style that isn't Normal", k > my && d->styles[k].based == my && d->styles[k].pf.after == 360);
  ok("styles: a loop of bases refused", (s = d->styles[my], s.based = (u8)k, doc_set_style(d, my, &s)) < 0);
  ok("styles: deleted", doc_delete_style(d, my) && d->styles[my].type == ST_GONE && d->p[2]->pf.style == 0 && d->p[2]->pf.after == d->styles[0].pf.after &&
     d->p[2]->pf.align == AL_LEFT);
  ok("styles: the style based on it keeps its look", d->styles[k].based == 0 && d->styles[k].pf.after == 360 && d->styles[k].pf.align == AL_CENTER);
  doc_undo(d, &c);
  ok("styles: deleting undone", d->styles[my].type == ST_PARA && d->p[2]->pf.style == my && d->p[2]->pf.after == 360 && d->styles[k].based == my);
  ok("styles: Normal can't be deleted", !doc_delete_style(d, 0));
  doc_free(d);
}
static void para_char_formats(void)
{
  Doc *d = doc_new(), *e;
  Layout *L;
  Pos c = doc_start(d), a, b;
  CharFmt f;
  ParaFmt pf;
  u8 *data;
  size_t n;
  unsigned lost;
  char err[100];
  int i;
  i32 y1, y2, w0, w1;
  doc_insert(d, &c, "Plain words here\nOne\nTwo\nThree\nBoxed one\nBoxed two\nEnd", 54, NULL);
  L = lay_new(d, &fs);
  lay_update(L);
  w0 = L->pl[0].boxes[0].w;
  y1 = L->placed[L->pl[2].placed0].y - L->placed[L->pl[1].placed0].y;
  /* spacing, scale, small caps */
  memset(&f, 0, sizeof f);
  a.para = b.para = 0; a.off = 0; b.off = 5;
  f.spacing = 40; doc_format(d, a, b, CM_SPACING, &f);
  lay_update(L);
  w1 = L->pl[0].boxes[0].w;
  ok("formats: character spacing widens the word by 5 x 2 points", w1 == w0 + 5 * 40);
  f.scale = 200; doc_format(d, a, b, CM_SCALE, &f);
  lay_update(L);
  ok("formats: scale doubles it (the spacing as it was)", L->pl[0].boxes[0].w >= w0 * 2 + 5 * 40 - 2 && L->pl[0].boxes[0].w <= w0 * 2 + 5 * 40 + 2);
  f.spacing = 0; f.scale = 0; doc_format(d, a, b, CM_SPACING | CM_SCALE, &f);
  f.fx = FX_SMALLCAPS; doc_format(d, a, b, CM_SMALLCAPS, &f);
  lay_update(L);
  ok("formats: small capitals: wider than lower case, narrower than capitals", L->pl[0].boxes[0].w > w0);
  f.position = 6; doc_format(d, a, b, CM_POSITION, &f);
  lay_update(L);
  ok("formats: raised text makes the line taller", L->pl[0].lines[0].ascent > L->pl[6].lines[0].ascent);
  /* contextual spacing */
  memset(&pf, 0, sizeof pf); pf.contextual = 1;
  doc_para_format(d, 1, 3, PM_CONTEXT, &pf);
  lay_update(L);
  y2 = L->placed[L->pl[2].placed0].y - L->placed[L->pl[1].placed0].y;
  ok("formats: contextual spacing: no space between paragraphs of one style", y2 == y1 - d->styles[0].pf.after - d->styles[0].pf.before && y2 == L->pl[1].lines[0].height);
  /* a box round two paragraphs, with a line between */
  memset(&pf, 0, sizeof pf);
  for (i = 0; i < PE_N; i++) { pf.bstyle[i] = PB_SINGLE; pf.bwidth[i] = 8; pf.bspace[i] = 2; pf.bcolor[i] = 0x2F5496; }
  pf.shade = HL_ON | 0xDEEAF6;
  y1 = L->placed[L->pl[4].placed0].y;
  doc_para_format(d, 4, 5, PM_BORDER | PM_SHADE, &pf);
  lay_update(L);
  ok("formats: a border box: its top line's room above the text", L->placed[L->pl[4].placed0].y == y1 + 40 + 20);
  ok("formats: one box for each paragraph, joined, a line between them", L->npbox == 2 && (L->pbox[0].lines & PB_TOPLINE) && !(L->pbox[0].lines & PB_BOTTOMLINE) &&
     (L->pbox[1].lines & (PB_BETWEENLINE | PB_BOTTOMLINE)) == (PB_BETWEENLINE | PB_BOTTOMLINE) && L->pbox[1].y0 == L->pbox[0].y1 &&
     L->pbox[0].x0 == d->page.left - 40 - 20);
  /* keep lines together, widow control off: saved and read back */
  pf.keeplines = 1; pf.nowidow = 1;
  doc_para_format(d, 6, 6, PM_PAGINATION, &pf);
  a.para = b.para = 6; a.off = 0; b.off = 3;
  memset(&f, 0, sizeof f);
  f.flags = CF_UNDERLINE; f.ul = UL_WAVE; f.fx = FX_DSTRIKE; f.spacing = -10; f.scale = 80; f.position = -4;
  doc_format(d, a, b, CM_UNDERLINE | CM_UL | CM_DSTRIKE | CM_SPACING | CM_SCALE | CM_POSITION, &f);
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("formats: borders, shading, contextual spacing and keeping lines together saved", e && e->np == d->np && e->p[4]->pf.bstyle[PE_BETWEEN] == PB_SINGLE &&
     e->p[4]->pf.bwidth[PE_TOP] == 8 && e->p[4]->pf.bspace[PE_LEFT] == 2 && e->p[5]->pf.bcolor[PE_RIGHT] == 0x2F5496 && e->p[5]->pf.shade == (HL_ON | 0xDEEAF6) &&
     e->p[2]->pf.contextual && e->p[6]->pf.keeplines && e->p[6]->pf.nowidow && !e->p[5]->pf.keeplines);
  {
    const CharFmt *g = e ? para_fmt_at(e->p[6], 1) : NULL, *g0 = e ? para_fmt_at(e->p[0], 1) : NULL;
    ok("formats: underline kinds, double strike, spacing, scale, position and small capitals saved", g && g->ul == UL_WAVE && (g->flags & CF_UNDERLINE) &&
       (g->fx & FX_DSTRIKE) && g->spacing == -10 && g->scale == 80 && g->position == -4 && g0 && (g0->fx & FX_SMALLCAPS) && g0->position == 6);
  }
  free(data); doc_free(e);
  lay_free(L);
  doc_free(d);
}
static void lists_sets_paste(void)
{
  Doc *d = doc_new(), *e;
  Layout *L;
  Pos c = doc_start(d), a, b;
  Fragment *f;
  CharFmt cf;
  int l, h1 = doc_style(d, "Heading1"), h2 = doc_style(d, "Heading2"), k;
  u32 n;
  const char *ft;
  { static const char *t0 = "Intro\nOne\nTwo\nThree\nFour\nChapter\nSection\nChapter two"; doc_insert(d, &c, t0, (u32)strlen(t0), NULL); }
  /* a legal list on One..Four, Two a level down */
  l = doc_list_preset(d, LP_LEGAL);
  ok("lists: a ready-made kind", l > 0 && !strcmp(d->lists[l - 1].lvl[2].text, "%1.%2.%3.") && d->lists[l - 1].lvl[1].left == 792);
  doc_list_apply(d, 1, 4, l);
  { ParaFmt pf = d->p[2]->pf; pf.level = 1; doc_para_format(d, 2, 2, PM_LIST, &pf); }
  L = lay_new(d, &fs);
  lay_update(L);
  ok("lists: numbered 1. 1.1. 2.", !strcmp(L->pl[1].label, "1.") && !strcmp(L->pl[2].label, "1.1.") && !strcmp(L->pl[3].label, "2.") && d->p[1]->pf.left == 360);
  doc_list_restart(d, 3, 5);
  lay_update(L);
  ok("lists: numbering started again at 5", !strcmp(L->pl[3].label, "5.") && !strcmp(L->pl[4].label, "6.") && d->p[3]->pf.list != l && d->p[4]->pf.list == d->p[3]->pf.list);
  doc_list_continue(d, 3);
  lay_update(L);
  ok("lists: and carried on from the list before", !strcmp(L->pl[3].label, "2.") && d->p[3]->pf.list == l);
  /* the headings numbered by their styles */
  doc_apply_style(d, 5, 5, h1); doc_apply_style(d, 6, 6, h2); doc_apply_style(d, 7, 7, h1);
  ok("lists: heading numbering through the styles", doc_heading_numbering(d, LP_HEADINGS) && d->styles[h1].pf.list && d->p[5]->pf.list == d->styles[h1].pf.list &&
     d->p[6]->pf.level == 1);
  lay_update(L);
  ok("lists: headings 1, 1.1, 2", !strcmp(L->pl[5].label, "1") && !strcmp(L->pl[6].label, "1.1") && !strcmp(L->pl[7].label, "2"));
  doc_undo(d, &c);
  lay_update(L);
  ok("lists: heading numbering undone at once", !d->styles[h1].pf.list && !d->p[5]->pf.list && !L->pl[5].label[0]);
  /* the document's look */
  ok("design: fonts for body and headings", doc_theme_fonts(d, TF_CALIBRI_CAMBRIA) && d->styles[0].cf.font == doc_font(d, "Calibri") &&
     d->styles[h1].cf.font == doc_font(d, "Cambria") && d->p[0]->r[0].f.font == doc_font(d, "Calibri") && d->p[5]->r[0].f.font == doc_font(d, "Cambria"));
  ok("design: paragraph spacing", doc_para_spacing(d, PS_DOUBLE) && d->styles[0].pf.line == 480 && d->p[0]->pf.line == 480 && d->styles[h1].pf.line == 480);
  ok("design: a style set", doc_style_set(d, SS_BOLD) && d->styles[h1].cf.color == 0xC00000 && d->p[5]->r[0].f.color == 0xC00000 && d->styles[0].pf.after == 160);
  doc_undo(d, &c);
  ok("design: a style set undone at once", d->styles[h1].cf.color == COLOR_AUTO && d->styles[0].pf.line == 480);
  /* copying with formatting: "ntro" bold, then pasted */
  a.para = 0; a.off = 1; b = a; b.off = 5;
  memset(&cf, 0, sizeof cf); cf.flags = CF_BOLD;
  doc_format(d, a, b, CM_BOLD, &cf);
  b.para = 1; b.off = 3;
  f = doc_fragment(d, a, b);
  ft = f ? fragment_text(f, &n) : NULL;
  ok("paste: a fragment's text", ft && n == 8 && !memcmp(ft, "ntro\nOne", 8));
  c.para = 7; c.off = 7;                      /* "Chapter| two" */
  ok("paste: kept formatting", f && doc_paste(d, &c, f, PASTE_KEEP) && d->np == 9 && !strcmp(d->p[7]->t, "Chapterntro") && !strcmp(d->p[8]->t, "One two") &&
     (para_fmt_at(d->p[7], 8)->flags & CF_BOLD) && d->p[7]->pf.style == 0 && d->p[8]->pf.style == h1 && c.para == 8 && c.off == 3);
  doc_undo(d, &c);
  ok("paste: undone", d->np == 8 && !strcmp(d->p[7]->t, "Chapter two"));
  c.para = 7; c.off = 7;
  doc_paste(d, &c, f, PASTE_TEXT);
  ok("paste: text only (the formatting where it goes)", d->p[7]->pf.style == h1 && d->p[8]->pf.style == h1 && para_fmt_at(d->p[7], 8)->font == para_fmt_at(d->p[7], 0)->font &&
     para_fmt_at(d->p[7], 8)->flags == para_fmt_at(d->p[7], 0)->flags && para_fmt_at(d->p[7], 8)->size == para_fmt_at(d->p[7], 0)->size);
  doc_undo(d, &c);
  /* into another document: fonts and styles found by name */
  e = doc_new();
  c = doc_start(e);
  k = doc_paste(e, &c, f, PASTE_KEEP);
  ok("paste: into another document", k && e->np == 2 && !strcmp(e->p[0]->t, "ntro") && (para_fmt_at(e->p[0], 1)->flags & CF_BOLD) &&
     !strcmp(e->fonts[para_fmt_at(e->p[0], 1)->font], d->fonts[para_fmt_at(d->p[0], 1)->font]) && e->p[1]->pf.style == 0);
  doc_free(e);
  fragment_free(f);
  lay_free(L);
  doc_free(d);
}
/* the order floating objects stack in: to the front, to the back, one up; undone; saved as Word's relativeHeight */
static void stacking(void)
{
  Doc *d = doc_new(), *e;
  Pos c = doc_start(d);
  Layout *L;
  int k[3], i;
  u32 *o;
  char ref[8], err[200];
  unsigned lost;
  size_t n;
  u8 *data;
  doc_insert(d, &c, "Three shapes on top of each other.", 34, NULL);
  c = doc_start(d);
  for (i = 0; i < 3; i++) {
    k[i] = doc_add_shape(d, SH_RECT, 2000, 2000);
    d->images[k[i]].wrap = WRAP_FRONT; d->images[k[i]].hrel = POS_PAGE; d->images[k[i]].vrel = POS_PAGE;
    d->images[k[i]].x = 2000 + i * 500; d->images[k[i]].y = 2000 + i * 500;
    pic_utf8(ref, k[i]);
    doc_insert(d, &c, ref, 4, NULL);
  }
  L = lay_new(d, &fs); lay_update(L);
  o = lay_stack_order(L, L->floats, L->nfloats);
  ok("stacking: by default in the order the anchors come", L->nfloats == 3 && o && o[0] == 0 && o[1] == 1 && o[2] == 2);
  free(o);
  ok("stacking: the first brought to the front", lay_restack(L, 0, 2) && lay_update(L) >= 0 && (o = lay_stack_order(L, L->floats, L->nfloats)) && o[2] == 0 && o[0] == 1);
  free(o);
  ok("stacking: already on top: nothing to do", lay_restack(L, 0, 2) == 0 && lay_restack(L, 0, 1) == 0);
  ok("stacking: sent one down, then to the back", lay_restack(L, 0, -1) && lay_update(L) >= 0 && (o = lay_stack_order(L, L->floats, L->nfloats)) && o[1] == 0);
  free(o);
  lay_restack(L, 0, -2); lay_update(L);
  o = lay_stack_order(L, L->floats, L->nfloats);
  ok("stacking: at the back", o && o[0] == 0);
  free(o);
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  if (e) {
    Layout *M = lay_new(e, &fs);
    lay_update(M);
    o = lay_stack_order(M, M->floats, M->nfloats);
    ok("stacking: the order survives saving", o && M->nfloats == 3 && M->floats[o[0]].x == 2000 && M->floats[o[1]].x == 2500 && M->floats[o[2]].x == 3000);
    free(o); lay_free(M); doc_free(e);
  } else ok("stacking: the order survives saving", 0);
  free(data);
  doc_undo(d, &c); lay_update(L);
  o = lay_stack_order(L, L->floats, L->nfloats);
  ok("stacking: undone in one step (back to its place second)", o && o[0] == 1 && o[1] == 0 && o[2] == 2);
  free(o); lay_free(L); doc_free(d);
}
static void floats_layout(void)
{
  static const char *T = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. ";
  Doc *d = doc_new();
  Pos c = doc_start(d);
  Layout *L;
  i32 body = d->page.width - d->page.left - d->page.right;
  int k, i;
  char ref[8];
  u32 ln, narrow = 0, wide = 0, bad = 0;
  for (i = 0; i < 12; i++) doc_insert(d, &c, T, (u32)strlen(T), NULL);
  k = doc_add_shape(d, SH_RECT, 3000, 2000);
  d->images[k].wrap = WRAP_SQUARE; d->images[k].hrel = POS_MARGIN; d->images[k].halign = PA_END; d->images[k].vrel = POS_PARA; d->images[k].y = 0;
  pic_utf8(ref, k);
  c = doc_start(d);
  doc_insert(d, &c, ref, 4, NULL);
  L = lay_new(d, &fs);
  lay_update(L);
  ok("floats: placed at the margin's right, from its paragraph's top", L->nfloats == 1 && L->floats[0].x == d->page.left + body - 3000 &&
     L->floats[0].y == L->placed[L->pl[0].placed0].y && L->floats[0].w == 3000);
  for (ln = 0; ln < L->pl[0].nlines; ln++) {
    Placed *pc = &L->placed[L->pl[0].placed0 + ln];
    Line *li = &L->pl[0].lines[ln];
    i32 right = d->page.left + pc->x + li->left + li->width;
    if (pc->y + li->height <= L->floats[0].y + 2000) { narrow++; if (right > L->floats[0].x - 180) bad++; }
    else { if (li->width > body - 3000) wide++; }
  }
  ok("floats: the lines beside it stop short of it, those below run the full width", narrow >= 3 && wide >= 3 && !bad && L->pl[0].shaped);
  /* top and bottom: no text beside it */
  d->images[k].wrap = WRAP_TOPBOTTOM;
  L->pl[0].ver = 0;                                  /* (the image changed in place: lay out afresh) */
  lay_update(L);
  ok("floats: top and bottom: the first line below it", L->pl[0].nlines && L->placed[L->pl[0].placed0].y + L->pl[0].lines[0].height >= L->floats[0].y + 2000);
  d->images[k].wrap = WRAP_BEHIND;
  L->pl[0].ver = 0;
  lay_update(L);
  ok("floats: behind the text: the lines as if it weren't there", L->pl[0].lines[0].width > body - 600 && !L->pl[0].shaped && L->nfloats == 1 && L->floats[0].wrap == WRAP_BEHIND);
  /* page-relative, centred on the page */
  d->images[k].hrel = POS_PAGE; d->images[k].halign = PA_CENTER; d->images[k].vrel = POS_PAGE; d->images[k].valign = PA_CENTER;
  L->pl[0].ver = 0;
  lay_update(L);
  ok("floats: centred on the page", L->floats[0].x == (d->page.width - 3000) / 2 && L->floats[0].y == (d->page.height - 2000) / 2);
  /* dragged somewhere else on the page (as the view does it): there after laying out again */
  {
    Image m = d->images[k], orig = d->images[k];
    Pos at;
    int nk;
    lay_float_moved(L, &L->floats[0], &m, 2000, 5000);
    obj_in(d, 0, &at);
    nk = doc_set_object(d, at, &m);
    lay_update(L);
    ok("floats: dragged to a new place", nk > k && L->nfloats == 1 && L->floats[0].x == 2000 && L->floats[0].y == 5000);
    k = nk;
    m = d->images[k]; m.vrel = POS_PARA; m.valign = PA_START;
    nk = doc_set_object(d, at, &m);
    lay_update(L);
    m = d->images[nk];
    lay_float_moved(L, &L->floats[0], &m, 2500, L->floats[0].y + 700);
    ok("floats: dragged down from its paragraph", m.vrel == POS_PARA && m.valign == PA_NONE && m.y == 700);
    doc_set_object(d, at, &m);
    k = doc_set_object(d, at, &orig);                      /* back as it was (centred on the page) */
  }
  /* turned a quarter: its width and height swap */
  d->images[k].rot = 900;
  L->pl[0].ver = 0;
  lay_update(L);
  ok("floats: turned a quarter, its box turns", L->floats[0].w == 2000 && L->floats[0].h == 3000);
  lay_free(L);
  doc_free(d);
  /* placed from the margin's top: the lines above its anchor wrap round it too */
  {
    Doc *q = doc_new();
    Layout *M;
    Pos e = doc_start(q);
    char r[8];
    int t;
    for (i = 0; i < 3; i++) { doc_insert(q, &e, T, (u32)strlen(T), NULL); doc_insert(q, &e, "\n", 1, NULL); }
    t = doc_add_shape(q, SH_RECT, 3000, 3000);
    q->images[t].wrap = WRAP_SQUARE; q->images[t].hrel = POS_MARGIN; q->images[t].halign = PA_END; q->images[t].vrel = POS_MARGIN; q->images[t].valign = PA_START;
    pic_utf8(r, t);
    e.para = 2; e.off = 0;
    doc_insert(q, &e, r, 4, NULL);
    M = lay_new(q, &fs);
    lay_update(M);
    ok("floats: from the margin's top, the first paragraph wraps round it", M->nfloats == 1 && M->floats[0].y == q->page.top &&
       M->pl[0].lines[0].width <= q->page.width - q->page.left - q->page.right - 3000);
    lay_update(M);
    ok("floats: and stays so laid out again", M->nfloats == 1 && M->pl[0].lines[0].width <= q->page.width - q->page.left - q->page.right - 3000);
    lay_free(M);
    doc_free(q);
  }
  /* a text box's text */
  {
    Doc *q = doc_new();
    Layout *M;
    Furn f;
    int t = doc_add_shape(q, -1, 2880, 1440);
    story_set_text(q, q->images[t].text, "In the box", 0, AL_LEFT);
    M = lay_new(q, &fs);
    ok("floats: a text box's text laid out inside it", lay_textbox(M, &M->d->images[t], 0, &f) && f.n == 1 && f.f[0].y >= TB_INSET_Y && f.f[0].pl.lines[0].left >= TB_INSET_X);
    lay_furniture_free(&f);
    {
      Story st = { NULL, 0 };
      char *back;
      ok("floats: a text box's text in paragraphs", story_set_paras(q, &st, "One\nTwo\n", q->images[t].text) && st.np == 3 &&
         !strcmp(st.p[1]->t, "Two") && st.p[2]->len == 0);
      back = story_paras(&st);
      ok("floats: and back", back && !strcmp(back, "One\nTwo\n"));
      free(back);
      story_free(&st);
    }
    lay_free(M);
    doc_free(q);
  }
}
static void header_rows(void)
{
  Doc *d = doc_new();
  Layout *L = lay_new(d, &fs);
  Pos c = doc_start(d);
  u32 i, k, pages_with_header = 0;
  tbl_insert(d, &c, 80, 2, 9000);
  doc_insert(d, &c, "Heading", 7, NULL);
  for (i = 0; i < 2; i++) { ParaFmt pf = d->p[i]->pf; pf.cell.header = 1; doc_para_format(d, i, i, PM_CELL, &pf); }
  lay_update(L);
  ok("header rows: the table runs over pages", L->npages >= 2);
  for (k = 0; k < L->npages; k++) {
    u32 a, b;
    lay_page_lines(L, k, &a, &b);
    for (i = a; i < b; i++) if (L->placed[i].page == k && L->placed[i].para == 0) { pages_with_header++; break; }
  }
  ok("header rows: repeated at the top of every page", pages_with_header == L->npages);
  ok("header rows: the caret stays with the first", L->placed[L->pl[0].placed0].page == 0);
  {
    size_t n; u8 *data = docx_save(d, &n); char err[100]; unsigned lost;
    Doc *e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
    ok("header rows: saved and read back", e && e->p[0]->pf.cell.header && e->p[1]->pf.cell.header && !e->p[2]->pf.cell.header);
    free(data); doc_free(e);
  }
  lay_free(L); doc_free(d);
}
static void line_breaks(void)
{
  Doc *d = doc_new(), *e;
  Layout *L = lay_new(d, &fs);
  Pos c = doc_start(d);
  size_t n;
  u8 *data;
  char err[100];
  unsigned lost;
  doc_insert(d, &c, "one\vtwo\v", 8, NULL);
  lay_update(L);
  ok("breaks: a line break starts a line", d->np == 1 && L->pl[0].nlines == 3 && L->pl[0].lines[1].start == 4 && L->pl[0].lines[2].start == 8);
  ok("breaks: the end of a broken line is before the break", lay_line_end(L, P(0, 1)).off == 3);
  { u32 pg; i32 x, y, y2, h; lay_caret(L, P(0, 8), &pg, &x, &y2, &h); lay_caret(L, P(0, 4), &pg, &x, &y, &h);
    ok("breaks: the caret after a final break is on a line of its own", y2 > y && x == d->page.left); }
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("breaks: saved as w:br and read back", e && e->np == 1 && !strcmp(e->p[0]->t, "one\vtwo\v"));
  free(data); doc_free(e);
  data = txt_save(d, 1, &n);
  ok("breaks: in text files, new lines", data && n == 8 && !memcmp(data, "one\ntwo\n", 8));
  free(data);
  lay_free(L); doc_free(d);
}

static void headers_footers(void)
{
  char err[200];
  unsigned lost;
  Doc *d = doc_load_file("tests/data/headers.docx", &lost, err, sizeof err), *e;
  Layout *L;
  Furn f;
  size_t n;
  u8 *data;
  char *t;
  if (!d) { ok("headers: loads", 0); return; }
  ok("headers: read, with a first page of its own", d->titlepg && d->hdr[HF_DEFAULT].np == 1 && !strcmp(d->hdr[HF_DEFAULT].p[0]->t, "Quill header: The Amiga Report") &&
     d->hdr[HF_FIRST].np == 1 && !strcmp(d->hdr[HF_FIRST].p[0]->t, "First page header") && !(lost & LOST_HEADERS));
  ok("headers: the footer's page fields", d->ftr[HF_DEFAULT].np == 1 && !strcmp(d->ftr[HF_DEFAULT].p[0]->t, "Page " FIELD_PAGE " of " FIELD_PAGES) &&
     d->ftr[HF_DEFAULT].p[0]->pf.align == AL_CENTER && !d->ftr[HF_FIRST].np);
  L = lay_new(d, &fs);
  lay_update(L);
  ok("headers: page 2's footer counts", lay_furniture(L, 1, 1, &f) && f.n == 1 && !strcmp(f.f[0].p->t, "Page 2 of 3") &&
     f.f[0].y + f.f[0].pl.lines[0].height == d->page.height - d->page.footer);
  lay_furniture_free(&f);
  ok("headers: none on page 1's footer, the first header", !lay_furniture(L, 0, 1, &f) && lay_furniture(L, 0, 0, &f) && !strcmp(f.f[0].p->t, "First page header") && f.f[0].y == d->page.header);
  lay_furniture_free(&f);
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("headers: saved and read back", e && e->titlepg && e->hdr[HF_FIRST].np == 1 && !strcmp(e->ftr[HF_DEFAULT].p[0]->t, d->ftr[HF_DEFAULT].p[0]->t) &&
     e->ftr[HF_DEFAULT].p[0]->pf.align == AL_CENTER && e->page.header == d->page.header);
  free(data); doc_free(e);
  t = story_text(&d->ftr[HF_DEFAULT]);
  ok("headers: as text for editing", t && !strcmp(t, "Page {page} of {pages}"));
  free(t);
  ok("headers: set from text", story_set_text(d, &d->hdr[HF_DEFAULT], "Draft {page}", 0, AL_RIGHT) && !strcmp(d->hdr[HF_DEFAULT].p[0]->t, "Draft " FIELD_PAGE));
  /* a tall header pushes the text down */
  story_set_text(d, &d->hdr[HF_DEFAULT], "A\tvery tall header that goes on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on", 0, AL_LEFT);
  lay_update(L);
  ok("headers: a tall header pushes the text down", L->placed[L->page_first[1]].y > d->page.top);
  lay_free(L); doc_free(d);
}

/* headers, footers, notes, comments and text boxes edited in the body's place */
static void story_editing(void)
{
  char err[200];
  unsigned lost;
  Doc *d = doc_load_file("tests/data/headers.docx", &lost, err, sizeof err);
  Layout *L;
  u32 np0, k;
  Pos a, b;
  int found = 0, n, can0;
  char num[16];
  if (!d) { ok("stories: loads", 0); return; }
  np0 = d->np; can0 = doc_can_undo(d);
  ok("stories: a header in the body's place", doc_story_begin(d, SK_HEADER, HF_DEFAULT, 0, 1, 3) && d->np == 1 &&
     !strcmp(d->p[0]->t, "Quill header: The Amiga Report") && !d->hdr[HF_DEFAULT].np && !doc_can_undo(d) && d->page.top == d->edit.page.header);
  ok("stories: only one at a time", !doc_story_begin(d, SK_FOOTER, HF_DEFAULT, 0, 0, 0));
  a.para = 0; a.off = 0; b.para = 0; b.off = d->p[0]->len;
  doc_toggle(d, a, b, CF_BOLD);
  doc_insert(d, &b, " - " FIELD_PAGE, 6, NULL);
  L = lay_new(d, &fs);
  lay_update(L);
  for (k = 0; k < L->pl[0].nboxes; k++)
    if (L->pl[0].boxes[k].kind == BX_FIELD) { lay_field_text(L, d->p[0], &L->pl[0].boxes[k], 0, num); found = !strcmp(num, "2") && L->pl[0].boxes[k].w > 0; }
  ok("stories: a page field shows the page it was opened from", found && L->npages == 1);
  ok("stories: undo inside the story", doc_can_undo(d) && doc_undo(d, &a) && d->p[0]->len == 30 && doc_redo(d, &a));
  ok("stories: done, the header formatted", doc_story_end(d, 1) && d->np == np0 && doc_can_undo(d) == can0 && d->hdr[HF_DEFAULT].np == 1 &&
     (d->hdr[HF_DEFAULT].p[0]->r[0].f.flags & CF_BOLD) && !strcmp(d->hdr[HF_DEFAULT].p[0]->t + d->hdr[HF_DEFAULT].p[0]->len - 3, FIELD_PAGE) && d->dirty);
  lay_update(L);
  {
    Furn f;
    ok("stories: the edited header on the page", lay_furniture(L, 1, 0, &f) && f.n == 1 && !strcmp(f.f[0].p->t + f.f[0].p->len - 4, " - 2"));
    lay_furniture_free(&f);
  }
  doc_story_begin(d, SK_FOOTER, HF_DEFAULT, 0, 0, 3);
  a.para = 0; a.off = 0; b.para = 0; b.off = d->p[0]->len;
  doc_delete(d, a, b);
  doc_story_end(d, 0);
  ok("stories: dropped, the footer as it was", d->ftr[HF_DEFAULT].np == 1 && !strcmp(d->ftr[HF_DEFAULT].p[0]->t, "Page " FIELD_PAGE " of " FIELD_PAGES));
  doc_story_begin(d, SK_FOOTER, HF_DEFAULT, 0, 0, 3);
  a.para = 0; a.off = 0; b.para = 0; b.off = d->p[0]->len;
  doc_delete(d, a, b);
  doc_story_end(d, 1);
  ok("stories: emptied, no footer", !d->ftr[HF_DEFAULT].np);
  ok("stories: a new even page header in its style", doc_story_begin(d, SK_HEADER, HF_EVEN, 0, 0, 1) && d->np == 1 && !d->p[0]->len &&
     (doc_style(d, "Header") < 0 || d->p[0]->pf.style == doc_style(d, "Header")) && doc_story_end(d, 1) && !d->hdr[HF_EVEN].np);
  lay_free(L);
  /* left open: freed with the document */
  doc_story_begin(d, SK_HEADER, HF_FIRST, 0, 0, 1);
  doc_free(d);
  /* notes, comments, text boxes */
  d = doc_new();
  b = doc_start(d);
  doc_insert(d, &b, "Body text", 9, NULL);
  n = doc_add_note(d);
  ok("stories: a new footnote, ten point", n >= 0 && doc_story_begin(d, SK_NOTE, n, 0, 0, 1) && d->np == 1 && d->p[0]->r[0].f.size == 20);
  b = doc_start(d);
  doc_insert(d, &b, "First\nSecond", 12, NULL);
  ok("stories: its two paragraphs kept", doc_story_end(d, 1) && d->notes[n].np == 2 && !strcmp(d->notes[n].p[1]->t, "Second") && !strcmp(d->p[0]->t, "Body text"));
  a.para = 0; a.off = 0; b.para = 0; b.off = 4;
  n = doc_comment(d, a, b, "Plain", "Me", "M", "2026-10-25T00:00:00Z");
  doc_story_begin(d, SK_COMMENT, n, 0, 0, 1);
  a.para = 0; a.off = 0; b.para = 0; b.off = 5;
  doc_toggle(d, a, b, CF_ITALIC);
  doc_story_end(d, 1);
  ok("stories: a comment's text in italics", n >= 0 && d->comments[n].text.np == 1 && (d->comments[n].text.p[0]->r[0].f.flags & CF_ITALIC));
  n = doc_add_shape(d, -1, 2880, 1440);
  ok("stories: a text box's text as wide as it", n >= 0 && doc_story_begin(d, SK_TEXTBOX, n, 0, 0, 1) && d->page.width - d->page.left - d->page.right == 2880 - 288);
  b = doc_start(d);
  doc_insert(d, &b, "Boxed", 5, NULL);
  ok("stories: the text box's text", doc_story_end(d, 1) && d->images[n].text && d->images[n].text->np == 1 && !strcmp(d->images[n].text->p[0]->t, "Boxed") &&
     d->page.width == 11906);
  ok("stories: nothing to end", !doc_story_end(d, 1) && !doc_story_begin(d, SK_NOTE, 99, 0, 0, 1));
  doc_free(d);
}
/* sections with their own page setup, headers and footers */
static void section_pages(void)
{
  Doc *d = doc_new(), *e;
  SectFmt s;
  ParaFmt pf;
  Layout *L;
  Pos p = doc_start(d);
  Furn f;
  u8 *data;
  size_t n;
  unsigned lost;
  char err[200];
  doc_insert(d, &p, "Wide page\nTall page\nAnd more " FIELD_PAGE, 33, NULL);
  sect_default(&s);
  s.haspage = 1; s.page = d->page; s.page.width = d->page.height; s.page.height = d->page.width;
  memset(&pf, 0, sizeof pf);
  pf.sect = (u16)doc_add_sect(d, &s);
  doc_para_format(d, 0, 0, PM_SECT, &pf);
  L = lay_new(d, &fs);
  lay_update(L);
  ok("section pages: landscape, then the document's", L->npages == 2 && lay_page(L, 0)->width == 16838 && lay_page(L, 1)->width == 11906 &&
     L->pinfo[1].first == 1 && L->pinfo[1].last && !L->pinfo[0].last && L->pl[0].avail == 16838 - 2880);
  {
    u32 k, pg;
    i32 x, y, h;
    char num[16];
    lay_caret(L, doc_start(d), &pg, &x, &y, &h);
    ok("section pages: the caret on the wide page", pg == 0 && x == 1440);
    for (k = 0; k < L->pl[2].nboxes && L->pl[2].boxes[k].kind != BX_FIELD; k++) ;
    ok("section pages: a page field in the body", k < L->pl[2].nboxes && (lay_field_text(L, d->p[2], &L->pl[2].boxes[k], 1, num), !strcmp(num, "2")));
  }
  ok("section pages: own headers", doc_sect_own_hf(d, 1, 1) && d->sects[0].hf && story_set_text(d, &d->sects[0].hf[HF_DEFAULT], "Wide header", 0, AL_LEFT) &&
     story_set_text(d, &d->hdr[HF_DEFAULT], "Document header", 0, AL_LEFT));
  d->sects[0].titlepg = 1;
  story_set_text(d, &d->sects[0].hf[HF_FIRST], "Cover", 0, AL_LEFT);
  d->titlepg = 1;
  story_set_text(d, &d->hdr[HF_FIRST], "Last first", 0, AL_LEFT);
  lay_update(L);
  ok("section pages: each section's first page header", lay_furniture(L, 0, 0, &f) && !strcmp(f.f[0].p->t, "Cover"));
  lay_furniture_free(&f);
  ok("section pages: the last section's own first page", lay_furniture(L, 1, 0, &f) && !strcmp(f.f[0].p->t, "Last first"));
  lay_furniture_free(&f);
  d->sects[0].titlepg = 0; d->titlepg = 0;
  lay_update(L);
  ok("section pages: and their usual ones", lay_furniture(L, 0, 0, &f) && !strcmp(f.f[0].p->t, "Wide header") && (lay_furniture_free(&f), lay_furniture(L, 1, 0, &f)) &&
     !strcmp(f.f[0].p->t, "Document header"));
  lay_furniture_free(&f);
  ok("section pages: editing a section's header", doc_story_begin(d, SK_HEADER, HF_DEFAULT, 1, 0, 2) && d->page.width == 16838 && doc_story_end(d, 1));
  d->sects[0].titlepg = 1;
  data = pdf_export(L, &n);
  ok("section pages: PDF pages of each size", data && memmem(data, n, "/MediaBox [0 0 841.90 595.30]", 29) && memmem(data, n, "/MediaBox [0 0 595.30 841.90]", 29));
  free(data);
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("section pages: saved and read back", e && e->p[0]->pf.sect && doc_sect(e, 0)->haspage && doc_sect(e, 0)->page.width == 16838 && doc_sect(e, 0)->titlepg &&
     doc_sect(e, 0)->hf && !strcmp(doc_sect(e, 0)->hf[HF_DEFAULT].p[0]->t, "Wide header") && !strcmp(doc_sect(e, 0)->hf[HF_FIRST].p[0]->t, "Cover") &&
     e->hdr[HF_DEFAULT].np && !strcmp(e->hdr[HF_DEFAULT].p[0]->t, "Document header") && e->page.width == 11906 && !e->titlepg &&
     e->np == 3 && !strcmp(e->p[2]->t, "And more " FIELD_PAGE));
  free(data); doc_free(e);
  /* a section with a footer where the last has none: the last's stays empty (not the one before's) */
  story_set_text(d, &d->sects[0].hf[HF_N + HF_DEFAULT], "Wide footer", 0, AL_LEFT);
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("section pages: a footer only the first section has", e && doc_sect(e, 0)->hf && doc_sect(e, 0)->hf[HF_N].np && !e->ftr[HF_DEFAULT].np);
  free(data); doc_free(e);
  /* back to the document's: the section has the same headers as the last, so none of its own when read */
  doc_sect_own_hf(d, 1, 0);
  d->sects[0].haspage = 0;
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("section pages: the document's again", e && !doc_sect(e, 0)->hf && !doc_sect(e, 0)->haspage && e->hdr[HF_DEFAULT].np);
  if (e) { lay_free(L); L = lay_new(e, &fs); lay_update(L); }
  ok("section pages: one size", e && L->npages == 2 && lay_page(L, 0)->width == 11906 && lay_furniture(L, 0, 0, &f) && !strcmp(f.f[0].p->t, "Last first"));
  lay_furniture_free(&f);
  free(data); doc_free(e);
  lay_free(L);
  doc_free(d);
}

/* pictures and shapes turned by any angle, more shapes, groups, and wrapping round outlines (0.11) */
static void angles_groups(void)
{
  Doc *d = doc_new(), *e;
  Layout *L;
  Pos p = doc_start(d), at[2];
  Image im;
  ObjPart parts[8];
  i32 u, v, bx, by, box[8];
  int k, k2, g, n, i, sh, ok_;
  u8 *data;
  size_t sz;
  unsigned lost;
  char err[200], ref[8];
  ok("angles: sines", isin(0) == 0 && isin(900) == TRIG_ONE && isin(300) == 8192 && icos(1800) == -TRIG_ONE && isin(2700) == -TRIG_ONE && isin(-900) == -TRIG_ONE &&
     isin(305) > 8192 && isin(305) < 8438);
  ok("angles: directions", iatan2(0, 100) == 0 && iatan2(100, 0) == 900 && iatan2(0, -5) == 1800 && iatan2(-7, 0) == 2700 && iatan2(100, 100) == 450 &&
     iatan2(-100, 100) == 3150 && iatan2(173, 100) >= 599 && iatan2(173, 100) <= 601);
  img_defaults(&im);
  im.cx = 2000; im.cy = 1000; im.rot = 300;
  ok("angles: a box turned 30 degrees", obj_w(&im) == 2232 && obj_h(&im) == 1866);
  im.rot = 900;
  ok("angles: a quarter turn swaps the sides", obj_w(&im) == 1000 && obj_h(&im) == 2000);
  obj_point(&im, 0, 0, &bx, &by);
  ok("angles: the top left corner turned a quarter is at the top right", bx == 1000 && by == 0);
  im.rot = 1234; im.flip = 1;
  obj_point(&im, 300, 700, &bx, &by);
  obj_unpoint(&im, bx, by, &u, &v);
  ok("angles: a point there and back", u >= 299 && u <= 301 && v >= 699 && v <= 701);
  /* shapes: every one has an outline inside its box (a speech bubble's point is below it) */
  for (sh = 0, ok_ = 1; sh < SH_N; sh++) {
    i32 xy[2 * SHAPE_MAXPTS];
    int closed;
    u32 m = shape_points(sh, 3000, 2000, xy, SHAPE_MAXPTS, &closed);
    if (m < 2) ok_ = 0;
    for (i = 0; i < (int)m; i++) if (xy[2 * i] < 0 || xy[2 * i] > 3000 || xy[2 * i + 1] < 0 || xy[2 * i + 1] > (sh == SH_CALLOUT ? 2300 : 2000)) ok_ = 0;
  }
  ok("shapes: the new ones fit their boxes", ok_);
  /* two floating shapes, one turned, saved and read back */
  doc_insert(d, &p, "Some text to wrap round the objects on this page, long enough to fill a few lines of it at least.   ", 100, NULL);
  k = doc_add_shape(d, SH_HEART, 2000, 1500);
  d->images[k].rot = 450; d->images[k].wrap = WRAP_TIGHT; d->images[k].hrel = d->images[k].vrel = POS_PAGE; d->images[k].x = 3000; d->images[k].y = 3000;
  k2 = doc_add_shape(d, SH_CHEVRON, 1500, 1000);
  d->images[k2].wrap = WRAP_SQUARE; d->images[k2].hrel = d->images[k2].vrel = POS_PAGE; d->images[k2].x = 6000; d->images[k2].y = 3500;
  pic_utf8(ref, k); doc_insert(d, &p, ref, 4, NULL);
  pic_utf8(ref, k2); doc_insert(d, &p, ref, 4, NULL);
  data = docx_save(d, &sz);
  e = data ? docx_load(data, sz, &lost, err, sizeof err) : NULL;
  free(data);
  {
    int a = -1, b = -1;
    if (e) for (i = 0; i < e->nimages; i++) { if (e->images[i].shape == SH_HEART) a = i; if (e->images[i].shape == SH_CHEVRON) b = i; }
    ok("angles: a turned heart and a chevron saved and read back", a >= 0 && b >= 0 && e->images[a].rot == 450 && e->images[a].wrap == WRAP_TIGHT && e->images[b].rot == 0);
  }
  doc_free(e);
  /* grouped: one object, where they were */
  L = lay_new(d, &fs);
  lay_update(L);
  n = 0;
  for (i = 0; i < (int)L->nfloats && n < 2; i++, n++) {
    at[n].para = 0; at[n].off = (u32)(d->p[0]->len - 8 + 4 * n);
    box[4 * n] = L->floats[i].x; box[4 * n + 1] = L->floats[i].y; box[4 * n + 2] = L->floats[i].w; box[4 * n + 3] = L->floats[i].h;
  }
  g = n == 2 ? doc_group(d, at, box, 2) : -1;
  ok("groups: two shapes made one", g >= 0 && d->images[g].kind == OBJ_GROUP && d->images[g].nkids == 2 && d->images[g].x == 3000 - 0 + (box[0] - 3000) &&
     d->p[0]->len == 104 && pic_index(d->p[0]->t + 100) == g);
  n = obj_parts(&d->images[g], parts, 8);
  ok("groups: drawn as its parts, each where it was", n == 2 && parts[0].im->shape == SH_HEART && parts[0].rot == 450 &&
     parts[0].x + d->images[g].x >= box[0] + box[2] / 2 - 2 && parts[0].x + d->images[g].x <= box[0] + box[2] / 2 + 2 &&
     parts[1].y + d->images[g].y >= box[5] + box[7] / 2 - 2 && parts[1].y + d->images[g].y <= box[5] + box[7] / 2 + 2);
  d->images[g].rot = 900;
  n = obj_parts(&d->images[g], parts, 8);
  ok("groups: turned, its parts turn with it", n == 2 && parts[0].rot == 1350 && parts[1].rot == 900);
  d->images[g].rot = 0;
  data = docx_save(d, &sz);
  e = data ? docx_load(data, sz, &lost, err, sizeof err) : NULL;
  free(data);
  {
    int a = -1;
    if (e) for (i = 0; i < e->nimages; i++) if (e->images[i].kind == OBJ_GROUP) a = i;
    ok("groups: saved and read back", a >= 0 && e->images[a].nkids == 2 && e->images[a].kids[0].shape == SH_HEART && e->images[a].kids[0].rot == 450 &&
       e->images[a].kids[1].shape == SH_CHEVRON && e->images[a].kids[1].x == d->images[g].kids[1].x && e->images[a].chw == d->images[g].chw && !(lost & LOST_PICTURES));
  }
  doc_free(e);
  {
    /* copied into another document: the group with its parts */
    Fragment *f;
    Doc *o = doc_new();
    Pos q = doc_start(o), a0 = { 0, 100 }, a1 = { 0, 104 };
    f = doc_fragment(d, a0, a1);
    ok("groups: pasted into another document", f && doc_paste(o, &q, f, PASTE_KEEP) && o->nimages == 1 && o->images[0].kind == OBJ_GROUP && o->images[0].nkids == 2);
    fragment_free(f);
    doc_free(o);
  }
  {
    Pos a = { 0, 100 };
    n = doc_ungroup(d, a, d->images[g].x, d->images[g].y);
    lay_update(L);
    ok("groups: taken apart, each where it was", n == 2 && d->p[0]->len == 108 && L->nfloats == 2 &&
       L->floats[0].x >= box[0] - 2 && L->floats[0].x <= box[0] + 2 && L->floats[0].y >= box[1] - 2 && L->floats[0].y <= box[1] + 2 &&
       d->images[pic_index(d->p[0]->t + 100)].rot == 450);
    ok("groups: taken apart, each wraps as it did", d->images[pic_index(d->p[0]->t + 100)].wrap == WRAP_TIGHT && d->images[pic_index(d->p[0]->t + 104)].wrap == WRAP_SQUARE);
    ok("groups: undone", doc_undo(d, &a) && d->p[0]->len == 104 && d->images[pic_index(d->p[0]->t + 100)].kind == OBJ_GROUP);
  }
  /* tight: the text wraps round the outline, closer than round the box */
  {
    i32 xy[2 * 128], x0, x1;
    Image sq;
    int m;
    img_defaults(&sq); sq.kind = OBJ_SHAPE; sq.shape = SH_DIAMOND; sq.cx = 2000; sq.cy = 2000;
    m = obj_outline(&sq, xy, 128);
    ok("wrap: a diamond's outline", m == 4 && xy[0] == 1000 && xy[1] == 0);
    sq.rot = 450; sq.shape = SH_RECT;
    m = obj_outline(&sq, xy, 128);
    for (i = 0, x0 = 99999, x1 = -99999; i < m; i++) { if (xy[2 * i] < x0) x0 = xy[2 * i]; if (xy[2 * i] > x1) x1 = xy[2 * i]; }
    ok("wrap: a square turned 45 degrees: its corners at the box's sides", m == 4 && x0 >= -2 && x0 <= 2 && x1 >= obj_w(&sq) - 2 && x1 <= obj_w(&sq) + 2 && obj_w(&sq) == 2828);
  }
  {
    u32 pn;
    u8 *png = slurp("tests/data/disc.png", &pn);
    i32 *poly = NULL, minx = 99999, maxx = 0;
    int m = png ? img_outline(png, pn, &poly) : 0;
    for (i = 0; i < m; i++) { if (poly[2 * i] < minx) minx = poly[2 * i]; if (poly[2 * i] > maxx) maxx = poly[2 * i]; }
    ok("wrap: a picture's outline round what it shows", m >= 8 && minx >= 21600 / 4 - 900 && minx <= 21600 / 4 + 900 && maxx >= 21600 * 3 / 4 - 900 && maxx <= 21600 * 3 / 4 + 900);
    free(poly); free(png);
  }
  data = pdf_export(L, &sz);
  {
    /* the pages' streams inflated, looking for the turn */
    static u8 page[200000];
    const u8 *q = data;
    int found = 0;
    while (data && !found && (q = memmem(q, sz - (size_t)(q - data), "/FlateDecode /Length ", 21))) {
      long len = atol((const char *)q + 21);
      const u8 *st = memmem(q, sz - (size_t)(q - data), "stream\n", 7);
      long got = st && len > 6 ? zinflate(st + 7 + 2, (size_t)len - 6, page, sizeof page) : -1;
      if (got > 0 && memmem(page, (size_t)got, "0.7070 -0.7070 0.7070 0.7070 0 0 cm", 35)) found = 1;
      q += 21;
    }
    ok("angles: PDF turns objects by any angle", found);
  }
  free(data);
  lay_free(L);
  doc_free(d);
}
static void pictures(void)
{
  char err[200];
  unsigned lost;
  Doc *d = doc_load_file("tests/data/pictures.docx", &lost, err, sizeof err), *e;
  Layout *L;
  u32 i, pj = 0, k;
  size_t n;
  u8 *data, *rgb;
  if (!d) { ok("pictures: loads", 0); return; }
  for (i = 0; i < d->np; i++) if (strstr(d->p[i]->t, "A small JPEG")) pj = i;
  ok("pictures: three read, none lost", d->nimages == 3 && !(lost & LOST_PICTURES) && pj);
  ok("pictures: the header's logo", d->hdr[HF_DEFAULT].np && d->hdr[HF_DEFAULT].p[0]->len == 4 && IS_PIC(d->hdr[HF_DEFAULT].p[0]->t));
  {
    const char *t = d->p[pj]->t + strlen("A small JPEG ");
    int k2 = IS_PIC(t) ? pic_index(t) : -1;
    ok("pictures: a JPEG in the text, 2 cm wide", k2 >= 0 && d->images[k2].type == IMG_JPEG && d->images[k2].cx == 1134 && d->images[k2].pw == 400 && d->images[k2].ph == 300);
    rgb = k2 >= 0 ? img_decode(d->images[k2].data, d->images[k2].len, 40, 30) : NULL;
    /* the yellow square in the photograph's top left quarter */
    ok("pictures: decoded and scaled", rgb && rgb[(10 * 40 + 10) * 3] > 200 && rgb[(10 * 40 + 10) * 3 + 1] > 200 && rgb[(10 * 40 + 10) * 3 + 2] < 80);
    free(rgb);
  }
  L = lay_new(d, &fs);
  lay_update(L);
  ok("pictures: the line with the JPEG grows", L->pl[pj].lines[0].height > 850 && L->pl[pj].lines[0].ascent >= 850);
  for (k = 0; k < L->pl[pj].nboxes && L->pl[pj].boxes[k].kind != BX_PIC; k++) ;
  ok("pictures: a box as wide as the picture", k < L->pl[pj].nboxes && L->pl[pj].boxes[k].w == 1134);
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("pictures: saved and read back", e && e->nimages == 3 && !(lost & LOST_PICTURES) && same_doc(d, e) && e->hdr[HF_DEFAULT].np && IS_PIC(e->hdr[HF_DEFAULT].p[0]->t));
  free(data); doc_free(e);
  data = pdf_export(L, &n);
  ok("pictures: in the PDF (the JPEG as it is)", data && n > 1000 && memmem(data, n, "/DCTDecode", 10) && memmem(data, n, "/XObject <</Im0", 15));
  free(data);
  /* deleting the picture's character takes it out of the text, and undo brings it back */
  {
    Pos a, b; a.para = b.para = pj; a.off = (u32)strlen("A small JPEG "); b.off = a.off + 4;
    doc_delete(d, a, b);
    ok("pictures: deleted as one character", !strstr(d->p[pj]->t, "\xF3"));
    doc_undo(d, &a);
    ok("pictures: undo", IS_PIC(d->p[pj]->t + strlen("A small JPEG ")));
  }
  lay_free(L); doc_free(d);
}
static void footnotes(const char *file)
{
  char err[200], what[200];
  unsigned lost;
  Doc *d = doc_load_file(file, &lost, err, sizeof err), *e;
  Layout *L;
  Furn f;
  i32 sepy;
  u32 i, p3 = 0, b;
  size_t n;
  u8 *data;
  if (!d) { ok("footnotes: loads", 0); return; }
  for (i = 0; i < d->np; i++) if (!strncmp(d->p[i]->t, "Paragraph 3.", 12)) p3 = i;
  sprintf(what, "footnotes %s: three read, none lost", file);
  ok(what, d->nnotes == 3 && !(lost & LOST_NOTES) && strstr(d->p[p3]->t, "\xF3\xB4"));
  ok("footnotes: their text, without Word's space", d->notes[0].np >= 1 && !strcmp(d->notes[0].p[0]->t, "The Amiga 1000 came out in 1985."));
  L = lay_new(d, &fs);
  lay_update(L);
  for (b = 0; b < L->pl[p3].nboxes && L->pl[p3].boxes[b].kind != BX_NOTE; b++) ;
  ok("footnotes: a raised number in the text", b < L->pl[p3].nboxes && lay_note_number(&L->pl[p3], b) == 1 && (L->pl[p3].boxes[b].f.flags & CF_SUPER));
  ok("footnotes: note 1 on page 1, 2 and 3 on page 2", L->npn == 3 && L->pn[0].page == 0 && L->pn[1].page == 1 && L->pn[2].page == 1);
  ok("footnotes: at the bottom of page 2, numbered", lay_notes(L, 1, &f, &sepy) && f.n == 2 && !strncmp(f.f[0].p->t, "2 Commodore", 11) &&
     f.f[1].y + f.f[1].pl.lines[0].height <= d->page.height - d->page.bottom && sepy < f.f[0].y);
  lay_furniture_free(&f);
  {
    /* the body on page 1 stops above its note */
    u32 a2, b2, last = 0;
    lay_page_lines(L, 0, &a2, &b2);
    for (i = a2; i < b2; i++) if (L->placed[i].page == 0) last = i;
    ok("footnotes: the text stops above them", lay_notes(L, 0, &f, &sepy) && L->placed[last].y + L->pl[L->placed[last].para].lines[L->placed[last].line].height <= sepy);
    lay_furniture_free(&f);
  }
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("footnotes: saved and read back", e && e->nnotes == 3 && same_doc(d, e) && !strcmp(e->notes[1].p[0]->t, d->notes[1].p[0]->t));
  free(data); doc_free(e);
  lay_free(L); doc_free(d);
}
/* the File page's templates: each made, laid out, saved and read back the same */
static void templates(void)
{
  int t;
  for (t = 0; t < TPL_N; t++) {
    char what[100], err[200];
    unsigned lost;
    Doc *d = doc_template(t), *e;
    Layout *L;
    size_t n;
    u8 *data;
    sprintf(what, "template %d (%s): made, laid out, saved and read back", t, template_name(t));
    if (!d) { ok(what, 0); continue; }
    L = lay_new(d, &fs);
    lay_update(L);
    data = docx_save(d, &n);
    e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
    ok(what, L->npages >= 1 && !d->dirty && e && same_doc(d, e) && (t == TPL_BLANK ? d->np == 1 : d->np > 5));
    free(data); doc_free(e); lay_free(L); doc_free(d);
  }
}
static void find_replace(void)
{
  Doc *d = doc_new();
  Pos c = doc_start(d), a, b;
  const char *t = "The cat sat on the mat.\nCaf\xC3\xA9 CAF\xC3\x89 concatenate cat";
  doc_insert(d, &c, t, (u32)strlen(t), NULL);
  ok("find: from the start", doc_find(d, doc_start(d), "cat", 0, &a, &b) && a.para == 0 && a.off == 4 && b.off == 7);
  ok("find: the next one", doc_find(d, b, "cat", 0, &a, &b) && a.para == 1 && a.off == 15);
  ok("find: whole words skip concatenate", doc_find(d, P(1, 0), "cat", FIND_WORD, &a, &b) && a.para == 1 && a.off == 24);
  ok("find: wraps round", doc_find(d, P(1, 28), "the", 0, &a, &b) && a.para == 0 && a.off == 0);
  ok("find: matching case", doc_find(d, P(0, 1), "The", FIND_CASE, &a, &b) && a.para == 0 && a.off == 0);
  ok("find: accented letters fold", doc_find(d, P(1, 1), "caf\xC3\xA9", 0, &a, &b) && a.para == 1 && a.off == 6);
  ok("find: backwards", doc_find(d, P(1, 0), "at", FIND_BACK, &a, &b) && a.para == 0 && a.off == 20);
  ok("find: nothing", !doc_find(d, doc_start(d), "dog", 0, &a, &b));
  ok("replace all: words only", doc_replace_all(d, "cat", "dog", FIND_WORD) == 2 && text_is(d, "The dog sat on the mat.\nCaf\xC3\xA9 CAF\xC3\x89 concatenate dog"));
  ok("replace all: one undo step", doc_undo(d, &c) && text_is(d, t));
  ok("replace all: with nothing", doc_replace_all(d, "at", "", 0) == 6 && text_is(d, "The c s on the m.\nCaf\xC3\xA9 CAF\xC3\x89 concene c") && runs_sane(d));
  doc_free(d);
}
static void partial_and_pdf(void)
{
  char err[200];
  unsigned lost;
  Doc *d = doc_load_file("tests/data/wordcraft-long.docx", &lost, err, sizeof err);
  Layout *L, *F;
  u32 full, rounds = 0;
  size_t n;
  u8 *pdf;
  if (!d) { ok("partial: loads", 0); return; }
  F = lay_new(d, &fs); lay_update(F); full = F->npages;
  L = lay_new(d, &fs);
  ok("partial: a first part", lay_update_part(L, 40) && !L->complete && L->npages >= 1 && L->npages < full && L->laid == 40);
  while (!L->complete && rounds < 1000) { lay_update_part(L, 200); rounds++; }
  ok("partial: continued to the end, the same pages", L->complete && L->npages == full && L->nplaced == F->nplaced &&
     L->placed[L->nplaced - 1].y == F->placed[F->nplaced - 1].y);
  lay_free(L); lay_free(F); doc_free(d);
  d = doc_load_file("tests/data/tables.docx", &lost, err, sizeof err);
  L = d ? lay_new(d, &fs) : NULL;
  pdf = L ? pdf_export(L, &n) : NULL;
  ok("pdf: a PDF with embedded fonts", pdf && n > 1000 && !memcmp(pdf, "%PDF-1.4", 8) && !memcmp(pdf + n - 6, "%%EOF\n", 6));
  if (pdf) file_write("/tmp/quill_tables.pdf", pdf, n);
  free(pdf); lay_free(L); doc_free(d);
}

/* hyperlinks (0.13): made and removed with undo, kept through saving, read in Word's three ways
   (w:hyperlink with a relationship or an anchor, fldSimple and complex HYPERLINK fields), carried
   by copy and paste into another document */
static int run_link_at(Doc *d, u32 para, u32 off) { return para_fmt_at(d->p[para], off)->link; }
/* bookmarks: set, found, kept through edits and undo, taken off; a cross-reference to the page one
   is on; saved as w:bookmarkStart/End and PAGEREF, and read from Word's complex REF and PAGEREF fields */
/* page numbers in roman numerals from a set number; a cover page put in, swapped for another design, taken out */
static void page_numbers_cover(void)
{
  Doc *d = doc_new(), *e;
  Pos c = doc_start(d);
  Layout *L;
  Box b;
  char num[16], err[200];
  unsigned lost;
  size_t n;
  u8 *data;
  Para *p;
  doc_insert(d, &c, "Body text.", 10, NULL);
  d->pgfmt = NF_LOWER_ROMAN; d->pgstart = 3;
  L = lay_new(d, &fs); lay_update(L);
  memset(&b, 0, sizeof b);
  p = para_new(&d->p[0]->pf, &d->p[0]->r[0].f);
  para_append(p, FIELD_PAGE, 3, &d->p[0]->r[0].f);
  b.off = 0;
  lay_field_text(L, p, &b, 1, num);
  ok("page numbers: lower roman, from 3 (the second page shows iv)", !strcmp(num, "iv"));
  para_free(p);
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("page numbers: the format and first number saved", e && e->pgfmt == NF_LOWER_ROMAN && e->pgstart == 3);
  free(data); doc_free(e); lay_free(L);
  ok("cover page: put in before the text, which starts a page", doc_cover_page(d, COVER_SIMPLE, "My Title", "A subtitle", "Me", "Today") &&
     d->np == 5 && !strcmp(d->p[0]->t, "My Title") && d->p[4]->pf.pagebreak && !strcmp(d->p[4]->t, "Body text."));
  ok("cover page: another design replaces it", doc_cover_page(d, COVER_BANDED, "T2", "", "", "") && d->np == 5 && d->p[0]->len == 6 && IS_PIC(d->p[0]->t) &&
     !strcmp(d->p[0]->t + 4, "T2"));
  ok("cover page: taken out, the text back at the top", doc_cover_remove(d) && d->np == 1 && !strcmp(d->p[0]->t, "Body text.") && !d->p[0]->pf.pagebreak &&
     !doc_cover_remove(d));
  doc_undo(d, &c);
  ok("cover page: removing undone in one step", d->np == 5 && !strcmp(d->p[0]->t + 4, "T2"));
  doc_free(d);
}
/* Design tab: colour sets, themes (one undo step), the page colour saved and read back */
static void design_looks(void)
{
  Doc *d = doc_new(), *e;
  Pos c = doc_start(d);
  char err[200];
  unsigned lost;
  size_t n;
  u8 *data;
  int h1 = doc_style(d, "Heading1"), title = doc_style(d, "Title");
  doc_insert(d, &c, "Text", 4, NULL);
  ok("colour sets: headings and the title coloured", doc_color_set(d, CS_GREEN) && d->styles[h1].cf.color == 0x538135 &&
     d->styles[title].cf.color == 0x375623 && d->look[3] == CS_GREEN && !doc_color_set(d, CS_N));
  ok("themes: style set, fonts and colours at once", doc_theme(d, TH_BOING) && d->styles[h1].cf.color == 0xC00000 &&
     d->look[0] == SS_BOLD && d->look[1] == TF_CALIBRI_CALIBRI && d->look[3] == CS_RED && d->styles[0].cf.font == doc_font(d, "Calibri"));
  doc_undo(d, &c);
  ok("themes: undone in one step", d->styles[h1].cf.color == 0x538135);
  d->pagecolor = HL_ON | 0xDEEBF7;
  {
    Pos a0 = doc_start(d), a1 = a0;
    CharFmt f;
    memset(&f, 0, sizeof f); f.fx = FX_BORDER;
    a1.off = 2;
    doc_format(d, a0, a1, CM_CBORDER, &f);
    ok("character border: on the first two letters only", (para_fmt_at(d->p[0], 0)->fx & FX_BORDER) && !(para_fmt_at(d->p[0], 2)->fx & FX_BORDER));
  }
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("page colour: saved and read back", e && e->pagecolor == (HL_ON | 0xDEEBF7) && e->styles[doc_style(e, "Heading1")].cf.color == 0x538135);
  ok("character border: saved and read back", e && (para_fmt_at(e->p[0], 1)->fx & FX_BORDER) && !(para_fmt_at(e->p[0], 3)->fx & FX_BORDER));
  free(data); doc_free(e);
  {
    Layout *L = lay_new(d, &fs);
    u8 *pdf;
    lay_update(L);
    pdf = pdf_export(L, &n);
    static u8 page[200000];
    const u8 *q = pdf;
    int border = 0, colour = 0;
    while (pdf && (q = memmem(q, n - (size_t)(q - pdf), "/FlateDecode /Length ", 21))) {
      long len = atol((const char *)q + 21);
      const u8 *st = memmem(q, n - (size_t)(q - pdf), "stream\n", 7);
      long got = st && len > 6 ? zinflate(st + 7 + 2, (size_t)len - 6, page, sizeof page) : -1;
      if (got > 0 && memmem(page, (size_t)got, " re S", 5)) border = 1;
      if (got > 0 && memmem(page, (size_t)got, "0.870 0.921 0.968 rg", 20)) colour = 1;
      q += 21;
    }
    ok("character border and page colour: in the PDF", border && colour);
    free(pdf); lay_free(L);
  }
  doc_free(d);
}
static void bookmarks(void)
{
  Doc *d = doc_new(), *e;
  Pos c = doc_start(d), a, b;
  ParaFmt pf;
  CharFmt f;
  Layout *L;
  char err[200], num[16];
  unsigned lost;
  size_t n;
  u8 *data;
  u32 k;
  doc_insert(d, &c, "See page \nChapter Two\nThe end.", 30, NULL);
  memset(&pf, 0, sizeof pf); pf.pagebreak = 1;
  doc_para_format(d, 1, 1, PM_BREAK, &pf);
  a.para = 1; a.off = 0; b.para = 1; b.off = 11;
  ok("bookmarks: set on a heading", doc_set_bmark(d, a, b, "two") && doc_bmark_find(d, "two", &a, &b) && a.para == 1 && a.off == 0 && b.off == 11 &&
     !doc_bmark_find(d, "three", NULL, NULL));
  c.para = 1; c.off = 7;
  doc_insert(d, &c, "s", 1, NULL);
  ok("bookmarks: text typed inside is in it", doc_bmark_find(d, "two", &a, &b) && b.off == 12 && doc_bmark_at(d, c, NULL, NULL));
  /* a cross-reference to its page: the field, linked to the bookmark */
  f = *para_fmt_at(d->p[0], 0);
  f.link = (u16)doc_link(d, "#two");
  c.para = 0; c.off = 9;
  doc_insert(d, &c, FIELD_PAGEREF, 3, &f);
  L = lay_new(d, &fs); lay_update(L);
  {
    ParaLay *pl = &L->pl[0];
    Box *fb = NULL;
    for (k = 0; k < pl->nboxes; k++) if (pl->boxes[k].kind == BX_FIELD) fb = &pl->boxes[k];
    if (fb) lay_field_text(L, d->p[0], fb, 0, num); else strcpy(num, "-");
    ok("bookmarks: a cross-reference shows the page it is on", L->npages == 2 && !strcmp(num, "2") && lay_bmark_page(L, "two") == 2);
  }
  data = docx_save(d, &n);
  e = data ? docx_load(data, n, &lost, err, sizeof err) : NULL;
  ok("bookmarks: saved and read back, the cross-reference too", e && doc_bmark_find(e, "two", &a, &b) && a.para == 1 && b.off == 12 &&
     e->p[0]->len == d->p[0]->len && !memcmp(e->p[0]->t + 9, FIELD_PAGEREF, 3) &&
     !strcmp(doc_link_url(e, para_fmt_at(e->p[0], 9)->link), "#two"));
  free(data); doc_free(e);
  ok("bookmarks: taken off, and back on undo", doc_remove_bmark(d, "two") && !doc_bmark_find(d, "two", NULL, NULL) && doc_undo(d, &c) && doc_bmark_find(d, "two", NULL, NULL));
  lay_free(L);
  doc_free(d);
  {
    /* Word's way: complex fields round the result */
    static const char *body =
      "<w:p><w:bookmarkStart w:id=\"0\" w:name=\"_Ref1\"/><w:r><w:t>Target</w:t></w:r><w:bookmarkEnd w:id=\"0\"/></w:p>"
      "<w:p><w:r><w:t xml:space=\"preserve\">See </w:t></w:r>"
      "<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText xml:space=\"preserve\"> REF _Ref1 \\h </w:instrText></w:r>"
      "<w:r><w:fldChar w:fldCharType=\"separate\"/></w:r><w:r><w:t>Target</w:t></w:r><w:r><w:fldChar w:fldCharType=\"end\"/></w:r>"
      "<w:r><w:t xml:space=\"preserve\"> on page </w:t></w:r>"
      "<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText xml:space=\"preserve\"> PAGEREF _Ref1 \\h </w:instrText></w:r>"
      "<w:r><w:fldChar w:fldCharType=\"separate\"/></w:r><w:r><w:t>7</w:t></w:r><w:r><w:fldChar w:fldCharType=\"end\"/></w:r></w:p>";
    ZipWriter z;
    Buf doc;
    Doc *g = NULL;
    buf_init(&doc);
    buf_adds(&doc, "<?xml version=\"1.0\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>");
    buf_adds(&doc, body);
    buf_adds(&doc, "</w:body></w:document>");
    zip_begin(&z);
    zip_add(&z, "word/document.xml", doc.data, doc.len);
    if (zip_finish(&z)) g = docx_load((const u8 *)z.out.data, z.out.len, &lost, err, sizeof err);
    zip_free(&z); buf_free(&doc);
    ok("bookmarks: read from Word's REF and PAGEREF fields", g && g->np >= 2 && doc_bmark_find(g, "_Ref1", &a, &b) && b.off == 6 &&
       /* (WP1: the REF field keeps its code, its result between its marks) */
       !memcmp(g->p[1]->t, "See \xF4\x84\x80\x80Target\xF4\x88\x80\x80 on page " FIELD_PAGEREF, 30) && g->p[1]->len == 30 &&
       !strcmp(doc_link_url(g, para_fmt_at(g->p[1], 9)->link), "#_Ref1") && !strcmp(doc_link_url(g, para_fmt_at(g->p[1], 27)->link), "#_Ref1"));
    doc_free(g);
  }
}
static void hyperlinks(void)
{
  Doc *d = doc_new(), *e;
  Pos p = doc_start(d), a = { 0, 10 }, b = { 0, 20 }, la, lb, end;
  char err[100];
  unsigned lost;
  size_t sz;
  u8 *data;
  int k, hs;
  doc_insert(d, &p, "Visit the Amiga site today.", 27, NULL);
  ok("links: made over a selection", doc_set_link(d, a, b, "https://www.amiga.org/", NULL, &end) &&
     run_link_at(d, 0, 12) && !run_link_at(d, 0, 9) && !run_link_at(d, 0, 21) && !strcmp(doc_link_url(d, run_link_at(d, 0, 12)), "https://www.amiga.org/"));
  hs = doc_style(d, "Hyperlink");
  ok("links: in the Hyperlink style (blue, underlined)", hs > 0 && para_fmt_at(d->p[0], 12)->cstyle == hs &&
     para_fmt_at(d->p[0], 12)->color == 0x0563C1 && (para_fmt_at(d->p[0], 12)->flags & CF_UNDERLINE));
  k = doc_link_at(d, (Pos){ 0, 15 }, &la, &lb);
  ok("links: found from inside and at the end", k && la.off == 10 && lb.off == 20 && doc_link_at(d, (Pos){ 0, 20 }, NULL, NULL) == k && !doc_link_at(d, (Pos){ 0, 5 }, NULL, NULL));
  ok("links: undone and redone", doc_undo(d, &p) && !run_link_at(d, 0, 12) && para_fmt_at(d->p[0], 12)->cstyle != hs && doc_redo(d, &p) && run_link_at(d, 0, 12));
  {
    Pos c = { 0, 27 }, e2;
    ok("links: text put in as a link", doc_set_link(d, c, c, "mailto:me@example.com", " Mail me", &e2) && e2.off == 35 && run_link_at(d, 0, 30) &&
       !strcmp(doc_link_url(d, run_link_at(d, 0, 30)), "mailto:me@example.com"));
  }
  /* saved and read back: Word's w:hyperlink with an external relationship */
  data = docx_save(d, &sz);
  e = data ? docx_load(data, sz, &lost, err, sizeof err) : NULL;
  ok("links: saved and read back", e && run_link_at(e, 0, 12) && !strcmp(doc_link_url(e, run_link_at(e, 0, 12)), "https://www.amiga.org/") &&
     !run_link_at(e, 0, 5) && para_fmt_at(e->p[0], 12)->cstyle == doc_style(e, "Hyperlink") && !strcmp(doc_link_url(e, run_link_at(e, 0, 30)), "mailto:me@example.com"));
  {
    /* the package has the relationship, marked External */
    ZipReader z;
    size_t n = 0;
    int zi;
    char *rels = NULL;
    if (data && zip_open(&z, data, sz)) {
      if ((zi = zip_find(&z, "word/_rels/document.xml.rels")) >= 0) rels = (char *)zip_read(&z, zi, &n);
      zip_close(&z);
    }
    ok("links: the address is an external relationship", rels && strstr(rels, "relationships/hyperlink") && strstr(rels, "Target=\"https://www.amiga.org/\" TargetMode=\"External\""));
    free(rels);
  }
  free(data);
  doc_free(e);
  {
    /* copied into another document: the link goes with it */
    Fragment *f = doc_fragment(d, a, b);
    Doc *o = doc_new();
    Pos q = doc_start(o);
    ok("links: pasted into another document", f && doc_paste(o, &q, f, PASTE_KEEP) && run_link_at(o, 0, 2) && !strcmp(doc_link_url(o, run_link_at(o, 0, 2)), "https://www.amiga.org/"));
    fragment_free(f);
    doc_free(o);
  }
  {
    /* a PDF: the link is a clickable area opening its address */
    Layout *L = lay_new(d, &fs);
    size_t n = 0;
    u8 *pdf = L ? pdf_export(L, &n) : NULL;
    ok("links: clickable in a PDF", pdf && memmem(pdf, n, "/Subtype /Link", 14) && memmem(pdf, n, "/URI (https://www.amiga.org/)", 29) && memmem(pdf, n, "/Annots [", 9));
    free(pdf);
    lay_free(L);
  }
  ok("links: removed (and the blue underline with them)", doc_set_link(d, a, b, NULL, NULL, NULL) && !run_link_at(d, 0, 12) && para_fmt_at(d->p[0], 12)->cstyle != hs &&
     !(para_fmt_at(d->p[0], 12)->flags & CF_UNDERLINE));
  doc_free(d);
  {
    /* read from a file written the ways Word and others write links */
    static const char *body =
      "<w:p><w:r><w:t xml:space=\"preserve\">A </w:t></w:r><w:hyperlink r:id=\"rId9\"><w:r><w:t>web</w:t></w:r></w:hyperlink>"
      "<w:r><w:t xml:space=\"preserve\"> and </w:t></w:r><w:hyperlink w:anchor=\"intro\"><w:r><w:t>place</w:t></w:r></w:hyperlink></w:p>"
      "<w:p><w:fldSimple w:instr=\" HYPERLINK &quot;http://old.example/&quot; \"><w:r><w:t>simple</w:t></w:r></w:fldSimple>"
      "<w:r><w:t xml:space=\"preserve\"> x </w:t></w:r>"
      "<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText xml:space=\"preserve\"> HYPERLINK \"http://field.example/\" \\l \"top\" </w:instrText></w:r>"
      "<w:r><w:fldChar w:fldCharType=\"separate\"/></w:r><w:r><w:t>field</w:t></w:r><w:r><w:fldChar w:fldCharType=\"end\"/></w:r>"
      "<w:r><w:t xml:space=\"preserve\"> after</w:t></w:r></w:p>";
    ZipWriter z;
    Buf doc;
    Doc *g = NULL;
    buf_init(&doc);
    buf_adds(&doc, "<?xml version=\"1.0\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
                   "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><w:body>");
    buf_adds(&doc, body);
    buf_adds(&doc, "</w:body></w:document>");
    zip_begin(&z);
    zip_add(&z, "word/document.xml", doc.data, doc.len);
    {
      static const char rels[] = "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId9\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" Target=\"https://web.example/a?b=1&amp;c=2\" TargetMode=\"External\"/></Relationships>";
      zip_add(&z, "word/_rels/document.xml.rels", rels, sizeof rels - 1);
    }
    if (zip_finish(&z)) g = docx_load((const u8 *)z.out.data, z.out.len, &lost, err, sizeof err);
    zip_free(&z); buf_free(&doc);
    ok("links: read from w:hyperlink (relationship and anchor)", g && g->np >= 2 && !strcmp(doc_link_url(g, run_link_at(g, 0, 3)), "https://web.example/a?b=1&c=2") &&
       !run_link_at(g, 0, 0) && !strcmp(doc_link_url(g, run_link_at(g, 0, 12)), "#intro"));
    ok("links: read from HYPERLINK fields (simple and complex)", g && !strcmp(doc_link_url(g, run_link_at(g, 1, 2)), "http://old.example/") &&
       !strcmp(doc_link_url(g, run_link_at(g, 1, 11)), "http://field.example/#top") && !run_link_at(g, 1, 8) && !run_link_at(g, 1, 17) &&
       !strncmp(g->p[1]->t, "simple x field after", 20));
    doc_free(g);
  }
}

int main(void)
{
  fonts();
  raster(); hinting(); more_fonts(); layout(); lists_and_breaks(); tables(); table_styles(); row_breaking(); table_ops(); nested_tables(); objects_docx(); floats_layout(); stacking(); jpeg_kinds(); style_model(); para_char_formats(); lists_sets_paste(); header_rows(); line_breaks(); partial_and_pdf(); find_replace(); templates(); headers_footers(); story_editing(); section_pages(); angles_groups(); hyperlinks(); bookmarks(); page_numbers_cover(); design_looks(); pictures(); footnotes("tests/data/notes.docx"); footnotes("tests/data/notes-lo.docx");
  zipcheck(); text_files();
  docx_read("tests/data/word.docx", 0); docx_read("tests/data/lo.docx", 1);
  docx_roundtrip("tests/data/word.docx"); docx_roundtrip("tests/data/lo.docx"); docx_roundtrip("tests/data/wordcraft-long.docx");
  typing(); formatting(); formatting2(); tab_stops(); contents(); cell_ops(); comments(); tracked(); endnotes(); spelling(); web_layout(); sections(); drop_caps(); hyphenation(); undo(); styles(); utf8(); hostile();
  /* the work packages' own tests (tests/test_wp*.c; WP0's: the scaffolding) */
  test_wp0();
  test_wp1(); test_wp2(); test_wp3(); test_wp4(); test_wp5(); test_wp6(); test_wp7(); test_wp8(); test_wp9();
  test_wp10(); test_wp11(); test_wp12(); test_wp13(); test_wp14a(); test_wp14b(); test_wp15a(); test_wp15b();
  test_wp15c(); test_wp16(); test_wp17(); test_wp18(); test_wp19(); test_wp20(); test_wp21a(); test_wp21b();
  test_wp22a(); test_wp22b(); test_wp23(); test_wp24(); test_wp25(); test_wp26(); test_wp27(); test_wp28a();
  test_wp28b(); test_wp28c(); test_wp28d(); test_wp28e(); test_wp29(); test_wp30(); test_wp31();
  test_wpmac();
  fonts_free(&fs);
  printf("%d tests, %d failed\n", total, fails);
  return fails != 0;
}
