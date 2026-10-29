/* layout.c - see layout.h. Paragraphs are laid out into lines (layout_para), the lines placed on pages
   (paginate), and the rest answers questions about the result: the caret, hit testing, headers,
   footers, notes, AutoFit and shapes' outlines */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "layout.h"
#include "objtext.h"
#include "hyph.h"
#include "pages.h"
#include "charfx.h"
#include "notes.h"   /* (WP22b) */
#include "math.h"     /* WP14a: equations */
#include "review.h"

#define DEFAULT_TAB 720                 /* half an inch, as Word */

/* a new, empty layout of d measured with fs (both still the caller's); NULL if out of memory.
   Nothing is laid out until lay_update */
Layout *lay_new(Doc *d, FontSet *fs)
{
  Layout *L = (Layout *)calloc(1, sizeof(Layout));
  if (!L) return NULL;
  L->d = d; L->fs = fs;
  return L;
}
/* free a paragraph layout's lines and boxes, leaving it empty (the ParaLay itself stays) */
static void pl_free(ParaLay *pl)
{
  free(pl->lines); free(pl->boxes);
  pl->lines = NULL; pl->boxes = NULL; pl->nlines = pl->nboxes = 0;
}
/* free L and all it holds, but not its Doc or FontSet; NULL is fine */
void lay_free(Layout *L)
{
  u32 i;
  if (!L) return;
  for (i = 0; i < L->npl; i++) pl_free(&L->pl[i]);
  free(L->pl); free(L->placed); free(L->page_first); free(L->page_end); free(L->cells); free(L->notenum); free(L->pn); free(L->enotenum); free(L->en); free(L->seps); free(L->psec); free(L->floats); free(L->early); free(L->pbox); free(L->pinfo);
  pages_layout_free(L);     /* (WP22a) */
  free(L->fnn); free(L->enn); free(L->ncut); free(L->enseps);      /* (WP22b) */
  free(L);
}
/* the metrics of f's font, style and size, through a small cache (fonts_size substitutes fonts, which
   is slow); the cache starts again from its first entry when full. Super and subscript are measured
   at two thirds of the size. An unknown font number means Times New Roman. NULL with no fonts at
   all, so callers check. The FontSet owns the result */
FaceSize *lay_face(Layout *L, const CharFmt *f)
{
  u16 size = f->size;
  const char *fam = f->font < L->d->nfonts ? L->d->fonts[f->font] : "Times New Roman";
  u32 key = ((u32)f->font << 24) | ((u32)(f->flags & (CF_BOLD | CF_ITALIC | CF_SUPER | CF_SUB)) << 16) | f->size;
  int i;
  FaceSize *z;
  for (i = 0; i < L->nfcache; i++) if (L->fcache[i].key == key) return L->fcache[i].z;
  if (f->flags & (CF_SUPER | CF_SUB)) size = (u16)((size * 2 + 2) / 3);
  z = fonts_size(L->fs, fam, f->flags & CF_BOLD, f->flags & CF_ITALIC, size);
  if (L->nfcache >= 64) L->nfcache = 0;
  L->fcache[L->nfcache].key = key; L->fcache[L->nfcache].z = z;
  L->nfcache++;
  return z;
}
/* small capitals: the capitals of lower case letters at four fifths of the size */
static FaceSize *small_face(Layout *L, const CharFmt *f)
{
  CharFmt t = *f;
  t.size = (u16)((f->size * 4 + 2) / 5);
  return lay_face(L, &t);
}
/* the capital of a Latin-1 lower case letter (y with diaeresis has its capital outside Latin-1);
   any other code point as it is */
static u32 upper_cp(u32 c)
{
  if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) return c - 32;
  if (c == 0xFF) return 0x178;
  return c;
}
/* a lower case letter, as far as upper_cp knows */
static int is_lower_cp(u32 c) { return upper_cp(c) != c; }
/* the width of n bytes of text in z (fine units), as f sets it: capitals, small capitals, spacing
   between the characters and their width (f NULL: as they are) */
static u32 measure(Layout *L, FaceSize *z, const char *s, u32 n, const CharFmt *f)
{
  int caps = f && (f->flags & CF_CAPS), small = f && (f->fx & FX_SMALLCAPS) && !caps;
  u32 i = 0, chars = 0;
  long w = 0;
  if (!z) return n * 120 * QFINE;          /* no fonts at all: something visible */
  if (n < 1024 && wp10_special(s, n)) {
    /* (WP10) an optional hyphen takes no room (until a line breaks at it); a nonbreaking hyphen is a hyphen.
       (One buffer will do: the text without them doesn't come back here) */
    static char t[1024];
    u32 m = wp10_plain(s, n, t);
    return measure(L, z, t, m, f);
  }
  if (caps || small) {
    FaceSize *zs = small ? small_face(L, f) : NULL;
    while (i < n) {
      int used;
      u32 c = utf8_get(s + i, n - i, &used);
      FaceSize *zz = z;
      if ((caps || small) && is_lower_cp(c)) { c = upper_cp(c); if (small && zs) zz = zs; }
      w += c < 0x80 ? zz->fine[c] : fonts_fine(zz, c);
      i += (u32)used; chars++;
    }
  } else {
    w = (long)fonts_text(z, s, n);
    if (f && f->spacing) for (i = 0; i < n; i++) if (((u8)s[i] & 0xC0) != 0x80) chars++;
  }
  if (f && f->scale && f->scale != 100) w = w * f->scale / 100;
  if (f && f->spacing) { w += (long)chars * f->spacing * QFINE; if (w < 0) w = 0; }
  return (u32)w;
}

/* ------------------------------------------------------------------ */
/* one paragraph */
typedef struct { Box *b; u32 n, cap; Line *l; u32 nl, lcap; } Work;
/* append a box to w, growing the array (which may move); 0 if out of memory */
static int add_box(Work *w, const Box *b)
{
  if (w->n >= w->cap) {
    u32 nc = w->cap ? w->cap * 2 : 64;
    Box *nb = (Box *)realloc(w->b, nc * sizeof(Box));
    if (!nb) return 0;
    w->b = nb; w->cap = nc;
  }
  w->b[w->n++] = *b;
  return 1;
}
/* append a line to w, growing the array; 0 if out of memory */
static int add_line(Work *w, const Line *l)
{
  if (w->nl >= w->lcap) {
    u32 nc = w->lcap ? w->lcap * 2 : 8;
    Line *nl = (Line *)realloc(w->l, nc * sizeof(Line));
    if (!nl) return 0;
    w->l = nl; w->lcap = nc;
  }
  w->l[w->nl++] = *l;
  return 1;
}
static void note_text(Layout *L, int end, int n, u32 counted, char *out);   /* (WP22b) */
/* the boxes of a paragraph, widths in fine units (x unset) */
static int make_boxes(Layout *L, Para *p, Work *w, u32 note, u32 enote)
{
  u32 r, start = 0;
  for (r = 0; r < p->nr; r++) {
    u32 end = p->r[r].end, i = start;
    CharFmt rvf = p->r[r].f;
    u8 rvm = 0;
    int rvh = (rvf.rev || rvf.frev) && rv_show_run(L->d, &rvf, &rvm);   /* (WP5: how a tracked change shows) */
    const CharFmt *f = &rvf;
    FaceSize *z = lay_face(L, f);
    while (i < end) {
      Box b;
      u32 j = i;
      memset(&b, 0, sizeof b);
      b.off = i; b.f = *f;
      b.rvmark = rvm;
      if (rvh && !(i + 4 <= end && IS_MARK(p->t + i))) {
        /* (WP5) text the change hides as shown: no width, up to the next comment mark */
        while (j < end && !(j + 4 <= end && IS_MARK(p->t + j)) && j - i < 60000) j = (u32)utf8_next(p->t, end, j);
        b.kind = BX_REVHIDE; b.len = (u16)(j - i); b.f.hl = 0;
        if (!add_box(w, &b)) return 0;
        i = j;
        continue;
      }
      if (p->t[i] == ' ') {
        while (j < end && p->t[j] == ' ' && j - i < 255) j++;
        b.kind = BX_SPACE; b.nspace = (u8)(j - i);
      } else if (p->t[i] == '\t') {
        j = i + 1; b.kind = BX_TAB;
      } else if (p->t[i] == '\v') {
        j = i + 1; b.kind = BX_BREAK;
      } else if (i + 4 <= end && IS_PIC(p->t + i)) {
        int n = pic_index(p->t + i);
        j = i + 4; b.kind = n < L->d->nimages && L->d->images[n].wrap != WRAP_INLINE ? BX_FLOAT : BX_PIC;
      } else if (i + 4 <= end && IS_NOTE(p->t + i)) {
        j = i + 4; b.kind = BX_NOTE;
      } else if (i + 4 <= end && IS_MARK(p->t + i)) {
        j = i + 4; b.kind = BX_MARK;
      } else if (i + 4 <= end && IS_ENOTE(p->t + i)) {
        j = i + 4; b.kind = BX_ENOTE;
      } else if (i + 3 <= end && IS_FIELD(p->t + i)) {
        j = i + 3; b.kind = BX_FIELD;
      } else if (i + 4 <= end && IS_MATH(p->t + i)) {        /* WP14a: an equation (nspace: a display one) */
        const MLay *ml = math_layout(L->fs, L->d, math_index(p->t + i), f);
        const MathEq *me = doc_math(L->d, math_index(p->t + i));
        j = i + 4; b.kind = BX_MATH; b.nspace = (u8)(me && me->display);
        b.w = ml ? ml->w * QFINE : 0;
      } else {
        while (j < end && p->t[j] != ' ' && p->t[j] != '\t' && p->t[j] != '\v' && !(j + 4 <= end && (IS_PIC(p->t + j) || IS_NOTE(p->t + j) || IS_MARK(p->t + j) || IS_ENOTE(p->t + j))) &&
               !(j + 3 <= end && IS_FIELD(p->t + j)) && !(j + 4 <= end && IS_MATH(p->t + j)) && j - i < 60000) j++;
        b.kind = BX_TEXT;
      }
      b.len = (u16)(j - i);
      if (b.kind == BX_MATH) ;
      else if (b.kind == BX_PIC) {
        int n = pic_index(p->t + i);
        b.w = n < L->d->nimages ? OBJ_W(&L->d->images[n]) * QFINE : 0;
      } else if (b.kind == BX_FLOAT) {
        b.w = 0;
      } else if (b.kind == BX_NOTE || b.kind == BX_ENOTE) {
        /* the note's number, raised */
        char num[24];
        FaceSize *nz;
        b.f.flags = (u8)((b.f.flags & ~CF_SUB) | CF_SUPER);
        nz = lay_face(L, &b.f);
        note_text(L, b.kind == BX_ENOTE, note_index(p->t + i), b.kind == BX_NOTE ? ++note : ++enote, num);   /* (WP22b) */
        b.w = nz ? (i32)fonts_text(nz, num, (u32)strlen(num)) : 0;
      } else if (b.kind == BX_FIELD) {
        char num[16];
        lay_field_text(L, p, &b, 0, num);
        b.w = (i32)measure(L, z, num, (u32)strlen(num), f);
      } else b.w = b.kind == BX_TAB || b.kind == BX_BREAK || b.kind == BX_MARK ? 0 : (i32)measure(L, z, p->t + i, j - i, f);
      if (ENCLOSURE(f) && b.kind == BX_TEXT) b.w = wp9_enclosed_w(b.w, f);   /* (WP9: room for the enclosure) */
      if (LAY_HIDDEN(L, &b)) b.w = 0;                                       /* (WP9: hidden text takes no room) */
      if (!add_box(w, &b)) return 0;
      i = j;
    }
    start = end;
  }
  return 1;
}
/* the next tab stop after x (twips from the left indent): the paragraph's own (up to 8, in order, a
   0 ending them), then the default stops every half inch */
static i32 next_tab(const ParaFmt *pf, i32 x)      /* x: twips from the left indent */
{
  int k;
  for (k = 0; k < 8 && pf->tabs[k]; k++) if (pf->tabs[k] > x) return pf->tabs[k];
  return (x / DEFAULT_TAB + 1) * DEFAULT_TAB;
}
/* the same, with the stop's alignment and leader (default stops: left, none) */
static i32 next_stop(const ParaFmt *pf, i32 x, int *kind, int *lead)
{
  int k;
  *kind = TAB_LEFT; *lead = LEAD_NONE;
  for (k = 0; k < 8 && pf->tabs[k]; k++) if (pf->tabs[k] > x) { *kind = pf->tabkind[k]; *lead = pf->tablead[k]; return pf->tabs[k]; }
  return (x / DEFAULT_TAB + 1) * DEFAULT_TAB;
}
/* the width (fine) of the text after a tab, up to the next tab or line break; for a decimal stop,
   up to its first '.' */
static i32 tab_segment(Layout *L, Para *p, Work *w, u32 from, int decimal)
{
  i32 sw = 0;
  u32 k;
  for (k = from; k < w->n; k++) {
    Box *b = &w->b[k];
    if (b->kind == BX_TAB || b->kind == BX_BREAK) break;
    if (decimal && b->kind == BX_TEXT) {
      const char *dot = memchr(p->t + b->off, '.', b->len);
      if (dot) {
        FaceSize *z = lay_face(L, &b->f);
        return sw + (z ? (i32)measure(L, z, p->t + b->off, (u32)(dot - (p->t + b->off)), &b->f) : 0);
      }
    }
    sw += b->w;
  }
  return sw;
}
/* split a text box that is too long for a line, so that the first part fits in room (fine) */
static int split_box(Layout *L, Para *p, Work *w, u32 bi, u32 room)
{
  Box *b = &w->b[bi], nb;
  FaceSize *z = lay_face(L, &b->f);
  u32 o = b->off, end = b->off + b->len, acc = 0;
  while (o < end) {
    u32 nx = (u32)utf8_next(p->t, end, o), cw = measure(L, z, p->t + o, nx - o, &b->f);
    if (acc + cw > room && o > b->off) break;
    acc += cw; o = nx;
  }
  if (o >= end) return 1;
  nb = *b;
  nb.off = o; nb.len = (u16)(end - o); nb.w = b->w - (i32)acc;
  b->len = (u16)(o - b->off); b->w = (i32)acc;
  if (!add_box(w, &nb)) return 0;                     /* may move w->b */
  memmove(w->b + bi + 2, w->b + bi + 1, (w->n - bi - 2) * sizeof(Box));
  w->b[bi + 1] = nb;
  return 1;
}
/* split a text box at byte off (inside it) */
static int split_at(Layout *L, Para *p, Work *w, u32 bi, u32 off)
{
  Box *b = &w->b[bi], nb;
  FaceSize *z = lay_face(L, &b->f);
  u32 end = b->off + b->len, a = z ? measure(L, z, p->t + b->off, off - b->off, &b->f) : 0;
  nb = *b;
  nb.off = off; nb.len = (u16)(end - off); nb.w = b->w - (i32)a;
  b->len = (u16)(off - b->off); b->w = (i32)a;
  if (!add_box(w, &nb)) return 0;                     /* may move w->b */
  memmove(w->b + bi + 2, w->b + bi + 1, (w->n - bi - 2) * sizeof(Box));
  w->b[bi + 1] = nb;
  return 1;
}
#define HYPH_ZONE pages_hyph_zone(L->d)   /* Word's hyphenation zone (360): a gap narrower than this isn't worth a hyphen (WP22a: the document's) */
/* automatic hyphenation: the word in box bi doesn't fit in what is left of the line (x of room, fine).
   Its longest start that fits with a hyphen: split there (the first part marked to show the hyphen,
   its width taking it in); 1 if it was split */
static int hyphenate(Layout *L, Para *p, Work *w, u32 bi, u32 x, u32 room)
{
  Box *b = &w->b[bi];
  FaceSize *z = lay_face(L, &b->f);
  const char *t = p->t + b->off;
  u32 lead = 0, core, k, hw;
  u8 brk[40];
  if (!z || b->kind != BX_TEXT || room < x + HYPH_ZONE * QFINE || (bi + 1 < w->n && w->b[bi + 1].kind == BX_TEXT)) return 0;
  while (lead < b->len && (t[lead] == '"' || t[lead] == '(' || t[lead] == '\'')) lead++;
  for (core = lead; core < b->len && (((t[core] | 32) >= 'a' && (t[core] | 32) <= 'z') || t[core] == '\''); core++) ;
  /* the rest may only be punctuation */
  for (k = core; k < b->len; k++) if (!strchr(".,;:!?\")'", t[k])) return 0;
  if (core - lead < 5 || core - lead > 32 || !hyph_word(t + lead, core - lead, brk)) return 0;
  if (!pages_hyph_allowed(L->d, t + lead, core - lead, w->l, w->nl, w->b)) return 0;      /* (WP22a: capitals, consecutive) */
  hw = measure(L, z, "-", 1, NULL);
  for (k = core - lead; k-- > 1; ) {
    if (!brk[k]) continue;
    if (x + measure(L, z, t, lead + k, &b->f) + hw <= room) {
      if (!split_at(L, p, w, bi, b->off + lead + k)) return 0;
      b = &w->b[bi];
      b->nspace = 1; b->w += (i32)hw;
      return 1;
    }
  }
  return 0;
}
/* (WP10) a word that doesn't fit in what is left of the line (x of room, fine) broken at its last
   optional hyphen (U+00AD, which then shows as a hyphen) or ordinary hyphen that leaves a first part
   that fits; never at a nonbreaking hyphen (U+2011). 1 if it was split */
static int hyphen_break(Layout *L, Para *p, Work *w, u32 bi, u32 x, u32 room)
{
  Box *b = &w->b[bi];
  FaceSize *z = lay_face(L, &b->f);
  const char *t = p->t + b->off;
  u32 k, hw;
  if (!z || b->kind != BX_TEXT || b->len < 2) return 0;
  hw = measure(L, z, "-", 1, NULL);
  for (k = b->len - 1; k > 0; k--) {
    int soft = k + 1 < b->len && (u8)t[k] == 0xC2 && (u8)t[k + 1] == 0xAD, hard = t[k] == '-' && k + 1 < b->len;
    if (!soft && !hard) continue;
    if (soft ? x + measure(L, z, t, k, &b->f) + hw <= room : x + measure(L, z, t, k + 1, &b->f) <= room) {
      if (!split_at(L, p, w, bi, b->off + k + (soft ? 2 : 1))) return 0;
      b = &w->b[bi];
      if (soft) { b->nspace = 1; b->w += (i32)hw; }      /* shown as a hyphen, as automatic hyphenation's */
      return 1;
    }
  }
  return 0;
}
/* a line's height and ascent: the tallest text on it (raised and lowered text making room), inline
   pictures standing on the baseline, then the paragraph's line spacing (exactly, at least, or a
   multiple in 240ths, as Word stores it) */
static void line_metrics(Layout *L, Para *p, Work *w, Line *ln)
{
  u32 k;
  i32 asc = 0, h = 0;
  const ParaFmt *pf = &p->pf;
  i32 picasc = 0, masc = 0, mdesc = 0;
  for (k = ln->box0; k < ln->box0 + ln->nbox; k++) {
    FaceSize *z;
    if (w->b[k].kind == BX_DROP) continue;            /* it hangs down over the lines */
    if (w->b[k].kind == BX_PIC) {
      int n = pic_index(p->t + w->b[k].off);
      if (n < L->d->nimages && OBJ_H(&L->d->images[n]) > picasc) picasc = OBJ_H(&L->d->images[n]);
      continue;
    }
    if (w->b[k].kind == BX_FLOAT || LAY_HIDDEN(L, &w->b[k])) continue;    /* (hidden text: WP9) */
    if (w->b[k].kind == BX_MATH) {                  /* WP14a: an equation's height and depth */
      const MLay *ml = math_layout(L->fs, L->d, math_index(p->t + w->b[k].off), &w->b[k].f);
      if (ml && ml->asc > masc) masc = ml->asc;
      if (ml && ml->desc > mdesc) mdesc = ml->desc;
      continue;
    }
    z = lay_face(L, &w->b[k].f);
    if (z) {
      /* raised text: the line's top above it; lowered: room below */
      i32 up = w->b[k].f.position * 10, a = z->ascent + (up > 0 ? up : 0), hh = z->height + (up > 0 ? up : -up);
      if (hh > h) h = hh;
      if (a > asc) asc = a;
    }
  }
  if (!h) {                                 /* no text on the line: the paragraph mark's format */
    FaceSize *z = lay_face(L, &p->r[p->nr - 1].f);
    h = z ? z->height : 276; asc = z ? z->ascent : 220;
  }
  if (pf->linerule == LS_EXACT && pf->line > 0) { asc = asc * pf->line / (h ? h : 1); h = pf->line; }
  else if (pf->linerule == LS_ATLEAST && pf->line > h) { asc += pf->line - h; h = pf->line; }
  else if (pf->linerule == LS_AUTO && pf->line > 0 && pf->line != 240) h = h * pf->line / 240;
  /* a picture taller than the text: its bottom on the baseline, the line grows (unless exact) */
  if (picasc > asc && pf->linerule != LS_EXACT) { h += picasc - asc; asc = picasc; }
  /* WP14a: an equation taller or deeper than the text: the line grows (unless exact) */
  if (masc > asc && pf->linerule != LS_EXACT) { h += masc - asc; asc = masc; }
  if (mdesc > h - asc && pf->linerule != LS_EXACT) h = asc + mdesc;
  ln->height = h; ln->ascent = asc;
}
/* text wrapping round floating objects: while laying out a paragraph placed on a page (sh_on), each
   line goes where the objects leave room: the widest gap between them at its height, or below them */
static int sh_on;
static const FloatBox *sh_fb; static u32 sh_nfb, sh_page;
static i32 sh_x0, sh_y0;                /* the paragraph's text area's left edge and its first line's top, on the page */
static const Doc *sh_doc;
/* (WP22a) columns of different widths: while laying out a paragraph placed in them (col_on), each
   line as wide as the column it is going to: the paragraph starts in column col_c0 at col_y0 (at its
   top: col_attop); a column's text runs from col_top (on later pages col_ptop) down to col_bottom */
static int col_on, col_n, col_c0, col_attop;
static i32 col_w[MAX_SECT_COLS], col_y0, col_top, col_ptop, col_bottom;
/* the width of the column the line after lines l[0..nl) goes in, est high */
static i32 col_room(const Line *l, u32 nl, i32 est)
{
  int c = col_c0, attop = col_attop;
  i32 y = col_y0;
  u32 q;
  for (q = 0; ; q++) {
    i32 h = q < nl ? l[q].height : est;
    if (y + h > col_bottom && !attop) { c = c + 1 < col_n ? c + 1 : 0; y = c ? col_top : col_ptop; attop = 1; }
    if (q >= nl) return col_w[c];
    y += h; attop = 0;
  }
}
/* the part of an outline (n points round, in page twips) between y0 and y1 across: 0 if none */
static int band_span(const i32 *xy, int n, i32 y0, i32 y1, i32 *x0, i32 *x1)
{
  int i, got = 0;
  i32 lo = 0x7FFFFFFF, hi = -0x7FFFFFFF;
  for (i = 0; i < n; i++) {
    i32 ax = xy[2 * i], ay = xy[2 * i + 1], bx = xy[2 * ((i + 1) % n)], by = xy[2 * ((i + 1) % n) + 1], t;
    i32 sy0, sy1, px0, px1;
    if (ay > by) { t = ay; ay = by; by = t; t = ax; ax = bx; bx = t; }
    if (by < y0 || ay > y1) continue;
    /* the edge clipped to the band: its ends' x */
    sy0 = ay < y0 ? y0 : ay; sy1 = by > y1 ? y1 : by;
    px0 = by == ay ? ax : ax + (i32)((long long)(bx - ax) * (sy0 - ay) / (by - ay));
    px1 = by == ay ? bx : ax + (i32)((long long)(bx - ax) * (sy1 - ay) / (by - ay));
    if (px0 < lo) lo = px0;
    if (px1 < lo) lo = px1;
    if (px0 > hi) hi = px0;
    if (px1 > hi) hi = px1;
    got = 1;
  }
  *x0 = lo; *x1 = hi;
  return got;
}
/* see layout.h. Word's wrap polygons are in a 21600 x 21600 square over the object, so they are
   scaled to its size, and one with more than max points is sampled evenly. An open shape (a line)
   with only its two ends falls back to the box */
int obj_outline(const Image *im, i32 *xy, int max)
{
  int n = 0, i, closed = 1;
  if (im->npoly >= 3) {
    /* its wrap outline: every point if there's room, else spread evenly */
    for (i = 0; i < max && i < im->npoly; i++) {
      int k = im->npoly <= max ? i : (int)((long)i * im->npoly / max);
      obj_point(im, (i32)((long long)im->poly[2 * k] * im->cx / 21600), (i32)((long long)im->poly[2 * k + 1] * im->cy / 21600), &xy[2 * n], &xy[2 * n + 1]);
      n++;
    }
    return n;
  }
  if (im->kind == OBJ_SHAPE) {
    i32 pts[2 * SHAPE_MAXPTS];
    u32 m = shape_points(im->shape, im->cx, im->cy, pts, SHAPE_MAXPTS, &closed);
    for (i = 0; i < (int)m && n < max; i++, n++) obj_point(im, pts[2 * i], pts[2 * i + 1], &xy[2 * n], &xy[2 * n + 1]);
    if (closed || n > 2) return n;
    n = 0;
  }
  /* its box (turned) */
  if (max < 4) return 0;
  obj_point(im, 0, 0, &xy[0], &xy[1]); obj_point(im, im->cx, 0, &xy[2], &xy[3]);
  obj_point(im, im->cx, im->cy, &xy[4], &xy[5]); obj_point(im, 0, im->cy, &xy[6], &xy[7]);
  return 4;
}
/* the room for a line at y (from the paragraph's top), est high, within left..right (from the text
   area's left), round the floating objects in sh_fb: its start and width; *gap: how far it must go
   down first. Takes the widest gap at least half an inch wide; where top-and-bottom wrapping or no
   room blocks the line it goes below the objects (8 tries at most) */
static void shape_line(i32 y, i32 est, i32 left, i32 right, i32 *start, i32 *room, i32 *gap)
{
  int tries;
  *gap = 0; *start = left; *room = right - left;
  for (tries = 0; tries < 8; tries++) {
    i32 segs[16][2], ns = 1, k, best = -1, py = sh_y0 + y + *gap, below = 0x3FFFFFFF;
    u32 i;
    int blocked = 0;
    segs[0][0] = left; segs[0][1] = right;
    for (i = 0; i < sh_nfb; i++) {
      const FloatBox *f = &sh_fb[i];
      const Image *im = &sh_doc->images[f->img];
      i32 t = f->y - im->dt, b = f->y + f->h + im->db, x0, x1;
      if (f->page != sh_page || (f->wrap != WRAP_SQUARE && f->wrap != WRAP_TIGHT && f->wrap != WRAP_TOPBOTTOM)) continue;
      if (b <= py || t >= py + est) continue;
      if (b - (py) < below) below = b - py;
      if (f->wrap == WRAP_TOPBOTTOM) { blocked = 1; continue; }
      x0 = f->x - im->dl - sh_x0; x1 = f->x + f->w + im->dr - sh_x0;
      if (f->wrap == WRAP_TIGHT) {
        /* tight: round the outline, as far as it reaches across in the line's height */
        i32 xy[2 * 128], a, c;
        int n = obj_outline(im, xy, 128), j;
        for (j = 0; j < n; j++) { xy[2 * j] += f->x; xy[2 * j + 1] += f->y; }
        if (n < 3 || !band_span(xy, n, py - im->dt, py + est + im->db, &a, &c)) continue;
        x0 = a - im->dl - sh_x0; x1 = c + im->dr - sh_x0;
      }
      /* take x0..x1 out of every segment */
      for (k = 0; k < ns; k++) {
        if (x1 <= segs[k][0] || x0 >= segs[k][1]) continue;
        if (x0 > segs[k][0] && x1 < segs[k][1] && ns < 16) {            /* split in two */
          segs[ns][0] = x1; segs[ns][1] = segs[k][1]; ns++;
          segs[k][1] = x0;
        } else if (x0 <= segs[k][0]) segs[k][0] = x1 < segs[k][1] ? x1 : segs[k][1];
        else segs[k][1] = x0;
      }
    }
    if (!blocked) for (k = 0; k < ns; k++) if (segs[k][1] - segs[k][0] >= 720 && (best < 0 || segs[k][1] - segs[k][0] > segs[best][1] - segs[best][0])) best = k;
    if (best >= 0) { *start = segs[best][0]; *room = segs[best][1] - segs[best][0]; return; }
    if (below >= 0x3FFFFFFF) return;              /* nothing in the way after all */
    *gap += below > 0 ? below : 20;               /* no room: below the objects */
  }
}
/* lay paragraph p out into pl, avail twips wide (the column's or cell's text width): its boxes, lines
   broken at spaces (or hyphenated, or a word too long for any line cut), tab stops, the list label,
   a drop cap, alignment and line heights. notebase and enotebase are the notes before it, for their
   numbers. Replaces whatever pl held. Box x and w are worked out in fine units (QFINE per twip) and
   rounded to twips line by line, so rounding doesn't add up along a line. With sh_on set, the lines
   go round floating objects. 0 if out of memory (pl then has no lines) */
/* (WP10) Distributed alignment: the room left on a line (extra, twips) spread over its characters
   (boxes ls..lend, trailing spaces left out; i: the line's end), the last line's too, as Word does:
   each character gets the same extra spacing after it, what is left over goes to the spaces */
static void wp10_distribute(Para *p, Work *w, u32 ls, u32 lend, u32 i, i32 extra, Line *ln)
{
  u32 k, chars = 0, nsp = 0, done = 0, lastt = i;
  i32 per, rem, add = 0;
  for (k = ls; k < lend; k++) {
    Box *b = &w->b[k];
    if (b->kind == BX_TEXT) { u32 o; for (o = b->off; o < b->off + b->len; o = (u32)utf8_next(p->t, b->off + b->len, o)) chars++; lastt = k; }
    else if (b->kind == BX_SPACE) { chars += b->nspace; nsp += b->nspace; }
  }
  if (chars < 2) return;
  per = extra / (i32)(chars - 1);                   /* (the last character's spacing falls past the line's end) */
  if (per > 2000) per = 2000;
  rem = extra - per * (i32)(chars - 1);
  for (k = ls; k < i; k++) {
    Box *b = &w->b[k];
    w->b[k].x += add;
    if (k >= lend) continue;
    if (b->kind == BX_TEXT) {
      u32 o, n = 0;
      for (o = b->off; o < b->off + b->len; o = (u32)utf8_next(p->t, b->off + b->len, o)) n++;
      b->f.spacing = (i16)(b->f.spacing + per);
      if (k == lastt) n--;                          /* (the last character's spacing is past the line's end) */
      b->w += per * (i32)n; add += per * (i32)n;
    } else if (b->kind == BX_SPACE) {
      i32 grow = per * b->nspace;
      if (nsp) { i32 before = rem * (i32)done / (i32)nsp; done += b->nspace; grow += rem * (i32)done / (i32)nsp - before; }
      b->w += grow; add += grow;
    }
  }
  ln->width += add;
}
static int layout_para(Layout *L, ParaLay *pl, Para *p, i32 avail, const char *label, u32 notebase, u32 enotebase)
{
  Work w;
  u32 i, ls = 0;                    /* first box of the current line */
  i32 dropw = 0;                    /* a drop cap's width (and the gap after it), twips */
  u32 droplines = 0;
  const ParaFmt *pf = &p->pf;
  memset(&w, 0, sizeof w);
  pl_free(pl);
  pl->p = p; pl->ver = p->ver; pl->avail = avail; pl->notebase = notebase; pl->enotebase = enotebase; pl->hyph = L->d->hyphenate;
  pl->shaped = 0;
  { u32 r; pl->hidden = 2; for (r = 0; r < p->nr; r++) if (p->r[r].f.fx2 & FX2_HIDDEN) pl->hidden = L->showhidden; }   /* (WP9) */
  pl->lay = L;                      /* (WP22b) */
  strncpy(pl->label, label, sizeof pl->label - 1); pl->label[sizeof pl->label - 1] = 0;
  pl->labelw = 0;
  if (label[0]) {
    FaceSize *z = lay_face(L, &p->r[0].f);
    pl->labelw = z ? (i32)((fonts_text(z, label, (u32)strlen(label)) + QFINE / 2) / QFINE) : 240;
  }
  if (!make_boxes(L, p, &w, notebase, enotebase)) goto oom;
  /* a drop cap: the first letter in its own box, big enough to reach down dropcap lines; the
     lines beside it start after it (or it sits in the margin) */
  if (pf->dropcap >= 2 && w.n && w.b[0].kind == BX_TEXT && !in_table(pf)) {
    u32 cl = (u32)utf8_next(p->t, w.b[0].off + w.b[0].len, w.b[0].off) - w.b[0].off;
    FaceSize *z0 = lay_face(L, &w.b[0].f), *zc;
    i32 h0 = z0 ? z0->height : 276;
    if (pf->linerule == LS_AUTO && pf->line > 0) h0 = h0 * pf->line / 240;
    else if (pf->linerule != LS_AUTO && pf->line > h0) h0 = pf->line;
    if (cl < w.b[0].len && !split_box(L, p, &w, 0, measure(L, z0, p->t + w.b[0].off, cl, &w.b[0].f))) goto oom;
    {
      /* capitals are about 0.7 of the size: big enough that one spans the lines' baselines and a capital's height */
      long em = ((long)(pf->dropcap - 1) * h0 * 10 / 7) + (long)w.b[0].f.size * 10;
      Box *b = &w.b[0];
      b->kind = BX_DROP;
      b->f.size = (u16)(em / 10 > 2000 ? 2000 : em / 10);
      b->f.flags &= (u8)~(CF_SUPER | CF_SUB);
      zc = lay_face(L, &b->f);
      b->w = zc ? (i32)measure(L, zc, p->t + b->off, b->len, &b->f) : 0;
      dropw = (b->w + QFINE / 2) / QFINE + 60;
      droplines = pf->dropcap;
    }
  }
  i = 0;
  for (;;) {
    Line ln;
    i32 left = pf->left + (w.nl == 0 ? pf->first : 0) + (w.nl < droplines && !pf->dropmargin ? dropw : 0), room = avail - left - pf->right, gap = 0;
    u32 x = 0, word = ls, k, lastend;
    if (col_on) {
      /* (WP22a) the line as wide as its column */
      i32 est = w.nl ? w.l[w.nl - 1].height : 276;
      if (!w.nl && ls < w.n) { FaceSize *z = lay_face(L, &w.b[ls].f); if (z) est = z->height; }
      room = col_room(w.l, w.nl, est) - left - pf->right;
      pl->shaped = 1;
    }
    if (sh_on) {
      /* floating objects alongside: the line in the room they leave */
      i32 y = 0, est, st, rm;
      u32 q;
      for (q = 0; q < w.nl; q++) y += w.l[q].height;
      est = w.nl ? w.l[w.nl - 1].height : 276;
      if (!w.nl && ls < w.n) { FaceSize *z = lay_face(L, &w.b[ls].f); if (z) est = z->height; }
      shape_line(y, est, left, avail - pf->right, &st, &rm, &gap);
      if (st != left || rm != room || gap) pl->shaped = 1;
      left = st; room = rm;
    }
    /* a list label opens the first line; the text starts at the hanging indent (an implicit tab
       stop), or the next tab stop if the label is wider */
    if (w.nl == 0 && pl->label[0]) {
      i32 hang = pf->first < 0 ? -pf->first : 0;
      x = (u32)((pl->labelw < hang ? hang : next_tab(pf, pl->labelw + pf->first)) * QFINE);
      if ((i32)(x / QFINE) >= room) x = (u32)(pl->labelw * QFINE);
    }
    int brk = 0;
    if (room < 720) room = 720;
    memset(&ln, 0, sizeof ln);
    /* fill the line: words that fit; a word too long for any line is cut */
    for (i = ls; i < w.n; i++) {
      Box *b = &w.b[i];
      if (LAY_HIDDEN(L, b)) { b->x = (i32)x; b->w = 0; continue; }                    /* (WP9: hidden text takes no room) */
      if (b->kind == BX_DROP) { b->x = -dropw * QFINE; word = i + 1; continue; }     /* before the line's start */
      if (b->kind == BX_FLOAT) { b->x = (i32)x; continue; }                            /* (placed apart from the text) */
      if (b->kind == BX_SPACE) { b->x = (i32)x; x += b->w; word = i + 1; continue; }
      if (b->kind == BX_BREAK) { b->x = (i32)x; i++; brk = 2; break; }     /* a line break ends the line */
      if (b->kind == BX_MATH) {                 /* WP14a: an equation, as a picture; a display one has a line of its own */
        if (i > ls && (b->nspace || x + b->w > (u32)room * QFINE)) { brk = 1; break; }
        b->x = (i32)x; x += b->w; word = i + 1;
        if (b->nspace) { i++; brk = 2; break; }
        continue;
      }
      if (b->kind == BX_PIC) {                  /* a picture: a line may break before and after it */
        if (x + b->w > (u32)room * QFINE && i > ls) { brk = 1; break; }
        b->x = (i32)x; x += b->w; word = i + 1;
        continue;
      }
      if (b->kind == BX_TAB) {
        int kind, lead;
        i32 tx = next_stop(pf, (i32)(x / QFINE), &kind, &lead) * QFINE;
        /* right, centred and decimal stops: the text after the tab ends at the stop (or its middle, or its point) */
        if (kind != TAB_LEFT) {
          i32 sw = tab_segment(L, p, &w, i + 1, kind == TAB_DECIMAL);
          tx -= kind == TAB_CENTER ? sw / 2 : sw;
          if (tx < (i32)x) tx = (i32)x;
        }
        b->x = (i32)x;
        b->w = tx - (i32)x;
        b->nspace = (u8)lead;                   /* a tab's leader, for drawing */
        if (tx > room * QFINE && i > ls) { brk = 1; i = word > ls ? word : i; break; }
        x = (u32)tx; word = i + 1;
        continue;
      }
      if (x + b->w > (u32)room * QFINE) {
        if (word == i && hyphen_break(L, p, &w, i, x, (u32)room * QFINE)) {   /* (WP10: at an optional or ordinary hyphen) */
          b = &w.b[i];
          b->x = (i32)x; x += b->w;
          i++; brk = 1;
          break;
        }
        if (word == i && L->d->hyphenate && hyph_ready() && hyphenate(L, p, &w, i, x, (u32)room * QFINE)) {
          b = &w.b[i];                                         /* hyphenated: its first part ends the line */
          b->x = (i32)x; x += b->w;
          i++; brk = 1;
          break;
        }
        if (word > ls) { i = word; brk = 1; break; }           /* the word goes to the next line */
        /* the line holds only this word: cut it */
        if (b->kind == BX_TEXT && !split_box(L, p, &w, i, (u32)room * QFINE - x)) goto oom;
        b = &w.b[i];
        b->x = (i32)x; x += b->w;
        i++; brk = 1;
        break;
      }
      b->x = (i32)x; x += b->w;
    }
    ln.box0 = ls; ln.nbox = i - ls; ln.left = left;
    ln.start = ls < w.n ? w.b[ls].off : p->len;
    ln.end = i < w.n ? w.b[i].off : p->len;
    /* width without trailing spaces */
    lastend = ln.nbox;
    while (lastend && (w.b[ls + lastend - 1].kind == BX_SPACE || w.b[ls + lastend - 1].kind == BX_BREAK)) lastend--;
    ln.width = lastend ? (w.b[ls + lastend - 1].x + w.b[ls + lastend - 1].w + QFINE / 2) / QFINE : 0;
    /* fine -> twips, then alignment */
    for (k = ls; k < i; k++) { w.b[k].x = (w.b[k].x + QFINE / 2) / QFINE; w.b[k].w = (w.b[k].w + QFINE / 2) / QFINE; }
    {
      i32 extra = room - ln.width, shift = 0;
      if (pf->align == AL_CENTER) shift = extra / 2;
      else if (pf->align == AL_RIGHT) shift = extra;
      else if (pf->align == AL_JUSTIFY && brk == 1 && extra > 0) {      /* not before a line break, as Word */
        /* spread the extra room over the spaces between words (not trailing ones, nor before a tab) */
        u32 nsp = 0, done = 0, lasttab = ls;
        i32 add = 0;
        for (k = ls; k < ls + lastend; k++) if (w.b[k].kind == BX_TAB) lasttab = k + 1;
        for (k = lasttab; k < ls + lastend; k++) if (w.b[k].kind == BX_SPACE) nsp += w.b[k].nspace;
        if (nsp) for (k = lasttab; k < i; k++) {
          w.b[k].x += add;
          if (w.b[k].kind == BX_SPACE && k < ls + lastend) {
            i32 grow;
            done += w.b[k].nspace;
            grow = (i32)((extra * (i32)done) / (i32)nsp) - (add);
            w.b[k].w += grow; add += grow;
          }
        }
        if (nsp) ln.width = room;
      }
      else if (pf->align == AL_DISTRIBUTE && extra > 0) wp10_distribute(p, &w, ls, ls + lastend, i, extra, &ln);   /* (WP10) */
      if (ln.nbox && w.b[ls].kind == BX_MATH && w.b[ls].nspace) shift = math_line_shift(L->d, math_index(p->t + w.b[ls].off), extra);   /* WP14a */
      if (shift > 0) ln.left += shift;      /* centred and right-aligned lines start further in */
    }
    line_metrics(L, p, &w, &ln);
    if (gap) { ln.height += gap; ln.ascent += gap; }       /* below floating objects in its way */
    if (!add_line(&w, &ln)) goto oom;
    if (i >= w.n) {
      if (w.n && w.b[w.n - 1].kind == BX_BREAK) {         /* a break at the end: an empty last line */
        memset(&ln, 0, sizeof ln);
        ln.box0 = w.n; ln.left = pf->left; ln.start = ln.end = p->len;
        line_metrics(L, p, &w, &ln);
        if (!add_line(&w, &ln)) goto oom;
      }
      break;
    }
    ls = i;
  }
  if (droplines && w.nl && w.nl < droplines) w.l[w.nl - 1].height += (i32)(droplines - w.nl) * w.l[0].height;   /* room for the cap below */
  pl->boxes = w.b; pl->nboxes = w.n;
  pl->lines = w.l; pl->nlines = w.nl;
  return 1;
oom:
  free(w.b); free(w.l);
  pl->nlines = 0;
  return 0;
}

/* ------------------------------------------------------------------ */
/* the document: reuse unchanged paragraphs' layouts (found by pointer), then paginate */
/* append a placed line (no line number yet); 0 if out of memory */
static int add_placed(Layout *L, u32 para, u32 line, u32 page, i32 y, i32 x, i32 w)
{
  Placed *pc;
  if (L->nplaced >= L->placedcap) {
    u32 nc = L->placedcap ? L->placedcap * 2 : 256;
    Placed *np = (Placed *)realloc(L->placed, nc * sizeof(Placed));
    if (!np) return 0;
    L->placed = np; L->placedcap = nc;
  }
  pc = &L->placed[L->nplaced++];
  pc->para = para; pc->line = line; pc->page = page; pc->y = y; pc->x = x; pc->w = w; pc->lnum = 0; pc->lnd = 0;
  return 1;
}
/* append a cell box at x, y on page (twips from the page's top left), w x h, with ce's borders and
   fill; NULL if out of memory. The pointer is into L->cells, so it moves with the next add */
static CellBox *add_cell(Layout *L, u32 page, i32 x, i32 y, i32 w, i32 h, const Cell *ce)
{
  int k;
  CellBox *c;
  if (L->ncells >= L->cellcap) {
    u32 nc = L->cellcap ? L->cellcap * 2 : 64;
    CellBox *n = (CellBox *)realloc(L->cells, nc * sizeof(CellBox));
    if (!n) return NULL;
    L->cells = n; L->cellcap = nc;
  }
  c = &L->cells[L->ncells++];
  c->page = page; c->x = x; c->y = y; c->w = w; c->h = h; c->borders = ce->borders; c->shade = ce->shade; c->depth = 0;
  c->table = ce->table; c->row = ce->row; c->col = ce->col; c->span = ce->span;
  for (k = 0; k < 4; k++) { c->bstyle[k] = ce->bstyle[k]; c->bwidth[k] = ce->bwidth[k]; c->bcolor[k] = ce->bcolor[k]; }
  return c;
}
/* the height of lines [from, to) of a paragraph (its space before and after not included) */
static i32 para_height(ParaLay *pl, u32 from, u32 to)
{
  i32 h = 0;
  u32 k;
  for (k = from; k < to && k < pl->nlines; k++) h += pl->lines[k].height;
  return h;
}
/* a table's left edge from its column's (avail wide): its indent, or where centring or right
   alignment puts it */
static i32 table_x(const Table *t, i32 avail)
{
  i32 sum = 0;
  int k;
  if (!t->jc) return t->indent;
  for (k = 0; k < t->ncols; k++) sum += t->grid[k];
  return t->jc == AL_CENTER ? (avail - sum) / 2 : avail - sum;
}
/* a table cell's columns: its left edge from the text area's left (in a column avail wide), and its width */
static void cell_geom(Doc *d, const ParaFmt *pf, i32 avail, i32 *x, i32 *w)
{
  Table *t = pf ? doc_table(d, pf) : NULL;
  int k;
  *x = 0; *w = 0;
  if (!t) return;
  *x = table_x(t, avail);
  for (k = 0; k < pf->cell.col && k < t->ncols; k++) *x += t->grid[k];
  for (; k < pf->cell.col + pf->cell.span && k < t->ncols; k++) *w += t->grid[k];
  if (*w < 240) *w = 240;
}
/* the width a paragraph is laid out in */
static i32 para_avail(Doc *d, const ParaFmt *pf, i32 body)
{
  Table *t = doc_table(d, pf);
  i32 x, w;
  if (!t) return body;
  cell_geom(d, pf, body, &x, &w);
  w -= t->marl + t->marr;
  return w < 240 ? 240 : w;
}
/* where the text goes next: the page, the column (sections may have several), y */
typedef struct {
  u32 page; i32 y; int at_top;          /* at_top: at a column's top, nothing placed in it yet */
  i32 top, bottom;                      /* the text area's, on every page */
  const SectFmt *sec;                   /* the section being placed */
  int col, ncols;
  i32 colx, colw, gap;                  /* the column's left edge from the text area's, its width, the gap between */
  i32 coltop, maxy;                     /* where this page's columns start; the lowest any reached */
  u32 pagefirst, cellfirst, sepfirst;   /* this page's first placed line, cell box, column line */
  u32 lnum;                             /* line numbers so far */
  int nofinish;                         /* the page isn't complete: don't align it */
  int last;                             /* the section is the document's last */
  u32 sfirst;                           /* the section's first page */
  /* WP22a: */
  const PageSetup *srcpg;               /* the section's page setup (L->pg: as this page has it, core/pages.c) */
  i32 cx[MAX_SECT_COLS], cw[MAX_SECT_COLS];   /* each column's left edge and width */
  int unequal;                          /* columns of different widths */
  u32 regfirst;                         /* the first line placed in these columns on this page */
} Flow;
static i32 furniture_extent(Layout *L, const PageSetup *pg, const SectFmt *sec, int last, int footer);
/* the page fl is on gets its page setup and section (as they are now) */
static int page_set(Layout *L, Flow *fl)
{
  if (fl->page >= L->pinfocap) {
    u32 nc = fl->page + 64;
    PageInfo *n = (PageInfo *)realloc(L->pinfo, nc * sizeof(PageInfo));
    if (!n) return 0;
    L->pinfo = n; L->pinfocap = nc;
  }
  if (fl->srcpg) L->pg = pages_eff(L, fl->srcpg, fl->sec, fl->page);      /* (WP22a: mirrored, halved...) */
  L->pinfo[fl->page].pg = L->pg; L->pinfo[fl->page].sec = fl->sec; L->pinfo[fl->page].first = fl->sfirst; L->pinfo[fl->page].last = (u8)fl->last;
  return 1;
}
/* a section's text area on its pages: its margins, or further in where a header or footer is taller */
static void flow_area(Layout *L, Flow *fl, const SectFmt *s, int web)
{
  const PageSetup *pg = L->pg;
  fl->top = pg->top; fl->bottom = pg->height - pg->bottom;
  if (web) fl->bottom = 0x3FFFFFFF;            /* web layout: one page as long as the text */
  else {
    i32 h = furniture_extent(L, pg, s, fl->last, 0), f = furniture_extent(L, pg, s, fl->last, 1);
    if (h > fl->top) fl->top = h;
    if (pg->height - f < fl->bottom) fl->bottom = pg->height - f;
  }
  if (fl->bottom - fl->top < 1440) fl->bottom = fl->top + 1440;
}
/* fl set up for section s's columns across body twips (their number, gap and width), starting in the
   first. Columns narrower than half an inch, and web layout, mean one column */
static void flow_cols(Flow *fl, const SectFmt *s, i32 body, int web)
{
  int k;
  fl->sec = s;
  /* (WP22a: columns of their own widths too) */
  fl->ncols = web ? (fl->cx[0] = 0, fl->cw[0] = body, 1) : pages_cols(s, body, fl->cx, fl->cw);
  fl->gap = fl->ncols > 1 ? fl->cx[1] - fl->cw[0] : 0;
  fl->unequal = 0;
  for (k = 1; k < fl->ncols; k++) if (fl->cw[k] != fl->cw[0]) fl->unequal = 1;
  fl->col = 0; fl->colx = 0; fl->colw = fl->cw[0];
  fl->regfirst = 0xFFFFFFFF;
}
/* the columns from coltop to the lowest one got: their lines (a section with lines between columns) */
static void end_region(Layout *L, Flow *fl)
{
  int c;
  if (fl->y > fl->maxy) fl->maxy = fl->y;
  if (!fl->sec || !fl->sec->colline || fl->ncols < 2 || fl->maxy <= fl->coltop) return;
  for (c = 1; c < fl->ncols; c++) {
    if (L->nseps >= L->sepcap) {
      u32 nc = L->sepcap ? L->sepcap * 2 : 16;
      ColSep *ns = (ColSep *)realloc(L->seps, nc * sizeof(ColSep));
      if (!ns) return;
      L->seps = ns; L->sepcap = nc;
    }
    L->seps[L->nseps].page = fl->page;
    L->seps[L->nseps].x = (fl->cx[c - 1] + fl->cw[c - 1] + fl->cx[c]) / 2;
    L->seps[L->nseps].y0 = fl->coltop; L->seps[L->nseps].y1 = fl->maxy;
    L->nseps++;
  }
}
static void justify_page(Layout *L, Flow *fl, i32 shift);    /* (WP22a) */
/* a full page done: its section may centre it, or put it at the bottom */
static void finish_page(Layout *L, Flow *fl)
{
  i32 shift;
  u32 k;
  if (fl->y > fl->maxy) fl->maxy = fl->y;
  if (fl->nofinish || !fl->sec || fl->sec->valign == VA_TOP || L->webw > 0 || fl->maxy >= fl->bottom) return;
  for (k = fl->pagefirst; k < L->nplaced; k++) {           /* footnotes at the bottom: left as it is */
    Placed *pc = &L->placed[k];
    ParaLay *pl = &L->pl[pc->para];
    u32 b;
    if (pc->page != fl->page) continue;
    for (b = pl->lines[pc->line].box0; b < pl->lines[pc->line].box0 + pl->lines[pc->line].nbox; b++) if (pl->boxes[b].kind == BX_NOTE) return;
  }
  shift = fl->bottom - fl->maxy;
  if (fl->sec->valign == VA_JUSTIFY) { justify_page(L, fl, shift); return; }     /* (WP22a) */
  if (fl->sec->valign == VA_CENTER) shift /= 2;
  for (k = fl->pagefirst; k < L->nplaced; k++) if (L->placed[k].page == fl->page) L->placed[k].y += shift;
  for (k = fl->cellfirst; k < L->ncells; k++) if (L->cells[k].page == fl->page) L->cells[k].y += shift;
  for (k = fl->sepfirst; k < L->nseps; k++) if (L->seps[k].page == fl->page) { L->seps[k].y0 += shift; L->seps[k].y1 += shift; }
  for (k = 0; k < L->nen; k++) if (L->en[k].page == fl->page) L->en[k].y += shift;
  for (k = 0; k < L->nenseps; k++) if (L->enseps[k].page == fl->page) L->enseps[k].y += shift;   /* (WP22b) */
  for (k = 0; k < L->nfloats; k++) if (L->floats[k].page == fl->page && L->d->images[L->floats[k].img].vrel == POS_PARA) L->floats[k].y += shift;
}
/* on to the top of the next page: the page just filled gets its column lines and vertical
   alignment, the new one its page setup, and line numbers counted by page start again */
static void new_page(Layout *L, Flow *fl)
{
  end_region(L, fl);
  finish_page(L, fl);
  fl->page++;
  if (!page_set(L, fl)) L->oom = 1;
  fl->y = fl->coltop = fl->maxy = fl->top; fl->at_top = 1;
  fl->col = 0; fl->colx = 0; fl->colw = fl->cw[0]; fl->regfirst = L->nplaced;
  fl->pagefirst = L->nplaced; fl->cellfirst = L->ncells; fl->sepfirst = L->nseps;
  if (fl->sec && fl->sec->linenum == LN_PAGE) fl->lnum = 0;
}
/* on to the next column, or the next page after the last */
static void flow_next(Layout *L, Flow *fl)
{
  if (fl->col + 1 < fl->ncols) {
    if (fl->y > fl->maxy) fl->maxy = fl->y;
    fl->col++;
    fl->colx = fl->cx[fl->col]; fl->colw = fl->cw[fl->col];
    fl->y = fl->coltop; fl->at_top = 1;
  } else new_page(L, fl);
}
/* (WP22a) a paragraph about to be placed in columns of different widths: laid out again (by
   place_floats) with each line as wide as its column */
static void col_shape(Flow *fl, ParaLay *pl)
{
  int k;
  if (in_table(&pl->p->pf)) return;
  col_on = 1; col_n = fl->ncols; col_c0 = fl->col; col_attop = fl->at_top;
  for (k = 0; k < fl->ncols; k++) col_w[k] = fl->cw[k];
  col_y0 = fl->y; col_top = fl->coltop; col_ptop = fl->top; col_bottom = fl->bottom;
  pl->shaped = 1;
}
/* (WP22a) the columns before a continuous section break balanced, as Word does: the lines placed in
   them on this page shared out so the columns end as level as they can (the shortest height they
   fit in, found by halving). Only for columns of one width with nothing but lines in them (no table
   cells, paragraph borders or floating objects anchored there) */
static void balance_cols(Layout *L, Flow *fl)
{
  u32 a = fl->regfirst, n, k;
  i32 lo, hi, maxh = 0, total = 0, *h, *g;
  if (fl->ncols < 2 || fl->unequal || a >= L->nplaced) return;
  n = L->nplaced - a;
  if (n < 2) return;
  for (k = a; k < L->nplaced; k++) if (L->placed[k].page != fl->page) return;
  for (k = fl->cellfirst; k < L->ncells; k++) if (L->cells[k].page == fl->page) return;
  for (k = 0; k < L->npbox; k++) if (L->pbox[k].page == fl->page && L->pbox[k].para >= L->placed[a].para) return;
  for (k = 0; k < L->nfloats; k++) if (L->floats[k].page == fl->page && L->floats[k].para >= L->placed[a].para) return;
  if (!(h = (i32 *)malloc(n * 2 * sizeof(i32)))) return;
  g = h + n;
  /* each line's height and the space above it (from where they are now; between paragraphs
     that came at a column's top, the space their formats ask for) */
  for (k = 0; k < n; k++) {
    const Placed *pc = &L->placed[a + k];
    h[k] = L->pl[pc->para].lines[pc->line].height;
    g[k] = 0;
    if (k) {
      const Placed *pp = &L->placed[a + k - 1];
      if (pp->x == pc->x) g[k] = pc->y - pp->y - h[k - 1];
      else if (pp->para != pc->para) g[k] = L->pl[pp->para].p->pf.after + L->pl[pc->para].p->pf.before;
      if (g[k] < 0) g[k] = 0;
    }
    if (h[k] > maxh) maxh = h[k];
    total += h[k] + g[k];
  }
  lo = maxh; hi = total;
  while (lo < hi) {
    i32 mid = lo + (hi - lo) / 2, y = 0;
    int c = 0;
    for (k = 0; k < n && c < fl->ncols; k++) {
      i32 gg = y ? g[k] : 0;
      if (y && y + gg + h[k] > mid) { c++; y = 0; gg = 0; }
      y += gg + h[k];
    }
    if (c < fl->ncols) hi = mid; else lo = mid + 1;
  }
  {
    i32 y = 0, low = 0;
    int c = 0;
    for (k = 0; k < n; k++) {
      Placed *pc = &L->placed[a + k];
      i32 gg = y ? g[k] : 0;
      if (y && y + gg + h[k] > lo && c + 1 < fl->ncols) { c++; y = 0; gg = 0; }
      y += gg;
      pc->x = fl->cx[c]; pc->w = fl->cw[c]; pc->y = fl->coltop + y;
      y += h[k];
      if (y > low) low = y;
    }
    fl->y = fl->maxy = fl->coltop + low;
  }
  free(h);
}
/* (WP22a) vertical alignment Justified: the room left at the foot of a page shared out between its
   paragraphs (between its lines if it has only one), as Word does; a page with more than one
   column is left at the top */
static void justify_page(Layout *L, Flow *fl, i32 shift)
{
  u32 k, gaps = 0, first = 0xFFFFFFFF, seen = 0;
  i32 x0 = 0, *at = NULL;
  int bypara = 0;
  for (k = fl->pagefirst; k < L->nplaced; k++) {
    const Placed *pc = &L->placed[k];
    if (pc->page != fl->page) continue;
    if (first == 0xFFFFFFFF) { first = k; x0 = pc->x; }
    else if (pc->x != x0) return;
    else if (pc->para != L->placed[k - 1].para) bypara = 1;
  }
  if (first == 0xFFFFFFFF) return;
  /* the tops of the lines that start a new paragraph (or every line after the first) */
  for (k = first + 1; k < L->nplaced; k++) if (L->placed[k].page == fl->page && (!bypara || L->placed[k].para != L->placed[k - 1].para)) gaps++;
  if (!gaps || !(at = (i32 *)malloc(gaps * sizeof(i32)))) return;
  for (k = first + 1; k < L->nplaced; k++)
    if (L->placed[k].page == fl->page && (!bypara || L->placed[k].para != L->placed[k - 1].para)) at[seen++] = L->placed[k].y;
  /* everything at or below the i'th gap moves down by i shares */
  {
    u32 i;
    for (k = fl->pagefirst; k < L->nplaced; k++) {
      Placed *pc = &L->placed[k];
      u32 m = 0;
      if (pc->page != fl->page) continue;
      for (i = 0; i < gaps && at[i] <= pc->y; i++) m = i + 1;
      pc->y += (i32)((long long)shift * m / gaps);
    }
    for (k = fl->cellfirst; k < L->ncells; k++) {
      CellBox *c = &L->cells[k];
      u32 m = 0;
      if (c->page != fl->page) continue;
      for (i = 0; i < gaps && at[i] <= c->y; i++) m = i + 1;
      c->y += (i32)((long long)shift * m / gaps);
    }
    for (k = 0; k < L->npbox; k++) {
      ParaBox *b = &L->pbox[k];
      u32 m = 0;
      if (b->page != fl->page) continue;
      for (i = 0; i < gaps && at[i] <= b->y0 + 1; i++) m = i + 1;
      b->y0 += (i32)((long long)shift * m / gaps); b->y1 += (i32)((long long)shift * m / gaps); b->ybetween += (i32)((long long)shift * m / gaps);
    }
    for (k = 0; k < L->nen; k++) {
      u32 m = 0;
      if (L->en[k].page != fl->page) continue;
      for (i = 0; i < gaps && at[i] <= L->en[k].y; i++) m = i + 1;
      L->en[k].y += (i32)((long long)shift * m / gaps);
    }
    for (k = 0; k < L->nfloats; k++) {
      FloatBox *f = &L->floats[k];
      u32 m = 0;
      if (f->page != fl->page || L->d->images[f->img].vrel != POS_PARA) continue;
      for (i = 0; i < gaps && at[i] <= f->y; i++) m = i + 1;
      f->y += (i32)((long long)shift * m / gaps);
    }
  }
  free(at);
}
/* a row of a table ran onto later pages by itself: the page bookkeeping catches up */
static void flow_jumped(Layout *L, Flow *fl, u32 page)
{
  u32 k = L->nplaced;
  end_region(L, fl);
  while (fl->page < page) { fl->page++; if (!page_set(L, fl)) L->oom = 1; }
  fl->col = 0; fl->colx = 0; fl->colw = fl->cw[0]; fl->coltop = fl->maxy = fl->top;
  while (k > 0 && L->placed[k - 1].page == page) k--;
  fl->pagefirst = k; fl->cellfirst = L->ncells; fl->sepfirst = L->nseps; fl->regfirst = k;
}

/* one table, from paragraph *pi: row by row. A row that doesn't fit runs on to the next page (its
   cells' lines going on independently, below the heading rows again), unless it mustn't break, or
   not even a line of it fits: then it starts the next page. A cell may hold a table of its own (one
   level deep): its rows go down the cell, each starting the next page if it doesn't fit. Returns 0
   if out of memory, and stops at a paragraph not laid out yet (*pi left there) */
#define CELLS_ROW 64
typedef struct {
  u32 cs[CELLS_ROW], ce[CELLS_ROW];     /* each cell's paragraphs */
  i32 ch[CELLS_ROW];                    /* the height of each one's content (with the margins) */
  i32 first;                            /* the most any cell needs for its first line */
  int nc;
  i32 rowh;
  u32 next;                             /* the next row's first paragraph */
} RowM;
static int measure_row(Layout *L, u32 i, Table *t, u16 tid, RowM *m);
/* the height of a cell's content, paragraphs [j0, j1) of table tid's cell (tables nested in it
   included); its first line's (or nested row's) height in *first. -1 if something isn't laid out */
static i32 block_height(Layout *L, u32 j0, u32 j1, u16 tid, i32 *first)
{
  u32 j = j0;
  i32 h = 0;
  *first = 0;
  while (j < j1) {
    ParaLay *pl = &L->pl[j];
    const ParaFmt *pf = &pl->p->pf;
    if (!pl->laid) return -1;
    if (pf->outer.table == tid && pf->cell.table) {          /* a table in the cell */
      u16 inner = pf->cell.table;
      Table *it = doc_table(L->d, pf);
      while (j < j1 && L->pl[j].p->pf.cell.table == inner) {
        RowM m;
        if (!it || !measure_row(L, j, it, inner, &m)) return -1;
        if (j == j0 || !*first) { if (!*first) *first = m.rowh; }
        h += m.rowh;
        j = m.next;
      }
      continue;
    }
    if (!*first && pl->nlines) *first = pf->before + pl->lines[0].height;
    h += pf->before + para_height(pl, 0, pl->nlines) + pf->after;
    j++;
  }
  return h;
}
/* measure the row of table tid from paragraph i; 0 if a paragraph isn't laid out yet */
static int measure_row(Layout *L, u32 i, Table *t, u16 tid, RowM *m)
{
  const Cell *rc = cell_in(&L->pl[i].p->pf, tid);
  u16 row = rc->row;
  u32 j;
  int c;
  m->nc = 0; m->rowh = 0; m->first = 0;
  for (j = i; j < L->npl; j++) {
    const Cell *cc = cell_in(&L->pl[j].p->pf, tid);
    if (!cc || cc->row != row) break;
    if (!L->pl[j].laid) return 0;
    if (m->nc == 0 || (!(cc->col == cell_in(&L->pl[j - 1].p->pf, tid)->col) && m->nc < CELLS_ROW)) { m->cs[m->nc] = j; m->nc++; }
    m->ce[m->nc - 1] = j + 1;
  }
  m->next = j;
  for (c = 0; c < m->nc; c++) {
    i32 f, h = block_height(L, m->cs[c], m->ce[c], tid, &f);
    if (h < 0) return 0;
    m->ch[c] = t->mart + t->marb + h;
    if (t->mart + f > m->first) m->first = t->mart + f;
    if (m->ch[c] > m->rowh) m->rowh = m->ch[c];
  }
  {
    i16 rh = rc->rowh;
    if (rh > 0 && m->rowh < rh) m->rowh = rh;
    else if (rh < 0) m->rowh = -rh;
  }
  return 1;
}
/* a nested table's row at (x: its table's left edge, page, y): cells side by side, lines running on
   to later pages when the row is taller than what's left; boxes for its cells. The row's end in
   *endpage, *endy. 0 out of memory */
static int place_inner_row(Layout *L, u32 i, Table *t, u16 tid, i32 tx0, u32 page, i32 y, i32 top, i32 bottom, i32 *last, int dup, u32 *endpage, i32 *endy, u32 *next)
{
  RowM m;
  int c, k;
  u32 j, pg;
  if (!measure_row(L, i, t, tid, &m)) return 2;
  *endpage = page; *endy = y + m.rowh;
  for (c = 0; c < m.nc; c++) {
    const ParaFmt *pf = &L->pl[m.cs[c]].p->pf;
    i32 cx = tx0, cw = 0, cy = y + t->mart;
    u32 cpage = page;
    for (k = 0; k < pf->cell.col && k < t->ncols; k++) cx += t->grid[k];
    for (; k < pf->cell.col + pf->cell.span && k < t->ncols; k++) cw += t->grid[k];
    if (cw < 240) cw = 240;
    if (pf->cell.valign && m.rowh > m.ch[c] && y + m.rowh <= bottom) cy += pf->cell.valign == 1 ? (m.rowh - m.ch[c]) / 2 : m.rowh - m.ch[c];
    for (j = m.cs[c]; j < m.ce[c]; j++) {
      ParaLay *pl = &L->pl[j];
      u32 ln;
      cy += pl->p->pf.before;
      if (!dup) pl->placed0 = L->nplaced;
      for (ln = 0; ln < pl->nlines; ln++) {
        i32 h = pl->lines[ln].height;
        if (cy + h > bottom && cy > top) { cpage++; cy = top; }
        if (!add_placed(L, j, ln, cpage, cy, cx + t->marl, cw - t->marl - t->marr)) return 0;
        cy += h;
      }
      cy += pl->p->pf.after;
    }
    cy += t->marb;
    if (cpage > *endpage || (cpage == *endpage && cy > *endy)) { *endy = cy; *endpage = cpage; }
  }
  for (c = 0; c < m.nc; c++) {
    const Cell *ce0 = &L->pl[m.cs[c]].p->pf.cell;
    i32 cx = tx0 + L->pg->left, cw = 0;
    CellBox *b;
    for (k = 0; k < ce0->col && k < t->ncols; k++) cx += t->grid[k];
    for (; k < ce0->col + ce0->span && k < t->ncols; k++) cw += t->grid[k];
    if (*endpage == page) {
      CellBox *above = ce0->vmerge == VM_CONT && ce0->col < MAX_COLS && last[ce0->col] >= 0 ? &L->cells[last[ce0->col]] : NULL;
      if (above && above->page == page && above->y + above->h == y) {
        above->h += *endy - y;
        above->borders = (u8)((above->borders & ~BD_BOTTOM) | (ce0->borders & BD_BOTTOM));
        above->bstyle[E_BOTTOM] = ce0->bstyle[E_BOTTOM]; above->bwidth[E_BOTTOM] = ce0->bwidth[E_BOTTOM]; above->bcolor[E_BOTTOM] = ce0->bcolor[E_BOTTOM];
        continue;
      }
      if (!(b = add_cell(L, page, cx, y, cw, *endy - y, ce0))) return 0;
      b->depth = 1;
      if (ce0->col < MAX_COLS) last[ce0->col] = (i32)L->ncells - 1;
    } else {
      for (pg = page; pg <= *endpage; pg++) {
        i32 y0 = pg == page ? y : top, y1 = pg == *endpage ? *endy : bottom;
        if (!(b = add_cell(L, pg, cx, y0, cw, y1 - y0, ce0))) return 0;
        b->depth = 1;
      }
      if (ce0->col < MAX_COLS) last[ce0->col] = -1;
    }
  }
  *next = m.next;
  return 1;
}
/* a cell's content (paragraphs [j0, j1) of table tid's cell) from (page, y), in the column x..x+w
   (from the text area's left): lines, and the rows of tables in it. top2: the top of a later page
   (below repeated heading rows). Its end in *endpage, *endy. 0 out of memory, 2 not laid out */
static int place_block(Layout *L, u32 j0, u32 j1, u16 tid, i32 x, i32 w, u32 page, i32 y, i32 top, i32 top2, i32 bottom, int dup, u32 *endpage, i32 *endy)
{
  u32 j = j0, page0 = page;
  while (j < j1) {
    ParaLay *pl = &L->pl[j];
    const ParaFmt *pf = &pl->p->pf;
    if (pf->outer.table == tid && pf->cell.table) {
      u16 inner = pf->cell.table;
      Table *it = doc_table(L->d, pf);
      i32 last[MAX_COLS], tx0;
      int k;
      if (!it) { j++; continue; }
      for (k = 0; k < MAX_COLS; k++) last[k] = -1;
      tx0 = x + table_x(it, w);
      while (j < j1 && L->pl[j].p->pf.cell.table == inner) {
        RowM m;
        u32 ep, nx;
        i32 ey;
        int r;
        if (!measure_row(L, j, it, inner, &m)) return 2;
        /* a row that doesn't fit starts the next page (unless it's at the top of one already) */
        if (y + m.rowh > bottom && y > (page == page0 ? top : top2)) {
          page++; y = top2;
          for (k = 0; k < MAX_COLS; k++) last[k] = -1;
        }
        r = place_inner_row(L, j, it, inner, tx0, page, y, top2, bottom, last, dup, &ep, &ey, &nx);
        if (r != 1) return r;
        page = ep; y = ey; j = nx;
      }
      continue;
    }
    y += pf->before;
    if (!dup) pl->placed0 = L->nplaced;
    {
      u32 ln;
      for (ln = 0; ln < pl->nlines; ln++) {
        i32 h = pl->lines[ln].height;
        if (y + h > bottom && y > top && y > top2) { page++; y = top2; }
        if (!add_placed(L, j, ln, page, y, x, w)) return 0;
        y += h;
      }
    }
    y += pf->after;
    j++;
  }
  *endpage = page; *endy = y;
  return 1;
}
static int place_row(Layout *L, u32 i, Flow *fl, Table *t, u16 tid, i32 *last, int dup, u32 hdr_from, u32 hdr_end, u32 *next);
/* the table's heading rows at the top of page pg (a row running on to it) */
static int place_headers(Layout *L, Flow *fl, u32 pg, Table *t, u16 tid, u32 hdr_from, u32 hdr_end)
{
  Flow save = *fl;
  i32 last[MAX_COLS];
  u32 h = hdr_from;
  int k, r;
  for (k = 0; k < MAX_COLS; k++) last[k] = -1;
  fl->page = pg; fl->y = fl->top; fl->at_top = 1;
  while (h < hdr_end) { r = place_row(L, h, fl, t, tid, last, 1, 0, 0, &h); if (r != 1) { *fl = save; return r; } }
  *fl = save;
  return 1;
}
/* one table row from paragraph i (dup: a heading row placed again, its paragraphs keep their first
   placing). Returns 0 out of memory, 1 placed (*next: the next row), 2 not laid out yet */
static int place_row(Layout *L, u32 i, Flow *fl, Table *t, u16 tid, i32 *last, int dup, u32 hdr_from, u32 hdr_end, u32 *next)
{
  Doc *d = L->d;
  RowM m;
  u32 endpage, pg;
  i32 endy, hh = 0, top2;
  int c, k, repeat = !dup && hdr_end > hdr_from && i >= hdr_end;
  const Cell *rc = cell_in(&L->pl[i].p->pf, tid);
  if (!measure_row(L, i, t, tid, &m)) return 2;
  /* the heading rows' height, for the pages a row runs on to */
  if (repeat) {
    u32 h = hdr_from;
    while (h < hdr_end) { RowM hm; if (!measure_row(L, h, t, tid, &hm)) return 2; hh += hm.rowh; h = hm.next; }
    if (hh > (fl->bottom - fl->top) / 2) { hh = 0; repeat = 0; }      /* headings taller than half a page: not repeated */
  }
  top2 = fl->top + hh;
  if (!fl->at_top && fl->y + m.rowh > fl->bottom &&
      (dup || rc->cantsplit || rc->rowh < 0 || fl->y + m.first > fl->bottom)) {
    flow_next(L, fl);
    for (k = 0; k < MAX_COLS; k++) last[k] = -1;
    /* the table's heading rows again at the top of the new page */
    if (repeat) {
      u32 h = hdr_from;
      while (h < hdr_end) { int r = place_row(L, h, fl, t, tid, last, 1, 0, 0, &h); if (r != 1) return r; }
      for (k = 0; k < MAX_COLS; k++) last[k] = -1;
    }
  }
  endpage = fl->page; endy = fl->y + m.rowh;
  for (c = 0; c < m.nc; c++) {
    const ParaFmt *pf = &L->pl[m.cs[c]].p->pf;
    const Cell *cc = cell_in(pf, tid);
    i32 cx, cw, cy = fl->y + t->mart, ey;
    u32 ep;
    int r;
    ParaFmt tmp = *pf;
    tmp.cell = *cc;                              /* (a cell starting with a nested table: the outer cell's) */
    cell_geom(d, &tmp, fl->colw, &cx, &cw);
    cx += fl->colx;
    if (cc->valign && m.rowh > m.ch[c] && fl->y + m.rowh <= fl->bottom) cy += cc->valign == 1 ? (m.rowh - m.ch[c]) / 2 : m.rowh - m.ch[c];
    r = place_block(L, m.cs[c], m.ce[c], tid, cx + t->marl, cw - t->marl - t->marr, fl->page, cy, fl->top, repeat ? top2 : fl->top, fl->bottom, dup, &ep, &ey);
    if (r != 1) return r;
    ey += t->marb;
    if (ep > endpage || (ep == endpage && ey > endy)) { endy = ey; endpage = ep; }
  }
  /* the cells' boxes */
  for (c = 0; c < m.nc; c++) {
    const ParaFmt *pf = &L->pl[m.cs[c]].p->pf;
    const Cell *ce0 = cell_in(pf, tid);
    ParaFmt tmp = *pf;
    i32 cx, cw;
    tmp.cell = *ce0;
    cell_geom(d, &tmp, fl->colw, &cx, &cw);
    cx += L->pg->left + fl->colx;
    if (endpage == fl->page) {
      CellBox *above = ce0->vmerge == VM_CONT && ce0->col < MAX_COLS && last[ce0->col] >= 0 ? &L->cells[last[ce0->col]] : NULL;
      if (above && above->page == fl->page && above->y + above->h == fl->y) {
        above->h += endy - fl->y;                  /* merged into the cell above */
        above->borders = (u8)((above->borders & ~BD_BOTTOM) | (ce0->borders & BD_BOTTOM));
        above->bstyle[E_BOTTOM] = ce0->bstyle[E_BOTTOM]; above->bwidth[E_BOTTOM] = ce0->bwidth[E_BOTTOM]; above->bcolor[E_BOTTOM] = ce0->bcolor[E_BOTTOM];
      } else {
        if (!add_cell(L, fl->page, cx, fl->y, cw, endy - fl->y, ce0)) return 0;
        if (ce0->col < MAX_COLS) last[ce0->col] = (i32)L->ncells - 1;
      }
    } else {
      for (pg = fl->page; pg <= endpage; pg++) {
        i32 y0 = pg == fl->page ? fl->y : repeat ? top2 : fl->top, y1 = pg == endpage ? endy : fl->bottom;
        if (!add_cell(L, pg, cx, y0, cw, y1 - y0, ce0)) return 0;
      }
      if (ce0->col < MAX_COLS) last[ce0->col] = -1;
    }
  }
  if (endpage != fl->page) {
    if (repeat) for (pg = fl->page + 1; pg <= endpage; pg++) { int r = place_headers(L, fl, pg, t, tid, hdr_from, hdr_end); if (r != 1) return r; }
    flow_jumped(L, fl, endpage);
  }
  fl->y = endy; fl->at_top = 0;
  *next = m.next;
  return 1;
}
/* place the table at *pi row by row (see the note above RowM); its heading rows are the whole rows
   at its start marked as such. *pi ends past the table, or at a paragraph not laid out yet. 0 if out
   of memory */
static int place_table(Layout *L, u32 *pi, Flow *fl)
{
  u32 i = *pi, hdr_end;
  const ParaFmt *pf0 = &L->pl[i].p->pf;
  u16 tid = top_cell(pf0)->table;
  Table *t = tid && tid <= L->d->ntables ? &L->d->tables[tid - 1] : NULL;
  i32 last[MAX_COLS];                    /* the box of the cell above, for vertical merging */
  int k;
  if (!t) { (*pi)++; return 1; }
  for (k = 0; k < MAX_COLS; k++) last[k] = -1;
  /* heading rows: those at the table's start marked to repeat */
  for (hdr_end = i; hdr_end < L->npl && cell_in(&L->pl[hdr_end].p->pf, tid) && cell_in(&L->pl[hdr_end].p->pf, tid)->header; hdr_end++) ;
  if (hdr_end < L->npl && hdr_end > i && cell_in(&L->pl[hdr_end].p->pf, tid) && cell_in(&L->pl[hdr_end].p->pf, tid)->row == cell_in(&L->pl[hdr_end - 1].p->pf, tid)->row)
    while (hdr_end > i && cell_in(&L->pl[hdr_end - 1].p->pf, tid)->row == cell_in(&L->pl[hdr_end].p->pf, tid)->row) hdr_end--;   /* whole rows only */
  while (i < L->npl && cell_in(&L->pl[i].p->pf, tid)) {
    int r = place_row(L, i, fl, t, tid, last, 0, *pi, hdr_end, &i);
    if (r == 0) return 0;
    if (r == 2) break;
  }
  *pi = i;
  return 1;
}
/* headers and footers. page_info: a page's page setup and section; for a page not laid out, the
   document's own page setup as its last section (in a static, so don't keep the pointer) */
static const PageInfo *page_info(Layout *L, u32 page)
{
  static PageInfo none;
  if (page < L->npages && page < L->pinfocap) return &L->pinfo[page];
  none.pg = &L->d->page; none.sec = NULL; none.first = 0; none.last = 1;
  return &none;
}
/* the page setup a page uses (its section's, or the document's) */
const PageSetup *lay_page(Layout *L, u32 page) { return page_info(L, page)->pg; }
/* see layout.h: the first page header or footer on a section's first page if it has a title page,
   the even one on even pages (odd page indexes) if the document has odd and even ones, else the
   default; and the section format whose they are (0: the document's) */
void lay_page_hf(Layout *L, u32 page, int *which, int *sect)
{
  const PageInfo *pi = page_info(L, page);
  Doc *d = L->d;
  *which = HF_DEFAULT;
  if (sect_titlepg(d, pi->sec, pi->last) && page == pi->first) *which = HF_FIRST;
  else if (d->evenodd && (page & 1)) *which = HF_EVEN;
  *sect = !pi->last && pi->sec && pi->sec->hf && pi->sec >= d->sects && pi->sec < d->sects + d->nsects ? (int)(pi->sec - d->sects) + 1 : 0;
}
/* the header (footer) story a page shows, as lay_page_hf picks it; it may be empty */
Story *lay_page_story(Layout *L, u32 page, int footer)
{
  const PageInfo *pi = page_info(L, page);
  int w, sect;
  lay_page_hf(L, page, &w, &sect);
  return sect_story(L->d, pi->sec, pi->last, footer, w);
}
/* a copy of src with the page fields (EE 80 80: this page, EE 80 81: the number of pages) replaced
   by numbers; NULL if out of memory, else the caller frees it */
static void counter_text(char *o, u32 n, int fmt);
static Para *fill_fields(Layout *L, Para *src, u32 page, u32 pages)
{
  Para *p = para_new(&src->pf, &src->r[0].f);
  u32 k, start = 0;
  if (!p) return NULL;
  for (k = 0; k < src->nr; k++) {
    u32 i = start, a = start, end = src->r[k].end;
    for (; i <= end; i++) {
      int field = i + 2 < end && IS_FIELD(src->t + i);
      if (i < end && !field) continue;
      if (i > a) para_append(p, src->t + a, i - a, &src->r[k].f);
      if (field) {
        /* the page (in the document's format), the number of pages, or a bookmark's page */
        char num[16];
        u32 first = L->d->pgstart > 1 ? L->d->pgstart : 1;
        int fmt = L->d->pgfmt && L->d->pgfmt != NF_NONE ? L->d->pgfmt : NF_DECIMAL;
        if ((u8)src->t[i + 2] == 0x80) counter_text(num, page + first, fmt);
        else if ((u8)src->t[i + 2] == 0x81) sprintf(num, "%lu", (unsigned long)pages);
        else {
          const char *u = doc_link_url(L->d, src->r[k].f.link);
          int pg = lay_bmark_page(L, u[0] == '#' ? u + 1 : u);
          if (pg > 0) counter_text(num, (u32)pg - 1 + first, fmt); else strcpy(num, "?");
        }
        para_append(p, num, (u32)strlen(num), &src->r[k].f);
        i += 2;
      }
      a = i + 1;
    }
    start = end;
  }
  return p;
}
/* free what lay_furniture, lay_notes, lay_endnotes or lay_textbox filled in, leaving f empty */
void lay_furniture_free(Furn *f)
{
  u32 i;
  for (i = 0; i < f->n; i++) { pl_free(&f->f[i].pl); para_free(f->f[i].p); }
  free(f->f); free(f->fb);
  f->f = NULL; f->n = 0; f->fb = NULL; f->nfb = 0;
}
static void float_rect(const PageSetup *pg, const Image *im, i32 py, i32 colx, i32 colw, i32 *x, i32 *y);
/* a story (a header or footer) laid out for page of pages (for its page fields), the page's text
   width wide: each paragraph's y from the story's top, *total its height. 0 if it's empty or nothing
   could be laid out; the caller frees f either way */
static int lay_story(Layout *L, const PageSetup *pg, Story *st, u32 page, u32 pages, Furn *f, i32 *total)
{
  i32 body = pg->width - pg->left - pg->right;
  u32 i, k;
  f->f = NULL; f->n = 0; f->fb = NULL; f->nfb = 0; *total = 0;
  if (!st->np) return 0;
  if (!(f->f = (FPara *)calloc(st->np, sizeof(FPara)))) return 0;
  for (i = 0; i < st->np; i++) {
    FPara *fp = &f->f[f->n];
    if (!(fp->p = fill_fields(L, st->p[i], page, pages))) break;
    f->n++;
    if (!layout_para(L, &fp->pl, fp->p, body < 1440 ? 1440 : body, "", 0, 0)) break;
    fp->y = *total + fp->p->pf.before;
    for (k = 0; k < fp->pl.nlines; k++) *total += fp->pl.lines[k].height;
    *total += fp->p->pf.before + fp->p->pf.after;
  }
  return f->n > 0;
}
/* see layout.h. A header goes at the page's header distance from the top, a footer ends at its
   footer distance from the bottom; floating objects in it are placed as in the body. None in web
   layout. Free f with lay_furniture_free */
int lay_furniture(Layout *L, u32 page, int footer, Furn *f)
{
  Doc *d = L->d;
  const PageSetup *pg = lay_page(L, page);
  i32 total, y0;
  u32 i;
  f->f = NULL; f->n = 0; f->fb = NULL; f->nfb = 0;
  if (L->webw > 0) return 0;
  if (!lay_story(L, pg, lay_page_story(L, page, footer), page, L->npages, f, &total)) { lay_furniture_free(f); return 0; }
  y0 = footer ? pg->height - pg->footer - total : pg->header;
  for (i = 0; i < f->n; i++) f->f[i].y += y0;
  /* floating objects in it (logos and the like): placed from its paragraphs, as in the body */
  for (i = 0; i < f->n; i++) {
    ParaLay *pl = &f->f[i].pl;
    u32 b;
    for (b = 0; b < pl->nboxes; b++)
      if (pl->boxes[b].kind == BX_FLOAT) {
        int n = pic_index(pl->p->t + pl->boxes[b].off);
        FloatBox *nf;
        i32 x, y;
        if (n >= d->nimages || !(nf = (FloatBox *)realloc(f->fb, (f->nfb + 1) * sizeof(FloatBox)))) continue;
        f->fb = nf;
        float_rect(pg, &d->images[n], f->f[i].y - f->f[i].p->pf.before, 0, pg->width - pg->left - pg->right, &x, &y);
        nf = &f->fb[f->nfb++];
        nf->page = page; nf->x = x; nf->y = y; nf->w = OBJ_W(&d->images[n]); nf->h = OBJ_H(&d->images[n]); nf->img = n; nf->para = i; nf->wrap = d->images[n].wrap; nf->early = 0;
      }
  }
  return 1;
}
/* see layout.h. The text is inset TB_INSET_X at the sides and TB_INSET_Y at the top, and gets at least
   a quarter inch. Free f with lay_furniture_free */
/* (WP18: laid out w wide, with its own insets, by tb_layout in objtext.c, which turns, anchors and
   links text boxes) */
static int lay_textbox_plain(Layout *L, const Image *im, i32 w, Furn *f)
{
  i32 y = tb_inset(im, 1);
  u32 i, k;
  memset(f, 0, sizeof *f);
  if (!im || !im->text || !im->text->np) return 0;
  if (w < 360) w = 360;
  if (!(f->f = (FPara *)calloc(im->text->np, sizeof(FPara)))) return 0;
  for (i = 0; i < im->text->np; i++) {
    FPara *fp = &f->f[f->n];
    if (!(fp->p = para_copy(im->text->p[i]))) break;
    f->n++;
    if (!layout_para(L, &fp->pl, fp->p, w, "", 0, 0)) break;
    y += fp->p->pf.before;
    fp->y = y;
    for (k = 0; k < fp->pl.nlines; k++) { fp->pl.lines[k].left += tb_inset(im, 0); y += fp->pl.lines[k].height; }
    y += fp->p->pf.after;
  }
  return f->n > 0;
}
int lay_textbox(Layout *L, const Image *im, i32 cx, Furn *f) { return tb_layout(L, im, cx, f, lay_textbox_plain); }
/* the text of a page field box on a page (from 0): the page's number or the number of pages. While a
   header or footer is being edited (d->edit.kind), the numbers it was opened with instead */
static void counter_text(char *o, u32 n, int fmt);
void lay_field_text(Layout *L, const Para *p, const Box *b, u32 page, char *out)
{
  u32 first = L->d->pgstart > 1 ? L->d->pgstart : 1;      /* (page numbers from here, in the document's format) */
  int fmt = L->d->pgfmt && L->d->pgfmt != NF_NONE ? L->d->pgfmt : NF_DECIMAL;
  const Doc *d = L->d;
  int pages = (u8)p->t[b->off + 2] == 0x81;
  if ((u8)p->t[b->off + 2] == 0x82) {
    /* a cross-reference to the page a bookmark is on ("?" if it isn't in the text, or not laid out yet) */
    const char *u = doc_link_url(d, b->f.link);
    int pg = lay_bmark_page(L, u[0] == '#' ? u + 1 : u);
    if (pg > 0) counter_text(out, (u32)pg - 1 + first, fmt); else strcpy(out, "?");
    return;
  }
  if (pages) sprintf(out, "%lu", (unsigned long)(d->edit.kind ? d->edit.fpages : L->npages));
  else counter_text(out, (d->edit.kind ? d->edit.fpage : page) + first, fmt);
}
/* ---- footnotes */
/* the number a note box shows: its paragraph's notebase (enotebase for an endnote) plus the notes of
   its kind up to and including this box */
u32 lay_note_number(ParaLay *pl, u32 box)
{
  int kind = box < pl->nboxes && pl->boxes[box].kind == BX_ENOTE ? BX_ENOTE : BX_NOTE;
  u32 k, n = kind == BX_ENOTE ? pl->enotebase : pl->notebase;
  for (k = 0; k <= box && k < pl->nboxes; k++) if (pl->boxes[k].kind == kind) n++;
  return n;
}
/* n in lower case roman numerals (endnotes' numbers); past 3999 as digits. out needs 16 bytes */
void lay_roman(u32 n, char *out)
{
  static const char *const r[13] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
  static const u32 v[13] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
  int i;
  out[0] = 0;
  if (n > 3999) { sprintf(out, "%lu", (unsigned long)n); return; }
  for (i = 0; i < 13; i++) while (n >= v[i]) { strcat(out, r[i]); n -= v[i]; }
}
/* the label a note box shows: digits for a footnote, roman numerals for an endnote */
void lay_note_label(ParaLay *pl, u32 box, char *out)
{
  u32 n = lay_note_number(pl, box);
  int end = box < pl->nboxes && pl->boxes[box].kind == BX_ENOTE;
  if (pl->lay && box < pl->nboxes) { note_text(pl->lay, end, note_index(pl->p->t + pl->boxes[box].off), n, out); return; }   /* (WP22b) */
  if (end) lay_roman(n, out);
  else sprintf(out, "%lu", (unsigned long)n);
}
/* (WP22b) note n's number as shown (end: an endnote's): as numbered for the Footnote and Endnote window's
   options, else counted (1, 2, 3 / i, ii, iii) */
static void note_text(Layout *L, int end, int n, u32 counted, char *out)
{
  NoteNum *nn = end ? L->enn : L->fnn;
  if (nn && n >= 0 && n < (end ? L->d->nenotes : L->d->nnotes) && nn[n].lab) { notes_label(nn[n].fmt, nn[n].lab, out); return; }
  if (end) lay_roman(counted, out);
  else sprintf(out, "%lu", (unsigned long)counted);
}
/* footnote n's paragraphs laid out (end: endnote n's), its number before the first; *total: their height */
static int lay_note_ex(Layout *L, const PageSetup *pg, int n, int end, Furn *f, i32 *total)
{
  Doc *d = L->d;
  Story *st;
  i32 body = pg->width - pg->left - pg->right;
  u32 i, k;
  f->f = NULL; f->n = 0; f->fb = NULL; f->nfb = 0; *total = 0;
  if (n < 0 || n >= (end ? d->nenotes : d->nnotes)) return 0;
  st = end ? &d->enotes[n] : &d->notes[n];
  if (!st->np || !(f->f = (FPara *)calloc(st->np, sizeof(FPara)))) return 0;
  for (i = 0; i < st->np; i++) {
    FPara *fp = &f->f[f->n];
    if (i == 0) {
      /* "1 Text...": the number raised, a space */
      Para *src = st->p[0];
      CharFmt nf = src->r[0].f;
      char num[16];
      u32 r, start = 0;
      nf.flags = (u8)((nf.flags & ~CF_SUB) | CF_SUPER);
      note_text(L, end, n, end ? (L->enotenum ? L->enotenum[n] : (u32)n + 1) : (L->notenum ? L->notenum[n] : (u32)n + 1), num);   /* (WP22b) */
      if (!(fp->p = para_new(&src->pf, &src->r[0].f))) break;
      para_append(fp->p, num, (u32)strlen(num), &nf);
      para_append(fp->p, " ", 1, &src->r[0].f);
      for (r = 0; r < src->nr; r++) { if (src->r[r].end > start) para_append(fp->p, src->t + start, src->r[r].end - start, &src->r[r].f); start = src->r[r].end; }
    } else if (!(fp->p = para_copy(st->p[i]))) break;
    f->n++;
    if (!layout_para(L, &fp->pl, fp->p, body < 1440 ? 1440 : body, "", 0, 0)) break;
    fp->y = *total + fp->p->pf.before;
    for (k = 0; k < fp->pl.nlines; k++) *total += fp->pl.lines[k].height;
    *total += fp->p->pf.before + fp->p->pf.after;
  }
  return f->n > 0;
}
/* footnote n laid out (see lay_note_ex) */
static int lay_note(Layout *L, const PageSetup *pg, int n, Furn *f, i32 *total) { return lay_note_ex(L, pg, n, 0, f, total); }
static int lay_story(Layout *L, const PageSetup *pg, Story *st, u32 page, u32 pages, Furn *f, i32 *total);
#define NOTE_SEP 240                     /* the gap above the notes, with the short line in it */
/* (WP22b) separator which of footnotes (end: endnotes) as it is drawn: its line (NSF_, 0 none), and its
   words laid out into f (the page's text width) with *h their height (0 and f empty: none) */
static int sep_story(Layout *L, const PageSetup *pg, int end, int which, Furn *f, i32 *h)
{
  const Story *t;
  int flags = notes_sep(L->d, end, which, &t);
  memset(f, 0, sizeof *f); *h = 0;
  if (t && !lay_story(L, pg, (Story *)t, 0, 1, f, h)) { lay_furniture_free(f); *h = 0; }
  return flags;
}
/* (WP22b) f's paragraphs moved to the end of out, dy added to their y (f left empty); 0 if out of memory */
static int furn_append(Furn *out, Furn *f, i32 dy)
{
  FPara *nf;
  u32 k;
  if (!f->n) return 1;
  if (!(nf = (FPara *)realloc(out->f, (out->n + f->n) * sizeof(FPara)))) { lay_furniture_free(f); return 0; }
  out->f = nf;
  for (k = 0; k < f->n; k++) { f->f[k].y += dy; out->f[out->n++] = f->f[k]; }
  free(f->f); f->f = NULL; f->n = 0;
  free(f->fb); f->fb = NULL; f->nfb = 0;
  return 1;
}
/* see layout.h: the endnote paragraphs pagination put on this page, each laid out and moved into
   place, after the separator's words if it has some (WP22b). Free out with lay_furniture_free */
int lay_endnotes(Layout *L, u32 page, Furn *out, i32 *sepy)
{
  u32 i;
  int last = -1;
  Furn f;
  memset(out, 0, sizeof *out);
  memset(&f, 0, sizeof f);
  *sepy = -1; L->sepw = 0; L->notesep_cont = 0;
  /* (WP22b) the line above them (each section's endnotes have one; a page they go on to, the continuation separator) */
  for (i = 0; i < L->nenseps; i++) {
    const PageSetup *pg = lay_page(L, page);
    Furn t;
    i32 th;
    int flags;
    if (L->enseps[i].page != page) continue;
    flags = sep_story(L, pg, 1, L->enseps[i].cont ? NSEP_CONT : NSEP_SEP, &t, &th);
    if (*sepy < 0 && flags) {
      *sepy = L->enseps[i].y;
      L->notesep_cont = (u8)(L->enseps[i].cont || (flags & NSF_LONG));
      L->sepw = L->notesep_cont ? pg->width - pg->left - pg->right : 2880;
    }
    if (t.n && !furn_append(out, &t, L->enseps[i].y + NOTE_SEP / 2)) break;
    lay_furniture_free(&t);
  }
  for (i = 0; i < L->nen; i++) {
    FPara *nf;
    i32 h;
    if (L->en[i].page != page) continue;
    if ((int)L->en[i].note != last) { lay_furniture_free(&f); last = (int)L->en[i].note; if (!lay_note_ex(L, lay_page(L, page), last, 1, &f, &h)) continue; }
    if (L->en[i].para >= f.n || !f.f[L->en[i].para].p) continue;
    if (!(nf = (FPara *)realloc(out->f, (out->n + 1) * sizeof(FPara)))) break;
    out->f = nf;
    out->f[out->n] = f.f[L->en[i].para];
    out->f[out->n].y = L->en[i].y;
    memset(&f.f[L->en[i].para], 0, sizeof(FPara));             /* moved */
    out->n++;
  }
  lay_furniture_free(&f);
  return out->n > 0 || *sepy >= 0;
}
/* the height footnote n takes at the foot of the page being filled (L->pg); laid out to measure it,
   then thrown away */
static i32 note_height(Layout *L, int n)
{
  Furn f;
  i32 h = 0;
  lay_note(L, L->pg, n, &f, &h);
  lay_furniture_free(&f);
  return h;
}
/* (WP22b) footnote n's lines (laid out at the foot of the page being filled): each one's height, a
   paragraph's space before and after counted in its first and last line; how many (at most max) */
#define NOTE_MAXL 400
static u32 note_lines(Layout *L, int n, i32 *hs, u32 max)
{
  Furn f;
  i32 h;
  u32 j, k, c = 0;
  if (lay_note(L, L->pg, n, &f, &h))
    for (j = 0; j < f.n; j++)
      for (k = 0; k < f.f[j].pl.nlines && c < max; k++) {
        i32 v = f.f[j].pl.lines[k].height;
        if (k == 0) v += f.f[j].p->pf.before;
        if (k + 1 == f.f[j].pl.nlines) v += f.f[j].p->pf.after;
        hs[c++] = v;
      }
  lay_furniture_free(&f);
  return c;
}
/* (WP22b) the lines of footnote n a page shows (all of them unless it is split over pages): 1 if a
   part, *l0..*l1 */
static int note_cut(Layout *L, u32 page, u32 n, u32 *l0, u32 *l1)
{
  u32 i;
  for (i = 0; i < L->nncut; i++) if (L->ncut[i].page == page && L->ncut[i].note == n) { *l0 = L->ncut[i].l0; *l1 = L->ncut[i].l1; return 1; }
  return 0;
}
static int add_cut(Layout *L, u32 page, u32 n, u32 l0, u32 l1)
{
  if (L->nncut >= L->ncutcap) {
    u32 nc = L->ncutcap ? L->ncutcap * 2 : 8;
    NoteCut *a = (NoteCut *)realloc(L->ncut, nc * sizeof(NoteCut));
    if (!a) return 0;
    L->ncut = a; L->ncutcap = nc;
  }
  L->ncut[L->nncut].page = page; L->ncut[L->nncut].note = n; L->ncut[L->nncut].l0 = l0; L->ncut[L->nncut].l1 = l1;
  L->nncut++;
  return 1;
}
/* (WP22b) only lines l0..l1 of the note's paragraphs f kept (counting lines across them), stacked
   from 0 again; *h their height */
static void furn_lines(Furn *f, u32 l0, u32 l1, i32 *h)
{
  u32 j, g = 0, keep = 0;
  i32 y = 0;
  for (j = 0; j < f->n; j++) {
    FPara *fp = &f->f[j];
    u32 n = fp->pl.nlines, a = g, b = g + n, s, e;
    g += n;
    s = a > l0 ? a : l0; e = b < l1 ? b : l1;
    if (s >= e) { pl_free(&fp->pl); para_free(fp->p); fp->p = NULL; continue; }
    if (s > a) memmove(fp->pl.lines, fp->pl.lines + (s - a), (e - s) * sizeof(Line));
    fp->pl.nlines = e - s;
    {
      i32 before = s == a ? fp->p->pf.before : 0, after = e == b ? fp->p->pf.after : 0;
      u32 k;
      fp->y = y + before;
      y += before + after;
      for (k = 0; k < fp->pl.nlines; k++) y += fp->pl.lines[k].height;
    }
    f->f[keep++] = *fp;
  }
  f->n = keep;
  *h = y;
}
/* see layout.h: the page's footnotes one after another, the last ending at the bottom margin (or,
   placed beneath the text, starting under its last line), the separator's gap NOTE_SEP above the
   first with its line halfway; a footnote continued from the page before has the continuation
   separator instead, one going on overleaf the continuation notice under it (WP22b). Free out with
   lay_furniture_free */
int lay_notes(Layout *L, u32 page, Furn *out, i32 *sepy)
{
  u32 i, n = 0;
  i32 y, total = 0, sh = 0, nh = 0;
  int cont = 0, cut = 0, flags, first = 1;
  const PageSetup *pg = lay_page(L, page);
  NotePr np;
  Furn st, nt;
  memset(out, 0, sizeof *out);
  *sepy = -1; L->sepw = 0; L->notesep_cont = 0;
  for (i = 0; i < L->npn; i++) if (L->pn[i].page == page) n++;
  if (!n) return 0;
  /* each note's paragraphs (or the part of it on this page), one after another */
  for (i = 0; i < L->npn; i++) {
    Furn f;
    i32 h;
    u32 l0 = 0, l1 = 0xFFFFFFFFUL;
    if (L->pn[i].page != page || !lay_note(L, pg, (int)L->pn[i].note, &f, &h)) continue;
    if (note_cut(L, page, L->pn[i].note, &l0, &l1)) {
      u32 j, all = 0;
      for (j = 0; j < f.n; j++) all += f.f[j].pl.nlines;
      if (first && l0 > 0) cont = 1;
      if (l1 < all) cut = 1;
      furn_lines(&f, l0, l1, &h);
    }
    first = 0;
    if (!furn_append(out, &f, total)) break;
    total += h;
  }
  /* the separator over them (its words, if it has some), the notice under them */
  flags = sep_story(L, pg, 0, cont ? NSEP_CONT : NSEP_SEP, &st, &sh);
  memset(&nt, 0, sizeof nt);
  if (cut) sep_story(L, pg, 0, NSEP_NOTICE, &nt, &nh);
  notes_eff(L->d, page < L->npages && L->pinfo ? L->pinfo[page].sec : NULL, 0, &np);
  if (np.pos == NPOS_BENEATHTEXT) {
    /* just under the page's last line of text */
    u32 a, b, k;
    i32 tb = pg->top;
    lay_page_lines(L, page, &a, &b);
    for (k = a; k < b; k++)
      if (L->placed[k].page == page) {
        i32 e = L->placed[k].y + L->pl[L->placed[k].para].lines[L->placed[k].line].height;
        if (e > tb) tb = e;
      }
    y = tb + NOTE_SEP + sh;
    if (y + total + nh > pg->height - pg->bottom) y = pg->height - pg->bottom - total - nh;
  } else y = pg->height - pg->bottom - total - nh;
  for (i = 0; i < out->n; i++) out->f[i].y += y;
  if (st.n) {
    /* the separator's words first, in the gap's lower part */
    Furn rest = *out;
    memset(out, 0, sizeof *out);
    furn_append(out, &st, y - sh);
    furn_append(out, &rest, 0);
    free(rest.f);
  }
  if (nt.n) furn_append(out, &nt, y + total);
  lay_furniture_free(&st); lay_furniture_free(&nt);
  if (flags) {
    *sepy = y - sh - NOTE_SEP / 2;
    L->notesep_cont = (u8)(cont || (flags & NSF_LONG));
    L->sepw = L->notesep_cont ? pg->width - pg->left - pg->right : 2880;
  }
  return out->n > 0;
}
/* the most room any header (footer) of a section takes, from the page edge (laid out with made-up
   page numbers: only the height matters) */
static i32 furniture_extent(Layout *L, const PageSetup *pg, const SectFmt *sec, int last, int footer)
{
  Doc *d = L->d;
  i32 most = 0, dist = footer ? pg->footer : pg->header;
  int w;
  for (w = 0; w < HF_N; w++) {
    Furn f;
    i32 total;
    if (lay_story(L, pg, sect_story(d, sec, last, footer, w), 98, 99, &f, &total) && total + dist > most) most = total + dist;
    lay_furniture_free(&f);
  }
  return most;
}
/* a floating object's place on a page: from its anchor paragraph's top (py), its column (colx, colw
   from the text area's left), the page */
static void float_rect(const PageSetup *pg, const Image *im, i32 py, i32 colx, i32 colw, i32 *x, i32 *y)
{
  i32 w = OBJ_W(im), h = OBJ_H(im), base, span;
  switch (im->hrel) {
  case POS_PAGE: base = 0; span = pg->width; break;
  case POS_MARGIN: base = pg->left; span = pg->width - pg->left - pg->right; break;
  default: base = pg->left + colx; span = colw;
  }
  *x = im->halign == PA_START ? base : im->halign == PA_CENTER ? base + (span - w) / 2 : im->halign == PA_END ? base + span - w : base + im->x;
  switch (im->vrel) {
  case POS_PAGE: base = 0; span = pg->height; break;
  case POS_MARGIN: base = pg->top; span = pg->height - pg->top - pg->bottom; break;
  default: base = py; span = h;
  }
  *y = im->valign == PA_START ? base : im->valign == PA_CENTER ? base + (span - h) / 2 : im->valign == PA_END ? base + span - h : base + im->y;
  if (*x + w > pg->width) *x = pg->width - w;
  if (*x < 0) *x = 0;
  if (*y + h > pg->height) *y = pg->height - h;
  if (*y < 0) *y = 0;
}
/* append a floating object placed at x, y on page, anchored in paragraph para; 0 if out of memory */
static int add_float(Layout *L, u32 page, i32 x, i32 y, int img, u32 para)
{
  FloatBox *f;
  const Image *im = &L->d->images[img];
  if (L->nfloats >= L->floatcap) {
    u32 nc = L->floatcap ? L->floatcap * 2 : 16;
    FloatBox *n = (FloatBox *)realloc(L->floats, nc * sizeof(FloatBox));
    if (!n) return 0;
    L->floats = n; L->floatcap = nc;
  }
  f = &L->floats[L->nfloats++];
  f->page = page; f->x = x; f->y = y; f->w = OBJ_W(im); f->h = OBJ_H(im); f->img = img; f->para = para; f->wrap = im->wrap; f->early = 0;
  return 1;
}
/* paragraph pi's floating objects onto the page, and its lines laid out round any that are in their
   way (fl: where the paragraph starts). 0 if out of memory */
static int place_floats(Layout *L, u32 pi, Flow *fl)
{
  Doc *d = L->d;
  ParaLay *pl = &L->pl[pi];
  u32 b, i;
  int wraps = 0;
  for (b = 0; b < pl->nboxes; b++)
    if (pl->boxes[b].kind == BX_FLOAT) {
      int n = pic_index(pl->p->t + pl->boxes[b].off);
      i32 x, y;
      if (n >= d->nimages) continue;
      float_rect(L->pg, &d->images[n], fl->y, fl->colx, fl->colw, &x, &y);
      /* placed early (from the last pass): now where it really is */
      for (i = 0; i < L->nfloats; i++)
        if (L->floats[i].early && L->floats[i].img == n && L->floats[i].para == pi) {
          L->floats[i].early = 0; L->floats[i].page = fl->page; L->floats[i].x = x; L->floats[i].y = y;
          L->floats[i].w = OBJ_W(&d->images[n]); L->floats[i].h = OBJ_H(&d->images[n]); L->floats[i].wrap = d->images[n].wrap;
          break;
        }
      if (i == L->nfloats && !add_float(L, fl->page, x, y, n, pi)) return 0;
    }
  /* objects on this page the text wraps round, in this paragraph's way? */
  {
    i32 top = fl->y, bottom = fl->y + para_height(pl, 0, pl->nlines) + 2880;
    for (i = 0; i < L->nfloats; i++) {
      const FloatBox *f = &L->floats[i];
      if (f->page == fl->page && (f->wrap == WRAP_SQUARE || f->wrap == WRAP_TIGHT || f->wrap == WRAP_TOPBOTTOM) &&
          f->y + f->h + d->images[f->img].db > top && f->y - d->images[f->img].dt < bottom) wraps = 1;
    }
  }
  if (!wraps && !pl->shaped) return 1;
  sh_on = wraps; sh_fb = L->floats; sh_nfb = L->nfloats; sh_page = fl->page; sh_doc = d;
  sh_x0 = L->pg->left + fl->colx; sh_y0 = fl->y;
  { char lab[32]; memcpy(lab, pl->label, sizeof lab);
    i = layout_para(L, pl, pl->p, pl->avail, lab, pl->notebase, pl->enotebase); }
  sh_on = 0;
  return (int)i;
}
/* the floats the text wraps round placed from the page or margin, kept for the next pass: 1 if they
   are where they were last time */
static int floats_settled(Layout *L)
{
  Doc *d = L->d;
  FloatBox *e = NULL;
  u32 i, n = 0;
  int same;
  for (i = 0; i < L->nfloats; i++) {
    const FloatBox *f = &L->floats[i];
    if (f->img < d->nimages && d->images[f->img].vrel != POS_PARA && (f->wrap == WRAP_SQUARE || f->wrap == WRAP_TIGHT || f->wrap == WRAP_TOPBOTTOM)) {
      if (!e && !(e = (FloatBox *)malloc(L->nfloats * sizeof(FloatBox)))) return 1;
      e[n] = *f; e[n].early = 0; n++;
    }
  }
  same = n == L->nearly;
  for (i = 0; same && i < n; i++)
    same = e[i].img == L->early[i].img && e[i].para == L->early[i].para && e[i].page == L->early[i].page && e[i].x == L->early[i].x &&
           e[i].y == L->early[i].y && e[i].w == L->early[i].w && e[i].h == L->early[i].h && e[i].wrap == L->early[i].wrap;
  free(L->early);
  L->early = e; L->nearly = n;
  return same;
}
/* paragraphs' borders and shading: those with the same run together in one box. boxed: pf has a
   border or a fill (never in a table, whose cells draw their own) */
static int boxed(const ParaFmt *pf)
{
  return !in_table(pf) && (pf->bstyle[PE_TOP] || pf->bstyle[PE_LEFT] || pf->bstyle[PE_BOTTOM] || pf->bstyle[PE_RIGHT] || pf->bstyle[PE_BETWEEN] || pf->shade);
}
/* paragraphs a and b share one border box: both boxed, with the same borders, fill and indents */
static int same_box(const ParaFmt *a, const ParaFmt *b)
{
  return boxed(a) && boxed(b) && !memcmp(a->bstyle, b->bstyle, sizeof a->bstyle) && !memcmp(a->bwidth, b->bwidth, sizeof a->bwidth) &&
         !memcmp(a->bspace, b->bspace, sizeof a->bspace) && !memcmp(a->bcolor, b->bcolor, sizeof a->bcolor) && a->shade == b->shade &&
         a->left == b->left && a->right == b->right;
}
/* a border's line width (twips; 0 none) and its line with the space to the text. bwidth is in
   eighths of a point (4 if unset), bspace in points; a double line takes three times the width */
static i32 bwid(const ParaFmt *pf, int e) { return pf->bstyle[e] ? (pf->bwidth[e] ? pf->bwidth[e] : 4) * 5 / 2 * (pf->bstyle[e] == PB_DOUBLE ? 3 : 1) : 0; }
static i32 bextra(const ParaFmt *pf, int e) { return pf->bstyle[e] ? pf->bspace[e] * 20 + bwid(pf, e) : 0; }
/* paragraph pi's boxes, one for each page or column its lines are on: topx, botx the room its top
   and bottom borders took; joined: it continues the box of the paragraph before (gprev), or the
   next continues its box (gnext) */
static int add_pboxes(Layout *L, u32 pi, i32 topx, i32 botx, int gprev, int gnext)
{
  ParaLay *pl = &L->pl[pi];
  const ParaFmt *pf = &pl->p->pf;
  u32 k = pl->placed0, e;
  while (k < L->nplaced) {
    ParaBox *b;
    Placed *f = &L->placed[k], *l;
    i32 in = pf->left + (pf->first < 0 ? pf->first : 0);
    for (e = k; e + 1 < L->nplaced && L->placed[e + 1].page == f->page && L->placed[e + 1].x == f->x; e++) ;
    l = &L->placed[e];
    if (L->npbox >= L->pboxcap) {
      u32 nc = L->pboxcap ? L->pboxcap * 2 : 32;
      ParaBox *nb = (ParaBox *)realloc(L->pbox, nc * sizeof(ParaBox));
      if (!nb) return 0;
      L->pbox = nb; L->pboxcap = nc;
    }
    b = &L->pbox[L->npbox];
    b->page = f->page; b->para = pi; b->lines = 0; b->ybetween = 0;
    b->x0 = L->pg->left + f->x + in - (pf->bstyle[PE_LEFT] ? pf->bspace[PE_LEFT] * 20 + bwid(pf, PE_LEFT) : 0);
    b->x1 = L->pg->left + f->x + f->w - pf->right + (pf->bstyle[PE_RIGHT] ? pf->bspace[PE_RIGHT] * 20 + bwid(pf, PE_RIGHT) : 0);
    b->y0 = f->y; b->y1 = l->y + pl->lines[l->line].height;
    if (k == pl->placed0) {
      b->y0 -= topx;
      if (gprev) {
        /* the box goes on from the paragraph before's (on the same page and column) */
        ParaBox *pv = L->npbox ? &L->pbox[L->npbox - 1] : NULL;
        if (pf->bstyle[PE_BETWEEN]) { b->lines |= PB_BETWEENLINE; b->ybetween = f->y - pf->bspace[PE_BETWEEN] * 20 - bwid(pf, PE_BETWEEN); }
        if (pv && pv->para + 1 == pi && pv->page == f->page) b->y0 = pv->y1;
      } else if (pf->bstyle[PE_TOP]) b->lines |= PB_TOPLINE;
    }
    if (e + 1 >= L->nplaced) {
      b->y1 += botx;
      if (!gnext && pf->bstyle[PE_BOTTOM]) b->lines |= PB_BOTTOMLINE;
      else if (gnext) {
        /* the gap to the next paragraph is inside the box too */
        b->y1 += pf->after;
      }
    }
    L->npbox++;
    k = e + 1;
  }
  return 1;
}
/* the width between a page's side margins */
static i32 page_body(const PageSetup *pg) { return pg->width - pg->left - pg->right; }
/* (WP22b) footnotes while paginating: the room kept at the foot of the page being filled, and a long
   footnote going on overleaf (its lines from cl0 on still to place) */
typedef struct {
  u32 respage; i32 reserve;       /* the page the notes' room is kept on, and how much */
  int cn; u32 cl0, cnl;           /* the footnote being continued (-1 none), its next line, its lines */
  i32 sep, sepc, notice;          /* room for the separator (its gap and words), the continuation separator, the notice */
  i32 hs[NOTE_MAXL];              /* the continued footnote's lines' heights */
} NoteFlow;
static void nflow_init(Layout *L, NoteFlow *nf)
{
  Furn f;
  i32 h;
  memset(nf, 0, sizeof *nf);
  nf->cn = -1;
  nf->sep = NOTE_SEP; nf->sepc = NOTE_SEP;
  if (!L->d->nnotes || L->webw > 0) return;
  sep_story(L, L->pg, 0, NSEP_SEP, &f, &h); nf->sep += h; lay_furniture_free(&f);
  sep_story(L, L->pg, 0, NSEP_CONT, &f, &h); nf->sepc += h; lay_furniture_free(&f);
  sep_story(L, L->pg, 0, NSEP_NOTICE, &f, &h); nf->notice = h; lay_furniture_free(&f);
}
/* a new page (area: its text area's height): the footnote going on from the page before takes the
   foot of it, as much as fits leaving half an inch for the text (full: all of it), at least one line */
static void nflow_page(Layout *L, NoteFlow *nf, u32 page, i32 area, int full)
{
  i32 room, acc = 0;
  u32 l1;
  nf->respage = page; nf->reserve = 0;
  if (nf->cn < 0) return;
  room = (full ? area : area - 720) - nf->sepc;
  for (l1 = nf->cl0; l1 < nf->cnl && acc + nf->hs[l1] <= room; l1++) acc += nf->hs[l1];
  if (l1 < nf->cnl) {
    /* more to come: room for the notice too */
    while (l1 > nf->cl0 && acc + nf->notice > room) acc -= nf->hs[--l1];
  }
  if (l1 == nf->cl0) acc += nf->hs[l1++];
  if (!add_cut(L, page, (u32)nf->cn, nf->cl0, l1)) L->oom = 1;
  nf->reserve = nf->sepc + acc + (l1 < nf->cnl ? nf->notice : 0);
  nf->cl0 = l1;
  if (l1 >= nf->cnl) nf->cn = -1;
}
/* the room line k of pl needs for its footnotes on the page (with the separator if it is the first) */
static i32 nflow_line(Layout *L, NoteFlow *nf, ParaLay *pl, u32 k)
{
  u32 b;
  i32 extra = 0;
  for (b = pl->lines[k].box0; b < pl->lines[k].box0 + pl->lines[k].nbox; b++)
    if (pl->boxes[b].kind == BX_NOTE) extra += note_height(L, note_index(pl->p->t + pl->boxes[b].off));
  if (extra && !nf->reserve) extra += nf->sep;
  return extra;
}
/* line k of pl doesn't fit with all its footnotes: if the line and the first lines of its last
   footnote fit (room: what is left under the line), that note is split, the rest going overleaf
   under the continuation separator; *extra the room it takes now. 1 if split */
static int nflow_split(Layout *L, NoteFlow *nf, ParaLay *pl, u32 k, u32 page, i32 room, i32 *extra)
{
  u32 b, l1, cnt;
  int last = -1;
  i32 others = 0, acc = 0, sep = nf->reserve ? 0 : nf->sep;
  if (nf->cn >= 0 || room <= 0) return 0;
  for (b = pl->lines[k].box0; b < pl->lines[k].box0 + pl->lines[k].nbox; b++)
    if (pl->boxes[b].kind == BX_NOTE) {
      if (last >= 0) others += note_height(L, last);
      last = note_index(pl->p->t + pl->boxes[b].off);
    }
  if (last < 0 || last >= L->d->nnotes) return 0;
  room -= sep + others + nf->notice;
  cnt = note_lines(L, last, nf->hs, NOTE_MAXL);
  for (l1 = 0; l1 < cnt && acc + nf->hs[l1] <= room; l1++) acc += nf->hs[l1];
  if (l1 == 0 || l1 >= cnt) return 0;
  if (!add_cut(L, page, (u32)last, 0, l1)) { L->oom = 1; return 0; }
  nf->cn = last; nf->cl0 = l1; nf->cnl = cnt;
  *extra = sep + others + acc + nf->notice;
  return 1;
}
/* the endnotes referred to in paragraphs a..b-1 not placed yet (done: those placed), in the order they
   are referred to, after the text so far: the separator's gap first (on a new page if there is no
   room), each paragraph on the next page when it doesn't fit there, a page they go on to starting with
   the continuation separator. resv: the room kept for footnotes on page respage. 0 if out of memory */
static int add_ensep(Layout *L, u32 page, i32 y, int cont)
{
  if (L->nenseps >= L->ensepcap) {
    u32 nc = L->ensepcap ? L->ensepcap * 2 : 8;
    NoteSepAt *a = (NoteSepAt *)realloc(L->enseps, nc * sizeof(NoteSepAt));
    if (!a) return 0;
    L->enseps = a; L->ensepcap = nc;
  }
  L->enseps[L->nenseps].page = page; L->enseps[L->nenseps].y = y; L->enseps[L->nenseps].cont = (u8)cont;
  if (!L->nenseps) { L->ensep_page = page; L->ensep_y = y; }
  L->nenseps++;
  return 1;
}
static int place_enotes(Layout *L, Flow *fl, u8 *done, u32 a, u32 b, i32 resv, u32 respage)
{
  Doc *d = L->d;
  u32 i, k;
  int any = 0;
  i32 sh = 0, ch = 0;
  if (!d->nenotes || !done) return 1;
  for (i = a; i < b && i < d->np; i++) {
    Para *p = d->p[i];
    for (k = 0; k + 4 <= p->len; k++) {
      int n;
      Furn f;
      i32 h;
      u32 j;
      if (!IS_ENOTE(p->t + k)) continue;
      n = note_index(p->t + k); k += 3;
      if (n >= d->nenotes || done[n]) continue;
      done[n] = 1;
      if (!any) {
        Furn t;
        sep_story(L, L->pg, 1, NSEP_SEP, &t, &sh); lay_furniture_free(&t);
        sep_story(L, L->pg, 1, NSEP_CONT, &t, &ch); lay_furniture_free(&t);
        fl->y += NOTE_SEP + sh;
        if (fl->y + 480 > fl->bottom - (fl->page == respage ? resv : 0)) { new_page(L, fl); fl->y += NOTE_SEP + sh; }
        if (!add_ensep(L, fl->page, fl->y - sh - NOTE_SEP / 2, 0)) return 0;
        any = 1;
      }
      if (!lay_note_ex(L, L->pg, n, 1, &f, &h)) continue;
      for (j = 0; j < f.n; j++) {
        i32 ph = f.f[j].p->pf.before + f.f[j].p->pf.after, q;
        for (q = 0; q < (i32)f.f[j].pl.nlines; q++) ph += f.f[j].pl.lines[q].height;
        if (fl->y + ph > fl->bottom - (fl->page == respage ? resv : 0) && fl->y > fl->top) {
          new_page(L, fl);
          fl->y += NOTE_SEP + ch;
          if (!add_ensep(L, fl->page, fl->y - ch - NOTE_SEP / 2, 1)) { lay_furniture_free(&f); return 0; }
        }
        if (L->nen >= L->encap) {
          u32 nc = L->encap ? L->encap * 2 : 16;
          void *ne = realloc(L->en, nc * sizeof L->en[0]);
          if (!ne) { lay_furniture_free(&f); return 0; }
          L->en = ne; L->encap = nc;
        }
        L->en[L->nen].note = (u32)n; L->en[L->nen].para = j; L->en[L->nen].page = fl->page; L->en[L->nen].y = fl->y + f.f[j].p->pf.before;
        L->nen++;
        fl->y += ph;
      }
      lay_furniture_free(&f);
    }
  }
  return 1;
}
/* place the laid out lines on pages and in columns: sections and their page setups, page and column
   breaks, tables, floating objects, keeping with the next, keeping lines together, widows and orphans,
   room for footnotes, paragraph borders, line numbers, then the endnotes. Stops at the first
   paragraph not laid out (L->complete 0). Then each page's range of placed lines and its footnotes.
   0 if out of memory */
static int paginate(Layout *L)
{
  Doc *d = L->d;
  Flow fl;
  u32 pi, k, npages;
  i32 body;
  int web = L->webw > 0;
  NoteFlow nf;                           /* (WP22b) footnotes' room, a long one going on overleaf */
  static u8 *edone; static u32 edcap;    /* (WP22b) endnotes placed already */
  u32 secfirst = 0;                      /* the section's first paragraph */
  int endsect;                           /* endnotes at each section's end */
  memset(&fl, 0, sizeof fl);
  L->oom = 0;
  fl.last = L->lastfirst == 0;
  fl.srcpg = sect_page(d, L->npsec ? L->psec[0] : NULL, fl.last);
  L->pg = pages_eff(L, fl.srcpg, L->npsec ? L->psec[0] : NULL, 0);
  body = web ? L->webw : page_body(L->pg);
  fl.sec = L->npsec ? L->psec[0] : NULL;
  /* a header or footer taller than its margin pushes the text in, as in Word */
  flow_area(L, &fl, fl.sec, web);
  fl.page = 0; fl.y = fl.coltop = fl.maxy = fl.top; fl.at_top = 1;
  flow_cols(&fl, fl.sec, body, web);
  fl.regfirst = 0;
  if (!page_set(L, &fl)) return 0;
  L->nplaced = 0; L->ncells = 0; L->nseps = 0; L->npn = 0; L->complete = 1; L->nfloats = 0; L->npbox = 0;
  L->nen = 0; L->nenseps = 0; L->nncut = 0;              /* (WP22b) */
  nflow_init(L, &nf);
  { NotePr ep; notes_eff(d, NULL, 1, &ep); endsect = ep.pos == NPOS_SECTEND && !web; }
  if ((u32)d->nenotes > edcap) { u8 *e = (u8 *)realloc(edone, (size_t)d->nenotes); if (e) { edone = e; edcap = (u32)d->nenotes; } }
  if (edone && edcap) memset(edone, 0, edcap);
  for (k = 0; k < L->nearly; k++) {
    const FloatBox *e = &L->early[k];
    if (e->img >= d->nimages || !add_float(L, e->page, e->x, e->y, e->img, e->para)) continue;
    L->floats[L->nfloats - 1].early = 1;
  }
  for (pi = 0; pi < L->npl; ) {
    ParaLay *pl = &L->pl[pi];
    const ParaFmt *pf = &pl->p->pf;
    i32 need, lnd, topx;
    int ctxprev, ctxnext, gprev, gnext;
    if (!pl->laid) { L->complete = 0; break; }
    if (L->oom) return 0;
    /* a new section: after a page break of its kind, or on the same page below the last (unless its
       page is set up differently: then a new page too) */
    if (pi > 0 && L->pl[pi - 1].p->pf.sect && pi < L->npsec) {
      const SectFmt *s = L->psec[pi];
      int last = pi >= L->lastfirst, newpage = 0;
      const PageSetup *npg = web ? L->pg : sect_page(d, s, last);
      int same = !memcmp(npg, fl.srcpg ? fl.srcpg : L->pg, sizeof *npg);
      if (!web && s->start == SB_CONT && same) balance_cols(L, &fl);      /* (WP22a) */
      end_region(L, &fl);
      if (endsect) {
        /* (WP22b) the section's endnotes after its text */
        if (fl.maxy > fl.y) fl.y = fl.maxy;
        if (!place_enotes(L, &fl, (u32)d->nenotes <= edcap ? edone : NULL, secfirst, pi, nf.reserve, nf.respage)) return 0;
        fl.maxy = fl.y;
      }
      secfirst = pi;
      if (!web) {
        if (s->start == SB_CONT && same) {
          fl.y = fl.maxy;
          if (fl.y >= fl.bottom - 240) new_page(L, &fl), newpage = 1;
        } else {
          int parity;
          if (!fl.at_top || fl.col > 0 || fl.y > fl.top) { fl.sec = L->psec[pi - 1]; new_page(L, &fl); }
          parity = s->start == SB_EVEN ? 1 : s->start == SB_ODD ? 0 : -1;    /* page 2 is index 1 */
          if (parity >= 0 && (int)(fl.page & 1) != parity) new_page(L, &fl);
          newpage = 1;
        }
      }
      fl.sec = s; fl.last = last;
      if (newpage) {
        /* the section's own page: its size, margins, headers and footers */
        fl.srcpg = npg; L->pg = web ? npg : pages_eff(L, npg, s, fl.page);
        fl.sfirst = fl.page;
        flow_area(L, &fl, s, web);
        fl.y = fl.coltop = fl.maxy = fl.top; fl.at_top = 1;
        if (!page_set(L, &fl)) return 0;
      }
      body = web ? L->webw : page_body(L->pg);
      flow_cols(&fl, s, body, web);
      fl.coltop = fl.maxy = fl.y; fl.regfirst = L->nplaced;
      if (s->linenum == LN_SECTION) fl.lnum = 0;
    }
    if (pf->pagebreak && !fl.at_top && !web) new_page(L, &fl);          /* starts a new page */
    else if (pf->colbreak && !fl.at_top && !web) { if (fl.ncols > 1) flow_next(L, &fl); else new_page(L, &fl); }
    if (in_table(pf)) {
      u32 was = pi;
      if (!place_table(L, &pi, &fl)) return 0;
      if (pi < L->npl && !L->pl[pi].laid) { L->complete = 0; break; }
      if (pi == was) pi++;
      continue;
    }
    /* space before: not at the top of a page the text flowed onto (but at the document's start, and
       after a page break, as Word) */
    /* contextual spacing: none between paragraphs of the same style */
    ctxprev = pi > 0 && pf->contextual && !in_table(&L->pl[pi - 1].p->pf) && L->pl[pi - 1].p->pf.style == pf->style;
    ctxnext = pi + 1 < L->npl && pf->contextual && !in_table(&L->pl[pi + 1].p->pf) && L->pl[pi + 1].p->pf.style == pf->style;
    if ((!fl.at_top || pi == 0 || pf->pagebreak) && !ctxprev) fl.y += pf->before;
    /* its floating objects, and its lines round those in their way; in columns of different widths,
       each line as wide as the column it will go in (WP22a) */
    if (fl.unequal && !web) col_shape(&fl, pl);
    if (!place_floats(L, pi, &fl)) return 0;
    col_on = 0;
    /* keep with next: this paragraph and the next one's first line on one page; keep lines together:
       the whole paragraph; else no orphaned first line */
    need = para_height(pl, 0, pl->nlines);
    if (pf->keepnext && pi + 1 < L->npl && L->pl[pi + 1].nlines) need += L->pl[pi + 1].p->pf.before + L->pl[pi + 1].lines[0].height;
    if (!fl.at_top && fl.y + need > fl.bottom && need <= fl.bottom - fl.coltop &&
        (pf->keepnext || pf->keeplines || (!pf->nowidow && fl.y + para_height(pl, 0, 2 < pl->nlines ? 2 : pl->nlines) > fl.bottom))) {
      flow_next(L, &fl);                     /* the paragraph starts a new column or page */
    }
    /* its border box: the room for its top line (or the line between it and the paragraph before) */
    gprev = pi > 0 && same_box(&L->pl[pi - 1].p->pf, pf);
    gnext = pi + 1 < L->npl && same_box(pf, &L->pl[pi + 1].p->pf);
    topx = boxed(pf) && !web ? (gprev ? bextra(pf, PE_BETWEEN) : bextra(pf, PE_TOP)) : 0;
    fl.y += topx;
    lnd = fl.sec ? fl.sec->lndist : 360;
    pl->placed0 = L->nplaced;
    for (k = 0; k < pl->nlines; k++) {
      i32 h = pl->lines[k].height, extra = 0;
      int over;
      if (d->nnotes && !web) {
        /* the footnotes this line refers to must fit at the bottom of the same page (WP22b: a page
           starts with the rest of a long one from the page before) */
        if (nf.respage != fl.page) nflow_page(L, &nf, fl.page, fl.bottom - fl.top, 0);
        extra = nflow_line(L, &nf, pl, k);
      }
      over = fl.y + h > fl.bottom - nf.reserve - extra;
      if (over && extra && nflow_split(L, &nf, pl, k, fl.page, fl.bottom - nf.reserve - fl.y - h, &extra)) over = 0;   /* (WP22b: a long footnote split) */
      if (over && !fl.at_top) {
        /* widow control: never leave a paragraph's last line alone at the top of a page */
        if (!pf->nowidow && k + 1 == pl->nlines && k >= 2 && L->nplaced > pl->placed0 && L->placed[L->nplaced - 1].page == fl.page) {
          L->nplaced--; k--; h = pl->lines[k].height;
          if (fl.sec && fl.sec->linenum != LN_NONE && !web && fl.lnum) fl.lnum--;     /* (counted, shown or not) */
        }
        flow_next(L, &fl);
        if (d->nnotes && !web) {         /* the separator again on the new page */
          if (nf.respage != fl.page) nflow_page(L, &nf, fl.page, fl.bottom - fl.top, 0);
          extra = nflow_line(L, &nf, pl, k);
          if (extra && fl.y + h > fl.bottom - nf.reserve - extra) nflow_split(L, &nf, pl, k, fl.page, fl.bottom - nf.reserve - fl.y - h, &extra);
        }
      }
      if (!add_placed(L, pi, k, fl.page, fl.y, fl.colx, fl.colw)) return 0;
      if (fl.sec && fl.sec->linenum != LN_NONE && !web) {
        u32 shown = ++fl.lnum + (fl.sec->lnstart > 1 ? fl.sec->lnstart - 1 : 0);   /* (numbered from the section's start value) */
        if (shown % (fl.sec->lnby ? fl.sec->lnby : 1) == 0) { L->placed[L->nplaced - 1].lnum = shown; L->placed[L->nplaced - 1].lnd = lnd; }
      }
      fl.y += h; fl.at_top = 0;
      nf.reserve += extra;
    }
    if (boxed(pf) && !web) {
      i32 botx = gnext ? 0 : bextra(pf, PE_BOTTOM);
      fl.y += botx;
      if (!add_pboxes(L, pi, topx, botx, gprev, gnext)) return 0;
    }
    if (!ctxnext) fl.y += pf->after;
    pi++;
  }
  end_region(L, &fl);
  if (fl.maxy > fl.y) fl.y = fl.maxy;
  /* (WP22b) the rest of a long footnote on pages of its own */
  if (d->nnotes && !web && L->complete)
    while (nf.cn >= 0) { if (nf.respage == fl.page) new_page(L, &fl); nflow_page(L, &nf, fl.page, fl.bottom - fl.top, 1); }
  /* the endnotes after the text, in the order they are referred to (once all of it is laid out): all
     of them, or (at each section's end) the last section's */
  if (L->complete && d->nenotes && L->enotenum && !place_enotes(L, &fl, (u32)d->nenotes <= edcap ? edone : NULL, endsect ? secfirst : 0, d->np, nf.reserve, nf.respage)) return 0;
  fl.nofinish = !L->complete;
  finish_page(L, &fl);
  /* early floats whose anchors didn't come (gone, or not laid out yet) */
  for (k = 0, pi = 0; k < L->nfloats; k++) if (!L->floats[k].early) L->floats[pi++] = L->floats[k];
  L->nfloats = pi;
  /* each page's lines */
  npages = fl.page + 1;
  if (npages > L->pagecap) {
    u32 nc = npages + 32, *a = (u32 *)realloc(L->page_first, nc * sizeof(u32)), *b;
    if (!a) return 0;
    L->page_first = a;
    if (!(b = (u32 *)realloc(L->page_end, nc * sizeof(u32)))) return 0;
    L->page_end = b; L->pagecap = nc;
  }
  L->npages = npages;
  for (k = 0; k < npages; k++) { L->page_first[k] = L->nplaced; L->page_end[k] = 0; }
  for (k = 0; k < L->nplaced; k++) {
    u32 pg = L->placed[k].page;
    if (k < L->page_first[pg]) L->page_first[pg] = k;
    if (k + 1 > L->page_end[pg]) L->page_end[pg] = k + 1;
  }
  for (k = 0; k < npages; k++) if (L->page_end[k] < L->page_first[k]) L->page_end[k] = L->page_first[k];
  /* the footnotes each page shows: those its lines refer to, in order (WP22b: after the rest of one
     continued from the page before) */
  L->npn = 0;
  if (d->nnotes && L->webw <= 0) {
    u32 c = 0;                           /* the next continuation (L->ncut, in page order) */
    for (k = 0; k <= L->nplaced; k++) {
      Placed *pc = k < L->nplaced ? &L->placed[k] : NULL;
      ParaLay *pl = pc ? &L->pl[pc->para] : NULL;
      Line *ln = pc ? &pl->lines[pc->line] : NULL;
      u32 b, q;
      for (b = ln ? ln->box0 : 0; !ln || b < ln->box0 + ln->nbox; b++) {
        u32 n = 0, pg = pc ? pc->page : 0xFFFFFFFFUL;
        if (ln && pl->boxes[b].kind != BX_NOTE) continue;
        /* continuations due on this page (or any left at the end) come first */
        while (c < L->nncut && (L->ncut[c].l0 == 0 || L->ncut[c].page <= pg)) {
          if (L->ncut[c].l0 > 0) {
            if (L->npn >= L->pncap) { u32 nc = L->pncap ? L->pncap * 2 : 32; void *np2 = realloc(L->pn, nc * sizeof(L->pn[0])); if (!np2) return 0; L->pn = np2; L->pncap = nc; }
            L->pn[L->npn].page = L->ncut[c].page; L->pn[L->npn].note = L->ncut[c].note; L->npn++;
          }
          c++;
        }
        if (!ln) break;
        n = (u32)note_index(pl->p->t + pl->boxes[b].off);
        for (q = 0; q < L->npn; q++) if (L->pn[q].page == pc->page && L->pn[q].note == n) break;
        if (q < L->npn) continue;
        if (L->npn >= L->pncap) {
          u32 nc = L->pncap ? L->pncap * 2 : 32;
          void *np2 = realloc(L->pn, nc * sizeof(L->pn[0]));
          if (!np2) return 0;
          L->pn = np2; L->pncap = nc;
        }
        L->pn[L->npn].page = pc->page; L->pn[L->npn].note = n; L->npn++;
      }
    }
    /* footnotes numbered afresh on each page */
    if (L->fnn) {
      u32 cnt = 0, pg = 0xFFFFFFFFUL, l0, l1;
      for (k = 0; k < L->npn; k++) {
        u32 n = L->pn[k].note;
        if (n >= (u32)d->nnotes || !L->fnn[n].perpage || (note_cut(L, L->pn[k].page, n, &l0, &l1) && l0 > 0)) continue;
        if (L->pn[k].page != pg) { pg = L->pn[k].page; cnt = 0; }
        L->fnn[n].lab = L->fnn[n].start + cnt++;
      }
    }
  }
  return 1;
}
/* see layout.h; an empty range for a page that doesn't exist */
void lay_page_lines(Layout *L, u32 page, u32 *a, u32 *b)
{
  *a = *b = 0;
  if (page < L->npages) { *a = L->page_first[page]; *b = L->page_end[page]; }
}
/* list labels: counters per list and level, in document order. roman: n in roman numerals, upper or
   lower case; 0 and numbers past 3999 as digits */
static void roman(char *o, u32 n, int upper)
{
  static const char *const r[13] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
  static const u32 v[13] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
  int i;
  *o = 0;
  if (!n || n > 3999) { sprintf(o, "%lu", (unsigned long)n); return; }
  for (i = 0; i < 13; i++) while (n >= v[i]) { strcat(o, r[i]); n -= v[i]; }
  if (upper) for (; *o; o++) if (*o >= 'a' && *o <= 'z') *o = (char)(*o - 32);
}
/* a list counter in a numbering format (NF_): letters go round again doubled past z (aa, bb...),
   as Word's do, up to 8 of them */
static void counter_text(char *o, u32 n, int fmt)
{
  switch (fmt) {
  case NF_LOWER_LETTER: case NF_UPPER_LETTER: {
    int k = (int)((n - 1) % 26), rep = (int)((n - 1) / 26) + 1, i;
    if (rep > 8) rep = 8;
    for (i = 0; i < rep; i++) o[i] = (char)((fmt == NF_UPPER_LETTER ? 'A' : 'a') + k);
    o[rep] = 0;
    break; }
  case NF_LOWER_ROMAN: roman(o, n, 0); break;
  case NF_UPPER_ROMAN: roman(o, n, 1); break;
  case NF_NONE: *o = 0; break;
  default: if (!nf_counter(o, n, fmt)) sprintf(o, "%lu", (unsigned long)n);   /* (WP10's formats: charfx.c) */
  }
}
/* the next label for a list paragraph: steps its level's counter (deeper levels start again) and
   fills out from the level's text, %1..%9 standing for the levels' counters, or gives the bullet.
   "" if not in a list. cnt holds 9 counters per list */
static void make_label(Doc *d, u32 *cnt, const ParaFmt *pf, char *out, size_t cap)
{
  const ListDef *l;
  const ListLevel *lv;
  u32 *c;
  int k;
  size_t o = 0;
  const char *t;
  out[0] = 0;
  if (!pf->list || pf->list > d->nlists || pf->level > 8) return;
  l = &d->lists[pf->list - 1];
  lv = &l->lvl[pf->level];
  c = cnt + (pf->list - 1) * 9;
  c[pf->level] = c[pf->level] ? c[pf->level] + 1 : lv->start;
  for (k = pf->level + 1; k < 9; k++) c[k] = 0;          /* deeper levels start again */
  if (lv->fmt == NF_BULLET) { strncpy(out, lv->text[0] ? lv->text : "\xE2\x80\xA2", cap - 1); out[cap - 1] = 0; return; }
  for (t = lv->text; *t && o + 12 < cap; t++) {
    if (t[0] == '%' && t[1] >= '1' && t[1] <= '9') {
      int lvl = t[1] - '1';
      char num[16];
      counter_text(num, c[lvl] ? c[lvl] : l->lvl[lvl].start, lvl == pf->level ? lv->fmt : l->lvl[lvl].fmt);
      strcpy(out + o, num); o += strlen(num);
      t++;
    } else out[o++] = *t;
  }
  out[o] = 0;
}
/* the width of a section's columns */
static i32 sect_width(const SectFmt *s, i32 body, int web)
{
  Flow fl;
  flow_cols(&fl, s, body, web);
  return fl.colw;
}
/* (WP22b) the notes paragraph i refers to numbered as their section's options say (*fc, *ec: the
   footnotes and endnotes counted so far, started again where the options say). Footnotes counted per
   page get their numbers once pagination knows the pages */
static void number_notes(Layout *L, u32 i, u32 *fc, u32 *ec)
{
  Doc *d = L->d;
  Para *p = d->p[i];
  NotePr fp, ep;
  u32 k;
  const SectFmt *sec = i < L->npsec ? L->psec[i] : NULL;
  notes_eff(d, sec, 0, &fp); notes_eff(d, sec, 1, &ep);
  if (i > 0 && d->p[i - 1]->pf.sect) { if (fp.restart == NRS_SECTION) *fc = 0; if (ep.restart == NRS_SECTION) *ec = 0; }
  for (k = 0; k + 4 <= p->len; k++) {
    int n, end;
    NoteNum *nn;
    const NotePr *np;
    if (IS_NOTE(p->t + k)) end = 0; else if (IS_ENOTE(p->t + k)) end = 1; else continue;
    n = note_index(p->t + k); k += 3;
    nn = end ? L->enn : L->fnn; np = end ? &ep : &fp;
    if (!nn || n >= (end ? d->nenotes : d->nnotes) || nn[n].lab) continue;
    if (end) ++*ec; else ++*fc;
    nn[n].lab = np->start + (end ? *ec : *fc) - 1; if (!nn[n].lab) nn[n].lab = 1;
    nn[n].fmt = np->fmt; nn[n].start = np->start; nn[n].perpage = (u8)(np->restart == NRS_PAGE);
  }
}
/* see layout.h. Old paragraph layouts are matched to the document's paragraphs by pointer (looking at
   most 64 ahead, as edits are local) and kept if the version, width, label, note numbers and
   hyphenation are the same and they didn't go round floating objects. Notes are numbered here too.
   Then pagination, up to twice more while floats placed from the page or margin move */
int lay_update_part(Layout *L, u32 budget)
{
  Doc *d = L->d;
  ParaLay *npl;
  u32 i, j = 0, *cnt = NULL;
  char label[32];
  int stopped = 0, oom = 0;
  u32 notes = 0, enotes = 0, fshown = 0, eshown = 0;
  L->laid = 0;
  if (!(npl = (ParaLay *)calloc(d->np ? d->np : 1, sizeof(ParaLay)))) return 0;
  if (d->nlists && !(cnt = (u32 *)calloc((size_t)d->nlists * 9, sizeof(u32)))) { free(npl); return 0; }
  /* footnotes are numbered in the order the text refers to them */
  free(L->notenum); L->notenum = NULL;
  if (d->nnotes && !(L->notenum = (u32 *)calloc((size_t)d->nnotes, sizeof(u32)))) { free(npl); free(cnt); return 0; }
  free(L->enotenum); L->enotenum = NULL;
  if (d->nenotes && !(L->enotenum = (u32 *)calloc((size_t)d->nenotes, sizeof(u32)))) { free(npl); free(cnt); return 0; }
  free(L->fnn); free(L->enn);                     /* (WP22b) their numbers as shown */
  L->fnn = d->nnotes ? (NoteNum *)calloc((size_t)d->nnotes, sizeof(NoteNum)) : NULL;
  L->enn = d->nenotes ? (NoteNum *)calloc((size_t)d->nenotes, sizeof(NoteNum)) : NULL;
  /* each paragraph's section (the format is on the paragraph that ends it) */
  {
    static SectFmt def;
    const SectFmt **ps = (const SectFmt **)realloc(L->psec, (d->np ? d->np : 1) * sizeof(*ps)), *cur;
    if (!ps) { free(npl); free(cnt); return 0; }
    if (!def.cols) sect_default(&def);
    L->psec = ps; L->npsec = d->np;
    cur = &def;
    for (i = d->np; i-- > 0; ) {
      if (d->p[i]->pf.sect && d->p[i]->pf.sect <= d->nsects) cur = &d->sects[d->p[i]->pf.sect - 1];
      else if (d->p[i]->pf.sect || i + 1 == d->np) cur = &def;
      ps[i] = cur;
    }
    /* where the last section starts (its page setup and headers are the document's) */
    for (L->lastfirst = d->np ? d->np - 1 : 0; L->lastfirst > 0 && !d->p[L->lastfirst - 1]->pf.sect; L->lastfirst--) ;
  }
  /* edits are local: walk old and new together, looking a little ahead for each paragraph */
  for (i = 0; i < d->np; i++) {
    Para *p = d->p[i];
    u32 k, found = L->npl, notebase = notes, enotebase = enotes;
    i32 body = L->webw > 0 ? L->webw : page_body(pages_eff(L, sect_page(d, L->psec[i], i >= L->lastfirst), L->psec[i], 0)), avail;
    if (body < 1440) body = 1440;
    avail = para_avail(d, &p->pf, sect_width(L->psec[i], body, L->webw > 0));
    make_label(d, cnt, &p->pf, label, sizeof label);
    if (d->nnotes)
      for (k = 0; k + 4 <= p->len; k++)
        if (IS_NOTE(p->t + k)) { int n = note_index(p->t + k); notes++; if (n < d->nnotes && !L->notenum[n]) L->notenum[n] = notes; k += 3; }
    if (d->nenotes)
      for (k = 0; k + 4 <= p->len; k++)
        if (IS_ENOTE(p->t + k)) { int n = note_index(p->t + k); enotes++; if (n < d->nenotes && !L->enotenum[n]) L->enotenum[n] = enotes; k += 3; }
    if (d->nnotes || d->nenotes) number_notes(L, i, &fshown, &eshown);   /* (WP22b) */
    for (k = j; k < L->npl && k < j + 64; k++) if (L->pl[k].p == p) { found = k; break; }
    if (found < L->npl && L->pl[found].laid && L->pl[found].ver == p->ver && L->pl[found].avail == avail && !strcmp(L->pl[found].label, label) &&
        L->pl[found].notebase == notebase && L->pl[found].enotebase == enotebase && L->pl[found].hyph == d->hyphenate && !L->pl[found].shaped &&
        (L->pl[found].hidden == 2 || L->pl[found].hidden == L->showhidden) &&   /* (WP9: hidden text shown or not) */
        L->pl[found].rvsig == rv_sig(d)) {
      npl[i] = L->pl[found];
      memset(&L->pl[found], 0, sizeof(ParaLay));
      j = found + 1;
    } else {
      if (found < L->npl) j = found + 1;
      /* a partial update stops at a paragraph outside a table once its budget is spent */
      if (budget && L->laid >= budget && !in_table(&p->pf)) stopped = 1;
      if (stopped) { npl[i].p = p; npl[i].ver = p->ver; continue; }
      /* the old layout goes first: a long document laid out again whole (a new width) needn't fit twice */
      if (found < L->npl) pl_free(&L->pl[found]);
      if (!layout_para(L, &npl[i], p, avail, label, notebase, enotebase)) {
        /* out of memory: this paragraph and those after it wait (laid out as far as there was room) */
        pl_free(&npl[i]);
        npl[i].p = p; npl[i].ver = p->ver;
        stopped = oom = 1;
        continue;
      }
      npl[i].laid = 1;
      npl[i].rvsig = rv_sig(d);                   /* (WP5) */
      L->laid++;
    }
  }
  free(cnt);
  for (i = 0; i < L->npl; i++) pl_free(&L->pl[i]);
  free(L->pl);
  L->pl = npl; L->npl = d->np;
  /* again while the floats placed from the page or margin move (twice more at most) */
  for (i = 0; i < 3; i++) {
    if (!paginate(L)) return 0;
    if (floats_settled(L)) break;
  }
  return !oom;
}
/* see layout.h */
int lay_bmark_page(Layout *L, const char *name)
{
  Pos a;
  u32 pg;
  i32 x, y, h;
  if (!name[0] || !doc_bmark_find(L->d, name, &a, NULL) || a.para >= L->npl || !L->pl[a.para].laid || !L->pl[a.para].nlines ||
      L->pl[a.para].placed0 >= L->nplaced) return 0;
  lay_caret(L, a, &pg, &x, &y, &h);
  return (int)pg + 1;
}
/* see layout.h: everything, no budget */
int lay_update(Layout *L) { return lay_update_part(L, 0); }

/* ------------------------------------------------------------------ */
/* caret and hit testing */
/* how far a drop cap's baseline is below the first line's: down to the baseline of the last line it
   reaches (b isn't used) */
i32 lay_drop_dy(const ParaLay *pl, const Box *b)
{
  u32 n = pl->p->pf.dropcap, k;
  i32 dy = 0, h0;
  int padded;
  (void)b;
  if (n < 2 || !pl->nlines) return 0;
  /* a paragraph shorter than the cap has its last line made taller to make room (see para_layout):
     the lines it doesn't have count as high as its first line was */
  padded = pl->nlines < n;
  h0 = padded && pl->nlines == 1 ? pl->lines[0].height / (i32)n : pl->lines[0].height;
  for (k = 0; k + 1 < n; k++) dy += !padded || k + 1 < pl->nlines ? pl->lines[k].height : h0;
  if (n - 1 < pl->nlines) dy += pl->lines[n - 1].ascent - pl->lines[0].ascent;
  return dy;
}
/* x (twips, like the box's) of byte off in box b: the box's start or end for an offset outside it
   or a box that isn't text, else measured */
i32 lay_text_x(Layout *L, const Box *b, const char *t, u32 off)
{
  FaceSize *z;
  if (off <= b->off) return b->x;
  if (b->kind != BX_TEXT || off >= b->off + b->len) return b->x + b->w;
  z = lay_face(L, &b->f);
  return b->x + (i32)((measure(L, z, t + b->off, off - b->off, &b->f) + QFINE / 2) / QFINE);
}
/* the line byte off is on; a wrap point belongs to the line after it */
static u32 line_of(ParaLay *pl, u32 off)
{
  u32 k;
  for (k = 0; k + 1 < pl->nlines; k++) if (off < pl->lines[k + 1].start) break;   /* a wrap point: the next line */
  return k;
}
/* see layout.h; x from the page's left edge. The first page's top left margin corner if p's
   paragraph isn't placed yet */
void lay_caret(Layout *L, Pos p, u32 *page, i32 *x, i32 *y, i32 *h)
{
  ParaLay *pl;
  Line *ln;
  Placed *pc;
  u32 k, li;
  i32 cx;
  *page = 0; *x = L->d->page.left; *y = L->d->page.top; *h = 276;
  p = pos_clamp(L->d, p);
  if (p.para >= L->npl) return;
  pl = &L->pl[p.para];
  if (!pl->nlines || pl->placed0 >= L->nplaced) return;
  li = line_of(pl, p.off);
  ln = &pl->lines[li];
  pc = &L->placed[pl->placed0 + li];
  cx = 0;
  for (k = ln->box0; k < ln->box0 + ln->nbox; k++) {
    Box *b = &pl->boxes[k];
    if (p.off >= b->off && p.off <= b->off + b->len) { cx = lay_text_x(L, b, pl->p->t, p.off); break; }
    cx = b->x + b->w;
  }
  *page = pc->page;
  *x = lay_page(L, pc->page)->left + pc->x + ln->left + cx;
  *y = pc->y; *h = ln->height;
}
/* the position on line li of paragraph para nearest x (from the text area's left; the line's own
   indent is taken off here), choosing between characters by halves of their width */
static Pos pos_in_line(Layout *L, u32 para, u32 li, i32 x)     /* x from the text area's left */
{
  ParaLay *pl = &L->pl[para];
  Line *ln = &pl->lines[li];
  Pos r;
  u32 k;
  r.para = para; r.off = ln->start;
  x -= ln->left;
  for (k = ln->box0; k < ln->box0 + ln->nbox; k++) {
    Box *b = &pl->boxes[k];
    if (x < b->x + b->w || k + 1 == ln->box0 + ln->nbox) {
      u32 o = b->off, end = b->off + b->len;
      if (b->kind != BX_TEXT) { r.off = (x - b->x) * 2 < b->w ? b->off : end; break; }
      while (o < end) {
        u32 nx = (u32)utf8_next(pl->p->t, end, o);
        i32 a = lay_text_x(L, b, pl->p->t, o), c = lay_text_x(L, b, pl->p->t, nx);
        if (x < (a + c) / 2) break;
        o = nx;
      }
      r.off = o;
      break;
    }
  }
  /* the end of a line that wraps is the start of the next: stay before its trailing spaces' end */
  if (li + 1 < pl->nlines && r.off >= pl->lines[li + 1].start) r.off = (u32)utf8_prev(pl->p->t, pl->lines[li + 1].start);
  if (!ln->nbox) r.off = ln->start;
  return r;
}
/* how far a point is from a placed line's box (0 inside) */
static i32 dist(Layout *L, Placed *pc, i32 x, i32 y)
{
  i32 h = L->pl[pc->para].lines[pc->line].height, dx = 0, dy = 0;
  x -= lay_page(L, pc->page)->left;
  if (y < pc->y) dy = pc->y - y; else if (y >= pc->y + h) dy = y - (pc->y + h) + 1;
  if (x < pc->x) dx = pc->x - x; else if (x >= pc->x + pc->w) dx = x - (pc->x + pc->w) + 1;
  return dx + dy;
}
/* see layout.h: the nearest placed line on the page (by across plus down distance), then the place
   in it. Past the last page, or on a page of only endnotes, the document's end once the layout is
   complete; else its start */
Pos lay_hit(Layout *L, u32 page, i32 x, i32 y)
{
  u32 a, b, k, best = 0;
  i32 bd = 0x7FFFFFFF;
  Pos r = doc_start(L->d);
  if (!L->npages) return r;
  if (page >= L->npages) return L->complete ? doc_end_pos(L->d) : r;
  lay_page_lines(L, page, &a, &b);
  if (a >= b) return L->complete && page > 0 ? doc_end_pos(L->d) : r;     /* a page of endnotes */
  for (k = a; k < b; k++) {
    Placed *pc = &L->placed[k];
    i32 dd;
    if (pc->page != page) continue;
    dd = dist(L, pc, x, y);
    if (dd < bd) { bd = dd; best = k; if (!dd) break; }
  }
  if (bd == 0x7FFFFFFF) return r;
  return pos_in_line(L, L->placed[best].para, L->placed[best].line, x - lay_page(L, page)->left - L->placed[best].x);
}
/* see layout.h: looks through the placed lines nearby for the first in the same column above
   (below), else takes the line before (after) in placed order. From the first line up goes to the
   document's start, from the last down to its end */
Pos lay_updown(Layout *L, Pos p, i32 x, int dir)
{
  ParaLay *pl;
  Placed *cur;
  u32 li, idx, k, n;
  i32 ch, rx;
  p = pos_clamp(L->d, p);
  if (p.para >= L->npl || !L->pl[p.para].nlines || L->pl[p.para].placed0 >= L->nplaced) return p;
  pl = &L->pl[p.para];
  li = line_of(pl, p.off);
  idx = pl->placed0 + li;
  rx = x - lay_page(L, L->placed[idx].page)->left;
  if (dir < 0 && idx == 0) return doc_start(L->d);
  if (dir > 0 && idx + 1 >= L->nplaced) return L->complete ? doc_end_pos(L->d) : p;
  cur = &L->placed[idx];
  ch = pl->lines[li].height;
  /* the nearest line above or below in the same column (in tables, placed order goes cell by cell) */
  for (n = 1; n < 2000; n++) {
    Placed *pc;
    if (dir > 0 ? idx + n >= L->nplaced : n > idx) break;
    k = dir > 0 ? idx + n : idx - n;
    pc = &L->placed[k];
    if (rx < pc->x || rx >= pc->x + pc->w) continue;
    if (dir > 0 ? (pc->page > cur->page || (pc->page == cur->page && pc->y >= cur->y + ch - 1))
                : (pc->page < cur->page || (pc->page == cur->page && pc->y + L->pl[pc->para].lines[pc->line].height <= cur->y + 1)))
      return pos_in_line(L, pc->para, pc->line, rx - pc->x);
  }
  k = dir < 0 ? idx - 1 : idx + 1;
  return pos_in_line(L, L->placed[k].para, L->placed[k].line, rx - L->placed[k].x);
}
/* the start of p's line (p clamped to the document first) */
Pos lay_line_start(Layout *L, Pos p)
{
  ParaLay *pl;
  p = pos_clamp(L->d, p);
  if (p.para >= L->npl || !L->pl[p.para].nlines) return p;
  pl = &L->pl[p.para];
  p.off = pl->lines[line_of(pl, p.off)].start;
  return p;
}
/* the end of p's line: before the spaces it wrapped at, or before its line break; the paragraph's
   end on its last line */
Pos lay_line_end(Layout *L, Pos p)
{
  ParaLay *pl;
  u32 li;
  p = pos_clamp(L->d, p);
  if (p.para >= L->npl || !L->pl[p.para].nlines) return p;
  pl = &L->pl[p.para];
  li = line_of(pl, p.off);
  if (li + 1 < pl->nlines) {
    u32 e = pl->lines[li + 1].start;
    if (e > pl->lines[li].start && pl->p->t[e - 1] == '\v') e--;        /* before a line break */
    else while (e > pl->lines[li].start && pl->p->t[e - 1] == ' ') e--;     /* before the spaces at the wrap */
    p.off = e;
  } else p.off = pl->p->len;
  return p;
}

/* AutoFit: the widths a table's columns want for their text. A column's least is its longest word,
   its most its longest line unbroken (from cells of that column only: spanning cells are left out);
   when the most doesn't fit in avail, the room between least and most is shared out in proportion */
int lay_autofit(Layout *L, u32 pi, int window, i32 avail, i32 *grid)
{
  Doc *d = L->d;
  Table *t;
  i32 minw[MAX_COLS], maxw[MAX_COLS], summin = 0, summax = 0, sum = 0;
  u32 i, a, b;
  int k, n;
  if (pi >= d->np || !(t = doc_table(d, &d->p[pi]->pf))) return 0;
  n = t->ncols;
  for (k = 0; k < n; k++) sum += t->grid[k];
  if (window || sum <= 0) {                     /* the same proportions, the column's width */
    for (k = 0; k < n; k++) grid[k] = sum > 0 ? (i32)((long)t->grid[k] * avail / sum) : avail / n;
    return n;
  }
  for (k = 0; k < n; k++) { minw[k] = maxw[k] = t->marl + t->marr + 120; }
  a = pi; while (a > 0 && d->p[a - 1]->pf.cell.table == d->p[pi]->pf.cell.table) a--;
  for (b = pi; b < d->np && d->p[b]->pf.cell.table == d->p[pi]->pf.cell.table; b++) ;
  for (i = a; i < b; i++) {
    Para *p = d->p[i];
    ParaLay pl;
    u32 ln, bx;
    i32 lo = 0, hi = 0, ind = p->pf.left + p->pf.right + (p->pf.first > 0 ? p->pf.first : 0) + t->marl + t->marr;
    int c = p->pf.cell.col;
    if (p->pf.cell.span != 1 || c >= n) continue;
    memset(&pl, 0, sizeof pl);
    if (!layout_para(L, &pl, p, 0x3FFFFFF, "", 0, 0)) { pl_free(&pl); return 0; }
    for (ln = 0; ln < pl.nlines; ln++) if (pl.lines[ln].width > hi) hi = pl.lines[ln].width;
    for (bx = 0; bx < pl.nboxes; bx++) if (pl.boxes[bx].kind != BX_SPACE && pl.boxes[bx].w > lo) lo = pl.boxes[bx].w;
    pl_free(&pl);
    if (lo + ind > minw[c]) minw[c] = lo + ind;
    if (hi + ind + 30 > maxw[c]) maxw[c] = hi + ind + 30;
  }
  for (k = 0; k < n; k++) { if (maxw[k] < minw[k]) maxw[k] = minw[k]; summin += minw[k]; summax += maxw[k]; }
  for (k = 0; k < n; k++) {
    if (summax <= avail) grid[k] = maxw[k];                                     /* everything on one line */
    else if (summin >= avail) grid[k] = (i32)((long)minw[k] * avail / summin);  /* even the words don't fit: shrink */
    else grid[k] = minw[k] + (i32)((long)(maxw[k] - minw[k]) * (avail - summin) / (summax - summin));
  }
  return n;
}

/* sin of 0..90 degrees in 16 steps, x 16384 */
static const i32 qsin[17] = { 0, 1606, 3196, 4756, 6270, 7723, 9102, 10394, 11585, 12665, 13623, 14449, 15137, 15679, 16069, 16305, 16384 };
/* sine and cosine of i 64ths of a turn, x 16384 */
static i32 fsin(int i) { i &= 63; return i <= 16 ? qsin[i] : i <= 32 ? qsin[32 - i] : i <= 48 ? -qsin[i - 32] : -qsin[64 - i]; }
static i32 fcos(int i) { return fsin(i + 16); }
#define PT(X, Y) do { if (n < max) { xy[2 * n] = (X); xy[2 * n + 1] = (Y); n++; } } while (0)
/* see layout.h: Word's preset shapes approximated; a shape not known here comes out as its box */
u32 shape_points(int shape, i32 w, i32 h, i32 *xy, u32 max, int *closed)
{
  u32 n = 0;
  int i;
  *closed = 1;
  switch (shape) {
  case SH_ROUNDRECT: {
    /* corners a sixth of the shorter side (Word's default), a quarter circle in 8 steps each */
    i32 r = (w < h ? w : h) / 6;
    static const int cx[4] = { 1, 1, 0, 0 }, cy[4] = { 0, 1, 1, 0 };
    int c;
    for (c = 0; c < 4; c++) {
      i32 ox = cx[c] ? w - r : r, oy = cy[c] ? h - r : r;
      for (i = 0; i <= 8; i++) {
        int a = c * 16 + 48 + i * 2;      /* from the top (corner 0: top right) round clockwise */
        PT(ox + (i32)((long)r * fcos(a) / 16384), oy + (i32)((long)r * fsin(a) / 16384));
      }
    }
    break;
  }
  case SH_ELLIPSE:
    for (i = 0; i < 64; i++) PT(w / 2 + (i32)((long)(w / 2) * fcos(i) / 16384), h / 2 + (i32)((long)(h / 2) * fsin(i) / 16384));
    break;
  case SH_LINE: case SH_ARROW:
    *closed = 0; PT(0, 0); PT(w, h);
    break;
  case SH_TRIANGLE:
    PT(w / 2, 0); PT(w, h); PT(0, h);
    break;
  case SH_DIAMOND:
    PT(w / 2, 0); PT(w, h / 2); PT(w / 2, h); PT(0, h / 2);
    break;
  case SH_RARROW: {
    /* Word's default: the shaft half the height, the head as long as half the shorter side */
    i32 hx = w - (w < h ? w : h) / 2;
    PT(0, h / 4); PT(hx, h / 4); PT(hx, 0); PT(w, h / 2); PT(hx, h); PT(hx, 3 * h / 4); PT(0, 3 * h / 4);
    break;
  }
  case SH_STAR: {
    /* five points; the inner ones at 0.382 of the outer (cos, sin x 10000 from the top, clockwise).
       As Word's star5: the radii stretched so the points touch the box's sides and bottom */
    static const i32 cs[10][2] = { { 0, -10000 }, { 5878, -8090 }, { 9511, -3090 }, { 9511, 3090 }, { 5878, 8090 }, { 0, 10000 },
                                   { -5878, 8090 }, { -9511, 3090 }, { -9511, -3090 }, { -5878, -8090 } };
    for (i = 0; i < 10; i++) {
      long ox = (long)w * 10515 / 20000, oy = (long)h * 11056 / 20000;
      long rx = i & 1 ? ox * 3820 / 10000 : ox, ry = i & 1 ? oy * 3820 / 10000 : oy;
      PT(w / 2 + (i32)(rx * cs[i][0] / 10000), (i32)oy + (i32)(ry * cs[i][1] / 10000));
    }
    break;
  }
  case SH_PENTAGON: case SH_HEART: {
    /* a regular pentagon from the top, or a heart (16 sin^3 t; 13 cos t - 5 cos 2t - 2 cos 3t - cos 4t),
       stretched to fill the box */
    i32 px[40], py[40], x0 = 0x7FFFFFFF, x1 = -0x7FFFFFFF, y0 = 0x7FFFFFFF, y1 = -0x7FFFFFFF;
    int m = shape == SH_PENTAGON ? 5 : 40;
    for (i = 0; i < m; i++) {
      if (shape == SH_PENTAGON) { int a = 2700 + i * 720; px[i] = icos(a); py[i] = isin(a); }
      else {
        int t = i * 90;
        long st = isin(t);
        px[i] = (i32)(st * st / TRIG_ONE * st / TRIG_ONE);
        py[i] = -(13 * icos(t) - 5 * icos(2 * t) - 2 * icos(3 * t) - icos(4 * t)) / 16;
      }
      if (px[i] < x0) x0 = px[i];
      if (px[i] > x1) x1 = px[i];
      if (py[i] < y0) y0 = py[i];
      if (py[i] > y1) y1 = py[i];
    }
    for (i = 0; i < m; i++) PT((i32)((long long)(px[i] - x0) * w / (x1 - x0 ? x1 - x0 : 1)), (i32)((long long)(py[i] - y0) * h / (y1 - y0 ? y1 - y0 : 1)));
    break;
  }
  case SH_HEXAGON:
    PT(w / 4, 0); PT(w - w / 4, 0); PT(w, h / 2); PT(w - w / 4, h); PT(w / 4, h); PT(0, h / 2);
    break;
  case SH_OCTAGON: {
    i32 c = (w < h ? w : h) * 293 / 1000;
    PT(c, 0); PT(w - c, 0); PT(w, c); PT(w, h - c); PT(w - c, h); PT(c, h); PT(0, h - c); PT(0, c);
    break;
  }
  case SH_PARALLELOGRAM: { i32 o = (w < h ? w : h) / 4; PT(o, 0); PT(w, 0); PT(w - o, h); PT(0, h); break; }
  case SH_TRAPEZOID: { i32 o = (w < h ? w : h) / 4; PT(o, 0); PT(w - o, 0); PT(w, h); PT(0, h); break; }
  case SH_RTRIANGLE: PT(0, 0); PT(w, h); PT(0, h); break;
  case SH_LARROW: { i32 hx = (w < h ? w : h) / 2; PT(w, h / 4); PT(hx, h / 4); PT(hx, 0); PT(0, h / 2); PT(hx, h); PT(hx, 3 * h / 4); PT(w, 3 * h / 4); break; }
  case SH_UARROW: { i32 hy = (w < h ? w : h) / 2; PT(w / 4, h); PT(w / 4, hy); PT(0, hy); PT(w / 2, 0); PT(w, hy); PT(3 * w / 4, hy); PT(3 * w / 4, h); break; }
  case SH_DARROW: { i32 hy = h - (w < h ? w : h) / 2; PT(w / 4, 0); PT(3 * w / 4, 0); PT(3 * w / 4, hy); PT(w, hy); PT(w / 2, h); PT(0, hy); PT(w / 4, hy); break; }
  case SH_PLUS: {
    i32 c = (w < h ? w : h) / 4;
    PT(c, 0); PT(w - c, 0); PT(w - c, c); PT(w, c); PT(w, h - c); PT(w - c, h - c); PT(w - c, h); PT(c, h); PT(c, h - c); PT(0, h - c); PT(0, c); PT(c, c);
    /* (Word's plus: arms a quarter in from each side) */
    break;
  }
  case SH_CALLOUT:
    /* a box with a wedge from its bottom edge to a point below it, left of the middle (Word's default) */
    PT(0, 0); PT(w, 0); PT(w, h); PT(w / 3, h); PT(w * 292 / 1000, h + h / 8); PT(w / 6, h); PT(0, h);
    break;
  case SH_CHEVRON: { i32 o = (w < h ? w : h) / 2; PT(0, 0); PT(w - o, 0); PT(w, h / 2); PT(w - o, h); PT(0, h); PT(o, h / 2); break; }
  case SH_HOMEPLATE: { i32 o = (w < h ? w : h) / 2; PT(0, 0); PT(w - o, 0); PT(w, h / 2); PT(w - o, h); PT(0, h); break; }
  case SH_STAR4:
    for (i = 0; i < 8; i++) {
      int a = 2700 + i * 450;
      long r = i & 1 ? TRIG_ONE / 4 : TRIG_ONE;
      PT(w / 2 + (i32)((long long)w / 2 * r / TRIG_ONE * icos(a) / TRIG_ONE), h / 2 + (i32)((long long)h / 2 * r / TRIG_ONE * isin(a) / TRIG_ONE));
    }
    break;
  default:
    if ((n = shapex_points(shape, w, h, xy, max, closed))) break;     /* WP18: more of Word's shapes */
    PT(0, 0); PT(w, 0); PT(w, h); PT(0, h);
  }
  return n;
}
#undef PT
/* SH_ARROW's head: xy gets three points, the tip at (w, h) and then the two barbs */
void shape_arrowhead(i32 w, i32 h, i32 lw, i32 *xy)
{
  /* a triangle three line widths (6 points at least) long and wide, its point at the end, along the line (by its length) */
  long len, dx = w, dy = h, s = lw * 3 < 120 ? 120 : lw * 3, ux, uy;
  /* the length by Newton's way from a fair guess */
  len = labs(dx) + labs(dy);
  if (len) { int k; for (k = 0; k < 8; k++) len = (len + (dx * dx + dy * dy) / len) / 2; }
  if (!len) len = 1;
  ux = dx * 1000 / len; uy = dy * 1000 / len;
  xy[0] = w; xy[1] = h;
  xy[2] = (i32)(w - (ux * s + uy * s / 2) / 1000); xy[3] = (i32)(h - (uy * s - ux * s / 2) / 1000);
  xy[4] = (i32)(w - (ux * s - uy * s / 2) / 1000); xy[5] = (i32)(h - (uy * s + ux * s / 2) / 1000);
}
/* see layout.h: the new place kept as offsets from the margin across, and from the paragraph's top
   (or the margin) down, alignment cleared so the object stays where it was dropped */
void lay_float_moved(Layout *L, const FloatBox *fb, Image *im, i32 nx, i32 ny)
{
  const PageSetup *pg = lay_page(L, fb->page);
  im->hrel = POS_MARGIN; im->halign = PA_NONE; im->x = nx - pg->left;
  if (im->vrel == POS_PARA) {
    /* the paragraph's top: aligned in the paragraph (whose span is its own height) it is at the top */
    i32 py = im->valign == PA_NONE ? fb->y - im->y : fb->y;
    im->valign = PA_NONE; im->y = ny - py;
  } else { im->vrel = POS_MARGIN; im->valign = PA_NONE; im->y = ny - pg->top; }
}

/* ------------------------------------------------------------------ */
/* the order floating objects stack in */
typedef struct { i32 z; u32 i; } ZKey;
/* qsort's comparison: by z, then by place in the list (so equal ones keep their order) */
static int zkey_cmp(const void *a, const void *b)
{
  const ZKey *x = (const ZKey *)a, *y = (const ZKey *)b;
  if (x->z != y->z) return x->z < y->z ? -1 : 1;
  return x->i < y->i ? -1 : x->i > y->i;
}
/* see layout.h */
u32 *lay_stack_order(const Layout *L, const FloatBox *fb, u32 n)
{
  ZKey *k = (ZKey *)malloc((n ? n : 1) * sizeof(ZKey));
  u32 *o = (u32 *)malloc((n ? n : 1) * sizeof(u32)), i;
  if (!k || !o) { free(k); free(o); return NULL; }
  for (i = 0; i < n; i++) { k[i].z = fb[i].img >= 0 && fb[i].img < L->d->nimages ? L->d->images[fb[i].img].z : 0; k[i].i = i; }
  qsort(k, n, sizeof(ZKey), zkey_cmp);
  for (i = 0; i < n; i++) o[i] = k[i].i;
  free(k);
  return o;
}
/* the byte offset of the reference to image img in paragraph pi, or -1 */
static long img_ref(const Doc *d, u32 pi, int img)
{
  const Para *p = pi < d->np ? d->p[pi] : NULL;
  u32 k;
  if (!p) return -1;
  for (k = 0; k + 4 <= p->len; k++) if (IS_PIC(p->t + k) && pic_index(p->t + k) == img) return (long)k;
  return -1;
}
/* see layout.h. The objects on the page are put in their present stacking order, the one moved
   takes its new place, and each is given z = its place (1 up); those whose z changes become new
   entries (doc_set_object), all in one undo step. The FloatBoxes are copied first: setting an object
   changes the document, and L's lists with it once laid out again */
int lay_restack(Layout *L, u32 fi, int how)
{
  Doc *d = L->d;
  FloatBox *fb;
  u32 *ord, n = 0, i, at = 0, to;
  Pos c;
  if (fi >= L->nfloats) return 0;
  if (!(fb = (FloatBox *)malloc(L->nfloats * sizeof(FloatBox)))) return 0;
  for (i = 0; i < L->nfloats; i++) if (L->floats[i].page == L->floats[fi].page) { if (i == fi) at = n; fb[n++] = L->floats[i]; }
  if (!(ord = lay_stack_order(L, fb, n))) { free(fb); return 0; }
  for (i = 0; i < n && ord[i] != at; i++) ;
  at = i;                               /* (now its place in the stack) */
  to = how == 2 ? n - 1 : how == -2 ? 0 : how > 0 ? (at + 1 < n ? at + 1 : at) : (at > 0 ? at - 1 : 0);
  if (to == at) { free(ord); free(fb); return 0; }
  {
    u32 m = ord[at];
    if (to > at) memmove(ord + at, ord + at + 1, (to - at) * sizeof(u32));
    else memmove(ord + to + 1, ord + to, (at - to) * sizeof(u32));
    ord[to] = m;
  }
  c.para = fb[0].para; c.off = 0;
  doc_begin(d, c);
  for (i = 0; i < n; i++) {
    FloatBox *f = &fb[ord[i]];
    long off = img_ref(d, f->para, f->img);
    Image im;
    if (off < 0 || f->img < 0 || f->img >= d->nimages || d->images[f->img].z == (i32)(i + 1)) continue;
    im = d->images[f->img];
    im.z = (i32)(i + 1);
    c.para = f->para; c.off = (u32)off;
    doc_set_object(d, c, &im);
  }
  doc_end(d, c);
  free(ord); free(fb);
  return 1;
}
