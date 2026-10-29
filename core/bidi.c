/*
 * bidi.c - the Unicode Bidirectional Algorithm (UAX #9), mirroring, Arabic shaping, numerals,
 * scripts and East Asian line breaking (WP27). See bidi.h.
 *
 * Written from UAX #9 (Unicode 15): explicit embeddings, overrides and isolates (X1-X10) with
 * isolating run sequences, weak types (W1-W7), paired brackets (N0), neutrals (N1-N2), implicit
 * levels (I1-I2), and per line L1 and L2. Shaping follows the Unicode Standard's chapter 9 (Arabic
 * joining by Joining_Type, the presentation forms as the glyphs); kinsoku the characters Word's
 * Japanese "standard" level lists.
 */
#include "bidi.h"
#include <stdlib.h>
#include <string.h>
#include "bidi_tab.h"

int bidi_numerals = NUM_ARABIC;

int bidi_class(u32 cp)
{
  int lo = 0, hi = BIDI_NRANGES - 1;
  while (lo < hi) {
    int mid = (lo + hi + 1) / 2;
    if (bidi_start[mid] <= cp) lo = mid; else hi = mid - 1;
  }
  return bidi_cls[lo];
}
/* a two-column u16 table sorted by its first column: the second column for key, 0 if none */
static u32 pair_find(const u16 (*tab)[2], int n, u32 key)
{
  int lo = 0, hi = n - 1;
  if (key > 0xFFFF) return 0;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (tab[mid][0] == key) return tab[mid][1];
    if (tab[mid][0] < key) lo = mid + 1; else hi = mid - 1;
  }
  return 0;
}
u32 bidi_mirror(u32 cp) { u32 m = pair_find(bidi_mirtab, BIDI_NMIRROR, cp); return m ? m : cp; }
u32 bidi_bracket_close(u32 cp) { return pair_find(bidi_brtab, BIDI_NBRACKET, cp); }
u32 bidi_bracket_open(u32 cp)
{
  int i;
  for (i = 0; i < BIDI_NBRACKET; i++) if (bidi_brtab[i][1] == cp) return bidi_brtab[i][0];
  return 0;
}
int bidi_is_rtl(u32 cp)
{
  int c = bidi_class(cp);
  return c == BC_R || c == BC_AL || (cp >= 0x0590 && cp <= 0x08FF && c != BC_NSM && c != BC_AN && c != BC_EN && c != BC_ON && c != BC_CS);
}
int bidi_invisible(u32 cp)
{
  return (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2060 && cp <= 0x2064) ||
         (cp >= 0x2066 && cp <= 0x2069) || cp == 0xFEFF || cp == 0x061C;
}
int bidi_wide(u32 cp)
{
  int lo = 0, hi = BIDI_NWIDE - 1;
  if (cp < 0x1100) return 0;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (cp < bidi_widetab[mid][0]) hi = mid - 1;
    else if (cp > bidi_widetab[mid][1]) lo = mid + 1;
    else return 1;
  }
  return 0;
}
int bidi_script(u32 cp)
{
  int c;
  if (cp >= 0xF0000) return SCR_NEUTRAL;                  /* Quill's marks (pictures, notes, fields...) */
  if (bidi_wide(cp) || (cp >= 0xFF61 && cp <= 0xFF9F)) return SCR_EA;   /* (and halfwidth katakana) */
  c = bidi_class(cp);
  if (c == BC_R || c == BC_AL || c == BC_AN) return SCR_CS;
  if (c == BC_NSM) return cp >= 0x0590 && cp <= 0x0EFF ? SCR_CS : SCR_NEUTRAL;
  if (c == BC_L) return cp >= 0x0900 && cp <= 0x0EFF ? SCR_CS : SCR_LATIN;
  return SCR_NEUTRAL;
}

/* ------------------------------------------------------------------ */
/* the algorithm */
#define IS_ISO_INIT(c) ((c) == BC_LRI || (c) == BC_RLI || (c) == BC_FSI)
#define IS_REMOVED(c) ((c) == BC_RLE || (c) == BC_LRE || (c) == BC_RLO || (c) == BC_LRO || (c) == BC_PDF || (c) == BC_BN)
#define IS_NI(c) ((c) == BC_B || (c) == BC_S || (c) == BC_WS || (c) == BC_ON || IS_ISO_INIT(c) || (c) == BC_PDI)
/* the first strong class (L 0, R or AL 1) of cls[a..b), isolates skipped (P2); -1 none */
static int first_strong(const u8 *cls, const int *match, int a, int b)
{
  int i;
  for (i = a; i < b; i++) {
    int c = cls[i];
    if (c == BC_L) return 0;
    if (c == BC_R || c == BC_AL) return 1;
    if (IS_ISO_INIT(c)) { if (match[i] < 0) return -1; i = match[i]; }
  }
  return -1;
}
/* strong direction for N0 and N1: L 0; R, EN, AN 1; else -1 */
static int strong_dir(int c) { return c == BC_L ? 0 : (c == BC_R || c == BC_EN || c == BC_AN || c == BC_AL) ? 1 : -1; }
static int canon_bracket(u32 c) { return c == 0x3008 ? 0x2329 : c == 0x3009 ? 0x232A : (int)c; }
/* W1-W7, N0-N2, I1-I2 on one isolating run sequence: s[0..m) indexes into the paragraph */
static void resolve_seq(const u32 *cp, const u8 *orig, u8 *t, u8 *levels, const int *s, int m, int sos, int eos)
{
  int k, lev = levels[s[0]], e = (lev & 1) ? BC_R : BC_L, last;
  /* W1 */
  for (k = 0; k < m; k++)
    if (t[s[k]] == BC_NSM) {
      int pv = k ? t[s[k - 1]] : sos;
      t[s[k]] = (u8)(IS_ISO_INIT(pv) || pv == BC_PDI ? BC_ON : pv);
    }
  /* W2, W3 */
  last = sos;
  for (k = 0; k < m; k++) {
    int c = t[s[k]];
    if (c == BC_L || c == BC_R || c == BC_AL) last = c;
    else if (c == BC_EN && last == BC_AL) t[s[k]] = BC_AN;
  }
  for (k = 0; k < m; k++) if (t[s[k]] == BC_AL) t[s[k]] = BC_R;
  /* W4 */
  for (k = 1; k + 1 < m; k++) {
    int c = t[s[k]], a = t[s[k - 1]], b = t[s[k + 1]];
    if (c == BC_ES && a == BC_EN && b == BC_EN) t[s[k]] = BC_EN;
    else if (c == BC_CS && a == b && (a == BC_EN || a == BC_AN)) t[s[k]] = (u8)a;
  }
  /* W5 */
  for (k = 0; k < m; ) {
    int j = k;
    if (t[s[k]] != BC_ET) { k++; continue; }
    while (j < m && t[s[j]] == BC_ET) j++;
    if ((k > 0 && t[s[k - 1]] == BC_EN) || (j < m && t[s[j]] == BC_EN)) { int q; for (q = k; q < j; q++) t[s[q]] = BC_EN; }
    k = j;
  }
  /* W6 */
  for (k = 0; k < m; k++) if (t[s[k]] == BC_ES || t[s[k]] == BC_ET || t[s[k]] == BC_CS) t[s[k]] = BC_ON;
  /* W7 */
  last = sos;
  for (k = 0; k < m; k++) {
    int c = t[s[k]];
    if (c == BC_L || c == BC_R) last = c;
    else if (c == BC_EN && last == BC_L) t[s[k]] = BC_L;
  }
  /* N0: paired brackets (BD16: a stack of 63 openers; pairs in the order of their openers) */
  {
    int stk[63], stc[63], sp = 0, np = 0, pa[128][2], q;
    for (k = 0; k < m && np < 128; k++) {
      u32 c = cp[s[k]], cl;
      if (t[s[k]] != BC_ON || orig[s[k]] != BC_ON) continue;
      if ((cl = bidi_bracket_close(c))) {
        if (sp == 63) break;
        stk[sp] = k; stc[sp] = canon_bracket(cl); sp++;
      } else if (bidi_bracket_open(c)) {
        int j;
        for (j = sp - 1; j >= 0 && stc[j] != canon_bracket(c); j--) ;
        if (j >= 0) { pa[np][0] = stk[j]; pa[np][1] = k; np++; sp = j; }
      }
    }
    /* sorted by opening position (insertion sort: few pairs) */
    for (q = 1; q < np; q++) {
      int a0 = pa[q][0], a1 = pa[q][1], j = q;
      while (j > 0 && pa[j - 1][0] > a0) { pa[j][0] = pa[j - 1][0]; pa[j][1] = pa[j - 1][1]; j--; }
      pa[j][0] = a0; pa[j][1] = a1;
    }
    for (q = 0; q < np; q++) {
      int o = pa[q][0], c = pa[q][1], fe = 0, fo = 0, dir = e == BC_R, set = -1;
      for (k = o + 1; k < c; k++) {
        int d = strong_dir(t[s[k]]);
        if (d == dir) fe = 1; else if (d >= 0) fo = 1;
      }
      if (fe) set = e;
      else if (fo) {
        int ctx = sos == BC_R;
        for (k = o - 1; k >= 0; k--) { int d = strong_dir(t[s[k]]); if (d >= 0) { ctx = d; break; } }
        set = ctx != dir ? (ctx ? BC_R : BC_L) : e;
      }
      if (set >= 0) {
        int z;
        t[s[o]] = t[s[c]] = (u8)set;
        /* NSMs after either bracket take its new type */
        for (z = o + 1; z < m && orig[s[z]] == BC_NSM; z++) t[s[z]] = (u8)set;
        for (z = c + 1; z < m && orig[s[z]] == BC_NSM; z++) t[s[z]] = (u8)set;
      }
    }
  }
  /* N1, N2 */
  for (k = 0; k < m; ) {
    int j = k, before, after, v, q;
    if (!IS_NI(t[s[k]])) { k++; continue; }
    while (j < m && IS_NI(t[s[j]])) j++;
    before = strong_dir(k ? t[s[k - 1]] : sos);
    after = strong_dir(j < m ? t[s[j]] : eos);
    v = before == after && before >= 0 ? (before ? BC_R : BC_L) : e;
    for (q = k; q < j; q++) t[s[q]] = (u8)v;
    k = j;
  }
  /* I1, I2 */
  for (k = 0; k < m; k++) {
    int c = t[s[k]];
    u8 *l = &levels[s[k]];
    if (!(*l & 1)) { if (c == BC_R) *l += 1; else if (c == BC_AN || c == BC_EN) *l += 2; }
    else if (c == BC_L || c == BC_EN || c == BC_AN) *l += 1;
  }
}
int bidi_levels(const u32 *cp, const u8 *cls, int n, int dir, u8 *levels)
{
  u8 *orig, *t, *mt, *el;
  int *match, *kept, *runs, *runat, *seq, *kpos, i, nk = 0, nr = 0, plevel;
  struct { u8 level, ovr, iso; } st[BIDI_MAX_DEPTH + 2];
  int sp = 0, ovi = 0, ove = 0, vi = 0;
  if (n <= 0) return dir > 0;
  orig = (u8 *)malloc((size_t)n * 4);
  match = (int *)malloc((size_t)n * 6 * sizeof(int));
  if (!orig || !match) { free(orig); free(match); memset(levels, dir > 0, (size_t)n); return -1; }
  t = orig + n; mt = t + n; el = mt + n;
  kept = match + n; runs = kept + n; runat = runs + n; seq = runat + n; kpos = seq + n;
  for (i = 0; i < n; i++) orig[i] = (u8)(cls ? cls[i] : bidi_class(cp[i]));
  /* BD9: each isolate initiator's matching PDI */
  {
    int top = 0;
    for (i = 0; i < n; i++) {
      match[i] = -1; mt[i] = 0;
      if (IS_ISO_INIT(orig[i])) runs[top++] = i;
      else if (orig[i] == BC_PDI && top > 0) { top--; match[runs[top]] = i; mt[i] = 1; }
    }
  }
  /* P2, P3 */
  plevel = dir >= 0 ? (dir > 0) : (first_strong(orig, match, 0, n) == 1);
  /* X1-X8 */
  st[0].level = (u8)plevel; st[0].ovr = BC_ON; st[0].iso = 0;
  for (i = 0; i < n; i++) {
    int c = orig[i], nl;
    t[i] = (u8)c;
    switch (c) {
    case BC_RLE: case BC_LRE: case BC_RLO: case BC_LRO:
      nl = (c == BC_RLE || c == BC_RLO) ? (st[sp].level + 1) | 1 : (st[sp].level + 2) & ~1;
      levels[i] = st[sp].level;
      if (nl <= BIDI_MAX_DEPTH && !ovi && !ove) {
        sp++; st[sp].level = (u8)nl; st[sp].ovr = (u8)(c == BC_RLO ? BC_R : c == BC_LRO ? BC_L : BC_ON); st[sp].iso = 0;
      } else if (!ovi) ove++;
      break;
    case BC_RLI: case BC_LRI: case BC_FSI: {
      int rtl = c == BC_RLI || (c == BC_FSI && first_strong(orig, match, i + 1, match[i] >= 0 ? match[i] : n) == 1);
      levels[i] = st[sp].level;
      if (st[sp].ovr != BC_ON) t[i] = st[sp].ovr;
      nl = rtl ? (st[sp].level + 1) | 1 : (st[sp].level + 2) & ~1;
      if (nl <= BIDI_MAX_DEPTH && !ovi && !ove) { vi++; sp++; st[sp].level = (u8)nl; st[sp].ovr = BC_ON; st[sp].iso = 1; }
      else ovi++;
      break;
    }
    case BC_PDI:
      if (ovi) ovi--;
      else if (vi) { ove = 0; while (sp > 0 && !st[sp].iso) sp--; if (sp > 0) sp--; vi--; }
      levels[i] = st[sp].level;
      if (st[sp].ovr != BC_ON) t[i] = st[sp].ovr;
      break;
    case BC_PDF:
      if (ovi) ;
      else if (ove) ove--;
      else if (!st[sp].iso && sp >= 1) sp--;
      levels[i] = st[sp].level;
      break;
    case BC_B: levels[i] = (u8)plevel; break;
    case BC_BN: levels[i] = st[sp].level; break;
    default:
      levels[i] = st[sp].level;
      if (st[sp].ovr != BC_ON) t[i] = st[sp].ovr;
    }
  }
  /* X9: the characters left (kept), X10: level runs over them */
  for (i = 0; i < n; i++) { kpos[i] = nk; el[i] = levels[i]; if (!IS_REMOVED(orig[i])) kept[nk++] = i; }
  for (i = 0; i < nk; i++) {
    if (!i || levels[kept[i]] != levels[kept[i - 1]]) runs[nr++] = i;
    runat[i] = nr - 1;
  }
  /* isolating run sequences: a run not starting with a matched PDI starts one; runs ending with a
     matched isolate initiator continue at the run its PDI starts */
  {
    int r;
    for (r = 0; r < nr; r++) {
      int a = runs[r], first = kept[a], m = 0, rr = r, sos, eos, prevl, nextl, lastc;
      if (orig[first] == BC_PDI && mt[first]) continue;      /* matched: a continuation */
      for (;;) {
        int b = rr + 1 < nr ? runs[rr + 1] : nk, q, lst;
        for (q = runs[rr]; q < b; q++) seq[m++] = kept[q];
        lst = kept[b - 1];
        if (IS_ISO_INIT(orig[lst]) && match[lst] >= 0) {
          /* the run starting with its PDI (which is kept) */
          int k2 = kpos[match[lst]];
          if (k2 >= nk || kept[k2] != match[lst]) break;
          rr = runat[k2];
          continue;
        }
        break;
      }
      prevl = a > 0 ? el[kept[a - 1]] : plevel;
      lastc = seq[m - 1];
      if (IS_ISO_INIT(orig[lastc])) nextl = plevel;
      else nextl = kpos[lastc] + 1 < nk ? el[kept[kpos[lastc] + 1]] : plevel;
      sos = ((el[first] > prevl ? el[first] : prevl) & 1) ? BC_R : BC_L;
      eos = ((el[lastc] > nextl ? el[lastc] : nextl) & 1) ? BC_R : BC_L;
      resolve_seq(cp, orig, t, levels, seq, m, sos, eos);
    }
  }
  /* removed characters: their neighbour's level */
  for (i = 0; i < n; i++) if (IS_REMOVED(orig[i])) levels[i] = i ? levels[i - 1] : (u8)plevel;
  free(orig); free(match);
  return plevel;
}
void bidi_line(const u32 *cp, const u8 *cls, int n, int paralevel, u8 *levels)
{
  int i, ws = -1;           /* ws: where the whitespace before i began */
  for (i = 0; i < n; i++) {
    int c = cls ? cls[i] : bidi_class(cp[i]);
    if (c == BC_S || c == BC_B) {
      int q;
      levels[i] = (u8)paralevel;
      for (q = ws >= 0 ? ws : i; q < i; q++) levels[q] = (u8)paralevel;
      ws = -1;
    } else if (c == BC_WS || IS_ISO_INIT(c) || c == BC_PDI || IS_REMOVED(c)) { if (ws < 0) ws = i; }
    else ws = -1;
  }
  if (ws >= 0) for (i = ws; i < n; i++) levels[i] = (u8)paralevel;
}
void bidi_reorder(const u8 *levels, int n, int *order)
{
  int i, hi = 0, lo = 255, lev;
  for (i = 0; i < n; i++) {
    order[i] = i;
    if (levels[i] > hi) hi = levels[i];
    if ((levels[i] & 1) && levels[i] < lo) lo = levels[i];
  }
  for (lev = hi; lev >= lo && lev > 0; lev--) {
    for (i = 0; i < n; ) {
      int j = i;
      if (levels[order[i]] < lev) { i++; continue; }
      while (j < n && levels[order[j]] >= lev) j++;
      { int a = i, b = j - 1; while (a < b) { int tt = order[a]; order[a] = order[b]; order[b] = tt; a++; b--; } }
      i = j;
    }
  }
}
int bidi_first_strong(const char *s, u32 n)
{
  u32 i = 0;
  int depth = 0;
  while (i < n) {
    int used, c;
    u32 cp = utf8_get(s + i, n - i, &used);
    i += (u32)used;
    c = bidi_class(cp);
    if (IS_ISO_INIT(c)) depth++;
    else if (c == BC_PDI) { if (depth) depth--; }
    else if (!depth && c == BC_L) return 0;
    else if (!depth && (c == BC_R || c == BC_AL)) return 1;
  }
  return -1;
}

/* ------------------------------------------------------------------ */
/* Arabic shaping */
enum { JT_U, JT_R, JT_L, JT_D, JT_C, JT_T };
static int jtype(u32 cp)
{
  int lo = 0, hi = BIDI_NJOIN - 1;
  if (cp > 0xFFFF) return JT_U;
  while (lo < hi) {
    int mid = (lo + hi + 1) / 2;
    if (bidi_jstart[mid] <= cp) lo = mid; else hi = mid - 1;
  }
  return bidi_jt[lo];
}
static const u16 *forms_of(u32 cp)
{
  int lo = 0, hi = BIDI_NFORMS - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (bidi_forms[mid][0] == cp) return bidi_forms[mid];
    if (bidi_forms[mid][0] < cp) lo = mid + 1; else hi = mid - 1;
  }
  return NULL;
}
int bidi_shapes(u32 cp) { return cp >= 0x0600 && cp <= 0x08FF && forms_of(cp) != NULL; }
void bidi_shape(u32 *cp, int n, u32 ctxb, u32 ctxa, int (*has)(u32 cp, void *ctx), void *ctx)
{
  int i, pj = ctxb ? jtype(ctxb) : JT_U;      /* the joining type of the last letter before i */
  for (i = 0; i < n; i++) {
    int j, nj = JT_U, me, jp, jn, form;
    u32 next = 0;
    const u16 *fm;
    if (cp[i] == BIDI_GONE) { pj = JT_R; continue; }       /* (the alef of a lam-alef: joins nothing after) */
    me = jtype(cp[i]);
    if (me == JT_T) continue;
    for (j = i + 1; j < n && (cp[j] == BIDI_GONE || jtype(cp[j]) == JT_T); j++) ;
    if (j < n) next = cp[j]; else next = ctxa;
    if (next && !(j >= n && jtype(next) == JT_T)) nj = jtype(next);
    jp = (me == JT_D || me == JT_R) && (pj == JT_D || pj == JT_L || pj == JT_C);
    jn = (me == JT_D || me == JT_L) && (nj == JT_D || nj == JT_R || nj == JT_C);
    pj = me;
    if (!(fm = forms_of(cp[i]))) continue;
    if (cp[i] == 0x0644 && jn && j < n && (next == 0x0622 || next == 0x0623 || next == 0x0625 || next == 0x0627)) {
      /* lam with alef: the ligature (isolated, or final when the lam joins the letter before) */
      int q;
      for (q = 0; q < BIDI_NLAMALEF; q++)
        if (bidi_lamalef[q][0] == next && bidi_lamalef[q][1] == jp && (!has || has(bidi_lamalef[q][2], ctx))) {
          cp[i] = bidi_lamalef[q][2]; cp[j] = BIDI_GONE; pj = JT_R;
          break;
        }
      if (q < BIDI_NLAMALEF) continue;
    }
    form = jp && jn ? 4 : jp ? 2 : jn ? 3 : 1;        /* medial, final, initial, isolated */
    if (!fm[form] && form == 4) form = 2;             /* (no medial: final) */
    if (!fm[form] && form == 3) form = 1;
    if (fm[form] && (!has || has(fm[form], ctx))) cp[i] = fm[form];
  }
}

/* ------------------------------------------------------------------ */
/* numerals */
u32 bidi_numeral(u32 cp, u32 prev, int rtl)
{
  if (cp < '0' || cp > '9') return cp;
  if (bidi_numerals == NUM_HINDI) return 0x0660 + (cp - '0');
  if (bidi_numerals == NUM_CONTEXT && ((prev && bidi_class(prev) == BC_AL) || (!prev && rtl))) return 0x0660 + (cp - '0');
  return cp;
}

/* ------------------------------------------------------------------ */
/* East Asian line breaking: Word's Japanese kinsoku characters (the "standard" level) */
static const u16 nostart[] = {
  0x0021, 0x0025, 0x0029, 0x002C, 0x002E, 0x003A, 0x003B, 0x003F, 0x005D, 0x007D, 0x00A2, 0x00B0, 0x2019, 0x201D,
  0x2030, 0x2032, 0x2033, 0x2103, 0x3001, 0x3002, 0x3005, 0x3009, 0x300B, 0x300D, 0x300F, 0x3011, 0x3015,
  0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085, 0x3087, 0x308E, 0x309B, 0x309C, 0x309D, 0x309E,
  0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30EE, 0x30F5, 0x30F6, 0x30FB, 0x30FC,
  0x30FD, 0x30FE, 0xFF01, 0xFF05, 0xFF09, 0xFF0C, 0xFF0E, 0xFF1A, 0xFF1B, 0xFF1F, 0xFF3D, 0xFF5D, 0xFF61, 0xFF63,
  0xFF64, 0xFF65, 0xFF67, 0xFF68, 0xFF69, 0xFF6A, 0xFF6B, 0xFF6C, 0xFF6D, 0xFF6E, 0xFF6F, 0xFF70, 0xFF9E, 0xFF9F, 0xFFE0 };
static const u16 noend[] = {
  0x0024, 0x0028, 0x005B, 0x005C, 0x007B, 0x00A3, 0x00A5, 0x2018, 0x201C, 0x3008, 0x300A, 0x300C, 0x300E, 0x3010,
  0x3014, 0xFF04, 0xFF08, 0xFF3B, 0xFF5B, 0xFF62, 0xFFE1, 0xFFE5 };
static int in_list(const u16 *l, int n, u32 cp)
{
  int lo = 0, hi = n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (l[mid] == cp) return 1;
    if (l[mid] < cp) lo = mid + 1; else hi = mid - 1;
  }
  return 0;
}
/* a document's own lists (w:noLineBreaksBefore / After), in place of Word's */
static u32 cstart[160], cend[96];
static int ncstart = -1, ncend = -1;
static int in_custom(const u32 *l, int n, u32 cp) { int i; for (i = 0; i < n; i++) if (l[i] == cp) return 1; return 0; }
static int fill_custom(u32 *l, int max, const char *s)
{
  int n = 0;
  size_t i = 0, len = s ? strlen(s) : 0;
  if (!len) return -1;
  while (i < len && n < max) { int used; l[n++] = utf8_get(s + i, len - i, &used); i += (size_t)used; }
  return n;
}
void bidi_kinsoku_set(const char *nostart_utf8, const char *noend_utf8)
{
  ncstart = fill_custom(cstart, 160, nostart_utf8);
  ncend = fill_custom(cend, 96, noend_utf8);
}
int bidi_no_start(u32 cp) { return ncstart >= 0 ? in_custom(cstart, ncstart, cp) : in_list(nostart, (int)(sizeof nostart / sizeof nostart[0]), cp); }
int bidi_no_end(u32 cp) { return ncend >= 0 ? in_custom(cend, ncend, cp) : in_list(noend, (int)(sizeof noend / sizeof noend[0]), cp); }
/* a character lines may break before or after (ideographs, kana, Hangul, wide forms) */
static int breaky(u32 cp) { return bidi_wide(cp) || (cp >= 0xFF61 && cp <= 0xFF9F); }
int bidi_can_break(u32 a, u32 b, int kinsoku)
{
  if (!breaky(a) && !breaky(b)) return 0;
  if (kinsoku && (bidi_no_start(b) || bidi_no_end(a))) return 0;
  /* even without kinsoku, not before narrow closing punctuation nor after narrow opening (a word's own) */
  if (!breaky(b) && (bidi_class(b) == BC_CS || bidi_class(b) == BC_ET || b == ')' || b == ']' || b == '}' || b == '!' || b == '?')) return 0;
  if (!breaky(a) && (a == '(' || a == '[' || a == '{')) return 0;
  return 1;
}

/* ------------------------------------------------------------------ */
/* vertical text: the presentation forms for vertical writing (Unicode's Vertical Forms and CJK
   Compatibility Forms blocks) of punctuation and brackets */
u32 bidi_vert_form(u32 cp)
{
  static const u16 v[][2] = {
    { 0x2026, 0xFE19 }, { 0x3001, 0xFE11 }, { 0x3002, 0xFE12 }, { 0x3008, 0xFE3F }, { 0x3009, 0xFE40 }, { 0x300A, 0xFE3D },
    { 0x300B, 0xFE3E }, { 0x300C, 0xFE41 }, { 0x300D, 0xFE42 }, { 0x300E, 0xFE43 }, { 0x300F, 0xFE44 }, { 0x3010, 0xFE3B },
    { 0x3011, 0xFE3C }, { 0x3014, 0xFE39 }, { 0x3015, 0xFE3A }, { 0xFF01, 0xFE15 }, { 0xFF08, 0xFE35 }, { 0xFF09, 0xFE36 },
    { 0xFF0C, 0xFE10 }, { 0xFF1A, 0xFE13 }, { 0xFF1B, 0xFE14 }, { 0xFF1F, 0xFE16 }, { 0xFF3B, 0xFE47 }, { 0xFF3D, 0xFE48 },
    { 0xFF5B, 0xFE37 }, { 0xFF5D, 0xFE38 } };
  u32 r = pair_find(v, (int)(sizeof v / sizeof v[0]), cp);
  return r ? r : cp;
}
