/*
 * rtf.c - RTF files (docs/wordcraft-gap.md WP28a). This file belongs to WP28a: only that package
 * edits it.
 *
 * Reading (Microsoft's RTF 1.9.1 specification): text in any single-byte code page (\ansicpg, each
 * font's \fcharset; double-byte ones only through \u), \uN with \ucN fallbacks, \bin; the font,
 * colour and style sheets (paragraph and character styles, \sbasedon, \snext); character formatting
 * (bold, italic, the underlines, strike, double strike, super/subscript, caps, small caps, font,
 * size, colour, highlight, spacing, scale, raised/lowered, a box); paragraph formatting (alignment,
 * indents, spacing, line spacing, keep with next, keep lines together, widow control, page break
 * before, tabs with leaders, borders, shading); lists (\listtable, \listoverridetable, \ls, \ilvl,
 * and Word 6's \pn); tables (\trowd ... \cell ... \row with \cellx, horizontal merges by width or by
 * \clmgf/\clmrg, vertical merges, borders, shading, row heights, heading rows; nested tables are
 * flattened into their cell); pictures (PNG and JPEG; metafiles and bitmaps become a placeholder box
 * of their size); \shppict and shapes' pictures (\shp pib); headers and footers (\header, \headerl,
 * \headerr, \headerf, and footers); footnotes and endnotes; fields (HYPERLINK becomes a link, PAGE,
 * NUMPAGES and PAGEREF Quill's own fields, others keep their result); bookmarks; tracked changes
 * (\revised, \deleted, \revtbl); sections with page setup, columns, line numbers, vertical alignment;
 * \info's title and author.
 *
 * Writing: the same, as Word would write it (ANSI code page 1252, every other character as \uN with a
 * code page 1252 or '?' fallback). Objects other than pictures (shapes, text boxes) and comments are
 * left out; floating pictures are written in line; nested tables are written flattened.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "docx.h"
#include "fmtio.h"
#include "codepage.h"
#include "image.h"

/* ================================================================== */
/* reading */

enum { D_BODY, D_SKIP, D_FONTTBL, D_COLORTBL, D_STYLESHEET, D_INFO, D_TITLE, D_AUTHOR, D_PICT, D_FLDINST,
       D_LISTTABLE, D_LIST, D_LEVEL, D_LEVELTEXT, D_LSOVERRIDE, D_BKSTART, D_BKEND, D_REVTBL, D_PN, D_PNTEXT,
       D_SHPINST, D_SV, D_SN, D_LISTNAME };

typedef struct {
  CharFmt cf;
  ParaFmt pf;
  unsigned cmask, pmask;    /* what has been set in this group's style sheet entry */
  u8 dest;
  u8 uc;                    /* characters a \u's fallback takes */
  u8 hidden;                /* \v */
  u8 rsltskip;              /* a field result Quill shows by itself (page numbers): its text goes */
  u8 intbl;                 /* \intbl, and \itap's level */
  u8 itap;
  u8 owner;                 /* this group opened its destination (it ends when the group does) */
  u8 uprskip;               /* a \upr's first part (skipped), D_ to go back to on \ud */
  u8 sty_entry;             /* a style sheet entry's group */
  u8 fldrslt;               /* a field's result */
  u8 field;                 /* a field's group (its index + 1 on the field stack) */
  u8 shpinst;
  int fcp;                  /* the code page of the current font (0: the document's) */
  u8 symbol;                /* the current font is a symbol font */
  u8 rev_ins, rev_del;      /* \revised, \deleted */
  int ra_ins, ra_del;       /* their \revauth, \revauthdel */
  u32 rdt_ins, rdt_del;     /* and dates (\revdttm, \revdttmdel) */
} GState;

#define MAXG 120
#define MAXSTO 6
#define RTF_MAXCELLS MAX_COLS

typedef struct {
  u8 vmerge, hmerge;        /* VM_; 1 \clmgf, 2 \clmrg */
  u8 valign, borders;
  u8 bstyle[4], bwidth[4];
  u32 bcolor[4];
  u32 shade;
  i32 x;                    /* \cellx: its right edge */
} CellDef;
typedef struct {
  int ncell;
  CellDef c[RTF_MAXCELLS];
  i32 left, gap, padl, padr;
  u8 padset;
  i32 rowh;
  u8 header, cantsplit, jc, autofit;
} RowDef;

/* a story being read: the body, a header or footer, a note */
typedef struct {
  int kind;                 /* 0 body, 1 header/footer, 2 note */
  int which;                /* header/footer: k (0..2*HF_N-1) */
  int depth;                /* the group it ends with */
  int endnote;
  ParaList l;
  Para *cur;                /* the paragraph being filled */
  int chftn;                /* a note: its number mark was seen (its first space then goes) */
  /* a table being read: its rows so far */
  u32 tfirst;               /* the table's first paragraph */
  int nrows;
  RowDef *rows; int rcap;
  int cell;                 /* cells ended in this row */
  u8 pagebreak, colbreak;   /* for the next paragraph */
} Sto;

typedef struct { int num, idx, cp; u8 symbol; } RFont;
typedef struct {
  int num; u8 type; char name[48];
  int based, next; u8 quick;
  ParaFmt pf; CharFmt cf; unsigned pmask, cmask;
} RStyle;
typedef struct { int id; ListDef def; } RList;

typedef struct {
  Doc *d;
  const u8 *s; size_t n, i;
  GState *g; int depth;
  unsigned *lost;
  int ansicpg, deff;
  RFont *fonts; int nfonts, fcap;
  int fnum, fcharset; u8 ffamily_tech;
  u32 colors[256]; int ncolors; int cr, cg, cb, cset;
  RStyle *styles; int nstyles, scap;
  int *stymap; int nstymap;          /* RTF style number -> Doc style index (-1) */
  RList *rlists; int nrlists, rlcap;
  RList curlist; int curlevel; u32 lt[40]; int nlt; u8 ltfont;
  int lsmap[1024];                   /* \lsN -> Doc list number */
  int lsid;                          /* the override being read: its \listid, \ls */
  int lsnum;
  int pnlist[2][9];                  /* Word 6 lists made so far: bulleted, numbered */
  int pnkind, pnlvl, pnstart, pnfmt;
  Sto sto[MAXSTO]; int nsto;
  RowDef row; int rowcell;           /* the row definition being read, and which cell's props */
  int bt;                            /* what \brdr... words describe: BT_ */
  /* section */
  SectFmt sec; PageSetup secpage; u8 secpage_set;
  Story sechf[2 * HF_N];
  /* picture */
  Buf pict; int ptype; i32 picw, pich, goalw, goalh, scx, scy; int hexhi;
  /* fields */
  struct { Buf inst; int kind; } fld[8]; int nfld;
  Buf tb;                            /* text of a destination (font and style names, title...) */
  u8 tbdest;
  int skip;                          /* fallback characters still to skip after \u */
  u32 hisur;                         /* a high surrogate waiting for its low one */
  int lead;                          /* a double-byte character's lead byte waiting */
  u16 bmark;                         /* the bookmark open */
  char bkname[80];
  /* pending text of the current story */
  Buf pend; CharFmt pendcf; int pendok;
  /* tracked changes: the revision table, and the last author and date made a Doc revision */
  char (*revauth)[64]; int nrev, revcap;
  int rc_auth; u32 rc_dttm; int rc_rid;
  /* shapes */
  char sn[32];
  int oom;
  u8 nested_lost;
  u8 facingp, titlepg_doc;
} R;

enum { BT_NONE, BT_PARA = 1, BT_CELL = 10, BT_PAGE = 20, BT_CHAR = 21, BT_BOX = 22 };

/* ---- pending text */
static Sto *top(R *r) { return &r->sto[r->nsto - 1]; }
static Para *cur_para(R *r)
{
  Sto *s = top(r);
  if (!s->cur) {
    GState *g = &r->g[r->depth];
    if (!(s->cur = para_new(&g->pf, &g->cf))) r->oom = 1;
  }
  return s->cur;
}
static void flush(R *r)
{
  Para *p;
  if (!r->pend.len) return;
  if ((p = cur_para(r)) && !para_append(p, r->pend.data, (u32)r->pend.len, &r->pendcf)) r->oom = 1;
  r->pend.len = 0; r->pend.data[0] = 0;
}
/* the character format text gets now */
/* the revision table's author k (the \revauth number) and a \revdttm date as a Doc revision */
static int rev_of(R *r, int k, u32 dttm)
{
  char date[24];
  if (r->rc_rid >= 0 && r->rc_auth == k && r->rc_dttm == dttm) return r->rc_rid;
  date[0] = 0;
  if (dttm) {
    int mi = (int)(dttm & 63), h = (int)((dttm >> 6) & 31), dd = (int)((dttm >> 11) & 31), mo = (int)((dttm >> 16) & 15), y = (int)((dttm >> 20) & 511) + 1900;
    sprintf(date, "%04d-%02d-%02dT%02d:%02d:00Z", y, mo, dd, h, mi);
  }
  r->rc_auth = k; r->rc_dttm = dttm;
  r->rc_rid = doc_add_rev(r->d, k >= 0 && k < r->nrev ? r->revauth[k] : "Unknown", date);
  if (r->rc_rid < 0) r->rc_rid = 0;
  return r->rc_rid;
}
static void text_cf_at(R *r, CharFmt *cf, int depth)
{
  GState *g = &r->g[depth];
  *cf = g->cf;
  cf->bmark = r->bmark;
  if (g->rev_del) { cf->rev = REV_DEL; cf->rid = (u16)rev_of(r, g->ra_del, g->rdt_del); }
  else if (g->rev_ins) { cf->rev = REV_INS; cf->rid = (u16)rev_of(r, g->ra_ins, g->rdt_ins); }
}
static void text_cf(R *r, CharFmt *cf) { text_cf_at(r, cf, r->depth); }
static void body_utf8_at(R *r, const char *u, int n, int depth)
{
  CharFmt cf;
  text_cf_at(r, &cf, depth);
  if (r->pend.len && memcmp(&cf, &r->pendcf, sizeof cf)) flush(r);
  if (!r->pend.len) r->pendcf = cf;
  buf_add(&r->pend, u, (size_t)n);
}
static void body_utf8(R *r, const char *u, int n) { body_utf8_at(r, u, n, r->depth); }

/* one character of text, wherever the destination sends it */
static void put_cp(R *r, u32 cp)
{
  GState *g = &r->g[r->depth];
  char u[8];
  int n;
  if (r->skip > 0) { r->skip--; return; }
  if (cp >= 0xD800 && cp <= 0xDBFF) { r->hisur = cp; return; }
  if (cp >= 0xDC00 && cp <= 0xDFFF) {
    if (!r->hisur) return;
    cp = 0x10000 + ((r->hisur - 0xD800) << 10) + (cp - 0xDC00);
  }
  r->hisur = 0;
  switch (g->dest) {
  case D_BODY:
    if (g->hidden || g->rsltskip || cp == 0) return;
    if (cp < 32 && cp != '\t' && cp != '\v') return;
    if (g->symbol && cp >= 0x20 && cp < 0x100) {
      /* Symbol and Wingdings characters as the Unicode ones they look like (Greek and bullets) */
      if (cp == 0xB7) cp = 0x2022;
      else if (cp >= 0x41 && cp <= 0x5A) { static const u16 G[26] = { 0x391,0x392,0x3A7,0x394,0x395,0x3A6,0x393,0x397,0x399,0x3D1,0x39A,0x39B,0x39C,0x39D,0x39F,0x3A0,0x398,0x3A1,0x3A3,0x3A4,0x3A5,0x3C2,0x3A9,0x39E,0x3A8,0x396 }; cp = G[cp - 0x41]; }
      else if (cp >= 0x61 && cp <= 0x7A) { static const u16 G[26] = { 0x3B1,0x3B2,0x3C7,0x3B4,0x3B5,0x3C6,0x3B3,0x3B7,0x3B9,0x3D5,0x3BA,0x3BB,0x3BC,0x3BD,0x3BF,0x3C0,0x3B8,0x3C1,0x3C3,0x3C4,0x3C5,0x3D6,0x3C9,0x3BE,0x3C8,0x3B6 }; cp = G[cp - 0x61]; }
      else if (cp == 0xA7) cp = 0x25AA;
      else if (cp == 0xD8) cp = 0x27A2;
      else if (cp == 0xFC) cp = 0x2713;
    } else if (cp >= 0xF020 && cp <= 0xF0FF) {
      switch (cp & 0xFF) { case 0xB7: case 0x9F: case 0x6C: cp = 0x2022; break; case 0xA7: case 0x6E: cp = 0x25AA; break; case 0xD8: case 0xE0: cp = 0x25BA; break; case 0xFC: cp = 0x2713; break; default: break; }
    }
    n = utf8_put(u, cp);
    body_utf8(r, u, n);
    return;
  case D_FONTTBL:
    if (cp == ';') {
      /* a font's entry is done */
      RFont *f;
      char *name = r->tb.data ? r->tb.data : (char *)"";
      while (*name == ' ') name++;
      if (r->nfonts >= r->fcap) {
        int nc = r->fcap ? r->fcap * 2 : 32;
        RFont *nf = (RFont *)realloc(r->fonts, (size_t)nc * sizeof(RFont));
        if (!nf) { r->oom = 1; return; }
        r->fonts = nf; r->fcap = nc;
      }
      f = &r->fonts[r->nfonts++];
      f->num = r->fnum;
      f->cp = cp_of_charset(r->fcharset);
      f->symbol = (u8)(f->cp == 2 || strstr(name, "Symbol") || strstr(name, "Wingdings") || strstr(name, "Webdings"));
      if (f->cp == 2) f->cp = 0;
      f->idx = doc_font(r->d, name[0] ? name : "Times New Roman");
      r->tb.len = 0; if (r->tb.data) r->tb.data[0] = 0;
      r->fcharset = 0;
      return;
    }
    break;
  case D_COLORTBL:
    if (cp == ';') {
      if (r->ncolors < 256) r->colors[r->ncolors++] = r->cset ? ((u32)r->cr << 16) | ((u32)r->cg << 8) | (u32)r->cb : COLOR_AUTO;
      r->cr = r->cg = r->cb = 0; r->cset = 0;
    }
    return;
  case D_LEVELTEXT:
    if (r->nlt < 40) r->lt[r->nlt++] = cp;
    return;
  case D_STYLESHEET: case D_TITLE: case D_AUTHOR: case D_FLDINST: case D_BKSTART: case D_BKEND: case D_REVTBL:
  case D_SN: case D_SV: case D_PN:
    break;
  default:
    return;
  }
  n = utf8_put(u, cp);
  if (g->dest == D_FLDINST && r->nfld) buf_add(&r->fld[r->nfld - 1].inst, u, (size_t)n);
  else buf_add(&r->tb, u, (size_t)n);
}
/* a byte of text in the current font's code page */
static void put_byte(R *r, u8 c)
{
  GState *g = &r->g[r->depth];
  int cp = g->fcp ? g->fcp : r->ansicpg;
  if (r->skip > 0) { r->skip--; return; }
  if (g->symbol) { put_cp(r, c); return; }
  if (cp_dbcs(cp)) {
    if (r->lead) { r->lead = 0; put_cp(r, '?'); return; }      /* (no double-byte tables: \u carries such text) */
    if (c >= 0x81 && c <= 0xFE) { r->lead = c; return; }
  }
  put_cp(r, cp_to_unicode(cp, c));
}

/* ---- paragraphs, cells, rows, tables */
static void table_end(R *r);
/* the paragraph being filled ends (cell: it ends a table cell) */
static void para_end(R *r)
{
  Sto *s = top(r);
  GState *g = &r->g[r->depth];
  Para *p;
  flush(r);
  if (!g->intbl && s->nrows) table_end(r);
  if (!s->cur) {
    CharFmt cf;
    text_cf(r, &cf);
    cf.rev = REV_NONE; cf.bmark = 0;
    if (!(s->cur = para_new(&g->pf, &cf))) { r->oom = 1; return; }
  }
  p = s->cur; s->cur = NULL;
  p->pf = g->pf;
  memset(&p->pf.cell, 0, sizeof p->pf.cell);
  memset(&p->pf.outer, 0, sizeof p->pf.outer);
  p->pf.sect = 0;
  if (s->pagebreak) { p->pf.pagebreak = 1; s->pagebreak = 0; }
  if (s->colbreak) { p->pf.colbreak = 1; s->colbreak = 0; }
  if (g->intbl) {
    p->pf.cell.table = 0xFFFF;
    p->pf.cell.row = (u16)s->nrows;
    p->pf.cell.col = (u8)(s->cell < RTF_MAXCELLS ? s->cell : RTF_MAXCELLS - 1);
  }
  /* a note's first space after its number goes (Quill draws the number) */
  if (s->kind == 2 && s->chftn == 1 && p->len && p->t[0] == ' ') {
    Para *q = para_new(&p->pf, &p->r[0].f);
    u32 k, start = 0;
    if (q) {
      for (k = 0; k < p->nr; k++) { u32 a = start > 1 ? start : 1; if (p->r[k].end > a) para_append(q, p->t + a, p->r[k].end - a, &p->r[k].f); start = p->r[k].end; }
      para_free(p); p = q;
    }
  }
  if (s->kind == 2) s->chftn = 2;
  if (!pl_add(&s->l, p)) r->oom = 1;
}
static void cell_end(R *r)
{
  Sto *s = top(r);
  GState *g = &r->g[r->depth];
  u8 was = g->intbl;
  if (!was) g->intbl = 1;               /* a \cell outside \intbl still ends a cell */
  para_end(r);
  g->intbl = was;
  s->cell++;
}
static void row_end(R *r)
{
  Sto *s = top(r);
  flush(r);
  if (s->cur) { GState *g = &r->g[r->depth]; u8 was = g->intbl; g->intbl = 1; para_end(r); g->intbl = was; s->cell++; }
  if (s->nrows >= s->rcap) {
    int nc = s->rcap ? s->rcap * 2 : 16;
    RowDef *nr = (RowDef *)realloc(s->rows, (size_t)nc * sizeof(RowDef));
    if (!nr) { r->oom = 1; return; }
    s->rows = nr; s->rcap = nc;
  }
  if (!s->nrows) {
    /* the table starts at the first paragraph of this row */
    u32 k = s->l.np;
    while (k > 0 && s->l.p[k - 1]->pf.cell.table == 0xFFFF && s->l.p[k - 1]->pf.cell.row == 0) k--;
    s->tfirst = k;
  }
  s->rows[s->nrows] = r->row;
  s->nrows++;
  s->cell = 0;
}
static int cmp_i32(const void *a, const void *b) { i32 x = *(const i32 *)a, y = *(const i32 *)b; return x < y ? -1 : x > y; }
/* the rows read become a table: the grid from every row's cell edges, each paragraph's cell */
static void table_end(R *r)
{
  Sto *s = top(r);
  i32 *edge;
  int ne = 0, nr = s->nrows, i, k, num;
  u32 pi;
  Table t;
  if (!nr) return;
  if (!(edge = (i32 *)malloc((size_t)(nr * (RTF_MAXCELLS + 1) + 1) * sizeof(i32)))) { r->oom = 1; s->nrows = 0; return; }
  for (i = 0; i < nr; i++) {
    RowDef *w = &s->rows[i];
    i32 prev = w->left;
    edge[ne++] = w->left;
    for (k = 0; k < w->ncell; k++) {
      if (w->c[k].x <= prev) w->c[k].x = prev + 10;      /* edges must grow */
      prev = w->c[k].x;
      if (w->c[k].hmerge != 2) edge[ne++] = w->c[k].x;
      else if (k > 0) edge[ne - 1] = w->c[k].x;          /* merged into the cell before: its edge moves */
    }
  }
  qsort(edge, (size_t)ne, sizeof(i32), cmp_i32);
  for (i = 1, k = 1; i < ne; i++) if (edge[i] - edge[k - 1] > 20) edge[k++] = edge[i];   /* (edges within a point are one) */
  ne = k;
  if (ne > MAX_COLS + 1) ne = MAX_COLS + 1;
  memset(&t, 0, sizeof t);
  t.ncols = (u8)(ne > 1 ? ne - 1 : 1);
  for (i = 0; i + 1 < ne; i++) t.grid[i] = edge[i + 1] - edge[i];
  if (ne < 2) t.grid[0] = 2000;
  t.indent = s->rows[0].left;
  t.jc = s->rows[0].jc;
  t.marl = s->rows[0].padset & 1 ? s->rows[0].padl : s->rows[0].gap;
  t.marr = s->rows[0].padset & 2 ? s->rows[0].padr : s->rows[0].gap;
  t.look = LK_DEFAULT;
  t.autofit = s->rows[0].autofit;
  if (!(num = doc_add_table(r->d, &t))) { r->oom = 1; free(edge); s->nrows = 0; return; }
  for (pi = s->tfirst; pi < s->l.np; pi++) {
    Para *p = s->l.p[pi];
    RowDef *w;
    int ci, from, to, a, b;
    i32 left, right;
    if (p->pf.cell.table != 0xFFFF) continue;
    if (p->pf.cell.row >= nr) p->pf.cell.row = (u16)(nr - 1);
    w = &s->rows[p->pf.cell.row];
    ci = p->pf.cell.col;
    if (w->ncell == 0) { p->pf.cell.table = (u16)num; p->pf.cell.col = 0; p->pf.cell.span = t.ncols; continue; }
    if (ci >= w->ncell) ci = w->ncell - 1;
    while (ci > 0 && w->c[ci].hmerge == 2) ci--;         /* (merged cells' text goes to the first) */
    from = ci; to = ci;
    while (to + 1 < w->ncell && w->c[to + 1].hmerge == 2) to++;
    left = from ? w->c[from - 1].x : w->left;
    right = w->c[to].x;
    for (a = 0; a + 1 < ne && edge[a + 1] <= left + 20; a++) ;
    for (b = a + 1; b + 1 < ne && edge[b] < right - 20; b++) ;
    if (a >= t.ncols) a = t.ncols - 1;
    if (b > t.ncols) b = t.ncols;
    if (b <= a) b = a + 1;
    {
      Cell *c = &p->pf.cell;
      CellDef *cd = &w->c[ci];
      u16 row = c->row;
      memset(c, 0, sizeof *c);
      c->table = (u16)num; c->row = row; c->col = (u8)a; c->span = (u8)(b - a);
      c->vmerge = cd->vmerge; c->valign = cd->valign; c->borders = cd->borders;
      memcpy(c->bstyle, cd->bstyle, 4); memcpy(c->bwidth, cd->bwidth, 4);
      memcpy(c->bcolor, cd->bcolor, sizeof c->bcolor);
      c->shade = cd->shade;
      c->rowh = (i16)(w->rowh > 32767 ? 32767 : w->rowh < -32767 ? -32767 : w->rowh);
      c->header = w->header; c->cantsplit = w->cantsplit;
    }
  }
  /* two paragraphs of one cell may have come from cells merged into it: fine (same col). A row's
     cells must go left to right: drop the order problems by sorting stably within each row */
  free(edge);
  s->nrows = 0; s->cell = 0;
}

/* ---- stories */
static void sto_push(R *r, int kind, int which)
{
  Sto *s;
  flush(r);
  if (r->nsto >= MAXSTO) { r->g[r->depth].dest = D_SKIP; return; }
  s = &r->sto[r->nsto++];
  memset(s, 0, sizeof *s);
  s->kind = kind; s->which = which; s->depth = r->depth;
  r->g[r->depth].owner = 1;
  /* a story starts with fresh paragraph formatting */
  {
    GState *g = &r->g[r->depth];
    g->intbl = 0; g->itap = 0;
  }
}
static void sto_close(R *r)
{
  Sto *s = top(r);
  flush(r);
  if (s->cur && (s->cur->len || s->kind == 0 || !s->l.np)) para_end(r);
  else if (s->cur) { para_free(s->cur); s->cur = NULL; }
  if (s->nrows) table_end(r);
}
static void sto_pop(R *r)
{
  Sto *s = top(r);
  sto_close(r);
  free(s->rows); s->rows = NULL;
  if (s->kind == 1) {
    pl_to_story(&s->l, &r->sechf[s->which]);
  } else if (s->kind == 2) {
    int k = s->endnote ? doc_add_endnote(r->d) : doc_add_note(r->d);
    char ref[8];
    r->nsto--;
    if (k < 0) { pl_free(&s->l); return; }
    pl_to_story(&s->l, s->endnote ? &r->d->enotes[k] : &r->d->notes[k]);
    if (s->endnote) enote_utf8(ref, k); else note_utf8(ref, k);
    body_utf8_at(r, ref, 4, r->depth > 0 ? r->depth - 1 : 0);   /* (in the text round the note) */
    return;
  }
  pl_free(&s->l);
  r->nsto--;
}

/* ---- sections */
static void sec_reset(R *r)
{
  sect_default(&r->sec);
  r->secpage = r->d->page;
  r->secpage_set = 0;
}
static int sec_plain(const SectFmt *s)
{
  return s->start == SB_NEXT && s->cols == 1 && !s->colline && s->valign == VA_TOP && s->linenum == LN_NONE && s->colgap == 720;
}
/* \sect: the section ends with the last paragraph */
static void sect_end(R *r)
{
  Sto *s = top(r);
  SectFmt f = r->sec;
  int k, own = 0;
  if (s->kind != 0) return;
  if (s->cur && (s->cur->len || !s->l.np)) para_end(r);
  else if (s->cur) { para_free(s->cur); s->cur = NULL; }
  if (s->nrows) table_end(r);
  if (!s->l.np) { GState *g = &r->g[r->depth]; s->cur = para_new(&g->pf, &g->cf); para_end(r); }
  if (!s->l.np) return;
  f.haspage = 1;
  f.page = r->secpage;
  for (k = 0; k < 2 * HF_N; k++) if (r->sechf[k].np) own = 1;
  f.hf = own ? r->sechf : NULL;
  if ((k = doc_add_sect(r->d, &f))) s->l.p[s->l.np - 1]->pf.sect = (u16)k;
}

/* ---- the style sheet */
static RStyle *style_entry(R *r)
{
  if (r->nstyles >= r->scap) {
    int nc = r->scap ? r->scap * 2 : 32;
    RStyle *ns = (RStyle *)realloc(r->styles, (size_t)nc * sizeof(RStyle));
    if (!ns) { r->oom = 1; return NULL; }
    r->styles = ns; r->scap = nc;
  }
  return &r->styles[r->nstyles];
}
static void apply_entry(R *r, int i);
/* a style sheet entry made a Doc style when the text first uses it (so a file's hundreds of unused
   styles don't fill Quill's table) */
static int materialize(R *r, int i)
{
  Doc *d = r->d;
  RStyle *e = &r->styles[i];
  Style s;
  char *o;
  const char *q;
  int k;
  memset(&s, 0, sizeof s);
  strncpy(s.name, e->name, sizeof s.name - 1);
  for (q = e->name, o = s.id; *q && o < s.id + sizeof s.id - 1; q++) if (*q != ' ') *o++ = *q;
  *o = 0;
  if (doc_style(d, s.id) >= 0) snprintf(s.id, sizeof s.id, "Rtf%d", e->num);
  s.type = e->type; s.based = NO_STYLE;
  s.pf = d->defpf; s.cf = d->defcf;
  if ((k = doc_add_style(d, &s)) < 0) { r->stymap[i] = -1; return -1; }
  r->stymap[i] = k;
  apply_entry(r, i);
  doc_resolve_styles(d);
  d->styles[k].pf.style = (u8)k;
  return k;
}
static int doc_style_of(R *r, int num)
{
  int i;
  for (i = 0; i < r->nstyles && i < r->nstymap; i++)
    if (r->styles[i].num == num && r->styles[i].type != 0xFF) return r->stymap[i] == -2 ? materialize(r, i) : r->stymap[i];
  return -1;
}
/* entry i's settings into its Doc style */
static void apply_entry(R *r, int i)
{
  Doc *d = r->d;
  RStyle *e = &r->styles[i];
  Style *s;
  int b, k = r->stymap[i];
  if (k < 0) return;
  s = &d->styles[k];
  s->opf = e->pf; s->ocf = e->cf;
  s->pmask = e->type == ST_PARA ? (e->pmask & PM_STYLED) : 0;
  s->cmask = e->cmask & CM_LOOK;
  s->opf.style = (u8)k;
  b = e->based >= 0 && e->based != e->num ? doc_style_of(r, e->based) : -1;
  s = &d->styles[k];
  s->based = (u8)(b >= 0 && d->styles[b].type == s->type && b != k ? b : NO_STYLE);
  if (s->type == ST_PARA && k == 0) s->based = NO_STYLE;
  b = e->next >= 0 && e->next != e->num ? doc_style_of(r, e->next) : -1;
  s = &d->styles[k];
  s->next = (u8)(b >= 0 && d->styles[b].type == ST_PARA ? b : (s->type == ST_PARA ? k : 0));
  if (e->quick) s->quick = 1;
}
/* the style sheet read: an entry whose name Quill has (or \s0, Normal) takes over that style now;
   the others become styles when the text uses them */
static void styles_done(R *r)
{
  Doc *d = r->d;
  int i;
  if (!r->nstyles) return;
  if (!(r->stymap = (int *)malloc((size_t)r->nstyles * sizeof(int)))) { r->oom = 1; return; }
  r->nstymap = r->nstyles;
  for (i = 0; i < r->nstyles; i++) {
    RStyle *e = &r->styles[i];
    int k = -1;
    char *nm = e->name;
    size_t L;
    r->stymap[i] = -1;
    if (e->type == 0xFF) continue;
    while (*nm == ' ') memmove(nm, nm + 1, strlen(nm));
    L = strlen(nm);
    while (L && (nm[L - 1] == ' ' || nm[L - 1] == ';')) nm[--L] = 0;
    if (!nm[0]) sprintf(nm, "Style %d", e->num);
    if (e->type == ST_PARA && e->num == 0) k = 0;                  /* \s0: Normal */
    if (k < 0) k = doc_style_by_name(d, nm);
    if (k < 0 && (!strcmp(nm, "Default Paragraph Font") || !strcmp(nm, "Default Paragraph Style"))) continue;
    if (k >= 0 && d->styles[k].type != e->type) k = -1;
    if (k >= 0 && str_ieq(d->styles[k].name, nm)) strcpy(d->styles[k].name, nm);    /* (the file's spelling: "Heading 1") */
    r->stymap[i] = k >= 0 ? k : -2;
  }
  for (i = 0; i < r->nstyles; i++) if (r->stymap[i] >= 0) apply_entry(r, i);
  doc_resolve_styles(d);
  for (i = 0; i < d->nstyles; i++) d->styles[i].pf.style = (u8)i;
}

/* ---- lists */
static void level_done(R *r)
{
  ListLevel *lv;
  int i;
  char *o;
  if (r->curlevel < 0 || r->curlevel > 8) return;
  lv = &r->curlist.def.lvl[r->curlevel];
  o = lv->text;
  if (r->nlt > 0) {
    int len = (int)r->lt[0];
    for (i = 1; i <= len && i < r->nlt && o < lv->text + sizeof lv->text - 5; i++) {
      u32 c = r->lt[i];
      if (c < 9) { o += sprintf(o, "%%%d", (int)c + 1); continue; }
      if (r->ltfont || (c >= 0xF000 && c <= 0xF0FF)) {
        switch (c & 0xFF) {
        case 0xB7: case 0x9F: case 0x6C: c = 0x2022; break;
        case 0xA7: case 0x6E: c = 0x25AA; break;
        case 0xD8: case 0xE0: c = 0x25BA; break;
        case 0xFC: c = 0x2713; break;
        case 0x76: c = 0x2756; break;
        default: if (r->ltfont) c = c == 'o' ? 'o' : 0x2022;
        }
      }
      o += utf8_put(o, c);
    }
  }
  *o = 0;
  if (lv->fmt == NF_BULLET && !lv->text[0]) strcpy(lv->text, "\xE2\x80\xA2");
}
static int list_by_id(R *r, int id)
{
  int i;
  for (i = 0; i < r->nrlists; i++) if (r->rlists[i].id == id) return i;
  return -1;
}
/* a Word 6 \pn list: one per kind and level look */
static void pn_done(R *r)
{
  GState *pg = &r->g[r->depth > 0 ? r->depth - 1 : 0];
  int kind = r->pnkind == 1 ? 0 : 1, lvl = r->pnlvl;
  if (lvl < 0 || lvl > 8) lvl = 0;
  if (!r->pnkind) return;
  if (!r->pnlist[kind][0]) {
    int n = doc_list_preset(r->d, kind ? LP_NUMBERS : LP_BULLETS);
    if (!n) return;
    r->pnlist[kind][0] = n;
    if (kind) { ListLevel *v = &r->d->lists[n - 1].lvl[0]; v->fmt = (u8)r->pnfmt; v->start = (u16)(r->pnstart > 0 ? r->pnstart : 1); }
  }
  pg->pf.list = (u16)r->pnlist[kind][0];
  pg->pf.level = (u8)lvl;
}

/* ---- pictures */
static void pict_done(R *r)
{
  Doc *d = r->d;
  i32 cx, cy;
  int k = -1;
  char ref[8];
  {
    int pd = r->g[r->depth > 0 ? r->depth - 1 : 0].dest;
    if ((pd != D_BODY && pd != D_SHPINST && pd != D_SV) || (pd == D_SV && strcmp(r->sn, "pib"))) { r->pict.len = 0; return; }
  }
  cx = r->goalw > 0 ? r->goalw : 0; cy = r->goalh > 0 ? r->goalh : 0;
  if (r->scx > 0 && cx) cx = (i32)((long)cx * r->scx / 100);
  if (r->scy > 0 && cy) cy = (i32)((long)cy * r->scy / 100);
  if (r->ptype == 1 || r->ptype == 2) {
    u8 *data = (u8 *)malloc(r->pict.len ? r->pict.len : 1);
    if (data) { memcpy(data, r->pict.data, r->pict.len); k = doc_add_image(d, data, (u32)r->pict.len, cx, cy); }
  }
  if (k < 0) {
    /* a metafile or a bitmap Quill can't show: a box of its size in its place */
    Image im;
    img_defaults(&im);
    im.kind = OBJ_SHAPE; im.shape = SH_RECT;
    im.lstyle = PB_DASHED; im.lwidth = 4; im.lcolor = 0x808080; im.fill = COLOR_AUTO;
    if (!cx && r->picw > 0) cx = r->ptype == 3 ? (i32)((long)r->picw * 1440 / 2540) : r->picw * 15;
    if (!cy && r->pich > 0) cy = r->ptype == 3 ? (i32)((long)r->pich * 1440 / 2540) : r->pich * 15;
    im.cx = cx > 0 ? cx : 1440; im.cy = cy > 0 ? cy : 1440;
    strcpy(im.alt, "Picture");
    k = doc_take_object(d, &im);
    if (r->lost) *r->lost |= LOST_PICTURES;
  }
  r->pict.len = 0;
  if (k < 0) return;
  pic_utf8(ref, k);
  {
    /* (the picture goes into the story's text even from inside a shape's properties) */
    GState *g = &r->g[r->depth];
    u8 dsave = g->dest, hsave = g->hidden;
    g->dest = D_BODY; g->hidden = 0;
    body_utf8(r, ref, 4);
    g->dest = dsave; g->hidden = hsave;
  }
}

/* ---- fields */
enum { FK_OTHER, FK_LINK, FK_PAGE, FK_PAGES, FK_PAGEREF };
static void field_inst_done(R *r)
{
  char *s, *p, *q;
  if (!r->nfld) return;
  s = r->fld[r->nfld - 1].inst.data;
  if (!s) return;
  for (p = s; *p == ' '; p++) ;
  if (!strncmp(p, "HYPERLINK", 9)) {
    char url[512], anchor[200];
    int local = 0;
    url[0] = anchor[0] = 0;
    p += 9;
    while (*p) {
      while (*p == ' ') p++;
      if (*p == '\\') {
        if (p[1] == 'l') local = 1;
        p += 2;
        continue;
      }
      if (!*p) break;
      {
        char tmp[512];
        size_t k = 0;
        if (*p == '"') { p++; while (*p && *p != '"' && k < sizeof tmp - 1) { if (*p == '\\' && p[1] == '\\') p++; tmp[k++] = *p++; } if (*p) p++; }
        else while (*p && *p != ' ' && k < sizeof tmp - 1) tmp[k++] = *p++;
        tmp[k] = 0;
        if (local) { strncpy(anchor, tmp, sizeof anchor - 1); anchor[sizeof anchor - 1] = 0; local = 0; }
        else if (!url[0]) strcpy(url, tmp);
      }
    }
    if (anchor[0]) {
      if (url[0]) { size_t k = strlen(url); if (k + strlen(anchor) + 2 < sizeof url) { url[k] = '#'; strcpy(url + k + 1, anchor); } }
      else { url[0] = '#'; strncpy(url + 1, anchor, sizeof url - 2); url[sizeof url - 1] = 0; }
    }
    r->fld[r->nfld - 1].kind = FK_LINK;
    buf_free(&r->fld[r->nfld - 1].inst);
    buf_adds(&r->fld[r->nfld - 1].inst, url);
    return;
  }
  for (q = p; *q && *q != ' '; q++) ;
  if ((q - p == 4 && !strncmp(p, "PAGE", 4))) r->fld[r->nfld - 1].kind = FK_PAGE;
  else if ((q - p == 8 && !strncmp(p, "NUMPAGES", 8)) || (q - p == 12 && !strncmp(p, "SECTIONPAGES", 12))) r->fld[r->nfld - 1].kind = FK_PAGES;
  else if (q - p == 7 && !strncmp(p, "PAGEREF", 7)) {
    char name[100];
    size_t k = 0;
    while (*q == ' ') q++;
    while (*q && *q != ' ' && *q != '\\' && k < sizeof name - 2) name[k++] = *q++;
    name[k] = 0;
    r->fld[r->nfld - 1].kind = FK_PAGEREF;
    buf_free(&r->fld[r->nfld - 1].inst);
    buf_addc(&r->fld[r->nfld - 1].inst, '#');
    buf_adds(&r->fld[r->nfld - 1].inst, name);
  }
}
/* a field's result starts: a link's text gets the link; page fields become Quill's own and their
   result goes */
static void fldrslt_start(R *r)
{
  GState *g = &r->g[r->depth];
  int k;
  if (!r->nfld) return;
  k = r->fld[r->nfld - 1].kind;
  g->fldrslt = 1;
  if (k == FK_LINK) {
    const char *u = r->fld[r->nfld - 1].inst.data;
    if (u && u[0]) g->cf.link = (u16)doc_link(r->d, u);
  } else if (k == FK_PAGE || k == FK_PAGES || k == FK_PAGEREF) {
    CharFmt save = g->cf;
    if (k == FK_PAGEREF) { const char *u = r->fld[r->nfld - 1].inst.data; g->cf.link = (u16)doc_link(r->d, u ? u : "#"); }
    body_utf8(r, k == FK_PAGE ? FIELD_PAGE : k == FK_PAGES ? FIELD_PAGES : FIELD_PAGEREF, 3);
    g->cf = save;
    g->rsltskip = 1;                     /* the result is what Quill shows by itself */
  }
}

/* ---- control words */
static u32 col(R *r, int k) { return k > 0 && k < r->ncolors ? r->colors[k] : COLOR_AUTO; }
static int font_of(R *r, int num, int *cp, u8 *symbol)
{
  int i;
  for (i = 0; i < r->nfonts; i++) if (r->fonts[i].num == num) { *cp = r->fonts[i].cp; *symbol = r->fonts[i].symbol; return r->fonts[i].idx; }
  *cp = 0; *symbol = 0;
  return -1;
}
static void set_font(R *r, GState *g, int num)
{
  int cp;
  u8 sym;
  int k = font_of(r, num, &cp, &sym);
  if (k >= 0) { g->cf.font = (u8)k; g->fcp = cp; g->symbol = sym; }
  g->cmask |= CM_FONT;
}
static void plain(R *r, GState *g)
{
  CharFmt *c = &g->cf;
  u16 link = c->link;
  memset(c, 0, sizeof *c);
  c->color = COLOR_AUTO;
  c->size = 24;
  c->link = link;
  set_font(r, g, r->deff);
  g->hidden = 0;
  g->rev_ins = g->rev_del = 0;
}
static void pard(R *r, GState *g)
{
  memset(&g->pf, 0, sizeof g->pf);
  g->pf.line = 240; g->pf.linerule = LS_AUTO;
  g->pf.nowidow = 1;                      /* RTF's default: no widow control (\widctlpar turns it on) */
  g->intbl = 0; g->itap = 0;
  r->bt = BT_NONE;
}
/* one tab stop's settings wait for its \tx */
static u8 tabkind, tablead;
static void add_tab(GState *g, i32 x)
{
  int k;
  for (k = 0; k < 8 && g->pf.tabs[k]; k++) ;
  if (k < 8 && x > 0) { g->pf.tabs[k] = x; g->pf.tabkind[k] = tabkind; g->pf.tablead[k] = tablead; if (k < 7) g->pf.tabs[k + 1] = 0; }
  tabkind = TAB_LEFT; tablead = LEAD_NONE;
  g->pmask |= PM_TABS;
}
/* \brdr... words: the line kind, width, colour, space of the border being described */
static void border_word(R *r, GState *g, const char *w, int has, long v)
{
  int pb = -1;
  u8 *style = NULL, *width = NULL, *space = NULL;
  u32 *color = NULL;
  u8 *bits = NULL; u8 bit = 0;
  if (r->bt >= BT_PARA && r->bt < BT_PARA + PE_N) {
    int e = r->bt - BT_PARA;
    style = &g->pf.bstyle[e]; width = &g->pf.bwidth[e]; space = &g->pf.bspace[e]; color = &g->pf.bcolor[e];
    g->pmask |= PM_BORDER;
  } else if (r->bt >= BT_CELL && r->bt < BT_CELL + 4 && r->rowcell < RTF_MAXCELLS) {
    int e = r->bt - BT_CELL;
    CellDef *c = &r->row.c[r->rowcell];
    style = &c->bstyle[e]; width = &c->bwidth[e]; color = &c->bcolor[e];
    bits = &c->borders; bit = (u8)(1 << e);
  } else if (r->bt == BT_PAGE) {
    style = &r->d->border.style; width = &r->d->border.width; color = &r->d->border.color;
  } else if (r->bt == BT_CHAR) {
    if (!strcmp(w, "brdrnone") || !strcmp(w, "brdrnil")) g->cf.fx &= (u8)~FX_BORDER; else if (strncmp(w, "brdrw", 5) && strncmp(w, "brdrcf", 6) && strcmp(w, "brsp")) g->cf.fx |= FX_BORDER;
    g->cmask |= CM_CBORDER;
    return;
  } else return;
  if (!strcmp(w, "brdrs") || !strcmp(w, "brdrhair") || !strcmp(w, "brdrsh") || !strcmp(w, "brdroutset") || !strcmp(w, "brdrinset") || !strcmp(w, "brdremboss") || !strcmp(w, "brdrengrave")) pb = PB_SINGLE;
  else if (!strcmp(w, "brdrdb") || !strcmp(w, "brdrtriple")) pb = PB_DOUBLE;
  else if (!strcmp(w, "brdrth") || !strncmp(w, "brdrthtn", 8) || !strncmp(w, "brdrtnth", 8)) pb = PB_THICK;
  else if (!strcmp(w, "brdrdot")) pb = PB_DOTTED;
  else if (!strcmp(w, "brdrdash") || !strcmp(w, "brdrdashsm") || !strcmp(w, "brdrdashd") || !strcmp(w, "brdrdashdd")) pb = PB_DASHED;
  else if (!strcmp(w, "brdrnone") || !strcmp(w, "brdrnil")) pb = PB_NONE;
  else if (!strcmp(w, "brdrw") && has) { long e = (v * 2 + 2) / 5; if (width) *width = (u8)(e < 1 ? 1 : e > 255 ? 255 : e); return; }
  else if (!strcmp(w, "brdrcf") && has) { if (color) *color = col(r, (int)v); return; }
  else if (!strcmp(w, "brsp") && has) { if (space) *space = (u8)(v / 20 > 255 ? 255 : v < 0 ? 0 : v / 20); if (r->bt == BT_PAGE) r->d->border.space = (i32)v; return; }
  else if (!strncmp(w, "brdr", 4)) pb = PB_SINGLE;              /* other kinds as a single line */
  if (pb < 0) return;
  if (bits) {
    if (pb == PB_NONE) *bits &= (u8)~bit; else { *bits |= bit; *style = (u8)(pb == PB_SINGLE ? 0 : pb); }
    return;
  }
  if (style) *style = (u8)pb;
  if (width && !*width && pb != PB_NONE) *width = 4;
  if (color && r->bt != BT_PAGE && pb != PB_NONE && !*color) *color = COLOR_AUTO;
}

static int kw(const char *w, const char *k) { return !strcmp(w, k); }

/* a destination word's group: what it collects */
static void dest(R *r, GState *g, int d) { g->dest = (u8)d; g->owner = 1; }

/* a control word (has: a number followed it). star: it came after \* (unknown ones are skipped) */
static void control(R *r, const char *w, int has, long v, int star)
{
  GState *g = &r->g[r->depth];
  int on = !has || v != 0;
  Doc *d = r->d;
  int dst = g->dest;

  /* ---- destinations */
  if (dst == D_SKIP) return;
  if (kw(w, "fonttbl")) { dest(r, g, D_FONTTBL); return; }
  if (kw(w, "colortbl")) { dest(r, g, D_COLORTBL); return; }
  if (kw(w, "stylesheet")) { dest(r, g, D_STYLESHEET); return; }
  if (kw(w, "info")) { dest(r, g, D_INFO); return; }
  if (dst == D_INFO && (kw(w, "title") || kw(w, "author"))) { dest(r, g, kw(w, "title") ? D_TITLE : D_AUTHOR); r->tb.len = 0; return; }
  if (kw(w, "listtable")) { dest(r, g, D_LISTTABLE); return; }
  if (kw(w, "listoverridetable")) { dest(r, g, D_LSOVERRIDE); return; }
  if (kw(w, "revtbl")) { dest(r, g, D_REVTBL); r->tb.len = 0; return; }
  if (kw(w, "pict")) {
    dest(r, g, D_PICT);
    r->pict.len = 0; r->ptype = 0; r->picw = r->pich = r->goalw = r->goalh = 0; r->scx = r->scy = 100; r->hexhi = -1;
    return;
  }
  if (kw(w, "shppict")) { return; }                          /* its \pict is read where it is */
  if (kw(w, "nonshppict") || kw(w, "shprslt") || kw(w, "pntext") || kw(w, "listtext") || kw(w, "pntxta") || kw(w, "pntxtb") ||
      kw(w, "fldtype") || kw(w, "datafield") || kw(w, "objdata") || kw(w, "objclass") || kw(w, "themedata") ||
      kw(w, "colorschememapping") || kw(w, "latentstyles") || kw(w, "datastore") || kw(w, "xmlnstbl") || kw(w, "rsidtbl") ||
      kw(w, "generator") || kw(w, "filetbl") || kw(w, "mmathPr") || kw(w, "userprops") || kw(w, "pgdsctbl") || kw(w, "xe") ||
      kw(w, "tc") || kw(w, "txe") || kw(w, "annotation") || kw(w, "atnid") || kw(w, "atnauthor") || kw(w, "atndate") ||
      kw(w, "atnref") || kw(w, "atnparent") || kw(w, "atrfstart") || kw(w, "atrfend") || kw(w, "nesttableprops") ||
      kw(w, "template") || kw(w, "docvar") || kw(w, "ftnsep") || kw(w, "ftnsepc") || kw(w, "aftnsep") || kw(w, "aftnsepc") ||
      kw(w, "ftncn") || kw(w, "aftncn") || kw(w, "private") || kw(w, "wgrffmtfilter") || kw(w, "listpicture") || kw(w, "hlinkbase") ||
      kw(w, "keywords") || kw(w, "subject") || kw(w, "doccomm") || kw(w, "comment") || kw(w, "operator") || kw(w, "company") ||
      kw(w, "manager") || kw(w, "category")) {
    if (kw(w, "pntxta") || kw(w, "pntxtb")) { dest(r, g, D_SKIP); return; }
    if (kw(w, "nesttableprops")) r->nested_lost = 1;
    dest(r, g, D_SKIP); return;
  }
  if (dst == D_LISTTABLE && kw(w, "list")) {
    dest(r, g, D_LIST);
    memset(&r->curlist, 0, sizeof r->curlist);
    {
      int k;
      for (k = 0; k < 9; k++) { r->curlist.def.lvl[k].fmt = NF_DECIMAL; r->curlist.def.lvl[k].start = 1; }
    }
    r->curlevel = -1;
    return;
  }
  if (dst == D_LIST && kw(w, "listlevel")) {
    dest(r, g, D_LEVEL);
    r->curlevel++; r->nlt = 0; r->ltfont = 0;
    return;
  }
  if (dst == D_LIST && kw(w, "listname")) { dest(r, g, D_SKIP); return; }
  if (dst == D_LIST && kw(w, "listid") && has) { r->curlist.id = (int)v; return; }
  if (dst == D_LEVEL) {
    ListLevel *lv = r->curlevel >= 0 && r->curlevel < 9 ? &r->curlist.def.lvl[r->curlevel] : NULL;
    if (kw(w, "leveltext")) { dest(r, g, D_LEVELTEXT); r->nlt = 0; return; }
    if (kw(w, "levelnumbers")) { dest(r, g, D_SKIP); return; }
    if (!lv) return;
    if ((kw(w, "levelnfc") || kw(w, "levelnfcn")) && has) {
      switch (v) { case 0: lv->fmt = NF_DECIMAL; break; case 1: lv->fmt = NF_UPPER_ROMAN; break; case 2: lv->fmt = NF_LOWER_ROMAN; break;
                   case 3: lv->fmt = NF_UPPER_LETTER; break; case 4: lv->fmt = NF_LOWER_LETTER; break; case 23: lv->fmt = NF_BULLET; break;
                   case 255: lv->fmt = NF_NONE; break; default: lv->fmt = NF_DECIMAL; }
      return;
    }
    if (kw(w, "levelstartat") && has) { lv->start = (u16)(v < 0 ? 0 : v); return; }
    if (kw(w, "li") || kw(w, "lin")) { if (has) lv->left = (i32)v; return; }
    if (kw(w, "fi")) { if (has) lv->hanging = (i32)-v; return; }
    if (kw(w, "f") && has) {
      int cp; u8 sym;
      int k = font_of(r, (int)v, &cp, &sym);
      if (k >= 0) { strncpy(lv->font, d->fonts[k], sizeof lv->font - 1); r->ltfont = sym; if (sym) lv->font[0] = 0; }
      return;
    }
    return;
  }
  if (dst == D_LSOVERRIDE && kw(w, "listoverride")) { dest(r, g, D_LSOVERRIDE); r->lsid = 0; r->lsnum = 0; g->owner = 2; return; }
  if (dst == D_LSOVERRIDE) {
    if (kw(w, "listid") && has) r->lsid = (int)v;
    else if (kw(w, "ls") && has) r->lsnum = (int)v;
    return;
  }
  if (kw(w, "field")) {
    if (r->nfld < 8) { memset(&r->fld[r->nfld], 0, sizeof r->fld[0]); r->nfld++; g->field = (u8)r->nfld; }
    return;
  }
  if (kw(w, "fldinst")) { dest(r, g, D_FLDINST); return; }
  if (kw(w, "fldrslt")) { fldrslt_start(r); return; }
  if (kw(w, "bkmkstart")) { dest(r, g, D_BKSTART); r->tb.len = 0; if (r->tb.data) r->tb.data[0] = 0; return; }
  if (kw(w, "bkmkend")) { dest(r, g, D_BKEND); r->tb.len = 0; if (r->tb.data) r->tb.data[0] = 0; return; }
  if (dst == D_BODY || dst == D_SHPINST || dst == D_SV) {
    int hk = -1;
    if (kw(w, "header") || kw(w, "headerr")) hk = HF_DEFAULT;
    else if (kw(w, "headerl")) hk = HF_EVEN;
    else if (kw(w, "headerf")) hk = HF_FIRST;
    else if (kw(w, "footer") || kw(w, "footerr")) hk = HF_N + HF_DEFAULT;
    else if (kw(w, "footerl")) hk = HF_N + HF_EVEN;
    else if (kw(w, "footerf")) hk = HF_N + HF_FIRST;
    if (hk >= 0) {
      if (top(r)->kind != 0) { dest(r, g, D_SKIP); return; }
      sto_push(r, 1, hk); g->dest = D_BODY; pard(r, g); plain(r, g); return;
    }
    if (kw(w, "footnote")) {
      if (top(r)->kind != 0) { dest(r, g, D_SKIP); return; }
      sto_push(r, 2, 0); g->dest = D_BODY; pard(r, g); plain(r, g); return;
    }
    if (kw(w, "shpinst")) { dest(r, g, D_SHPINST); g->shpinst = 1; return; }
  }
  if (dst == D_SHPINST || dst == D_SV || dst == D_SN) {
    if (kw(w, "sn")) { dest(r, g, D_SN); r->tb.len = 0; if (r->tb.data) r->tb.data[0] = 0; return; }
    if (kw(w, "sv")) { dest(r, g, D_SV); return; }
    if (kw(w, "shptxt")) { dest(r, g, D_SKIP); return; }
    if (dst == D_SV && !(r->sn[0] && !strcmp(r->sn, "pib"))) { if (!kw(w, "pict")) return; }
    if (dst != D_SV) return;
  }
  if (kw(w, "ud")) { if (g->uprskip) g->dest = g->uprskip, g->uprskip = 0; return; }
  if (kw(w, "upr")) { g->uprskip = (u8)(dst ? dst : 0xFE); return; }
  if (dst == D_PICT) {
    if (kw(w, "pngblip")) r->ptype = 1;
    else if (kw(w, "jpegblip")) r->ptype = 2;
    else if (kw(w, "wmetafile") || kw(w, "emfblip") || kw(w, "macpict") || kw(w, "pmmetafile")) r->ptype = 3;
    else if (kw(w, "dibitmap") || kw(w, "wbitmap")) r->ptype = 4;
    else if (kw(w, "picw") && has) r->picw = (i32)v;
    else if (kw(w, "pich") && has) r->pich = (i32)v;
    else if (kw(w, "picwgoal") && has) r->goalw = (i32)v;
    else if (kw(w, "pichgoal") && has) r->goalh = (i32)v;
    else if (kw(w, "picscalex") && has) r->scx = (i32)v;
    else if (kw(w, "picscaley") && has) r->scy = (i32)v;
    else if (kw(w, "bliptag") || kw(w, "blipuid") || kw(w, "picprop")) { if (kw(w, "blipuid") || kw(w, "picprop")) dest(r, g, D_SKIP); }
    return;
  }
  if (dst == D_PN || kw(w, "pn")) {
    if (kw(w, "pn")) { dest(r, g, D_PN); r->pnkind = 0; r->pnlvl = 0; r->pnstart = 1; r->pnfmt = NF_DECIMAL; return; }
    if (kw(w, "pnlvlblt")) r->pnkind = 1;
    else if (kw(w, "pnlvlbody") || kw(w, "pnlvlcont")) r->pnkind = 2;
    else if (kw(w, "pnlvl") && has) { r->pnkind = 2; r->pnlvl = (int)v - 1; }
    else if (kw(w, "pnstart") && has) r->pnstart = (int)v;
    else if (kw(w, "pndec")) r->pnfmt = NF_DECIMAL;
    else if (kw(w, "pnucrm")) r->pnfmt = NF_UPPER_ROMAN;
    else if (kw(w, "pnlcrm")) r->pnfmt = NF_LOWER_ROMAN;
    else if (kw(w, "pnucltr")) r->pnfmt = NF_UPPER_LETTER;
    else if (kw(w, "pnlcltr")) r->pnfmt = NF_LOWER_LETTER;
    return;
  }
  /* style sheet entries (\*\cs: a character style) */
  if (dst == D_STYLESHEET && g->sty_entry) {
      RStyle *e = &r->styles[r->nstyles];
      if (kw(w, "s") && has) { e->num = (int)v; e->type = ST_PARA; return; }
      if (kw(w, "cs") && has) { e->num = (int)v; e->type = ST_CHAR; return; }
      if ((kw(w, "ds") || kw(w, "ts")) && has) { e->num = (int)v; e->type = 0xFF; return; }
      if (kw(w, "sbasedon") && has) { e->based = (int)v; return; }
      if (kw(w, "snext") && has) { e->next = (int)v; return; }
      if (kw(w, "sqformat")) { e->quick = 1; return; }
      if (kw(w, "additive") || kw(w, "slink") || kw(w, "ssemihidden") || kw(w, "sunhideused") || kw(w, "spriority") || kw(w, "styrsid") || kw(w, "slocked") || kw(w, "sautoupd")) return;
    }
  if (star) { dest(r, g, D_SKIP); return; }                 /* an ignorable destination Quill doesn't know */

  /* ---- the document */
  if (kw(w, "ansicpg") && has) { r->ansicpg = (int)v; return; }
  if (kw(w, "mac")) { r->ansicpg = 10000; return; }
  if (kw(w, "pc")) { r->ansicpg = 437; return; }
  if (kw(w, "pca")) { r->ansicpg = 850; return; }
  if (kw(w, "deff") && has) { r->deff = (int)v; return; }
  if (kw(w, "uc") && has) { g->uc = (u8)(v < 0 ? 0 : v > 10 ? 10 : v); return; }
  if (kw(w, "u") && has) {
    long c = v < 0 ? v + 65536 : v;
    int skip = g->uc;
    put_cp(r, (u32)c);
    r->skip = skip;
    return;
  }
  if (dst == D_FONTTBL) {
    if (kw(w, "f") && has) { r->fnum = (int)v; r->fcharset = 0; r->tb.len = 0; if (r->tb.data) r->tb.data[0] = 0; }
    else if (kw(w, "fcharset") && has) r->fcharset = (int)v;
    return;
  }
  if (dst == D_COLORTBL) {
    if (kw(w, "red") && has) { r->cr = (int)v & 255; r->cset = 1; }
    else if (kw(w, "green") && has) { r->cg = (int)v & 255; r->cset = 1; }
    else if (kw(w, "blue") && has) { r->cb = (int)v & 255; r->cset = 1; }
    return;
  }
  if (dst != D_STYLESHEET && dst != D_BODY && dst != D_PN) return;

  /* ---- sections and the page */
  if (kw(w, "paperw") && has) { d->page.width = (i32)v; r->secpage.width = (i32)v; return; }
  if (kw(w, "paperh") && has) { d->page.height = (i32)v; r->secpage.height = (i32)v; return; }
  if (kw(w, "margl") && has) { d->page.left = (i32)v; r->secpage.left = (i32)v; return; }
  if (kw(w, "margr") && has) { d->page.right = (i32)v; r->secpage.right = (i32)v; return; }
  if (kw(w, "margt") && has) { d->page.top = (i32)v; r->secpage.top = (i32)v; return; }
  if (kw(w, "margb") && has) { d->page.bottom = (i32)v; r->secpage.bottom = (i32)v; return; }
  if (kw(w, "pgwsxn") && has) { r->secpage.width = (i32)v; r->secpage_set = 1; return; }
  if (kw(w, "pghsxn") && has) { r->secpage.height = (i32)v; r->secpage_set = 1; return; }
  if (kw(w, "marglsxn") && has) { r->secpage.left = (i32)v; r->secpage_set = 1; return; }
  if (kw(w, "margrsxn") && has) { r->secpage.right = (i32)v; r->secpage_set = 1; return; }
  if (kw(w, "margtsxn") && has) { r->secpage.top = (i32)v; r->secpage_set = 1; return; }
  if (kw(w, "margbsxn") && has) { r->secpage.bottom = (i32)v; r->secpage_set = 1; return; }
  if (kw(w, "headery") && has) { r->secpage.header = (i32)v; if (!r->secpage_set) d->page.header = (i32)v; return; }
  if (kw(w, "footery") && has) { r->secpage.footer = (i32)v; if (!r->secpage_set) d->page.footer = (i32)v; return; }
  if (kw(w, "facingp")) { r->facingp = 1; return; }
  if (kw(w, "titlepg")) { r->sec.titlepg = 1; return; }
  if (kw(w, "sectd")) { sec_reset(r); return; }     /* (headers carry on from the section before) */
  if (kw(w, "sect")) { sect_end(r); return; }
  if (kw(w, "sbknone")) { r->sec.start = SB_CONT; return; }
  if (kw(w, "sbkpage")) { r->sec.start = SB_NEXT; return; }
  if (kw(w, "sbkcol")) { r->sec.start = SB_NEXT; return; }
  if (kw(w, "sbkeven")) { r->sec.start = SB_EVEN; return; }
  if (kw(w, "sbkodd")) { r->sec.start = SB_ODD; return; }
  if (kw(w, "cols") && has) { r->sec.cols = (u8)(v < 1 ? 1 : v > MAX_SECT_COLS ? MAX_SECT_COLS : v); return; }
  if (kw(w, "colsx") && has) { r->sec.colgap = (i32)v; return; }
  if (kw(w, "linebetcol")) { r->sec.colline = 1; return; }
  if (kw(w, "vertalt")) { r->sec.valign = VA_TOP; return; }
  if (kw(w, "vertalc") || kw(w, "vertalj")) { r->sec.valign = VA_CENTER; return; }
  if (kw(w, "vertalb")) { r->sec.valign = VA_BOTTOM; return; }
  if (kw(w, "linemod") && has) { r->sec.lnby = (u8)(v < 1 ? 1 : v > 100 ? 100 : v); if (r->sec.linenum == LN_NONE && v > 0) r->sec.linenum = LN_PAGE; return; }
  if (kw(w, "linex") && has) { r->sec.lndist = (i32)v; return; }
  if (kw(w, "linestarts") && has) { r->sec.lnstart = (u16)v; return; }
  if (kw(w, "linerestart")) { r->sec.linenum = LN_SECTION; return; }
  if (kw(w, "lineppage")) { r->sec.linenum = LN_PAGE; return; }
  if (kw(w, "linecont")) { r->sec.linenum = LN_CONT; return; }
  if (kw(w, "pgnstarts") && has) { d->pgstart = (u16)v; return; }
  if (kw(w, "pgndec")) { d->pgfmt = NF_DECIMAL; return; }
  if (kw(w, "pgnucrm")) { d->pgfmt = NF_UPPER_ROMAN; return; }
  if (kw(w, "pgnlcrm")) { d->pgfmt = NF_LOWER_ROMAN; return; }
  if (kw(w, "pgnucltr")) { d->pgfmt = NF_UPPER_LETTER; return; }
  if (kw(w, "pgnlcltr")) { d->pgfmt = NF_LOWER_LETTER; return; }
  if (kw(w, "pgbrdrt") || kw(w, "pgbrdrl") || kw(w, "pgbrdrb") || kw(w, "pgbrdrr")) { r->bt = BT_PAGE; return; }
  if (kw(w, "pgbrdropt") && has) { d->border.frompage = (u8)((v & 32) == 0); return; }
  if (kw(w, "revtim") || kw(w, "creatim")) return;
  if (kw(w, "fet")) return;

  /* ---- breaks and special characters */
  if (dst == D_BODY) {
    if (kw(w, "par")) { para_end(r); return; }
    if (kw(w, "cell")) { cell_end(r); return; }
    if (kw(w, "nestcell")) { r->nested_lost = 1; para_end(r); return; }
    if (kw(w, "row")) { row_end(r); return; }
    if (kw(w, "nestrow")) { r->nested_lost = 1; return; }
    if (kw(w, "line")) { put_cp(r, '\v'); return; }
    if (kw(w, "tab")) { put_cp(r, '\t'); return; }
    if (kw(w, "page")) {
      Sto *s = top(r);
      flush(r);
      if (s->cur && s->cur->len) para_end(r);
      if (s->kind == 0) s->pagebreak = 1;
      return;
    }
    if (kw(w, "column")) {
      Sto *s = top(r);
      flush(r);
      if (s->cur && s->cur->len) para_end(r);
      if (s->kind == 0) s->colbreak = 1;
      return;
    }
    if (kw(w, "emdash")) { put_cp(r, 0x2014); return; }
    if (kw(w, "endash")) { put_cp(r, 0x2013); return; }
    if (kw(w, "lquote")) { put_cp(r, 0x2018); return; }
    if (kw(w, "rquote")) { put_cp(r, 0x2019); return; }
    if (kw(w, "ldblquote")) { put_cp(r, 0x201C); return; }
    if (kw(w, "rdblquote")) { put_cp(r, 0x201D); return; }
    if (kw(w, "bullet")) { put_cp(r, 0x2022); return; }
    if (kw(w, "emspace")) { put_cp(r, 0x2003); return; }
    if (kw(w, "enspace")) { put_cp(r, 0x2002); return; }
    if (kw(w, "qmspace")) { put_cp(r, 0x2005); return; }
    if (kw(w, "zwj")) { put_cp(r, 0x200D); return; }
    if (kw(w, "zwnj")) { put_cp(r, 0x200C); return; }
    if (kw(w, "zwbo")) { put_cp(r, 0x200B); return; }
    if (kw(w, "chftn")) { Sto *s = top(r); if (s->kind == 2 && !s->chftn) s->chftn = 1; return; }
    if (kw(w, "chpgn")) { body_utf8(r, FIELD_PAGE, 3); return; }
    if (kw(w, "ftnalt")) { Sto *s = top(r); if (s->kind == 2) s->endnote = 1; return; }
    if (kw(w, "chdate") || kw(w, "chtime") || kw(w, "chatn")) return;
  }

  /* ---- tables */
  if (kw(w, "trowd")) { memset(&r->row, 0, sizeof r->row); r->row.gap = 0; r->rowcell = 0; r->row.c[0].shade = COLOR_AUTO; { int k; for (k = 0; k < RTF_MAXCELLS; k++) { CellDef *c = &r->row.c[k]; c->shade = COLOR_AUTO; c->bcolor[0] = c->bcolor[1] = c->bcolor[2] = c->bcolor[3] = COLOR_AUTO; } } r->bt = BT_NONE; return; }
  if (kw(w, "trgaph") && has) { r->row.gap = (i32)v; return; }
  if (kw(w, "trleft") && has) { r->row.left = (i32)v; return; }
  if (kw(w, "trrh") && has) { r->row.rowh = (i32)v; return; }
  if (kw(w, "trhdr")) { r->row.header = 1; return; }
  if (kw(w, "trkeep")) { r->row.cantsplit = 1; return; }
  if (kw(w, "trautofit") && has) { r->row.autofit = (u8)(v != 0); return; }
  if (kw(w, "trqc")) { r->row.jc = AL_CENTER; return; }
  if (kw(w, "trqr")) { r->row.jc = AL_RIGHT; return; }
  if (kw(w, "trql")) { r->row.jc = AL_LEFT; return; }
  if (kw(w, "trpaddl") && has) { r->row.padl = (i32)v; return; }
  if (kw(w, "trpaddr") && has) { r->row.padr = (i32)v; return; }
  if (kw(w, "trpaddfl") && has) { if (v == 3) r->row.padset |= 1; return; }
  if (kw(w, "trpaddfr") && has) { if (v == 3) r->row.padset |= 2; return; }
  if (kw(w, "cellx") && has) {
    if (r->rowcell < RTF_MAXCELLS) { r->row.c[r->rowcell].x = (i32)v; r->rowcell++; r->row.ncell = r->rowcell; }
    r->bt = BT_NONE;
    return;
  }
  if (r->rowcell < RTF_MAXCELLS) {
    CellDef *c = &r->row.c[r->rowcell];
    if (kw(w, "clvmgf")) { c->vmerge = VM_START; return; }
    if (kw(w, "clvmrg")) { c->vmerge = VM_CONT; return; }
    if (kw(w, "clmgf")) { c->hmerge = 1; return; }
    if (kw(w, "clmrg")) { c->hmerge = 2; return; }
    if (kw(w, "clvertalt")) { c->valign = 0; return; }
    if (kw(w, "clvertalc")) { c->valign = 1; return; }
    if (kw(w, "clvertalb")) { c->valign = 2; return; }
    if (kw(w, "clcbpat") && has) { c->shade = col(r, (int)v); return; }
    if (kw(w, "clbrdrt")) { r->bt = BT_CELL + E_TOP; c->borders |= BD_TOP; return; }
    if (kw(w, "clbrdrl")) { r->bt = BT_CELL + E_LEFT; c->borders |= BD_LEFT; return; }
    if (kw(w, "clbrdrb")) { r->bt = BT_CELL + E_BOTTOM; c->borders |= BD_BOTTOM; return; }
    if (kw(w, "clbrdrr")) { r->bt = BT_CELL + E_RIGHT; c->borders |= BD_RIGHT; return; }
  }
  if (kw(w, "trbrdrt") || kw(w, "trbrdrl") || kw(w, "trbrdrb") || kw(w, "trbrdrr") || kw(w, "trbrdrh") || kw(w, "trbrdrv")) { r->bt = BT_NONE; return; }
  if (!strncmp(w, "brdr", 4) || kw(w, "brsp")) {
    if (r->bt == BT_BOX) { int e; for (e = PE_TOP; e <= PE_RIGHT; e++) { r->bt = BT_PARA + e; border_word(r, g, w, has, v); } r->bt = BT_BOX; }
    else border_word(r, g, w, has, v);
    return;
  }
  if (kw(w, "intbl")) { g->intbl = 1; if (!g->itap) g->itap = 1; return; }
  if (kw(w, "itap") && has) { g->itap = (u8)v; g->intbl = v > 0; if (v > 1) r->nested_lost = 1; return; }

  /* ---- paragraph formatting */
  if (kw(w, "pard")) { pard(r, g); return; }
  /* \s and \cs name the style; the formatting itself is all spelt out after them (Word's RTF) */
  if (kw(w, "s") && has) {
    int k;
    if (dst == D_STYLESHEET) return;
    k = doc_style_of(r, (int)v);
    if (k >= 0 && d->styles[k].type == ST_PARA) g->pf.style = (u8)k;
    return;
  }
  if (kw(w, "cs") && has) {
    int k = doc_style_of(r, (int)v);
    if (k > 0 && d->styles[k].type == ST_CHAR) g->cf.cstyle = (u8)k;
    return;
  }
  if (kw(w, "ql")) { g->pf.align = AL_LEFT; g->pmask |= PM_ALIGN; return; }
  if (kw(w, "qc")) { g->pf.align = AL_CENTER; g->pmask |= PM_ALIGN; return; }
  if (kw(w, "qr")) { g->pf.align = AL_RIGHT; g->pmask |= PM_ALIGN; return; }
  if (kw(w, "qj") || kw(w, "qd")) { g->pf.align = AL_JUSTIFY; g->pmask |= PM_ALIGN; return; }
  if ((kw(w, "li") || kw(w, "lin")) && has) { g->pf.left = (i32)v; g->pmask |= PM_INDENT; return; }
  if ((kw(w, "ri") || kw(w, "rin")) && has) { g->pf.right = (i32)v; g->pmask |= PM_INDENT; return; }
  if (kw(w, "fi") && has) { g->pf.first = (i32)v; g->pmask |= PM_INDENT; return; }
  if (kw(w, "sb") && has) { g->pf.before = (i32)v; g->pmask |= PM_SPACING; return; }
  if (kw(w, "sa") && has) { g->pf.after = (i32)v; g->pmask |= PM_SPACING; return; }
  if (kw(w, "sl") && has) {
    if (v == 0) { g->pf.line = 240; g->pf.linerule = LS_AUTO; }
    else if (v > 0) { g->pf.line = (i32)v; g->pf.linerule = LS_ATLEAST; }
    else { g->pf.line = (i32)-v; g->pf.linerule = LS_EXACT; }
    g->pmask |= PM_LINE;
    return;
  }
  if (kw(w, "slmult") && has) { if (v == 1 && g->pf.linerule == LS_ATLEAST) g->pf.linerule = LS_AUTO; return; }
  if (kw(w, "keepn")) { g->pf.keepnext = (u8)on; g->pmask |= PM_KEEP; return; }
  if (kw(w, "keep")) { g->pf.keeplines = (u8)on; g->pmask |= PM_PAGINATION; return; }
  if (kw(w, "widctlpar")) { g->pf.nowidow = 0; g->pmask |= PM_PAGINATION; return; }
  if (kw(w, "nowidctlpar")) { g->pf.nowidow = 1; g->pmask |= PM_PAGINATION; return; }
  if (kw(w, "pagebb")) { g->pf.pagebreak = (u8)on; g->pmask |= PM_BREAK; return; }
  if (kw(w, "contextualspace")) { g->pf.contextual = 1; g->pmask |= PM_CONTEXT; return; }
  if (kw(w, "tqc")) { tabkind = TAB_CENTER; return; }
  if (kw(w, "tqr")) { tabkind = TAB_RIGHT; return; }
  if (kw(w, "tqdec")) { tabkind = TAB_DECIMAL; return; }
  if (kw(w, "tldot") || kw(w, "tlmdot")) { tablead = LEAD_DOT; return; }
  if (kw(w, "tlhyph")) { tablead = LEAD_HYPHEN; return; }
  if (kw(w, "tlul") || kw(w, "tlth") || kw(w, "tleq")) { tablead = LEAD_LINE; return; }
  if ((kw(w, "tx") || kw(w, "tb")) && has) { add_tab(g, (i32)v); return; }
  if (kw(w, "brdrt")) { r->bt = BT_PARA + PE_TOP; return; }
  if (kw(w, "brdrl")) { r->bt = BT_PARA + PE_LEFT; return; }
  if (kw(w, "brdrb")) { r->bt = BT_PARA + PE_BOTTOM; return; }
  if (kw(w, "brdrr")) { r->bt = BT_PARA + PE_RIGHT; return; }
  if (kw(w, "brdrbtw")) { r->bt = BT_PARA + PE_BETWEEN; return; }
  if (kw(w, "box")) { r->bt = BT_BOX; return; }
  if (kw(w, "cbpat") && has) { u32 c = col(r, (int)v); g->pf.shade = c == COLOR_AUTO ? 0 : HL_ON | c; g->pmask |= PM_SHADE; return; }
  if (kw(w, "ls") && has) { g->pf.list = (u16)(v > 0 && v < 1024 ? r->lsmap[v] : 0); g->pmask |= PM_LIST; return; }
  if (kw(w, "ilvl") && has) { g->pf.level = (u8)(v < 0 ? 0 : v > 8 ? 8 : v); g->pmask |= PM_LIST; return; }
  if (kw(w, "outlinelevel")) return;

  /* ---- character formatting */
  if (kw(w, "plain")) { plain(r, g); return; }
  if (kw(w, "b")) { if (on) g->cf.flags |= CF_BOLD; else g->cf.flags &= (u8)~CF_BOLD; g->cmask |= CM_BOLD; return; }
  if (kw(w, "i")) { if (on) g->cf.flags |= CF_ITALIC; else g->cf.flags &= (u8)~CF_ITALIC; g->cmask |= CM_ITALIC; return; }
  if (kw(w, "strike")) { if (on) g->cf.flags |= CF_STRIKE; else g->cf.flags &= (u8)~CF_STRIKE; g->cmask |= CM_STRIKE; return; }
  if (kw(w, "striked")) { if (on) g->cf.fx |= FX_DSTRIKE; else g->cf.fx &= (u8)~FX_DSTRIKE; g->cmask |= CM_DSTRIKE; return; }
  if (kw(w, "caps")) { if (on) g->cf.flags |= CF_CAPS; else g->cf.flags &= (u8)~CF_CAPS; g->cmask |= CM_CAPS; return; }
  if (kw(w, "scaps")) { if (on) g->cf.fx |= FX_SMALLCAPS; else g->cf.fx &= (u8)~FX_SMALLCAPS; g->cmask |= CM_SMALLCAPS; return; }
  if (kw(w, "super")) { g->cf.flags = (u8)((g->cf.flags & ~CF_SUB) | CF_SUPER); g->cmask |= CM_SUPER | CM_SUB; return; }
  if (kw(w, "sub")) { g->cf.flags = (u8)((g->cf.flags & ~CF_SUPER) | CF_SUB); g->cmask |= CM_SUPER | CM_SUB; return; }
  if (kw(w, "nosupersub")) { g->cf.flags &= (u8)~(CF_SUPER | CF_SUB); g->cmask |= CM_SUPER | CM_SUB; return; }
  if (kw(w, "v")) { g->hidden = (u8)on; return; }
  {
    int ul = -1;
    if (kw(w, "ul")) ul = on ? UL_SINGLE : -2;
    else if (kw(w, "uld")) ul = UL_DOTTED;
    else if (kw(w, "uldb")) ul = UL_DOUBLE;
    else if (kw(w, "ulth") || kw(w, "ulthd") || kw(w, "ulthdash") || kw(w, "ulthldash")) ul = UL_THICK;
    else if (kw(w, "ulw")) ul = UL_WORDS;
    else if (kw(w, "ulwave") || kw(w, "ululdbwave") || kw(w, "ulhwave")) ul = UL_WAVE;
    else if (kw(w, "uldash") || kw(w, "uldashd") || kw(w, "uldashdd") || kw(w, "ulldash")) ul = UL_DASHED;
    else if (kw(w, "ulnone")) ul = -2;
    if (ul != -1) {
      if (ul == -2) { g->cf.flags &= (u8)~CF_UNDERLINE; g->cf.ul = 0; }
      else { g->cf.flags |= CF_UNDERLINE; g->cf.ul = (u8)ul; }
      g->cmask |= CM_UNDERLINE | CM_UL;
      return;
    }
  }
  if (kw(w, "f") && has) { set_font(r, g, (int)v); return; }
  if (kw(w, "fs") && has) { g->cf.size = (u16)(v < 2 ? 2 : v > 3276 ? 3276 : v); g->cmask |= CM_SIZE; return; }
  if (kw(w, "cf") && has) { g->cf.color = col(r, (int)v); g->cmask |= CM_COLOR; return; }
  if ((kw(w, "highlight") || kw(w, "chcbpat") || kw(w, "cb")) && has) { u32 c = col(r, (int)v); g->cf.hl = c == COLOR_AUTO ? 0 : HL_ON | c; g->cmask |= CM_HL; return; }
  if (kw(w, "expndtw") && has) { g->cf.spacing = (i16)v; g->cmask |= CM_SPACING; return; }
  if (kw(w, "expnd") && has) { g->cf.spacing = (i16)(v * 5); g->cmask |= CM_SPACING; return; }
  if (kw(w, "charscalex") && has) { g->cf.scale = (u16)(v == 100 ? 0 : v); g->cmask |= CM_SCALE; return; }
  if (kw(w, "up") && has) { g->cf.position = (i16)(has ? v : 6); g->cmask |= CM_POSITION; return; }
  if (kw(w, "dn")) { g->cf.position = (i16)-(has ? v : 6); g->cmask |= CM_POSITION; return; }
  if (kw(w, "chbrdr")) { r->bt = BT_CHAR; g->cf.fx |= FX_BORDER; g->cmask |= CM_CBORDER; return; }
  if (kw(w, "revised")) { g->rev_ins = (u8)on; return; }
  if (kw(w, "deleted")) { g->rev_del = (u8)on; return; }
  if (kw(w, "revauth") && has) { g->ra_ins = (int)v; return; }
  if (kw(w, "revauthdel") && has) { g->ra_del = (int)v; return; }
  if (kw(w, "revdttm") && has) { g->rdt_ins = (u32)v; return; }
  if (kw(w, "revdttmdel") && has) { g->rdt_del = (u32)v; return; }
}

/* ---- groups */
static void group_open(R *r)
{
  if (r->depth + 1 >= MAXG) { r->depth++; return; }       /* too deep: not tracked (the closing brace pops) */
  r->g[r->depth + 1] = r->g[r->depth];
  r->depth++;
  {
    GState *g = &r->g[r->depth], *pg = &r->g[r->depth - 1];
    g->owner = 0; g->field = 0; g->sty_entry = 0;
    g->uprskip = 0;
    if (pg->uprskip) { g->uprskip = pg->uprskip == 0xFE ? D_BODY : pg->uprskip; g->dest = D_SKIP; pg->uprskip = 0; }
    if (pg->dest == D_STYLESHEET && !pg->sty_entry) {
      RStyle *e = style_entry(r);
      if (e) {
        memset(e, 0, sizeof *e);
        e->type = ST_PARA; e->based = -1; e->next = -1;
        g->sty_entry = 1; g->owner = 1;
        pard(r, g); plain(r, g);
        g->cmask = g->pmask = 0;
        r->tb.len = 0; if (r->tb.data) r->tb.data[0] = 0;
      }
    }
  }
}
static void group_close(R *r)
{
  GState *g;
  if (r->depth <= 0) return;
  if (r->depth >= MAXG) { r->depth--; return; }
  g = &r->g[r->depth];
  if (r->nsto > 1 && top(r)->depth == r->depth) sto_pop(r);
  if (g->owner) {
    switch (g->dest) {
    case D_STYLESHEET:
      if (g->sty_entry) {
        RStyle *e = &r->styles[r->nstyles];
        char *nm = r->tb.data ? r->tb.data : (char *)"";
        char *semi = strchr(nm, ';');
        if (semi) *semi = 0;
        strncpy(e->name, nm, sizeof e->name - 1); e->name[sizeof e->name - 1] = 0;
        e->pf = g->pf; e->cf = g->cf; e->pmask = g->pmask; e->cmask = g->cmask;
        r->nstyles++;
        r->tb.len = 0; if (r->tb.data) r->tb.data[0] = 0;
      } else styles_done(r);
      break;
    case D_FONTTBL:
      {
        int cp; u8 sym;
        int fi = font_of(r, r->deff, &cp, &sym);
        if (fi >= 0) r->d->defcf.font = (u8)fi;
        set_font(r, &r->g[0], r->deff);
      }
      break;
    case D_TITLE: case D_AUTHOR:
      {
        char *t = r->tb.data ? r->tb.data : (char *)"";
        char *dst = g->dest == D_TITLE ? r->d->title : r->d->author;
        strncpy(dst, t, 127); dst[127] = 0;
        r->tb.len = 0;
      }
      break;
    case D_REVTBL:
      break;
    case D_LEVELTEXT:
      break;
    case D_LEVEL:
      level_done(r);
      break;
    case D_LIST:
      if (r->nrlists >= r->rlcap) {
        int nc = r->rlcap ? r->rlcap * 2 : 16;
        RList *nl = (RList *)realloc(r->rlists, (size_t)nc * sizeof(RList));
        if (!nl) { r->oom = 1; break; }
        r->rlists = nl; r->rlcap = nc;
      }
      r->rlists[r->nrlists++] = r->curlist;
      break;
    case D_LSOVERRIDE:
      if (g->owner == 2) {
        int k = list_by_id(r, r->lsid);
        if (k >= 0 && r->lsnum > 0 && r->lsnum < 1024) {
          ListDef def = r->rlists[k].def;
          def.id = (u16)r->lsnum;
          r->lsmap[r->lsnum] = doc_add_list(r->d, &def);
        }
      }
      break;
    case D_PICT:
      pict_done(r);
      break;
    case D_FLDINST:
      field_inst_done(r);
      break;
    case D_BKSTART:
      {
        char *t = r->tb.data ? r->tb.data : (char *)"";
        flush(r);
        r->bmark = (u16)doc_bmark(r->d, t);
        strncpy(r->bkname, t, sizeof r->bkname - 1); r->bkname[sizeof r->bkname - 1] = 0;
        r->tb.len = 0;
      }
      break;
    case D_BKEND:
      {
        char *t = r->tb.data ? r->tb.data : (char *)"";
        flush(r);
        if (!strcmp(t, r->bkname)) { r->bmark = 0; r->bkname[0] = 0; }
        r->tb.len = 0;
      }
      break;
    case D_SN:
      {
        char *t = r->tb.data ? r->tb.data : (char *)"";
        strncpy(r->sn, t, sizeof r->sn - 1); r->sn[sizeof r->sn - 1] = 0;
        r->tb.len = 0;
      }
      break;
    case D_PN:
      pn_done(r);
      break;
    default: break;
    }
  }
  if (g->dest == D_REVTBL && r->depth > 0 && r->g[r->depth - 1].dest == D_REVTBL) {
    /* a revision table entry: an author's name */
  }
  if (g->field && g->field == r->nfld) { buf_free(&r->fld[r->nfld - 1].inst); r->nfld--; }
  if (g->dest == D_BODY || g->fldrslt) {
    /* formatting changes at the group's end: pending text keeps the format it had */
  }
  r->depth--;
  /* the revision table: entries end with ';' (put_cp collects the names) */
}

/* the revision table's names come in as text with ';' between them */
static void revtbl_text(R *r)
{
  char *t = r->tb.data, *p;
  if (!t) return;
  while ((p = strchr(t, ';'))) {
    *p = 0;
    if (r->nrev >= r->revcap) {
      int nc = r->revcap ? r->revcap * 2 : 8;
      char (*na)[64] = (char (*)[64])realloc(r->revauth, (size_t)nc * 64);
      if (!na) { r->oom = 1; return; }
      r->revauth = na; r->revcap = nc;
    }
    while (*t == ' ') t++;
    strncpy(r->revauth[r->nrev], t, 63); r->revauth[r->nrev][63] = 0;
    r->nrev++;
    t = p + 1;
  }
  r->tb.len = 0; r->tb.data[0] = 0;
}

static int is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : (c | 0x20) >= 'a' && (c | 0x20) <= 'f' ? (c | 0x20) - 'a' + 10 : -1; }

static void parse(R *r)
{
  int star = 0;
  while (r->i < r->n && !r->oom) {
    u8 c = r->s[r->i];
    GState *g = &r->g[r->depth < MAXG ? r->depth : MAXG - 1];
    if (c == '{') { r->i++; group_open(r); star = 0; r->skip = 0; continue; }
    if (c == '}') {
      r->i++;
      if (g->dest == D_REVTBL) revtbl_text(r);
      group_close(r); star = 0; r->skip = 0;
      continue;
    }
    if (r->depth >= MAXG) { r->i += c == '\\' ? 2 : 1; continue; }
    if (c == '\\') {
      char w[40];
      int k = 0, has = 0, neg = 0;
      long v = 0;
      r->i++;
      if (r->i >= r->n) break;
      c = r->s[r->i];
      if (!is_alpha(c)) {
        r->i++;
        switch (c) {
        case '\'':
          if (r->i + 1 < r->n && hexv(r->s[r->i]) >= 0 && hexv(r->s[r->i + 1]) >= 0) {
            u8 b = (u8)(hexv(r->s[r->i]) * 16 + hexv(r->s[r->i + 1]));
            r->i += 2;
            if (g->dest == D_SKIP || g->dest == D_PICT) break;
            put_byte(r, b);
          }
          break;
        case '*': star = 1; continue;
        case '~': if (g->dest != D_SKIP) put_cp(r, 0xA0); break;
        case '-': if (g->dest != D_SKIP) put_cp(r, 0xAD); break;
        case '_': if (g->dest != D_SKIP) put_cp(r, 0x2011); break;
        case '\n': case '\r': if (g->dest == D_BODY) para_end(r); break;
        case '\\': case '{': case '}': if (g->dest != D_SKIP && g->dest != D_PICT) put_cp(r, c); break;
        case '|': case ':': break;
        case '\t': if (g->dest != D_SKIP) put_cp(r, '\t'); break;
        default: break;
        }
        star = 0;
        continue;
      }
      while (r->i < r->n && is_alpha(r->s[r->i]) && k < (int)sizeof w - 1) w[k++] = (char)r->s[r->i++];
      while (r->i < r->n && is_alpha(r->s[r->i])) r->i++;
      w[k] = 0;
      if (r->i < r->n && r->s[r->i] == '-' && r->i + 1 < r->n && r->s[r->i + 1] >= '0' && r->s[r->i + 1] <= '9') { neg = 1; r->i++; }
      while (r->i < r->n && r->s[r->i] >= '0' && r->s[r->i] <= '9') { has = 1; if (v < 100000000L) v = v * 10 + (r->s[r->i] - '0'); r->i++; }
      if (neg) v = -v;
      if (r->i < r->n && r->s[r->i] == ' ') r->i++;
      if (!strcmp(w, "bin")) {
        size_t nb = has && v > 0 ? (size_t)v : 0;
        if (nb > r->n - r->i) nb = r->n - r->i;
        if (g->dest == D_PICT) buf_add(&r->pict, r->s + r->i, nb);
        r->i += nb;
        if (r->skip > 0) r->skip--;
        star = 0;
        continue;
      }
      if (r->skip > 0 && strcmp(w, "u")) { r->skip--; star = 0; continue; }
      control(r, w, has, v, star);
      star = 0;
      continue;
    }
    r->i++;
    if (c == '\r' || c == '\n') continue;
    if (g->dest == D_PICT) {
      int h = hexv(c);
      if (h < 0) continue;
      if (r->hexhi < 0) r->hexhi = h;
      else { buf_addc(&r->pict, r->hexhi * 16 + h); r->hexhi = -1; }
      continue;
    }
    if (g->dest == D_SKIP) continue;
    if (c == '\t') { put_cp(r, '\t'); continue; }
    if (c < 32) continue;
    put_byte(r, c);
  }
}

/* the reader's state freed */
static void r_free(R *r)
{
  int k;
  while (r->nsto > 0) { pl_free(&r->sto[r->nsto - 1].l); para_free(r->sto[r->nsto - 1].cur); free(r->sto[r->nsto - 1].rows); r->nsto--; }
  for (k = 0; k < 2 * HF_N; k++) story_free(&r->sechf[k]);
  for (k = 0; k < r->nfld; k++) buf_free(&r->fld[k].inst);
  free(r->g); free(r->fonts); free(r->styles); free(r->stymap); free(r->rlists); free(r->revauth);
  buf_free(&r->pict); buf_free(&r->tb); buf_free(&r->pend);
}

static Doc *rtf_load(const u8 *data, size_t len, unsigned *lost, char *err, size_t errcap)
{
  R *r = (R *)calloc(1, sizeof(R));
  Doc *d;
  Sto *body;
  int k;
  unsigned dummy = 0;
  if (!lost) lost = &dummy;
  if (!r || !(r->g = (GState *)calloc(MAXG, sizeof(GState))) || !(d = doc_new())) {
    if (r) { free(r->g); free(r); }
    snprintf(err, errcap, "Not enough memory to open the file.");
    return NULL;
  }
  r->d = d; r->s = data; r->n = len; r->lost = lost;
  r->ansicpg = 1252;
  r->rc_rid = -1;
  buf_init(&r->pict); buf_init(&r->tb); buf_init(&r->pend);
  buf_adds(&r->tb, ""); buf_adds(&r->pend, "");
  /* RTF's defaults: 12 point text, no spacing, Letter paper with 1.25" side margins */
  d->defcf.size = 24;
  d->defpf.after = 0; d->defpf.before = 0;
  d->page.width = 12240; d->page.height = 15840;
  d->page.left = d->page.right = 1800; d->page.top = d->page.bottom = 1440;
  d->page.header = d->page.footer = 720;
  r->g[0].uc = 1; r->g[0].dest = D_BODY;
  plain(r, &r->g[0]); pard(r, &r->g[0]);
  sec_reset(r);
  r->nsto = 1; body = &r->sto[0]; memset(body, 0, sizeof *body); body->depth = -1;
  /* the body's default style is the document's Normal (index 0), whatever the file's style sheet says */
  r->g[0].pf.style = 0;
  parse(r);
  if (r->oom) { r_free(r); free(r); doc_free(d); snprintf(err, errcap, "Not enough memory to open the file."); return NULL; }
  /* fonts read after the defaults were set: the default font is \deff's */
  {
    int cp; u8 sym;
    int fi = font_of(r, r->deff, &cp, &sym);
    if (fi >= 0) d->defcf.font = (u8)fi;
  }
  while (r->nsto > 1) sto_pop(r);
  sto_close(r);
  /* the last section's settings are the document's */
  {
    SectFmt *s = &r->sec;
    ParaList *l = &body->l;
    d->page = r->secpage;
    d->titlepg = s->titlepg;
    d->evenodd = r->facingp;
    if (!d->nsects) s->start = SB_NEXT;          /* (how the only section starts doesn't matter) */
    if (l->np && !sec_plain(s)) {
      SectFmt f = *s;
      f.hf = NULL; f.haspage = 0;
      if ((k = doc_add_sect(d, &f))) l->p[l->np - 1]->pf.sect = (u16)k;
    }
    for (k = 0; k < d->nsects; k++) {
      SectFmt *t = &d->sects[k];
      if (t->haspage && !memcmp(&t->page, &d->page, sizeof d->page)) t->haspage = 0;
    }
    for (k = 0; k < HF_N; k++) {
      Story *h = &r->sechf[k], *f = &r->sechf[HF_N + k];
      story_free(&d->hdr[k]); story_free(&d->ftr[k]);
      d->hdr[k] = *h; d->ftr[k] = *f;
      h->p = NULL; h->np = 0; f->p = NULL; f->np = 0;
    }
    if (d->hdr[HF_EVEN].np || d->ftr[HF_EVEN].np) d->evenodd = 1;
  }
  if (!pl_to_body(d, &body->l)) { r_free(r); free(r); doc_free(d); snprintf(err, errcap, "Not enough memory to open the file."); return NULL; }
  if (r->nested_lost) *lost |= LOST_TABLES;
  r_free(r); free(r);
  /* paragraphs left with table 0xFFFF (rows never ended) are plain paragraphs */
  {
    u32 i;
    for (i = 0; i < d->np; i++) if (d->p[i]->pf.cell.table == 0xFFFF) memset(&d->p[i]->pf.cell, 0, sizeof(Cell));
    for (k = 0; k < d->nnotes; k++) { u32 j; for (j = 0; j < d->notes[k].np; j++) if (d->notes[k].p[j]->pf.cell.table == 0xFFFF) memset(&d->notes[k].p[j]->pf.cell, 0, sizeof(Cell)); }
  }
  doc_clear_undo(d);
  d->dirty = 0;
  return d;
}

static int rtf_sniff(const u8 *b, size_t n) { return n >= 5 && !memcmp(b, "{\\rtf", 5); }

/* ================================================================== */
/* writing */

typedef struct {
  Doc *d;
  Buf *b;
  u32 colors[256]; int ncolors;
  u16 bmark;                /* the bookmark open in the text written so far */
  int col;                  /* characters on the line (lines are broken near 200) */
} W;

static int color_index(W *w, u32 c)
{
  int i;
  if (c == COLOR_AUTO) return 0;
  c &= 0xFFFFFF;
  for (i = 1; i < w->ncolors; i++) if (w->colors[i] == c) return i;
  if (w->ncolors >= 256) return 0;
  w->colors[w->ncolors] = c;
  return w->ncolors++;
}
/* every colour the document uses, so the table comes first */
static void collect_cf(W *w, const CharFmt *f)
{
  if (f->color != COLOR_AUTO) color_index(w, f->color);
  if (f->hl & HL_ON) color_index(w, f->hl & 0xFFFFFF);
}
static void collect_para(W *w, const Para *p)
{
  u32 k;
  int e;
  for (k = 0; k < p->nr; k++) collect_cf(w, &p->r[k].f);
  if (p->pf.shade & HL_ON) color_index(w, p->pf.shade & 0xFFFFFF);
  for (e = 0; e < PE_N; e++) if (p->pf.bstyle[e] != PB_NONE && p->pf.bcolor[e] != COLOR_AUTO) color_index(w, p->pf.bcolor[e]);
  if (p->pf.cell.table) {
    if (p->pf.cell.shade != COLOR_AUTO) color_index(w, p->pf.cell.shade);
    for (e = 0; e < 4; e++) if ((p->pf.cell.borders & (1 << e)) && p->pf.cell.bcolor[e] != COLOR_AUTO && p->pf.cell.bcolor[e]) color_index(w, p->pf.cell.bcolor[e]);
  }
}
static void collect_story(W *w, const Story *s) { u32 i; for (i = 0; i < s->np; i++) collect_para(w, s->p[i]); }

static void wtext(W *w, const char *s, u32 n);
/* text: \ \{ \} escaped, tab and line break as words, anything past ASCII as \uN with a code page
   1252 fallback */
static void wtext(W *w, const char *s, u32 n)
{
  Buf *b = w->b;
  u32 i = 0;
  while (i < n) {
    int used;
    u32 c = utf8_get(s + i, n - i, &used);
    i += (u32)used;
    if (c == '\\' || c == '{' || c == '}') { buf_addc(b, '\\'); buf_addc(b, (int)c); w->col += 2; }
    else if (c == '\t') { buf_adds(b, "\\tab "); w->col += 5; }
    else if (c == '\v') { buf_adds(b, "\\line "); w->col += 6; }
    else if (c == 0xA0) { buf_adds(b, "\\~"); w->col += 2; }
    else if (c == 0xAD) { buf_adds(b, "\\-"); w->col += 2; }
    else if (c == 0x2011) { buf_adds(b, "\\_"); w->col += 2; }
    else if (c >= 32 && c < 128) { buf_addc(b, (int)c); w->col++; }
    else if (c < 32) continue;
    else {
      int fb = cp_from_unicode(1252, c);
      if (c > 0xFFFF) {
        u32 v = c - 0x10000;
        buf_printf(b, "\\u%d?\\u%d?", (int)(0xD800 + (v >> 10)) - 65536, (int)(0xDC00 + (v & 0x3FF)) - 65536);
      } else {
        int sv = c > 32767 ? (int)c - 65536 : (int)c;
        if (fb >= 0) buf_printf(b, "\\u%d\\'%02x", sv, fb);
        else buf_printf(b, "\\u%d?", sv);
      }
      w->col += 10;
    }
    if (w->col > 200 && c == ' ') { buf_addc(b, '\n'); w->col = 0; }
  }
}
/* a font's family word, for programs that don't have it */
static const char *family(const char *name)
{
  if (fmt_mono_font(name)) return "fmodern";
  if (strstr(name, "Times") || strstr(name, "Georgia") || strstr(name, "Cambria") || strstr(name, "Garamond") || strstr(name, "Book") ||
      strstr(name, "Serif") || strstr(name, "Roman") || strstr(name, "Palatino") || strstr(name, "Century")) return strstr(name, "Sans") ? "fswiss" : "froman";
  if (strstr(name, "Symbol") || strstr(name, "Wingdings")) return "ftech";
  return "fswiss";
}
static void dttm(char *out, const char *iso)
{
  int y = 0, mo = 0, dd = 0, h = 0, mi = 0;
  u32 v;
  out[0] = 0;
  if (sscanf(iso, "%d-%d-%dT%d:%d", &y, &mo, &dd, &h, &mi) < 3 || y < 1900) return;
  v = (u32)mi | ((u32)h << 6) | ((u32)dd << 11) | ((u32)mo << 16) | ((u32)(y - 1900) << 20);
  sprintf(out, "%lu", (unsigned long)v);
}
static void wcf(W *w, const CharFmt *f)
{
  Buf *b = w->b;
  if (f->cstyle && f->cstyle < w->d->nstyles && w->d->styles[f->cstyle].type == ST_CHAR) buf_printf(b, "\\cs%d", (int)f->cstyle);
  buf_printf(b, "\\f%d\\fs%d", f->font, f->size ? f->size : 24);
  if (f->flags & CF_BOLD) buf_adds(b, "\\b");
  if (f->flags & CF_ITALIC) buf_adds(b, "\\i");
  if (f->flags & CF_UNDERLINE) {
    static const char *const ul[UL_N] = { "\\ul", "\\uldb", "\\ulth", "\\uld", "\\uldash", "\\ulwave", "\\ulw" };
    buf_adds(b, f->ul < UL_N ? ul[f->ul] : "\\ul");
  }
  if (f->flags & CF_STRIKE) buf_adds(b, "\\strike");
  if (f->fx & FX_DSTRIKE) buf_adds(b, "\\striked1");
  if (f->flags & CF_SUPER) buf_adds(b, "\\super");
  if (f->flags & CF_SUB) buf_adds(b, "\\sub");
  if (f->flags & CF_CAPS) buf_adds(b, "\\caps");
  if (f->fx & FX_SMALLCAPS) buf_adds(b, "\\scaps");
  if (f->fx & FX_BORDER) buf_adds(b, "\\chbrdr\\brdrs\\brdrw10");
  if (f->color != COLOR_AUTO) buf_printf(b, "\\cf%d", color_index(w, f->color));
  if (f->hl & HL_ON) buf_printf(b, "\\highlight%d", color_index(w, f->hl & 0xFFFFFF));
  if (f->spacing) buf_printf(b, "\\expndtw%d", (int)f->spacing);
  if (f->scale && f->scale != 100) buf_printf(b, "\\charscalex%d", (int)f->scale);
  if (f->position > 0) buf_printf(b, "\\up%d", (int)f->position);
  if (f->position < 0) buf_printf(b, "\\dn%d", (int)-f->position);
  if (f->rev != REV_NONE) {
    char t[24];
    t[0] = 0;
    if (f->rid < w->d->nrevs) dttm(t, w->d->revs[f->rid].date);
    if (f->rev == REV_INS) buf_printf(b, "\\revised\\revauth%d", (int)f->rid + 1);
    else buf_printf(b, "\\deleted\\revauthdel%d", (int)f->rid + 1);
    if (t[0]) buf_printf(b, f->rev == REV_INS ? "\\revdttm%s" : "\\revdttmdel%s", t);
  }
  buf_addc(b, ' ');
}
static void wborder(W *w, const char *edge, int style, int width, u32 color, int space)
{
  static const char *const k[] = { "brdrnone", "brdrs", "brdrdb", "brdrth", "brdrdot", "brdrdash" };
  buf_printf(w->b, "\\%s\\%s\\brdrw%d", edge, k[style >= 0 && style <= PB_DASHED ? style : 1], (width ? width : 4) * 5 / 2);
  if (color != COLOR_AUTO && color) buf_printf(w->b, "\\brdrcf%d", color_index(w, color));
  else if (color == 0) buf_printf(w->b, "\\brdrcf%d", color_index(w, 0));
  if (space) buf_printf(w->b, "\\brsp%d", space * 20);
}
/* paragraph formatting after \pard (all of it but the table cell) */
static void wpf(W *w, const ParaFmt *p, int instyle)
{
  Buf *b = w->b;
  int k;
  static const char *const al[4] = { "\\ql", "\\qc", "\\qr", "\\qj" };
  if (!instyle) buf_printf(b, "\\s%d", p->style);
  buf_adds(b, al[p->align & 3]);
  /* (everything, zeros too: a style's settings would otherwise show through) */
  buf_printf(b, "\\li%ld\\lin%ld\\ri%ld\\rin%ld\\fi%ld\\sb%ld\\sa%ld", (long)p->left, (long)p->left, (long)p->right, (long)p->right,
             (long)p->first, (long)p->before, (long)p->after);
  if (p->linerule == LS_AUTO) buf_printf(b, "\\sl%ld\\slmult1", (long)(p->line ? p->line : 240));
  else if (p->linerule == LS_ATLEAST) buf_printf(b, "\\sl%ld\\slmult0", (long)p->line);
  else buf_printf(b, "\\sl-%ld\\slmult0", (long)p->line);
  if (p->keepnext) buf_adds(b, "\\keepn");
  if (p->keeplines) buf_adds(b, "\\keep");
  buf_adds(b, p->nowidow ? "\\nowidctlpar" : "\\widctlpar");
  if (!instyle && p->pagebreak) buf_adds(b, "\\pagebb");
  if (p->contextual) buf_adds(b, "\\contextualspace");
  for (k = 0; k < 8 && p->tabs[k]; k++) {
    static const char *const tk[4] = { "", "\\tqc", "\\tqr", "\\tqdec" };
    static const char *const tl[4] = { "", "\\tldot", "\\tlhyph", "\\tlul" };
    buf_printf(b, "%s%s\\tx%ld", tk[p->tabkind[k] & 3], tl[p->tablead[k] & 3], (long)p->tabs[k]);
  }
  {
    static const char *const edge[PE_N] = { "brdrt", "brdrl", "brdrb", "brdrr", "brdrbtw" };
    for (k = 0; k < PE_N; k++) if (p->bstyle[k] != PB_NONE) wborder(w, edge[k], p->bstyle[k], p->bwidth[k], p->bcolor[k] == 0 ? COLOR_AUTO : p->bcolor[k], p->bspace[k]);
  }
  if (p->shade & HL_ON) buf_printf(b, "\\cbpat%d", color_index(w, p->shade & 0xFFFFFF));
  if (p->list && p->list <= w->d->nlists) buf_printf(b, "\\ls%d\\ilvl%d", (int)p->list, (int)p->level);
  buf_addc(b, ' ');
}
static void wpara_runs(W *w, const Para *p, int note_ok);
static void wstory(W *w, const Story *s, int note_ok);
/* a picture's group */
static void wpict(W *w, const Image *im)
{
  Buf *b = w->b;
  u32 i;
  if (im->kind != OBJ_PICTURE || !im->data || !im->len || (im->type != IMG_PNG && im->type != IMG_JPEG)) return;
  buf_printf(b, "{\\*\\shppict{\\pict\\picscalex100\\picscaley100\\picw%lu\\pich%lu\\picwgoal%ld\\pichgoal%ld%s\n",
             (unsigned long)im->pw, (unsigned long)im->ph, (long)im->cx, (long)im->cy, im->type == IMG_PNG ? "\\pngblip" : "\\jpegblip");
  if (!buf_reserve(b, im->len * 2 + im->len / 64 + 8)) return;
  for (i = 0; i < im->len; i++) {
    static const char hx[] = "0123456789abcdef";
    b->data[b->len++] = hx[im->data[i] >> 4];
    b->data[b->len++] = hx[im->data[i] & 15];
    if ((i & 63) == 63) b->data[b->len++] = '\n';
  }
  b->data[b->len] = 0;
  buf_adds(b, "}}");
  w->col = 0;
}
static void wbmark(W *w, u16 to)
{
  if (to == w->bmark) return;
  if (w->bmark) { buf_adds(w->b, "{\\*\\bkmkend "); wtext(w, doc_bmark_name(w->d, w->bmark), (u32)strlen(doc_bmark_name(w->d, w->bmark))); buf_adds(w->b, "}"); }
  if (to) { buf_adds(w->b, "{\\*\\bkmkstart "); wtext(w, doc_bmark_name(w->d, to), (u32)strlen(doc_bmark_name(w->d, to))); buf_adds(w->b, "}"); }
  w->bmark = to;
}
/* a paragraph's runs: links as HYPERLINK fields, pictures, notes and Quill's page fields as theirs */
static void wpara_runs(W *w, const Para *p, int note_ok)
{
  Buf *b = w->b;
  u32 k, start = 0;
  u16 link = 0;
  for (k = 0; k < p->nr; k++) {
    const CharFmt *f = &p->r[k].f;
    u32 end = p->r[k].end, i = start, j;
    u16 want = f->link;
    if (end > start && IS_FIELD(p->t + start) && end - start >= 3 && (u8)p->t[start + 2] == 0x82) want = 0;   /* a PAGEREF's link names its bookmark */
    if (want != link) {
      if (link) buf_adds(b, "}}}");
      link = want;
      if (link) {
        const char *u = doc_link_url(w->d, link);
        buf_adds(b, "{\\field{\\*\\fldinst {HYPERLINK ");
        if (u[0] == '#') { buf_adds(b, "\\\\l \""); wtext(w, u + 1, (u32)strlen(u + 1)); }
        else { buf_addc(b, '"'); wtext(w, u, (u32)strlen(u)); }
        buf_adds(b, "\" }}{\\fldrslt {");
      }
    }
    wbmark(w, f->bmark);
    while (i < end) {
      /* plain text up to the next special character */
      for (j = i; j < end; j++) {
        u8 c = (u8)p->t[j];
        if ((c == 0xF3 || c == 0xF4) && j + 4 <= end && (IS_PIC(p->t + j) || IS_NOTE(p->t + j) || IS_ENOTE(p->t + j) || IS_MARK(p->t + j))) break;
        if (c == 0xEE && j + 3 <= end && IS_FIELD(p->t + j)) break;
      }
      if (j > i) { buf_adds(b, "{\\plain"); wcf(w, f); wtext(w, p->t + i, j - i); buf_addc(b, '}'); }
      i = j;
      if (i >= end) break;
      if (IS_FIELD(p->t + i)) {
        u8 kind = (u8)p->t[i + 2];
        buf_adds(b, "{\\field{\\*\\fldinst {\\plain"); wcf(w, f);
        if (kind == 0x80) buf_adds(b, " PAGE }}{\\fldrslt {\\plain"), wcf(w, f), buf_adds(b, "1}}}");
        else if (kind == 0x81) buf_adds(b, " NUMPAGES }}{\\fldrslt {\\plain"), wcf(w, f), buf_adds(b, "1}}}");
        else {
          const char *u = doc_link_url(w->d, f->link);
          buf_adds(b, " PAGEREF ");
          wtext(w, u[0] == '#' ? u + 1 : u, (u32)strlen(u[0] == '#' ? u + 1 : u));
          buf_adds(b, " \\\\h }}{\\fldrslt {\\plain"); wcf(w, f); buf_adds(b, "1}}}");
        }
        i += 3;
        continue;
      }
      if (IS_PIC(p->t + i)) {
        int n = pic_index(p->t + i);
        if (n >= 0 && n < w->d->nimages) { buf_adds(b, "{\\plain"); wcf(w, f); wpict(w, &w->d->images[n]); buf_addc(b, '}'); }
        i += 4;
        continue;
      }
      if (IS_NOTE(p->t + i) || IS_ENOTE(p->t + i)) {
        int n = note_index(p->t + i), en = IS_ENOTE(p->t + i);
        const Story *s = en ? (n >= 0 && n < w->d->nenotes ? &w->d->enotes[n] : NULL) : (n >= 0 && n < w->d->nnotes ? &w->d->notes[n] : NULL);
        i += 4;
        if (!s || !note_ok) continue;
        buf_adds(b, "{\\plain"); wcf(w, f); buf_adds(b, "\\super\\chftn}");
        buf_printf(b, "{\\footnote%s\\pard\\plain ", en ? "\\ftnalt" : "");
        if (!s->np) buf_adds(b, "{\\super\\chftn} ");
        else {
          u32 q;
          for (q = 0; q < s->np; q++) {
            const Para *np = s->p[q];
            buf_adds(b, "\\pard\\plain "); wpf(w, &np->pf, 0);
            if (q == 0) { buf_adds(b, "{\\plain"); wcf(w, &np->r[0].f); buf_adds(b, "\\super\\chftn}{ }"); }
            wpara_runs(w, np, 0);
            if (q + 1 < s->np) buf_adds(b, "\\par\n");
          }
        }
        buf_adds(b, "}");
        continue;
      }
      i += 4;                    /* a comment's mark: left out */
    }
    start = end;
  }
  if (link) buf_adds(b, "}}}");
}
/* a story's paragraphs (a header, a footer: no tables, sections or notes) */
static void wstory(W *w, const Story *s, int note_ok)
{
  u32 i;
  for (i = 0; i < s->np; i++) {
    const Para *p = s->p[i];
    buf_adds(w->b, "\\pard\\plain "); wpf(w, &p->pf, 0);
    wpara_runs(w, p, note_ok);
    { const CharFmt *f = &p->r[p->nr - 1].f; buf_adds(w->b, "\\plain"); wcf(w, f); }
    buf_adds(w->b, i + 1 < s->np ? "\\par\n" : "\\par}\n");
  }
}
static void wcell_borders(W *w, const Cell *c)
{
  static const char *const e[4] = { "clbrdrt", "clbrdrl", "clbrdrb", "clbrdrr" };
  int k;
  for (k = 0; k < 4; k++)
    if (c->borders & (1 << k)) wborder(w, e[k], c->bstyle[k] ? c->bstyle[k] : PB_SINGLE, c->bwidth[k], c->bcolor[k], 0);
}
/* a table row's definition: the cells from paragraphs p[a..b) */
static void wrow(W *w, Para **p, u32 a, u32 b, const Table *t)
{
  Buf *bf = w->b;
  u32 i;
  i32 x;
  const Cell *c0 = &p[a]->pf.cell;
  buf_printf(bf, "\\trowd\\irow%u\\trgaph%ld\\trleft%ld", (unsigned)c0->row, (long)t->marl, (long)t->indent);
  if (t->jc == AL_CENTER) buf_adds(bf, "\\trqc");
  else if (t->jc == AL_RIGHT) buf_adds(bf, "\\trqr");
  if (c0->rowh) buf_printf(bf, "\\trrh%d", (int)c0->rowh);
  if (c0->header) buf_adds(bf, "\\trhdr");
  if (c0->cantsplit) buf_adds(bf, "\\trkeep");
  buf_printf(bf, "\\trpaddl%ld\\trpaddr%ld\\trpaddfl3\\trpaddfr3", (long)t->marl, (long)t->marr);
  if (t->autofit) buf_adds(bf, "\\trautofit1");
  for (i = a; i < b; i++) {
    const Cell *c = &p[i]->pf.cell;
    int k;
    if (i > a && p[i - 1]->pf.cell.col == c->col) continue;
    if (c->vmerge == VM_START) buf_adds(bf, "\\clvmgf");
    if (c->vmerge == VM_CONT) buf_adds(bf, "\\clvmrg");
    if (c->valign == 1) buf_adds(bf, "\\clvertalc");
    else if (c->valign == 2) buf_adds(bf, "\\clvertalb");
    else buf_adds(bf, "\\clvertalt");
    wcell_borders(w, c);
    if (c->shade != COLOR_AUTO) buf_printf(bf, "\\clcbpat%d", color_index(w, c->shade));
    x = t->indent;
    for (k = 0; k < c->col + (c->span ? c->span : 1) && k < t->ncols; k++) x += t->grid[k];
    {
      i32 wd = 0;
      for (k = c->col; k < c->col + (c->span ? c->span : 1) && k < t->ncols; k++) wd += t->grid[k];
      buf_printf(bf, "\\clwWidth%ld\\clftsWidth3\\cellx%ld\n", (long)wd, (long)x);
    }
  }
}
/* the body or a story with tables: paragraphs from a to b */
static void wbody(W *w, Para **p, u32 a, u32 b)
{
  Buf *bf = w->b;
  u32 i;
  Doc *d = w->d;
  for (i = a; i < b; i++) {
    const Para *q = p[i];
    const Cell *c = top_cell(&q->pf);
    int intbl = c->table != 0;
    int lastcell = 0, lastrow = 0;
    if (intbl) {
      const Table *t = c->table <= d->ntables ? &d->tables[c->table - 1] : NULL;
      const Cell *pc = i > a ? top_cell(&p[i - 1]->pf) : NULL;
      int rowstart = !pc || pc->table != c->table || pc->row != c->row;
      if (rowstart && t) {
        u32 e = i;
        while (e < b && top_cell(&p[e]->pf)->table == c->table && top_cell(&p[e]->pf)->row == c->row) e++;
        {
          /* the row's cells, one paragraph per cell for the definition */
          Para **cells = (Para **)malloc((e - i) * sizeof(Para *));
          u32 k, n = 0;
          if (cells) {
            for (k = i; k < e; k++) cells[n++] = p[k];
            {
              /* the definition wants each cell's own Cell: the outer one for nested paragraphs */
              u32 m;
              Para **cp = (Para **)malloc(n * sizeof(Para *));
              Para *fake = (Para *)malloc(n * sizeof(Para));
              if (cp && fake) {
                for (m = 0; m < n; m++) { fake[m] = *cells[m]; fake[m].pf.cell = *top_cell(&cells[m]->pf); cp[m] = &fake[m]; }
                wrow(w, cp, 0, n, t);
              }
              free(cp); free(fake);
            }
            free(cells);
          }
        }
      }
      {
        const Cell *nc = i + 1 < b ? top_cell(&p[i + 1]->pf) : NULL;
        lastcell = !nc || nc->table != c->table || nc->row != c->row || nc->col != c->col;
        lastrow = !nc || nc->table != c->table || nc->row != c->row;
      }
    }
    buf_adds(bf, "\\pard\\plain ");
    if (intbl) buf_adds(bf, "\\intbl");
    wpf(w, &q->pf, 0);
    if (q->pf.colbreak && !intbl) buf_adds(bf, "\\column ");
    wpara_runs(w, q, 1);
    buf_adds(bf, "\\plain");
    wcf(w, &q->r[q->nr - 1].f);
    if (intbl) {
      buf_adds(bf, lastcell ? "\\cell " : "\\par ");
      if (lastrow) buf_adds(bf, "\\row ");
      buf_addc(bf, '\n');
    } else buf_adds(bf, "\\par\n");
    w->col = 0;
  }
}
static void wsect(W *w, const SectFmt *s, int last)
{
  Buf *b = w->b;
  Doc *d = w->d;
  const PageSetup *pg;
  static const char *const sb[4] = { "\\sbkpage", "\\sbknone", "\\sbkeven", "\\sbkodd" };
  int k;
  SectFmt def;
  sect_default(&def);
  if (!s) s = &def;
  pg = sect_page(d, s, last);
  buf_printf(b, "\\sectd%s", sb[s->start & 3]);
  buf_printf(b, "\\pgwsxn%ld\\pghsxn%ld\\marglsxn%ld\\margrsxn%ld\\margtsxn%ld\\margbsxn%ld\\headery%ld\\footery%ld",
             (long)pg->width, (long)pg->height, (long)pg->left, (long)pg->right, (long)pg->top, (long)pg->bottom, (long)pg->header, (long)pg->footer);
  if (pg->width > pg->height) buf_adds(b, "\\lndscpsxn");
  if (s->cols > 1) buf_printf(b, "\\cols%d\\colsx%ld", s->cols, (long)s->colgap);
  if (s->colline) buf_adds(b, "\\linebetcol");
  if (s->valign == VA_CENTER) buf_adds(b, "\\vertalc");
  else if (s->valign == VA_BOTTOM) buf_adds(b, "\\vertalb");
  if (s->linenum != LN_NONE) {
    buf_printf(b, "\\linemod%d\\linex%ld", s->lnby ? s->lnby : 1, (long)s->lndist);
    if (s->lnstart > 1) buf_printf(b, "\\linestarts%d", (int)s->lnstart);
    buf_adds(b, s->linenum == LN_PAGE ? "\\lineppage" : s->linenum == LN_SECTION ? "\\linerestart" : "\\linecont");
  }
  if (sect_titlepg(d, s, last)) buf_adds(b, "\\titlepg");
  if (d->pgstart > 1) buf_printf(b, "\\pgnstarts%d\\pgnrestart", (int)d->pgstart);
  switch (d->pgfmt) {
  case NF_UPPER_ROMAN: buf_adds(b, "\\pgnucrm"); break;
  case NF_LOWER_ROMAN: buf_adds(b, "\\pgnlcrm"); break;
  case NF_UPPER_LETTER: buf_adds(b, "\\pgnucltr"); break;
  case NF_LOWER_LETTER: buf_adds(b, "\\pgnlcltr"); break;
  default: buf_adds(b, "\\pgndec");
  }
  buf_addc(b, '\n');
  for (k = 0; k < 2 * HF_N; k++) {
    int footer = k >= HF_N, which = k % HF_N;
    Story *st = sect_story(d, s, last, footer, which);
    const char *name;
    if (!st || !st->np) continue;
    if (which == HF_FIRST) name = footer ? "footerf" : "headerf";
    else if (which == HF_EVEN) name = footer ? "footerl" : "headerl";
    else name = d->evenodd ? (footer ? "footerr" : "headerr") : (footer ? "footer" : "header");
    buf_printf(b, "{\\%s ", name);
    wstory(w, st, 0);
  }
}

static void wlevel_text(W *w, const ListLevel *lv)
{
  /* "%1." -> \'02\'00. : the length, then each character, levels as \'0n */
  char out[200];
  int n = 0;
  const char *t = lv->text;
  Buf *b = w->b;
  size_t o = 0;
  out[0] = 0;
  while (*t && o < sizeof out - 16) {
    if (t[0] == '%' && t[1] >= '1' && t[1] <= '9') { o += (size_t)sprintf(out + o, "\\'%02x", t[1] - '1'); t += 2; n++; continue; }
    {
      int used;
      u32 c = utf8_get(t, strlen(t), &used);
      t += used;
      if (c == '\\' || c == '{' || c == '}' || c == ';') o += (size_t)sprintf(out + o, "\\'%02x", (int)c);
      else if (c >= 32 && c < 128) out[o++] = (char)c, out[o] = 0;
      else o += (size_t)sprintf(out + o, "\\u%d ?", c > 32767 ? (int)c - 65536 : (int)c);
      n++;
    }
  }
  out[o] = 0;
  if (lv->fmt == NF_NONE) { n = 0; out[0] = 0; }
  buf_printf(b, "{\\leveltext\\'%02x%s;}", n, out);
  {
    /* \levelnumbers: where the level placeholders are (1-based positions in the text) */
    int pos = 1;
    t = lv->text;
    buf_adds(b, "{\\levelnumbers");
    while (*t && lv->fmt != NF_NONE) {
      if (t[0] == '%' && t[1] >= '1' && t[1] <= '9') { buf_printf(b, "\\'%02x", pos); t += 2; pos++; continue; }
      { int used; utf8_get(t, strlen(t), &used); t += used; pos++; }
    }
    buf_adds(b, ";}");
  }
}
static void wlists(W *w)
{
  Doc *d = w->d;
  Buf *b = w->b;
  int i, k;
  if (!d->nlists) return;
  buf_adds(b, "{\\*\\listtable\n");
  for (i = 0; i < d->nlists; i++) {
    buf_printf(b, "{\\list\\listtemplateid%d\\listhybrid\n", 1000 + i);
    for (k = 0; k < 9; k++) {
      const ListLevel *lv = &d->lists[i].lvl[k];
      int nfc;
      switch (lv->fmt) {
      case NF_BULLET: nfc = 23; break;
      case NF_UPPER_ROMAN: nfc = 1; break;
      case NF_LOWER_ROMAN: nfc = 2; break;
      case NF_UPPER_LETTER: nfc = 3; break;
      case NF_LOWER_LETTER: nfc = 4; break;
      case NF_NONE: nfc = 255; break;
      default: nfc = 0;
      }
      buf_printf(b, "{\\listlevel\\levelnfc%d\\levelnfcn%d\\leveljc0\\leveljcn0\\levelfollow0\\levelstartat%d", nfc, nfc, (int)lv->start);
      wlevel_text(w, lv);
      if (lv->font[0] && !strstr(lv->font, "Symbol") && !strstr(lv->font, "Wingdings")) {
        int f;
        for (f = 0; f < d->nfonts && strcmp(d->fonts[f], lv->font); f++) ;
        if (f < d->nfonts) buf_printf(b, "\\f%d", f);
      }
      buf_printf(b, "\\fi%ld\\li%ld\\lin%ld}\n", (long)-lv->hanging, (long)lv->left, (long)lv->left);
    }
    buf_printf(b, "{\\listname ;}\\listid%d}\n", i + 1);
  }
  buf_adds(b, "}\n{\\*\\listoverridetable\n");
  for (i = 0; i < d->nlists; i++) buf_printf(b, "{\\listoverride\\listid%d\\listoverridecount0\\ls%d}\n", i + 1, i + 1);
  buf_adds(b, "}\n");
}
static void wstyles(W *w)
{
  Doc *d = w->d;
  Buf *b = w->b;
  int k;
  buf_adds(b, "{\\stylesheet\n");
  for (k = 0; k < d->nstyles; k++) {
    const Style *s = &d->styles[k];
    if (s->type == ST_GONE) continue;
    if (s->type == ST_PARA) {
      buf_printf(b, "{\\s%d", k);
      wpf(w, &s->pf, 1);
      buf_adds(b, "\\plain");
      wcf(w, &s->cf);
      /* what it turns off, so its base doesn't show through */
      if (!(s->cf.flags & CF_BOLD)) buf_adds(b, "\\b0");
      if (!(s->cf.flags & CF_ITALIC)) buf_adds(b, "\\i0");
      if (!(s->cf.flags & CF_UNDERLINE)) buf_adds(b, "\\ulnone");
      if (!(s->cf.flags & CF_STRIKE)) buf_adds(b, "\\strike0");
      if (!(s->cf.flags & CF_CAPS)) buf_adds(b, "\\caps0");
      if (!(s->cf.fx & FX_SMALLCAPS)) buf_adds(b, "\\scaps0");
      if (!(s->cf.flags & (CF_SUPER | CF_SUB))) buf_adds(b, "\\nosupersub");
      if (s->cf.color == COLOR_AUTO) buf_adds(b, "\\cf0");
    } else {
      CharFmt f = s->cf;
      buf_printf(b, "{\\*\\cs%d\\additive", k);
      /* a character style: only what it sets */
      if (s->ecmask & (CM_FONT)) buf_printf(b, "\\f%d", f.font);
      if (s->ecmask & CM_SIZE) buf_printf(b, "\\fs%d", f.size);
      if ((s->ecmask & CM_BOLD) && (f.flags & CF_BOLD)) buf_adds(b, "\\b");
      if ((s->ecmask & CM_ITALIC) && (f.flags & CF_ITALIC)) buf_adds(b, "\\i");
      if ((s->ecmask & CM_UNDERLINE) && (f.flags & CF_UNDERLINE)) buf_adds(b, "\\ul");
      if ((s->ecmask & CM_STRIKE) && (f.flags & CF_STRIKE)) buf_adds(b, "\\strike");
      if ((s->ecmask & CM_CAPS) && (f.flags & CF_CAPS)) buf_adds(b, "\\caps");
      if ((s->ecmask & CM_SMALLCAPS) && (f.fx & FX_SMALLCAPS)) buf_adds(b, "\\scaps");
      if ((s->ecmask & CM_SUPER) && (f.flags & CF_SUPER)) buf_adds(b, "\\super");
      if ((s->ecmask & CM_SUB) && (f.flags & CF_SUB)) buf_adds(b, "\\sub");
      if ((s->ecmask & CM_COLOR) && f.color != COLOR_AUTO) buf_printf(b, "\\cf%d", color_index(w, f.color));
      if ((s->ecmask & CM_HL) && (f.hl & HL_ON)) buf_printf(b, "\\highlight%d", color_index(w, f.hl & 0xFFFFFF));
      buf_addc(b, ' ');
    }
    if (s->based != NO_STYLE && s->based < d->nstyles) buf_printf(b, "\\sbasedon%d", s->based);
    if (s->type == ST_PARA) buf_printf(b, "\\snext%d", s->next < d->nstyles ? s->next : k);
    if (s->quick) buf_adds(b, "\\sqformat");
    buf_addc(b, ' ');
    wtext(w, s->name, (u32)strlen(s->name));
    buf_adds(b, ";}\n");
  }
  buf_adds(b, "}\n");
}

static u8 *rtf_save(Doc *d, size_t *len)
{
  Buf b, body;
  W w;
  int k;
  u32 i, a;
  memset(&w, 0, sizeof w);
  w.d = d; w.ncolors = 1;
  buf_init(&b); buf_init(&body);
  /* colours first (the body may add none after this: they're all collected) */
  for (i = 0; i < d->np; i++) collect_para(&w, d->p[i]);
  for (k = 0; k < HF_N; k++) { collect_story(&w, &d->hdr[k]); collect_story(&w, &d->ftr[k]); }
  for (k = 0; k < d->nsects; k++) if (d->sects[k].hf) { int j; for (j = 0; j < 2 * HF_N; j++) collect_story(&w, &d->sects[k].hf[j]); }
  for (k = 0; k < d->nnotes; k++) collect_story(&w, &d->notes[k]);
  for (k = 0; k < d->nenotes; k++) collect_story(&w, &d->enotes[k]);
  for (k = 0; k < d->nstyles; k++) { collect_cf(&w, &d->styles[k].cf); if (d->styles[k].pf.shade & HL_ON) color_index(&w, d->styles[k].pf.shade & 0xFFFFFF); }
  color_index(&w, 0);
  /* the body into its own buffer, then the tables that come before it */
  w.b = &body;
  buf_printf(&body, "\\paperw%ld\\paperh%ld\\margl%ld\\margr%ld\\margt%ld\\margb%ld%s%s\\deftab720\\widowctrl\\ftnbj\\aenddoc\\fet%d\\viewkind1\n",
             (long)d->page.width, (long)d->page.height, (long)d->page.left, (long)d->page.right, (long)d->page.top, (long)d->page.bottom,
             d->evenodd ? "\\facingp" : "", d->page.width > d->page.height ? "\\landscape" : "", d->nenotes ? 2 : 0);
  a = 0;
  for (i = 0; i < d->np; i++) {
    int last = i + 1 >= d->np;
    if (d->p[i]->pf.sect || last) {
      wsect(&w, d->p[i]->pf.sect && d->p[i]->pf.sect <= d->nsects ? &d->sects[d->p[i]->pf.sect - 1] : NULL, last);
      wbody(&w, d->p, a, i + 1);
      if (!last) buf_adds(&body, "\\sect\n");
      a = i + 1;
    }
  }
  if (!d->np) wsect(&w, NULL, 1);
  wbmark(&w, 0);
  /* the header */
  w.b = &b;
  buf_adds(&b, "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n{\\fonttbl");
  for (k = 0; k < d->nfonts; k++) {
    buf_printf(&b, "{\\f%d\\%s\\fcharset%d ", k, family(d->fonts[k]), strstr(d->fonts[k], "Symbol") ? 2 : 0);
    wtext(&w, d->fonts[k], (u32)strlen(d->fonts[k]));
    buf_adds(&b, ";}");
  }
  if (!d->nfonts) buf_adds(&b, "{\\f0\\froman\\fcharset0 Times New Roman;}");
  buf_adds(&b, "}\n{\\colortbl;");
  for (k = 1; k < w.ncolors; k++) buf_printf(&b, "\\red%lu\\green%lu\\blue%lu;", (unsigned long)(w.colors[k] >> 16), (unsigned long)((w.colors[k] >> 8) & 255), (unsigned long)(w.colors[k] & 255));
  buf_adds(&b, "}\n");
  if (d->nrevs) {
    buf_adds(&b, "{\\*\\revtbl {Unknown;}");
    for (k = 0; k < d->nrevs; k++) { buf_addc(&b, '{'); wtext(&w, d->revs[k].author, (u32)strlen(d->revs[k].author)); buf_adds(&b, ";}"); }
    buf_adds(&b, "}\n");
  }
  {
    /* (the style sheet's colours were collected above, so its \cf numbers are in the table) */
    Buf st;
    buf_init(&st);
    w.b = &st;
    wstyles(&w);
    wlists(&w);
    w.b = &b;
    buf_add(&b, st.data, st.len);
    buf_free(&st);
  }
  if (d->title[0] || d->author[0]) {
    buf_adds(&b, "{\\info");
    if (d->title[0]) { buf_adds(&b, "{\\title "); wtext(&w, d->title, (u32)strlen(d->title)); buf_addc(&b, '}'); }
    if (d->author[0]) { buf_adds(&b, "{\\author "); wtext(&w, d->author, (u32)strlen(d->author)); buf_addc(&b, '}'); }
    buf_adds(&b, "}\n");
  }
  buf_add(&b, body.data, body.len);
  buf_adds(&b, "}\n");
  buf_free(&body);
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}

const DocFormat fmt_rtf = { "Rich Text Format", "rtf", rtf_sniff, rtf_load, rtf_save, 0 };
