/* tblquick.c - see tblquick.h (WP21a) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tblquick.h"
#include "table.h"
#include "tstyle.h"

/* the gallery style with this id: its number in d, 0 if none */
static int gallery(Doc *d, const char *id)
{
  int k;
  for (k = 0; k < tstyle_builtin_count(); k++) if (!strcmp(tstyle_builtin(k)->id, id)) return tstyle_use_builtin(d, k);
  return 0;
}
/* the cell at row, col of the table the caret is in: its paragraph as a position */
static Pos cellpos(Doc *d, Pos in, int r, int c)
{
  Pos p;
  u32 k = tbl_cell_para(d, in, r, c);
  p.para = k == (u32)-1 ? in.para : k; p.off = 0;
  return p;
}
/* text into the cell at row, col (empty text: nothing), bold if asked, aligned (AL_, -1 as it is) */
static void put(Doc *d, Pos in, int r, int c, const char *s, int bold, int align)
{
  Pos p = cellpos(d, in, r, c);
  if (s && *s) {
    CharFmt f = *para_fmt_at(d->p[p.para], 0);
    Pos q = p;
    if (bold) f.flags |= CF_BOLD;
    doc_insert(d, &q, s, (u32)strlen(s), &f);
  }
  if (align >= 0) {
    ParaFmt pf;
    memset(&pf, 0, sizeof pf);
    pf.align = (u8)align;
    doc_para_format(d, p.para, p.para, PM_ALIGN, &pf);
  }
}
static int is_leap(int y) { return (y % 4 == 0 && y % 100) || y % 400 == 0; }
int month_days(int y, int m)
{
  static const int n[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (m < 1 || m > 12) return 30;
  return n[m - 1] + (m == 2 && is_leap(y));
}
/* Sakamoto's method: 0 Sunday, turned to 0 Monday */
int date_wday(int y, int m, int d)
{
  static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
  int w;
  if (m < 3) y--;
  w = (y + y / 4 - y / 100 + y / 400 + t[(m - 1) % 12] + d) % 7;
  return (w + 6) % 7;
}
/* the ISO week number of a date */
static int iso_week(int y, int m, int d)
{
  int doy = d, k, wd = date_wday(y, m, d), w;
  for (k = 1; k < m; k++) doy += month_days(y, k);
  w = (doy - wd + 9) / 7;                       /* (doy - (wd + 1) + 10) / 7 */
  if (w < 1) {
    int py = y - 1, pdoy = 365 + is_leap(py), pwd = date_wday(py, 12, 31);
    w = (pdoy - pwd + 9) / 7;
  } else if (w == 53) {
    int jan1 = date_wday(y + 1, 1, 1);
    if (jan1 <= 3) w = 1;                        /* the week belongs to next year */
  }
  return w;
}
static const char *W(const char *s, const char *en) { return s && *s ? s : en; }

int tbl_quick(Doc *d, Pos *at, int kind, int year, int month, const QuickWords *w, i32 width)
{
  static const char *const months[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
  static const char *const days[7] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
  QuickWords none;
  int rows = 0, cols = 0, r, c, ts = 0;
  u16 look = LK_FIRSTROW | LK_NOVBAND;
  Pos in;
  char s[96];
  if (!w) { memset(&none, 0, sizeof none); w = &none; }
  if (month < 1 || month > 12) month = 1;
  if (year < 1600 || year > 9999) year = 2026;
  switch (kind) {
  case QT_CAL1: case QT_CAL2: case QT_CAL3: {
    int first = date_wday(year, month, 1), nd = month_days(year, month), lead = kind == QT_CAL2 ? (first + 1) % 7 : first;
    rows = 2 + (lead + nd + 6) / 7; cols = kind == QT_CAL3 ? 8 : 7; break; }
  case QT_DOUBLE: rows = 6; cols = 5; break;
  case QT_MATRIX: rows = 5; cols = 5; break;
  case QT_TABULAR: rows = 7; cols = 2; break;
  case QT_SUBHEADS1: case QT_SUBHEADS2: rows = 9; cols = 4; break;
  default: return 0;
  }
  doc_begin(d, *at);
  if (!tbl_insert(d, at, rows, cols, width)) { doc_end(d, *at); return 0; }
  in = *at;
  switch (kind) {
  case QT_CAL1: case QT_CAL2: case QT_CAL3: {
    /* the month over the top, the days' names, then the weeks (Sunday first in the second) */
    int first = date_wday(year, month, 1), nd = month_days(year, month), sun = kind == QT_CAL2, wk = kind == QT_CAL3;
    int lead = sun ? (first + 1) % 7 : first, day;
    ts = gallery(d, kind == QT_CAL1 ? "QuillGridBlue" : kind == QT_CAL2 ? "QuillBandedTeal" : "QuillLines");
    look = kind == QT_CAL2 ? (LK_FIRSTROW | LK_NOVBAND) : LK_FIRSTROW | LK_NOVBAND | LK_NOHBAND;
    if (ts) tbl_apply_style(d, &in, ts, look);
    for (c = 0; c < 7; c++) put(d, in, 1, c + wk, W(w->wday[sun ? (c + 6) % 7 : c], days[sun ? (c + 6) % 7 : c]), 1, AL_CENTER);
    if (wk) put(d, in, 1, 0, W(w->week, "Wk"), 1, AL_CENTER);
    for (day = 1; day <= nd; day++) {
      int cell = lead + day - 1;
      sprintf(s, "%d", day);
      put(d, in, 2 + cell / 7, wk + cell % 7, s, 0, AL_RIGHT);
      if (wk && (cell % 7 == 0 || day == 1)) { sprintf(s, "%d", iso_week(year, month, day)); put(d, in, 2 + cell / 7, 0, s, 0, AL_CENTER); }
    }
    /* the title last: merging the first row moves the cells' paragraphs */
    sprintf(s, "%.60s %d", W(w->month[month - 1], months[month - 1]), year);
    {
      Pos a = cellpos(d, in, 0, 0), b = cellpos(d, in, 0, cols - 1);
      tbl_merge(d, a, b);
      in = cellpos(d, a, 0, 0);
      put(d, in, 0, 0, s, 1, AL_CENTER);
      { CharFmt f; Pos e = in; memset(&f, 0, sizeof f); f.size = 28; e.off = d->p[in.para]->len; doc_format(d, in, e, CM_SIZE, &f); }
    }
    break; }
  case QT_DOUBLE: {
    /* two lists side by side, a narrow gap between */
    static const int amounts[2][4] = { { 120, 85, 40, 15 }, { 64, 32, 21, 9 } };
    int half, k;
    Table *t, nt;
    ts = gallery(d, "QuillList");
    look = LK_FIRSTROW | LK_LASTROW | LK_NOVBAND | LK_NOHBAND;
    if (ts) tbl_apply_style(d, &in, ts, look);
    if ((t = doc_table(d, &d->p[in.para]->pf))) {           /* (after the style: adding tables moves them) */
      nt = *t;
      for (k = 0; k < 5; k++) nt.grid[k] = k == 2 ? width / 20 : (width - width / 20) / 4;
      tbl_set_table(d, &in, &nt);
    }
    for (half = 0; half < 2; half++) {
      int c0 = half * 3, tot = 0;
      put(d, in, 0, c0, W(w->item, "Item"), 0, -1);
      put(d, in, 0, c0 + 1, W(w->amount, "Amount"), 0, AL_RIGHT);
      for (r = 0; r < 4; r++) {
        sprintf(s, "%.60s %d", W(w->item, "Item"), half * 4 + r + 1);
        put(d, in, r + 1, c0, s, 0, -1);
        sprintf(s, "%d", amounts[half][r]); tot += amounts[half][r];
        put(d, in, r + 1, c0 + 1, s, 0, AL_RIGHT);
      }
      put(d, in, 5, c0, W(w->total, "Total"), 0, -1);
      sprintf(s, "%d", tot); put(d, in, 5, c0 + 1, s, 0, AL_RIGHT);
    }
    break; }
  case QT_MATRIX: {
    /* distances between four points, as a mileage chart */
    static const int dist[4][4] = { { 0, 12, 30, 47 }, { 12, 0, 21, 35 }, { 30, 21, 0, 18 }, { 47, 35, 18, 0 } };
    ts = gallery(d, "QuillGridGrey");
    look = LK_FIRSTROW | LK_FIRSTCOL | LK_NOVBAND | LK_NOHBAND;
    if (ts) tbl_apply_style(d, &in, ts, look);
    for (r = 0; r < 4; r++) {
      sprintf(s, "%.60s %c", W(w->point, "Point"), 'A' + r);
      put(d, in, 0, r + 1, s, 0, AL_CENTER);
      put(d, in, r + 1, 0, s, 0, -1);
      for (c = 0; c < 4; c++) { if (r == c) strcpy(s, "\xE2\x80\x94"); else sprintf(s, "%d", dist[r][c]); put(d, in, r + 1, c + 1, s, 0, AL_CENTER); }
    }
    break; }
  case QT_TABULAR: {
    static const int need[6] = { 3, 12, 1, 8, 24, 2 };
    ts = gallery(d, "QuillLines");
    look = LK_FIRSTROW | LK_NOVBAND | LK_NOHBAND;
    if (ts) tbl_apply_style(d, &in, ts, look);
    put(d, in, 0, 0, W(w->item, "Item"), 0, -1);
    put(d, in, 0, 1, W(w->needed, "Needed"), 0, AL_RIGHT);
    for (r = 0; r < 6; r++) {
      sprintf(s, "%.60s %d", W(w->item, "Item"), r + 1); put(d, in, r + 1, 0, s, 0, -1);
      sprintf(s, "%d", need[r]); put(d, in, r + 1, 1, s, 0, AL_RIGHT);
    }
    break; }
  case QT_SUBHEADS1: case QT_SUBHEADS2: {
    /* a heading row of years, two groups of items under subheadings, a total */
    static const int fig[5][3] = { { 410, 435, 462 }, { 120, 131, 140 }, { 75, 70, 88 }, { 260, 284, 301 }, { 98, 102, 110 } };
    int sum[3] = { 0, 0, 0 }, item = 0, k, merged = kind == QT_SUBHEADS1;
    static const int subrow[2] = { 1, 5 };
    ts = gallery(d, merged ? "QuillBandedBlue" : "QuillList");
    look = LK_FIRSTROW | LK_LASTROW | LK_NOVBAND | LK_NOHBAND;
    if (ts) tbl_apply_style(d, &in, ts, look);
    put(d, in, 0, 0, W(w->item, "Item"), 0, -1);
    for (c = 0; c < 3; c++) { sprintf(s, "%d", year - 2 + c); put(d, in, 0, c + 1, s, 0, AL_RIGHT); }
    for (r = 1; r < 8; r++) {
      if (r == subrow[0] || r == subrow[1]) continue;
      sprintf(s, "%.60s %d", W(w->item, "Item"), item + 1); put(d, in, r, 0, s, 0, -1);
      for (c = 0; c < 3; c++) { sprintf(s, "%d", fig[item][c]); sum[c] += fig[item][c]; put(d, in, r, c + 1, s, 0, AL_RIGHT); }
      item++;
    }
    put(d, in, 8, 0, W(w->total, "Total"), 0, -1);
    for (c = 0; c < 3; c++) { sprintf(s, "%d", sum[c]); put(d, in, 8, c + 1, s, 0, AL_RIGHT); }
    /* the subheadings: across the whole row (the first kind), or in the first cell, bold and shaded */
    for (k = 1; k >= 0; k--) {
      Pos a = cellpos(d, in, subrow[k], 0);
      sprintf(s, "%.60s %d", W(w->subhead, "Subheading"), k + 1);
      if (merged) { Pos b = cellpos(d, in, subrow[k], 3); tbl_merge(d, a, b); a = cellpos(d, a, subrow[k], 0); }
      put(d, a, subrow[k], 0, s, 1, -1);
      if (!merged) { Pos b = cellpos(d, a, subrow[k], 3); tbl_cell_format(d, a, b, TF_SHADE, 0xE7E6E6); }
      in = a;
    }
    break; }
  }
  *at = cellpos(d, in, 0, 0);
  doc_end(d, *at);
  return 1;
}

/* ---- CSV */
/* valid UTF-8 throughout */
static int utf8_ok(const char *s, size_t n)
{
  size_t i = 0;
  while (i < n) {
    unsigned char c = (unsigned char)s[i];
    int k = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
    if (k < 0 || i + k >= n + (k ? 0 : 1)) return 0;
    for (i++; k > 0; k--, i++) if (i >= n || ((unsigned char)s[i] & 0xC0) != 0x80) return 0;
  }
  return 1;
}
/* a field that is a number (digits with one decimal point or comma, a sign, a currency or per cent) */
static int numeric(const char *s)
{
  int digits = 0, dots = 0;
  while (*s == ' ') s++;
  if (*s == '-' || *s == '+' || *s == '$') s++;
  for (; *s; s++) {
    if (*s >= '0' && *s <= '9') digits++;
    else if (*s == '.' || *s == ',') dots++;
    else if (*s == '%' && !s[1]) break;
    else if (*s != ' ') return 0;
  }
  return digits > 0 && dots <= 3;
}
typedef struct { char **f; int n, cap; } Row;
int tbl_from_csv(Doc *d, Pos *at, const char *csv, size_t n, i32 width)
{
  char *text = NULL;
  Row *rows = NULL;
  int nrows = 0, rcap = 0, ncols = 1, r, c, ok = 0;
  size_t i = 0;
  char sep = ',';
  Buf fb;
  /* Latin-1 text made UTF-8 */
  if (!utf8_ok(csv, n)) {
    Buf b;
    buf_init(&b);
    for (i = 0; i < n; i++) { char u[4]; int k = utf8_put(u, (unsigned char)csv[i]); buf_add(&b, u, (size_t)k); }
    if (b.oom) { buf_free(&b); return 0; }
    n = b.len; text = buf_take(&b); csv = text;
  }
  if (n >= 3 && (unsigned char)csv[0] == 0xEF && (unsigned char)csv[1] == 0xBB && (unsigned char)csv[2] == 0xBF) { csv += 3; n -= 3; }   /* a byte order mark */
  {
    /* the separator: the one the first line has most of, outside quotes */
    int cnt[3] = { 0, 0, 0 }, q = 0;
    for (i = 0; i < n && (q || (csv[i] != '\n' && csv[i] != '\r')); i++) {
      if (csv[i] == '"') q = !q;
      else if (!q) { if (csv[i] == ',') cnt[0]++; else if (csv[i] == ';') cnt[1]++; else if (csv[i] == '\t') cnt[2]++; }
    }
    if (cnt[2] >= cnt[0] && cnt[2] >= cnt[1] && cnt[2]) sep = '\t';
    else if (cnt[1] > cnt[0]) sep = ';';
  }
  buf_init(&fb);
  i = 0;
  while (i < n && nrows < 4000) {
    Row row;
    int endrow = 0;
    memset(&row, 0, sizeof row);
    while (!endrow) {
      /* one field */
      int quoted = 0;
      fb.len = 0;
      if (i < n && csv[i] == '"') { quoted = 1; i++; }
      while (i < n) {
        char ch = csv[i];
        if (quoted) {
          if (ch == '"') { if (i + 1 < n && csv[i + 1] == '"') { buf_addc(&fb, '"'); i += 2; continue; } quoted = 0; i++; continue; }
          if (ch == '\r') { i++; continue; }
          buf_addc(&fb, ch == '\n' ? '\v' : ch); i++;            /* a line end inside: a line break in the cell */
          continue;
        }
        if (ch == sep) { i++; break; }
        if (ch == '\r' || ch == '\n') { if (ch == '\r' && i + 1 < n && csv[i + 1] == '\n') i++; i++; endrow = 1; break; }
        buf_addc(&fb, ch); i++;
      }
      if (i >= n) endrow = 1;
      if (row.n >= row.cap) {
        int nc = row.cap ? row.cap * 2 : 8;
        char **nf = (char **)realloc(row.f, (size_t)nc * sizeof(char *));
        if (!nf) goto oom;
        row.f = nf; row.cap = nc;
      }
      if (!(row.f[row.n] = str_ndup(fb.data ? fb.data : "", fb.len))) goto oom;
      row.n++;
    }
    if (row.n == 1 && !row.f[0][0]) { free(row.f[0]); free(row.f); continue; }     /* an empty line */
    if (nrows >= rcap) {
      int nc = rcap ? rcap * 2 : 32;
      Row *nr = (Row *)realloc(rows, (size_t)nc * sizeof(Row));
      if (!nr) { for (c = 0; c < row.n; c++) free(row.f[c]); free(row.f); goto oom; }
      rows = nr; rcap = nc;
    }
    rows[nrows++] = row;
    if (row.n > ncols) ncols = row.n;
    continue;
  oom:
    for (c = 0; c < row.n; c++) free(row.f[c]);
    free(row.f);
    goto done;
  }
  if (!nrows) goto done;
  if (ncols > MAX_COLS) ncols = MAX_COLS;
  doc_begin(d, *at);
  if (tbl_insert(d, at, nrows, ncols, width)) {
    Pos in = *at;
    u32 first = in.para;
    /* a fresh table has one paragraph a cell, row by row: from the last cell back, so nothing moves */
    for (r = nrows - 1; r >= 0; r--)
      for (c = ncols - 1; c >= 0; c--) {
        Pos p;
        const char *f = c < rows[r].n ? rows[r].f[c] : "";
        p.para = first + (u32)r * ncols + c; p.off = 0;
        if (*f) {
          doc_insert(d, &p, f, (u32)strlen(f), NULL);
          if (r > 0 && numeric(f)) { ParaFmt pf; memset(&pf, 0, sizeof pf); pf.align = AL_RIGHT; doc_para_format(d, first + (u32)r * ncols + c, first + (u32)r * ncols + c, PM_ALIGN, &pf); }
        }
      }
    {
      /* the first row repeats on each page, as a heading row */
      Pos a = in, b = in;
      b.para = first + ncols - 1;
      tbl_cell_format(d, a, b, TF_HEADER, 1);
    }
    *at = in;
    ok = 1;
  }
  doc_end(d, *at);
done:
  for (r = 0; r < nrows; r++) { for (c = 0; c < rows[r].n; c++) free(rows[r].f[c]); free(rows[r].f); }
  free(rows);
  buf_free(&fb);
  free(text);
  return ok;
}
