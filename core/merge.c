/*
 * merge.c - mail merge (WP4, core/merge.h): the main document's settings and recipients, filtering
 * and sorting (kept in the query as SQL, as Word keeps them), Match Fields, Address Block and
 * Greeting Line, the merge rules, previewing and merging. Written from Word's documented behaviour.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "merge.h"

/* ------------------------------------------------------------------ the main document's settings */
MergeInfo *merge_info(Doc *d)
{
  MergeInfo *mi;
  if (d->merge) return d->merge;
  if (!(mi = (MergeInfo *)calloc(1, sizeof *mi))) return NULL;
  mi->hdr = 1;
  d->merge = mi;
  return mi;
}
void merge_free(Doc *d)
{
  MergeInfo *mi = d ? d->merge : NULL;
  if (!mi) return;
  md_free(mi->data);
  free(mi->view); free(mi->extra); free(mi->odsox);
  free(mi);
  d->merge = NULL;
}
void merge_set_data(Doc *d, MergeData *m)
{
  MergeInfo *mi = merge_info(d);
  if (!mi) { md_free(m); return; }
  if (mi->data != m) md_free(mi->data);
  mi->data = m;
  mi->loaded = 1;
  mi->rec = 0;
  merge_auto_match(mi);
  merge_apply_view(mi);
}
int merge_ensure(Doc *d, char *err, size_t errcap)
{
  MergeInfo *mi = d->merge;
  if (err && errcap) err[0] = 0;
  if (!mi) return 0;
  if (!mi->data && !mi->loaded && mi->src[0]) {
    MergeData *m;
    char e[160] = "";
    mi->loaded = 1;
    if ((m = md_load_file(mi->src, e, sizeof e))) {
      int keep = mi->rec;
      merge_set_data(d, m);
      merge_parse_query(mi);
      merge_apply_view(mi);
      if (keep < mi->nview) mi->rec = keep;
    } else if (err) snprintf(err, errcap, "%s", e);
  }
  return mi->data && mi->data->nrows > 0;
}

/* ------------------------------------------------------------------ the view: filter and sort */
static int ci_cmp(const char *a, const char *b)
{
  for (;; a++, b++) {
    int x = (u8)*a, y = (u8)*b;
    if (x >= 'a' && x <= 'z') x -= 32;
    if (y >= 'a' && y <= 'z') y -= 32;
    if (x != y || !x) return x - y;
  }
}
static int ci_contains(const char *h, const char *n)
{
  size_t ln = strlen(n), i;
  if (!ln) return 1;
  for (; *h; h++) {
    for (i = 0; i < ln; i++) {
      int x = (u8)h[i], y = (u8)n[i];
      if (x >= 'a' && x <= 'z') x -= 32;
      if (y >= 'a' && y <= 'z') y -= 32;
      if (x != y) break;
    }
    if (i == ln) return 1;
  }
  return 0;
}
static int is_num(const char *s, double *v)
{
  char *e;
  while (*s == ' ') s++;
  if (!*s) return 0;
  *v = strtod(s, &e);
  while (*e == ' ') e++;
  return !*e;
}
/* two values compared as Word's filter compares them: numbers as numbers, else text ignoring case */
static int val_cmp(const char *a, const char *b)
{
  double x, y;
  if (is_num(a, &x) && is_num(b, &y)) return x < y ? -1 : x > y;
  return ci_cmp(a, b);
}
static int cond_true(const MergeInfo *mi, int row, const MergeFilter *f)
{
  int c = md_col(mi->data, f->col);
  const char *v = c >= 0 ? mi->data->cell[row][c] : "";
  int r;
  switch (f->op) {
  case MF_EQ: return val_cmp(v, f->val) == 0;
  case MF_NE: return val_cmp(v, f->val) != 0;
  case MF_LT: return val_cmp(v, f->val) < 0;
  case MF_GT: return val_cmp(v, f->val) > 0;
  case MF_LE: return val_cmp(v, f->val) <= 0;
  case MF_GE: return val_cmp(v, f->val) >= 0;
  case MF_BLANK: return !v[0];
  case MF_NOTBLANK: return v[0] != 0;
  case MF_CONTAINS: r = ci_contains(v, f->val); return r;
  case MF_NOTCONTAINS: return !ci_contains(v, f->val);
  }
  return 1;
}
/* AND binds before OR, as in SQL */
int merge_row_passes(const MergeInfo *mi, int row)
{
  int i, any = 0, cur = 1;
  if (!mi->nfilt) return 1;
  for (i = 0; i < mi->nfilt; i++) {
    if (i && mi->filt[i].or_) { if (cur) return 1; cur = 1; }
    if (!mi->filt[i].col[0]) continue;
    any = 1;
    if (cur && !cond_true(mi, row, &mi->filt[i])) cur = 0;
  }
  return cur || !any;
}
static const MergeInfo *sort_mi;
static int row_cmp(int a, int b)
{
  int k;
  for (k = 0; k < MERGE_NSORT; k++) {
    int c = sort_mi->sortcol[k][0] ? md_col(sort_mi->data, sort_mi->sortcol[k]) : -1, r;
    if (c < 0) continue;
    r = val_cmp(sort_mi->data->cell[a][c], sort_mi->data->cell[b][c]);
    if (r) return sort_mi->sortdesc[k] ? -r : r;
  }
  return a - b;
}
/* a stable sort (merge sort) of n row numbers */
static void msort(int *v, int *tmp, int n)
{
  int h, i, j, k;
  if (n < 2) return;
  h = n / 2;
  msort(v, tmp, h); msort(v + h, tmp, n - h);
  for (i = 0, j = h, k = 0; i < h || j < n; ) tmp[k++] = (j >= n || (i < h && row_cmp(v[i], v[j]) <= 0)) ? v[i++] : v[j++];
  memcpy(v, tmp, (size_t)n * sizeof(int));
}
void merge_apply_view(MergeInfo *mi)
{
  int i, *tmp;
  free(mi->view); mi->view = NULL; mi->nview = 0;
  if (!mi->data) return;
  if (!(mi->view = (int *)malloc((size_t)(mi->data->nrows + 1) * sizeof(int)))) return;
  for (i = 0; i < mi->data->nrows; i++) if (merge_row_passes(mi, i)) mi->view[mi->nview++] = i;
  if ((tmp = (int *)malloc((size_t)(mi->nview + 1) * sizeof(int)))) { sort_mi = mi; msort(mi->view, tmp, mi->nview); free(tmp); }
  if (mi->rec >= mi->nview) mi->rec = mi->nview ? mi->nview - 1 : 0;
  merge_make_query(mi);
}
int merge_count(const MergeInfo *mi)
{
  int i, n = 0;
  if (!mi || !mi->data) return 0;
  for (i = 0; i < mi->nview; i++) if (mi->data->on[mi->view[i]]) n++;
  return n;
}
int merge_active(const MergeInfo *mi, int k)
{
  int i;
  if (!mi || !mi->data || k < 0) return -1;
  for (i = 0; i < mi->nview; i++) if (mi->data->on[mi->view[i]] && k-- == 0) return i;
  return -1;
}

/* ---- the query: SELECT * FROM `table` WHERE (`col` op 'value') AND ... ORDER BY `col` ASC, ... */
static const char *const sql_ops[MF_N] = { "=", "<>", "<", ">", "<=", ">=", "IS NULL", "IS NOT NULL", "LIKE", "NOT LIKE" };
static void q_name(Buf *b, const char *n) { buf_addc(b, '`'); buf_adds(b, n); buf_addc(b, '`'); }
static void q_str(Buf *b, const char *s, int like)
{
  buf_addc(b, '\'');
  if (like) buf_addc(b, '%');
  for (; *s; s++) { if (*s == '\'') buf_addc(b, '\''); buf_addc(b, *s); }
  if (like) buf_addc(b, '%');
  buf_addc(b, '\'');
}
void merge_make_query(MergeInfo *mi)
{
  Buf b;
  int i, first = 1;
  const char *t = mi->table, *base;
  char tb[260];
  if (!t[0]) {
    for (base = t = mi->src; *t; t++) if (*t == '/' || *t == ':' || *t == '\\') base = t + 1;
    snprintf(tb, sizeof tb, "%s", base[0] ? base : "Recipients");
    t = tb;
  }
  buf_init(&b);
  buf_adds(&b, "SELECT * FROM ");
  q_name(&b, t);
  for (i = 0; i < mi->nfilt; i++) {
    const MergeFilter *f = &mi->filt[i];
    if (!f->col[0]) continue;
    buf_adds(&b, first ? " WHERE (" : f->or_ ? " OR (" : " AND (");
    first = 0;
    q_name(&b, f->col);
    buf_addc(&b, ' ');
    buf_adds(&b, sql_ops[f->op < MF_N ? f->op : 0]);
    if (f->op != MF_BLANK && f->op != MF_NOTBLANK) { buf_addc(&b, ' '); q_str(&b, f->val, f->op >= MF_CONTAINS); }
    buf_addc(&b, ')');
  }
  first = 1;
  for (i = 0; i < MERGE_NSORT; i++) {
    if (!mi->sortcol[i][0]) continue;
    buf_adds(&b, first ? " ORDER BY " : ", ");
    first = 0;
    q_name(&b, mi->sortcol[i]);
    buf_adds(&b, mi->sortdesc[i] ? " DESC" : " ASC");
  }
  buf_addc(&b, 0);
  if (!b.oom) snprintf(mi->query, sizeof mi->query, "%s", b.data);
  buf_free(&b);
}
static const char *q_ws(const char *s) { while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++; return s; }
static int q_kw(const char **s, const char *kw)
{
  const char *p = q_ws(*s);
  size_t n = strlen(kw), i;
  for (i = 0; i < n; i++) { char c = p[i]; if (c >= 'a' && c <= 'z') c = (char)(c - 32); if (c != kw[i]) return 0; }
  if (p[n] && p[n] != ' ' && p[n] != '(' && p[n] != '\'' && p[n] != '`' && p[n] != '[' && p[n] != '\t' && p[n] != ')' && p[n] != ',') return 0;
  *s = p + n;
  return 1;
}
/* a column name (`x`, [x], "x" or a word) */
static int q_ident(const char **s, char *out, size_t cap)
{
  const char *p = q_ws(*s);
  size_t n = 0;
  char close = *p == '`' ? '`' : *p == '[' ? ']' : *p == '"' ? '"' : 0;
  if (close) { p++; while (*p && *p != close) { if (n + 1 < cap) out[n++] = *p; p++; } if (*p) p++; }
  else while (*p && *p != ' ' && *p != ')' && *p != '=' && *p != '<' && *p != '>' && *p != ',') { if (n + 1 < cap) out[n++] = *p; p++; }
  out[n] = 0;
  *s = p;
  return n > 0;
}
static int q_value(const char **s, char *out, size_t cap)
{
  const char *p = q_ws(*s);
  size_t n = 0;
  if (*p == '\'') {
    p++;
    while (*p) { if (*p == '\'' && p[1] == '\'') { if (n + 1 < cap) out[n++] = '\''; p += 2; continue; } if (*p == '\'') { p++; break; } if (n + 1 < cap) out[n++] = *p; p++; }
  } else while (*p && *p != ' ' && *p != ')') { if (n + 1 < cap) out[n++] = *p; p++; }
  out[n] = 0;
  *s = p;
  return 1;
}
int merge_parse_query(MergeInfo *mi)
{
  const char *s = mi->query, *w = NULL, *p;
  int q = 0, any = 0;
  /* the clauses (outside quotes) */
  for (p = s; *p; p++) {
    if (*p == '\'') q = !q;
    else if (!q && !w) { const char *t = p; if ((p == s || p[-1] == ' ' || p[-1] == ')') && q_kw(&t, "WHERE")) w = t; }
    if (!q) { const char *t = p; if ((p == s || p[-1] == ' ' || p[-1] == ')') && q_kw(&t, "ORDER")) { if (q_kw(&t, "BY")) { int k = 0; s = t; while (k < MERGE_NSORT) {
            char nm[64];
            if (!q_ident(&s, nm, sizeof nm)) break;
            snprintf(mi->sortcol[k], sizeof mi->sortcol[k], "%s", nm);
            mi->sortdesc[k] = (u8)q_kw(&s, "DESC");
            if (!mi->sortdesc[k]) q_kw(&s, "ASC");
            k++; any = 1;
            s = q_ws(s);
            if (*s != ',') break;
            s++;
          }
          break; } } }
  }
  if (w) {
    int or_ = 0;
    mi->nfilt = 0;
    s = w;
    while (mi->nfilt < MERGE_NFILT) {
      MergeFilter *f = &mi->filt[mi->nfilt];
      int op = -1;
      memset(f, 0, sizeof *f);
      s = q_ws(s);
      while (*s == '(') s = q_ws(s + 1);
      if (q_kw(&s, "ORDER") || !*s) break;
      if (!q_ident(&s, f->col, sizeof f->col)) break;
      s = q_ws(s);
      if (q_kw(&s, "IS")) op = q_kw(&s, "NOT") ? MF_NOTBLANK : MF_BLANK, q_kw(&s, "NULL");
      else if (q_kw(&s, "NOT")) { q_kw(&s, "LIKE"); op = MF_NOTCONTAINS; }
      else if (q_kw(&s, "LIKE")) op = MF_CONTAINS;
      else if (s[0] == '<' && s[1] == '>') { op = MF_NE; s += 2; }
      else if (s[0] == '<' && s[1] == '=') { op = MF_LE; s += 2; }
      else if (s[0] == '>' && s[1] == '=') { op = MF_GE; s += 2; }
      else if (*s == '=') { op = MF_EQ; s++; }
      else if (*s == '<') { op = MF_LT; s++; }
      else if (*s == '>') { op = MF_GT; s++; }
      else break;
      f->op = (u8)op;
      if (op != MF_BLANK && op != MF_NOTBLANK) {
        q_value(&s, f->val, sizeof f->val);
        if (op >= MF_CONTAINS) {               /* '%x%' */
          size_t n = strlen(f->val);
          if (n && f->val[n - 1] == '%') f->val[--n] = 0;
          if (f->val[0] == '%') memmove(f->val, f->val + 1, n);
        }
      }
      f->or_ = (u8)or_;
      mi->nfilt++;
      any = 1;
      s = q_ws(s);
      while (*s == ')') s = q_ws(s + 1);
      if (q_kw(&s, "AND")) or_ = 0;
      else if (q_kw(&s, "OR")) or_ = 1;
      else break;
    }
  }
  return any;
}

/* ------------------------------------------------------------------ address fields and Match Fields */
const char *const af_names[AF_N] = { "Unique Identifier", "Courtesy Title", "First Name", "Middle Name", "Last Name", "Suffix", "Nickname",
  "Job Title", "Company", "Address 1", "Address 2", "City", "State", "Postal Code", "Country or Region", "Business Phone", "Business Fax",
  "Home Phone", "Home Fax", "E-mail Address", "Web Page", "Spouse/Partner Courtesy Title", "Spouse/Partner First Name",
  "Spouse/Partner Middle Name", "Spouse/Partner Last Name", "Spouse/Partner Nickname", "Phonetic Guide for First Name",
  "Phonetic Guide for Last Name", "Address 3", "Department" };
/* other names a column may have for each (Word's own lists, and the common ones in English) */
static const char *const af_alias[AF_N] = {
  "ID|Unique ID|Customer ID", "Title|Mr/Mrs|Salutation|Prefix", "FirstName|First|Given Name|Forename|First_Name", "Middle|Middle Initial|MiddleName",
  "LastName|Last|Surname|Family Name|Last_Name", "Suffix", "Nick", "Job|Position|JobTitle", "Company Name|Organisation|Organization|Firm|Company_Name",
  "Address Line 1|Address1|Street|Address|Street Address|Address_Line_1|Addr1", "Address Line 2|Address2|Address_Line_2|Addr2",
  "Town|Town/City|City/Town|Locality", "County|Province|Region|State/Province", "ZIP Code|ZIP|Postcode|Post Code|Postal|ZIP_Code|PLZ",
  "Country|Country or Region|Country/Region|Country_or_Region", "Work Phone|Office Phone|Business Telephone", "Work Fax",
  "Phone|Telephone|Home Telephone|Home_Phone", "Fax", "Email|E-mail|Email Address|E-mail_Address|Mail", "Website|Web|URL|Homepage",
  "", "", "", "", "", "", "", "Address Line 3|Address3", "Dept" };
static int alias_col(const MergeData *m, int af)
{
  char nm[64];
  const char *p = af_alias[af], *e;
  int c = md_col(m, af_names[af]);
  while (c < 0 && *p) {
    size_t n;
    e = strchr(p, '|');
    n = e ? (size_t)(e - p) : strlen(p);
    if (n >= sizeof nm) n = sizeof nm - 1;
    memcpy(nm, p, n); nm[n] = 0;
    c = md_col(m, nm);
    p = e ? e + 1 : p + n;
  }
  return c;
}
int merge_af_col(const MergeInfo *mi, int af)
{
  if (!mi || !mi->data || af < 0 || af >= AF_N) return -1;
  if (mi->map[af][0]) return md_col(mi->data, mi->map[af]);
  return alias_col(mi->data, af);
}
void merge_auto_match(MergeInfo *mi)
{
  int af, c;
  if (!mi->data) return;
  for (af = 0; af < AF_N; af++)
    if (!mi->map[af][0] && (c = alias_col(mi->data, af)) >= 0) snprintf(mi->map[af], sizeof mi->map[af], "%s", mi->data->cols[c]);
}
int merge_value(const MergeInfo *mi, int v, const char *name, char *out, size_t cap)
{
  int c, af;
  if (cap) out[0] = 0;
  if (!mi || !mi->data) return 0;
  c = md_col(mi->data, name);
  if (c < 0) for (af = 0; af < AF_N && c < 0; af++) if (!ci_cmp(name, af_names[af])) c = merge_af_col(mi, af);
  if (c < 0) return 0;
  if (v >= 0 && v < mi->nview) snprintf(out, cap, "%s", mi->data->cell[mi->view[v]][c]);
  return 1;
}

/* ------------------------------------------------------------------ Address Block and Greeting Line */
/* Word's placeholders in ADDRESSBLOCK's and GREETINGLINE's \f strings, and the address field each is */
static const struct { const char *tok; int af; } toks[] = {
  { "_TITLE0_", AF_TITLE }, { "_FIRST0_", AF_FIRST }, { "_MIDDLE0_", AF_MIDDLE }, { "_LAST0_", AF_LAST }, { "_SUFFIX0_", AF_SUFFIX },
  { "_NICK0_", AF_NICK }, { "_POSITION_", AF_JOB }, { "_COMPANY_", AF_COMPANY }, { "_STREET1_", AF_ADDR1 }, { "_STREET2_", AF_ADDR2 },
  { "_STREET3_", AF_ADDR3 }, { "_CITY_", AF_CITY }, { "_STATE_", AF_STATE }, { "_POSTAL_", AF_POSTAL }, { "_COUNTRY_", AF_COUNTRY },
  { "_TITLE1_", AF_STITLE }, { "_FIRST1_", AF_SFIRST }, { "_MIDDLE1_", AF_SMIDDLE }, { "_LAST1_", AF_SLAST }, { "_NICK1_", AF_SNICK },
  { "_DEPARTMENT_", AF_DEPT } };
#define NTOKS ((int)(sizeof toks / sizeof toks[0]))
/* Word's sample recipient (shown when there is no data) */
static const char *sample(int af)
{
  switch (af) {
  case AF_TITLE: return "Mr.";
  case AF_FIRST: return "Joshua";
  case AF_MIDDLE: return "Q.";
  case AF_LAST: return "Randall";
  case AF_SUFFIX: return "Jr.";
  case AF_NICK: return "Josh";
  case AF_COMPANY: return "Blue Sky Airlines";
  case AF_ADDR1: return "1 Main St.";
  case AF_CITY: return "Any City";
  case AF_STATE: return "WA";
  case AF_POSTAL: return "98052";
  }
  return "";
}
static void af_value(const MergeInfo *mi, int v, int af, char *out, size_t cap)
{
  int c = merge_af_col(mi, af);
  if (cap) out[0] = 0;
  if (!mi || !mi->data) snprintf(out, cap, "%s", sample(af));        /* (no recipients yet: Word's sample) */
  else if (c >= 0 && v >= 0 && v < mi->nview) snprintf(out, cap, "%s", mi->data->cell[mi->view[v]][c]);
}
/* a \f string worked out: <<text _TOKEN_ text>> groups shown only when the token has a value
   (_BEFORE_ and _AFTER_ groups always, without the token); "\n" (or a real line end) a new line.
   *names: whether any name token had a value. excl: the country left out (ADDRESSBLOCK \c 2) */
static void fill(const MergeInfo *mi, int v, const char *f, int country, const char *excl, char *out, size_t cap, int *names)
{
  size_t n = 0;
  char grp[256], val[256];
#define PUTC(c) do { if (n + 1 < cap) out[n++] = (char)(c); } while (0)
  if (names) *names = 0;
  while (*f) {
    if (f[0] == '<' && f[1] == '<') {
      const char *e = strstr(f + 2, ">>");
      size_t gl;
      int i, show = 0;
      char *tp = NULL;
      if (!e) e = f + strlen(f);
      gl = (size_t)(e - f - 2);
      if (gl >= sizeof grp) gl = sizeof grp - 1;
      memcpy(grp, f + 2, gl); grp[gl] = 0;
      f = *e ? e + 2 : e;
      val[0] = 0;
      if ((tp = strstr(grp, "_BEFORE_")) || (tp = strstr(grp, "_AFTER_"))) {
        size_t tl = tp[1] == 'B' ? 8 : 7;
        if (tp[tl] == ' ') tl++;
        memmove(tp, tp + tl, strlen(tp + tl) + 1);
        show = 1;
      } else
        for (i = 0; i < NTOKS; i++)
          if ((tp = strstr(grp, toks[i].tok))) {
            size_t tl = strlen(toks[i].tok), vl;
            af_value(mi, v, toks[i].af, val, sizeof val);
            if (toks[i].af == AF_COUNTRY && (country == 0 || (country == 2 && excl && excl[0] && !ci_cmp(val, excl)))) val[0] = 0;
            if (!val[0]) break;
            show = 1;
            if (names && toks[i].af <= AF_NICK) *names = 1;
            if (names && toks[i].af >= AF_STITLE && toks[i].af <= AF_SNICK) *names = 1;
            vl = strlen(val);
            if (gl - tl + vl >= sizeof grp) break;
            memmove(tp + vl, tp + tl, strlen(tp + tl) + 1);
            memcpy(tp, val, vl);
            break;
          }
      if (show) {
        const char *g;
        for (g = grp; *g; g++) {
          if (g[0] == '\\' && g[1] == 'n') { PUTC('\v'); g++; }
          else if (*g == '\r' || *g == '\n') PUTC('\v');
          else PUTC(*g);
        }
      }
      continue;
    }
    if (f[0] == '\\' && f[1] == 'n') { PUTC('\v'); f += 2; continue; }
    if (*f == '\r' || *f == '\n') { PUTC('\v'); f++; continue; }
    PUTC(*f); f++;
  }
  out[n] = 0;
  /* no empty lines, none at the ends */
  {
    char *p = out, *q = out;
    int prev = 1;
    for (; *p; p++) { if (*p == '\v' && prev) continue; prev = *p == '\v'; *q++ = *p; }
    while (q > out && q[-1] == '\v') q--;
    *q = 0;
  }
#undef PUTC
}
#define DEF_ADDRESS "<<_TITLE0_ >><<_FIRST0_>><< _LAST0_>><< _SUFFIX0_>>\\n<<_COMPANY_\\n>><<_STREET1_\\n>><<_STREET2_\\n>><<_CITY_>><<, _STATE_>><< _POSTAL_>><<\\n_COUNTRY_>>"
#define DEF_GREETING "<<_BEFORE_ Dear >><<_TITLE0_ >><<_LAST0_>><<_AFTER_ ,>>"
void merge_address(const MergeInfo *mi, int v, const char *code, char *out, size_t cap)
{
  char f[512], c[8], e[128];
  int country = 2;
  if (!field_switch(code, 'f', 0, f, sizeof f) || !f[0]) snprintf(f, sizeof f, "%s", DEF_ADDRESS);
  if (field_switch(code, 'c', 0, c, sizeof c) && c[0]) country = atoi(c);
  if (!field_switch(code, 'e', 0, e, sizeof e)) e[0] = 0;
  fill(mi, v, f, country, e, out, cap, NULL);
}
void merge_greeting(const MergeInfo *mi, int v, const char *code, char *out, size_t cap)
{
  char f[512], e[128];
  int names = 0;
  if (!field_switch(code, 'f', 0, f, sizeof f) || !f[0]) snprintf(f, sizeof f, "%s", DEF_GREETING);
  if (!field_switch(code, 'e', 0, e, sizeof e)) snprintf(e, sizeof e, "Dear Sir or Madam,");
  fill(mi, v, f, 1, NULL, out, cap, &names);
  if (!names) snprintf(out, cap, "%s", e);
}
/* the recipient's name formats of Word's Address Block window, and of its Greeting Line window */
const char *const merge_name_pics[] = { "Joshua", "Joshua Randall Jr.", "Joshua Q. Randall Jr.", "Mr. Josh Randall Jr.",
  "Mr. Josh Q. Randall Jr.", "Mr. Joshua Randall Jr.", "Mr. Joshua Q. Randall Jr.", "Joshua Randall", "Mr. Randall", "Randall, Joshua" };
const char *const merge_name_fmts[] = { "<<_FIRST0_>>", "<<_FIRST0_>><< _LAST0_>><< _SUFFIX0_>>", "<<_FIRST0_>><< _MIDDLE0_>><< _LAST0_>><< _SUFFIX0_>>",
  "<<_TITLE0_ >><<_NICK0_>><< _LAST0_>><< _SUFFIX0_>>", "<<_TITLE0_ >><<_NICK0_>><< _MIDDLE0_>><< _LAST0_>><< _SUFFIX0_>>",
  "<<_TITLE0_ >><<_FIRST0_>><< _LAST0_>><< _SUFFIX0_>>", "<<_TITLE0_ >><<_FIRST0_>><< _MIDDLE0_>><< _LAST0_>><< _SUFFIX0_>>",
  "<<_FIRST0_>><< _LAST0_>>", "<<_TITLE0_ >><<_LAST0_>>", "<<_LAST0_>><<, _FIRST0_>>" };
const int merge_nname = 10;
const char *const merge_greet_pics[] = { "Mr. Randall", "Joshua", "Josh", "Mr. Joshua Randall", "Mr. and Mrs. Randall", "Joshua and Cynthia", "Joshua Randall Jr." };
const char *const merge_greet_fmts[] = { "<<_TITLE0_ >><<_LAST0_>>", "<<_FIRST0_>>", "<<_NICK0_>>", "<<_TITLE0_ >><<_FIRST0_>><< _LAST0_>>",
  "<<_TITLE0_>><< and _TITLE1_>><< _LAST0_>>", "<<_FIRST0_>><< and _FIRST1_>>", "<<_FIRST0_>><< _LAST0_>><< _SUFFIX0_>>" };
const int merge_ngreet = 7;
/* a quoted field argument (" and \ escaped) */
static void put_q(Buf *b, const char *s)
{
  buf_addc(b, '"');
  for (; *s; s++) { if (*s == '"' || (*s == '\\' && s[1] != 'n')) buf_addc(b, '\\'); buf_addc(b, *s); }
  buf_addc(b, '"');
}
void merge_address_code(int name, int company, int postal, int country, const char *excluded, char *out, size_t cap)
{
  Buf b;
  buf_init(&b);
  buf_adds(&b, " ADDRESSBLOCK \\f \"");
  if (name >= 0 && name < merge_nname) buf_adds(&b, merge_name_fmts[name]);
  if (name >= 0 && name < merge_nname) buf_adds(&b, "\\n");
  if (company) buf_adds(&b, "<<_COMPANY_\\n>>");
  if (postal) {
    buf_adds(&b, "<<_STREET1_\\n>><<_STREET2_\\n>><<_CITY_>><<, _STATE_>><< _POSTAL_>>");
    if (country) buf_adds(&b, "<<\\n_COUNTRY_>>");
  }
  buf_adds(&b, "\" \\l 1033 \\c ");
  buf_addc(&b, '0' + (country < 0 || country > 2 ? 2 : country));
  if (excluded && excluded[0]) { buf_adds(&b, " \\e "); put_q(&b, excluded); }
  buf_adds(&b, " \\d ");
  buf_addc(&b, 0);
  snprintf(out, cap, "%s", b.oom ? "" : b.data);
  buf_free(&b);
}
void merge_greeting_code(int before, int name, int after, const char *invalid, char *out, size_t cap)
{
  static const char *const bef[] = { "Dear", "To", "" }, *const aft[] = { ",", ":", "" };
  Buf b;
  buf_init(&b);
  buf_adds(&b, " GREETINGLINE \\f \"");
  if (before >= 0 && before < 2) { buf_adds(&b, "<<_BEFORE_ "); buf_adds(&b, bef[before]); buf_adds(&b, " >>"); }
  if (name >= 0 && name < merge_ngreet) buf_adds(&b, merge_greet_fmts[name]);
  if (after >= 0 && after < 2) { buf_adds(&b, "<<_AFTER_ "); buf_adds(&b, aft[after]); buf_adds(&b, ">>"); }
  buf_adds(&b, "\" \\l 1033");
  if (invalid && invalid[0]) { buf_adds(&b, " \\e "); put_q(&b, invalid); }
  buf_addc(&b, ' ');
  buf_addc(&b, 0);
  snprintf(out, cap, "%s", b.oom ? "" : b.data);
  buf_free(&b);
}

/* ------------------------------------------------------------------ the fields */
static const char *const mnames[] = { "MERGEFIELD", "ADDRESSBLOCK", "GREETINGLINE", "ASK", "FILLIN", "IF", "MERGEREC", "MERGESEQ",
  "NEXT", "NEXTIF", "SET", "SKIPIF" };
int merge_is_field(const char *name)
{
  size_t i;
  for (i = 0; i < sizeof mnames / sizeof mnames[0]; i++) if (!strcmp(name, mnames[i])) return 1;
  return 0;
}
int merge_is_field_k(const Doc *d, int k)
{
  char nm[32];
  const Field *f = field_get(d, k);
  if (!f) return 0;
  field_name(f->code, nm, sizeof nm);
  return merge_is_field(nm);
}
#define MAXVARS 32
typedef struct {
  Doc *d;
  MergeInfo *mi;
  int act;                  /* the record: an index among the active ones (merge_active) */
  int v;                    /* ... and its view row, -1 past the last */
  int seq;                  /* MERGESEQ */
  int mode;                 /* RM_ */
  int to;                   /* the last record to merge (RM_BUILD) */
  int skip;                 /* SKIPIF was true */
  MergeAsk ask; void *actx;
  char var[MAXVARS][40]; char val[MAXVARS][256]; int nvar;
  int asked[64]; int nasked;   /* fields with \o already asked (their records) */
  Buf *errs;
  int changed;
} MRun;
enum { RM_PREVIEW, RM_BUILD };
static void run_rec(MRun *r)
{
  if (r->mode == RM_PREVIEW) r->v = r->act < r->mi->nview ? r->act : -1;
  else r->v = r->act <= r->to ? merge_active(r->mi, r->act) : -1;
}
static int run_merge(void *ctx, const char *name, char *out, size_t cap)
{
  MRun *r = (MRun *)ctx;
  if (merge_value(r->mi, r->v, name, out, cap)) return 1;
  if (r->mode == RM_BUILD) {
    if (r->errs) buf_printf(r->errs, "Record %d: the merge field \"%s\" isn't in the recipient list.\n", r->act + 1, name);
    if (cap) out[0] = 0;
    return 1;
  }
  return 0;
}
static const char *var_get(MRun *r, const char *n)
{
  int i;
  for (i = 0; i < r->nvar; i++) if (!ci_cmp(r->var[i], n)) return r->val[i];
  return NULL;
}
static void var_set(MRun *r, const char *n, const char *v)
{
  int i;
  for (i = 0; i < r->nvar && ci_cmp(r->var[i], n); i++) ;
  if (i == r->nvar) { if (r->nvar == MAXVARS) return; r->nvar++; }
  snprintf(r->var[i], sizeof r->var[i], "%s", n);
  snprintf(r->val[i], sizeof r->val[i], "%s", v);
}
/* a nested field's own value for the rules Quill works out itself (MERGEREC, MERGESEQ, a bookmark SET
   or ASK gave); 1 if it is one */
static int own_value(MRun *r, const char *code, char *out, size_t cap)
{
  char nm[64], a0[64];
  const char *v;
  field_name(code, nm, sizeof nm);
  if (!strcmp(nm, "MERGEREC") || !strcmp(nm, "MERGESEQ")) {
    long n = nm[5] == 'R' ? (r->v >= 0 ? r->v + 1 : 0) : r->seq;
    if (!field_format_number(code, n, out, cap)) snprintf(out, cap, "%ld", n);
    return 1;
  }
  if (!strcmp(nm, "REF") && field_arg(code, 0, a0, sizeof a0) && (v = var_get(r, a0))) { snprintf(out, cap, "%s", v); return 1; }
  if (!field_arg(code, 0, a0, sizeof a0)) {         /* a bare bookmark name: { name } */
    const char *c0 = code;
    char raw[64];
    size_t n = 0;
    while (*c0 == ' ') c0++;
    while (*c0 && *c0 != ' ' && n + 1 < sizeof raw) raw[n++] = *c0++;
    raw[n] = 0;
    if ((v = var_get(r, raw))) { snprintf(out, cap, "%s", v); return 1; }
  }
  return 0;
}
/* the code with nested MERGEREC, MERGESEQ and bookmark fields given their values (malloc'd) */
static char *subst(MRun *r, const char *code)
{
  Buf b;
  buf_init(&b);
  while (*code) {
    if (*code == FLD_OPEN) {
      const char *p = code + 1, *sep = NULL, *end;
      int depth = 0;
      char inner[600], val[300];
      for (; *p; p++) {
        if (*p == FLD_OPEN) depth++;
        else if (*p == FLD_SEP && !depth && !sep) sep = p;
        else if (*p == FLD_CLOSE) { if (!depth) break; depth--; }
      }
      end = *p ? p + 1 : p;
      {
        size_t il = (size_t)((sep ? sep : p) - code - 1);
        if (il >= sizeof inner) il = sizeof inner - 1;
        memcpy(inner, code + 1, il); inner[il] = 0;
      }
      if (own_value(r, inner, val, sizeof val)) {
        buf_addc(&b, FLD_OPEN); buf_adds(&b, inner); buf_addc(&b, FLD_SEP); buf_adds(&b, val); buf_addc(&b, FLD_CLOSE);
      } else buf_add(&b, code, (size_t)(end - code));
      code = end;
      continue;
    }
    buf_addc(&b, *code++);
  }
  if (b.oom) { buf_free(&b); return NULL; }
  return b.data ? buf_take(&b) : str_dup("");
}
/* the code after its name */
static const char *after_name(const char *code)
{
  while (*code == ' ') code++;
  while (*code && *code != ' ') code++;
  return code;
}
/* NEXTIF's and SKIPIF's comparison: worked out as IF's */
static int cond(MRun *r, const char *code, Pos at, const FieldEnv *e)
{
  Buf b;
  char out[16];
  int res = 0;
  buf_init(&b);
  buf_adds(&b, " IF");
  buf_adds(&b, after_name(code));
  buf_adds(&b, " \"1\" \"0\" ");
  buf_addc(&b, 0);
  if (!b.oom && field_eval_code(r->d, b.data, at, e, out, sizeof out) == FE_TEXT) res = !strcmp(out, "1");
  buf_free(&b);
  return res;
}
static int was_asked(MRun *r, int k)
{
  int i;
  for (i = 0; i < r->nasked; i++) if (r->asked[i] == k) return 1;
  if (r->nasked < 64) r->asked[r->nasked++] = k;
  return 0;
}
/* what field k (start mark at s) gives for the record: FE_TEXT and out, FE_KEEP (not a merge field, or
   one Quill can't work out) */
static int eval(MRun *r, int k, Pos s, char *out, size_t cap)
{
  const Field *f = field_get(r->d, k);
  char nm[32], a0[128], a1[256], def[256];
  char *cs;
  FieldEnv e;
  int res = FE_KEEP;
  if (!f) return FE_KEEP;
  field_name(f->code, nm, sizeof nm);
  if (cap) out[0] = 0;
  if (!(cs = subst(r, f->code))) return FE_KEEP;
  field_env(&e);
  e.merge = run_merge; e.ctx = r;
  if (!strcmp(nm, "MERGEFIELD") || !strcmp(nm, "IF")) res = field_eval_code(r->d, cs, s, &e, out, cap);
  else if (!strcmp(nm, "ADDRESSBLOCK")) { merge_address(r->mi, r->v, cs, out, cap); res = FE_TEXT; }
  else if (!strcmp(nm, "GREETINGLINE")) { merge_greeting(r->mi, r->v, cs, out, cap); res = FE_TEXT; }
  else if (!strcmp(nm, "MERGEREC") || !strcmp(nm, "MERGESEQ")) { own_value(r, cs, out, cap); res = FE_TEXT; }
  else if (!strcmp(nm, "NEXT")) { r->act++; run_rec(r); res = FE_TEXT; }
  else if (!strcmp(nm, "NEXTIF")) { if (cond(r, cs, s, &e)) { r->act++; run_rec(r); } res = FE_TEXT; }
  else if (!strcmp(nm, "SKIPIF")) { if (r->mode == RM_BUILD && cond(r, cs, s, &e)) r->skip = 1; res = FE_TEXT; }
  else if (!strcmp(nm, "SET")) {
    if (field_arg(cs, 0, a0, sizeof a0)) { field_arg(cs, 1, a1, sizeof a1); var_set(r, a0, a1); }
    res = FE_TEXT;
  } else if (!strcmp(nm, "ASK") || !strcmp(nm, "FILLIN")) {
    int ask = nm[0] == 'A', n0 = ask ? 1 : 0, once = field_switch(cs, 'o', 0, NULL, 0);
    char ans[256];
    if (ask && !field_arg(cs, 0, a0, sizeof a0)) a0[0] = 0;
    if (!field_arg(cs, n0, a1, sizeof a1)) a1[0] = 0;
    if (!field_switch(cs, 'd', 0, def, sizeof def)) def[0] = 0;
    snprintf(ans, sizeof ans, "%s", def);
    if (r->mode == RM_BUILD && r->ask && !(once && was_asked(r, k))) { if (!r->ask(r->actx, a1, def, ans, sizeof ans)) snprintf(ans, sizeof ans, "%s", def); }
    else if (r->mode == RM_BUILD && once && ask && var_get(r, a0)) snprintf(ans, sizeof ans, "%s", var_get(r, a0));
    else if (r->mode == RM_BUILD && once && !ask) { const char *pv; char key[16]; sprintf(key, "\x01%d", k); if ((pv = var_get(r, key))) snprintf(ans, sizeof ans, "%s", pv); }
    if (ask) { if (a0[0]) var_set(r, a0, ans); }
    else { char key[16]; sprintf(key, "\x01%d", k); var_set(r, key, ans); snprintf(out, cap, "%s", ans); }
    res = FE_TEXT;
  } else if (!strcmp(nm, "REF") || (nm[0] && !merge_is_field(nm))) {
    if (own_value(r, cs, out, cap)) res = FE_TEXT;
  }
  free(cs);
  return res;
}
/* the end mark of the field whose start mark is at s, and the formatting its result starts with */
static int field_span(Doc *d, Pos s, Pos *e, CharFmt *f)
{
  if (!field_end(d, s, e)) return 0;
  *f = *para_fmt_at(d->p[s.para], s.off + 4 < d->p[s.para]->len ? s.off + 4 : s.off);
  return 1;
}
/* field at s gets text as its result (unlink: the field replaced by it); *next where to go on looking */
static void put_result(MRun *r, Pos s, const char *text, int unlink, Pos *next)
{
  Doc *d = r->d;
  Pos e, rs, at;
  CharFmt f;
  char *old;
  u32 n;
  if (!field_span(d, s, &e, &f)) { next->para = s.para; next->off = s.off + 4; return; }
  rs.para = s.para; rs.off = s.off + 4;
  old = doc_text(d, rs, e, &n);
  if (!unlink && old && !strcmp(old, text)) {        /* (the same: nothing to do; tabs and line breaks compare as text) */
    free(old);
    *next = e; next->off += 4;
    return;
  }
  free(old);
  if (unlink) {
    Pos e4 = e;
    e4.off += 4;
    doc_delete(d, e, e4);
    if (pos_cmp(rs, e) < 0) doc_delete(d, rs, e);
    { Pos s4 = s; s4.off += 4; doc_delete(d, s, s4); }
    at = s;
  } else {
    if (pos_cmp(rs, e) < 0) doc_delete(d, rs, e);
    at = rs;
  }
  if (text[0]) doc_insert(d, &at, text, (u32)strlen(text), &f);
  *next = at;
  if (!unlink) next->off += 4;
  r->changed++;
}
/* the next field start mark at or after *p in paragraphs before pend; 0 if none */
static int next_start(Doc *d, Pos *p, u32 pend)
{
  for (; p->para < pend && p->para < d->np; p->para++, p->off = 0) {
    Para *q = d->p[p->para];
    for (; p->off + 4 <= q->len; p->off++) if (IS_FSTART(q->t + p->off)) return 1;
  }
  return 0;
}
/* the fields of paragraphs pa..pend-1 worked out for the run's records */
static void run_paras(MRun *r, u32 pa, u32 *pend)
{
  Pos p;
  char out[2048];
  p.para = pa; p.off = 0;
  while (!r->skip) {
    u32 before = r->d->np;
    int k, res;
    Pos next;
    if (!next_start(r->d, &p, *pend)) break;
    k = fmark_index(r->d->p[p.para]->t + p.off);
    res = eval(r, k, p, out, sizeof out);
    if (res == FE_TEXT) {
      put_result(r, p, out, r->mode == RM_BUILD, &next);
      p = next;
    } else { Pos e; if (field_end(r->d, p, &e)) { p = e; p.off += 4; } else p.off += 4; }
    *pend += r->d->np - before;          /* (results with paragraphs in them, or ones taken away) */
  }
}
/* what a merge field shows without data, Word's way */
static int placeholder(const Doc *d, int k, char *out, size_t cap)
{
  const Field *f = field_get(d, k);
  char nm[32], a[128];
  if (!f) return 0;
  field_name(f->code, nm, sizeof nm);
  if (!strcmp(nm, "MERGEFIELD")) { if (!field_arg(f->code, 0, a, sizeof a)) return 0; snprintf(out, cap, "\xC2\xAB%s\xC2\xBB", a); return 1; }
  if (!strcmp(nm, "ADDRESSBLOCK")) { snprintf(out, cap, "\xC2\xAB" "AddressBlock" "\xC2\xBB"); return 1; }
  if (!strcmp(nm, "GREETINGLINE")) { snprintf(out, cap, "\xC2\xAB" "GreetingLine" "\xC2\xBB"); return 1; }
  if (!strcmp(nm, "NEXT")) { snprintf(out, cap, "\xC2\xAB" "Next Record" "\xC2\xBB"); return 1; }
  if (!strcmp(nm, "NEXTIF")) { snprintf(out, cap, "\xC2\xAB" "Next Record If..." "\xC2\xBB"); return 1; }
  if (!strcmp(nm, "SKIPIF")) { snprintf(out, cap, "\xC2\xAB" "Skip Record If..." "\xC2\xBB"); return 1; }
  if (!strcmp(nm, "MERGEREC")) { snprintf(out, cap, "\xC2\xAB" "Merge Record #" "\xC2\xBB"); return 1; }
  if (!strcmp(nm, "MERGESEQ")) { snprintf(out, cap, "\xC2\xAB" "Merge Sequence #" "\xC2\xBB"); return 1; }
  if (!strcmp(nm, "ASK") || !strcmp(nm, "SET")) { out[0] = 0; return 1; }
  if (!strcmp(nm, "FILLIN")) { if (!field_switch(f->code, 'd', 0, out, cap)) out[0] = 0; return 1; }
  return 0;
}
int merge_preview(Doc *d, int on)
{
  MergeInfo *mi = merge_info(d);
  MRun r;
  Pos z;
  u32 pend = d->np;
  if (!mi || !d->nfields) return 0;
  memset(&r, 0, sizeof r);
  r.d = d; r.mi = mi; r.mode = RM_PREVIEW;
  r.act = mi->rec; run_rec(&r);
  if (on && mi->data) r.v = mi->rec < mi->nview ? mi->rec : -1;
  r.seq = 1;
  z.para = 0; z.off = 0;
  doc_begin(d, z);
  if (on) run_paras(&r, 0, &pend);
  else {
    Pos p = z, next;
    char out[512];
    while (next_start(d, &p, d->np)) {
      int k = fmark_index(d->p[p.para]->t + p.off);
      if (placeholder(d, k, out, sizeof out)) { put_result(&r, p, out, 0, &next); p = next; }
      else { Pos e; if (field_end(d, p, &e)) { p = e; p.off += 4; } else p.off += 4; }
    }
  }
  doc_end(d, z);
  mi->viewmerged = (u8)(on != 0);
  return r.changed;
}

/* ------------------------------------------------------------------ merging */
/* the paragraphs' tables given new numbers (copies of their tables), and bookmarks taken off */
static void renumber(Doc *d, u32 pa, u32 pb, int nobm)
{
  int map[64][2], nmap = 0, i;
  u32 pi, r;
  for (pi = pa; pi < pb; pi++) {
    Para *q = d->p[pi];
    Cell *cs[2];
    int c;
    cs[0] = &q->pf.cell; cs[1] = &q->pf.outer;
    for (c = 0; c < 2; c++) {
      u16 t = cs[c]->table;
      if (!t || t > d->ntables) continue;
      for (i = 0; i < nmap && map[i][0] != t; i++) ;
      if (i == nmap) {
        Table copy = d->tables[t - 1];
        int nt = doc_add_table(d, &copy);
        if (!nt || nmap == 64) continue;
        map[nmap][0] = t; map[nmap][1] = nt; nmap++;
      }
      cs[c]->table = (u16)map[i][1];
    }
    if (nobm) for (r = 0; r < q->nr; r++) q->r[r].f.bmark = 0;
    q->ver++;
  }
}
Doc *merge_build(Doc *src, int from, int to, MergeAsk ask, void *ctx, int *count, Buf *errs)
{
  MergeInfo *mi = src->merge;
  Doc *out = NULL;
  u8 *bytes;
  size_t len;
  unsigned lost = 0;
  char err[128];
  u32 nt, i, sectpara;
  int n = 0, total = merge_count(mi), first = 1;
  MRun r;
  SectFmt last;
  if (count) *count = 0;
  if (!mi || !total) return NULL;
  if (from < 0) from = 0;
  if (to >= total) to = total - 1;
  if (from > to) return NULL;
  /* a copy of the main document (through .docx, which carries everything) */
  if (!(bytes = docx_save(src, &len))) return NULL;
  out = docx_load(bytes, len, &lost, err, sizeof err);
  free(bytes);
  if (!out) return NULL;
  merge_free(out);
  nt = out->np;
  /* the last section's format, for the breaks between records */
  sectpara = nt - 1;
  if (out->p[sectpara]->pf.sect && out->p[sectpara]->pf.sect <= out->nsects) last = out->sects[out->p[sectpara]->pf.sect - 1];
  else sect_default(&last);
  last.start = SB_NEXT;
  if (!last.haspage) { last.haspage = 1; last.page = out->page; }
  memset(&r, 0, sizeof r);
  r.d = out; r.mi = mi; r.mode = RM_BUILD; r.ask = ask; r.actx = ctx; r.errs = errs;
  r.act = from; r.to = to;
  while (r.act <= to) {
    u32 pa = out->np, pend;
    /* the main document's paragraphs again, at the end */
    if (!first && mi->type != MT_DIRECTORY) {
      int sn = doc_add_sect(out, &last);
      Para *lp = out->p[out->np - 1];
      if (sn) { lp->pf.sect = (u16)sn; lp->ver++; }
    }
    for (i = 0; i < nt; i++) {
      Para *c = para_copy(out->p[i]);
      if (!c || !doc_insert_para(out, out->np, c)) { para_free(c); goto oom; }
    }
    pend = out->np;
    renumber(out, pa, pend, !first);
    r.skip = 0;
    run_rec(&r);
    r.seq = n + 1;
    run_paras(&r, pa, &pend);
    if (r.skip) {
      /* SKIPIF: this record's copy goes */
      Para **none = NULL;
      if (pend > pa) doc_replace(out, pa, out->np - pa, none, 0, doc_start(out));
      if (!first && mi->type != MT_DIRECTORY) { Para *lp = out->p[out->np - 1]; lp->pf.sect = 0; lp->ver++; }
    } else { n++; first = 0; }
    r.act++;                             /* (past the records NEXT used too) */
    doc_clear_undo(out);
  }
  if (!n) { doc_free(out); return NULL; }
  /* the main document itself taken out */
  doc_replace(out, 0, nt, NULL, 0, doc_start(out));
  doc_clear_undo(out);
  out->dirty = 1;
  if (count) *count = n;
  return out;
oom:
  doc_free(out);
  return NULL;
}

/* ------------------------------------------------------------------ checking and finding */
int merge_unknown_fields(Doc *d, char names[][64], int max)
{
  MergeInfo *mi = d->merge;
  int k, n = 0, i;
  char nm[32], a[64];
  for (k = 0; k < d->nfields; k++) {
    field_name(d->fields[k].code, nm, sizeof nm);
    if (strcmp(nm, "MERGEFIELD") || !field_arg(d->fields[k].code, 0, a, sizeof a)) continue;
    if (mi && mi->data && merge_value(mi, -1, a, NULL, 0)) continue;
    for (i = 0; i < n && ci_cmp(names[i], a); i++) ;
    if (i == n && n < max) snprintf(names[n++], 64, "%s", a);
  }
  return n;
}
/* whether the body has field k in it */
static int used(Doc *d, int k)
{
  u32 pi, o;
  for (pi = 0; pi < d->np; pi++) {
    Para *q = d->p[pi];
    for (o = 0; o + 4 <= q->len; o++) if (IS_FSTART(q->t + o) && fmark_index(q->t + o) == k) return 1;
  }
  return 0;
}
int merge_check(Doc *d, Buf *errs)
{
  MergeInfo *mi = d->merge;
  int k, n = 0, i, nu = 0, any = 0;
  char unk[32][64], nm[32];
  if (!mi || mi->type == MT_NONE) { buf_printf(errs, "This isn't a mail merge main document (Start Mail Merge).\n"); return 1; }
  if (!mi->data) { buf_printf(errs, "No recipient list has been chosen (Select Recipients).\n"); n++; }
  else if (!merge_count(mi)) { buf_printf(errs, "No recipients are checked or none pass the filter.\n"); n++; }
  for (k = 0; k < d->nfields; k++) { field_name(d->fields[k].code, nm, sizeof nm); if (merge_is_field(nm) && used(d, k)) any = 1; }
  if (!any) { buf_printf(errs, "The document has no merge fields: every copy will be the same.\n"); n++; }
  if (mi->data) {
    nu = merge_unknown_fields(d, unk, 32);
    for (i = 0; i < nu; i++) { buf_printf(errs, "The merge field \"%s\" isn't in the recipient list (Match Fields, or Insert Merge Field).\n", unk[i]); n++; }
    for (k = 0; k < d->nfields; k++) {
      field_name(d->fields[k].code, nm, sizeof nm);
      if ((!strcmp(nm, "ADDRESSBLOCK") || !strcmp(nm, "GREETINGLINE")) && used(d, k) &&
          merge_af_col(mi, AF_LAST) < 0 && merge_af_col(mi, AF_FIRST) < 0) { buf_printf(errs, "%s: no column is matched to the recipient's name (Match Fields).\n", nm); n++; break; }
    }
  }
  /* the merge itself, simulated, for its own errors */
  if (mi->data && merge_count(mi)) {
    Buf e2;
    int got = 0;
    Doc *m;
    buf_init(&e2);
    if ((m = merge_build(d, 0, merge_count(mi) - 1, NULL, NULL, &got, &e2))) doc_free(m);
    if (e2.len && !nu) { buf_add(errs, e2.data, e2.len); n++; }
    buf_free(&e2);
  }
  return n;
}
int merge_find(const MergeInfo *mi, int v, const char *what, int col)
{
  int i, c;
  if (!mi || !mi->data || !what[0]) return -1;
  for (i = 1; i <= mi->nview; i++) {
    int w = (v + i) % mi->nview, row = mi->view[w];
    for (c = 0; c < mi->data->ncols; c++) if ((col < 0 || c == col) && ci_contains(mi->data->cell[row][c], what)) return w;
  }
  return -1;
}
static int trim_eq(const char *a, const char *b)
{
  const char *ae, *be;
  while (*a == ' ') a++;
  while (*b == ' ') b++;
  ae = a + strlen(a); be = b + strlen(b);
  while (ae > a && ae[-1] == ' ') ae--;
  while (be > b && be[-1] == ' ') be--;
  if (ae - a != be - b) return 0;
  for (; a < ae; a++, b++) { int x = (u8)*a, y = (u8)*b; if (x >= 'a' && x <= 'z') x -= 32; if (y >= 'a' && y <= 'z') y -= 32; if (x != y) return 0; }
  return 1;
}
int merge_duplicates(const MergeData *m, u8 *dup)
{
  int i, j, c, n = 0;
  memset(dup, 0, (size_t)m->nrows);
  for (i = 1; i < m->nrows; i++)
    for (j = 0; j < i; j++) {
      if (dup[j]) continue;
      for (c = 0; c < m->ncols && trim_eq(m->cell[i][c], m->cell[j][c]); c++) ;
      if (c == m->ncols) { dup[i] = 1; n++; break; }
    }
  return n;
}
int merge_validate(const MergeInfo *mi, u8 *bad)
{
  int i, n = 0;
  int cf = merge_af_col(mi, AF_FIRST), cl = merge_af_col(mi, AF_LAST), cc = merge_af_col(mi, AF_COMPANY), cs = merge_af_col(mi, AF_ADDR1);
  int ct = merge_af_col(mi, AF_CITY), cp = merge_af_col(mi, AF_POSTAL), cn = merge_af_col(mi, AF_COUNTRY);
  if (!mi->data) return 0;
  memset(bad, 0, (size_t)mi->data->nrows);
  for (i = 0; i < mi->data->nrows; i++) {
    char **r = mi->data->cell[i];
    int name = (cf >= 0 && r[cf][0]) || (cl >= 0 && r[cl][0]) || (cc >= 0 && r[cc][0]);
    int ok = name && cs >= 0 && r[cs][0] && ct >= 0 && r[ct][0] && ((cp >= 0 && r[cp][0]) || (cn >= 0 && r[cn][0]));
    if (!ok) { bad[i] = 1; n++; }
  }
  return n;
}
