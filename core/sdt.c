/*
 * sdt.c - content controls and legacy form fields (WP16): see sdt.h. Records, w:sdtPr read and
 * written, finding controls in the text, the edits (insert, remove, set text, toggle, choose, date,
 * properties, picture, repeating section items, group), the checks the edit paths make (locks,
 * filling in forms) and the legacy form fields (FORMTEXT, FORMCHECKBOX, FORMDROPDOWN). Written from
 * ECMA-376 Part 1 (17.5.2) and Word's documented behaviour.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protect.h"
#include "sdt.h"
#include "field.h"
#include "xml.h"
#include "table.h"

int sdt_bypass;

/* ------------------------------------------------------------------ */
/* the marks and the records */
void smark_utf8(char *out, int k, int end) { out[utf8_put(out, (end ? 0x10B000u : 0x107000u) + (u32)(k & 0xFFF))] = 0; }
int smark_index(const char *t) { int used; return (int)(utf8_get(t, 4, &used) & 0xFFF); }

SdtSet *sdt_set(Doc *d)
{
  if (!d->sdt) d->sdt = (SdtSet *)calloc(1, sizeof(SdtSet));
  return d->sdt;
}
void sdt_rec_free(Sdt *s)
{
  int i;
  free(s->alias); free(s->tag); free(s->plcpart); free(s->plctext); free(s->rpr); free(s->binding);
  free(s->other); free(s->endpr); free(s->nsdecl); free(s->lastvalue);
  for (i = 0; i < s->nitems; i++) { free(s->items[i].text); free(s->items[i].value); }
  free(s->items);
  free(s->datefmt); free(s->lid); free(s->storeas); free(s->calendar); free(s->fulldate);
  free(s->gallery); free(s->category); free(s->sectitle);
  memset(s, 0, sizeof *s);
}
void sdt_free_all(Doc *d)
{
  int i;
  if (!d->sdt) return;
  for (i = 0; i < d->sdt->n; i++) sdt_rec_free(&d->sdt->r[i]);
  free(d->sdt->r); free(d->sdt->rootns);
  free(d->sdt); d->sdt = NULL;
}
Sdt *sdt_get(const Doc *d, int k) { return d->sdt && k >= 0 && k < d->sdt->n ? &d->sdt->r[k] : NULL; }
int sdt_count(const Doc *d) { return d->sdt ? d->sdt->n : 0; }
static int add_rec(Doc *d)
{
  SdtSet *s = sdt_set(d);
  if (!s || s->n >= SDT_MAX) return -1;
  if (!(s->n & 15)) {
    Sdt *nr = (Sdt *)realloc(s->r, (size_t)(s->n + 16) * sizeof(Sdt));
    if (!nr) return -1;
    s->r = nr;
  }
  memset(&s->r[s->n], 0, sizeof(Sdt));
  return s->n++;
}
static char *dup0(const char *s) { return s ? str_dup(s) : NULL; }
static void set_str(char **dst, const char *s) { free(*dst); *dst = s ? str_dup(s) : NULL; }
/* a new id like Word's (a signed 32-bit number, different each time) */
static i32 new_id(void)
{
  static u32 seed = 0x2C3A5E1Bu;
  seed = seed * 1103515245u + 12345u;
  return (i32)(seed & 0x7FFFFFFF) - 0x40000000;
}
static void copy_rec(Sdt *t, const Sdt *s)
{
  int i;
  *t = *s;
  t->alias = dup0(s->alias); t->tag = dup0(s->tag); t->plcpart = dup0(s->plcpart); t->plctext = dup0(s->plctext);
  t->rpr = dup0(s->rpr); t->binding = dup0(s->binding); t->other = dup0(s->other); t->endpr = dup0(s->endpr);
  t->nsdecl = dup0(s->nsdecl); t->lastvalue = dup0(s->lastvalue);
  t->items = NULL; t->nitems = 0;
  if (s->nitems && (t->items = (SdtItem *)calloc((size_t)s->nitems, sizeof(SdtItem)))) {
    for (i = 0; i < s->nitems; i++) { t->items[i].text = dup0(s->items[i].text); t->items[i].value = dup0(s->items[i].value); }
    t->nitems = s->nitems;
  }
  t->datefmt = dup0(s->datefmt); t->lid = dup0(s->lid); t->storeas = dup0(s->storeas); t->calendar = dup0(s->calendar);
  t->fulldate = dup0(s->fulldate); t->gallery = dup0(s->gallery); t->category = dup0(s->category); t->sectitle = dup0(s->sectitle);
}
void sdt_copy(Sdt *dst, const Sdt *src) { copy_rec(dst, src); }
int sdt_clone(Doc *d, int k)
{
  int n;
  Sdt c;
  if (!sdt_get(d, k)) return -1;
  copy_rec(&c, &d->sdt->r[k]);
  if ((n = add_rec(d)) < 0) { sdt_rec_free(&c); return -1; }
  d->sdt->r[n] = c;
  return n;
}
static int add_item(Sdt *s, const char *text, const char *value)
{
  SdtItem *ni = (SdtItem *)realloc(s->items, (size_t)(s->nitems + 1) * sizeof(SdtItem));
  if (!ni) return 0;
  s->items = ni;
  ni[s->nitems].text = str_dup(text ? text : "");
  ni[s->nitems].value = str_dup(value ? value : "");
  s->nitems++;
  return 1;
}
static const char *const type_names[SDT_N] = { "Rich Text", "Plain Text", "Picture", "Check Box", "Combo Box", "Drop-Down List",
  "Date Picker", "Building Block Gallery", "Repeating Section", "Repeating Section Item", "Group", "Document Part",
  "Citation", "Bibliography", "Equation" };
const char *sdt_type_name(int type) { return type >= 0 && type < SDT_N ? type_names[type] : ""; }
int sdt_add(Doc *d) { return add_rec(d); }
int sdt_new(Doc *d, int type, int level)
{
  int k = add_rec(d);
  Sdt *s;
  if (k < 0) return -1;
  s = &d->sdt->r[k];
  s->type = (u8)type; s->level = (u8)level; s->id = new_id();
  switch (type) {
  case SDT_RICH: case SDT_TEXT: s->plctext = str_dup("Click or tap here to enter text."); s->plcpart = str_dup("DefaultPlaceholder_-1854013440"); break;
  case SDT_PICTURE: s->plctext = str_dup("Click or tap to insert a picture."); break;
  case SDT_CHECK: s->chk_on = 0x2612; s->chk_off = 0x2610; strcpy(s->chk_onfont, "MS Gothic"); strcpy(s->chk_offfont, "MS Gothic"); break;
  case SDT_COMBO: case SDT_DROP:
    s->plctext = str_dup("Choose an item."); s->plcpart = str_dup("DefaultPlaceholder_-1854013438");
    add_item(s, "Choose an item.", "");
    break;
  case SDT_DATE:
    s->plctext = str_dup("Click or tap to enter a date."); s->plcpart = str_dup("DefaultPlaceholder_-1854013437");
    s->datefmt = str_dup("dd/MM/yyyy"); s->lid = str_dup("en-GB"); s->storeas = str_dup("dateTime"); s->calendar = str_dup("gregorian");
    break;
  case SDT_GALLERY: s->plctext = str_dup("Choose a building block."); s->plcpart = str_dup("DefaultPlaceholder_-1854013436"); s->gallery = str_dup("Quick Parts"); break;
  case SDT_REPEAT: s->plcpart = str_dup("DefaultPlaceholder_-1854013435"); break;
  }
  return k;
}

/* ------------------------------------------------------------------ */
/* w:sdtPr read */
static int onoff_val(const char *v) { return !v || !strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "on"); }
static void addraw(char **dst, const char *s, size_t n)
{
  size_t l = *dst ? strlen(*dst) : 0;
  char *nd = (char *)realloc(*dst, l + n + 1);
  if (!nd) return;
  memcpy(nd + l, s, n); nd[l + n] = 0;
  *dst = nd;
}
/* the first START named name in xml[0..n) (at any depth): its attribute attr into out; 1 if found */
static int find_attr(const char *xml, size_t n, const char *name, const char *attr, char *out, size_t cap)
{
  Xml x;
  int r = 0;
  if (cap) out[0] = 0;
  xml_init(&x, xml, n);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START && !strcmp(x.name, name)) {
      const char *v = xml_attr(&x, attr);
      if (v) snprintf(out, cap, "%s", v);
      r = 1;
      break;
    }
  }
  xml_free(&x);
  return r;
}
/* the prefix an element was written with ("w15" for "<w15:dataBinding"), into out */
static void prefix_of(const char *raw, char *out, size_t cap)
{
  const char *c = raw + 1, *e = raw + 1;
  while (*e && *e != ':' && *e != ' ' && *e != '>' && *e != '/') e++;
  if (*e == ':' && (size_t)(e - c) < cap) { memcpy(out, c, (size_t)(e - c)); out[e - c] = 0; }
  else if (cap) out[0] = 0;
}
static void parse_list(Sdt *s, const char *raw, size_t n)
{
  Xml x;
  xml_init(&x, raw, n);
  while (xml_next(&x) != XT_EOF) {
    if (x.type != XT_START) continue;
    if (!strcmp(x.name, "comboBox") || !strcmp(x.name, "dropDownList")) { const char *v = xml_attr(&x, "lastValue"); if (v) set_str(&s->lastvalue, v); }
    else if (!strcmp(x.name, "listItem")) {
      const char *t = xml_attr(&x, "displayText"), *v = xml_attr(&x, "value");
      add_item(s, t ? t : v, v ? v : t);
    }
  }
  xml_free(&x);
}
static void parse_date(Sdt *s, const char *raw, size_t n)
{
  Xml x;
  xml_init(&x, raw, n);
  while (xml_next(&x) != XT_EOF) {
    const char *v;
    if (x.type != XT_START) continue;
    v = xml_attr(&x, "val");
    if (!strcmp(x.name, "date")) { const char *f = xml_attr(&x, "fullDate"); if (f) set_str(&s->fulldate, f); }
    else if (!strcmp(x.name, "dateFormat")) set_str(&s->datefmt, v);
    else if (!strcmp(x.name, "lid")) set_str(&s->lid, v);
    else if (!strcmp(x.name, "storeMappedDataAs")) set_str(&s->storeas, v);
    else if (!strcmp(x.name, "calendar")) set_str(&s->calendar, v);
  }
  xml_free(&x);
}
static void parse_docpart(Sdt *s, const char *raw, size_t n)
{
  Xml x;
  xml_init(&x, raw, n);
  while (xml_next(&x) != XT_EOF) {
    const char *v;
    if (x.type != XT_START) continue;
    v = xml_attr(&x, "val");
    if (!strcmp(x.name, "docPartGallery")) set_str(&s->gallery, v);
    else if (!strcmp(x.name, "docPartCategory")) set_str(&s->category, v);
    else if (!strcmp(x.name, "docPartUnique")) s->dpunique = (u8)onoff_val(v);
  }
  xml_free(&x);
}
static void parse_check(Sdt *s, const char *raw, size_t n)
{
  Xml x;
  xml_init(&x, raw, n);
  while (xml_next(&x) != XT_EOF) {
    const char *v, *f;
    if (x.type != XT_START) continue;
    v = xml_attr(&x, "val"); f = xml_attr(&x, "font");
    if (!strcmp(x.name, "checked")) s->checked0 = (u8)onoff_val(v);
    else if (!strcmp(x.name, "checkedState")) { if (v) s->chk_on = (u32)strtoul(v, NULL, 16); if (f) snprintf(s->chk_onfont, sizeof s->chk_onfont, "%s", f); }
    else if (!strcmp(x.name, "uncheckedState")) { if (v) s->chk_off = (u32)strtoul(v, NULL, 16); if (f) snprintf(s->chk_offfont, sizeof s->chk_offfont, "%s", f); }
  }
  xml_free(&x);
}
static void parse_repeat(Sdt *s, const char *raw, size_t n)
{
  Xml x;
  xml_init(&x, raw, n);
  while (xml_next(&x) != XT_EOF) {
    const char *v;
    if (x.type != XT_START) continue;
    v = xml_attr(&x, "val");
    if (!strcmp(x.name, "sectionTitle")) set_str(&s->sectitle, v);
    else if (!strcmp(x.name, "doNotAllowInsertDeleteSection")) s->nodelitem = (u8)onoff_val(v);
  }
  xml_free(&x);
}
void sdt_parse_pr(Sdt *s, const char *xml, size_t n)
{
  Xml x;
  int type = -1;
  xml_init(&x, xml, n);
  if (xml_next(&x) != XT_START || x.pending_end) { xml_free(&x); s->type = SDT_RICH; return; }   /* <w:sdtPr> (or empty) */
  while (xml_next(&x) != XT_EOF) {
    const char *raw, *v;
    char val[256] = "", pre[16];
    char name[64];
    size_t rn;
    if (x.type == XT_END && x.depth < 1) break;
    if (x.type != XT_START) continue;
    snprintf(name, sizeof name, "%s", x.name);
    v = xml_attr(&x, "val");
    if (v) snprintf(val, sizeof val, "%s", v);
    if (!strcmp(name, "alias")) { set_str(&s->alias, v); xml_skip(&x); continue; }
    if (!strcmp(name, "tag")) { set_str(&s->tag, v); xml_skip(&x); continue; }
    if (!strcmp(name, "id")) { s->id = v ? (i32)strtol(v, NULL, 10) : 0; xml_skip(&x); continue; }
    if (!strcmp(name, "lock")) {
      s->lock = (u8)(!strcmp(val, "sdtLocked") ? SLK_DEL : !strcmp(val, "contentLocked") ? SLK_EDIT : !strcmp(val, "sdtContentLocked") ? SLK_DEL | SLK_EDIT : 0);
      xml_skip(&x); continue;
    }
    if (!strcmp(name, "temporary")) { s->temporary = (u8)onoff_val(v); xml_skip(&x); continue; }
    if (!strcmp(name, "showingPlcHdr")) { s->showplc = (u8)onoff_val(v); xml_skip(&x); continue; }
    if (!strcmp(name, "appearance")) {
      s->hasapp = 1;
      s->appearance = (u8)(!strcmp(val, "tags") ? SAP_TAGS : !strcmp(val, "hidden") ? SAP_HIDDEN : SAP_BOX);
      xml_skip(&x); continue;
    }
    if (!strcmp(name, "color") && v && strlen(v) == 6) { s->hascolor = 1; s->color = (u32)strtoul(v, NULL, 16); xml_skip(&x); continue; }
    if (!strcmp(name, "text")) { const char *m = xml_attr(&x, "multiLine"); type = SDT_TEXT; s->multiline = (u8)(m && onoff_val(m)); xml_skip(&x); continue; }
    if (!strcmp(name, "richText")) { type = SDT_RICH; s->richtag = 1; xml_skip(&x); continue; }
    if (!strcmp(name, "picture")) { type = SDT_PICTURE; xml_skip(&x); continue; }
    if (!strcmp(name, "group")) { type = SDT_GROUP; xml_skip(&x); continue; }
    if (!strcmp(name, "citation")) { type = SDT_CITATION; xml_skip(&x); continue; }
    if (!strcmp(name, "bibliography")) { type = SDT_BIBLIO; xml_skip(&x); continue; }
    if (!strcmp(name, "equation")) { type = SDT_EQUATION; xml_skip(&x); continue; }
    if (!strcmp(name, "repeatingSectionItem")) { type = SDT_REPITEM; xml_skip(&x); continue; }
    rn = xml_raw(&x, &raw);
    prefix_of(raw, pre, sizeof pre);
    if (!strcmp(name, "rPr")) { free(s->rpr); s->rpr = str_ndup(raw, rn); }
    else if (!strcmp(name, "dataBinding") && strcmp(pre, "w15")) { free(s->binding); s->binding = str_ndup(raw, rn); }
    else if (!strcmp(name, "placeholder")) { char p[256]; if (find_attr(raw, rn, "docPart", "val", p, sizeof p)) set_str(&s->plcpart, p); }
    else if (!strcmp(name, "comboBox") || !strcmp(name, "dropDownList")) { type = name[0] == 'c' ? SDT_COMBO : SDT_DROP; parse_list(s, raw, rn); }
    else if (!strcmp(name, "date")) { type = SDT_DATE; parse_date(s, raw, rn); }
    else if (!strcmp(name, "docPartObj") || !strcmp(name, "docPartList")) {
      s->dplist = (u8)(name[7] == 'L');
      parse_docpart(s, raw, rn);
      type = s->dplist || (s->gallery && !strcmp(s->gallery, "Quick Parts")) ? SDT_GALLERY : SDT_DOCPART;
    }
    else if (!strcmp(name, "checkbox") && !strcmp(pre, "w14")) { type = SDT_CHECK; parse_check(s, raw, rn); }
    else if (!strcmp(name, "repeatingSection")) { type = SDT_REPEAT; parse_repeat(s, raw, rn); }
    else addraw(&s->other, raw, rn);
  }
  xml_free(&x);
  s->type = (u8)(type < 0 ? SDT_RICH : type);
}
int sdt_showing_plc(const Sdt *s, const char *content)
{
  return s->showplc && s->plctext && !strcmp(content, s->plctext);
}

/* ------------------------------------------------------------------ */
/* w:sdtPr written */
static void val_el(Buf *o, const char *el, const char *v)
{
  buf_printf(o, "<%s w:val=\"", el);
  xml_escape(o, v, strlen(v));
  buf_adds(o, "\"/>");
}
/* whether the content shows the check box's checked symbol (its first character), or as read */
static int content_checked(const Sdt *s, const char *content)
{
  int used;
  u32 c;
  while (*content == ' ') content++;
  if (!*content) return s->checked0;
  c = utf8_get(content, strlen(content), &used);
  if (c == s->chk_on) return 1;
  if (c == s->chk_off) return 0;
  return s->checked0;
}
void sdt_write_pr(Buf *o, const Doc *d, int k, const char *content, i32 id)
{
  const Sdt *s = sdt_get(d, k);
  int i, plc;
  if (!s) return;
  plc = sdt_showing_plc(s, content);
  buf_adds(o, "<w:sdtPr>");
  if (s->rpr) buf_adds(o, s->rpr);
  if (s->alias) val_el(o, "w:alias", s->alias);
  if (s->tag) val_el(o, "w:tag", s->tag);
  buf_printf(o, "<w:id w:val=\"%ld\"/>", (long)id);
  if (s->hascolor) buf_printf(o, "<w15:color w:val=\"%06lX\"/>", (unsigned long)(s->color & 0xFFFFFF));
  if (s->hasapp) buf_printf(o, "<w15:appearance w15:val=\"%s\"/>", s->appearance == SAP_TAGS ? "tags" : s->appearance == SAP_HIDDEN ? "hidden" : "boundingBox");
  if (s->lock) buf_printf(o, "<w:lock w:val=\"%s\"/>", s->lock == SLK_DEL ? "sdtLocked" : s->lock == SLK_EDIT ? "contentLocked" : "sdtContentLocked");
  if (s->plcpart) { buf_adds(o, "<w:placeholder>"); val_el(o, "w:docPart", s->plcpart); buf_adds(o, "</w:placeholder>"); }
  if (s->temporary) buf_adds(o, "<w:temporary/>");
  if (plc) buf_adds(o, "<w:showingPlcHdr/>");
  if (s->binding) buf_adds(o, s->binding);
  if (s->other) buf_adds(o, s->other);
  switch (s->type) {
  case SDT_TEXT: buf_adds(o, s->multiline ? "<w:text w:multiLine=\"1\"/>" : "<w:text/>"); break;
  case SDT_RICH: if (s->richtag) buf_adds(o, "<w:richText/>"); break;
  case SDT_PICTURE: buf_adds(o, "<w:picture/>"); break;
  case SDT_GROUP: buf_adds(o, "<w:group/>"); break;
  case SDT_CITATION: buf_adds(o, "<w:citation/>"); break;
  case SDT_BIBLIO: buf_adds(o, "<w:bibliography/>"); break;
  case SDT_EQUATION: buf_adds(o, "<w:equation/>"); break;
  case SDT_REPITEM: buf_adds(o, "<w15:repeatingSectionItem/>"); break;
  case SDT_COMBO: case SDT_DROP: {
    const char *el = s->type == SDT_COMBO ? "w:comboBox" : "w:dropDownList";
    const char *last = NULL;
    for (i = 0; i < s->nitems && !plc; i++) if (!strcmp(s->items[i].text, content)) { last = s->items[i].value; break; }
    if (!last && s->lastvalue && !plc && s->type == SDT_COMBO && content[0]) last = content;   /* a typed value */
    buf_printf(o, "<%s", el);
    if (last && last[0]) { buf_adds(o, " w:lastValue=\""); xml_escape(o, last, strlen(last)); buf_adds(o, "\""); }
    buf_adds(o, ">");
    for (i = 0; i < s->nitems; i++) {
      buf_adds(o, "<w:listItem w:displayText=\""); xml_escape(o, s->items[i].text, strlen(s->items[i].text));
      buf_adds(o, "\" w:value=\""); xml_escape(o, s->items[i].value, strlen(s->items[i].value)); buf_adds(o, "\"/>");
    }
    buf_printf(o, "</%s>", el);
    break; }
  case SDT_DATE:
    buf_adds(o, "<w:date");
    if (s->fulldate && !plc) { buf_adds(o, " w:fullDate=\""); xml_escape(o, s->fulldate, strlen(s->fulldate)); buf_adds(o, "\""); }
    buf_adds(o, ">");
    if (s->datefmt) val_el(o, "w:dateFormat", s->datefmt);
    if (s->lid) val_el(o, "w:lid", s->lid);
    if (s->storeas) val_el(o, "w:storeMappedDataAs", s->storeas);
    if (s->calendar) val_el(o, "w:calendar", s->calendar);
    buf_adds(o, "</w:date>");
    break;
  case SDT_GALLERY: case SDT_DOCPART:
    buf_adds(o, s->dplist ? "<w:docPartList>" : "<w:docPartObj>");
    if (s->gallery) val_el(o, "w:docPartGallery", s->gallery);
    if (s->category) val_el(o, "w:docPartCategory", s->category);
    if (s->dpunique) buf_adds(o, "<w:docPartUnique/>");
    buf_adds(o, s->dplist ? "</w:docPartList>" : "</w:docPartObj>");
    break;
  case SDT_CHECK:
    buf_printf(o, "<w14:checkbox><w14:checked w14:val=\"%d\"/>", content_checked(s, content));
    buf_printf(o, "<w14:checkedState w14:val=\"%04lX\" w14:font=\"", (unsigned long)s->chk_on);
    xml_escape(o, s->chk_onfont, strlen(s->chk_onfont));
    buf_printf(o, "\"/><w14:uncheckedState w14:val=\"%04lX\" w14:font=\"", (unsigned long)s->chk_off);
    xml_escape(o, s->chk_offfont, strlen(s->chk_offfont));
    buf_adds(o, "\"/></w14:checkbox>");
    break;
  case SDT_REPEAT:
    buf_adds(o, "<w15:repeatingSection>");
    if (s->sectitle) val_el(o, "w15:sectionTitle", s->sectitle);
    if (s->nodelitem) buf_adds(o, "<w15:doNotAllowInsertDeleteSection w:val=\"1\"/>");
    buf_adds(o, "</w15:repeatingSection>");
    break;
  }
  buf_adds(o, "</w:sdtPr>");
}

/* ------------------------------------------------------------------ */
/* where controls are */
static int is_smark(const Para *q, u32 o) { return o + 4 <= q->len && (u8)q->t[o] == 0xF4 && ((u8)q->t[o + 1] == 0x87 || (u8)q->t[o + 1] == 0x8B); }
int sdt_end(Doc *d, Pos s, Pos *e)
{
  u32 pi, o;
  int k, depth = 0;
  if (s.para >= d->np || !is_smark(d->p[s.para], s.off) || !IS_SSTART(d->p[s.para]->t + s.off)) return 0;
  k = smark_index(d->p[s.para]->t + s.off);
  for (pi = s.para; pi < d->np; pi++) {
    const Para *q = d->p[pi];
    for (o = pi == s.para ? s.off : 0; o + 4 <= q->len; o++) {
      if (!is_smark(q, o) || smark_index(q->t + o) != k) continue;
      if (IS_SSTART(q->t + o)) depth++;
      else if (--depth == 0) { if (e) { e->para = pi; e->off = o; } return 1; }
      o += 3;
    }
  }
  return 0;
}
int sdt_at_depth(Doc *d, Pos p, int outer, Pos *s, Pos *e)
{
  long pi;
  int depth = 0;
  if (!sdt_count(d) || p.para >= d->np) return -1;
  for (pi = (long)p.para; pi >= 0; pi--) {
    const Para *q = d->p[pi];
    long o = pi == (long)p.para ? (long)(p.off > q->len ? q->len : p.off) - 4 : (long)q->len - 4;
    for (; o >= 0; o--) {
      Pos st, en;
      if (!is_smark(q, (u32)o)) continue;
      if (IS_SEND(q->t + o)) { depth++; continue; }
      if (depth > 0) { depth--; continue; }
      st.para = (u32)pi; st.off = (u32)o;
      if (!sdt_end(d, st, &en)) continue;                 /* an orphan: not a control */
      if (en.para < p.para || (en.para == p.para && en.off < p.off)) continue;
      if (outer-- > 0) continue;
      if (s) *s = st;
      if (e) *e = en;
      return smark_index(q->t + o);
    }
  }
  return -1;
}
int sdt_at(Doc *d, Pos p, Pos *s, Pos *e) { return sdt_at_depth(d, p, 0, s, e); }
int sdt_next(Doc *d, Pos p, int dir, Pos *s, Pos *e)
{
  long pi;
  if (!sdt_count(d)) return -1;
  if (dir > 0) {
    for (pi = (long)p.para; pi < (long)d->np; pi++) {
      const Para *q = d->p[pi];
      u32 o;
      for (o = pi == (long)p.para ? p.off : 0; o + 4 <= q->len; o++) {
        Pos st;
        if (!is_smark(q, o) || !IS_SSTART(q->t + o)) continue;
        st.para = (u32)pi; st.off = o;
        if (pi == (long)p.para && o == p.off) continue;
        if (sdt_end(d, st, e)) { if (s) *s = st; return smark_index(q->t + o); }
      }
    }
  } else {
    for (pi = (long)p.para; pi >= 0; pi--) {
      const Para *q = d->p[pi];
      long o = pi == (long)p.para ? (long)p.off - 1 : (long)q->len - 4;
      for (; o >= 0; o--) {
        Pos st;
        if (!is_smark(q, (u32)o) || !IS_SSTART(q->t + o)) continue;
        st.para = (u32)pi; st.off = (u32)o;
        if (sdt_end(d, st, e)) { if (s) *s = st; return smark_index(q->t + o); }
      }
    }
  }
  return -1;
}
int sdt_all(Doc *d, Pos *at, int max)
{
  u32 pi, o;
  int n = 0;
  if (!sdt_count(d)) return 0;
  for (pi = 0; pi < d->np; pi++) {
    const Para *q = d->p[pi];
    for (o = 0; o + 4 <= q->len; o++) {
      if (!is_smark(q, o)) continue;
      if (IS_SSTART(q->t + o)) { if (n < max) { at[n].para = pi; at[n].off = o; } n++; }
      o += 3;
    }
  }
  return n;
}
void sdt_text(Doc *d, Pos s, Pos e, char *out, size_t cap) { sdt_text_paras(d->p, d->np, s, e, out, cap); }
void sdt_text_paras(Para *const *pp, u32 np, Pos s, Pos e, char *out, size_t cap)
{
  size_t n = 0;
  u32 pi;
  if (!cap) return;
  for (pi = s.para; pi <= e.para && pi < np; pi++) {
    const Para *q = pp[pi];
    u32 o = pi == s.para ? s.off + 4 : 0, end = pi == e.para ? e.off : q->len;
    if (pi > s.para && n + 1 < cap) out[n++] = '\n';
    while (o < end && o < q->len) {
      if (o + 4 <= q->len && (IS_MARK(q->t + o) || IS_FMARK(q->t + o))) { o += 4; continue; }   /* comment, field and control marks */
      if (n + 1 < cap) out[n++] = q->t[o];
      o++;
    }
  }
  out[n] = 0;
}

/* ------------------------------------------------------------------ */
/* edits */
/* the Placeholder Text character style (grey), made if the document hasn't one; its index, 0 none */
static int plc_style(Doc *d)
{
  int k = doc_style(d, "PlaceholderText");
  Style s;
  if (k > 0) return k;
  memset(&s, 0, sizeof s);
  snprintf(s.id, sizeof s.id, "PlaceholderText");
  snprintf(s.name, sizeof s.name, "Placeholder Text");
  s.type = ST_CHAR; s.based = NO_STYLE; s.next = 0;
  s.ocf = d->styles[0].cf; s.ocf.color = 0x808080; s.cmask = CM_COLOR;
  k = doc_add_style(d, &s);
  if (k < 0) return 0;
  doc_resolve_styles(d);
  return k;
}
static void plc_fmt(Doc *d, CharFmt *cf)
{
  int ps = plc_style(d);
  if (ps > 0) { cf->cstyle = (u8)ps; cf->color = d->styles[ps].cf.color; }
}
static int ins(Doc *d, Pos *at, const char *s, u32 n, const CharFmt *f) { return doc_insert(d, at, s, n, f); }
static int ins_mark(Doc *d, Pos *at, int k, int end, const CharFmt *f)
{
  char m[8];
  smark_utf8(m, k, end);
  return ins(d, at, m, 4, f);
}
/* the marks of the control at s (end mark at e) made to name nk */
static int swap_marks(Doc *d, Pos s, Pos e, int nk)
{
  CharFmt f;
  char m[8];
  Pos x;
  f = *para_fmt_at(d->p[e.para], e.off);
  x = e; x.off += 4;
  if (!doc_delete(d, e, x)) return 0;
  smark_utf8(m, nk, 1);
  ins(d, &e, m, 4, &f);
  f = *para_fmt_at(d->p[s.para], s.off);
  x = s; x.off += 4;
  doc_delete(d, s, x);
  smark_utf8(m, nk, 0);
  return ins(d, &s, m, 4, &f);
}
/* the first and last paragraphs of the table rows a and b are in */
static void row_span(Doc *d, u32 a, u32 b, u32 *fp, u32 *lp)
{
  const Cell *ca = &d->p[a]->pf.cell, *cb = &d->p[b]->pf.cell;
  u32 i = a, j = b;
  while (i > 0 && d->p[i - 1]->pf.cell.table == ca->table && d->p[i - 1]->pf.cell.row == ca->row) i--;
  while (j + 1 < d->np && d->p[j + 1]->pf.cell.table == cb->table && d->p[j + 1]->pf.cell.row == cb->row) j++;
  *fp = i; *lp = j;
}
static u32 utf8_sym(char *out, u32 cp) { return (u32)utf8_put(out, cp); }
int sdt_insert(Doc *d, Pos a, Pos b, int type, int block)
{
  int k, ki = -1, level;
  Pos p;
  CharFmt cf;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  a = pos_clamp(d, a); b = pos_clamp(d, b);
  {
    int rows = type == SDT_REPEAT && in_table(&d->p[a.para]->pf) && d->p[a.para]->pf.cell.table == d->p[b.para]->pf.cell.table;
    int whole = a.off == 0 && b.off == d->p[b.para]->len && (a.para != b.para || d->p[a.para]->len > 0);
    level = rows ? SL_ROW : (type == SDT_REPEAT || ((whole || block) && type != SDT_CHECK && type != SDT_DATE && type != SDT_COMBO && type != SDT_DROP)) ? SL_BLOCK : SL_INLINE;
    if (level == SL_BLOCK && in_table(&d->p[a.para]->pf) && !same_cell(&d->p[a.para]->pf, &d->p[b.para]->pf)) level = SL_INLINE, b = a;
  }
  if ((k = sdt_new(d, type, level)) < 0) return -1;
  if (type == SDT_REPEAT && (ki = sdt_new(d, SDT_REPITEM, level)) < 0) return -1;
  {
    const ParaFmt *pf = &d->p[a.para]->pf;
    int nc = pf->outer.table ? 2 : pf->cell.table ? 1 : 0;
    d->sdt->r[k].depth = (u8)(level == SL_ROW && nc ? nc - 1 : nc);
    if (ki >= 0) d->sdt->r[ki].depth = d->sdt->r[k].depth;
  }
  sdt_bypass++;
  doc_begin(d, a);
  if (level == SL_INLINE) {
    Sdt *r = &d->sdt->r[k];
    cf = *para_fmt_before(d->p[a.para], a.off);
    cf.link = 0;
    if (!pos_cmp(a, b) || type == SDT_CHECK) {
      CharFmt tf = cf;
      p = a;
      ins_mark(d, &p, k, 0, &cf);
      if (type == SDT_CHECK) {
        char sym[8];
        u32 n = utf8_sym(sym, r->chk_off);
        tf.font = (u8)doc_font(d, r->chk_offfont);
        ins(d, &p, sym, n, &tf);
      } else if (r->plctext) {
        plc_fmt(d, &tf);
        ins(d, &p, r->plctext, (u32)strlen(r->plctext), &tf);
        d->sdt->r[k].showplc = 1;
      }
      ins_mark(d, &p, k, 1, &cf);
    } else {
      CharFmt ef = *para_fmt_before(d->p[b.para], b.off);
      p = b; ins_mark(d, &p, k, 1, &ef);
      p = a; ins_mark(d, &p, k, 0, &cf);
    }
  } else {
    u32 fp = a.para, lp = b.para;
    if (level == SL_ROW) row_span(d, a.para, b.para, &fp, &lp);
    p.para = lp; p.off = d->p[lp]->len;
    cf = *para_fmt_before(d->p[lp], p.off); cf.link = 0;
    if (ki >= 0) ins_mark(d, &p, ki, 1, &cf);
    ins_mark(d, &p, k, 1, &cf);
    p.para = fp; p.off = 0;
    cf = *para_fmt_at(d->p[fp], 0); cf.link = 0;
    ins_mark(d, &p, k, 0, &cf);
    if (ki >= 0) ins_mark(d, &p, ki, 0, &cf);
  }
  doc_end(d, a);
  sdt_bypass--;
  return k;
}
int sdt_remove(Doc *d, Pos s, int keep)
{
  Pos e, s4;
  int r;
  if (!sdt_end(d, s, &e)) return 0;
  sdt_bypass++;
  doc_begin(d, s);
  if (keep) {
    Pos e4 = e; e4.off += 4;
    s4 = s; s4.off += 4;
    r = doc_delete(d, e, e4) && doc_delete(d, s, s4);
  } else { e.off += 4; r = doc_delete(d, s, e); }
  doc_end(d, s);
  sdt_bypass--;
  return r;
}
/* the formatting new content of the control at s takes: its first character's, not the placeholder's */
static CharFmt content_fmt(Doc *d, Pos s, Pos e)
{
  CharFmt f = *para_fmt_at(d->p[s.para], s.off);
  int ps = doc_style(d, "PlaceholderText");
  if ((s.para != e.para || s.off + 4 < e.off) && s.off + 4 < d->p[s.para]->len) {
    const CharFmt *c = para_fmt_at(d->p[s.para], s.off + 4);
    if (ps <= 0 || c->cstyle != ps) f = *c;
  }
  f.link = 0;
  return f;
}
static int set_text_fmt(Doc *d, Pos s, Pos e, const char *text, const CharFmt *f)
{
  Pos a = s;
  a.off += 4;
  doc_begin(d, s);
  if (pos_cmp(a, e) < 0) doc_delete(d, a, e);
  if (text[0]) ins(d, &a, text, (u32)strlen(text), f);
  doc_end(d, s);
  return 1;
}
int sdt_set_text(Doc *d, Pos s, const char *text)
{
  Pos e;
  CharFmt f;
  Sdt *r;
  int ret;
  if (!sdt_end(d, s, &e) || !(r = sdt_get(d, smark_index(d->p[s.para]->t + s.off)))) return 0;
  f = content_fmt(d, s, e);
  if (r->plctext && r->showplc && !strcmp(text, r->plctext)) plc_fmt(d, &f);
  sdt_bypass++;
  ret = set_text_fmt(d, s, e, text, &f);
  sdt_bypass--;
  return ret;
}
int sdt_checked(Doc *d, Pos s)
{
  Pos e;
  Sdt *r;
  char t[16];
  if (!sdt_end(d, s, &e) || !(r = sdt_get(d, smark_index(d->p[s.para]->t + s.off))) || r->type != SDT_CHECK) return -1;
  sdt_text(d, s, e, t, sizeof t);
  return content_checked(r, t);
}
int sdt_toggle(Doc *d, Pos s)
{
  Pos e;
  Sdt *r;
  CharFmt f;
  char sym[8];
  int on = sdt_checked(d, s), ret;
  if (on < 0 || !sdt_end(d, s, &e)) return 0;
  r = sdt_get(d, smark_index(d->p[s.para]->t + s.off));
  f = content_fmt(d, s, e);
  f.font = (u8)doc_font(d, on ? r->chk_offfont : r->chk_onfont);
  sym[utf8_sym(sym, on ? r->chk_off : r->chk_on)] = 0;
  sdt_bypass++;
  ret = set_text_fmt(d, s, e, sym, &f);
  sdt_bypass--;
  return ret;
}
int sdt_choose(Doc *d, Pos s, int i)
{
  Sdt *r = sdt_get(d, s.para < d->np && is_smark(d->p[s.para], s.off) ? smark_index(d->p[s.para]->t + s.off) : -1);
  if (!r || i < 0 || i >= r->nitems) return 0;
  if (r->plctext && r->type == SDT_DROP && !r->items[i].value[0] && !strcmp(r->items[i].text, r->plctext)) {
    /* "Choose an item.": the placeholder again */
    Pos e; CharFmt f; int ret;
    if (!sdt_end(d, s, &e)) return 0;
    f = content_fmt(d, s, e); plc_fmt(d, &f);
    if (!r->showplc) { int nk = sdt_clone(d, smark_index(d->p[s.para]->t + s.off)); if (nk >= 0) { d->sdt->r[nk].showplc = 1; sdt_bypass++; doc_begin(d, s); swap_marks(d, s, e, nk); set_text_fmt(d, s, e, r->plctext, &f); doc_end(d, s); sdt_bypass--; return 1; } }
    sdt_bypass++; ret = set_text_fmt(d, s, e, r->plctext, &f); sdt_bypass--;
    return ret;
  }
  return sdt_set_text(d, s, r->items[i].text);
}
static int wday_of(int y, int m, int dd)
{
  static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
  if (m < 3) y--;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + dd) % 7;
}
int sdt_set_date(Doc *d, Pos s, int y, int m, int day)
{
  Pos e;
  Sdt *r;
  int k, nk;
  FieldTime t;
  char txt[128], fd[32];
  CharFmt f;
  if (!sdt_end(d, s, &e) || !(r = sdt_get(d, k = smark_index(d->p[s.para]->t + s.off))) || m < 1 || m > 12 || day < 1 || day > 31) return 0;
  memset(&t, 0, sizeof t);
  t.year = y; t.month = m; t.day = day; t.wday = wday_of(y, m, day);
  field_format_date(r->datefmt ? r->datefmt : "dd/MM/yyyy", &t, txt, sizeof txt);
  if ((nk = sdt_clone(d, k)) < 0) return 0;
  sprintf(fd, "%04d-%02d-%02dT00:00:00Z", y, m, day);
  set_str(&d->sdt->r[nk].fulldate, fd);
  f = content_fmt(d, s, e);
  sdt_bypass++;
  doc_begin(d, s);
  swap_marks(d, s, e, nk);
  set_text_fmt(d, s, e, txt, &f);
  doc_end(d, s);
  sdt_bypass--;
  return 1;
}
int sdt_set_props(Doc *d, Pos s, const Sdt *nr)
{
  Pos e;
  int nk, ret;
  Sdt c;
  if (!sdt_end(d, s, &e)) return 0;
  copy_rec(&c, nr);
  if ((nk = add_rec(d)) < 0) { sdt_rec_free(&c); return 0; }
  d->sdt->r[nk] = c;
  sdt_bypass++;
  doc_begin(d, s);
  ret = swap_marks(d, s, e, nk);
  doc_end(d, s);
  sdt_bypass--;
  return ret;
}
int sdt_set_picture(Doc *d, Pos s, int img)
{
  Pos e;
  char t[8];
  CharFmt f;
  int ret;
  if (!sdt_end(d, s, &e)) return 0;
  pic_utf8(t, img);
  f = content_fmt(d, s, e);
  sdt_bypass++;
  ret = set_text_fmt(d, s, e, t, &f);
  sdt_bypass--;
  return ret;
}
int sdt_group(Doc *d, Pos a, Pos b)
{
  if (!pos_cmp(a, b)) return -1;
  return sdt_insert(d, a, b, SDT_GROUP, 0);
}
int sdt_ungroup(Doc *d, Pos p)
{
  int depth;
  for (depth = 0; depth < 16; depth++) {
    Pos s, e;
    int k = sdt_at_depth(d, p, depth, &s, &e);
    if (k < 0) return 0;
    if (d->sdt->r[k].type == SDT_GROUP) return sdt_remove(d, s, 1);
  }
  return 0;
}

/* ---- repeating section items */
/* whether record k has both its marks in paragraphs fp..lp from (fp, foff) to (lp, loff) */
static int inner_rec(Doc *d, int k, Pos a, Pos b)
{
  u32 pi, o;
  int s = 0, e = 0;
  for (pi = a.para; pi <= b.para; pi++) {
    const Para *q = d->p[pi];
    for (o = pi == a.para ? a.off : 0; o + 4 <= (pi == b.para ? b.off : q->len); o++) {
      if (!is_smark(q, o)) continue;
      if (smark_index(q->t + o) == k) { if (IS_SSTART(q->t + o)) s = 1; else e = 1; }
      o += 3;
    }
  }
  return s && e;
}
/* paragraph src's text from off to end copied run by run to *at (moved past it), the control marks
   of records not inside a..b left out */
static void copy_text(Doc *d, u32 src, u32 off, u32 end, Pos *at, Pos a, Pos b)
{
  u32 r, start = 0;
  for (r = 0; r < d->p[src]->nr; r++) {
    u32 rs = start > off ? start : off, re = d->p[src]->r[r].end < end ? d->p[src]->r[r].end : end;
    start = d->p[src]->r[r].end;
    while (rs < re) {
      Para *q = d->p[src];
      CharFmt f = q->r[r].f;
      u32 j = rs;
      char buf[256];
      u32 n = 0;
      while (j < re && n + 4 < sizeof buf) {
        if (is_smark(q, j) && !inner_rec(d, smark_index(q->t + j), a, b)) { j += 4; break; }
        buf[n++] = q->t[j++];
      }
      /* (whole characters only) */
      while (n && j < re && ((u8)q->t[j] & 0xC0) == 0x80) { buf[n++] = q->t[j++]; }
      if (n) ins(d, at, buf, n, &f);
      rs = j;
    }
  }
}
static int parent_allows(Doc *d, Pos item)
{
  Pos in = item, ps, pe;
  int k;
  in.off += 4;
  k = sdt_at_depth(d, in, 1, &ps, &pe);
  return k < 0 || !(d->sdt->r[k].type == SDT_REPEAT && d->sdt->r[k].nodelitem);
}
static int rows_add(Doc *d, Pos item, Pos e, int before, Pos *out)
{
  u32 fp, lp, i, n0, ns, nnew, src0;
  int nrows = 1;
  Pos c, e4 = e;
  e4.off += 4;
  row_span(d, item.para, e.para, &fp, &lp);
  for (i = fp + 1; i <= lp; i++) if (d->p[i]->pf.cell.row != d->p[i - 1]->pf.cell.row) nrows++;
  n0 = d->np;
  c.para = before ? fp : lp; c.off = 0;
  for (i = 0; i < (u32)nrows; i++) if (!tbl_row_insert(d, &c, !before)) return 0;
  nnew = d->np - n0;                       /* the new rows' paragraphs (one a cell) */
  ns = before ? fp : lp + 1;
  src0 = before ? fp + nnew : fp;
  if (before) { item.para += nnew; e.para += nnew; e4.para += nnew; lp += nnew; }
  {
    /* each source cell's text into its new cell, from the last one back (the indexes stay right) */
    u32 cells[512], ncell = 0, k;
    for (i = src0; i <= lp && ncell < 512; i++) if (i == src0 || !same_cell(&d->p[i]->pf, &d->p[i - 1]->pf)) cells[ncell++] = i;
    if (ncell > nnew) ncell = nnew;
    for (k = ncell; k-- > 0;) {
      u32 cfirst = cells[k], clast = k + 1 < ncell ? cells[k + 1] - 1 : lp, pi;
      Pos at;
      at.para = ns + k; at.off = 0;
      for (pi = cfirst; pi <= clast; pi++) {
        u32 off = pi == item.para ? item.off : 0, end = pi == e.para ? e4.off : d->p[pi]->len;
        if (pi > cfirst) ins(d, &at, "\n", 1, NULL);
        copy_text(d, pi, off, end, &at, item, e4);
      }
    }
  }
  /* the section's own marks stay at its ends */
  if (before && item.off) {
    Para *q = d->p[item.para];
    char m[64];
    u32 n = item.off < sizeof m ? item.off : 0;
    CharFmt f = *para_fmt_at(q, 0);
    Pos a0, at;
    if (n) {
      memcpy(m, q->t, n);
      a0.para = item.para; a0.off = 0;
      doc_delete(d, a0, item);
      at.para = ns; at.off = 0;
      ins(d, &at, m, n, &f);
    }
  }
  if (!before) {
    /* marks after the item's end in its last cell (the section's end) go to the new last cell's end */
    Para *q = d->p[e.para];
    u32 n = q->len - e4.off, last = ns;
    while (last + 1 < d->np && d->p[last + 1]->pf.cell.table == d->p[ns]->pf.cell.table && d->p[last + 1]->pf.cell.row >= d->p[ns]->pf.cell.row &&
           d->p[last + 1]->pf.cell.row < d->p[ns]->pf.cell.row + nrows) last++;
    if (n && n < 64) {
      char m[64];
      CharFmt f = *para_fmt_at(q, e4.off);
      Pos b0, at;
      memcpy(m, q->t + e4.off, n);
      b0.para = e.para; b0.off = q->len;
      doc_delete(d, e4, b0);
      at.para = last; at.off = d->p[last]->len;
      ins(d, &at, m, n, &f);
    }
  }
  if (out) {
    /* the new item's start mark: the first control start in the new rows */
    Pos p; p.para = ns; p.off = 0;
    *out = p;
    if (!(is_smark(d->p[ns], 0) && IS_SSTART(d->p[ns]->t))) sdt_next(d, p, 1, out, NULL);
    else { u32 o = 0; while (is_smark(d->p[ns], o) && smark_index(d->p[ns]->t + o) != smark_index(d->p[item.para]->t + item.off)) o += 4; out->off = o; }
  }

  return 1;
}
int sdt_item_add(Doc *d, Pos item, int before, Pos *out)
{
  Pos e, e4, p;
  Sdt *r;
  int ok = 1;
  if (item.para >= d->np || !is_smark(d->p[item.para], item.off) || !(r = sdt_get(d, smark_index(d->p[item.para]->t + item.off))) ||
      r->type != SDT_REPITEM || !sdt_end(d, item, &e) || !parent_allows(d, item)) return 0;
  e4 = e; e4.off += 4;
  sdt_bypass++;
  doc_begin(d, item);
  if (r->level == SL_ROW) ok = rows_add(d, item, e, before, out);
  else if (r->level == SL_BLOCK) {
    Fragment *f = doc_fragment(d, item, e4);
    if (!f) ok = 0;
    else if (!before) {
      p = e4;
      ins(d, &p, "\n", 1, NULL);
      if (out) *out = p;
      ok = doc_paste(d, &p, f, PASTE_KEEP);
    } else {
      p = item;
      ins(d, &p, "\n", 1, NULL);
      p = item;
      ok = doc_paste(d, &p, f, PASTE_KEEP);
      if (out) *out = item;
    }
    fragment_free(f);
  } else {
    /* an inline item: copied beside itself */
    Fragment *f = doc_fragment(d, item, e4);
    p = before ? item : e4;
    if (out) *out = p;
    ok = f && doc_paste(d, &p, f, PASTE_KEEP);
    fragment_free(f);
  }
  doc_end(d, item);
  sdt_bypass--;
  return ok;
}
int sdt_item_remove(Doc *d, Pos item)
{
  Pos e, e4, a, b;
  Sdt *r;
  int ok = 0;
  if (item.para >= d->np || !is_smark(d->p[item.para], item.off) || !(r = sdt_get(d, smark_index(d->p[item.para]->t + item.off))) ||
      r->type != SDT_REPITEM || !sdt_end(d, item, &e) || !parent_allows(d, item)) return 0;
  e4 = e; e4.off += 4;
  sdt_bypass++;
  doc_begin(d, item);
  if (r->level == SL_ROW) {
    u32 fp, lp, i, nrows = 1, last;
    row_span(d, item.para, e.para, &fp, &lp);
    for (i = fp + 1; i <= lp; i++) if (d->p[i]->pf.cell.row != d->p[i - 1]->pf.cell.row) nrows++;
    last = e4.off < d->p[e.para]->len;           /* the section ends here: it is the last item */
    if (!(item.off && last)) {
      Pos c;
      if (item.off) {
        /* the first item: the section's start marks move to the next item's first cell */
        char m[64]; u32 n = item.off < 64 ? item.off : 0;
        CharFmt f = *para_fmt_at(d->p[item.para], 0);
        Pos at, a0;
        if (n && lp + 1 < d->np) {
          memcpy(m, d->p[item.para]->t, n);
          at.para = lp + 1; at.off = 0; ins(d, &at, m, n, &f);
          a0.para = item.para; a0.off = 0; doc_delete(d, a0, item);
        }
      } else if (last && fp > 0) {
        char m[64]; u32 n = d->p[e.para]->len - e4.off;
        CharFmt f = *para_fmt_at(d->p[e.para], e4.off);
        Pos at, b0;
        if (n < 64) {
          memcpy(m, d->p[e.para]->t + e4.off, n);
          b0.para = e.para; b0.off = d->p[e.para]->len; doc_delete(d, e4, b0);
          at.para = fp - 1; at.off = d->p[fp - 1]->len; ins(d, &at, m, n, &f);
        }
      }
      for (i = 0; i < nrows; i++) { c.para = fp; c.off = 0; if (tbl_row_delete(d, &c)) ok = 1; }
    }
  } else if (r->level == SL_BLOCK) {
    int first = item.off > 0, last = e4.off < d->p[e.para]->len;
    if (!last && e.para + 1 < d->np) { b.para = e.para + 1; b.off = 0; ok = doc_delete(d, item, b); }
    else if (!first && item.para > 0) { a.para = item.para - 1; a.off = d->p[a.para]->len; ok = doc_delete(d, a, e4); }
  } else {
    /* inline: only if another item is beside it */
    Pos in = item; int k;
    in.off += 4;
    k = sdt_at_depth(d, in, 1, &a, &b);
    if (k >= 0) {
      Pos a4 = a; a4.off += 4;
      if (pos_cmp(a4, item) || pos_cmp(e4, b)) ok = doc_delete(d, item, e4);
    }
  }
  doc_end(d, item);
  sdt_bypass--;
  return ok;
}

/* ------------------------------------------------------------------ */
/* the checks the edit paths make */
/* the kinds whose content isn't typed in (chosen, toggled or picked) */
static int fixed_kind(int t) { return t == SDT_CHECK || t == SDT_DROP || t == SDT_PICTURE; }
int sdt_typing(Doc *d, Pos *at)
{
  Pos s, e, a;
  int k, done = 0;
  Sdt *r;
  char t[512];
  if (!sdt_count(d) || (k = sdt_at(d, *at, &s, &e)) < 0) return 0;
  r = &d->sdt->r[k];
  if ((r->lock & SLK_EDIT) || fixed_kind(r->type) || r->type == SDT_GROUP) return 0;
  a = s; a.off += 4;
  if (r->plctext) {
    /* the placeholder showing: the typing takes its place */
    sdt_text(d, s, e, t, sizeof t);
    if (!strcmp(t, r->plctext) && pos_cmp(a, e) < 0) {
      sdt_bypass++;
      doc_begin(d, a);
      doc_delete(d, a, e);
      doc_end(d, a);
      sdt_bypass--;
      *at = a; e = a; done = 1;
    }
  }
  if (r->temporary && !(r->lock & SLK_DEL) && sdt_remove(d, s, 1)) {
    /* "remove when edited": the control goes, its content stays */
    if (at->para == s.para && at->off >= s.off + 4) at->off -= 4;
    done = 1;
  }
  return done;
}
int sdt_insert_ok(Doc *d, Pos at)
{
  int depth;
  if (sdt_bypass || !sdt_count(d)) return 1;
  for (depth = 0; depth < 32; depth++) {
    Pos s, e;
    int k = sdt_at_depth(d, at, depth, &s, &e);
    Sdt *r;
    if (k < 0) break;
    r = &d->sdt->r[k];
    if (r->lock & SLK_EDIT) return 0;
    if (!depth && (fixed_kind(r->type) || r->type == SDT_GROUP)) return 0;   /* chosen, not typed; a group's own text */
  }
  return 1;
}
/* a..b reaches into the content of a control round p without taking the whole control */
static int touches(Doc *d, Pos p, Pos a, Pos b)
{
  int depth;
  for (depth = 0; depth < 32; depth++) {
    Pos s, e, e4;
    int k = sdt_at_depth(d, p, depth, &s, &e);
    Sdt *r;
    if (k < 0) break;
    r = &d->sdt->r[k];
    e4 = e; e4.off += 4;
    if (pos_cmp(a, s) <= 0 && pos_cmp(e4, b) <= 0) continue;        /* all of it goes */
    if (r->lock & SLK_EDIT) return 1;
    if (!depth && (fixed_kind(r->type) || r->type == SDT_GROUP)) return 1;
  }
  return 0;
}
int sdt_delete_ok(Doc *d, Pos a, Pos b)
{
  u32 pi, o;
  if (sdt_bypass || !sdt_count(d)) return 1;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  if (touches(d, a, a, b) || touches(d, b, a, b)) return 0;
  for (pi = a.para; pi <= b.para && pi < d->np; pi++) {
    const Para *q = d->p[pi];
    for (o = pi == a.para ? a.off : 0; o + 4 <= (pi == b.para ? b.off : q->len); o++) {
      Sdt *r;
      if (!is_smark(q, o)) continue;
      r = sdt_get(d, smark_index(q->t + o));
      if (r && (r->lock & SLK_DEL)) return 0;
      if (r && IS_SSTART(q->t + o)) {
        /* a control wholly inside: its content too, if it is locked against editing */
        Pos s, e; s.para = pi; s.off = o;
        if ((r->lock & SLK_EDIT) && sdt_end(d, s, &e) && (e.para > b.para || (e.para == b.para && e.off + 4 > b.off))) return 0;
      }
      o += 3;
    }
  }
  return 1;
}
/* the text form field round p, its marks; 0 if none */
static int text_ff_at(Doc *d, Pos p, Pos *s, Pos *e)
{
  int k;
  if (!d->nfields) return 0;
  k = field_at(d, p, s, e);
  return k >= 0 && ff_kind(d, k) == FF_TEXT;
}
int sdt_forms_ok(Doc *d, Pos a, Pos b)
{
  Pos s, e;
  int k;
  if (sdt_bypass) return 1;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  k = sdt_at(d, a, &s, &e);
  if (k >= 0 && pos_cmp(b, e) <= 0 && !(d->sdt->r[k].lock & SLK_EDIT) && d->sdt->r[k].type != SDT_GROUP) {
    Pos s4 = s; s4.off += 4;
    if (pos_cmp(a, s4) >= 0) return 1;
  }
  if (text_ff_at(d, a, &s, &e) && pos_cmp(b, e) <= 0) {
    /* in its result: after the separator (Quill keeps the result between the marks) */
    Pos s4 = s; s4.off += 4;
    if (pos_cmp(a, s4) >= 0) return 1;
  }
  return 0;
}
int sdt_forms_next(Doc *d, Pos p, int dir, Pos *a, Pos *b)
{
  Pos s, e, best, beste;
  int found = 0, pass;
  for (pass = 0; pass < 2 && !found; pass++) {
    Pos q = pass ? (dir > 0 ? doc_start(d) : doc_end_pos(d)) : p;
    int k = sdt_next(d, q, dir, &s, &e), fk;
    Pos fs, fe;
    while (k >= 0 && (d->sdt->r[k].type == SDT_GROUP || d->sdt->r[k].type == SDT_REPEAT || d->sdt->r[k].type == SDT_REPITEM || (d->sdt->r[k].lock & SLK_EDIT))) {
      k = sdt_next(d, s, dir, &s, &e);
    }
    if (k >= 0) { best = s; beste = e; found = 1; }
    fk = d->nfields ? field_next(d, q, dir, &fs, &fe) : -1;
    while (fk >= 0 && ff_kind(d, fk) == FF_NONE) { Pos nx = fs; if (dir > 0) nx.off += 4; fk = field_next(d, nx, dir, &fs, &fe); }
    if (fk >= 0 && (!found || (dir > 0 ? pos_cmp(fs, best) < 0 : pos_cmp(fs, best) > 0))) { best = fs; beste = fe; found = 1; }
  }
  if (!found) return 0;
  best.off += 4;
  if (a) *a = best;
  if (b) *b = beste;
  return 1;
}

/* ------------------------------------------------------------------ */
/* legacy form fields */
int ff_kind(const Doc *d, int k)
{
  const Field *f = field_get(d, k);
  char n[24];
  if (!f || !f->ffdata) return FF_NONE;
  field_name(f->code, n, sizeof n);
  return !strcmp(n, "FORMTEXT") ? FF_TEXT : !strcmp(n, "FORMCHECKBOX") ? FF_CHECK : !strcmp(n, "FORMDROPDOWN") ? FF_DROP : FF_NONE;
}
int ff_val(const char *ffdata, const char *elem, char *out, size_t cap)
{
  if (cap) out[0] = 0;
  if (!ffdata) return 0;
  return find_attr(ffdata, strlen(ffdata), elem, "val", out, cap);
}
int ff_checked(const Doc *d, int k)
{
  const Field *f = field_get(d, k);
  char v[16];
  if (!f || !f->ffdata) return 0;
  if (ff_val(f->ffdata, "checked", v, sizeof v)) return onoff_val(v[0] ? v : NULL);
  ff_val(f->ffdata, "default", v, sizeof v);
  return v[0] && onoff_val(v);
}
int ff_result(const Doc *d, int k)
{
  const Field *f = field_get(d, k);
  char v[16];
  if (!f || !f->ffdata || !ff_val(f->ffdata, "result", v, sizeof v)) {
    if (f && f->ffdata && ff_val(f->ffdata, "default", v, sizeof v) && v[0]) return atoi(v);
    return 0;
  }
  return atoi(v);
}
int ff_entries(const Doc *d, int k, int i, char *out, size_t cap)
{
  const Field *f = field_get(d, k);
  Xml x;
  int n = 0;
  if (cap) out[0] = 0;
  if (!f || !f->ffdata) return 0;
  xml_init(&x, f->ffdata, strlen(f->ffdata));
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START && !strcmp(x.name, "listEntry")) {
      const char *v = xml_attr(&x, "val");
      if (n == i && v) snprintf(out, cap, "%s", v);
      n++;
    }
  }
  xml_free(&x);
  return n;
}
int ff_box(const Doc *d, int k, char *out, size_t cap)
{
  const Field *f = field_get(d, k);
  int kind = ff_kind(d, k);
  if (!f || !f->nosep || (kind != FF_CHECK && kind != FF_DROP) || cap < 8) return 0;
  if (kind == FF_CHECK) { out[utf8_put(out, ff_checked(d, k) ? 0x2612 : 0x2610)] = 0; return 1; }
  if (!ff_entries(d, k, ff_result(d, k), out, cap) || !out[0]) snprintf(out, cap, "\xE2\x80\x82\xE2\x80\x82\xE2\x80\x82\xE2\x80\x82\xE2\x80\x82");
  return 1;
}
/* field k's start mark at s, end mark at e: made to name a new record with ffdata (the code kept) */
static int ff_swap(Doc *d, Pos s, Pos e, int k, const char *ffdata)
{
  const Field *f = field_get(d, k);
  int nk;
  char m[8];
  CharFmt cf;
  Pos x;
  if (!f) return 0;
  if ((nk = field_new(d, f->code, f->locked)) < 0) return 0;
  f = field_get(d, k);
  d->fields[nk].dirty = f->dirty; d->fields[nk].nosep = f->nosep;
  d->fields[nk].ffdata = str_dup(ffdata);
  sdt_bypass++;
  doc_begin(d, s);
  cf = *para_fmt_at(d->p[e.para], e.off);
  x = e; x.off += 4;
  doc_delete(d, e, x);
  fmark_utf8(m, nk, 1); ins(d, &e, m, 4, &cf);
  cf = *para_fmt_at(d->p[s.para], s.off);
  x = s; x.off += 4;
  doc_delete(d, s, x);
  fmark_utf8(m, nk, 0); ins(d, &s, m, 4, &cf);
  doc_end(d, s);
  sdt_bypass--;
  return 1;
}
/* ffdata with element el (and anything in it) taken out */
static void cut_el(Buf *b, const char *ff, const char *el)
{
  size_t n = strlen(ff), i = 0, l = strlen(el);
  while (i < n) {
    const char *lt = strchr(ff + i, '<');
    const char *nm, *gt;
    if (!lt) { buf_adds(b, ff + i); return; }
    buf_add(b, ff + i, (size_t)(lt - (ff + i)));
    nm = lt + 1;
    { const char *c = nm; while (*c && *c != ':' && *c != ' ' && *c != '>' && *c != '/') c++; if (*c == ':') nm = c + 1; }
    gt = strchr(lt, '>');
    if (!gt) { buf_adds(b, lt); return; }
    if (lt[1] != '/' && !strncmp(nm, el, l) && (nm[l] == ' ' || nm[l] == '/' || nm[l] == '>')) {
      if (gt[-1] == '/') { i = (size_t)(gt + 1 - ff); continue; }
      {
        /* its closing tag */
        const char *c = gt;
        char close[48];
        size_t pl = (size_t)(nm - (lt + 1));
        snprintf(close, sizeof close, "</%.*s%s>", (int)pl, lt + 1, el);
        c = strstr(gt, close);
        i = c ? (size_t)(c + strlen(close) - ff) : n;
        continue;
      }
    }
    buf_add(b, lt, (size_t)(gt + 1 - lt));
    i = (size_t)(gt + 1 - ff);
  }
}
/* ffdata with ins put right after the start tag (or empty tag, made a start tag) of el */
static char *put_after(const char *ff, const char *el, const char *insx)
{
  Buf b;
  const char *p = ff;
  size_t l = strlen(el);
  buf_init(&b);
  while ((p = strchr(p, '<'))) {
    const char *nm = p + 1, *c = nm, *gt;
    while (*c && *c != ':' && *c != ' ' && *c != '>' && *c != '/') c++;
    if (*c == ':') nm = c + 1;
    gt = strchr(p, '>');
    if (!gt) break;
    if (p[1] != '/' && !strncmp(nm, el, l) && (nm[l] == ' ' || nm[l] == '/' || nm[l] == '>')) {
      if (gt[-1] == '/') {
        size_t pl = (size_t)(nm - (p + 1));
        buf_add(&b, ff, (size_t)(gt - 1 - ff)); buf_adds(&b, ">"); buf_adds(&b, insx);
        buf_printf(&b, "</%.*s%s>", (int)pl, p + 1, el);
        buf_adds(&b, gt + 1);
      } else { buf_add(&b, ff, (size_t)(gt + 1 - ff)); buf_adds(&b, insx); buf_adds(&b, gt + 1); }
      return b.data ? buf_take(&b) : str_dup("");
    }
    p = gt + 1;
  }
  buf_adds(&b, ff);
  return b.data ? buf_take(&b) : str_dup("");
}
/* after el's own end (its empty tag, or its closing tag) */
static char *put_behind(const char *ff, const char *el, const char *insx)
{
  Buf b;
  const char *p = ff;
  size_t l = strlen(el);
  buf_init(&b);
  while ((p = strchr(p, '<'))) {
    const char *nm = p + 1, *c = nm, *gt;
    while (*c && *c != ':' && *c != ' ' && *c != '>' && *c != '/') c++;
    if (*c == ':') nm = c + 1;
    gt = strchr(p, '>');
    if (!gt) break;
    if (!strncmp(nm, el, l) && (nm[l] == ' ' || nm[l] == '/' || nm[l] == '>') && (gt[-1] == '/' || p[1] == '/')) {
      buf_add(&b, ff, (size_t)(gt + 1 - ff)); buf_adds(&b, insx); buf_adds(&b, gt + 1);
      return b.data ? buf_take(&b) : str_dup("");
    }
    p = gt + 1;
  }
  buf_adds(&b, ff);
  return b.data ? buf_take(&b) : str_dup("");
}
int ff_set_data(Doc *d, Pos s, const char *ffdata)
{
  Pos e;
  int k;
  if (s.para >= d->np || s.off + 4 > d->p[s.para]->len || !IS_FSTART(d->p[s.para]->t + s.off)) return 0;
  k = fmark_index(d->p[s.para]->t + s.off);
  if (!field_end(d, s, &e)) return 0;
  return ff_swap(d, s, e, k, ffdata);
}
int ff_toggle(Doc *d, Pos s)
{
  int k, on, r;
  const Field *f;
  Buf b;
  char *nf;
  if (s.para >= d->np || s.off + 4 > d->p[s.para]->len || !IS_FSTART(d->p[s.para]->t + s.off)) return 0;
  k = fmark_index(d->p[s.para]->t + s.off);
  if (ff_kind(d, k) != FF_CHECK || !(f = field_get(d, k))) return 0;
  on = ff_checked(d, k);
  buf_init(&b);
  cut_el(&b, f->ffdata, "checked");
  {
    const char *base = b.data ? b.data : "";
    const char *ck = on ? "<w:checked w:val=\"0\"/>" : "<w:checked/>";
    nf = strstr(base, "default") ? put_behind(base, "default", ck) : put_after(base, "checkBox", ck);
  }
  buf_free(&b);
  if (!nf) return 0;
  r = ff_set_data(d, s, nf);
  free(nf);
  return r;
}
int ff_choose(Doc *d, Pos s, int i)
{
  int k, r;
  const Field *f;
  Buf b;
  char ins_[48], *nf;
  if (s.para >= d->np || s.off + 4 > d->p[s.para]->len || !IS_FSTART(d->p[s.para]->t + s.off)) return 0;
  k = fmark_index(d->p[s.para]->t + s.off);
  if (ff_kind(d, k) != FF_DROP || !(f = field_get(d, k)) || i < 0 || i >= ff_entries(d, k, 0, ins_, sizeof ins_)) return 0;
  buf_init(&b);
  cut_el(&b, f->ffdata, "result");
  sprintf(ins_, "<w:result w:val=\"%d\"/>", i);
  nf = put_after(b.data ? b.data : "", "ddList", ins_);
  buf_free(&b);
  if (!nf) return 0;
  doc_begin(d, s);
  r = ff_set_data(d, s, nf);
  if (r && !d->fields[d->nfields - 1].nosep) {
    /* a result in the text: the entry's text */
    Pos e;
    char t[256];
    ff_entries(d, d->nfields - 1, i, t, sizeof t);
    if (field_end(d, s, &e)) {
      Pos a = s; CharFmt cf;
      a.off += 4;
      cf = *para_fmt_at(d->p[s.para], s.off);
      sdt_bypass++;
      if (pos_cmp(a, e) < 0) doc_delete(d, a, e);
      ins(d, &a, t, (u32)strlen(t), &cf);
      sdt_bypass--;
    }
  }
  doc_end(d, s);
  free(nf);
  return r;
}
int ff_insert(Doc *d, Pos *at, int kind)
{
  FieldEnv e;
  int k, n = 0, i;
  char name[24], ff[400];
  const char *code = kind == FF_CHECK ? " FORMCHECKBOX " : kind == FF_DROP ? " FORMDROPDOWN " : " FORMTEXT ";
  const char *base = kind == FF_CHECK ? "Check" : kind == FF_DROP ? "Dropdown" : "Text";
  for (i = 0; i < d->nfields; i++) if (ff_kind(d, i) == kind) n++;
  sprintf(name, "%s%d", base, n + 1);
  if (kind == FF_CHECK) sprintf(ff, "<w:ffData><w:name w:val=\"%s\"/><w:enabled/><w:calcOnExit w:val=\"0\"/><w:checkBox><w:sizeAuto/><w:default w:val=\"0\"/></w:checkBox></w:ffData>", name);
  else if (kind == FF_DROP) sprintf(ff, "<w:ffData><w:name w:val=\"%s\"/><w:enabled/><w:calcOnExit w:val=\"0\"/><w:ddList/></w:ffData>", name);
  else sprintf(ff, "<w:ffData><w:name w:val=\"%s\"/><w:enabled/><w:calcOnExit w:val=\"0\"/><w:textInput/></w:ffData>", name);
  field_env(&e);
  sdt_bypass++;
  k = field_insert_result(d, at, code, kind == FF_TEXT ? "\xE2\x80\x82\xE2\x80\x82\xE2\x80\x82\xE2\x80\x82\xE2\x80\x82" : "", &e, NULL);
  sdt_bypass--;
  if (k < 0) return -1;
  d->fields[k].ffdata = str_dup(ff);
  d->fields[k].nosep = (u8)(kind != FF_TEXT);
  d->fields[k].dirty = 0;
  return k;
}
int (*sdt_forms_protected)(const Doc *d) = prot_forms_only;   /* (WP7: Restrict Editing's "filling in forms") */
char *ff_build(const char *old, const char *name, const char *kindxml)
{
  Buf a, b, c;
  const char *src = old ? old : "<w:ffData></w:ffData>";
  char *r, *t;
  buf_init(&a); buf_init(&b); buf_init(&c);
  cut_el(&a, src, "name");
  cut_el(&b, a.data ? a.data : "", "textInput");
  buf_free(&a); buf_init(&a);
  cut_el(&a, b.data ? b.data : "", "checkBox");
  buf_free(&b); buf_init(&b);
  cut_el(&b, a.data ? a.data : "", "ddList");
  buf_free(&a);
  if (!strstr(b.data ? b.data : "", "ffData")) { buf_free(&b); buf_init(&b); buf_adds(&b, "<w:ffData><w:enabled/><w:calcOnExit w:val=\"0\"/></w:ffData>"); }
  buf_adds(&c, "<w:name w:val=\"");
  xml_escape(&c, name ? name : "", name ? strlen(name) : 0);
  buf_adds(&c, "\"/>");
  t = put_after(b.data, "ffData", c.data ? c.data : "");
  buf_free(&b); buf_free(&c);
  if (!t) return NULL;
  {
    /* the kind's element last, before the closing tag */
    char *close = strstr(t, "</w:ffData>");
    Buf o;
    buf_init(&o);
    if (close) { buf_add(&o, t, (size_t)(close - t)); buf_adds(&o, kindxml ? kindxml : ""); buf_adds(&o, close); }
    else buf_adds(&o, t);
    free(t);
    r = o.data ? buf_take(&o) : NULL;
  }
  return r;
}
const char *const sdt_calendars[] = { "gregorian", "gregorianUs", "hijri", "hebrew", "japan", "korea", "saka", "taiwan", "thai", NULL };
const char *const sdt_store_as[] = { "text", "date", "dateTime", NULL };
char *ff_options(const char *old, int kind, const char *name, const char *deftext, int maxlen, int defchecked, int checked,
                 const char *const *entries, int n, int result)
{
  Buf k;
  char ty[32], fm[128];
  char *r;
  int i;
  buf_init(&k);
  if (kind == FF_TEXT) {
    buf_adds(&k, "<w:textInput>");
    if (ff_val(old, "type", ty, sizeof ty) && ty[0]) { buf_adds(&k, "<w:type w:val=\""); xml_escape(&k, ty, strlen(ty)); buf_adds(&k, "\"/>"); }
    if (deftext && deftext[0]) { buf_adds(&k, "<w:default w:val=\""); xml_escape(&k, deftext, strlen(deftext)); buf_adds(&k, "\"/>"); }
    if (maxlen > 0) buf_printf(&k, "<w:maxLength w:val=\"%d\"/>", maxlen);
    if (ff_val(old, "format", fm, sizeof fm) && fm[0]) { buf_adds(&k, "<w:format w:val=\""); xml_escape(&k, fm, strlen(fm)); buf_adds(&k, "\"/>"); }
    buf_adds(&k, "</w:textInput>");
  } else if (kind == FF_CHECK) {
    buf_adds(&k, "<w:checkBox>");
    if (ff_val(old, "size", ty, sizeof ty) && ty[0]) { buf_adds(&k, "<w:size w:val=\""); xml_escape(&k, ty, strlen(ty)); buf_adds(&k, "\"/>"); }
    else buf_adds(&k, "<w:sizeAuto/>");
    buf_printf(&k, "<w:default w:val=\"%d\"/>", defchecked ? 1 : 0);
    buf_adds(&k, checked ? "<w:checked/>" : "<w:checked w:val=\"0\"/>");
    buf_adds(&k, "</w:checkBox>");
  } else {
    buf_printf(&k, "<w:ddList><w:result w:val=\"%d\"/>", result >= 0 && result < n ? result : 0);
    for (i = 0; i < n; i++) { buf_adds(&k, "<w:listEntry w:val=\""); xml_escape(&k, entries[i], strlen(entries[i])); buf_adds(&k, "\"/>"); }
    buf_adds(&k, "</w:ddList>");
  }
  r = ff_build(old, name, k.data ? k.data : "");
  buf_free(&k);
  return r;
}
int ff_maxlen(const Doc *d, int k)
{
  const Field *f = field_get(d, k);
  char v[16];
  return f && f->ffdata && ff_val(f->ffdata, "maxLength", v, sizeof v) ? atoi(v) : 0;
}
int ff_name(const Doc *d, int k, char *out, size_t cap)
{
  const Field *f = field_get(d, k);
  return ff_val(f ? f->ffdata : NULL, "name", out, cap);
}
int ff_default_text(const Doc *d, int k, char *out, size_t cap)
{
  const Field *f = field_get(d, k);
  if (cap) out[0] = 0;
  return f && ff_kind(d, k) == FF_TEXT && ff_val(f->ffdata, "default", out, cap);
}
int ff_default_checked(const Doc *d, int k)
{
  const Field *f = field_get(d, k);
  char v[8];
  return f && ff_kind(d, k) == FF_CHECK && ff_val(f->ffdata, "default", v, sizeof v) && v[0] == '1';
}
int sdt_form_at(const Doc *d, Pos p) { return sdt_forms_ok((Doc *)d, p, p); }
