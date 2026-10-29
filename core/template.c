/* template.c - see template.h */
#include <string.h>
#include <stdlib.h>
#include "template.h"

static const char *const names[TPL_N] = { "Blank document", "Welcome to Quill", "Letter", "R\xc3\xa9sum\xc3\xa9", "Report" };
/* the name shown on the File page; "" for a number out of range, so callers need no check */
const char *template_name(int which) { return which >= 0 && which < TPL_N ? names[which] : ""; }

/* a paragraph at the end in a style (by id), its text with *bold* and _italic_ marked; extra: more
   flags for all of it. Returns the paragraph (to adjust), or NULL */
static Para *add(Doc *d, const char *style, const char *text, unsigned extra)
{
  int si = doc_style(d, style);
  const Style *st = &d->styles[si < 0 ? 0 : si];
  ParaFmt pf = st->pf;
  CharFmt cf = st->cf;
  Para *p;
  const char *s = text, *run = text;
  pf.style = (u8)(si < 0 ? 0 : si);
  cf.flags |= (u8)extra;
  if (!(p = para_new(&pf, &cf))) return NULL;
  for (;; s++) {
    if (*s == '*' || *s == '_' || !*s) {
      if (s > run) para_append(p, run, (u32)(s - run), &cf);
      if (!*s) break;
      cf.flags ^= (u8)(*s == '*' ? CF_BOLD : CF_ITALIC);
      run = s + 1;
    }
  }
  para_normalise(p);
  /* the new document's one empty paragraph is used for the first */
  if (d->np == 1 && d->p[0]->len == 0 && !d->p[0]->pf.cell.table && d->p[0]->pf.style == 0 && !d->p[0]->pf.list && d->p[0]->r[0].end == 0 && p) {
    para_free(d->p[0]);
    d->p[0] = p;
    return p;
  }
  if (!doc_insert_para(d, d->np, p)) return NULL;
  return p;
}
/* a List Paragraph in list number list (1-based, 0 for a plain paragraph) at level 0, with the
   indents the list's first level asks for and tighter spacing below */
static void bullet(Doc *d, int list, const char *text)
{
  Para *p = add(d, "ListParagraph", text, 0);
  if (p && list) {
    p->pf.list = (u16)list; p->pf.level = 0; p->pf.after = 40;
    p->pf.left = d->lists[list - 1].lvl[0].left; p->pf.first = -d->lists[list - 1].lvl[0].hanging;   /* as the list gives them */
  }
}
/* a table of rows x cols cells (the text of each in order), the first row bold and shaded */
static void table(Doc *d, int rows, int cols, const char *const *text)
{
  Table t;
  int r, c, k;
  i32 width = d->page.width - d->page.left - d->page.right;
  memset(&t, 0, sizeof t);
  t.ncols = (u8)cols; t.marl = t.marr = 108;
  for (c = 0; c < cols; c++) t.grid[c] = width / cols;
  if (!(k = doc_add_table(d, &t))) return;
  for (r = 0; r < rows; r++)
    for (c = 0; c < cols; c++) {
      Para *p = add(d, "Normal", text[r * cols + c], r == 0 ? CF_BOLD : 0);
      if (!p) return;
      p->pf.after = 0; p->pf.before = 0;
      p->pf.cell.table = (u16)k; p->pf.cell.row = (u16)r; p->pf.cell.col = (u8)c; p->pf.cell.span = 1;
      p->pf.cell.borders = BD_ALL; p->pf.cell.shade = r == 0 ? 0xDEEAF6 : COLOR_AUTO; p->pf.cell.header = r == 0;
    }
}
/* the look of the newer templates: Calibri text, headings in blue */
static void modern(Doc *d)
{
  int i, cal = doc_font(d, "Calibri"), cam = doc_font(d, "Cambria");
  for (i = 0; i < d->nstyles; i++) {
    Style *s = &d->styles[i];
    if (!strcmp(s->id, "Normal") || !strcmp(s->id, "ListParagraph") || !strcmp(s->id, "Quote")) { s->cf.font = (u8)cal; s->cf.size = 22; s->pf.after = 160; s->pf.line = 259; }
    else if (!strncmp(s->id, "Heading", 7)) { s->cf.font = (u8)cam; s->cf.color = 0x2F5496; }
    else if (!strcmp(s->id, "Title")) { s->cf.font = (u8)cam; s->cf.size = 56; s->pf.after = 80; }
    else if (!strcmp(s->id, "Subtitle")) { s->cf.font = (u8)cal; s->cf.color = 0x595959; s->cf.flags = 0; }
  }
  if ((i = doc_style(d, "ListParagraph")) >= 0) d->styles[i].pf.after = 40;
  doc_styles_from_effective(d);
}

/* builds the template in code: all but the blank one get the modern look first, then their
   paragraphs. The result has no undo history and is not marked modified, so closing it
   straight away asks nothing. An unknown number gives a document with only the modern look */
Doc *doc_template(int which)
{
  Doc *d = doc_new();
  int list;
  Para *p;
  if (!d || which == TPL_BLANK) return d;
  modern(d);
  d->p[0]->pf = d->styles[0].pf;
  d->p[0]->r[0].f = d->styles[0].cf;
  switch (which) {
  case TPL_WELCOME: {
    static const char *const cells[] = { "Key", "Does", "Amiga-B", "Bold", "Amiga-I", "Italic", "Amiga-Z", "Undo", "Amiga-F", "Find and replace" };
    add(d, "Title", "Welcome to Quill", 0);
    add(d, "Subtitle", "A word processor for the Amiga", 0);
    add(d, "Heading1", "Getting started", 0);
    add(d, "Normal", "Quill reads and writes *Word documents* (.docx), so what you write here opens in other word processors and comes back "
                     "the same. Lines break where they would in Word, because Quill measures text with fonts of the same widths.", 0);
    add(d, "Normal", "The ribbon above holds the tools: _Home_ for fonts and paragraphs, _Insert_ for tables, pictures and page numbers, "
                     "_Layout_ for the page, _References_ for footnotes and contents, _Review_ for comments and changes.", 0);
    add(d, "Heading2", "Things to try", 0);
    list = doc_new_list(d, 0);
    bullet(d, list, "Pick a style from the gallery on the Home tab.");
    bullet(d, list, "Search with the box in the navigation pane on the left.");
    bullet(d, list, "Drag the slider at the bottom right to zoom.");
    bullet(d, list, "Export a PDF from the File page.");
    add(d, "Heading2", "Keys", 0);
    table(d, 5, 2, cells);
    add(d, "Normal", "", 0);
    add(d, "Quote", "\xe2\x80\x9cThe best way to learn a word processor is to write something with it.\xe2\x80\x9d", 0);
    break; }
  case TPL_LETTER:
    if ((p = add(d, "Normal", "*Your Name*\v12 High Street\vTownsville\vAB1 2CD", 0))) p->pf.align = AL_RIGHT;
    add(d, "Normal", "24 October 2026", 0);
    add(d, "Normal", "Recipient Name\vTheir Company\v1 Business Park\vCityville\vEF3 4GH", 0);
    add(d, "Normal", "Dear Recipient Name,", 0);
    add(d, "Normal", "Thank you for your letter. Write the first paragraph of your letter here: say why you are writing, "
                     "and what you would like the reader to know or do.", 0);
    add(d, "Normal", "Use a second paragraph for the details, and a third to close: what happens next, and how to reach you.", 0);
    add(d, "Normal", "Yours sincerely,", 0);
    add(d, "Normal", "", 0);
    add(d, "Normal", "Your Name", 0);
    break;
  case TPL_RESUME:
    if ((p = add(d, "Title", "Your Name", 0))) p->pf.after = 0;
    add(d, "Subtitle", "12 High Street, Townsville  \xc2\xb7  01234 567890  \xc2\xb7  you@example.com", 0);
    add(d, "Heading1", "Profile", 0);
    add(d, "Normal", "Two or three sentences about you: what you do best, what you have done, and the work you are looking for.", 0);
    add(d, "Heading1", "Experience", 0);
    add(d, "Heading2", "Job Title, Company \xe2\x80\x94 2022 to now", 0);
    list = doc_new_list(d, 0);
    bullet(d, list, "What you were responsible for, in a line.");
    bullet(d, list, "Something you achieved, with a number if you can.");
    add(d, "Heading2", "Job Title, Company \xe2\x80\x94 2018 to 2022", 0);
    bullet(d, list, "What you were responsible for, in a line.");
    bullet(d, list, "Something you achieved, with a number if you can.");
    add(d, "Heading1", "Education", 0);
    add(d, "Normal", "*Degree or qualification*, School or University, 2018", 0);
    add(d, "Heading1", "Skills", 0);
    add(d, "Normal", "Writing  \xc2\xb7  Planning  \xc2\xb7  Languages  \xc2\xb7  Software you know well", 0);
    break;
  case TPL_REPORT: {
    static const char *const cells[] = { "Quarter", "Result", "Change", "First", "1,200", "+4%", "Second", "1,350", "+12%", "Third", "1,310", "\xe2\x88\x92" "3%" };
    add(d, "Title", "Report Title", 0);
    add(d, "Subtitle", "A subtitle or the date", 0);
    add(d, "Heading1", "Summary", 0);
    add(d, "Normal", "Say in a paragraph what the report found and what it recommends. Many readers read only this.", 0);
    add(d, "Heading1", "Introduction", 0);
    add(d, "Normal", "Why the report was written, and what it covers. Headings in the Heading 1 and Heading 2 styles appear in the "
                     "navigation pane and in a table of contents.", 0);
    add(d, "Heading1", "Findings", 0);
    add(d, "Heading2", "The first finding", 0);
    add(d, "Normal", "The details, with the figures that support them.", 0);
    table(d, 4, 3, cells);
    add(d, "Normal", "", 0);
    add(d, "Heading2", "The second finding", 0);
    add(d, "Normal", "The details, with the figures that support them.", 0);
    add(d, "Heading1", "Recommendations", 0);
    list = doc_new_list(d, 1);
    bullet(d, list, "The first thing to do.");
    bullet(d, list, "The second thing to do.");
    {
      int k = doc_style(d, "Footer");
      story_set_text(d, &d->ftr[HF_DEFAULT], "Page {page} of {pages}", k < 0 ? 0 : k, AL_CENTER);
    }
    break; }
  }
  doc_clear_undo(d);
  d->dirty = 0;
  return d;
}

/* ---- cover pages */
/* a paragraph of the cover: style, text, alignment, space before (twips), in the bookmark bm */
static Para *cover_para(Doc *d, const char *style, const char *text, u8 align, i32 before, u16 bm, u32 color)
{
  int si = doc_style(d, style);
  const Style *st = &d->styles[si < 0 ? 0 : si];
  ParaFmt pf = st->pf;
  CharFmt cf = st->cf;
  Para *p;
  pf.style = (u8)(si < 0 ? 0 : si);
  pf.align = align; pf.before = before; pf.pagebreak = 0; pf.list = 0;
  cf.bmark = bm;
  if (color != COLOR_AUTO) cf.color = color;
  if (!(p = para_new(&pf, &cf))) return NULL;
  if (text[0]) para_append(p, text, (u32)strlen(text), &cf);
  return p;
}
/* where the cover page is: its bookmark's stretch */
static int doc_cover_page_find(const Doc *d, Pos *a, Pos *b) { return doc_bmark_find(d, "_Cover", a, b); }
/* see template.h */
int doc_cover_remove(Doc *d)
{
  Pos a, b;
  ParaFmt pf;
  u32 n;
  if (!doc_cover_page_find(d, &a, &b)) return 0;
  n = b.para - a.para + 1;
  doc_begin(d, a);
  if (n >= d->np) {
    Para *blank[1];
    if (!(blank[0] = para_new(&d->styles[0].pf, &d->styles[0].cf)) || !doc_replace(d, a.para, n, blank, 1, a)) { doc_end(d, a); return 0; }
  } else {
    if (!doc_replace(d, a.para, n, NULL, 0, a)) { doc_end(d, a); return 0; }
    /* the text that followed no longer needs to start a page of its own */
    memset(&pf, 0, sizeof pf);
    if (a.para == 0) doc_para_format(d, 0, 0, PM_BREAK, &pf);
  }
  doc_end(d, a);
  return 1;
}
/* see template.h */
int doc_cover_page(Doc *d, int design, const char *title, const char *subtitle, const char *author, const char *date)
{
  static const u32 accent = 0x1F4E79;
  Para *nw[6];
  int n = 0, i;
  u16 bm;
  Pos at;
  ParaFmt pf;
  at.para = 0; at.off = 0;
  if (design < 0 || design >= COVER_N || !(bm = (u16)doc_bmark(d, "_Cover"))) return 0;
  doc_begin(d, at);
  doc_cover_remove(d);
  if (design == COVER_BANDED) {
    /* a dark band across the top of the page behind the title, which is white on it */
    int k = doc_add_shape(d, SH_RECT, d->page.width, 4320);
    char ref[8];
    nw[n++] = cover_para(d, "Title", "", AL_LEFT, 1080, bm, 0xFFFFFF);
    if (k >= 0 && nw[0]) {
      Image *im = &d->images[k];
      CharFmt cf = nw[0]->r[0].f;
      im->wrap = WRAP_BEHIND; im->hrel = POS_PAGE; im->vrel = POS_PAGE; im->x = 0; im->y = 0;
      im->fill = accent; im->lstyle = PB_NONE;
      pic_utf8(ref, k);
      para_append(nw[0], ref, 4, &cf);
      para_append(nw[0], title, (u32)strlen(title), &cf);
    }
    nw[n++] = cover_para(d, "Subtitle", subtitle, AL_LEFT, 1440, bm, COLOR_AUTO);
    nw[n++] = cover_para(d, "Normal", author, AL_LEFT, 6000, bm, COLOR_AUTO);
    nw[n++] = cover_para(d, "Normal", date, AL_LEFT, 0, bm, COLOR_AUTO);
  } else {
    u8 al = design == COVER_SIMPLE ? AL_CENTER : AL_LEFT;
    nw[n++] = cover_para(d, "Title", title, al, 4320, bm, COLOR_AUTO);
    nw[n++] = cover_para(d, "Subtitle", subtitle, al, 0, bm, COLOR_AUTO);
    nw[n++] = cover_para(d, "Normal", author, al, 5760, bm, COLOR_AUTO);
    nw[n++] = cover_para(d, "Normal", date, al, 0, bm, COLOR_AUTO);
    if (design == COVER_SIDELINE)
      for (i = 0; i < n; i++) if (nw[i]) {
        /* a thick line down the left of the cover's text */
        nw[i]->pf.bstyle[PE_LEFT] = PB_THICK; nw[i]->pf.bwidth[PE_LEFT] = 36; nw[i]->pf.bcolor[PE_LEFT] = accent; nw[i]->pf.bspace[PE_LEFT] = 12;
        nw[i]->pf.left = 360;
      }
  }
  for (i = 0; i < n; i++) if (!nw[i]) { for (i = 0; i < n; i++) para_free(nw[i]); doc_end(d, at); return 0; }
  /* empty lines keep the bookmark running on: each has a space at least */
  for (i = 0; i < n; i++) if (!nw[i]->len) para_append(nw[i], " ", 1, &nw[i]->r[0].f);
  if (!doc_replace(d, 0, 0, nw, (u32)n, at)) { doc_end(d, at); return 0; }
  memset(&pf, 0, sizeof pf); pf.pagebreak = 1;
  if ((u32)n < d->np) doc_para_format(d, (u32)n, (u32)n, PM_BREAK, &pf);
  doc_end(d, at);
  return 1;
}
