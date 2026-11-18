/*
 * docbin.c - Word 97-2003 documents (.doc; docs/wordcraft-gap.md WP28d), read. This file belongs to
 * WP28d: only that package edits it.
 *
 * A .doc is an OLE compound file (core/cfb.c) with a WordDocument stream (the FIB, the text and the
 * formatting pages) and a table stream (0Table or 1Table: the piece table, style sheet, font table and
 * the PLCs saying where everything is), as [MS-DOC] describes them. Read here: the text through the
 * piece table (8-bit and 16-bit pieces), paragraphs and their formatting (PAPX FKPs: style, alignment,
 * indents, spacing, line spacing, tabs, keeping, breaks, borders and shading, outline level, lists),
 * character formatting (CHPX FKPs and the pieces' own sprms: bold, italic, underlines, strike, caps,
 * small caps, hidden, super/subscript, raised and lowered, font, size, colour, highlight, shading,
 * spacing, scale, character styles), the style sheet (paragraph and character styles with their bases),
 * the font table, lists (LSTF/LVL/LFO), tables (cell and row marks, TDefTable widths, merged cells,
 * borders, shading, heading rows, row heights; nested tables flattened into their cell), sections (page
 * size and margins, columns, how each starts, title page, vertical alignment), headers and footers,
 * footnotes and endnotes, fields (their results kept as text; HYPERLINK as a link), page and column
 * breaks, special characters. Not read: pictures and drawn objects (LOST_PICTURES), comments
 * (LOST_COMMENTS), tracked changes (accepted: deletions left out, LOST_TRACKED), Word 6/95 files and
 * encrypted files (refused with a message).
 *
 * Files are hostile input: every offset and count is checked against its stream; a damaged structure
 * is left out rather than read past its end. WP7's encrypted .docx files are compound files too: they
 * are decrypted before the formats are tried (core/protect.c), so they never reach this reader.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "docx.h"
#include "cfb.h"
#include "fmtio.h"
#include "codepage.h"
#include "field.h"

/* an OLE compound file: D0 CF 11 E0 A1 B1 1A E1 */
static int sniff_cfb(const u8 *b, size_t n) { return n >= 8 && b[0] == 0xD0 && b[1] == 0xCF && b[2] == 0x11 && b[3] == 0xE0; }

/* ---- reading little-endian numbers safely (0 past the end) */
static u32 g8(const u8 *b, size_t n, size_t o) { return o < n ? b[o] : 0; }
static u32 g16(const u8 *b, size_t n, size_t o) { return o + 2 <= n && o + 2 > o ? (u32)b[o] | (u32)b[o + 1] << 8 : 0; }
static u32 g32(const u8 *b, size_t n, size_t o) { return o + 4 <= n && o + 4 > o ? (u32)b[o] | (u32)b[o + 1] << 8 | (u32)b[o + 2] << 16 | (u32)b[o + 3] << 24 : 0; }
static i32 s16(u32 v) { return v & 0x8000 ? (i32)v - 0x10000 : (i32)v; }

/* Word's 16 colours (ico) */
static const u32 ico_tab[17] = { COLOR_AUTO, 0x000000, 0x0000FF, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0xFFFF00, 0xFFFFFF,
                              0x000080, 0x008080, 0x008000, 0x800080, 0x800000, 0x808000, 0x808080, 0xC0C0C0 };
static u32 ico_rgb(u32 ico) { return ico < 17 ? ico_tab[ico] : COLOR_AUTO; }
/* a COLORREF: red, green, blue, then 0xFF for "automatic" */
static u32 cv_rgb(u32 cv) { return (cv >> 24) == 0xFF ? COLOR_AUTO : (cv & 0xFF) << 16 | (cv & 0xFF00) | (cv >> 16 & 0xFF); }

/* ---- the reader's state */
typedef struct { u32 a, b; u32 off; u16 n; } Prop;          /* an FC range and its CHPX/PAPX (offset into WordDocument, bytes) */
typedef struct {
  u16 sti, stk, base, next;
  char name[48];
  u32 papx, npapx;          /* (paragraph styles) istd then grpprl, in the table stream */
  u32 chpx, nchpx;          /* grpprl */
  int k;                    /* its Doc style, -1 none */
  u8 done, busy;
  ParaFmt pf; CharFmt cf;   /* what it comes to (the paragraph styles: their text's look too) */
} DStyle;
typedef struct {
  u32 lsid;
  u32 off;                  /* its LVLs in the table stream */
  u8 simple;
  int list;                 /* the Doc list made from it, 0 not yet */
} DLst;
typedef struct R {
  Doc *d;
  unsigned *lost;
  int oom;
  const u8 *w; size_t wn;   /* WordDocument */
  const u8 *t; size_t tn;   /* the table stream */
  u32 ccp[8];               /* main, footnotes, headers, macros, comments, endnotes, text boxes, header text boxes */
  u32 *pcp; u32 *pfc; u16 *pprm; int npc;    /* the pieces: CPs (npc + 1), FCs, Prms */
  u32 *prc; int nprc;       /* the Prc grpprls' offsets in the table stream (cb at off - 2) */
  Prop *chp; int nchp;      /* character runs, by FC */
  Prop *pap; int npap;      /* paragraph runs, by FC (n: istd + grpprl bytes) */
  DStyle *st; int nst;
  int ftc[512]; int nftc;   /* fonts: Doc font index of each */
  DLst *lst; int nlst;
  u32 *lfo; int nlfo;       /* LFO number - 1: its lsid */
  int *lfolist;             /* and the Doc list made for it, 0 none */
  u32 *sed; int nsed;       /* section boundaries (nsed + 1 CPs) and each Sepx's FC */
  u32 *sepx;
  u32 *fref, *ftxt; int nfn;   /* footnotes: references' CPs, text CPs (nfn + 1) */
  u32 *eref, *etxt; int nen;
  u32 *hdd; int nhdd;       /* headers and footers (nhdd + 1 CPs) */
  int depth;                /* stories read inside stories (notes in notes are left out) */
} R;

static void *rmalloc(R *r, size_t n) { void *p = malloc(n ? n : 1); if (!p) r->oom = 1; return p; }

/* ---- sprms: an operand's size */
static u32 sprm_size(const u8 *g, u32 n, u32 i, u32 sprm)
{
  switch (sprm >> 13) {
  case 0: case 1: return 1;
  case 2: case 4: case 5: return 2;
  case 3: return 4;
  case 7: return 3;
  default:
    if (sprm == 0xD608 || sprm == 0xD606) { u32 cb = g16(g, n, i); return cb ? cb + 1 : 2; }   /* TDefTable: a 16-bit size */
    if (sprm == 0xC615 && g8(g, n, i) == 255) {      /* PChgTabs' long form */
      u32 del = g8(g, n, i + 1), add = g8(g, n, i + 2 + del * 4);
      return 1 + 1 + del * 4 + 1 + add * 3;
    }
    return 1 + g8(g, n, i);
  }
}
/* calls each sprm of a grpprl: f(r, sprm, operand bytes, their count, arg) */
typedef void (*SprmFn)(R *r, u32 sprm, const u8 *op, u32 opn, void *arg);
static void sprms(R *r, const u8 *g, u32 n, SprmFn f, void *arg)
{
  u32 i = 0;
  while (i + 2 <= n) {
    u32 sprm = g16(g, n, i), sz = sprm_size(g, n, i + 2, sprm);
    if (i + 2 + sz > n) break;
    f(r, sprm, g + i + 2, sz, arg);
    i += 2 + sz;
  }
}

/* ---- character formatting */
typedef struct { CharFmt *cf; const CharFmt *base; int skipstyle; } ChpArg;
/* a toggle's operand: 0 off, 1 on, 0x80 the style's, 0x81 the opposite of the style's */
static int toggle(u32 v, int was) { return v == 0x80 ? was : v == 0x81 ? !was : v != 0; }
static void cflag(CharFmt *cf, const CharFmt *base, u8 bit, u32 v)
{
  if (toggle(v, (base->flags & bit) != 0)) cf->flags |= bit; else cf->flags &= (u8)~bit;
}
static void cfx2(CharFmt *cf, const CharFmt *base, u16 bit, u32 v)
{
  if (toggle(v, (base->fx2 & bit) != 0)) cf->fx2 |= bit; else cf->fx2 &= (u16)~bit;
}
static void cfx(CharFmt *cf, const CharFmt *base, u8 bit, u32 v)
{
  if (toggle(v, (base->fx & bit) != 0)) cf->fx |= bit; else cf->fx &= (u8)~bit;
}
static int font_of(R *r, u32 ftc) { return ftc < (u32)r->nftc ? r->ftc[ftc] : -1; }
static void apply_cstyle(R *r, int istd, CharFmt *cf, int depth);
static void chp_sprm(R *r, u32 sprm, const u8 *op, u32 n, void *arg)
{
  ChpArg *a = (ChpArg *)arg;
  CharFmt *cf = a->cf;
  const CharFmt *b = a->base;
  u32 v = g8(op, n, 0), v16 = g16(op, n, 0);
  int k;
  switch (sprm) {
  case 0x0835: cflag(cf, b, CF_BOLD, v); break;
  case 0x0836: cflag(cf, b, CF_ITALIC, v); break;
  case 0x0837: cflag(cf, b, CF_STRIKE, v); break;
  case 0x0838: cfx2(cf, b, FX2_OUTLINE, v); break;
  case 0x0839: cfx2(cf, b, FX2_SHADOW, v); break;
  case 0x083A: cfx(cf, b, FX_SMALLCAPS, v); break;
  case 0x083B: cflag(cf, b, CF_CAPS, v); break;
  case 0x083C: cfx2(cf, b, FX2_HIDDEN, v); break;
  case 0x0858: cfx2(cf, b, FX2_EMBOSS, v); break;
  case 0x0854: cfx2(cf, b, FX2_ENGRAVE, v); break;
  case 0x2A53: cfx(cf, b, FX_DSTRIKE, v); break;
  case 0x0875: cf->noproof = (u8)toggle(v, b->noproof); break;
  case 0x2A3E:                                         /* underline */
    if (!v) { cf->flags &= (u8)~CF_UNDERLINE; cf->ul = UL_SINGLE; break; }
    cf->flags |= CF_UNDERLINE;
    cf->ul = v == 2 ? UL_WORDS : v == 3 ? UL_DOUBLE : v == 4 || v == 20 || v == 23 ? UL_DOTTED : v == 6 ? UL_THICK :
             v == 7 || v == 9 || v == 10 || v == 39 || v == 55 ? UL_DASHED : v == 11 || v == 43 || v == 27 ? UL_WAVE : UL_SINGLE;
    break;
  case 0x2A42: cf->color = ico_rgb(v); break;
  case 0x6870: cf->color = cv_rgb(g32(op, n, 0)); break;
  case 0x4A43: if (v16 >= 2 && v16 <= 3276) cf->size = (u16)v16; break;
  case 0x2A48: cf->flags &= (u8)~(CF_SUPER | CF_SUB); if (v == 1) cf->flags |= CF_SUPER; else if (v == 2) cf->flags |= CF_SUB; break;
  case 0x4845: cf->position = (i16)s16(v16); break;
  case 0x8840: cf->spacing = (i16)s16(v16); break;
  case 0x4852: cf->scale = (u16)(v16 == 100 ? 0 : v16 > 600 ? 600 : v16); break;
  case 0x4A4F: case 0x4A3D: if ((k = font_of(r, v16)) >= 0) cf->font = (u8)k; break;
  case 0x2A0C: cf->hl = v && v < 17 ? HL_ON | ico_rgb(v) : 0; break;
  case 0x4866:                                         /* sprmCShd80: icoFore, icoBack, ipat */
    k = (int)(v16 >> 5 & 31);
    cf->shade = k && k < 17 ? HL_ON | ico_rgb((u32)k) : 0;
    break;
  case 0xCA71:                                         /* sprmCShd: cvFore, cvBack, ipat */
    { u32 back = cv_rgb(g32(op, n, 5)); cf->shade = back == COLOR_AUTO ? 0 : HL_ON | back; }
    break;
  case 0x4A30:                                         /* the character style */
    if (!a->skipstyle) apply_cstyle(r, (int)v16, cf, 0);
    break;
  default: break;
  }
}
/* the paragraph style's text look, then a character style's sprms (its bases' first) */
static void resolve_style(R *r, int i);
static int style_k(R *r, int i);
static void apply_cstyle(R *r, int istd, CharFmt *cf, int depth)
{
  DStyle *s;
  ChpArg a;
  CharFmt base;
  if (istd < 0 || istd >= r->nst || depth > 10) return;
  s = &r->st[istd];
  if (s->stk != 2) return;
  if (s->base != 0xFFF && s->base != istd) apply_cstyle(r, s->base, cf, depth + 1);
  { int k = style_k(r, istd); if (k > 0) cf->cstyle = (u8)k; }
  base = *cf;
  a.cf = cf; a.base = &base; a.skipstyle = 1;
  if (s->nchpx) sprms(r, r->t + s->chpx, s->nchpx, chp_sprm, &a);
}

/* ---- paragraph formatting */
typedef struct {
  ParaFmt *pf;
  u8 intable, ttp, innercell, innerttp, ilvl, hasindent, hasilfo;
  u16 ilfo;
  int itap;
  const u8 *tdef; u32 ntdef;                /* sprmTDefTable */
  const u8 *shd; u32 nshd; int shd80;       /* sprmTDefTableShd / Shd80 */
  const u8 *tbrc; u32 ntbrc;                /* sprmTTableBorders80 */
  i32 gap, rowh, tleft; u8 header, cantsplit, jc, hasleft;
  i32 padl, padr; u8 haspad;                /* sprmTCellPaddingDefault: the cells' left and right margins */
} PapArg;
static void brc80(const u8 *op, u32 n, u32 o, u8 *style, u8 *width, u32 *color)
{
  u32 w = g8(op, n, o), t = g8(op, n, o + 1), ico = g8(op, n, o + 2);
  if (!t || t == 0xFF) { *style = PB_NONE; return; }
  *style = (u8)(t == 3 ? PB_DOUBLE : t == 2 ? PB_THICK : t == 6 ? PB_DOTTED : t == 7 || t == 8 || t == 9 || t == 22 ? PB_DASHED : PB_SINGLE);
  *width = (u8)(w ? w : 4);
  *color = ico_rgb(ico);
}
static void pap_sprm(R *r, u32 sprm, const u8 *op, u32 n, void *arg)
{
  PapArg *a = (PapArg *)arg;
  ParaFmt *pf = a->pf;
  u32 v = g8(op, n, 0), v16 = g16(op, n, 0);
  int k;
  switch (sprm) {
  case 0x2403: case 0x2461:
    pf->align = (u8)(v == 1 ? AL_CENTER : v == 2 ? AL_RIGHT : v == 3 || v == 4 || v == 5 ? AL_JUSTIFY : AL_LEFT);
    break;
  case 0x2405: pf->keeplines = (u8)(v != 0); break;
  case 0x2406: pf->keepnext = (u8)(v != 0); break;
  case 0x2407: pf->pagebreak = (u8)(v != 0); break;
  case 0x2431: pf->nowidow = (u8)(v == 0); break;
  case 0x246D: pf->contextual = (u8)(v != 0); break;
  case 0x840F: case 0x845E: pf->left = s16(v16); a->hasindent = 1; break;
  case 0x840E: case 0x845D: pf->right = s16(v16); break;
  case 0x8411: case 0x8460: pf->first = s16(v16); a->hasindent = 1; break;
  case 0xA413: pf->before = (i32)v16; break;
  case 0xA414: pf->after = (i32)v16; break;
  case 0x6412: {
    i32 line = s16(v16), mult = s16(g16(op, n, 2));
    if (mult) { pf->linerule = LS_AUTO; pf->line = line > 0 ? line : 240; }
    else if (line >= 0) { pf->linerule = LS_ATLEAST; pf->line = line; }
    else { pf->linerule = LS_EXACT; pf->line = -line; }
    break;
  }
  case 0x2640: pf->outlvl = (u8)(v < 9 ? v + 1 : 0); break;
  case 0x2416: a->intable = (u8)(v != 0); if (v && a->itap < 1) a->itap = 1; break;
  case 0x2417: a->ttp = (u8)(v != 0); break;
  case 0x6649: a->itap = (int)g32(op, n, 0); break;
  case 0x664A: a->itap += (int)g32(op, n, 0); break;       /* sprmPDtap */
  case 0x244B: a->innercell = (u8)(v != 0); break;
  case 0x244C: a->innerttp = (u8)(v != 0); break;
  case 0x260A: a->ilvl = (u8)(v < 9 ? v : 0); break;
  case 0x460B: a->ilfo = (u16)v16; a->hasilfo = 1; break;
  case 0x442D: k = (int)(v16 >> 5 & 31); pf->shade = k && k < 17 ? HL_ON | ico_rgb((u32)k) : 0; break;
  case 0xC64D: { u32 back = cv_rgb(g32(op, n, 5)); pf->shade = back == COLOR_AUTO ? 0 : HL_ON | back; break; }
  case 0x6424: case 0x6425: case 0x6426: case 0x6427: case 0x6428: {    /* borders (Brc80): top, left, bottom, right, between */
    static const u8 edge[5] = { PE_TOP, PE_LEFT, PE_BOTTOM, PE_RIGHT, PE_BETWEEN };
    int e = edge[sprm - 0x6424];
    brc80(op, n, 0, &pf->bstyle[e], &pf->bwidth[e], &pf->bcolor[e]);
    pf->bspace[e] = (u8)(g8(op, n, 3) & 31);
    break;
  }
  case 0xC60D: case 0xC615: {                               /* tabs: deleted, then added */
    u32 o = 1, del, add, i;
    if (sprm == 0xC615 && v == 255) break;
    del = g8(op, n, o++);
    for (i = 0; i < del; i++) {
      i32 x = s16(g16(op, n, o + i * 2));
      int j, m;
      for (j = 0; j < 8 && pf->tabs[j]; j++) if (pf->tabs[j] > x - 20 && pf->tabs[j] < x + 20) break;
      if (j < 8 && pf->tabs[j]) {
        for (m = j; m < 7; m++) { pf->tabs[m] = pf->tabs[m + 1]; pf->tabkind[m] = pf->tabkind[m + 1]; pf->tablead[m] = pf->tablead[m + 1]; }
        pf->tabs[7] = 0;
      }
    }
    o += del * (sprm == 0xC615 ? 4 : 2);
    add = g8(op, n, o++);
    for (i = 0; i < add; i++) {
      i32 x = s16(g16(op, n, o + i * 2));
      u32 tbd = g8(op, n, o + add * 2 + i), jc = tbd & 7, tlc = tbd >> 3 & 7;
      int j, m;
      if (x <= 0 || jc == 4) continue;                      /* (bar tabs aren't stops) */
      for (j = 0; j < 8 && pf->tabs[j] && pf->tabs[j] < x; j++) ;
      if (j >= 8) continue;
      if (pf->tabs[j] == x) { pf->tabkind[j] = (u8)(jc > 3 ? 0 : jc); pf->tablead[j] = (u8)(tlc == 1 || tlc == 5 ? LEAD_DOT : tlc == 2 ? LEAD_HYPHEN : tlc == 3 || tlc == 4 ? LEAD_LINE : LEAD_NONE); continue; }
      for (m = 7; m > j; m--) { pf->tabs[m] = pf->tabs[m - 1]; pf->tabkind[m] = pf->tabkind[m - 1]; pf->tablead[m] = pf->tablead[m - 1]; }
      pf->tabs[j] = x;
      pf->tabkind[j] = (u8)(jc > 3 ? 0 : jc);
      pf->tablead[j] = (u8)(tlc == 1 || tlc == 5 ? LEAD_DOT : tlc == 2 ? LEAD_HYPHEN : tlc == 3 || tlc == 4 ? LEAD_LINE : LEAD_NONE);
    }
    break;
  }
  /* tables (in a row's last paragraph) */
  case 0xD608: a->tdef = op; a->ntdef = n; break;
  case 0xD612: a->shd = op; a->nshd = n; a->shd80 = 0; break;
  case 0xD609: if (!a->shd) { a->shd = op; a->nshd = n; a->shd80 = 1; } break;
  case 0xD605: a->tbrc = op; a->ntbrc = n; break;
  case 0x9602: a->gap = s16(v16); break;
  case 0xD634: case 0xD632: {
    u32 sides = g8(op, n, 3), fts = g8(op, n, 4);
    i32 w = (i32)g16(op, n, 5);
    if (fts != 3 || (sprm == 0xD632 && g8(op, n, 1) != 0)) break;      /* (twips, and the first cell's) */
    if (sides & 2) { a->padl = w; a->haspad |= 1; }
    if (sides & 8) { a->padr = w; a->haspad |= 2; }
    break;
  }
  case 0x9601: a->tleft = s16(v16); a->hasleft = 1; break;
  case 0x9407: a->rowh = s16(v16); break;
  case 0x3404: a->header = (u8)(v != 0); break;
  case 0x3403: case 0x3644: a->cantsplit = (u8)(v != 0); break;
  case 0x5400: case 0x548A: a->jc = (u8)(v16 == 1 ? AL_CENTER : v16 == 2 ? AL_RIGHT : AL_LEFT); break;
  default: break;
  }
}

/* ---- the style sheet */
static void resolve_style(R *r, int i)
{
  DStyle *s = &r->st[i];
  ChpArg ca;
  PapArg pa;
  CharFmt base;
  if (s->done || s->busy) return;
  s->busy = 1;
  if (s->base < r->nst && s->base != i && r->st[s->base].stk == s->stk && s->stk == 1) {
    resolve_style(r, s->base);
    s->pf = r->st[s->base].pf; s->cf = r->st[s->base].cf;
  } else { s->pf = r->d->defpf; s->cf = r->d->defcf; }
  if (s->stk == 1) {
    memset(&pa, 0, sizeof pa);
    pa.pf = &s->pf;
    if (s->npapx > 2) sprms(r, r->t + s->papx + 2, s->npapx - 2, pap_sprm, &pa);
    base = s->cf;
    ca.cf = &s->cf; ca.base = &base; ca.skipstyle = 1;
    if (s->nchpx) sprms(r, r->t + s->chpx, s->nchpx, chp_sprm, &ca);
  }
  s->busy = 0; s->done = 1;
}
/* a style's id from its name: "heading 1" becomes Heading1, as .docx files name them */
static void style_id(const char *name, char *id, size_t cap)
{
  size_t o = 0;
  int up = 1;
  for (; *name && o + 1 < cap; name++) {
    if (*name == ' ' || *name == '-' || *name == '_') { up = 1; continue; }
    id[o++] = up && *name >= 'a' && *name <= 'z' ? (char)(*name - 32) : *name;
    up = 0;
  }
  id[o] = 0;
}
static void read_styles(R *r, u32 fc, u32 lcb)
{
  u32 cbstshi, cstd, cbbase, o, end, i;
  Doc *d = r->d;
  int k;
  if (!lcb || fc >= r->tn || lcb > r->tn - fc) return;
  end = fc + lcb;
  cbstshi = g16(r->t, end, fc);
  cstd = g16(r->t, end, fc + 2);
  cbbase = g16(r->t, end, fc + 4);
  if (cbbase < 10) cbbase = 10;
  /* the default font (ftcAsci) under every style */
  if (cbstshi >= 18 && (k = font_of(r, g16(r->t, end, fc + 2 + 12))) >= 0) d->defcf.font = (u8)k;
  if (cstd > 4096) cstd = 4096;
  if (!(r->st = (DStyle *)calloc(cstd ? cstd : 1, sizeof(DStyle)))) { r->oom = 1; return; }
  r->nst = (int)cstd;
  o = fc + 2 + cbstshi;
  for (i = 0; i < cstd && o + 2 <= end; i++) {
    u32 cb = g16(r->t, end, o), s0 = o + 2, se = s0 + cb, x, cch, j, upx, nupx, u;
    DStyle *s = &r->st[i];
    s->k = -1;
    o = se;
    if (!cb || se > end) { s->stk = 0; continue; }
    s->sti = (u16)(g16(r->t, se, s0) & 0xFFF);
    s->stk = (u16)(g16(r->t, se, s0 + 2) & 0xF);
    s->base = (u16)(g16(r->t, se, s0 + 2) >> 4);
    s->next = (u16)(g16(r->t, se, s0 + 4) >> 4);
    nupx = g16(r->t, se, s0 + 4) & 0xF;
    x = s0 + cbbase;
    cch = g16(r->t, se, x);
    for (j = 0; j < cch && j < sizeof s->name - 4; ) {
      u32 c = g16(r->t, se, x + 2 + j * 2);
      char u8s[8];
      int l = utf8_put(u8s, c ? c : '?');
      size_t have = strlen(s->name);
      if (have + (size_t)l >= sizeof s->name) break;
      memcpy(s->name + have, u8s, (size_t)l); s->name[have + l] = 0;
      j++;
    }
    upx = x + 2 + cch * 2 + 2;
    for (u = 0; u < nupx && upx + 2 <= se; u++) {
      u32 n = g16(r->t, se, upx);
      if (upx + 2 + n > se) break;
      if (s->stk == 1 && u == 0) { s->papx = upx + 2; s->npapx = n; }
      else if ((s->stk == 1 && u == 1) || (s->stk == 2 && u == 0)) { s->chpx = upx + 2; s->nchpx = n; }
      upx += 2 + n + (n & 1);
    }
  }
  /* Normal's look is the document's defaults; every paragraph style's resolved */
  for (i = 0; i < cstd; i++) if (r->st[i].stk == 1) resolve_style(r, (int)i);
  for (i = 0; i < cstd; i++) r->st[i].k = -2;          /* (made when the text first uses them) */
  for (i = 0; i < cstd; i++) if (r->st[i].stk == 1 && r->st[i].sti == 0) { style_k(r, (int)i); break; }
}
/* a style of the file made a Doc style when first used (its base and next style too), so files with
   hundreds of styles don't fill Quill's table: its index, -1 if it can't be (0 for Normal) */
static int style_k(R *r, int i)
{
  Doc *d = r->d;
  DStyle *s;
  Style ds;
  int k;
  if (i < 0 || i >= r->nst) return -1;
  s = &r->st[i];
  if (s->k != -2) return s->k;
  s->k = -1;                                            /* (while it is made: a loop of bases ends here) */
  if (s->stk != 1 && s->stk != 2) return -1;
  if (s->stk == 2 && (s->sti == 65 || !strcmp(s->name, "Default Paragraph Font"))) return -1;   /* "no character style" */
  memset(&ds, 0, sizeof ds);
  strncpy(ds.name, s->name[0] ? s->name : "Style", sizeof ds.name - 1);
  style_id(ds.name, ds.id, sizeof ds.id);
  if (!ds.id[0]) snprintf(ds.id, sizeof ds.id, "Style%d", i);
  ds.type = s->stk == 1 ? ST_PARA : ST_CHAR;
  ds.based = NO_STYLE;
  if (s->stk == 1 && s->sti == 0) {
    ds.builtin = 1; ds.quick = 1;
    strcpy(ds.id, "Normal");
    ds.pf = s->pf; ds.cf = s->cf;
    ds.pf.style = 0; ds.next = 0;
    d->styles[0] = ds;
    return s->k = 0;
  }
  if ((k = doc_style(d, ds.id)) >= 0 && d->styles[k].type != ds.type)
    strncat(ds.id, ds.type == ST_CHAR ? "Char" : "Para", sizeof ds.id - 1 - strlen(ds.id));
  if (s->sti >= 1 && s->sti <= 9) ds.quick = 1;
  if (s->stk == 1) { ds.pf = s->pf; ds.cf = s->cf; }
  else {
    /* a character style: what it sets, measured against no formatting at all */
    CharFmt none = d->defcf;
    ChpArg ca;
    ds.cf = none;
    ca.cf = &ds.cf; ca.base = &none; ca.skipstyle = 1;
    if (s->nchpx) sprms(r, r->t + s->chpx, s->nchpx, chp_sprm, &ca);
  }
  if ((k = doc_add_style(d, &ds)) < 0) return -1;
  s->k = k;
  d->styles[k].pf.style = (u8)(s->stk == 1 ? k : 0);
  d->styles[k].next = (u8)k;
  if (s->base < r->nst && s->base != i && r->st[s->base].stk == s->stk) {
    int bk = style_k(r, s->base);
    if (bk >= 0 && bk != k) d->styles[k].based = (u8)bk;
  } else if (s->stk == 1) d->styles[k].based = 0;
  if (s->stk == 1 && s->next < r->nst && s->next != i && r->st[s->next].stk == 1) {
    int nk = style_k(r, s->next);
    if (nk >= 0) d->styles[k].next = (u8)nk;
  }
  return k;
}
/* the styles made: what each sets itself, worked out from what it comes to (character styles: what
   their sprms name, so they lay over any paragraph style) */
static void finish_styles(R *r)
{
  Doc *d = r->d;
  int i, k;
  unsigned cm[MAX_STYLES];
  CharFmt oc[MAX_STYLES];
  u8 mine[MAX_STYLES];
  memset(mine, 0, sizeof mine);
  for (i = 0; i < r->nst; i++) if (r->st[i].stk == 2 && r->st[i].k > 0) {
    Style *ds = &d->styles[r->st[i].k];
    CharFmt z = d->defcf;
    unsigned m = 0;
    if (ds->cf.flags != z.flags) m |= (unsigned)((ds->cf.flags ^ z.flags) & 0x7F);
    if (ds->cf.font != z.font) m |= CM_FONT;
    if (ds->cf.size != z.size) m |= CM_SIZE;
    if (ds->cf.color != z.color) m |= CM_COLOR;
    if (ds->cf.hl != z.hl) m |= CM_HL;
    if (ds->cf.fx != z.fx) m |= CM_SMALLCAPS | CM_DSTRIKE;
    if (ds->cf.ul != z.ul) m |= CM_UL;
    if (ds->cf.fx2 != z.fx2) m |= CM_FX2;
    if (ds->cf.position != z.position) m |= CM_POSITION;
    ds->ocf = ds->cf; ds->cmask = m;
    mine[r->st[i].k] = 1;
  }
  for (k = 0; k < d->nstyles; k++) { cm[k] = d->styles[k].cmask; oc[k] = d->styles[k].ocf; }
  doc_styles_from_effective(d);
  for (k = 0; k < d->nstyles; k++) if (mine[k]) { d->styles[k].cmask = cm[k]; d->styles[k].ocf = oc[k]; d->styles[k].based = NO_STYLE; }
  doc_resolve_styles(d);
}

/* ---- fonts */
static void read_fonts(R *r, u32 fc, u32 lcb)
{
  u32 n, o, end, i;
  if (!lcb || fc >= r->tn || lcb > r->tn - fc) return;
  end = fc + lcb;
  n = g16(r->t, end, fc);
  if (n == 0xFFFF) return;                    /* (an extended table: not how fonts are stored) */
  o = fc + 4;
  for (i = 0; i < n && i < 512 && o < end; i++) {
    u32 cb = g8(r->t, end, o), j;
    char name[48];
    size_t l = 0;
    for (j = o + 40; j + 1 < o + 1 + cb && j + 1 < end; j += 2) {
      u32 c = g16(r->t, end, j);
      char u[8];
      int ul;
      if (!c) break;
      ul = utf8_put(u, c);
      if (l + (size_t)ul >= sizeof name) break;
      memcpy(name + l, u, (size_t)ul); l += (size_t)ul;
    }
    name[l] = 0;
    r->ftc[i] = doc_font(r->d, name[0] ? name : "Times New Roman");
    r->nftc = (int)i + 1;
    o += 1 + cb;
  }
}

/* ---- PLCs: n + 1 CPs then n entries of size sz; n, NULL if damaged */
static int plc_count(u32 lcb, u32 sz) { return lcb >= 4 ? (int)((lcb - 4) / (4 + sz)) : 0; }
static u32 *read_cps(R *r, u32 fc, u32 lcb, u32 sz, int *np)
{
  int n, i;
  u32 *cp;
  *np = 0;
  if (!lcb || fc >= r->tn || lcb > r->tn - fc) return NULL;
  n = plc_count(lcb, sz);
  if (n <= 0 || n > 1000000) return NULL;
  if (!(cp = (u32 *)rmalloc(r, (size_t)(n + 1) * sizeof(u32)))) return NULL;
  for (i = 0; i <= n; i++) cp[i] = g32(r->t, r->tn, fc + (u32)i * 4);
  *np = n;
  return cp;
}

/* ---- the FKPs: runs of character and paragraph formatting by FC */
static int cmp_prop(const void *a, const void *b)
{
  const Prop *x = (const Prop *)a, *y = (const Prop *)b;
  return x->a < y->a ? -1 : x->a > y->a;
}
static void read_fkps(R *r, u32 fc, u32 lcb, int pap)
{
  int n, i, cap = 0, np = 0;
  Prop *p = NULL;
  u32 *cps = read_cps(r, fc, lcb, 4, &n);
  if (!cps) return;
  for (i = 0; i < n; i++) {
    u32 pn = g32(r->t, r->tn, fc + (u32)(n + 1) * 4 + (u32)i * 4) & 0x3FFFFF, pg = pn * 512, cnt, j;
    if (pg + 512 > r->wn || pg + 512 < pg) continue;
    cnt = r->w[pg + 511];
    for (j = 0; j < cnt; j++) {
      u32 a = g32(r->w, r->wn, pg + j * 4), b = g32(r->w, r->wn, pg + (j + 1) * 4), off, len;
      if (pap) {
        u32 bo, cb;
        if ((cnt + 1) * 4 + j * 13 >= 511) break;
        bo = (u32)r->w[pg + (cnt + 1) * 4 + j * 13] * 2;
        off = 0; len = 0;
        if (bo) {
          cb = r->w[pg + bo];
          if (cb) { off = pg + bo + 1; len = 2 * cb - 1; }
          else if (bo + 1 < 511) { off = pg + bo + 2; len = 2 * (u32)r->w[pg + bo + 1]; }
          if (off + len > pg + 512) { off = 0; len = 0; }
        }
      } else {
        u32 bo;
        if ((cnt + 1) * 4 + j >= 511) break;
        bo = (u32)r->w[pg + (cnt + 1) * 4 + j] * 2;
        off = 0; len = 0;
        if (bo && bo < 511) { len = r->w[pg + bo]; off = pg + bo + 1; if (off + len > pg + 512) { off = 0; len = 0; } }
      }
      if (b <= a) continue;
      if (np >= cap) {
        int nc = cap ? cap * 2 : 256;
        Prop *q = (Prop *)realloc(p, (size_t)nc * sizeof(Prop));
        if (!q) { r->oom = 1; break; }
        p = q; cap = nc;
      }
      p[np].a = a; p[np].b = b; p[np].off = off; p[np].n = (u16)len;
      np++;
    }
  }
  free(cps);
  if (p) qsort(p, (size_t)np, sizeof(Prop), cmp_prop);
  if (pap) { r->pap = p; r->npap = np; } else { r->chp = p; r->nchp = np; }
}
static const Prop *prop_at(const Prop *p, int n, u32 fc)
{
  int lo = 0, hi = n - 1;
  while (lo <= hi) {
    int m = (lo + hi) / 2;
    if (fc < p[m].a) hi = m - 1;
    else if (fc >= p[m].b) lo = m + 1;
    else return &p[m];
  }
  return NULL;
}

/* ---- the piece table */
static int read_clx(R *r, u32 fc, u32 lcb)
{
  u32 o, end;
  if (!lcb || fc >= r->tn || lcb > r->tn - fc) return 0;
  o = fc; end = fc + lcb;
  while (o < end) {
    u32 t = r->t[o];
    if (t == 1) {
      u32 cb = g16(r->t, end, o + 1);
      if (o + 3 + cb > end) return 0;
      if (r->nprc < 4096) {
        u32 *q = (u32 *)realloc(r->prc, (size_t)(r->nprc + 1) * sizeof(u32));
        if (!q) { r->oom = 1; return 0; }
        r->prc = q; r->prc[r->nprc++] = o + 3;
      }
      o += 3 + cb;
    } else if (t == 2) {
      u32 l = g32(r->t, end, o + 1), p = o + 5;
      int n = plc_count(l, 8), i;
      if (n <= 0 || p + l > end) return 0;
      r->pcp = (u32 *)rmalloc(r, (size_t)(n + 1) * sizeof(u32));
      r->pfc = (u32 *)rmalloc(r, (size_t)n * sizeof(u32));
      r->pprm = (u16 *)rmalloc(r, (size_t)n * sizeof(u16));
      if (r->oom) return 0;
      for (i = 0; i <= n; i++) r->pcp[i] = g32(r->t, end, p + (u32)i * 4);
      for (i = 0; i < n; i++) {
        u32 e = p + (u32)(n + 1) * 4 + (u32)i * 8;
        r->pfc[i] = g32(r->t, end, e + 2);
        r->pprm[i] = (u16)g16(r->t, end, e + 6);
      }
      /* pieces whose CPs don't grow, or whose text isn't in the stream, end the table (damaged) */
      for (i = 0; i < n; i++) {
        u32 len = r->pcp[i + 1] - r->pcp[i], f = r->pfc[i], start = f & 0x40000000UL ? (f & 0x3FFFFFFFUL) / 2 : f, bytes = f & 0x40000000UL ? len : len * 2;
        if (r->pcp[i + 1] <= r->pcp[i] || len > r->wn || start > r->wn || bytes > r->wn - start) break;
      }
      r->npc = i;
      return i > 0;
    } else return 0;
  }
  return 0;
}
static int piece_of(const R *r, u32 cp)
{
  int lo = 0, hi = r->npc - 1;
  while (lo <= hi) {
    int m = (lo + hi) / 2;
    if (cp < r->pcp[m]) hi = m - 1;
    else if (cp >= r->pcp[m + 1]) lo = m + 1;
    else return m;
  }
  return -1;
}
/* the character at cp (Unicode), its FC and piece; 0 if outside the text */
static int char_at(const R *r, u32 cp, u32 *ch, u32 *fc, int *piece)
{
  int i = piece_of(r, cp);
  u32 f, o;
  if (i < 0) return 0;
  f = r->pfc[i];
  if (f & 0x40000000UL) {
    o = (f & 0x3FFFFFFFUL) / 2 + (cp - r->pcp[i]);
    if (o >= r->wn) return 0;
    *ch = r->w[o] < 0x80 ? r->w[o] : cp_to_unicode(1252, r->w[o]);
  } else {
    o = f + (cp - r->pcp[i]) * 2;
    if (o + 2 > r->wn || o + 2 < o) return 0;
    *ch = g16(r->w, r->wn, o);
  }
  *fc = o; *piece = i;
  return 1;
}

/* ---- lists */
static int list_for(R *r, u32 ilfo)
{
  DLst *l = NULL;
  ListDef ld;
  int i, lv;
  u32 o;
  if (!ilfo || ilfo > (u32)r->nlfo) return 0;
  if (r->lfolist[ilfo - 1]) return r->lfolist[ilfo - 1];
  for (i = 0; i < r->nlst; i++) if (r->lst[i].lsid == r->lfo[ilfo - 1]) { l = &r->lst[i]; break; }
  if (!l) return 0;
  memset(&ld, 0, sizeof ld);
  ld.id = (u16)ilfo;
  o = l->off;
  for (lv = 0; lv < 9; lv++) {
    ListLevel *v = &ld.lvl[lv];
    u32 start = g32(r->t, r->tn, o), nfc = g8(r->t, r->tn, o + 4), cbc = g8(r->t, r->tn, o + 24), cbp = g8(r->t, r->tn, o + 25);
    u32 p = o + 28, x, cch, j;
    u8 nums[9];
    PapArg pa;
    ParaFmt pf;
    CharFmt cf;
    ChpArg ca;
    size_t tl = 0;
    int symfont;
    if (lv > 0 && l->simple) { *v = ld.lvl[0]; continue; }
    if (o + 28 > r->tn) break;
    for (j = 0; j < 9; j++) nums[j] = (u8)g8(r->t, r->tn, o + 6 + j);
    v->fmt = (u8)(nfc == 0 ? NF_DECIMAL : nfc == 1 ? NF_UPPER_ROMAN : nfc == 2 ? NF_LOWER_ROMAN : nfc == 3 ? NF_UPPER_LETTER :
                  nfc == 4 ? NF_LOWER_LETTER : nfc == 5 ? NF_ORDINAL : nfc == 6 ? NF_CARDINAL_TEXT : nfc == 7 ? NF_ORDINAL_TEXT :
                  nfc == 22 ? NF_DECIMAL_ZERO : nfc == 23 ? NF_BULLET : nfc == 255 ? NF_NONE : NF_DECIMAL);
    v->start = (u16)(start > 32767 ? 1 : start);
    memset(&pf, 0, sizeof pf);
    memset(&pa, 0, sizeof pa);
    pa.pf = &pf;
    if (p + cbp <= r->tn) sprms(r, r->t + p, cbp, pap_sprm, &pa);
    v->left = pf.left; v->hanging = -pf.first;
    p += cbp;
    cf = r->d->defcf;
    ca.cf = &cf; ca.base = &r->d->defcf; ca.skipstyle = 1;
    if (p + cbc <= r->tn) sprms(r, r->t + p, cbc, chp_sprm, &ca);
    symfont = cf.font < r->d->nfonts && (strstr(r->d->fonts[cf.font], "Symbol") || strstr(r->d->fonts[cf.font], "Wingdings") || strstr(r->d->fonts[cf.font], "Webdings"));
    if (cf.font != r->d->defcf.font && cf.font < r->d->nfonts && !symfont) strncpy(v->font, r->d->fonts[cf.font], sizeof v->font - 1);
    p += cbc;
    x = p;
    cch = g16(r->t, r->tn, x);
    if (cch > 64) cch = 64;
    for (j = 0; j < cch; j++) {
      u32 c = g16(r->t, r->tn, x + 2 + j * 2);
      char u[8];
      int ul, k, isnum = 0;
      for (k = 0; k < 9 && nums[k]; k++) if (nums[k] == j + 1) isnum = 1;
      if (isnum && c < 9) { u[0] = '%'; u[1] = (char)('1' + c); ul = 2; }
      else {
        if (v->fmt == NF_BULLET && (symfont || (c >= 0xF000 && c <= 0xF0FF))) {
          /* a bullet in a symbol font: the Unicode character it looks like, so it shows without the font */
          u32 lo = c & 0xFF;
          c = lo == 0xA7 || lo == 0x6E ? 0x25AA : lo == 0xD8 || lo == 0xE0 ? 0x25BA : lo == 0xFC ? 0x2713 : lo == 0x6F ? 0x25A1 : 0x2022;
        } else if (v->fmt == NF_BULLET && c == 0xB7) c = 0x2022;
        ul = utf8_put(u, c);
      }
      if (tl + (size_t)ul >= sizeof v->text) break;
      memcpy(v->text + tl, u, (size_t)ul); tl += (size_t)ul;
    }
    v->text[tl] = 0;
    o = x + 2 + cch * 2;
  }
  i = doc_add_list(r->d, &ld);
  r->lfolist[ilfo - 1] = i;
  return i;
}
static void read_lists(R *r, u32 fcl, u32 lcbl, u32 fco, u32 lcbo)
{
  u32 n, i, o;
  if (lcbl >= 2 && fcl < r->tn && lcbl <= r->tn - fcl) {
    n = g16(r->t, r->tn, fcl);
    if (n > 0 && n < 4096 && (r->lst = (DLst *)calloc(n, sizeof(DLst)))) {
      o = fcl + 2 + n * 28;                     /* the LVLs follow the LSTFs */
      for (i = 0; i < n; i++) {
        u32 e = fcl + 2 + i * 28, lv, nl;
        DLst *l = &r->lst[i];
        l->lsid = g32(r->t, r->tn, e);
        l->simple = (u8)(g8(r->t, r->tn, e + 26) & 1);
        l->off = o;
        nl = l->simple ? 1 : 9;
        for (lv = 0; lv < nl && o + 28 <= r->tn; lv++) {
          u32 cbc = g8(r->t, r->tn, o + 24), cbp = g8(r->t, r->tn, o + 25), x = o + 28 + cbc + cbp;
          o = x + 2 + g16(r->t, r->tn, x) * 2;
        }
      }
      r->nlst = (int)n;
    }
  }
  if (lcbo >= 4 && fco < r->tn && lcbo <= r->tn - fco) {
    n = g32(r->t, r->tn, fco);
    if (n > 0 && n < 32768 && (r->lfo = (u32 *)calloc(n, sizeof(u32))) && (r->lfolist = (int *)calloc(n, sizeof(int)))) {
      for (i = 0; i < n; i++) r->lfo[i] = g32(r->t, r->tn, fco + 4 + i * 16);
      r->nlfo = (int)n;
    }
  }
}

/* ---- sections */
typedef struct { SectFmt *s; PageSetup *pg; } SepArg;
static void sep_sprm(R *r, u32 sprm, const u8 *op, u32 n, void *arg)
{
  SepArg *a = (SepArg *)arg;
  u32 v = g8(op, n, 0), v16 = g16(op, n, 0);
  switch (sprm) {
  case 0x3009: a->s->start = (u8)(v == 0 ? SB_CONT : v == 3 ? SB_EVEN : v == 4 ? SB_ODD : SB_NEXT); break;
  case 0x500B: a->s->cols = (u8)(v16 + 1 > MAX_SECT_COLS ? MAX_SECT_COLS : v16 + 1); break;
  case 0x900C: a->s->colgap = s16(v16); break;
  case 0x3019: a->s->colline = (u8)(v != 0); break;
  case 0x300A: a->s->titlepg = (u8)(v != 0); break;
  case 0x301A: a->s->valign = (u8)(v == 1 ? VA_CENTER : v == 3 ? VA_BOTTOM : VA_TOP); break;
  case 0xB01F: if (v16 >= 720) a->pg->width = (i32)v16; break;
  case 0xB020: if (v16 >= 720) a->pg->height = (i32)v16; break;
  case 0xB021: a->pg->left = (i32)v16; break;
  case 0xB022: a->pg->right = (i32)v16; break;
  case 0x9023: a->pg->top = s16(v16) < 0 ? -s16(v16) : s16(v16); break;
  case 0x9024: a->pg->bottom = s16(v16) < 0 ? -s16(v16) : s16(v16); break;
  case 0xB017: a->pg->header = (i32)v16; break;
  case 0xB018: a->pg->footer = (i32)v16; break;
  case 0xB025: a->pg->gutter = (i32)v16; break;
  case 0x3013: a->s->linenum = (u8)(v == 0 ? LN_PAGE : v == 1 ? LN_SECTION : LN_CONT); break;
  case 0x5015: if (!v16) a->s->linenum = LN_NONE; else a->s->lnby = (u8)(v16 > 100 ? 100 : v16); break;
  default: break;
  }
}
static void sect_fmt(R *r, int i, SectFmt *s, PageSetup *pg)
{
  u32 fc = r->sepx && i >= 0 && i < r->nsed ? r->sepx[i] : 0xFFFFFFFFUL, cb;
  SepArg a;
  sect_default(s);
  pg->width = 12240; pg->height = 15840;              /* Word's defaults: Letter, 1.25" side margins */
  pg->left = pg->right = 1800; pg->top = pg->bottom = 1440;
  pg->header = pg->footer = 720; pg->gutter = 0;
  s->linenum = LN_NONE;
  a.s = s; a.pg = pg;
  if (fc == 0xFFFFFFFFUL || fc + 2 > r->wn) return;
  cb = g16(r->w, r->wn, fc);
  if (fc + 2 + cb > r->wn) return;
  sprms(r, r->w + fc + 2, cb, sep_sprm, &a);
  if (s->lnby == 0) s->lnby = 1;
}

/* ---- stories: the text between two CPs made paragraphs */
#define MAXFLD 16
typedef struct { u32 ch, fc; int piece; u16 link; } Ch;
#define IS_NOTE_CP(c) (((c) & ~0x3FFFUL) == 0xF4000UL || ((c) & ~0x3FFFUL) == 0x100000UL)   /* (a footnote's or endnote's reference: Quill raises it itself) */
typedef struct {
  R *r;
  ParaList *l;
  int main;                 /* the document's text: tables, sections and notes */
  u32 cpbase;               /* its first CP (notes and headers: their subdocument's) */
  Ch *c; u32 nc, ccap;      /* the paragraph being read */
  u8 pagebreak, colbreak;   /* the next paragraph starts a new page or column */
  /* fields: what each open one is reading (0 its code, 1 its result), its code, its link */
  int nfld;
  struct DFld { u8 part, incode, skip; int k; u16 link, cl; char code[256]; } fld[MAXFLD];
  /* the table being read */
  int intable; u32 tfirst; int row, cell;
  struct TRow { u32 first; int nedge; i16 edge[MAX_COLS + 1]; u16 tc[MAX_COLS]; u8 brc[MAX_COLS][4][3]; u32 bcol[MAX_COLS][4];
                u32 shade[MAX_COLS]; i32 rowh; u8 header, cantsplit; } *rows;
  int nrows, rcap;
  i32 tgap, tleft, tpadl, tpadr; u8 tjc, tpad;
  int nested;
} Sb;

static int sb_put(Sb *b, u32 ch, u32 fc, int piece, u16 link)
{
  if (b->nc >= b->ccap) {
    u32 nc = b->ccap ? b->ccap * 2 : 256;
    Ch *q = (Ch *)realloc(b->c, nc * sizeof(Ch));
    if (!q) { b->r->oom = 1; return 0; }
    b->c = q; b->ccap = nc;
  }
  b->c[b->nc].ch = ch; b->c[b->nc].fc = fc; b->c[b->nc].piece = piece; b->c[b->nc].link = link;
  b->nc++;
  return 1;
}
/* a piece's own sprms (Prm): a Prc's grpprl, or one sprm in two bytes */
static void piece_sprms(R *r, int piece, SprmFn f, void *arg)
{
  u16 prm;
  if (piece < 0 || piece >= r->npc) return;
  prm = r->pprm[piece];
  if (prm & 1) {
    u32 i = prm >> 1;
    if (i < (u32)r->nprc) { u32 o = r->prc[i]; sprms(r, r->t + o, g16(r->t, r->tn, o - 2), f, arg); }
  } else if (prm) {
    /* Prm0: isprm into [MS-DOC]'s table of the sprms that fit, and one byte */
    static u16 tab[0x80];
    static int init;
    u32 isprm = prm >> 1 & 0x7F;
    u8 op[2];
    if (!init) {
      u16 *t = tab;
      t[0x04] = 0x2403; t[0x05] = 0x2405; t[0x06] = 0x2406; t[0x07] = 0x2407; t[0x0C] = 0x2416; t[0x0D] = 0x2417;
      t[0x41] = 0x0835; t[0x42] = 0x0836; t[0x43] = 0x0837; t[0x44] = 0x0838; t[0x45] = 0x0839; t[0x46] = 0x083A;
      t[0x47] = 0x083B; t[0x48] = 0x083C; t[0x4A] = 0x2A3E; t[0x4D] = 0x2A42; t[0x55] = 0x2A48; t[0x5E] = 0x2A0C;
      init = 1;
    }
    if (!tab[isprm]) return;
    op[0] = (u8)(prm >> 8); op[1] = 0;
    f(r, tab[isprm], op, 1, arg);
  }
}
/* the text's look at a character: the paragraph style's, then the CHPX run's sprms, then the piece's */
static void char_fmt(R *r, const DStyle *ps, u32 fc, int piece, CharFmt *cf)
{
  const Prop *p = prop_at(r->chp, r->nchp, fc);
  ChpArg a;
  CharFmt base;
  *cf = ps ? ps->cf : r->d->defcf;
  /* a character style first (sprmCIstd), so toggles measure against it */
  if (p && p->n) {
    const u8 *g = r->w + p->off;
    u32 i = 0;
    while (i + 2 <= p->n) {
      u32 sprm = g16(g, p->n, i), sz = sprm_size(g, p->n, i + 2, sprm);
      if (i + 2 + sz > p->n) break;
      if (sprm == 0x4A30) apply_cstyle(r, (int)g16(g, p->n, i + 2), cf, 0);
      i += 2 + sz;
    }
  }
  base = *cf;
  a.cf = cf; a.base = &base; a.skipstyle = 1;
  if (p && p->n) sprms(r, r->w + p->off, p->n, chp_sprm, &a);
  piece_sprms(r, piece, chp_sprm, &a);
}
/* is the character a tracked deletion (sprmCFRMarkDel)? */
static void del_sprm(R *r, u32 sprm, const u8 *op, u32 n, void *arg) { if (sprm == 0x0800 && g8(op, n, 0) == 1) *(int *)arg = 1; if (sprm == 0x0801 && g8(op, n, 0) == 1) *(int *)arg |= 2; }
static int char_rev(R *r, u32 fc, int piece)
{
  const Prop *p = prop_at(r->chp, r->nchp, fc);
  int del = 0;
  if (p && p->n) sprms(r, r->w + p->off, p->n, del_sprm, &del);
  piece_sprms(r, piece, del_sprm, &del);
  return del;
}

static void read_story(R *r, u32 cpa, u32 cpb, ParaList *l, int main, u32 cpbase);
/* a footnote's or endnote's story at the reference cp: its number in the Doc, -1 if none */
static int note_at(R *r, u32 cp, int *endnote)
{
  int lo, hi, k;
  ParaList l;
  for (k = 0; k < 2; k++) {
    u32 *ref = k ? r->eref : r->fref, *txt = k ? r->etxt : r->ftxt;
    int n = k ? r->nen : r->nfn;
    u32 base = k ? r->ccp[0] + r->ccp[1] + r->ccp[2] + r->ccp[3] + r->ccp[4] : r->ccp[0];
    lo = 0; hi = n - 1;
    while (lo <= hi) {
      int m = (lo + hi) / 2;
      if (ref[m] < cp) lo = m + 1;
      else if (ref[m] > cp) hi = m - 1;
      else {
        int dn;
        if (!txt || r->depth > 0) return -1;
        memset(&l, 0, sizeof l);
        r->depth++;
        read_story(r, base + txt[m], base + txt[m + 1], &l, 0, base);
        r->depth--;
        dn = k ? doc_add_endnote(r->d) : doc_add_note(r->d);
        if (dn < 0) { pl_free(&l); return -1; }
        if (!l.np) pl_add(&l, para_new(&r->d->styles[0].pf, &r->d->styles[0].cf));
        pl_to_story(&l, k ? &r->d->enotes[dn] : &r->d->notes[dn]);
        *endnote = k;
        return dn;
      }
    }
  }
  return -1;
}
/* a field's code read: HYPERLINK's address becomes a link */
static u16 field_link(R *r, const char *code)
{
  const char *s = code, *q;
  char url[256];
  size_t n = 0;
  int anchor = 0;
  while (*s == ' ') s++;
  if (strncmp(s, "HYPERLINK", 9) && strncmp(s, "hyperlink", 9)) return 0;
  s += 9;
  for (;;) {
    while (*s == ' ') s++;
    if (s[0] == '\\' && (s[1] == 'l' || s[1] == 'L')) { anchor = 1; s += 2; continue; }
    if (s[0] == '\\' && s[1]) { s += 2; while (*s == ' ') s++; if (*s == '"') { s++; while (*s && *s != '"') s++; if (*s) s++; } else while (*s && *s != ' ') s++; continue; }
    break;
  }
  if (anchor) url[n++] = '#';
  if (*s == '"') { s++; for (q = s; *q && *q != '"' && n + 1 < sizeof url; q++) url[n++] = *q; }
  else for (q = s; *q && *q != ' ' && n + 1 < sizeof url; q++) url[n++] = *q;
  url[n] = 0;
  if (n == (size_t)anchor) return 0;
  return (u16)doc_link(r->d, url);
}

/* the table read so far made a Quill table: its rows' cell edges into one grid */
static int cmp_i32(const void *a, const void *b) { i32 x = *(const i32 *)a, y = *(const i32 *)b; return x < y ? -1 : x > y; }
static void table_end(Sb *b)
{
  R *r = b->r;
  ParaList *l = b->l;
  i32 edge[(MAX_COLS + 1) * 8];
  int ne = 0, i, k, num, nr = b->nrows;
  Table t;
  u32 pi;
  b->intable = 0;
  if (!nr) {
    for (pi = b->tfirst; pi < l->np; pi++) if (l->p[pi]->pf.cell.table == 0xFFFF) memset(&l->p[pi]->pf.cell, 0, sizeof(Cell));
    return;
  }
  for (i = 0; i < nr; i++) for (k = 0; k < b->rows[i].nedge && ne < (int)(sizeof edge / sizeof edge[0]); k++) edge[ne++] = b->rows[i].edge[k];
  qsort(edge, (size_t)ne, sizeof(i32), cmp_i32);
  for (i = 1, k = 1; i < ne; i++) if (edge[i] - edge[k - 1] > 20) edge[k++] = edge[i];
  ne = ne ? k : 0;
  if (ne > MAX_COLS + 1) ne = MAX_COLS + 1;
  memset(&t, 0, sizeof t);
  t.ncols = (u8)(ne > 1 ? ne - 1 : 1);
  for (i = 0; i + 1 < ne; i++) t.grid[i] = edge[i + 1] - edge[i];
  if (ne < 2) t.grid[0] = fmt_text_width(r->d);
  t.marl = t.marr = b->tgap > 0 ? b->tgap : 0;
  if (b->tpad & 1) t.marl = b->tpadl;
  if (b->tpad & 2) t.marr = b->tpadr;
  t.indent = ne ? edge[0] : 0;            /* (to the table's edge, as Quill keeps it) */
  t.jc = b->tjc;
  t.look = LK_DEFAULT;
  if (!(num = doc_add_table(r->d, &t))) { r->oom = 1; return; }
  for (pi = b->tfirst; pi < l->np; pi++) {
    Para *p = l->p[pi];
    struct TRow *w;
    int ci, from, to, a, e;
    if (p->pf.cell.table != 0xFFFF) continue;
    if (p->pf.cell.row >= nr) {        /* (a row never ended: the paragraphs are left out of the table) */
      memset(&p->pf.cell, 0, sizeof(Cell));
      continue;
    }
    w = &b->rows[p->pf.cell.row];
    ci = p->pf.cell.col;
    if (w->nedge < 2) { p->pf.cell.table = (u16)num; p->pf.cell.col = 0; p->pf.cell.span = t.ncols; continue; }
    if (ci > w->nedge - 2) ci = w->nedge - 2;
    while (ci > 0 && (w->tc[ci] & 3) == 2) ci--;             /* (merged cells' text goes to the first) */
    from = ci; to = ci;
    while (to + 1 < w->nedge - 1 && (w->tc[to + 1] & 3) == 2) to++;
    for (a = 0; a + 1 < ne && edge[a + 1] <= w->edge[from] + 20; a++) ;
    for (e = a; e + 1 < ne && edge[e + 1] < w->edge[to + 1] - 20; e++) ;
    if (e >= ne - 1) e = ne - 2;
    if (a > e) a = e;
    if (a < 0) a = 0;
    p->pf.cell.table = (u16)num;
    p->pf.cell.col = (u8)a;
    p->pf.cell.span = (u8)(e - a + 1);
    p->pf.cell.vmerge = (u8)(w->tc[from] & 0x40 ? VM_START : w->tc[from] & 0x20 ? VM_CONT : VM_NONE);
    p->pf.cell.valign = (u8)(w->tc[from] >> 7 & 3);
    if (p->pf.cell.valign > 2) p->pf.cell.valign = 0;
    p->pf.cell.header = w->header;
    p->pf.cell.cantsplit = w->cantsplit;
    p->pf.cell.rowh = (i16)(w->rowh > 32767 ? 32767 : w->rowh < -32767 ? -32767 : w->rowh);
    p->pf.cell.shade = w->shade[from];
    p->pf.cell.borders = 0;
    for (k = 0; k < 4; k++) {
      int src = k == E_RIGHT ? to : from;
      if (w->brc[src][k][0] == PB_NONE) continue;
      p->pf.cell.borders |= (u8)(1 << k);
      p->pf.cell.bstyle[k] = w->brc[src][k][0];
      p->pf.cell.bwidth[k] = w->brc[src][k][1];
      p->pf.cell.bcolor[k] = w->bcol[src][k];
    }
  }
  b->nrows = 0;
}
/* a row ended (its TTP paragraph): its cells' edges and settings */
static void row_end(Sb *b, const PapArg *pa)
{
  struct TRow *w;
  int n, k, e;
  const u8 *g = pa->tdef;
  u32 gn = pa->ntdef;
  if (b->nrows >= b->rcap) {
    int nc = b->rcap ? b->rcap * 2 : 16;
    struct TRow *q = (struct TRow *)realloc(b->rows, (size_t)nc * sizeof(struct TRow));
    if (!q) { b->r->oom = 1; return; }
    b->rows = q; b->rcap = nc;
  }
  w = &b->rows[b->nrows];
  memset(w, 0, sizeof *w);
  w->first = b->tfirst;
  w->rowh = pa->rowh; w->header = pa->header; w->cantsplit = pa->cantsplit;
  n = g ? (int)g8(g, gn, 2) : 0;
  if (n > MAX_COLS) n = MAX_COLS;
  for (k = 0; k <= n; k++) w->edge[k] = (i16)s16(g16(g, gn, 3 + (u32)k * 2));
  for (k = 1; k <= n; k++) if (w->edge[k] < w->edge[k - 1]) w->edge[k] = w->edge[k - 1];
  w->nedge = n ? n + 1 : 0;
  for (k = 0; k < n; k++) {
    u32 o = 3 + (u32)(n + 1) * 2 + (u32)k * 20;
    w->tc[k] = (u16)g16(g, gn, o);
    for (e = 0; e < 4; e++) {
      w->brc[k][e][0] = PB_NONE;
      if (o + 20 <= gn) brc80(g, gn, o + 4 + (u32)e * 4, &w->brc[k][e][0], &w->brc[k][e][1], &w->bcol[k][e]);
    }
    /* the table's own borders where the cell has none */
    if (pa->tbrc) {
      static const int tb[4][2] = { { 0, 4 }, { 1, 5 }, { 2, 4 }, { 3, 5 } };   /* top: top or inside H; left: left or inside V... */
      for (e = 0; e < 4; e++) if (w->brc[k][e][0] == PB_NONE) {
        int outer = (e == E_TOP && b->nrows == 0) || (e == E_LEFT && k == 0) || (e == E_RIGHT && k == n - 1) || e == E_BOTTOM;
        brc80(pa->tbrc, pa->ntbrc, (u32)(outer ? tb[e][0] : tb[e][1]) * 4, &w->brc[k][e][0], &w->brc[k][e][1], &w->bcol[k][e]);
      }
    }
    if (pa->shd) {
      u32 back;
      if (pa->shd80) { u32 v = g16(pa->shd, pa->nshd, 1 + (u32)k * 2), ib = v >> 5 & 31; back = ib ? ico_rgb(ib) : COLOR_AUTO; }
      else back = (1 + (u32)k * 10 + 8 <= pa->nshd) ? cv_rgb(g32(pa->shd, pa->nshd, 1 + (u32)k * 10 + 4)) : COLOR_AUTO;
      w->shade[k] = back;
    } else w->shade[k] = COLOR_AUTO;
  }
  if (b->nrows == 0) { b->tgap = pa->gap; b->tjc = pa->jc; b->tpad = pa->haspad; b->tpadl = pa->padl; b->tpadr = pa->padr; }
  b->nrows++;
}
/* the paragraph read (b->c, its mark last) added to the list */
static void para_end(Sb *b, u32 markcp, int markch)
{
  R *r = b->r;
  const Prop *pp;
  DStyle *ps = NULL;
  PapArg pa;
  ParaFmt pf;
  Para *p = NULL;
  int istd = 0;
  u32 i, n = b->nc;
  if (!n) return;
  pp = prop_at(r->pap, r->npap, b->c[n - 1].fc);
  if (pp && pp->n >= 2) istd = (int)g16(r->w, r->wn, pp->off);
  if (istd < r->nst && r->st[istd].stk == 1) ps = &r->st[istd];
  pf = ps ? ps->pf : r->d->defpf;
  pf.style = 0;
  if (ps) { int k = style_k(r, istd); pf.style = (u8)(k >= 0 ? k : 0); }
  memset(&pa, 0, sizeof pa);
  pa.pf = &pf;
  if (pp && pp->n > 2) sprms(r, r->w + pp->off + 2, pp->n - 2u, pap_sprm, &pa);
  piece_sprms(r, b->c[n - 1].piece, pap_sprm, &pa);
  if (!b->main) pa.itap = 0;
  pf.cell.table = 0; memset(&pf.cell, 0, sizeof pf.cell); memset(&pf.outer, 0, sizeof pf.outer);
  pf.sect = 0;
  if (pa.hasilfo) {
    int li = list_for(r, pa.ilfo);
    pf.list = (u16)li; pf.level = pa.ilvl;
    if (li && !pa.hasindent && li <= r->d->nlists) { pf.left = r->d->lists[li - 1].lvl[pa.ilvl].left; pf.first = -r->d->lists[li - 1].lvl[pa.ilvl].hanging; }
  }
  /* a row's end: no text of its own */
  if (pa.itap >= 1 && ((pa.itap == 1 && pa.ttp) || (pa.itap > 1 && pa.innerttp))) {
    if (pa.itap == 1) {
      if (!b->intable) { b->intable = 1; b->tfirst = b->l->np; b->row = 0; }
      row_end(b, &pa);
      b->row++; b->cell = 0;
    } else b->nested = 1;
    b->nc = 0;
    return;
  }
  if (pa.itap >= 1 && !b->intable) { b->intable = 1; b->tfirst = b->l->np; b->row = 0; b->cell = 0; b->nrows = 0; }
  if (pa.itap < 1 && b->intable) table_end(b);
  if (pa.itap > 1) b->nested = 1;
  if (b->pagebreak) { pf.pagebreak = 1; b->pagebreak = 0; }
  if (b->colbreak) { pf.colbreak = 1; b->colbreak = 0; }
  /* the text, run by run (a page or column break inside it starts a new paragraph) */
  {
    char buf[512];
    size_t bl = 0;
    CharFmt cf, cur;
    int have = 0, lastpiece = -2;
    u16 lastlink = 0;
    const Prop *lastp = NULL;
    u32 last = n && (b->c[n - 1].ch == 0x0D || b->c[n - 1].ch == 0x07) ? n - 1 : n;     /* (the mark isn't text) */
    memset(&cur, 0, sizeof cur);
    memset(&cf, 0, sizeof cf);
    if (!(p = para_new(&pf, ps ? &ps->cf : &r->d->defcf))) { r->oom = 1; b->nc = 0; return; }
    for (i = 0; i < last; i++) {
      Ch *c = &b->c[i];
      char u[8];
      int ul;
      const Prop *cp;
      if (c->ch == 0xFFFFFFFFUL) {
        int col = c->fc == 1;
        if (bl) { if (!para_append(p, buf, (u32)bl, &cur)) r->oom = 1; bl = 0; }
        if (!p->len) { if (col) p->pf.colbreak = 1; else p->pf.pagebreak = 1; continue; }
        if (i + 1 >= last) { if (col) b->colbreak = 1; else b->pagebreak = 1; continue; }
        {
          ParaFmt np = p->pf;
          np.pagebreak = (u8)!col; np.colbreak = (u8)col;
          np.sect = 0;
          if (b->intable && pa.itap >= 1) { p->pf.cell.table = 0xFFFF; p->pf.cell.row = (u16)b->row; p->pf.cell.col = (u8)(b->cell > 255 ? 255 : b->cell); }
          pl_add(b->l, p);
          if (!(p = para_new(&np, &cur))) { r->oom = 1; b->nc = 0; return; }
          have = 0;
        }
        continue;
      }
      cp = prop_at(r->chp, r->nchp, c->fc);
      if (cp != lastp || c->piece != lastpiece || c->link != lastlink || !have) {
        char_fmt(r, ps, c->fc, c->piece, &cf);
        cf.link = c->link;
        lastp = cp; lastpiece = c->piece; lastlink = c->link;
      }
      if (IS_NOTE_CP(c->ch) || (i > 0 && IS_NOTE_CP(b->c[i - 1].ch))) {
        char_fmt(r, ps, c->fc, c->piece, &cf);
        cf.link = c->link;
        if (IS_NOTE_CP(c->ch)) { cf.flags &= (u8)~CF_SUPER; cf.position = 0; }
        lastp = NULL;
      }
      if (!have || memcmp(&cf, &cur, sizeof cf)) {
        if (bl) { if (!para_append(p, buf, (u32)bl, &cur)) r->oom = 1; bl = 0; }
        cur = cf; have = 1;
      }
      ul = utf8_put(u, c->ch);
      if (bl + (size_t)ul > sizeof buf) { if (!para_append(p, buf, (u32)bl, &cur)) r->oom = 1; bl = 0; }
      memcpy(buf + bl, u, (size_t)ul); bl += (size_t)ul;
    }
    if (bl && !para_append(p, buf, (u32)bl, &cur)) r->oom = 1;
    /* an empty paragraph's look is its mark's */
    if (!p->len && n) { char_fmt(r, ps, b->c[n - 1].fc, b->c[n - 1].piece, &cf); p->r[0].f = cf; }
  }
  (void)markcp;
  if (b->intable && pa.itap >= 1) {
    p->pf.cell.table = 0xFFFF;
    p->pf.cell.row = (u16)b->row;
    p->pf.cell.col = (u8)(b->cell > 255 ? 255 : b->cell);
    if (markch == 0x07) b->cell++;
  }
  pl_add(b->l, p);
  b->nc = 0;
}
/* a field's code is read (at its separator, or its end when it has none): what Quill makes of it */
static void field_decide(Sb *b, struct DFld *f, u32 fc, int piece, int nosep)
{
  R *r = b->r;
  const char *code = f->code;
  int kind = field_kind(code), j;
  char arg[200], url[220], m[8];
  if (kind == FC_TOC && !field_switch(code, 'c', 0, NULL, 0) && !field_switch(code, 'a', 0, NULL, 0) && !field_switch(code, 'f', 0, NULL, 0)) {
    FieldInfo *fi = field_info(r->d);
    if (fi && !fi->toccode) fi->toccode = str_dup(code);
    return;
  }
  if (kind == FC_HYPERLINK) f->link = field_link(r, code);
  else if ((kind == FC_REF || kind == FK_PAGEREF) && field_arg(code, 0, arg, sizeof arg)) { snprintf(url, sizeof url, "#%s", arg); f->link = (u16)doc_link(r->d, url); }
  if (field_is_bare(code)) {
    if (kind != FC_HYPERLINK) {
      sb_put(b, kind == FK_PAGE ? 0xE000 : kind == FK_NUMPAGES ? 0xE001 : 0xE002, fc, piece, f->link);
      f->skip = 1;
    }
    return;
  }
  if ((f->k = field_new(r->d, code, 0)) < 0) return;       /* (too many: the result stays as text) */
  if (nosep) r->d->fields[f->k].nosep = 1;
  fmark_utf8(m, f->k, 0);
  sb_put(b, utf8_get(m, 4, &j), fc, piece, 0);
  if (kind >= FK_PAGE && kind <= FK_PAGEREF) {
    sb_put(b, kind == FK_PAGEREF ? 0xE002 : (kind == FK_NUMPAGES || kind == FK_SECTIONPAGES) ? 0xE001 : 0xE000, fc, piece, f->link);
    f->skip = 1;
  }
}
static void read_story(R *r, u32 cpa, u32 cpb, ParaList *l, int main, u32 cpbase)
{
  Sb *b = (Sb *)calloc(1, sizeof(Sb));
  u32 cp;
  int si = 0, aftref = 0;
  u16 link = 0;
  if (!b) { r->oom = 1; return; }
  if (cpb > r->pcp[r->npc]) cpb = r->pcp[r->npc];        /* (no text past the pieces) */
  b->r = r; b->l = l; b->main = main; b->cpbase = cpbase;
  for (cp = cpa; cp < cpb && !r->oom; cp++) {
    u32 ch, fc;
    int piece, k, hidden = 0;
    if (!char_at(r, cp, &ch, &fc, &piece)) continue;
    /* fields (core/field.h): code read, the result between the field's marks (page numbers as
       placeholders, plain links and the table of contents without marks, as .docx reads them) */
    if (ch == 0x13) {
      if (b->nfld < MAXFLD) {
        struct DFld *f = &b->fld[b->nfld];
        int j;
        memset(f, 0, sizeof *f - sizeof f->code);
        f->code[0] = 0; f->k = -1;
        for (j = 0; j < b->nfld; j++) if (!b->fld[j].part || b->fld[j].incode) f->incode = 1;
      }
      b->nfld++;
      continue;
    }
    if ((ch == 0x14 || ch == 0x15) && b->nfld) {
      int t = b->nfld - 1;
      if (t < MAXFLD) {
        struct DFld *f = &b->fld[t];
        if (!f->part && !f->incode) { field_decide(b, f, fc, piece, ch == 0x15); f->part = 1; }
        if (ch == 0x15 && f->k >= 0) { char m[8]; int j; fmark_utf8(m, f->k, 1); sb_put(b, utf8_get(m, 4, &j), fc, piece, 0); }
      }
      if (ch == 0x15) b->nfld--;
      continue;
    }
    for (k = 0; k < b->nfld && k < MAXFLD; k++) if (!b->fld[k].part || b->fld[k].incode || b->fld[k].skip) { hidden = 1; break; }
    if (hidden) {
      int t = b->nfld - 1;
      if (t < MAXFLD && !b->fld[t].part && !b->fld[t].incode && ch >= 0x20) {
        struct DFld *f = &b->fld[t];
        char u[8];
        int ul = utf8_put(u, ch);
        if (f->cl + (u32)ul < sizeof f->code) { memcpy(f->code + f->cl, u, (size_t)ul); f->cl = (u16)(f->cl + ul); f->code[f->cl] = 0; }
      }
      if (ch != 0x0D && ch != 0x07) continue;
    }
    link = 0;
    for (k = 0; k < b->nfld && k < MAXFLD; k++) if (b->fld[k].link) link = b->fld[k].link;
    /* tracked changes: deletions left out (the changes accepted) */
    if (r->chp && ch != 0x0D && ch != 0x07) {
      int rv = char_rev(r, fc, piece);
      if (rv) *r->lost |= LOST_TRACKED;
      if (rv & 1) continue;
    }
    switch (ch) {
    case 0x0D: case 0x07:
      sb_put(b, ch, fc, piece, 0);
      para_end(b, cp, (int)ch);
      continue;
    case 0x0C:
      if (main && r->sed && si < r->nsed && cp + 1 == r->sed[si + 1] && si + 1 < r->nsed) {
        /* a section ends: the paragraph with it */
        sb_put(b, 0x0D, fc, piece, 0);
        para_end(b, cp, 0x0D);
        if (b->intable) table_end(b);
        if (l->np) {
          SectFmt s;
          PageSetup pg;
          int k2;
          sect_fmt(r, si, &s, &pg);
          s.haspage = 1; s.page = pg;
          if ((k2 = doc_add_sect(r->d, &s))) l->p[l->np - 1]->pf.sect = (u16)k2;
        }
        si++;
        continue;
      }
      sb_put(b, 0xFFFFFFFFUL, 0, piece, 0);        /* a page break */
      continue;
    case 0x0E: sb_put(b, 0xFFFFFFFFUL, 1, piece, 0); continue;   /* a column break */
    case 0x0B: ch = '\v'; break;
    case 0x09: break;
    case 0x1E: ch = 0x2011; break;
    case 0x1F: ch = 0xAD; break;
    case 0x02:
      if (!main) { aftref = 1; continue; }        /* (a note's own number: the space after it goes too) */
      {
        int en = 0, dn = note_at(r, cp - cpbase, &en);
        char ref[8];
        if (dn >= 0) {
          int j;
          if (en) enote_utf8(ref, dn); else note_utf8(ref, dn);
          sb_put(b, utf8_get(ref, 4, &j), fc, piece, link);
        } else *r->lost |= LOST_NOTES;
      }
      continue;
    case 0x01: case 0x08: *r->lost |= LOST_PICTURES; continue;
    case 0x05: *r->lost |= LOST_COMMENTS; continue;
    default:
      if (ch < 0x20) continue;
      if (ch >= 0xD800 && ch < 0xE000) {          /* a surrogate pair */
        u32 c2, f2;
        int p2;
        if (ch < 0xDC00 && cp + 1 < cpb && char_at(r, cp + 1, &c2, &f2, &p2) && c2 >= 0xDC00 && c2 < 0xE000) { ch = 0x10000 + ((ch - 0xD800) << 10) + (c2 - 0xDC00); cp++; }
        else ch = 0xFFFD;
      }
      if (ch >= 0xF0000) ch = 0xFFFD;              /* (Quill's own marks live up there) */
      break;
    }
    if (aftref && ch == ' ') { aftref = 0; continue; }
    aftref = 0;
    sb_put(b, ch, fc, piece, link);
  }
  if (b->nc) { sb_put(b, 0x0D, b->c[b->nc - 1].fc, b->c[b->nc - 1].piece, 0); para_end(b, cpb, 0x0D); }
  if (b->intable) table_end(b);
  if (b->nested) *r->lost |= LOST_TABLES;
  free(b->c); free(b->rows); free(b);
}

static void r_free(R *r)
{
  free(r->pcp); free(r->pfc); free(r->pprm); free(r->prc);
  free(r->chp); free(r->pap); free(r->st); free(r->lst); free(r->lfo); free(r->lfolist);
  free(r->sed); free(r->sepx); free(r->fref); free(r->ftxt); free(r->eref); free(r->etxt); free(r->hdd);
}

static Doc *load_doc(const u8 *data, size_t len, unsigned *lost, char *err, size_t errcap)
{
  Cfb c;
  u8 *wd = NULL, *tb = NULL;
  size_t wn = 0, tn = 0;
  R rr, *r = &rr;
  Doc *d;
  u32 flags, nfib, csw, cslw, rglw, cbrg, blob, i;
  unsigned dummy = 0;
  ParaList body;
#define FC(i) (i < cbrg ? g32(wd, wn, blob + (u32)(i) * 8) : 0)
#define LCB(i) (i < cbrg ? g32(wd, wn, blob + (u32)(i) * 8 + 4) : 0)
  if (!lost) lost = &dummy;
  *lost = 0;
  if (!cfb_open(&c, data, len)) { snprintf(err, errcap, "This Word 97-2003 document (.doc) is damaged: it can't be read."); return NULL; }
  if (!(wd = cfb_read_path(&c, "WordDocument", &wn))) {
    cfb_close(&c);
    snprintf(err, errcap, "This is not a Word document.");
    return NULL;
  }
  if (wn < 0x200 || g16(wd, wn, 0) != 0xA5EC) { free(wd); cfb_close(&c); snprintf(err, errcap, "This is not a Word 97-2003 document."); return NULL; }
  nfib = g16(wd, wn, 2);
  flags = g16(wd, wn, 0x0A);
  if (nfib < 0xC1 && nfib != 0) {
    free(wd); cfb_close(&c);
    snprintf(err, errcap, "This is a Word 6 or Word 95 document, which Quill can't read. Save it as .docx in Word or LibreOffice first.");
    return NULL;
  }
  if (flags & 0x0100) {
    free(wd); cfb_close(&c);
    snprintf(err, errcap, "This Word 97-2003 document is encrypted with a password, which Quill can't open in .doc files. Save it as .docx in Word or LibreOffice first.");
    return NULL;
  }
  tb = cfb_read_path(&c, flags & 0x0200 ? "1Table" : "0Table", &tn);
  cfb_close(&c);
  if (!tb) { free(wd); snprintf(err, errcap, "This Word document is damaged: its table stream is missing."); return NULL; }
  memset(r, 0, sizeof *r);
  if (!(d = doc_new())) { free(wd); free(tb); snprintf(err, errcap, "Not enough memory to open the file."); return NULL; }
  r->d = d; r->lost = lost;
  r->w = wd; r->wn = wn; r->t = tb; r->tn = tn;
  /* the FIB: its counts of characters, then where the structures are */
  csw = g16(wd, wn, 32);
  cslw = g16(wd, wn, 34 + csw * 2);
  rglw = 36 + csw * 2;
  for (i = 0; i < 8; i++) r->ccp[i] = g32(wd, wn, rglw + (3 + i) * 4);
  cbrg = g16(wd, wn, rglw + cslw * 4);
  blob = rglw + cslw * 4 + 2;
  /* Word's defaults under everything: Times New Roman 10 pt, no spacing, single lines */
  d->defcf.size = 20;
  memset(&d->defpf, 0, sizeof d->defpf);
  d->defpf.line = 240; d->defpf.linerule = LS_AUTO;
  read_fonts(r, FC(15), LCB(15));
  if (!r->nftc) d->defcf.font = (u8)doc_font(d, "Times New Roman");
  else d->defcf.font = (u8)r->ftc[0];
  read_styles(r, FC(1), LCB(1));
  read_lists(r, FC(73), LCB(73), FC(74), LCB(74));
  read_fkps(r, FC(12), LCB(12), 0);
  read_fkps(r, FC(13), LCB(13), 1);
  if (!read_clx(r, FC(33), LCB(33))) {
    r_free(r); free(wd); free(tb); doc_free(d);
    snprintf(err, errcap, "This Word document is damaged: its text can't be found.");
    return NULL;
  }
  /* sections */
  if ((r->sed = read_cps(r, FC(6), LCB(6), 12, &r->nsed)) && (r->sepx = (u32 *)rmalloc(r, (size_t)r->nsed * sizeof(u32))))
    for (i = 0; i < (u32)r->nsed; i++) r->sepx[i] = g32(tb, tn, FC(6) + (u32)(r->nsed + 1) * 4 + i * 12 + 2);
  else { free(r->sed); r->sed = NULL; r->nsed = 0; }
  /* notes */
  {
    int n;
    r->fref = read_cps(r, FC(2), LCB(2), 2, &r->nfn);
    r->ftxt = read_cps(r, FC(3), LCB(3), 0, &n);
    if (!r->ftxt || n < r->nfn) { free(r->ftxt); r->ftxt = NULL; }
    r->eref = read_cps(r, FC(46), LCB(46), 2, &r->nen);
    r->etxt = read_cps(r, FC(47), LCB(47), 0, &n);
    if (!r->etxt || n < r->nen) { free(r->etxt); r->etxt = NULL; }
    r->hdd = read_cps(r, FC(11), LCB(11), 0, &r->nhdd);
  }
  memset(&body, 0, sizeof body);
  read_story(r, 0, r->ccp[0], &body, 1, 0);
  /* the last section's settings are the document's */
  {
    SectFmt s;
    PageSetup pg;
    int k;
    sect_fmt(r, r->nsed ? r->nsed - 1 : 0, &s, &pg);
    d->page = pg;
    d->titlepg = s.titlepg;
    if (!d->nsects) s.start = SB_NEXT;
    if (body.np && (s.cols > 1 || s.valign != VA_TOP || s.linenum != LN_NONE || s.start != SB_NEXT)) {
      s.hf = NULL; s.haspage = 0;
      if ((k = doc_add_sect(d, &s))) body.p[body.np - 1]->pf.sect = (u16)k;
    }
    for (k = 0; k < d->nsects; k++) {
      SectFmt *t = &d->sects[k];
      if (t->haspage && !memcmp(&t->page, &d->page, sizeof d->page)) t->haspage = 0;
      if (k == 0 && d->nsects && t->start == SB_NEXT) { }
    }
  }
  /* headers and footers: each kind the last section that has it (earlier sections' carry on) */
  if (r->hdd && r->nhdd > 6) {
    static const int kind[6] = { HF_EVEN, HF_DEFAULT, HF_EVEN, HF_DEFAULT, HF_FIRST, HF_FIRST };
    u32 base = r->ccp[0] + r->ccp[1];
    int sct, k, ns = (r->nhdd - 6) / 6;
    for (k = 0; k < 6; k++) {
      for (sct = ns - 1; sct >= 0; sct--) {
        int e = 6 + sct * 6 + k;
        u32 a = r->hdd[e], z = r->hdd[e + 1];
        ParaList l;
        if (z <= a || z - a < 1) continue;
        memset(&l, 0, sizeof l);
        r->depth++;
        read_story(r, base + a, base + z, &l, 0, base);
        r->depth--;
        /* (Word ends each with an extra paragraph mark: an empty last paragraph dropped) */
        if (l.np > 1 && !l.p[l.np - 1]->len) { para_free(l.p[l.np - 1]); l.np--; }
        if (l.np == 1 && !l.p[0]->len) { pl_free(&l); continue; }      /* (an empty story: none, the section before's carries on) */
        if (k < 2 || k == 4) pl_to_story(&l, &d->hdr[kind[k]]); else pl_to_story(&l, &d->ftr[kind[k]]);
        break;
      }
    }
    if (!d->titlepg) { story_free(&d->hdr[HF_FIRST]); story_free(&d->ftr[HF_FIRST]); }
    if (d->hdr[HF_EVEN].np || d->ftr[HF_EVEN].np) {
      /* even pages' own only when the document says so (the DOP's fFacingPages) */
      u32 dop = FC(31);
      if (LCB(31) > 0 && dop < tn && (g8(tb, tn, dop) & 1)) d->evenodd = 1;
      else { story_free(&d->hdr[HF_EVEN]); story_free(&d->ftr[HF_EVEN]); }
    }
  }
  finish_styles(r);
  if (r->oom || !pl_to_body(d, &body)) {
    pl_free(&body); r_free(r); free(wd); free(tb); doc_free(d);
    snprintf(err, errcap, "Not enough memory to open the file.");
    return NULL;
  }
  r_free(r); free(wd); free(tb);
  doc_clear_undo(d);
  d->dirty = 0;
  return d;
#undef FC
#undef LCB
}
const DocFormat fmt_doc = { "Word 97-2003 Document", "doc", sniff_cfb, load_doc, NULL, 0 };
