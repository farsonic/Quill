/*
 * mergedocx.c - a mail merge main document's settings in .docx (WP4, core/merge.h): settings.xml's
 * w:mailMerge (ECMA-376 17.14) read into Doc.merge and written back, the data source's path through
 * word/_rels/settings.xml.rels, Match Fields as w:odso/w:fieldMapData. What Quill doesn't model is
 * kept verbatim (w:recipientData is dropped: Quill keeps which recipients are checked itself).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "merge.h"

static const char *const types[] = { "", "formLetters", "envelopes", "mailingLabels", "catalog", "email", "fax" };
#define NTYPES 7
/* the relationships the file's w:mailMerge names (read in merge_read_setting, resolved in merge_read_parts) */
static char rid_ds[64], rid_src[64];

static int on(Xml *x)
{
  const char *v = xml_attr(x, "val");
  return !v || (strcmp(v, "0") && strcmp(v, "false") && strcmp(v, "off"));
}
static void sval(Xml *x, char *out, size_t cap) { const char *v = xml_attr(x, "val"); snprintf(out, cap, "%s", v ? v : ""); }
static void append(char **dst, const char *s, size_t n)
{
  size_t o = *dst ? strlen(*dst) : 0;
  char *p = (char *)realloc(*dst, o + n + 1);
  if (!p) return;
  memcpy(p + o, s, n); p[o + n] = 0;
  *dst = p;
}
static void read_odso(MergeInfo *mi, Xml *x)
{
  int depth = x->depth, nmap = 0;
  if (x->pending_end) return;
  while (xml_next(x) != XT_EOF) {
    const char *st;
    size_t n;
    if (x->type == XT_END && x->depth < depth) break;
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "table")) sval(x, mi->table, sizeof mi->table);
    else if (!strcmp(x->name, "src")) { const char *r = xml_attr(x, "id"); snprintf(rid_src, sizeof rid_src, "%s", r ? r : ""); }
    else if (!strcmp(x->name, "colDelim")) { char v[8]; sval(x, v, sizeof v); mi->coldelim = (u8)atoi(v); }
    else if (!strcmp(x->name, "fHdr")) mi->hdr = (u8)on(x);
    else if (!strcmp(x->name, "fieldMapData")) {
      /* the n'th is the n'th of Word's address fields */
      int d2 = x->depth, isdb = 0;
      char name[64] = "";
      if (!x->pending_end)
        while (xml_next(x) != XT_EOF) {
          if (x->type == XT_END && x->depth < d2) break;
          if (x->type != XT_START) continue;
          if (!strcmp(x->name, "type")) { char v[24]; sval(x, v, sizeof v); isdb = !strcmp(v, "dbColumn"); }
          else if (!strcmp(x->name, "name")) sval(x, name, sizeof name);
        }
      if (nmap < AF_N && isdb) snprintf(mi->map[nmap], sizeof mi->map[nmap], "%s", name);
      nmap++;
    } else if (!strcmp(x->name, "recipientData")) xml_skip(x);
    else if ((n = xml_raw(x, &st))) append(&mi->odsox, st, n);
  }
}
void merge_read_setting(Doc *d, Xml *x)
{
  MergeInfo *mi;
  int depth;
  if (strcmp(x->name, "mailMerge") || !(mi = merge_info(d))) return;
  rid_ds[0] = rid_src[0] = 0;
  depth = x->depth;
  mi->type = MT_LETTERS;
  if (x->pending_end) return;
  while (xml_next(x) != XT_EOF) {
    const char *st;
    size_t n;
    if (x->type == XT_END && x->depth < depth) break;
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "mainDocumentType")) {
      char v[24]; int i;
      sval(x, v, sizeof v);
      for (i = 1; i < NTYPES; i++) if (!strcmp(v, types[i])) mi->type = (u8)i;
    } else if (!strcmp(x->name, "linkToQuery")) mi->linkquery = (u8)on(x);
    else if (!strcmp(x->name, "dataType")) sval(x, mi->dtype, sizeof mi->dtype);
    else if (!strcmp(x->name, "connectString")) sval(x, mi->connect, sizeof mi->connect);
    else if (!strcmp(x->name, "query")) sval(x, mi->query, sizeof mi->query);
    else if (!strcmp(x->name, "dataSource")) { const char *r = xml_attr(x, "id"); snprintf(rid_ds, sizeof rid_ds, "%s", r ? r : ""); }
    else if (!strcmp(x->name, "viewMergedData")) mi->viewmerged = (u8)on(x);
    else if (!strcmp(x->name, "activeRecord")) { char v[16]; sval(x, v, sizeof v); mi->active = atol(v); }
    else if (!strcmp(x->name, "checkErrors")) { char v[16]; sval(x, v, sizeof v); mi->checkerrors = (u8)atoi(v); }
    else if (!strcmp(x->name, "odso")) read_odso(mi, x);
    else if (!strcmp(x->name, "headerSource")) xml_skip(x);
    else if ((n = xml_raw(x, &st))) append(&mi->extra, st, n);
  }
  merge_parse_query(mi);
  if (mi->active > 0) mi->rec = (int)mi->active - 1;
}
/* a relationship's target by id in a .rels part */
static int rel_target(const char *rels, size_t n, const char *id, char *out, size_t cap)
{
  Xml x;
  int got = 0;
  xml_init(&x, rels, n);
  while (!got && xml_next(&x) != XT_EOF)
    if (x.type == XT_START && !strcmp(x.name, "Relationship")) {
      const char *i = xml_attr(&x, "Id"), *t = xml_attr(&x, "Target");
      if (i && t && !strcmp(i, id)) {
        if (!strncmp(t, "file:///", 8)) t += 8;
        else if (!strncmp(t, "file://", 7)) t += 7;
        snprintf(out, cap, "%s", t);
        got = 1;
      }
    }
  xml_free(&x);
  return got;
}
void merge_read_parts(Doc *d, ZipReader *z)
{
  MergeInfo *mi = d->merge;
  int i;
  size_t n;
  char *rels;
  if (!mi || (!rid_ds[0] && !rid_src[0])) return;
  if ((i = zip_find(z, "word/_rels/settings.xml.rels")) < 0 || !(rels = (char *)zip_read(z, i, &n))) return;
  if (!(rid_ds[0] && rel_target(rels, n, rid_ds, mi->src, sizeof mi->src)) && rid_src[0]) rel_target(rels, n, rid_src, mi->src, sizeof mi->src);
  /* "%20" and the like in a file URL */
  {
    char *p = mi->src, *q = mi->src;
    for (; *p; p++) {
      if (*p == '%' && p[1] && p[2]) { char h[3]; h[0] = p[1]; h[1] = p[2]; h[2] = 0; *q++ = (char)strtol(h, NULL, 16); p += 2; }
      else *q++ = *p;
    }
    *q = 0;
  }
  free(rels);
  rid_ds[0] = rid_src[0] = 0;
}
static void attr(Buf *b, const char *el, const char *v)
{
  buf_adds(b, "<w:"); buf_adds(b, el); buf_adds(b, " w:val=\""); xml_escape(b, v, strlen(v)); buf_adds(b, "\"/>");
}
/* the data source's kind from its name */
static int src_kind(const char *src)
{
  const char *dot = strrchr(src, '.');
  if (dot && str_ieq(dot, ".xlsx")) return 1;
  if (dot && (str_ieq(dot, ".docx") || str_ieq(dot, ".doc"))) return 2;
  return 0;
}
void merge_write_settings(Doc *d, Buf *b, int where)
{
  MergeInfo *mi = d->merge;
  int k, af;
  char num[16];
  if (where != 1 || !mi || mi->type == MT_NONE || mi->type >= NTYPES) return;
  k = src_kind(mi->src);
  buf_adds(b, "<w:mailMerge xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">");
  attr(b, "mainDocumentType", types[mi->type]);
  if (mi->linkquery) buf_adds(b, "<w:linkToQuery/>");
  attr(b, "dataType", mi->dtype[0] ? mi->dtype : k == 1 ? "spreadsheet" : "textFile");
  if (mi->src[0]) {
    if (mi->connect[0]) attr(b, "connectString", mi->connect);
    else if (k == 1) {
      char cs[600];
      snprintf(cs, sizeof cs, "Provider=Microsoft.ACE.OLEDB.12.0;User ID=Admin;Data Source=%s;Mode=Read;Extended Properties=\"HDR=YES;IMEX=1;\";", mi->src);
      attr(b, "connectString", cs);
    } else attr(b, "connectString", "");
    if (mi->data) merge_make_query(mi);
    if (mi->query[0]) attr(b, "query", mi->query);
    buf_adds(b, "<w:dataSource r:id=\"rIdMM1\"/>");
  }
  if (mi->extra) buf_adds(b, mi->extra);
  if (mi->viewmerged) buf_adds(b, "<w:viewMergedData/>");
  if (mi->viewmerged || mi->rec) { sprintf(num, "%d", mi->rec + 1); attr(b, "activeRecord", num); }
  if (mi->checkerrors) { sprintf(num, "%d", mi->checkerrors); attr(b, "checkErrors", num); }
  if (mi->src[0]) {
    buf_adds(b, "<w:odso>");
    if (mi->table[0]) attr(b, "table", mi->table);
    buf_adds(b, "<w:src r:id=\"rIdMM2\"/>");
    if (k == 0) { sprintf(num, "%d", mi->coldelim ? mi->coldelim : 44); attr(b, "colDelim", num); }
    attr(b, "type", k == 1 ? "database" : k == 2 ? "document1" : "text");
    if (mi->hdr) buf_adds(b, "<w:fHdr/>");
    for (af = 0; af < AF_N; af++) {
      int c = mi->data && mi->map[af][0] ? md_col(mi->data, mi->map[af]) : -1;
      buf_adds(b, "<w:fieldMapData>");
      if (mi->map[af][0] && (c >= 0 || !mi->data)) {
        attr(b, "type", "dbColumn");
        attr(b, "name", mi->map[af]);
        attr(b, "mappedName", af_names[af]);
        if (c >= 0) { sprintf(num, "%d", c); attr(b, "column", num); }
      }
      attr(b, "lid", "en-US");
      buf_adds(b, "</w:fieldMapData>");
    }
    if (mi->odsox) buf_adds(b, mi->odsox);
    buf_adds(b, "</w:odso>");
  }
  buf_adds(b, "</w:mailMerge>");
}
void merge_write_parts(Doc *d, DocxOut *o)
{
  MergeInfo *mi = d->merge;
  Buf b;
  int r;
  if (!mi || mi->type == MT_NONE || !mi->src[0]) return;
  buf_init(&b);
  buf_adds(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
  for (r = 1; r <= 2; r++) {
    buf_printf(&b, "<Relationship Id=\"rIdMM%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/mailMergeSource\" Target=\"", r);
    if (mi->src[0] && strchr(mi->src, ':') && mi->src[1] == ':') buf_adds(&b, "file:///");    /* (a Windows path, as Word writes it) */
    xml_escape(&b, mi->src, strlen(mi->src));
    buf_adds(&b, "\" TargetMode=\"External\"/>");
  }
  buf_adds(&b, "</Relationships>");
  if (!b.oom) docx_out_part(o, "word/_rels/settings.xml.rels", NULL, NULL, NULL, NULL, b.data, b.len);
  buf_free(&b);
}
