/* test_wp28c.c - the native tests of WP28c HTML and Markdown (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp28c() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/fmtio.h"

extern const DocFormat fmt_html, fmt_markdown;

static Doc *again(const DocFormat *f, Doc *d, char **out)
{
  size_t n;
  char err[200];
  unsigned l = 0;
  u8 *data = f->save(d, &n);
  Doc *e = data ? f->load(data, n, &l, err, sizeof err) : NULL;
  if (out) *out = (char *)data; else free(data);
  return e;
}
static Doc *from(const DocFormat *f, const char *s, unsigned *lost)
{
  char err[200];
  unsigned l = 0;
  Doc *d = f->load((const u8 *)s, strlen(s), &l, err, sizeof err);
  if (lost) *lost = l;
  return d;
}
static void add(Doc *d, Pos *c, const char *s) { doc_insert(d, c, s, (u32)strlen(s), NULL); }
static void unpic(Doc *d)
{
  u32 i, k;
  char ref[8];
  pic_utf8(ref, 0);
  for (i = 0; i < d->np; i++) for (k = 0; k + 4 <= d->p[i]->len; k++) if (IS_PIC(d->p[i]->t + k)) memcpy(d->p[i]->t + k, ref, 4);
}
static int texts_equal(Doc *a, Doc *b)
{
  u32 i;
  if (a->np != b->np) { printf("  %lu vs %lu paragraphs\n", (unsigned long)a->np, (unsigned long)b->np); return 0; }
  for (i = 0; i < a->np; i++) if (strcmp(a->p[i]->t, b->p[i]->t)) { printf("  paragraph %lu: [%s] vs [%s]\n", (unsigned long)i, a->p[i]->t, b->p[i]->t); return 0; }
  return 1;
}
static u32 find_para(Doc *d, const char *t) { u32 i; for (i = 0; i < d->np; i++) if (strstr(d->p[i]->t, t)) return i; return (u32)-1; }
static const CharFmt *fmt_of(Doc *d, const char *t)
{
  u32 i = find_para(d, t);
  if (i == (u32)-1) return NULL;
  return para_fmt_at(d->p[i], (u32)(strstr(d->p[i]->t, t) - d->p[i]->t));
}

/* a document with what HTML and Markdown both carry */
static Doc *sample(void)
{
  Doc *d = doc_new();
  Pos c = doc_start(d);
  CharFmt f;
  ParaFmt pf;
  Table t;
  int list, k, pic, note;
  char ref[8];
  size_t n;
  u8 *png = file_read("tests/data/disc.png", &n);
  add(d, &c, "Main heading\nPlain bold italic struck code linked caf\xC3\xA9 \xE2\x80\x9Cquoted\xE2\x80\x9D 3 < 4 & 5 > 2 *stars* _under_ [br]\nBullet one\nNested\nBullet two\nFirst\nSecond\nSub heading\nCode line one\n    indented code\n");
  doc_apply_style(d, 0, 0, doc_style(d, "Heading1"));
  doc_apply_style(d, 7, 7, doc_style(d, "Heading2"));
  k = fmt_code_style(d);
  doc_apply_style(d, 8, 9, k);
  f = *para_fmt_at(d->p[1], 0);
  f.flags = CF_BOLD; doc_format(d, P(1, 6), P(1, 10), CM_BOLD, &f);
  f.flags = CF_ITALIC; doc_format(d, P(1, 11), P(1, 17), CM_ITALIC, &f);
  f.flags = CF_STRIKE; doc_format(d, P(1, 18), P(1, 24), CM_STRIKE, &f);
  f.font = (u8)doc_font(d, "Courier New"); doc_format(d, P(1, 25), P(1, 29), CM_FONT, &f);
  doc_set_link(d, P(1, 30), P(1, 36), "https://example.org/a b", NULL, NULL);
  list = doc_list_preset(d, LP_BULLETS);
  doc_list_apply(d, 2, 4, list);
  memset(&pf, 0, sizeof pf); pf.list = (u16)list; pf.level = 1; pf.left = 1440; pf.first = -360;
  doc_para_format(d, 3, 3, PM_LIST | PM_INDENT, &pf);
  list = doc_list_preset(d, LP_NUMBERS);
  doc_list_apply(d, 5, 6, list);
  /* a table: heading row, a cell merged across */
  memset(&t, 0, sizeof t);
  t.ncols = 3; t.grid[0] = t.grid[1] = t.grid[2] = 3000; t.marl = t.marr = 108; t.look = LK_DEFAULT;
  {
    int tn = doc_add_table(d, &t);
    static const struct { int row, col, span; const char *text; } cells[] = {
      { 0, 0, 1, "Name" }, { 0, 1, 1, "Value" }, { 0, 2, 1, "Note" }, { 1, 0, 1, "a|b" }, { 1, 1, 1, "1" }, { 1, 2, 1, "x" } };
    for (k = 0; k < 6; k++) {
      Para *p;
      pf = d->styles[0].pf;
      memset(&pf.cell, 0, sizeof pf.cell);
      pf.cell.table = (u16)tn; pf.cell.row = (u16)cells[k].row; pf.cell.col = (u8)cells[k].col; pf.cell.span = (u8)cells[k].span;
      pf.cell.borders = BD_ALL; pf.cell.shade = COLOR_AUTO; pf.cell.header = (u8)(cells[k].row == 0);
      pf.cell.bcolor[0] = pf.cell.bcolor[1] = pf.cell.bcolor[2] = pf.cell.bcolor[3] = COLOR_AUTO;
      p = para_new(&pf, &d->styles[0].cf);
      para_append(p, cells[k].text, (u32)strlen(cells[k].text), &d->styles[0].cf);
      doc_insert_para(d, d->np - 1, p);
    }
  }
  c = doc_end_pos(d);
  add(d, &c, "Picture ");
  pic = png ? doc_add_image(d, png, (u32)n, 1500, 1500) : -1;
  if (pic >= 0) { pic_utf8(ref, pic); add(d, &c, ref); strcpy(d->images[pic].alt, "a disc"); }
  add(d, &c, " and a note");
  note = doc_add_note(d);
  note_utf8(ref, note); add(d, &c, ref);
  story_set_text(d, &d->notes[note], "The note's text.", 0, AL_LEFT);
  add(d, &c, " end.\nLast line");
  strcpy(d->title, "Sample & co");
  return d;
}

static void html_roundtrip(void)
{
  Doc *d = sample(), *e;
  char *html = NULL;
  ParaFmt pf;
  CharFmt f;
  /* things only HTML has of these: alignment, spacing, colours, fonts */
  memset(&pf, 0, sizeof pf);
  pf.align = AL_CENTER; pf.before = 240; pf.after = 60; pf.left = 360; pf.first = 180; pf.line = 360; pf.linerule = LS_AUTO;
  doc_para_format(d, d->np - 1, d->np - 1, PM_ALIGN | PM_SPACING | PM_INDENT | PM_LINE, &pf);
  f = *para_fmt_at(d->p[d->np - 1], 0);
  f.color = 0x1F4E79; f.size = 30; f.font = (u8)doc_font(d, "Arial"); f.hl = HL_ON | 0xFFFF00; f.flags |= CF_UNDERLINE;
  doc_format(d, P(d->np - 1, 0), P(d->np - 1, 4), CM_COLOR | CM_SIZE | CM_FONT | CM_HL | CM_UNDERLINE, &f);
  doc_set_bmark(d, P(d->np - 1, 5), P(d->np - 1, 9), "lastline");
  e = again(&fmt_html, d, &html);
  ok("html: written and read back", e != NULL);
  if (!e) { doc_free(d); free(html); return; }
  unpic(d); unpic(e);
  ok("html: the same text, paragraph, cell and character formatting", same_doc(d, e));
  ok("html: headings as <h1>, styles as classes, the title", html && strstr(html, "<h1 class=\"Heading1\"") && strstr(html, ".Code {") && !strcmp(e->title, "Sample & co"));
  ok("html: lists, levels and kinds", e->p[2]->pf.list && e->p[3]->pf.level == 1 && e->p[5]->pf.list && e->p[5]->pf.list != e->p[2]->pf.list &&
     e->lists[e->p[5]->pf.list - 1].lvl[0].fmt == NF_DECIMAL && e->lists[e->p[2]->pf.list - 1].lvl[0].fmt == NF_BULLET);
  ok("html: link", fmt_of(e, "linked") && !strcmp(doc_link_url(e, fmt_of(e, "linked")->link), "https://example.org/a b"));
  ok("html: table with a heading row", e->p[10]->pf.cell.table && e->p[10]->pf.cell.header && !strcmp(e->p[13]->t, "a|b") && e->tables[e->p[10]->pf.cell.table - 1].ncols == 3);
  {
    u32 pi = find_para(e, "Picture");
    ok("html: picture embedded with its size and alt text", e->nimages == 1 && e->images[0].cx == 1500 && !strcmp(e->images[0].alt, "a disc") && e->images[0].len == d->images[0].len &&
       html && strstr(html, "<img src=\"data:image/png;base64,iVBOR") != NULL);
    ok("html: footnote back as a footnote (in LibreOffice's markup)", e->nnotes == 1 && e->notes[0].np == 1 && !strcmp(e->notes[0].p[0]->t, "The note's text.") && pi != (u32)-1 && strstr(e->p[pi]->t, "\xF3\xB4\x80\x80"));
  }
  {
    const CharFmt *g = fmt_of(e, "Last");
    Para *q = e->p[e->np - 1];
    ok("html: colour, size, font, highlight, underline; alignment, spacing, indents, line spacing",
       g && g->color == 0x1F4E79 && g->size == 30 && !strcmp(e->fonts[g->font], "Arial") && g->hl == (HL_ON | 0xFFFF00) && (g->flags & CF_UNDERLINE) &&
       q->pf.align == AL_CENTER && q->pf.before == 240 && q->pf.after == 60 && q->pf.left == 360 && q->pf.first == 180 && q->pf.line == 360);
    ok("html: bookmark", para_fmt_at(q, 5)->bmark && !strcmp(doc_bmark_name(e, para_fmt_at(q, 5)->bmark), "lastline"));
  }
  ok("html: what was read is sane", runs_sane(e));
  free(html);
  doc_free(e); doc_free(d);
}

static void markdown_roundtrip(void)
{
  Doc *d = sample(), *e;
  char *md = NULL;
  e = again(&fmt_markdown, d, &md);
  if (getenv("QUILL_DUMP") && md) file_write("/tmp/q.md", md, strlen(md));
  ok("md: written and read back", e != NULL);
  if (!e) { doc_free(d); free(md); return; }
  unpic(d); unpic(e);
  ok("md: the same paragraphs and text", texts_equal(d, e));
  ok("md: headings", !strcmp(e->styles[e->p[0]->pf.style].id, "Heading1") && !strcmp(e->styles[e->p[7]->pf.style].id, "Heading2") && md && !strncmp(md, "# Main heading\n", 15));
  {
    const CharFmt *b = fmt_of(e, "bold"), *i = fmt_of(e, "italic"), *s = fmt_of(e, "struck"), *c = fmt_of(e, "code"), *l = fmt_of(e, "linked"), *p = fmt_of(e, "Plain");
    ok("md: bold, italic, strikethrough, code, links; punctuation escaped", b && (b->flags & CF_BOLD) && i && (i->flags & CF_ITALIC) && s && (s->flags & CF_STRIKE) &&
       c && !strcmp(e->fonts[c->font], "Courier New") && l && !strcmp(doc_link_url(e, l->link), "https://example.org/a b") && p && !(p->flags & (CF_BOLD | CF_ITALIC)) &&
       strstr(md, "\\*stars\\*") && strstr(md, "\\[br\\]"));
  }
  ok("md: lists with levels; numbered after bulleted", e->p[2]->pf.list && e->p[3]->pf.level == 1 && e->p[4]->pf.level == 0 && e->p[5]->pf.list != e->p[2]->pf.list &&
     e->lists[e->p[5]->pf.list - 1].lvl[0].fmt == NF_DECIMAL && strstr(md, "- Bullet one\n  - Nested\n- Bullet two\n\n1. First\n2. Second\n"));
  ok("md: code paragraphs as a fenced block, kept as they are", fmt_is_code_style(e, e->p[8]->pf.style) && fmt_is_code_style(e, e->p[9]->pf.style) &&
     !strcmp(e->p[9]->t, "    indented code") && strstr(md, "```\nCode line one\n    indented code\n```\n"));
  ok("md: a GitHub table (a pipe in a cell escaped)", e->p[10]->pf.cell.table && e->p[13]->pf.cell.row == 1 && !strcmp(e->p[13]->t, "a|b") && strstr(md, "| Name | Value | Note |\n| --- | --- | --- |\n| a\\|b | 1 | x |\n"));
  ok("md: picture as a data: URI, with its alt text", e->nimages == 1 && !strcmp(e->images[0].alt, "a disc") && e->images[0].len == d->images[0].len && strstr(md, "![a disc](data:image/png;base64,"));
  ok("md: footnote", e->nnotes == 1 && e->notes[0].np == 1 && !strcmp(e->notes[0].p[0]->t, "The note's text.") && strstr(md, "[^1]") && strstr(md, "\n[^1]: The note's text.\n"));
  ok("md: what was read is sane", runs_sane(e));
  {
    char *md2 = NULL;
    Doc *g = again(&fmt_markdown, e, &md2);
    ok("md: a second round trip writes the same file", g && md2 && !strcmp(md, md2));
    doc_free(g); free(md2);
  }
  free(md);
  doc_free(e); doc_free(d);
}

/* Word's and LibreOffice's .docx test documents through HTML */
static void html_docx_files(void)
{
  static const char *const files[] = { "tests/data/word.docx", "tests/data/lo.docx", "tests/data/headers.docx", "tests/data/notes.docx", "tests/data/pictures.docx" };
  int i;
  for (i = 0; i < (int)(sizeof files / sizeof files[0]); i++) {
    char err[200], what[120];
    unsigned lost;
    Doc *d = doc_load_file(files[i], &lost, err, sizeof err), *e;
    if (!d) { ok(files[i], 0); continue; }
    e = again(&fmt_html, d, NULL);
    if (e) { unpic(d); unpic(e); }
    snprintf(what, sizeof what, "html: %s through HTML keeps text and formatting", files[i] + 11);
    ok(what, e && same_doc(d, e) && e->nnotes == d->nnotes);
    doc_free(e); doc_free(d);
  }
  {
    /* tables.docx: the text and the cells (a covered cell's empty paragraph takes Normal's spacing) */
    char err[200];
    unsigned lost;
    Doc *d = doc_load_file("tests/data/tables.docx", &lost, err, sizeof err), *e = d ? again(&fmt_html, d, NULL) : NULL;
    u32 k, cells = 0;
    if (d && e && d->np == e->np) for (k = 0; k < d->np; k++) cells += d->p[k]->pf.cell.span == e->p[k]->pf.cell.span && d->p[k]->pf.cell.vmerge == e->p[k]->pf.cell.vmerge && d->p[k]->pf.cell.col == e->p[k]->pf.cell.col;
    ok("html: tables.docx keeps its merged cells", d && e && texts_equal(d, e) && cells == d->np && e->ntables == d->ntables);
    doc_free(e); doc_free(d);
  }
}

/* HTML that LibreOffice wrote (from tests/data's .docx files) */
static void html_from_libreoffice(void)
{
  static const char *const names[] = { "word", "tables", "notes", "pictures" };
  int i;
  for (i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
    char a[80], b[80], err[200], what[120];
    unsigned lost;
    Doc *d, *e;
    u32 k, same = 0, nd = 0, j = 0;
    snprintf(a, sizeof a, "tests/data/%s.docx", names[i]);
    snprintf(b, sizeof b, "tests/data/lo-%s.html", names[i]);
    d = doc_load_file(a, &lost, err, sizeof err);
    e = doc_load_file(b, &lost, err, sizeof err);
    if (d && e) {
      unpic(d); unpic(e);
      /* the non-empty paragraphs in order (LibreOffice puts a header's picture first) */
      for (k = 0; k < d->np; k++) {
        if (!d->p[k]->len) continue;
        nd++;
        while (j < e->np && (!e->p[j]->len || strcmp(d->p[k]->t, e->p[j]->t))) j++;
        if (j < e->np) { same++; j++; }
      }
    }
    snprintf(what, sizeof what, "html: LibreOffice's %s.html reads as %s.docx (text, tables, notes, pictures)", names[i], names[i]);
    ok(what, d && e && same == nd && e->ntables == d->ntables && e->nnotes == d->nnotes && e->nimages == d->nimages);
    if (d && e && i == 0) {
      const CharFmt *g = fmt_of(e, "bold");
      ok("html: LibreOffice's headings, bold, centring, lists, tab", g && (g->flags & CF_BOLD) && fmt_heading_level(e, e->p[1]->pf.style) == 1 && e->p[3]->pf.align == AL_CENTER &&
         e->p[6]->pf.list && strchr(e->p[2]->t, '\t'));
    }
    if (d && e && i == 3) ok("html: LibreOffice's pictures read from the files next to the page", e->nimages == 3 && e->images[1].len > 1000);
    doc_free(d); doc_free(e);
  }
}

static void html_details(void)
{
  unsigned lost;
  Doc *d;
  const CharFmt *f;
  d = from(&fmt_html, "<html><head><title>T</title><style>p { margin-bottom: 0 } .x{color:red}</style><script>var a='<p>no</p>';</script></head>"
                      "<body>  <p>one\n   two&nbsp;&amp;&#x263A; <b>bold <i>both</i></b> <font face=\"Courier New\" color=\"#ff0000\" size=\"5\">f</font>"
                      " <span style=\"font-weight:bold; color: rgb(0, 128, 0); font-size: 18pt\">s</span><br>after<br></p>"
                      "<p></p><div>in div<p>p in div</p>tail</div><center>c</center><pre>  a\n    b</pre>"
                      "<ul><li>x<ul><li>y</li></ul></li><li>z</li></ul><ol start=\"3\" type=\"a\"><li>q</li></ol>"
                      "<blockquote>quoted</blockquote><hr><p style=\"text-align:right;margin-left:1in;line-height:150%\">styled</p>"
                      "<h3>h</h3><p>a<sup>2</sup> H<sub>2</sub>O <u>u</u> <s>s</s> <code>c</code> <a href=\"http://x.example/?a=1&amp;b=2\">l</a></p></body></html>", &lost);
  ok("html: white space collapsed, entities, <br>; head and script left out", d && !strcmp(d->p[0]->t, "one two\xC2\xA0&\xE2\x98\xBA bold both f s\vafter") && !strcmp(d->title, "T"));
  f = d ? fmt_of(d, "both") : NULL;
  ok("html: nested inline formatting, <font>, CSS in style=", f && (f->flags & (CF_BOLD | CF_ITALIC)) == (CF_BOLD | CF_ITALIC) &&
     (f = fmt_of(d, "f")) && f->color == 0xFF0000 && f->size == 36 && !strcmp(d->fonts[f->font], "Courier New") &&
     (f = fmt_of(d, " s")) && (para_fmt_at(d->p[0], (u32)(strstr(d->p[0]->t, " s") - d->p[0]->t) + 1)->flags & CF_BOLD) &&
     para_fmt_at(d->p[0], (u32)(strstr(d->p[0]->t, " s") - d->p[0]->t) + 1)->color == 0x008000 && para_fmt_at(d->p[0], (u32)(strstr(d->p[0]->t, " s") - d->p[0]->t) + 1)->size == 36);
  ok("html: blocks: an empty <p>, text round a <p> in a <div>, <center>, <pre> lines kept", d && d->np > 8 && !d->p[1]->len && !strcmp(d->p[2]->t, "in div") && !strcmp(d->p[3]->t, "p in div") &&
     !strcmp(d->p[4]->t, "tail") && d->p[5]->pf.align == AL_CENTER && !strcmp(d->p[6]->t, "  a") && !strcmp(d->p[7]->t, "    b") && fmt_is_code_style(d, d->p[7]->pf.style));
  ok("html: nested lists, an ordered list's start and type", d && !strcmp(d->p[8]->t, "x") && d->p[9]->pf.level == 1 && d->p[10]->pf.level == 0 &&
     d->p[11]->pf.list && d->lists[d->p[11]->pf.list - 1].lvl[0].fmt == NF_LOWER_LETTER && d->lists[d->p[11]->pf.list - 1].lvl[0].start == 3);
  ok("html: blockquote, hr, paragraph CSS", d && !strcmp(d->styles[d->p[12]->pf.style].name, "Quote") && d->p[13]->pf.bstyle[PE_BOTTOM] == PB_SINGLE &&
     d->p[14]->pf.align == AL_RIGHT && d->p[14]->pf.left == 1440 && d->p[14]->pf.line == 360 && d->p[14]->pf.after == 0);
  ok("html: <style> rules change styles", d && d->styles[0].pf.after == 0);
  ok("html: sup, sub, u, s, code, a link's &amp;", d && fmt_heading_level(d, d->p[15]->pf.style) == 3 && (fmt_of(d, "2 H")->flags & CF_SUPER) &&
     (para_fmt_at(d->p[16], 4)->flags & CF_SUB) && (fmt_of(d, "u ")->flags & CF_UNDERLINE) && (fmt_of(d, "s c")->flags & CF_STRIKE) &&
     !strcmp(d->fonts[fmt_of(d, "c l")->font], "Courier New") && !strcmp(doc_link_url(d, para_fmt_at(d->p[16], d->p[16]->len - 1)->link), "http://x.example/?a=1&b=2"));
  doc_free(d);
  d = from(&fmt_html, "<table border=1><tr><th>A</th><th colspan=2>B</th></tr><tr><td rowspan=2 bgcolor=\"#ffff00\">C</td><td>D</td><td>E</td></tr>"
                      "<tr><td>F</td><td><table><tr><td>in</td></tr></table></td></tr></table><p>after</p>", &lost);
  ok("html: colspan, rowspan, shading, borders; a nested table flattened", d && d->ntables == 1 && d->tables[0].ncols == 3 &&
     d->p[1]->pf.cell.span == 2 && d->p[2]->pf.cell.vmerge == VM_START && d->p[2]->pf.cell.shade == 0xFFFF00 && d->p[5]->pf.cell.vmerge == VM_CONT &&
     d->p[5]->pf.cell.col == 0 && !strcmp(d->p[6]->t, "F") && !strcmp(d->p[7]->t, "in") && d->p[7]->pf.cell.col == 2 && (d->p[0]->pf.cell.borders & BD_ALL) == BD_ALL &&
     d->p[0]->pf.cell.header && (lost & LOST_TABLES) && !strcmp(d->p[d->np - 1]->t, "after") && !d->p[d->np - 1]->pf.cell.table);
  doc_free(d);
  {
    /* Windows 1252 bytes, a picture in a file next to the page, one missing */
    static const char page[] = "<p>caf\xE9 \x93q\x94 <img src=\"disc%20copy.png\" width=\"40\"> <img src=\"missing.png\"></p>";
    char err[200];
    size_t n;
    u8 *png = file_read("tests/data/disc.png", &n);
    file_write("/tmp/quill_wp28c_page.html", page, sizeof page - 1);
    if (png) file_write("/tmp/disc copy.png", png, n);
    d = doc_load_file("/tmp/quill_wp28c_page.html", &lost, err, sizeof err);
    ok("html: Windows 1252 text; a picture from a file next to the page, sized; a missing one reported", d && !strncmp(d->p[0]->t, "caf\xC3\xA9 \xE2\x80\x9Cq\xE2\x80\x9D ", 12) &&
       d->nimages == 1 && d->images[0].cx == 600 && d->images[0].cy == 600 && (lost & LOST_PICTURES));
    doc_free(d); free(png);
  }
  ok("html: the format is known by its name", doc_format_for_name("Page.HTM", 1) == &fmt_html && doc_format_for_name("x.html", 0) == &fmt_html);
  ok("html: garbage doesn't crash", (d = from(&fmt_html, "<<p <b>x</i></table></li><td>y<!-- z", &lost)) != NULL);
  doc_free(d);
}

static void markdown_details(void)
{
  unsigned lost;
  Doc *d;
  const CharFmt *f;
  d = from(&fmt_markdown, "Title\n=====\n\nSub\n---\n\n### Three ###\n\nA *b* **c** ***d*** _e_ __f__ ~~g~~ `h *i*` j_k_l *m\n"
                          "continued  \nbreak\\\nagain\n\n> quote\nlazy\n\n* * *\n\n- a\n- b\n  1. c\n  2. d\n\n     para in b?\n- e\n\nPlain.\n\n"
                          "    code\n\n~~~ c\nint x;\n~~~\n\n[ref] and [text][ref] and <http://auto.example> and [inline](http://i.example \"t\") ![pic](missing.png)\n\n"
                          "[ref]: http://ref.example\n\n| L | C | R |\n|:--|:-:|--:|\n| 1 | 2 | 3 |\n\nNote[^n].\n\n[^n]: The note.\n", &lost);
  ok("md: setext and ATX headings", d && fmt_heading_level(d, d->p[0]->pf.style) == 1 && fmt_heading_level(d, d->p[1]->pf.style) == 2 &&
     fmt_heading_level(d, d->p[2]->pf.style) == 3 && !strcmp(d->p[2]->t, "Three"));
  ok("md: emphasis by CommonMark's rules, code spans, soft and hard breaks", d && !strcmp(d->p[3]->t, "A b c d e f g h *i* j_k_l *m continued\vbreak\vagain") &&
     (fmt_of(d, "b c")->flags & CF_ITALIC) && (fmt_of(d, "c d")->flags & CF_BOLD) && (fmt_of(d, "d e")->flags & (CF_BOLD | CF_ITALIC)) == (CF_BOLD | CF_ITALIC) &&
     (fmt_of(d, "e f")->flags & CF_ITALIC) && (fmt_of(d, "f g")->flags & CF_BOLD) && (fmt_of(d, "g h")->flags & CF_STRIKE) &&
     !strcmp(d->fonts[fmt_of(d, "h *i*")->font], "Courier New") && !(fmt_of(d, "j_k")->flags & CF_ITALIC) && !(fmt_of(d, "*m")->flags & CF_ITALIC));
  ok("md: block quote with a lazy line, thematic break", d && !strcmp(d->p[4]->t, "quote lazy") && !strcmp(d->styles[d->p[4]->pf.style].name, "Quote") &&
     d->p[5]->pf.bstyle[PE_BOTTOM] == PB_SINGLE && !d->p[5]->len);
  {
    u32 a = 0, c = 0, k;
    for (k = 0; d && k < d->np; k++) { if (!strcmp(d->p[k]->t, "a") && !a) a = k; if (!strcmp(d->p[k]->t, "c") && !c) c = k; }
    ok("md: nested lists of both kinds, a paragraph inside an item", d && a == 6 && c == 8 && d->p[c]->pf.level == 1 && d->lists[d->p[c]->pf.list - 1].lvl[1].fmt != NF_BULLET &&
       d->p[6]->pf.list == d->p[c]->pf.list && !strcmp(d->p[10]->t, "para in b?") && !strcmp(d->p[11]->t, "e") && d->p[11]->pf.level == 0 && d->p[11]->pf.list == d->p[6]->pf.list);
  }
  ok("md: indented and fenced code", d && !strcmp(d->p[12]->t, "Plain.") && !strcmp(d->p[13]->t, "code") && fmt_is_code_style(d, d->p[13]->pf.style) && !strcmp(d->p[14]->t, "int x;") && fmt_is_code_style(d, d->p[14]->pf.style));
  f = d ? fmt_of(d, "ref and") : NULL;
  ok("md: reference, full reference, auto and inline links; a missing picture", f && !strcmp(doc_link_url(d, f->link), "http://ref.example") &&
     !strcmp(doc_link_url(d, fmt_of(d, "text")->link), "http://ref.example") && !strcmp(doc_link_url(d, fmt_of(d, "http://auto")->link), "http://auto.example") &&
     !strcmp(doc_link_url(d, fmt_of(d, "inline")->link), "http://i.example") && (lost & LOST_PICTURES) && find_para(d, "[ref]:") == (u32)-1);
  {
    u32 t = find_para(d, "L");
    ok("md: a GitHub table with its columns' alignment", t != (u32)-1 && d->p[t]->pf.cell.table && d->p[t + 1]->pf.align == AL_CENTER && d->p[t + 2]->pf.align == AL_RIGHT &&
       !strcmp(d->p[t + 5]->t, "3") && d->p[t + 5]->pf.cell.row == 1 && d->p[t]->pf.cell.header);
  }
  ok("md: footnote reference and definition", d && d->nnotes == 1 && !strcmp(d->notes[0].p[0]->t, "The note.") && IS_NOTE(d->p[d->np - 1]->t + 4));
  doc_free(d);
  d = from(&fmt_markdown, "caf\xE9\r\n\r\n1) one\r\n2) two\r\n\r\n5. five\r\n", &lost);
  ok("md: Latin-1 and CRLF; ordered lists with ')' and a start number", d && !strcmp(d->p[0]->t, "caf\xC3\xA9") && d->p[1]->pf.list && d->p[3]->pf.list &&
     d->p[3]->pf.list != d->p[1]->pf.list && d->lists[d->p[3]->pf.list - 1].lvl[0].start == 5);
  doc_free(d);
  ok("md: garbage doesn't crash", (d = from(&fmt_markdown, "*[`<a\n|x|\n|-|\n> - ```\n[^", &lost)) != NULL);
  doc_free(d);
  ok("md: the format is known by its name", doc_format_for_name("README.md", 1) == &fmt_markdown && doc_format_for_name("x.markdown", 0) == &fmt_markdown);
}

void test_wp28c(void)
{
  html_roundtrip();
  markdown_roundtrip();
  html_docx_files();
  html_from_libreoffice();
  html_details();
  markdown_details();
}
