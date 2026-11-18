/*
 * mergedata.c - mail merge recipient lists (WP4, core/merge.h): the table itself, and reading it
 * from CSV or tab-separated text, .xlsx (the first sheet) and a .docx (its first table); writing CSV.
 * Every file is hostile input: lengths and indexes are checked, sizes capped.
 * The .xlsx reader is written for Quill after the one in Ledger (same author), cut down to the
 * values: shared and inline strings, numbers, booleans, and dates by their number formats.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "merge.h"
#include "docx.h"
#include "zip.h"
#include "xml.h"

#define MD_MAXCOLS 256
#define MD_MAXROWS 100000

const char *const md_new_cols[] = { "Title", "First Name", "Last Name", "Company Name", "Address Line 1", "Address Line 2", "City",
  "State", "ZIP Code", "Country or Region", "Home Phone", "Work Phone", "E-mail Address" };
const int md_nnew_cols = 13;

/* ------------------------------------------------------------------ the table */
MergeData *md_new(int ncols, const char *const *names)
{
  MergeData *m = (MergeData *)calloc(1, sizeof *m);
  int i;
  if (!m) return NULL;
  if (ncols > MD_MAXCOLS) ncols = MD_MAXCOLS;
  for (i = 0; i < ncols; i++) if (md_add_col(m, names ? names[i] : "") < 0) { md_free(m); return NULL; }
  return m;
}
void md_free(MergeData *m)
{
  int i, j;
  if (!m) return;
  for (i = 0; i < m->nrows; i++) { for (j = 0; j < m->ncols; j++) free(m->cell[i][j]); free(m->cell[i]); }
  for (j = 0; j < m->ncols; j++) free(m->cols[j]);
  free(m->cols); free(m->cell); free(m->on); free(m);
}
int md_add_col(MergeData *m, const char *name)
{
  char **nc, buf[24];
  int i;
  if (m->ncols >= MD_MAXCOLS) return -1;
  if (!name || !name[0]) { sprintf(buf, "Column %d", m->ncols + 1); name = buf; }
  if (!(nc = (char **)realloc(m->cols, (size_t)(m->ncols + 1) * sizeof(char *)))) return -1;
  m->cols = nc;
  if (!(m->cols[m->ncols] = str_dup(name))) return -1;
  for (i = 0; i < m->nrows; i++) {
    char **r = (char **)realloc(m->cell[i], (size_t)(m->ncols + 1) * sizeof(char *));
    if (!r) return -1;
    m->cell[i] = r;
    if (!(r[m->ncols] = str_dup(""))) return -1;
  }
  return m->ncols++;
}
int md_add_row(MergeData *m, const char *const *cells)
{
  char **r;
  int j;
  if (m->nrows >= MD_MAXROWS) return -1;
  if (m->nrows == m->rcap) {
    int nc = m->rcap ? m->rcap * 2 : 16;
    char ***c = (char ***)realloc(m->cell, (size_t)nc * sizeof(char **));
    u8 *on;
    if (!c) return -1;
    m->cell = c;
    if (!(on = (u8 *)realloc(m->on, (size_t)nc))) return -1;
    m->on = on;
    m->rcap = nc;
  }
  if (!(r = (char **)calloc((size_t)(m->ncols ? m->ncols : 1), sizeof(char *)))) return -1;
  for (j = 0; j < m->ncols; j++)
    if (!(r[j] = str_dup(cells && cells[j] ? cells[j] : ""))) { while (j-- > 0) free(r[j]); free(r); return -1; }
  m->cell[m->nrows] = r;
  m->on[m->nrows] = 1;
  return m->nrows++;
}
int md_set(MergeData *m, int row, int col, const char *v)
{
  char *s;
  if (row < 0 || row >= m->nrows || col < 0 || col >= m->ncols || !(s = str_dup(v ? v : ""))) return 0;
  free(m->cell[row][col]);
  m->cell[row][col] = s;
  return 1;
}
int md_delete_row(MergeData *m, int row)
{
  int j;
  if (row < 0 || row >= m->nrows) return 0;
  for (j = 0; j < m->ncols; j++) free(m->cell[row][j]);
  free(m->cell[row]);
  memmove(&m->cell[row], &m->cell[row + 1], (size_t)(m->nrows - row - 1) * sizeof(char **));
  memmove(&m->on[row], &m->on[row + 1], (size_t)(m->nrows - row - 1));
  m->nrows--;
  return 1;
}
/* names equal ignoring case, '_' as ' ' and spaces at the ends */
static int name_eq(const char *a, const char *b)
{
  while (*a == ' ') a++;
  while (*b == ' ') b++;
  for (;;) {
    char x = *a, y = *b;
    if (x == '_') x = ' ';
    if (y == '_') y = ' ';
    if (x >= 'a' && x <= 'z') x = (char)(x - 32);
    if (y >= 'a' && y <= 'z') y = (char)(y - 32);
    if (!x || !y) {
      while (*a == ' ' || *a == '_') a++;
      while (*b == ' ' || *b == '_') b++;
      return !*a && !*b;
    }
    if (x != y) return 0;
    a++; b++;
  }
}
int md_col(const MergeData *m, const char *name)
{
  int j;
  if (!m || !name) return -1;
  for (j = 0; j < m->ncols; j++) if (name_eq(m->cols[j], name)) return j;
  return -1;
}

/* ------------------------------------------------------------------ text */
static int valid_utf8(const u8 *s, size_t n)
{
  size_t i = 0;
  while (i < n) {
    u8 c = s[i];
    int k = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
    if (k < 0 || (k && i + (size_t)k >= n)) return 0;
    i++;
    while (k-- > 0) { if ((s[i] & 0xC0) != 0x80) return 0; i++; }
  }
  return 1;
}
/* the file's text as UTF-8 (UTF-16 with a byte order mark, UTF-8, else Latin-1), malloc'd */
static char *as_utf8(const u8 *d, size_t n, size_t *outn)
{
  Buf b;
  size_t i;
  buf_init(&b);
  if (n >= 2 && ((d[0] == 0xFF && d[1] == 0xFE) || (d[0] == 0xFE && d[1] == 0xFF))) {
    int le = d[0] == 0xFF;
    for (i = 2; i + 1 < n; i += 2) {
      u32 c = le ? (u32)(d[i] | d[i + 1] << 8) : (u32)(d[i] << 8 | d[i + 1]);
      char e[5];
      if (c >= 0xD800 && c < 0xDC00 && i + 3 < n) {
        u32 lo = le ? (u32)(d[i + 2] | d[i + 3] << 8) : (u32)(d[i + 2] << 8 | d[i + 3]);
        if (lo >= 0xDC00 && lo < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); i += 2; }
      }
      buf_add(&b, e, (size_t)utf8_put(e, c));
    }
  } else if (n >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) buf_add(&b, d + 3, n - 3);
  else if (valid_utf8(d, n)) buf_add(&b, d, n);
  else for (i = 0; i < n; i++) { char e[5]; buf_add(&b, e, (size_t)utf8_put(e, d[i])); }
  buf_addc(&b, 0);
  if (b.oom) { buf_free(&b); return NULL; }
  *outn = b.len - 1;
  return buf_take(&b);
}
MergeData *md_load_text(const u8 *data, size_t len)
{
  size_t n, i;
  char *s = as_utf8(data, len, &n), delim = ',';
  int counts[3] = { 0, 0, 0 }, q = 0, row = 0;
  MergeData *m = NULL;
  char **cells = NULL;
  int ncells = 0, ccap = 0;
  Buf f;
  if (!s) return NULL;
  /* the delimiter: the commonest of tab, ';' and ',' on the first line (outside quotes) */
  for (i = 0; i < n && (q || (s[i] != '\n' && s[i] != '\r')); i++) {
    if (s[i] == '"') q = !q;
    else if (!q && s[i] == '\t') counts[0]++;
    else if (!q && s[i] == ';') counts[1]++;
    else if (!q && s[i] == ',') counts[2]++;
  }
  if (counts[0]) delim = '\t'; else if (counts[1] > counts[2]) delim = ';';
  buf_init(&f);
  q = 0;
  for (i = 0; i <= n; i++) {
    char c = i < n ? s[i] : '\n';
    int endf = 0, endr = 0;
    if (q) {
      if (c == '"' && i + 1 < n && s[i + 1] == '"') { buf_addc(&f, '"'); i++; }
      else if (c == '"') q = 0;
      else if (i == n) { endf = endr = 1; }
      else buf_addc(&f, c);
    } else if (c == '"' && f.len == 0) q = 1;
    else if (c == delim) endf = 1;
    else if (c == '\r' || c == '\n') { endf = endr = 1; if (c == '\r' && i + 1 < n && s[i + 1] == '\n') i++; }
    else buf_addc(&f, c);
    if (endf) {
      if (ncells == ccap) {
        char **nc = (char **)realloc(cells, (size_t)(ccap = ccap ? ccap * 2 : 16) * sizeof(char *));
        if (!nc) goto oom;
        cells = nc;
      }
      buf_addc(&f, 0);
      if (f.oom || !(cells[ncells] = str_dup(f.data))) goto oom;
      ncells++;
      f.len = 0;
    }
    if (endr) {
      int j, blank = 1;
      for (j = 0; j < ncells; j++) if (cells[j][0]) blank = 0;
      if (row == 0) {
        if (!blank) {
          if (!(m = md_new(ncells, (const char *const *)cells))) goto oom;
          row = 1;
        }
      } else if (!blank) {
        while (ncells > m->ncols) if (md_add_col(m, NULL) < 0) goto oom;
        { const char *rc[MD_MAXCOLS]; for (j = 0; j < m->ncols; j++) rc[j] = j < ncells ? cells[j] : ""; if (md_add_row(m, rc) < 0) break; }
      }
      for (j = 0; j < ncells; j++) free(cells[j]);
      ncells = 0;
    }
  }
  buf_free(&f); free(cells); free(s);
  return m;
oom:
  { int j; for (j = 0; j < ncells; j++) free(cells[j]); }
  buf_free(&f); free(cells); free(s); md_free(m);
  return NULL;
}
char *md_csv(const MergeData *m, size_t *len)
{
  Buf b;
  int i, j;
  buf_init(&b);
  for (i = -1; i < m->nrows; i++) {
    for (j = 0; j < m->ncols; j++) {
      const char *v = i < 0 ? m->cols[j] : m->cell[i][j];
      if (j) buf_addc(&b, ',');
      if (strpbrk(v, ",\"\r\n;") || v[0] == ' ') {
        buf_addc(&b, '"');
        for (; *v; v++) { if (*v == '"') buf_addc(&b, '"'); buf_addc(&b, *v); }
        buf_addc(&b, '"');
      } else buf_adds(&b, v);
    }
    buf_adds(&b, "\r\n");
  }
  buf_addc(&b, 0);
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len - 1;
  return buf_take(&b);
}

/* ------------------------------------------------------------------ .xlsx */
static char *zpart(ZipReader *z, const char *name, size_t *n)
{
  int i = zip_find(z, name);
  return i < 0 ? NULL : (char *)zip_read(z, i, n);
}
/* the text of the element just started (its t children; phonetic runs left out), to its end */
static void elem_text(Xml *x, Buf *out)
{
  int depth = x->depth, in_t = 0, rph = 0;
  if (x->pending_end) return;
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_START) { if (!strcmp(x->name, "rPh")) rph++; else if (!strcmp(x->name, "t")) in_t = 1; }
    else if (x->type == XT_END) { if (x->depth < depth) break; if (!strcmp(x->name, "rPh")) rph--; else if (!strcmp(x->name, "t")) in_t = 0; }
    else if (x->type == XT_TEXT && in_t && !rph) buf_add(out, x->text.data, x->text.len);
  }
}
/* a number format code that shows a date */
static int date_code(const char *c)
{
  int q = 0, br = 0, any = 0;
  for (; *c; c++) {
    if (*c == '"') q = !q;
    else if (q) continue;
    else if (*c == '[') br = 1;
    else if (*c == ']') br = 0;
    else if (br) continue;
    else if (*c == '\\') { if (c[1]) c++; }
    else if (*c == 'd' || *c == 'D' || *c == 'y' || *c == 'Y') any = 1;
    else if (*c == '#' || *c == '0') return 0;
  }
  return any;
}
/* an Excel date code made a field date picture (months as M, minutes as m) */
static void date_pic(const char *c, char *out, size_t cap)
{
  size_t n = 0;
  int q = 0;
  for (; *c && n + 2 < cap; c++) {
    if (*c == '"') { q = !q; continue; }
    if (*c == '[' && !q) { while (*c && *c != ']') c++; if (!*c) break; continue; }
    if (*c == '\\' && c[1]) { out[n++] = *++c; continue; }
    if (!q && (*c == 'm' || *c == 'M')) {
      int minutes = 0;
      const char *p;
      for (p = c - 1; p >= c - 3 && n && (p[0] == ':' || p[0] == 'h' || p[0] == 'H'); p--) if (*p == ':' || *p == 'h' || *p == 'H') minutes = 1;
      out[n++] = minutes ? 'm' : 'M';
      continue;
    }
    if (!q && *c == ';') break;
    out[n++] = *c;
  }
  out[n] = 0;
}
/* a cell reference's column ("BC12": 54) from 0, and row from 0; 0 if it isn't one */
static int cellref(const char *r, int *col, int *row)
{
  int c = 0, n = 0;
  while (*r >= 'A' && *r <= 'Z') { c = c * 26 + (*r - 'A' + 1); r++; if (c > 20000) return 0; }
  while (*r >= '0' && *r <= '9') { n = n * 10 + (*r - '0'); r++; if (n > 2000000) return 0; }
  if (!c || !n) return 0;
  *col = c - 1; *row = n - 1;
  return 1;
}
static void num_text(const char *v, char *out, size_t cap)
{
  /* a binary fraction written out in full ("0.30000000000000004") rounded to 15 significant digits,
     in the digits (no floating point) */
  char dg[40];
  int n = 0, sig = 0, dot = -1, i, neg = v[0] == '-', lead = 1;
  const char *p = v + neg;
  snprintf(out, cap, "%s", v);
  if (strchr(v, 'E') || strchr(v, 'e') || !strchr(v, '.') || strlen(v) >= sizeof dg) return;
  for (; *p; p++) {
    if (*p == '.') { dot = n; continue; }
    if (*p < '0' || *p > '9') return;
    dg[n++] = *p;
    if (*p != '0') lead = 0;
    if (!lead) sig++;
  }
  if (sig <= 15) return;
  /* the digit position where 15 significant ones end */
  for (i = 0, sig = 0; i < n; i++) { if (dg[i] != '0' || sig) sig++; if (sig == 15) break; }
  if (i + 1 < n && dg[i + 1] >= '5') {
    int k = i;
    while (k >= 0) { if (dg[k] == '9') { dg[k] = '0'; k--; } else { dg[k]++; break; } }
    if (k < 0) return;                       /* (999... rounding up a place: kept as written) */
  }
  n = i + 1 > dot ? i + 1 : dot;
  while (n > dot && dg[n - 1] == '0') n--;
  {
    size_t o = 0;
    if (neg && o + 1 < cap) out[o++] = '-';
    for (i = 0; i < n && o + 2 < cap; i++) { if (i == dot) { if (!i) out[o++] = '0'; out[o++] = '.'; } out[o++] = dg[i]; }
    if (dot == 0 && n == 0 && o + 1 < cap) out[o++] = '0';
    out[o] = 0;
  }
}
MergeData *md_load_xlsx(const u8 *data, size_t len, char *err, size_t errcap)
{
  ZipReader z;
  char *wb = NULL, *rels = NULL, *ss = NULL, *st = NULL, *sh = NULL, rid[64] = "", sheet[256] = "xl/worksheets/sheet1.xml";
  size_t n;
  char **strs = NULL;
  long nstrs = 0, scap = 0;
  int *xfdate = NULL, nxf = 0;
  char (*xfpic)[48] = NULL;
  char (*cust)[64] = NULL; int *custid = NULL, ncust = 0;
  MergeData *m = NULL;
  Xml x;
  if (!zip_open(&z, data, len)) { snprintf(err, errcap, "It isn't a spreadsheet."); return NULL; }
  /* the first sheet, through the workbook's relationships */
  if ((wb = zpart(&z, "xl/workbook.xml", &n))) {
    xml_init(&x, wb, n);
    while (xml_next(&x) != XT_EOF) if (x.type == XT_START && !strcmp(x.name, "sheet")) { const char *r = xml_attr(&x, "id"); if (r) snprintf(rid, sizeof rid, "%s", r); break; }
    xml_free(&x);
  }
  if (rid[0] && (rels = zpart(&z, "xl/_rels/workbook.xml.rels", &n))) {
    xml_init(&x, rels, n);
    while (xml_next(&x) != XT_EOF)
      if (x.type == XT_START && !strcmp(x.name, "Relationship")) {
        const char *id = xml_attr(&x, "Id"), *t = xml_attr(&x, "Target");
        if (id && t && !strcmp(id, rid)) {
          if (t[0] == '/') snprintf(sheet, sizeof sheet, "%s", t + 1); else snprintf(sheet, sizeof sheet, "xl/%s", t);
          break;
        }
      }
    xml_free(&x);
  }
  /* the shared strings */
  if ((ss = zpart(&z, "xl/sharedStrings.xml", &n))) {
    xml_init(&x, ss, n);
    while (xml_next(&x) != XT_EOF)
      if (x.type == XT_START && !strcmp(x.name, "si")) {
        Buf t;
        buf_init(&t);
        elem_text(&x, &t);
        buf_addc(&t, 0);
        if (nstrs == scap) {
          char **ns = (char **)realloc(strs, (size_t)(scap = scap ? scap * 2 : 256) * sizeof(char *));
          if (!ns || scap > 2000000) { buf_free(&t); break; }
          strs = ns;
        }
        strs[nstrs++] = t.oom ? str_dup("") : buf_take(&t);
        buf_free(&t);
      }
    xml_free(&x);
  }
  /* the styles: which cell formats show dates, and how */
  if ((st = zpart(&z, "xl/styles.xml", &n))) {
    int inxfs = 0;
    xml_init(&x, st, n);
    while (xml_next(&x) != XT_EOF) {
      if (x.type == XT_START && !strcmp(x.name, "numFmt")) {
        const char *id = xml_attr(&x, "numFmtId"), *c = xml_attr(&x, "formatCode");
        void *a = realloc(cust, (size_t)(ncust + 1) * sizeof *cust), *b2;
        if (!a) continue;
        cust = a;
        if (!(b2 = realloc(custid, (size_t)(ncust + 1) * sizeof(int)))) continue;
        custid = b2;
        custid[ncust] = id ? atoi(id) : -1;
        snprintf(cust[ncust], sizeof cust[0], "%s", c ? c : "");
        ncust++;
      } else if (x.type == XT_START && !strcmp(x.name, "cellXfs")) inxfs = 1;
      else if (x.type == XT_END && !strcmp(x.name, "cellXfs")) inxfs = 0;
      else if (x.type == XT_START && inxfs && !strcmp(x.name, "xf") && nxf < 4096) {
        const char *id = xml_attr(&x, "numFmtId");
        int f = id ? atoi(id) : 0, k;
        void *a = realloc(xfdate, (size_t)(nxf + 1) * sizeof(int)), *b2;
        if (!a) continue;
        xfdate = a;
        if (!(b2 = realloc(xfpic, (size_t)(nxf + 1) * sizeof *xfpic))) continue;
        xfpic = b2;
        xfdate[nxf] = (f >= 14 && f <= 17) || f == 22 || (f >= 45 && f <= 47);
        strcpy(xfpic[nxf], f == 14 ? "M/d/yyyy" : f == 15 ? "d-MMM-yy" : f == 16 ? "d-MMM" : f == 17 ? "MMM-yy" : f == 22 ? "M/d/yyyy H:mm" : "HH:mm");
        for (k = 0; k < ncust; k++) if (custid[k] == f && date_code(cust[k])) { xfdate[nxf] = 1; date_pic(cust[k], xfpic[nxf], sizeof xfpic[0]); }
        nxf++;
      }
    }
    xml_free(&x);
  }
  /* the sheet's cells */
  if (!(sh = zpart(&z, sheet, &n))) { snprintf(err, errcap, "The spreadsheet has no sheet Quill can read."); goto done; }
  {
    char ***grid = NULL;
    int nr = 0, nc = 0, rowcap = 0, cur = -1, col = 0, r, c;
    xml_init(&x, sh, n);
    while (xml_next(&x) != XT_EOF) {
      if (x.type == XT_START && !strcmp(x.name, "row")) {
        const char *ra = xml_attr(&x, "r");
        cur = ra ? atoi(ra) - 1 : cur + 1;
        col = 0;
      } else if (x.type == XT_START && !strcmp(x.name, "c") && cur >= 0 && cur < MD_MAXROWS) {
        const char *ref = xml_attr(&x, "r"), *t = xml_attr(&x, "t"), *s = xml_attr(&x, "s");
        int rr = cur, cc = col, depth = x.depth, sidx = s ? atoi(s) : 0;
        char v[512] = "", out[512];
        Buf is;
        buf_init(&is);
        if (ref) cellref(ref, &cc, &rr);
        col = cc + 1;
        if (!x.pending_end)
          while (xml_next(&x) != XT_EOF) {
            if (x.type == XT_END && x.depth < depth) break;
            if (x.type == XT_START && !strcmp(x.name, "v")) {
              Buf vb; buf_init(&vb);
              while (xml_next(&x) == XT_TEXT) buf_add(&vb, x.text.data, x.text.len);
              buf_addc(&vb, 0);
              if (!vb.oom) snprintf(v, sizeof v, "%s", vb.data);
              buf_free(&vb);
              if (x.type == XT_END && x.depth < depth) break;
            } else if (x.type == XT_START && !strcmp(x.name, "is")) elem_text(&x, &is);
          }
        out[0] = 0;
        if (t && !strcmp(t, "s")) { long k = atol(v); if (k >= 0 && k < nstrs) snprintf(out, sizeof out, "%s", strs[k]); }
        else if (t && !strcmp(t, "inlineStr")) { buf_addc(&is, 0); if (!is.oom && is.data) snprintf(out, sizeof out, "%s", is.data); }
        else if (t && !strcmp(t, "b")) snprintf(out, sizeof out, "%s", atoi(v) ? "TRUE" : "FALSE");
        else if (t && (!strcmp(t, "str") || !strcmp(t, "e"))) snprintf(out, sizeof out, "%s", v);
        else if (v[0] && sidx >= 0 && sidx < nxf && xfdate[sidx]) {
          /* a date: days since 1899-12-30 (the 1900 system) */
          long days = atol(v), frac = 0, secs;
          const char *fp = strchr(v, '.');
          int fd = 0;
          if (fp) for (fp++; *fp >= '0' && *fp <= '9' && fd < 5; fp++, fd++) frac = frac * 10 + (*fp - '0');
          while (fd++ < 5) frac *= 10;
          secs = (frac * 864L + 500) / 1000;   /* (a fraction of a day in 1/100000ths, as seconds) */
          FieldTime ft;
          long z2 = days - 25569 + 719468, era, doe, yoe, doy, mp;     /* (days since 1970, then Hinnant's civil_from_days) */
          era = (z2 >= 0 ? z2 : z2 - 146096) / 146097;
          doe = z2 - era * 146097;
          yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
          doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
          mp = (5 * doy + 2) / 153;
          memset(&ft, 0, sizeof ft);
          ft.day = (int)(doy - (153 * mp + 2) / 5 + 1);
          ft.month = (int)(mp < 10 ? mp + 3 : mp - 9);
          ft.year = (int)(yoe + era * 400 + (ft.month <= 2));
          ft.hour = (int)(secs / 3600); ft.min = (int)(secs / 60 % 60); ft.sec = (int)(secs % 60);
          ft.wday = (int)(((days - 25569) % 7 + 11) % 7);
          field_format_date(xfpic[sidx], &ft, out, sizeof out);
        } else num_text(v, out, sizeof out);
        buf_free(&is);
        if (rr < MD_MAXROWS && cc < MD_MAXCOLS && out[0]) {
          if (rr >= rowcap) {
            int ncap = rr + 64, i;
            char ***g = (char ***)realloc(grid, (size_t)ncap * sizeof(char **));
            if (!g) continue;
            grid = g;
            for (i = rowcap; i < ncap; i++) grid[i] = NULL;
            rowcap = ncap;
          }
          if (!grid[rr] && !(grid[rr] = (char **)calloc(MD_MAXCOLS, sizeof(char *)))) continue;
          free(grid[rr][cc]);
          grid[rr][cc] = str_dup(out);
          if (rr + 1 > nr) nr = rr + 1;
          if (cc + 1 > nc) nc = cc + 1;
        }
      }
    }
    xml_free(&x);
    /* the first row with anything in it names the columns */
    for (r = 0; r < nr && !grid[r]; r++) ;
    if (r < nr) {
      const char *names[MD_MAXCOLS];
      for (c = 0; c < nc; c++) names[c] = grid[r][c] ? grid[r][c] : "";
      if ((m = md_new(nc, names)))
        for (r++; r < nr; r++) {
          const char *cells[MD_MAXCOLS];
          if (!grid[r]) continue;
          for (c = 0; c < nc; c++) cells[c] = grid[r][c] ? grid[r][c] : "";
          if (md_add_row(m, cells) < 0) break;
        }
    } else snprintf(err, errcap, "The spreadsheet's first sheet is empty.");
    for (r = 0; r < rowcap; r++) if (grid[r]) { for (c = 0; c < MD_MAXCOLS; c++) free(grid[r][c]); free(grid[r]); }
    free(grid);
  }
done:
  { long k; for (k = 0; k < nstrs; k++) free(strs[k]); }
  free(strs); free(wb); free(rels); free(ss); free(st); free(sh); free(xfdate); free(xfpic); free(cust); free(custid);
  zip_close(&z);
  return m;
}

/* ------------------------------------------------------------------ .docx */
/* a paragraph's text without Quill's marks and objects */
static void para_plain(const Para *p, Buf *out)
{
  u32 o;
  for (o = 0; o < p->len; o++) {
    u8 c = (u8)p->t[o];
    if ((c == 0xF3 || c == 0xF4) && o + 4 <= p->len) { o += 3; continue; }
    if (c == 0xEE && o + 3 <= p->len && (u8)p->t[o + 1] == 0x80) { o += 2; continue; }
    buf_addc(out, c == '\t' || c == '\v' ? ' ' : (char)c);
  }
}
MergeData *md_load_docx(const u8 *data, size_t len, char *err, size_t errcap)
{
  unsigned lost = 0;
  Doc *d = docx_load(data, len, &lost, err, errcap);
  MergeData *m = NULL;
  u32 pi, first;
  u16 t;
  int maxc = 0, row = -1;
  char *cells[MD_MAXCOLS];
  if (!d) return NULL;
  for (pi = 0; pi < d->np && !d->p[pi]->pf.cell.table; pi++) ;
  if (pi == d->np) { snprintf(err, errcap, "The document has no table to take the recipients from."); doc_free(d); return NULL; }
  t = top_cell(&d->p[pi]->pf)->table;
  first = pi;
  for (; pi < d->np && top_cell(&d->p[pi]->pf)->table == t; pi++) { int c = top_cell(&d->p[pi]->pf)->col + 1; if (c > maxc) maxc = c; }
  if (maxc > MD_MAXCOLS) maxc = MD_MAXCOLS;
  memset(cells, 0, sizeof cells);
  for (pi = first; ; pi++) {
    const Cell *c = pi < d->np ? top_cell(&d->p[pi]->pf) : NULL;
    int done = !c || c->table != t;
    if (done || (int)c->row != row) {
      int j;
      if (row >= 0) {
        if (!m) m = md_new(maxc, (const char *const *)cells);
        else md_add_row(m, (const char *const *)cells);
      }
      for (j = 0; j < MD_MAXCOLS; j++) { free(cells[j]); cells[j] = NULL; }
      if (done) break;
      row = c->row;
    }
    if (c->col < maxc && c->vmerge != VM_CONT) {
      Buf b;
      buf_init(&b);
      if (cells[c->col]) { buf_adds(&b, cells[c->col]); buf_addc(&b, ' '); }
      para_plain(d->p[pi], &b);
      buf_addc(&b, 0);
      free(cells[c->col]);
      cells[c->col] = b.oom ? NULL : buf_take(&b);
      buf_free(&b);
    }
  }
  doc_free(d);
  if (!m) snprintf(err, errcap, "Quill ran out of memory.");
  return m;
}

MergeData *md_load_file(const char *path, char *err, size_t errcap)
{
  size_t n;
  u8 *data = file_read(path, &n);
  MergeData *m = NULL;
  if (!data) { snprintf(err, errcap, "Quill could not read the file."); return NULL; }
  if (n >= 4 && data[0] == 'P' && data[1] == 'K') {
    ZipReader z;
    int isdoc = 0;
    if (zip_open(&z, data, n)) { isdoc = zip_find(&z, "word/document.xml") >= 0; zip_close(&z); }
    m = isdoc ? md_load_docx(data, n, err, errcap) : md_load_xlsx(data, n, err, errcap);
  } else if (n >= 8 && data[0] == 0xD0 && data[1] == 0xCF) snprintf(err, errcap, "Old Excel (.xls) and Access files can't be read: save the list as .xlsx or CSV.");
  else {
    m = md_load_text(data, n);
    if (!m) snprintf(err, errcap, "The file has no recipients.");
  }
  free(data);
  return m;
}
