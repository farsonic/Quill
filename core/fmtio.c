/*
 * fmtio.c - what the RTF, HTML and Markdown readers and writers share (WP28a, WP28c; fmtio.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fmtio.h"

/* ---- paragraph lists */
int pl_add(ParaList *l, Para *p)
{
  if (!p) { l->oom = 1; return 0; }
  if (l->np >= l->cap) {
    u32 nc = l->cap ? l->cap * 2 : 64;
    Para **n = (Para **)realloc(l->p, nc * sizeof(Para *));
    if (!n) { para_free(p); l->oom = 1; return 0; }
    l->p = n; l->cap = nc;
  }
  para_normalise(p);
  l->p[l->np++] = p;
  return 1;
}
void pl_free(ParaList *l)
{
  u32 i;
  for (i = 0; i < l->np; i++) para_free(l->p[i]);
  free(l->p);
  l->p = NULL; l->np = l->cap = 0;
}
int pl_to_body(Doc *d, ParaList *l)
{
  u32 i;
  if (!l->np) {
    Para *p = para_new(&d->styles[0].pf, &d->styles[0].cf);
    if (!pl_add(l, p)) return 0;
  }
  for (i = 0; i < d->np; i++) para_free(d->p[i]);
  free(d->p);
  d->p = l->p; d->np = l->np; d->pcap = l->cap;
  l->p = NULL; l->np = l->cap = 0;
  return 1;
}
void pl_to_story(ParaList *l, Story *s)
{
  story_free(s);
  s->p = l->p; s->np = l->np;
  l->p = NULL; l->np = l->cap = 0;
}

/* ---- base64 */
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
void b64_encode(Buf *b, const u8 *d, size_t n)
{
  size_t i;
  char q[4];
  if (!buf_reserve(b, (n + 2) / 3 * 4 + 1)) return;
  for (i = 0; i + 2 < n; i += 3) {
    q[0] = b64[d[i] >> 2]; q[1] = b64[((d[i] & 3) << 4) | (d[i + 1] >> 4)];
    q[2] = b64[((d[i + 1] & 15) << 2) | (d[i + 2] >> 6)]; q[3] = b64[d[i + 2] & 63];
    buf_add(b, q, 4);
  }
  if (i < n) {
    q[0] = b64[d[i] >> 2];
    if (i + 1 < n) { q[1] = b64[((d[i] & 3) << 4) | (d[i + 1] >> 4)]; q[2] = b64[(d[i + 1] & 15) << 2]; }
    else { q[1] = b64[(d[i] & 3) << 4]; q[2] = '='; }
    q[3] = '=';
    buf_add(b, q, 4);
  }
}
static int b64_val(int c)
{
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+' || c == '-') return 62;
  if (c == '/' || c == '_') return 63;
  return -1;
}
u8 *b64_decode(const char *s, size_t n, u32 *len)
{
  u8 *o = (u8 *)malloc(n / 4 * 3 + 4);
  u32 acc = 0, k = 0, bits = 0;
  size_t i;
  if (!o) return NULL;
  for (i = 0; i < n; i++) {
    int v = b64_val((u8)s[i]);
    if (s[i] == '=') break;
    if (v < 0) continue;
    acc = (acc << 6) | (u32)v; bits += 6;
    if (bits >= 8) { bits -= 8; o[k++] = (u8)(acc >> bits); }
  }
  *len = k;
  return o;
}

/* ---- pictures named by HTML and Markdown */
static int hexval(int c) { return c >= '0' && c <= '9' ? c - '0' : (c | 0x20) >= 'a' && (c | 0x20) <= 'f' ? (c | 0x20) - 'a' + 10 : -1; }
/* %xx undone (in place) */
static size_t unescape(char *s, size_t n)
{
  size_t i, o = 0;
  for (i = 0; i < n; i++) {
    if (s[i] == '%' && i + 2 < n && hexval((u8)s[i + 1]) >= 0 && hexval((u8)s[i + 2]) >= 0) {
      s[o++] = (char)(hexval((u8)s[i + 1]) * 16 + hexval((u8)s[i + 2])); i += 2;
    } else s[o++] = s[i];
  }
  s[o] = 0;
  return o;
}
u8 *fmt_fetch(const char *src, u32 *len)
{
  size_t n = strlen(src);
  if (n > 5 && !strncmp(src, "data:", 5)) {
    const char *comma = strchr(src, ',');
    const char *b = strstr(src, ";base64");
    if (!comma) return NULL;
    if (b && b < comma) return b64_decode(comma + 1, n - (size_t)(comma + 1 - src), len);
    {
      char *c = str_dup(comma + 1);
      if (!c) return NULL;
      *len = (u32)unescape(c, strlen(c));
      return (u8 *)c;
    }
  }
  if (strstr(src, "://")) {
    if (n > 7 && !strncmp(src, "file://", 7)) src += 7; else return NULL;      /* nothing from the network */
  }
  {
    const char *base = doc_load_path ? doc_load_path : "";
    const char *cut = NULL, *k;
    char *path;
    size_t bl;
    u8 *data;
    size_t got;
    for (k = base; *k; k++) if (*k == '/' || *k == ':') cut = k + 1;
    bl = cut ? (size_t)(cut - base) : 0;
    if (src[0] == '/' || strchr(src, ':')) bl = 0;      /* an absolute name */
    if (!(path = (char *)malloc(bl + n + 1))) return NULL;
    memcpy(path, base, bl); memcpy(path + bl, src, n + 1);
    unescape(path + bl, n);
#if defined(__amigaos__) || defined(__VBCC__) || defined(AMIGA)
    {
      /* Amiga names: "../" is "/" (the parent), "./" nothing */
      char *r = path + bl, *w = path + bl;
      while (*r) {
        if (r[0] == '.' && r[1] == '.' && r[2] == '/') { *w++ = '/'; r += 3; }
        else if (r[0] == '.' && r[1] == '/') r += 2;
        else *w++ = *r++;
      }
      *w = 0;
    }
#endif
    data = file_read(path, &got);
    free(path);
    if (data) *len = (u32)got;
    return data;
  }
}

/* ---- styles */
int fmt_heading_level(const Doc *d, int k)
{
  const Style *s;
  if (k < 0 || k >= d->nstyles) return 0;
  s = &d->styles[k];
  if (s->type != ST_PARA) return 0;
  if (!strncmp(s->id, "Heading", 7) && s->id[7] >= '1' && s->id[7] <= '9' && !s->id[8]) return s->id[7] - '0';
  if (!strncmp(s->name, "heading ", 8) && s->name[8] >= '1' && s->name[8] <= '9' && !s->name[9]) return s->name[8] - '0';
  return 0;
}
int fmt_heading_style(Doc *d, int level)
{
  char id[16];
  int k, base;
  Style s;
  if (level < 1) level = 1;
  if (level > 9) level = 9;
  sprintf(id, "Heading%d", level);
  if ((k = doc_style(d, id)) >= 0) return k;
  for (k = 0; k < d->nstyles; k++) if (fmt_heading_level(d, k) == level) return k;
  base = doc_style(d, "Heading3");
  memset(&s, 0, sizeof s);
  if (base >= 0) { s.pf = d->styles[base].pf; s.cf = d->styles[base].cf; }
  else { s.pf = d->styles[0].pf; s.cf = d->styles[0].cf; s.cf.flags |= CF_BOLD; s.pf.keepnext = 1; }
  if (level >= 4) { s.cf.size = level == 4 ? 24 : 22; if (level == 4) s.cf.flags |= CF_ITALIC; }
  strcpy(s.id, id); sprintf(s.name, "heading %d", level);
  s.type = ST_PARA; s.builtin = 1; s.quick = 1; s.next = 0;
  s.pf.style = (u8)d->nstyles;
  return doc_add_style_from(d, &s, 0);
}
int fmt_is_code_style(const Doc *d, int k)
{
  const char *n;
  if (k < 0 || k >= d->nstyles || d->styles[k].type != ST_PARA) return 0;
  n = d->styles[k].name;
  return str_ieq(n, "Code") || str_ieq(n, "Plain Text") || str_ieq(n, "HTML Preformatted") || str_ieq(n, "Source Code") ||
         str_ieq(n, "Preformatted Text") || str_ieq(n, "Code Block");
}
int fmt_code_style(Doc *d)
{
  int k = doc_style(d, "Code");
  Style s;
  if (k >= 0 && d->styles[k].type == ST_PARA) return k;
  memset(&s, 0, sizeof s);
  s.pf = d->styles[0].pf; s.cf = d->styles[0].cf;
  s.pf.before = s.pf.after = 0; s.pf.line = 240; s.pf.linerule = LS_AUTO;
  s.cf.font = (u8)doc_font(d, "Courier New"); s.cf.size = 20;
  strcpy(s.id, "Code"); strcpy(s.name, "Code");
  s.type = ST_PARA; s.quick = 1;
  s.pf.style = (u8)d->nstyles; s.next = (u8)d->nstyles;
  k = doc_add_style_from(d, &s, 0);
  return k < 0 ? 0 : k;
}
int fmt_mono_font(const char *f)
{
  return strstr(f, "Courier") || strstr(f, "Mono") || strstr(f, "mono") || strstr(f, "Consol") || strstr(f, "Monaco") ||
         strstr(f, "Menlo") || strstr(f, "Typewriter") || strstr(f, "Fixed") || str_ieq(f, "monospace");
}
int fmt_quote_style(Doc *d)
{
  int k = doc_style(d, "Quote");
  Style s;
  if (k >= 0) return k;
  memset(&s, 0, sizeof s);
  s.pf = d->styles[0].pf; s.cf = d->styles[0].cf;
  s.pf.left = s.pf.right = 720; s.cf.flags |= CF_ITALIC;
  strcpy(s.id, "Quote"); strcpy(s.name, "Quote");
  s.type = ST_PARA; s.builtin = 1; s.quick = 1;
  s.pf.style = (u8)d->nstyles; s.next = (u8)d->nstyles;
  return doc_add_style_from(d, &s, 0);
}
void fmt_link_cf(Doc *d, CharFmt *cf, const char *url)
{
  int k = doc_style(d, "Hyperlink");
  cf->link = (u16)doc_link(d, url);
  if (!cf->link) return;
  cf->flags |= CF_UNDERLINE; cf->ul = UL_SINGLE;
  cf->color = 0x0563C1;
  if (k > 0 && d->styles[k].type == ST_CHAR) { cf->cstyle = (u8)k; cf->color = d->styles[k].cf.color != COLOR_AUTO ? d->styles[k].cf.color : 0x0563C1; }
}

/* ---- tables from rows and cells */
i32 fmt_text_width(const Doc *d)
{
  i32 w = d->page.width - d->page.left - d->page.right;
  return w > 1440 ? w : 1440;
}
void tb_begin(TableBld *t, u32 first, int borders)
{
  memset(t, 0, sizeof *t);
  t->first = first; t->row = -1; t->borders = (u8)borders;
}
static u8 *busy_at(TableBld *t, int r, int c)
{
  if (r >= t->nrows) {
    int nr = r + 16;
    u8 *b;
    if (nr > TB_MAXROWS) return NULL;
    if (!(b = (u8 *)realloc(t->busy, (size_t)nr * MAX_COLS))) return NULL;
    memset(b + (size_t)t->nrows * MAX_COLS, 0, (size_t)(nr - t->nrows) * MAX_COLS);
    t->busy = b; t->nrows = nr;
  }
  return &t->busy[(size_t)r * MAX_COLS + c];
}
void tb_row(TableBld *t)
{
  if (t->row + 1 >= TB_MAXROWS) return;
  t->row++; t->col = 0; t->incell = 0;
}
/* a place covered by a cell spanning rows from above holds 0x80 + its column span in the cell's
   first column, 1 in the others */
void tb_cell(TableBld *t, int colspan, int rowspan, int header)
{
  u8 *b;
  int r, c;
  if (t->row < 0) tb_row(t);
  if (t->incell) t->col += t->incell & 0xFF;
  while (t->col < MAX_COLS && (b = busy_at(t, t->row, t->col)) && *b) t->col += (*b & 0x80) ? (*b & 0x7F) : 1;
  if (t->col >= MAX_COLS) t->col = MAX_COLS - 1;
  if (colspan < 1) colspan = 1;
  if (t->col + colspan > MAX_COLS) colspan = MAX_COLS - t->col;
  if (rowspan < 1) rowspan = 1;
  if (t->row + rowspan > TB_MAXROWS) rowspan = TB_MAXROWS - t->row;
  for (r = 1; r < rowspan; r++) {
    if ((b = busy_at(t, t->row + r, t->col))) *b = (u8)(0x80 | colspan);
    for (c = 1; c < colspan; c++) if ((b = busy_at(t, t->row + r, t->col + c))) *b = 1;
  }
  if (t->row == 0 && header) t->header0 = 1;
  if (t->col + colspan > t->ncols) t->ncols = t->col + colspan;
  t->incell = colspan | (rowspan > 1 ? 0x100 : 0);
}
void tb_mark(TableBld *t, ParaFmt *pf)
{
  memset(&pf->cell, 0, sizeof pf->cell);
  memset(&pf->outer, 0, sizeof pf->outer);
  if (t->row < 0 || !t->incell) tb_cell(t, 1, 1, 0);
  pf->cell.table = 0xFFFF;                 /* (given its number in tb_end) */
  pf->cell.row = (u16)t->row;
  pf->cell.col = (u8)t->col;
  pf->cell.span = (u8)(t->incell & 0xFF ? t->incell & 0xFF : 1);
  pf->cell.vmerge = (u8)((t->incell & 0x100) ? VM_START : VM_NONE);
  pf->list = 0;
}
static int cell_cmp(const Para *a, const Para *b)
{
  if (a->pf.cell.row != b->pf.cell.row) return a->pf.cell.row < b->pf.cell.row ? -1 : 1;
  if (a->pf.cell.col != b->pf.cell.col) return a->pf.cell.col < b->pf.cell.col ? -1 : 1;
  return 0;
}
int tb_end(TableBld *t, Doc *d, ParaList *l, i32 width)
{
  Table tb;
  int num, r, c, nrows = t->row + 1;
  u32 i, n = l->np - t->first, k;
  Para **cells;
  ParaList out;
  if (n == 0 || t->ncols < 1) { free(t->busy); t->busy = NULL; return 1; }
  memset(&tb, 0, sizeof tb);
  tb.ncols = (u8)t->ncols;
  for (c = 0; c < t->ncols; c++) tb.grid[c] = width / t->ncols;
  tb.marl = tb.marr = 108;
  tb.indent = 0;
  tb.look = LK_DEFAULT;
  tb.autofit = 1;
  if (!(num = doc_add_table(d, &tb))) { free(t->busy); t->busy = NULL; return 0; }
  /* stable sort of the table's paragraphs by row and column (insertion: tables are small) */
  cells = l->p + t->first;
  for (i = 1; i < n; i++) {
    Para *p = cells[i];
    u32 j = i;
    while (j > 0 && cell_cmp(cells[j - 1], p) > 0) { cells[j] = cells[j - 1]; j--; }
    cells[j] = p;
  }
  /* rebuilt in order, with an empty paragraph for each place covered from above or left empty */
  memset(&out, 0, sizeof out);
  k = 0;
  for (r = 0; r < nrows; r++) {
    c = 0;
    while (c < t->ncols) {
      u8 *b = (t->busy && r < t->nrows) ? &t->busy[(size_t)r * MAX_COLS + c] : NULL;
      int span = 1;
      if (k < n && cells[k]->pf.cell.row == r && cells[k]->pf.cell.col == c) {
        span = cells[k]->pf.cell.span;
        while (k < n && cells[k]->pf.cell.row == r && cells[k]->pf.cell.col == c) {
          Para *p = cells[k++];
          p->pf.cell.table = (u16)num;
          p->pf.cell.borders = t->borders ? BD_ALL : 0;
          p->pf.cell.shade = COLOR_AUTO;
          p->pf.cell.bcolor[0] = p->pf.cell.bcolor[1] = p->pf.cell.bcolor[2] = p->pf.cell.bcolor[3] = COLOR_AUTO;
          p->pf.cell.header = (u8)(r == 0 && t->header0);
          pl_add(&out, p);
        }
      } else {
        /* covered from above (VM_CONT) or a place the row left empty */
        Para *q;
        ParaFmt pf = d->styles[0].pf;
        int cover = b && (*b & 0x80);
        if (b && *b == 1) { c++; continue; }        /* inside a wider covering cell */
        span = cover ? (*b & 0x7F) : 1;
        if (span < 1) span = 1;
        memset(&pf.cell, 0, sizeof pf.cell);
        pf.cell.table = (u16)num; pf.cell.row = (u16)r; pf.cell.col = (u8)c; pf.cell.span = (u8)span;
        pf.cell.vmerge = cover ? VM_CONT : VM_NONE;
        pf.cell.borders = t->borders ? BD_ALL : 0;
        pf.cell.shade = COLOR_AUTO;
        pf.cell.bcolor[0] = pf.cell.bcolor[1] = pf.cell.bcolor[2] = pf.cell.bcolor[3] = COLOR_AUTO;
        pf.cell.header = (u8)(r == 0 && t->header0);
        q = para_new(&pf, &d->styles[0].cf);
        pl_add(&out, q);
      }
      c += span;
    }
  }
  /* anything left over (rows past the limit) is appended to the last cell */
  while (k < n) {
    Para *p = cells[k++];
    if (out.np) p->pf.cell = out.p[out.np - 1]->pf.cell;
    pl_add(&out, p);
  }
  free(t->busy); t->busy = NULL;
  if (out.oom) { pl_free(&out); l->np = t->first; return 0; }
  /* the list's tail replaced by the rebuilt paragraphs */
  l->np = t->first;
  for (i = 0; i < out.np; i++) {
    if (!pl_add(l, out.p[i])) { u32 j; for (j = i + 1; j < out.np; j++) para_free(out.p[j]); break; }
  }
  free(out.p);
  /* a VM_START cell whose cover never came (a row span past the table's end) is a plain cell */
  return !l->oom;
}

/* ---- CSS */
static const struct { const char *name; u32 rgb; } named[] = {
  { "black", 0x000000 }, { "silver", 0xC0C0C0 }, { "gray", 0x808080 }, { "grey", 0x808080 }, { "white", 0xFFFFFF },
  { "maroon", 0x800000 }, { "red", 0xFF0000 }, { "purple", 0x800080 }, { "fuchsia", 0xFF00FF }, { "magenta", 0xFF00FF },
  { "green", 0x008000 }, { "lime", 0x00FF00 }, { "olive", 0x808000 }, { "yellow", 0xFFFF00 }, { "navy", 0x000080 },
  { "blue", 0x0000FF }, { "teal", 0x008080 }, { "aqua", 0x00FFFF }, { "cyan", 0x00FFFF }, { "orange", 0xFFA500 },
  { "darkblue", 0x00008B }, { "darkred", 0x8B0000 }, { "darkgreen", 0x006400 }, { "brown", 0xA52A2A },
  { "pink", 0xFFC0CB }, { "gold", 0xFFD700 }, { "lightgray", 0xD3D3D3 }, { "lightgrey", 0xD3D3D3 },
  { "darkgray", 0xA9A9A9 }, { "darkgrey", 0xA9A9A9 }, { "violet", 0xEE82EE }, { "indigo", 0x4B0082 }
};
u32 css_color(const char *s, size_t n)
{
  size_t i;
  while (n && (*s == ' ' || *s == '\t')) { s++; n--; }
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == ';')) n--;
  if (n && s[0] == '#') {
    u32 v = 0;
    for (i = 1; i < n && hexval((u8)s[i]) >= 0; i++) v = v * 16 + (u32)hexval((u8)s[i]);
    if (i == 7) return v;
    if (i == 4) return ((v >> 8) & 15) * 0x110000 + ((v >> 4) & 15) * 0x1100 + (v & 15) * 0x11;
    return COLOR_AUTO;
  }
  if (n > 4 && !strncmp(s, "rgb", 3)) {
    int ch[3] = { 0, 0, 0 }, k = 0;
    for (i = 3; i < n && k < 3; i++) {
      if (s[i] >= '0' && s[i] <= '9') {
        int v = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') v = v * 10 + (s[i++] - '0');
        if (i < n && s[i] == '.') while (i < n && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.')) i++;
        if (i < n && s[i] == '%') v = v * 255 / 100;
        ch[k++] = v > 255 ? 255 : v;
      }
    }
    return ((u32)ch[0] << 16) | ((u32)ch[1] << 8) | (u32)ch[2];
  }
  for (i = 0; i < sizeof named / sizeof named[0]; i++) {
    size_t k = strlen(named[i].name), j;
    if (k != n) continue;
    for (j = 0; j < k && (s[j] | 0x20) == named[i].name[j]; j++) ;
    if (j == k) return named[i].rgb;
  }
  return COLOR_AUTO;
}
i32 css_twips(const char *s, size_t n, int *ok)
{
  long whole = 0, frac = 0, fd = 1;
  int neg = 0, any = 0;
  size_t i = 0;
  double v;
  *ok = 0;
  while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
  if (i < n && (s[i] == '-' || s[i] == '+')) neg = s[i++] == '-';
  while (i < n && s[i] >= '0' && s[i] <= '9') { whole = whole * 10 + (s[i++] - '0'); any = 1; if (whole > 100000) whole = 100000; }
  if (i < n && s[i] == '.') { i++; while (i < n && s[i] >= '0' && s[i] <= '9') { if (fd < 10000) { frac = frac * 10 + (s[i] - '0'); fd *= 10; } i++; any = 1; } }
  if (!any) return 0;
  /* twips per unit, times 1000 (integers only: vbcc's maths) */
  {
    long per = 15000;                                   /* px (96 dpi) */
    long t;
    (void)v;
    if (i + 1 < n || i < n) {
      const char *u = s + i;
      size_t un = n - i;
      if (un >= 2 && (u[0] | 0x20) == 'p' && (u[1] | 0x20) == 't') per = 20000;
      else if (un >= 2 && (u[0] | 0x20) == 'i' && (u[1] | 0x20) == 'n') per = 1440000;
      else if (un >= 2 && (u[0] | 0x20) == 'c' && (u[1] | 0x20) == 'm') per = 566929;
      else if (un >= 2 && (u[0] | 0x20) == 'm' && (u[1] | 0x20) == 'm') per = 56693;
      else if (un >= 2 && (u[0] | 0x20) == 'p' && (u[1] | 0x20) == 'c') per = 240000;
      else if (un >= 2 && (u[0] | 0x20) == 'e' && (u[1] | 0x20) == 'm') per = 240000;
      else if (un >= 3 && (u[0] | 0x20) == 'r' && (u[1] | 0x20) == 'e' && (u[2] | 0x20) == 'm') per = 240000;
      else if (un >= 2 && (u[0] | 0x20) == 'e' && (u[1] | 0x20) == 'x') per = 120000;
      else if (un >= 1 && u[0] == '%') per = 2400;
    }
    t = (whole * per + frac * per / fd) / 1000;
    *ok = 1;
    return (i32)(neg ? -t : t);
  }
}
static const struct { const char *name; u16 cp; } ents[] = {
  { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 },
  { "copy", 0xA9 }, { "reg", 0xAE }, { "trade", 0x2122 }, { "hellip", 0x2026 }, { "mdash", 0x2014 }, { "ndash", 0x2013 },
  { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "ldquo", 0x201C }, { "rdquo", 0x201D }, { "laquo", 0xAB }, { "raquo", 0xBB },
  { "bull", 0x2022 }, { "middot", 0xB7 }, { "deg", 0xB0 }, { "euro", 0x20AC }, { "pound", 0xA3 }, { "yen", 0xA5 },
  { "cent", 0xA2 }, { "sect", 0xA7 }, { "para", 0xB6 }, { "times", 0xD7 }, { "divide", 0xF7 }, { "plusmn", 0xB1 },
  { "shy", 0xAD }, { "iexcl", 0xA1 }, { "iquest", 0xBF }, { "frac12", 0xBD }, { "frac14", 0xBC }, { "frac34", 0xBE },
  { "auml", 0xE4 }, { "ouml", 0xF6 }, { "uuml", 0xFC }, { "Auml", 0xC4 }, { "Ouml", 0xD6 }, { "Uuml", 0xDC }, { "szlig", 0xDF },
  { "eacute", 0xE9 }, { "egrave", 0xE8 }, { "ecirc", 0xEA }, { "aacute", 0xE1 }, { "agrave", 0xE0 }, { "acirc", 0xE2 },
  { "iacute", 0xED }, { "oacute", 0xF3 }, { "uacute", 0xFA }, { "ntilde", 0xF1 }, { "ccedil", 0xE7 }, { "Eacute", 0xC9 },
  { "Ntilde", 0xD1 }, { "Ccedil", 0xC7 }, { "aring", 0xE5 }, { "Aring", 0xC5 }, { "oslash", 0xF8 }, { "Oslash", 0xD8 },
  { "aelig", 0xE6 }, { "AElig", 0xC6 }, { "ensp", 0x2002 }, { "emsp", 0x2003 }, { "thinsp", 0x2009 }, { "larr", 0x2190 },
  { "rarr", 0x2192 }, { "uarr", 0x2191 }, { "darr", 0x2193 }, { "check", 0x2713 }, { "dagger", 0x2020 }, { "Dagger", 0x2021 },
  { "permil", 0x2030 }, { "prime", 0x2032 }, { "infin", 0x221E }, { "ne", 0x2260 }, { "le", 0x2264 }, { "ge", 0x2265 },
  { "alpha", 0x3B1 }, { "beta", 0x3B2 }, { "gamma", 0x3B3 }, { "delta", 0x3B4 }, { "pi", 0x3C0 }, { "mu", 0x3BC }
};
u32 html_entity(const char *s, size_t n, int *used)
{
  size_t i;
  if (n < 3 || s[0] != '&') return 0;
  if (s[1] == '#') {
    u32 v = 0;
    int hex = s[2] == 'x' || s[2] == 'X';
    i = hex ? 3 : 2;
    if (i >= n) return 0;
    for (; i < n && i < 12; i++) {
      int h = hexval((u8)s[i]);
      if (hex ? h < 0 : !(s[i] >= '0' && s[i] <= '9')) break;
      v = hex ? v * 16 + (u32)h : v * 10 + (u32)(s[i] - '0');
    }
    if (i < n && s[i] == ';') i++;
    else if (i == (size_t)(hex ? 3 : 2)) return 0;
    *used = (int)i;
    if (v == 0 || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) v = 0xFFFD;
    return v;
  }
  for (i = 0; i < sizeof ents / sizeof ents[0]; i++) {
    size_t k = strlen(ents[i].name);
    if (k + 2 <= n && !strncmp(s + 1, ents[i].name, k) && s[k + 1] == ';') { *used = (int)k + 2; return ents[i].cp; }
  }
  return 0;
}
