/* blocks.c - see blocks.h (WP23) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blocks.h"
#include "docx.h"
#include "keep.h"
#include "zip.h"
#include "info.h"
#include "doctheme.h"

#define GLOSS_TYPE "http://schemas.openxmlformats.org/officeDocument/2006/relationships/glossaryDocument"
#define GLOSS_CT   "application/vnd.openxmlformats-officedocument.wordprocessingml.document.glossary+xml"
#define STYLES_CT  "application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"
static const char NS[] = "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
                         "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
                         "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" "
                         "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\"";
static const char HDR[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n";

const char *blocks_main_ctype(const Doc *d)
{
  if (d->tplkind == TK_DOTM) return "application/vnd.ms-word.template.macroEnabledTemplate.main+xml";
  if (d->tplkind == TK_DOCM) return "application/vnd.ms-word.document.macroEnabled.main+xml";
  if (d->tplkind == TK_DOTX) return "application/vnd.openxmlformats-officedocument.wordprocessingml.template.main+xml";
  return "application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml";
}

static const char *const gwords[GAL_N] = { "docParts", "autoTxt", "coverPg", "hdrs", "ftrs", "pgNum", "txtBox", "tbls", "watermarks", "eq", "" };
static const char *const glabels[GAL_N] = { "Quick Parts", "AutoText", "Cover Pages", "Headers", "Footers", "Page Numbers", "Text Boxes", "Tables",
                                            "Watermarks", "Equations", "Other" };
const char *gallery_word(int g) { return g >= 0 && g < GAL_OTHER ? gwords[g] : "docParts"; }
const char *gallery_label(int g) { return g >= 0 && g < GAL_N ? glabels[g] : glabels[GAL_OTHER]; }
int gallery_of(const char *w)
{
  int g;
  if (!w) return GAL_OTHER;
  for (g = 0; g < GAL_OTHER; g++) if (!strcmp(w, gwords[g])) return g;
  if (!strcmp(w, "custQuickParts")) return GAL_QUICKPARTS;
  if (!strcmp(w, "custAutoTxt")) return GAL_AUTOTEXT;
  if (!strncmp(w, "pgNum", 5)) return GAL_PAGENUMBERS;
  return GAL_OTHER;
}

/* ---- the glossary read */
static void cpy(char *dst, const char *src, size_t cap) { if (src) { strncpy(dst, src, cap - 1); dst[cap - 1] = 0; } else dst[0] = 0; }
static int gloss_part(const Doc *d)
{
  int i;
  for (i = 0; i < d->nkept; i++) {
    const char *t = d->kept[i].reltype;
    size_t n = t ? strlen(t) : 0;
    if (n >= 17 && !strcmp(t + n - 17, "/glossaryDocument")) return i;
  }
  return -1;
}
/* a block's properties from its w:docPart */
static void block_props(Block *b)
{
  Xml x;
  int incat = 0, inpr = 0;
  xml_init(&x, b->xml, b->len);
  strcpy(b->gallery, "docParts"); strcpy(b->category, "General");
  while (xml_next(&x) != XT_EOF) {
    const char *v;
    if (x.type == XT_END) {
      if (!strcmp(x.name, "category")) incat = 0;
      else if (!strcmp(x.name, "docPartPr")) inpr = 0;
      continue;
    }
    if (x.type != XT_START) continue;
    v = xml_attr(&x, "val");
    if (!strcmp(x.name, "docPartBody")) break;
    if (!strcmp(x.name, "docPartPr")) inpr = 1;
    else if (!inpr) continue;
    else if (!strcmp(x.name, "category")) incat = 1;
    else if (!strcmp(x.name, "name")) cpy(incat ? b->category : b->name, v, incat ? sizeof b->category : sizeof b->name);
    else if (!strcmp(x.name, "gallery")) cpy(b->gallery, v, sizeof b->gallery);
    else if (!strcmp(x.name, "description")) cpy(b->desc, v, sizeof b->desc);
    else if (!strcmp(x.name, "behavior") && v) b->behavior = (u8)(!strcmp(v, "p") ? BB_PARA : !strcmp(v, "pg") ? BB_PAGE : BB_CONTENT);
  }
  xml_free(&x);
}
static void gloss_free(Glossary *g)
{
  int i;
  if (!g) return;
  for (i = 0; i < g->n; i++) free(g->b[i].xml);
  free(g->b); free(g->head); free(g->tail);
  free(g);
}
void blocks_free(Doc *d)
{
  if (!d) return;
  gloss_free(d->gloss); d->gloss = NULL;
  free(d->attached); d->attached = NULL;
}
static int add_block(Glossary *g, const char *xml, size_t len)
{
  Block *nb = (Block *)realloc(g->b, (size_t)(g->n + 1) * sizeof(Block)), *b;
  if (!nb) return -1;
  g->b = nb;
  b = &g->b[g->n];
  memset(b, 0, sizeof *b);
  if (!(b->xml = str_ndup(xml, len))) return -1;
  b->len = len;
  block_props(b);
  return g->n++;
}
void blocks_read(Doc *d)
{
  int k = gloss_part(d);
  const char *s;
  size_t n;
  Xml x;
  Glossary *g;
  if (k < 0 || d->gloss) return;
  s = (const char *)d->kept[k].data; n = d->kept[k].len;
  if (!(g = (Glossary *)calloc(1, sizeof *g))) return;
  xml_init(&x, s, n);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START && !strcmp(x.name, "docParts")) {
      const char *gt = strchr(s + x.tok, '>');
      size_t hend = gt ? (size_t)(gt - s) + 1 : x.tok;
      int d0 = x.depth;
      if (x.pending_end) {
        /* <w:docParts/>: the head up to it, written open; the tail after it */
        g->head = (char *)malloc(x.tok + 40);
        if (g->head) { memcpy(g->head, s, x.tok); strcpy(g->head + x.tok, "<w:docParts>"); }
        g->tail = (char *)malloc(n - hend + 20);
        if (g->tail) { strcpy(g->tail, "</w:docParts>"); memcpy(g->tail + 13, s + hend, n - hend); g->tail[13 + n - hend] = 0; }
        break;
      }
      g->head = str_ndup(s, hend);
      while (xml_next(&x) != XT_EOF) {
        if (x.type == XT_END && x.depth < d0) {
          /* the closing tag: from its '<' on is the tail */
          size_t e = x.i;
          while (e > 0 && s[e - 1] != '<') e--;
          if (e > 0) e--;
          g->tail = str_ndup(s + e, n - e);
          break;
        }
        if (x.type == XT_START && !strcmp(x.name, "docPart")) {
          const char *st;
          size_t len = xml_raw(&x, &st);
          add_block(g, st, len);
        }
      }
      break;
    }
  }
  xml_free(&x);
  if (!g->head || !g->tail) { gloss_free(g); return; }
  d->gloss = g;
}
/* the glossary document's folder ("word/glossary/") */
static void gloss_dir(const Doc *d, char *out, size_t cap)
{
  int k = gloss_part(d);
  const char *nm = k >= 0 ? d->kept[k].name : "word/glossary/document.xml", *sl = strrchr(nm, '/');
  size_t n = sl ? (size_t)(sl - nm) + 1 : 0;
  if (n >= cap) n = cap - 1;
  memcpy(out, nm, n); out[n] = 0;
}
int blocks_store(Doc *d)
{
  Buf b;
  int i, k;
  Glossary *g = d->gloss;
  if (!g) return 0;
  buf_init(&b);
  if (g->head) buf_adds(&b, g->head);
  else { buf_adds(&b, HDR); buf_adds(&b, "<w:glossaryDocument "); buf_adds(&b, NS); buf_adds(&b, "><w:docParts>"); }
  for (i = 0; i < g->n; i++) buf_add(&b, g->b[i].xml, g->b[i].len);
  buf_adds(&b, g->tail ? g->tail : "</w:docParts></w:glossaryDocument>");
  if (b.oom) { buf_free(&b); return 0; }
  if ((k = gloss_part(d)) >= 0) {
    KeptPart *kp = &d->kept[k];
    free(kp->data);
    kp->len = b.len;
    kp->data = (u8 *)buf_take(&b);
  } else {
    k = keep_add(d, "word/glossary/document.xml", GLOSS_CT, "word/document.xml", GLOSS_TYPE, "rIdStGloss", b.data, b.len);
    buf_free(&b);
    if (k < 0) return 0;
  }
  d->dirty = 1;
  return 1;
}

/* ---- settings */
static char tpl_rid[40];                  /* (while reading) w:attachedTemplate's relationship */
void blocks_read_setting(Doc *d, Xml *x)
{
  const char *v = xml_attr(x, "val");
  if (!strcmp(x->name, "attachedTemplate")) cpy(tpl_rid, xml_attr(x, "id"), sizeof tpl_rid);
  else if (!strcmp(x->name, "linkStyles")) { const char *o = v; d->linkstyles = (u8)(!o || !(!strcmp(o, "0") || !strcmp(o, "false") || !strcmp(o, "off"))); }
  else if (!strcmp(x->name, "stylePaneFormatFilter") && v) d->spfilter = (u16)strtoul(v, NULL, 16);
  else if (!strcmp(x->name, "stylePaneSortMethod") && v) {
    static const char *const names[] = { "name", "priority", "font", "basedOn", "type", "default" };
    int i;
    for (i = 0; i < 6; i++) if (!strcmp(v, names[i]) || (strlen(v) == 4 && strtoul(v, NULL, 16) == (unsigned long)i)) d->spsort = (u8)(SPS_NAME + i);
  }
}
void blocks_read_parts(Doc *d, const char *ct, size_t ctn, const char *setrels, size_t srn)
{
  /* a template: the main part's content type */
  if (ct) {
    const char *t = strstr(ct, "macroEnabledTemplate.main+xml"), *e = ct + ctn;
    if (t && t < e) d->tplkind = TK_DOTM;
    else if ((t = strstr(ct, "wordprocessingml.template.main+xml")) && t < e) d->tplkind = TK_DOTX;
    else if ((t = strstr(ct, "document.macroEnabled.main+xml")) && t < e) d->tplkind = TK_DOCM;
  }
  if (tpl_rid[0] && setrels) {
    Xml x;
    xml_init(&x, setrels, srn);
    while (xml_next(&x) != XT_EOF) {
      const char *id, *tg;
      if (x.type == XT_START && !strcmp(x.name, "Relationship") && (id = xml_attr(&x, "Id")) && !strcmp(id, tpl_rid) && (tg = xml_attr(&x, "Target"))) {
        free(d->attached);
        d->attached = str_dup(tg);
        break;
      }
    }
    xml_free(&x);
  }
  tpl_rid[0] = 0;
  blocks_read(d);
}
void blocks_write_settings(Doc *d, Buf *b)
{
  if (d->attached && d->attached[0]) buf_adds(b, "<w:attachedTemplate xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" r:id=\"rIdStTpl\"/>");
  if (d->linkstyles) buf_adds(b, "<w:linkStyles/>");
  if (d->spfilter) buf_printf(b, "<w:stylePaneFormatFilter w:val=\"%04X\"/>", (unsigned)d->spfilter);
  if (d->spsort > SPS_NONE && d->spsort < SPS_N) buf_printf(b, "<w:stylePaneSortMethod w:val=\"%04X\"/>", (unsigned)(d->spsort - SPS_NAME));
}

/* ---- building blocks */
int blocks_find(const Doc *d, const char *name, int gallery)
{
  int i;
  if (!d->gloss) return -1;
  for (i = 0; i < d->gloss->n; i++)
    if (str_ieq(d->gloss->b[i].name, name) && (gallery < 0 || gallery_of(d->gloss->b[i].gallery) == gallery)) return i;
  return -1;
}
/* a part of a zip (malloc'd, NUL-terminated) */
static char *zpart(const u8 *z, size_t zn, const char *name, size_t *n)
{
  ZipReader r;
  char *s = NULL;
  int i;
  if (!zip_open(&r, z, zn)) return NULL;
  if ((i = zip_find(&r, name)) >= 0) s = (char *)zip_read(&r, i, n);
  zip_close(&r);
  return s;
}
static void attr_esc(Buf *b, const char *s) { xml_escape(b, s, strlen(s)); }
static u32 guid_seed = 0x5A17C0DEUL;
/* the w:docPart for a block with this body (its paragraphs' XML) */
static char *docpart_xml(const char *name, int gallery, const char *category, const char *desc, int behavior, const char *body, size_t bl, size_t *len)
{
  static const char *const beh[3] = { "content", "p", "pg" };
  Buf b;
  u32 h = guid_seed;
  const char *p;
  for (p = name; *p; p++) h = h * 31 + (u8)*p;
  guid_seed = h * 1103515245UL + 12345UL;
  buf_init(&b);
  buf_adds(&b, "<w:docPart><w:docPartPr><w:name w:val=\""); attr_esc(&b, name);
  buf_adds(&b, "\"/><w:category><w:name w:val=\""); attr_esc(&b, category && category[0] ? category : "General");
  buf_adds(&b, "\"/><w:gallery w:val=\""); attr_esc(&b, gallery_word(gallery));
  buf_printf(&b, "\"/></w:category><w:behaviors><w:behavior w:val=\"%s\"/></w:behaviors>", beh[behavior >= 0 && behavior < 3 ? behavior : 0]);
  if (desc && desc[0]) { buf_adds(&b, "<w:description w:val=\""); attr_esc(&b, desc); buf_adds(&b, "\"/>"); }
  buf_printf(&b, "<w:guid w:val=\"{%08lX-%04X-4%03X-A%03X-%08lX%04X}\"/></w:docPartPr><w:docPartBody ", (unsigned long)h, (unsigned)(guid_seed & 0xFFFF),
             (unsigned)((h >> 4) & 0xFFF), (unsigned)((guid_seed >> 8) & 0xFFF), (unsigned long)(h ^ guid_seed), (unsigned)((h >> 16) & 0xFFFF));
  buf_adds(&b, NS); buf_adds(&b, ">");
  buf_add(&b, body, bl);
  buf_adds(&b, "</w:docPartBody></w:docPart>");
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return buf_take(&b);
}
/* the text a..b of src as a document of its own, with only what a block can keep: no pictures, notes,
   comment marks, links or lists. NULL if empty or out of memory */
static Doc *plain_copy(Doc *src, Pos a, Pos b)
{
  Fragment *f = doc_fragment(src, a, b);
  Doc *t;
  Pos at;
  u32 i;
  if (!f) return NULL;
  if (!(t = doc_new())) { fragment_free(f); return NULL; }
  at = doc_start(t);
  doc_paste(t, &at, f, PASTE_KEEP);
  fragment_free(f);
  for (i = t->np; i-- > 0;) {
    Para *p = t->p[i];
    u32 o = p->len;
    while (o > 0) {
      u32 s = (u32)utf8_prev(p->t, o);
      const u8 *c = (const u8 *)p->t + s;
      if (o - s == 4 && (c[0] == 0xF3 || c[0] == 0xF4)) {
        Pos x, y;
        x.para = y.para = i; x.off = s; y.off = o;
        doc_delete(t, x, y);
        p = t->p[i];
      }
      o = s;
    }
  }
  {
    CharFmt z;
    ParaFmt pz;
    memset(&z, 0, sizeof z); memset(&pz, 0, sizeof pz);
    doc_format(t, doc_start(t), doc_end_pos(t), CM_LINK, &z);
    doc_para_format(t, 0, t->np - 1, PM_LIST, &pz);
  }
  return t;
}
int blocks_add(Doc *d, Doc *src, Pos a, Pos b, const char *name, int gallery, const char *category, const char *desc, int behavior)
{
  Doc *t;
  u8 *z;
  size_t zn, n = 0, sn = 0, pl;
  char *docxml, *stxml = NULL, *body, *bend, *sect, *part;
  int k, old;
  if (!name || !name[0] || !(t = plain_copy(src, a, b))) return -1;
  z = docx_save(t, &zn);
  doc_free(t);
  if (!z) return -1;
  docxml = zpart(z, zn, "word/document.xml", &n);
  if (!d->gloss) stxml = zpart(z, zn, "word/styles.xml", &sn);
  free(z);
  if (!docxml || !(body = strstr(docxml, "<w:body>")) || !(bend = strstr(body, "</w:body>"))) { free(docxml); free(stxml); return -1; }
  body += 8;
  /* the section's settings at the body's end are the document's, not the block's */
  for (sect = strstr(body, "<w:sectPr"); sect && sect < bend; ) {
    char *nx = strstr(sect + 1, "<w:sectPr");
    if (!nx || nx >= bend) break;
    sect = nx;
  }
  if (sect && sect < bend) bend = sect;
  part = docpart_xml(name, gallery, category, desc, behavior, body, (size_t)(bend - body), &pl);
  free(docxml);
  if (!part) { free(stxml); return -1; }
  if (!d->gloss) {
    /* a glossary of its own, with the document's styles */
    Glossary *g = (Glossary *)calloc(1, sizeof *g);
    static const char rels[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
      "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" "
      "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/></Relationships>";
    if (!g) { free(part); free(stxml); return -1; }
    d->gloss = g;
    if (gloss_part(d) < 0) {
      if (stxml) keep_add(d, "word/glossary/styles.xml", STYLES_CT, NULL, NULL, NULL, stxml, sn);
      keep_add(d, "word/glossary/_rels/document.xml.rels", NULL, NULL, NULL, NULL, rels, sizeof rels - 1);
    }
  }
  free(stxml);
  old = blocks_find(d, name, gallery);
  if (old >= 0) {
    free(d->gloss->b[old].xml);
    d->gloss->b[old].xml = part; d->gloss->b[old].len = pl;
    memset(d->gloss->b[old].desc, 0, sizeof d->gloss->b[old].desc);
    d->gloss->b[old].behavior = 0;
    block_props(&d->gloss->b[old]);
    k = old;
  } else {
    k = add_block(d->gloss, part, pl);
    free(part);
  }
  if (k < 0 || !blocks_store(d)) return -1;
  return k;
}
int blocks_delete(Doc *d, int i)
{
  Glossary *g = d->gloss;
  if (!g || i < 0 || i >= g->n) return 0;
  free(g->b[i].xml);
  memmove(&g->b[i], &g->b[i + 1], (size_t)(g->n - i - 1) * sizeof(Block));
  g->n--;
  return blocks_store(d);
}
int blocks_set(Doc *d, int i, const char *name, int gallery, const char *category, const char *desc, int behavior)
{
  Glossary *g = d->gloss;
  Block *b;
  const char *s, *body, *bend;
  char *nx, *bx;
  size_t nl;
  if (!g || i < 0 || i >= g->n) return 0;
  b = &g->b[i];
  /* the block written afresh round its body (everything else in its w:docPartPr is Word's: kept by
     changing only what Quill knows) */
  s = b->xml;
  if (!(body = strstr(s, "docPartBody"))) return 0;
  while (body > s && *body != '<') body--;
  bend = s + b->len;
  {
    /* the body's inside: after its start tag, before its closing tag */
    const char *in = strchr(body, '>'), *close = bend;
    if (!in) return 0;
    if (in[-1] == '/') { in = close = in + 1; }
    else {
      in++;
      while (close > in && strncmp(close, "</", 2)) close--;      /* </w:docPart> */
      if (close > in) { close--; while (close > in && strncmp(close, "</", 2)) close--; }   /* </w:docPartBody> */
    }
    if (!(bx = str_ndup(in, (size_t)(close - in)))) return 0;
  }
  nx = docpart_xml(name ? name : b->name, gallery >= 0 ? gallery : gallery_of(b->gallery), category ? category : b->category, desc ? desc : b->desc,
                   behavior >= 0 ? behavior : b->behavior, bx, strlen(bx), &nl);
  free(bx);
  if (!nx) return 0;
  free(b->xml);
  b->xml = nx; b->len = nl;
  memset(b->desc, 0, sizeof b->desc); b->behavior = 0;
  block_props(b);
  return blocks_store(d);
}
Doc *blocks_doc(const Doc *owner, int i)
{
  const Glossary *g = owner->gloss;
  const Block *b;
  ZipWriter z;
  Buf docx, ct, rels;
  char dir[200], mainname[260];
  const char *body, *in, *close, *root, *rootend;
  int k, ok, gp;
  Doc *res = NULL;
  unsigned lost;
  char err[200];
  if (!g || i < 0 || i >= g->n) return NULL;
  b = &g->b[i];
  gp = gloss_part(owner);
  gloss_dir(owner, dir, sizeof dir);
  snprintf(mainname, sizeof mainname, "%s", gp >= 0 ? owner->kept[gp].name : "word/glossary/document.xml");
  /* the body's inside */
  if (!(body = strstr(b->xml, "docPartBody"))) return NULL;
  while (body > b->xml && *body != '<') body--;
  if (!(in = strchr(body, '>'))) return NULL;
  close = b->xml + b->len;
  if (in[-1] == '/') close = in + 1;
  else {
    while (close > in && strncmp(close, "</", 2)) close--;
    if (close > in) { close--; while (close > in && strncmp(close, "</", 2)) close--; }
  }
  in++;
  if (close < in) close = in;
  /* a document with the glossary root's namespaces round it */
  buf_init(&docx);
  buf_adds(&docx, HDR);
  buf_adds(&docx, "<w:document ");
  root = g->head ? strstr(g->head, "glossaryDocument") : NULL;
  rootend = root ? strchr(root, '>') : NULL;
  if (root && rootend) { root += 16; buf_add(&docx, root, (size_t)(rootend - root)); if (!strstr(g->head, "xmlns:a=")) buf_adds(&docx, " xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\""); }
  else buf_adds(&docx, NS);
  buf_adds(&docx, "><w:body>");
  buf_add(&docx, in, (size_t)(close - in));
  buf_adds(&docx, "</w:body></w:document>");
  buf_init(&ct); buf_init(&rels);
  buf_adds(&ct, HDR);
  buf_adds(&ct, "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                "<Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/");
  xml_escape(&ct, mainname, strlen(mainname));
  buf_adds(&ct, "\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>");
  zip_begin(&z);
  ok = !docx.oom;
  for (k = 0; k < owner->nkept && ok; k++) {
    const KeptPart *kp = &owner->kept[k];
    if (k == gp || strncmp(kp->name, dir, strlen(dir))) continue;
    if (kp->ctype) {
      buf_adds(&ct, "<Override PartName=\"/"); xml_escape(&ct, kp->name, strlen(kp->name));
      buf_adds(&ct, "\" ContentType=\""); xml_escape(&ct, kp->ctype, strlen(kp->ctype)); buf_adds(&ct, "\"/>");
    }
    ok = zip_add(&z, kp->name, kp->data, kp->len);
  }
  buf_adds(&ct, "</Types>");
  buf_adds(&rels, HDR);
  buf_adds(&rels, "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" "
                  "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"");
  xml_escape(&rels, mainname, strlen(mainname));
  buf_adds(&rels, "\"/></Relationships>");
  ok = ok && !ct.oom && !rels.oom && zip_add(&z, "[Content_Types].xml", ct.data, ct.len) && zip_add(&z, "_rels/.rels", rels.data, rels.len) &&
       zip_add(&z, mainname, docx.data, docx.len) && zip_finish(&z);
  if (ok) res = docx_load((const u8 *)z.out.data, z.out.len, &lost, err, sizeof err);
  zip_free(&z);
  buf_free(&docx); buf_free(&ct); buf_free(&rels);
  if (res) blocks_free(res);
  return res;
}
/* f pasted at *at as paragraphs of their own (para) or in the text */
static int paste_doc(Doc *d, Pos *at, Doc *src, int behavior)
{
  Fragment *f;
  int ok;
  if (!(f = doc_fragment(src, doc_start(src), doc_end_pos(src)))) return 0;
  if (behavior != BB_CONTENT && at->off > 0) doc_insert(d, at, "\n", 1, NULL);
  if (behavior == BB_PAGE) {
    ParaFmt pf;
    memset(&pf, 0, sizeof pf); pf.pagebreak = 1;
    doc_para_format(d, at->para, at->para, PM_BREAK, &pf);
  }
  ok = doc_paste(d, at, f, PASTE_KEEP);
  if (ok && behavior != BB_CONTENT) doc_insert(d, at, "\n", 1, NULL);
  fragment_free(f);
  return ok;
}
int blocks_insert(Doc *d, Pos *at, const Doc *owner, int i)
{
  Doc *b = blocks_doc(owner, i);
  int ok;
  if (!b) return 0;
  doc_begin(d, *at);
  ok = paste_doc(d, at, b, owner->gloss->b[i].behavior);
  doc_end(d, *at);
  doc_free(b);
  return ok;
}

/* ---- templates */
void template_to_new(Doc *d, const char *path)
{
  int k;
  d->tplkind = TK_DOC;
  /* the template's building blocks stay in the template */
  gloss_free(d->gloss); d->gloss = NULL;
  while ((k = gloss_part(d)) >= 0) {
    char dir[200], nm[260];
    int j;
    gloss_dir(d, dir, sizeof dir);
    snprintf(nm, sizeof nm, "%s", d->kept[k].name);
    keep_remove(d, nm);
    for (j = d->nkept; j-- > 0;) if (j < d->nkept && !strncmp(d->kept[j].name, dir, strlen(dir))) { snprintf(nm, sizeof nm, "%s", d->kept[j].name); keep_remove(d, nm); }
  }
  doc_attach_template(d, path);
  doc_clear_undo(d);
  d->dirty = 0;
}
int template_save(Doc *d, const char *path, int kind)
{
  u8 old = d->tplkind, *data;
  size_t n;
  int ok;
  d->tplkind = (u8)(kind == TK_DOTM ? TK_DOTM : TK_DOTX);
  data = docx_save(d, &n);
  d->tplkind = old;
  if (!data) return 0;
  ok = file_write(path, data, n);
  free(data);
  return ok;
}
void doc_attach_template(Doc *d, const char *path)
{
  const char *b;
  free(d->attached); d->attached = NULL;
  if (path && path[0]) d->attached = str_dup(path);
  b = path ? path : "";
  { const char *s1 = strrchr(b, '/'), *s2 = strrchr(b, ':'), *s3 = strrchr(b, '\\');
    if (s2 > s1) s1 = s2;
    if (s3 > s1) s1 = s3;
    if (s1) b = s1 + 1; }
  info_set(d, IP_TEMPLATE, b[0] ? b : "Normal.dotm");
  d->dirty = 1;
}
/* src's style k as d's: fonts and lists made d's (based, next and link left for later) */
static void style_for(Doc *d, const Doc *src, int k, Style *s)
{
  const Style *o = &src->styles[k];
  *s = *o;
  s->based = NO_STYLE; s->next = 0; s->x.link = 0;
  if (o->ocf.font < src->nfonts) s->ocf.font = (u8)doc_font(d, src->fonts[o->ocf.font]);
  if (o->cf.font < src->nfonts) s->cf.font = (u8)doc_font(d, src->fonts[o->cf.font]);
  if ((s->pmask & PM_LIST) && o->opf.list && o->opf.list <= src->nlists) {
    int l = doc_add_list(d, &src->lists[o->opf.list - 1]);
    if (l) s->opf.list = (u16)l; else s->pmask &= ~(unsigned)PM_LIST;
  } else s->pmask &= ~(unsigned)PM_LIST;
}
static int wanted(const char *name, const char *const *names, int n)
{
  int i;
  if (!names) return 1;
  for (i = 0; i < n; i++) if (str_ieq(names[i], name)) return 1;
  return 0;
}
int doc_copy_styles(Doc *d, const Doc *src, const char *const *names, int n)
{
  int k, done = 0, map[MAX_STYLES];
  Pos at;
  at.para = 0; at.off = 0;
  doc_begin(d, at);
  for (k = 0; k < src->nstyles; k++) {
    Style s;
    int dk;
    map[k] = -1;
    if ((src->styles[k].type != ST_PARA && src->styles[k].type != ST_CHAR) || !wanted(src->styles[k].name, names, n)) continue;
    style_for(d, src, k, &s);
    dk = k == 0 ? 0 : doc_style_by_name(d, s.name);
    if (dk >= 0 && d->styles[dk].type != s.type) continue;
    if (dk >= 0) { strcpy(s.id, d->styles[dk].id); if (dk > 0 && d->styles[dk].based != NO_STYLE) s.based = d->styles[dk].based; }
    else if (doc_style(d, s.id) >= 0) s.id[0] = 0;
    if (dk == 0) s.based = NO_STYLE;
    map[k] = doc_set_style(d, dk, &s);
    if (map[k] >= 0) done++;
  }
  /* bases, next styles and links by name */
  for (k = 0; k < src->nstyles; k++) {
    const Style *o = &src->styles[k];
    Style s;
    int b;
    if (map[k] <= 0) continue;
    s = d->styles[map[k]];
    b = o->based != NO_STYLE && o->based < src->nstyles ? doc_style_by_name(d, src->styles[o->based].name) : -1;
    if (o->based == 0) b = 0;
    s.based = (u8)(b >= 0 && b != map[k] && d->styles[b].type == s.type ? b : (s.type == ST_PARA ? 0 : NO_STYLE));
    if (s.type == ST_PARA && o->next < src->nstyles) { int nx = doc_style_by_name(d, src->styles[o->next].name); s.next = (u8)(nx >= 0 ? nx : map[k]); }
    if (o->x.link && o->x.link - 1 < src->nstyles) { int lk = doc_style_by_name(d, src->styles[o->x.link - 1].name); s.x.link = (u8)(lk >= 0 ? lk + 1 : 0); }
    /* what it sets itself stays what the source's says: the effective formatting comes from the new base */
    doc_set_style(d, map[k], &s);
  }
  doc_end(d, at);
  return done;
}

/* ---- another file */
int doc_insert_file(Doc *d, Pos *at, const char *path, const char *bookmark, char *err, size_t errcap)
{
  unsigned lost = 0;
  Doc *src = doc_load_file(path, &lost, err, errcap);
  Fragment *f;
  Pos a, b;
  int ok;
  if (!src) return 0;
  a = doc_start(src); b = doc_end_pos(src);
  if (bookmark && bookmark[0] && !doc_bmark_find(src, bookmark, &a, &b)) {
    snprintf(err, errcap, "The bookmark %s isn't in the file.", bookmark);
    doc_free(src);
    return 0;
  }
  f = doc_fragment(src, a, b);
  doc_free(src);
  if (!f) { snprintf(err, errcap, "The file is empty."); return 0; }
  doc_begin(d, *at);
  ok = doc_paste(d, at, f, PASTE_KEEP);
  doc_end(d, *at);
  fragment_free(f);
  if (!ok) snprintf(err, errcap, "Not enough memory.");
  return ok;
}

/* ---- the file formats (docx.c's doc_formats[]): Word templates, read as .docx (the content type
   says it is a template) and written with a template's content type */
static Doc *load_tpl(const u8 *b, size_t n, unsigned *lost, char *err, size_t errcap) { return docx_load(b, n, lost, err, errcap); }
static u8 *save_as(Doc *d, size_t *len, int kind)
{
  u8 old = d->tplkind, *r;
  d->tplkind = (u8)kind;
  r = docx_save(d, len);
  d->tplkind = old;
  return r;
}
static u8 *save_dotx(Doc *d, size_t *len) { return save_as(d, len, TK_DOTX); }
static u8 *save_dotm(Doc *d, size_t *len) { return save_as(d, len, TK_DOTM); }
static u8 *save_docm(Doc *d, size_t *len) { return save_as(d, len, TK_DOCM); }      /* (WP28e: its macros kept, docx.c keep_type) */
const DocFormat fmt_dotx = { "Word Template", "dotx", NULL, load_tpl, save_dotx, DF_NATIVE };
const DocFormat fmt_dotm = { "Word Macro-Enabled Template", "dotm", NULL, load_tpl, save_dotm, 0 };
const DocFormat fmt_docm = { "Word Macro-Enabled Document", "docm", NULL, load_tpl, save_docm, DF_NATIVE };
