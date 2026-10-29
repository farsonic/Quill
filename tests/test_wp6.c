/* test_wp6.c - the native tests of WP6 Compare and Combine (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp6() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/compare.h"

static Doc *mk(const char *text)
{
  Doc *d = doc_new();
  Pos p = P(0, 0);
  doc_insert(d, &p, text, (u32)strlen(text), NULL);
  doc_clear_undo(d);
  return d;
}
/* the text as it reads with every change accepted (rejected) */
static char *settled(Doc *d, int accept)
{
  Doc *c = docx_again(d, NULL);
  char *t;
  if (!c) return str_dup("?");
  doc_resolve(c, doc_start(c), doc_end_pos(c), accept);
  t = all(c);
  doc_free(c);
  return t;
}
static int settles(Doc *d, const char *acc, const char *rej)
{
  char *a = settled(d, 1), *r = settled(d, 0);
  int ok_ = !strcmp(a, acc) && (!rej || !strcmp(r, rej));
  if (!ok_) printf("  accepted [%s] want [%s]\n  rejected [%s] want [%s]\n", a, acc, r, rej ? rej : "-");
  free(a); free(r);
  return ok_;
}
/* the text marked rev (REV_INS, REV_DEL) by author, joined */
static int marked(Doc *d, int rev, const char *author, const char *want)
{
  char got[400];
  u32 pi, i, n = 0, start;
  for (pi = 0; pi < d->np; pi++) {
    Para *p = d->p[pi];
    for (i = 0, start = 0; i < p->nr; start = p->r[i].end, i++)
      if (p->r[i].f.rev == rev && (!author || !strcmp(d->revs[p->r[i].f.rid].author, author))) {
        u32 k = p->r[i].end - start;
        if (n + k + 2 >= sizeof got) break;
        if (n && got[n - 1] != '|' && !(i && p->r[i - 1].f.rev == rev)) got[n++] = '|';
        memcpy(got + n, p->t + start, k); n += k;
      }
  }
  got[n] = 0;
  if (strcmp(got, want)) { printf("  marked %d: [%s] want [%s]\n", rev, got, want); return 0; }
  return 1;
}

static void basic(void)
{
  CompareOpts o;
  Doc *a = mk("The quick brown fox jumps over the lazy dog."), *b = mk("The slow brown fox leaps over the lazy dog today.");
  int n;
  compare_defaults(&o);
  strcpy(o.author, "Rev");
  n = doc_compare(a, b, &o);
  ok("compare: changes found", n >= 3);
  ok("compare: deletions by the reviser", marked(b, REV_DEL, "Rev", "quick|jumps"));
  ok("compare: insertions", marked(b, REV_INS, "Rev", "slow|leaps| today"));
  ok("compare: accepting gives the revised text, rejecting the original", settles(b, "The slow brown fox leaps over the lazy dog today.", "The quick brown fox jumps over the lazy dog."));
  ok("compare: runs tidy", runs_sane(b));
  ok("compare: the original untouched", text_is(a, "The quick brown fox jumps over the lazy dog."));
  doc_free(a); doc_free(b);

  /* paragraphs: one removed, one added, one changed */
  a = mk("First paragraph.\nSecond goes away.\nThird stays.");
  b = mk("First paragraph.\nThird stays, longer.\nA new fourth.");
  doc_compare(a, b, &o);
  ok("compare: paragraphs follow the revised document", b->np == 3);
  ok("compare: a removed paragraph shows deleted (its break as a line break)", marked(b, REV_DEL, NULL, "Second goes away.\v"));
  ok("compare: and accepting all gives the revised document", settles(b, "First paragraph.\nThird stays, longer.\nA new fourth.", "First paragraph.\nSecond goes away.\vThird stays.\n"));
  doc_free(a); doc_free(b);

  /* identical: nothing marked */
  a = mk("Same text.\nTwice."); b = mk("Same text.\nTwice.");
  ok("compare: nothing to mark", doc_compare(a, b, &o) == 0 && marked(b, REV_INS, NULL, "") && marked(b, REV_DEL, NULL, ""));
  doc_free(a); doc_free(b);
}

static void options(void)
{
  CompareOpts o;
  Doc *a, *b;
  compare_defaults(&o);
  /* case and white space */
  a = mk("Hello  World"); b = mk("hello World");
  o.casechg = 0; o.whitespace = 0;
  ok("compare: case and white space ignored", doc_compare(a, b, &o) == 0 && text_is(b, "hello World"));
  doc_free(a); doc_free(b);
  a = mk("Hello  World"); b = mk("hello World");
  o.casechg = 1; o.whitespace = 1;
  ok("compare: case and white space compared", doc_compare(a, b, &o) > 0 && marked(b, REV_DEL, NULL, "Hello  ") && marked(b, REV_INS, NULL, "hello "));
  doc_free(a); doc_free(b);
  /* characters */
  a = mk("The colour of honour."); b = mk("The color of honor.");
  o.chars = 1;
  doc_compare(a, b, &o);
  ok("compare: at the character level", marked(b, REV_DEL, NULL, "u|u") && marked(b, REV_INS, NULL, ""));
  o.chars = 0;
  doc_free(a); doc_free(b);
  a = mk("The colour of honour."); b = mk("The color of honor.");
  doc_compare(a, b, &o);
  ok("compare: at the word level", marked(b, REV_DEL, NULL, "colour|honour") && marked(b, REV_INS, NULL, "color|honor"));
  doc_free(a); doc_free(b);
  /* formatting */
  {
    CharFmt bold;
    memset(&bold, 0, sizeof bold); bold.flags = CF_BOLD;
    a = mk("Plain words here."); b = mk("Plain words here.");
    doc_format(b, P(0, 6), P(0, 11), CM_BOLD, &bold);
    doc_clear_undo(b);
    doc_compare(a, b, &o);
    ok("compare: a formatting change shown (old look deleted, new inserted)", marked(b, REV_DEL, NULL, "words") && marked(b, REV_INS, NULL, "words") &&
       settles(b, "Plain words here.", "Plain words here."));
    doc_free(a); doc_free(b);
    a = mk("Plain words here."); b = mk("Plain words here.");
    doc_format(b, P(0, 6), P(0, 11), CM_BOLD, &bold);
    o.formatting = 0;
    ok("compare: formatting not compared", doc_compare(a, b, &o) == 0);
    o.formatting = 1;
    doc_free(a); doc_free(b);
  }
}

static void combine(void)
{
  CompareOpts o;
  Doc *a = mk("One two three four."), *b = mk("One two three four.");
  int ra, rb;
  Pos p;
  compare_defaults(&o);
  strcpy(o.author, "Unmarked");
  /* reviewer A inserted "big" (tracked); reviewer B deleted "three" (tracked) and changed "four" untracked */
  ra = doc_add_rev(a, "Ann", "2026-10-01T10:00:00Z");
  {
    CharFmt f = *para_fmt_at(a->p[0], 0);
    f.rev = REV_INS; f.rid = (u16)ra;
    p = P(0, 4);
    doc_insert(a, &p, "big ", 4, &f);
  }
  rb = doc_add_rev(b, "Bob", "2026-10-02T10:00:00Z");
  doc_track_delete(b, P(0, 8), P(0, 14), rb);
  doc_delete(b, P(0, 14), P(0, 18));
  p = P(0, 14);
  {
    CharFmt f = *para_fmt_at(b->p[0], 0);    /* (plain: not continuing the deletion before it) */
    doc_insert(b, &p, "five", 4, &f);
  }
  doc_clear_undo(a); doc_clear_undo(b);
  ok("combine: done", doc_combine(a, b, &o) > 0);
  ok("combine: Ann's insertion kept", marked(b, REV_INS, "Ann", "big "));
  ok("combine: Bob's deletion kept", marked(b, REV_DEL, "Bob", "three "));
  ok("combine: the untracked difference labelled", marked(b, REV_DEL, "Unmarked", "four") && marked(b, REV_INS, "Unmarked", "five"));
  ok("combine: accepting all", settles(b, "One big two five.", "One two three four."));
  doc_free(a); doc_free(b);
}

static void documents(void)
{
  char err[200];
  unsigned lost;
  Doc *a = doc_load_file("tests/data/word.docx", &lost, err, sizeof err), *b = doc_load_file("tests/data/word.docx", &lost, err, sizeof err), *c;
  CompareOpts o;
  Pos p;
  char *t0;
  if (!a || !b) { ok("compare: word.docx", 0); doc_free(a); doc_free(b); return; }
  compare_defaults(&o);
  strcpy(o.author, "Quill");
  p = P(2, 6);
  doc_insert(b, &p, "INSERTED ", 9, NULL);
  doc_delete(b, P(4, 0), P(4, 2));
  doc_clear_undo(b);
  t0 = all(b);
  ok("compare: a real document", doc_compare(a, b, &o) >= 2 && runs_sane(b));
  c = docx_again(b, &lost);
  ok("compare: its result saved and read back with its changes", c && marked(c, REV_INS, "Quill", "INSERTED ") && c->np == b->np);
  if (c) {
    char *t;
    doc_resolve(c, doc_start(c), doc_end_pos(c), 1);
    t = all(c);
    ok("compare: accepted, the revised document again", !strcmp(t, t0));
    free(t);
  }
  free(t0);
  doc_free(c);
  doc_free(a); doc_free(b);
}

void test_wp6(void)
{
  basic();
  options();
  combine();
  documents();
}
