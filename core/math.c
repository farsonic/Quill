/* math.c - see math.h: the equation model, its entries in the document, OMML and linear format.
   (Layout is in mathlay.c.) Written from ECMA-376 Part 1 §22.1 and the UnicodeMath notes
   (Unicode Technical Note #28) */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "math.h"

/* ------------------------------------------------------------------ */
/* references */
int math_index(const char *t)
{
  int used;
  return (int)(utf8_get(t, 4, &used) - MATH_CP0);
}
void math_utf8(char *out, int index) { out[utf8_put(out, MATH_CP0 + (u32)index)] = 0; }

/* ------------------------------------------------------------------ */
/* what each structure is in OMML: its element, its properties' element and its arguments */
enum { PT_BOOL, PT_CHAR, PT_CHOICE, PT_INT, PT_BRK, PT_MCS };
typedef struct { const char *name; u8 type, slot; const char *const *choices; } PropDef;
typedef struct { const char *name, *pr; const PropDef *props; int nprops; const char *const *args; int nargs; } KindDef;

static const char *const c_pos[] = { "top", "bot", NULL };
static const char *const c_shp[] = { "centered", "match", NULL };
static const char *const c_ftype[] = { "bar", "skw", "lin", "noBar", NULL };
static const char *const c_limloc[] = { "undOvr", "subSup", NULL };
static const char *const c_yalign[] = { "top", "bot", "center", "inline", "inside", "outside", NULL };
static const char *const c_scr[] = { "roman", "script", "fraktur", "double-struck", "sans-serif", "monospace", NULL };
static const char *const c_sty[] = { "p", "b", "i", "bi", NULL };
static const char *const c_mcjc[] = { "center", "left", "right", "inside", "outside", NULL };
static const char *const c_pjc[] = { "left", "right", "center", "centerGroup", NULL };

static const PropDef p_acc[] = { { "chr", PT_CHAR, 0, NULL } };
static const PropDef p_bar[] = { { "pos", PT_CHOICE, P_BAR_POS, c_pos } };
static const PropDef p_box[] = { { "opEmu", PT_BOOL, P_BOX_OPEMU, NULL }, { "noBreak", PT_BOOL, P_BOX_NOBREAK, NULL }, { "diff", PT_BOOL, P_BOX_DIFF, NULL },
                                 { "brk", PT_BRK, P_BOX_BRK, NULL }, { "aln", PT_BOOL, P_BOX_ALN, NULL } };
static const PropDef p_bb[] = { { "hideTop", PT_BOOL, P_BB_HIDETOP, NULL }, { "hideBot", PT_BOOL, P_BB_HIDEBOT, NULL }, { "hideLeft", PT_BOOL, P_BB_HIDELEFT, NULL },
                                { "hideRight", PT_BOOL, P_BB_HIDERIGHT, NULL }, { "strikeH", PT_BOOL, P_BB_STRIKEH, NULL }, { "strikeV", PT_BOOL, P_BB_STRIKEV, NULL },
                                { "strikeBLTR", PT_BOOL, P_BB_STRIKEBLTR, NULL }, { "strikeTLBR", PT_BOOL, P_BB_STRIKETLBR, NULL } };
static const PropDef p_d[] = { { "begChr", PT_CHAR, 0, NULL }, { "sepChr", PT_CHAR, 1, NULL }, { "endChr", PT_CHAR, 2, NULL },
                               { "grow", PT_BOOL, P_D_GROW, NULL }, { "shp", PT_CHOICE, P_D_SHP, c_shp } };
static const PropDef p_eq[] = { { "baseJc", PT_CHOICE, P_EQ_BASEJC, c_yalign }, { "maxDist", PT_BOOL, P_EQ_MAXDIST, NULL }, { "objDist", PT_BOOL, P_EQ_OBJDIST, NULL },
                                { "rSpRule", PT_INT, 0, NULL }, { "rSp", PT_INT, 1, NULL } };
static const PropDef p_f[] = { { "type", PT_CHOICE, P_F_TYPE, c_ftype } };
static const PropDef p_g[] = { { "chr", PT_CHAR, 0, NULL }, { "pos", PT_CHOICE, P_G_POS, c_pos }, { "vertJc", PT_CHOICE, P_G_VERTJC, c_pos } };
static const PropDef p_m[] = { { "baseJc", PT_CHOICE, P_M_BASEJC, c_yalign }, { "plcHide", PT_BOOL, P_M_PLCHIDE, NULL }, { "rSpRule", PT_INT, 0, NULL },
                               { "cGpRule", PT_INT, 1, NULL }, { "rSp", PT_INT, 2, NULL }, { "cSp", PT_INT, 3, NULL }, { "cGp", PT_INT, 4, NULL },
                               { "mcs", PT_MCS, 0, NULL } };
static const PropDef p_n[] = { { "chr", PT_CHAR, 0, NULL }, { "limLoc", PT_CHOICE, P_N_LIMLOC, c_limloc }, { "grow", PT_BOOL, P_N_GROW, NULL },
                               { "subHide", PT_BOOL, P_N_SUBHIDE, NULL }, { "supHide", PT_BOOL, P_N_SUPHIDE, NULL } };
static const PropDef p_ph[] = { { "show", PT_BOOL, P_PH_SHOW, NULL }, { "zeroWid", PT_BOOL, P_PH_ZEROWID, NULL }, { "zeroAsc", PT_BOOL, P_PH_ZEROASC, NULL },
                                { "zeroDesc", PT_BOOL, P_PH_ZERODESC, NULL }, { "transp", PT_BOOL, P_PH_TRANSP, NULL } };
static const PropDef p_rad[] = { { "degHide", PT_BOOL, P_RAD_DEGHIDE, NULL } };
static const PropDef p_ss[] = { { "alnScr", PT_BOOL, P_SS_ALNSCR, NULL } };
static const PropDef p_r[] = { { "lit", PT_BOOL, P_R_LIT, NULL }, { "nor", PT_BOOL, P_R_NOR, NULL }, { "scr", PT_CHOICE, P_R_SCR, c_scr },
                               { "sty", PT_CHOICE, P_R_STY, c_sty }, { "brk", PT_BRK, P_R_BRK, NULL }, { "aln", PT_BOOL, P_R_ALN, NULL } };

static const char *const a_e[] = { "e" };
static const char *const a_f[] = { "num", "den" };
static const char *const a_func[] = { "fName", "e" };
static const char *const a_lim[] = { "e", "lim" };
static const char *const a_nary[] = { "sub", "sup", "e" };
static const char *const a_rad[] = { "deg", "e" };
static const char *const a_ssub[] = { "e", "sub" };
static const char *const a_ssup[] = { "e", "sup" };
static const char *const a_sss[] = { "e", "sub", "sup" };
#define N(a) (int)(sizeof a / sizeof a[0])
static const KindDef kinds[MK_N] = {
  { "oMath", NULL, NULL, 0, NULL, 0 },
  { "r", "rPr", p_r, N(p_r), NULL, 0 },
  { "acc", "accPr", p_acc, N(p_acc), a_e, 1 },
  { "bar", "barPr", p_bar, N(p_bar), a_e, 1 },
  { "box", "boxPr", p_box, N(p_box), a_e, 1 },
  { "borderBox", "borderBoxPr", p_bb, N(p_bb), a_e, 1 },
  { "d", "dPr", p_d, N(p_d), a_e, -1 },
  { "eqArr", "eqArrPr", p_eq, N(p_eq), a_e, -1 },
  { "f", "fPr", p_f, N(p_f), a_f, 2 },
  { "func", "funcPr", NULL, 0, a_func, 2 },
  { "groupChr", "groupChrPr", p_g, N(p_g), a_e, 1 },
  { "limLow", "limLowPr", NULL, 0, a_lim, 2 },
  { "limUpp", "limUppPr", NULL, 0, a_lim, 2 },
  { "m", "mPr", p_m, N(p_m), a_e, -1 },
  { "nary", "naryPr", p_n, N(p_n), a_nary, 3 },
  { "phant", "phantPr", p_ph, N(p_ph), a_e, 1 },
  { "rad", "radPr", p_rad, N(p_rad), a_rad, 2 },
  { "sPre", "sPrePr", NULL, 0, a_nary, 3 },
  { "sSub", "sSubPr", NULL, 0, a_ssub, 2 },
  { "sSubSup", "sSubSupPr", p_ss, N(p_ss), a_sss, 3 },
  { "sSup", "sSupPr", NULL, 0, a_ssup, 2 },
  { "", NULL, NULL, 0, NULL, 0 }
};
int mn_nargs(int kind) { return kind >= 0 && kind < MK_N ? kinds[kind].nargs : 0; }
const char *mn_name(int kind) { return kind >= 0 && kind < MK_N ? kinds[kind].name : ""; }

/* ------------------------------------------------------------------ */
/* nodes */
MNode *mn_new(int kind)
{
  MNode *n = (MNode *)calloc(1, sizeof(MNode));
  int k;
  if (!n) return NULL;
  n->kind = (u8)kind;
  for (k = 0; k < 3; k++) n->ch[k] = MCH_UNSET;
  for (k = 0; k < 5; k++) n->iv[k] = MI_UNSET;
  n->color = COLOR_AUTO;
  return n;
}
void mn_free(MNode *n)
{
  u16 i;
  if (!n) return;
  for (i = 0; i < n->nkid; i++) mn_free(n->kid[i]);
  free(n->kid); free(n->mcs); free(n->text); free(n->rpr); free(n->ctrl); free(n->xpr);
  free(n);
}
static char *dup_or_null(const char *s, int *bad)
{
  char *c;
  if (!s) return NULL;
  if (!(c = str_dup(s))) *bad = 1;
  return c;
}
MNode *mn_clone(const MNode *n)
{
  MNode *c;
  int bad = 0;
  u16 i;
  if (!n || !(c = (MNode *)malloc(sizeof(MNode)))) return NULL;
  *c = *n;
  c->kid = NULL; c->nkid = c->kcap = 0; c->mcs = NULL;
  c->text = dup_or_null(n->text, &bad); c->rpr = dup_or_null(n->rpr, &bad);
  c->ctrl = dup_or_null(n->ctrl, &bad); c->xpr = dup_or_null(n->xpr, &bad);
  if (n->nmcs && (c->mcs = (u8 (*)[2])malloc(n->nmcs * 2))) memcpy(c->mcs, n->mcs, n->nmcs * 2);
  else if (n->nmcs) bad = 1;
  if (!c->mcs) c->nmcs = 0;
  if (n->nkid) {
    if (!(c->kid = (MNode **)calloc(n->nkid, sizeof(MNode *)))) bad = 1;
    else {
      c->kcap = n->nkid;
      for (i = 0; i < n->nkid; i++) {
        if (!(c->kid[i] = mn_clone(n->kid[i]))) { bad = 1; break; }
        c->nkid++;
      }
    }
  }
  if (bad) { mn_free(c); return NULL; }
  return c;
}
int mn_insert(MNode *p, int at, MNode *kid)
{
  if (!kid) return 0;
  if (p->nkid >= p->kcap) {
    u16 nc = (u16)(p->kcap ? p->kcap * 2 : 4);
    MNode **nk;
    if (p->kcap >= 32768 || !(nk = (MNode **)realloc(p->kid, nc * sizeof(MNode *)))) { mn_free(kid); return 0; }
    p->kid = nk; p->kcap = nc;
  }
  if (at < 0 || at > p->nkid) at = p->nkid;
  memmove(p->kid + at + 1, p->kid + at, (size_t)(p->nkid - at) * sizeof(MNode *));
  p->kid[at] = kid;
  p->nkid++;
  return 1;
}
MNode *mn_remove(MNode *p, int at)
{
  MNode *k;
  if (at < 0 || at >= p->nkid) return NULL;
  k = p->kid[at];
  memmove(p->kid + at, p->kid + at + 1, (size_t)(p->nkid - at - 1) * sizeof(MNode *));
  p->nkid--;
  return k;
}
MNode *mn_struct(int kind, int n, int cols)
{
  MNode *s = mn_new(kind);
  int i, na = mn_nargs(kind);
  if (!s) return NULL;
  if (kind == MK_MATRIX) {
    if (n < 1) n = 1;
    if (cols < 1) cols = 1;
    s->rows = (u16)n; s->cols = (u16)cols;
    na = n * cols;
  } else if (na < 0) na = n < 1 ? 1 : n;
  for (i = 0; i < na; i++) if (!mn_insert(s, -1, mn_new(MK_ROW))) { mn_free(s); return NULL; }
  return s;
}
MNode *mn_run(const char *text, size_t len)
{
  MNode *r = mn_new(MK_RUN);
  if (r && !(r->text = str_ndup(text, len))) { mn_free(r); return NULL; }
  return r;
}

MathEq *math_new(int display)
{
  MathEq *e = (MathEq *)calloc(1, sizeof(MathEq));
  if (!e) return NULL;
  e->display = (u8)display;
  if (!(e->line = (MNode **)calloc(1, sizeof(MNode *))) || !(e->line[0] = mn_new(MK_ROW))) { math_free(e); return NULL; }
  e->nline = 1;
  return e;
}
void math_free(MathEq *e)
{
  u16 i;
  if (!e) return;
  for (i = 0; i < e->nline; i++) mn_free(e->line[i]);
  free(e->line); free(e->ppr);
  math_lay_free(e->lay);
  free(e);
}
MathEq *math_clone(const MathEq *e)
{
  MathEq *c;
  u16 i;
  if (!e || !(c = (MathEq *)calloc(1, sizeof(MathEq)))) return NULL;
  c->display = e->display; c->jc = e->jc; c->ver = e->ver;
  if (e->ppr && !(c->ppr = str_dup(e->ppr))) goto oom;
  if (e->nline && !(c->line = (MNode **)calloc(e->nline, sizeof(MNode *)))) goto oom;
  for (i = 0; i < e->nline; i++) { if (!(c->line[i] = mn_clone(e->line[i]))) goto oom; c->nline++; }
  return c;
oom:
  math_free(c);
  return NULL;
}
static int node_empty(const MNode *n)
{
  u16 i;
  if (n->kind == MK_RUN) return !n->text || !n->text[0];
  if (n->kind != MK_ROW) return 0;
  for (i = 0; i < n->nkid; i++) if (!node_empty(n->kid[i])) return 0;
  return 1;
}
static u16 node_size(const MNode *n)
{
  u16 i, z;
  if (n->kind == MK_RUN) return n->size;
  for (i = 0; i < n->nkid; i++) if ((z = node_size(n->kid[i]))) return z;
  return 0;
}
u16 math_size(const MathEq *e)
{
  u16 i, z;
  for (i = 0; i < e->nline; i++) if ((z = node_size(e->line[i]))) return z;
  return 0;
}
int math_empty(const MathEq *e)
{
  u16 i;
  for (i = 0; i < e->nline; i++) if (!node_empty(e->line[i])) return 0;
  return 1;
}

/* ------------------------------------------------------------------ */
/* in the document */
int doc_add_math(Doc *d, MathEq *e)
{
  if (!e) return -1;
  if (d->nmaths >= MAX_MATHS) { math_free(e); return -1; }
  if (d->nmaths >= d->mathcap) {
    int nc = d->mathcap ? d->mathcap * 2 : 8;
    MathEq **nm = (MathEq **)realloc(d->maths, (size_t)nc * sizeof(MathEq *));
    if (!nm) { math_free(e); return -1; }
    d->maths = nm; d->mathcap = nc;
  }
  d->maths[d->nmaths] = e;
  return d->nmaths++;
}
MathEq *doc_math(const Doc *d, int k) { return k >= 0 && k < d->nmaths ? d->maths[k] : NULL; }
void math_doc_free(Doc *d)
{
  int i;
  for (i = 0; i < d->nmaths; i++) math_free(d->maths[i]);
  free(d->maths); d->maths = NULL; d->nmaths = d->mathcap = 0;
  free(d->mathpr); d->mathpr = NULL;
}
int doc_insert_math(Doc *d, Pos *at, MathEq *e, const CharFmt *f)
{
  char ref[8];
  int k = doc_add_math(d, e);
  if (k < 0) return -1;
  math_utf8(ref, k);
  doc_begin(d, *at);
  if (!doc_insert(d, at, ref, 4, f)) k = -1;
  doc_end(d, *at);
  return k;
}
int doc_set_math(Doc *d, Pos at, MathEq *e)
{
  Para *p;
  CharFmt f;
  Pos b;
  char ref[8];
  int k;
  if (at.para >= d->np || at.off + 4 > (p = d->p[at.para])->len || !IS_MATH(p->t + at.off)) { math_free(e); return -1; }
  f = *para_fmt_at(p, at.off);
  if ((k = doc_add_math(d, e)) < 0) return -1;
  math_utf8(ref, k);
  b = at; b.off += 4;
  doc_begin(d, at);
  if (!doc_delete(d, at, b) || !doc_insert(d, &at, ref, 4, &f)) k = -1;
  doc_end(d, at);
  return k;
}

/* ------------------------------------------------------------------ */
/* OMML in */
/* the start tag just read is in the m: namespace (as Word writes it: by its prefix) */
static int is_m(const Xml *x)
{
  const char *s = x->s + x->tok + 1;
  return x->tok + 3 < x->n && s[0] == 'm' && s[1] == ':';
}
static int on_off(const char *v)
{
  if (!v) return MB_ON;
  return !strcmp(v, "off") || !strcmp(v, "0") || !strcmp(v, "false") ? MB_OFF : MB_ON;
}
/* s with t appended (both malloc'd strings or NULL); s unchanged if out of memory */
static char *cat(char *s, const char *t, size_t n)
{
  size_t l = s ? strlen(s) : 0;
  char *r = (char *)realloc(s, l + n + 1);
  if (!r) return s;
  memcpy(r + l, t, n); r[l + n] = 0;
  return r;
}
static char *raw_copy(Xml *x)
{
  const char *st;
  size_t n = xml_raw(x, &st);
  return str_ndup(st, n);
}
static int read_row(Xml *x, MNode *row, int depth);
static MNode *read_struct(Xml *x, int kind, int depth);
/* a structure's (or run's) properties element: x at its START, read to its END */
static void read_props(Xml *x, MNode *n)
{
  const KindDef *kd = &kinds[n->kind];
  int d0 = x->depth;
  while (xml_next(x) != XT_EOF) {
    int i;
    const char *v;
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "ctrlPr")) { free(n->ctrl); n->ctrl = raw_copy(x); continue; }
    for (i = 0; i < kd->nprops; i++) if (!strcmp(x->name, kd->props[i].name)) break;
    if (i == kd->nprops || !is_m(x)) {
      const char *st;
      size_t len = xml_raw(x, &st);
      n->xpr = cat(n->xpr, st, len);
      continue;
    }
    v = xml_attr(x, "val");
    switch (kd->props[i].type) {
    case PT_BOOL: n->pv[kd->props[i].slot] = (u8)on_off(v); break;
    case PT_CHAR: {
      int used;
      n->ch[kd->props[i].slot] = v && v[0] ? utf8_get(v, strlen(v), &used) : 0;
      break; }
    case PT_CHOICE: {
      int c;
      for (c = 0; v && kd->props[i].choices[c]; c++) if (!strcmp(v, kd->props[i].choices[c])) { n->pv[kd->props[i].slot] = (u8)(c + 1); break; }
      break; }
    case PT_INT: if (v) n->iv[kd->props[i].slot] = (i16)atoi(v); break;
    case PT_BRK: {
      const char *a = xml_attr(x, "alnAt");
      n->pv[kd->props[i].slot] = MB_ON;
      n->iv[0] = a ? (i16)atoi(a) : MI_UNSET;
      break; }
    case PT_MCS: {
      /* m:mcs: m:mc each with m:mcPr (m:count, m:mcJc) */
      int d1 = x->depth;
      if (x->pending_end) break;
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type != XT_START) continue;
        if (!strcmp(x->name, "mc")) {
          u8 (*nm)[2] = (u8 (*)[2])realloc(n->mcs, (size_t)(n->nmcs + 1) * 2);
          if (nm) { n->mcs = nm; n->mcs[n->nmcs][0] = 1; n->mcs[n->nmcs][1] = 0; n->nmcs++; }
        } else if (n->nmcs && !strcmp(x->name, "count") && (v = xml_attr(x, "val"))) {
          int c = atoi(v);
          n->mcs[n->nmcs - 1][0] = (u8)(c < 1 ? 1 : c > 255 ? 255 : c);
        } else if (n->nmcs && !strcmp(x->name, "mcJc") && (v = xml_attr(x, "val"))) {
          int c;
          for (c = 0; c_mcjc[c]; c++) if (!strcmp(v, c_mcjc[c])) n->mcs[n->nmcs - 1][1] = (u8)(c + 1);
        }
      }
      continue; }
    }
    if (!x->pending_end) xml_skip(x); else xml_next(x);
  }
}
/* what layout uses from a run's w:rPr: size, colour, bold, italic, font */
static void run_wrpr(MNode *r)
{
  Xml y;
  xml_init(&y, r->rpr, strlen(r->rpr));
  while (xml_next(&y) != XT_EOF) {
    const char *v;
    if (y.type != XT_START) continue;
    v = xml_attr(&y, "val");
    if (!strcmp(y.name, "sz") && v) r->size = (u16)atoi(v);
    else if (!strcmp(y.name, "color") && v && strcmp(v, "auto")) r->color = (u32)strtoul(v, NULL, 16) & 0xFFFFFF;
    else if (!strcmp(y.name, "b")) r->wb = (u8)(on_off(v) == MB_ON);
    else if (!strcmp(y.name, "i")) r->wi = (u8)(on_off(v) == MB_ON);
    else if (!strcmp(y.name, "rFonts") && (v = xml_attr(&y, "ascii"))) { strncpy(r->font, v, sizeof r->font - 1); r->font[sizeof r->font - 1] = 0; }
  }
  xml_free(&y);
}
/* m:r: x at its START */
static MNode *read_run(Xml *x)
{
  MNode *r = mn_new(MK_RUN);
  Buf t;
  int d0 = x->depth;
  if (!r) { xml_skip(x); return NULL; }
  buf_init(&t);
  if (!x->pending_end) while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "rPr") && is_m(x)) { if (x->pending_end) xml_next(x); else read_props(x, r); }
    else if (!strcmp(x->name, "rPr")) { free(r->rpr); if ((r->rpr = raw_copy(x))) run_wrpr(r); }
    else if (!strcmp(x->name, "t")) {
      int d1 = x->depth;
      if (x->pending_end) { xml_next(x); continue; }
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type == XT_TEXT) buf_add(&t, x->text.data, x->text.len);
      }
    } else {                                    /* anything else in the run (w:br, w:sym...): kept */
      const char *st;
      size_t len = xml_raw(x, &st);
      r->xpr = cat(r->xpr, st, len);
    }
  } else xml_next(x);
  r->text = t.data ? buf_take(&t) : str_dup("");
  buf_free(&t);
  if (!r->text) { mn_free(r); return NULL; }
  return r;
}
static int kind_of(const char *name)
{
  int k;
  for (k = MK_ACC; k < MK_RAW; k++) if (!strcmp(name, kinds[k].name)) return k;
  return -1;
}
/* elements of a row: x after the row's START; read to its END. depth: nesting (a guard) */
static int read_row(Xml *x, MNode *row, int depth)
{
  int d0 = x->depth, k;
  if (x->pending_end) { xml_next(x); return 1; }
  while (xml_next(x) != XT_EOF) {
    MNode *n = NULL;
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (is_m(x) && !strcmp(x->name, "argPr")) {
      int d1 = x->depth;
      if (x->pending_end) { xml_next(x); continue; }
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type == XT_START && !strcmp(x->name, "argSz")) {
          const char *v = xml_attr(x, "val");
          row->argsz = (signed char)(v ? atoi(v) : 0); row->hasargsz = 1;
        }
      }
      continue;
    }
    if (is_m(x) && !strcmp(x->name, "ctrlPr")) { free(row->ctrl); row->ctrl = raw_copy(x); continue; }
    if (is_m(x) && !strcmp(x->name, "r")) n = read_run(x);
    else if (is_m(x) && (k = kind_of(x->name)) >= 0 && depth < 60) n = read_struct(x, k, depth + 1);
    else {
      /* kept as it is; a tracked insertion's (or a wrapper's) math shown as well */
      int show = !strcmp(x->name, "ins") || !strcmp(x->name, "moveTo") || !strcmp(x->name, "smartTag") || !strcmp(x->name, "customXml");
      size_t at = x->tok;
      const char *st;
      size_t len;
      if ((n = mn_new(MK_RAW)) && show && !x->pending_end) {
        read_row(x, n, depth + 1);
        st = x->s + at; len = x->i - at;
      } else len = xml_raw(x, &st);
      if (n && !(n->text = str_ndup(st, len))) { mn_free(n); n = NULL; }
    }
    if (n && !mn_insert(row, -1, n)) return 0;
  }
  return 1;
}
/* a structure: x at its START, read to its END */
static MNode *read_struct(Xml *x, int kind, int depth)
{
  const KindDef *kd = &kinds[kind];
  MNode *s = mn_new(kind);
  int d0 = x->depth, fixed = kd->nargs > 0, i;
  int filled[3] = { 0, 0, 0 };
  if (!s) { xml_skip(x); return NULL; }
  if (fixed) for (i = 0; i < kd->nargs; i++) if (!mn_insert(s, -1, mn_new(MK_ROW))) { mn_free(s); xml_skip(x); return NULL; }
  if (x->pending_end) { xml_next(x); return s; }
  while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (kd->pr && !strcmp(x->name, kd->pr)) { if (x->pending_end) xml_next(x); else read_props(x, s); continue; }
    if (kind == MK_MATRIX && !strcmp(x->name, "mr")) {
      /* a row of cells */
      int d1 = x->depth, c = 0;
      if (x->pending_end) { xml_next(x); continue; }
      s->rows++;
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type == XT_START && !strcmp(x->name, "e")) {
          MNode *e = mn_new(MK_ROW);
          if (!e || !mn_insert(s, -1, e)) { xml_skip(x); continue; }
          read_row(x, e, depth);
          c++;
        } else if (x->type == XT_START) xml_skip(x);
      }
      if (s->rows == 1) s->cols = (u16)c;
      /* every row as wide as the first */
      while (c < s->cols) { if (!mn_insert(s, -1, mn_new(MK_ROW))) break; c++; }
      while (c > s->cols && s->nkid) { mn_free(mn_remove(s, s->nkid - 1)); c--; }
      continue;
    }
    if (!fixed) {
      if (!strcmp(x->name, "e")) {
        MNode *e = mn_new(MK_ROW);
        if (!e || !mn_insert(s, -1, e)) { xml_skip(x); continue; }
        read_row(x, e, depth);
      } else xml_skip(x);
      continue;
    }
    for (i = 0; i < kd->nargs; i++) if (!filled[i] && !strcmp(x->name, kd->args[i])) break;
    if (i < kd->nargs) { filled[i] = 1; read_row(x, s->kid[i], depth); }
    else xml_skip(x);
  }
  if (kind == MK_MATRIX && !s->cols) s->rows = 0;
  if (!fixed && kind != MK_MATRIX && !s->nkid) mn_insert(s, -1, mn_new(MK_ROW));
  return s;
}
MathEq *math_read_omml(Xml *x)
{
  MathEq *e = (MathEq *)calloc(1, sizeof(MathEq));
  int d0 = x->depth;
  if (!e) { xml_skip(x); return NULL; }
  if (!strcmp(x->name, "oMath")) {
    MNode *row = mn_new(MK_ROW);
    if (!row || !(e->line = (MNode **)malloc(sizeof(MNode *)))) { mn_free(row); free(e); xml_skip(x); return NULL; }
    e->line[0] = row; e->nline = 1;
    read_row(x, row, 0);
    return e;
  }
  e->display = 1;
  if (x->pending_end) xml_next(x);
  else while (xml_next(x) != XT_EOF) {
    if (x->type == XT_END && x->depth < d0) break;
    if (x->type != XT_START) continue;
    if (!strcmp(x->name, "oMathParaPr")) {
      int d1 = x->depth;
      if (x->pending_end) { xml_next(x); continue; }
      while (xml_next(x) != XT_EOF) {
        if (x->type == XT_END && x->depth < d1) break;
        if (x->type != XT_START) continue;
        if (!strcmp(x->name, "jc")) {
          const char *v = xml_attr(x, "val");
          int c;
          for (c = 0; v && c_pjc[c]; c++) if (!strcmp(v, c_pjc[c])) e->jc = (u8)(c + 1);
          if (!x->pending_end) xml_skip(x); else xml_next(x);
        } else {
          const char *st;
          size_t len = xml_raw(x, &st);
          e->ppr = cat(e->ppr, st, len);
        }
      }
    } else if (!strcmp(x->name, "oMath")) {
      MNode *row = mn_new(MK_ROW), **nl;
      if (!row || !(nl = (MNode **)realloc(e->line, (e->nline + 1) * sizeof(MNode *)))) { mn_free(row); xml_skip(x); continue; }
      e->line = nl; e->line[e->nline++] = row;
      read_row(x, row, 0);
    } else xml_skip(x);
  }
  if (!e->nline) {
    if (!(e->line = (MNode **)malloc(sizeof(MNode *))) || !(e->line[0] = mn_new(MK_ROW))) { math_free(e); return NULL; }
    e->nline = 1;
  }
  return e;
}

/* ------------------------------------------------------------------ */
/* OMML out */
#define MNS "xmlns:m=\"http://schemas.openxmlformats.org/officeDocument/2006/math\""
/* namespaces verbatim pieces may use beyond w:, m: and r: (which every part Quill writes declares
   or the equation declares itself) */
static const char *const extra_ns[][2] = {
  { "w14", "http://schemas.microsoft.com/office/word/2010/wordml" },
  { "w15", "http://schemas.microsoft.com/office/word/2012/wordml" },
  { "mc", "http://schemas.openxmlformats.org/markup-compatibility/2006" },
  { "wp14", "http://schemas.microsoft.com/office/word/2010/wordprocessingDrawing" },
  { "a", "http://schemas.openxmlformats.org/drawingml/2006/main" },
  { "wp", "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing" },
  { "v", "urn:schemas-microsoft-com:vml" },
  { "o", "urn:schemas-microsoft-com:office:office" }
};
#define NEXTRA (int)(sizeof extra_ns / sizeof extra_ns[0])
/* which of extra_ns the verbatim XML s uses in its tags (bits); a prefix nobody knows sets bit 31 */
static u32 ns_used(const char *s)
{
  u32 m = 0;
  int intag = 0, quote = 0;
  const char *p;
  if (!s) return 0;
  for (p = s; *p; p++) {
    if (quote) { if (*p == quote) quote = 0; continue; }
    if (*p == '<') { intag = 1; continue; }
    if (!intag) continue;
    if (*p == '>') { intag = 0; continue; }
    if (*p == '"' || *p == '\'') { quote = *p; continue; }
    if ((p[-1] == '<' || p[-1] == '/' || p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r') &&
        ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) {
      const char *b = p;
      size_t n;
      int k;
      while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.') p++;
      n = (size_t)(p - b);
      if (*p != ':') { p--; continue; }
      if ((n == 1 && (*b == 'w' || *b == 'm' || *b == 'r')) || (n == 3 && !strncmp(b, "xml", 3)) || (n == 5 && !strncmp(b, "xmlns", 5))) continue;
      for (k = 0; k < NEXTRA; k++) if (strlen(extra_ns[k][0]) == n && !strncmp(b, extra_ns[k][0], n)) { m |= 1UL << k; break; }
      if (k == NEXTRA) m |= 0x80000000UL;
    }
  }
  return m;
}
static u32 node_ns(const MNode *n)
{
  u32 m = ns_used(n->rpr) | ns_used(n->ctrl) | ns_used(n->xpr);
  u16 i;
  if (n->kind == MK_RAW) m |= ns_used(n->text);
  for (i = 0; i < n->nkid; i++) m |= node_ns(n->kid[i]);
  return m;
}
/* verbatim text written unless it uses a namespace no declaration covers */
static void put_raw(Buf *o, const char *s)
{
  if (s && !(ns_used(s) & 0x80000000UL)) buf_adds(o, s);
}
static void put_val(Buf *o, const char *el, const char *v)
{
  buf_printf(o, "<m:%s m:val=\"", el);
  xml_escape(o, v, strlen(v));
  buf_adds(o, "\"/>");
}
static void put_chr(Buf *o, const char *el, u32 c)
{
  char u[8];
  if (c == MCH_UNSET) return;
  u[utf8_put(u, c)] = 0;
  put_val(o, el, c ? u : "");
}
static void write_props(Buf *o, const MNode *n)
{
  const KindDef *kd = &kinds[n->kind];
  int i, any = n->ctrl || n->xpr;
  Buf b;
  buf_init(&b);
  for (i = 0; i < kd->nprops; i++) {
    const PropDef *pd = &kd->props[i];
    switch (pd->type) {
    case PT_BOOL: if (n->pv[pd->slot]) buf_printf(&b, "<m:%s m:val=\"%s\"/>", pd->name, n->pv[pd->slot] == MB_ON ? "1" : "0"); break;
    case PT_CHAR: put_chr(&b, pd->name, n->ch[pd->slot]); break;
    case PT_CHOICE: if (n->pv[pd->slot]) put_val(&b, pd->name, pd->choices[n->pv[pd->slot] - 1]); break;
    case PT_INT: if (n->iv[pd->slot] != MI_UNSET) buf_printf(&b, "<m:%s m:val=\"%d\"/>", pd->name, (int)n->iv[pd->slot]); break;
    case PT_BRK:
      if (n->pv[pd->slot] == MB_ON) {
        if (n->iv[0] != MI_UNSET) buf_printf(&b, "<m:brk m:alnAt=\"%d\"/>", (int)n->iv[0]);
        else buf_adds(&b, "<m:brk/>");
      }
      break;
    case PT_MCS:
      if (n->nmcs) {
        u16 k;
        buf_adds(&b, "<m:mcs>");
        for (k = 0; k < n->nmcs; k++) {
          buf_printf(&b, "<m:mc><m:mcPr><m:count m:val=\"%u\"/>", (unsigned)n->mcs[k][0]);
          if (n->mcs[k][1]) buf_printf(&b, "<m:mcJc m:val=\"%s\"/>", c_mcjc[n->mcs[k][1] - 1]);
          buf_adds(&b, "</m:mcPr></m:mc>");
        }
        buf_adds(&b, "</m:mcs>");
      }
      break;
    }
  }
  if (b.len || any) {
    buf_printf(o, "<m:%s>", kd->pr);
    if (b.data) buf_add(o, b.data, b.len);
    put_raw(o, n->xpr);
    put_raw(o, n->ctrl);
    buf_printf(o, "</m:%s>", kd->pr);
  }
  buf_free(&b);
}
static void write_row(Buf *o, const MNode *row, const char *rpr, size_t rl);
static void write_arg(Buf *o, const char *el, const MNode *row, const char *rpr, size_t rl)
{
  buf_printf(o, "<m:%s>", el);
  if (row->hasargsz) buf_printf(o, "<m:argPr><m:argSz m:val=\"%d\"/></m:argPr>", (int)row->argsz);
  write_row(o, row, rpr, rl);
  put_raw(o, row->ctrl);
  buf_printf(o, "</m:%s>", el);
}
static void write_node(Buf *o, const MNode *n, const char *rpr, size_t rl)
{
  const KindDef *kd = &kinds[n->kind];
  u16 i;
  if (n->kind == MK_RAW) { put_raw(o, n->text); return; }
  if (n->kind == MK_ROW) { write_row(o, n, rpr, rl); return; }
  if (n->kind == MK_RUN) {
    const char *t = n->text ? n->text : "";
    size_t tl = strlen(t);
    buf_adds(o, "<m:r>");
    write_props(o, n);
    if (n->rpr) put_raw(o, n->rpr);
    else if (rpr && rl) buf_add(o, rpr, rl);
    buf_adds(o, tl && (t[0] == ' ' || t[tl - 1] == ' ') ? "<m:t xml:space=\"preserve\">" : "<m:t>");
    xml_escape(o, t, tl);
    buf_adds(o, "</m:t>");
    put_raw(o, n->xpr);
    buf_adds(o, "</m:r>");
    return;
  }
  buf_printf(o, "<m:%s>", kd->name);
  write_props(o, n);
  if (n->kind == MK_MATRIX) {
    u16 r, c;
    for (r = 0; r < n->rows; r++) {
      buf_adds(o, "<m:mr>");
      for (c = 0; c < n->cols && r * n->cols + c < n->nkid; c++) write_arg(o, "e", n->kid[r * n->cols + c], rpr, rl);
      buf_adds(o, "</m:mr>");
    }
  } else for (i = 0; i < n->nkid; i++) write_arg(o, kd->nargs > 0 && i < kd->nargs ? kd->args[i] : "e", n->kid[i], rpr, rl);
  buf_printf(o, "</m:%s>", kd->name);
}
static void write_row(Buf *o, const MNode *row, const char *rpr, size_t rl)
{
  u16 i;
  for (i = 0; i < row->nkid; i++) write_node(o, row->kid[i], rpr, rl);
}
/* the namespace declarations an element of the equation needs */
static void write_ns(Buf *o, const MathEq *e, const char *rpr, size_t rl)
{
  u32 m = 0;
  u16 i;
  int k;
  for (i = 0; i < e->nline; i++) m |= node_ns(e->line[i]);
  m |= ns_used(e->ppr);
  if (rpr && rl) { char *c = str_ndup(rpr, rl); m |= ns_used(c); free(c); }
  buf_adds(o, " " MNS);
  for (k = 0; k < NEXTRA; k++) if (m & (1UL << k)) buf_printf(o, " xmlns:%s=\"%s\"", extra_ns[k][0], extra_ns[k][1]);
}
static void math_write_lines(Buf *o, const MathEq *e, const char *rpr, size_t rl);
void math_write_omml(Buf *o, const MathEq *e, const char *rpr, size_t rl)
{
  Buf mr;
  /* runs without a w:rPr of their own get Word's math font and the text's size and colour */
  buf_init(&mr);
  buf_adds(&mr, "<w:rPr><w:rFonts w:ascii=\"Cambria Math\" w:hAnsi=\"Cambria Math\"/>");
  if (rpr && rl) {
    Xml x;
    xml_init(&x, rpr, rl);
    while (xml_next(&x) != XT_EOF)
      if (x.type == XT_START && (!strcmp(x.name, "color") || !strcmp(x.name, "sz") || !strcmp(x.name, "szCs"))) {
        const char *st;
        size_t n = xml_raw(&x, &st);
        buf_add(&mr, st, n);
      }
    xml_free(&x);
  }
  buf_adds(&mr, "</w:rPr>");
  rpr = mr.oom ? NULL : mr.data; rl = mr.oom ? 0 : mr.len;
  math_write_lines(o, e, rpr, rl);
  buf_free(&mr);
}
static void math_write_lines(Buf *o, const MathEq *e, const char *rpr, size_t rl)
{
  u16 i;
  if (!e->display) {
    buf_adds(o, "<m:oMath");
    write_ns(o, e, rpr, rl);
    buf_adds(o, ">");
    if (e->nline) write_row(o, e->line[0], rpr, rl);
    buf_adds(o, "</m:oMath>");
    return;
  }
  buf_adds(o, "<m:oMathPara");
  write_ns(o, e, rpr, rl);
  buf_adds(o, ">");
  if (e->jc || e->ppr) {
    buf_adds(o, "<m:oMathParaPr>");
    if (e->jc) buf_printf(o, "<m:jc m:val=\"%s\"/>", c_pjc[e->jc - 1]);
    put_raw(o, e->ppr);
    buf_adds(o, "</m:oMathParaPr>");
  }
  for (i = 0; i < e->nline; i++) {
    buf_adds(o, "<m:oMath>");
    write_row(o, e->line[i], rpr, rl);
    buf_adds(o, "</m:oMath>");
  }
  buf_adds(o, "</m:oMathPara>");
}
void math_read_mathpr(Doc *d, Xml *x)
{
  free(d->mathpr);
  d->mathpr = raw_copy(x);
}
void math_write_mathpr(const Doc *d, Buf *o)
{
  const char *s = d->mathpr, *gt;
  if (!s || strncmp(s, "<m:mathPr", 9) || (ns_used(s) & 0x80000000UL)) return;
  /* its namespace declared on it (settings.xml's root declares only w:) */
  if (strstr(s, "xmlns:m=")) { buf_adds(o, s); return; }
  buf_adds(o, "<m:mathPr " MNS);
  gt = s + 9;
  buf_adds(o, gt);
}
void math_settings(const Doc *d, MathPr *mp)
{
  Xml x;
  memset(mp, 0, sizeof *mp);
  mp->narysub = 0; mp->intsub = 1; mp->wrapind = 1440; mp->defjc = MPJC_CENTERGROUP + 1;
  if (!d->mathpr) return;
  xml_init(&x, d->mathpr, strlen(d->mathpr));
  while (xml_next(&x) != XT_EOF) {
    const char *v;
    if (x.type != XT_START || !(v = xml_attr(&x, "val"))) continue;
    if (!strcmp(x.name, "intLim")) mp->intsub = (u8)!strcmp(v, "subSup");
    else if (!strcmp(x.name, "naryLim")) mp->narysub = (u8)!strcmp(v, "subSup");
    else if (!strcmp(x.name, "smallFrac")) mp->smallfrac = (u8)(on_off(v) == MB_ON);
    else if (!strcmp(x.name, "wrapIndent")) mp->wrapind = atoi(v);
    else if (!strcmp(x.name, "defJc")) { int c; for (c = 0; c_pjc[c]; c++) if (!strcmp(v, c_pjc[c])) mp->defjc = (u8)(c + 1); }
  }
  xml_free(&x);
}

i32 math_line_shift(const Doc *d, int k, i32 extra)
{
  const MathEq *e = doc_math(d, k);
  int jc;
  if (extra <= 0) return 0;
  jc = e && e->jc ? e->jc - 1 : -1;
  if (jc < 0) { MathPr mp; math_settings(d, &mp); jc = mp.defjc ? mp.defjc - 1 : MPJC_CENTERGROUP; }
  return jc == MPJC_LEFT ? 0 : jc == MPJC_RIGHT ? extra : extra / 2;
}

/* ------------------------------------------------------------------ */
/* linear format: control words */
typedef struct { const char *name; u32 cp; } Kw;
static const Kw kw[] = {
  { "Alpha", 0x391 }, { "Beta", 0x392 }, { "Chi", 0x3A7 }, { "Delta", 0x394 }, { "Epsilon", 0x395 }, { "Eta", 0x397 }, { "Gamma", 0x393 },
  { "Im", 0x2111 }, { "Iota", 0x399 }, { "Kappa", 0x39A }, { "Lambda", 0x39B }, { "Mu", 0x39C }, { "Nu", 0x39D }, { "Omega", 0x3A9 },
  { "Omicron", 0x39F }, { "Phi", 0x3A6 }, { "Pi", 0x3A0 }, { "Psi", 0x3A8 }, { "Re", 0x211C }, { "Rho", 0x3A1 }, { "Sigma", 0x3A3 },
  { "Tau", 0x3A4 }, { "Theta", 0x398 }, { "Upsilon", 0x3A5 }, { "Xi", 0x39E }, { "Zeta", 0x396 },
  { "Leftarrow", 0x21D0 }, { "Leftrightarrow", 0x21D4 }, { "Rightarrow", 0x21D2 }, { "Uparrow", 0x21D1 }, { "Downarrow", 0x21D3 },
  { "above", 0x2534 }, { "acute", 0x301 }, { "aleph", 0x2135 }, { "alpha", 0x3B1 }, { "amalg", 0x2210 }, { "angle", 0x2220 },
  { "approx", 0x2248 }, { "ast", 0x2217 }, { "bar", 0x305 }, { "below", 0x252C }, { "beta", 0x3B2 }, { "bot", 0x22A5 },
  { "box", 0x25A1 }, { "breve", 0x306 }, { "bullet", 0x2219 }, { "cap", 0x2229 }, { "cbrt", 0x221B }, { "cdot", 0x22C5 },
  { "cdots", 0x22EF }, { "check", 0x30C }, { "chi", 0x3C7 }, { "circ", 0x2218 }, { "close", 0x2524 }, { "coprod", 0x2210 },
  { "cup", 0x222A }, { "dagger", 0x2020 }, { "dd", 0x2146 }, { "ddot", 0x308 }, { "ddots", 0x22F1 }, { "degree", 0xB0 },
  { "delta", 0x3B4 }, { "div", 0xF7 }, { "dot", 0x307 }, { "doteq", 0x2250 }, { "dots", 0x2026 }, { "downarrow", 0x2193 },
  { "ee", 0x2147 }, { "ell", 0x2113 }, { "emptyset", 0x2205 }, { "epsilon", 0x3F5 }, { "eqarray", 0x2588 }, { "equiv", 0x2261 },
  { "eta", 0x3B7 }, { "exists", 0x2203 }, { "forall", 0x2200 }, { "funcapply", 0x2061 }, { "gamma", 0x3B3 }, { "ge", 0x2265 },
  { "geq", 0x2265 }, { "gg", 0x226B }, { "grave", 0x300 }, { "hat", 0x302 }, { "hbar", 0x210F }, { "iiint", 0x222D }, { "iint", 0x222C },
  { "ii", 0x2148 }, { "in", 0x2208 }, { "inc", 0x2206 }, { "infty", 0x221E }, { "int", 0x222B }, { "iota", 0x3B9 }, { "jj", 0x2149 },
  { "kappa", 0x3BA }, { "lambda", 0x3BB }, { "langle", 0x27E8 }, { "lbrace", '{' }, { "lbrack", '[' }, { "lceil", 0x2308 },
  { "ldots", 0x2026 }, { "le", 0x2264 }, { "left", 0x251C }, { "leftarrow", 0x2190 }, { "leftrightarrow", 0x2194 }, { "leq", 0x2264 },
  { "lfloor", 0x230A }, { "ll", 0x226A }, { "matrix", 0x25A0 }, { "mid", 0x2223 }, { "mp", 0x2213 }, { "mu", 0x3BC }, { "nabla", 0x2207 },
  { "ne", 0x2260 }, { "neg", 0xAC }, { "neq", 0x2260 }, { "ni", 0x220B }, { "norm", 0x2016 }, { "notin", 0x2209 }, { "nu", 0x3BD },
  { "odot", 0x2299 }, { "oint", 0x222E }, { "omega", 0x3C9 }, { "ominus", 0x2296 }, { "open", 0x251C }, { "oplus", 0x2295 },
  { "oslash", 0x2298 }, { "otimes", 0x2297 }, { "overbar", 0xAF }, { "overbrace", 0x23DE }, { "overparen", 0x23DC }, { "parallel", 0x2225 },
  { "partial", 0x2202 }, { "perp", 0x22A5 }, { "phantom", 0x27E1 }, { "phi", 0x3D5 }, { "pi", 0x3C0 }, { "pm", 0xB1 }, { "prime", 0x2032 },
  { "prod", 0x220F }, { "propto", 0x221D }, { "psi", 0x3C8 }, { "qdrt", 0x221C }, { "rangle", 0x27E9 }, { "rbrace", '}' }, { "rbrack", ']' },
  { "rceil", 0x2309 }, { "rect", 0x25AD }, { "rfloor", 0x230B }, { "rho", 0x3C1 }, { "right", 0x2524 }, { "rightarrow", 0x2192 },
  { "sigma", 0x3C3 }, { "sim", 0x223C }, { "simeq", 0x2243 }, { "sqrt", 0x221A }, { "subset", 0x2282 }, { "subseteq", 0x2286 },
  { "sum", 0x2211 }, { "supset", 0x2283 }, { "supseteq", 0x2287 }, { "tau", 0x3C4 }, { "therefore", 0x2234 }, { "theta", 0x3B8 },
  { "tilde", 0x303 }, { "times", 0xD7 }, { "to", 0x2192 }, { "top", 0x22A4 }, { "underbar", 0x2581 }, { "underbrace", 0x23DF },
  { "underparen", 0x23DD }, { "uparrow", 0x2191 }, { "upsilon", 0x3C5 }, { "varepsilon", 0x3B5 }, { "varphi", 0x3C6 }, { "vdots", 0x22EE },
  { "vec", 0x20D7 }, { "vee", 0x2228 }, { "vert", '|' }, { "wedge", 0x2227 }, { "xi", 0x3BE }, { "zeta", 0x3B6 }, { "Vert", 0x2016 },
  { "bigcap", 0x22C2 }, { "bigcup", 0x22C3 }, { "bigvee", 0x22C1 }, { "bigwedge", 0x22C0 }, { "bigoplus", 0x2A01 }, { "bigotimes", 0x2A02 },
  { "ndiv", 0x2298 }, { "sdiv", 0x2044 }, { "ldiv", 0x2215 }, { "atop", 0xA6 }, { "naryand", 0x2592 }, { "of", 0x2592 },
  { "boxdot", 0x22A1 }, { "lbbrack", 0x27E6 }, { "rbbrack", 0x27E7 }, { "begin", 0x3016 }, { "end", 0x3017 }
};
static int is_combining_cp(u32 c) { return (c >= 0x300 && c <= 0x36F) || (c >= 0x20D0 && c <= 0x20FF); }
u32 math_keyword(const char *name, size_t n)
{
  size_t k;
  for (k = 0; k < sizeof kw / sizeof kw[0]; k++) if (strlen(kw[k].name) == n && !memcmp(kw[k].name, name, n)) return kw[k].cp;
  return 0;
}

void math_keywords(const char *s, Buf *o)
{
  size_t i = 0, n = strlen(s), k;
  while (i < n) {
    int used;
    u32 c = utf8_get(s + i, n - i, &used);
    if (c < 0x100) buf_add(o, s + i, (size_t)used);
    else if (c == 0x2212 || c == 0x2217 || c == 0x2061) buf_addc(o, c == 0x2212 ? '-' : c == 0x2217 ? '*' : ' ');
    else {
      for (k = 0; k < sizeof kw / sizeof kw[0] && kw[k].cp != c; k++) ;
      if (k < sizeof kw / sizeof kw[0]) {
        buf_addc(o, '\\'); buf_adds(o, kw[k].name);
        if (!is_combining_cp(c)) buf_addc(o, ' ');
      } else buf_add(o, s + i, (size_t)used);
    }
    i += (size_t)used;
  }
}

/* ------------------------------------------------------------------ */
/* linear format in: the text as code points (control words replaced), parsed by recursive descent */
typedef struct { u32 *c; int n, i; int depth; int oom; int noscr; } Lin;
static int lpeek(Lin *l) { return l->i < l->n ? (int)l->c[l->i] : -1; }
static int is_combining(u32 c) { return (c >= 0x300 && c <= 0x36F) || (c >= 0x20D0 && c <= 0x20FF); }
static int is_nary(u32 c)
{
  return (c >= 0x220F && c <= 0x2211) || (c >= 0x222B && c <= 0x2233) || (c >= 0x22C0 && c <= 0x22C3) || (c >= 0x2A00 && c <= 0x2A0C);
}
static int is_open(u32 c) { return c == '(' || c == '[' || c == '{' || c == 0x27E8 || c == 0x230A || c == 0x2308 || c == 0x27E6 || c == 0x3016 || c == 0x251C; }
static u32 close_of(u32 c)
{
  switch (c) {
  case '(': return ')'; case '[': return ']'; case '{': return '}'; case 0x27E8: return 0x27E9; case 0x230A: return 0x230B;
  case 0x2308: return 0x2309; case 0x27E6: return 0x27E7; case 0x3016: return 0x3017; case 0x251C: return 0x2524;
  }
  return 0;
}
static int is_close(u32 c) { return c == ')' || c == ']' || c == '}' || c == 0x27E9 || c == 0x230B || c == 0x2309 || c == 0x27E7 || c == 0x3017 || c == 0x2524; }
static int is_letter(u32 c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= 0x391 && c <= 0x3C9) || c == 0x3D5 || c == 0x3F5 || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7); }
static int is_digit(u32 c) { return (c >= '0' && c <= '9') || c == '.'; }
/* an operator that ends an operand (a fraction's, an n-ary's...) */
static int is_operator(u32 c)
{
  static const char ops[] = "+-=<>,;:!*";
  if (c < 128) return c && strchr(ops, (int)c) != NULL;
  return c == 0xB1 || c == 0xD7 || c == 0xF7 || c == 0x2212 || c == 0x2213 || (c >= 0x2190 && c <= 0x21FF) || (c >= 0x2208 && c <= 0x220D) ||
         (c >= 0x2215 && c <= 0x221D && c != 0x221A && c != 0x221B && c != 0x221C) || (c >= 0x2227 && c <= 0x222A) || (c >= 0x2234 && c <= 0x22FF && !is_nary(c)) ||
         c == 0x22C5 || c == 0x2217 || c == 0x2218 || c == 0x2219;
}
static const char *const funcs[] = { "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh", "coth", "sin", "cos", "tan", "cot", "sec", "csc",
                                     "log", "ln", "lg", "exp", "lim", "max", "min", "det", "arg", "deg", "dim", "gcd", "hom", "inf", "ker",
                                     "sup", "Pr", "mod", NULL };
static MNode *lin_row(Lin *l, const u32 *stop);
static MNode *lin_operand(Lin *l);
static MNode *run_cp(const u32 *c, int n)
{
  char buf[256];
  int k = 0, i;
  for (i = 0; i < n && k < (int)sizeof buf - 4; i++) k += utf8_put(buf + k, c[i]);
  return mn_run(buf, (size_t)k);
}
/* a row holding just node n (taken) */
static MNode *row_of(MNode *n)
{
  MNode *r = mn_new(MK_ROW);
  if (!r) { mn_free(n); return NULL; }
  if (n && !mn_insert(r, -1, n)) { mn_free(r); return NULL; }
  return r;
}
/* an argument from an operand: a row, its outer parentheses dropped (UnicodeMath's rule) */
static MNode *arg_of(MNode *n)
{
  if (n && n->kind == MK_DELIM && n->nkid == 1 && n->ch[0] == MCH_UNSET && n->ch[2] == MCH_UNSET) {
    MNode *in = mn_remove(n, 0);
    mn_free(n);
    return in;
  }
  if (n && n->kind == MK_ROW) return n;
  return row_of(n);
}
/* the operand a fraction, n-ary or function takes after it: operands up to an operator, a space or a stop */
static MNode *lin_group(Lin *l, const u32 *stop)
{
  MNode *r = mn_new(MK_ROW);
  if (!r) { l->oom = 1; return NULL; }
  for (;;) {
    int c = lpeek(l), k;
    MNode *n;
    if (c < 0 || c == ' ' || is_operator((u32)c) || is_close((u32)c) || c == '/' || c == 0x2298 || c == 0x2215 || c == 0xA6 || c == 0x2044) break;
    for (k = 0; stop[k]; k++) if ((u32)c == stop[k]) break;
    if (stop[k]) break;
    if (!(n = lin_operand(l))) break;
    if (!mn_insert(r, -1, n)) { l->oom = 1; break; }
  }
  /* one parenthesised operand: its contents */
  if (r->nkid == 1 && r->kid[0]->kind == MK_DELIM) {
    MNode *d = mn_remove(r, 0);
    mn_free(r);
    return arg_of(d);
  }
  return r;
}
/* a script's operand: one operand (a sign may lead), its parentheses dropped */
static MNode *lin_script(Lin *l)
{
  int c = lpeek(l);
  MNode *n;
  if (c == '-' || c == '+' || c == 0xB1 || c == 0x2212) {
    MNode *r = mn_new(MK_ROW), *s;
    u32 cc = (u32)c;
    l->i++;
    s = run_cp(&cc, 1);
    if (!r || !mn_insert(r, -1, s)) { mn_free(r); return NULL; }
    if (lpeek(l) >= 0 && lpeek(l) != ' ' && (n = lin_operand(l))) {
      MNode *a = arg_of(n);
      while (a && a->nkid) mn_insert(r, -1, mn_remove(a, 0));
      mn_free(a);
    }
    return r;
  }
  if (c < 0 || c == ' ') return mn_new(MK_ROW);
  l->noscr++;                         /* (x_i^2: the ^ belongs to x, not to i) */
  n = lin_operand(l);
  l->noscr--;
  return n ? arg_of(n) : mn_new(MK_ROW);
}
static const u32 st_none[] = { 0 };
static const u32 st_paren[] = { ')', 0 };
/* a parenthesised argument list for ■ █ □ ...: "(" row ")" or a plain operand */
static MNode *lin_parens(Lin *l, const u32 *stop)
{
  MNode *r;
  if (lpeek(l) != '(') { MNode *n = lin_operand(l); return n ? arg_of(n) : mn_new(MK_ROW); }
  l->i++;
  r = lin_row(l, stop);
  return r;
}
/* scripts after a base (taken): _sub ^sup in either order, then ┬ ┴ limits; also a following combining accent */
static MNode *lin_scripts(Lin *l, MNode *base)
{
  for (;;) {
    int c = lpeek(l);
    if (!base) return NULL;
    if (c == '_' || c == '^') {
      MNode *sub = NULL, *sup = NULL, *s;
      while ((c = lpeek(l)) == '_' || c == '^') {
        l->i++;
        if (c == '_' && !sub) sub = lin_script(l);
        else if (c == '^' && !sup) sup = lin_script(l);
        else break;
      }
      if (base->kind == MK_NARY) {
        if (sub) { mn_free(base->kid[0]); base->kid[0] = sub; base->pv[P_N_SUBHIDE] = 0; }
        if (sup) { mn_free(base->kid[1]); base->kid[1] = sup; base->pv[P_N_SUPHIDE] = 0; }
        continue;
      }
      s = mn_new(sub && sup ? MK_SSUBSUP : sub ? MK_SSUB : MK_SSUP);
      if (!s) { mn_free(base); mn_free(sub); mn_free(sup); l->oom = 1; return NULL; }
      mn_insert(s, -1, arg_of(base));
      if (sub) mn_insert(s, -1, sub);
      if (sup) mn_insert(s, -1, sup);
      base = s;
      continue;
    }
    if (c == 0x252C || c == 0x2534) {                 /* below, above */
      MNode *s = mn_new(c == 0x252C ? MK_LIMLOW : MK_LIMUPP);
      l->i++;
      if (!s) { mn_free(base); return NULL; }
      mn_insert(s, -1, arg_of(base));
      mn_insert(s, -1, lin_script(l));
      base = s;
      continue;
    }
    if (c > 0 && is_combining((u32)c)) {
      MNode *s = mn_new(MK_ACC);
      l->i++;
      if (!s) { mn_free(base); return NULL; }
      if (c != 0x302) s->ch[0] = (u32)c;
      mn_insert(s, -1, arg_of(base));
      base = s;
      continue;
    }
    if (c == '\'' || c == 0x2032) {                   /* primes: a superscript ′ */
      MNode *s = mn_new(MK_SSUP);
      u32 p[3];
      int k = 0;
      while ((lpeek(l) == '\'' || lpeek(l) == 0x2032) && k < 3) { p[k++] = 0x2032; l->i++; }
      if (k == 2) { p[0] = 0x2033; k = 1; } else if (k == 3) { p[0] = 0x2034; k = 1; }
      if (!s) { mn_free(base); return NULL; }
      mn_insert(s, -1, arg_of(base));
      mn_insert(s, -1, row_of(run_cp(p, k)));
      base = s;
      continue;
    }
    if (c >= 0) {
      /* Unicode superscript and subscript digits and signs */
      static const u32 sups[] = { 0x2070, 0xB9, 0xB2, 0xB3, 0x2074, 0x2075, 0x2076, 0x2077, 0x2078, 0x2079, 0x207A, 0x207B, 0x207D, 0x207E, 0x207F, 0x2071 };
      static const char supc[] = "0123456789+-()ni";
      int k, issub = c >= 0x2080 && c <= 0x208E;
      for (k = 0; k < 16; k++) if ((u32)c == sups[k]) break;
      if (k < 16 || issub) {
        u32 t[16];
        int n = 0;
        MNode *s = mn_new(issub ? MK_SSUB : MK_SSUP);
        while (n < 16 && (c = lpeek(l)) >= 0) {
          if (issub && c >= 0x2080 && c <= 0x208E) t[n++] = (u32)"0123456789+-=()"[c - 0x2080];
          else if (!issub) { for (k = 0; k < 16 && (u32)c != sups[k]; k++) ; if (k == 16) break; t[n++] = (u32)supc[k]; }
          else break;
          l->i++;
        }
        if (!s) { mn_free(base); return NULL; }
        mn_insert(s, -1, arg_of(base));
        mn_insert(s, -1, row_of(run_cp(t, n)));
        base = s;
        continue;
      }
    }
    return base;
  }
}
/* one operand with its scripts: a number, a letter, a function, a bracketed group, a structure */
static MNode *lin_operand(Lin *l)
{
  int c = lpeek(l), k;
  MNode *n = NULL;
  if (c < 0) return NULL;
  if (++l->depth > 50) { l->depth--; l->i = l->n; return NULL; }
  if (is_digit((u32)c)) {
    int a = l->i;
    while (l->i < l->n && is_digit(l->c[l->i])) l->i++;
    n = run_cp(l->c + a, l->i - a);
  } else if (is_letter((u32)c)) {
    /* a function's name: its argument after it */
    for (k = 0; funcs[k]; k++) {
      int m = (int)strlen(funcs[k]), j;
      for (j = 0; j < m && l->i + j < l->n && l->c[l->i + j] == (u32)funcs[k][j]; j++) ;
      if (j == m && !(l->i + m < l->n && is_letter(l->c[l->i + m]))) break;
    }
    if (funcs[k]) {
      MNode *f = mn_new(MK_FUNC), *name = mn_run(funcs[k], strlen(funcs[k])), *fn;
      l->i += (int)strlen(funcs[k]);
      if (name) name->pv[P_R_STY] = MSTY_P + 1;
      fn = name;
      if (lpeek(l) == '_' || lpeek(l) == 0x252C) {        /* lim_(x→0): the limit under the name */
        MNode *ll = mn_new(MK_LIMLOW);
        l->i++;
        if (ll) { mn_insert(ll, -1, row_of(name)); mn_insert(ll, -1, lin_script(l)); }
        else mn_free(name);
        fn = ll;
      } else if (lpeek(l) == '^' || lpeek(l) == 0x2534) {
        MNode *ss = mn_new(MK_SSUP);                      /* sin^2 x */
        l->i++;
        if (ss) { mn_insert(ss, -1, row_of(name)); mn_insert(ss, -1, lin_script(l)); }
        else mn_free(name);
        fn = ss;
      }
      if (lpeek(l) == 0x2061) l->i++;
      while (lpeek(l) == ' ') l->i++;
      if (!f) { mn_free(fn); l->depth--; return NULL; }
      mn_insert(f, -1, row_of(fn));
      if (lpeek(l) == '(') {                              /* sin(x): the brackets stay */
        MNode *r = mn_new(MK_ROW), *o = lin_operand(l);
        if (r && o) mn_insert(r, -1, o); else mn_free(o);
        mn_insert(f, -1, r);
      } else mn_insert(f, -1, lin_group(l, st_none));
      l->depth--;
      return f;
    }
    l->i++;
    { u32 cc = (u32)c; n = run_cp(&cc, 1); }
  } else if (c == '"') {
    int a = ++l->i;
    while (l->i < l->n && l->c[l->i] != '"') l->i++;
    n = run_cp(l->c + a, l->i - a);
    if (n) n->pv[P_R_NOR] = MB_ON;
    if (l->i < l->n) l->i++;
  } else if (is_open((u32)c) || c == '|' || c == 0x2016) {
    u32 cl = c == '|' || c == 0x2016 ? (u32)c : close_of((u32)c);
    u32 stop[4];
    MNode *d = mn_new(MK_DELIM), *e;
    int invisible = c == 0x3016, pick = c == 0x251C;
    l->i++;
    stop[0] = cl; stop[1] = 0x2502; stop[2] = 0x2223; stop[3] = 0;
    if (!d) { l->oom = 1; l->depth--; return NULL; }
    for (;;) {
      e = lin_row(l, stop);
      if (e) mn_insert(d, -1, e);
      if (lpeek(l) == 0x2502 || lpeek(l) == 0x2223) { l->i++; continue; }
      break;
    }
    if (lpeek(l) == (int)cl) l->i++;
    if (invisible) { d->ch[0] = 0; d->ch[2] = 0; }
    else if (pick) { d->ch[0] = 0; d->ch[2] = 0; }
    else if (c != '(') { d->ch[0] = (u32)c; d->ch[2] = cl; }
    if (d->nkid > 1) d->ch[1] = MCH_UNSET;
    n = d;
  } else if (c == 0x221A || c == 0x221B || c == 0x221C) {
    MNode *r = mn_struct(MK_RAD, 0, 0);
    l->i++;
    if (!r) { l->depth--; return NULL; }
    if (c != 0x221A) { u32 dg = c == 0x221B ? '3' : '4'; mn_insert(r->kid[0], -1, run_cp(&dg, 1)); }
    if (lpeek(l) == '(') {
      static const u32 st[] = { '&', ')', 0 };
      MNode *a;
      l->i++;
      a = lin_row(l, st);
      if (lpeek(l) == '&') {
        l->i++;
        mn_free(r->kid[0]); r->kid[0] = a;
        a = lin_row(l, st_paren);
      }
      if (lpeek(l) == ')') l->i++;
      mn_free(r->kid[1]); r->kid[1] = a ? a : mn_new(MK_ROW);
    } else { MNode *o = lin_operand(l); mn_free(r->kid[1]); r->kid[1] = o ? arg_of(o) : mn_new(MK_ROW); }
    if (!r->kid[0]->nkid) r->pv[P_RAD_DEGHIDE] = MB_ON;
    n = r;
  } else if (is_nary((u32)c)) {
    MNode *s = mn_struct(MK_NARY, 0, 0);
    l->i++;
    if (!s) { l->depth--; return NULL; }
    if (c != 0x222B) s->ch[0] = (u32)c;
    s->pv[P_N_SUBHIDE] = s->pv[P_N_SUPHIDE] = MB_ON;
    s = lin_scripts(l, s);
    if (s && (lpeek(l) == 0x2592 || lpeek(l) == ' ')) {
      while (lpeek(l) == ' ') l->i++;
      if (lpeek(l) == 0x2592) l->i++;
    }
    if (s && s->kind == MK_NARY) { mn_free(s->kid[2]); s->kid[2] = lin_group(l, st_none); }
    l->depth--;
    return s;
  } else if (c == 0x25A0 || c == 0x2588) {            /* ■ matrix, █ equation array */
    static const u32 st[] = { '&', '@', ')', 0 }, st2[] = { '@', ')', 0 };
    MNode *m = mn_new(c == 0x25A0 ? MK_MATRIX : MK_EQARR);
    int cols = 0, col = 0;
    l->i++;
    if (!m) { l->depth--; return NULL; }
    if (lpeek(l) == '(') {
      l->i++;
      if (c == 0x25A0) m->rows = 1;
      for (;;) {
        MNode *e = lin_row(l, c == 0x25A0 ? st : st2);
        int t = lpeek(l);
        if (e) mn_insert(m, -1, e);
        col++;
        if (t == '&') { l->i++; continue; }
        if (c == 0x25A0) {
          if (m->rows == 1) cols = col;
          while (col < cols) { mn_insert(m, -1, mn_new(MK_ROW)); col++; }
          while (col > cols) { mn_free(mn_remove(m, m->nkid - 1)); col--; }
        }
        if (t == '@') { l->i++; col = 0; if (c == 0x25A0) m->rows++; continue; }
        if (t == ')') l->i++;
        break;
      }
    }
    if (c == 0x25A0) m->cols = (u16)(cols ? cols : 1);
    if (!m->nkid) { mn_insert(m, -1, mn_new(MK_ROW)); if (c == 0x25A0) m->rows = m->cols = 1; }
    n = m;
  } else if (c == 0x25A1 || c == 0x25AD || c == 0x27E1 || c == 0xAF || c == 0x2581 || c == 0x23DE || c == 0x23DF || c == 0x23DC || c == 0x23DD) {
    int kind = c == 0x25A1 ? MK_BOX : c == 0x25AD ? MK_BORDERBOX : c == 0x27E1 ? MK_PHANT : (c == 0xAF || c == 0x2581) ? MK_BAR : MK_GROUPCHR;
    MNode *s = mn_new(kind), *a;
    l->i++;
    if (!s) { l->depth--; return NULL; }
    a = lin_parens(l, st_paren);
    if (lpeek(l) == ')') l->i++;
    mn_insert(s, -1, a ? a : mn_new(MK_ROW));
    if (kind == MK_PHANT) s->pv[P_PH_SHOW] = MB_OFF;
    if (kind == MK_BAR) s->pv[P_BAR_POS] = c == 0xAF ? MPOS_TOP + 1 : 0;
    if (kind == MK_GROUPCHR) {
      if (c != 0x23DF) s->ch[0] = (u32)c;
      if (c == 0x23DE || c == 0x23DC) { s->pv[P_G_POS] = MPOS_TOP + 1; s->pv[P_G_VERTJC] = MPOS_BOT + 1; }
    }
    n = s;
  } else if (c == '_' || c == '^') {
    /* pre-scripts: _a^b▒x */
    MNode *s = mn_struct(MK_SPRE, 0, 0);
    if (!s) { l->depth--; return NULL; }
    while ((c = lpeek(l)) == '_' || c == '^') {
      MNode *a;
      l->i++;
      a = lin_script(l);
      mn_free(s->kid[c == '_' ? 0 : 1]); s->kid[c == '_' ? 0 : 1] = a ? a : mn_new(MK_ROW);
    }
    if (lpeek(l) == 0x2592) l->i++;
    { MNode *o = lin_operand(l); mn_free(s->kid[2]); s->kid[2] = o ? arg_of(o) : mn_new(MK_ROW); }
    l->depth--;
    return s;
  } else if (is_combining((u32)c)) {
    /* \hat(x): the accent before its base */
    MNode *s = mn_new(MK_ACC), *o;
    l->i++;
    if (!s) { l->depth--; return NULL; }
    if (c != 0x302) s->ch[0] = (u32)c;
    while (lpeek(l) == ' ') l->i++;
    o = lin_operand(l);
    mn_insert(s, -1, o ? arg_of(o) : mn_new(MK_ROW));
    n = s;
  } else {
    u32 cc = (u32)c;
    l->i++;
    if (cc == '-') cc = 0x2212;
    else if (cc == '*') cc = 0x2217;
    n = run_cp(&cc, 1);
    if (is_operator(cc) || c == '&' || c == '@') { l->depth--; return n; }   /* (an operator takes no scripts) */
  }
  if (!n) { l->oom = 1; l->depth--; return NULL; }
  if (!l->noscr) n = lin_scripts(l, n);
  l->depth--;
  return n;
}
/* runs side by side with nothing set merged into one */
static void merge_runs(MNode *row)
{
  int i;
  for (i = 0; i + 1 < row->nkid; ) {
    MNode *a = row->kid[i], *b = row->kid[i + 1];
    if (a->kind == MK_RUN && b->kind == MK_RUN && !memcmp(a->pv, b->pv, sizeof a->pv) && !a->rpr && !b->rpr && !a->xpr && !b->xpr && !a->ctrl && !b->ctrl) {
      size_t la = strlen(a->text), lb = strlen(b->text);
      char *t = (char *)malloc(la + lb + 1);
      if (!t) return;
      memcpy(t, a->text, la); memcpy(t + la, b->text, lb + 1);
      free(a->text); a->text = t;
      mn_free(mn_remove(row, i + 1));
      continue;
    }
    i++;
  }
}
static void merge_all(MNode *n)
{
  u16 i;
  for (i = 0; i < n->nkid; i++) merge_all(n->kid[i]);
  if (n->kind == MK_ROW) merge_runs(n);
}
/* a row up to a stop character (not taken) or the end; fractions made as they come */
static MNode *lin_row(Lin *l, const u32 *stop)
{
  MNode *r = mn_new(MK_ROW);
  int bound = 0, noscr = l->noscr;    /* bound: where the operand a fraction's bar would take starts */
  if (!r) { l->oom = 1; return NULL; }
  l->noscr = 0;
  if (++l->depth > 50) { l->depth--; l->i = l->n; l->noscr = noscr; return r; }
  for (;;) {
    int c = lpeek(l), k;
    MNode *n;
    if (c < 0) break;
    for (k = 0; stop[k]; k++) if ((u32)c == stop[k]) break;
    if (stop[k]) break;
    if (c == ' ') { l->i++; bound = r->nkid; continue; }
    if (is_close((u32)c) && !stop[0]) { u32 cc = (u32)c; l->i++; mn_insert(r, -1, run_cp(&cc, 1)); bound = r->nkid; continue; }
    if (is_close((u32)c)) break;
    if (c == '/' || c == 0x2298 || c == 0x2215 || c == 0xA6 || c == 0x2044) {
      /* a fraction: the operand before (since the last operator or space), the one after */
      MNode *f = mn_struct(MK_FRAC, 0, 0), *num, *den;
      int j;
      l->i++;
      if (!f) { l->oom = 1; break; }
      num = mn_new(MK_ROW);
      for (j = bound; j < r->nkid; ) mn_insert(num, -1, mn_remove(r, j));
      if (num && num->nkid == 1 && num->kid[0]->kind == MK_DELIM) { MNode *d = mn_remove(num, 0); mn_free(num); num = arg_of(d); }
      while (lpeek(l) == ' ') l->i++;
      den = lin_group(l, stop);
      mn_free(f->kid[0]); f->kid[0] = num ? num : mn_new(MK_ROW);
      mn_free(f->kid[1]); f->kid[1] = den ? den : mn_new(MK_ROW);
      if (c == 0x2298) f->pv[P_F_TYPE] = MF_LIN + 1;
      else if (c == 0x2215 || c == 0x2044) f->pv[P_F_TYPE] = MF_SKW + 1;
      else if (c == 0xA6) f->pv[P_F_TYPE] = MF_NOBAR + 1;
      mn_insert(r, -1, f);
      continue;
    }
    n = lin_operand(l);
    if (!n) { if (l->oom) break; continue; }
    if (!mn_insert(r, -1, n)) { l->oom = 1; break; }
    if (n->kind == MK_RUN && n->text && n->text[0]) {
      int used;
      u32 cc = utf8_get(n->text, strlen(n->text), &used);
      if (is_operator(cc) && !n->pv[P_R_NOR]) bound = r->nkid;
    }
  }
  l->depth--;
  l->noscr = noscr;
  return r;
}
MathEq *math_from_linear(const char *s, int display)
{
  Lin l;
  size_t i = 0, n = strlen(s);
  MathEq *e;
  MNode *row;
  memset(&l, 0, sizeof l);
  if (!(l.c = (u32 *)malloc((n + 1) * sizeof(u32)))) return NULL;
  while (i < n) {
    int used;
    u32 c = utf8_get(s + i, n - i, &used);
    if (c == '\\' && i + 1 < n && ((s[i + 1] | 32) >= 'a' && (s[i + 1] | 32) <= 'z')) {
      size_t a = i + 1, b = a;
      u32 k;
      while (b < n && ((s[b] | 32) >= 'a' && (s[b] | 32) <= 'z')) b++;
      if ((k = math_keyword(s + a, b - a))) {
        l.c[l.n++] = k;
        i = b;
        if (i < n && s[i] == ' ' && !is_combining(k)) i++;     /* the space ending a control word */
        continue;
      }
    }
    if (c == '\t' || c == '\n' || c == '\r' || c == 0xA0) c = ' ';
    l.c[l.n++] = c;
    i += (size_t)used;
  }
  row = lin_row(&l, st_none);
  free(l.c);
  if (!row) return NULL;
  merge_all(row);
  if (!(e = (MathEq *)calloc(1, sizeof(MathEq))) || !(e->line = (MNode **)malloc(sizeof(MNode *)))) { free(e); mn_free(row); return NULL; }
  e->line[0] = row; e->nline = 1; e->display = (u8)display;
  return e;
}

/* ------------------------------------------------------------------ */
/* linear format out */
static void lo_row(Buf *o, const MNode *r);
static void put_cp(Buf *o, u32 c) { char u[4]; buf_add(o, u, (size_t)utf8_put(u, c)); }
/* a row as an operand: bare if it is one plain operand, else in parentheses */
static void lo_arg(Buf *o, const MNode *r)
{
  int bare = 0;
  if (r->kind == MK_ROW && r->nkid == 1) {
    const MNode *k = r->kid[0];
    if (k->kind == MK_RUN && !k->pv[P_R_NOR] && k->text) {
      size_t i, n = strlen(k->text);
      int digits = 1, used;
      for (i = 0; i < n; i++) if (!is_digit((u8)k->text[i])) digits = 0;
      bare = digits || (utf8_get(k->text, n, &used) && (size_t)used == n && !is_operator(utf8_get(k->text, n, &used)));
    } else if (k->kind != MK_RUN && k->kind != MK_DELIM && k->kind != MK_FUNC && k->kind != MK_FRAC && k->kind != MK_RAW) bare = 1;
  } else if (r->kind == MK_ROW && r->nkid == 0) { buf_adds(o, "()"); return; }
  if (!bare) buf_addc(o, '(');
  lo_row(o, r);
  if (!bare) buf_addc(o, ')');
}
static void lo_node(Buf *o, const MNode *n)
{
  u16 i;
  switch (n->kind) {
  case MK_ROW: lo_row(o, n); break;
  case MK_RAW: if (n->nkid) lo_row(o, n); break;
  case MK_RUN:
    if (n->pv[P_R_NOR]) { buf_addc(o, '"'); buf_adds(o, n->text); buf_addc(o, '"'); }
    else buf_adds(o, n->text ? n->text : "");
    break;
  case MK_FRAC: {
    int t = n->pv[P_F_TYPE] ? n->pv[P_F_TYPE] - 1 : MF_BAR;
    lo_arg(o, n->kid[0]);
    put_cp(o, t == MF_LIN ? 0x2298 : t == MF_SKW ? 0x2215 : t == MF_NOBAR ? 0xA6 : '/');
    lo_arg(o, n->kid[1]);
    break; }
  case MK_SSUB: case MK_SSUP: case MK_SSUBSUP:
    lo_arg(o, n->kid[0]);
    if (n->kind != MK_SSUP) { buf_addc(o, '_'); lo_arg(o, n->kid[1]); }
    if (n->kind != MK_SSUB) { buf_addc(o, '^'); lo_arg(o, n->kid[n->kind == MK_SSUP ? 1 : 2]); }
    break;
  case MK_SPRE:
    buf_addc(o, '_'); lo_arg(o, n->kid[0]); buf_addc(o, '^'); lo_arg(o, n->kid[1]);
    put_cp(o, 0x2592); lo_arg(o, n->kid[2]);
    break;
  case MK_RAD:
    put_cp(o, 0x221A); buf_addc(o, '(');
    if (n->pv[P_RAD_DEGHIDE] != MB_ON && n->kid[0]->nkid) { lo_row(o, n->kid[0]); buf_addc(o, '&'); }
    lo_row(o, n->kid[1]); buf_addc(o, ')');
    break;
  case MK_NARY:
    put_cp(o, n->ch[0] == MCH_UNSET ? 0x222B : n->ch[0]);
    if (n->pv[P_N_SUBHIDE] != MB_ON) { buf_addc(o, '_'); lo_arg(o, n->kid[0]); }
    if (n->pv[P_N_SUPHIDE] != MB_ON) { buf_addc(o, '^'); lo_arg(o, n->kid[1]); }
    put_cp(o, 0x2592);
    lo_arg(o, n->kid[2]);
    break;
  case MK_DELIM: {
    u32 b = n->ch[0] == MCH_UNSET ? '(' : n->ch[0], e = n->ch[2] == MCH_UNSET ? ')' : n->ch[2];
    put_cp(o, b ? b : 0x3016);
    for (i = 0; i < n->nkid; i++) { if (i) put_cp(o, n->ch[1] == MCH_UNSET || !n->ch[1] ? 0x2502 : n->ch[1]); lo_row(o, n->kid[i]); }
    put_cp(o, e ? e : 0x3017);
    break; }
  case MK_FUNC:
    lo_row(o, n->kid[0]); put_cp(o, 0x2061); lo_arg(o, n->kid[1]);
    break;
  case MK_ACC:
    lo_arg(o, n->kid[0]); put_cp(o, n->ch[0] == MCH_UNSET ? 0x302 : n->ch[0]);
    break;
  case MK_BAR: put_cp(o, n->pv[P_BAR_POS] == MPOS_TOP + 1 ? 0xAF : 0x2581); buf_addc(o, '('); lo_row(o, n->kid[0]); buf_addc(o, ')'); break;
  case MK_GROUPCHR: put_cp(o, n->ch[0] == MCH_UNSET ? 0x23DF : n->ch[0]); buf_addc(o, '('); lo_row(o, n->kid[0]); buf_addc(o, ')'); break;
  case MK_LIMLOW: case MK_LIMUPP:
    lo_arg(o, n->kid[0]); put_cp(o, n->kind == MK_LIMLOW ? 0x252C : 0x2534); lo_arg(o, n->kid[1]);
    break;
  case MK_BOX: case MK_BORDERBOX: case MK_PHANT:
    put_cp(o, n->kind == MK_BOX ? 0x25A1 : n->kind == MK_BORDERBOX ? 0x25AD : 0x27E1);
    buf_addc(o, '('); lo_row(o, n->kid[0]); buf_addc(o, ')');
    break;
  case MK_MATRIX: case MK_EQARR:
    put_cp(o, n->kind == MK_MATRIX ? 0x25A0 : 0x2588); buf_addc(o, '(');
    for (i = 0; i < n->nkid; i++) {
      if (i) buf_addc(o, n->kind == MK_MATRIX && n->cols && i % n->cols ? '&' : '@');
      lo_row(o, n->kid[i]);
    }
    buf_addc(o, ')');
    break;
  }
}
static void lo_row(Buf *o, const MNode *r)
{
  u16 i;
  for (i = 0; i < r->nkid; i++) {
    /* a space keeps an operand from running into the fraction or structure after it */
    if (i && r->kid[i]->kind == MK_FRAC && o->len && o->data[o->len - 1] != ' ') buf_addc(o, ' ');
    lo_node(o, r->kid[i]);
    if (i + 1 < r->nkid && r->kid[i]->kind == MK_FRAC) buf_addc(o, ' ');
  }
}
void math_to_linear(const MathEq *e, Buf *o)
{
  u16 i;
  for (i = 0; i < e->nline; i++) { if (i) buf_addc(o, '\n'); lo_row(o, e->line[i]); }
}
