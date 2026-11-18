/*
 * wordxml.c - Word's XML documents (WP28e, docs/wordcraft-gap.md): Flat OPC (Word 2007 and later's
 * "Word XML Document", a whole .docx package in one pkg:package file) read and written, and Word
 * 2003 XML (w:wordDocument, WordprocessingML 2003) read. Both go through the .docx code: a Flat OPC
 * file is the zip it stands for, made in memory; a Word 2003 file is turned into the parts of a
 * .docx (document, styles, numbering, settings, notes, comments, headers and footers, pictures,
 * core properties) and read as one. Row `fmt_wordxml` of core/docx.c's table of formats.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "docx.h"
#include "blocks.h"
#include "fmtio.h"
#include "image.h"
#include "xml.h"
#include "zip.h"

#define NS_W "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
#define NS_R "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
#define CT_W "application/vnd.openxmlformats-officedocument.wordprocessingml."
#define RT "http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
#define XML_DECL "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"

/* ------------------------------------------------------------------ sniffing */
/* the root element's name (local) and whether the namespace s names is declared in the first bytes:
   1 Flat OPC, 2 Word 2003 XML, 0 neither */
int wordxml_kind(const u8 *b, size_t n)
{
  size_t i = 0, m = n < 4096 ? n : 4096;
  char root[40];
  int k = 0;
  if (m >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) i = 3;
  for (;;) {
    while (i < m && b[i] != '<') { if (b[i] != ' ' && b[i] != '\t' && b[i] != '\r' && b[i] != '\n') return 0; i++; }
    if (i + 1 >= m) return 0;
    if (b[i + 1] == '?' || b[i + 1] == '!') { while (i < m && b[i] != '>') i++; i++; continue; }
    break;
  }
  i++;
  {
    size_t a = i, c = 0;
    while (i < m && b[i] != ' ' && b[i] != '>' && b[i] != '/' && b[i] != '\t' && b[i] != '\r' && b[i] != '\n') { if (b[i] == ':') a = i + 1; i++; }
    if (i - a >= sizeof root) return 0;
    for (; a < i; a++) root[c++] = (char)b[a];
    root[c] = 0;
  }
  {
    /* the namespace among the first bytes (Word declares it on the root) */
    const char *want = !strcmp(root, "package") ? "schemas.microsoft.com/office/2006/xmlPackage" :
                       !strcmp(root, "wordDocument") ? "schemas.microsoft.com/office/word/2003/wordml" : NULL;
    size_t wl, j;
    if (!want) return 0;
    wl = strlen(want);
    for (j = 0; j + wl <= m; j++) if (b[j] == want[0] && !memcmp(b + j, want, wl)) { k = root[0] == 'p' ? 1 : 2; break; }
  }
  return k;
}
static int sniff_wordxml(const u8 *b, size_t n) { return wordxml_kind(b, n) != 0; }

/* ------------------------------------------------------------------ Flat OPC */
/* content types: [Content_Types].xml started (with the defaults every package needs) */
static void ct_begin(Buf *ct)
{
  buf_adds(ct, XML_DECL "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
           "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
           "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
           "<Default Extension=\"png\" ContentType=\"image/png\"/><Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
           "<Default Extension=\"jpg\" ContentType=\"image/jpeg\"/><Default Extension=\"gif\" ContentType=\"image/gif\"/>"
           "<Default Extension=\"bmp\" ContentType=\"image/bmp\"/><Default Extension=\"wmf\" ContentType=\"image/x-wmf\"/>"
           "<Default Extension=\"emf\" ContentType=\"image/x-emf\"/>");
}
static void ct_override(Buf *ct, const char *name, const char *type)
{
  buf_adds(ct, "<Override PartName=\"");
  if (name[0] != '/') buf_addc(ct, '/');
  xml_escape(ct, name, strlen(name));
  buf_adds(ct, "\" ContentType=\"");
  xml_escape(ct, type, strlen(type));
  buf_adds(ct, "\"/>");
}
/* a Flat OPC file as the zip it stands for (malloc'd, *zlen its size); NULL if it has no parts or
   memory ran out */
u8 *flat_to_zip(const u8 *b, size_t n, size_t *zlen)
{
  Xml x;
  ZipWriter w;
  Buf ct;
  int parts = 0, oom = 0;
  zip_begin(&w);
  buf_init(&ct);
  ct_begin(&ct);
  xml_init(&x, (const char *)b, n);
  while (xml_next(&x) != XT_EOF) {
    char name[256], type[200];
    const char *v;
    int d0;
    if (x.type != XT_START || strcmp(x.name, "part")) continue;
    v = xml_attr(&x, "name");
    snprintf(name, sizeof name, "%s", v ? v : "");
    v = xml_attr(&x, "contentType");
    snprintf(type, sizeof type, "%s", v ? v : "");
    d0 = x.depth;
    while (xml_next(&x) != XT_EOF) {
      if (x.type == XT_END && x.depth < d0) break;
      if (x.type != XT_START) continue;
      if (!strcmp(x.name, "xmlData")) {
        /* the part's root element, as written */
        int d1 = x.depth;
        while (xml_next(&x) != XT_EOF) {
          if (x.type == XT_END && x.depth < d1) break;
          if (x.type == XT_START) {
            const char *raw;
            size_t rl = xml_raw(&x, &raw);
            Buf p;
            buf_init(&p);
            buf_adds(&p, XML_DECL);
            buf_add(&p, raw, rl);
            if (p.oom || !name[0] || !zip_add(&w, name + (name[0] == '/'), p.data, p.len)) oom |= p.oom;
            else parts++;
            buf_free(&p);
          }
        }
      } else if (!strcmp(x.name, "binaryData")) {
        int d1 = x.depth;
        Buf t;
        buf_init(&t);
        while (xml_next(&x) != XT_EOF) {
          if (x.type == XT_END && x.depth < d1) break;
          if (x.type == XT_TEXT) buf_add(&t, x.text.data, x.text.len);
        }
        if (name[0] && !t.oom) {
          u32 len = 0;
          u8 *data = b64_decode(t.data ? t.data : "", t.len, &len);
          if (data && zip_add(&w, name + (name[0] == '/'), data, len)) parts++;
          else oom = 1;
          free(data);
        }
        buf_free(&t);
      }
    }
    if (name[0] && type[0] && !strstr(type, "relationships+xml")) ct_override(&ct, name, type);
  }
  xml_free(&x);
  buf_adds(&ct, "</Types>");
  if (!parts || oom || ct.oom || !zip_add(&w, "[Content_Types].xml", ct.data, ct.len) || !zip_finish(&w)) {
    buf_free(&ct); zip_free(&w);
    return NULL;
  }
  buf_free(&ct);
  *zlen = w.out.len;
  {
    u8 *out = (u8 *)w.out.data;
    w.out.data = NULL; w.out.len = 0;
    zip_free(&w);
    return out;
  }
}
/* a part's content type from [Content_Types].xml (an Override for it, else the Default for its
   ending); "application/octet-stream" if neither */
static void ct_lookup(const char *ct, size_t ctn, const char *name, char *out, size_t cap)
{
  Xml x;
  const char *dot = strrchr(name, '.');
  char def[200] = "";
  snprintf(out, cap, "application/octet-stream");
  if (!ct) return;
  xml_init(&x, ct, ctn);
  while (xml_next(&x) != XT_EOF) {
    const char *pn, *t;
    if (x.type != XT_START) continue;
    t = xml_attr(&x, "ContentType");
    if (!t) continue;
    if (!strcmp(x.name, "Override") && (pn = xml_attr(&x, "PartName")) && str_ieq(pn + (pn[0] == '/'), name)) { snprintf(out, cap, "%s", t); xml_free(&x); return; }
    if (!strcmp(x.name, "Default") && dot && (pn = xml_attr(&x, "Extension")) && str_ieq(pn, dot + 1)) snprintf(def, sizeof def, "%s", t);
  }
  xml_free(&x);
  if (def[0]) snprintf(out, cap, "%s", def);
}
/* a .docx package as Flat OPC: every part a pkg:part, XML ones as their XML (pkg:xmlData, without
   their declaration), others in base64 (pkg:binaryData) */
u8 *zip_to_flat(const u8 *z, size_t zn, size_t *len)
{
  ZipReader r;
  Buf o;
  char *ct;
  size_t ctn = 0;
  int i;
  if (!zip_open(&r, z, zn)) return NULL;
  ct = (char *)zip_read(&r, zip_find(&r, "[Content_Types].xml"), &ctn);
  buf_init(&o);
  buf_adds(&o, XML_DECL "<?mso-application progid=\"Word.Document\"?>\n"
           "<pkg:package xmlns:pkg=\"http://schemas.microsoft.com/office/2006/xmlPackage\">");
  for (i = 0; i < r.n; i++) {
    char type[200];
    size_t n = 0;
    u8 *data;
    const char *name = r.e[i].name;
    if (!strcmp(name, "[Content_Types].xml") || !name[0] || name[strlen(name) - 1] == '/') continue;
    if (!(data = zip_read(&r, i, &n))) continue;
    ct_lookup(ct, ctn, name, type, sizeof type);
    if (strstr(name, ".rels") && !strcmp(type, "application/octet-stream")) snprintf(type, sizeof type, "application/vnd.openxmlformats-package.relationships+xml");
    buf_adds(&o, "\n<pkg:part pkg:name=\"/");
    xml_escape(&o, name, strlen(name));
    buf_adds(&o, "\" pkg:contentType=\"");
    xml_escape(&o, type, strlen(type));
    buf_adds(&o, "\"");
    if (strstr(type, "xml")) {
      /* the XML without its declaration */
      size_t s = 0;
      if (n >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) s = 3;
      while (s < n && (data[s] == ' ' || data[s] == '\r' || data[s] == '\n' || data[s] == '\t')) s++;
      if (s + 1 < n && data[s] == '<' && data[s + 1] == '?') { while (s < n && data[s] != '>') s++; s++; }
      while (s < n && (data[s] == ' ' || data[s] == '\r' || data[s] == '\n' || data[s] == '\t')) s++;
      if (strstr(type, "relationships+xml")) buf_adds(&o, " pkg:padding=\"256\"");
      buf_adds(&o, "><pkg:xmlData>");
      if (s < n) buf_add(&o, data + s, n - s);
      buf_adds(&o, "</pkg:xmlData></pkg:part>");
    } else {
      Buf b64;
      size_t k;
      buf_init(&b64);
      b64_encode(&b64, data, n);
      buf_adds(&o, " pkg:compression=\"store\"><pkg:binaryData>");
      for (k = 0; k < b64.len; k += 76) { buf_add(&o, b64.data + k, b64.len - k < 76 ? b64.len - k : 76); buf_addc(&o, '\n'); }
      buf_adds(&o, "</pkg:binaryData></pkg:part>");
      if (b64.oom) o.oom = 1;
      buf_free(&b64);
    }
    free(data);
  }
  buf_adds(&o, "\n</pkg:package>\n");
  free(ct);
  zip_close(&r);
  if (o.oom) { buf_free(&o); return NULL; }
  *len = o.len;
  return (u8 *)o.data;
}

/* ------------------------------------------------------------------ Word 2003 XML */
/* the namespace prefix of the start tag just read (its raw text), "" for none */
static void tag_prefix(Xml *x, char *out, size_t cap)
{
  size_t i = x->tok + 1, k = 0;
  const char *s = x->s;
  out[0] = 0;
  while (i < x->n && s[i] != ':' && s[i] != ' ' && s[i] != '>' && s[i] != '/' && s[i] != '\t' && s[i] != '\r' && s[i] != '\n') {
    if (k + 1 < cap) out[k++] = s[i];
    i++;
  }
  out[k] = 0;
  if (i >= x->n || s[i] != ':') out[0] = 0;
}
/* each attribute's prefix in the start tag just read, in order (as xml_next reads them) */
static void attr_prefixes(Xml *x, char pre[][8])
{
  const char *s = x->s;
  size_t e = x->tok + 1, n = x->n;
  int k = 0;
  while (e < n && s[e] != ' ' && s[e] != '>' && s[e] != '/' && s[e] != '\t' && s[e] != '\r' && s[e] != '\n') e++;
  while (k < x->nattr && e < n) {
    size_t na, j;
    char q;
    while (e < n && (s[e] == ' ' || s[e] == '\t' || s[e] == '\r' || s[e] == '\n')) e++;
    if (e >= n || s[e] == '>' || s[e] == '/') break;
    na = e;
    while (e < n && s[e] != '=' && s[e] != ' ' && s[e] != '>') e++;
    pre[k][0] = 0;
    for (j = na; j < e && s[j] != ':'; j++) ;
    if (j < e) { size_t l = j - na < 7 ? j - na : 7; memcpy(pre[k], s + na, l); pre[k][l] = 0; }
    while (e < n && s[e] != '"' && s[e] != '\'') e++;
    if (e >= n) break;
    q = s[e++];
    while (e < n && s[e] != q) e++;
    e++;
    k++;
  }
  for (; k < x->nattr; k++) pre[k][0] = 0;
}
/* Word 2003's hyphenated names (b-cs, first-line, line-rule, next-page...) as 2006's camelCase */
static void camel(const char *s, char *out, size_t cap)
{
  size_t k = 0;
  int up = 0;
  for (; *s && k + 1 < cap; s++) {
    if (*s == '-') { up = 1; continue; }
    out[k++] = up && *s >= 'a' && *s <= 'z' ? (char)(*s - 32) : *s;
    up = 0;
  }
  out[k] = 0;
}
static int lower_word(const char *s) { for (; *s; s++) if (!((*s >= 'a' && *s <= 'z') || *s == '-')) return 0; return 1; }


#define W3_MAXDEPTH 256
#define W3_MAXPARTS 96
typedef struct { char name[64]; Buf b; } W3Part;
typedef struct { char name[96]; char ext[6]; u32 pw, ph; } W3Media;
/* an open element: on its END, close is written to closeto, then after to restore, which becomes the
   output again */
typedef struct { Buf *restore, *closeto; char close[56]; char after[8]; char tag[40]; } W3Frame;
typedef struct {
  Buf doc, styles, numbering, settings, foot, end, comments, prels, mrels, core;
  W3Part part[W3_MAXPARTS]; int nparts;
  int nfoot, nend, ncomment, nrel, nhdr, nftr;
  W3Media *media; int nmedia, mediacap;
  W3Frame st[W3_MAXDEPTH]; int sp, lost;
  Buf *out;
  char deffont[64];
  ZipWriter zw;
} W3;

static void w3_rel(W3 *c, Buf *b, const char *type, const char *target, int external, char *id, size_t cap)
{
  snprintf(id, cap, "rIdW%d", ++c->nrel);
  buf_adds(b, "<Relationship Id=\""); buf_adds(b, id);
  buf_adds(b, "\" Type=\"" RT); buf_adds(b, type);
  buf_adds(b, "\" Target=\""); xml_escape(b, target, strlen(target));
  buf_adds(b, external ? "\" TargetMode=\"External\"/>" : "\"/>");
}
static void w3_attr(Buf *o, const char *name, const char *val)
{
  buf_addc(o, ' ');
  buf_adds(o, name);
  buf_adds(o, "=\"");
  xml_escape(o, val ? val : "", val ? strlen(val) : 0);
  buf_addc(o, '"');
}
/* the start tag just read written as 2006's w:tag: its w: attributes in camelCase (fareast as
   eastAsia), "on"/"off" as 1/0, and with camelvals a hyphenated w:val too (next-page, thin-thick-gap) */
static void w3_open(W3 *c, Xml *x, const char *tag, int camelvals)
{
  char pre[XML_MAXATTR][8], an[64], av[128];
  int k;
  Buf *o = c->out;
  attr_prefixes(x, pre);
  buf_adds(o, "<w:"); buf_adds(o, tag);
  for (k = 0; k < x->nattr; k++) {
    const char *n = x->an[k], *v = x->av[k];
    if (!strcmp(pre[k], "xml")) { buf_adds(o, " xml:"); buf_adds(o, n); buf_adds(o, "=\""); xml_escape(o, v, strlen(v)); buf_addc(o, '"'); continue; }
    if (strcmp(pre[k], "w")) continue;          /* (namespace declarations, wx: hints, others) */
    if (!strcmp(n, "fareast")) n = "eastAsia";
    else if (!strcmp(n, "listDefId")) n = "abstractNumId";
    else if (!strcmp(n, "ilfo")) n = "numId";
    camel(n, an, sizeof an);
    if (camelvals && !strcmp(an, "val") && lower_word(v) && strchr(v, '-')) { camel(v, av, sizeof av); v = av; }
    else if (!strcmp(v, "on")) v = "1";
    else if (!strcmp(v, "off")) v = "0";
    w3_attr(o, an, v);
  }
  buf_addc(o, '>');
}
/* an element begun: on its END, close goes to closeto (NULL: the output then), after to the output
   before it, which comes back */
static void w3_push(W3 *c, Buf *restore, const char *tag, const char *close, Buf *closeto, const char *after)
{
  W3Frame *f;
  if (c->sp >= W3_MAXDEPTH) { c->sp++; return; }
  f = &c->st[c->sp++];
  f->restore = restore;
  f->closeto = closeto ? closeto : c->out;
  snprintf(f->close, sizeof f->close, "%s", close ? close : "");
  snprintf(f->after, sizeof f->after, "%s", after ? after : "");
  snprintf(f->tag, sizeof f->tag, "%s", tag ? tag : "");
}
static void w3_pop(W3 *c)
{
  W3Frame *f;
  if (!c->sp) return;
  if (--c->sp >= W3_MAXDEPTH) return;
  f = &c->st[c->sp];
  if (f->close[0] && f->closeto) buf_adds(f->closeto, f->close);
  c->out = f->restore;
  if (f->after[0] && c->out) buf_adds(c->out, f->after);
}
static const char *w3_parent(W3 *c) { return c->sp > 0 && c->sp <= W3_MAXDEPTH ? c->st[c->sp - 1].tag : ""; }
/* Word 2003's list number codes (w:nfc) as 2006's names (w:numFmt) */
static const char *nfc_name(const char *v)
{
  static const char *const names[] = { "decimal", "upperRoman", "lowerRoman", "upperLetter", "lowerLetter", "ordinal", "cardinalText", "ordinalText" };
  int k = v ? atoi(v) : 0;
  if (k >= 0 && k < 8) return names[k];
  if (k == 22) return "decimalZero";
  if (k == 23) return "bullet";
  if (k == 255) return "none";
  return "decimal";
}
/* a w:pict: a picture (its w:binData, or the one an earlier w:pict brought, by v:imagedata's src)
   becomes a DrawingML picture in line, at the size the shape's style gives (else the picture's own at
   96 dpi); anything else (a text watermark, a VML shape) is copied as it is written */
static void w3_pict(W3 *c, Xml *x)
{
  const char *raw;
  size_t rl = xml_raw(x, &raw);
  Xml p;
  char src[96] = "", style[300] = "", bname[96] = "";
  Buf bin;
  int media = -1, k;
  i32 cx = 0, cy = 0;
  buf_init(&bin);
  xml_init(&p, raw, rl);
  while (xml_next(&p) != XT_EOF) {
    const char *v;
    if (p.type != XT_START) continue;
    if (!strcmp(p.name, "binData")) {
      int d = p.depth;
      v = xml_attr(&p, "name");
      snprintf(bname, sizeof bname, "%s", v ? v : "");
      while (xml_next(&p) != XT_EOF) { if (p.type == XT_END && p.depth < d) break; if (p.type == XT_TEXT) buf_add(&bin, p.text.data, p.text.len); }
    } else if (!strcmp(p.name, "imagedata") && (v = xml_attr(&p, "src"))) snprintf(src, sizeof src, "%s", v);
    else if (!strcmp(p.name, "shape") && (v = xml_attr(&p, "style")) && !style[0]) snprintf(style, sizeof style, "%s", v);
  }
  xml_free(&p);
  if (bin.len && !bin.oom) {
    /* the picture into the package as word/media/w2003-n.ext */
    u32 len = 0, pw = 0, ph = 0;
    u8 *data = b64_decode(bin.data, bin.len, &len);
    int t = data ? img_info(data, len, &pw, &ph) : IMG_NONE;
    if (data && c->nmedia >= c->mediacap && c->mediacap < 4096) {
      int nc = c->mediacap ? c->mediacap * 2 : 16;
      W3Media *nm = (W3Media *)realloc(c->media, (size_t)nc * sizeof *nm);
      if (nm) { c->media = nm; c->mediacap = nc; }
    }
    if (data && c->nmedia < c->mediacap) {
      W3Media *m = &c->media[c->nmedia];
      char name[64];
      snprintf(m->name, sizeof m->name, "%s", bname);
      snprintf(m->ext, sizeof m->ext, "%s", t == IMG_PNG ? "png" : t == IMG_JPEG ? "jpeg" : t == IMG_GIF ? "gif" : t == IMG_BMP ? "bmp" : "bin");
      m->pw = pw; m->ph = ph;
      snprintf(name, sizeof name, "word/media/w2003-%d.%s", c->nmedia + 1, m->ext);
      if (zip_add(&c->zw, name, data, len)) media = c->nmedia++;
    }
    free(data);
  }
  if (media < 0 && src[0]) for (k = 0; k < c->nmedia; k++) if (!strcmp(c->media[k].name, src)) media = k;
  if (media < 0) {
    if (bin.len || src[0]) c->lost = 1;
    else buf_add(c->out, raw, rl);          /* (not a picture: copied, for the watermark reader) */
    buf_free(&bin);
    return;
  }
  buf_free(&bin);
  {
    /* its size: the shape's width and height, else its pixels at 96 dpi */
    const char *s = style;
    while (*s) {
      const char *e = strchr(s, ';'), *colon = strchr(s, ':');
      size_t l = e ? (size_t)(e - s) : strlen(s);
      int okv = 0;
      while (*s == ' ' && l) { s++; l--; }
      if (colon && colon < s + l) {
        i32 tw = css_twips(colon + 1, (size_t)(s + l - colon - 1), &okv);
        if (okv && !strncmp(s, "width:", 6)) cx = tw;
        else if (okv && !strncmp(s, "height:", 7)) cy = tw;
      }
      s += l + (e ? 1 : 0);
    }
  }
  if (cx <= 0 || cy <= 0) { cx = (i32)c->media[media].pw * 15; cy = (i32)c->media[media].ph * 15; }
  if (cx <= 0 || cx > 31680) cx = cx <= 0 ? 1440 : 31680;
  if (cy <= 0 || cy > 31680) cy = cy <= 0 ? 1440 : 31680;
  {
    char id[24], target[48], ext[64], num[16];
    Buf *o = c->out;
    snprintf(target, sizeof target, "media/w2003-%d.%s", media + 1, c->media[media].ext);
    w3_rel(c, &c->mrels, "image", target, 0, id, sizeof id);
    snprintf(ext, sizeof ext, "cx=\"%ld\" cy=\"%ld\"", (long)cx * 635, (long)cy * 635);
    snprintf(num, sizeof num, "%d", c->nrel);
    buf_adds(o, "<w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\"><wp:extent ");
    buf_adds(o, ext);
    buf_adds(o, "/><wp:docPr id=\""); buf_adds(o, num); buf_adds(o, "\" name=\"Picture "); buf_adds(o, num);
    buf_adds(o, "\"/><a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:pic><pic:nvPicPr><pic:cNvPr id=\"0\" name=\"\"/><pic:cNvPicPr/></pic:nvPicPr>"
                "<pic:blipFill><a:blip r:embed=\"");
    buf_adds(o, id);
    buf_adds(o, "\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext ");
    buf_adds(o, ext);
    buf_adds(o, "/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing>");
  }
}
/* the text of an element's children (o:Title...), as one string: up to cap bytes */
static void w3_text(Xml *x, char *out, size_t cap)
{
  int d = x->depth;
  size_t k = 0;
  out[0] = 0;
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d) break;
    if (x->type == XT_TEXT) { size_t l = x->text.len < cap - 1 - k ? x->text.len : cap - 1 - k; memcpy(out + k, x->text.data, l); k += l; out[k] = 0; }
  }
}
/* o:DocumentProperties as docProps/core.xml */
static void w3_props(W3 *c, Xml *x)
{
  static const char *const from[] = { "Title", "Subject", "Author", "Keywords", "Description", "LastAuthor", "Category" };
  static const char *const to[] = { "dc:title", "dc:subject", "dc:creator", "cp:keywords", "dc:description", "cp:lastModifiedBy", "cp:category" };
  int d = x->depth, k;
  buf_adds(&c->core, XML_DECL "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
           "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">");
  while (xml_next(x) != XT_EOF) {
    char t[512];
    if (x->type == XT_END && x->depth < d) break;
    if (x->type != XT_START) continue;
    for (k = 0; k < 7 && strcmp(x->name, from[k]); k++) ;
    if (k == 7) { xml_skip(x); continue; }
    w3_text(x, t, sizeof t);
    if (!t[0]) continue;
    buf_addc(&c->core, '<'); buf_adds(&c->core, to[k]); buf_addc(&c->core, '>');
    xml_escape(&c->core, t, strlen(t));
    buf_adds(&c->core, "</"); buf_adds(&c->core, to[k]); buf_addc(&c->core, '>');
  }
  buf_adds(&c->core, "</cp:coreProperties>");
}
/* w:docPr: the settings Quill reads, into settings.xml (the rest, and the notes' separators, left out) */
static void w3_docpr(W3 *c, Xml *x)
{
  static const char *const keep[] = { "evenAndOddHeaders", "defaultTabStop", "autoHyphenation", "mirrorMargins", "gutterAtTop", "displayBackgroundShape", "doNotHyphenateCaps", NULL };
  int d = x->depth, k;
  Buf *o = c->out;
  c->out = &c->settings;
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d) break;
    if (x->type != XT_START) continue;
    for (k = 0; keep[k] && strcmp(keep[k], x->name); k++) ;
    if (keep[k]) { w3_open(c, x, x->name, 0); buf_adds(c->out, "</w:"); buf_adds(c->out, x->name); buf_addc(c->out, '>'); }
    xml_skip(x);
  }
  c->out = o;
}
/* aml:annotation: bookmarks, insertions and deletions, comments; other kinds left out */
static void w3_annotation(W3 *c, Xml *x)
{
  const char *t = xml_attr(x, "type"), *id = xml_attr(x, "id");
  Buf *o = c->out;
  if (!t) t = "";
  if (!id) id = "0";
  if (!strcmp(t, "Word.Bookmark.Start") || !strcmp(t, "Word.Bookmark.End")) {
    int start = t[14] == 'S';
    buf_adds(o, start ? "<w:bookmarkStart" : "<w:bookmarkEnd");
    w3_attr(o, "w:id", id);
    if (start) w3_attr(o, "w:name", xml_attr(x, "name"));
    buf_adds(o, "/>");
    xml_skip(x);
  } else if (!strcmp(t, "Word.Insertion") || !strcmp(t, "Word.Deletion")) {
    const char *a = xml_attr(x, "author"), *dt = xml_attr(x, "createdate");
    int ins = t[5] == 'I';
    buf_adds(o, ins ? "<w:ins" : "<w:del");
    w3_attr(o, "w:id", id);
    w3_attr(o, "w:author", a ? a : "");
    if (dt) w3_attr(o, "w:date", dt);
    buf_addc(o, '>');
    w3_push(c, o, "annotation", ins ? "</w:ins>" : "</w:del>", NULL, NULL);
  } else if (!strcmp(t, "Word.Comment.Start") || !strcmp(t, "Word.Comment.End")) {
    buf_adds(o, t[13] == 'S' ? "<w:commentRangeStart" : "<w:commentRangeEnd");
    w3_attr(o, "w:id", id);
    buf_adds(o, "/>");
    xml_skip(x);
  } else if (!strcmp(t, "Word.Comment")) {
    const char *a = xml_attr(x, "author"), *dt = xml_attr(x, "createdate"), *ini = xml_attr(x, "initials");
    int inrun = !strcmp(w3_parent(c), "r");
    if (!inrun) buf_adds(o, "<w:r>");
    buf_adds(o, "<w:commentReference"); w3_attr(o, "w:id", id); buf_adds(o, "/>");
    buf_adds(&c->comments, "<w:comment");
    w3_attr(&c->comments, "w:id", id);
    w3_attr(&c->comments, "w:author", a ? a : "");
    if (dt) w3_attr(&c->comments, "w:date", dt);
    if (ini) w3_attr(&c->comments, "w:initials", ini);
    buf_addc(&c->comments, '>');
    c->ncomment++;
    c->out = &c->comments;
    w3_push(c, o, "annotation", "</w:comment>", &c->comments, inrun ? NULL : "</w:r>");
  } else xml_skip(x);
}
static const char *const nocamel[] = { "pStyle", "rStyle", "tblStyle", "basedOn", "next", "link", "name", "styleLink", "listStyleLink", "lvlText", "aliases", "t", "instrText", NULL };
/* an element start inside one of the outputs */
static void w3_elem(W3 *c, Xml *x)
{
  char pre[16], tag[48];
  const char *n = x->name;
  Buf *o = c->out;
  int k;
  tag_prefix(x, pre, sizeof pre);
  if (!strcmp(pre, "o") || !strcmp(pre, "v") || !strcmp(pre, "w10") || !strcmp(pre, "sl") || !strcmp(pre, "dt")) { xml_skip(x); return; }
  if (!strcmp(pre, "aml") && !strcmp(n, "annotation")) { w3_annotation(c, x); return; }
  if (pre[0] && strcmp(pre, "w")) { w3_push(c, o, "", NULL, NULL, NULL); return; }   /* wx:sect, aml:content, smart tags: their content */
  if (!strcmp(n, "pict")) { w3_pict(c, x); return; }
  if (!strcmp(n, "binData") || !strcmp(n, "hmerge") || !strcmp(n, "docOleData") || !strcmp(n, "shapeDefaults") || !strcmp(n, "bgPict")) { xml_skip(x); return; }
  if (!strcmp(n, "footnote") || !strcmp(n, "endnote")) {
    int fn = n[0] == 'f';
    char id[16];
    Buf *to = fn ? &c->foot : &c->end;
    snprintf(id, sizeof id, "%d", fn ? ++c->nfoot : ++c->nend);
    buf_adds(o, fn ? "<w:footnoteReference" : "<w:endnoteReference"); w3_attr(o, "w:id", id); buf_adds(o, "/>");
    buf_adds(to, fn ? "<w:footnote" : "<w:endnote"); w3_attr(to, "w:id", id); buf_addc(to, '>');
    c->out = to;
    w3_push(c, o, n, fn ? "</w:footnote>" : "</w:endnote>", to, NULL);
    return;
  }
  if ((!strcmp(n, "hdr") || !strcmp(n, "ftr")) && c->nparts < W3_MAXPARTS) {
    int h = n[0] == 'h';
    const char *t = xml_attr(x, "type");
    char id[24], name[32];
    W3Part *p = &c->part[c->nparts++];
    snprintf(name, sizeof name, "%s%d.xml", h ? "header" : "footer", h ? ++c->nhdr : ++c->nftr);
    snprintf(p->name, sizeof p->name, "word/%s", name);
    buf_init(&p->b);
    w3_rel(c, &c->prels, h ? "header" : "footer", name, 0, id, sizeof id);
    buf_adds(o, h ? "<w:headerReference" : "<w:footerReference");
    w3_attr(o, "w:type", !t || !strcmp(t, "odd") ? "default" : t);
    w3_attr(o, "r:id", id);
    buf_adds(o, "/>");
    buf_adds(&p->b, XML_DECL);
    buf_adds(&p->b, h ? "<w:hdr" : "<w:ftr");
    buf_adds(&p->b, " xmlns:w=\"" NS_W "\" xmlns:r=\"" NS_R "\" xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\""
             " xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">");
    c->out = &p->b;
    w3_push(c, o, n, h ? "</w:hdr>" : "</w:ftr>", &p->b, NULL);
    return;
  }
  if (!strcmp(n, "hlink")) {
    const char *dest = xml_attr(x, "dest"), *bm = xml_attr(x, "bookmark");
    buf_adds(o, "<w:hyperlink");
    if (dest && dest[0]) { char id[24]; w3_rel(c, &c->mrels, "hyperlink", dest, 1, id, sizeof id); w3_attr(o, "r:id", id); }
    if (bm && bm[0]) w3_attr(o, "w:anchor", bm);
    buf_addc(o, '>');
    w3_push(c, o, "hyperlink", "</w:hyperlink>", NULL, NULL);
    return;
  }
  if (!strcmp(n, "nfc")) {
    buf_adds(o, "<w:numFmt"); w3_attr(o, "w:val", nfc_name(xml_attr(x, "val"))); buf_adds(o, "/>");
    xml_skip(x);
    return;
  }
  if (!strcmp(n, "vmerge")) {
    const char *v = xml_attr(x, "val");
    buf_adds(o, "<w:vMerge");
    if (v && !strcmp(v, "rest")) w3_attr(o, "w:val", "restart");
    buf_adds(o, "/>");
    xml_skip(x);
    return;
  }
  if (!strcmp(n, "listPr")) snprintf(tag, sizeof tag, "numPr");
  else if (!strcmp(n, "ilfo") && !strcmp(w3_parent(c), "numPr")) snprintf(tag, sizeof tag, "numId");
  else if (!strcmp(n, "listDef")) snprintf(tag, sizeof tag, "abstractNum");
  else if (!strcmp(n, "list") && o == &c->numbering && !strcmp(w3_parent(c), "")) snprintf(tag, sizeof tag, "num");
  else if (!strcmp(n, "ilst")) snprintf(tag, sizeof tag, "abstractNumId");
  else camel(n, tag, sizeof tag);
  for (k = 0; nocamel[k] && strcmp(nocamel[k], n); k++) ;
  w3_open(c, x, tag, !nocamel[k]);
  {
    char close[56];
    snprintf(close, sizeof close, "</w:%s>", tag);
    w3_push(c, o, tag, close, NULL, NULL);
  }
}
/* a Word 2003 XML document as the .docx package it stands for (malloc'd zip, *zlen its size); NULL if
   it has no body or memory ran out. *lost: pictures it couldn't take */
u8 *w2003_to_zip(const u8 *b, size_t n, size_t *zlen, int *lost)
{
  W3 *c = (W3 *)calloc(1, sizeof(W3));
  Xml x;
  Buf ct, t;
  int body = 0, k, ok = 1;
  u8 *out = NULL;
  if (!c) return NULL;
  buf_init(&c->doc); buf_init(&c->styles); buf_init(&c->numbering); buf_init(&c->settings); buf_init(&c->foot);
  buf_init(&c->end); buf_init(&c->comments); buf_init(&c->prels); buf_init(&c->mrels); buf_init(&c->core);
  zip_begin(&c->zw);
  xml_init(&x, (const char *)b, n);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_END) { w3_pop(c); continue; }
    if (x.type == XT_TEXT) { if (c->out) xml_escape(c->out, x.text.data, x.text.len); continue; }
    if (x.type != XT_START) continue;
    if (c->out) { w3_elem(c, &x); continue; }
    /* the document's own parts */
    if (!strcmp(x.name, "wordDocument")) w3_push(c, NULL, "", NULL, NULL, NULL);
    else if (!strcmp(x.name, "DocumentProperties")) w3_props(c, &x);
    else if (!strcmp(x.name, "fonts")) {
      int d = x.depth;
      while (xml_next(&x) != XT_EOF) {
        const char *f;
        if (x.type == XT_END && x.depth < d) break;
        if (x.type == XT_START && !strcmp(x.name, "defaultFonts") && (f = xml_attr(&x, "ascii"))) snprintf(c->deffont, sizeof c->deffont, "%s", f);
      }
    }
    else if (!strcmp(x.name, "lists")) { w3_push(c, NULL, "", NULL, NULL, NULL); c->out = &c->numbering; }
    else if (!strcmp(x.name, "styles")) {
      w3_push(c, NULL, "", NULL, NULL, NULL); c->out = &c->styles;
      if (c->deffont[0]) {
        buf_adds(&c->styles, "<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts");
        w3_attr(&c->styles, "w:ascii", c->deffont); w3_attr(&c->styles, "w:hAnsi", c->deffont);
        buf_adds(&c->styles, "/></w:rPr></w:rPrDefault></w:docDefaults>");
      }
    }
    else if (!strcmp(x.name, "docPr")) w3_docpr(c, &x);
    else if (!strcmp(x.name, "body")) { w3_push(c, NULL, "", NULL, NULL, NULL); c->out = &c->doc; body = 1; }
    else xml_skip(&x);
  }
  xml_free(&x);
  *lost = c->lost;
  /* the parts */
  buf_init(&ct); buf_init(&t);
  ct_begin(&ct);
#define NSDECL " xmlns:w=\"" NS_W "\" xmlns:r=\"" NS_R "\" xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\"" \
               " xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\""
#define PART(nm, type, root, body_) do { t.len = 0; buf_adds(&t, XML_DECL "<w:" root NSDECL ">"); buf_add(&t, (body_).data ? (body_).data : "", (body_).len); \
    buf_adds(&t, "</w:" root ">"); ok &= !t.oom && zip_add(&c->zw, nm, t.data, t.len); ct_override(&ct, nm, CT_W type); } while (0)
  {
    Buf body2;
    buf_init(&body2);
    buf_adds(&body2, "<w:body>"); buf_add(&body2, c->doc.data ? c->doc.data : "", c->doc.len); buf_adds(&body2, "</w:body>");
    PART("word/document.xml", "document.main+xml", "document", body2);
    buf_free(&body2);
  }
  PART("word/styles.xml", "styles+xml", "styles", c->styles);
  PART("word/numbering.xml", "numbering+xml", "numbering", c->numbering);
  PART("word/settings.xml", "settings+xml", "settings", c->settings);
  {
    char id[24];
    w3_rel(c, &c->prels, "styles", "styles.xml", 0, id, sizeof id);
    w3_rel(c, &c->prels, "numbering", "numbering.xml", 0, id, sizeof id);
    w3_rel(c, &c->prels, "settings", "settings.xml", 0, id, sizeof id);
    if (c->nfoot) {
      Buf f;
      buf_init(&f);
      buf_adds(&f, "<w:footnote w:type=\"separator\" w:id=\"-1\"><w:p><w:r><w:separator/></w:r></w:p></w:footnote>"
                   "<w:footnote w:type=\"continuationSeparator\" w:id=\"0\"><w:p><w:r><w:continuationSeparator/></w:r></w:p></w:footnote>");
      buf_add(&f, c->foot.data, c->foot.len);
      PART("word/footnotes.xml", "footnotes+xml", "footnotes", f);
      buf_free(&f);
      w3_rel(c, &c->prels, "footnotes", "footnotes.xml", 0, id, sizeof id);
    }
    if (c->nend) {
      Buf f;
      buf_init(&f);
      buf_adds(&f, "<w:endnote w:type=\"separator\" w:id=\"-1\"><w:p><w:r><w:separator/></w:r></w:p></w:endnote>"
                   "<w:endnote w:type=\"continuationSeparator\" w:id=\"0\"><w:p><w:r><w:continuationSeparator/></w:r></w:p></w:endnote>");
      buf_add(&f, c->end.data, c->end.len);
      PART("word/endnotes.xml", "endnotes+xml", "endnotes", f);
      buf_free(&f);
      w3_rel(c, &c->prels, "endnotes", "endnotes.xml", 0, id, sizeof id);
    }
    if (c->ncomment) {
      PART("word/comments.xml", "comments+xml", "comments", c->comments);
      w3_rel(c, &c->prels, "comments", "comments.xml", 0, id, sizeof id);
    }
  }
#undef PART
  for (k = 0; k < c->nparts; k++) {
    /* headers and footers, each with the pictures' and links' relationships */
    W3Part *p = &c->part[k];
    char rn[80];
    int h = !strncmp(p->name, "word/header", 11);
    buf_adds(&p->b, "");
    ok &= !p->b.oom && zip_add(&c->zw, p->name, p->b.data, p->b.len);
    ct_override(&ct, p->name, h ? CT_W "header+xml" : CT_W "footer+xml");
    snprintf(rn, sizeof rn, "word/_rels/%s.rels", p->name + 5);
    t.len = 0;
    buf_adds(&t, XML_DECL "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    buf_add(&t, c->mrels.data ? c->mrels.data : "", c->mrels.len);
    buf_adds(&t, "</Relationships>");
    ok &= !t.oom && zip_add(&c->zw, rn, t.data, t.len);
  }
  t.len = 0;
  buf_adds(&t, XML_DECL "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
  buf_add(&t, c->prels.data ? c->prels.data : "", c->prels.len);
  buf_add(&t, c->mrels.data ? c->mrels.data : "", c->mrels.len);
  buf_adds(&t, "</Relationships>");
  ok &= !t.oom && zip_add(&c->zw, "word/_rels/document.xml.rels", t.data, t.len);
  t.len = 0;
  buf_adds(&t, XML_DECL "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
           "<Relationship Id=\"rId1\" Type=\"" RT "officeDocument\" Target=\"word/document.xml\"/>");
  if (c->core.len) {
    buf_adds(&t, "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>");
    ok &= zip_add(&c->zw, "docProps/core.xml", c->core.data, c->core.len);
    ct_override(&ct, "docProps/core.xml", "application/vnd.openxmlformats-package.core-properties+xml");
  }
  buf_adds(&t, "</Relationships>");
  ok &= !t.oom && zip_add(&c->zw, "_rels/.rels", t.data, t.len);
  buf_adds(&ct, "</Types>");
  ok &= !ct.oom && zip_add(&c->zw, "[Content_Types].xml", ct.data, ct.len);
  if (ok && body && !c->doc.oom && !c->styles.oom && !c->numbering.oom && !c->foot.oom && !c->end.oom && !c->comments.oom && zip_finish(&c->zw)) {
    out = (u8 *)c->zw.out.data; *zlen = c->zw.out.len;
    c->zw.out.data = NULL; c->zw.out.len = 0;
  }
  zip_free(&c->zw);
  buf_free(&ct); buf_free(&t);
  buf_free(&c->doc); buf_free(&c->styles); buf_free(&c->numbering); buf_free(&c->settings); buf_free(&c->foot);
  buf_free(&c->end); buf_free(&c->comments); buf_free(&c->prels); buf_free(&c->mrels); buf_free(&c->core);
  for (k = 0; k < c->nparts; k++) buf_free(&c->part[k].b);
  free(c->media);
  free(c);
  return out;
}

/* ------------------------------------------------------------------ the format */
static Doc *load_wordxml(const u8 *b, size_t n, unsigned *lost, char *err, size_t errcap)
{
  int kind = wordxml_kind(b, n), plost = 0;
  size_t zn = 0;
  u8 *z = kind == 1 ? flat_to_zip(b, n, &zn) : kind == 2 ? w2003_to_zip(b, n, &zn, &plost) : NULL;
  Doc *d;
  if (!z) { snprintf(err, errcap, kind ? "This Word XML document is damaged, or there is not enough memory to read it." : "This is not a Word XML document."); return NULL; }
  d = docx_load(z, zn, lost, err, errcap);
  free(z);
  if (d && plost && lost) *lost |= LOST_PICTURES;
  return d;
}
/* written as Flat OPC (what Word 2007 and later write for "Word XML Document") */
static u8 *save_wordxml(Doc *d, size_t *len)
{
  size_t zn = 0;
  u8 old = d->tplkind, *z, *f;
  if (old == TK_DOCM) d->tplkind = TK_DOC;       /* (macros aren't written to a Word XML file) */
  z = docx_save(d, &zn);
  d->tplkind = old;
  if (!z) return NULL;
  f = zip_to_flat(z, zn, len);
  free(z);
  return f;
}
const DocFormat fmt_wordxml = { "Word XML Document", "xml", sniff_wordxml, load_wordxml, save_wordxml, DF_NATIVE };
