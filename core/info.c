/* info.c - WP24, see info.h */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "info.h"
#include "xml.h"

static char clock_iso[32], user_name[128];
void info_set_clock(const char *iso) { snprintf(clock_iso, sizeof clock_iso, "%s", iso ? iso : ""); }
void info_set_user(const char *u) { snprintf(user_name, sizeof user_name, "%s", u ? u : ""); }

DocInfo *info_get(Doc *d)
{
  if (!d->info) d->info = (DocInfo *)calloc(1, sizeof(DocInfo));
  return d->info;
}
const char *info_str(const Doc *d, int which)
{
  return d->info && which >= 0 && which < IP_N && d->info->v[which] ? d->info->v[which] : "";
}
/* *dst replaced by a copy of s (NULL for empty); 0 if out of memory */
static int setstr(char **dst, const char *s)
{
  char *c = NULL;
  if (s && s[0] && !(c = str_dup(s))) return 0;
  free(*dst);
  *dst = c;
  return 1;
}
int info_set(Doc *d, int which, const char *utf8)
{
  DocInfo *in;
  if (which < 0 || which >= IP_N || !(in = info_get(d))) return 0;
  return setstr(&in->v[which], utf8);
}
static void raws_free(RawEl **r, int *n)
{
  int i;
  for (i = 0; i < *n; i++) { free((*r)[i].name); free((*r)[i].raw); }
  free(*r); *r = NULL; *n = 0;
}
static void cust_free1(CustProp *c) { free(c->name); free(c->value); free(c->raw); free(c->link); }
void info_free(Doc *d)
{
  DocInfo *in = d->info;
  int i;
  if (!in) return;
  for (i = 0; i < IP_N; i++) free(in->v[i]);
  raws_free(&in->core, &in->ncore); raws_free(&in->app, &in->napp);
  for (i = 0; i < in->ncust; i++) cust_free1(&in->cust[i]);
  free(in->cust); free(in->coreroot); free(in->approot); free(in->custroot);
  free(in);
  d->info = NULL;
}
int info_cust_find(const Doc *d, const char *name)
{
  int i;
  if (!d->info) return -1;
  for (i = 0; i < d->info->ncust; i++) if (!strcmp(d->info->cust[i].name, name)) return i;
  return -1;
}
int info_cust_set(Doc *d, const char *name, int type, const char *value)
{
  DocInfo *in = info_get(d);
  int k;
  CustProp *c;
  if (!in || !name || !name[0]) return 0;
  if ((k = info_cust_find(d, name)) < 0) {
    CustProp *n = (CustProp *)realloc(in->cust, (size_t)(in->ncust + 1) * sizeof(CustProp));
    if (!n) return 0;
    in->cust = n;
    k = in->ncust++;
    memset(&in->cust[k], 0, sizeof(CustProp));
    if (!(in->cust[k].name = str_dup(name))) { in->ncust--; return 0; }
  }
  c = &in->cust[k];
  c->type = (u8)(type >= CP_TEXT && type <= CP_BOOL ? type : CP_TEXT);
  free(c->raw); c->raw = NULL;
  free(c->value);
  c->value = str_dup(value ? value : "");
  return c->value != NULL;
}
void info_cust_remove(Doc *d, int k)
{
  DocInfo *in = d->info;
  if (!in || k < 0 || k >= in->ncust) return;
  cust_free1(&in->cust[k]);
  memmove(&in->cust[k], &in->cust[k + 1], (size_t)(in->ncust - k - 1) * sizeof(CustProp));
  in->ncust--;
}

/* ---- reading */
/* the root start tag just read (its text, "/>" made ">"), malloc'd */
static char *root_tag(Xml *x)
{
  size_t a = x->tok, b = x->i, n;
  char *r;
  if (b <= a || b > x->n) return NULL;
  n = b - a;
  if (!(r = (char *)malloc(n + 2))) return NULL;
  memcpy(r, x->s + a, n); r[n] = 0;
  if (n >= 2 && r[n - 2] == '/' && r[n - 1] == '>') { r[n - 2] = '>'; r[n - 1] = 0; }
  return r;
}
/* the text of the element just started, to its end (malloc'd, "" if none) */
static char *elem_text(Xml *x)
{
  int d0 = x->depth;
  Buf b;
  buf_init(&b);
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type == XT_TEXT) buf_add(&b, x->text.data, x->text.len);
  }
  buf_add(&b, "", 1);
  if (b.oom) { buf_free(&b); return NULL; }
  return buf_take(&b);
}
static void raw_add(RawEl **r, int *n, const char *name, const char *raw, size_t len)
{
  RawEl *nr = (RawEl *)realloc(*r, (size_t)(*n + 1) * sizeof(RawEl));
  char *c;
  if (!nr) return;
  *r = nr;
  if (!(c = (char *)malloc(len + 1))) return;
  memcpy(c, raw, len); c[len] = 0;
  if (!(nr[*n].name = str_dup(name))) { free(c); return; }
  nr[*n].raw = c;
  (*n)++;
}
static const char *const core_names[] = { "subject", "keywords", "description", "category", "contentStatus", "lastModifiedBy", "revision",
  "created", "modified", "lastPrinted", "language", "identifier", "version" };
void info_read_core(Doc *d, const char *xml, size_t n)
{
  DocInfo *in = info_get(d);
  Xml x;
  if (!in) return;
  xml_init(&x, xml, n);
  while (xml_next(&x) != XT_EOF) {
    int k;
    if (x.type != XT_START) continue;
    if (x.depth == 1) { free(in->coreroot); in->coreroot = root_tag(&x); continue; }
    if (x.depth != 2) continue;
    for (k = 0; k <= IP_VERSION; k++) if (!strcmp(x.name, core_names[k])) break;
    if (k <= IP_VERSION || !strcmp(x.name, "title") || !strcmp(x.name, "creator")) {
      int title = !strcmp(x.name, "title"), creator = !strcmp(x.name, "creator");
      char *t = elem_text(&x);
      if (!t) continue;
      if (title) { strncpy(d->title, t, sizeof d->title - 1); d->title[sizeof d->title - 1] = 0; }
      else if (creator) { strncpy(d->author, t, sizeof d->author - 1); d->author[sizeof d->author - 1] = 0; }
      else setstr(&in->v[k], t);
      free(t);
    } else {
      char name[64];
      const char *raw;
      size_t len;
      snprintf(name, sizeof name, "%s", x.name);
      len = xml_raw(&x, &raw);
      raw_add(&in->core, &in->ncore, name, raw, len);
    }
  }
  xml_free(&x);
}
static const char *const app_edit[4] = { "Company", "Manager", "HyperlinkBase", "Template" };     /* IP_COMPANY... */
void info_read_app(Doc *d, const char *xml, size_t n)
{
  DocInfo *in = info_get(d);
  Xml x;
  if (!in) return;
  raws_free(&in->app, &in->napp);
  xml_init(&x, xml, n);
  while (xml_next(&x) != XT_EOF) {
    char name[64];
    const char *raw;
    size_t len;
    int k;
    if (x.type != XT_START) continue;
    if (x.depth == 1) { free(in->approot); in->approot = root_tag(&x); continue; }
    if (x.depth != 2) continue;
    snprintf(name, sizeof name, "%s", x.name);
    len = xml_raw(&x, &raw);
    raw_add(&in->app, &in->napp, name, raw, len);
    for (k = 0; k < 4; k++)
      if (!strcmp(name, app_edit[k])) {
        /* its text: the raw element read again */
        Xml y;
        xml_init(&y, raw, len);
        if (xml_next(&y) == XT_START) { char *t = elem_text(&y); if (t) { setstr(&in->v[IP_COMPANY + k], t); free(t); } }
        xml_free(&y);
      }
  }
  xml_free(&x);
}
void info_read_custom(Doc *d, const char *xml, size_t n)
{
  DocInfo *in = info_get(d);
  Xml x;
  if (!in) return;
  xml_init(&x, xml, n);
  while (xml_next(&x) != XT_EOF) {
    const char *a;
    char name[256] = "", link[256] = "";
    int d0;
    if (x.type != XT_START) continue;
    if (x.depth == 1) { free(in->custroot); in->custroot = root_tag(&x); continue; }
    if (x.depth != 2 || strcmp(x.name, "property")) continue;
    if ((a = xml_attr(&x, "name"))) snprintf(name, sizeof name, "%s", a);
    if ((a = xml_attr(&x, "linkTarget"))) snprintf(link, sizeof link, "%s", a);
    d0 = x.depth;
    while (xml_next(&x) != XT_EOF) {
      const char *t = x.name;
      int type;
      if (x.type == XT_END && x.depth < d0) break;
      if (x.type != XT_START || !name[0]) continue;
      type = !strcmp(t, "lpwstr") || !strcmp(t, "lpstr") || !strcmp(t, "bstr") ? CP_TEXT :
             !strcmp(t, "filetime") || !strcmp(t, "date") ? CP_DATE : !strcmp(t, "bool") ? CP_BOOL :
             (t[0] == 'i' && t[1] >= '1' && t[1] <= '8') || (t[0] == 'u' && t[1] == 'i') || !strcmp(t, "int") || !strcmp(t, "uint") ||
             !strcmp(t, "r4") || !strcmp(t, "r8") || !strcmp(t, "decimal") ? CP_NUMBER : CP_OTHER;
      if (type == CP_OTHER) {
        const char *raw;
        size_t len = xml_raw(&x, &raw);
        if (info_cust_set(d, name, CP_TEXT, "")) {
          CustProp *c = &in->cust[info_cust_find(d, name)];
          c->type = CP_OTHER;
          if ((c->raw = (char *)malloc(len + 1))) { memcpy(c->raw, raw, len); c->raw[len] = 0; }
        }
      } else {
        char *v = elem_text(&x);
        if (v && type == CP_BOOL) { int on = !strcmp(v, "true") || !strcmp(v, "1"); free(v); v = str_dup(on ? "1" : "0"); }
        if (v) info_cust_set(d, name, type, v);
        free(v);
      }
      if (link[0] && info_cust_find(d, name) >= 0) { CustProp *c = &in->cust[info_cust_find(d, name)]; free(c->link); c->link = str_dup(link); }
      name[0] = 0;              /* (one value a property) */
    }
  }
  xml_free(&x);
}

/* ---- writing */
static void elem(Buf *b, const char *tag, const char *attrs, const char *v)
{
  if (!v || !v[0]) return;
  buf_printf(b, "<%s%s>", tag, attrs ? attrs : "");
  xml_escape(b, v, strlen(v));
  buf_printf(b, "</%s>", tag);
}
static const char *const CORE_ROOT = "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
  "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" xmlns:dcmitype=\"http://purl.org/dc/dcmitype/\" "
  "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">";
/* the root as read, if it names the four namespaces by the prefixes Quill writes */
static int core_root_ok(const char *r)
{
  return r && strstr(r, "xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\"") &&
         strstr(r, "xmlns:dc=\"http://purl.org/dc/elements/1.1/\"") && strstr(r, "xmlns:dcterms=\"http://purl.org/dc/terms/\"") &&
         strstr(r, "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"") && !strncmp(r, "<cp:coreProperties", 18);
}
void info_write_core(Doc *d, Buf *b)
{
  DocInfo *in = d->info;
  const char *own = in && core_root_ok(in->coreroot) ? in->coreroot : NULL;
  char rev[16];
  int i;
  static const char *const DT = " xsi:type=\"dcterms:W3CDTF\"";
  buf_adds(b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n");
  buf_adds(b, own ? own : CORE_ROOT);
  if (clock_iso[0] && !info_str(d, IP_CREATED)[0]) info_set(d, IP_CREATED, clock_iso);   /* (a new document's: once) */
  in = d->info;
  elem(b, "dc:title", NULL, d->title);
  elem(b, "dc:subject", NULL, info_str(d, IP_SUBJECT));
  elem(b, "dc:creator", NULL, d->author);
  elem(b, "cp:keywords", NULL, info_str(d, IP_KEYWORDS));
  elem(b, "dc:description", NULL, info_str(d, IP_DESCRIPTION));
  elem(b, "cp:lastModifiedBy", NULL, user_name[0] ? user_name : info_str(d, IP_LASTBY));
  /* the revision goes up by one each time the program saves */
  snprintf(rev, sizeof rev, "%ld", atol(info_str(d, IP_REVISION)) + (clock_iso[0] ? 1 : 0));
  elem(b, "cp:revision", NULL, strcmp(rev, "0") ? rev : (clock_iso[0] ? "1" : ""));
  elem(b, "cp:lastPrinted", NULL, info_str(d, IP_LASTPRINTED));
  elem(b, "dcterms:created", DT, info_str(d, IP_CREATED));
  elem(b, "dcterms:modified", DT, clock_iso[0] ? clock_iso : info_str(d, IP_MODIFIED));
  elem(b, "cp:category", NULL, info_str(d, IP_CATEGORY));
  elem(b, "cp:contentStatus", NULL, info_str(d, IP_STATUS));
  elem(b, "dc:language", NULL, info_str(d, IP_LANGUAGE));
  elem(b, "dc:identifier", NULL, info_str(d, IP_IDENTIFIER));
  elem(b, "cp:version", NULL, info_str(d, IP_VERSION));
  /* the others as they were (with Quill's root, only those in its namespaces) */
  for (i = 0; in && i < in->ncore; i++)
    if (own || !strncmp(in->core[i].raw, "<cp:", 4) || !strncmp(in->core[i].raw, "<dc:", 4) || !strncmp(in->core[i].raw, "<dcterms:", 9))
      buf_adds(b, in->core[i].raw);
  buf_adds(b, "</cp:coreProperties>");
}
void info_stats(Doc *d, u32 *words, u32 *chars, u32 *charsp, u32 *paras)
{
  u32 i, k;
  *words = doc_count_words(d); *chars = *charsp = *paras = 0;
  for (i = 0; i < d->np; i++) {
    const Para *p = d->p[i];
    if (p->len) (*paras)++;
    for (k = 0; k < p->len; k++) {
      u8 c = (u8)p->t[k];
      if ((c & 0xC0) == 0x80) continue;
      if (c >= 0xF3) { k += 3; continue; }                    /* (pictures', notes' and comments' marks) */
      if (c == '\t' || c == '\v') continue;
      (*charsp)++;
      if (c != ' ') (*chars)++;
    }
  }
}
/* the prefix the root's name has ("ep:" for <ep:Properties ...>; "" none) */
static void root_prefix(const char *root, char *out, size_t cap)
{
  const char *c, *sp;
  out[0] = 0;
  if (!root || root[0] != '<') return;
  c = strchr(root, ':'); sp = strpbrk(root, " >");
  if (c && (!sp || c < sp) && (size_t)(c - root) < cap) { memcpy(out, root + 1, (size_t)(c - root)); out[c - root] = 0; }
}
char *info_app_xml(Doc *d, size_t *len)
{
  DocInfo *in = d->info;
  Buf b;
  u32 words, chars, charsp, paras;
  char pf[24], val[32];
  static const char *const stat[5] = { "Pages", "Words", "Characters", "Paragraphs", "CharactersWithSpaces" };
  u8 done[5] = { 0, 0, 0, 0, 0 }, edone[4] = { 0, 0, 0, 0 }, appdone = 0;
  int i, k;
  info_stats(d, &words, &chars, &charsp, &paras);
  buf_init(&b);
  buf_adds(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n");
  root_prefix(in ? in->approot : NULL, pf, sizeof pf - 1);
  if (pf[0]) strcat(pf, ":");
  buf_adds(&b, in && in->approot ? in->approot : "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\" "
           "xmlns:vt=\"http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes\">");
#define APPEL(nm, v) do { buf_printf(&b, "<%s%s>", pf, nm); xml_escape(&b, v, strlen(v)); buf_printf(&b, "</%s%s>", pf, nm); } while (0)
  for (i = 0; i <= (in ? in->napp : 0); i++) {
    const char *nm = in && i < in->napp ? in->app[i].name : NULL;
    if (!nm) break;
    for (k = 0; k < 4 && strcmp(nm, app_edit[k]); k++) ;
    if (k < 4) { APPEL(nm, info_str(d, IP_COMPANY + k)); edone[k] = 1; continue; }
    if (!strcmp(nm, "Application")) { APPEL(nm, "Quill"); appdone = 1; continue; }
    for (k = 0; k < 5 && strcmp(nm, stat[k]); k++) ;
    if (k == 0 && !(in->pages)) { buf_adds(&b, in->app[i].raw); done[0] = 1; continue; }
    if (k < 5) {
      sprintf(val, "%lu", (unsigned long)(k == 0 ? in->pages : k == 1 ? words : k == 2 ? chars : k == 3 ? paras : charsp));
      APPEL(nm, val); done[k] = 1; continue;
    }
    buf_adds(&b, in->app[i].raw);
  }
  /* what the file didn't have (the schema takes them in any order) */
  for (k = 0; k < 4; k++) if (!edone[k] && info_str(d, IP_COMPANY + k)[0]) APPEL(app_edit[k], info_str(d, IP_COMPANY + k));
  if (!appdone) APPEL("Application", "Quill");
  for (k = 0; k < 5; k++) {
    if (done[k] || (k == 0 && !(in && in->pages))) continue;
    sprintf(val, "%lu", (unsigned long)(k == 0 ? in->pages : k == 1 ? words : k == 2 ? chars : k == 3 ? paras : charsp));
    APPEL(stat[k], val);
  }
#undef APPEL
  buf_printf(&b, "</%sProperties>", pf);
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return buf_take(&b);
}
char *info_custom_xml(Doc *d, size_t *len)
{
  DocInfo *in = d->info;
  Buf b;
  int i;
  const char *root = in && in->custroot && strstr(in->custroot, "xmlns:vt=\"http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes\"") &&
                     !strncmp(in->custroot, "<Properties", 11) ? in->custroot : NULL;
  if (!in || !in->ncust) return NULL;
  buf_init(&b);
  buf_adds(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n");
  buf_adds(&b, root ? root : "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/custom-properties\" "
           "xmlns:vt=\"http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes\">");
  for (i = 0; i < in->ncust; i++) {
    const CustProp *c = &in->cust[i];
    const char *v = c->value ? c->value : "";
    buf_printf(&b, "<property fmtid=\"{D5CDD505-2E9C-101B-9397-08002B2CF9AE}\" pid=\"%d\" name=\"", i + 2);
    xml_escape(&b, c->name, strlen(c->name));
    buf_adds(&b, "\"");
    if (c->link) { buf_adds(&b, " linkTarget=\""); xml_escape(&b, c->link, strlen(c->link)); buf_adds(&b, "\""); }
    buf_adds(&b, ">");
    switch (c->type) {
    case CP_OTHER: buf_adds(&b, c->raw ? c->raw : "<vt:lpwstr></vt:lpwstr>"); break;
    case CP_BOOL: buf_printf(&b, "<vt:bool>%s</vt:bool>", strcmp(v, "1") ? "false" : "true"); break;
    case CP_DATE: buf_adds(&b, "<vt:filetime>"); xml_escape(&b, v, strlen(v)); buf_adds(&b, "</vt:filetime>"); break;
    case CP_NUMBER: {
      /* a whole number as vt:i4, else vt:r8 */
      const char *q = v + (v[0] == '-');
      int whole = *q != 0;
      for (; *q; q++) if (*q < '0' || *q > '9') whole = 0;
      if (whole && strlen(v) > 9) whole = 0;
      buf_adds(&b, whole ? "<vt:i4>" : "<vt:r8>"); xml_escape(&b, v[0] ? v : "0", strlen(v[0] ? v : "0")); buf_adds(&b, whole ? "</vt:i4>" : "</vt:r8>");
      break; }
    default: buf_adds(&b, "<vt:lpwstr>"); xml_escape(&b, v, strlen(v)); buf_adds(&b, "</vt:lpwstr>");
    }
    buf_adds(&b, "</property>");
  }
  buf_adds(&b, "</Properties>");
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return buf_take(&b);
}
