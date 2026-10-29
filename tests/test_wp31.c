/* test_wp31.c - the native tests of WP31 Proofing languages (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp31() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/proflang.h"
#include "../core/hunspell.h"
#include "../core/hyph.h"
#include "../core/spell.h"
#include "../core/layout.h"

Doc *wp9_mini_docx(const char *body, const char *styles);       /* (test_wp9.c) */
char *wp9_saved_xml(Doc *d, const char *part);

/* a small Hunspell dictionary in Hunspell's own format: suffixes (with a condition and a strip),
   a prefix with a cross product, two suffixes, a forbidden word, KEEPCASE, NEEDAFFIX, compounds */
static const char mini_aff[] =
  "SET UTF-8\nTRY esianrtolcdugmphbyfvkwz\n"
  "KEEPCASE K\nFORBIDDENWORD X\nNEEDAFFIX N\nCOMPOUNDFLAG C\nCOMPOUNDMIN 3\nONLYINCOMPOUND O\n"
  "PFX U Y 1\nPFX U 0 un .\n"
  "SFX S Y 3\nSFX S y ies [^aeiou]y\nSFX S 0 s [aeiou]y\nSFX S 0 s [^y]\n"
  "SFX D Y 2\nSFX D 0 ed [^e]\nSFX D 0 d e\n"
  "SFX L Y 1\nSFX L 0 ly/S .\n"           /* (ly, and S after it: two suffixes) */
  "SFX Y Y 1\nSFX Y 0 y .\n"
  "REP 1\nREP f ph\n";
static const char mini_dic[] = "11\nhappy/U\ncity/S\nday/S\nwalk/DU\nbake/D\nAmiga/K\nfriend/LS\ncolour\nfoot/C\nball/CS\ngoed/X\ntelephone\nrespons/NY\n";

static int hok(Hun *h, const char *w) { return hun_ok(h, w, (u32)strlen(w)); }
/* (the tests' dictionary loader: German from a few words, no others) */
static int test_loader(u16 id, Spell *s)
{
  static const char aff[] = "SET ISO8859-1\nSFX E Y 1\nSFX E 0 e .\n";
  static const char dic[] = "3\nHaus/E\nStra\xDF" "e\nsch\xF6n/E\n";       /* (in Latin-1, as many German dictionaries are) */
  if (!strcmp(plang_tag(id), "de-DE")) return spell_load_hunspell(s, aff, sizeof aff - 1, dic, sizeof dic - 1);
  return 0;
}
static char *hyph_file(u16 lang, size_t *n)
{
  char path[64];
  u8 *d;
  const char *t = plang_tag(lang);
  sprintf(path, "dict/hyph_%c%c_%c%c.dic", t[0], t[1], t[3], t[4]);
  if (strlen(t) != 5) return NULL;
  d = file_read(path, n);
  return (char *)d;
}
/* the breaks in w as "syl-la-bles" */
static void show(u16 lang, const char *w, char *out)
{
  u8 brk[80];
  u32 n = (u32)strlen(w), i, o = 0;
  hyph_word_lang(lang, w, n, brk);
  for (i = 0; i < n; i++) { if (brk[i]) out[o++] = '-'; out[o++] = w[i]; }
  out[o] = 0;
}

void test_wp31(void)
{
  u16 gb = plang_find("en-GB"), us = plang_find("en_us"), de = plang_find("de-DE"), xx = plang_find("tlh-Piqd"), xx2;
  /* the languages */
  ok("wp31: tags", gb == 1 && us && !strcmp(plang_tag(us), "en-US") && !strcmp(plang_name(de), "German (Germany)") && plang_lcid(de) == 0x0407);
  xx2 = plang_find("TLH-piqd");
  ok("wp31: an unknown tag is kept", xx > plang_nlisted() && xx2 == xx && !strcmp(plang_tag(xx), "tlh-Piqd") && !plang_find("") && !plang_known("zz-ZZ"));
  ok("wp31: primary language", plang_primary(us, "en") && !plang_primary(de, "en") && plang_primary(de, "de"));
  {
    /* w:lang and w:noProof read on runs, styles and the defaults, and written back */
    Doc *d = wp9_mini_docx("<w:p><w:r><w:t xml:space=\"preserve\">Hello </w:t></w:r>"
                           "<w:r><w:rPr><w:lang w:val=\"de-DE\" w:eastAsia=\"ja-JP\" w:bidi=\"ar-SA\"/></w:rPr><w:t xml:space=\"preserve\">Haus </w:t></w:r>"
                           "<w:r><w:rPr><w:noProof/></w:rPr><w:t xml:space=\"preserve\">xqzt </w:t></w:r>"
                           "<w:r><w:rPr><w:lang w:val=\"x-klingon\"/></w:rPr><w:t>nuqneH</w:t></w:r></w:p>",
                           "<w:docDefaults><w:rPrDefault><w:rPr><w:lang w:val=\"en-US\"/></w:rPr></w:rPrDefault></w:docDefaults>"
                           "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style>"
                           "<w:style w:type=\"character\" w:styleId=\"Code\"><w:name w:val=\"Code\"/><w:rPr><w:noProof/><w:lang w:val=\"fr-FR\"/></w:rPr></w:style>");
    char *x;
    Doc *e;
    const CharFmt *f;
    ok("wp31: read", d && d->np == 1);
    if (!d) return;
    f = para_fmt_at(d->p[0], 0);
    ok("wp31: the defaults' language", f->lang == us && !f->noproof);
    f = para_fmt_at(d->p[0], 7);
    ok("wp31: a run's language and its other two", f->lang == de && !strcmp(plang_tag(f->langea), "ja-JP") && !strcmp(plang_tag(f->langbidi), "ar-SA"));
    f = para_fmt_at(d->p[0], 12);
    ok("wp31: do not check", f->noproof && f->lang == us);
    f = para_fmt_at(d->p[0], 18);
    ok("wp31: a tag Quill doesn't list", !strcmp(plang_tag(f->lang), "x-klingon"));
    {
      int k = doc_style(d, "Code");
      ok("wp31: a style's language", k > 0 && d->styles[k].cf.lang == plang_find("fr-FR") && d->styles[k].cf.noproof && (d->styles[k].cmask & CM_LANG));
    }
    x = wp9_saved_xml(d, "word/document.xml");
    ok("wp31: written", x && strstr(x, "<w:lang w:val=\"de-DE\" w:eastAsia=\"ja-JP\" w:bidi=\"ar-SA\"/>") && strstr(x, "<w:noProof/>") &&
       strstr(x, "w:val=\"x-klingon\"") && !strstr(x, "w:val=\"en-US\""));
    free(x);
    x = wp9_saved_xml(d, "word/styles.xml");
    ok("wp31: styles and defaults written", x && strstr(x, "<w:lang w:val=\"en-US\"/>") && strstr(x, "<w:lang w:val=\"fr-FR\"/>") && strstr(x, "<w:noProof/>"));
    free(x);
    e = docx_again(d, NULL);
    ok("wp31: round trip", e && same_doc(d, e) && para_fmt_at(e->p[0], 7)->lang == de && para_fmt_at(e->p[0], 12)->noproof);
    doc_free(e);
    /* set on a range, undone */
    {
      Pos c;
      ok("wp31: set the language", doc_set_lang(d, P(0, 0), P(0, 5), de, 1) && para_fmt_at(d->p[0], 2)->lang == de && para_fmt_at(d->p[0], 2)->noproof &&
         runs_sane(d));
      ok("wp31: and undone", doc_undo(d, &c) && para_fmt_at(d->p[0], 2)->lang == us && !para_fmt_at(d->p[0], 2)->noproof);
    }
    doc_free(d);
  }
  {
    /* the Hunspell reader */
    Hun *h = hun_load(mini_aff, sizeof mini_aff - 1, mini_dic, sizeof mini_dic - 1);
    char *s[8];
    int n, i, found;
    ok("wp31: hunspell loads", h && hun_count(h) == 13);
    if (h) {
      ok("wp31: stems", hok(h, "happy") && hok(h, "city") && hok(h, "colour") && !hok(h, "colur"));
      ok("wp31: suffixes with conditions", hok(h, "cities") && hok(h, "days") && !hok(h, "citys") && !hok(h, "dayies") && hok(h, "walked") && hok(h, "baked") && !hok(h, "bakeed"));
      ok("wp31: a prefix, and both", hok(h, "unhappy") && hok(h, "unwalked") && !hok(h, "uncity"));
      ok("wp31: two suffixes", hok(h, "friendly") && hok(h, "friendlies") && !hok(h, "friendlys") && hok(h, "friends"));
      ok("wp31: case", hok(h, "Happy") && hok(h, "HAPPY") && hok(h, "Amiga") && !hok(h, "amiga") && !hok(h, "AMIGA"));
      ok("wp31: forbidden, needaffix", !hok(h, "goed") && !hok(h, "respons") && hok(h, "responsy"));
      ok("wp31: compounds", hok(h, "football") && hok(h, "footballs") && !hok(h, "fooball") && !hok(h, "foothappy"));
      n = hun_suggest(h, "telefone", 8, s, 8);
      for (i = 0, found = 0; i < n; i++) { if (!strcmp(s[i], "telephone")) found = 1; free(s[i]); }
      ok("wp31: suggestions by REP", found);
      n = hun_suggest(h, "Hapy", 4, s, 8);
      ok("wp31: suggestions keep the capital", n > 0 && !strcmp(s[0], "Happy"));
      for (i = 0; i < n; i++) free(s[i]);
      hun_free(h);
    }
    {
      /* other flag kinds: long and numbers, and AF aliases */
      static const char aff2[] = "FLAG long\nSFX Aa Y 1\nSFX Aa 0 s .\n";
      static const char dic2[] = "1\ncat/AaBb\n";
      static const char aff3[] = "FLAG num\nAF 1\nAF 7,12\nSFX 12 Y 1\nSFX 12 0 er .\n";
      static const char dic3[] = "1\nfast/1\n";
      Hun *h2 = hun_load(aff2, sizeof aff2 - 1, dic2, sizeof dic2 - 1), *h3 = hun_load(aff3, sizeof aff3 - 1, dic3, sizeof dic3 - 1);
      ok("wp31: FLAG long", h2 && hok(h2, "cats") && !hok(h2, "catss"));
      ok("wp31: FLAG num with AF", h3 && hok(h3, "faster") && !hok(h3, "fastest"));
      hun_free(h2); hun_free(h3);
    }
  }
  {
    /* a language's dictionary, through the loader; English falls back to the built-in list */
    Spell en;
    CharFmt f;
    Spell *s;
    static const char words[] = "colour\nhouse\n";
    spell_load_mem(&en, words, sizeof words - 1);
    plang_set_loader(test_loader);
    memset(&f, 0, sizeof f);
    f.lang = de;
    s = proof_spell(&en, &f);
    ok("wp31: the German dictionary (Latin-1 made UTF-8)", s && s != &en && spell_ok(s, "Haus", 4) && spell_ok(s, "Hause", 5) &&
       spell_ok(s, "Stra\xC3\x9F" "e", 7) && spell_ok(s, "sch\xC3\xB6ne", 7) && !spell_ok(s, "house", 5));
    f.lang = us;
    ok("wp31: English without its own: the built-in list", proof_spell(&en, &f) == &en);
    f.lang = 0;
    ok("wp31: no language: the default", proof_spell(&en, &f) == &en);
    f.lang = plang_find("fr-FR");
    ok("wp31: no French dictionary: none", proof_spell(&en, &f) == NULL);
    f.lang = de; f.noproof = 1;
    ok("wp31: not checked", proof_spell(&en, &f) == NULL);
    {
      /* going from mistake to mistake, each word by its own language's dictionary */
      Doc *d = doc_new();
      Pos c = P(0, 0), a, b;
      CharFmt g;
      doc_insert(d, &c, "colour hosue Haus Hauss xyzzy", 29, NULL);
      memset(&g, 0, sizeof g); g.lang = de;
      doc_format(d, P(0, 13), P(0, 23), CM_LANG, &g);
      g.noproof = 1;
      doc_format(d, P(0, 24), P(0, 29), CM_LANG | CM_NOPROOF, &g);
      ok("wp31: next mistake (English)", proof_next_spelling(&en, d, P(0, 0), &a, &b) && a.off == 7 && b.off == 12);
      ok("wp31: next mistake (German)", proof_next_spelling(&en, d, b, &a, &b) && a.off == 18 && b.off == 23);
      ok("wp31: round again", proof_next_spelling(&en, d, b, &a, &b) && a.off == 7);
      doc_free(d);
    }
    plang_forget();
    plang_set_loader(NULL);
    spell_free(&en);
  }
  {
    /* hyphenation in other languages, from dict/ */
    char out[96];
    hyph_set_loader(hyph_file);
    show(de, "Silbentrennung", out);
    ok("wp31: German hyphenation", !strcmp(out, "Sil-ben-tren-nung"));
    show(de, "B\xC3\xBC" "cherregal", out);
    ok("wp31: German with an umlaut", strstr(out, "B\xC3\xBC-cher") != NULL);
    show(plang_find("fr-FR"), "hyphénation", out);
    ok("wp31: French", !strcmp(out, "hy-phé-na-tion"));
    show(plang_find("es-ES"), "silabeo", out);
    ok("wp31: Spanish", !strcmp(out, "si-la-beo"));
    show(plang_find("it-IT"), "sillabazione", out);
    ok("wp31: Italian", !strcmp(out, "sil-la-ba-zio-ne"));
    show(plang_find("pt-PT"), "hifenização", out);
    ok("wp31: Portuguese", !strcmp(out, "hi-fe-ni-za-ção"));
    show(plang_find("nl-NL"), "woordafbreking", out);
    ok("wp31: Dutch", !strcmp(out, "woord-af-bre-king"));
    show(plang_find("pl-PL"), "przeniesienie", out);
    ok("wp31: no Polish patterns: no breaks", !strchr(out, '-'));
    hyph_set_loader(NULL);
  }
}
