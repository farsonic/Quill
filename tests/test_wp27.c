/* test_wp27.c - the native tests of WP27 Right-to-left and East Asian (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp27() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/bidi.h"
#include "../core/intl.h"
#include "../core/layout.h"
#include "../core/zip.h"
#include "../core/pdf.h"

Doc *wp9_mini_docx(const char *body, const char *styles);       /* (test_wp9.c) */
char *wp9_saved_xml(Doc *d, const char *part);

/* UTF-8 text as code points */
static void ins(Doc *d, const char *s) { Pos at = P(0, 0); doc_insert(d, &at, s, (u32)strlen(s), NULL); }
static int cps(const char *s, u32 *out, int max)
{
  int n = 0;
  size_t i = 0, len = strlen(s);
  while (i < len && n < max) { int used; out[n++] = utf8_get(s + i, len - i, &used); i += (size_t)used; }
  return n;
}
/* the visual order of s (a line) as UTF-8 code points joined, dir 0/1/-1 */
static void visual(const char *s, int dir, char *out)
{
  u32 c[128];
  u8 lv[128];
  int ord[128], n = cps(s, c, 128), k, pl;
  size_t o = 0;
  pl = bidi_levels(c, NULL, n, dir, lv);
  bidi_line(c, NULL, n, pl, lv);
  bidi_reorder(lv, n, ord);
  for (k = 0; k < n; k++) { u32 ch = c[ord[k]]; if (lv[ord[k]] & 1) ch = bidi_mirror(ch); o += (size_t)utf8_put(out + o, ch); }
  out[o] = 0;
}

static void algorithm(void)
{
  char v[512];
  u32 c[64];
  u8 lv[64];
  int n;
  /* Hebrew in an English sentence, English in a Hebrew one, numbers, brackets */
  visual("abc \xD7\x90\xD7\x91\xD7\x92 def", 0, v);
  ok("bidi: Hebrew word reversed inside English", !strcmp(v, "abc \xD7\x92\xD7\x91\xD7\x90 def"));
  visual("\xD7\x90\xD7\x91 abc \xD7\x92", 1, v);
  ok("bidi: English word kept inside right-to-left text", !strcmp(v, "\xD7\x92 abc \xD7\x91\xD7\x90"));
  visual("\xD7\x90 123 \xD7\x91", 1, v);
  ok("bidi: numbers stay left to right in Hebrew", !strcmp(v, "\xD7\x91 123 \xD7\x90"));
  visual("\xD7\x90(\xD7\x91)", 1, v);
  ok("bidi: brackets mirrored in right-to-left text", !strcmp(v, "(\xD7\x91)\xD7\x90"));
  n = cps("a\xD7\x90", c, 64);
  ok("bidi: P2 first strong (auto direction) left to right", bidi_levels(c, NULL, n, -1, lv) == 0);
  n = cps("\xD7\x90 a", c, 64);
  ok("bidi: P2 first strong right to left", bidi_levels(c, NULL, n, -1, lv) == 1 && lv[0] == 1 && lv[2] == 2);
  /* explicit isolates and overrides */
  visual("abc \xE2\x80\xAE" "def\xE2\x80\xAC ghi", 0, v);     /* RLO def PDF */
  ok("bidi: an override turns Latin round", strstr(v, "fed") != NULL);
  ok("bidi: first strong of UTF-8", bidi_first_strong("123 \xD8\xB3", 6) == 1 && bidi_first_strong("12 x", 4) == 0 && bidi_first_strong("12", 2) == -1);
  ok("bidi: mirror", bidi_mirror('(') == ')' && bidi_mirror('<') == '>' && bidi_mirror('a') == 'a' && bidi_mirror(0xAB) == 0xBB);
  ok("bidi: classes", bidi_class(0x5D0) == BC_R && bidi_class(0x627) == BC_AL && bidi_class('1') == BC_EN && bidi_class(0x661) == BC_AN && bidi_class(' ') == BC_WS);
  ok("bidi: scripts", bidi_script('a') == SCR_LATIN && bidi_script(0x627) == SCR_CS && bidi_script(0x4E00) == SCR_EA && bidi_script(0x3042) == SCR_EA &&
     bidi_script('1') == SCR_NEUTRAL && bidi_script(0x915) == SCR_CS);
  /* Arabic shaping: bism (ba sin mim): initial, medial, final */
  {
    u32 g[8] = { 0x628, 0x633, 0x645 };
    bidi_shape(g, 3, 0, 0, NULL, NULL);
    ok("shaping: initial, medial, final forms", g[0] == 0xFE91 && g[1] == 0xFCB4 - 0xFCB4 + 0xFEB4 && g[2] == 0xFEE2);
  }
  {
    u32 g[8] = { 0x627, 0x644, 0x644, 0x647 };     /* alif lam lam ha: Allah's letters, alif isolated */
    bidi_shape(g, 4, 0, 0, NULL, NULL);
    ok("shaping: right-joining alif stays isolated, lam initial", g[0] == 0xFE8D && g[1] == 0xFEDF && g[2] == 0xFEE0 && g[3] == 0xFEEA);
  }
  {
    u32 g[8] = { 0x644, 0x627 };                    /* lam alif: the ligature */
    bidi_shape(g, 2, 0, 0, NULL, NULL);
    ok("shaping: lam-alif ligature", g[0] == 0xFEFB && g[1] == BIDI_GONE);
    g[0] = 0x628; g[1] = 0x644; g[2] = 0x627;       /* after a joining letter: its final form */
    bidi_shape(g, 3, 0, 0, NULL, NULL);
    ok("shaping: lam-alif final after ba", g[1] == 0xFEFC && g[2] == BIDI_GONE && g[0] == 0xFE91);
  }
  {
    u32 g[4] = { 0x5D0, 0x5D1 };                    /* Hebrew: no shaping */
    bidi_shape(g, 2, 0, 0, NULL, NULL);
    ok("shaping: Hebrew unchanged", g[0] == 0x5D0 && g[1] == 0x5D1);
  }
  /* numerals */
  bidi_numerals = NUM_HINDI;
  ok("numerals: Hindi", bidi_numeral('3', 'a', 0) == 0x663);
  bidi_numerals = NUM_CONTEXT;
  ok("numerals: by context", bidi_numeral('3', 0x628, 0) == 0x663 && bidi_numeral('3', 'a', 1) == '3' && bidi_numeral('3', 0, 1) == 0x663);
  bidi_numerals = NUM_ARABIC;
  ok("numerals: Arabic", bidi_numeral('3', 0x628, 1) == '3');
  /* East Asian line breaking */
  ok("kinsoku: between ideographs", bidi_can_break(0x6F22, 0x5B57, 1));
  ok("kinsoku: not before an ideographic full stop", !bidi_can_break(0x6F22, 0x3002, 1) && bidi_can_break(0x6F22, 0x3002, 0));
  ok("kinsoku: not after an opening bracket", !bidi_can_break(0x300C, 0x6F22, 1));
  ok("kinsoku: not between Latin letters", !bidi_can_break('a', 'b', 1) && bidi_can_break(0x6F22, 'a', 1));
  ok("kinsoku: small kana may not start a line", !bidi_can_break(0x3042, 0x3063, 1));
  ok("wide", bidi_wide(0x4E00) && bidi_wide(0xFF21) && !bidi_wide('A') && !bidi_wide(0x5D0));
}

/* UAX #9 conformance: lines of the Unicode Character Database's BidiCharacterTest.txt (code points;
   direction 0/1/2 auto; paragraph level; levels, x for characters X9 removes; visual order without
   them). Returns the number of lines that failed, *total the lines read; -1 if the file is missing */
static long bidi_char_test(const char *path, long *total)
{
  FILE *f = fopen(path, "r");
  static char line[8192];
  static u32 cp[512];
  static u8 lv[512], kl[512];
  static int kept[512], o2[512];
  long bad = 0;
  *total = 0;
  if (!f) return -1;
  while (fgets(line, sizeof line, f)) {
    char *fld[5], *p = line, *q;
    int k, n = 0, dir, pl, good = 1, i, m = 0;
    if (line[0] == '#' || line[0] == '\n') continue;
    for (k = 0; k < 5 && p; k++) { fld[k] = p; p = strchr(p, ';'); if (p) *p++ = 0; }
    if (k < 5) continue;
    for (q = fld[0]; *q && n < 512; ) { char *e; unsigned long v = strtoul(q, &e, 16); if (e == q) break; cp[n++] = (u32)v; q = e; }
    dir = atoi(fld[1]);
    (*total)++;
    pl = bidi_levels(cp, NULL, n, dir == 2 ? -1 : dir, lv);
    bidi_line(cp, NULL, n, pl, lv);
    if (pl != atoi(fld[2])) good = 0;
    for (q = fld[3], i = 0; i < n; i++) {
      while (*q == ' ') q++;
      if (*q == 'x') { q++; continue; }
      if (atoi(q) != lv[i]) good = 0;
      kept[m] = i; kl[m] = lv[i]; m++;
      while (*q && *q != ' ') q++;
    }
    if (good) {
      bidi_reorder(kl, m, o2);
      for (q = fld[4], i = 0; i < m; i++) {
        while (*q == ' ') q++;
        if (atoi(q) != kept[o2[i]]) good = 0;
        while (*q && *q != ' ' && *q != '\n') q++;
      }
    }
    if (!good) bad++;
  }
  fclose(f);
  return bad;
}
/* UAX #9 conformance by classes: BidiTest.txt (@Levels lines, then "classes; bitset of directions"), each
   case run with the classes given (the code points don't matter). Returns the cases that failed */
static long bidi_class_test(const char *path, long *total)
{
  static const char *const names[] = { "L", "R", "AL", "EN", "ES", "ET", "AN", "CS", "NSM", "BN", "B", "S", "WS", "ON",
                                       "LRE", "LRO", "RLE", "RLO", "PDF", "LRI", "RLI", "FSI", "PDI" };
  FILE *f = fopen(path, "r");
  static char line[4096], levs[2048];
  static u8 cls[256], lv[256];
  static u32 cp[256];
  long bad = 0;
  *total = 0;
  if (!f) return -1;
  levs[0] = 0;
  while (fgets(line, sizeof line, f)) {
    char *p, *q, tok[16];
    int n = 0, bits, b;
    if (line[0] == '#' || line[0] == '\n') continue;
    if (!strncmp(line, "@Levels:", 8)) { strncpy(levs, line + 8, sizeof levs - 1); continue; }
    if (line[0] == '@' || !(p = strchr(line, ';'))) continue;
    *p++ = 0;
    bits = (int)strtol(p, NULL, 16);
    for (q = line; n < 256 && sscanf(q, "%15s", tok) == 1; n++) {
      int k;
      for (k = 0; k < 23 && strcmp(tok, names[k]); k++) ;
      cls[n] = (u8)k; cp[n] = 'a';
      q = strstr(q, tok) + strlen(tok);
    }
    for (b = 0; b < 3; b++) {
      int pl, i, good = 1;
      char *z = levs;
      if (!(bits & (1 << b))) continue;
      (*total)++;
      pl = bidi_levels(cp, cls, n, b == 0 ? -1 : b == 1 ? 0 : 1, lv);
      bidi_line(cp, cls, n, pl, lv);
      for (i = 0; i < n; i++) {
        while (*z == ' ' || *z == '\t') z++;
        if (*z == 'x') { z++; continue; }
        if (atoi(z) != lv[i]) good = 0;
        while (*z && *z != ' ' && *z != '\t' && *z != '\n') z++;
      }
      if (!good) bad++;
    }
  }
  fclose(f);
  return bad;
}
static void conformance(void)
{
  long total, bad = bidi_char_test("tests/data/BidiCharacterTest-sample.txt", &total);
  const char *full = getenv("QUILL_BIDI_TEST");      /* the whole BidiCharacterTest.txt, where fetched (and BidiTest.txt below) */
  ok("bidi: BidiCharacterTest sample (Unicode 18) all pass", bad == 0 && total > 300);
  if (full) {
    bad = bidi_char_test(full, &total);
    printf("BidiCharacterTest %s: %ld lines, %ld failed\n", full, total, bad);
    ok("bidi: the whole BidiCharacterTest passes", bad == 0 && total > 0);
  }
  if ((full = getenv("QUILL_BIDI_CLASSTEST"))) {        /* BidiTest.txt */
    bad = bidi_class_test(full, &total);
    printf("BidiTest %s: %ld cases, %ld failed\n", full, total, bad);
    ok("bidi: the whole BidiTest passes", bad == 0 && total > 0);
  }
}

/* a font set with Arabic and Hebrew (DejaVu Sans) and CJK (WenQuanYi or IPA Gothic) when the system has them */
static FontSet ifs;
static int have_dejavu, have_cjk;
static void fonts_intl(void)
{
  static const char *const cjk[] = { "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc", "/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf",
                                     "/usr/share/fonts/truetype/fonts-japanese-gothic.ttf" };
  size_t i;
  FILE *f;
  fonts_init(&ifs, "fonts");
  if ((f = fopen("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "rb"))) {
    fclose(f);
    have_dejavu = fonts_add(&ifs, "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "DejaVu Sans", 0, 0) >= 0;
  }
  for (i = 0; i < sizeof cjk / sizeof cjk[0] && !have_cjk; i++)
    if ((f = fopen(cjk[i], "rb"))) { fclose(f); have_cjk = fonts_add(&ifs, cjk[i], i ? "IPAGothic" : "WenQuanYi Zen Hei", 0, 0) >= 0; }
}

static void layout_rtl(void)
{
  Doc *d = doc_new();
  Layout *L;
  u32 pg;
  i32 x0, y, h, x1, xe;
  Pos p;
  CharFmt f;
  /* a right-to-left Hebrew paragraph: its start at the right margin */
  ins(d, "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xD7\xA2\xD7\x95\xD7\x9C\xD7\x9D abc");
  intl_set_dir(d, 0, 0, 1);
  ok("rtl: paragraph direction set (undoable)", d->p[0]->pf.bidi == 1);
  L = lay_new(d, have_dejavu ? &ifs : &fs);
  lay_update(L);
  ok("rtl: laid out right to left", L->pl[0].rtl == 1 && L->pl[0].nboxes >= 3 && (L->pl[0].boxes[0].lvl & 1));
  lay_caret(L, P(0, 0), &pg, &x0, &y, &h);
  lay_caret(L, P(0, 2), &pg, &x1, &y, &h);
  lay_caret(L, P(0, d->p[0]->len), &pg, &xe, &y, &h);
  ok("rtl: the caret at the start is at the right, moving left", x0 > x1 && x0 > d->page.width / 2);
  ok("rtl: right edge near the right margin", x0 > d->page.width - d->page.right - 200 && x0 <= d->page.width - d->page.right + 20);
  ok("rtl: the Latin word at the left end", L->pl[0].boxes[L->pl[0].nboxes - 1].x < L->pl[0].boxes[0].x);
  p = lay_hit(L, 0, x1, y + 10);
  ok("rtl: hit test finds the place back", p.para == 0 && p.off == 2);
  /* the cursor keys in visual order: Left from the start goes on into the text, and Right comes back */
  p = lay_visual_step(L, P(0, 0), -1);
  ok("rtl: Left from the start of a right-to-left line steps into it", p.para == 0 && p.off == 2);
  p = lay_visual_step(L, P(0, 2), 1);
  ok("rtl: Right steps back", p.para == 0 && p.off == 0);
  {
    /* from the right edge to the left one, every step to the left on the screen, every place visited */
    i32 lastx = x0, xx;
    int steps = 0, mono = 1, n = 0;
    u32 o;
    Pos q = P(0, 0);
    for (o = 0; o < d->p[0]->len; o = (u32)utf8_next(d->p[0]->t, d->p[0]->len, o)) n++;
    for (;;) {
      Pos nq = lay_visual_step(L, q, -1);
      if (nq.para != 0 || (nq.para == q.para && nq.off == q.off) || steps > 40) break;
      lay_caret(L, nq, &pg, &xx, &y, &h);
      if (xx >= lastx) { mono = 0; break; }
      lastx = xx; q = nq; steps++;
    }
    ok("rtl: Left goes across the mixed line one place at a time", mono && steps >= n - 1);
  }
  {
    /* the glyphs as shown: the first word reversed */
    char vis[64];
    u32 n = lay_visual(L, &L->pl[0].boxes[0], d->p[0]->t, vis, sizeof vis);
    ok("rtl: lay_visual reverses the word", n == 8 && !memcmp(vis, "\xD7\x9D\xD7\x95\xD7\x9C\xD7\xA9", 8));
  }
  lay_free(L);
  { Pos c; doc_undo(d, &c); }
  ok("rtl: undo gives back left to right", d->p[0]->pf.bidi == 0);
  /* a right-to-left run in a left-to-right paragraph */
  memset(&f, 0, sizeof f);
  intl_set_rtl(d, P(0, 0), P(0, 4), 1);
  ok("rtl: run property set", para_fmt_at(d->p[0], 1)->rtl == 1 && para_fmt_at(d->p[0], 20)->rtl == 0);
  doc_free(d);
  /* Arabic, shaped and measured as its presentation forms */
  if (have_dejavu) {
    d = doc_new();
    ins(d, "\xD8\xA8\xD8\xB3\xD9\x85 \xD8\xA7\xD9\x84\xD9\x84\xD9\x87");
    intl_set_dir(d, 0, 0, 1);
    L = lay_new(d, &ifs);
    lay_update(L);
    {
      char vis[64];
      u32 n = lay_visual(L, &L->pl[0].boxes[0], d->p[0]->t, vis, sizeof vis);
      u32 g[8];
      int k = 0;
      size_t i = 0;
      while (i < n && k < 8) { int used; g[k++] = utf8_get(vis + i, n - i, &used); i += (size_t)used; }
      ok("arabic: shown shaped and right to left", k == 3 && g[0] == 0xFEE2 && g[1] == 0xFEB4 && g[2] == 0xFE91);
      ok("arabic: the font has the letters (its own or one that covers them)", L->pl[0].boxes[0].w > 100);
    }
    lay_free(L);
    doc_free(d);
  }
}

static void layout_cjk(void)
{
  Doc *d = doc_new();
  Layout *L;
  char text[4096];
  size_t o = 0;
  int k;
  u32 li;
  int bad = 0;
  /* a long run of ideographs with full stops: broken between characters, never before a full stop */
  for (k = 0; k < 300; k++) {
    o += (size_t)utf8_put(text + o, k % 7 == 6 ? 0x3002 : 0x6F22 + (u32)(k % 5));
  }
  text[o] = 0;
  ins(d, text);
  L = lay_new(d, have_cjk ? &ifs : &fs);
  lay_update(L);
  ok("cjk: ideographs break into several lines", L->pl[0].nlines > 3);
  for (li = 1; li < L->pl[0].nlines; li++) {
    int used;
    u32 c = utf8_get(d->p[0]->t + L->pl[0].lines[li].start, 4, &used);
    if (c == 0x3002) bad++;
  }
  ok("cjk: no line starts with a full stop (kinsoku)", !bad);
  {
    /* lines filled: the first nearly as wide as the text area */
    i32 tw = d->page.width - d->page.left - d->page.right;
    ok("cjk: lines are full", L->pl[0].lines[0].width > tw * 85 / 100);
  }
  lay_free(L);
  /* the document grid: lines a whole number of pitches */
  {
    SectFmt s;
    sect_default(&s);
    s.grid = GRID_LINES; s.linepitch = 400;
    d->p[0]->pf.sect = (u16)doc_add_sect(d, &s);
    L = lay_new(d, have_cjk ? &ifs : &fs);
    lay_update(L);
    ok("grid: line height on the pitch", L->pl[0].lines[0].height % 400 == 0 && L->pl[0].lines[1].height % 400 == 0);
    lay_free(L);
    d->sects[d->p[0]->pf.sect - 1].grid = GRID_LINESCHARS;
    d->sects[d->p[0]->pf.sect - 1].charspace = 0;
    d->p[0]->ver++;
    L = lay_new(d, have_cjk ? &ifs : &fs);
    lay_update(L);
    {
      /* each ideograph as wide as the pitch (Normal's size: 11 pt = 220 twips) */
      const Box *b = &L->pl[0].boxes[1];
      i32 pitch = d->styles[0].cf.size * 10;
      ok("grid: characters on the pitch", b->kind == BX_TEXT && (b->w - pitch < 3 && pitch - b->w < 3));
    }
    lay_free(L);
  }
  doc_free(d);
}

static void docx_rtl(void)
{
  static const char body[] =
    "<w:p><w:pPr><w:bidi/><w:jc w:val=\"left\"/></w:pPr>"
    "<w:r><w:rPr><w:rFonts w:ascii=\"Arial\" w:hAnsi=\"Arial\" w:cs=\"Tahoma\"/><w:b/><w:bCs w:val=\"0\"/><w:sz w:val=\"24\"/><w:szCs w:val=\"32\"/><w:rtl/></w:rPr>"
    "<w:t>\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D</w:t></w:r>"
    "<w:dir w:val=\"ltr\"><w:r><w:t>abc</w:t></w:r></w:dir></w:p>"
    "<w:p><w:pPr><w:kinsoku w:val=\"0\"/><w:wordWrap w:val=\"0\"/><w:topLinePunct/><w:snapToGrid w:val=\"0\"/><w:textAlignment w:val=\"center\"/></w:pPr>"
    "<w:r><w:rPr><w:rFonts w:eastAsia=\"MS Mincho\" w:hint=\"eastAsia\"/><w:em w:val=\"dot\"/></w:rPr><w:t>\xE6\xBC\xA2</w:t></w:r>"
    "<w:r><w:ruby><w:rubyPr><w:rubyAlign w:val=\"distributeSpace\"/><w:hps w:val=\"10\"/><w:hpsRaise w:val=\"18\"/><w:hpsBaseText w:val=\"21\"/><w:lid w:val=\"ja-JP\"/></w:rubyPr>"
    "<w:rt><w:r><w:rPr><w:rFonts w:hint=\"eastAsia\"/><w:sz w:val=\"10\"/></w:rPr><w:t>\xE3\x81\x8B\xE3\x82\x93</w:t></w:r></w:rt>"
    "<w:rubyBase><w:r><w:t>\xE6\xBC\xA2</w:t></w:r></w:rubyBase></w:ruby></w:r>"
    "<w:r><w:rPr><w:eastAsianLayout w:id=\"7\" w:combine=\"1\" w:combineBrackets=\"round\"/></w:rPr><w:t>abcd</w:t></w:r></w:p>"
    "<w:tbl><w:tblPr><w:bidiVisual/><w:tblW w:w=\"2000\" w:type=\"dxa\"/></w:tblPr><w:tblGrid><w:gridCol w:w=\"1000\"/><w:gridCol w:w=\"1000\"/></w:tblGrid>"
    "<w:tr><w:tc><w:p><w:r><w:t>A</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>B</w:t></w:r></w:p></w:tc></w:tr></w:tbl>"
    "<w:p><w:r><w:t>end</w:t></w:r></w:p>"
    "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/><w:textDirection w:val=\"tbRl\"/><w:bidi/><w:docGrid w:type=\"linesAndChars\" w:linePitch=\"360\" w:charSpace=\"-2048\"/></w:sectPr>";
  Doc *d = wp9_mini_docx(body, NULL), *e;
  const CharFmt *f;
  char *xml;
  ok("docx rtl: read", d != NULL && d->np >= 4);
  if (!d) return;
  ok("docx rtl: paragraph direction", d->p[0]->pf.bidi == 1 && d->p[0]->pf.align == AL_LEFT);
  f = para_fmt_at(d->p[0], 0);
  ok("docx rtl: run direction, complex script font and size", f->rtl == 1 && f->fontcs && !strcmp(d->fonts[f->fontcs - 1], "Tahoma") && f->sizecs == 32 && f->size == 24);
  ok("docx rtl: bold without complex script bold", (f->flags & CF_BOLD) && (f->cs & CS_OWNB) && !(f->cs & CS_BOLD));
  ok("docx rtl: w:dir becomes an embedding round its text", strstr(d->p[0]->t, "\xE2\x80\xAA" "abc\xE2\x80\xAC") != NULL);
  ok("docx asian: typography bits", (d->p[1]->pf.asian & (AS_NOKINSOKU | AS_NOWORDWRAP | AS_TOPLINE | AS_NOSNAP)) == (AS_NOKINSOKU | AS_NOWORDWRAP | AS_TOPLINE | AS_NOSNAP) &&
     d->p[1]->pf.talign == TXA_CENTER);
  f = para_fmt_at(d->p[1], 0);
  ok("docx asian: East Asian font, hint, emphasis", f->fontea && !strcmp(d->fonts[f->fontea - 1], "MS Mincho") && f->hint == HINT_EA && f->em == EM_DOT);
  f = para_fmt_at(d->p[1], 3);
  ok("docx ruby: guide read onto its base", f->ruby && intl_ruby(d, f->ruby) && !strcmp(intl_ruby(d, f->ruby)->text, "\xE3\x81\x8B\xE3\x82\x93") &&
     intl_ruby(d, f->ruby)->align == RA_DSPACE && intl_ruby(d, f->ruby)->hps == 10 && intl_ruby(d, f->ruby)->raise == 18);
  f = para_fmt_at(d->p[1], 6);
  ok("docx asian: two lines in one", (f->eal & EAL_COMBINE) && EAL_BRACKETS(f) == EB_ROUND && f->ealid == 7);
  ok("docx table: right to left", d->ntables == 1 && d->tables[0].bidivisual == 1);
  {
    const SectFmt *s = intl_sect_of(d, d->np - 1);
    ok("docx section: direction, text direction, grid", s && s->bidi && s->tdir == STD_TBRL && s->grid == GRID_LINESCHARS && s->linepitch == 360 && s->charspace == -2048);
  }
  xml = wp9_saved_xml(d, "word/document.xml");
  ok("docx write: w:bidi, w:rtl, cs font, szCs, bCs", xml && strstr(xml, "<w:bidi/>") && strstr(xml, "<w:rtl/>") && strstr(xml, "w:cs=\"Tahoma\"") &&
     strstr(xml, "<w:szCs w:val=\"32\"/>") && strstr(xml, "<w:bCs w:val=\"0\"/>"));
  ok("docx write: Asian typography in order", xml && strstr(xml, "<w:kinsoku w:val=\"0\"/><w:wordWrap w:val=\"0\"/><w:topLinePunct/>") && strstr(xml, "<w:textAlignment w:val=\"center\"/>"));
  ok("docx write: ruby kept", xml && strstr(xml, "<w:ruby><w:rubyPr><w:rubyAlign w:val=\"distributeSpace\"/><w:hps w:val=\"10\"/><w:hpsRaise w:val=\"18\"/>") &&
     strstr(xml, "<w:rubyBase><w:r><w:t") && strstr(xml, "\xE3\x81\x8B\xE3\x82\x93</w:t></w:r></w:rt>"));
  ok("docx write: eastAsianLayout, em, eastAsia font", xml && strstr(xml, "<w:eastAsianLayout w:id=\"7\" w:combine=\"1\" w:combineBrackets=\"round\"/>") &&
     strstr(xml, "<w:em w:val=\"dot\"/>") && strstr(xml, "w:eastAsia=\"MS Mincho\""));
  ok("docx write: bidiVisual, section's direction and grid", xml && strstr(xml, "<w:bidiVisual/>") && strstr(xml, "<w:textDirection w:val=\"tbRl\"/><w:bidi/>") &&
     strstr(xml, "<w:docGrid w:type=\"linesAndChars\" w:linePitch=\"360\" w:charSpace=\"-2048\"/>"));
  free(xml);
  e = docx_again(d, NULL);
  ok("docx rtl: the same after a round trip", e && same_doc(d, e));
  if (e) {
    f = para_fmt_at(e->p[0], 0);
    ok("docx rtl: round trip keeps the run's own complex script look", f->rtl && f->sizecs == 32 && (f->cs & CS_OWNB) && e->p[0]->pf.bidi);
    f = para_fmt_at(e->p[1], 3);
    ok("docx ruby: round trip", f->ruby && !strcmp(intl_ruby(e, f->ruby)->text, "\xE3\x81\x8B\xE3\x82\x93"));
  }
  doc_free(e);
  /* a phonetic guide made here, written without a kept w:rt */
  {
    Ruby r;
    memset(&r, 0, sizeof r);
    strcpy(r.text, "kan");
    intl_set_ruby(d, P(1, 0), P(1, 3), &r);
    xml = wp9_saved_xml(d, "word/document.xml");
    ok("ruby: a new guide written", xml && strstr(xml, "<w:t xml:space=\"preserve\">kan</w:t></w:r></w:rt><w:rubyBase>"));
    free(xml);
  }
  doc_free(d);
  /* settings */
  {
    Doc *s = doc_new();
    IntlSettings *is = intl_settings(s);
    is->cspset = 1; is->cspacing = CSC_PUNCT; is->strict = 1; strcpy(is->nobefore, "!%),.:;?]}"); strcpy(is->lang, "ja-JP");
    xml = wp9_saved_xml(s, "word/settings.xml");
    ok("settings: East Asian line breaking written", xml && strstr(xml, "<w:characterSpacingControl w:val=\"compressPunctuation\"/><w:strictFirstAndLastChars/>") &&
       strstr(xml, "<w:noLineBreaksBefore w:lang=\"ja-JP\" w:val=\"!%),.:;?]}\"/>"));
    free(xml);
    e = docx_again(s, NULL);
    ok("settings: read back", e && e->intl && e->intl->cspacing == CSC_PUNCT && e->intl->strict && !strcmp(e->intl->nobefore, "!%),.:;?]}"));
    doc_free(e); doc_free(s);
  }
}

/* the dark pixels of page 0 at 72 dpi in the band of y (pixels) [y0, y1) */
static long dark_pixels(Layout *L, u32 y0, u32 y1)
{
  u32 w, h, x, y;
  long n = 0;
  u8 *rgb = pdf_page_rgb(L, 0, 150, &w, &h);
  if (!rgb) return -1;
  for (y = y0; y < y1 && y < h; y++)
    for (x = 0; x < w; x++) { const u8 *q = rgb + (y * w + x) * 3; if (q[0] + q[1] + q[2] < 600) n++; }
  free(rgb);
  return n;
}
/* emphasis marks and phonetic guides: room made above the line, and drawn (in the PDF, and so in page pictures) */
static void marks(void)
{
  Doc *d = doc_new();
  Layout *L;
  i32 h0, h1, h2;
  long plain, dotted, guided;
  Ruby r;
  ins(d, "emphasis marks here");
  L = lay_new(d, &fs);
  lay_update(L);
  h0 = L->pl[0].lines[0].height;
  plain = dark_pixels(L, 0, 2000);
  lay_free(L);
  ok("em: set on the text (undoable)", intl_set_em(d, P(0, 0), P(0, 8), EM_DOT) && para_fmt_at(d->p[0], 2)->em == EM_DOT && para_fmt_at(d->p[0], 12)->em == EM_NONE);
  L = lay_new(d, &fs);
  lay_update(L);
  h1 = L->pl[0].lines[0].height;
  dotted = dark_pixels(L, 0, 2000);
  lay_free(L);
  ok("em: room for the marks above the line", h1 > h0);
  ok("em: the dots drawn", plain > 0 && dotted > plain);
  { Pos c; doc_undo(d, &c); }
  ok("em: undo", para_fmt_at(d->p[0], 2)->em == EM_NONE);
  memset(&r, 0, sizeof r);
  strcpy(r.text, "guide");
  ok("ruby: a phonetic guide set", intl_set_ruby(d, P(0, 9), P(0, 14), &r) && para_fmt_at(d->p[0], 10)->ruby);
  L = lay_new(d, &fs);
  lay_update(L);
  h2 = L->pl[0].lines[0].height;
  guided = dark_pixels(L, 0, 2000);
  lay_free(L);
  ok("ruby: room above the line and the guide drawn", h2 > h0 && guided > plain + 20);
  doc_free(d);
}

/* PDF: right-to-left text goes out as its visual glyphs (with an embedded font that has them) */
static void pdf_rtl(void)
{
  Doc *d;
  Layout *L;
  size_t n;
  u8 *pdf;
  if (!have_dejavu) return;
  d = doc_new();
  ins(d, "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D and \xD8\xA8\xD8\xB3\xD9\x85");
  intl_set_dir(d, 0, 0, 1);
  L = lay_new(d, &ifs);
  lay_update(L);
  pdf = pdf_export(L, &n);
  ok("pdf: right-to-left text exported", pdf && n > 1000);
  free(pdf);
  lay_free(L);
  doc_free(d);
}

void test_wp27(void)
{
  algorithm();
  conformance();
  fonts_intl();
  layout_rtl();
  layout_cjk();
  docx_rtl();
  pdf_rtl();
  marks();
  fonts_free(&ifs);
}
