/*
 * edcmd.c - editing commands on a document and its selection: see edcmd.h. Each follows what the
 * Amiga's view does for the same command (amiga/view.c: CMD_TABLE, CMD_ROW_ABOVE..., CMD_PAGEBREAK,
 * CMD_SECTBREAK, CMD_PICTURE; amiga/main.c: footnotes and Insert > Date).
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "edcmd.h"
#include "table.h"
#include "field.h"

#define MIN_TEXT_W 720            /* text never narrower than half an inch (as the Amiga's view) */

Pos es_lo(const ESel *s) { return pos_cmp(s->anchor, s->caret) <= 0 ? s->anchor : s->caret; }
Pos es_hi(const ESel *s) { return pos_cmp(s->anchor, s->caret) <= 0 ? s->caret : s->anchor; }

static const PageSetup *para_page(Doc *d, u32 pi) { return sect_page(d, doc_sect(d, pi), doc_last_sect(d, pi)); }

i32 ec_text_width(Doc *d, u32 pi)
{
  const Para *p = d->p[pi];
  Table *t = doc_table(d, &p->pf);
  const PageSetup *ps = para_page(d, pi);
  i32 w = ps->width - ps->left - ps->right;
  if (t) {
    int c;
    w = -t->marl - t->marr;
    for (c = p->pf.cell.col; c < p->pf.cell.col + p->pf.cell.span && c < t->ncols; c++) w += t->grid[c];
  }
  w -= p->pf.left + p->pf.right;
  return w < MIN_TEXT_W ? MIN_TEXT_W : w;
}

/* the room a new table has at paragraph pi: an outer table's cell, or the page between the margins */
static i32 table_room(Doc *d, u32 pi)
{
  const ParaFmt *pf = &d->p[pi]->pf;
  const PageSetup *ps;
  if (in_table(pf)) return ec_text_width(d, pi);
  ps = para_page(d, pi);
  return ps->width - ps->left - ps->right;
}

/* the selection's text gone (inside an undo group the caller opened), the caret where it began */
static void cut_sel(Doc *d, ESel *s)
{
  Pos a = es_lo(s), b = es_hi(s);
  if (pos_cmp(a, b)) doc_delete(d, a, b);
  s->caret = s->anchor = a;
}

int ec_text(Doc *d, ESel *s, const char *text)
{
  int ok;
  doc_begin(d, s->caret);
  cut_sel(d, s);
  ok = doc_insert(d, &s->caret, text, (u32)strlen(text), NULL);
  doc_end(d, s->caret);
  s->anchor = s->caret;
  return ok;
}

int ec_table(Doc *d, ESel *s, int rows, int cols)
{
  int ok;
  if (rows < 1 || cols < 1 || cols > MAX_COLS) return 0;
  doc_begin(d, s->caret);
  cut_sel(d, s);
  ok = tbl_insert(d, &s->caret, rows, cols, table_room(d, s->caret.para));
  doc_end(d, s->caret);
  s->anchor = s->caret;
  return ok;
}

int ec_table_op(Doc *d, ESel *s, int op)
{
  Pos c = es_lo(s);
  int ok;
  if (!in_table(&d->p[c.para]->pf)) return 0;
  switch (op) {
  case ET_ROW_ABOVE: ok = tbl_row_insert(d, &c, 0); break;
  case ET_ROW_BELOW: ok = tbl_row_insert(d, &c, 1); break;
  case ET_COL_LEFT: ok = tbl_col_insert(d, &c, 0); break;
  case ET_COL_RIGHT: ok = tbl_col_insert(d, &c, 1); break;
  case ET_DEL_ROW: ok = tbl_row_delete(d, &c); break;
  case ET_DEL_COL: ok = tbl_col_delete(d, &c); break;
  case ET_DEL_TABLE: ok = tbl_delete(d, &c); break;
  case ET_MERGE:
    if (!(ok = tbl_merge(d, es_lo(s), es_hi(s)))) return 0;
    c = pos_clamp(d, es_lo(s));
    break;
  case ET_SPLIT: c = s->caret; ok = tbl_split(d, &c); break;
  default: return 0;
  }
  if (ok) s->caret = s->anchor = pos_clamp(d, c);
  return ok;
}

int ec_break(Doc *d, ESel *s, int column)
{
  ParaFmt pf;
  int ok;
  doc_begin(d, s->caret);
  cut_sel(d, s);
  ok = doc_insert(d, &s->caret, "\n", 1, NULL);
  memset(&pf, 0, sizeof pf);
  if (column) pf.colbreak = 1; else pf.pagebreak = 1;
  if (ok) ok = doc_para_format(d, s->caret.para, s->caret.para, PM_BREAK, &pf);
  doc_end(d, s->caret);
  s->anchor = s->caret;
  return ok;
}

int ec_section(Doc *d, ESel *s, int start)
{
  SectFmt sf, next;
  ParaFmt pf;
  u32 e;
  if (in_table(&d->p[s->caret.para]->pf)) return 0;
  doc_begin(d, s->caret);
  cut_sel(d, s);
  sf = *doc_sect(d, s->caret.para);
  sf.titlepg = (u8)sect_titlepg(d, &sf, doc_last_sect(d, s->caret.para));     /* (Word copies the section's settings) */
  if (!doc_insert(d, &s->caret, "\n", 1, NULL)) { doc_end(d, s->caret); return 0; }
  memset(&pf, 0, sizeof pf);
  if ((pf.sect = (u16)doc_add_sect(d, &sf))) doc_para_format(d, s->caret.para - 1, s->caret.para - 1, PM_SECT, &pf);
  e = doc_sect_end(d, s->caret.para);
  next = sf;
  next.start = (u8)start;
  if (!in_table(&d->p[e]->pf) && (pf.sect = (u16)doc_add_sect(d, &next))) doc_para_format(d, e, e, PM_SECT, &pf);
  doc_end(d, s->caret);
  s->anchor = s->caret;
  return 1;
}

int ec_picture(Doc *d, ESel *s, u8 *data, u32 len)
{
  char ref[8];
  Image *im;
  i32 w;
  int k = doc_add_image(d, data, len, 0, 0);       /* (takes the data) */
  if (k < 0) return -1;
  im = &d->images[k];
  w = ec_text_width(d, es_lo(s).para);
  if (im->cx > w) { im->cy = (i32)((long)im->cy * w / im->cx); im->cx = w; }    /* not wider than the column */
  pic_utf8(ref, k);
  return ec_text(d, s, ref) ? k : -1;
}

int ec_note(Doc *d, ESel *s, int end, const char *text)
{
  char ref[8];
  int k = end ? doc_add_endnote(d) : doc_add_note(d), st;
  Story *note;
  if (k < 0) return -1;
  note = end ? &d->enotes[k] : &d->notes[k];
  st = doc_style(d, end ? "EndnoteText" : "FootnoteText");
  if (!story_set_text(d, note, text && *text ? text : " ", st < 0 ? 0 : st, AL_LEFT)) return -1;
  if (end) enote_utf8(ref, k); else note_utf8(ref, k);
  return ec_text(d, s, ref) ? k : -1;
}

int ec_field(Doc *d, ESel *s, const char *code)
{
  FieldEnv e;
  int k;
  field_env(&e);
  doc_begin(d, s->caret);
  cut_sel(d, s);
  k = field_insert(d, &s->caret, code, &e, NULL);
  doc_end(d, s->caret);
  s->anchor = s->caret;
  return k >= 0;
}

int ec_page_setup(Doc *d, i32 width, i32 height, i32 top, i32 bottom, i32 left, i32 right)
{
  PageSetup *pg = &d->page;
  int k;
  if (width < 1440 || height < 1440 || left + right > width - 720 || top + bottom > height - 720 || left < 0 || right < 0 || top < 0 || bottom < 0)
    return 0;
  if (pg->width == width && pg->height == height && pg->top == top && pg->bottom == bottom && pg->left == left && pg->right == right)
    return 0;
  /* sections that had their own page setup the same as the document's go on following it */
  for (k = 0; k < d->nsects; k++)
    if (d->sects[k].haspage && !memcmp(&d->sects[k].page, pg, sizeof *pg)) d->sects[k].haspage = 0;
  pg->width = width; pg->height = height; pg->top = top; pg->bottom = bottom; pg->left = left; pg->right = right;
  d->dirty = 1;
  d->ver++;
  return 1;
}

/* sections pa..pb (the paragraphs ending them) given new formats, set by how and v: one undo step
   (the Amiga's CMD_COLUMNS and CMD_LINENUM); how: 0 columns, 1 line numbers */
static int sections(Doc *d, ESel *s, u32 pa, u32 pb, int how, int v)
{
  u32 i;
  int done = 0;
  if (in_table(&d->p[doc_sect_end(d, d->np - 1)]->pf) && pb == d->np - 1) return 0;
  doc_begin(d, s->caret);
  for (i = pa; i <= pb; i++) {
    SectFmt sf;
    ParaFmt pf;
    if ((!d->p[i]->pf.sect && i != d->np - 1) || in_table(&d->p[i]->pf)) continue;
    sf = *doc_sect(d, i);
    if (how == 0) sf.cols = (u8)v; else sf.linenum = (u8)v;
    memset(&pf, 0, sizeof pf);
    if (!(pf.sect = (u16)doc_add_sect(d, &sf))) break;
    done |= doc_para_format(d, i, i, PM_SECT, &pf);
  }
  doc_end(d, s->caret);
  return done;
}

int ec_columns(Doc *d, ESel *s, int cols)
{
  u32 e = doc_sect_end(d, s->caret.para);
  if (cols < 1 || cols > 3) return 0;
  return sections(d, s, e, e, 0, cols);
}

int ec_line_numbers(Doc *d, ESel *s, int ln)
{
  if (ln < LN_NONE || ln > LN_CONT) return 0;
  return sections(d, s, 0, d->np - 1, 1, ln);
}

int ec_para(Doc *d, ESel *s, unsigned mask, const ParaFmt *pf)
{
  return doc_para_format(d, es_lo(s).para, es_hi(s).para, mask, pf);
}

/* twips in a hundredth of each unit: an inch is 1440 twips, a centimetre 567, a point 20 */
static const i32 per_unit[3] = { 1440, 567, 20 };

int ec_measure(const char *s, int unit, i32 *tw)
{
  long whole = 0, hund = 0;
  int neg = 0, digits = 0, dec = 0;
  while (*s == ' ') s++;
  if (*s == '-') { neg = 1; s++; }
  while (*s >= '0' && *s <= '9' && whole < 100000) { whole = whole * 10 + (*s++ - '0'); digits++; }
  if (*s == '.' || *s == ',') {
    s++;
    while (*s >= '0' && *s <= '9') {
      if (dec < 2) { hund = hund * 10 + (*s - '0'); dec++; }
      s++; digits++;
    }
  }
  if (dec == 1) hund *= 10;
  while (*s == ' ') s++;
  if (!digits || *s || unit < EU_INCH || unit > EU_PT) return 0;
  *tw = (i32)(((whole * 100 + hund) * per_unit[unit] + 50) / 100);
  if (neg) *tw = -*tw;
  return 1;
}

void ec_measure_text(i32 tw, int unit, char *out, int cap)
{
  long a = tw < 0 ? -(long)tw : tw, h = (a * 100 + per_unit[unit] / 2) / per_unit[unit];
  char t[24];
  int n;
  n = snprintf(t, sizeof t, "%s%ld.%02ld", tw < 0 && h ? "-" : "", h / 100, h % 100);
  while (n > 0 && t[n - 1] == '0') t[--n] = 0;      /* 1.50 1.5; 2.00 2 */
  if (n > 0 && t[n - 1] == '.') t[--n] = 0;
  snprintf(out, (size_t)cap, "%s", t);
}
