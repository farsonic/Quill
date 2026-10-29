/*
 * refs.c - (WP2) see refs.h: the caption labels and their settings, captions, the fields that refer
 * to other places (REF with its numbering switches, NOTEREF, STYLEREF, PAGEREF \p), marking index
 * entries and citations, AutoMark, cross-references and the bookmarks they need. The block fields
 * (TOC, INDEX, TOA) are made in core/refsgen.c. Written from Word's documented behaviour.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "refsi.h"
#include "notes.h"
#include "outline.h"
#include "charfx.h"

int refs_lang = RL_EN;

/* ------------------------------------------------------------------ */
/* languages and the words fields put in the text */
int refs_lang_lcid(int l) { static const int v[RL_N] = { 1033, 1031, 1036, 3082, 1040 }; return l >= 0 && l < RL_N ? v[l] : 1033; }
int refs_lang_of_lcid(int lcid)
{
  switch (lcid & 0x3FF) { case 7: return RL_DE; case 12: return RL_FR; case 10: return RL_ES; case 16: return RL_IT; }
  return RL_EN;
}
static const char *const words[RL_N][RW_N] = {
  { "above", "below", "on page", "See", "See also", "passim", "No table of contents entries found.", "No table of figures entries found.",
    "No index entries found.", "No table of authorities entries found.", "Contents", "Index", "Table of Authorities",
    "Error! No text of specified style in document.", "Symbols" },
  { "oben", "unten", "auf Seite", "Siehe", "Siehe auch", "passim", "Es wurden keine Eintr\xC3\xA4ge f\xC3\xBCr das Inhaltsverzeichnis gefunden.",
    "Es konnten keine Eintr\xC3\xA4ge f\xC3\xBCr ein Abbildungsverzeichnis gefunden werden.", "Keine Indexeintr\xC3\xA4ge gefunden.",
    "Keine Eintr\xC3\xA4ge f\xC3\xBCr ein Rechtsgrundlagenverzeichnis gefunden.", "Inhalt", "Index", "Rechtsgrundlagenverzeichnis",
    "Fehler! Kein Text mit angegebener Formatvorlage im Dokument.", "Symbole" },
  { "ci-dessus", "ci-dessous", "\xC3\xA0 la page", "Voir", "Voir aussi", "passim", "Aucune entr\xC3\xA9" "e de table des mati\xC3\xA8res n'a \xC3\xA9t\xC3\xA9 trouv\xC3\xA9" "e.",
    "Aucune entr\xC3\xA9" "e de table d'illustration n'a \xC3\xA9t\xC3\xA9 trouv\xC3\xA9" "e.", "Aucune entr\xC3\xA9" "e d'index n'a \xC3\xA9t\xC3\xA9 trouv\xC3\xA9" "e.",
    "Aucune entr\xC3\xA9" "e de table des r\xC3\xA9" "f\xC3\xA9rences n'a \xC3\xA9t\xC3\xA9 trouv\xC3\xA9" "e.", "Table des mati\xC3\xA8res", "Index", "Table des r\xC3\xA9" "f\xC3\xA9rences",
    "Erreur ! Il n'y a pas de texte r\xC3\xA9pondant \xC3\xA0 ce style dans ce document.", "Symboles" },
  { "arriba", "abajo", "en la p\xC3\xA1gina", "V\xC3\xA9" "ase", "V\xC3\xA9" "ase tambi\xC3\xA9n", "passim", "No se encuentran elementos de tabla de contenido.",
    "No se encuentran elementos de tabla de ilustraciones.", "No se encontraron entradas de \xC3\xADndice.", "No se encuentra ninguna entrada para la tabla de autoridades.",
    "Contenido", "\xC3\x8Dndice", "Tabla de autoridades", "\xC2\xA1" "Error! No hay texto con el estilo especificado en el documento.", "S\xC3\xADmbolos" },
  { "sopra", "sotto", "a pagina", "Vedere", "Vedere anche", "passim", "Non sono state trovate voci di sommario.", "Non \xC3\xA8 stata trovata alcuna voce dell'indice delle figure.",
    "Nessuna voce di indice trovata.", "Non \xC3\xA8 stata trovata alcuna voce dell'indice delle fonti.", "Sommario", "Indice", "Indice delle fonti",
    "Errore. Non esiste testo dello stile specificato nel documento.", "Simboli" } };
const char *refs_word(int l, int w)
{
  if (l < 0 || l >= RL_N) l = RL_EN;
  return w >= 0 && w < RW_N ? words[l][w] : "";
}

/* ------------------------------------------------------------------ */
/* the labels, AutoCaption and the categories */
static const char *const builtin_lab[3] = { "Figure", "Table", "Equation" };
static const char *const cat_names[RL_N][7] = {
  { "Cases", "Statutes", "Other Authorities", "Rules", "Treatises", "Regulations", "Constitutional Provisions" },
  { "F\xC3\xA4lle", "Gesetze", "Andere Quellen", "Regeln", "Abhandlungen", "Bestimmungen", "Verfassungsrechtliche Bestimmungen" },
  { "Cas", "Lois", "Autres r\xC3\xA9" "f\xC3\xA9rences", "R\xC3\xA8gles", "Trait\xC3\xA9s", "R\xC3\xA8glements", "Dispositions constitutionnelles" },
  { "Casos", "Estatutos", "Otras autoridades", "Reglas", "Tratados", "Regulaciones", "Disposiciones constitucionales" },
  { "Casi", "Statuti", "Altre fonti", "Regole", "Trattati", "Regolamenti", "Disposizioni costituzionali" } };
RefsInfo *refs_info(Doc *d)
{
  int i;
  if (d->refs) return d->refs;
  if (!(d->refs = (RefsInfo *)calloc(1, sizeof(RefsInfo)))) return NULL;
  for (i = 0; i < 3; i++) {
    CapLabel *l = &d->refs->lab[i];
    strcpy(l->name, builtin_lab[i]);
    l->heading = 1;
  }
  d->refs->nlab = 3;
  return d->refs;
}
void refs_free(Doc *d) { free(d->refs); d->refs = NULL; }
int refs_label(Doc *d, const char *name)
{
  RefsInfo *ri = refs_info(d);
  int i;
  if (!ri || !name) return -1;
  for (i = 0; i < ri->nlab; i++) if (str_ieq(ri->lab[i].name, name)) return i;
  return -1;
}
int refs_add_label(Doc *d, const char *name)
{
  RefsInfo *ri = refs_info(d);
  int k = refs_label(d, name);
  if (k >= 0 || !ri || !name || !name[0]) return k;
  if (ri->nlab >= REFS_MAXLAB) return -1;
  memset(&ri->lab[ri->nlab], 0, sizeof ri->lab[0]);
  snprintf(ri->lab[ri->nlab].name, sizeof ri->lab[0].name, "%s", name);
  ri->lab[ri->nlab].heading = 1;
  return ri->nlab++;
}
int refs_delete_label(Doc *d, const char *name)
{
  RefsInfo *ri = refs_info(d);
  int k = refs_label(d, name);
  if (k < 3 || !ri) return 0;
  memmove(&ri->lab[k], &ri->lab[k + 1], (size_t)(ri->nlab - k - 1) * sizeof ri->lab[0]);
  ri->nlab--;
  return 1;
}
const char *refs_category(Doc *d, int c)
{
  static char num[16];
  if (c < 1 || c > REFS_NCAT) return "";
  if (d && d->refs && d->refs->cat[c - 1][0]) return d->refs->cat[c - 1];
  if (c <= 7) return cat_names[refs_lang >= 0 && refs_lang < RL_N ? refs_lang : 0][c - 1];
  sprintf(num, "%d", c);
  return num;
}
const char *refs_sep_text(int sep)
{
  static const char *const s[CSEP_N] = { "-", ".", ":", "\xE2\x80\x94", "\xE2\x80\x93" };
  return sep >= 0 && sep < CSEP_N ? s[sep] : "-";
}
/* Word's names for the number formats and separators in w:caption */
static const char *const cn_xml[CN_N] = { "decimal", "lowerLetter", "upperLetter", "lowerRoman", "upperRoman" };
static const char *const cs_xml[CSEP_N] = { "hyphen", "period", "colon", "emDash", "enDash" };
static const char *const cn_sw[CN_N] = { "ARABIC", "alphabetic", "ALPHABETIC", "roman", "ROMAN" };
static int on_val(Xml *x, const char *name)
{
  const char *v = xml_attr(x, name);
  return v && (!strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "on"));
}
void refs_read_setting(Doc *d, Xml *x)
{
  RefsInfo *ri;
  const char *v;
  int i;
  if (strcmp(x->name, "captions") && strcmp(x->name, "caption") && strcmp(x->name, "autoCaption")) return;
  if (!(ri = refs_info(d))) return;
  ri->fromfile = 1;
  if (!strcmp(x->name, "caption") && (v = xml_attr(x, "name")) && v[0]) {
    int k = refs_add_label(d, v);
    CapLabel *l;
    if (k < 0) return;
    l = &ri->lab[k];
    l->pos = (u8)((v = xml_attr(x, "pos")) && !strcmp(v, "above") ? CP_ABOVE : CP_BELOW);
    l->chap = (u8)on_val(x, "chapNum");
    l->nolabel = (u8)on_val(x, "noLabel");
    if ((v = xml_attr(x, "heading")) && atoi(v) >= 1 && atoi(v) <= 9) l->heading = (u8)atoi(v);
    if ((v = xml_attr(x, "numFmt"))) for (i = 0; i < CN_N; i++) if (!strcmp(v, cn_xml[i])) l->fmt = (u8)i;
    if ((v = xml_attr(x, "sep"))) for (i = 0; i < CSEP_N; i++) if (!strcmp(v, cs_xml[i])) l->sep = (u8)i;
  } else if (!strcmp(x->name, "autoCaption") && (v = xml_attr(x, "name")) && ri->nac < REFS_MAXAC) {
    AutoCap *a = &ri->ac[ri->nac++];
    snprintf(a->name, sizeof a->name, "%s", v);
    snprintf(a->label, sizeof a->label, "%s", (v = xml_attr(x, "caption")) ? v : "");
    a->on = 1;
  }
}
void refs_write_settings(Doc *d, Buf *b)
{
  RefsInfo *ri = d->refs;
  int i, any = 0;
  if (!ri) return;
  for (i = 0; i < ri->nac; i++) if (ri->ac[i].on) any = 1;
  if (!ri->fromfile && ri->nlab == 3 && !any) {
    /* the three built-in labels as Word has them: nothing to say */
    for (i = 0; i < 3; i++) { const CapLabel *l = &ri->lab[i]; if (l->pos || l->fmt || l->chap || l->nolabel || l->sep || l->heading != 1) any = 1; }
    if (!any) return;
  }
  buf_adds(b, "<w:captions>");
  for (i = 0; i < ri->nlab; i++) {
    const CapLabel *l = &ri->lab[i];
    buf_adds(b, "<w:caption w:name=\"");
    xml_escape(b, l->name, strlen(l->name));
    buf_printf(b, "\" w:pos=\"%s\"", l->pos == CP_ABOVE ? "above" : "below");
    if (l->chap) buf_printf(b, " w:chapNum=\"1\" w:heading=\"%d\"", l->heading ? l->heading : 1);
    if (l->nolabel) buf_adds(b, " w:noLabel=\"1\"");
    if (l->fmt) buf_printf(b, " w:numFmt=\"%s\"", cn_xml[l->fmt < CN_N ? l->fmt : 0]);
    if (l->sep) buf_printf(b, " w:sep=\"%s\"", cs_xml[l->sep < CSEP_N ? l->sep : 0]);
    buf_adds(b, "/>");
  }
  if (any && ri->nac) {
    buf_adds(b, "<w:autoCaptions>");
    for (i = 0; i < ri->nac; i++) {
      if (!ri->ac[i].on) continue;
      buf_adds(b, "<w:autoCaption w:name=\""); xml_escape(b, ri->ac[i].name, strlen(ri->ac[i].name));
      buf_adds(b, "\" w:caption=\""); xml_escape(b, ri->ac[i].label, strlen(ri->ac[i].label)); buf_adds(b, "\"/>");
    }
    buf_adds(b, "</w:autoCaptions>");
  }
  buf_adds(b, "</w:captions>");
}

/* ------------------------------------------------------------------ */
/* shared helpers (refsi.h) */
int ri_add(PList *l, Para *p)
{
  if (!p) { l->oom = 1; return 0; }
  if (l->n == l->cap) {
    Para **np = (Para **)realloc(l->p, (l->cap + 32) * sizeof(Para *));
    if (!np) { para_free(p); l->oom = 1; return 0; }
    l->p = np; l->cap += 32;
  }
  l->p[l->n++] = p;
  return 1;
}
void ri_free(PList *l)
{
  u32 i;
  for (i = 0; i < l->n; i++) para_free(l->p[i]);
  free(l->p);
  memset(l, 0, sizeof *l);
}
Para *ri_para(Doc *d, int style)
{
  Para *p;
  if (style < 0 || style >= d->nstyles) style = 0;
  if (!(p = para_new(&d->styles[style].pf, &d->styles[style].cf))) return NULL;
  p->pf.style = (u8)style;
  memset(&p->pf.cell, 0, sizeof p->pf.cell); memset(&p->pf.outer, 0, sizeof p->pf.outer);
  p->pf.sect = 0; p->pf.pagebreak = 0;
  return p;
}
int ri_text(Para *p, const char *s, const CharFmt *f) { return !*s || para_append(p, s, (u32)strlen(s), f ? f : &p->r[p->nr - 1].f); }
u32 ri_next_mark(const Para *q, u32 off)
{
  const u8 *t = (const u8 *)q->t;
  for (; off + 4 <= q->len; off++) if (t[off] == 0xF4 && IS_FMARK(q->t + off)) return off;
  return q->len;
}
int ri_field(const Doc *d, const Para *q, u32 off, char *name, size_t cap)
{
  int k;
  if (cap) name[0] = 0;
  if (off + 4 > q->len || !IS_FSTART(q->t + off)) return -1;
  k = fmark_index(q->t + off);
  if (k >= d->nfields) return -1;
  if (cap) field_name(d->fields[k].code, name, cap);
  return k;
}
u32 ri_field_end(const Para *q, u32 off) { return field_end_in_para(q, off); }
u8 *ri_block_mask(Doc *d)
{
  u8 *m = NULL;
  u32 pi;
  Pos st, en;
  for (pi = 0; pi < d->np;) {
    if (refs_block_find(d, BK_NONE, pi, &st, &en) < 0) break;
    if (!m && !(m = (u8 *)calloc(d->np, 1))) return NULL;
    for (pi = st.para; pi <= en.para && pi < d->np; pi++) m[pi] = 1;
  }
  return m;
}
u32 ri_moved_from = 0xFFFFFFFFUL;
long ri_moved_by;
long ri_page(Doc *d, Layout *L, Pos p)
{
  u32 i;
  long page = -1;
  const ParaLay *pl;
  if (!L || p.para >= d->np) return -1;
  if (L->npl != d->np) {
    /* (a block field after this one was just made afresh: the layout's paragraphs after it moved) */
    if (p.para >= ri_moved_from) { long q = (long)p.para - ri_moved_by; if (q < 0) return -1; p.para = (u32)q; }
  }
  if (p.para >= L->npl || !L->pl[p.para].laid) return -1;
  /* the placed line holding it (not lay_caret: the layout's paragraphs may have been replaced since,
     and only its own lines and pages are still good) */
  pl = &L->pl[p.para];
  for (i = 0; i < L->nplaced; i++) {
    const Placed *pc = &L->placed[i];
    if (pc->para != p.para || pc->line >= pl->nlines) continue;
    if (page < 0) page = (long)pc->page;
    if (p.off >= pl->lines[pc->line].start) page = (long)pc->page;
    else break;
  }
  return page;
}
static void roman_s(char *o, u32 n, int upper)
{
  static const char *const r[13] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
  static const u32 v[13] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
  int i;
  *o = 0;
  if (!n || n > 3999) { sprintf(o, "%lu", (unsigned long)n); return; }
  for (i = 0; i < 13; i++) while (n >= v[i]) { strcat(o, r[i]); n -= v[i]; }
  if (upper) for (; *o; o++) if (*o >= 'a' && *o <= 'z') *o = (char)(*o - 32);
}
/* a counter in a list numbering format (NF_), as the layout shows it */
static void counter(char *o, u32 n, int fmt)
{
  switch (fmt) {
  case NF_LOWER_LETTER: case NF_UPPER_LETTER: {
    int k = (int)((n ? n - 1 : 0) % 26), rep = (int)((n ? n - 1 : 0) / 26) + 1, i;
    if (rep > 8) rep = 8;
    for (i = 0; i < rep; i++) o[i] = (char)((fmt == NF_UPPER_LETTER ? 'A' : 'a') + k);
    o[rep] = 0;
    break; }
  case NF_LOWER_ROMAN: roman_s(o, n, 0); break;
  case NF_UPPER_ROMAN: roman_s(o, n, 1); break;
  case NF_NONE: *o = 0; break;
  case NF_BULLET: *o = 0; break;
  default: if (!nf_counter(o, n, fmt)) sprintf(o, "%lu", (unsigned long)n);
  }
}
void ri_pagenum(Doc *d, long page, char *out)
{
  long first = d->pgstart > 1 ? d->pgstart : 1;
  counter(out, (u32)(page + first), d->pgfmt == NF_BULLET ? NF_DECIMAL : d->pgfmt);
}
void ri_fold(int l, const char *s, char *out, size_t cap)
{
  size_t n = 0, len = strlen(s), i = 0;
#define PUTC(c) do { if (n + 1 < cap) out[n++] = (char)(c); } while (0)
  while (i < len) {
    int used;
    u32 c = utf8_get(s + i, len - i, &used);
    i += (size_t)used;
    if (c >= 'A' && c <= 'Z') PUTC(c + 32);
    else if (c < 0x80) PUTC(c);
    else if (c >= 0xC0 && c <= 0xFF) {
      static const char map[] = "aaaaaaaceeeeiiiidnooooo/ouuuuyts" "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
      u32 lc = c >= 0xE0 ? c - 0xE0 : c - 0xC0;
      if (lc == 6) { PUTC('a'); PUTC('e'); }                      /* ae */
      else if (c == 0xDF) { PUTC('s'); PUTC('s'); }              /* sharp s */
      else if (lc == 30) { PUTC('t'); PUTC('h'); }               /* thorn */
      else if (lc == 17 && l == RL_ES) { PUTC('n'); PUTC('{'); }   /* Spanish: n with tilde a letter of its own, after n */
      else if (lc == 23) PUTC('*');                               /* (times and divided by) */
      else PUTC(map[lc]);
    } else if (c == 0x152 || c == 0x153) { PUTC('o'); PUTC('e'); }
    else { char u[5]; int k, m = utf8_put(u, c); for (k = 0; k < m; k++) PUTC(u[k]); }
  }
  if (cap) out[n] = 0;
#undef PUTC
}
i32 ri_text_width(Doc *d, u32 pi)
{
  const SectFmt *s = doc_sect(d, pi < d->np ? pi : d->np - 1);
  const PageSetup *pg = sect_page(d, s, doc_last_sect(d, pi < d->np ? pi : d->np - 1));
  i32 w = pg->width - pg->left - pg->right - pg->gutter;
  if (s && s->cols > 1) w = (w - (s->cols - 1) * (s->colgap ? s->colgap : 720)) / s->cols;
  return w > 1440 ? w : 1440;
}
void refs_plain(const Doc *d, u32 pi, u32 a, u32 b, char *out, size_t cap)
{
  const Para *q;
  u32 o, n = 0;
  if (cap) out[0] = 0;
  if (pi >= d->np) return;
  q = d->p[pi];
  if (b > q->len) b = q->len;
  for (o = a; o < b;) {
    u8 c = (u8)q->t[o];
    u32 w;
    if ((c == 0xF3 || c == 0xF4) && o + 4 <= q->len) { o += 4; continue; }     /* marks, pictures, notes: nothing */
    if (c == 0xEE && o + 3 <= q->len && IS_FIELD(q->t + o)) { o += 3; continue; }
    w = (u32)utf8_next(q->t, q->len, o) - o;
    if (!w) w = 1;
    if (n + w + 1 > cap) break;
    if (c == '\t' || c == '\v') out[n++] = ' ';
    else { memcpy(out + n, q->t + o, w); n += w; }
    o += w;
  }
  if (cap) out[n] = 0;
}

/* ------------------------------------------------------------------ */
/* styles */
static void tab_right(ParaFmt *pf, i32 at, int lead)
{
  memset(pf->tabs, 0, sizeof pf->tabs); memset(pf->tabkind, 0, sizeof pf->tabkind); memset(pf->tablead, 0, sizeof pf->tablead);
  pf->tabs[0] = at; pf->tabkind[0] = TAB_RIGHT; pf->tablead[0] = (u8)lead;
}
int refs_style(Doc *d, int kind, int level)
{
  char id[40], name[48];
  int k, based = 0;
  Style s;
  i32 w = d->page.width - d->page.left - d->page.right;
  if (level < 1) level = 1;
  if (level > 9) level = 9;
  switch (kind) {
  case RS_TOC: sprintf(id, "TOC%d", level); sprintf(name, "toc %d", level); break;
  case RS_TOF: strcpy(id, "TableofFigures"); strcpy(name, "table of figures"); break;
  case RS_INDEX: sprintf(id, "Index%d", level); sprintf(name, "index %d", level); break;
  case RS_INDEXHEAD: strcpy(id, "IndexHeading"); strcpy(name, "index heading"); break;
  case RS_TOA: strcpy(id, "TableofAuthorities"); strcpy(name, "table of authorities"); break;
  case RS_TOAHEAD: strcpy(id, "TOAHeading"); strcpy(name, "toa heading"); break;
  case RS_CAPTION: strcpy(id, "Caption"); strcpy(name, "caption"); break;
  default: strcpy(id, "TOCHeading"); strcpy(name, "TOC Heading"); break;
  }
  if ((k = doc_style_by_name(d, name)) >= 0 || (k = doc_style(d, id)) >= 0) return k;
  if (kind == RS_TOCHEAD) {
    if ((based = doc_style(d, "Heading1")) < 0 && (based = doc_style_by_name(d, "heading 1")) < 0) based = 0;
  }
  s = d->styles[based];
  strcpy(s.id, id); strcpy(s.name, name);
  s.builtin = 0; s.next = 0; s.quick = 0; s.type = ST_PARA;
  s.pf.keepnext = 0; s.pf.outlvl = 0; s.pf.list = 0; s.pf.level = 0;
  if (kind != RS_TOCHEAD) { s.pf.before = 0; s.pf.after = 0; s.pf.left = 0; s.pf.first = 0; }
  memset(s.pf.tabs, 0, sizeof s.pf.tabs); memset(s.pf.tabkind, 0, sizeof s.pf.tabkind); memset(s.pf.tablead, 0, sizeof s.pf.tablead);
  switch (kind) {
  case RS_TOC: s.pf.left = (level - 1) * 220; s.pf.after = 100; tab_right(&s.pf, w - s.pf.left, LEAD_DOT); break;
  case RS_TOF: tab_right(&s.pf, w, LEAD_DOT); break;
  case RS_INDEX: s.pf.left = 220 * level; s.pf.first = -220; break;
  case RS_INDEXHEAD: s.cf.flags |= CF_BOLD; s.pf.before = 120; s.pf.after = 60; s.pf.keepnext = 1; break;
  case RS_TOA: s.pf.left = 220; s.pf.first = -220; tab_right(&s.pf, w - 220, LEAD_DOT); break;
  case RS_TOAHEAD: s.cf.flags |= CF_BOLD; s.pf.before = 120; s.pf.keepnext = 1; break;
  case RS_CAPTION: s.cf.flags |= CF_ITALIC; s.cf.size = 18; s.cf.color = 0x44546A; s.pf.after = 200; break;
  }
  s.pf.style = (u8)d->nstyles;
  return doc_add_style_from(d, &s, kind == RS_TOCHEAD ? based : 0);
}

/* ------------------------------------------------------------------ */
/* list numbers */
static int is_delim(char c) { return c == '.' || c == '-' || c == ':' || c == ',' || c == '/' || c == '\xE2'; }
/* a level's label from its text with the counters c (9 of them) */
static void level_label(const ListDef *l, int lv, const u32 *c, int suppress, char *out, size_t cap)
{
  const char *t = l->lvl[lv].text;
  size_t o = 0;
  out[0] = 0;
  if (l->lvl[lv].fmt == NF_BULLET) return;
  for (; *t && o + 16 < cap; t++) {
    if (t[0] == '%' && t[1] >= '1' && t[1] <= '9') {
      int k = t[1] - '1';
      char num[24];
      counter(num, c[k] ? c[k] : l->lvl[k].start, k == lv ? l->lvl[lv].fmt : l->lvl[k].fmt);
      strcpy(out + o, num); o += strlen(num);
      t++;
    } else if (!suppress || (is_delim(*t) && strchr(t, '%'))) out[o++] = *t;
  }
  out[o] = 0;
}
static void strip_periods(char *s) { size_t n = strlen(s); while (n && (s[n - 1] == '.' || s[n - 1] == ' ')) s[--n] = 0; }
void refs_para_number(Doc *d, u32 pi, int mode, u32 from, char *out, size_t cap)
{
  u32 *cnt, fromc[9], tc[9], i, last;
  int list, lv, fromsame = 0, j;
  const ListDef *l;
  if (cap) out[0] = 0;
  if (pi >= d->np || !d->nlists) return;
  list = d->p[pi]->pf.list; lv = d->p[pi]->pf.level;
  if (!list || list > d->nlists || lv > 8 || d->lists[list - 1].lvl[lv].fmt == NF_BULLET) return;
  if (!(cnt = (u32 *)calloc((size_t)d->nlists * 9, sizeof(u32)))) return;
  memset(fromc, 0, sizeof fromc); memset(tc, 0, sizeof tc);
  last = mode == RN_REL && from > pi ? from : pi;
  if (last >= d->np) last = d->np - 1;
  for (i = 0; i <= last; i++) {
    const ParaFmt *pf = &d->p[i]->pf;
    if (pf->list && pf->list <= d->nlists && pf->level <= 8) {
      u32 *c = cnt + (pf->list - 1) * 9;
      const ListLevel *ll = &d->lists[pf->list - 1].lvl[pf->level];
      int k;
      c[pf->level] = c[pf->level] ? c[pf->level] + 1 : ll->start;
      for (k = pf->level + 1; k < 9; k++) c[k] = 0;
    }
    if (i == pi) memcpy(tc, cnt + (list - 1) * 9, sizeof tc);
    if (mode == RN_REL && i == from) { memcpy(fromc, cnt + (list - 1) * 9, sizeof fromc); fromsame = d->p[i]->pf.list == list || i < pi; }
  }
  free(cnt);
  l = &d->lists[list - 1];
  if (mode == RN_PLAIN || mode == RN_SUPPRESS) {
    level_label(l, lv, tc, mode == RN_SUPPRESS, out, cap);
    strip_periods(out);
    return;
  }
  {
    /* each level's own label, from the first one not shared with where it is seen from */
    int start = 0, refsall = 1;
    size_t o = 0;
    char piece[64];
    for (j = 0; j <= lv; j++) { char tok[16]; sprintf(tok, "%%%d", j + 1); if (!strstr(l->lvl[lv].text, tok)) refsall = 0; }
    if (mode == RN_REL && fromsame) {
      while (start < lv && fromc[start] == tc[start]) start++;
      if (refsall && start == 0) { level_label(l, lv, tc, 0, out, cap); strip_periods(out); return; }
    } else if (refsall) { level_label(l, lv, tc, 0, out, cap); strip_periods(out); return; }
    for (j = start; j <= lv && o + 1 < cap; j++) {
      u32 c2[9];
      memcpy(c2, tc, sizeof c2);
      /* a level's own number only: the levels above it as their own labels give them */
      {
        const char *t = l->lvl[j].text;
        size_t q = 0;
        piece[0] = 0;
        for (; *t && q + 16 < sizeof piece; t++) {
          if (t[0] == '%' && t[1] >= '1' && t[1] <= '9') {
            int k = t[1] - '1';
            if (k == j) { char num[24]; counter(num, c2[k] ? c2[k] : l->lvl[k].start, l->lvl[k].fmt); strcpy(piece + q, num); q += strlen(num); }
            t++;
          } else piece[q++] = *t;
        }
        piece[q] = 0;
      }
      if (j < lv && !piece[0]) continue;
      snprintf(out + o, cap - o, "%s", piece);
      o = strlen(out);
    }
    strip_periods(out);
  }
}

/* ------------------------------------------------------------------ */
/* bookmarks for references */
/* whether a REF or NOTEREF field (any record) refers to bookmark name */
static int referred(const Doc *d, const char *name)
{
  int k;
  char nm[16], a[128];
  for (k = 0; k < d->nfields; k++) {
    field_name(d->fields[k].code, nm, sizeof nm);
    if ((!strcmp(nm, "REF") || !strcmp(nm, "NOTEREF")) && field_arg(d->fields[k].code, 0, a, sizeof a) && !strcmp(a, name)) return 1;
  }
  return 0;
}
static int new_name(Doc *d, const char *prefix, char *name)
{
  long n = 100000000L + (long)d->nbmarks * 7 + 1;
  int tries;
  for (tries = 0; tries < 10000; tries++, n += 13) {
    snprintf(name, 64, "%s%ld", prefix, n);
    if (!doc_bmark_find(d, name, NULL, NULL)) return 1;
  }
  return 0;
}
int refs_target(Doc *d, u32 pi, u32 a, u32 b, int exact, const char *prefix, char *name)
{
  Para *q;
  u32 r, s = 0;
  int found = 0, other = 0, toc = 0, tocinside = 1;
  Pos pa, pb, fa, fb;
  name[0] = 0;
  if (pi >= d->np) return 0;
  q = d->p[pi];
  if (b > q->len) b = q->len;
  if (!exact) { a = 0; b = q->len; }
  if (a >= b) return 0;
  /* the bookmarks in the paragraph */
  for (r = 0; r < q->nr; s = q->r[r].end, r++) {
    u16 k = q->r[r].f.bmark;
    const char *nm;
    if (!k || q->r[r].end <= s) continue;
    nm = doc_bmark_name(d, k);
    if (!exact) { snprintf(name, 64, "%s", nm); return 1; }
    if (q->r[r].end <= a || s >= b) {
      /* beside the stretch: one that also runs into it is looked at below */
      continue;
    }
    if (!strncmp(nm, "_Toc", 4) && !referred(d, nm)) {
      if (!toc) toc = k;
      else if (toc != k) other = 1;
      if (s < a || q->r[r].end > b) tocinside = 0;
    } else {
      if (!found) found = k;
      else if (found != k) other = 1;
    }
  }
  pa.para = pb.para = pi; pa.off = a; pb.off = b;
  if (found && !other && !toc && doc_bmark_find(d, doc_bmark_name(d, found), &fa, &fb) && !pos_cmp(fa, pa) && !pos_cmp(fb, pb)) {
    snprintf(name, 64, "%s", doc_bmark_name(d, found));
    return 1;
  }
  if (found || other) return 0;
  if (toc && tocinside && doc_bmark_find(d, doc_bmark_name(d, toc), &fa, &fb) && fa.para == pi && fb.para == pi && fa.off >= a && fb.off <= b) {
    /* a _Toc bookmark only inside the stretch: it grows to cover it (the table's links still work) */
    snprintf(name, 64, "%s", doc_bmark_name(d, toc));
  } else if (!new_name(d, prefix, name)) return 0;
  if (!doc_set_bmark(d, pa, pb, name)) { name[0] = 0; return 0; }
  return 1;
}

/* ------------------------------------------------------------------ */
/* evaluating REF, NOTEREF, STYLEREF, PAGEREF \p */
static const char *above_below(Doc *d, Pos at, Pos target) { (void)d; return refs_word(refs_lang, pos_cmp(target, at) < 0 ? RW_ABOVE : RW_BELOW); }
/* a note's number as its section's options show it, counted in the text up to its reference at p */
static void note_number(Doc *d, Pos p, int end, char *out)
{
  u32 pi, n = 0, s0 = 0, o;
  NotePr np;
  notes_eff(d, doc_sect(d, p.para), end, &np);
  if (np.restart == NRS_SECTION) for (pi = 0; pi < p.para; pi++) if (d->p[pi]->pf.sect) s0 = pi + 1;
  for (pi = s0; pi <= p.para && pi < d->np; pi++) {
    Para *q = d->p[pi];
    for (o = 0; o + 4 <= q->len; o++) {
      if (pi == p.para && o > p.off) break;
      if (end ? IS_ENOTE(q->t + o) : IS_NOTE(q->t + o)) { n++; o += 3; }
    }
  }
  notes_label(np.fmt, (np.start ? np.start : 1) + n - 1, out);
}
/* the first note reference in a..b (its place, whether an endnote); 0 none */
static int note_in(Doc *d, Pos a, Pos b, Pos *at, int *end)
{
  u32 pi, o;
  for (pi = a.para; pi <= b.para && pi < d->np; pi++) {
    Para *q = d->p[pi];
    for (o = pi == a.para ? a.off : 0; o + 4 <= q->len && (pi < b.para || o < b.off); o++) {
      if (IS_NOTE(q->t + o) || IS_ENOTE(q->t + o)) { at->para = pi; at->off = o; *end = IS_ENOTE(q->t + o); return 1; }
    }
  }
  return 0;
}
static int style_matches(Doc *d, u32 pi, const char *style)
{
  const Style *st = &d->styles[d->p[pi]->pf.style < d->nstyles ? d->p[pi]->pf.style : 0];
  if (style[0] >= '1' && style[0] <= '9' && !style[1]) return heading_style_level(st->name) == style[0] - '0';
  return str_ieq(st->name, style) || str_ieq(st->id, style) || (heading_style_level(st->name) && !strncmp(style, "Heading ", 8) && heading_style_level(st->name) == atoi(style + 8));
}
int refs_eval(Doc *d, const char *nm, const char *flat, Pos at, const struct FieldEnv *e, char *out, size_t cap)
{
  char bm[200], num[128], sep[16];
  Pos a, b;
  int n_ = 0, r_ = 0, w_ = 0, p_, d_;
  if (cap) out[0] = 0;
  if (!strcmp(nm, "REF") || !strcmp(nm, "NOTEREF") || !strcmp(nm, "PAGEREF")) {
    p_ = field_switch(flat, 'p', 0, NULL, 0);
    if (!strcmp(nm, "PAGEREF") && !p_) return -1;
    if (!strcmp(nm, "REF")) {
      n_ = field_switch(flat, 'n', 0, NULL, 0); r_ = field_switch(flat, 'r', 0, NULL, 0); w_ = field_switch(flat, 'w', 0, NULL, 0);
      if (!n_ && !r_ && !w_ && !p_) return -1;
    }
    if (!field_arg(flat, 0, bm, sizeof bm) || !doc_bmark_find(d, bm, &a, &b)) return FE_KEEP;
    if (!strcmp(nm, "PAGEREF")) {
      long pf = ri_page(d, e ? e->L : NULL, at), pt = ri_page(d, e ? e->L : NULL, a);
      if (pf >= 0 && pf == pt) snprintf(out, cap, "%s", above_below(d, at, a));
      else snprintf(out, cap, "%s %s", refs_word(refs_lang, RW_ONPAGE), FIELD_PAGEREF);
      return FE_TEXT;
    }
    if (!strcmp(nm, "NOTEREF")) {
      Pos np;
      int end;
      if (!note_in(d, a, b, &np, &end)) return FE_KEEP;
      note_number(d, np, end, num);
      if (p_) snprintf(out, cap, "%s %s", num, above_below(d, at, a));
      else snprintf(out, cap, "%s", num);
      return FE_TEXT;
    }
    num[0] = 0;
    if (n_ || r_ || w_) {
      refs_para_number(d, a.para, w_ ? RN_FULL : r_ ? RN_REL : RN_PLAIN, at.para, num, sizeof num);
      if (!num[0]) strcpy(num, "0");
      d_ = field_switch(flat, 'd', 0, sep, sizeof sep);
      if (d_ && sep[0]) {
        char t[128];
        size_t i, o = 0;
        for (i = 0; num[i] && o + strlen(sep) + 1 < sizeof t; i++) {
          if (num[i] == '.') { strcpy(t + o, sep); o += strlen(sep); } else t[o++] = num[i];
        }
        t[o] = 0;
        strcpy(num, t);
      }
    }
    if (p_) snprintf(out, cap, num[0] ? "%s %s" : "%s%s", num, above_below(d, at, a));
    else snprintf(out, cap, "%s", num);
    field_format_case(flat, out, cap);
    return FE_TEXT;
  }
  if (!strcmp(nm, "STYLEREF")) {
    char style[64];
    long pi, found = -1;
    if (!field_arg(flat, 0, style, sizeof style)) return FE_KEEP;
    if (at.para >= d->np) at.para = d->np ? d->np - 1 : 0;
    for (pi = (long)at.para; pi >= 0 && found < 0; pi--) if (style_matches(d, (u32)pi, style)) found = pi;
    for (pi = (long)at.para + 1; pi < (long)d->np && found < 0; pi++) if (style_matches(d, (u32)pi, style)) found = pi;
    if (found < 0) { snprintf(out, cap, "%s", refs_word(refs_lang, RW_NOSTYLE)); return FE_TEXT; }
    if (field_switch(flat, 's', 0, NULL, 0)) refs_para_number(d, (u32)found, RN_SUPPRESS, at.para, out, cap);
    else if (field_switch(flat, 'n', 0, NULL, 0)) refs_para_number(d, (u32)found, RN_PLAIN, at.para, out, cap);
    else if (field_switch(flat, 'w', 0, NULL, 0)) refs_para_number(d, (u32)found, RN_FULL, at.para, out, cap);
    else if (field_switch(flat, 'r', 0, NULL, 0)) refs_para_number(d, (u32)found, RN_REL, at.para, out, cap);
    else { refs_plain(d, (u32)found, 0, d->p[found]->len, out, cap); field_format_case(flat, out, cap); return FE_TEXT; }
    if (!out[0]) snprintf(out, cap, "%s", refs_word(refs_lang, RW_NOSTYLE));
    return FE_TEXT;
  }
  return -1;
}

/* ------------------------------------------------------------------ */
/* captions */
static void seq_id(const char *label, char *out, size_t cap)
{
  size_t i;
  snprintf(out, cap, "%s", label);
  for (i = 0; out[i]; i++) if (out[i] == ' ') out[i] = '_';       /* (Word's: a SEQ name has no spaces) */
}
void refs_seq_code(const CapLabel *l, char *out, size_t cap)
{
  char id[48];
  seq_id(l->name, id, sizeof id);
  if (l->chap) snprintf(out, cap, " SEQ %s \\* %s \\s %d ", id, cn_sw[l->fmt < CN_N ? l->fmt : 0], l->heading ? l->heading : 1);
  else snprintf(out, cap, " SEQ %s \\* %s ", id, cn_sw[l->fmt < CN_N ? l->fmt : 0]);
}
/* the table round paragraph pi: its first and last paragraphs */
static int table_span(Doc *d, u32 pi, u32 *a, u32 *b)
{
  u16 t = top_cell(&d->p[pi]->pf)->table;
  if (!t) return 0;
  *a = *b = pi;
  while (*a > 0 && top_cell(&d->p[*a - 1]->pf)->table == t) (*a)--;
  while (*b + 1 < d->np && top_cell(&d->p[*b + 1]->pf)->table == t) (*b)++;
  return 1;
}
static int has_picture(const Para *q) { u32 o; for (o = 0; o + 4 <= q->len; o++) if (IS_PIC(q->t + o)) return 1; return 0; }
static void env_or(const struct FieldEnv *e, FieldEnv *own, const FieldEnv **use)
{
  if (e) { *use = e; return; }
  field_env(own);
  *use = own;
}
int refs_insert_caption(Doc *d, Pos *at, int li, const char *text, int pos, const struct FieldEnv *e)
{
  RefsInfo *ri = refs_info(d);
  const CapLabel *l;
  Para *p;
  u32 pi, ta, tb, ip;
  int replace = 0, k1 = -1, k2, st;
  char code[160], m[8];
  CharFmt cf;
  FieldEnv own;
  const FieldEnv *fe;
  Pos where;
  if (!ri || li < 0 || li >= ri->nlab) return 0;
  l = &ri->lab[li];
  *at = pos_clamp(d, *at);
  pi = at->para;
  if (table_span(d, pi, &ta, &tb)) ip = (pos < 0 ? l->pos : pos) == CP_ABOVE ? ta : tb + 1;
  else if (has_picture(d->p[pi])) ip = (pos < 0 ? l->pos : pos) == CP_ABOVE ? pi : pi + 1;
  else if (!d->p[pi]->len) { ip = pi; replace = 1; }
  else ip = pi + 1;
  if ((st = refs_style(d, RS_CAPTION, 1)) < 0) return 0;
  if (!(p = ri_para(d, st))) return 0;
  if (replace) p->pf.sect = d->p[pi]->pf.sect;
  cf = p->r[0].f;
  if (!l->nolabel) { char t[48]; snprintf(t, sizeof t, "%s ", l->name); ri_text(p, t, &cf); }
  if (l->chap) {
    snprintf(code, sizeof code, " STYLEREF %d \\s ", l->heading ? l->heading : 1);
    if ((k1 = field_new(d, code, 0)) < 0) { para_free(p); return 0; }
    fmark_utf8(m, k1, 0); ri_text(p, m, &cf);
    fmark_utf8(m, k1, 1); ri_text(p, m, &cf);
    ri_text(p, refs_sep_text(l->sep), &cf);
  }
  refs_seq_code(l, code, sizeof code);
  if ((k2 = field_new(d, code, 0)) < 0) { para_free(p); return 0; }
  fmark_utf8(m, k2, 0); ri_text(p, m, &cf);
  fmark_utf8(m, k2, 1); ri_text(p, m, &cf);
  if (text && text[0]) ri_text(p, text, &cf);
  para_normalise(p);
  where.para = ip; where.off = 0;
  doc_begin(d, *at);
  if (!doc_replace(d, ip, replace ? 1 : 0, &p, 1, where)) { doc_end(d, *at); return 0; }
  env_or(e, &own, &fe);
  {
    char id[48];
    seq_id(l->name, id, sizeof id);
    refs_update_seq(d, id, fe);
  }
  at->para = ip; at->off = d->p[ip]->len;
  doc_end(d, *at);
  (void)k1;
  return 1;
}
void refs_caption_preview(Doc *d, Pos at, int li, char *out, size_t cap)
{
  RefsInfo *ri = refs_info(d);
  const CapLabel *l;
  char id[48], nm[16], a[64], num[32], chap[32] = "";
  u32 pi, o, n = 0;
  long head = -1;
  if (cap) out[0] = 0;
  if (!ri || li < 0 || li >= ri->nlab) return;
  l = &ri->lab[li];
  seq_id(l->name, id, sizeof id);
  at = pos_clamp(d, at);
  for (pi = 0; pi <= at.para && pi < d->np; pi++) {
    Para *q = d->p[pi];
    int hl = heading_style_level(d->styles[q->pf.style < d->nstyles ? q->pf.style : 0].name);
    if (l->chap && hl && hl <= (l->heading ? l->heading : 1)) { n = 0; head = (long)pi; }
    for (o = ri_next_mark(q, 0); o < q->len; o = ri_next_mark(q, o + 4)) {
      int k = ri_field(d, q, o, nm, sizeof nm);
      if (k >= 0 && !strcmp(nm, "SEQ") && field_arg(d->fields[k].code, 0, a, sizeof a) && str_ieq(a, id)) n++;
    }
  }
  {
    char code[80];
    sprintf(code, " SEQ x \\* %s ", cn_sw[l->fmt < CN_N ? l->fmt : 0]);
    field_format_number(code, (long)n + 1, num, sizeof num);
  }
  if (l->chap) {
    if (head >= 0) refs_para_number(d, (u32)head, RN_SUPPRESS, at.para, chap, sizeof chap);
    if (!chap[0]) strcpy(chap, "0");
    snprintf(out, cap, "%s%s%s%s%s", l->nolabel ? "" : l->name, l->nolabel ? "" : " ", chap, refs_sep_text(l->sep), num);
  } else snprintf(out, cap, "%s%s%s", l->nolabel ? "" : l->name, l->nolabel ? "" : " ", num);
}
int refs_update_seq(Doc *d, const char *label, const struct FieldEnv *e)
{
  Pos *at = NULL;
  int n = 0, cap = 0, i, done = 0;
  u32 pi, o;
  char nm[16], a[64], id[48];
  FieldEnv own;
  const FieldEnv *fe;
  env_or(e, &own, &fe);
  if (label) seq_id(label, id, sizeof id);
  for (pi = 0; pi < d->np; pi++) {
    Para *q = d->p[pi];
    int hasseq = 0;
    for (o = ri_next_mark(q, 0); o < q->len; o = ri_next_mark(q, o + 4)) {
      int k = ri_field(d, q, o, nm, sizeof nm);
      if (k >= 0 && !strcmp(nm, "SEQ") && (!label || (field_arg(d->fields[k].code, 0, a, sizeof a) && str_ieq(a, id)))) hasseq = 1;
    }
    if (!hasseq) continue;
    for (o = ri_next_mark(q, 0); o < q->len; o = ri_next_mark(q, o + 4)) {
      int k = ri_field(d, q, o, nm, sizeof nm);
      if (k < 0 || (strcmp(nm, "SEQ") && strcmp(nm, "STYLEREF"))) continue;
      if (n == cap) { Pos *np = (Pos *)realloc(at, (size_t)(cap + 64) * sizeof(Pos)); if (!np) break; at = np; cap += 64; }
      at[n].para = pi; at[n].off = o; n++;
    }
  }
  if (n) {
    doc_begin(d, at[0]);
    for (i = n; i-- > 0;) done += field_update(d, at[i], at[i], fe);
    doc_end(d, at[0]);
  }
  free(at);
  return done;
}
int refs_autocaption(Doc *d, Pos at, int table, const struct FieldEnv *e)
{
  RefsInfo *ri = d->refs;
  int i, li;
  if (!ri) return 0;
  for (i = 0; i < ri->nac; i++) {
    const AutoCap *a = &ri->ac[i];
    int istable = strstr(a->name, "Table") != NULL;
    if (!a->on || istable != (table != 0)) continue;
    if ((li = refs_label(d, a->label)) < 0) continue;
    return refs_insert_caption(d, &at, li, "", -1, e);
  }
  return 0;
}

/* ------------------------------------------------------------------ */
/* marking entries */
void refs_quote(const char *s, int colons, char *out, size_t cap)
{
  size_t n = 0;
  for (; *s && n + 3 < cap; s++) {
    if (*s == '"' || *s == '\\' || (colons && *s == ':')) out[n++] = '\\';
    out[n++] = *s;
  }
  if (cap) out[n] = 0;
}
/* a field with no result (XE, TA, TC) at *at, its marks hidden text as Word writes them */
static int mark_field(Doc *d, Pos *at, const char *code)
{
  CharFmt f;
  int k;
  *at = pos_clamp(d, *at);
  f = *para_fmt_before(d->p[at->para], at->off);
  f.link = 0; f.bmark = 0;
  f.fx2 |= FX2_HIDDEN;
  k = field_insert_result(d, at, code, "", NULL, &f);
  if (k >= 0) d->fields[k].nosep = 1;
  return k;
}
static void xe_code(const char *mainq, const char *sub, const char *xref, const char *bm, int bold, int italic, char *code, size_t cap)
{
  char s[300], x[300], b[200];
  size_t n;
  snprintf(code, cap, " XE \"%s", mainq);
  if (sub && sub[0]) { refs_quote(sub, 1, s, sizeof s); n = strlen(code); snprintf(code + n, cap - n, ":%s", s); }
  n = strlen(code); snprintf(code + n, cap - n, "\" ");
  if (xref && xref[0]) { refs_quote(xref, 0, x, sizeof x); n = strlen(code); snprintf(code + n, cap - n, "\\t \"%s\" ", x); }
  else if (bm && bm[0]) { refs_quote(bm, 0, b, sizeof b); n = strlen(code); snprintf(code + n, cap - n, "\\r \"%s\" ", b); }
  if (bold) { n = strlen(code); snprintf(code + n, cap - n, "\\b "); }
  if (italic) { n = strlen(code); snprintf(code + n, cap - n, "\\i "); }
}
int refs_mark_entry(Doc *d, Pos *at, const char *main, const char *sub, const char *xref, const char *bm, int bold, int italic)
{
  char mq[300], code[1200];
  Pos p = *at;
  int k;
  if (!main || !main[0]) return -1;
  refs_quote(main, 1, mq, sizeof mq);
  xe_code(mq, sub, xref, bm, bold, italic, code, sizeof code);
  doc_begin(d, p);
  k = mark_field(d, at, code);
  doc_end(d, *at);
  return k;
}
static int word_char(const char *t, u32 o, u32 len)
{
  u8 c;
  if (o >= len) return 0;
  c = (u8)t[o];
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c >= 0x80;
}
/* the first whole-word, same-case occurrence of what in q at or after from (q->len if none) */
static u32 find_in(const Para *q, const char *what, u32 from, int whole)
{
  u32 n = (u32)strlen(what), o;
  if (!n) return q->len;
  for (o = from; o + n <= q->len; o++) {
    if (memcmp(q->t + o, what, n)) continue;
    if (whole && ((o > 0 && word_char(q->t, o - 1, q->len) && (u8)q->t[o - 1] < 0xF0) || (word_char(q->t, o + n, q->len) && (u8)q->t[o + n] < 0xF0))) continue;
    return o;
  }
  return q->len;
}
/* the code given, after the first occurrence in each paragraph (not in block fields; not where the
   same code follows already); how many */
static int mark_all_code(Doc *d, const char *what, const char *code, int every, const char *avoid)
{
  u8 *mask = ri_block_mask(d);
  long pi;
  int n = 0;
  Pos c0;
  c0.para = 0; c0.off = 0;
  doc_begin(d, c0);
  for (pi = (long)d->np - 1; pi >= 0; pi--) {
    u32 o, from = 0, hits[64];
    int nh = 0, i;
    Para *q = d->p[pi];
    if (mask && mask[pi]) continue;
    while ((o = find_in(q, what, from, 1)) < q->len && nh < 64) {
      u32 after = o + (u32)strlen(what);
      int dup = avoid && avoid[0] && o + strlen(avoid) <= q->len && !memcmp(q->t + o, avoid, strlen(avoid));   /* (part of the long citation) */
      if (after + 4 <= q->len && IS_FSTART(q->t + after)) {
        int k = fmark_index(q->t + after);
        if (k < d->nfields && !strcmp(d->fields[k].code, code)) dup = 1;
      }
      if (!dup) hits[nh++] = after;
      if (!every) break;
      from = after;
    }
    for (i = nh; i-- > 0;) {
      Pos at;
      at.para = (u32)pi; at.off = hits[i];
      if (mark_field(d, &at, code) >= 0) n++;
    }
  }
  doc_end(d, c0);
  free(mask);
  return n;
}
int refs_mark_all(Doc *d, const char *what, const char *main, const char *sub, const char *xref, const char *bm, int bold, int italic)
{
  char mq[300], code[1200];
  if (!what || !what[0] || !main || !main[0]) return 0;
  refs_quote(main, 1, mq, sizeof mq);
  xe_code(mq, sub, xref, bm, bold, italic, code, sizeof code);
  return mark_all_code(d, what, code, 0, NULL);
}
/* a cell's or paragraph's text without marks */
static void cell_text(Doc *c, u32 pi, char *out, size_t cap)
{
  size_t n;
  refs_plain(c, pi, 0, c->p[pi]->len, out, cap);
  while (out[0] == ' ') memmove(out, out + 1, strlen(out));
  n = strlen(out);
  while (n && out[n - 1] == ' ') out[--n] = 0;
}
int refs_automark(Doc *d, Doc *conc)
{
  u32 pi;
  int n = 0;
  for (pi = 0; pi < conc->np; pi++) {
    char what[256], entry[256], eq[600], code[700];
    const Cell *c = top_cell(&conc->p[pi]->pf);
    what[0] = entry[0] = 0;
    if (c->table) {
      u32 pj;
      if (c->col != 0) continue;
      if (pi > 0 && same_cell(&conc->p[pi - 1]->pf, &conc->p[pi]->pf)) continue;     /* (a cell's later paragraphs) */
      cell_text(conc, pi, what, sizeof what);
      for (pj = pi + 1; pj < conc->np && top_cell(&conc->p[pj]->pf)->table == c->table && top_cell(&conc->p[pj]->pf)->row == c->row; pj++)
        if (top_cell(&conc->p[pj]->pf)->col == 1 && !same_cell(&conc->p[pj - 1]->pf, &conc->p[pj]->pf)) { cell_text(conc, pj, entry, sizeof entry); break; }
    } else {
      char line[512], *tab;
      refs_plain(conc, pi, 0, conc->p[pi]->len, line, sizeof line);
      /* (refs_plain turns tabs into spaces: read the tab from the paragraph itself) */
      tab = strchr(conc->p[pi]->t, '\t');
      if (tab) {
        size_t k = (size_t)(tab - conc->p[pi]->t);
        snprintf(what, sizeof what, "%.*s", (int)(k < sizeof what - 1 ? k : sizeof what - 1), conc->p[pi]->t);
        if (!k) what[0] = 0;
        snprintf(entry, sizeof entry, "%s", tab + 1);
      } else snprintf(what, sizeof what, "%.255s", line);
    }
    if (!what[0]) continue;
    if (!entry[0]) refs_quote(what, 1, eq, sizeof eq);
    else refs_quote(entry, 0, eq, sizeof eq);          /* (its colons make levels) */
    snprintf(code, sizeof code, " XE \"%s\" ", eq);
    n += mark_all_code(d, what, code, 0, NULL);
  }
  return n;
}
static void ta_code(const char *longc, const char *shortc, int cat, char *code, size_t cap)
{
  char l[600], s[300];
  refs_quote(longc ? longc : "", 0, l, sizeof l);
  refs_quote(shortc ? shortc : "", 0, s, sizeof s);
  if (longc && longc[0]) snprintf(code, cap, " TA \\l \"%s\" \\s \"%s\" \\c %d ", l, s[0] ? s : l, cat < 1 ? 1 : cat);
  else snprintf(code, cap, " TA \\s \"%s\" ", s);
}
int refs_mark_citation(Doc *d, Pos *at, const char *longc, const char *shortc, int cat)
{
  char code[1000];
  Pos p = *at;
  int k;
  if ((!longc || !longc[0]) && (!shortc || !shortc[0])) return -1;
  ta_code(longc, shortc, cat, code, sizeof code);
  doc_begin(d, p);
  k = mark_field(d, at, code);
  doc_end(d, *at);
  return k;
}
int refs_mark_all_citations(Doc *d, const char *longc, const char *shortc, int cat)
{
  char lcode[1000], scode[700];
  int n = 0;
  ta_code(longc, shortc, cat, lcode, sizeof lcode);
  ta_code(NULL, shortc && shortc[0] ? shortc : longc, cat, scode, sizeof scode);
  if (longc && longc[0]) n += mark_all_code(d, longc, scode, 1, NULL);
  if (shortc && shortc[0] && (!longc || strcmp(shortc, longc))) n += mark_all_code(d, shortc, scode, 1, longc);
  if (longc && longc[0] && n) {
    /* the first occurrence's mark carries the long citation */
    u32 pi, o;
    char nm[8];
    for (pi = 0; pi < d->np; pi++) {
      Para *q = d->p[pi];
      for (o = ri_next_mark(q, 0); o < q->len; o = ri_next_mark(q, o + 4)) {
        int k = ri_field(d, q, o, nm, sizeof nm);
        Pos s;
        if (k < 0 || strcmp(d->fields[k].code, scode)) continue;
        s.para = pi; s.off = o;
        field_set_code(d, s, lcode);
        return n;
      }
    }
  }
  return n;
}
int refs_next_citation(Doc *d, Pos p, Pos *a, Pos *b)
{
  static const char *const keys[] = { " v. ", "In re ", "Id.", "Ibid", "Supra", "supra", "Infra", "infra", "\xC2\xA7", "U.S.C.", "F.2d", "F.3d",
    "S. Ct.", " Act", "Stat.", "Cong.", "et seq", "Const.", "C.F.R." };
  u32 pi;
  size_t i;
  for (pi = p.para; pi < d->np; pi++) {
    Para *q = d->p[pi];
    u32 best = q->len, bl = 0;
    for (i = 0; i < sizeof keys / sizeof keys[0]; i++) {
      u32 o = find_in(q, keys[i], pi == p.para ? p.off : 0, 0);
      if (o < best) { best = o; bl = (u32)strlen(keys[i]); }
    }
    if (best < q->len) {
      /* the keyword without the spaces round it */
      while (bl && q->t[best] == ' ') { best++; bl--; }
      while (bl && q->t[best + bl - 1] == ' ') bl--;
      a->para = b->para = pi; a->off = best; b->off = best + bl;
      return 1;
    }
  }
  return 0;
}
static int cmp_str(const void *x, const void *y) { return strcmp(*(char *const *)x, *(char *const *)y); }
char **refs_citations(Doc *d, int *n)
{
  char **v = NULL, nm[8], l[600];
  int cap = 0, i;
  u32 pi, o;
  *n = 0;
  for (pi = 0; pi < d->np; pi++) {
    Para *q = d->p[pi];
    for (o = ri_next_mark(q, 0); o < q->len; o = ri_next_mark(q, o + 4)) {
      int k = ri_field(d, q, o, nm, sizeof nm), dup = 0;
      if (k < 0 || strcmp(nm, "TA") || !field_switch(d->fields[k].code, 'l', 0, l, sizeof l) || !l[0]) continue;
      for (i = 0; i < *n; i++) if (!strcmp(v[i], l)) dup = 1;
      if (dup) continue;
      if (*n == cap) { char **nv = (char **)realloc(v, (size_t)(cap + 16) * sizeof(char *)); if (!nv) return v; v = nv; cap += 16; }
      if ((v[*n] = str_dup(l))) (*n)++;
    }
  }
  if (*n > 1) qsort(v, (size_t)*n, sizeof(char *), cmp_str);
  return v;
}

/* ------------------------------------------------------------------ */
/* cross-references */
static int caption_label_of(Doc *d, u32 pi, char *id, size_t cap, u32 *seqs, u32 *seqe)
{
  Para *q = d->p[pi];
  u32 o;
  char nm[16];
  for (o = ri_next_mark(q, 0); o < q->len; o = ri_next_mark(q, o + 4)) {
    int k = ri_field(d, q, o, nm, sizeof nm);
    if (k >= 0 && !strcmp(nm, "SEQ") && field_arg(d->fields[k].code, 0, id, cap)) { if (seqs) *seqs = o; if (seqe) *seqe = ri_field_end(q, o); return 1; }
  }
  return 0;
}
int refs_targets(Doc *d, int type, RefTarget *t, int max)
{
  int n = 0;
  u32 pi, o;
  u8 *mask = ri_block_mask(d);
  char id[64], want[48];
  if (type >= RT_LABEL) {
    RefsInfo *ri = refs_info(d);
    if (!ri || type - RT_LABEL >= ri->nlab) { free(mask); return 0; }
    seq_id(ri->lab[type - RT_LABEL].name, want, sizeof want);
  }
  if (type == RT_BOOKMARK) {
    int k;
    for (k = 1; k <= d->nbmarks && n < max; k++) {
      const char *nm = doc_bmark_name(d, k);
      Pos a, b;
      if (!nm[0] || nm[0] == '_' || !doc_bmark_find(d, nm, &a, &b)) continue;
      memset(&t[n], 0, sizeof t[n]);
      t[n].para = a.para; t[n].off = a.off; t[n].note = -1;
      snprintf(t[n].text, sizeof t[n].text, "%s", nm);
      n++;
    }
    free(mask);
    return n;
  }
  for (pi = 0; pi < d->np && n < max; pi++) {
    Para *q = d->p[pi];
    if (mask && mask[pi]) continue;
    if (type == RT_FOOTNOTE || type == RT_ENDNOTE) {
      for (o = 0; o + 4 <= q->len && n < max; o++) {
        int end = IS_ENOTE(q->t + o);
        if (!(type == RT_ENDNOTE ? end : IS_NOTE(q->t + o))) continue;
        memset(&t[n], 0, sizeof t[n]);
        t[n].para = pi; t[n].off = o; t[n].note = note_index(q->t + o);
        {
          char num[16], tx[72] = "";
          Pos p;
          Story *s = end ? (t[n].note < d->nenotes ? &d->enotes[t[n].note] : NULL) : (t[n].note < d->nnotes ? &d->notes[t[n].note] : NULL);
          p.para = pi; p.off = o;
          note_number(d, p, end, num);
          if (s && s->np) {
            size_t k = 0, m = 0;
            const char *st = s->p[0]->t;
            while (st[m] && k + 1 < sizeof tx) { u8 c = (u8)st[m]; if ((c == 0xF3 || c == 0xF4)) { m += 4; continue; } tx[k++] = (char)c; m++; }
            tx[k] = 0;
          }
          snprintf(t[n].text, sizeof t[n].text, "%.15s %.72s", num, tx);
        }
        n++; o += 3;
      }
      continue;
    }
    if (!q->len) continue;
    if (type == RT_HEADING) {
      int lv = outline_level(d, pi);
      const Style *st = &d->styles[q->pf.style < d->nstyles ? q->pf.style : 0];
      if (!lv || !strncmp(st->name, "toc ", 4) || !strncmp(st->name, "TOC ", 4)) continue;
      memset(&t[n], 0, sizeof t[n]); t[n].para = pi; t[n].note = -1; t[n].level = (u8)lv;
      refs_plain(d, pi, 0, q->len, t[n].text, sizeof t[n].text);
      n++;
    } else if (type == RT_NUMBERED) {
      char num[48];
      refs_para_number(d, pi, RN_PLAIN, pi, num, sizeof num);
      if (!num[0]) continue;
      memset(&t[n], 0, sizeof t[n]); t[n].para = pi; t[n].note = -1; t[n].level = q->pf.level;
      {
        char tx[96];
        refs_plain(d, pi, 0, q->len, tx, sizeof tx);
        snprintf(t[n].text, sizeof t[n].text, "%.40s %.54s", num, tx);
      }
      n++;
    } else if (type >= RT_LABEL) {
      if (!caption_label_of(d, pi, id, sizeof id, NULL, NULL) || !str_ieq(id, want)) continue;
      memset(&t[n], 0, sizeof t[n]); t[n].para = pi; t[n].note = -1;
      refs_plain(d, pi, 0, q->len, t[n].text, sizeof t[n].text);
      n++;
    }
  }
  free(mask);
  return n;
}
int refs_kinds(int type, int *k)
{
  static const int num[] = { XK_PAGE, XK_NUMBER, XK_NUMNOCTX, XK_NUMFULL, XK_TEXT, XK_ABOVEBELOW };
  static const int head[] = { XK_TEXT, XK_PAGE, XK_NUMBER, XK_NUMNOCTX, XK_NUMFULL, XK_ABOVEBELOW };
  static const int bm[] = { XK_TEXT, XK_PAGE, XK_NUMBER, XK_NUMNOCTX, XK_NUMFULL, XK_ABOVEBELOW };
  static const int note[] = { XK_NOTENUM, XK_PAGE, XK_ABOVEBELOW, XK_NOTEFMT };
  static const int cap[] = { XK_CAPTION, XK_LABELNUM, XK_CAPTEXT, XK_PAGE, XK_ABOVEBELOW };
  const int *s;
  int n, i;
  switch (type) {
  case RT_NUMBERED: s = num; n = 6; break;
  case RT_HEADING: s = head; n = 6; break;
  case RT_BOOKMARK: s = bm; n = 6; break;
  case RT_FOOTNOTE: case RT_ENDNOTE: s = note; n = 4; break;
  default: s = cap; n = 5; break;
  }
  for (i = 0; i < n; i++) k[i] = s[i];
  return n;
}
/* a separator character after a caption's number */
static int cap_sep(const char *t, u32 o, u32 len, u32 *w)
{
  u8 c = (u8)t[o];
  *w = 1;
  if (c == ' ' || c == ':' || c == '.' || c == '-' || c == ',') return 1;
  if (c == 0xE2 && o + 3 <= len && (u8)t[o + 1] == 0x80 && ((u8)t[o + 2] == 0x93 || (u8)t[o + 2] == 0x94)) { *w = 3; return 1; }
  if (c == 0xC2 && o + 2 <= len && (u8)t[o + 1] == 0xA0) { *w = 2; return 1; }
  return 0;
}
int refs_insert_xref(Doc *d, Pos *at, int type, const RefTarget *t, int kind, int link, int above, const char *sep, const struct FieldEnv *e)
{
  char name[64], code[300], sq[40];
  u32 pi = t->para, a = 0, b = 0;
  int exact = 1, k;
  CharFmt f;
  Pos p0 = *at;
  Para *q;
  if (pi >= d->np) return -1;
  q = d->p[pi];
  doc_begin(d, p0);
  name[0] = 0;
  if (type == RT_BOOKMARK) {
    snprintf(name, sizeof name, "%.63s", t->text);
    if (!doc_bmark_find(d, name, NULL, NULL)) { doc_end(d, p0); return -1; }
  } else {
    if (type == RT_FOOTNOTE || type == RT_ENDNOTE) { a = t->off; b = t->off + 4; }
    else if (type >= RT_LABEL) {
      u32 ss = 0, se = 0, w;
      char id[64];
      a = 0; b = q->len;
      while (b > 0 && q->t[b - 1] == ' ') b--;
      if (caption_label_of(d, pi, id, sizeof id, &ss, &se) && se < q->len) {
        if (kind == XK_LABELNUM) b = se + 4;
        else if (kind == XK_CAPTEXT) { a = se + 4; while (a < b && cap_sep(q->t, a, q->len, &w)) a += w; }
      }
      if (kind == XK_PAGE || kind == XK_ABOVEBELOW) exact = 0;
    } else {
      a = 0; b = q->len;
      if (kind == XK_PAGE || kind == XK_ABOVEBELOW || kind == XK_NUMBER || kind == XK_NUMNOCTX || kind == XK_NUMFULL) exact = 0;
    }
    if (!refs_target(d, pi, a, b, exact, "_Ref", name)) { doc_end(d, p0); return -1; }
  }
  sq[0] = 0;
  if (sep && sep[0] && kind == XK_NUMFULL) { char s2[24]; refs_quote(sep, 0, s2, sizeof s2); snprintf(sq, sizeof sq, "\\d \"%s\" ", s2); }
  switch (kind) {
  case XK_PAGE: snprintf(code, sizeof code, " PAGEREF %s %s%s", name, link ? "\\h " : "", above ? "\\p " : ""); break;
  case XK_NUMBER: snprintf(code, sizeof code, " REF %s \\r %s%s", name, link ? "\\h " : "", above ? "\\p " : ""); break;
  case XK_NUMNOCTX: snprintf(code, sizeof code, " REF %s \\n %s%s", name, link ? "\\h " : "", above ? "\\p " : ""); break;
  case XK_NUMFULL: snprintf(code, sizeof code, " REF %s \\w %s%s%s", name, sq, link ? "\\h " : "", above ? "\\p " : ""); break;
  case XK_ABOVEBELOW: snprintf(code, sizeof code, " REF %s \\p %s", name, link ? "\\h " : ""); break;
  case XK_NOTENUM: snprintf(code, sizeof code, " NOTEREF %s %s%s", name, link ? "\\h " : "", above ? "\\p " : ""); break;
  case XK_NOTEFMT: snprintf(code, sizeof code, " NOTEREF %s \\f %s%s", name, link ? "\\h " : "", above ? "\\p " : ""); break;
  default: snprintf(code, sizeof code, " REF %s %s", name, link ? "\\h " : ""); break;     /* (above/below: a field of its own after it, as Word does) */
  }
  *at = pos_clamp(d, *at);
  f = *para_fmt_before(d->p[at->para], at->off);
  f.link = 0; f.bmark = 0; f.fx2 &= (u16)~FX2_HIDDEN;
  if (kind == XK_NOTEFMT) { f.flags |= CF_SUPER; f.flags &= (u8)~CF_SUB; }
  {
    FieldEnv own;
    const FieldEnv *fe;
    env_or(e, &own, &fe);
    k = field_insert(d, at, code, fe, &f);
    if (k >= 0 && above && (kind == XK_TEXT || kind == XK_CAPTION || kind == XK_LABELNUM || kind == XK_CAPTEXT)) {
      snprintf(code, sizeof code, " REF %s \\p %s", name, link ? "\\h " : "");
      doc_insert(d, at, " ", 1, &f);
      field_insert(d, at, code, fe, &f);
    }
  }
  doc_end(d, *at);
  return k;
}
