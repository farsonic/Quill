/* test_wp12.c - the native tests of WP12 Find, Go To, selection and editing keys (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp12() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/findx.h"
#include "../core/layout.h"
#include "../core/table.h"

static FxPattern pat;                  /* (big: not on the stack) */

static Doc *make(const char *text)
{
  Doc *d = doc_new();
  Pos c = doc_start(d);
  doc_insert(d, &c, text, (u32)strlen(text), NULL);
  doc_clear_undo(d);
  return d;
}
/* the first match of what (flags) from the start: its text, or "" */
static const char *first(Doc *d, const char *what, unsigned flags, const FxFormat *f)
{
  static char out[256];
  Pos a, b;
  char *t;
  out[0] = 0;
  if (!fx_compile(&pat, what, flags, f) || !fx_find(d, &pat, doc_start(d), NULL, &a, &b)) return "(none)";
  t = doc_text(d, a, b, NULL);
  strncpy(out, t, sizeof out - 1); out[sizeof out - 1] = 0;
  free(t);
  return out;
}
static int count(Doc *d, const char *what, unsigned flags)
{
  static Pos r[200];
  if (!fx_compile(&pat, what, flags, NULL)) return -1;
  return (int)fx_find_all(d, &pat, NULL, r, 100);
}

static void special_chars(void)
{
  Doc *d = make("One\ttwo 2024.\nThe end^ here\vnow\nThree \xC2\xA0 x\xE2\x80\x94y");
  ok("find: ^t a tab", !strcmp(first(d, "e^tt", 0, NULL), "e\tt"));
  ok("find: ^p across paragraphs", !strcmp(first(d, ".^pthe", 0, NULL), ".\nThe"));
  ok("find: ^# digits", !strcmp(first(d, "^#^#^#^#", 0, NULL), "2024"));
  ok("find: ^$ letters", !strcmp(first(d, "^$^$^$^#", 0, NULL), "(none)") && !strcmp(first(d, "e^$^$", 0, NULL), "end"));
  ok("find: ^? any", !strcmp(first(d, "t^?o", 0, NULL), "two"));
  ok("find: ^^ a caret", !strcmp(first(d, "d^^", 0, NULL), "d^"));
  ok("find: ^l a line break", !strcmp(first(d, "e^lnow", 0, NULL), "e\vnow"));
  ok("find: ^w white space", !strcmp(first(d, "Three^wx", 0, NULL), "Three \xC2\xA0 x"));
  ok("find: ^+ em dash", !strcmp(first(d, "x^+y", 0, NULL), "x\xE2\x80\x94y"));
  ok("find: ^s nonbreaking space", count(d, "^s", 0) == 1);
  ok("find: ^13 and ^u8212 codes", count(d, "^13", 0) == 2 && count(d, "^u8212", 0) == 1 && count(d, "^0151", 0) == 1);
  ok("find: a lone ^ is itself", count(d, "^", 0) == 1);
  ok("find: ^p counts marks (not after the last)", count(d, "^p", 0) == 2);
  ok("find: whole words", count(d, "the", FX_WORD) == 1 && count(d, "e", FX_WORD) == 0 && count(d, "e", 0) == 7);
  ok("find: prefix and suffix", count(d, "th", FX_PREFIX) == 2 && count(d, "ee", FX_SUFFIX) == 1 && count(d, "re", FX_SUFFIX) == 1);
  ok("find: ignoring punctuation", !strcmp(first(d, "2024the", FX_NOPUNCT | FX_NOSPACE, NULL), "(none)") && !strcmp(first(d, "two2024", FX_NOPUNCT | FX_NOSPACE, NULL), "two 2024"));
  ok("find: match case", count(d, "the", FX_CASE) == 0 && count(d, "The", FX_CASE) == 1);
  doc_free(d);
}

static void wildcards(void)
{
  Doc *d = make("The cat sat on the mat. Color colour colouur. bat123 A1 b22 c333");
  Pos a, b;
  ok("wild: ? one character", count(d, "?at", FX_WILD) == 4);
  ok("wild: * shortest run", !strcmp(first(d, "c*t", FX_WILD, NULL), "cat"));
  ok("wild: [ ] and ranges", count(d, "[cms]at", FX_WILD) == 3 && count(d, "[!cms]at", FX_WILD) == 1);
  ok("wild: @ one or more", !strcmp(first(d, "colou@r", FX_WILD, NULL), "colour") && count(d, "colou@r", FX_WILD) == 2);
  ok("wild: {n,m}", count(d, "[0-9]{2,3}", FX_WILD) == 3 && count(d, "[0-9]{3}", FX_WILD) == 2 && count(d, "[a-z][0-9]{1,}", FX_WILD) == 3);
  ok("wild: < and > word edges", count(d, "<[cms]at>", FX_WILD) == 3 && count(d, "<at", FX_WILD) == 0 && count(d, "at>", FX_WILD) == 3);
  ok("wild: always matches case", count(d, "the", FX_WILD) == 1);
  ok("wild: \\ escapes", count(d, "\\.", FX_WILD) == 2 && count(d, ".", FX_WILD) == 2);
  ok("wild: bad patterns", !fx_compile(&pat, "[a-", FX_WILD, NULL) && !fx_compile(&pat, "(ab", FX_WILD, NULL) && !fx_compile(&pat, "a{3,1}", FX_WILD, NULL) &&
     !fx_compile(&pat, "\\1", FX_WILD, NULL) && pat.err[0]);
  /* groups in Replace with */
  ok("wild: groups swap", fx_compile(&pat, "(<[a-z]@) (<[a-z]@)", FX_WILD, NULL) && fx_find(d, &pat, doc_start(d), NULL, &a, &b) &&
     fx_replace(d, &pat, a, b, "\\2 \\1", NULL, NULL, &b) && text_is(d, "The sat cat on the mat. Color colour colouur. bat123 A1 b22 c333"));
  ok("wild: replace all with ^&", fx_compile(&pat, "[0-9]@", FX_WILD, NULL) && fx_replace_all(d, &pat, NULL, "<^&>", NULL, NULL) == 4 &&
     text_is(d, "The sat cat on the mat. Color colour colouur. bat<123> A<1> b<22> c<333>"));
  ok("wild: replace all is one undo step", doc_undo_count(d, 0) == 2 && doc_undo_step(d, 0, 0, NULL, NULL, NULL) == UK_REPLACEALL);
  doc_free(d);
}

static void formatting(void)
{
  Doc *d = make("plain bold plain bold italic end");
  CharFmt v;
  FxFormat f, rf;
  Pos a, b, r[20];
  int k;
  memset(&v, 0, sizeof v);
  v.flags = CF_BOLD;
  doc_format(d, P(0, 6), P(0, 10), CM_BOLD, &v);
  doc_format(d, P(0, 17), P(0, 21), CM_BOLD, &v);
  v.flags = CF_ITALIC;
  doc_format(d, P(0, 22), P(0, 28), CM_ITALIC, &v);
  fx_format_none(&f); f.set = CF_BOLD;
  ok("format: text in bold", !strcmp(first(d, "bold", 0, &f), "bold") && fx_find(d, &pat, doc_start(d), NULL, &a, &b) && a.off == 6);
  ok("format: formatting alone", fx_compile(&pat, "", 0, &f) && fx_find_all(d, &pat, NULL, r, 10) == 2 && r[0].off == 6 && r[1].off == 10 && r[2].off == 17);
  fx_format_none(&f); f.set = CF_BOLD | CF_ITALIC;
  ok("format: nothing both", !strcmp(first(d, "", 0, &f), "(none)"));
  fx_format_none(&f); f.clear = CF_BOLD;
  ok("format: not bold", fx_compile(&pat, "bold", 0, &f) && fx_find_all(d, &pat, NULL, r, 10) == 0);
  ok("format: nothing to find", !fx_compile(&pat, "", 0, NULL));
  /* replace formatting: bold text becomes italic and not bold, its text kept */
  fx_format_none(&f); f.set = CF_BOLD;
  fx_format_none(&rf); rf.set = CF_ITALIC; rf.clear = CF_BOLD;
  ok("format: replace formatting only", fx_compile(&pat, "", 0, &f) && fx_replace_all(d, &pat, NULL, "", NULL, &rf) == 2 &&
     text_is(d, "plain bold plain bold italic end") && (para_fmt_at(d->p[0], 7)->flags & (CF_BOLD | CF_ITALIC)) == CF_ITALIC && runs_sane(d));
  /* the style: a paragraph style found and given */
  {
    Pos c = doc_end_pos(d);
    int h = doc_style_by_name(d, "heading 1");
    doc_insert(d, &c, "\nHead", 5, NULL);
    if (h < 0) h = doc_style(d, "Heading1");
    doc_apply_style(d, 1, 1, h);
    fx_format_none(&f); strcpy(f.pstyle, d->styles[h].name);
    ok("format: paragraph style", !strcmp(first(d, "", 0, &f), "Head"));
    fx_format_none(&rf); strcpy(rf.pstyle, d->styles[h].name);
    k = fx_compile(&pat, "plain", 0, NULL) && fx_find(d, &pat, doc_start(d), NULL, &a, &b) && fx_replace(d, &pat, a, b, "^&", NULL, &rf, &b);
    ok("format: replace gives a style", k && d->p[0]->pf.style == h);
  }
  doc_free(d);
}

static void replacing(void)
{
  Doc *d = make("a. b. c.");
  Pos a, b, range[2];
  ok("replace: ^p makes paragraphs", fx_compile(&pat, ". ", 0, NULL) && fx_replace_all(d, &pat, NULL, ".^p", NULL, NULL) == 2 && d->np == 3 && text_is(d, "a.\nb.\nc."));
  ok("replace: ^p found and joined", fx_compile(&pat, "^p", 0, NULL) && fx_replace_all(d, &pat, NULL, " ", NULL, NULL) == 2 && d->np == 1 && text_is(d, "a. b. c."));
  ok("replace: ^m a page break", fx_compile(&pat, "b", 0, NULL) && fx_find(d, &pat, doc_start(d), NULL, &a, &b) &&
     fx_replace(d, &pat, a, b, "^mB", NULL, NULL, &b) && d->np == 2 && d->p[1]->pf.pagebreak && text_is(d, "a. \nB. c."));
  ok("find: ^m finds it", count(d, "^m", 0) == 1 && count(d, "^n", 0) == 0);
  {
    unsigned lost = 0;
    Doc *e = docx_again(d, &lost);
    ok("replace: page break saved and read", e && e->np == 2 && e->p[1]->pf.pagebreak);
    doc_free(e);
  }
  ok("replace: ^m taken out", fx_compile(&pat, "^m", 0, NULL) && fx_replace_all(d, &pat, NULL, "", NULL, NULL) == 1 && d->np == 1 && text_is(d, "a. B. c."));
  ok("replace: ^c and ^t", fx_compile(&pat, "B", 0, NULL) && fx_replace_all(d, &pat, NULL, "^c^t^^", "clip", NULL) == 1 && text_is(d, "a. clip\t^. c."));
  /* in the selection only */
  doc_free(d);
  d = make("x x x x");
  range[0] = P(0, 2); range[1] = P(0, 5);
  ok("replace: within the selection", fx_compile(&pat, "x", 0, NULL) && fx_replace_all(d, &pat, range, "yy", NULL, NULL) == 2 && text_is(d, "x yy yy x"));
  range[0] = P(0, 2); range[1] = P(0, 7);
  ok("find: within the selection wraps there", fx_compile(&pat, "x", 0, NULL) && !fx_find(d, &pat, P(0, 3), range, &a, &b) && fx_find(d, &pat, P(0, 0), NULL, &a, &b) && a.off == 0);
  ok("find: backwards", fx_compile(&pat, "yy", FX_BACK, NULL) && fx_find(d, &pat, P(0, 9), NULL, &a, &b) && a.off == 5 &&
     fx_find(d, &pat, a, NULL, &a, &b) && a.off == 2 && fx_find(d, &pat, a, NULL, &a, &b) && a.off == 5);
  ok("find: no wrapping", fx_compile(&pat, "x", FX_NOWRAP, NULL) && !fx_find(d, &pat, P(0, 9), NULL, &a, &b));
  ok("replace: runs sane", runs_sane(d));
  doc_free(d);
}

static void go_to(void)
{
  Doc *d = make("Title\nIntro text\nMore\nLast");
  Layout *L;
  Pos a, b, t[8];
  int h = doc_style_by_name(d, "heading 1");
  SectFmt s;
  Table tb;
  if (h < 0) h = doc_style(d, "Heading1");
  doc_apply_style(d, 0, 0, h);
  doc_apply_style(d, 2, 2, h);
  doc_set_bmark(d, P(1, 0), P(1, 5), "intro");
  sect_default(&s);
  {
    ParaFmt pf;
    memset(&pf, 0, sizeof pf); pf.sect = (u16)doc_add_sect(d, &s);
    doc_para_format(d, 1, 1, PM_SECT, &pf);
  }
  ok("goto: headings", fx_targets(d, NULL, GT_HEADING, t, NULL, 8) == 2 && t[1].para == 2);
  ok("goto: sections", fx_targets(d, NULL, GT_SECTION, t, NULL, 8) == 2 && t[1].para == 2);
  ok("goto: next heading", fx_goto(d, NULL, GT_HEADING, "", 0, P(0, 2), &a, &b) && a.para == 2);
  ok("goto: previous heading", fx_goto(d, NULL, GT_HEADING, "", -1, P(3, 0), &a, &b) && a.para == 2);
  ok("goto: heading 1 by number", fx_goto(d, NULL, GT_HEADING, " 1 ", 0, P(3, 0), &a, &b) && a.para == 0);
  ok("goto: +1 and -1", fx_goto(d, NULL, GT_SECTION, "+1", 0, P(0, 0), &a, &b) && a.para == 2 && fx_goto(d, NULL, GT_SECTION, "-1", 0, P(3, 0), &a, &b) && a.para == 0);
  ok("goto: none past the last", !fx_goto(d, NULL, GT_HEADING, "3", 0, P(0, 0), &a, &b) && !fx_goto(d, NULL, GT_TABLE, "", 0, P(0, 0), &a, &b));
  ok("goto: bookmark by name", fx_goto(d, NULL, GT_BOOKMARK, "intro", 0, P(0, 0), &a, &b) && a.para == 1 && b.off == 5);
  ok("goto: bookmark by number", fx_goto(d, NULL, GT_BOOKMARK, "1", 0, P(0, 0), &a, &b) && a.para == 1);
  memset(&tb, 0, sizeof tb);
  {
    Pos c = P(3, 4);
    doc_insert(d, &c, "\n", 1, NULL);
    tbl_insert(d, &c, 2, 2, 9000);
  }
  ok("goto: a table", fx_targets(d, NULL, GT_TABLE, t, NULL, 8) == 1 && fx_goto(d, NULL, GT_TABLE, "1", 0, P(0, 0), &a, &b) && in_table(&d->p[a.para]->pf));
  {
    int k = doc_add_note(d);
    char ref[8];
    Pos c = P(1, 2);
    note_utf8(ref, k);
    doc_insert(d, &c, ref, 4, NULL);
    ok("goto: a footnote", fx_targets(d, NULL, GT_FOOTNOTE, t, NULL, 8) == 1 && t[0].para == 1 && t[0].off == 2);
    ok("find: ^f a footnote mark", count(d, "^f", 0) == 1 && count(d, "^g", 0) == 0);
  }
  doc_comment(d, P(3, 0), P(3, 4), "note", "Ann Smith", "AS", "");
  ok("goto: a comment by its reviewer", fx_goto(d, NULL, GT_COMMENT, "ann smith", 0, P(0, 0), &a, &b) && a.para == 3 && pos_cmp(b, a) > 0);
  ok("goto: no comment by someone else", !fx_goto(d, NULL, GT_COMMENT, "Bob", 0, P(0, 0), &a, &b));
  L = lay_new(d, &fs);
  lay_update(L);
  ok("goto: page 1 and lines", fx_targets(d, L, GT_PAGE, t, NULL, 8) == L->npages && fx_goto(d, L, GT_PAGE, "1", 0, P(3, 0), &a, &b) && a.para == 0 &&
     fx_targets(d, L, GT_LINE, NULL, NULL, 0) >= d->np && fx_goto(d, L, GT_LINE, "2", 0, P(0, 0), &a, &b) && a.para == 1);
  lay_free(L);
  doc_free(d);
}

static void selections(void)
{
  Doc *d = make("aa bb cc dd\nee ff");
  CharFmt v;
  Pos r[20], c;
  Layout *L;
  u32 n;
  char *t;
  memset(&v, 0, sizeof v);
  v.flags = CF_BOLD;
  doc_format(d, P(0, 3), P(0, 5), CM_BOLD, &v);
  doc_format(d, P(0, 9), P(0, 11), CM_BOLD, &v);
  doc_format(d, P(1, 0), P(1, 2), CM_BOLD, &v);
  n = fx_similar(d, P(0, 4), r, 10);
  ok("similar: the bold stretches", n == 3 && r[0].off == 3 && r[1].off == 5 && r[2].off == 9 && r[4].para == 1);
  n = fx_similar(d, P(0, 0), r, 10);
  ok("similar: the plain ones", n == 3 && r[0].off == 0 && r[1].off == 3);
  /* a block: the same columns of two lines */
  doc_free(d);
  d = make("abcdef\nabcdef\nabcdef");
  L = lay_new(d, &fs);
  lay_update(L);
  n = fx_block(L, P(0, 1), P(2, 3), r, 10);
  ok("block: one range a line", n == 3 && r[0].off == 1 && r[1].off == 3 && r[2].para == 1 && r[2].off == 1 && r[5].off == 3);
  t = fx_block_text(d, r, n, NULL);
  ok("block: its text", t && !strcmp(t, "bc\nbc\nbc"));
  free(t);
  lay_free(L);
  /* Alt+X */
  doc_free(d);
  d = make("x 00e9");
  ok("unicode: code to character", fx_toggle_unicode(d, P(0, 6), P(0, 6), &c) && text_is(d, "x \xC3\xA9") && c.off == 4);
  ok("unicode: character to code", fx_toggle_unicode(d, c, c, &c) && text_is(d, "x 00E9") && c.off == 6);
  doc_free(d);
  d = make("abU+20ac");
  ok("unicode: U+ form", fx_toggle_unicode(d, P(0, 8), P(0, 8), &c) && text_is(d, "ab\xE2\x82\xAC"));
  ok("unicode: after hex digits U+", fx_toggle_unicode(d, P(0, 5), P(0, 5), &c) && text_is(d, "abU+20AC"));
  ok("unicode: a selected code", fx_toggle_unicode(d, P(0, 2), P(0, 8), &c) && text_is(d, "ab\xE2\x82\xAC") && doc_undo(d, &c) && text_is(d, "abU+20AC"));
  ok("unicode: nothing at the start", !fx_toggle_unicode(d, P(0, 0), P(0, 0), &c));
  doc_free(d);
}

static void history(void)
{
  Doc *d = make("");
  Pos c = doc_start(d), bf, af;
  CharFmt v;
  char *t;
  u32 s0, s1;
  int i;
  const char *w = "Hello there";
  for (i = 0; w[i]; i++) doc_insert(d, &c, w + i, 1, NULL);
  ok("undo list: typing", doc_undo_count(d, 0) == 2 && doc_undo_step(d, 0, 0, &bf, &af, &s1) == UK_TYPING && doc_undo_step(d, 0, 1, NULL, NULL, &s0) == UK_TYPING && s1 > s0);
  t = doc_undo_typed(d, 0);
  ok("repeat: the typed text", t && !strcmp(t, "Hello there"));
  free(t);
  t = doc_undo_typed(d, s0);
  ok("repeat: only since a step", t && !strcmp(t, "there") && !doc_undo_typed(d, s1));
  free(t);
  memset(&v, 0, sizeof v); v.flags = CF_BOLD;
  doc_format(d, P(0, 0), P(0, 5), CM_BOLD, &v);
  doc_delete(d, P(0, 5), P(0, 11));
  ok("undo list: deleting and formatting", doc_undo_step(d, 0, 0, NULL, &af, NULL) == UK_DELETE && doc_undo_step(d, 0, 1, NULL, NULL, NULL) == UK_FORMAT && !doc_undo_typed(d, 0));
  doc_undo(d, &c);
  ok("redo list: named too", doc_undo_count(d, 1) == 1 && doc_undo_step(d, 1, 0, NULL, NULL, NULL) == UK_DELETE && doc_undo_step(d, 0, 5, NULL, NULL, NULL) == -1);
  doc_free(d);
}

void test_wp12(void)
{
  special_chars();
  wildcards();
  formatting();
  replacing();
  go_to();
  selections();
  history();
}
