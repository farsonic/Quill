/*
 * layout_intl.h - WP27's part of the layout, included by layout.c (it uses layout.c's Work, measure,
 * add_box and lay_face): bidirectional levels and line reordering (UAX #9 through bidi.c), right-to-left
 * paragraphs laid out from the right, the complex script and East Asian look of text by its script
 * (with a font that has the characters when the text's own lacks them), Arabic shaping and numerals
 * as glyphs to measure and draw, East Asian line breaking (kinsoku), the document grid, room for
 * phonetic guides and emphasis marks, two lines in one.
 *
 * A paragraph with none of these (left to right, no character from Hebrew up, no WP27 formatting, no
 * grid) is laid out exactly as before: intl_boxes and intl_line return at once.
 */
#include "bidi.h"
#include "intl.h"

static const SectFmt *intl_sec;             /* the section of the paragraph being laid out (NULL: the default) */
static i32 intl_lpitch, intl_cpitch;        /* its document grid: line and character pitch (twips), 0 none */

/* before laying out paragraph i of L's document: its section's grid; a signature of what WP27's layout
   of it depends on beyond the paragraph itself (a cached layout with another is laid out afresh) */
static u32 intl_begin(Layout *L, u32 i)
{
  const SectFmt *s = i < L->npsec ? L->psec[i] : NULL;
  intl_sec = s;
  intl_grid(s, &intl_lpitch, &intl_cpitch, L->d->styles[0].cf.size);
  if (i == 0) bidi_kinsoku_set(L->d->intl ? L->d->intl->nobefore : NULL, L->d->intl ? L->d->intl->noafter : NULL);
  if (L->outline || L->webw > 0) intl_lpitch = intl_cpitch = 0;    /* (Outline view and web layout: no grid, as Word) */
  {
    u32 h = (u32)intl_lpitch * 31u + (u32)intl_cpitch * 7u + ((u32)bidi_numerals << 28) + 1u;
    const char *c;
    if (L->d->intl) { for (c = L->d->intl->nobefore; *c; c++) h = h * 33u + (u8)*c; for (c = L->d->intl->noafter; *c; c++) h = h * 33u + (u8)*c; }
    return h;
  }
}

/* ---- right-to-left tables and sections */
/* a right-to-left table's left edge in a column avail wide: its indent from the right (or centred, or
   at the left for end alignment) */
static i32 intl_table_x(const Table *t, i32 avail)
{
  i32 sum = 0;
  int k;
  for (k = 0; k < t->ncols; k++) sum += t->grid[k];
  return t->jc == AL_CENTER ? (avail - sum) / 2 : t->jc == AL_RIGHT ? t->indent : avail - sum - t->indent;
}
/* a cell of a right-to-left table (x worked out from the table's left edge tx0, w wide) put where it goes
   counting its columns from the right */
static void intl_cell_mirror(const Table *t, i32 tx0, i32 *x, i32 w)
{
  i32 sum = 0;
  int k;
  for (k = 0; k < t->ncols; k++) sum += t->grid[k];
  *x = tx0 + sum - (*x - tx0) - w;
}
/* a right-to-left section's columns: the first on the right */
static void intl_cols_rtl(i32 *cx, i32 *cw, int n, i32 body)
{
  int k;
  for (k = 0; k < n; k++) cx[k] = body - cx[k] - cw[k];
}

/* ---- vertical text (w:textDirection tbRl): laid out across a page as long as the page is high, then
   each line turned a quarter clockwise onto its page, the first on the right */
static int intl_vert(const SectFmt *s) { return s && s->tdir == STD_TBRL; }
/* the line length paragraph i is laid out to: the text area's height in a vertical section */
static i32 intl_body(Layout *L, u32 i, i32 body)
{
  const SectFmt *s = i < L->npsec ? L->psec[i] : NULL;
  if (intl_vert(s) && L->webw <= 0 && !L->outline) {
    const PageSetup *pg = sect_page(L->d, s, i >= L->lastfirst);
    i32 h = pg->height - pg->top - pg->bottom;
    return h < 1440 ? 1440 : h;
  }
  return body;
}
/* a vertical section's pages hold as many lines as fit across the text area's width */
static void intl_area(const PageSetup *pg, const SectFmt *s, int web, i32 *top, i32 *bottom)
{
  if (intl_vert(s) && !web) *bottom = *top + (pg->width - pg->left - pg->right);
}
/* after pagination: the lines of vertical sections turned (Placed.turn TD_DOWN): x the band across the
   page (from the right), y the text area's top, tlen its height */
static void intl_vertical(Layout *L)
{
  u32 k;
  if (L->webw > 0 || L->outline) return;
  for (k = 0; k < L->nplaced; k++) {
    Placed *pc = &L->placed[k];
    const PageInfo *pi;
    const PageSetup *pg;
    i32 h, rel, tw;
    if (pc->turn || pc->page >= L->npages || !L->pinfo) continue;
    pi = &L->pinfo[pc->page];
    if (!intl_vert(pi->sec) || in_table(&L->pl[pc->para].p->pf)) continue;
    pg = pi->pg;
    h = L->pl[pc->para].lines[pc->line].height;
    tw = pg->width - pg->left - pg->right;
    rel = pc->y - pg->top;
    pc->turn = TD_DOWN;
    pc->x = tw - rel - h; pc->w = h;
    pc->y = pg->top; pc->tlen = pg->height - pg->top - pg->bottom; pc->tshift = 0;
  }
}

/* ---- glyphs */
/* a face has a glyph for cp (shaping keeps letters whose forms it lacks) */
static int intl_has(u32 cp, void *ctx) { return ttf_glyph((Face *)ctx, cp) != 0; }
/* the last strong letter before byte off of t (for the Numeral option's context), 0 none */
static u32 intl_prev_strong(const char *t, u32 off)
{
  u32 o = off, n = 0;
  while (o > 0 && n++ < 64) {
    int used, c;
    u32 cp;
    o = (u32)utf8_prev(t, o);
    cp = utf8_get(t + o, 4, &used);
    c = bidi_class(cp);
    if (c == BC_L || c == BC_R || c == BC_AL) return cp;
  }
  return 0;
}
/* the glyphs box b (of paragraph text t, NUL-terminated) shows, in logical order: g[k] a code point
   (BIDI_GONE: nothing: an invisible control, or a lam-alef's alef), src[k] the byte it comes from.
   Arabic shaped (with the letters on either side of the box), digits as the Numeral option has them,
   mirrored glyphs at odd levels. Returns how many (at most max) */
static int intl_glyphs(Layout *L, const Box *b, const char *t, u32 *g, u32 *src, int max)
{
  u32 o = b->off, end = b->off + b->len, prev = 0;
  int n = 0, arabic = 0, k, rtlpara = 0;
  if (bidi_numerals == NUM_CONTEXT) { prev = intl_prev_strong(t, b->off); rtlpara = b->lvl & 1; }
  while (o < end && n < max) {
    int used, c;
    u32 cp = utf8_get(t + o, end - o, &used);
    src[n] = o;
    if (bidi_invisible(cp)) cp = BIDI_GONE;
    else {
      c = bidi_class(cp);
      if (c == BC_L || c == BC_R || c == BC_AL) prev = cp;
      if (cp >= '0' && cp <= '9' && bidi_numerals != NUM_ARABIC) cp = bidi_numeral(cp, prev, rtlpara);
      if (cp >= 0x0600 && cp <= 0x08FF) arabic = 1;
    }
    g[n++] = cp;
    o += (u32)used;
  }
  if (arabic) {
    FaceSize *z = lay_face(L, &b->f);
    u32 cb = 0, ca = 0;
    int used;
    if (b->off > 0) cb = utf8_get(t + utf8_prev(t, b->off), 4, &used);
    if (t[end]) ca = utf8_get(t + end, 4, &used);
    bidi_shape(g, n, cb, ca, z ? intl_has : NULL, z ? (void *)z->face : NULL);
  }
  if (b->lvl & 1) for (k = 0; k < n; k++) if (g[k] != BIDI_GONE) g[k] = bidi_mirror(g[k]);
  return n;
}
/* box b needs its glyphs worked out (else its text is shown as it is) */
static int intl_needs(const Box *b, const char *t)
{
  u32 o = b->off, end = b->off + b->len;
  if (b->lvl & 1) return 1;
  while (o < end) {
    int used;
    u32 cp = utf8_get(t + o, end - o, &used);
    if (bidi_invisible(cp) || (cp >= 0x0600 && cp <= 0x08FF) || (cp >= '0' && cp <= '9' && bidi_numerals != NUM_ARABIC)) return 1;
    o += (u32)used;
  }
  return 0;
}
/* glyphs g[a..b) as UTF-8 into out (cap bytes), reversed with rev; their length */
static u32 intl_utf8(const u32 *g, int a, int b, int rev, char *out, u32 cap)
{
  u32 n = 0;
  int k;
  for (k = 0; k < b - a; k++) {
    u32 cp = g[rev ? b - 1 - k : a + k];
    if (cp == BIDI_GONE || n + 5 > cap) continue;
    n += (u32)utf8_put(out + n, cp);
  }
  if (n < cap) out[n] = 0;
  return n;
}
#define INTL_MAXG 512
/* see layout.h */
u32 lay_visual(Layout *L, const Box *b, const char *t, char *out, u32 cap)
{
  u32 g[INTL_MAXG], src[INTL_MAXG];
  int n;
  if (!b->vis || b->kind != BX_TEXT) return 0;
  n = intl_glyphs(L, b, t, g, src, INTL_MAXG);
  return intl_utf8(g, 0, n, b->lvl & 1, out, cap);
}
/* the width (fine) of box b's text from its start up to byte off (logical order) as shown */
static u32 intl_prefix_w(Layout *L, const Box *b, const char *t, u32 off)
{
  u32 g[INTL_MAXG], src[INTL_MAXG];
  char s[INTL_MAXG * 4 + 8];
  int n = intl_glyphs(L, b, t, g, src, INTL_MAXG), k;
  FaceSize *z = lay_face(L, &b->f);
  for (k = 0; k < n && src[k] < off; k++) ;
  return z ? measure(L, z, s, intl_utf8(g, 0, k, 0, s, sizeof s), &b->f) : 0;
}
/* lay_text_x for a box laid out by WP27: right to left from its right edge, or measured as glyphs */
static i32 intl_text_x(Layout *L, const Box *b, const char *t, u32 off)
{
  i32 w;
  if (off <= b->off) return b->lvl & 1 ? b->x + b->w : b->x;
  if (off >= b->off + b->len) return b->lvl & 1 ? b->x : b->x + b->w;
  w = (i32)((intl_prefix_w(L, b, t, off) + QFINE / 2) / QFINE);
  if (w > b->w) w = b->w;
  return b->lvl & 1 ? b->x + b->w - w : b->x + w;
}

/* ---- fonts for scripts the text's font lacks */
/* a family (Doc.fonts index) whose face has cp, for text in f; -1 if none does (kept per 128 code points) */
static int intl_cover(Layout *L, const CharFmt *f, u32 cp)
{
  static struct { FontSet *fs; u32 blk; int face; } cache[48];
  static int ncache;
  FontSet *fs = L->fs;
  int i, best = -1;
  u32 blk = cp >> 7;
  for (i = 0; i < ncache; i++) if (cache[i].fs == fs && cache[i].blk == blk) { best = cache[i].face; break; }
  if (i == ncache) {
    for (i = 0; i < fs->nfaces; i++) {
      Face *fc = &fs->faces[i];
      if (fc->failed || (!fc->loaded && !ttf_load(fc))) continue;
      if (ttf_glyph(fc, cp)) { best = i; if (!fc->bold && !fc->italic) break; }
    }
    if (ncache < 48) { cache[ncache].fs = fs; cache[ncache].blk = blk; cache[ncache].face = best; ncache++; }
  }
  (void)f;
  return best >= 0 ? doc_font(L->d, fs->faces[best].family) : -1;
}

/* see layout.h */
void lay_ruby_look(Layout *L, const Box *b, const Ruby *r, CharFmt *rf)
{
  FaceSize *z;
  u32 cp;
  int used;
  *rf = b->f;
  rf->size = (u16)(r->hps ? r->hps : (b->f.size + 1) / 2);
  rf->ruby = 0; rf->em = 0; rf->eal = 0; rf->flags &= (u8)~(CF_UNDERLINE | CF_STRIKE | CF_SUPER | CF_SUB);
  rf->spacing = 0; rf->scale = 0; rf->position = 0;
  cp = r->text[0] ? utf8_get(r->text, strlen(r->text), &used) : 0;
  if (r->font[0]) {
    CharFmt t = *rf;
    int k = doc_font(L->d, r->font);
    if (k >= 0) { t.font = (u8)k; if ((z = lay_face(L, &t)) && (!cp || ttf_glyph(z->face, cp))) { *rf = t; return; } }
  }
  if (cp && (z = lay_face(L, rf)) && !ttf_glyph(z->face, cp)) { int k = intl_cover(L, rf, cp); if (k >= 0) rf->font = (u8)k; }
}

/* ---- boxes */
/* a paragraph WP27 has nothing to do with: left to right, no character past U+058F (but Quill's own
   marks), no WP27 formatting, no character grid */
static int intl_plain(Layout *L, const Para *p)
{
  u32 r, o;
  (void)L;
  if (p->pf.bidi || intl_cpitch) return 0;
  for (r = 0; r < p->nr; r++) {
    const CharFmt *f = &p->r[r].f;
    if (f->rtl || f->eal || f->em || f->ruby) return 0;
  }
  for (o = 0; o < p->len; o++)
    if ((u8)p->t[o] >= 0xD6 && (u8)p->t[o] < 0xF3) return 0;          /* (a lead byte of U+0580 up, below Quill's marks) */
    else if ((u8)p->t[o] == 0xEF && (u8)p->t[o + 1] >= 0xAC) return 0;  /* (presentation forms, wide forms: U+FB00 up) */
  if (bidi_numerals == NUM_HINDI) for (o = 0; o < p->len; o++) if (p->t[o] >= '0' && p->t[o] <= '9') return 0;
  return 1;
}
/* the width (fine) of box b as laid out: its glyphs, two lines in one at half size */
static i32 intl_box_w(Layout *L, Para *p, Box *b)
{
  FaceSize *z = lay_face(L, &b->f);
  if (!z) return 0;
  if (b->f.eal & EAL_COMBINE) {
    /* two lines in one: the longer half at half the size, and brackets */
    CharFmt hf = b->f;
    u32 mid = b->off, k = 0, n = 0, o;
    i32 w1, w2;
    FaceSize *hz;
    for (o = b->off; o < b->off + b->len; o = (u32)utf8_next(p->t, b->off + b->len, o)) n++;
    for (o = b->off; o < b->off + b->len && k < (n + 1) / 2; o = (u32)utf8_next(p->t, b->off + b->len, o)) k++;
    mid = o;
    hf.size = (u16)((b->f.size + 1) / 2);
    if (!(hz = lay_face(L, &hf))) return 0;
    w1 = (i32)measure(L, hz, p->t + b->off, mid - b->off, &hf);
    w2 = (i32)measure(L, hz, p->t + mid, b->off + b->len - mid, &hf);
    return (w1 > w2 ? w1 : w2) + (EAL_BRACKETS(&b->f) ? (i32)fonts_text(z, "()", 2) : 0);
  }
  if (b->vis) {
    u32 g[INTL_MAXG], src[INTL_MAXG];
    char s[INTL_MAXG * 4 + 8];
    int n = intl_glyphs(L, b, p->t, g, src, INTL_MAXG);
    return (i32)measure(L, z, s, intl_utf8(g, 0, n, 0, s, sizeof s), &b->f);
  }
  return (i32)measure(L, z, p->t + b->off, b->len, &b->f);
}
/* the script a character counts as for its look (neutrals take the one before, else after) */
static int intl_scr(u32 cp) { return bidi_script(cp); }
/* make box nb (a piece of a text box) ready: the look of its script, a font that has its letters,
   whether it needs its glyphs worked out, the grid, its width */
static void intl_piece(Layout *L, Para *p, Box *nb, int scr, u32 strong)
{
  FaceSize *z;
  intl_look(L->d, &nb->f, scr);
  if (strong && (z = lay_face(L, &nb->f)) && !ttf_glyph(z->face, strong)) {
    int k = intl_cover(L, &nb->f, strong);
    if (k >= 0) nb->f.font = (u8)k;
  }
  nb->vis = (u8)intl_needs(nb, p->t);
  if (intl_cpitch && scr == SCR_EA && !(p->pf.asian & AS_NOSNAP) && !(nb->f.eal & EAL_COMBINE) && (z = lay_face(L, &nb->f))) {
    /* the character grid: each wide character as wide as the pitch */
    int used;
    i32 adv = (i32)((fonts_fine(z, utf8_get(p->t + nb->off, nb->len, &used)) + QFINE / 2) / QFINE);
    if (adv > 0 && adv < 2 * intl_cpitch) nb->f.spacing = (i16)(nb->f.spacing + intl_cpitch - adv);
  }
  nb->w = intl_box_w(L, p, nb);
  if (LAY_HIDDEN(L, nb)) nb->w = 0;
}
/* a Latin letter (wordWrap off lets a line break between two) */
static int intl_letter(u32 c) { return bidi_class(c) == BC_L && !bidi_wide(c) && c > ' '; }
/* the space Word puts between East Asian text and Latin letters (autoSpaceDE) or digits (autoSpaceDN)
   touching it: a quarter of the size, after the first of the two boxes */
static void intl_autospace(Layout *L, Para *p, Box *b, u32 n)
{
  u32 k;
  (void)L;
  if ((p->pf.asian & (AS_NOSPACEDE | AS_NOSPACEDN)) == (AS_NOSPACEDE | AS_NOSPACEDN)) return;
  for (k = 0; k + 1 < n; k++) {
    int used;
    u32 a, c;
    int ea, eb, digit;
    if (b[k].kind != BX_TEXT || b[k + 1].kind != BX_TEXT || !b[k].len || !b[k + 1].len || b[k].off + b[k].len != b[k + 1].off) continue;
    a = utf8_get(p->t + utf8_prev(p->t, b[k].off + b[k].len), 4, &used);
    c = utf8_get(p->t + b[k + 1].off, b[k + 1].len, &used);
    ea = bidi_script(a) == SCR_EA; eb = bidi_script(c) == SCR_EA;
    if (ea == eb) continue;
    {
      u32 o = ea ? c : a;
      digit = o >= '0' && o <= '9';
      if (!digit && !intl_letter(o)) continue;
      if (p->pf.asian & (digit ? AS_NOSPACEDN : AS_NOSPACEDE)) continue;
    }
    b[k].w += (ea ? b[k].f.size : b[k + 1].f.size) * 10 / 4 * QFINE;
  }
}
/* punctuation that may hang past the line's end (overflowPunct, East Asian rules on): an ideographic comma or full stop */
static int intl_hang(Para *p, const Box *b)
{
  int used;
  u32 c;
  if (b->kind != BX_TEXT || (p->pf.asian & (AS_NOOVERFLOW | AS_NOKINSOKU)) || !b->len) return 0;
  c = utf8_get(p->t + b->off, b->len, &used);
  return (u32)used == b->len && (c == 0x3001 || c == 0x3002 || c == 0xFF0C || c == 0xFF0E);
}
/* the paragraph's boxes (from make_boxes, widths fine) gone through for WP27: levels (pl->rtl the
   paragraph's), text boxes split where the level, the script or a line break opportunity (East Asian
   text) changes. 0 if out of memory */
static int intl_boxes(Layout *L, ParaLay *pl, Para *p, Work *w)
{
  u32 *cp = NULL, *offs = NULL, n = 0, o, k, bi;
  u8 *cls = NULL, *lev = NULL;
  Box *nb = NULL;
  u32 nn = 0, ncap;
  int plevel, kinsoku = !(p->pf.asian & AS_NOKINSOKU), wrapmid = (p->pf.asian & AS_NOWORDWRAP) != 0;
  pl->rtl = 0;
  if (intl_plain(L, p)) return 1;
  /* the paragraph as code points, their classes, levels (w:rtl doesn't change the order: as LibreOffice) */
  cp = (u32 *)malloc((p->len + 1) * sizeof(u32));
  offs = (u32 *)malloc((p->len + 2) * sizeof(u32));
  cls = (u8 *)malloc(p->len + 1);
  lev = (u8 *)malloc(p->len + 1);
  ncap = w->n * 2 + 16;
  nb = (Box *)malloc(ncap * sizeof(Box));
  if (!cp || !offs || !cls || !lev || !nb) goto oom;
  {
    u32 r = 0;
    for (o = 0; o < p->len; ) {
      int used, c;
      u32 ch = utf8_get(p->t + o, p->len - o, &used);
      while (r + 1 < p->nr && o >= p->r[r].end) r++;
      c = ch >= 0xF0000 ? BC_ON : ch == '\t' ? BC_S : ch == '\v' ? BC_WS : bidi_class(ch);
      cp[n] = ch; cls[n] = (u8)c; offs[n] = o; n++;
      o += (u32)used;
    }
    offs[n] = p->len;
  }
  plevel = n ? bidi_levels(cp, cls, (int)n, p->pf.bidi ? 1 : 0, lev) : (p->pf.bidi ? 1 : 0);
  if (plevel < 0) plevel = p->pf.bidi ? 1 : 0;
  pl->rtl = (u8)plevel;
  /* the boxes again, text boxes in pieces */
  k = 0;                                     /* (the character the box starts at) */
  for (bi = 0; bi < w->n; bi++) {
    Box b = w->b[bi];
    while (k < n && offs[k] < b.off) k++;
    if (nn + 2 + b.len > ncap) {
      u32 nc = ncap * 2 + b.len + 4;
      Box *t = (Box *)realloc(nb, nc * sizeof(Box));
      if (!t) goto oom;
      nb = t; ncap = nc;
    }
    b.lvl = (u8)(k < n ? lev[k] : plevel);
    b.brk = 0; b.vis = 0;
    if (b.kind != BX_TEXT || ENCLOSURE(&b.f) || !b.len) { nb[nn++] = b; continue; }
    {
      /* walk its characters: a new piece where the level or script changes, or East Asian text may break */
      u32 end = b.off + b.len, q, start = k, strong = 0;
      int scr = SCR_NEUTRAL, combine = (b.f.eal & EAL_COMBINE) != 0;
      u8 pbrk = (u8)(k > 0 && bi > 0 && w->b[bi - 1].kind == BX_TEXT && w->b[bi - 1].off + w->b[bi - 1].len == b.off &&
                     !combine && bidi_can_break(cp[k - 1], cp[k], kinsoku));
      for (q = k; q < n && offs[q] < end; q++) {
        int s = intl_scr(cp[q]), cut = 0, canb = 0;
        if (q > start) {
          canb = !combine && (bidi_can_break(cp[q - 1], cp[q], kinsoku) ||
                              (wrapmid && intl_letter(cp[q - 1]) && intl_letter(cp[q])));    /* (Latin text wrapping in a word) */
          cut = canb || lev[q] != lev[q - 1] || (s != SCR_NEUTRAL && scr != SCR_NEUTRAL && s != scr) ||
                (!combine && bidi_wide(cp[q]) && bidi_no_start(cp[q]));    /* (punctuation that may hang: a piece of its own) */
        }
        if (cut) {
          Box pc = b;
          pc.off = offs[start]; pc.len = (u16)(offs[q] - offs[start]); pc.lvl = lev[start]; pc.brk = pbrk;
          intl_piece(L, p, &pc, scr == SCR_NEUTRAL ? s : scr, strong);
          nb[nn++] = pc;
          pbrk = (u8)canb; start = q; scr = SCR_NEUTRAL; strong = 0;
        }
        if (s != SCR_NEUTRAL) { if (scr == SCR_NEUTRAL) scr = s; if (!strong && s != SCR_LATIN) strong = cp[q]; }
      }
      {
        Box pc = b;
        pc.off = offs[start]; pc.len = (u16)(end - offs[start]); pc.lvl = start < n ? lev[start] : (u8)plevel; pc.brk = pbrk;
        intl_piece(L, p, &pc, scr, strong);
        nb[nn++] = pc;
      }
    }
  }
  intl_autospace(L, p, nb, nn);
  free(w->b);
  w->b = nb; w->n = nn; w->cap = ncap;
  free(cp); free(offs); free(cls); free(lev);
  return 1;
oom:
  free(cp); free(offs); free(cls); free(lev); free(nb);
  return 0;
}

/* ---- lines */
/* the extra room above a line for phonetic guides and emphasis marks over its text */
static i32 intl_above(Layout *L, Para *p, Work *w, u32 a, u32 b)
{
  u32 k;
  i32 up = 0;
  for (k = a; k < b; k++) {
    const Box *bx = &w->b[k];
    i32 e = 0;
    if (bx->kind != BX_TEXT && bx->kind != BX_SPACE) continue;
    if (bx->f.ruby) {
      const Ruby *r = intl_ruby(L->d, bx->f.ruby);
      e = (r && r->hps ? r->hps : (bx->f.size + 1) / 2) * 10 * 6 / 5;
    } else if (bx->f.em && bx->kind == BX_TEXT) e = bx->f.size * 10 / 3;
    if (e > up) up = e;
  }
  (void)p;
  return up;
}
/* a line (boxes a..b of w, x in twips after alignment) finished for WP27: rule L1, the boxes put in
   their visual order (L2), a right-to-left paragraph's line turned to run from the right edge of its
   room W (twips from the column's left: the line's left before alignment, its room and the right
   indent); the grid's line pitch; room for guides and marks above the text */
static void intl_line(Layout *L, ParaLay *pl, Para *p, Work *w, u32 a, u32 b, Line *ln, i32 W)
{
  u32 k, n = b > a ? b - a : 0;
  int rtl = pl->rtl & 1, mixed = rtl;
  i32 up;
  ln->labelx = 0;
  for (k = a; k < b && !mixed; k++) if (w->b[k].lvl) mixed = 1;
  if (mixed && n) {
    u8 lv[256];
    int ord[256], v;
    i32 cur = 0, start = 0, E;
    u32 m = n < 256 ? n : 256, q;
    int seen = 0;
    /* L1: whitespace at the end and before tabs takes the paragraph's level; tabs do */
    for (q = 0; q < m; q++) lv[q] = w->b[a + q].lvl;
    for (q = m; q-- > 0; ) {
      int kd = w->b[a + q].kind;
      if (kd == BX_SPACE || kd == BX_BREAK || kd == BX_MARK || kd == BX_FLOAT || kd == BX_REVHIDE || kd == BX_TAB) lv[q] = (u8)pl->rtl;
      else break;
    }
    for (q = 0; q < m; q++)
      if (w->b[a + q].kind == BX_TAB) {
        u32 z = q;
        lv[q] = (u8)pl->rtl;
        while (z-- > 0 && (w->b[a + z].kind == BX_SPACE || w->b[a + z].kind == BX_MARK)) lv[z] = (u8)pl->rtl;
      }
    bidi_reorder(lv, (int)m, ord);
    for (q = 0; q < m; q++) if (w->b[a + q].kind != BX_DROP) { start = w->b[a + q].x; seen = 1; break; }
    (void)seen;
    /* each box's distance from the start edge, in visual order from that edge */
    cur = start;
    for (v = 0; v < (int)m; v++) {
      Box *bx = &w->b[a + ord[rtl ? (int)m - 1 - v : v]];
      if (bx->kind == BX_DROP) continue;
      bx->x = cur; cur += bx->w;
    }
    E = cur;
    if (rtl) {
      /* turned round: from the right */
      for (q = 0; q < n; q++) { Box *bx = &w->b[a + q]; bx->x = E - bx->x - bx->w; }
      ln->left = W - ln->left - E;
    }
  } else if (rtl) ln->left = W - ln->left;
  /* first line of a right-to-left paragraph: its list label on the right */
  if (rtl && pl->label[0] && a == 0) {
    i32 E = 0;
    for (k = a; k < b; k++) if (w->b[k].kind != BX_DROP && w->b[k].x + w->b[k].w > E) E = w->b[k].x + w->b[k].w;
    ln->labelx = E > pl->labelw ? E - pl->labelw : 0;
  }
  /* guides and marks above the text */
  if ((up = intl_above(L, p, w, a, b)) > 0 && p->pf.linerule != LS_EXACT) { ln->ascent += up; ln->height += up; }
  /* the document grid's lines: a whole number of them */
  if (intl_lpitch > 0 && !(p->pf.asian & AS_NOSNAP) && p->pf.linerule == LS_AUTO && !in_table(&p->pf)) {
    i32 h = ln->height, nh = (h + intl_lpitch - 1) / intl_lpitch * intl_lpitch;
    if (nh < intl_lpitch) nh = intl_lpitch;
    ln->ascent += (nh - h) / 2; ln->height = nh;
  }
}

/* ---- caret and hit testing in mixed lines */
static int intl_mixed(const ParaLay *pl, const Line *ln)
{
  u32 k;
  if (pl->rtl) return 1;
  for (k = ln->box0; k < ln->box0 + ln->nbox; k++) if (pl->boxes[k].lvl || pl->boxes[k].vis) return 1;
  return 0;
}
/* pos_in_line for a line in visual order: the box under x (else the nearest), then the character
   boundary nearest x in it (right to left in a right-to-left box). x from the line's left */
static Pos intl_pos_in_line(Layout *L, ParaLay *pl, u32 para, u32 li, i32 x)
{
  Line *ln = &pl->lines[li];
  Pos r;
  u32 k, best = ln->box0 + ln->nbox;
  i32 bd = 0x7FFFFFFF;
  r.para = para; r.off = ln->start;
  for (k = ln->box0; k < ln->box0 + ln->nbox; k++) {
    Box *b = &pl->boxes[k];
    i32 dd = x < b->x ? b->x - x : x >= b->x + b->w ? x - (b->x + b->w) + 1 : 0;
    if (b->kind == BX_DROP || b->kind == BX_FLOAT || (b->w == 0 && b->kind != BX_TEXT && b->kind != BX_BREAK)) continue;
    if (dd < bd) { bd = dd; best = k; if (!dd) break; }
  }
  if (best < ln->box0 + ln->nbox) {
    Box *b = &pl->boxes[best];
    u32 o = b->off, end = b->off + b->len;
    if (b->kind != BX_TEXT) {
      int before = (x - b->x) * 2 < b->w;
      if (b->lvl & 1) before = !before;
      r.off = before ? b->off : end;
    } else {
      r.off = end;
      while (o < end) {
        u32 nx = (u32)utf8_next(pl->p->t, end, o);
        i32 xa = lay_text_x(L, b, pl->p->t, o), xc = lay_text_x(L, b, pl->p->t, nx);
        if ((b->lvl & 1) ? x > (xa + xc) / 2 : x < (xa + xc) / 2) { r.off = o; break; }
        o = nx;
      }
    }
  }
  if (li + 1 < pl->nlines && r.off >= pl->lines[li + 1].start) r.off = (u32)utf8_prev(pl->p->t, pl->lines[li + 1].start);
  if (!ln->nbox) r.off = ln->start;
  return r;
}
