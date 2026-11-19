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
#include "toc.h"
#include "refs.h"
#include "review.h"

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

int ec_find(Doc *d, ESel *s, const char *what, unsigned flags)
{
  Pos a, b;
  if (!what || !*what || !doc_find(d, (flags & FIND_BACK) ? es_lo(s) : es_hi(s), what, flags, &a, &b)) return 0;
  s->anchor = a;
  s->caret = b;
  return 1;
}

/* the selection is exactly a match (as doc_find would find it from its start) */
static int sel_matches(Doc *d, const ESel *s, const char *what, unsigned flags)
{
  Pos a, b, lo = es_lo(s);
  return pos_cmp(s->anchor, s->caret) && doc_find(d, lo, what, flags & ~FIND_BACK, &a, &b) && !pos_cmp(a, lo) && !pos_cmp(b, es_hi(s));
}

int ec_replace(Doc *d, ESel *s, const char *what, const char *with, unsigned flags)
{
  int did = 0;
  if (!what || !*what) return 0;
  if (sel_matches(d, s, what, flags)) did = ec_text(d, s, with ? with : "");
  return ec_find(d, s, what, flags & ~FIND_BACK) || did;
}

u32 ec_replace_all(Doc *d, ESel *s, const char *what, const char *with, unsigned flags)
{
  u32 n;
  if (!what || !*what) return 0;
  n = doc_replace_all(d, what, with ? with : "", flags & ~FIND_BACK);
  s->caret = s->anchor = pos_clamp(d, s->caret);
  return n;
}

int ec_toc(Doc *d, struct Layout *L, ESel *s)
{
  Pos at = es_lo(s);
  int again = 0, ok;
  lay_update(L);
  ok = doc_toc(d, L, &at, &again);
  s->caret = s->anchor = pos_clamp(d, s->caret);
  return ok;
}

int ec_next_note(Doc *d, ESel *s)
{
  u32 pi = s->caret.para, n, k, off;
  const Para *p = d->p[pi];
  /* (from after a reference the caret is at) */
  off = s->caret.off + (s->caret.off + 4 <= p->len && (IS_NOTE(p->t + s->caret.off) || IS_ENOTE(p->t + s->caret.off)) ? 4 : 0);
  for (n = 0; n <= d->np; n++, pi = (pi + 1) % d->np, off = 0) {
    p = d->p[pi];
    for (k = off; k + 4 <= p->len; k++)
      if (IS_NOTE(p->t + k) || IS_ENOTE(p->t + k)) {
        s->caret.para = s->anchor.para = pi;
        s->caret.off = s->anchor.off = k;
        return 1;
      }
  }
  return 0;
}

int ec_caption(Doc *d, ESel *s, int label, const char *text)
{
  FieldEnv e;
  Pos at = es_lo(s);
  RefsInfo *ri = refs_info(d);
  if (!ri || label < 0 || label >= ri->nlab) return 0;
  field_env(&e);
  if (!refs_insert_caption(d, &at, label, text ? text : "", -1, &e)) return 0;
  s->caret = s->anchor = pos_clamp(d, at);
  return 1;
}

/* is c part of a word (letters, digits, anything not ASCII) */
static int wordch(unsigned char c) { return c >= 0x80 || (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z'); }

int ec_bookmark(Doc *d, ESel *s, const char *name)
{
  Pos a = es_lo(s), b = es_hi(s);
  const char *c;
  if (!name || !*name) return 0;
  for (c = name; *c; c++) if (!wordch((unsigned char)*c) && *c != '_') return 0;      /* (Word's names: a word, no spaces) */
  if (!pos_cmp(a, b)) {
    const Para *p = d->p[a.para];
    while (a.off > 0 && wordch((unsigned char)p->t[a.off - 1])) a.off--;
    while (b.off < p->len && wordch((unsigned char)p->t[b.off])) b.off++;
    if (!pos_cmp(a, b)) return 0;
  }
  return doc_set_bmark(d, a, b, name);
}

int ec_xref(Doc *d, ESel *s, const char *bookmark)
{
  char code[120];
  Pos a, b;
  if (!bookmark || !*bookmark || strlen(bookmark) > 80 || !doc_bmark_find(d, bookmark, &a, &b)) return 0;
  snprintf(code, sizeof code, "REF %s \\h", bookmark);
  return ec_field(d, s, code);
}

/* the word at p (letters, digits, anything not ASCII), as a..b; 0 if p is not in one */
static int word_at(Doc *d, Pos p, Pos *a, Pos *b)
{
  const Para *q = d->p[p.para];
  *a = *b = p;
  while (a->off > 0 && wordch((unsigned char)q->t[a->off - 1])) a->off--;
  while (b->off < q->len && wordch((unsigned char)q->t[b->off])) b->off++;
  return pos_cmp(*a, *b) != 0;
}

void ec_now(char *out)
{
  FieldTime t;
  field_time_now(&t);
  field_time_w3c(&t, out);
}

int ec_comment(Doc *d, ESel *s, const char *text, const char *author, const char *date)
{
  Pos a = es_lo(s), b = es_hi(s);
  char ini[4] = { 0, 0, 0, 0 };
  int k;
  if (!pos_cmp(a, b)) word_at(d, a, &a, &b);
  if (author && author[0]) ini[0] = author[0];
  if ((k = doc_comment(d, a, b, text ? text : "", author ? author : "", ini, date ? date : "")) < 0) return -1;
  s->caret = s->anchor = pos_clamp(d, b);
  return k;
}

int ec_comment_next(Doc *d, ESel *s, int dir)
{
  /* (the Amiga's CMD_NEXTCOMMENT) a comment's start mark after (or before) the caret */
  u32 pi = s->caret.para, n;
  Pos at = s->caret, e;
  int found = 0, k;
  for (n = 0; n <= d->np && !found; n++) {
    const Para *q = d->p[pi];
    if (dir > 0) {
      u32 k0 = pi == s->caret.para && n == 0 ? s->caret.off : 0, j;
      for (j = k0; j + 4 <= q->len; j++)
        if (IS_CSTART(q->t + j) && !(pi == s->caret.para && n == 0 && j + 4 == s->caret.off)) { at.para = pi; at.off = j; found = 1; break; }
      pi = (pi + 1) % d->np;
    } else {
      u32 j = pi == s->caret.para && n == 0 ? s->caret.off : q->len;
      while (j >= 4) {
        j = (u32)utf8_prev(q->t, j);
        if (IS_CSTART(q->t + j) && !(pi == s->caret.para && n == 0 && j + 4 >= s->caret.off)) { at.para = pi; at.off = j; found = 1; break; }
      }
      pi = pi ? pi - 1 : d->np - 1;
    }
  }
  if (!found) return -1;
  k = note_index(d->p[at.para]->t + at.off);
  if (!doc_comment_mark(d, k, 1, &e)) e = at;
  at.off += 4;
  s->anchor = at;
  s->caret = pos_cmp(e, at) > 0 ? e : at;
  return k;
}

int ec_comment_delete(Doc *d, ESel *s)
{
  int k = doc_comment_at(d, es_lo(s), NULL, NULL);
  if (k < 0 && pos_cmp(s->anchor, s->caret)) {
    /* (a selection that starts at the comment's mark) */
    Pos q = es_lo(s);
    q.off += 4 <= d->p[q.para]->len - q.off ? 4 : 0;
    k = doc_comment_at(d, q, NULL, NULL);
  }
  if (k < 0 || !doc_delete_comment(d, k)) return 0;
  s->caret = s->anchor = pos_clamp(d, es_lo(s));
  return 1;
}

int ec_change_next(Doc *d, ESel *s, int dir)
{
  Pos a, b;
  if (!doc_next_change(d, dir > 0 ? es_hi(s) : es_lo(s), dir, &a, &b)) return 0;
  s->anchor = a;
  s->caret = b;
  return 1;
}

int ec_resolve(Doc *d, ESel *s, int accept, int all)
{
  /* (the Amiga's CMD_RESOLVE) */
  Pos a, b;
  if (all) { a = doc_start(d); b = doc_end_pos(d); }
  else if (pos_cmp(s->anchor, s->caret)) { a = es_lo(s); b = es_hi(s); }
  else {
    const Para *q = d->p[s->caret.para];
    u32 r, rs = 0;
    a = b = s->caret;
    for (r = 0; r < q->nr; r++) {
      if (rv_is_change(&q->r[r].f) && rs <= s->caret.off && q->r[r].end >= s->caret.off && q->r[r].end > rs) { a.off = rs; b.off = q->r[r].end; break; }
      rs = q->r[r].end;
    }
    if (r == q->nr && !doc_next_change(d, s->caret, 1, &a, &b)) return 0;
  }
  if (!doc_resolve(d, a, b, accept)) return 0;
  s->caret = s->anchor = pos_clamp(d, a);
  if (!all && doc_next_change(d, s->caret, 1, &a, &b)) { s->anchor = a; s->caret = b; }
  return 1;
}

int ec_delete(Doc *d, Pos a, Pos b, int rid)
{
  return rid >= 0 ? doc_track_delete(d, a, b, rid) : doc_delete(d, a, b);
}

void ec_track_fmt(CharFmt *f, int rid)
{
  f->rev = REV_INS;
  f->rid = (u16)rid;
  f->frev = 0;
  f->move = 0;           /* (new text is neither a formatting change nor a move) */
}
