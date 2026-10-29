/*
 * refsgen.c - (WP2) the block fields of refs.h: tables of contents and figures (TOC), the index
 * (INDEX) and tables of authorities (TOA) made from the document, put between their field's marks,
 * found, inserted, updated and removed; the old table of contents (Quill's "toc N" run) taken over.
 * Written from Word's documented field switches (ECMA-376 part 1, 17.16.5).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "refsi.h"
#include "outline.h"
#include "toc.h"

int refs_block_kind(const char *code)
{
  char nm[16];
  field_name(code, nm, sizeof nm);
  if (!strcmp(nm, "TOC")) return field_switch(code, 'c', 0, NULL, 0) || field_switch(code, 'a', 0, NULL, 0) ? BK_TOF : BK_TOC;
  if (!strcmp(nm, "INDEX")) return BK_INDEX;
  if (!strcmp(nm, "TOA")) return BK_TOA;
  return BK_NONE;
}
int refs_block_find(Doc *d, int kind, u32 from, Pos *st, Pos *en)
{
  u32 pi, o;
  for (pi = from; pi < d->np; pi++) {
    Para *q = d->p[pi];
    for (o = ri_next_mark(q, 0); o < q->len; o = ri_next_mark(q, o + 4)) {
      int k, bk;
      Pos s, e;
      if (!IS_FSTART(q->t + o) || (k = fmark_index(q->t + o)) >= d->nfields) continue;
      bk = refs_block_kind(d->fields[k].code);
      if (!bk || (kind && bk != kind)) continue;
      s.para = pi; s.off = o;
      if (!field_end(d, s, &e)) continue;
      if (st) *st = s;
      if (en) *en = e;
      return k;
    }
  }
  return -1;
}
int refs_mark_field(const Doc *d, int k)
{
  char nm[8];
  const Field *f = field_get(d, k);
  if (!f) return 0;
  field_name(f->code, nm, sizeof nm);
  return !strcmp(nm, "XE") || !strcmp(nm, "TC") || !strcmp(nm, "TA");
}

/* ------------------------------------------------------------------ */
/* making the paragraphs */
typedef struct {
  Doc *d;
  Layout *L;
  const char *code;
  int leader;
  u8 *mask;                   /* paragraphs in block fields (their entries aren't listed) */
  PList out;
  u32 bma, bmb;               /* \b: only paragraphs bma..bmb */
} Gen;
/* a range "1-3" (en dash too) into lo, hi; 0 if s has none */
static int range(const char *s, int *lo, int *hi)
{
  const char *p = s;
  if (!s[0]) return 0;
  *lo = atoi(s); *hi = *lo;
  while (*p && (*p == ' ' || (*p >= '0' && *p <= '9'))) p++;
  while (*p && !(*p >= '0' && *p <= '9')) p++;
  if (*p) *hi = atoi(p);
  if (*lo < 1) *lo = 1;
  if (*hi < *lo) *hi = *lo;
  return 1;
}
static void set_tab(ParaFmt *pf, i32 at, int lead)
{
  memset(pf->tabs, 0, sizeof pf->tabs); memset(pf->tabkind, 0, sizeof pf->tabkind); memset(pf->tablead, 0, sizeof pf->tablead);
  if (at < 360) at = 360;
  pf->tabs[0] = at; pf->tabkind[0] = TAB_RIGHT; pf->tablead[0] = (u8)lead;
}
static i32 body_width(Doc *d, u32 pi)
{
  const SectFmt *s = doc_sect(d, pi < d->np ? pi : d->np - 1);
  const PageSetup *pg = sect_page(d, s, doc_last_sect(d, pi < d->np ? pi : d->np - 1));
  return pg->width - pg->left - pg->right - pg->gutter;
}
static int message(Gen *g, int word)
{
  Para *p = ri_para(g->d, 0);
  CharFmt f;
  if (!p) return ri_add(&g->out, NULL);
  f = p->r[0].f; f.flags |= CF_BOLD;
  ri_text(p, refs_word(refs_lang, word), &f);
  return ri_add(&g->out, p);
}
/* the text of paragraph pi for a table's entry: list number, field results; from byte a */
static void entry_text(Doc *d, u32 pi, u32 a, int number, char *out, size_t cap)
{
  char num[48] = "", t[400];
  if (number) refs_para_number(d, pi, RN_PLAIN, pi, num, sizeof num);
  refs_plain(d, pi, a, d->p[pi]->len, t, sizeof t);
  { char *s = t; while (*s == ' ') s++; snprintf(out, cap, num[0] ? "%s %s" : "%s%s", num, s); }
  { size_t n = strlen(out); while (n && out[n - 1] == ' ') out[--n] = 0; }
}
static int in_bm(Gen *g, u32 pi) { return pi >= g->bma && pi <= g->bmb; }
static void restrict_bm(Gen *g)
{
  char bm[128];
  Pos a, b;
  g->bma = 0; g->bmb = 0xFFFFFFFFUL;
  if (field_switch(g->code, 'b', 0, bm, sizeof bm) && bm[0] && doc_bmark_find(g->d, bm, &a, &b)) { g->bma = a.para; g->bmb = b.para; }
}
/* \t "Style,1,Other,2": the level paragraph pi's style has there, 0 none */
static int styles_level(Doc *d, const char *list, u32 pi)
{
  const Style *st = &d->styles[d->p[pi]->pf.style < d->nstyles ? d->p[pi]->pf.style : 0];
  char name[64];
  const char *p = list;
  while (*p) {
    size_t n = 0;
    int lv;
    while (*p == ' ') p++;
    while (*p && *p != ',' && *p != ';' && n + 1 < sizeof name) name[n++] = *p++;
    name[n] = 0;
    while (n && name[n - 1] == ' ') name[--n] = 0;
    if (*p) p++;
    lv = atoi(p);
    while (*p && *p != ',' && *p != ';') p++;
    if (*p) p++;
    if (name[0] && (str_ieq(name, st->name) || str_ieq(name, st->id))) return lv >= 1 && lv <= 9 ? lv : 1;
  }
  return 0;
}

/* ---- tables of contents and figures */
typedef struct { u32 pi; int level; int nopage; char text[400]; } TEnt;
static int gen_toc(Gen *g)
{
  Doc *d = g->d;
  char ov[32], tv[400], fv[16], lv_[32], lab[64], nv[32], pv[32], nm[16], a0[64];
  int has_o, has_t, has_f, has_l, has_n, has_p, link, olo = 1, ohi = 9, llo = 1, lhi = 9, nlo = 1, nhi = 9, tof, noplab, n = 0, cap = 0, i;
  TEnt *e = NULL;
  u32 pi, o;
  has_o = field_switch(g->code, 'o', 0, ov, sizeof ov);
  has_t = field_switch(g->code, 't', 0, tv, sizeof tv);
  has_f = field_switch(g->code, 'f', 0, fv, sizeof fv);
  has_l = field_switch(g->code, 'l', 0, lv_, sizeof lv_);
  has_n = field_switch(g->code, 'n', 0, nv, sizeof nv);
  has_p = field_switch(g->code, 'p', 0, pv, sizeof pv);
  link = field_switch(g->code, 'h', 0, NULL, 0);
  noplab = field_switch(g->code, 'a', 0, lab, sizeof lab);
  tof = noplab || field_switch(g->code, 'c', 0, lab, sizeof lab);
  if (has_o && !range(ov, &olo, &ohi)) { olo = 1; ohi = 9; }
  if (has_l) range(lv_, &llo, &lhi);
  if (has_n && !range(nv, &nlo, &nhi)) { nlo = 1; nhi = 9; }
  if (!has_o && !has_t && !has_f && !has_l && !tof && !field_switch(g->code, 'u', 0, NULL, 0)) { has_o = 1; olo = 1; ohi = 9; }
  restrict_bm(g);
  for (pi = 0; pi < d->np; pi++) {
    Para *q = d->p[pi];
    int level = 0;
    if ((g->mask && g->mask[pi]) || toc_para(d, q) || !in_bm(g, pi)) continue;
    if (n + 8 >= cap) { TEnt *ne = (TEnt *)realloc(e, (size_t)(cap + 64) * sizeof(TEnt)); if (!ne) break; e = ne; cap += 64; }
    if (tof) {
      for (o = ri_next_mark(q, 0); o < q->len; o = ri_next_mark(q, o + 4)) {
        int k = ri_field(d, q, o, nm, sizeof nm);
        if (k < 0 || strcmp(nm, "SEQ") || !field_arg(d->fields[k].code, 0, a0, sizeof a0) || !str_ieq(a0, lab)) continue;
        e[n].pi = pi; e[n].level = 1; e[n].nopage = 0;
        if (noplab) {
          /* the caption without its label and number: from after the SEQ field, its separator skipped */
          u32 s = ri_field_end(q, o);
          s = s < q->len ? s + 4 : q->len;
          while (s < q->len && (q->t[s] == ' ' || q->t[s] == ':' || q->t[s] == '.' || q->t[s] == '-')) s++;
          while (s + 3 <= q->len && (u8)q->t[s] == 0xE2 && (u8)q->t[s + 1] == 0x80 && ((u8)q->t[s + 2] == 0x93 || (u8)q->t[s + 2] == 0x94)) { s += 3; while (s < q->len && q->t[s] == ' ') s++; }
          entry_text(d, pi, s, 0, e[n].text, sizeof e[n].text);
        } else entry_text(d, pi, 0, 0, e[n].text, sizeof e[n].text);
        if (e[n].text[0]) n++;
        break;
      }
    } else {
      if (has_o) { int lv = outline_level(d, pi); if (lv >= olo && lv <= ohi) level = lv; }
      if (has_t) { int lv = styles_level(d, tv, pi); if (lv) level = lv; }
      if (level && q->len) {
        e[n].pi = pi; e[n].level = level; e[n].nopage = has_n && level >= nlo && level <= nhi;
        entry_text(d, pi, 0, 1, e[n].text, sizeof e[n].text);
        if (e[n].text[0]) n++;
      }
    }
    if (has_f || has_l) {
      /* TC fields: their own text, level (\l) and type (\f) */
      for (o = ri_next_mark(q, 0); o < q->len && n + 1 < cap; o = ri_next_mark(q, o + 4)) {
        int k = ri_field(d, q, o, nm, sizeof nm), lv = 1;
        char ty[16], lvs[8], txt[400];
        if (k < 0 || strcmp(nm, "TC") || !field_arg(d->fields[k].code, 0, txt, sizeof txt)) continue;
        if (!field_switch(d->fields[k].code, 'f', 0, ty, sizeof ty) || !ty[0]) strcpy(ty, "C");
        if (field_switch(d->fields[k].code, 'l', 0, lvs, sizeof lvs) && atoi(lvs) >= 1) lv = atoi(lvs);
        if (has_f && !str_ieq(ty, fv[0] ? fv : "C")) continue;
        if (lv < llo || lv > lhi) continue;
        e[n].pi = pi; e[n].level = tof ? 1 : lv;
        e[n].nopage = field_switch(d->fields[k].code, 'n', 0, NULL, 0) || (has_n && lv >= nlo && lv <= nhi);
        snprintf(e[n].text, sizeof e[n].text, "%s", txt);
        n++;
      }
    }
  }
  if (!n) { free(e); return message(g, tof ? RW_NOTOF : RW_NOTOC); }
  for (i = 0; i < n; i++) {
    char name[64] = "", url[80];
    int st = tof ? refs_style(d, RS_TOF, 1) : refs_style(d, RS_TOC, e[i].level);
    Para *p = ri_para(d, st);
    CharFmt cf, pf;
    u16 lk = 0;
    if (!p) { ri_add(&g->out, NULL); break; }
    if (refs_target(d, e[i].pi, 0, 0, 0, "_Toc", name)) { snprintf(url, sizeof url, "#%s", name); lk = (u16)doc_link(d, url); }
    cf = p->r[0].f; pf = cf;
    if (link) cf.link = lk;
    pf.link = lk;
    ri_text(p, e[i].text, &cf);
    if (!e[i].nopage && lk) {
      if (has_p) ri_text(p, pv, &cf);
      else { ri_text(p, "\t", &cf); set_tab(&p->pf, body_width(d, e[i].pi) - p->pf.left, g->leader); }
      ri_text(p, FIELD_PAGEREF, &pf);
    }
    para_normalise(p);
    if (!ri_add(&g->out, p)) break;
  }
  free(e);
  return !g->out.oom;
}

/* ---- the index */
#define XLEV 6
typedef struct {
  char key[XLEV][96];         /* the levels as written */
  char fold[XLEV][96];        /* and as they sort */
  int nlev;
  long page, page2;           /* its page (from 0), the end of its range (-1 none) */
  u8 bold, italic;
  char xref[160];             /* \t: "See ..." instead of a page */
} XEnt;
static int xe_lang;
static int xcmp(const void *pa, const void *pb)
{
  const XEnt *a = (const XEnt *)pa, *b = (const XEnt *)pb;
  int i, c;
  for (i = 0; i < XLEV; i++) {
    if (i >= a->nlev || i >= b->nlev) return i >= a->nlev ? (i >= b->nlev ? 0 : -1) : 1;
    if ((c = strcmp(a->fold[i], b->fold[i]))) return c;
    if ((c = strcmp(a->key[i], b->key[i]))) return c;
  }
  if (a->page != b->page) return a->page < b->page ? -1 : 1;
  return 0;
}
static int same_path(const XEnt *a, const XEnt *b, int levels)
{
  int i;
  for (i = 0; i < levels; i++) if (i >= a->nlev || i >= b->nlev || strcmp(a->key[i], b->key[i])) return 0;
  return 1;
}
/* the levels of an XE's text, split at colons not written \: */
static int xe_levels(const char *s, XEnt *x)
{
  int lv = 0;
  size_t n = 0;
  for (; *s && lv < XLEV; s++) {
    if (*s == '\\' && s[1] == ':') { if (n + 1 < sizeof x->key[0]) x->key[lv][n++] = ':'; s++; continue; }
    if (*s == ':') { x->key[lv][n] = 0; lv++; n = 0; continue; }
    if (n + 1 < sizeof x->key[0]) x->key[lv][n++] = *s;
  }
  if (lv < XLEV) { x->key[lv][n] = 0; lv++; }
  return lv;
}
/* the letter group of a sorting key: 'a'..'z', 'n' + 1 for the Spanish n with tilde (as 27), 0 symbols */
static int group_of(const char *fold)
{
  u8 c = (u8)fold[0];
  if (c >= 'a' && c <= 'z') return c == 'n' && fold[1] == '{' ? 'z' + 1 : c;
  return 0;
}
/* the pages of a run of entries (one node) into p: each once, bold or italic as marked, ranges */
static void add_pages(Gen *g, Para *p, const XEnt *x, int nx, const CharFmt *base, const char *psep, const char *rsep)
{
  int i, j, first = 1;
  for (i = 0; i < nx; i++) {
    char a[24], b[24], t[128];
    CharFmt f = *base;
    int dup = 0;
    if (x[i].page < 0 || x[i].xref[0]) continue;
    for (j = 0; j < i; j++) if (x[j].page == x[i].page && x[j].page2 == x[i].page2 && !x[j].xref[0]) dup = 1;
    if (dup) continue;
    for (j = i; j < nx; j++) if (x[j].page == x[i].page && x[j].page2 == x[i].page2) { if (x[j].bold) f.flags |= CF_BOLD; if (x[j].italic) f.flags |= CF_ITALIC; }
    ri_pagenum(g->d, x[i].page, a);
    if (x[i].page2 > x[i].page) { ri_pagenum(g->d, x[i].page2, b); snprintf(t, sizeof t, "%s%s%s", a, rsep, b); }
    else snprintf(t, sizeof t, "%s", a);
    if (!first) ri_text(p, psep, base);
    ri_text(p, t, &f);
    first = 0;
  }
}
static int has_pages(const XEnt *x, int nx) { int i; for (i = 0; i < nx; i++) if (x[i].page >= 0 && !x[i].xref[0]) return 1; return 0; }
/* the cross-references of a node: ". See X" ("See" in italics when it is the word) */
static void add_xrefs(Gen *g, Para *p, const XEnt *x, int nx, const CharFmt *base, const char *ksep, int pages)
{
  int i;
  for (i = 0; i < nx; i++) {
    const char *see = refs_word(xe_lang, RW_SEE), *also = refs_word(xe_lang, RW_SEEALSO), *w = NULL;
    CharFmt it = *base;
    if (!x[i].xref[0]) continue;
    ri_text(p, pages || i ? ksep : ksep, base);
    pages = 1;
    if (!strncmp(x[i].xref, also, strlen(also))) w = also;
    else if (!strncmp(x[i].xref, see, strlen(see))) w = see;
    if (w) {
      char t[48];
      snprintf(t, sizeof t, "%s", w);
      it.flags |= CF_ITALIC;
      ri_text(p, t, &it);
      ri_text(p, x[i].xref + strlen(w), base);
    } else ri_text(p, x[i].xref, base);
  }
}
static int gen_index(Gen *g)
{
  Doc *d = g->d;
  char v[64], esep[64] = ", ", psep[64] = ", ", rsep[64] = "\xE2\x80\x93", ksep[64] = ". ", head[32], ftype[16], letters[16], nm[16];
  int runin, hashead, hasf, cols = 1, n = 0, cap = 0, i, j, plo = 0, phi = 255, lastgroup = -1, tabsep;
  XEnt *x = NULL;
  u32 pi, o;
  i32 colw;
  if (field_switch(g->code, 'c', 0, v, sizeof v) && atoi(v) > 1) cols = atoi(v) > MAX_SECT_COLS ? MAX_SECT_COLS : atoi(v);
  runin = field_switch(g->code, 'r', 0, NULL, 0);
  if (field_switch(g->code, 'e', 0, v, sizeof v)) snprintf(esep, sizeof esep, "%s", v);
  if (field_switch(g->code, 'l', 0, v, sizeof v)) snprintf(psep, sizeof psep, "%s", v);
  if (field_switch(g->code, 'g', 0, v, sizeof v)) snprintf(rsep, sizeof rsep, "%s", v);
  if (field_switch(g->code, 'k', 0, v, sizeof v)) snprintf(ksep, sizeof ksep, "%s", v);
  hashead = field_switch(g->code, 'h', 0, head, sizeof head);
  hasf = field_switch(g->code, 'f', 0, ftype, sizeof ftype);
  xe_lang = field_switch(g->code, 'z', 0, v, sizeof v) ? refs_lang_of_lcid(atoi(v)) : refs_lang;
  if (field_switch(g->code, 'p', 0, letters, sizeof letters) && letters[0]) {
    char f[16];
    ri_fold(xe_lang, letters, f, sizeof f);
    plo = (u8)f[0];
    phi = (u8)f[strlen(f) - 1];
  }
  tabsep = !strcmp(esep, "\t");
  restrict_bm(g);
  for (pi = 0; pi < d->np; pi++) {
    Para *q = d->p[pi];
    if ((g->mask && g->mask[pi]) || !in_bm(g, pi)) continue;
    for (o = ri_next_mark(q, 0); o < q->len; o = ri_next_mark(q, o + 4)) {
      int k = ri_field(d, q, o, nm, sizeof nm), l;
      char text[600], ty[16], bm[128];
      Pos at, ba, bb;
      if (k < 0 || strcmp(nm, "XE") || !field_arg(d->fields[k].code, 0, text, sizeof text) || !text[0]) continue;
      if (field_switch(d->fields[k].code, 'f', 0, ty, sizeof ty) && ty[0]) { if (!hasf || !str_ieq(ty, ftype)) continue; }
      else if (hasf && ftype[0] && !str_ieq(ftype, "I")) continue;
      if (n == cap) { XEnt *nx = (XEnt *)realloc(x, (size_t)(cap + 64) * sizeof(XEnt)); if (!nx) break; x = nx; cap += 64; }
      memset(&x[n], 0, sizeof x[n]);
      x[n].nlev = xe_levels(text, &x[n]);
      for (l = 0; l < x[n].nlev; l++) ri_fold(xe_lang, x[n].key[l], x[n].fold[l], sizeof x[n].fold[l]);
      at.para = pi; at.off = o;
      x[n].page = ri_page(d, g->L, at); x[n].page2 = -1;
      if (x[n].page < 0) x[n].page = 0;
      if (field_switch(d->fields[k].code, 'r', 0, bm, sizeof bm) && bm[0] && doc_bmark_find(d, bm, &ba, &bb)) {
        long p1 = ri_page(d, g->L, ba), p2 = ri_page(d, g->L, bb);
        if (p1 >= 0) x[n].page = p1;
        if (p2 > x[n].page) x[n].page2 = p2;
      }
      x[n].bold = (u8)field_switch(d->fields[k].code, 'b', 0, NULL, 0);
      x[n].italic = (u8)field_switch(d->fields[k].code, 'i', 0, NULL, 0);
      field_switch(d->fields[k].code, 't', 0, x[n].xref, sizeof x[n].xref);
      if (plo || phi != 255) { int gch = group_of(x[n].fold[0]); if (gch < plo || gch > phi) continue; }
      n++;
    }
  }
  if (!n) { free(x); return message(g, RW_NOINDEX); }
  qsort(x, (size_t)n, sizeof(XEnt), xcmp);
  colw = (body_width(d, d->np - 1) - (cols - 1) * 720) / cols;
  for (i = 0; i < n;) {
    int nx = 1, c = 0, lv, grp = group_of(x[i].fold[0]);
    while (i + nx < n && x[i + nx].nlev == x[i].nlev && same_path(&x[i], &x[i + nx], x[i].nlev)) nx++;
    if (hashead && grp != lastgroup) {
      /* a letter's heading (\h "A"), or a blank line between letters (\h "") */
      if (head[0] || lastgroup >= 0) {
        Para *p = ri_para(d, head[0] ? refs_style(d, RS_INDEXHEAD, 1) : refs_style(d, RS_INDEX, 1));
        if (p && head[0]) {
          char t[48], letter[8];
          const char *a = strchr(head, 'A');
          if (!grp) snprintf(letter, sizeof letter, "%s", "#");
          else if (grp == 'z' + 1) strcpy(letter, "\xC3\x91");
          else { letter[0] = (char)(grp - 32); letter[1] = 0; }
          if (a) snprintf(t, sizeof t, "%.*s%s%s", (int)(a - head), head, letter, a + 1);
          else snprintf(t, sizeof t, "%s", letter);
          ri_text(p, t, NULL);
        }
        if (!ri_add(&g->out, p)) break;
      }
      lastgroup = grp;
    }
    if (i > 0) while (c < x[i].nlev - 1 && same_path(&x[i - 1], &x[i], c + 1)) c++;
    if (runin) {
      /* run-in: a main entry starts a paragraph; its subentries follow in it */
      Para *p;
      CharFmt base;
      if (c == 0) {
        if (!(p = ri_para(d, refs_style(d, RS_INDEX, 1)))) { ri_add(&g->out, NULL); break; }
        if (!ri_add(&g->out, p)) break;
        ri_text(p, x[i].key[0], NULL);
        base = p->r[0].f;
        if (x[i].nlev == 1) {
          if (has_pages(x + i, nx)) { ri_text(p, esep, &base); add_pages(g, p, x + i, nx, &base, psep, rsep); }
          add_xrefs(g, p, x + i, nx, &base, ksep, has_pages(x + i, nx));
          i += nx;
          continue;
        }
      }
      p = g->out.p[g->out.n - 1];
      base = p->r[0].f;
      ri_text(p, (c == 0 && g->out.p[g->out.n - 1]->len == strlen(x[i].key[0])) ? ": " : "; ", &base);
      for (lv = 1; lv < x[i].nlev; lv++) { if (lv > 1) ri_text(p, ", ", &base); ri_text(p, x[i].key[lv], &base); }
      if (has_pages(x + i, nx)) { ri_text(p, esep, &base); add_pages(g, p, x + i, nx, &base, psep, rsep); }
      add_xrefs(g, p, x + i, nx, &base, ksep, has_pages(x + i, nx));
      i += nx;
      continue;
    }
    for (lv = c; lv < x[i].nlev; lv++) {
      Para *p = ri_para(d, refs_style(d, RS_INDEX, lv + 1));
      CharFmt base;
      if (!p) { ri_add(&g->out, NULL); break; }
      base = p->r[0].f;
      ri_text(p, x[i].key[lv], &base);
      if (lv == x[i].nlev - 1) {
        int pg = has_pages(x + i, nx);
        if (pg) {
          if (tabsep) set_tab(&p->pf, colw - p->pf.left, g->leader);
          ri_text(p, esep, &base);
          add_pages(g, p, x + i, nx, &base, psep, rsep);
        }
        add_xrefs(g, p, x + i, nx, &base, ksep, pg);
      }
      para_normalise(p);
      if (!ri_add(&g->out, p)) break;
    }
    i += nx;
  }
  (void)j;
  free(x);
  return !g->out.oom;
}

/* ---- tables of authorities */
typedef struct { char lng[400], shrt[400], fold[200]; int cat; } Auth;
typedef struct { char key[400]; long page, page2; u8 bold, italic; } TaUse;
static int acmp(const void *pa, const void *pb)
{
  const Auth *a = (const Auth *)pa, *b = (const Auth *)pb;
  int c = strcmp(a->fold, b->fold);
  return c ? c : strcmp(a->lng, b->lng);
}
static int gen_toa(Gen *g)
{
  Doc *d = g->d;
  char v[32], esep[32] = "\t", psep[32] = ", ", rsep[32] = "\xE2\x80\x93", nm[8];
  int cat = 1, heading, passim, na = 0, nu = 0, capa = 0, capu = 0, i, j, c, any = 0, tabsep;
  Auth *au = NULL;
  TaUse *us = NULL;
  u32 pi, o;
  if (field_switch(g->code, 'c', 0, v, sizeof v)) cat = atoi(v);
  heading = field_switch(g->code, 'h', 0, NULL, 0);
  passim = field_switch(g->code, 'p', 0, NULL, 0);
  if (field_switch(g->code, 'e', 0, v, sizeof v)) snprintf(esep, sizeof esep, "%s", v);
  if (field_switch(g->code, 'l', 0, v, sizeof v)) snprintf(psep, sizeof psep, "%s", v);
  if (field_switch(g->code, 'g', 0, v, sizeof v)) snprintf(rsep, sizeof rsep, "%s", v);
  tabsep = !strcmp(esep, "\t");
  restrict_bm(g);
  for (pi = 0; pi < d->np; pi++) {
    Para *q = d->p[pi];
    if ((g->mask && g->mask[pi]) || !in_bm(g, pi)) continue;
    for (o = ri_next_mark(q, 0); o < q->len; o = ri_next_mark(q, o + 4)) {
      int k = ri_field(d, q, o, nm, sizeof nm);
      char l[400], s[200], cs[8], bm[128];
      Pos at, ba, bb;
      if (k < 0 || strcmp(nm, "TA")) continue;
      l[0] = s[0] = 0;
      field_switch(d->fields[k].code, 'l', 0, l, sizeof l);
      field_switch(d->fields[k].code, 's', 0, s, sizeof s);
      if (!l[0] && !s[0]) continue;
      if (l[0]) {
        int dup = 0;
        for (j = 0; j < na; j++) if (!strcmp(au[j].lng, l)) dup = 1;
        if (!dup) {
          if (na == capa) { Auth *nw = (Auth *)realloc(au, (size_t)(capa + 32) * sizeof(Auth)); if (!nw) break; au = nw; capa += 32; }
          snprintf(au[na].lng, sizeof au[na].lng, "%s", l);
          snprintf(au[na].shrt, sizeof au[na].shrt, "%s", s[0] ? s : l);
          ri_fold(refs_lang, l, au[na].fold, sizeof au[na].fold);
          au[na].cat = field_switch(d->fields[k].code, 'c', 0, cs, sizeof cs) && atoi(cs) > 0 ? atoi(cs) : 1;
          na++;
        }
      }
      if (nu == capu) { TaUse *nw = (TaUse *)realloc(us, (size_t)(capu + 64) * sizeof(TaUse)); if (!nw) break; us = nw; capu += 64; }
      snprintf(us[nu].key, sizeof us[nu].key, "%s", s[0] ? s : l);
      at.para = pi; at.off = o;
      us[nu].page = ri_page(d, g->L, at); if (us[nu].page < 0) us[nu].page = 0;
      us[nu].page2 = -1;
      if (field_switch(d->fields[k].code, 'r', 0, bm, sizeof bm) && bm[0] && doc_bmark_find(d, bm, &ba, &bb)) {
        long p2 = ri_page(d, g->L, bb);
        if (p2 > us[nu].page) us[nu].page2 = p2;
      }
      us[nu].bold = (u8)field_switch(d->fields[k].code, 'b', 0, NULL, 0);
      us[nu].italic = (u8)field_switch(d->fields[k].code, 'i', 0, NULL, 0);
      nu++;
    }
  }
  if (na > 1) qsort(au, (size_t)na, sizeof(Auth), acmp);
  for (c = cat ? cat : 1; c <= (cat ? cat : REFS_NCAT); c++) {
    int head_done = 0;
    for (i = 0; i < na; i++) {
      Para *p;
      CharFmt base;
      long pages[64];
      int np = 0, k;
      if (au[i].cat != c) continue;
      if (heading && !head_done) {
        Para *h = ri_para(d, refs_style(d, RS_TOAHEAD, 1));
        if (h) ri_text(h, refs_category(d, c), NULL);
        if (!ri_add(&g->out, h)) break;
        head_done = 1;
      }
      if (!(p = ri_para(d, refs_style(d, RS_TOA, 1)))) { ri_add(&g->out, NULL); break; }
      base = p->r[0].f;
      ri_text(p, au[i].lng, &base);
      for (k = 0; k < nu; k++) {
        int dup = 0, m;
        if (strcmp(us[k].key, au[i].shrt) && strcmp(us[k].key, au[i].lng)) continue;
        for (m = 0; m < np; m++) if (pages[m] == us[k].page) dup = 1;
        if (!dup && np < 64) pages[np++] = us[k].page;
      }
      if (np) {
        if (tabsep) set_tab(&p->pf, body_width(d, 0) - p->pf.left, g->leader);
        ri_text(p, esep, &base);
        if (passim && np >= 5) ri_text(p, refs_word(refs_lang, RW_PASSIM), &base);
        else {
          int first = 1;
          for (k = 0; k < nu; k++) {
            char a[24], b[24], t[128];
            CharFmt f = base;
            int m, seen = 0;
            if (strcmp(us[k].key, au[i].shrt) && strcmp(us[k].key, au[i].lng)) continue;
            for (m = 0; m < k; m++) if (us[m].page == us[k].page && (!strcmp(us[m].key, au[i].shrt) || !strcmp(us[m].key, au[i].lng))) seen = 1;
            if (seen) continue;
            if (us[k].bold) f.flags |= CF_BOLD;
            if (us[k].italic) f.flags |= CF_ITALIC;
            ri_pagenum(d, us[k].page, a);
            if (us[k].page2 > us[k].page) { ri_pagenum(d, us[k].page2, b); snprintf(t, sizeof t, "%s%s%s", a, rsep, b); }
            else snprintf(t, sizeof t, "%s", a);
            if (!first) ri_text(p, psep, &base);
            ri_text(p, t, &f);
            first = 0;
          }
        }
      }
      para_normalise(p);
      if (!ri_add(&g->out, p)) break;
      any = 1;
    }
  }
  free(au); free(us);
  if (!any && !g->out.oom) return message(g, RW_NOTOA);
  return !g->out.oom;
}

/* ------------------------------------------------------------------ */
/* putting the paragraphs in */
/* a copy of p's bytes a..b with its paragraph formatting */
static Para *sub_para(const Para *p, u32 a, u32 b)
{
  Para *q = para_new(&p->pf, &p->r[0].f);
  u32 r, s = 0;
  if (!q) return NULL;
  for (r = 0; r < p->nr; s = p->r[r].end, r++) {
    u32 lo = s > a ? s : a, hi = p->r[r].end < b ? p->r[r].end : b;
    if (hi > lo && !para_append(q, p->t + lo, hi - lo, &p->r[r].f)) { para_free(q); return NULL; }
  }
  para_normalise(q);
  return q;
}
/* p with a mark before (start) or after it, formatted as its first (last) character without link */
static Para *with_mark(Para *p, int k, int end)
{
  Para *q;
  char m[8];
  CharFmt f = end ? p->r[p->nr - 1].f : p->r[0].f;
  u32 r, s = 0;
  f.link = 0; f.bmark = 0; f.fx2 &= (u16)~FX2_HIDDEN;
  fmark_utf8(m, k, end);
  if (!(q = para_new(&p->pf, &p->r[0].f))) return NULL;
  if (!end) para_append(q, m, 4, &f);
  for (r = 0; r < p->nr; s = p->r[r].end, r++) if (p->r[r].end > s) para_append(q, p->t + s, p->r[r].end - s, &p->r[r].f);
  if (end) para_append(q, m, 4, &f);
  para_normalise(q);
  return q;
}
int refs_block_leader(Doc *d, Pos st, Pos en)
{
  u32 pi;
  for (pi = st.para; pi <= en.para && pi < d->np; pi++) {
    const ParaFmt *pf = &d->p[pi]->pf;
    if (pf->tabs[0] && pf->tabkind[0] == TAB_RIGHT && strchr(d->p[pi]->t, '\t')) return pf->tablead[0];
  }
  return -1;
}
/* the index's columns: its paragraphs a..b a section of their own with cols columns, continuous with
   what comes before and after it (Word's section breaks round an index) */
static void index_columns(Doc *d, u32 a, u32 b, int cols)
{
  SectFmt sf, before, after;
  ParaFmt pf;
  int k;
  if (cols < 2 || b >= d->np) return;
  if (d->p[b]->pf.sect && d->p[b]->pf.sect <= d->nsects && d->sects[d->p[b]->pf.sect - 1].cols == cols) return;
  before = *doc_sect(d, a > 0 ? a - 1 : a);
  after = *doc_sect(d, b);
  sf = after;
  sf.cols = (u8)cols; sf.start = SB_CONT; sf.colunequal = 0;
  if (!(k = doc_add_sect(d, &sf))) return;
  memset(&pf, 0, sizeof pf);
  pf.sect = (u16)k;
  if (a > 0 && !d->p[a - 1]->pf.sect) {
    ParaFmt pb;
    int kb = doc_add_sect(d, &before);
    memset(&pb, 0, sizeof pb);
    pb.sect = (u16)kb;
    if (kb) doc_para_format(d, a - 1, a - 1, PM_SECT, &pb);
  }
  if (b + 1 == d->np) {
    /* at the document's end: an empty paragraph after it ends the last section, continuous, so the
       index's columns are balanced at its section break as in Word */
    Para *np = para_new(&d->styles[0].pf, &d->styles[0].cf);
    Pos w;
    int ka;
    after.start = SB_CONT; after.cols = 1;
    w.para = b + 1; w.off = 0;
    if (np && (ka = doc_add_sect(d, &after))) { np->pf.sect = (u16)ka; doc_replace(d, b + 1, 0, &np, 1, w); }
    else para_free(np);
  } else if (after.start != SB_CONT) {
    u32 e = doc_sect_end(d, b + 1);
    int ka;
    ParaFmt pa;
    after.start = SB_CONT;
    if ((ka = doc_add_sect(d, &after))) { memset(&pa, 0, sizeof pa); pa.sect = (u16)ka; doc_para_format(d, e, e, PM_SECT, &pa); }
  }
  doc_para_format(d, b, b, PM_SECT, &pf);
}
int refs_block_update(Doc *d, Pos st, Pos *pen, Layout *L, int leader)
{
  Pos en, where;
  Gen g;
  int k, kind, ok = 0, cols = 1;
  u32 a, b, i, nn = 0, nold;
  Para **nw = NULL, *pre = NULL, *suf = NULL;
  int haspre = 0;
  char v[16];
  if (st.para >= d->np || st.off + 4 > d->p[st.para]->len || !IS_FSTART(d->p[st.para]->t + st.off)) return 0;
  if (pen && pen->para < d->np && pen->off + 4 <= d->p[pen->para]->len && IS_FEND(d->p[pen->para]->t + pen->off)) en = *pen;
  else if (!field_end(d, st, &en)) return 0;
  k = fmark_index(d->p[st.para]->t + st.off);
  if (k >= d->nfields || !(kind = refs_block_kind(d->fields[k].code))) return 0;
  memset(&g, 0, sizeof g);
  g.d = d; g.L = L; g.code = d->fields[k].code;
  if (leader < 0) leader = refs_block_leader(d, st, en);
  g.leader = leader < 0 ? LEAD_DOT : leader;
  g.mask = ri_block_mask(d);
  if (L && L->npl == d->np) { ri_moved_from = 0xFFFFFFFFUL; ri_moved_by = 0; }
  doc_begin(d, st);
  if (kind == BK_INDEX) ok = gen_index(&g);
  else if (kind == BK_TOA) ok = gen_toa(&g);
  else ok = gen_toc(&g);
  free(g.mask);
  if (!ok || !g.out.n) goto done;
  g.code = d->fields[k].code;
  if (kind == BK_INDEX && field_switch(g.code, 'c', 0, v, sizeof v) && atoi(v) > 1) cols = atoi(v);
  a = st.para; b = en.para; nold = b - a + 1;
  /* what was before the start mark and after the end mark in their paragraphs stays, as paragraphs of its own */
  if (st.off > 0) { char t[8]; refs_plain(d, a, 0, st.off, t, sizeof t); if (t[0] && strcmp(t, " ")) pre = sub_para(d->p[a], 0, st.off); }
  if (en.off + 4 < d->p[b]->len) suf = sub_para(d->p[b], en.off + 4, d->p[b]->len);
  if (!(nw = (Para **)calloc(g.out.n + 2, sizeof(Para *)))) goto done;
  if (pre) { pre->pf.sect = 0; nw[nn++] = pre; pre = NULL; haspre = 1; }
  for (i = 0; i < g.out.n; i++) {
    Para *p = g.out.p[i], *q = p;
    if (i == 0 && !(q = with_mark(p, k, 0))) break;
    if (i + 1 == g.out.n) { Para *r = with_mark(q, k, 1); if (q != p) para_free(q); q = r; if (!q) break; }
    if (q != p) { para_free(p); g.out.p[i] = NULL; }
    else g.out.p[i] = NULL;
    nw[nn++] = q;
  }
  if (i < g.out.n) { for (i = 0; i < nn; i++) para_free(nw[i]); nn = 0; goto done; }
  /* the section the last paragraph ended goes on ending after the table */
  if (suf) nw[nn++] = suf, suf = NULL;
  else nw[nn - 1]->pf.sect = d->p[b]->pf.sect;
  /* the same as it was? nothing to do */
  if (nn == nold) {
    for (i = 0; i < nn && !strcmp(nw[i]->t, d->p[a + i]->t) && !memcmp(nw[i]->pf.tabs, d->p[a + i]->pf.tabs, sizeof nw[i]->pf.tabs) &&
                !memcmp(nw[i]->pf.tablead, d->p[a + i]->pf.tablead, sizeof nw[i]->pf.tablead) && nw[i]->pf.style == d->p[a + i]->pf.style; i++) ;
    if (i == nn) {
      for (i = 0; i < nn; i++) para_free(nw[i]);
      if (pen) *pen = en;
      ok = 1;
      goto done2;
    }
  }
  where.para = a; where.off = 0;
  if (!doc_replace(d, a, nold, nw, nn, where)) { nn = 0; ok = 0; goto done; }
  if (L) { ri_moved_from = a + nn; ri_moved_by = (long)d->np - (long)L->npl; }
  {
    /* the new end mark */
    u32 last = a + nn - 1;
    Para *lp;
    if (!d->p[last]->len || !IS_FEND(d->p[last]->t + d->p[last]->len - 4)) last--;
    lp = d->p[last];
    en.para = last; en.off = lp->len >= 4 ? lp->len - 4 : 0;
    if (kind == BK_INDEX && cols > 1) index_columns(d, a + (u32)haspre, last, cols);
  }
  if (pen) *pen = en;
  ok = 1;
done2:
  free(nw); nw = NULL;
done:
  free(nw);
  para_free(pre); para_free(suf);
  ri_free(&g.out);
  doc_end(d, st);
  return ok;
}
int refs_block_insert(Doc *d, Pos *at, const char *code, const char *title, Layout *L, int leader)
{
  u32 ip;
  int k, replace = 0, n = 0;
  Para *nw[2];
  char m[8];
  Pos where, st, en;
  *at = pos_clamp(d, *at);
  ip = at->para;
  while (ip > 0 && top_cell(&d->p[ip]->pf)->table && top_cell(&d->p[ip - 1]->pf)->table == top_cell(&d->p[ip]->pf)->table) ip--;   /* not inside a table */
  if (top_cell(&d->p[ip]->pf)->table) { /* before the table */ }
  else if (!d->p[ip]->len) replace = 1;
  else if (at->off > 0) ip++;
  if ((k = field_new(d, code, 0)) < 0) return -1;
  doc_begin(d, *at);
  if (title) {
    int hs = refs_style(d, RS_TOCHEAD, 1);
    if ((nw[n] = ri_para(d, hs))) ri_text(nw[n], title, NULL);
    if (nw[n]) n++;
  }
  if ((nw[n] = ri_para(d, 0))) {
    CharFmt f = nw[n]->r[0].f;
    if (replace) nw[n]->pf.sect = d->p[ip]->pf.sect;
    fmark_utf8(m, k, 0); para_append(nw[n], m, 4, &f);
    fmark_utf8(m, k, 1); para_append(nw[n], m, 4, &f);
    n++;
  }
  where.para = ip; where.off = 0;
  if (!n || !doc_replace(d, ip, replace ? 1 : 0, nw, (u32)n, where)) { doc_end(d, *at); return -1; }
  st.para = ip + (title ? 1 : 0); st.off = 0;
  en = st; en.off = 4;
  refs_block_update(d, st, &en, L, leader);
  at->para = en.para; at->off = d->p[en.para]->len;
  doc_end(d, *at);
  return k;
}
int refs_block_remove(Doc *d, Pos st)
{
  Pos en, where;
  u32 a, b;
  Para *keep[2];
  u32 nk = 0;
  int k;
  if (!field_end(d, st, &en)) return 0;
  k = fmark_index(d->p[st.para]->t + st.off);
  a = st.para; b = en.para;
  if (st.off > 0 && (keep[nk] = sub_para(d->p[a], 0, st.off))) { keep[nk]->pf.sect = 0; nk++; }
  if (en.off + 4 < d->p[b]->len && (keep[nk] = sub_para(d->p[b], en.off + 4, d->p[b]->len))) nk++;
  else if (nk) keep[nk - 1]->pf.sect = d->p[b]->pf.sect;
  if (k < d->nfields && refs_block_kind(d->fields[k].code) == BK_TOC && a > 0 && !nk) {
    const Style *s = &d->styles[d->p[a - 1]->pf.style < d->nstyles ? d->p[a - 1]->pf.style : 0];
    if (!strcmp(s->name, "TOC Heading")) a--;
  }
  where.para = a; where.off = 0;
  if (!nk && d->p[b]->pf.sect && b + 1 < d->np) {
    /* (a section ended here: an empty paragraph keeps ending it) */
    if ((keep[0] = para_new(&d->styles[0].pf, &d->styles[0].cf))) { keep[0]->pf.sect = d->p[b]->pf.sect; nk = 1; }
  }
  if (!nk && b - a + 1 == d->np && (keep[0] = para_new(&d->styles[0].pf, &d->styles[0].cf))) nk = 1;
  doc_begin(d, st);
  k = doc_replace(d, a, b - a + 1, keep, nk, where);
  doc_end(d, where);
  return k;
}
int refs_update_kind(Doc *d, int kind, Layout *L)
{
  Pos st, en;
  u32 from = 0;
  int n = 0, guard = 0;
  /* from the last to the first, so the layout's paragraphs before each are still right */
  Pos list[32];
  int nl = 0, i;
  while (nl < 32 && refs_block_find(d, kind, from, &st, &en) >= 0) { list[nl++] = st; from = en.para + 1; }
  if (!nl) return 0;
  doc_begin(d, list[0]);
  for (i = nl; i-- > 0 && guard++ < 64;) if (refs_block_update(d, list[i], NULL, L, -1)) n++;
  doc_end(d, list[0]);
  return n;
}
int refs_field_update(Doc *d, Pos st, Pos en, const struct FieldEnv *e)
{
  return refs_block_update(d, st, &en, e ? e->L : NULL, -1);
}

/* ------------------------------------------------------------------ */
/* the formats of the Table of Contents, Table of Figures and Index windows (Word's) */
int refs_apply_format(Doc *d, int kind, int fmt)
{
  int lv, n = kind == BK_TOC ? 9 : kind == BK_INDEX ? 3 : 1;
  for (lv = 1; lv <= n; lv++) {
    int k = refs_style(d, kind == BK_TOC ? RS_TOC : kind == BK_INDEX ? RS_INDEX : RS_TOF, lv);
    Style s;
    if (k < 0) return 0;
    s = d->styles[k];
    s.cf.flags &= (u8)~(CF_BOLD | CF_ITALIC | CF_CAPS);
    s.cf.fx &= (u8)~FX_SMALLCAPS;
    s.cmask |= CM_BOLD | CM_ITALIC | CM_CAPS | CM_SMALLCAPS;
    s.ocf = s.cf;
    switch (fmt) {
    case RF_CLASSIC: if (lv == 1) s.cf.flags |= CF_BOLD; else s.cf.fx |= FX_SMALLCAPS; break;
    case RF_DISTINCTIVE: if (lv == 1) s.cf.flags |= CF_BOLD | CF_CAPS; break;
    case RF_FANCY: if (lv == 1) s.cf.flags |= CF_BOLD; else s.cf.flags |= CF_ITALIC; break;
    case RF_MODERN: if (lv == 1) s.cf.flags |= CF_BOLD; break;
    case RF_FORMAL: s.cf.flags |= CF_CAPS; break;
    case RF_SIMPLE: break;
    default: break;
    }
    s.ocf = s.cf;
    if (doc_set_style(d, k, &s) < 0) return 0;
  }
  return 1;
}
