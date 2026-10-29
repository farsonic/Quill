/* cite.c - citations and bibliography (WP3); see cite.h. Written from Word's documented behaviour and
   the published style guides (APA, MLA, Chicago, Harvard, IEEE, ISO 690, GB/T 7714, GOST 7.1, SIST 02,
   Turabian), in Quill's own code */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cite.h"
#include "field.h"
#include "keep.h"
#include "xml.h"

#define BIBNS "http://schemas.openxmlformats.org/officeDocument/2006/bibliography"

/* ------------------------------------------------------------------ */
/* sources */
static char *dupn(const char *s) { return str_dup(s ? s : ""); }
static void role_free(CiteRole *r)
{
  int i;
  for (i = 0; i < r->np; i++) { free(r->p[i].last); free(r->p[i].first); free(r->p[i].middle); }
  free(r->p); free(r->corp);
  r->p = NULL; r->np = 0; r->corp = NULL;
}
void cite_source_free(CiteSource *s)
{
  int i;
  if (!s) return;
  for (i = 0; i < s->nkv; i++) { free(s->kv[i].k); free(s->kv[i].v); }
  for (i = 0; i < s->nr; i++) role_free(&s->r[i]);
  free(s->kv); free(s->r); free(s->extra);
  memset(s, 0, sizeof *s);
}
int cite_source_copy(CiteSource *to, const CiteSource *f)
{
  int i, j, bad = 0;
  memset(to, 0, sizeof *to);
  if (f->nkv && !(to->kv = (CiteKV *)calloc((size_t)f->nkv, sizeof(CiteKV)))) return 0;
  for (i = 0; i < f->nkv; i++) {
    to->kv[i].k = dupn(f->kv[i].k); to->kv[i].v = dupn(f->kv[i].v);
    to->nkv++;
    if (!to->kv[i].k || !to->kv[i].v) bad = 1;
  }
  if (f->nr && !(to->r = (CiteRole *)calloc((size_t)f->nr, sizeof(CiteRole)))) bad = 1;
  for (i = 0; to->r && i < f->nr; i++) {
    const CiteRole *fr = &f->r[i];
    CiteRole *tr = &to->r[i];
    to->nr++;
    memcpy(tr->role, fr->role, sizeof tr->role);
    if (fr->corp && !(tr->corp = str_dup(fr->corp))) bad = 1;
    if (fr->np && !(tr->p = (CitePerson *)calloc((size_t)fr->np, sizeof(CitePerson)))) { bad = 1; continue; }
    for (j = 0; j < fr->np; j++) {
      tr->p[j].last = dupn(fr->p[j].last); tr->p[j].first = dupn(fr->p[j].first); tr->p[j].middle = dupn(fr->p[j].middle);
      tr->np++;
      if (!tr->p[j].last || !tr->p[j].first || !tr->p[j].middle) bad = 1;
    }
  }
  if (f->extra && !(to->extra = str_dup(f->extra))) bad = 1;
  if (bad) { cite_source_free(to); return 0; }
  return 1;
}
const char *cite_get(const CiteSource *s, const char *k)
{
  int i;
  for (i = 0; i < s->nkv; i++) if (!strcmp(s->kv[i].k, k)) return s->kv[i].v;
  return "";
}
int cite_set(CiteSource *s, const char *k, const char *v)
{
  int i;
  CiteKV *n;
  for (i = 0; i < s->nkv; i++)
    if (!strcmp(s->kv[i].k, k)) {
      char *c;
      if (!v || !v[0]) {
        free(s->kv[i].k); free(s->kv[i].v);
        memmove(&s->kv[i], &s->kv[i + 1], (size_t)(s->nkv - i - 1) * sizeof(CiteKV));
        s->nkv--;
        return 1;
      }
      if (!(c = str_dup(v))) return 0;
      free(s->kv[i].v); s->kv[i].v = c;
      return 1;
    }
  if (!v || !v[0]) return 1;
  if (!(n = (CiteKV *)realloc(s->kv, (size_t)(s->nkv + 1) * sizeof(CiteKV)))) return 0;
  s->kv = n;
  n[s->nkv].k = str_dup(k); n[s->nkv].v = str_dup(v);
  if (!n[s->nkv].k || !n[s->nkv].v) { free(n[s->nkv].k); free(n[s->nkv].v); return 0; }
  s->nkv++;
  return 1;
}
const CiteRole *cite_role_c(const CiteSource *s, const char *role)
{
  int i;
  for (i = 0; i < s->nr; i++) if (!strcmp(s->r[i].role, role)) return &s->r[i];
  return NULL;
}
CiteRole *cite_role(CiteSource *s, const char *role, int create)
{
  CiteRole *r = (CiteRole *)cite_role_c(s, role), *n;
  if (r || !create) return r;
  if (!(n = (CiteRole *)realloc(s->r, (size_t)(s->nr + 1) * sizeof(CiteRole)))) return NULL;
  s->r = n;
  r = &n[s->nr++];
  memset(r, 0, sizeof *r);
  snprintf(r->role, sizeof r->role, "%s", role);
  return r;
}
static void role_drop(CiteSource *s, const char *role)
{
  int i;
  for (i = 0; i < s->nr; i++)
    if (!strcmp(s->r[i].role, role)) {
      role_free(&s->r[i]);
      memmove(&s->r[i], &s->r[i + 1], (size_t)(s->nr - i - 1) * sizeof(CiteRole));
      s->nr--;
      return;
    }
}
/* s[a..b) trimmed, malloc'd */
static char *trimdup(const char *a, const char *b)
{
  while (a < b && (*a == ' ' || *a == '\t')) a++;
  while (b > a && (b[-1] == ' ' || b[-1] == '\t')) b--;
  return str_ndup(a, (size_t)(b - a));
}
static int add_person(CiteRole *r, const char *last, const char *first, const char *middle)
{
  CitePerson *n = (CitePerson *)realloc(r->p, (size_t)(r->np + 1) * sizeof(CitePerson));
  if (!n) return 0;
  r->p = n;
  n[r->np].last = dupn(last); n[r->np].first = dupn(first); n[r->np].middle = dupn(middle);
  if (!n[r->np].last || !n[r->np].first || !n[r->np].middle) { free(n[r->np].last); free(n[r->np].first); free(n[r->np].middle); return 0; }
  r->np++;
  return 1;
}
int cite_set_names(CiteSource *s, const char *role, const char *text, int corp)
{
  CiteRole *r;
  const char *p = text ? text : "", *e;
  while (*p == ' ') p++;
  role_drop(s, role);
  if (!*p) return 1;
  if (!(r = cite_role(s, role, 1))) return 0;
  if (corp) { char *c = trimdup(p, p + strlen(p)); if (!c) return 0; r->corp = c; return 1; }
  while (*p) {
    char *one, *comma, *last, *rest, *mid;
    e = strchr(p, ';');
    if (!e) e = p + strlen(p);
    if (!(one = trimdup(p, e))) return 0;
    if (one[0]) {
      if ((comma = strchr(one, ','))) {
        /* "Last, First Middle" */
        *comma = 0;
        last = one; rest = comma + 1;
        while (*rest == ' ') rest++;
        mid = strchr(rest, ' ');
        if (mid) { *mid++ = 0; while (*mid == ' ') mid++; } else mid = (char *)"";
        if (!add_person(r, last, rest, mid)) { free(one); return 0; }
      } else {
        /* "First Middle Last" */
        char *sp = strrchr(one, ' ');
        if (!sp) { if (!add_person(r, one, "", "")) { free(one); return 0; } }
        else {
          *sp = 0;
          mid = strchr(one, ' ');
          if (mid) { *mid++ = 0; while (*mid == ' ') mid++; } else mid = (char *)"";
          if (!add_person(r, sp + 1, one, mid)) { free(one); return 0; }
        }
      }
    }
    free(one);
    p = *e ? e + 1 : e;
  }
  if (!r->np) role_drop(s, role);
  return 1;
}
void cite_names_text(const CiteSource *s, const char *role, char *out, size_t cap)
{
  const CiteRole *r = cite_role_c(s, role);
  size_t n = 0;
  int i;
  if (cap) out[0] = 0;
  if (!r) return;
  if (r->corp) { snprintf(out, cap, "%s", r->corp); return; }
  for (i = 0; i < r->np && n + 1 < cap; i++) {
    const CitePerson *p = &r->p[i];
    snprintf(out + n, cap - n, "%s%s%s%s%s%s", i ? "; " : "", p->last, p->first[0] || p->middle[0] ? ", " : "", p->first,
             p->middle[0] ? " " : "", p->middle);
    n = strlen(out);
  }
}
int cite_is_placeholder(const CiteSource *s) { return !cite_get(s, "SourceType")[0]; }

/* ------------------------------------------------------------------ */
/* lists */
void cite_list_free(CiteList *l)
{
  int i;
  for (i = 0; i < l->n; i++) cite_source_free(&l->s[i]);
  free(l->s); free(l->selstyle); free(l->stylename); free(l->version);
  memset(l, 0, sizeof *l);
}
int cite_find(const CiteList *l, const char *tag)
{
  int i;
  if (!tag || !tag[0]) return -1;
  for (i = 0; i < l->n; i++) if (str_ieq(cite_get(&l->s[i], "Tag"), tag)) return i;
  return -1;
}
int cite_add(CiteList *l, const CiteSource *s)
{
  CiteSource c, *n;
  int i = cite_find(l, cite_get(s, "Tag"));
  if (!cite_source_copy(&c, s)) return -1;
  if (i >= 0) { cite_source_free(&l->s[i]); l->s[i] = c; return i; }
  if (!(n = (CiteSource *)realloc(l->s, (size_t)(l->n + 1) * sizeof(CiteSource)))) { cite_source_free(&c); return -1; }
  l->s = n;
  n[l->n] = c;
  return l->n++;
}
void cite_remove(CiteList *l, int i)
{
  if (i < 0 || i >= l->n) return;
  cite_source_free(&l->s[i]);
  memmove(&l->s[i], &l->s[i + 1], (size_t)(l->n - i - 1) * sizeof(CiteSource));
  l->n--;
}
/* the first letters of a UTF-8 string, n characters */
static void first_chars(const char *s, int n, char *out, size_t cap)
{
  size_t i = 0, len;
  int k = 0;
  while (*s == ' ' || *s == '"' || *s == '\'') s++;
  len = strlen(s);
  while (i < len && k < n && s[i] != ' ') { i = utf8_next(s, len, i); k++; }
  if (i >= cap) i = cap - 1;
  memcpy(out, s, i); out[i] = 0;
}
void cite_make_tag(const CiteList *l, const CiteSource *s, char *out, size_t cap)
{
  const CiteRole *r = cite_role_c(s, "Author");
  const char *base = "", *y = cite_get(s, "Year");
  char b[64], t[80];
  size_t yl = strlen(y);
  int k;
  if (r && r->np) base = r->p[0].last;
  else if (r && r->corp) base = r->corp;
  else if (s->nr && s->r[0].np) base = s->r[0].p[0].last;
  else base = cite_get(s, "Title");
  first_chars(base, 3, b, sizeof b);
  if (!b[0]) snprintf(b, sizeof b, "Src");
  snprintf(t, sizeof t, "%s%s", b, yl >= 2 ? y + yl - 2 : "");
  snprintf(out, cap, "%s", t);
  for (k = 1; l && cite_find(l, out) >= 0; k++) snprintf(out, cap, "%s%d", t, k);
}
void cite_make_guid(char *out)
{
  static unsigned long seed;
  unsigned long v[4];
  int i;
  if (!seed) seed = (unsigned long)time(NULL) ^ 0x5DEECE66UL;
  for (i = 0; i < 4; i++) { seed = seed * 1103515245UL + 12345UL; v[i] = (seed >> 3) & 0xFFFFFFFFUL; seed ^= v[i] << 7; }
  sprintf(out, "{%08lX-%04lX-%04lX-%04lX-%04lX%08lX}", v[0] & 0xFFFFFFFFUL, v[1] >> 16 & 0xFFFF, (v[1] & 0x0FFF) | 0x4000,
          (v[2] >> 16 & 0x3FFF) | 0x8000, v[2] & 0xFFFF, v[3] & 0xFFFFFFFFUL);
}

/* ---- XML */
/* the text of a simple element whose raw form is raw (its children: 0 none, 1 elements inside) */
static char *elem_text(const char *raw, size_t n, int *kids)
{
  Xml x;
  Buf b;
  int depth = 0;
  buf_init(&b);
  *kids = 0;
  xml_init(&x, raw, n);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START) { if (++depth > 1) *kids = 1; }
    else if (x.type == XT_END) depth--;
    else if (x.type == XT_TEXT && depth == 1) buf_add(&b, x.text.data, x.text.len);
  }
  xml_free(&x);
  if (!b.data) buf_adds(&b, "");
  return buf_take(&b);
}
/* b:Author's children: each a role with b:NameList/b:Person or b:Corporate */
static void read_roles(Xml *x, CiteSource *s)
{
  int d0 = x->depth;
  CiteRole *r = NULL;
  char last[256], first[256], mid[256], *cur = NULL;
  Buf corp;
  buf_init(&corp);
  last[0] = first[0] = mid[0] = 0;
  while (xml_next(x) != XT_EOF && x->depth >= d0) {
    if (x->type == XT_START) {
      if (x->depth == d0 + 1) r = cite_role(s, x->name, 1);
      else if (!strcmp(x->name, "Person")) { last[0] = first[0] = mid[0] = 0; cur = NULL; }
      else if (!strcmp(x->name, "Last")) cur = last;
      else if (!strcmp(x->name, "First")) cur = first;
      else if (!strcmp(x->name, "Middle")) cur = mid;
      else if (!strcmp(x->name, "Corporate")) { buf_free(&corp); buf_init(&corp); cur = (char *)&corp; }
    } else if (x->type == XT_TEXT && cur) {
      if (cur == (char *)&corp) buf_add(&corp, x->text.data, x->text.len);
      else { size_t l = strlen(cur); snprintf(cur + l, 256 - l, "%.*s", (int)(x->text.len < 255 ? x->text.len : 255), x->text.data); }
    } else if (x->type == XT_END) {
      if (!strcmp(x->name, "Person") && r) add_person(r, last, first, mid);
      else if (!strcmp(x->name, "Corporate") && r) { free(r->corp); r->corp = str_dup(corp.data ? corp.data : ""); }
      if (strcmp(x->name, "Person")) cur = NULL;
    }
  }
  buf_free(&corp);
}
int cite_parse(CiteList *l, const char *xml, size_t n)
{
  Xml x;
  int ok = 0;
  cite_list_free(l);
  xml_init(&x, xml, n);
  while (xml_next(&x) != XT_EOF) {
    if (x.type != XT_START) continue;
    if (x.depth == 1) {
      const char *a;
      if (strcmp(x.name, "Sources")) break;
      ok = 1;
      if ((a = xml_attr(&x, "SelectedStyle"))) l->selstyle = str_dup(a);
      if ((a = xml_attr(&x, "StyleName"))) l->stylename = str_dup(a);
      if ((a = xml_attr(&x, "Version"))) l->version = str_dup(a);
    } else if (x.depth == 2 && !strcmp(x.name, "Source")) {
      CiteSource s, *ns;
      memset(&s, 0, sizeof s);
      while (xml_next(&x) != XT_EOF && x.depth >= 2) {
        const char *raw;
        size_t rl;
        if (x.type != XT_START) continue;
        if (!strcmp(x.name, "Author")) { read_roles(&x, &s); continue; }
        {
          char nm[64];
          int kids;
          char *t;
          snprintf(nm, sizeof nm, "%s", x.name);
          rl = xml_raw(&x, &raw);
          if (!(t = elem_text(raw, rl, &kids))) continue;
          if (kids) {
            Buf e;
            buf_init(&e);
            if (s.extra) buf_adds(&e, s.extra);
            buf_add(&e, raw, rl);
            free(s.extra); s.extra = buf_take(&e);
          } else cite_set(&s, nm, t[0] ? t : " ");   /* (an empty element kept as a space, then trimmed) */
          free(t);
        }
      }
      { int i; for (i = 0; i < s.nkv; i++) if (!strcmp(s.kv[i].v, " ")) s.kv[i].v[0] = 0; }
      if ((ns = (CiteSource *)realloc(l->s, (size_t)(l->n + 1) * sizeof(CiteSource)))) { l->s = ns; ns[l->n++] = s; }
      else cite_source_free(&s);
    }
  }
  xml_free(&x);
  return ok;
}
static void esc(Buf *b, const char *s) { xml_escape(b, s, strlen(s)); }
static void put_elem(Buf *b, const char *k, const char *v)
{
  buf_printf(b, "<b:%s>", k); esc(b, v); buf_printf(b, "</b:%s>", k);
}
char *cite_xml(const CiteList *l, size_t *n)
{
  Buf b;
  int i, j, k;
  static const char *const first[] = { "Tag", "SourceType", "Guid", "LCID" };
  buf_init(&b);
  buf_adds(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\r\n<b:Sources");
  if (l->selstyle) { buf_adds(&b, " SelectedStyle=\""); esc(&b, l->selstyle); buf_adds(&b, "\""); }
  if (l->stylename) { buf_adds(&b, " StyleName=\""); esc(&b, l->stylename); buf_adds(&b, "\""); }
  if (l->version) { buf_adds(&b, " Version=\""); esc(&b, l->version); buf_adds(&b, "\""); }
  buf_adds(&b, " xmlns:b=\"" BIBNS "\" xmlns=\"" BIBNS "\">");
  for (i = 0; i < l->n; i++) {
    const CiteSource *s = &l->s[i];
    buf_adds(&b, "<b:Source>");
    for (k = 0; k < 4; k++) { const char *v = cite_get(s, first[k]); if (v[0]) put_elem(&b, first[k], v); }
    if (s->nr) {
      buf_adds(&b, "<b:Author>");
      for (j = 0; j < s->nr; j++) {
        const CiteRole *r = &s->r[j];
        buf_printf(&b, "<b:%s>", r->role);
        if (r->corp) put_elem(&b, "Corporate", r->corp);
        else {
          int p;
          buf_adds(&b, "<b:NameList>");
          for (p = 0; p < r->np; p++) {
            buf_adds(&b, "<b:Person>");
            if (r->p[p].last[0]) put_elem(&b, "Last", r->p[p].last);
            if (r->p[p].first[0]) put_elem(&b, "First", r->p[p].first);
            if (r->p[p].middle[0]) put_elem(&b, "Middle", r->p[p].middle);
            buf_adds(&b, "</b:Person>");
          }
          buf_adds(&b, "</b:NameList>");
        }
        buf_printf(&b, "</b:%s>", r->role);
      }
      buf_adds(&b, "</b:Author>");
    }
    for (j = 0; j < s->nkv; j++) {
      for (k = 0; k < 4 && strcmp(s->kv[j].k, first[k]); k++) ;
      if (k == 4) put_elem(&b, s->kv[j].k, s->kv[j].v);
    }
    if (s->extra) buf_adds(&b, s->extra);
    buf_adds(&b, "</b:Source>");
  }
  buf_adds(&b, "</b:Sources>");
  if (b.oom) { buf_free(&b); return NULL; }
  *n = b.len;
  return buf_take(&b);
}
int cite_load_file(CiteList *l, const char *path)
{
  FILE *f = fopen(path, "rb");
  Buf b;
  char tmp[2048];
  size_t n;
  int ok;
  if (!f) return 0;
  buf_init(&b);
  while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) buf_add(&b, tmp, n);
  fclose(f);
  ok = !b.oom && b.data && cite_parse(l, b.data, b.len);
  buf_free(&b);
  return ok;
}
int cite_save_file(const CiteList *l, const char *path)
{
  size_t n;
  char *x = cite_xml(l, &n);
  FILE *f;
  int ok;
  if (!x) return 0;
  if (!(f = fopen(path, "wb"))) { free(x); return 0; }
  ok = fwrite(x, 1, n, f) == n;
  if (fclose(f)) ok = 0;
  free(x);
  return ok;
}

/* ------------------------------------------------------------------ */
/* styles */
const CiteStyleDef cite_styles[CITE_NSTYLES] = {
  { "APA Sixth Edition", "APA 6th", "\\APASixthEditionOfficeOnline.xsl", "APA", "6", "References" },
  { "APA Seventh Edition", "APA 7th", "\\APASeventhEdition.xsl", "APA", "7", "References" },
  { "Chicago Sixteenth Edition", "Chicago 16th", "\\CHICAGO.XSL", "Chicago", "16", "Bibliography" },
  { "Chicago Seventeenth Edition", "Chicago 17th", "\\ChicagoSeventeenthEdition.xsl", "Chicago", "17", "Bibliography" },
  { "GB7714 2005", "GB7714", "\\GB.XSL", "GB7714", "2005", "References" },
  { "GOST - Name Sort", "GOST name", "\\GostName.XSL", "GOST - Name Sort", "2003", "References" },
  { "GOST - Title Sort", "GOST title", "\\GostTitle.XSL", "GOST - Title Sort", "2003", "References" },
  { "Harvard - Anglia 2008", "Harvard", "\\HarvardAnglia2008OfficeOnline.xsl", "Harvard - Anglia", "2008", "Bibliography" },
  { "IEEE 2006", "IEEE", "\\IEEE2006OfficeOnline.xsl", "IEEE", "2006", "References" },
  { "ISO 690 - First Element and Date", "ISO 690", "\\ISO690.XSL", "ISO 690 - First Element and Date", "1987", "Bibliography" },
  { "ISO 690 - Numerical Reference", "ISO 690 num.", "\\ISO690Nmerical.XSL", "ISO 690 - Numerical Reference", "1987", "Bibliography" },
  { "MLA Seventh Edition", "MLA 7th", "\\MLASeventhEditionOfficeOnline.xsl", "MLA", "7", "Works Cited" },
  { "MLA Eighth Edition", "MLA 8th", "\\MLAEighthEdition.xsl", "MLA", "8", "Works Cited" },
  { "MLA Ninth Edition", "MLA 9th", "\\MLANinthEdition.xsl", "MLA", "9", "Works Cited" },
  { "SIST02", "SIST02", "\\SIST02.XSL", "SIST02", "2003", "References" },
  { "Turabian Sixth Edition", "Turabian", "\\TURABIAN.XSL", "Turabian", "6", "Bibliography" } };
/* lower-case letters and digits of s */
static void squash(const char *s, char *out, size_t cap)
{
  size_t n = 0;
  for (; *s && n + 1 < cap; s++) {
    char c = *s;
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[n++] = c;
  }
  out[n] = 0;
}
int cite_style_of(const CiteList *l)
{
  char a[128], b[128];
  int i;
  if (l->selstyle) {
    squash(l->selstyle, a, sizeof a);
    for (i = 0; i < CITE_NSTYLES; i++) { squash(cite_styles[i].xsl, b, sizeof b); if (!strcmp(a, b)) return i; }
  }
  if (l->stylename) {
    int best = -1;
    squash(l->stylename, a, sizeof a);
    for (i = 0; i < CITE_NSTYLES; i++) {
      squash(cite_styles[i].stylename, b, sizeof b);
      if (!strcmp(a, b)) {
        if (best < 0) best = i;
        if (l->version && !strcmp(l->version, cite_styles[i].version)) return i;
      }
    }
    if (best >= 0) return best;
    /* a style Word knows by another name: the nearest family (APA, MLA ...) */
    for (i = 0; i < CITE_NSTYLES; i++) { squash(cite_styles[i].stylename, b, sizeof b); if (b[0] && !strncmp(a, b, strlen(b) < 3 ? strlen(b) : 3)) return i; }
  }
  return CS_APA6;
}
int cite_list_set_style(CiteList *l, int cs)
{
  char *a, *b, *c;
  if (cs < 0 || cs >= CITE_NSTYLES) return 0;
  a = str_dup(cite_styles[cs].xsl); b = str_dup(cite_styles[cs].stylename); c = str_dup(cite_styles[cs].version);
  if (!a || !b || !c) { free(a); free(b); free(c); return 0; }
  free(l->selstyle); free(l->stylename); free(l->version);
  l->selstyle = a; l->stylename = b; l->version = c;
  return 1;
}
int cite_numbered(int cs)
{
  return cs == CS_IEEE || cs == CS_ISO690NUM || cs == CS_GB7714 || cs == CS_GOSTNAME || cs == CS_GOSTTITLE || cs == CS_SIST02;
}

/* ------------------------------------------------------------------ */
/* Word's source types and fields */
static const char *const roles[] = { "Author", "Editor", "Translator", "BookAuthor", "Compiler", "Composer", "Conductor", "Counsel",
  "Director", "Interviewee", "Interviewer", "Inventor", "Performer", "ProducerName", "Writer", "Artist", NULL };
int cite_is_role(const char *k)
{
  int i;
  for (i = 0; roles[i]; i++) if (!strcmp(roles[i], k)) return 1;
  return 0;
}
static const struct { const char *k, *label; } field_labels[] = {
  { "Author", "Author" }, { "Editor", "Editor" }, { "Translator", "Translator" }, { "BookAuthor", "Book Author" },
  { "Compiler", "Compiler" }, { "Composer", "Composer" }, { "Conductor", "Conductor" }, { "Counsel", "Counsel" },
  { "Director", "Director" }, { "Interviewee", "Interviewee" }, { "Interviewer", "Interviewer" }, { "Inventor", "Inventor" },
  { "Performer", "Performer" }, { "ProducerName", "Producer Name" }, { "Writer", "Writer" }, { "Artist", "Artist" },
  { "Title", "Title" }, { "Year", "Year" }, { "Month", "Month" }, { "Day", "Day" }, { "City", "City" },
  { "StateProvince", "State/Province" }, { "CountryRegion", "Country/Region" }, { "Publisher", "Publisher" },
  { "Volume", "Volume" }, { "NumberVolumes", "Number of Volumes" }, { "Edition", "Edition" }, { "Pages", "Pages" },
  { "ShortTitle", "Short Title" }, { "StandardNumber", "Standard Number" }, { "Comments", "Comments" }, { "Medium", "Medium" },
  { "YearAccessed", "Year Accessed" }, { "MonthAccessed", "Month Accessed" }, { "DayAccessed", "Day Accessed" },
  { "URL", "URL" }, { "DOI", "DOI" }, { "JournalName", "Journal Name" }, { "PeriodicalTitle", "Periodical Title" },
  { "Issue", "Issue" }, { "BookTitle", "Book Title" }, { "ConferenceName", "Conference Publication Name" },
  { "Institution", "Institution" }, { "Department", "Department" }, { "ThesisType", "Report Type" },
  { "ChapterNumber", "Chapter Number" }, { "InternetSiteTitle", "Name of Web Site" }, { "ProductionCompany", "Production Company" },
  { "Distributor", "Distributor" }, { "AlbumTitle", "Album Title" }, { "Theater", "Theater" }, { "Station", "Station" },
  { "Broadcaster", "Broadcaster" }, { "BroadcastTitle", "Broadcast Title" }, { "Court", "Court" }, { "Reporter", "Reporter" },
  { "CaseNumber", "Case Number" }, { "AbbreviatedCaseNumber", "Abbreviated Case Number" }, { "PatentNumber", "Patent Number" },
  { "Type", "Type" }, { "Version", "Version" }, { "PublicationTitle", "Publication Title" }, { "RecordingNumber", "Recording Number" } };
const char *cite_field_label(const char *k)
{
  size_t i;
  for (i = 0; i < sizeof field_labels / sizeof field_labels[0]; i++) if (!strcmp(field_labels[i].k, k)) return field_labels[i].label;
  return k;
}
#define COMMON "Volume", "NumberVolumes", "Edition", "ShortTitle", "StandardNumber", "Comments", "Medium", "URL", "DOI", \
  "YearAccessed", "MonthAccessed", "DayAccessed"
static const char *const f_book[] = { "Author", "Title", "Year", "City", "Publisher", NULL };
static const char *const m_book[] = { "Editor", "Translator", "StateProvince", "CountryRegion", COMMON, "Pages", NULL };
static const char *const f_sect[] = { "Author", "Title", "BookTitle", "Editor", "Year", "Pages", "City", "Publisher", NULL };
static const char *const m_sect[] = { "BookAuthor", "Translator", "ChapterNumber", "StateProvince", "CountryRegion", COMMON, NULL };
static const char *const f_jour[] = { "Author", "Title", "JournalName", "Year", "Pages", NULL };
static const char *const m_jour[] = { "Editor", "Translator", "Month", "Day", "Issue", "Publisher", "City", COMMON, NULL };
static const char *const f_peri[] = { "Author", "Title", "PeriodicalTitle", "Year", "Month", "Day", "Pages", NULL };
static const char *const m_peri[] = { "Editor", "Translator", "Issue", "Publisher", "City", COMMON, NULL };
static const char *const f_conf[] = { "Author", "Title", "Pages", "Year", "ConferenceName", "City", "Publisher", NULL };
static const char *const m_conf[] = { "Editor", "Translator", "StateProvince", "CountryRegion", COMMON, NULL };
static const char *const f_rep[] = { "Author", "Title", "Year", "Publisher", "City", NULL };
static const char *const m_rep[] = { "Editor", "Translator", "ThesisType", "Institution", "Department", "StateProvince", "CountryRegion", COMMON, "Pages", NULL };
static const char *const f_web[] = { "Author", "Title", "Year", "Month", "Day", "YearAccessed", "MonthAccessed", "DayAccessed", "URL", NULL };
static const char *const m_web[] = { "Editor", "InternetSiteTitle", "ProductionCompany", "Version", "ShortTitle", "StandardNumber", "Comments", "Medium", "DOI", NULL };
static const char *const f_webd[] = { "Author", "Title", "InternetSiteTitle", "Year", "Month", "Day", "YearAccessed", "MonthAccessed", "DayAccessed", "URL", NULL };
static const char *const m_webd[] = { "Editor", "ProductionCompany", "Version", "ShortTitle", "StandardNumber", "Comments", "Medium", "DOI", NULL };
static const char *const f_elec[] = { "Author", "Title", "Year", "City", "Publisher", NULL };
static const char *const m_elec[] = { "Editor", "Translator", "PublicationTitle", "Month", "Day", "StateProvince", "CountryRegion", COMMON, NULL };
static const char *const f_art[] = { "Artist", "Title", "Year", "Institution", "City", NULL };
static const char *const m_art[] = { "Publisher", "StateProvince", "CountryRegion", "PublicationTitle", COMMON, NULL };
static const char *const f_snd[] = { "Composer", "Performer", "Title", "AlbumTitle", "Year", "City", "ProductionCompany", NULL };
static const char *const m_snd[] = { "Conductor", "Writer", "Month", "Day", "Publisher", "StateProvince", "CountryRegion", "RecordingNumber", COMMON, NULL };
static const char *const f_perf[] = { "Writer", "Performer", "Title", "Theater", "City", "Year", NULL };
static const char *const m_perf[] = { "Director", "ProducerName", "Month", "Day", "StateProvince", "CountryRegion", COMMON, NULL };
static const char *const f_film[] = { "Performer", "Director", "Title", "Year", "ProductionCompany", "Distributor", NULL };
static const char *const m_film[] = { "ProducerName", "Writer", "Month", "Day", "City", "CountryRegion", COMMON, NULL };
static const char *const f_int[] = { "Interviewee", "Interviewer", "Title", "Year", "Month", "Day", "Broadcaster", "Station", NULL };
static const char *const m_int[] = { "Editor", "Translator", "BroadcastTitle", "City", "Publisher", COMMON, NULL };
static const char *const f_pat[] = { "Inventor", "Title", "CountryRegion", "Type", "PatentNumber", "Year", "Month", "Day", NULL };
static const char *const m_pat[] = { "ShortTitle", "StandardNumber", "Comments", "Medium", "URL", "DOI", "YearAccessed", "MonthAccessed", "DayAccessed", NULL };
static const char *const f_case[] = { "Title", "CaseNumber", "Court", "Year", "Month", "Day", "Reporter", "Volume", "Pages", NULL };
static const char *const m_case[] = { "Counsel", "AbbreviatedCaseNumber", "City", "ShortTitle", "StandardNumber", "Comments", "Medium", "URL", "DOI", NULL };
static const char *const f_misc[] = { "Author", "Title", "Year", "City", "Publisher", NULL };
static const char *const m_misc[] = { "Editor", "Translator", "PublicationTitle", "Month", "Day", "StateProvince", "CountryRegion", COMMON, NULL };
const CiteTypeDef cite_types[] = {
  { "Book", "Book", f_book, m_book },
  { "BookSection", "Book Section", f_sect, m_sect },
  { "JournalArticle", "Journal Article", f_jour, m_jour },
  { "ArticleInAPeriodical", "Article in a Periodical", f_peri, m_peri },
  { "ConferenceProceedings", "Conference Proceedings", f_conf, m_conf },
  { "Report", "Report", f_rep, m_rep },
  { "InternetSite", "Web site", f_web, m_web },
  { "DocumentFromInternetSite", "Document From Web site", f_webd, m_webd },
  { "ElectronicSource", "Electronic Source", f_elec, m_elec },
  { "Art", "Art", f_art, m_art },
  { "SoundRecording", "Sound Recording", f_snd, m_snd },
  { "Performance", "Performance", f_perf, m_perf },
  { "Film", "Film", f_film, m_film },
  { "Interview", "Interview", f_int, m_int },
  { "Patent", "Patent", f_pat, m_pat },
  { "Case", "Case", f_case, m_case },
  { "Misc", "Miscellaneous", f_misc, m_misc } };
const int cite_ntypes = (int)(sizeof cite_types / sizeof cite_types[0]);
int cite_type_index(const char *xml)
{
  int i;
  for (i = 0; i < cite_ntypes; i++) if (!strcmp(cite_types[i].xml, xml)) return i;
  return -1;
}

/* ------------------------------------------------------------------ */
/* formatting: entries go into a Buf as UTF-8 with CITE_I1 ... CITE_I0 round italic text */
#define LQ "\xE2\x80\x9C"           /* the quotation marks round article titles */
#define RQ "\xE2\x80\x9D"
#define ENDASH "\xE2\x80\x93"
#define ELLIPSIS ". . . "
/* the last character shown (markers and closing quotes skipped) */
static int last_vis(const Buf *b)
{
  size_t i = b->len;
  while (i > 0) {
    char c = b->data[i - 1];
    if (c == CITE_I0 || c == CITE_I1) { i--; continue; }
    if (i >= 3 && !memcmp(b->data + i - 3, RQ, 3)) { i -= 3; continue; }
    return (u8)c;
  }
  return 0;
}
static void add(Buf *b, const char *s) { buf_adds(b, s); }
/* punctuation: a full stop isn't doubled after ".", "?" or "!" */
static void P(Buf *b, const char *p)
{
  int c = last_vis(b);
  if (p[0] == '.' && (c == '.' || c == '?' || c == '!')) p++;
  else if (p[0] == ',' && (c == '?' || c == '!')) p++;
  buf_adds(b, p);
}
static void I(Buf *b, const char *s)
{
  if (!s[0]) return;
  buf_addc(b, CITE_I1); buf_adds(b, s); buf_addc(b, CITE_I0);
}
/* "Title." in quotation marks, the punctuation inside (left out after ? or !) */
static void Q(Buf *b, const char *s, const char *punct)
{
  size_t n = strlen(s);
  if (!n) return;
  add(b, LQ); add(b, s);
  if (!(s[n - 1] == '?' || s[n - 1] == '!' || (s[n - 1] == '.' && punct[0] == '.'))) add(b, punct);
  add(b, RQ);
}
static int all_digits(const char *s) { if (!*s) return 0; for (; *s; s++) if (*s < '0' || *s > '9') return 0; return 1; }
/* a page range: has a dash or a comma */
static int is_range(const char *p) { return strchr(p, '-') || strchr(p, ',') || strstr(p, ENDASH); }
static void pages_dash(Buf *b, const char *p)
{
  for (; *p; p++) { if (*p == '-') { add(b, ENDASH); while (p[1] == '-') p++; } else buf_addc(b, *p); }
}
/* UTF-8 upper case for ASCII and Latin-1 letters */
static void upper_into(Buf *b, const char *s)
{
  for (; *s; s++) {
    u8 c = (u8)*s;
    if (c >= 'a' && c <= 'z') buf_addc(b, c - 32);
    else if (c == 0xC3 && (u8)s[1] >= 0xA0 && (u8)s[1] <= 0xBE && (u8)s[1] != 0xB7) { buf_addc(b, 0xC3); buf_addc(b, (u8)s[1] - 0x20); s++; }
    else buf_addc(b, c);
  }
}
/* initials of first and middle names: "J. R." (dots), "J.R." (dots, tight), "J R" (bare) */
static void initials(Buf *b, const char *first, const char *middle, int dots, int tight)
{
  const char *parts[2];
  int k, any = 0;
  parts[0] = first; parts[1] = middle;
  for (k = 0; k < 2; k++) {
    const char *s = parts[k];
    size_t len = strlen(s), i = 0;
    while (i < len) {
      size_t j;
      while (i < len && s[i] == ' ') i++;
      if (i >= len) break;
      if (any) add(b, tight ? "" : " ");
      j = utf8_next(s, len, i);
      buf_add(b, s + i, j - i);
      if (dots) add(b, ".");
      any = 1;
      /* the rest of the word; a hyphen gives its part an initial too (J.-P.) */
      for (i = j; i < len && s[i] != ' '; i++)
        if (s[i] == '-' && i + 1 < len && s[i + 1] != ' ') { size_t m = utf8_next(s, len, i + 1); add(b, "-"); buf_add(b, s + i + 1, m - i - 1); if (dots) add(b, "."); i = m - 1; }
    }
  }
}
enum { NM_LF, NM_FL, NM_LI, NM_IL, NM_UF, NM_UBI, NM_LSI, NM_LIT };
static void person(Buf *b, const CitePerson *p, int form)
{
  int fm = p->first[0] || p->middle[0];
  switch (form) {
  case NM_LF: add(b, p->last); if (fm) { add(b, ", "); add(b, p->first); if (p->middle[0]) { add(b, " "); add(b, p->middle); } } break;
  case NM_FL: if (p->first[0]) { add(b, p->first); add(b, " "); } if (p->middle[0]) { add(b, p->middle); add(b, " "); } add(b, p->last); break;
  case NM_LI: add(b, p->last); if (fm) { add(b, ", "); initials(b, p->first, p->middle, 1, 0); } break;
  case NM_LIT: add(b, p->last); if (fm) { add(b, ", "); initials(b, p->first, p->middle, 1, 1); } break;
  case NM_IL: if (fm) { initials(b, p->first, p->middle, 1, 0); add(b, " "); } add(b, p->last); break;
  case NM_UF: upper_into(b, p->last); if (fm) { add(b, ", "); add(b, p->first); if (p->middle[0]) { add(b, " "); add(b, p->middle); } } break;
  case NM_UBI: upper_into(b, p->last); if (fm) { add(b, " "); initials(b, p->first, p->middle, 0, 0); } break;
  case NM_LSI: add(b, p->last); if (fm) { add(b, " "); initials(b, p->first, p->middle, 1, 0); } break;
  }
}
/* a name list: the first in form f1, the others in fn; sep between, two between exactly two, last
   before the last of three or more; more than max (0: no limit) shows keep of them and then etal
   (ellipsis: keep, ". . . " and the last one, as APA does) */
typedef struct { int f1, fn; const char *sep, *two, *last; int max, keep; const char *etal; int ellipsis; } NameRule;
static void names(Buf *b, const CiteRole *r, const NameRule *nr)
{
  int i, n;
  if (!r) return;
  if (r->corp) { if (nr->f1 == NM_UF || nr->f1 == NM_UBI) upper_into(b, r->corp); else add(b, r->corp); return; }
  n = r->np;
  if (nr->max && n > nr->max) {
    for (i = 0; i < nr->keep; i++) { if (i) add(b, nr->sep); person(b, &r->p[i], i ? nr->fn : nr->f1); }
    if (nr->ellipsis) { add(b, nr->sep); add(b, ELLIPSIS); person(b, &r->p[n - 1], nr->fn); }
    else add(b, nr->etal);
    return;
  }
  for (i = 0; i < n; i++) {
    if (i) add(b, n == 2 ? nr->two : i == n - 1 ? nr->last : nr->sep);
    person(b, &r->p[i], i ? nr->fn : nr->f1);
  }
}
static int role_count(const CiteRole *r) { return !r ? 0 : r->corp ? 1 : r->np; }
/* the type's group */
enum { GR_BOOK, GR_SECT, GR_JOUR, GR_PERI, GR_CONF, GR_REP, GR_WEB, GR_FILM, GR_SND, GR_ART, GR_PERF, GR_INT, GR_PAT, GR_CASE };
static int group_of(const CiteSource *s)
{
  static const u8 g[] = { GR_BOOK, GR_SECT, GR_JOUR, GR_PERI, GR_CONF, GR_REP, GR_WEB, GR_WEB, GR_BOOK, GR_ART, GR_SND, GR_PERF,
    GR_FILM, GR_INT, GR_PAT, GR_CASE, GR_BOOK };
  int t = cite_type_index(cite_get(s, "SourceType"));
  return t < 0 ? GR_BOOK : g[t];
}
/* who a source is by: its authors, else the type's own (director, composer ...), else its editors (*ed) */
static const CiteRole *main_role(const CiteSource *s, int *ed)
{
  static const char *const by[] = { "Director", "Composer", "Performer", "Artist", "Writer", "Interviewee", "Inventor", NULL };
  const CiteRole *r = cite_role_c(s, "Author");
  int i, g = group_of(s);
  *ed = 0;
  if (role_count(r)) return r;
  for (i = 0; by[i]; i++) {
    r = cite_role_c(s, by[i]);
    if (!role_count(r)) continue;
    if ((g == GR_FILM && i == 0) || (g == GR_SND && (i == 1 || i == 2)) || (g == GR_ART && i == 3) || (g == GR_PERF && (i == 4 || i == 2)) ||
        (g == GR_INT && i == 5) || (g == GR_PAT && i == 6)) return r;
  }
  r = cite_role_c(s, "Editor");
  if (role_count(r)) { *ed = 1; return r; }
  return NULL;
}
/* months: by name, abbreviation or number; form 0 full, 1 MLA's abbreviations, 2 IEEE's, 3 two digits */
static const char *const mon_full[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September",
  "October", "November", "December" };
static const char *const mon_mla[12] = { "Jan.", "Feb.", "Mar.", "Apr.", "May", "June", "July", "Aug.", "Sept.", "Oct.", "Nov.", "Dec." };
static const char *const mon_ieee[12] = { "Jan.", "Feb.", "Mar.", "Apr.", "May", "Jun.", "Jul.", "Aug.", "Sep.", "Oct.", "Nov.", "Dec." };
static int month_no(const char *m)
{
  int i;
  if (all_digits(m)) { i = atoi(m); return i >= 1 && i <= 12 ? i : 0; }
  for (i = 0; i < 12; i++) {
    size_t n = strlen(m);
    if (n >= 3 && !strncmp(mon_full[i], m, 3) && (n <= strlen(mon_full[i]))) {
      size_t k;
      for (k = 0; k < n && (m[k] | 32) == (mon_full[i][k] | 32); k++) ;
      if (k == n || (k >= 3 && m[k] == '.')) return i + 1;
    }
  }
  return 0;
}
static void month(Buf *b, const char *m, int form)
{
  int k = month_no(m);
  if (!k) { add(b, m); return; }
  if (form == 3) { buf_printf(b, "%02d", k); return; }
  add(b, form == 1 ? mon_mla[k - 1] : form == 2 ? mon_ieee[k - 1] : mon_full[k - 1]);
}
/* an edition: "2" becomes "2nd ed.", words are kept ("Revised ed.") */
static void edition(Buf *b, const char *e, const char *ed)
{
  if (!e[0]) return;
  if (all_digits(e)) {
    int n = atoi(e);
    const char *sfx = (n % 100 >= 11 && n % 100 <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
    buf_printf(b, "%d%s", n, sfx);
  } else add(b, e);
  if (!strstr(e, "ed")) { add(b, " "); add(b, ed); }
}
/* a source's year with the letter telling apart one author's works of that year */
static int author_date(int cs)
{
  return cs == CS_APA6 || cs == CS_APA7 || cs == CS_CHICAGO16 || cs == CS_CHICAGO17 || cs == CS_HARVARD || cs == CS_ISO690 || cs == CS_TURABIAN;
}
/* the key a source sorts by: who it is by (or its title), the year, the title, lower case */
static void sort_key(const CiteSource *s, int bytitle, char *out, size_t cap)
{
  Buf b;
  int ed;
  const CiteRole *r = main_role(s, &ed);
  char *c;
  buf_init(&b);
  if (!bytitle && role_count(r)) {
    NameRule nr = { NM_LF, NM_LF, "; ", "; ", "; ", 0, 0, "", 0 };
    names(&b, r, &nr);
  } else {
    const char *t = cite_get(s, "Title");
    if (!strncmp(t, "The ", 4)) t += 4; else if (!strncmp(t, "A ", 2)) t += 2; else if (!strncmp(t, "An ", 3)) t += 3;
    add(&b, t);
  }
  add(&b, "\x01"); add(&b, cite_get(s, "Year")); add(&b, "\x01"); add(&b, cite_get(s, "Title"));
  snprintf(out, cap, "%s", b.data ? b.data : "");
  buf_free(&b);
  for (c = out; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);
}
static void who_key(const CiteSource *s, char *out, size_t cap)
{
  Buf b;
  int ed;
  const CiteRole *r = main_role(s, &ed);
  NameRule nr = { NM_LF, NM_LF, ";", ";", ";", 0, 0, "", 0 };
  buf_init(&b);
  if (r) names(&b, r, &nr); else add(&b, cite_get(s, "Title"));
  snprintf(out, cap, "%s", b.data ? b.data : "");
  buf_free(&b);
}
int cite_year_suffix(const CiteList *l, int i, int cs)
{
  char wi[300], wj[300], ti[300], tj[300];
  const char *yi;
  int j, before = 0, same = 0;
  if (!author_date(cs) || i < 0 || i >= l->n || cite_is_placeholder(&l->s[i])) return 0;
  yi = cite_get(&l->s[i], "Year");
  who_key(&l->s[i], wi, sizeof wi);
  if (!role_count(cite_role_c(&l->s[i], "Author")) && !main_role(&l->s[i], &j)) return 0;
  sort_key(&l->s[i], 1, ti, sizeof ti);
  for (j = 0; j < l->n; j++) {
    if (j == i || cite_is_placeholder(&l->s[j]) || strcmp(cite_get(&l->s[j], "Year"), yi)) continue;
    who_key(&l->s[j], wj, sizeof wj);
    if (strcmp(wi, wj)) continue;
    same = 1;
    sort_key(&l->s[j], 1, tj, sizeof tj);
    if (strcmp(tj, ti) < 0 || (!strcmp(tj, ti) && j < i)) before++;
  }
  return same ? 'a' + (before > 25 ? 25 : before) : 0;
}
/* the year (sfx: its letter; nd: what stands for no date, NULL nothing) */
static void year(Buf *b, const CiteSource *s, int sfx, const char *nd)
{
  const char *y = cite_get(s, "Year");
  if (y[0]) add(b, y); else if (nd) add(b, nd);
  if (sfx && (y[0] || nd)) { if (!y[0]) add(b, "-"); buf_addc(b, sfx); }
}
static const char *G(const CiteSource *s, const char *k) { return cite_get(s, k); }
/* the DOI as a link */
static void doi(Buf *b, const char *d, int apa6)
{
  if (!d[0]) return;
  if (!strncmp(d, "http", 4)) add(b, d);
  else { add(b, apa6 ? "doi:" : "https://doi.org/"); add(b, d); }
}
/* "City, State: Publisher" (colon: what goes between place and publisher) */
static void place_pub(Buf *b, const CiteSource *s, const char *pub, const char *colon)
{
  const char *c = G(s, "City"), *st = G(s, "StateProvince");
  if (c[0]) { add(b, c); if (st[0]) { add(b, ", "); add(b, st); } }
  if (c[0] && pub[0]) add(b, colon);
  add(b, pub);
}
static const char *publisher(const CiteSource *s)
{
  static const char *const k[] = { "Publisher", "ProductionCompany", "Institution", "Distributor", "Broadcaster", "Station", "Theater", NULL };
  int i;
  for (i = 0; k[i]; i++) if (G(s, k[i])[0]) return G(s, k[i]);
  return "";
}
/* the container a part is in (book, journal, periodical, conference, web site, album) */
static const char *container(const CiteSource *s, int g)
{
  switch (g) {
  case GR_SECT: return G(s, "BookTitle");
  case GR_JOUR: return G(s, "JournalName");
  case GR_PERI: return G(s, "PeriodicalTitle");
  case GR_CONF: return G(s, "ConferenceName");
  case GR_WEB: return G(s, "InternetSiteTitle");
  case GR_SND: return G(s, "AlbumTitle");
  case GR_INT: return G(s, "BroadcastTitle");
  }
  return G(s, "PublicationTitle");
}
/* whether the title is a part of something (quoted) or a whole work (italic) */
static int is_part(const CiteSource *s, int g)
{
  return g == GR_SECT || g == GR_JOUR || g == GR_PERI || g == GR_CONF || (g == GR_WEB && G(s, "InternetSiteTitle")[0]) ||
         (g == GR_SND && G(s, "AlbumTitle")[0]);
}

/* ---- one entry per style */
typedef struct { const CiteList *l; const CiteSource *s; int cs, n, g, sfx; Buf *b; } E;
static const char *descr(int g)
{
  switch (g) {
  case GR_FILM: return "Film";
  case GR_SND: return "Song";
  case GR_ART: return "Artwork";
  case GR_PERF: return "Performance";
  case GR_INT: return "Interview";
  }
  return "";
}

/* APA, 6th and 7th editions */
static void e_apa(E *e)
{
  Buf *b = e->b;
  const CiteSource *s = e->s;
  int ed, g = e->g, seven = e->cs == CS_APA7, part = is_part(s, g);
  const CiteRole *r = main_role(s, &ed);
  NameRule nr = { NM_LI, NM_LI, ", ", ", & ", ", & ", 0, 0, "", 1 };
  const char *t = G(s, "Title"), *pg = G(s, "Pages"), *pub = publisher(s);
  if (seven) { nr.max = 20; nr.keep = 19; } else { nr.max = 7; nr.keep = 6; }
  if (g == GR_CASE) {
    /* legal: Title, Volume Reporter Pages (Court Year). */
    I(b, t);
    if (G(s, "Volume")[0] || G(s, "Reporter")[0]) { add(b, ", "); add(b, G(s, "Volume")); if (G(s, "Reporter")[0]) { add(b, " "); add(b, G(s, "Reporter")); } if (pg[0]) { add(b, " "); add(b, pg); } }
    add(b, " ("); if (G(s, "Court")[0]) { add(b, G(s, "Court")); add(b, " "); } year(b, s, 0, "n.d."); add(b, ")."); 
    if (G(s, "URL")[0]) { add(b, " "); add(b, G(s, "URL")); }
    return;
  }
  if (r) {
    names(b, r, &nr);
    if (ed) add(b, role_count(r) > 1 ? " (Eds.)" : " (Ed.)");
    else if (g == GR_FILM && r == cite_role_c(s, "Director")) add(b, " (Director)");
    P(b, ". ");
  } else if (t[0]) {
    if (part) add(b, t); else I(b, t);
    P(b, ". ");
  }
  add(b, "(");
  year(b, s, e->sfx, "n.d.");
  if ((g == GR_PERI || g == GR_WEB || g == GR_INT || g == GR_PAT) && G(s, "Month")[0]) {
    add(b, ", "); month(b, G(s, "Month"), 0);
    if (G(s, "Day")[0]) { add(b, " "); add(b, G(s, "Day")); }
  }
  add(b, "). ");
  if (r && t[0]) {
    if (part) add(b, t); else I(b, t);
  }
  /* what follows the title */
  switch (g) {
  case GR_SECT: case GR_CONF: {
    const CiteRole *edr = cite_role_c(s, "Editor");
    NameRule en = { NM_IL, NM_IL, ", ", ", & ", ", & ", 0, 0, "", 0 };
    if (r && t[0]) P(b, ". ");
    add(b, "In ");
    if (role_count(edr) && !ed) { names(b, edr, &en); add(b, role_count(edr) > 1 ? " (Eds.), " : " (Ed.), "); }
    I(b, container(s, g));
    if (pg[0]) { add(b, is_range(pg) ? " (pp. " : " (p. "); pages_dash(b, pg); add(b, ")"); }
    P(b, ". ");
    if (!seven) place_pub(b, s, pub, ": "); else add(b, pub);
    if (pub[0] || (!seven && G(s, "City")[0])) P(b, ".");
    break; }
  case GR_JOUR: case GR_PERI:
    if (r && t[0]) P(b, ". ");
    I(b, container(s, g));
    if (G(s, "Volume")[0]) { add(b, ", "); I(b, G(s, "Volume")); }
    if (G(s, "Issue")[0]) { add(b, "("); add(b, G(s, "Issue")); add(b, ")"); }
    if (pg[0]) { add(b, ", "); if (g == GR_PERI && !seven) add(b, is_range(pg) ? "pp. " : "p. "); pages_dash(b, pg); }
    P(b, ".");
    break;
  case GR_WEB:
    if (r && t[0]) P(b, ". ");
    if (G(s, "InternetSiteTitle")[0]) { add(b, G(s, "InternetSiteTitle")); P(b, "."); }
    if (!seven && G(s, "URL")[0]) { if (b->len) add(b, " "); add(b, "Retrieved from "); add(b, G(s, "URL")); return; }
    break;
  default: {
    /* whole works: (2nd ed., Vol. 3) [Film]. Publisher. */
    int paren = G(s, "Edition")[0] || G(s, "Volume")[0] || (g == GR_REP && G(s, "StandardNumber")[0]) || (g == GR_PAT && G(s, "PatentNumber")[0]);
    if (paren) {
      int any = 0;
      add(b, " (");
      if (G(s, "Edition")[0]) { edition(b, G(s, "Edition"), "ed."); any = 1; }
      if (G(s, "Volume")[0]) { if (any) add(b, ", "); add(b, "Vol. "); add(b, G(s, "Volume")); any = 1; }
      if (g == GR_REP && G(s, "StandardNumber")[0]) { if (any) add(b, ", "); add(b, G(s, "ThesisType")[0] ? G(s, "ThesisType") : "Report"); add(b, " No. "); add(b, G(s, "StandardNumber")); }
      if (g == GR_PAT && G(s, "PatentNumber")[0]) { if (any) add(b, ", "); if (G(s, "CountryRegion")[0]) { add(b, G(s, "CountryRegion")); add(b, " "); } add(b, "Patent No. "); add(b, G(s, "PatentNumber")); }
      add(b, ")");
    }
    if (descr(g)[0]) {
      add(b, " ["); add(b, g == GR_ART && G(s, "Medium")[0] ? G(s, "Medium") : descr(g)); add(b, "]");
    }
    if (g == GR_SND && G(s, "AlbumTitle")[0]) { P(b, ". On "); I(b, G(s, "AlbumTitle")); }
    P(b, ". ");
    if (g == GR_ART) { add(b, G(s, "Institution")); if (G(s, "Institution")[0] && G(s, "City")[0]) add(b, ", "); add(b, G(s, "City")); }
    else if (!seven && g != GR_PAT) place_pub(b, s, pub, ": ");
    else if (g != GR_PAT) add(b, pub);
    P(b, ".");
    if (last_vis(b) == ' ') b->data[--b->len] = 0;
    break; }
  }
  while (b->len && b->data[b->len - 1] == ' ') b->data[--b->len] = 0;
  if (G(s, "DOI")[0]) { add(b, " "); doi(b, G(s, "DOI"), !seven); }
  else if (G(s, "URL")[0]) { add(b, " "); if (!seven) add(b, "Retrieved from "); add(b, G(s, "URL")); }
}

/* MLA, 7th, 8th and 9th editions */
static void e_mla(E *e)
{
  Buf *b = e->b;
  const CiteSource *s = e->s;
  int ed, g = e->g, seven = e->cs == CS_MLA7, part = is_part(s, g);
  const CiteRole *r = main_role(s, &ed);
  NameRule nr = { NM_LF, NM_FL, ", ", ", and ", ", and ", 2, 1, ", et al.", 0 };
  NameRule fl = { NM_FL, NM_FL, ", ", " and ", ", and ", 2, 1, " et al.", 0 };
  const char *t = G(s, "Title"), *pg = G(s, "Pages"), *pub = publisher(s), *ct = container(s, g);
  int any;
  if (seven) { nr.max = 3; nr.keep = 1; }
  if (r && !(g == GR_FILM && r == cite_role_c(s, "Director"))) {
    names(b, r, &nr);
    if (ed) add(b, role_count(r) > 1 ? ", editors" : ", editor");
    P(b, ". ");
  }
  if (part) Q(b, t, "."); else { I(b, t); P(b, "."); }
  if (g == GR_FILM && role_count(cite_role_c(s, "Director"))) { add(b, seven ? " Dir. " : " Directed by "); names(b, cite_role_c(s, "Director"), &fl); P(b, seven ? "." : ","); }
  if (g == GR_INT) { add(b, " Interview"); if (role_count(cite_role_c(s, "Interviewer"))) { add(b, " by "); names(b, cite_role_c(s, "Interviewer"), &fl); } P(b, "."); }
  if (seven) {
    /* MLA 7: City: Publisher, Year. Medium. */
    if (ct[0]) { add(b, " "); I(b, ct); }
    if (g == GR_JOUR) {
      if (G(s, "Volume")[0]) { add(b, " "); add(b, G(s, "Volume")); if (G(s, "Issue")[0]) { add(b, "."); add(b, G(s, "Issue")); } }
      add(b, " ("); year(b, s, 0, "n.d."); add(b, ")"); if (pg[0]) { add(b, ": "); pages_dash(b, pg); } P(b, ". Print.");
      return;
    }
    if (g == GR_PERI) {
      add(b, " ");
      if (G(s, "Day")[0]) { add(b, G(s, "Day")); add(b, " "); }
      if (G(s, "Month")[0]) { month(b, G(s, "Month"), 1); add(b, " "); }
      year(b, s, 0, "n.d."); if (pg[0]) { add(b, ": "); pages_dash(b, pg); } P(b, ". Print.");
      return;
    }
    if (g == GR_WEB) {
      if (ct[0]) P(b, ".");
      if (pub[0]) { add(b, " "); add(b, pub); P(b, ","); }
      add(b, " ");
      if (G(s, "Day")[0]) { add(b, G(s, "Day")); add(b, " "); }
      if (G(s, "Month")[0]) { month(b, G(s, "Month"), 1); add(b, " "); }
      year(b, s, 0, "n.d."); P(b, ". Web.");
      if (G(s, "YearAccessed")[0]) { add(b, " "); if (G(s, "DayAccessed")[0]) { add(b, G(s, "DayAccessed")); add(b, " "); } if (G(s, "MonthAccessed")[0]) { month(b, G(s, "MonthAccessed"), 1); add(b, " "); } add(b, G(s, "YearAccessed")); add(b, "."); }
      return;
    }
    if (ct[0]) P(b, ".");
    if (g == GR_SECT && role_count(cite_role_c(s, "Editor"))) { add(b, " Ed. "); names(b, cite_role_c(s, "Editor"), &fl); P(b, "."); }
    if (G(s, "Edition")[0]) { add(b, " "); edition(b, G(s, "Edition"), "ed."); P(b, "."); }
    add(b, " ");
    if (G(s, "City")[0] || pub[0]) { place_pub(b, s, pub, ": "); add(b, ", "); }
    year(b, s, 0, "n.d."); P(b, ".");
    if (pg[0]) { add(b, " "); pages_dash(b, pg); P(b, "."); }
    add(b, g == GR_FILM ? " Film." : g == GR_SND ? " CD." : g == GR_PERF ? " Performance." : g == GR_ART ? " Print." : " Print.");
    return;
  }
  /* MLA 8 and 9: the container's elements in order, commas between, a full stop at the end */
  any = 0;
  if (ct[0]) { add(b, " "); I(b, ct); any = 1; }
  if (g == GR_SECT && role_count(cite_role_c(s, "Editor")) && !ed) { add(b, any ? ", edited by " : " Edited by "); names(b, cite_role_c(s, "Editor"), &fl); any = 1; }
  if (G(s, "Edition")[0]) { add(b, any ? ", " : " "); edition(b, G(s, "Edition"), "ed."); any = 1; }
  if (G(s, "Volume")[0]) { add(b, any ? ", vol. " : " Vol. "); add(b, G(s, "Volume")); any = 1; }
  if (G(s, "Issue")[0]) { add(b, any ? ", no. " : " No. "); add(b, G(s, "Issue")); any = 1; }
  if (pub[0] && g != GR_JOUR && g != GR_PERI) { add(b, any ? ", " : " "); add(b, pub); any = 1; }
  if (G(s, "Year")[0]) {
    add(b, any ? ", " : " ");
    if (G(s, "Day")[0] && G(s, "Month")[0]) { add(b, G(s, "Day")); add(b, " "); }
    if (G(s, "Month")[0]) { month(b, G(s, "Month"), 1); add(b, " "); }
    add(b, G(s, "Year")); any = 1;
  }
  if (pg[0]) { add(b, any ? ", " : " "); add(b, is_range(pg) ? "pp. " : "p. "); pages_dash(b, pg); any = 1; }
  if (G(s, "DOI")[0]) { add(b, any ? ", " : " "); doi(b, G(s, "DOI"), 0); any = 1; }
  else if (G(s, "URL")[0]) { add(b, any ? ", " : " "); add(b, G(s, "URL")); any = 1; }
  if (any) P(b, ".");
  if (g == GR_WEB && G(s, "YearAccessed")[0] && (e->cs == CS_MLA9 || !G(s, "Year")[0])) {
    add(b, " Accessed ");
    if (G(s, "DayAccessed")[0]) { add(b, G(s, "DayAccessed")); add(b, " "); }
    if (G(s, "MonthAccessed")[0]) { month(b, G(s, "MonthAccessed"), 1); add(b, " "); }
    add(b, G(s, "YearAccessed")); add(b, ".");
  }
}

/* Chicago 16th and 17th (author-date) and Turabian */
static void e_chicago(E *e)
{
  Buf *b = e->b;
  const CiteSource *s = e->s;
  int ed, g = e->g, part = is_part(s, g), turabian = e->cs == CS_TURABIAN;
  const CiteRole *r = main_role(s, &ed);
  NameRule nr = { NM_LF, NM_FL, ", ", ", and ", ", and ", 10, 7, ", et al.", 0 };
  NameRule fl = { NM_FL, NM_FL, ", ", " and ", ", and ", 0, 0, "", 0 };
  const char *t = G(s, "Title"), *pg = G(s, "Pages"), *pub = publisher(s), *ct = container(s, g);
  if (e->cs == CS_CHICAGO16) nr.max = 0;
  if (r) { names(b, r, &nr); if (ed) add(b, role_count(r) > 1 ? ", eds" : ", ed"); P(b, ". "); }
  if (!turabian) { year(b, s, e->sfx, "n.d."); add(b, ". "); }
  if (part) Q(b, t, "."); else { I(b, t); P(b, "."); }
  switch (g) {
  case GR_SECT: case GR_CONF:
    add(b, " In "); I(b, ct);
    if (role_count(cite_role_c(s, "Editor")) && !ed) { add(b, ", edited by "); names(b, cite_role_c(s, "Editor"), &fl); }
    if (pg[0]) { add(b, ", "); pages_dash(b, pg); }
    P(b, ". ");
    place_pub(b, s, pub, ": ");
    if (turabian) { add(b, ", "); year(b, s, 0, "n.d."); }
    P(b, ".");
    break;
  case GR_JOUR:
    add(b, " "); I(b, ct);
    if (G(s, "Volume")[0]) { add(b, " "); add(b, G(s, "Volume")); }
    if (G(s, "Issue")[0]) { add(b, ", no. "); add(b, G(s, "Issue")); }
    if (turabian) { add(b, " ("); year(b, s, 0, "n.d."); add(b, ")"); }
    if (pg[0]) { add(b, ": "); pages_dash(b, pg); }
    P(b, ".");
    break;
  case GR_PERI: case GR_WEB:
    if (ct[0]) { add(b, " "); if (g == GR_PERI) I(b, ct); else add(b, ct); P(b, g == GR_PERI ? "," : "."); }
    if (G(s, "Month")[0]) { add(b, " "); month(b, G(s, "Month"), 0); if (G(s, "Day")[0]) { add(b, " "); add(b, G(s, "Day")); } if (turabian) { add(b, ", "); add(b, G(s, "Year")); } P(b, "."); }
    if (g == GR_WEB && G(s, "YearAccessed")[0]) {
      add(b, " Accessed "); if (G(s, "MonthAccessed")[0]) { month(b, G(s, "MonthAccessed"), 0); add(b, " "); }
      if (G(s, "DayAccessed")[0]) { add(b, G(s, "DayAccessed")); add(b, ", "); }
      add(b, G(s, "YearAccessed")); add(b, ".");
    }
    break;
  default:
    if (g == GR_FILM && role_count(cite_role_c(s, "Director")) && r != cite_role_c(s, "Director")) { add(b, " Directed by "); names(b, cite_role_c(s, "Director"), &fl); P(b, "."); }
    if (G(s, "Edition")[0]) { add(b, " "); edition(b, G(s, "Edition"), "ed."); P(b, "."); }
    if (G(s, "Volume")[0]) { add(b, " Vol. "); add(b, G(s, "Volume")); P(b, "."); }
    if (descr(g)[0] && g != GR_FILM) { add(b, " "); add(b, g == GR_ART && G(s, "Medium")[0] ? G(s, "Medium") : descr(g)); P(b, "."); }
    if (G(s, "City")[0] || pub[0]) { add(b, " "); place_pub(b, s, pub, ": "); if (turabian) add(b, ","); else P(b, "."); }
    if (turabian) { add(b, " "); year(b, s, 0, "n.d."); P(b, "."); }
    break;
  }
  if (G(s, "DOI")[0]) { add(b, " "); doi(b, G(s, "DOI"), 0); P(b, "."); }
  else if (G(s, "URL")[0]) { add(b, " "); add(b, G(s, "URL")); P(b, "."); }
}

/* Harvard - Anglia 2008 */
static void e_harvard(E *e)
{
  Buf *b = e->b;
  const CiteSource *s = e->s;
  int ed, g = e->g, part = is_part(s, g) && g != GR_WEB;
  const CiteRole *r = main_role(s, &ed);
  NameRule nr = { NM_LIT, NM_LIT, ", ", " and ", " and ", 0, 0, "", 0 };
  NameRule en = { NM_IL, NM_IL, ", ", " and ", " and ", 0, 0, "", 0 };
  const char *t = G(s, "Title"), *pg = G(s, "Pages"), *pub = publisher(s), *ct = container(s, g);
  if (r) { names(b, r, &nr); if (ed) add(b, role_count(r) > 1 ? " eds." : " ed."); add(b, ", "); }
  else if (t[0]) { I(b, t); add(b, ", "); }
  year(b, s, e->sfx, "n.d."); add(b, ". ");
  if (r) { if (part) add(b, t); else I(b, t); P(b, "."); }
  switch (g) {
  case GR_SECT: case GR_CONF:
    add(b, " In");
    if (role_count(cite_role_c(s, "Editor")) && !ed) { add(b, ": "); names(b, cite_role_c(s, "Editor"), &en); add(b, role_count(cite_role_c(s, "Editor")) > 1 ? ", eds. " : ", ed. "); }
    else add(b, " ");
    I(b, ct); P(b, ". ");
    place_pub(b, s, pub, ": "); P(b, ".");
    if (pg[0]) { add(b, " pp."); pages_dash(b, pg); P(b, "."); }
    break;
  case GR_JOUR: case GR_PERI:
    add(b, " "); I(b, ct);
    if (g == GR_PERI && G(s, "Month")[0]) { add(b, ", "); if (G(s, "Day")[0]) { add(b, G(s, "Day")); add(b, " "); } month(b, G(s, "Month"), 0); }
    if (G(s, "Volume")[0]) { add(b, ", "); add(b, G(s, "Volume")); if (G(s, "Issue")[0]) { add(b, "("); add(b, G(s, "Issue")); add(b, ")"); } }
    if (pg[0]) { add(b, ", pp."); pages_dash(b, pg); }
    P(b, ".");
    break;
  case GR_WEB:
    if (ct[0]) { add(b, " "); add(b, ct); P(b, "."); }
    break;
  default:
    if (G(s, "Edition")[0]) { add(b, " "); edition(b, G(s, "Edition"), "ed."); P(b, "."); }
    if (descr(g)[0] && g != GR_FILM) { add(b, " ["); add(b, g == GR_ART && G(s, "Medium")[0] ? G(s, "Medium") : descr(g)); add(b, "]"); }
    if (g == GR_FILM) add(b, " [Film]");
    if (G(s, "City")[0] || pub[0]) { add(b, " "); place_pub(b, s, pub, ": "); }
    P(b, ".");
    break;
  }
  if (G(s, "URL")[0]) {
    add(b, " [Online] Available at: "); add(b, G(s, "URL"));
    if (G(s, "YearAccessed")[0]) {
      add(b, " [Accessed "); if (G(s, "DayAccessed")[0]) { add(b, G(s, "DayAccessed")); add(b, " "); }
      if (G(s, "MonthAccessed")[0]) { month(b, G(s, "MonthAccessed"), 0); add(b, " "); }
      add(b, G(s, "YearAccessed")); add(b, "]");
    }
    add(b, ".");
  } else if (G(s, "DOI")[0]) { add(b, " "); doi(b, G(s, "DOI"), 0); P(b, "."); }
}

/* IEEE 2006: [n] then a tab and the entry */
static void e_ieee(E *e)
{
  Buf *b = e->b;
  const CiteSource *s = e->s;
  int ed, g = e->g, part = is_part(s, g);
  const CiteRole *r = main_role(s, &ed);
  NameRule nr = { NM_IL, NM_IL, ", ", " and ", ", and ", 6, 1, " et al.", 0 };
  const char *t = G(s, "Title"), *pg = G(s, "Pages"), *pub = publisher(s), *ct = container(s, g);
  buf_printf(b, "[%d]\t", e->n);
  if (r) { names(b, r, &nr); if (ed) add(b, role_count(r) > 1 ? ", Eds." : ", Ed."); }
  if (g == GR_WEB) {
    if (r) P(b, ". ");
    add(b, "("); year(b, s, 0, "n.d.");
    if (G(s, "Month")[0]) { add(b, ", "); month(b, G(s, "Month"), 2); if (G(s, "Day")[0]) { add(b, " "); add(b, G(s, "Day")); } }
    add(b, ") "); I(b, t); P(b, ". ");
    if (ct[0]) { add(b, ct); P(b, ". "); }
    if (G(s, "URL")[0]) { add(b, "[Online]. Available: "); add(b, G(s, "URL")); }
    while (b->len && b->data[b->len - 1] == ' ') b->data[--b->len] = 0;
    return;
  }
  if (r) add(b, ", ");
  if (part || g == GR_REP || g == GR_PAT) Q(b, t, ","); else { I(b, t); add(b, ","); }
  switch (g) {
  case GR_SECT: case GR_CONF:
    add(b, g == GR_CONF ? " in Proc. " : " in "); I(b, ct);
    if (g == GR_SECT && role_count(cite_role_c(s, "Editor")) && !ed) {
      NameRule en = { NM_IL, NM_IL, ", ", " and ", ", and ", 0, 0, "", 0 };
      add(b, ", "); names(b, cite_role_c(s, "Editor"), &en); add(b, role_count(cite_role_c(s, "Editor")) > 1 ? ", Eds." : ", Ed.");
    }
    if (g == GR_SECT) { add(b, " "); place_pub(b, s, pub, ": "); }
    else if (G(s, "City")[0]) { add(b, ", "); add(b, G(s, "City")); }
    add(b, ", "); year(b, s, 0, "n.d.");
    if (pg[0]) { add(b, is_range(pg) ? ", pp. " : ", p. "); pages_dash(b, pg); }
    P(b, ".");
    break;
  case GR_JOUR: case GR_PERI:
    add(b, " "); I(b, ct);
    if (G(s, "Volume")[0]) { add(b, ", vol. "); add(b, G(s, "Volume")); }
    if (G(s, "Issue")[0]) { add(b, ", no. "); add(b, G(s, "Issue")); }
    if (pg[0]) { add(b, is_range(pg) ? ", pp. " : ", p. "); pages_dash(b, pg); }
    add(b, ", ");
    if (G(s, "Month")[0]) { month(b, G(s, "Month"), 2); add(b, " "); }
    year(b, s, 0, "n.d."); P(b, ".");
    break;
  case GR_PAT:
    add(b, " "); if (G(s, "CountryRegion")[0]) { add(b, G(s, "CountryRegion")); add(b, " "); }
    add(b, "Patent "); add(b, G(s, "PatentNumber")); add(b, ", ");
    if (G(s, "Month")[0]) { month(b, G(s, "Month"), 2); add(b, " "); if (G(s, "Day")[0]) { add(b, G(s, "Day")); add(b, ", "); } }
    year(b, s, 0, "n.d."); P(b, ".");
    break;
  case GR_REP:
    if (G(s, "Institution")[0] || pub[0]) { add(b, " "); add(b, G(s, "Institution")[0] ? G(s, "Institution") : pub); }
    if (G(s, "City")[0]) { add(b, ", "); add(b, G(s, "City")); }
    if (G(s, "StandardNumber")[0]) { add(b, ", Rep. "); add(b, G(s, "StandardNumber")); }
    add(b, ", "); year(b, s, 0, "n.d."); P(b, ".");
    break;
  default:
    if (G(s, "Edition")[0]) { add(b, " "); edition(b, G(s, "Edition"), "ed."); add(b, "."); }
    if (G(s, "City")[0] || pub[0]) { add(b, " "); place_pub(b, s, pub, ": "); add(b, ","); }
    add(b, " "); year(b, s, 0, "n.d."); P(b, ".");
    break;
  }
  if (G(s, "DOI")[0]) { add(b, " "); doi(b, G(s, "DOI"), 1); P(b, "."); }
  else if (G(s, "URL")[0]) { add(b, " [Online]. Available: "); add(b, G(s, "URL")); }
}

/* ISO 690: first element and date (author, year, title ...), or numerical (n. author, title ... year) */
static void e_iso(E *e)
{
  Buf *b = e->b;
  const CiteSource *s = e->s;
  int ed, g = e->g, part = is_part(s, g), num = e->cs == CS_ISO690NUM;
  const CiteRole *r = main_role(s, &ed);
  NameRule nr = { NM_UF, NM_UF, ", ", " and ", " and ", 3, 1, ", et al.", 0 };
  const char *t = G(s, "Title"), *pg = G(s, "Pages"), *pub = publisher(s), *ct = container(s, g);
  if (num) buf_printf(b, "%d.\t", e->n);
  if (r) { names(b, r, &nr); if (ed) add(b, ", [ed.]"); P(b, ". "); }
  if (!num) { year(b, s, e->sfx, "n.d."); add(b, ". "); }
  if (part) add(b, t); else I(b, t);
  P(b, ".");
  switch (g) {
  case GR_SECT: case GR_CONF:
    if (role_count(cite_role_c(s, "Editor")) && !ed) { add(b, " [ed.] "); names(b, cite_role_c(s, "Editor"), &nr); P(b, "."); }
    add(b, " "); I(b, ct); P(b, ". ");
    place_pub(b, s, pub, " : "); add(b, ", "); year(b, s, 0, "n.d.");
    if (pg[0]) { add(b, ", pp. "); pages_dash(b, pg); }
    P(b, ".");
    break;
  case GR_JOUR: case GR_PERI:
    add(b, " "); I(b, ct); P(b, ". ");
    year(b, s, 0, "n.d.");
    if (G(s, "Volume")[0]) { add(b, ", Vol. "); add(b, G(s, "Volume")); }
    if (G(s, "Issue")[0]) { add(b, ", "); add(b, G(s, "Issue")); }
    if (pg[0]) { add(b, ", pp. "); pages_dash(b, pg); }
    P(b, ".");
    break;
  case GR_WEB:
    if (ct[0]) { add(b, " "); add(b, ct); P(b, "."); }
    add(b, " [Online] "); year(b, s, 0, "n.d."); add(b, ".");
    if (G(s, "YearAccessed")[0]) {
      add(b, " [Cited: "); if (G(s, "MonthAccessed")[0]) { month(b, G(s, "MonthAccessed"), 0); add(b, " "); }
      if (G(s, "DayAccessed")[0]) { add(b, G(s, "DayAccessed")); add(b, ", "); }
      add(b, G(s, "YearAccessed")); add(b, ".]");
    }
    if (G(s, "URL")[0]) { add(b, " "); add(b, G(s, "URL")); P(b, "."); }
    return;
  default:
    if (G(s, "Edition")[0]) { add(b, " "); edition(b, G(s, "Edition"), "ed."); P(b, "."); }
    if (G(s, "Volume")[0]) { add(b, " Vol. "); add(b, G(s, "Volume")); P(b, "."); }
    add(b, " ");
    if (G(s, "City")[0] || pub[0]) { place_pub(b, s, pub, " : "); add(b, ", "); }
    year(b, s, 0, "n.d."); P(b, ".");
    break;
  }
  if (G(s, "DOI")[0]) { add(b, " "); doi(b, G(s, "DOI"), 0); P(b, "."); }
  else if (G(s, "URL")[0]) { add(b, " Available from: "); add(b, G(s, "URL")); P(b, "."); }
}

/* GB/T 7714-2005: [n] SURNAME I, ... Title[M]. City: Publisher, Year. */
static void e_gb(E *e)
{
  Buf *b = e->b;
  const CiteSource *s = e->s;
  int ed, g = e->g;
  const CiteRole *r = main_role(s, &ed);
  NameRule nr = { NM_UBI, NM_UBI, ", ", ", ", ", ", 3, 3, ", et al", 0 };
  const char *t = G(s, "Title"), *pg = G(s, "Pages"), *pub = publisher(s), *ct = container(s, g);
  const char *code = g == GR_JOUR || g == GR_PERI ? (g == GR_PERI ? "N" : "J") : g == GR_CONF ? "C" : g == GR_REP ? "R" : g == GR_PAT ? "P" :
                     g == GR_WEB ? "EB/OL" : g == GR_BOOK || g == GR_SECT ? "M" : "Z";
  buf_printf(b, "[%d]\t", e->n);
  if (r) { names(b, r, &nr); P(b, ". "); }
  add(b, t); add(b, "["); add(b, code); add(b, "]");
  switch (g) {
  case GR_SECT: case GR_CONF:
    add(b, "//");
    if (role_count(cite_role_c(s, "Editor")) && !ed) { names(b, cite_role_c(s, "Editor"), &nr); add(b, ". "); }
    add(b, ct); P(b, ". "); place_pub(b, s, pub, ": "); add(b, ", "); year(b, s, 0, "");
    if (pg[0]) { add(b, ": "); pages_dash(b, pg); }
    P(b, ".");
    break;
  case GR_JOUR: case GR_PERI:
    add(b, ". "); add(b, ct); add(b, ", "); year(b, s, 0, "");
    if (G(s, "Volume")[0]) { add(b, ", "); add(b, G(s, "Volume")); }
    if (G(s, "Issue")[0]) { add(b, "("); add(b, G(s, "Issue")); add(b, ")"); }
    if (pg[0]) { add(b, ": "); pages_dash(b, pg); }
    P(b, ".");
    break;
  case GR_WEB:
    add(b, ". ("); year(b, s, 0, "");
    if (G(s, "Month")[0]) { add(b, "-"); month(b, G(s, "Month"), 3); if (G(s, "Day")[0]) { add(b, "-"); if (strlen(G(s, "Day")) < 2) add(b, "0"); add(b, G(s, "Day")); } }
    add(b, ")");
    if (G(s, "YearAccessed")[0]) {
      add(b, "["); add(b, G(s, "YearAccessed"));
      if (G(s, "MonthAccessed")[0]) { add(b, "-"); month(b, G(s, "MonthAccessed"), 3); if (G(s, "DayAccessed")[0]) { add(b, "-"); if (strlen(G(s, "DayAccessed")) < 2) add(b, "0"); add(b, G(s, "DayAccessed")); } }
      add(b, "]");
    }
    add(b, ". "); add(b, G(s, "URL"));
    return;
  default:
    add(b, ". ");
    if (G(s, "City")[0] || pub[0]) { place_pub(b, s, pub, ": "); add(b, ", "); }
    year(b, s, 0, ""); P(b, ".");
    break;
  }
  if (G(s, "URL")[0]) { add(b, " "); add(b, G(s, "URL")); }
}

/* GOST 7.1 (name or title sort): n. Surname I. Title. - City : Publisher, Year. - P. pages. */
static void e_gost(E *e)
{
  Buf *b = e->b;
  const CiteSource *s = e->s;
  int ed, g = e->g;
  const CiteRole *r = main_role(s, &ed);
  NameRule nr = { NM_LSI, NM_LSI, ", ", ", ", ", ", 3, 3, " [et al.]", 0 };
  const char *t = G(s, "Title"), *pg = G(s, "Pages"), *pub = publisher(s), *ct = container(s, g);
  buf_printf(b, "%d.\t", e->n);
  if (r) { names(b, r, &nr); add(b, " "); }
  add(b, t);
  if (g == GR_WEB) add(b, " [Electronic resource]");
  if (g == GR_SECT || g == GR_JOUR || g == GR_PERI || g == GR_CONF) {
    add(b, " // "); add(b, ct); add(b, ". " ENDASH " ");
    if (g == GR_SECT || g == GR_CONF) { place_pub(b, s, pub, " : "); add(b, ", "); }
    year(b, s, 0, "");
    if (G(s, "Volume")[0]) { add(b, ". " ENDASH " Vol. "); add(b, G(s, "Volume")); }
    if (G(s, "Issue")[0]) { add(b, ", No. "); add(b, G(s, "Issue")); }
    if (pg[0]) { add(b, ". " ENDASH " P. "); pages_dash(b, pg); }
    P(b, ".");
  } else {
    P(b, ".");
    if (G(s, "Edition")[0]) { add(b, " " ENDASH " "); edition(b, G(s, "Edition"), "ed."); P(b, "."); }
    if (G(s, "City")[0] || pub[0] || G(s, "Year")[0]) {
      add(b, " " ENDASH " ");
      if (G(s, "City")[0] || pub[0]) { place_pub(b, s, pub, " : "); add(b, ", "); }
      year(b, s, 0, ""); P(b, ".");
    }
    if (pg[0]) { add(b, " " ENDASH " "); pages_dash(b, pg); add(b, " p."); }
  }
  if (G(s, "URL")[0]) { add(b, " " ENDASH " URL: "); add(b, G(s, "URL")); P(b, "."); }
}

/* SIST 02: n) Surname, First; ... Title. City, Publisher, Year, p. pages. */
static void e_sist(E *e)
{
  Buf *b = e->b;
  const CiteSource *s = e->s;
  int ed, g = e->g;
  const CiteRole *r = main_role(s, &ed);
  NameRule nr = { NM_LF, NM_LF, "; ", "; ", "; ", 0, 0, "", 0 };
  const char *t = G(s, "Title"), *pg = G(s, "Pages"), *pub = publisher(s), *ct = container(s, g);
  buf_printf(b, "%d)\t", e->n);
  if (r) { names(b, r, &nr); P(b, ". "); }
  add(b, t); P(b, ".");
  if (g == GR_JOUR || g == GR_PERI || g == GR_SECT || g == GR_CONF) {
    add(b, " "); add(b, ct); P(b, ". "); year(b, s, 0, "");
    if (G(s, "Volume")[0]) { add(b, ", vol. "); add(b, G(s, "Volume")); }
    if (G(s, "Issue")[0]) { add(b, ", no. "); add(b, G(s, "Issue")); }
    if (pg[0]) { add(b, ", p. "); pages_dash(b, pg); }
    P(b, ".");
  } else {
    if (G(s, "Edition")[0]) { add(b, " "); edition(b, G(s, "Edition"), "ed."); add(b, ","); }
    add(b, " ");
    if (G(s, "City")[0]) { add(b, G(s, "City")); add(b, ", "); }
    if (pub[0]) { add(b, pub); add(b, ", "); }
    year(b, s, 0, "");
    if (pg[0]) { add(b, ", "); pages_dash(b, pg); add(b, "p"); }
    P(b, ".");
  }
  if (G(s, "URL")[0]) { add(b, " "); add(b, G(s, "URL")); P(b, ","); add(b, " (accessed "); add(b, G(s, "YearAccessed")); if (G(s, "MonthAccessed")[0]) { add(b, "-"); month(b, G(s, "MonthAccessed"), 3); } add(b, ")."); }
}

void cite_entry(const CiteList *l, int i, int cs, int n, Buf *out)
{
  E e;
  if (i < 0 || i >= l->n) return;
  e.l = l; e.s = &l->s[i]; e.cs = cs; e.n = n; e.g = group_of(e.s); e.b = out;
  e.sfx = cite_year_suffix(l, i, cs);
  if (cite_is_placeholder(e.s)) { add(out, cite_get(e.s, "Tag")); return; }
  switch (cs) {
  case CS_APA6: case CS_APA7: e_apa(&e); break;
  case CS_MLA7: case CS_MLA8: case CS_MLA9: e_mla(&e); break;
  case CS_CHICAGO16: case CS_CHICAGO17: case CS_TURABIAN: e_chicago(&e); break;
  case CS_HARVARD: e_harvard(&e); break;
  case CS_IEEE: e_ieee(&e); break;
  case CS_ISO690: case CS_ISO690NUM: e_iso(&e); break;
  case CS_GB7714: e_gb(&e); break;
  case CS_GOSTNAME: case CS_GOSTTITLE: e_gost(&e); break;
  default: e_sist(&e); break;
  }
  while (out->len && out->data[out->len - 1] == ' ') out->data[--out->len] = 0;
}

/* ---- citations */
/* the surnames a citation names */
static void who_cite(Buf *b, const CiteRole *r, int cs, int first)
{
  int n = role_count(r), i, etal, upper = cs == CS_ISO690;
  const char *two = " and ", *lastsep = ", and ";
  if (r->corp) { if (upper) upper_into(b, r->corp); else add(b, r->corp); return; }
  switch (cs) {
  case CS_APA6: etal = first ? 6 : 3; two = " & "; lastsep = ", & "; break;
  case CS_APA7: etal = 3; two = " & "; break;
  case CS_MLA7: etal = 4; break;
  case CS_MLA8: case CS_MLA9: etal = 3; break;
  case CS_HARVARD: case CS_ISO690: etal = 4; lastsep = " and "; break;
  default: etal = 4; break;
  }
  if (n >= etal) {
    if (upper) upper_into(b, r->p[0].last); else add(b, r->p[0].last);
    add(b, cs == CS_ISO690 ? ", et al." : " et al.");
    return;
  }
  for (i = 0; i < n; i++) {
    if (i) add(b, n == 2 ? two : i == n - 1 ? lastsep : ", ");
    if (upper) upper_into(b, r->p[i].last); else add(b, r->p[i].last);
  }
}
void cite_cite_one(const CiteList *l, int i, int cs, int n, const char *pages, const char *volume, int suppress, int first, Buf *b)
{
  const CiteSource *s;
  const CiteRole *r;
  int ed, g, any = 0, sfx;
  size_t start = b->len;
  if (i < 0 || i >= l->n) return;
  s = &l->s[i];
  if (!pages) pages = "";
  if (!volume) volume = "";
  if (cite_is_placeholder(s)) { add(b, cite_get(s, "Tag")); return; }
  if (cite_numbered(cs)) {
    buf_printf(b, "%d", n);
    if (volume[0]) { add(b, ", Vol. "); add(b, volume); }
    if (pages[0]) { add(b, cs == CS_GB7714 ? ": " : is_range(pages) ? ", pp. " : ", p. "); pages_dash(b, pages); }
    return;
  }
  r = main_role(s, &ed);
  g = group_of(s);
  sfx = cite_year_suffix(l, i, cs);
  if (r && !(suppress & CITE_NO_AUTHOR)) { who_cite(b, r, cs, first); any = 1; }
  else if (!(suppress & CITE_NO_TITLE) && cite_get(s, "Title")[0]) {
    if (is_part(s, g)) { add(b, LQ); add(b, cite_get(s, "Title")); add(b, RQ); }
    else add(b, cite_get(s, "Title"));
    any = 1;
  }
  if (!(suppress & CITE_NO_YEAR) && cs != CS_MLA7 && cs != CS_MLA8 && cs != CS_MLA9) {
    if (any) add(b, cs == CS_CHICAGO16 || cs == CS_CHICAGO17 || cs == CS_TURABIAN ? " " : ", ");
    year(b, s, sfx, "n.d.");
    any = 1;
  }
  if (cs == CS_MLA7 || cs == CS_MLA8 || cs == CS_MLA9) {
    if (volume[0] || pages[0]) { if (any) add(b, " "); if (volume[0]) { add(b, volume); if (pages[0]) add(b, ": "); } pages_dash(b, pages); }
    return;
  }
  if (volume[0]) { if (any) add(b, ", "); add(b, "Vol. "); add(b, volume); any = 1; }
  if (pages[0]) {
    if (any) add(b, ", ");
    if (cs != CS_CHICAGO16 && cs != CS_CHICAGO17 && cs != CS_TURABIAN) add(b, is_range(pages) ? "pp. " : "p. ");
    pages_dash(b, pages);
  }
  if (b->len == start) add(b, cite_get(s, "Tag"));
}

/* ------------------------------------------------------------------ */
/* the document */
CiteDoc *cite_doc(Doc *d)
{
  if (!d->cite) d->cite = (CiteDoc *)calloc(1, sizeof(CiteDoc));
  return d->cite;
}
void cite_free(Doc *d)
{
  if (!d || !d->cite) return;
  cite_list_free(&d->cite->list);
  free(d->cite->readxml);
  free(d->cite);
  d->cite = NULL;
}
int cite_doc_style(Doc *d) { return d->cite ? cite_style_of(&d->cite->list) : CS_APA6; }
/* the record a start mark at p of the body names, if it is a field named nm (else -1) */
static int named_at(Doc *d, Pos p, const char *nm)
{
  const Para *q = d->p[p.para];
  int k;
  char n[32];
  if (p.off + 4 > q->len || !IS_FSTART(q->t + p.off)) return -1;
  k = fmark_index(q->t + p.off);
  if (k < 0 || k >= d->nfields) return -1;
  field_name(d->fields[k].code, n, sizeof n);
  return strcmp(n, nm) ? -1 : k;
}
static int list_named(Doc *d, const char *nm, Pos *at, int max)
{
  u32 pi, o;
  int n = 0;
  if (!d->nfields) return 0;
  for (pi = 0; pi < d->np; pi++) {
    const Para *q = d->p[pi];
    for (o = 0; o + 4 <= q->len; o++) {
      Pos p;
      if ((u8)q->t[o] != 0xF4 || !IS_FSTART(q->t + o)) continue;
      p.para = pi; p.off = o;
      if (named_at(d, p, nm) >= 0) { if (n < max) at[n] = p; n++; }
      o += 3;
    }
  }
  return n;
}
int cite_list_citations(Doc *d, Pos *at, int max) { return list_named(d, "CITATION", at, max); }
/* src into dst (cap bytes), cut short */
static void scopy(char *dst, size_t cap, const char *src)
{
  size_t n = strlen(src);
  if (!cap) return;
  if (n >= cap) n = cap - 1;
  memcpy(dst, src, n); dst[n] = 0;
}
/* a CITATION code read: Word's switches \l \f \s \m \p \v take a value (quoted or not), \n \y \t don't */
typedef struct { char tag[16][64]; int nt; char pages[64], vol[32], pre[128], suf[128]; int suppress; } CCode;
static const char *ctoken(const char *s, char *out, size_t cap, int *quoted)
{
  size_t n = 0;
  *quoted = 0;
  while (*s == ' ' || *s == '\t') s++;
  if (!*s) return NULL;
  if (*s == '"') {
    *quoted = 1;
    for (s++; *s && *s != '"'; s++) { if (*s == '\\' && (s[1] == '"' || s[1] == '\\')) s++; if (n + 1 < cap) out[n++] = *s; }
    if (*s) s++;
  } else for (; *s && *s != ' ' && *s != '\t'; s++) if (n + 1 < cap) out[n++] = *s;
  if (cap) out[n] = 0;
  return s;
}
static void read_code(const char *code, CCode *c)
{
  char t[256], v[256];
  int q, first = 1;
  const char *s = code;
  memset(c, 0, sizeof *c);
  c->nt = 1;                                            /* (tag[0]: the source's own tag) */
  while ((s = ctoken(s, t, sizeof t, &q))) {
    if (first) { first = 0; continue; }                 /* CITATION */
    if (!q && t[0] == '\\' && t[1]) {
      char sw = t[1];
      if (sw >= 'A' && sw <= 'Z') sw = (char)(sw + 32);
      if (sw == 'n') { c->suppress |= CITE_NO_AUTHOR; continue; }
      if (sw == 'y') { c->suppress |= CITE_NO_YEAR; continue; }
      if (sw == 't') { c->suppress |= CITE_NO_TITLE; continue; }
      if (strchr("lfsmpv", sw)) {
        const char *s2 = ctoken(s, v, sizeof v, &q);
        if (!s2) break;
        s = s2;
        if (sw == 'm' && c->nt < 16 && v[0]) scopy(c->tag[c->nt++], 64, v);
        else if (sw == 'p') scopy(c->pages, sizeof c->pages, v);
        else if (sw == 'v') scopy(c->vol, sizeof c->vol, v);
        else if (sw == 'f') scopy(c->pre, sizeof c->pre, v);
        else if (sw == 's') scopy(c->suf, sizeof c->suf, v);
      }
      continue;
    }
    if (!c->tag[0][0]) scopy(c->tag[0], 64, t);     /* the source's tag (first) */
  }
  if (!c->tag[0][0] && c->nt > 1) { memmove(c->tag[0], c->tag[1], (size_t)(c->nt - 1) * 64); c->nt--; }
  else if (!c->tag[0][0]) c->nt = 0;
}
/* the tags a citation's code names: its argument and every \m; how many (into tags[][64]) */
static int code_tags(const char *code, char tags[][64], int max)
{
  CCode c;
  int i;
  read_code(code, &c);
  for (i = 0; i < c.nt && i < max; i++) snprintf(tags[i], 64, "%s", c.tag[i]);
  return i;
}
#define MAXCITE 1024
int cite_bib_order(Doc *d, int cs, int *order, int *num, int max)
{
  CiteList *l = d->cite ? &d->cite->list : NULL;
  int i, n = 0, next = 1;
  if (!l || !l->n) return 0;
  for (i = 0; i < l->n && i < max; i++) num[i] = 0;
  if (cite_numbered(cs) && cs != CS_GOSTNAME && cs != CS_GOSTTITLE) {
    Pos *at = (Pos *)malloc(MAXCITE * sizeof(Pos));
    int nc = at ? cite_list_citations(d, at, MAXCITE) : 0, c, t;
    if (nc > MAXCITE) nc = MAXCITE;
    for (c = 0; c < nc; c++) {
      char tags[16][64];
      int nt = code_tags(d->fields[named_at(d, at[c], "CITATION")].code, tags, 16);
      for (t = 0; t < nt; t++) { int k = cite_find(l, tags[t]); if (k >= 0 && k < max && !num[k] && !cite_is_placeholder(&l->s[k])) num[k] = next++; }
    }
    free(at);
    for (i = 0; i < l->n && i < max; i++) if (!num[i] && !cite_is_placeholder(&l->s[i])) num[i] = next++;
    for (c = 1; c < next; c++) for (i = 0; i < l->n && i < max; i++) if (num[i] == c) order[n++] = i;
    return n;
  }
  /* alphabetical (GOST title sort: by title) */
  {
    char ki[400], kj[400];
    int j;
    for (i = 0; i < l->n && i < max; i++) if (!cite_is_placeholder(&l->s[i])) order[n++] = i;
    for (i = 1; i < n; i++) {
      int v = order[i];
      sort_key(&l->s[v], cs == CS_GOSTTITLE, ki, sizeof ki);
      for (j = i; j > 0; j--) {
        sort_key(&l->s[order[j - 1]], cs == CS_GOSTTITLE, kj, sizeof kj);
        if (strcmp(kj, ki) <= 0) break;
        order[j] = order[j - 1];
      }
      order[j] = v;
    }
    for (i = 0; i < n; i++) num[order[i]] = i + 1;
  }
  return n;
}
/* whether a citation of tag comes before at in the body */
static int cited_before(Doc *d, const char *tag, Pos at)
{
  Pos *p = (Pos *)malloc(MAXCITE * sizeof(Pos));
  int n = p ? cite_list_citations(d, p, MAXCITE) : 0, i, t, r = 0;
  if (n > MAXCITE) n = MAXCITE;
  for (i = 0; i < n && !r && pos_cmp(p[i], at) < 0; i++) {
    char tags[16][64];
    int nt = code_tags(d->fields[named_at(d, p[i], "CITATION")].code, tags, 16);
    for (t = 0; t < nt; t++) if (str_ieq(tags[t], tag)) r = 1;
  }
  free(p);
  return r;
}
int cite_is_cited(Doc *d, const char *tag)
{
  Pos e;
  e.para = d->np; e.off = 0;
  return cited_before(d, tag, e);
}
/* drop the italic markers */
static void plain(char *s)
{
  char *o = s;
  for (; *s; s++) if (*s != CITE_I0 && *s != CITE_I1) *o++ = *s;
  *o = 0;
}
int cite_eval(Doc *d, const char *code, Pos at, char *out, size_t cap)
{
  CiteList *l = d->cite ? &d->cite->list : NULL;
  CCode cc;
  int nt, t, cs, *num = NULL, *order = NULL, suppress, numbered;
  char (*tags)[64] = cc.tag, *pages = cc.pages, *vol = cc.vol, *pre = cc.pre, *suf = cc.suf;
  Buf b;
  if (!l || !l->n) return FE_KEEP;
  read_code(code, &cc);
  nt = cc.nt;
  suppress = cc.suppress;
  if (!nt) return FE_KEEP;
  for (t = 0; t < nt; t++) if (cite_find(l, tags[t]) < 0) return FE_KEEP;     /* (a source no longer in the list: its result stays) */
  cs = cite_style_of(l);
  numbered = cite_numbered(cs);
  if (numbered) {
    num = (int *)calloc((size_t)l->n, sizeof(int)); order = (int *)calloc((size_t)l->n, sizeof(int));
    if (!num || !order) { free(num); free(order); return FE_KEEP; }
    cite_bib_order(d, cs, order, num, l->n);
  }
  buf_init(&b);
  for (t = 0; t < nt; t++) {
    int k = cite_find(l, tags[t]);
    int first = cs == CS_APA6 && !cited_before(d, tags[t], at);
    if (cs == CS_IEEE) { if (t) add(&b, ", "); add(&b, "["); if (!t) add(&b, pre); }
    else if (!t) { add(&b, numbered && cs != CS_ISO690NUM && cs != CS_SIST02 ? "[" : "("); add(&b, pre); }
    else add(&b, numbered ? ", " : "; ");
    cite_cite_one(l, k, cs, numbered ? num[k] : 0, t ? "" : pages, t ? "" : vol, t ? 0 : suppress, first, &b);
    if (cs == CS_IEEE) { if (t == nt - 1) add(&b, suf); add(&b, "]"); }
  }
  if (cs != CS_IEEE) { add(&b, suf); add(&b, numbered && cs != CS_ISO690NUM && cs != CS_SIST02 ? "]" : ")"); }
  free(num); free(order);
  snprintf(out, cap, "%s", b.data ? b.data : "");
  buf_free(&b);
  plain(out);
  return FE_TEXT;
}
void cite_code(char *out, size_t cap, const char *tag, const char *pages, int suppress, const char *const *more, int nmore)
{
  size_t n;
  int i;
  snprintf(out, cap, " CITATION %s ", tag);
  n = strlen(out);
  if (pages && pages[0] && n < cap) { snprintf(out + n, cap - n, strchr(pages, ' ') ? "\\p \"%s\" " : "\\p %s ", pages); n = strlen(out); }
  if ((suppress & CITE_NO_AUTHOR) && n < cap) { snprintf(out + n, cap - n, "\\n "); n = strlen(out); }
  if ((suppress & CITE_NO_YEAR) && n < cap) { snprintf(out + n, cap - n, "\\y "); n = strlen(out); }
  if ((suppress & CITE_NO_TITLE) && n < cap) { snprintf(out + n, cap - n, "\\t "); n = strlen(out); }
  if (n < cap) { snprintf(out + n, cap - n, "\\l 1033 "); n = strlen(out); }
  for (i = 0; i < nmore && n < cap; i++) { snprintf(out + n, cap - n, "\\m %s ", more[i]); n = strlen(out); }
}
int cite_insert(Doc *d, Pos *at, const char *tag, const char *pages, int suppress, const char *const *more, int nmore)
{
  char code[512];
  FieldEnv e;
  int k;
  cite_code(code, sizeof code, tag, pages, suppress, more, nmore);
  field_env(&e);
  doc_begin(d, *at);
  k = field_insert_result(d, at, code, NULL, &e, NULL);
  /* numbered styles: the numbers go by the order of first citation, so the others may change (and the
     new one was worked out before its marks were in) */
  if (k >= 0 && cite_numbered(cite_doc_style(d))) {
    Pos s, en;
    if (cite_field_at(d, pos_left(d, *at), 0, &s, &en) >= 0) { cite_update_all(d); if (field_end(d, s, &en)) { en.off += 4; *at = en; } }
  }
  doc_end(d, *at);
  return k;
}
int cite_field_at(Doc *d, Pos p, int bib, Pos *s, Pos *e)
{
  Pos ss, ee;
  int k;
  if (!d->nfields) return -1;
  k = field_at(d, p, &ss, &ee);
  if (k < 0 || named_at(d, ss, bib ? "BIBLIOGRAPHY" : "CITATION") < 0) {
    /* a citation inside something else (a bibliography holds none, but a hyperlink may hold one) */
    Pos at[8];
    int n = field_list(d, p, p, at, 8), i;
    for (i = n < 8 ? n : 8; i-- > 0;)
      if (named_at(d, at[i], bib ? "BIBLIOGRAPHY" : "CITATION") >= 0 && field_end(d, at[i], &ee) && pos_cmp(at[i], p) <= 0 &&
          (ee.para > p.para || (ee.para == p.para && ee.off + 4 >= p.off))) { ss = at[i]; break; }
    if (i < 0) return -1;
  }
  if (s) *s = ss;
  if (e) *e = ee;
  return named_at(d, ss, bib ? "BIBLIOGRAPHY" : "CITATION");
}

/* ---- the bibliography's result */
/* the Bibliography style (Word's built-in one), made if the document hasn't got it */
static int bib_style(Doc *d)
{
  int k = doc_style(d, "Bibliography"), nm;
  Style st;
  if (k >= 0) return k;
  nm = doc_style(d, "Normal");
  if (nm < 0) nm = 0;
  st = d->styles[nm];
  snprintf(st.id, sizeof st.id, "Bibliography");
  snprintf(st.name, sizeof st.name, "Bibliography");
  st.type = ST_PARA; st.builtin = 1; st.quick = 0; st.next = (u8)nm;
  k = doc_add_style_from(d, &st, nm);
  return k >= 0 ? k : nm;
}
/* bytes a..b of p appended to q with their formatting */
static int append_range(Para *q, const Para *p, u32 a, u32 b)
{
  u32 r, start = 0;
  for (r = 0; r < p->nr; r++) {
    u32 e = p->r[r].end, lo = start > a ? start : a, hi = e < b ? e : b;
    if (hi > lo && !para_append(q, p->t + lo, hi - lo, &p->r[r].f)) return 0;
    start = e;
  }
  return 1;
}
static int rebuild_bib(Doc *d, Pos s, Pos e)
{
  CiteList *l = d->cite ? &d->cite->list : NULL;
  int cs = cite_doc_style(d), n = 0, i, sty, np = 0, ok = 1, ital = 0;
  int *order = NULL, *num = NULL;
  Buf t;
  Para **arr = NULL, *q, *ps = d->p[s.para], *pe = d->p[e.para];
  ParaFmt pf;
  CharFmt cf;
  const char *c;
  size_t cnt = 1;
  buf_init(&t);
  if (l && l->n) {
    order = (int *)calloc((size_t)l->n, sizeof(int)); num = (int *)calloc((size_t)l->n, sizeof(int));
    if (order && num) n = cite_bib_order(d, cs, order, num, l->n);
  }
  for (i = 0; i < n; i++) { if (i) buf_addc(&t, '\n'); cite_entry(l, order[i], cs, num[order[i]], &t); }
  if (!n) add(&t, "There are no sources in the current document.");
  free(order); free(num);
  if (t.oom) { buf_free(&t); return 0; }
  for (c = t.data; *c; c++) if (*c == '\n') cnt++;
  if (!(arr = (Para **)calloc(cnt, sizeof(Para *)))) { buf_free(&t); return 0; }
  sty = bib_style(d);
  ps = d->p[s.para]; pe = d->p[e.para];                    /* (the style table may have moved nothing, but be safe) */
  pf = d->styles[sty].pf;
  pf.style = (u8)sty;
  pf.left = 720; pf.first = -720;
  pf.cell = ps->pf.cell; pf.outer = ps->pf.outer;
  pf.sect = 0; pf.pagebreak = 0; pf.colbreak = 0; pf.list = 0;
  cf = *para_fmt_at(ps, s.off);
  cf.flags &= (u8)~CF_ITALIC;
  cf.link = 0;
  q = para_new(s.off ? &ps->pf : &pf, &cf);
  if (!q || !append_range(q, ps, 0, s.off + 4)) ok = 0;
  for (c = t.data; ok && *c;) {
    const char *e2 = c;
    CharFmt f = cf;
    if (*c == '\n') {
      arr[np++] = q;
      if (!(q = para_new(&pf, &cf))) ok = 0;
      c++;
      continue;
    }
    if (*c == CITE_I1 || *c == CITE_I0) { ital = *c == CITE_I1; c++; continue; }
    while (*e2 && *e2 != '\n' && *e2 != CITE_I1 && *e2 != CITE_I0) e2++;
    if (ital) f.flags |= CF_ITALIC;
    if (!para_append(q, c, (u32)(e2 - c), &f)) ok = 0;
    c = e2;
  }
  if (ok && q && !append_range(q, pe, e.off, pe->len)) ok = 0;
  if (q) { q->pf.sect = pe->pf.sect; if (pe->pf.sect) { q->pf.pagebreak = 0; } arr[np++] = q; q = NULL; }
  buf_free(&t);
  for (i = 0; i < np; i++) para_normalise(arr[i]);
  if (ok && (u32)np == e.para - s.para + 1) {
    /* nothing changed: left alone (no undo step, not counted) */
    int same = 1;
    for (i = 0; i < np && same; i++) {
      const Para *o = d->p[s.para + (u32)i], *nw = arr[i];
      u32 r;
      if (o->len != nw->len || memcmp(o->t, nw->t, o->len) || o->nr != nw->nr || o->pf.style != nw->pf.style || o->pf.left != nw->pf.left ||
          o->pf.first != nw->pf.first) same = 0;
      for (r = 0; same && r < o->nr; r++) if (o->r[r].end != nw->r[r].end || o->r[r].f.flags != nw->r[r].f.flags) same = 0;
    }
    if (same) ok = 0;
  }
  if (!ok) { for (i = 0; i < np; i++) para_free(arr[i]); free(arr); return 0; }
  ok = doc_replace(d, s.para, e.para - s.para + 1, arr, (u32)np, s);
  free(arr);
  return ok;
}
int cite_update_bibs(Doc *d, Pos a, Pos b)
{
  Pos at[64], e;
  int n, i, done = 0;
  if (!d->nfields) return 0;
  n = field_list(d, a, b, at, 64);
  if (n > 64) n = 64;
  for (i = n; i-- > 0;) {
    int k = named_at(d, at[i], "BIBLIOGRAPHY");
    if (k < 0 || d->fields[k].locked || !field_end(d, at[i], &e)) continue;
    if (rebuild_bib(d, at[i], e)) done++;
  }
  return done;
}
int cite_insert_bib(Doc *d, Pos *at, int kind)
{
  static const char *const titles[4] = { "", "Bibliography", "References", "Works Cited" };
  Pos p = pos_clamp(d, *at), st, en;
  FieldEnv fe;
  int k, h;
  doc_begin(d, p);
  if (p.off > 0) doc_insert(d, &p, "\n", 1, NULL);
  if (d->p[p.para]->len > 0) { Pos q = p; doc_insert(d, &q, "\n", 1, NULL); }   /* (the text after stays in its own paragraph) */
  if (kind > 0 && kind < 4) {
    doc_insert(d, &p, titles[kind], (u32)strlen(titles[kind]), NULL);
    h = doc_style(d, "Heading1");
    if (h >= 0) doc_apply_style(d, p.para, p.para, h);
    doc_insert(d, &p, "\n", 1, NULL);
    if (h >= 0 && d->p[p.para]->pf.style == (u8)h) { int nm = doc_style(d, "Normal"); if (nm >= 0) doc_apply_style(d, p.para, p.para, nm); }
  }
  st = p;
  field_env(&fe);
  k = field_insert_result(d, &p, " BIBLIOGRAPHY ", "", &fe, NULL);
  if (k >= 0) {
    cite_update_bibs(d, st, st);
    if (field_end(d, st, &en)) { en.off += 4; p = en; }
  }
  doc_end(d, p);
  *at = p;
  return k;
}
int cite_update_all(Doc *d)
{
  Pos *at = (Pos *)malloc(MAXCITE * sizeof(Pos));
  int n, i, done = 0;
  FieldEnv e;
  if (!at) return 0;
  field_env(&e);
  doc_begin(d, doc_start(d));
  n = cite_list_citations(d, at, MAXCITE);
  if (n > MAXCITE) n = MAXCITE;
  for (i = n; i-- > 0;) done += field_update(d, at[i], at[i], &e) > 0;
  done += cite_update_bibs(d, doc_start(d), doc_end_pos(d));
  doc_end(d, doc_start(d));
  free(at);
  return done;
}
int cite_to_static(Doc *d, Pos a, Pos b, int bib)
{
  Pos at[256];
  int n, i, done = 0;
  FieldEnv e;
  if (!d->nfields) return 0;
  field_env(&e);
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  n = field_list(d, a, b, at, 256);
  if (n > 256) n = 256;
  doc_begin(d, a);
  for (i = n; i-- > 0;)
    if (named_at(d, at[i], "CITATION") >= 0 || (bib && named_at(d, at[i], "BIBLIOGRAPHY") >= 0)) {
      Pos en;
      if (!field_end(d, at[i], &en)) continue;
      if (en.para == at[i].para) done += field_unlink(d, at[i], at[i], &e) > 0;
      else {
        /* a bibliography over several paragraphs: its marks taken out, the text kept */
        Pos x = en, y = en;
        y.off += 4;
        doc_delete(d, x, y);
        x = at[i]; y = at[i]; y.off += 4;
        doc_delete(d, x, y);
        done++;
      }
    }
  doc_end(d, a);
  return done;
}
/* code with the tag old (its argument or a \m value) renamed; malloc'd, NULL if it hasn't it */
static char *retag_code(const char *code, const char *old, const char *nw)
{
  Buf b;
  const char *p = code;
  size_t ol = strlen(old);
  int hit = 0;
  buf_init(&b);
  while (*p) {
    if ((p == code || p[-1] == ' ') && !strncmp(p, old, ol) && (p[ol] == ' ' || !p[ol])) { add(&b, nw); p += ol; hit = 1; continue; }
    buf_addc(&b, *p++);
  }
  if (!hit || b.oom) { buf_free(&b); return NULL; }
  return buf_take(&b);
}
int cite_retag(Doc *d, const char *old, const char *nw)
{
  Pos *at = (Pos *)malloc(MAXCITE * sizeof(Pos));
  int n, i, done = 0;
  if (!at) return 0;
  n = cite_list_citations(d, at, MAXCITE);
  if (n > MAXCITE) n = MAXCITE;
  for (i = n; i-- > 0;) {
    int k = named_at(d, at[i], "CITATION");
    char tags[16][64], *c;
    int nt = code_tags(d->fields[k].code, tags, 16), t, has = 0;
    for (t = 0; t < nt; t++) if (str_ieq(tags[t], old)) has = 1;
    if (!has || !(c = retag_code(d->fields[k].code, tags[0][0] && str_ieq(tags[0], old) ? tags[0] : old, nw))) continue;
    done += field_set_code(d, at[i], c);
    free(c);
  }
  free(at);
  return done;
}

/* ------------------------------------------------------------------ */
/* .docx: the current list is a customXml part (customXml/itemN.xml with its itemPropsN.xml naming the
   bibliography schema), related from the document */
void cite_docx_read(Doc *d)
{
  int i;
  for (i = 0; i < d->nkept; i++) {
    KeptPart *k = &d->kept[i];
    CiteDoc *c;
    CiteList l;
    size_t j, n;
    int hit = 0;
    if (strncmp(k->name, "customXml/", 10) || strstr(k->name, "itemProps") || strstr(k->name, "_rels")) continue;
    for (j = 0; j + 20 < k->len && j < 400 && !hit; j++) if (!memcmp(k->data + j, "Sources", 7)) hit = 1;
    if (!hit) continue;
    memset(&l, 0, sizeof l);
    if (!cite_parse(&l, (const char *)k->data, k->len)) { cite_list_free(&l); continue; }
    if (!(c = cite_doc(d))) { cite_list_free(&l); return; }
    cite_list_free(&c->list);
    c->list = l;
    snprintf(c->part, sizeof c->part, "%s", k->name);
    free(c->readxml);
    c->readxml = cite_xml(&c->list, &n);
    return;
  }
}
/* the part stays among the kept ones (core/keep.h), written back byte for byte while the list is as
   it was read; a changed list replaces its data, and a document that had none gets a new part:
   customXml/itemN.xml, its itemPropsN.xml naming the bibliography schema, and their relationship */
int cite_sync(Doc *d)
{
  CiteDoc *c = d->cite;
  char *x;
  size_t n;
  int k;
  if (!c) return 1;
  if (!(x = cite_xml(&c->list, &n))) return 0;
  if (c->part[0] && (k = keep_find(d, c->part)) >= 0) {
    KeptPart *kp = &d->kept[k];
    if (!(c->readxml && !strcmp(c->readxml, x))) { free(kp->data); kp->data = (u8 *)x; kp->len = n; x = NULL; }
    free(x);
    return 1;
  }
  if (!c->list.n && !c->part[0]) { free(x); return 1; }
  {
    char item[64], props[64], rels[80], guid[48];
    Buf p, r;
    for (k = 1; k < 100; k++) {
      sprintf(item, "customXml/item%d.xml", k); sprintf(props, "customXml/itemProps%d.xml", k);
      if (keep_find(d, item) < 0 && keep_find(d, props) < 0) break;
    }
    sprintf(rels, "customXml/_rels/item%d.xml.rels", k);
    cite_make_guid(guid);
    buf_init(&p); buf_init(&r);
    buf_printf(&p, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\r\n<ds:datastoreItem ds:itemID=\"%s\" "
               "xmlns:ds=\"http://schemas.openxmlformats.org/officeDocument/2006/customXml\"><ds:schemaRefs>"
               "<ds:schemaRef ds:uri=\"" BIBNS "\"/></ds:schemaRefs></ds:datastoreItem>", guid);
    buf_printf(&r, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
               "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXmlProps\" "
               "Target=\"itemProps%d.xml\"/></Relationships>", k);
    k = !p.oom && !r.oom &&
        keep_add(d, item, NULL, "word/document.xml", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml", "rIdW3Src", x, n) >= 0 &&
        keep_add(d, props, "application/vnd.openxmlformats-officedocument.customXmlProperties+xml", NULL, NULL, NULL, p.data, p.len) >= 0 &&
        keep_add(d, rels, NULL, NULL, NULL, NULL, r.data, r.len) >= 0;
    if (k) snprintf(c->part, sizeof c->part, "%s", item);
    buf_free(&p); buf_free(&r);
  }
  free(x);
  return k;
}
