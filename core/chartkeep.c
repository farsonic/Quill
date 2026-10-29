/*
 * chartkeep.c - see chart.h: the parts of charts and SmartArt kept byte for byte (ObjPack), read
 * from a .docx with the parts their own relationships lead to, laid out for drawing, and written back
 * when the document is saved: under the names they came with where nothing else takes them (else a
 * new number), with their relationships from the document (the ids they had where Quill's own and the
 * kept parts' don't clash), and their own .rels files unchanged unless a part had to be renamed.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "chart.h"
#include "keep.h"

/* ------------------------------------------------------------------ packs */
ObjPack *pack_new(int kind)
{
  ObjPack *pk = (ObjPack *)calloc(1, sizeof *pk);
  if (!pk) return NULL;
  pk->refs = 1;
  pk->kind = (u8)kind;
  prims_init(&pk->cache);
  return pk;
}
ObjPack *pack_ref(ObjPack *pk) { if (pk) pk->refs++; return pk; }
void pack_unref(ObjPack *pk)
{
  int i;
  if (!pk || --pk->refs > 0) return;
  for (i = 0; i < pk->np; i++) { PackPart *p = &pk->p[i]; free(p->name); free(p->ctype); free(p->reltype); free(p->relid); free(p->data); }
  free(pk->p);
  chart_free(pk->chart);
  smartart_free(pk->art);
  if (pk->theme && --pk->theme->refs <= 0) { free(pk->theme->data); free(pk->theme); }
  prims_free(&pk->cache);
  free(pk);
}
int pack_find(const ObjPack *pk, const char *name)
{
  int i;
  for (i = 0; i < pk->np; i++) if (!strcmp(pk->p[i].name, name)) return i;
  return -1;
}
int pack_add(ObjPack *pk, const char *name, const char *ctype, const char *reltype, const char *relid, int role, int owner,
             const void *data, size_t len)
{
  PackPart p, *np;
  int bad = 0;
  memset(&p, 0, sizeof p);
  if (!name || pack_find(pk, name) >= 0) return -1;
  if (!(p.name = str_dup(name))) bad = 1;
  if (ctype && !(p.ctype = str_dup(ctype))) bad = 1;
  if (reltype && !(p.reltype = str_dup(reltype))) bad = 1;
  if (relid && !(p.relid = str_dup(relid))) bad = 1;
  if (!(p.data = (u8 *)malloc(len + 1))) bad = 1;      /* (NUL-terminated, for the XML readers) */
  if (!bad && !(np = (PackPart *)realloc(pk->p, (size_t)(pk->np + 1) * sizeof *np))) bad = 1;
  if (bad) { free(p.name); free(p.ctype); free(p.reltype); free(p.relid); free(p.data); return -1; }
  if (len) memcpy(p.data, data, len);
  p.data[len] = 0;
  p.len = len; p.role = (u8)role; p.owner = owner;
  pk->p = np;
  pk->p[pk->np] = p;
  return pk->np++;
}
void pack_title(const ObjPack *pk, char *out, size_t cap)
{
  if (pk && pk->kind == PK_SMARTART) { snprintf(out, cap, "SmartArt"); return; }
  if (pk && pk->chart && pk->chart->title && pk->chart->title[0]) snprintf(out, cap, "%s", pk->chart->title);
  else if (pk && pk->chart) snprintf(out, cap, "%s", chart_kind_name(pk->chart->kind));
  else snprintf(out, cap, "Chart");
}

/* ------------------------------------------------------------------ drawing */
typedef struct { FontSet *fs; const char *family; } TwCtx;
/* a text's width in pixels at 96 dpi, px pixels to the em, in the family's metrics */
static i32 tw_fonts(void *ctx, const char *s, int px, int bold)
{
  TwCtx *t = (TwCtx *)ctx;
  FaceSize *z;
  if (!t->fs || !(z = fonts_size(t->fs, t->family, bold, 0, (u16)(px * 3 / 2 > 0 ? px * 3 / 2 : 1)))) return (i32)(strlen(s) * px / 2);
  return (i32)((fonts_text(z, s, (u32)strlen(s)) + 120) / 240);     /* fine units (1/16 twip) to pixels (15 twips) */
}
const PrimList *pack_prims(ObjPack *pk, i32 cx, i32 cy, FontSet *fs, const char *family)
{
  TwCtx t;
  int w = cx / 15, h = cy / 15;
  if (!pk) return NULL;
  if (pk->font[0]) family = pk->font;
  if (!family) family = "Calibri";
  if (pk->cache.n && pk->ccx == cx && pk->ccy == cy && !strncmp(pk->cfam, family, sizeof pk->cfam - 1)) return &pk->cache;
  prims_free(&pk->cache);
  t.fs = fs; t.family = family;
  if (pk->kind == PK_SMARTART) {
    if (pk->art) smartart_render(pk->art, w, h, tw_fonts, &t, &pk->cache);
  } else if (pk->chart) chart_render(pk->chart, w, h, tw_fonts, &t, &pk->cache);
  if (!pk->cache.n && w > 2 && h > 2) {
    /* nothing it can draw: a frame (charts: with "Chart") */
    Prim *p = prim_add(&pk->cache, PR_RECT, CE_CHART);
    if (p) { p->w = w; p->h = h; p->line = PRIM_ON | 0xBFBFBF; p->fill = PRIM_ON | 0xFFFFFF; p->width = 1; }
    if (pk->kind != PK_SMARTART && (p = prim_add(&pk->cache, PR_TEXT, CE_TITLE)) && (p->text = str_dup("Chart"))) {
      p->x = w / 2; p->y = h / 2; p->size = 13; p->align = 1; p->valign = 1; p->line = PRIM_ON | 0x595959; p->w = tw_fonts(&t, "Chart", 13, 0); p->h = 15;
    }
  }
  if (pk->cache.oom) { prims_free(&pk->cache); return NULL; }
  pk->ccx = cx; pk->ccy = cy;
  snprintf(pk->cfam, sizeof pk->cfam, "%s", family);
  return &pk->cache;
}

/* ------------------------------------------------------------------ reading */
/* target (a relationship's Target) as a part name, from the folder base ("word/", "" ...) */
static void part_path(const char *base, const char *tg, char *out, size_t cap)
{
  char b[256];
  if (tg[0] == '/') { snprintf(out, cap, "%s", tg + 1); return; }
  snprintf(b, sizeof b, "%s", base);
  while (!strncmp(tg, "../", 3) || !strncmp(tg, "./", 2)) {
    char *sl;
    if (tg[1] == '/') { tg += 2; continue; }
    tg += 3;
    b[strlen(b) ? strlen(b) - 1 : 0] = 0;
    sl = strrchr(b, '/');
    if (sl) sl[1] = 0; else b[0] = 0;
  }
  snprintf(out, cap, "%s%s", b, tg);
}
/* a part's content type from [Content_Types].xml: its Override, else its extension's Default; "" if neither */
static void content_type(const char *ct, size_t n, const char *name, char *out, size_t cap)
{
  Xml x;
  const char *ext = strrchr(name, '.');
  char def[200] = "";
  out[0] = 0;
  if (!ct) return;
  xml_init(&x, ct, n);
  while (xml_next(&x) != XT_EOF && !out[0]) {
    const char *pn, *ty;
    if (x.type != XT_START) continue;
    ty = xml_attr(&x, "ContentType");
    if (!ty) continue;
    if (!strcmp(x.name, "Override") && (pn = xml_attr(&x, "PartName")) && pn[0] == '/' && str_ieq(pn + 1, name)) snprintf(out, cap, "%s", ty);
    else if (!strcmp(x.name, "Default") && ext && (pn = xml_attr(&x, "Extension")) && str_ieq(pn, ext + 1)) snprintf(def, sizeof def, "%s", ty);
  }
  xml_free(&x);
  if (!out[0]) snprintf(out, cap, "%s", def);
}
/* relationship id in rels: its type and target part name (from base); 0 if there is none (or it is external) */
static int rel_find(const char *rels, size_t rn, const char *base, const char *id, char *type, size_t tcap, char *target, size_t gcap)
{
  Xml x;
  int found = 0;
  if (!rels || !id || !id[0]) return 0;
  xml_init(&x, rels, rn);
  while (!found && xml_next(&x) != XT_EOF) {
    const char *i, *t, *tg, *mode;
    if (x.type != XT_START || strcmp(x.name, "Relationship") || !(i = xml_attr(&x, "Id")) || strcmp(i, id)) continue;
    t = xml_attr(&x, "Type"); tg = xml_attr(&x, "Target"); mode = xml_attr(&x, "TargetMode");
    if (!t || !tg || (mode && !strcmp(mode, "External"))) break;
    snprintf(type, tcap, "%s", t);
    part_path(base, tg, target, gcap);
    found = 1;
  }
  xml_free(&x);
  return found;
}
static void dir_of(const char *name, char *dir, size_t cap)
{
  const char *sl = strrchr(name, '/');
  snprintf(dir, cap, "%.*s", sl ? (int)(sl - name + 1) : 0, name);
}
/* part name and, through its own .rels, the parts it leads to, into the pack. role, reltype, relid: its
   relationship from the document (RO_NONE for the parts below). Its index, -1 if it isn't there */
static int add_tree(ObjPack *pk, ZipReader *z, const char *ct, size_t ctn, const char *name, int role, const char *reltype,
                    const char *relid, int depth)
{
  char ty[200], relsname[300], dir[256], tgt[300];
  char *data, *rels;
  size_t n, rn;
  const char *sl;
  int me, ri, at;
  if (depth > 4) return -1;
  if ((me = pack_find(pk, name)) >= 0) return me;
  if ((at = zip_find(z, name)) < 0 || !(data = (char *)zip_read(z, at, &n))) return -1;
  content_type(ct, ctn, name, ty, sizeof ty);
  me = pack_add(pk, name, ty[0] ? ty : "application/xml", reltype, relid, role, -1, data, n);
  free(data);
  if (me < 0) return -1;
  sl = strrchr(name, '/');
  dir_of(name, dir, sizeof dir);
  snprintf(relsname, sizeof relsname, "%s_rels/%s.rels", dir, sl ? sl + 1 : name);
  if ((at = zip_find(z, relsname)) < 0 || !(rels = (char *)zip_read(z, at, &rn))) return me;
  ri = pack_add(pk, relsname, NULL, NULL, NULL, RO_NONE, me, rels, rn);
  if (ri >= 0) {
    Xml x;
    xml_init(&x, rels, rn);
    while (xml_next(&x) != XT_EOF) {
      const char *t, *mode;
      if (x.type != XT_START || strcmp(x.name, "Relationship") || !(t = xml_attr(&x, "Target"))) continue;
      if ((mode = xml_attr(&x, "TargetMode")) && !strcmp(mode, "External")) continue;
      part_path(dir, t, tgt, sizeof tgt);
      add_tree(pk, z, ct, ctn, tgt, RO_NONE, NULL, NULL, depth + 1);
    }
    xml_free(&x);
  }
  free(rels);
  return me;
}
ObjPack *pack_load_chart(ZipReader *z, const char *ct, size_t ctn, const char *rels, size_t rn, const char *base,
                         const char *rid, int ex, const u32 *theme, const char *minor, const char *major)
{
  char type[200], name[300];
  ObjPack *pk;
  int k;
  if (!z || !rel_find(rels, rn, base, rid, type, sizeof type, name, sizeof name) || !(pk = pack_new(ex ? PK_CHARTEX : PK_CHART))) return NULL;
  if ((k = add_tree(pk, z, ct, ctn, name, RO_CHART, type, rid, 0)) < 0) { pack_unref(pk); return NULL; }
  pk->chart = chart_read((const char *)pk->p[k].data, pk->p[k].len, ex, theme);
  /* its font: its own, the theme's body (or heading) font it names, or the theme's body font as Word's charts have */
  if (pk->chart && pk->chart->font[0] && pk->chart->font[0] != '+') snprintf(pk->font, sizeof pk->font, "%s", pk->chart->font);
  else if (pk->chart && !strncmp(pk->chart->font, "+mj", 3) && major && major[0]) snprintf(pk->font, sizeof pk->font, "%s", major);
  else if (minor && minor[0]) snprintf(pk->font, sizeof pk->font, "%s", minor);
  return pk;
}
ObjPack *pack_load_smartart(ZipReader *z, const char *ct, size_t ctn, const char *rels, size_t rn, const char *base,
                            const char *dm, const char *lo, const char *qs, const char *cs, const u32 *theme, const char *minor)
{
  const char *ids[4];
  static const u8 roles[4] = { RO_DM, RO_LO, RO_QS, RO_CS };
  char type[200], name[300];
  ObjPack *pk;
  int i, kd = -1;
  ids[0] = dm; ids[1] = lo; ids[2] = qs; ids[3] = cs;
  if (!z || !(pk = pack_new(PK_SMARTART))) return NULL;
  for (i = 0; i < 4; i++) {
    int k;
    if (!rel_find(rels, rn, base, ids[i], type, sizeof type, name, sizeof name)) continue;
    k = add_tree(pk, z, ct, ctn, name, roles[i], type, ids[i], 0);
    if (i == 0) kd = k;
  }
  if (kd < 0) { pack_unref(pk); return NULL; }
  if (minor && minor[0]) snprintf(pk->font, sizeof pk->font, "%s", minor);
  {
    /* the drawing Word saved: named by the data part's dsp:dataModelExt relId, a relationship of the
       part showing the SmartArt */
    Xml x;
    char did[64] = "";
    xml_init(&x, (const char *)pk->p[kd].data, pk->p[kd].len);
    while (xml_next(&x) != XT_EOF)
      if (x.type == XT_START && !strcmp(x.name, "dataModelExt")) { const char *r = xml_attr(&x, "relId"); if (r) snprintf(did, sizeof did, "%s", r); break; }
    xml_free(&x);
    if (did[0] && rel_find(rels, rn, base, did, type, sizeof type, name, sizeof name)) {
      int k = add_tree(pk, z, ct, ctn, name, RO_DRAWING, type, did, 0);
      if (k >= 0) pk->art = smartart_read((const char *)pk->p[k].data, pk->p[k].len, theme);
    }
  }
  return pk;
}

#define RT_THEME "http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme"
#define CT_THEME "application/vnd.openxmlformats-officedocument.theme+xml"
void pack_set_theme(ObjPack *pk, Doc *d, ZipReader *z, const char *rels, size_t rn, const char *base)
{
  char name[300] = "", relsname[320];
  u8 *data;
  size_t n;
  int i, at;
  if (!pk || pk->theme || !z) return;
  if (rels) {
    /* the main part's relationship of type .../theme */
    Xml x;
    xml_init(&x, rels, rn);
    while (!name[0] && xml_next(&x) != XT_EOF) {
      const char *t, *tg;
      if (x.type == XT_START && !strcmp(x.name, "Relationship") && (t = xml_attr(&x, "Type")) && !strcmp(t, RT_THEME) && (tg = xml_attr(&x, "Target")))
        part_path(base ? base : "word/", tg, name, sizeof name);
    }
    xml_free(&x);
  }
  if (!name[0]) snprintf(name, sizeof name, "word/theme/theme1.xml");
  {
    const char *sl = strrchr(name, '/');
    snprintf(relsname, sizeof relsname, "%.*s_rels/%s.rels", sl ? (int)(sl - name + 1) : 0, name, sl ? sl + 1 : name);
  }
  if (zip_find(z, relsname) >= 0 || (at = zip_find(z, name)) < 0 || !(data = zip_read(z, at, &n))) return;
  for (i = 0; d && i < d->nimages; i++) {
    PackBlob *b = d->images[i].pack ? d->images[i].pack->theme : NULL;
    if (b && b->len == n && !memcmp(b->data, data, n)) { b->refs++; pk->theme = b; free(data); return; }
  }
  if (!(pk->theme = (PackBlob *)malloc(sizeof *pk->theme))) { free(data); return; }
  pk->theme->refs = 1; pk->theme->data = data; pk->theme->len = n;
}

/* ------------------------------------------------------------------ saving */
/* while saving: each pack written, its parts' names in the package and the ids of its relationships
   from the document */
typedef struct { ObjPack *pk; char **name; char **id; int renamed; } SavePack;
static SavePack *sp; static int nsp;
static Buf srels;                       /* <Relationship .../> for every part showing objects */
static int nextq;
static PackBlob *stheme; static char *sthemename, *sthemeid;   /* a theme written with them (the document has none) */
static void save_clear(void)
{
  int i, j;
  for (i = 0; i < nsp; i++) {
    for (j = 0; j < sp[i].pk->np; j++) { free(sp[i].name[j]); free(sp[i].id[j]); }
    free(sp[i].name); free(sp[i].id);
    pack_unref(sp[i].pk);
  }
  free(sp); sp = NULL; nsp = 0;
  buf_free(&srels);
  nextq = 0;
  stheme = NULL; free(sthemename); free(sthemeid); sthemename = sthemeid = NULL;
}
static SavePack *save_of(const ObjPack *pk)
{
  int i;
  for (i = 0; pk && i < nsp; i++) if (sp[i].pk == pk) return &sp[i];
  return NULL;
}
static void note_pack(ObjPack *pk)
{
  SavePack *n;
  if (!pk || save_of(pk) || !(n = (SavePack *)realloc(sp, (size_t)(nsp + 1) * sizeof *n))) return;
  sp = n;
  memset(&sp[nsp], 0, sizeof sp[nsp]);
  sp[nsp].name = (char **)calloc((size_t)pk->np + 1, sizeof(char *));
  sp[nsp].id = (char **)calloc((size_t)pk->np + 1, sizeof(char *));
  if (!sp[nsp].name || !sp[nsp].id) { free(sp[nsp].name); free(sp[nsp].id); return; }
  sp[nsp].pk = pack_ref(pk);
  nsp++;
}
static void scan_story(Doc *d, Para **pp, u32 np)
{
  u32 i, k;
  for (i = 0; i < np; i++)
    for (k = 0; k + 4 <= pp[i]->len; k++)
      if (IS_PIC(pp[i]->t + k)) { int n = pic_index(pp[i]->t + k); if (n < d->nimages && d->images[n].pack) note_pack(d->images[n].pack); k += 3; }
}
/* a name Quill writes itself (a kept part must not take its place) */
static int quill_name(const char *name)
{
  static const char *const own[] = { "[Content_Types].xml", "_rels/.rels", "word/document.xml", "word/_rels/document.xml.rels", "word/styles.xml",
    "word/settings.xml", "word/numbering.xml", "word/footnotes.xml", "word/endnotes.xml", "word/comments.xml", "docProps/core.xml" };
  size_t i;
  for (i = 0; i < sizeof own / sizeof own[0]; i++) if (str_ieq(name, own[i])) return 1;
  return !strncmp(name, "word/media/", 11) || !strncmp(name, "word/header", 11) || !strncmp(name, "word/footer", 11) ||
         !strncmp(name, "word/_rels/", 11) || !strncmp(name, "_rels/", 6) || !strncmp(name, "docProps/", 9);
}
static int name_taken(Doc *d, const char *name)
{
  int i, j;
  if (quill_name(name)) return 1;
  for (i = 0; i < d->nkept; i++) if (str_ieq(d->kept[i].name, name)) return 1;
  for (i = 0; i < nsp; i++) for (j = 0; j < sp[i].pk->np; j++) if (sp[i].name[j] && str_ieq(sp[i].name[j], name)) return 1;
  return 0;
}
static int id_taken(Doc *d, const char *id)
{
  int i, j;
  if (keep_id_reserved(id)) return 1;
  for (i = 0; i < d->nkept; i++) if (d->kept[i].owner && !strcmp(d->kept[i].owner, "word/document.xml") && d->kept[i].relid && !strcmp(d->kept[i].relid, id)) return 1;
  for (i = 0; i < nsp; i++) for (j = 0; j < sp[i].pk->np; j++) if (sp[i].id[j] && !strcmp(sp[i].id[j], id)) return 1;
  return 0;
}
/* a free name like want: want itself, or its number changed (chart1.xml: chart2.xml, chart3.xml...);
   in Quill's own folders (media) it moves to word/charts/ */
static char *free_name(Doc *d, const char *want)
{
  char dir[256], stem[200], ext[40], t[512];
  const char *sl, *dot, *b;
  size_t sn;
  int k;
  if (!name_taken(d, want)) return str_dup(want);
  dir_of(want, dir, sizeof dir);
  if (quill_name(want)) snprintf(dir, sizeof dir, "word/charts/");
  sl = strrchr(want, '/');
  b = sl ? sl + 1 : want;
  dot = strrchr(b, '.');
  snprintf(ext, sizeof ext, "%s", dot ? dot : "");
  sn = dot ? (size_t)(dot - b) : strlen(b);
  while (sn && b[sn - 1] >= '0' && b[sn - 1] <= '9') sn--;
  snprintf(stem, sizeof stem, "%.*s", (int)(sn < 190 ? sn : 190), b);
  for (k = 1; k < 100000; k++) {
    snprintf(t, sizeof t, "%s%s%d%s", dir, stem, k, ext);
    if (!name_taken(d, t)) return str_dup(t);
  }
  return NULL;
}
void pack_save_begin(Doc *d)
{
  int i, j, w, n;
  save_clear();
  if (!d) return;
  /* the packs the text refers to: body, headers and footers, text boxes, notes and comments */
  scan_story(d, d->p, d->np);
  for (w = 0; w < HF_N; w++) { scan_story(d, d->hdr[w].p, d->hdr[w].np); scan_story(d, d->ftr[w].p, d->ftr[w].np); }
  for (n = 0; n < d->nsects; n++) if (d->sects[n].hf) for (w = 0; w < 2 * HF_N; w++) scan_story(d, d->sects[n].hf[w].p, d->sects[n].hf[w].np);
  for (w = 0; w < d->nimages; w++) if (d->images[w].text) scan_story(d, d->images[w].text->p, d->images[w].text->np);
  for (w = 0; w < d->nnotes; w++) scan_story(d, d->notes[w].p, d->notes[w].np);
  for (w = 0; w < d->nenotes; w++) scan_story(d, d->enotes[w].p, d->enotes[w].np);
  for (w = 0; w < d->ncomments; w++) scan_story(d, d->comments[w].text.p, d->comments[w].text.np);
  /* names: as they came where free (the .rels follow their parts), then the relationships' ids */
  for (i = 0; i < nsp; i++) {
    ObjPack *pk = sp[i].pk;
    for (j = 0; j < pk->np; j++) {
      if (pk->p[j].owner >= 0) continue;
      sp[i].name[j] = free_name(d, pk->p[j].name);
      if (!sp[i].name[j]) sp[i].name[j] = str_dup(pk->p[j].name);
      if (sp[i].name[j] && strcmp(sp[i].name[j], pk->p[j].name)) sp[i].renamed = 1;
    }
    for (j = 0; j < pk->np; j++) {
      const char *on, *sl;
      char dir[256], t[512];
      if (pk->p[j].owner < 0) continue;
      on = sp[i].name[pk->p[j].owner] ? sp[i].name[pk->p[j].owner] : pk->p[pk->p[j].owner].name;
      dir_of(on, dir, sizeof dir);
      sl = strrchr(on, '/');
      snprintf(t, sizeof t, "%s_rels/%s.rels", dir, sl ? sl + 1 : on);
      sp[i].name[j] = str_dup(t);
    }
    for (j = 0; j < pk->np; j++) {
      char t[32];
      if (pk->p[j].role == RO_NONE) continue;
      if (pk->p[j].relid && !id_taken(d, pk->p[j].relid)) sp[i].id[j] = str_dup(pk->p[j].relid);
      else {
        do snprintf(t, sizeof t, "rIdQ%d", ++nextq); while (id_taken(d, t));
        sp[i].id[j] = str_dup(t);
      }
    }
  }
  /* the theme their colours and fonts refer to, when the document keeps none */
  for (i = 0; i < nsp && !stheme; i++) if (sp[i].pk->theme) stheme = sp[i].pk->theme;
  for (i = 0; stheme && i < d->nkept; i++) if (d->kept[i].reltype && !strcmp(d->kept[i].reltype, RT_THEME)) stheme = NULL;
  if (stheme) {
    char t[32];
    do snprintf(t, sizeof t, "rIdQ%d", ++nextq); while (id_taken(d, t));
    sthemename = free_name(d, "word/theme/theme1.xml");
    sthemeid = str_dup(t);
    if (!sthemename || !sthemeid) stheme = NULL;
  }
  /* their relationships, for the document's .rels and every header's and footer's */
  buf_init(&srels);
  for (i = 0; i < nsp; i++)
    for (j = 0; j < sp[i].pk->np; j++) {
      const PackPart *p = &sp[i].pk->p[j];
      const char *nm = sp[i].name[j];
      if (p->role == RO_NONE || !nm || !sp[i].id[j] || !p->reltype) continue;
      buf_adds(&srels, "<Relationship Id=\""); xml_escape(&srels, sp[i].id[j], strlen(sp[i].id[j]));
      buf_adds(&srels, "\" Type=\""); xml_escape(&srels, p->reltype, strlen(p->reltype));
      if (!strncmp(nm, "word/", 5)) { buf_adds(&srels, "\" Target=\""); nm += 5; } else buf_adds(&srels, "\" Target=\"/");
      xml_escape(&srels, nm, strlen(nm)); buf_adds(&srels, "\"/>");
    }
}
/* the id written for the pack's part of a role, or NULL */
static const char *role_id(const SavePack *s, int role)
{
  int j;
  for (j = 0; j < s->pk->np; j++) if (s->pk->p[j].role == role) return s->id[j];
  return NULL;
}
int pack_save_isex(const Image *im)
{
  const SavePack *s = save_of(im->pack);
  return s && s->pk->kind == PK_CHARTEX && role_id(s, RO_CHART);
}
void pack_save_fallback(Buf *o, const Image *im)
{
  char t[200], s[220];
  pack_title(im->pack, t, sizeof t);
  snprintf(s, sizeof s, "[Chart: %s]", t);
  buf_adds(o, "</mc:Choice><mc:Fallback><w:t>");
  xml_escape(o, s, strlen(s));
  buf_adds(o, "</w:t></mc:Fallback></mc:AlternateContent>");
}
int pack_save_graphic(Buf *o, const Image *im)
{
  const SavePack *s = save_of(im->pack);
  const char *a, *b, *c, *dd;
  if (!s) return 0;
  if (s->pk->kind == PK_SMARTART) {
    a = role_id(s, RO_DM); b = role_id(s, RO_LO); c = role_id(s, RO_QS); dd = role_id(s, RO_CS);
    if (!a) return 0;
    buf_adds(o, "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/diagram\"><dgm:relIds xmlns:dgm=\"http://schemas.openxmlformats.org/drawingml/2006/diagram\" "
                "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"");
    buf_printf(o, " r:dm=\"%s\" r:lo=\"%s\" r:qs=\"%s\" r:cs=\"%s\"/></a:graphicData>", a, b ? b : "", c ? c : "", dd ? dd : "");
    return 1;
  }
  if (!(a = role_id(s, RO_CHART))) return 0;
  if (s->pk->kind == PK_CHARTEX)
    buf_printf(o, "<a:graphicData uri=\"http://schemas.microsoft.com/office/drawing/2014/chartex\"><cx:chart xmlns:cx=\"http://schemas.microsoft.com/office/drawing/2014/chartex\" "
                  "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" r:id=\"%s\"/></a:graphicData>", a);
  else
    buf_printf(o, "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\"><c:chart xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" "
                  "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" r:id=\"%s\"/></a:graphicData>", a);
  return 1;
}
/* a .rels part written again because parts it leads to were renamed: the same relationships, their
   targets the parts' new names (from the package root) */
static void rels_again(const SavePack *s, int j, Buf *o)
{
  const PackPart *p = &s->pk->p[j];
  char dir[256], tgt[300];
  Xml x;
  dir_of(s->pk->p[p->owner].name, dir, sizeof dir);
  buf_adds(o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
  xml_init(&x, (const char *)p->data, p->len);
  while (xml_next(&x) != XT_EOF) {
    const char *id, *ty, *tg, *mode;
    int k;
    if (x.type != XT_START || strcmp(x.name, "Relationship")) continue;
    id = xml_attr(&x, "Id"); ty = xml_attr(&x, "Type"); tg = xml_attr(&x, "Target"); mode = xml_attr(&x, "TargetMode");
    if (!id || !ty || !tg) continue;
    buf_adds(o, "<Relationship Id=\""); xml_escape(o, id, strlen(id));
    buf_adds(o, "\" Type=\""); xml_escape(o, ty, strlen(ty));
    buf_adds(o, "\" Target=\"");
    if (mode && !strcmp(mode, "External")) xml_escape(o, tg, strlen(tg));
    else {
      part_path(dir, tg, tgt, sizeof tgt);
      k = pack_find(s->pk, tgt);
      if (k >= 0 && s->name[k]) { buf_addc(o, '/'); xml_escape(o, s->name[k], strlen(s->name[k])); }
      else xml_escape(o, tg, strlen(tg));
    }
    buf_adds(o, "\"");
    if (mode) { buf_adds(o, " TargetMode=\""); xml_escape(o, mode, strlen(mode)); buf_adds(o, "\""); }
    buf_adds(o, "/>");
  }
  xml_free(&x);
  buf_adds(o, "</Relationships>");
}
void pack_save_parts(Doc *d, DocxOut *o)
{
  int i, j;
  if (stheme) docx_out_part(o, sthemename, CT_THEME, "word/document.xml", RT_THEME, sthemeid, stheme->data, stheme->len);
  for (i = 0; i < nsp; i++) {
    const SavePack *s = &sp[i];
    const char *drawid = role_id(s, RO_DRAWING);
    for (j = 0; j < s->pk->np; j++) {
      const PackPart *p = &s->pk->p[j];
      const char *nm = s->name[j];
      if (!nm) continue;
      if (p->owner >= 0 && s->renamed) {
        Buf b;
        buf_init(&b);
        rels_again(s, j, &b);
        if (!b.oom) docx_out_part(o, nm, NULL, NULL, NULL, NULL, b.data, b.len);
        buf_free(&b);
        continue;
      }
      if (p->role == RO_DM && drawid) {
        /* the data part names its drawing by the document's relationship id: changed with it */
        const PackPart *dp = NULL;
        int k;
        for (k = 0; k < s->pk->np; k++) if (s->pk->p[k].role == RO_DRAWING) dp = &s->pk->p[k];
        if (dp && dp->relid && strcmp(dp->relid, drawid)) {
          char want[80];
          const char *at, *src = (const char *)p->data;
          Buf b;
          snprintf(want, sizeof want, "relId=\"%s\"", dp->relid);
          buf_init(&b);
          if ((at = strstr(src, want))) {
            buf_add(&b, src, (size_t)(at - src));
            buf_printf(&b, "relId=\"%s\"", drawid);
            buf_add(&b, at + strlen(want), p->len - (size_t)(at - src) - strlen(want));
          } else buf_add(&b, p->data, p->len);
          if (!b.oom) docx_out_part(o, nm, p->ctype, "word/document.xml", p->reltype, s->id[j], b.data, b.len);
          buf_free(&b);
          continue;
        }
      }
      if (p->role != RO_NONE) docx_out_part(o, nm, p->ctype, "word/document.xml", p->reltype, s->id[j], p->data, p->len);
      else docx_out_part(o, nm, p->ctype, NULL, NULL, NULL, p->data, p->len);
    }
  }
}
void pack_save_rels(Buf *o) { if (srels.len) buf_add(o, srels.data, srels.len); }
int pack_save_has_rels(void) { return srels.len > 0; }
