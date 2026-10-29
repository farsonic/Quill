/* test_wp8.c - the native tests of WP8 Proofing: grammar, thesaurus, AutoCorrect, AutoFormat, Read Aloud (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp8() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/grammar.h"
#include "../core/thesaurus.h"
#include "../core/autocorrect.h"
#include "../core/proflang.h"

/* the issues grammar finds in s, as "kind:flagged>suggestion;" */
static const char *issues(const char *s, unsigned opts)
{
  static char out[512];
  GramIssue g[16];
  int n = grammar_check(s, (u32)strlen(s), opts, g, 16), i;
  out[0] = 0;
  for (i = 0; i < n; i++)
    sprintf(out + strlen(out), "%d:%.*s>%s;", g[i].kind, (int)(g[i].b - g[i].a), s + g[i].a, g[i].has_sug ? g[i].sug : "-");
  return out;
}
/* text typed into d at the caret a character at a time, AutoCorrect told of each */
static void type(Doc *d, Pos *c, AutoCorrect *ac, const char *s)
{
  while (*s) {
    int used;
    utf8_get(s, strlen(s), &used);
    doc_insert(d, c, s, (u32)used, NULL);
    ac_typed(ac, d, c, s, (u32)used, -1);
    s += used;
  }
}
static int para_is(Doc *d, u32 pi, const char *want)
{
  int same = pi < d->np && !strcmp(d->p[pi]->t, want);
  if (!same) printf("    para %u: \"%s\", wanted \"%s\"\n", (unsigned)pi, pi < d->np ? d->p[pi]->t : "(none)", want);
  return same;
}

void test_wp8(void)
{
  /* ---- grammar */
  ok("wp8: repeated word", !strcmp(issues("It was the the best.", GRO_GRAMMAR), "0: the>;"));
  ok("wp8: had had is fine", !strcmp(issues("She had had enough.", GRO_GRAMMAR), ""));
  ok("wp8: a and an", !strcmp(issues("It is a apple and an pear, an hour, a unicorn, an FBI agent, a European.", GRO_GRAMMAR), "1:a>an;1:an>a;"));
  ok("wp8: sentence capital", !strcmp(issues("This is fine. this is not. Mr. smith is fine, e.g. this.", GRO_GRAMMAR), "2:this>This;"));
  ok("wp8: paragraph start", !strcmp(issues("the start.", GRO_GRAMMAR), "2:the>The;"));
  ok("wp8: space before punctuation", !strcmp(issues("Hello , world !", GRO_GRAMMAR), "3: ,>,;3: !>!;"));
  ok("wp8: space after punctuation", !strcmp(issues("One,two and 1,000 and end.The next.", GRO_GRAMMAR), "4:,>, ;4:.>. ;"));
  ok("wp8: double space", !strcmp(issues("Two  spaces.", GRO_GRAMMAR), "5:  > ;"));
  ok("wp8: confused words", !strcmp(issues("I could of gone. Their is more then one.", GRO_GRAMMAR), "6:could of>could have;6:Their is>There is;6:more then>more than;"));
  ok("wp8: style off by default", !strcmp(issues("The cake was eaten in order to win.", GRO_GRAMMAR), ""));
  ok("wp8: passive and wordy", !strcmp(issues("The cake was eaten in order to win.", GRO_GRAMMAR | GRO_STYLE), "7:was eaten>-;8:in order to>to;"));
  ok("wp8: abbreviations and addresses", !strcmp(issues("See www.example.com and e.g. this. Visit the U.S. today.", GRO_GRAMMAR), ""));
  {
    Doc *d = doc_new();
    Pos c = P(0, 0), a, b;
    GramIssue gi;
    CharFmt f;
    doc_insert(d, &c, "Fine text.\nA second the the line.", 33, NULL);
    ok("wp8: grammar_next", grammar_next(d, P(0, 0), GRO_GRAMMAR, &a, &b, &gi) && a.para == 1 && gi.kind == GR_REPEAT);
    grammar_ignore(&gi, d->p[1]->t);
    ok("wp8: ignored", !grammar_next(d, P(0, 0), GRO_GRAMMAR, &a, &b, &gi));
    grammar_forget();
    memset(&f, 0, sizeof f); f.lang = plang_find("de-DE");
    doc_format(d, P(1, 0), P(1, d->p[1]->len), CM_LANG, &f);
    ok("wp8: only English is checked", !grammar_next(d, P(0, 0), GRO_GRAMMAR, &a, &b, &gi));
    doc_free(d);
  }
  /* ---- the thesaurus (dict/th_en.dat, made from WordNet) */
  {
    Thes *t = thes_open("dict/th_en.dat");
    ThesResult r;
    int i, j, found = 0;
    ok("wp8: thesaurus opens", t != NULL);
    if (t) {
      ok("wp8: happy", thes_lookup(t, "happy", 5, &r) && r.n >= 2 && !strcmp(r.m[0].pos, "adj"));
      for (i = 0; i < r.n; i++) for (j = 0; j < r.m[i].nterms; j++) if (!strcmp(r.m[i].terms[j], "glad")) found = 1;
      ok("wp8: happy: glad", found);
      thes_result_free(&r);
      ok("wp8: case and base forms", thes_lookup(t, "Cities", 6, &r) && !strcmp(r.head, "city"));
      thes_result_free(&r);
      ok("wp8: the first and last words", thes_lookup(t, "a", 1, &r) && (thes_result_free(&r), 1) && thes_lookup(t, "zygote", 6, &r));
      thes_result_free(&r);
      ok("wp8: not there", !thes_lookup(t, "xqzzy", 5, &r) && r.n == 0);
      thes_close(t);
    }
    {
      static const char mem[] = "ISO8859-1\nbig|1\n(adj)|large|gro\xDF\nsmall|1\n(adj)|little\n";
      Thes *m = thes_open_mem(mem, sizeof mem - 1);
      ok("wp8: a Latin-1 thesaurus", m && thes_lookup(m, "big", 3, &r) && r.m[0].nterms == 2 && !strcmp(r.m[0].terms[1], "gro\xC3\x9F"));
      thes_result_free(&r);
      thes_close(m);
    }
  }
  /* ---- AutoCorrect while typing */
  {
    AutoCorrect ac;
    Doc *d = doc_new();
    Pos c = P(0, 0);
    Buf b;
    ac_init(&ac);
    type(d, &c, &ac, "teh cat. it is Monday or tuesday. THe end, hELLO (c) 1/2 and 1st...");
    ok("wp8: autocorrect as you type", para_is(d, 0, "The cat. It is Monday or Tuesday. The end, Hello \xC2\xA9 \xC2\xBD and 1st\xE2\x80\xA6"));
    ok("wp8: the ordinal raised", para_fmt_at(d->p[0], (u32)(strstr(d->p[0]->t, "st\xE2\x80\xA6") - d->p[0]->t))->flags & CF_SUPER);
    ok("wp8: caret kept", c.para == 0 && c.off == d->p[0]->len);
    {
      Pos u;
      doc_undo(d, &u);
      ok("wp8: undo takes back the correction only", para_is(d, 0, "The cat. It is Monday or Tuesday. The end, Hello \xC2\xA9 \xC2\xBD and 1st..."));
      doc_redo(d, &u);
    }
    doc_free(d);
    d = doc_new(); c = P(0, 0);
    type(d, &c, &ac, "He said \"don't\" -- well-known word--word and this - that e.g. this ");
    ok("wp8: quotes and dashes", para_is(d, 0, "He said \xE2\x80\x9C" "don\xE2\x80\x99t\xE2\x80\x9D \xE2\x80\x93 well-known word\xE2\x80\x94word and this \xE2\x80\x93 that e.g. this "));
    doc_free(d);
    d = doc_new(); c = P(0, 0);
    type(d, &c, &ac, "Some *bold* and _slanted_ words at www.amiga.org and me@example.com today ");
    ok("wp8: bold, italic, links", para_is(d, 0, "Some bold and slanted words at www.amiga.org and me@example.com today "));
    ok("wp8: bold", para_fmt_at(d->p[0], 6)->flags & CF_BOLD);
    ok("wp8: italic", para_fmt_at(d->p[0], 15)->flags & CF_ITALIC);
    {
      Pos la, lb;
      int k = doc_link_at(d, P(0, 32), &la, &lb), k2 = doc_link_at(d, P(0, 50), NULL, NULL);
      ok("wp8: a web address linked", k && !strcmp(doc_link_url(d, k), "http://www.amiga.org") && la.off == 31 && lb.off == 44);
      ok("wp8: a mail address linked", k2 && !strcmp(doc_link_url(d, k2), "mailto:me@example.com"));
    }
    doc_free(d);
    d = doc_new(); c = P(0, 0);
    type(d, &c, &ac, "* first\nsecond\n");
    ok("wp8: a bulleted list", para_is(d, 0, "first") && d->p[0]->pf.list && d->p[1]->pf.list == d->p[0]->pf.list &&
       d->lists[d->p[0]->pf.list - 1].lvl[0].fmt == NF_BULLET);
    doc_list_apply(d, c.para, c.para, 0);          /* (as Return in an empty item does) */
    type(d, &c, &ac, "3. one\ntwo\n");
    ok("wp8: a numbered list", para_is(d, 2, "One") && d->p[2]->pf.list && d->p[2]->pf.list != d->p[0]->pf.list &&
       d->lists[d->p[2]->pf.list - 1].lvl[0].fmt == NF_DECIMAL && d->lists[d->p[2]->pf.list - 1].lvl[0].start == 3);
    doc_list_apply(d, c.para, c.para, 0);
    type(d, &c, &ac, "---\nafter\n");
    {
      u32 k;
      for (k = 0; k < d->np && d->p[k]->pf.bstyle[PE_BOTTOM] == PB_NONE; k++) ;
      ok("wp8: a border from ---", k < d->np && d->p[k]->len == 0 && d->p[k]->pf.bstyle[PE_BOTTOM] == PB_SINGLE);
    }
    ok("wp8: runs sane", runs_sane(d));
    doc_free(d);
    /* options off, the list changed, exceptions, saved and read back */
    ac.opts &= ~(AC_SENTENCE | AF_QUOTES);
    ac_add(&ac, "qll", "Quill");
    ac_remove(&ac, "teh");
    ac_exc_add(&ac, 1, "QUill");
    d = doc_new(); c = P(0, 0);
    type(d, &c, &ac, "teh \"qll\" is. QUill and CDs ");
    ok("wp8: options and lists", para_is(d, 0, "teh \"Quill\" is. QUill and CDs "));
    doc_free(d);
    buf_init(&b);
    ac_save(&ac, &b);
    {
      AutoCorrect ac2;
      ac_init(&ac2);
      ok("wp8: autocorrect saved and read", ac_load(&ac2, b.data, b.len) && ac2.opts == ac.opts && ac_find(&ac2, "qll") >= 0 && ac_find(&ac2, "teh") < 0 &&
         ac2.n == ac.n && ac2.nexc2 == ac.nexc2);
      ac_free(&ac2);
    }
    buf_free(&b);
    ac_free(&ac);
    /* German text keeps its words (the list is English) */
    ac_init(&ac);
    {
      CharFmt f;
      memset(&f, 0, sizeof f);
      d = doc_new(); c = P(0, 0);
      f.lang = plang_find("it-IT");
      doc_insert(d, &c, "Ecco", 4, &f);
      type(d, &c, &ac, " i gatti ");
      ok("wp8: the word list only for English", para_is(d, 0, "Ecco i gatti "));
      doc_free(d);
    }
    /* AutoFormat Now */
    d = doc_new(); c = P(0, 0);
    { static const char t[] = "* \"one\" -- two\n1/2 of the 2nd *part*\nsee www.aros.org now"; doc_insert(d, &c, t, sizeof t - 1, NULL); }
    ok("wp8: AutoFormat Now", ac_format_now(&ac, d, 0, d->np - 1) > 0 && para_is(d, 0, "\xE2\x80\x9Cone\xE2\x80\x9D \xE2\x80\x93 two") &&
       d->p[0]->pf.list && para_is(d, 1, "\xC2\xBD of the 2nd part") && (para_fmt_at(d->p[1], 11)->flags & CF_SUPER) &&
       (para_fmt_at(d->p[1], 17)->flags & CF_BOLD) && doc_link_at(d, P(2, 6), NULL, NULL));
    {
      Pos u;
      ok("wp8: AutoFormat Now is one undo step", doc_undo(d, &u) && para_is(d, 0, "* \"one\" -- two") && !d->p[0]->pf.list);
    }
    doc_free(d);
    ac_free(&ac);
  }
}
