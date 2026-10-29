/* test_wp28b.c - the native tests of WP28b ODT (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp28b() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/fmtio.h"
#include "../core/math.h"
#include "../core/mathml.h"
#include "../core/info.h"
#include "../core/zip.h"
#include "../core/notes.h"

extern const DocFormat fmt_odt, fmt_fodt;

/* d written as .odt (flat: Flat ODT) and read back */
static Doc *odt_again(Doc *d, int flat, unsigned *lost, u8 **out, size_t *outn)
{
  size_t n;
  char err[200];
  unsigned l = 0;
  const DocFormat *f = flat ? &fmt_fodt : &fmt_odt;
  u8 *data = f->save(d, &n);
  Doc *e = data ? f->load(data, n, &l, err, sizeof err) : NULL;
  if (data && !e) printf("  odt: %s\n", err);
  if (lost) *lost = l;
  if (out) { *out = data; *outn = n; } else free(data);
  return e;
}
static Doc *odt_str(const char *content, const char *styles, unsigned *lost)
{
  /* a package of content.xml (and styles.xml) */
  ZipWriter z;
  char err[200];
  unsigned l = 0;
  Doc *d;
  zip_begin(&z);
  zip_add(&z, "mimetype", "application/vnd.oasis.opendocument.text", 39);
  zip_add(&z, "content.xml", content, strlen(content));
  if (styles) zip_add(&z, "styles.xml", styles, strlen(styles));
  zip_finish(&z);
  d = fmt_odt.load((const u8 *)z.out.data, z.out.len, &l, err, sizeof err);
  if (!d) printf("  odt: %s\n", err);
  zip_free(&z);
  if (lost) *lost = l;
  return d;
}
/* one part of a package, malloc'd (NUL-terminated) */
static char *odt_part(const u8 *data, size_t n, const char *name)
{
  ZipReader z;
  char *s = NULL;
  size_t len;
  int k;
  if (!zip_open(&z, data, n)) return NULL;
  if ((k = zip_find(&z, name)) >= 0) s = (char *)zip_read(&z, k, &len);
  zip_close(&z);
  return s;
}
static void add(Doc *d, Pos *c, const char *s) { doc_insert(d, c, s, (u32)strlen(s), NULL); }
static char *linear(Doc *d, int k)
{
  Buf b;
  MathEq *e = doc_math(d, k);
  buf_init(&b);
  if (e) math_to_linear(e, &b);
  return b.data ? b.data : str_dup("");
}
static int find_para(Doc *d, const char *s)
{
  u32 i;
  for (i = 0; i < d->np; i++) if (strstr(d->p[i]->t, s)) return (int)i;
  return -1;
}

/* a document with a bit of everything ODT carries, written and read back */
static void odt_everything(void)
{
  Doc *d = doc_new(), *e;
  Pos c = doc_start(d);
  CharFmt f;
  ParaFmt pf;
  Table t;
  int tn, k, pic, note, enote, list, list2, eq, box, cm, ps;
  char ref[8];
  unsigned lost;
  size_t n, on;
  u8 *png = file_read("tests/data/disc.png", &n), *odt = NULL;
  add(d, &c, "Heading\nPlain bold italic red \xCE\xB1\xCE\xB2\xCE\xB3 \xF0\x9F\x98\x80 caf\xC3\xA9\tend  two  spaces \nCentred\nBullet one\nBullet two\nSub item\nAfter the list\nNumber one\nNumber two\n");
  doc_apply_style(d, 0, 0, doc_style(d, "Heading1"));
  f = *para_fmt_at(d->p[1], 0);
  f.flags = CF_BOLD; doc_format(d, P(1, 6), P(1, 10), CM_BOLD, &f);
  f.flags = CF_ITALIC; doc_format(d, P(1, 11), P(1, 17), CM_ITALIC, &f);
  f.color = 0xC00000; doc_format(d, P(1, 18), P(1, 21), CM_COLOR, &f);
  f.size = 31; f.font = (u8)doc_font(d, "Liberation Sans"); doc_format(d, P(1, 0), P(1, 5), CM_SIZE | CM_FONT, &f);
  f.flags = CF_UNDERLINE; f.ul = UL_DOUBLE; doc_format(d, P(1, 22), P(1, 28), CM_UNDERLINE | CM_UL, &f);
  f.flags = CF_SUPER; doc_format(d, P(1, 29), P(1, 33), CM_SUPER, &f);
  f.fx = FX_SMALLCAPS; f.hl = HL_ON | 0xFFFF00; f.spacing = 40; doc_format(d, P(1, 34), P(1, 39), CM_SMALLCAPS | CM_HL | CM_SPACING, &f);
  memset(&pf, 0, sizeof pf);
  pf.align = AL_CENTER; pf.left = 720; pf.first = -360; pf.before = 240; pf.after = 120; pf.line = 276; pf.linerule = LS_AUTO;
  pf.tabs[0] = 1440; pf.tabkind[0] = TAB_RIGHT; pf.tablead[0] = LEAD_DOT;
  pf.tabs[1] = 2880; pf.tabkind[1] = TAB_DECIMAL;
  pf.keepnext = 1; pf.shade = HL_ON | 0xDDEEFF;
  doc_para_format(d, 2, 2, PM_ALIGN | PM_INDENT | PM_SPACING | PM_LINE | PM_TABS | PM_KEEP | PM_SHADE, &pf);
  list = doc_list_preset(d, LP_BULLETS);
  doc_list_apply(d, 3, 5, list);
  memset(&pf, 0, sizeof pf); pf.list = (u16)list; pf.level = 1; pf.left = 1440; pf.first = -360;
  doc_para_format(d, 5, 5, PM_LIST | PM_INDENT, &pf);
  list2 = doc_list_preset(d, LP_LEGAL);
  doc_list_apply(d, 7, 8, list2);
  /* a 3 x 3 table: the first row's first two cells merged and a heading row, the last column merged
     down two rows, shading and borders; a table in a cell */
  memset(&t, 0, sizeof t);
  t.ncols = 3; t.grid[0] = 2000; t.grid[1] = 2500; t.grid[2] = 3000; t.marl = t.marr = 108; t.look = LK_DEFAULT; t.jc = AL_CENTER;
  tn = doc_add_table(d, &t);
  {
    static const struct { int row, col, span, vm; const char *text; } cells[] = {
      { 0, 0, 2, VM_NONE, "wide" }, { 0, 2, 1, VM_START, "tall" },
      { 1, 0, 1, VM_NONE, "a" }, { 1, 1, 1, VM_NONE, "b" }, { 1, 2, 1, VM_CONT, "" },
      { 2, 0, 1, VM_NONE, "c" }, { 2, 1, 1, VM_NONE, "d" }, { 2, 2, 1, VM_NONE, "e" } };
    for (k = 0; k < 8; k++) {
      Para *p;
      pf = d->styles[0].pf;
      memset(&pf.cell, 0, sizeof pf.cell);
      pf.cell.table = (u16)tn; pf.cell.row = (u16)cells[k].row; pf.cell.col = (u8)cells[k].col; pf.cell.span = (u8)cells[k].span;
      pf.cell.vmerge = (u8)cells[k].vm; pf.cell.borders = k == 3 ? BD_TOP | BD_BOTTOM : BD_ALL; pf.cell.shade = k == 2 ? 0xFFFF00 : COLOR_AUTO;
      pf.cell.header = cells[k].row == 0; pf.cell.rowh = cells[k].row == 2 ? -500 : 0; pf.cell.valign = k == 6 ? 1 : 0;
      pf.cell.bcolor[0] = pf.cell.bcolor[1] = pf.cell.bcolor[2] = pf.cell.bcolor[3] = COLOR_AUTO;
      p = para_new(&pf, &d->styles[0].cf);
      para_append(p, cells[k].text, (u32)strlen(cells[k].text), &d->styles[0].cf);
      doc_insert_para(d, d->np - 1, p);
      if (k == 6) {
        /* "d" holds a 1 x 2 table after its text */
        Table in;
        int itn;
        int j;
        memset(&in, 0, sizeof in);
        in.ncols = 2; in.grid[0] = 1000; in.grid[1] = 1200; in.marl = in.marr = 108; in.look = LK_DEFAULT;
        itn = doc_add_table(d, &in);
        for (j = 0; j < 2; j++) {
          ParaFmt q = pf;
          Para *pp;
          q.outer = pf.cell;
          memset(&q.cell, 0, sizeof q.cell);
          q.cell.table = (u16)itn; q.cell.row = 0; q.cell.col = (u8)j; q.cell.span = 1; q.cell.borders = BD_ALL; q.cell.shade = COLOR_AUTO;
          q.cell.bcolor[0] = q.cell.bcolor[1] = q.cell.bcolor[2] = q.cell.bcolor[3] = COLOR_AUTO;
          pp = para_new(&q, &d->styles[0].cf);
          para_append(pp, j ? "in2" : "in1", 3, &d->styles[0].cf);
          doc_insert_para(d, d->np - 1, pp);
        }
      }
    }
  }
  /* after the table: a picture in line, a floating one, a text box, a shape, a footnote, an endnote, a
     link, a bookmark, a comment, an equation, page fields; then a page break and a two-column section */
  c = doc_end_pos(d);
  add(d, &c, "Picture ");
  pic = png ? doc_add_image(d, png, (u32)n, 1440, 1440) : -1;
  if (pic >= 0) { pic_utf8(ref, pic); add(d, &c, ref); strcpy(d->images[pic].alt, "A disc"); }
  {
    Image im = d->images[pic >= 0 ? pic : 0];
    int fl;
    im.wrap = WRAP_SQUARE; im.hrel = POS_PAGE; im.vrel = POS_PARA; im.x = 3000; im.y = 200; im.halign = PA_NONE; im.valign = PA_NONE; im.flip = 1;
    im.crop[0] = 10000; im.alt[0] = 0;
    fl = doc_add_object(d, &im);
    pic_utf8(ref, fl); add(d, &c, ref);
  }
  box = doc_add_shape(d, -1, 2000, 800);
  if (box >= 0) { story_set_text(d, d->images[box].text ? d->images[box].text : (d->images[box].text = (Story *)calloc(1, sizeof(Story))), "Boxed text", 0, AL_LEFT); pic_utf8(ref, box); add(d, &c, ref); }
  ps = doc_add_shape(d, SH_ELLIPSE, 1000, 600);
  if (ps >= 0) { d->images[ps].fill = 0x00FF00; pic_utf8(ref, ps); add(d, &c, ref); }
  add(d, &c, " note");
  note = doc_add_note(d);
  note_utf8(ref, note); add(d, &c, ref);
  story_set_text(d, &d->notes[note], "The footnote's text.", 0, AL_LEFT);
  enote = doc_add_endnote(d);
  enote_utf8(ref, enote); add(d, &c, ref);
  story_set_text(d, &d->enotes[enote], "An endnote.", 0, AL_LEFT);
  add(d, &c, " see Quill site, x=");
  eq = doc_add_math(d, math_from_linear("(a+b)/c+\xE2\x88\x9A(x)+x^2+\xE2\x88\x91_(i=1)^n\xE2\x96\x92i", 0));
  if (eq >= 0) { math_utf8(ref, eq); add(d, &c, ref); }
  add(d, &c, " end\n");
  {
    MathEq *de = math_from_linear("a/b=c", 1);
    int dk = doc_add_math(d, de);
    math_utf8(ref, dk); add(d, &c, ref);
  }
  add(d, &c, "\nAfter the break\nTwo columns of text\nOne column again");
  {
    int pi = find_para(d, "Quill site");
    Pos a, b;
    char *tx = d->p[pi]->t;
    char *s = strstr(tx, "Quill site");
    a = P((u32)pi, (u32)(s - tx)); b = P((u32)pi, (u32)(s - tx) + 10);
    doc_set_link(d, a, b, "https://example.org/quill", NULL, NULL);
    doc_set_bmark(d, P((u32)pi, 0), P((u32)pi, 7), "pic");
    cm = doc_comment(d, P(6, 0), P(6, 5), "A comment", "Ada", "AL", "2026-10-24T10:30:00Z");
  }
  memset(&pf, 0, sizeof pf); pf.pagebreak = 1;
  doc_para_format(d, d->np - 3, d->np - 3, PM_BREAK, &pf);
  {
    SectFmt s;
    sect_default(&s); s.cols = 2; s.colgap = 360; s.colline = 1; s.start = SB_CONT; s.titlepg = 1;
    d->p[d->np - 2]->pf.sect = (u16)doc_add_sect(d, &s);     /* the section ending with "Two columns of text" */
    sect_default(&s); s.titlepg = 1;
    d->p[d->np - 3]->pf.sect = (u16)doc_add_sect(d, &s);     /* and the one before it, ending at "After the break" */
    sect_default(&s); s.start = SB_CONT;
    d->p[d->np - 1]->pf.sect = (u16)doc_add_sect(d, &s);     /* the last, continuous */
  }
  story_set_text(d, &d->hdr[HF_DEFAULT], "Header page {page} of {pages}", 0, AL_RIGHT);
  story_set_text(d, &d->hdr[HF_FIRST], "First page header", 0, AL_LEFT);
  story_set_text(d, &d->ftr[HF_DEFAULT], "Footer", 0, AL_CENTER);
  story_set_text(d, &d->ftr[HF_EVEN], "Even footer", 0, AL_LEFT);
  d->titlepg = 1; d->evenodd = 1;
  strcpy(d->title, "Everything"); strcpy(d->author, "Quill Tester");
  info_set(d, IP_SUBJECT, "Testing"); info_set(d, IP_KEYWORDS, "odt, quill");
  info_cust_set(d, "Client", CP_TEXT, "Amiga");
  d->fnpr.set = NPS_FMT | NPS_RESTART; d->fnpr.fmt = NNF_LOWER_ROMAN; d->fnpr.restart = NRS_PAGE;
  d->page.width = 12240; d->page.height = 15840; d->page.left = 1800;
  /* a tracked insertion and a deletion */
  {
    int r = doc_add_rev(d, "Ada", "2026-10-24T10:30:00Z"), r2 = doc_add_rev(d, "Bob", "2026-10-25T09:00:00Z");
    f = *para_fmt_at(d->p[2], 0); f.rev = REV_INS; f.rid = (u16)r;
    c = P(2, 0);
    doc_insert(d, &c, "new ", 4, &f);
    f.rev = REV_DEL; f.rid = (u16)r2;
    doc_format(d, P(2, 4), P(2, 8), CM_REV, &f);
  }
  /* a style of its own, based on Normal, with a next style */
  {
    Style s;
    int k2;
    memset(&s, 0, sizeof s);
    strcpy(s.name, "My Body"); s.type = ST_PARA; s.based = 0; s.next = 0;
    s.pf = d->styles[0].pf; s.cf = d->styles[0].cf; s.pf.after = 300; s.cf.flags |= CF_ITALIC;
    k2 = doc_add_style_from(d, &s, 0);
    doc_apply_style(d, 6, 6, k2);
  }

  e = odt_again(d, 0, &lost, &odt, &on);
  ok("odt: everything written and read back", e != NULL);
  if (!e) { doc_free(d); free(odt); return; }
  ok("odt: the same text, paragraph, cell, section and character formatting", same_doc(d, e));
  ok("odt: styles keep their names; a style of its own with its base", !strcmp(e->styles[e->p[0]->pf.style].name, "heading 1") &&
     !strcmp(e->styles[e->p[6]->pf.style].name, "My Body") && e->styles[e->p[6]->pf.style].based == 0 && e->styles[e->p[6]->pf.style].pf.after == 300);
  ok("odt: Unicode beyond Latin-1, spaces kept", strstr(e->p[1]->t, "\xCE\xB1\xCE\xB2\xCE\xB3 \xF0\x9F\x98\x80") != NULL && strstr(e->p[1]->t, "end  two  spaces ") != NULL);
  ok("odt: underline kind, super, small caps, highlight, spacing", para_fmt_at(e->p[1], 23)->ul == UL_DOUBLE && (para_fmt_at(e->p[1], 30)->flags & CF_SUPER) &&
     (para_fmt_at(e->p[1], 35)->fx & FX_SMALLCAPS) && para_fmt_at(e->p[1], 35)->hl == (HL_ON | 0xFFFF00) && para_fmt_at(e->p[1], 35)->spacing == 40);
  ok("odt: tabs with leaders, spacing, line spacing, keep, shading", e->p[2]->pf.tabs[0] == 1440 && e->p[2]->pf.tabkind[0] == TAB_RIGHT && e->p[2]->pf.tablead[0] == LEAD_DOT &&
     e->p[2]->pf.tabkind[1] == TAB_DECIMAL && e->p[2]->pf.line == 276 && e->p[2]->pf.keepnext && e->p[2]->pf.shade == (HL_ON | 0xDDEEFF));
  ok("odt: lists: bullets, levels, numbers, the legal style's text", e->p[3]->pf.list && e->p[5]->pf.level == 1 && e->p[5]->pf.left == 1440 && !e->p[6]->pf.list &&
     !strcmp(e->lists[e->p[3]->pf.list - 1].lvl[0].text, d->lists[list - 1].lvl[0].text) && e->lists[e->p[3]->pf.list - 1].lvl[0].fmt == NF_BULLET &&
     e->p[7]->pf.list && e->p[7]->pf.list != e->p[3]->pf.list && !strcmp(e->lists[e->p[7]->pf.list - 1].lvl[1].text, d->lists[list2 - 1].lvl[1].text));
  {
    int ti = find_para(e, "wide");
    const Table *et = ti >= 0 && e->p[ti]->pf.cell.table ? &e->tables[e->p[ti]->pf.cell.table - 1] : NULL;
    int di = find_para(e, "in1");
    ok("odt: table grid, merged cells, heading row, row height, shading, alignment", et && et->ncols == 3 && et->grid[1] == 2500 && et->jc == AL_CENTER &&
       e->p[ti]->pf.cell.span == 2 && e->p[ti]->pf.cell.header && e->p[ti + 1]->pf.cell.vmerge == VM_START && e->p[ti + 4]->pf.cell.vmerge == VM_CONT &&
       e->p[ti + 2]->pf.cell.shade == 0xFFFF00 && e->p[ti + 3]->pf.cell.borders == (BD_TOP | BD_BOTTOM) && e->p[ti + 6]->pf.cell.rowh == -500 && et->marl == 108);
    ok("odt: a table in a cell", di > 0 && e->p[di]->pf.outer.table && e->p[di]->pf.cell.table && e->p[di]->pf.cell.table != e->p[di]->pf.outer.table &&
       e->p[di + 1]->pf.cell.col == 1 && e->tables[e->p[di]->pf.cell.table - 1].grid[1] == 1200);
  }
  {
    int pi = find_para(e, "Quill site");
    Para *p = e->p[pi];
    char *s = strstr(p->t, "Quill site");
    int ln = s ? para_fmt_at(p, (u32)(s - p->t))->link : 0;
    ok("odt: pictures with their size, alt text, a floating one", e->nimages >= 4 && e->images[0].cx == 1440 && e->images[0].type == d->images[0].type &&
       e->images[0].len == d->images[0].len && !memcmp(e->images[0].data, d->images[0].data, d->images[0].len) && !strcmp(e->images[0].alt, "A disc") &&
       e->images[1].wrap == WRAP_SQUARE && e->images[1].hrel == POS_PAGE && e->images[1].x == 3000 && e->images[1].flip == 1 && e->images[1].crop[0] / 100 == 100);
    ok("odt: a text box with its text, a shape with its fill", e->images[2].kind == OBJ_TEXTBOX && e->images[2].text && e->images[2].text->np &&
       !strcmp(e->images[2].text->p[0]->t, "Boxed text") && e->images[3].kind == OBJ_SHAPE && e->images[3].shape == SH_ELLIPSE && e->images[3].fill == 0x00FF00);
    ok("odt: footnote and endnote", e->nnotes == 1 && e->nenotes == 1 && e->notes[0].np == 1 && !strcmp(e->notes[0].p[0]->t, "The footnote's text.") &&
       !strcmp(e->enotes[0].p[0]->t, "An endnote."));
    ok("odt: hyperlink", ln && !strcmp(doc_link_url(e, ln), "https://example.org/quill"));
    ok("odt: bookmark", para_fmt_at(p, 0)->bmark && !strcmp(doc_bmark_name(e, para_fmt_at(p, 0)->bmark), "pic") && !para_fmt_at(p, 8)->bmark);
    ok("odt: comment over its range, author, initials, date, text", e->ncomments == 1 && !strcmp(e->comments[0].author, "Ada") && !strcmp(e->comments[0].initials, "AL") &&
       !strncmp(e->comments[0].date, "2026-10-24T10:30:00", 19) && e->comments[0].text.np == 1 && !strcmp(e->comments[0].text.p[0]->t, "A comment") && cm >= 0);
    {
      char *l1 = linear(d, eq), *l2 = NULL;
      int k2 = -1;
      u32 j;
      for (j = 0; j + 4 <= p->len; j++) if (IS_MATH(p->t + j)) { k2 = math_index(p->t + j); break; }
      l2 = k2 >= 0 ? linear(e, k2) : NULL;
      ok("odt: an equation (MathML) in line, the same as linear text", l2 && !strcmp(l1, l2) && !doc_math(e, k2)->display);
      if (l2 && strcmp(l1, l2)) printf("  %s\n  %s\n", l1, l2);
      free(l1); free(l2);
    }
    {
      int di = pi + 1;
      ok("odt: a display equation (centred as shown, left as it was)", IS_MATH(e->p[di]->t) && doc_math(e, math_index(e->p[di]->t)) && doc_math(e, math_index(e->p[di]->t))->display &&
         e->p[di]->pf.align == AL_LEFT);
    }
  }
  {
    int bi = find_para(e, "After the break"), ci = find_para(e, "Two columns");
    ok("odt: page break and sections with columns, continuous ones", bi > 0 && e->p[bi]->pf.pagebreak && e->p[bi]->pf.sect && e->p[ci]->pf.sect && doc_sect(e, (u32)ci)->cols == 2 &&
       doc_sect(e, (u32)ci)->colline && doc_sect(e, (u32)ci)->colgap == 360 && doc_sect(e, (u32)ci)->start == SB_CONT && doc_sect(e, e->np - 1)->start == SB_CONT);
  }
  {
    char *h = story_text(&e->hdr[HF_DEFAULT]), *h1 = story_text(&e->hdr[HF_FIRST]), *fe = story_text(&e->ftr[HF_EVEN]);
    ok("odt: headers with page fields, first page and even page ones, footer", h && !strcmp(h, "Header page {page} of {pages}") && e->hdr[HF_DEFAULT].p[0]->pf.align == AL_RIGHT &&
       e->titlepg && e->evenodd && h1 && !strcmp(h1, "First page header") && fe && !strcmp(fe, "Even footer") &&
       e->ftr[HF_DEFAULT].np == 1 && !strcmp(e->ftr[HF_DEFAULT].p[0]->t, "Footer"));
    free(h); free(h1); free(fe);
  }
  ok("odt: page size and margins", e->page.width == 12240 && e->page.height == 15840 && e->page.left == 1800 && e->page.top == d->page.top && e->page.header == d->page.header);
  ok("odt: footnote options", e->fnpr.fmt == NNF_LOWER_ROMAN && (e->fnpr.set & NPS_FMT) && e->fnpr.restart == NRS_PAGE);
  ok("odt: title, author and other properties", !strcmp(e->title, "Everything") && !strcmp(e->author, "Quill Tester") && !strcmp(info_str(e, IP_SUBJECT), "Testing") &&
     !strcmp(info_str(e, IP_KEYWORDS), "odt, quill") && info_cust_find(e, "Client") >= 0);
  ok("odt: tracked insertion and deletion with author and date", para_fmt_at(e->p[2], 0)->rev == REV_INS && para_fmt_at(e->p[2], 5)->rev == REV_DEL &&
     para_fmt_at(e->p[2], 9)->rev == REV_NONE && !strcmp(e->revs[para_fmt_at(e->p[2], 0)->rid].author, "Ada") &&
     !strcmp(e->revs[para_fmt_at(e->p[2], 5)->rid].author, "Bob") && !strncmp(e->revs[para_fmt_at(e->p[2], 0)->rid].date, "2026-10-24T10:30", 16));
  {
    char *ct = odt ? odt_part(odt, on, "content.xml") : NULL, *mf = odt ? odt_part(odt, on, "META-INF/manifest.xml") : NULL;
    ok("odt: the package: mimetype stored first, manifest, pictures, a formula object", odt && on > 80 && !memcmp(odt + 30, "mimetype", 8) && odt[8] == 0 &&
       !memcmp(odt + 38, "application/vnd.oasis.opendocument.text", 39) && mf && strstr(mf, "Pictures/1.png") && strstr(mf, "Object 1/") && ct && strstr(ct, "<text:tracked-changes>"));
    {
      /* and once more: what was read writes the same content */
      u8 *odt2 = NULL;
      size_t on2;
      Doc *g = odt_again(e, 0, &lost, &odt2, &on2);
      char *ct2 = odt2 ? odt_part(odt2, on2, "content.xml") : NULL, *st = odt_part(odt, on, "styles.xml"), *st2 = odt2 ? odt_part(odt2, on2, "styles.xml") : NULL;
      ok("odt: a second round trip writes the same content.xml and styles.xml", g && ct && ct2 && !strcmp(ct, ct2) && st && st2 && !strcmp(st, st2));
      if (getenv("QUILL_DUMP")) { file_write("/tmp/q1.odt", odt, on); if (odt2) file_write("/tmp/q2.odt", odt2, on2); }
      doc_free(g); free(odt2); free(ct2); free(st); free(st2);
    }
    free(ct); free(mf);
  }
  {
    /* Flat ODT: the same, in one file */
    u8 *fo = NULL, *fo2 = NULL;
    size_t fn, fn2;
    Doc *g = odt_again(d, 1, &lost, &fo, &fn), *h;
    ok("odt: Flat ODT written and read back the same", g && same_doc(d, g) && g->nimages == e->nimages && g->nnotes == 1 && g->nmaths == e->nmaths && g->ncomments == 1 &&
       fo && fmt_fodt.sniff(fo, fn) && !fmt_odt.sniff(fo, fn) && strstr((char *)fo, "<office:binary-data>"));
    h = g ? odt_again(g, 1, &lost, &fo2, &fn2) : NULL;
    ok("odt: Flat ODT twice gives the same file", h && fo && fo2 && fn == fn2 && !memcmp(fo, fo2, fn));
    if (getenv("QUILL_DUMP") && fo) file_write("/tmp/q1.fodt", fo, fn);
    doc_free(g); doc_free(h); free(fo); free(fo2);
  }
  ok("odt: what was read is sane", runs_sane(e));
  free(odt);
  doc_free(e); doc_free(d);
}

/* every picture reference made the first picture's, every equation's the first equation's (the order
   they are numbered in differs between formats) */
static void unpic(Doc *d)
{
  u32 i, k;
  char ref[8], mref[8];
  pic_utf8(ref, 0);
  math_utf8(mref, 0);
  for (i = 0; i < d->np; i++) for (k = 0; k + 4 <= d->p[i]->len; k++) {
    if (IS_PIC(d->p[i]->t + k)) memcpy(d->p[i]->t + k, ref, 4);
    else if (IS_MATH(d->p[i]->t + k)) {
      memcpy(d->p[i]->t + k, mref, 4);
      if (k >= 4 && !memcmp(d->p[i]->t + k - 4, mref, 4)) {
        /* (two equations side by side as one: LibreOffice makes a display equation's lines objects of their own) */
        memmove(d->p[i]->t + k, d->p[i]->t + k + 4, d->p[i]->len - k - 4 + 1);
        d->p[i]->len -= 4; k -= 1;
      }
    }
  }
}
/* Word's and LibreOffice's .docx test documents through ODT */
static void odt_docx_files(void)
{
  static const char *const files[] = { "tests/data/word.docx", "tests/data/lo.docx", "tests/data/tables.docx", "tests/data/headers.docx",
                                       "tests/data/notes.docx", "tests/data/pictures.docx", "tests/data/notes-lo.docx", "tests/data/equations.docx",
                                       "tests/data/charts.docx", "tests/data/wordcraft-long.docx" };
  int i;
  for (i = 0; i < (int)(sizeof files / sizeof files[0]); i++) {
    char err[200], what[120];
    unsigned lost;
    Doc *d = doc_load_file(files[i], &lost, err, sizeof err), *e;
    int k, same;
    if (!d) { ok(files[i], 0); continue; }
    e = odt_again(d, 0, &lost, NULL, NULL);
    snprintf(what, sizeof what, "odt: %s through ODT keeps text and formatting", files[i] + 11);
    if (e) { unpic(d); unpic(e); }
    /* (a last section that only has note options of its own: ODT keeps them for the document) */
    if (e && d->np && d->p[d->np - 1]->pf.sect && !e->p[e->np - 1]->pf.sect) {
      const SectFmt *s = doc_sect(d, d->np - 1);
      if (s->cols == 1 && s->start == SB_NEXT && !s->linenum) d->p[d->np - 1]->pf.sect = 0;
    }
    same = e && same_doc(d, e);
    ok(what, same && e->nnotes == d->nnotes && e->nenotes == d->nenotes && e->nimages == d->nimages && e->ntables == d->ntables &&
       e->ncomments == d->ncomments && e->nmaths == d->nmaths);
    if (e && same) {
      /* the equations come back as they were */
      int bad = 0;
      for (k = 0; k < d->nmaths && k < e->nmaths; k++) { char *a = linear(d, k), *b = linear(e, k); if (strcmp(a, b)) bad++; free(a); free(b); }
      if (d->nmaths) ok("odt: equations.docx's equations through MathML", !bad);
    }
    doc_free(e); doc_free(d);
  }
}

/* ODT that LibreOffice wrote (from tests/data's .docx files) reads as the .docx does */
static void odt_from_libreoffice(void)
{
  static const char *const names[] = { "word", "lo", "tables", "headers", "notes", "pictures", "equations" };
  int i;
  for (i = 0; i < (int)(sizeof names / sizeof names[0]) + 1; i++) {
    char a[80], b[80], err[200], what[120];
    unsigned lost;
    Doc *d, *e;
    u32 k, same = 0;
    const char *nm = i < (int)(sizeof names / sizeof names[0]) ? names[i] : "tables";
    snprintf(a, sizeof a, "tests/data/%s.docx", nm);
    if (i < (int)(sizeof names / sizeof names[0])) snprintf(b, sizeof b, "tests/data/lo-%s.odt", nm);
    else snprintf(b, sizeof b, "tests/data/lo-%s.fodt", nm);
    d = doc_load_file(a, &lost, err, sizeof err);
    e = doc_load_file(b, &lost, err, sizeof err);
    if (d && e) { unpic(d); unpic(e); }
    if (d && e) {
      /* the non-empty paragraphs' text, in order */
      u32 j = 0, nd = 0;
      for (k = 0; k < d->np; k++) {
        if (!d->p[k]->len) continue;
        nd++;
        while (j < e->np && !e->p[j]->len) j++;
        if (j < e->np && !strcmp(d->p[k]->t, e->p[j]->t)) same++;
        else printf("  %s: [%s] vs [%s]\n", b, d->p[k]->t, j < e->np ? e->p[j]->t : "");
        j++;
      }
      same = same == nd ? d->np : 0;
    }
    snprintf(what, sizeof what, "odt: LibreOffice's %s reads as %s.docx (text, tables, notes, pictures)", b + 11, nm);
    ok(what, d && e && same == d->np && e->ntables == d->ntables && e->nnotes == d->nnotes && e->nimages == d->nimages && e->nmaths == d->nmaths + (!strcmp(nm, "equations")));
    if (d && e && i == 0) {
      int bi = find_para(e, "bold");
      ok("odt: LibreOffice's styles, bold, a centred paragraph, lists", !strcmp(e->styles[e->p[1]->pf.style].name, "Heading 1") && e->p[3]->pf.align == AL_CENTER &&
         bi >= 0 && (para_fmt_at(e->p[bi], (u32)(strstr(e->p[bi]->t, "bold") - e->p[bi]->t))->flags & CF_BOLD) && e->p[6]->pf.list && e->p[8]->pf.list &&
         e->lists[e->p[8]->pf.list - 1].lvl[0].fmt == NF_DECIMAL && e->nstyles < 40 && fmt_heading_level(e, e->p[1]->pf.style) == 1);
    }
    if (d && e && i == 2) {
      int span2 = 0;
      for (k = 0; k < e->np; k++) if (e->p[k]->pf.cell.span == 2 || e->p[k]->pf.cell.vmerge) span2++;
      {
        int shade = 0, grid = d->ntables == e->ntables;
        for (k = 0; k < e->np; k++) if (e->p[k]->pf.cell.shade != COLOR_AUTO && e->p[k]->pf.cell.shade == d->p[k]->pf.cell.shade) shade++;
        for (k = 0; grid && k < (u32)d->ntables; k++) { int c; for (c = 0; c < d->tables[k].ncols; c++) if (abs(d->tables[k].grid[c] - e->tables[k].grid[c]) > 20) grid = 0; }
        ok("odt: LibreOffice's merged cells, shading, widths", span2 > 0 && shade > 0 && grid);
      }
    }
    if (d && e && i == 3) {
      char *h = story_text(&e->hdr[HF_DEFAULT]), *f = story_text(&e->ftr[HF_DEFAULT]), *h1 = story_text(&e->hdr[HF_FIRST]), *dh = story_text(&d->hdr[HF_DEFAULT]);
      ok("odt: LibreOffice's headers and footers (first page, page fields)", h && f && h1 && dh && e->titlepg && !strcmp(h, dh) && strstr(f, "{page}") &&
         !strcmp(h1, "First page header") && e->page.top == d->page.top && e->page.header == d->page.header && e->page.width == d->page.width);
      free(h); free(f); free(h1); free(dh);
    }
    if (d && e && i == 4) {
      char *n1 = story_text(&d->notes[0]), *n2 = story_text(&e->notes[0]);
      ok("odt: LibreOffice's footnotes' text", n1 && n2 && !strcmp(n1, n2));
      free(n1); free(n2);
    }
    if (d && e && i == 5) ok("odt: LibreOffice's pictures (the same bytes and sizes)", e->nimages == 3 && e->images[1].len == d->images[1].len &&
                             !memcmp(e->images[1].data, d->images[1].data, d->images[1].len) && e->images[1].cx / 20 == d->images[1].cx / 20);
    doc_free(d); doc_free(e);
  }
}

/* hand-written ODT: white space, lists, tables, sections, changes, MathML */
#define C0 "<?xml version=\"1.0\"?><office:document-content xmlns:office=\"o\" xmlns:text=\"t\" xmlns:style=\"s\" xmlns:table=\"ta\" xmlns:fo=\"f\" " \
           "xmlns:draw=\"d\" xmlns:xlink=\"x\" xmlns:dc=\"dc\" xmlns:svg=\"sv\" office:version=\"1.3\">"
static void odt_details(void)
{
  unsigned lost;
  Doc *d;
  d = odt_str(C0 "<office:body><office:text><text:p>  Two\n   words<text:s text:c=\"3\"/>and<text:tab/>tab <text:span> x </text:span> y<text:line-break/>z</text:p>"
              "<text:p><text:span>a</text:span>  <text:span> b</text:span></text:p></office:text></office:body></office:document-content>", NULL, &lost);
  ok("odt: white space collapses (leading dropped, runs of it one space, text:s kept)", d && d->np == 2 && !strcmp(d->p[0]->t, "Two words   and\ttab x y\vz") &&
     !strcmp(d->p[1]->t, "a b"));
  doc_free(d);
  d = odt_str(C0 "<office:automatic-styles><text:list-style style:name=\"N\"><text:list-level-style-number text:level=\"1\" style:num-format=\"1\" style:num-suffix=\")\"/>"
              "<text:list-level-style-number text:level=\"2\" style:num-format=\"a\" style:num-prefix=\"(\" style:num-suffix=\")\" text:display-levels=\"2\"/></text:list-style></office:automatic-styles>"
              "<office:body><office:text><text:list text:style-name=\"N\"><text:list-item><text:p>one</text:p><text:p>more</text:p><text:list><text:list-item><text:p>one a</text:p></text:list-item></text:list></text:list-item></text:list>"
              "<text:p>break</text:p><text:list text:style-name=\"N\" text:continue-numbering=\"true\"><text:list-item><text:p>two</text:p></text:list-item></text:list>"
              "<text:list text:style-name=\"N\"><text:list-item><text:p>again</text:p></text:list-item><text:list-item text:start-value=\"7\"><text:p>seven</text:p></text:list-item></text:list>"
              "</office:text></office:body></office:document-content>", NULL, &lost);
  ok("odt: lists: levels, prefixes and levels shown, a second paragraph in an item, continued, restarted, start values", d && d->np == 7 &&
     d->p[0]->pf.list && !strcmp(d->lists[d->p[0]->pf.list - 1].lvl[0].text, "%1)") && !d->p[1]->pf.list && d->p[1]->pf.left == d->p[0]->pf.left &&
     d->p[2]->pf.level == 1 && !strcmp(d->lists[d->p[2]->pf.list - 1].lvl[1].text, "(%1.%2)") && d->lists[d->p[2]->pf.list - 1].lvl[1].fmt == NF_LOWER_LETTER &&
     d->p[4]->pf.list == d->p[0]->pf.list && d->p[5]->pf.list && d->p[5]->pf.list != d->p[0]->pf.list && d->p[6]->pf.list != d->p[5]->pf.list &&
     d->lists[d->p[6]->pf.list - 1].lvl[0].start == 7);
  doc_free(d);
  d = odt_str(C0 "<office:automatic-styles><style:style style:name=\"T\" style:family=\"table\"><style:table-properties style:width=\"6in\"/></style:style>"
              "<style:style style:name=\"A\" style:family=\"table-column\"><style:table-column-properties style:rel-column-width=\"1000*\"/></style:style>"
              "<style:style style:name=\"B\" style:family=\"table-column\"><style:table-column-properties style:rel-column-width=\"2000*\"/></style:style>"
              "<style:style style:name=\"c\" style:family=\"table-cell\"><style:table-cell-properties fo:border=\"0.5pt solid #ff0000\" fo:background-color=\"#00ff00\" fo:padding=\"0.1in\"/></style:style>"
              "</office:automatic-styles><office:body><office:text><table:table table:style-name=\"T\"><table:table-column table:style-name=\"A\" table:number-columns-repeated=\"2\"/>"
              "<table:table-column table:style-name=\"B\"/><table:table-row><table:table-cell table:style-name=\"c\" table:number-columns-spanned=\"2\" table:number-rows-spanned=\"2\"><text:p>big</text:p></table:table-cell>"
              "<table:covered-table-cell/><table:table-cell/></table:table-row><table:table-row><table:covered-table-cell/><table:covered-table-cell/>"
              "<table:table-cell><text:p>x</text:p><table:table><table:table-column/><table:table-row><table:table-cell><text:p>deep</text:p>"
              "<table:table><table:table-row><table:table-cell><text:p>deeper</text:p></table:table-cell></table:table-row></table:table></table:table-cell></table:table-row></table:table>"
              "</table:table-cell></table:table-row></table:table><text:p>after</text:p></office:text></office:body></office:document-content>", NULL, &lost);
  ok("odt: tables: relative widths, spans across rows and columns, empty cells, borders, padding, a table in a cell, deeper ones flattened",
     d && d->ntables == 2 && d->tables[0].ncols == 3 && d->tables[0].grid[0] == 2160 && d->tables[0].grid[2] == 4320 && d->tables[0].marl == 144 &&
     !strcmp(d->p[0]->t, "big") && d->p[0]->pf.cell.span == 2 && d->p[0]->pf.cell.vmerge == VM_START && d->p[0]->pf.cell.shade == 0x00FF00 &&
     d->p[0]->pf.cell.borders == BD_ALL && d->p[0]->pf.cell.bcolor[0] == 0xFF0000 && d->p[1]->pf.cell.col == 2 && !d->p[1]->len &&
     d->p[2]->pf.cell.vmerge == VM_CONT && d->p[2]->pf.cell.row == 1 && d->p[2]->pf.cell.span == 2 && !strcmp(d->p[3]->t, "x") && d->p[3]->pf.cell.col == 2 &&
     !strcmp(d->p[4]->t, "deep") && d->p[4]->pf.outer.table == 1 && d->p[4]->pf.cell.table == 2 && !strcmp(d->p[5]->t, "deeper") && d->p[5]->pf.cell.table == 2 &&
     (lost & LOST_TABLES) && !strcmp(d->p[d->np - 1]->t, "after") && !d->p[d->np - 1]->pf.cell.table);
  doc_free(d);
  d = odt_str(C0 "<office:body><office:text><text:tracked-changes><text:changed-region text:id=\"c1\"><text:deletion><office:change-info><dc:creator>Cy</dc:creator>"
              "<dc:date>2026-01-02T03:04:05.123</dc:date></office:change-info><text:p>gone</text:p><text:p>also gone</text:p></text:deletion></text:changed-region>"
              "<text:changed-region text:id=\"c2\"><text:insertion><office:change-info><dc:creator>Di</dc:creator><dc:date>2026-01-02T03:04:05</dc:date></office:change-info></text:insertion></text:changed-region></text:tracked-changes>"
              "<text:p>keep <text:change text:change-id=\"c1\"/>rest <text:change-start text:change-id=\"c2\"/>new<text:change-end text:change-id=\"c2\"/>"
              "<office:annotation><dc:creator>Ed</dc:creator><text:p>note</text:p></office:annotation></text:p></office:text></office:body></office:document-content>", NULL, &lost);
  ok("odt: tracked changes: a deletion of two paragraphs splits as it shows, an insertion; a comment on a point", d && d->np == 2 && !strcmp(d->p[0]->t, "keep gone") &&
     para_fmt_at(d->p[0], 5)->rev == REV_DEL && !strcmp(d->revs[para_fmt_at(d->p[0], 5)->rid].author, "Cy") &&
     !strcmp(d->revs[para_fmt_at(d->p[0], 5)->rid].date, "2026-01-02T03:04:05Z") && !strncmp(d->p[1]->t, "also gonerest new", 17) &&
     para_fmt_at(d->p[1], 0)->rev == REV_DEL && para_fmt_at(d->p[1], 9)->rev == REV_NONE && para_fmt_at(d->p[1], 14)->rev == REV_INS &&
     d->ncomments == 1 && IS_CSTART(d->p[1]->t + 17) && IS_CEND(d->p[1]->t + 21));
  doc_free(d);
  {
    static const char *const styles = "<?xml version=\"1.0\"?><office:document-styles xmlns:office=\"o\" xmlns:text=\"t\" xmlns:style=\"s\" xmlns:fo=\"f\">"
      "<office:font-face-decls><style:font-face style:name=\"Face\" svg:font-family=\"'Liberation Serif'\" xmlns:svg=\"v\"/></office:font-face-decls>"
      "<office:styles><style:default-style style:family=\"paragraph\"><style:text-properties fo:font-size=\"10pt\" style:font-name=\"Face\"/></style:default-style>"
      "<style:style style:name=\"Standard\" style:family=\"paragraph\"><style:paragraph-properties fo:margin-bottom=\"0.1in\"/></style:style>"
      "<style:style style:name=\"Heading\" style:family=\"paragraph\" style:parent-style-name=\"Standard\"><style:text-properties fo:font-size=\"14pt\" fo:font-weight=\"bold\"/></style:style>"
      "<style:style style:name=\"Heading_20_1\" style:display-name=\"Heading 1\" style:family=\"paragraph\" style:parent-style-name=\"Heading\"><style:text-properties fo:font-size=\"130%\"/></style:style>"
      "<style:style style:name=\"Unused\" style:family=\"paragraph\"/></office:styles>"
      "<office:automatic-styles><style:page-layout style:name=\"pm1\"><style:page-layout-properties fo:page-width=\"21cm\" fo:page-height=\"29.7cm\" fo:margin-top=\"1cm\" fo:margin-bottom=\"2cm\" "
      "fo:margin-left=\"2cm\" fo:margin-right=\"2cm\"/><style:header-style><style:header-footer-properties fo:min-height=\"1cm\"/></style:header-style></style:page-layout>"
      "<style:page-layout style:name=\"pm2\"><style:page-layout-properties fo:page-width=\"29.7cm\" fo:page-height=\"21cm\" style:print-orientation=\"landscape\"/></style:page-layout></office:automatic-styles>"
      "<office:master-styles><style:master-page style:name=\"First_20_Page\" style:page-layout-name=\"pm1\" style:next-style-name=\"Standard\"><style:header><text:p>first</text:p></style:header></style:master-page>"
      "<style:master-page style:name=\"Standard\" style:page-layout-name=\"pm1\"><style:header><text:p>rest</text:p></style:header></style:master-page>"
      "<style:master-page style:name=\"Wide\" style:page-layout-name=\"pm2\"/></office:master-styles></office:document-styles>";
    d = odt_str(C0 "<office:automatic-styles><style:style style:name=\"P1\" style:family=\"paragraph\" style:parent-style-name=\"Heading_20_1\" style:master-page-name=\"First_20_Page\"/>"
                "<style:style style:name=\"P2\" style:family=\"paragraph\" style:parent-style-name=\"Standard\" style:master-page-name=\"Wide\"/>"
                "<style:style style:name=\"P3\" style:family=\"paragraph\" style:parent-style-name=\"Standard\" style:master-page-name=\"Standard\"/>"
                "<style:style style:name=\"S\" style:family=\"section\"><style:section-properties><style:columns fo:column-count=\"3\" fo:column-gap=\"0.5in\"/></style:section-properties></style:style>"
                "</office:automatic-styles><office:body><office:text><text:h text:style-name=\"P1\" text:outline-level=\"1\">Title</text:h><text:p>one</text:p>"
                "<text:section text:style-name=\"S\" text:name=\"s\"><text:p>cols</text:p></text:section><text:p text:style-name=\"P2\">wide</text:p><text:p text:style-name=\"P3\">back</text:p>"
                "</office:text></office:body></office:document-content>", styles, &lost);
    ok("odt: styles: default style, parents, relative sizes, fonts, only used ones made", d && !strcmp(d->styles[d->p[0]->pf.style].name, "Heading 1") &&
       d->styles[d->p[0]->pf.style].cf.size == 36 && (d->styles[d->p[0]->pf.style].cf.flags & CF_BOLD) && d->styles[0].pf.after == 144 &&
       !strcmp(d->fonts[d->styles[0].cf.font], "Liberation Serif") && d->styles[0].cf.size == 20 && doc_style_by_name(d, "Unused") < 0 && fmt_heading_level(d, d->p[0]->pf.style) == 1);
    ok("odt: master pages: sections by them (a first page master, a landscape one), section columns", d && d->np == 5 && d->nsects == 3 &&
       d->p[1]->pf.sect && doc_sect(d, 2)->cols == 3 && doc_sect(d, 2)->colgap == 720 && doc_sect(d, 2)->start == SB_CONT && d->p[3]->pf.sect &&
       doc_sect(d, 3)->haspage && doc_sect(d, 3)->page.width > doc_sect(d, 3)->page.height && doc_sect(d, 3)->start == SB_NEXT && !d->p[4]->pf.sect &&
       d->page.top == 1134 && d->page.header == 567 && d->page.left == 1134 && d->hdr[HF_DEFAULT].np == 1 && !strcmp(d->hdr[HF_DEFAULT].p[0]->t, "rest") &&
       doc_sect(d, 0)->titlepg && doc_sect(d, 0)->hf && !strcmp(doc_sect(d, 0)->hf[HF_FIRST].p[0]->t, "first"));
    doc_free(d);
  }
  {
    /* MathML as other programs write it */
    static const char *const m = "<math xmlns=\"http://www.w3.org/1998/Math/MathML\"><mrow><mfrac><mi>a</mi><mn>2</mn></mfrac><msqrt><mi>x</mi></msqrt>"
      "<mroot><mi>y</mi><mn>3</mn></mroot><munderover><mo>&#x2211;</mo><mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow><mi>n</mi></munderover><msub><mi>x</mi><mi>i</mi></msub>"
      "<mfenced open=\"[\" close=\"]\"><mi>u</mi><mi>v</mi></mfenced><mi>sin</mi><mo>&#x2061;</mo><mi>t</mi><mover accent=\"true\"><mi>z</mi><mo>^</mo></mover>"
      "<mtable><mtr><mtd><mn>1</mn></mtd><mtd><mn>0</mn></mtd></mtr><mtr><mtd><mn>0</mn></mtd><mtd><mn>1</mn></mtd></mtr></mtable><mtext>ok</mtext></mrow></math>";
#define MLIN "a/2 \xE2\x88\x9A(x)\xE2\x88\x9A(3&y)\xE2\x88\x91_(i=1)^n\xE2\x96\x92x_i[u,v]sin\xE2\x81\xA1tz\xCC\x82\xE2\x96\xA0(1&0@0&1)\"ok\""
    MathEq *e = mathml_parse(m, strlen(m));
    Buf b;
    buf_init(&b);
    if (e) math_to_linear(e, &b);
    ok("odt: MathML read into Quill's equations (fractions, roots, n-ary, scripts, fences, functions, accents, matrices, text)",
       e && e->display && b.data && !strcmp(b.data, MLIN));
    if (b.data && e && strcmp(b.data, MLIN)) printf("  %s\n", b.data);
    buf_free(&b);
    math_free(e);
  }
  ok("odt: garbage and truncated files don't crash", (d = odt_str(C0 "<office:body><office:text><text:p>x<text:span><text:a", NULL, &lost)) != NULL && d->p[0]->t[0] == 'x');
  doc_free(d);
  {
    static const u8 docx_like[] = "PK\3\4";
#define FL "<?xml version=\"1.0\"?>\n<office:document office:mimetype=\"application/vnd.oasis.opendocument.text\">"
    ok("odt: known by the package's mimetype and the name; Flat ODT by its root", !fmt_odt.sniff(docx_like, 4) && doc_format_for_name("A.ODT", 1) == &fmt_odt &&
       doc_format_for_name("b.fodt", 1) == &fmt_fodt && fmt_fodt.sniff((const u8 *)FL, strlen(FL)) && !fmt_fodt.sniff((const u8 *)"<html>", 6));
  }
}

void test_wp28b(void)
{
  odt_everything();
  odt_docx_files();
  odt_from_libreoffice();
  odt_details();
}
