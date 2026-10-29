/*
 * markdown.c - Markdown files (docs/wordcraft-gap.md WP28c). This file belongs to WP28c: only that
 * package edits it.
 *
 * Reading: CommonMark's blocks - ATX and setext headings (the heading styles), paragraphs, block
 * quotes (Quote), bullet and ordered lists nested by indentation (Quill lists; ordered ones keep
 * their first number), fenced and indented code blocks (a paragraph per line in the Code style),
 * thematic breaks (an empty paragraph with a line under it), link reference definitions - and
 * GitHub's tables (with column alignment) and footnotes ([^1]: Quill's footnotes). Inline:
 * emphasis and strong emphasis with * and _ (CommonMark's delimiter rules), ~~strikethrough~~, code
 * spans (Courier New), links (inline, reference, <autolinks>), images (data: URIs, or files next to
 * the document), hard line breaks, backslash escapes, entities, and the inline HTML tags b, i, u,
 * s, sup, sub, code and br.
 *
 * Writing: the same, so that what Quill writes reads back as it was as far as Markdown can say it:
 * headings 1-6, bold, italic, strikethrough, code, links, pictures as data: URIs, lists, Code-style
 * paragraphs as fenced blocks, Quote as "> ", tables as GitHub tables (the first row the heading;
 * merged cells repeat nothing), footnotes and endnotes as [^n] and [^en] notes. Fonts, sizes,
 * colours, alignment and page layout have no Markdown and are left out.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "docx.h"
#include "fmtio.h"
#include "image.h"

/* ================================================================== */
/* writing */

typedef struct {
  Doc *d;
  Buf *b;
  int nnote, nenote;
  int *noteorder, *enoteorder;
} MW;

static int is_mono(const Doc *d, const CharFmt *f) { return fmt_mono_font(d->fonts[f->font]); }
/* text with Markdown's punctuation escaped (start: at a line's start, where more is special) */
static void md_esc(Buf *b, const char *s, u32 n, int start, int intable)
{
  u32 i;
  for (i = 0; i < n; i++) {
    char c = s[i];
    if (c == '\\' || c == '`' || c == '*' || c == '_' || c == '[' || c == ']' || c == '<' || c == '>' || c == '~' || (c == '|' && intable)) { buf_addc(b, '\\'); buf_addc(b, c); }
    else if (c == '&') {
      /* an ampersand that would read as an entity */
      if (i + 1 < n && ((s[i + 1] >= 'a' && s[i + 1] <= 'z') || (s[i + 1] >= 'A' && s[i + 1] <= 'Z') || s[i + 1] == '#')) buf_adds(b, "&amp;");
      else buf_addc(b, c);
    } else if (start && i == 0 && (c == '#' || c == '+' || c == '-' || c == '=')) { buf_addc(b, '\\'); buf_addc(b, c); }
    else if (start && i > 0 && (c == '.' || c == ')')) {
      /* "1." at a line's start would be a list */
      u32 k = 0;
      while (k < i && s[k] >= '0' && s[k] <= '9') k++;
      if (k == i) buf_addc(b, '\\');
      buf_addc(b, c);
    } else if (c == '\v') buf_adds(b, intable ? "<br>" : "\\\n");
    else buf_addc(b, c);
    if (c == '\v') start = 1, s += i + 1, n -= i + 1, i = (u32)-1;
  }
}
static int note_num(int *order, int *n, int k)
{
  int i;
  for (i = 0; i < *n; i++) if (order[i] == k) return i + 1;
  order[(*n)++] = k;
  return *n;
}
/* a paragraph's text with its emphasis, code, links, pictures and notes */
static void md_runs(MW *w, const Para *p, int intable, int notes)
{
  Doc *d = w->d;
  Buf *b = w->b;
  const CharFmt *base = &d->styles[p->pf.style < d->nstyles ? p->pf.style : 0].cf;
  u32 k, start = 0;
  int code_para = fmt_is_code_style(d, p->pf.style);
  unsigned open = 0;            /* 1 bold, 2 italic, 4 strike, 8 code */
  u16 link = 0;
  int atstart = 1, pend_sp = 0;
  for (k = 0; k <= p->nr; k++) {
    const CharFmt *f = k < p->nr ? &p->r[k].f : NULL;
    u32 end = f ? p->r[k].end : start, i;
    unsigned want = 0;
    u16 wl = f ? f->link : 0;
    if (f && end == start) continue;
    if (f && f->rev == REV_DEL) { start = end; continue; }
    if (f) {
      if ((f->flags & CF_BOLD) && !(base->flags & CF_BOLD)) want |= 1;
      if ((f->flags & CF_ITALIC) && !(base->flags & CF_ITALIC)) want |= 2;
      if (f->flags & CF_STRIKE) want |= 4;
      if (is_mono(d, f) && !code_para) want |= 8;
      if ((f->flags & CF_UNDERLINE) && !f->link && !(base->flags & CF_UNDERLINE)) want |= 16;   /* (no Markdown for these: */
      if (f->flags & CF_SUPER) want |= 32;                                                      /*  HTML's tags, which */
      if (f->flags & CF_SUB) want |= 64;                                                        /*  Markdown allows) */
    }
    /* close what ends (inner first), then the link, then open */
    if ((open & 8) && !(want & 8 && wl == link)) { buf_addc(b, '`'); open &= ~8u; }
    if ((open & 64) && (!(want & 64) || wl != link)) { buf_adds(b, "</sub>"); open &= ~64u; }
    if ((open & 32) && (!(want & 32) || wl != link)) { buf_adds(b, "</sup>"); open &= ~32u; }
    if ((open & 16) && (!(want & 16) || wl != link)) { buf_adds(b, "</u>"); open &= ~16u; }
    if ((open & 4) && (!(want & 4) || wl != link)) { buf_adds(b, "~~"); open &= ~4u; }
    if ((open & 2) && (!(want & 2) || wl != link)) { buf_addc(b, '*'); open &= ~2u; }
    if ((open & 1) && (!(want & 1) || wl != link)) { buf_adds(b, "**"); open &= ~1u; }
    if (pend_sp && (open & 8)) { while (pend_sp) { buf_addc(b, ' '); pend_sp--; } }
    if (link && wl != link) {
      const char *u = doc_link_url(d, link);
      /* an address with spaces or brackets goes in <...> (CommonMark keeps it as it is) */
      int angle = strpbrk(u, " ()<>") != NULL;
      buf_adds(b, angle ? "](<" : "](");
      for (; *u; u++) { if (*u == '<' || *u == '>' || *u == '\\') buf_addc(b, '\\'); buf_addc(b, *u); }
      buf_adds(b, angle ? ">)" : ")");
      link = 0;
    }
    while (pend_sp) { buf_addc(b, ' '); pend_sp--; }
    if (!f) break;
    /* spaces at the run's start go before the marks open (CommonMark won't open emphasis before a space) */
    if ((want & ~open) || (wl && wl != link)) while (start < end && p->t[start] == ' ') { buf_addc(b, ' '); start++; atstart = 0; }
    if (wl && wl != link) { buf_addc(b, '['); link = wl; }
    if ((want & 1) && !(open & 1)) { buf_adds(b, "**"); open |= 1; }
    if ((want & 2) && !(open & 2)) { buf_addc(b, '*'); open |= 2; }
    if ((want & 4) && !(open & 4)) { buf_adds(b, "~~"); open |= 4; }
    if ((want & 16) && !(open & 16)) { buf_adds(b, "<u>"); open |= 16; }
    if ((want & 32) && !(open & 32)) { buf_adds(b, "<sup>"); open |= 32; }
    if ((want & 64) && !(open & 64)) { buf_adds(b, "<sub>"); open |= 64; }
    if ((want & 8) && !(open & 8)) { buf_addc(b, '`'); open |= 8; }
    i = start;
    if (want) {
      /* and spaces at its end after they close */
      u32 e2 = end;
      while (e2 > i && p->t[e2 - 1] == ' ') e2--;
      pend_sp = (int)(end - e2);
      end = e2;
    }
    while (i < end) {
      u32 j;
      for (j = i; j < end; j++) {
        u8 c = (u8)p->t[j];
        if ((c == 0xF3 || c == 0xF4) && j + 4 <= end && (IS_PIC(p->t + j) || IS_NOTE(p->t + j) || IS_ENOTE(p->t + j) || IS_MARK(p->t + j))) break;
        if (c == 0xEE && j + 3 <= end && IS_FIELD(p->t + j)) break;
      }
      if (j > i) {
        if (open & 8) buf_add(b, p->t + i, j - i);        /* (code spans take text as it is) */
        else md_esc(b, p->t + i, j - i, atstart, intable);
        atstart = 0;
      }
      i = j;
      if (i >= end) break;
      if (IS_FIELD(p->t + i)) { i += 3; continue; }
      if (IS_PIC(p->t + i)) {
        int n = pic_index(p->t + i);
        const Image *im = n >= 0 && n < d->nimages ? &d->images[n] : NULL;
        if (im && im->kind == OBJ_PICTURE && im->data && (im->type == IMG_PNG || im->type == IMG_JPEG)) {
          buf_adds(b, "![");
          md_esc(b, im->alt, (u32)strlen(im->alt), 0, 1);
          buf_printf(b, "](data:image/%s;base64,", im->type == IMG_PNG ? "png" : "jpeg");
          b64_encode(b, im->data, im->len);
          buf_addc(b, ')');
        }
        atstart = 0;
        i += 4;
        continue;
      }
      if (IS_NOTE(p->t + i) || IS_ENOTE(p->t + i)) {
        int en = IS_ENOTE(p->t + i), n = note_index(p->t + i);
        if (notes && n >= 0 && n < (en ? d->nenotes : d->nnotes)) {
          int num = en ? note_num(w->enoteorder, &w->nenote, n) : note_num(w->noteorder, &w->nnote, n);
          buf_printf(b, en ? "[^e%d]" : "[^%d]", num);
        }
        atstart = 0;
        i += 4;
        continue;
      }
      i += 4;
    }
    start = p->r[k].end;
  }
}
static void md_indent(Buf *b, int n) { while (n-- > 0) buf_addc(b, ' '); }
static int md_table(MW *w, Para **p, u32 i, u32 end);
/* the blocks of paragraphs a..b */
static void md_blocks(MW *w, Para **p, u32 a, u32 b)
{
  Doc *d = w->d;
  Buf *o = w->b;
  u32 i = a;
  int first = 1;
  while (i < b) {
    const Para *q = p[i];
    int k = q->pf.style < d->nstyles ? q->pf.style : 0;
    if (!first) buf_addc(o, '\n');
    first = 0;
    if (top_cell(&q->pf)->table) { i = (u32)md_table(w, p, i, b); continue; }
    if (fmt_is_code_style(d, k)) {
      /* a fenced block: as many backticks as the text's longest run, plus one */
      u32 e = i, j;
      int fence = 3;
      while (e < b && fmt_is_code_style(d, p[e]->pf.style) && !top_cell(&p[e]->pf)->table) {
        int run = 0;
        for (j = 0; j < p[e]->len; j++) { if (p[e]->t[j] == '`') { if (++run >= fence) fence = run + 1; } else run = 0; }
        e++;
      }
      md_indent(o, 0);
      { int f; for (f = 0; f < fence; f++) buf_addc(o, '`'); }
      buf_addc(o, '\n');
      for (j = i; j < e; j++) {
        u32 c;
        for (c = 0; c < p[j]->len; c++) {
          if (IS_PIC(p[j]->t + c) || IS_NOTE(p[j]->t + c) || IS_ENOTE(p[j]->t + c) || IS_MARK(p[j]->t + c)) { c += 3; continue; }
          if (IS_FIELD(p[j]->t + c)) { c += 2; continue; }
          buf_addc(o, p[j]->t[c] == '\v' ? '\n' : p[j]->t[c]);
        }
        buf_addc(o, '\n');
      }
      { int f; for (f = 0; f < fence; f++) buf_addc(o, '`'); }
      buf_addc(o, '\n');
      i = e;
      continue;
    }
    if (q->pf.list && q->pf.list <= d->nlists) {
      /* a list: items at their levels, each indented to its parent's text */
      int col[10], num[9], lvl, prevlvl = -1;
      u16 list = q->pf.list;
      col[0] = 0;
      memset(num, 0, sizeof num);
      while (i < b && p[i]->pf.list == list && !top_cell(&p[i]->pf)->table) {
        const Para *r = p[i];
        const ListLevel *lv;
        char mark[16];
        int z;
        lvl = r->pf.level > 8 ? 8 : r->pf.level;
        if (lvl > prevlvl + 1) lvl = prevlvl + 1;             /* (Markdown can't skip a level) */
        lv = &d->lists[list - 1].lvl[lvl];
        for (z = lvl + 1; z < 9; z++) num[z] = 0;
        if (lv->fmt == NF_BULLET || lv->fmt == NF_NONE) strcpy(mark, "-");
        else { if (!num[lvl]) num[lvl] = lv->start ? lv->start : 1; else num[lvl]++; sprintf(mark, "%d.", num[lvl]); }
        md_indent(o, col[lvl]);
        buf_adds(o, mark); buf_addc(o, ' ');
        col[lvl + 1] = col[lvl] + (int)strlen(mark) + 1;
        md_runs(w, r, 0, 1);
        buf_addc(o, '\n');
        prevlvl = lvl;
        i++;
      }
      continue;
    }
    {
      int h = fmt_heading_level(d, k);
      int quote = !strcmp(d->styles[k].id, "Quote") || !strcmp(d->styles[k].name, "Quote") || !strcmp(d->styles[k].name, "Intense Quote");
      if (!q->len && q->pf.bstyle[PE_BOTTOM] != PB_NONE && !h) { buf_adds(o, "***\n"); i++; continue; }
      if (!strcmp(d->styles[k].id, "Title")) h = 1;
      if (h > 6) h = 6;
      if (h) { int z; for (z = 0; z < h; z++) buf_addc(o, '#'); buf_addc(o, ' '); }
      if (quote) buf_adds(o, "> ");
      if (!q->len && !h) buf_adds(o, "&nbsp;");            /* (an empty paragraph keeps its place) */
      {
        /* a quote's line breaks continue the quote */
        size_t from = o->len;
        md_runs(w, q, 0, 1);
        if (quote) {
          size_t z;
          Buf t;
          buf_init(&t);
          for (z = from; z < o->len; z++) { buf_addc(&t, o->data[z]); if (o->data[z] == '\n') buf_adds(&t, "> "); }
          o->len = from; o->data[from] = 0;
          buf_add(o, t.data, t.len);
          buf_free(&t);
        }
      }
      buf_addc(o, '\n');
      i++;
    }
  }
}
/* a GitHub table: the first row is the heading row */
static int md_table(MW *w, Para **p, u32 i, u32 end)
{
  Doc *d = w->d;
  Buf *o = w->b;
  u16 tn = top_cell(&p[i]->pf)->table;
  const Table *t = tn <= d->ntables ? &d->tables[tn - 1] : NULL;
  int ncols = t ? t->ncols : 1, row = -1, c;
  u32 j = i;
  while (j < end && top_cell(&p[j]->pf)->table == tn) {
    const Cell *x = top_cell(&p[j]->pf);
    int col = 0;
    row = x->row;
    buf_addc(o, '|');
    while (j < end && top_cell(&p[j]->pf)->table == tn && top_cell(&p[j]->pf)->row == row) {
      const Cell *y = top_cell(&p[j]->pf);
      int sp = y->span ? y->span : 1, firstp = 1;
      while (col < y->col) { buf_adds(o, "  |"); col++; }
      buf_addc(o, ' ');
      while (j < end && top_cell(&p[j]->pf)->table == tn && top_cell(&p[j]->pf)->row == row && top_cell(&p[j]->pf)->col == y->col) {
        if (y->vmerge != VM_CONT) { if (!firstp) buf_adds(o, "<br>"); md_runs(w, p[j], 1, 1); }
        firstp = 0;
        j++;
      }
      buf_adds(o, " |");
      col++;
      while (--sp > 0 && col < ncols) { buf_adds(o, "  |"); col++; }
    }
    while (col < ncols) { buf_adds(o, "  |"); col++; }
    buf_addc(o, '\n');
    if (row == top_cell(&p[i]->pf)->row) {
      buf_addc(o, '|');
      for (c = 0; c < ncols; c++) {
        /* the heading row's alignment, column by column */
        u32 q;
        int al = AL_LEFT;
        for (q = i; q < j; q++) if (top_cell(&p[q]->pf)->col == c) { al = p[q]->pf.align; break; }
        buf_adds(o, al == AL_CENTER ? " :---: |" : al == AL_RIGHT ? " ---: |" : " --- |");
      }
      buf_addc(o, '\n');
    }
  }
  return (int)j;
}
static void md_notes(MW *w, int end)
{
  Doc *d = w->d;
  Buf *o = w->b;
  int i, n = end ? w->nenote : w->nnote;
  for (i = 0; i < n; i++) {
    int k = end ? w->enoteorder[i] : w->noteorder[i];
    const Story *s = end ? &d->enotes[k] : &d->notes[k];
    u32 j;
    buf_printf(o, end ? "\n[^e%d]: " : "\n[^%d]: ", i + 1);
    for (j = 0; j < s->np; j++) {
      if (j) buf_adds(o, "\n\n    ");
      md_runs(w, s->p[j], 0, 0);
    }
    buf_addc(o, '\n');
  }
}
static u8 *md_save(Doc *d, size_t *len)
{
  Buf b;
  MW w;
  memset(&w, 0, sizeof w);
  w.d = d;
  buf_init(&b);
  w.b = &b;
  w.noteorder = (int *)malloc((size_t)(d->nnotes + 1) * sizeof(int));
  w.enoteorder = (int *)malloc((size_t)(d->nenotes + 1) * sizeof(int));
  if (!w.noteorder || !w.enoteorder) { free(w.noteorder); free(w.enoteorder); return NULL; }
  md_blocks(&w, d->p, 0, d->np);
  md_notes(&w, 0);
  md_notes(&w, 1);
  free(w.noteorder); free(w.enoteorder);
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}

/* ================================================================== */
/* reading */

/* ---- inline */
enum { PK_TEXT, PK_DELIM, PK_OPEN, PK_IMGOPEN, PK_GONE };
enum { MF_BOLD = 1, MF_ITALIC = 2, MF_STRIKE = 4, MF_CODE = 8, MF_UL = 16, MF_SUP = 32, MF_SUB = 64 };
typedef struct {
  u8 kind;
  char ch;                  /* a delimiter's character (* _ ~), or a bracket's */
  int count;                /* a delimiter run's length left */
  u8 canopen, canclose;
  u32 off, len;             /* text in the inline buffer */
  unsigned fmt;
  u16 link;
} Piece;
typedef struct { char label[64]; char *url; } LinkDef;
typedef struct {
  Doc *d;
  Buf text;                 /* the pieces' text */
  Piece *pc; int npc, cap;
  LinkDef *defs; int ndefs;
  int notemap[2][512];      /* [^label] numbers -> note index + 1 */
  char (*notelabels)[32]; int nnotelabels;
  unsigned *lost;
  unsigned htmlfmt;         /* formats opened by inline HTML tags */
} MR;

static Piece *new_piece(MR *m, int kind)
{
  Piece *p;
  if (m->npc >= m->cap) {
    int nc = m->cap ? m->cap * 2 : 64;
    Piece *np = (Piece *)realloc(m->pc, (size_t)nc * sizeof(Piece));
    if (!np) return NULL;
    m->pc = np; m->cap = nc;
  }
  p = &m->pc[m->npc++];
  memset(p, 0, sizeof *p);
  p->kind = (u8)kind;
  p->off = (u32)m->text.len;
  p->fmt = m->htmlfmt;
  return p;
}
static void add_text(MR *m, const char *s, size_t n)
{
  Piece *p = m->npc ? &m->pc[m->npc - 1] : NULL;
  if (!p || p->kind != PK_TEXT || p->off + p->len != m->text.len || p->fmt != m->htmlfmt || p->link) p = new_piece(m, PK_TEXT);
  if (!p) return;
  buf_add(&m->text, s, n);
  p->len += (u32)n;
}
static void add_cp(MR *m, u32 cp) { char u[8]; add_text(m, u, (size_t)utf8_put(u, cp)); }
static int is_punct(u32 c) { return (c >= 33 && c <= 47) || (c >= 58 && c <= 64) || (c >= 91 && c <= 96) || (c >= 123 && c <= 126) || c == 0x2018 || c == 0x2019 || c == 0x201C || c == 0x201D; }
static int is_ws(u32 c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0xA0 || c == 0; }

/* a delimiter run that ended up literal: its characters */
static void delim_text(MR *m, Piece *p)
{
  int k;
  p->kind = PK_TEXT;
  p->off = (u32)m->text.len;
  for (k = 0; k < p->count; k++) buf_addc(&m->text, p->ch);
  p->len = (u32)p->count;
  p->count = 0;
}
/* emphasis between pieces lo..hi (exclusive): CommonMark's "process emphasis" */
static void emphasis(MR *m, int lo, int hi)
{
  int c;
  for (c = lo; c < hi; c++) {
    Piece *cl = &m->pc[c];
    int o;
    if (cl->kind != PK_DELIM || !cl->canclose || cl->count <= 0) continue;
    for (o = c - 1; o >= lo; o--) {
      Piece *op = &m->pc[o];
      if (op->kind != PK_DELIM || op->ch != cl->ch || !op->canopen || op->count <= 0) continue;
      if (cl->ch != '~' && (op->canclose || cl->canopen) && (op->count + cl->count) % 3 == 0 && (op->count % 3 || cl->count % 3)) continue;
      if (cl->ch == '~' && (op->count < 2 || cl->count < 2)) continue;
      {
        int use = cl->ch == '~' ? 2 : (op->count >= 2 && cl->count >= 2 ? 2 : 1), k;
        unsigned f = cl->ch == '~' ? MF_STRIKE : use == 2 ? MF_BOLD : MF_ITALIC;
        for (k = o + 1; k < c; k++) {
          if (m->pc[k].kind == PK_DELIM) delim_text(m, &m->pc[k]);    /* delimiters left in between are literal */
          if (m->pc[k].kind == PK_TEXT) m->pc[k].fmt |= f;
        }
        op->count -= use; cl->count -= use;
        if (cl->count > 0) c--;          /* the closer may close again */
        break;
      }
    }
  }
}
static int scan_dest(const char *s, size_t n, size_t *i, char *url, size_t cap)
{
  size_t k = *i, o = 0;
  int depth = 0;
  while (k < n && (s[k] == ' ' || s[k] == '\n')) k++;
  if (k < n && s[k] == '<') {
    k++;
    while (k < n && s[k] != '>' && s[k] != '\n') { if (s[k] == '\\' && k + 1 < n) k++; if (o + 1 < cap) url[o++] = s[k]; k++; }
    if (k >= n || s[k] != '>') return 0;
    k++;
  } else {
    while (k < n && s[k] != ' ' && s[k] != '\n') {
      if (s[k] == '(') depth++;
      else if (s[k] == ')') { if (!depth) break; depth--; }
      if (s[k] == '\\' && k + 1 < n && is_punct((u8)s[k + 1])) k++;
      if (o + 1 < cap) url[o++] = s[k];
      k++;
    }
  }
  url[o] = 0;
  *i = k;
  return 1;
}
/* "(dest "title")" after a ']' at s[*i]; on success *i is past it */
static int scan_inline_link(const char *s, size_t n, size_t *i, char *url, size_t cap)
{
  size_t k = *i;
  if (k >= n || s[k] != '(') return 0;
  k++;
  if (!scan_dest(s, n, &k, url, cap)) return 0;
  while (k < n && (s[k] == ' ' || s[k] == '\n')) k++;
  if (k < n && (s[k] == '"' || s[k] == '\'' || s[k] == '(')) {
    char q = s[k] == '(' ? ')' : s[k];
    k++;
    while (k < n && s[k] != q) { if (s[k] == '\\') k++; k++; }
    if (k >= n) return 0;
    k++;
    while (k < n && (s[k] == ' ' || s[k] == '\n')) k++;
  }
  if (k >= n || s[k] != ')') return 0;
  *i = k + 1;
  return 1;
}
static void norm_label(const char *s, size_t n, char *out, size_t cap)
{
  size_t i, o = 0;
  int sp = 0;
  for (i = 0; i < n && o + 1 < cap; i++) {
    char c = s[i];
    if (c == ' ' || c == '\n' || c == '\t') { sp = 1; continue; }
    if (sp && o) out[o++] = ' ';
    sp = 0;
    out[o++] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
  }
  out[o] = 0;
}
static const char *find_def(MR *m, const char *s, size_t n)
{
  char lab[64];
  int i;
  norm_label(s, n, lab, sizeof lab);
  for (i = 0; i < m->ndefs; i++) if (!strcmp(m->defs[i].label, lab)) return m->defs[i].url;
  return NULL;
}
static int note_for(MR *m, const char *label, size_t n)
{
  char lab[32];
  int i, en;
  size_t k = n < sizeof lab - 1 ? n : sizeof lab - 1;
  memcpy(lab, label, k); lab[k] = 0;
  en = lab[0] == 'e' && lab[1] >= '0' && lab[1] <= '9';
  for (i = 0; i < m->nnotelabels; i++) if (!strcmp(m->notelabels[i], lab)) break;
  if (i == m->nnotelabels) {
    char (*nl)[32];
    if (i >= 1024 || !(nl = (char (*)[32])realloc(m->notelabels, (size_t)(i + 1) * 32))) return -1;
    m->notelabels = nl;
    strcpy(m->notelabels[i], lab);
    m->nnotelabels++;
  }
  if (i >= 512) return -1;
  if (!m->notemap[en][i]) {
    int k2 = en ? doc_add_endnote(m->d) : doc_add_note(m->d);
    if (k2 < 0) return -1;
    m->notemap[en][i] = k2 + 1;
  }
  return (m->notemap[en][i] - 1) | (en ? 0x10000 : 0);
}
static void finish_link(MR *m, int open, const char *url, int image)
{
  int k;
  emphasis(m, open + 1, m->npc);
  if (image) {
    /* the picture replaces its alt text */
    u32 n = 0;
    u8 *data = fmt_fetch(url, &n);
    char alt[160];
    size_t o = 0;
    int idx = -1;
    for (k = open + 1; k < m->npc; k++) {
      Piece *p = &m->pc[k];
      if (p->kind == PK_DELIM) delim_text(m, p);
      if (p->kind == PK_TEXT && o + p->len < sizeof alt) { memcpy(alt + o, m->text.data + p->off, p->len); o += p->len; }
    }
    alt[o] = 0;
    if (data) idx = doc_add_image(m->d, data, n, 0, 0);
    if (idx < 0 && m->lost) *m->lost |= LOST_PICTURES;
    m->npc = open;
    if (idx >= 0) {
      char ref[8];
      Image *im = &m->d->images[idx];
      i32 maxw = fmt_text_width(m->d);
      strncpy(im->alt, alt, sizeof im->alt - 1);
      if (im->cx > maxw) { im->cy = (i32)((long)im->cy * maxw / im->cx); im->cx = maxw; }
      pic_utf8(ref, idx);
      add_text(m, ref, 4);
      m->pc[m->npc - 1].link = 0;
      new_piece(m, PK_GONE);          /* (so following text isn't joined to the picture's piece) */
    }
    return;
  }
  {
    u16 ln = (u16)doc_link(m->d, url);
    m->pc[open].kind = PK_GONE;
    for (k = open + 1; k < m->npc; k++) {
      if (m->pc[k].kind == PK_DELIM) delim_text(m, &m->pc[k]);
      if (m->pc[k].kind == PK_TEXT && !m->pc[k].link) m->pc[k].link = ln;
    }
    /* no links inside links: earlier openers become text */
    for (k = open - 1; k >= 0; k--) if (m->pc[k].kind == PK_OPEN) { m->pc[k].kind = PK_TEXT; m->pc[k].off = (u32)m->text.len; buf_addc(&m->text, '['); m->pc[k].len = 1; }
  }
}
static int html_inline(MR *m, const char *s, size_t n, size_t *i)
{
  static const struct { const char *t; unsigned f; } tags[] = { { "b", MF_BOLD }, { "strong", MF_BOLD }, { "i", MF_ITALIC }, { "em", MF_ITALIC },
    { "u", MF_UL }, { "ins", MF_UL }, { "s", MF_STRIKE }, { "del", MF_STRIKE }, { "strike", MF_STRIKE }, { "sup", MF_SUP }, { "sub", MF_SUB }, { "code", MF_CODE } };
  size_t k = *i + 1, t0;
  int end = 0, j;
  char tag[12];
  size_t tl = 0;
  if (k < n && s[k] == '/') { end = 1; k++; }
  t0 = k;
  while (k < n && ((s[k] >= 'a' && s[k] <= 'z') || (s[k] >= 'A' && s[k] <= 'Z')) && tl < sizeof tag - 1) tag[tl++] = (char)(s[k++] | 0x20);
  tag[tl] = 0;
  if (k == t0) return 0;
  while (k < n && s[k] != '>' && s[k] != '<' && s[k] != '\n') k++;
  if (k >= n || s[k] != '>') return 0;
  if (!strcmp(tag, "br")) { add_text(m, "\v", 1); *i = k + 1; return 1; }
  for (j = 0; j < (int)(sizeof tags / sizeof tags[0]); j++)
    if (!strcmp(tag, tags[j].t)) {
      if (end) m->htmlfmt &= ~tags[j].f; else m->htmlfmt |= tags[j].f;
      *i = k + 1;
      return 1;
    }
  if (!strcmp(tag, "span") || !strcmp(tag, "a") || !strcmp(tag, "font") || !strcmp(tag, "img") || !strcmp(tag, "kbd")) { *i = k + 1; return 1; }   /* (dropped) */
  return 0;
}
/* the inline content s[0..n) into pieces */
static void inlines(MR *m, const char *s, size_t n)
{
  size_t i = 0;
  int *opens = NULL, nopen = 0;
  while (i < n) {
    char c = s[i];
    if (c == '\\' && i + 1 < n) {
      if (s[i + 1] == '\n') { add_text(m, "\v", 1); i += 2; continue; }
      if (is_punct((u8)s[i + 1])) { add_text(m, s + i + 1, 1); i += 2; continue; }
      add_text(m, "\\", 1); i++; continue;
    }
    if (c == '`') {
      size_t run = 0, k, e = 0;
      while (i + run < n && s[i + run] == '`') run++;
      for (k = i + run; k < n; k++) {
        if (s[k] == '`') {
          size_t r2 = 0;
          while (k + r2 < n && s[k + r2] == '`') r2++;
          if (r2 == run) { e = k; break; }
          k += r2 - 1;
        }
      }
      if (e) {
        size_t a = i + run, b2 = e, q;
        Piece *p;
        if (b2 - a >= 2 && s[a] == ' ' && s[b2 - 1] == ' ') { int allsp = 1; for (q = a; q < b2; q++) if (s[q] != ' ') allsp = 0; if (!allsp) { a++; b2--; } }
        p = new_piece(m, PK_TEXT);
        if (p) {
          p->fmt |= MF_CODE;
          for (q = a; q < b2; q++) buf_addc(&m->text, s[q] == '\n' ? ' ' : s[q]);
          p->len = (u32)(b2 - a);
        }
        new_piece(m, PK_GONE);
        i = e + run;
        continue;
      }
      add_text(m, s + i, run); i += run; continue;
    }
    if (c == '*' || c == '_' || c == '~') {
      size_t run = 0;
      int used;
      u32 before = 0, after = 0;
      Piece *p;
      while (i + run < n && s[i + run] == c) run++;
      if (c == '~' && run != 2) { add_text(m, s + i, run); i += run; continue; }
      if (i > 0) { size_t b2 = utf8_prev(s, i); before = utf8_get(s + b2, i - b2, &used); }
      if (i + run < n) after = utf8_get(s + i + run, n - i - run, &used);
      p = new_piece(m, PK_DELIM);
      if (!p) return;
      p->ch = c; p->count = (int)run;
      {
        int lf = !is_ws(after) && (!is_punct(after) || is_ws(before) || is_punct(before));
        int rf = !is_ws(before) && (!is_punct(before) || is_ws(after) || is_punct(after));
        if (c == '_') { p->canopen = (u8)(lf && (!rf || is_punct(before))); p->canclose = (u8)(rf && (!lf || is_punct(after))); }
        else { p->canopen = (u8)lf; p->canclose = (u8)rf; }
        if (i == 0) p->canclose = 0;
        if (i + run >= n) p->canopen = 0;
      }
      i += run;
      continue;
    }
    if (c == '!' && i + 1 < n && s[i + 1] == '[') {
      Piece *p = new_piece(m, PK_IMGOPEN);
      int *no = (int *)realloc(opens, (size_t)(nopen + 1) * sizeof(int));
      if (!p || !no) { free(no ? no : opens); return; }
      opens = no; opens[nopen++] = m->npc - 1;
      i += 2;
      continue;
    }
    if (c == '[') {
      /* a footnote reference? */
      if (i + 2 < n && s[i + 1] == '^') {
        size_t k = i + 2;
        while (k < n && s[k] != ']' && s[k] != ' ' && s[k] != '[') k++;
        if (k < n && s[k] == ']' && k > i + 2) {
          int note = note_for(m, s + i + 2, k - i - 2);
          if (note >= 0) {
            char ref[8];
            if (note & 0x10000) enote_utf8(ref, note & 0xFFFF); else note_utf8(ref, note);
            add_text(m, ref, 4);
            new_piece(m, PK_GONE);
          }
          i = k + 1;
          continue;
        }
      }
      {
        Piece *p = new_piece(m, PK_OPEN);
        int *no = (int *)realloc(opens, (size_t)(nopen + 1) * sizeof(int));
        if (!p || !no) { free(no ? no : opens); return; }
        p->off = (u32)i;                    /* (where the label starts, for reference links) */
        opens = no; opens[nopen++] = m->npc - 1;
        i++;
        continue;
      }
    }
    if (c == ']' && nopen) {
      int op = opens[nopen - 1];
      int image = m->pc[op].kind == PK_IMGOPEN;
      char *url;
      size_t k = i + 1;
      const char *def = NULL;
      int ok = 0;
      if (m->pc[op].kind != PK_OPEN && m->pc[op].kind != PK_IMGOPEN) { nopen--; add_text(m, "]", 1); i++; continue; }
      if (!(url = (char *)malloc(n - i + 1))) { free(opens); return; }      /* (a picture's data: URI is long) */
      url[0] = 0;
      if (scan_inline_link(s, n, &k, url, n - i + 1)) ok = 1;
      else if (k < n && s[k] == '[') {
        size_t e = k + 1;
        while (e < n && s[e] != ']') e++;
        if (e < n) {
          def = e > k + 1 ? find_def(m, s + k + 1, e - k - 1) : NULL;
          if (def) { k = e + 1; ok = 2; }
        }
      }
      if (!ok && !image) {
        /* a shortcut reference: [label] */
        size_t ls = m->pc[op].off + 1;
        if (ls <= i) def = find_def(m, s + ls, i - ls);
        if (def) ok = 2;
      }
      if (ok) {
        finish_link(m, op, ok == 1 ? url : def, image);
        free(url);
        nopen--;
        i = ok == 1 ? k : (k > i + 1 && s[i + 1] == '[' ? k : i + 1);
        continue;
      }
      free(url);
      /* not a link: the bracket is text */
      m->pc[op].kind = PK_TEXT; m->pc[op].off = (u32)m->text.len; buf_adds(&m->text, image ? "![" : "["); m->pc[op].len = image ? 2 : 1;
      nopen--;
      add_text(m, "]", 1); i++;
      continue;
    }
    if (c == '<') {
      /* an autolink, or inline HTML */
      size_t k = i + 1;
      while (k < n && s[k] != '>' && s[k] != ' ' && s[k] != '<' && s[k] != '\n') k++;
      if (k < n && s[k] == '>' && k > i + 1) {
        char url[512];
        size_t l = k - i - 1;
        const char *colon = memchr(s + i + 1, ':', l), *at = memchr(s + i + 1, '@', l);
        if ((colon && colon > s + i + 2) || at) {
          Piece *p;
          if (l >= sizeof url - 8) l = sizeof url - 8;
          if (at && !colon) { strcpy(url, "mailto:"); memcpy(url + 7, s + i + 1, l); url[7 + l] = 0; }
          else { memcpy(url, s + i + 1, l); url[l] = 0; }
          p = new_piece(m, PK_TEXT);
          if (p) { buf_add(&m->text, s + i + 1, k - i - 1); p->len = (u32)(k - i - 1); p->link = (u16)doc_link(m->d, url); }
          new_piece(m, PK_GONE);
          i = k + 1;
          continue;
        }
      }
      if (html_inline(m, s, n, &i)) continue;
      add_text(m, "<", 1); i++;
      continue;
    }
    if (c == '&') {
      int used;
      u32 cp = html_entity(s + i, n - i, &used);
      if (cp) { add_cp(m, cp); i += (size_t)used; continue; }
    }
    if (c == '\n') {
      /* a hard break: two spaces before the line's end */
      size_t t = m->text.len;
      Piece *p = m->npc ? &m->pc[m->npc - 1] : NULL;
      if (p && p->kind == PK_TEXT && p->len >= 2 && t >= 2 && m->text.data[t - 1] == ' ' && m->text.data[t - 2] == ' ' && p->off + p->len == t) {
        while (p->len && m->text.data[p->off + p->len - 1] == ' ') { p->len--; m->text.len--; }
        m->text.data[m->text.len] = 0;
        add_text(m, "\v", 1);
      } else {
        if (p && p->kind == PK_TEXT && p->off + p->len == t) while (p->len && m->text.data[p->off + p->len - 1] == ' ') { p->len--; m->text.len--; }
        add_text(m, " ", 1);
      }
      i++;
      while (i < n && s[i] == ' ') i++;
      continue;
    }
    {
      int used;
      utf8_get(s + i, n - i, &used);
      add_text(m, s + i, (size_t)used);
      i += (size_t)used;
    }
  }
  /* brackets left open are text */
  while (nopen > 0) {
    Piece *p = &m->pc[opens[--nopen]];
    int image = p->kind == PK_IMGOPEN;
    p->kind = PK_TEXT; p->off = (u32)m->text.len; buf_adds(&m->text, image ? "![" : "["); p->len = image ? 2 : 1;
  }
  free(opens);
  emphasis(m, 0, m->npc);
}
/* the pieces into paragraph p, on top of the style's character format cf */
static void emit_pieces(MR *m, Para *p, const CharFmt *cf)
{
  int k;
  for (k = 0; k < m->npc; k++) {
    Piece *q = &m->pc[k];
    CharFmt f = *cf;
    if (q->kind == PK_DELIM) delim_text(m, q);
    if (q->kind != PK_TEXT || !q->len) continue;
    if (q->fmt & MF_BOLD) f.flags |= CF_BOLD;
    if (q->fmt & MF_ITALIC) f.flags |= CF_ITALIC;
    if (q->fmt & MF_STRIKE) f.flags |= CF_STRIKE;
    if (q->fmt & MF_UL) { f.flags |= CF_UNDERLINE; f.ul = UL_SINGLE; }
    if (q->fmt & MF_SUP) f.flags |= CF_SUPER;
    if (q->fmt & MF_SUB) f.flags |= CF_SUB;
    if (q->fmt & MF_CODE) f.font = (u8)doc_font(m->d, "Courier New");
    if (q->link) fmt_link_cf(m->d, &f, doc_link_url(m->d, q->link));
    if (q->len == 4 && (IS_NOTE(m->text.data + q->off) || IS_ENOTE(m->text.data + q->off))) f = *cf;
    para_append(p, m->text.data + q->off, q->len, &f);
  }
  m->npc = 0;
  m->text.len = 0;
  if (m->text.data) m->text.data[0] = 0;
  m->htmlfmt = 0;
}

/* ---- blocks */
typedef struct { int indent, content, ordered; } Item;
typedef struct {
  MR m;
  Doc *d;
  ParaList l;
  Buf para;                 /* the paragraph being gathered (its lines) */
  ParaFmt ppf; CharFmt pcf; /* its formatting */
  int pnote;                /* it belongs to note (index + 1; | 0x10000 endnote) */
  Item items[9]; int nitems;
  int list;                 /* the Quill list the items are in */
  int blank;                /* the line before was blank */
  int code;                 /* the Code style */
  int quote;                /* inside a block quote */
  int noteblock;            /* lines indented under a footnote's definition belong to it */
  char delim;               /* the outermost list's marker (- * + . or ): another starts another list) */
  int listnew;
} MB;

static void add_para(MB *b, Para *p, int note)
{
  if (!p) return;
  if (note) {
    Story *st = (note & 0x10000) ? &b->d->enotes[(note & 0xFFFF) - 1] : &b->d->notes[(note & 0xFFFF) - 1];
    Para **np = (Para **)realloc(st->p, (st->np + 1) * sizeof(Para *));
    if (!np) { para_free(p); return; }
    p->pf.list = 0;
    para_normalise(p);
    st->p = np; st->p[st->np++] = p;
    return;
  }
  pl_add(&b->l, p);
}
/* the gathered paragraph made (its inline content parsed) */
static void flush_para(MB *b)
{
  Para *p;
  size_t n = b->para.len;
  if (!n) return;
  while (n && (b->para.data[n - 1] == '\n' || b->para.data[n - 1] == ' ')) n--;
  p = para_new(&b->ppf, &b->pcf);
  if (p) {
    inlines(&b->m, b->para.data, n);
    emit_pieces(&b->m, p, &b->pcf);
    /* "&nbsp;" alone is how Quill writes an empty paragraph */
    if (p->len == 2 && (u8)p->t[0] == 0xC2 && (u8)p->t[1] == 0xA0) { Para *q = para_new(&p->pf, &p->r[0].f); para_free(p); p = q; }
  }
  add_para(b, p, b->pnote);
  b->para.len = 0; b->para.data[0] = 0;
  b->pnote = 0;
}
static int count_indent(const char *s, size_t n, size_t *pos)
{
  int col = 0;
  size_t i = 0;
  while (i < n && (s[i] == ' ' || s[i] == '\t')) { col = s[i] == '\t' ? (col + 4) & ~3 : col + 1; i++; }
  *pos = i;
  return col;
}
/* skip `cols` columns of indentation */
static size_t skip_cols(const char *s, size_t n, int cols)
{
  int col = 0;
  size_t i = 0;
  while (i < n && col < cols && (s[i] == ' ' || s[i] == '\t')) { col = s[i] == '\t' ? (col + 4) & ~3 : col + 1; i++; }
  return i;
}
static int thematic(const char *s, size_t n)
{
  size_t i;
  char c = 0;
  int cnt = 0;
  for (i = 0; i < n; i++) {
    if (s[i] == ' ' || s[i] == '\t') continue;
    if (!c && (s[i] == '*' || s[i] == '-' || s[i] == '_')) c = s[i];
    if (s[i] != c) return 0;
    cnt++;
  }
  return cnt >= 3;
}
/* a list marker at s: its width (marker and the space after), ordered or not, its number */
static int list_marker(const char *s, size_t n, int *ordered, int *num)
{
  size_t i = 0;
  if (n >= 1 && (s[0] == '-' || s[0] == '*' || s[0] == '+')) {
    if (n == 1) { *ordered = 0; return 1; }
    if (s[1] == ' ' || s[1] == '\t') { *ordered = 0; return 2; }
    return 0;
  }
  while (i < n && i < 9 && s[i] >= '0' && s[i] <= '9') i++;
  if (i == 0 || i >= n || (s[i] != '.' && s[i] != ')')) return 0;
  if (i + 1 < n && s[i + 1] != ' ' && s[i + 1] != '\t') return 0;
  *ordered = 1; *num = atoi(s);
  return (int)i + (i + 1 < n ? 2 : 1);
}
static void para_style(MB *b, int style)
{
  b->ppf = b->d->styles[style].pf; b->pcf = b->d->styles[style].cf;
}
/* a table's cells from a line "| a | b |" */
static int split_row(const char *s, size_t n, size_t *cs, size_t *ce, int max)
{
  size_t i = 0;
  int k = 0;
  while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
  if (i < n && s[i] == '|') i++;
  while (i < n && k < max) {
    size_t a = i;
    while (i < n && s[i] != '|') { if (s[i] == '\\' && i + 1 < n) i++; else if (s[i] == '`') { size_t e = i + 1; while (e < n && s[e] != '`') e++; if (e < n) i = e; } i++; }
    {
      size_t e = i;
      while (a < e && (s[a] == ' ' || s[a] == '\t')) a++;
      while (e > a && (s[e - 1] == ' ' || s[e - 1] == '\t')) e--;
      if (i >= n && a == e && k > 0) break;      /* (the line's closing pipe) */
      cs[k] = a; ce[k] = e; k++;
    }
    if (i < n) i++;
  }
  return k;
}
static int delim_row(const char *s, size_t n, u8 *align, int max)
{
  size_t cs[MAX_COLS], ce[MAX_COLS];
  int k = split_row(s, n, cs, ce, max), j;
  int pipe = memchr(s, '|', n) != NULL;
  if (!k || (!pipe && k < 2)) return 0;
  for (j = 0; j < k; j++) {
    size_t a = cs[j], e = ce[j], q;
    int dash = 0;
    if (a >= e) return 0;
    align[j] = AL_LEFT;
    if (s[a] == ':') { a++; }
    if (e > a && s[e - 1] == ':') { e--; align[j] = s[cs[j]] == ':' ? AL_CENTER : AL_RIGHT; }
    for (q = a; q < e; q++) { if (s[q] != '-') return 0; dash++; }
    if (!dash) return 0;
  }
  return pipe ? k : 0;
}

static Doc *md_load(const u8 *data, size_t len, unsigned *lost, char *err, size_t errcap)
{
  MB *b = (MB *)calloc(1, sizeof(MB));
  Doc *d;
  const char *s = (const char *)data;
  size_t pos = 0;
  Buf src;
  unsigned dummy = 0;
  int fence = 0, fencelen = 0, fenceindent = 0;
  char fencech = 0;
  int tablecols = 0;
  TableBld tb;
  u8 talign[MAX_COLS];
  if (!lost) lost = &dummy;
  if (!b || !(d = doc_new())) { free(b); snprintf(err, errcap, "Not enough memory to open the file."); return NULL; }
  b->d = d; b->m.d = d; b->m.lost = lost;
  buf_init(&b->para); buf_init(&b->m.text); buf_init(&src);
  buf_adds(&b->para, ""); buf_adds(&b->m.text, "");
  b->code = -1;
  para_style(b, 0);
  /* the text as UTF-8 with \n line ends (a BOM dropped; Latin-1 when it isn't UTF-8) */
  if (len >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) { s += 3; len -= 3; }
  {
    size_t i;
    int valid = 1;
    for (i = 0; i < len && valid; ) {
      u8 c = (u8)s[i];
      int k = c < 0x80 ? 0 : c >= 0xC2 && c <= 0xDF ? 1 : c >= 0xE0 && c <= 0xEF ? 2 : c >= 0xF0 && c <= 0xF4 ? 3 : -1;
      if (k < 0) { valid = 0; break; }
      i++;
      while (k-- > 0) { if (i >= len || ((u8)s[i] & 0xC0) != 0x80) { valid = 0; break; } i++; }
    }
    for (i = 0; i < len; i++) {
      if (s[i] == '\r') { if (i + 1 < len && s[i + 1] == '\n') continue; buf_addc(&src, '\n'); continue; }
      if (valid || (u8)s[i] < 0x80) buf_addc(&src, s[i]);
      else { char u[4]; buf_add(&src, u, (size_t)utf8_put(u, (u8)s[i])); }
    }
    buf_addc(&src, '\n');
  }
  s = src.data; len = src.len;
  /* link reference definitions first (they may come after their use) */
  {
    size_t i = 0;
    while (i < len) {
      size_t e = i, k;
      while (e < len && s[e] != '\n') e++;
      k = i;
      while (k < e && k < i + 3 && s[k] == ' ') k++;
      if (k < e && s[k] == '[' && k + 1 < e && s[k + 1] != '^') {
        size_t l = k + 1;
        while (l < e && s[l] != ']') l++;
        if (l + 1 < e && s[l + 1] == ':') {
          char *url = (char *)malloc(e - l + 1);
          size_t q = l + 2;
          if (url && scan_dest(s, e, &q, url, e - l + 1) && url[0]) {
            LinkDef *nd = (LinkDef *)realloc(b->m.defs, (size_t)(b->m.ndefs + 1) * sizeof(LinkDef));
            if (nd) {
              b->m.defs = nd;
              norm_label(s + k + 1, l - k - 1, nd[b->m.ndefs].label, sizeof nd[0].label);
              nd[b->m.ndefs].url = str_dup(url);
              if (nd[b->m.ndefs].url) b->m.ndefs++;
            }
          }
          free(url);
        }
      }
      i = e + 1;
    }
  }
  while (pos < len) {
    size_t e = pos, ip;
    const char *ln;
    size_t n;
    int ind, ordered = 0, num = 1, mw;
    while (e < len && s[e] != '\n') e++;
    ln = s + pos; n = e - pos;
    pos = e + 1;
    /* fenced code */
    if (fence) {
      size_t k = skip_cols(ln, n, 3), q = k;
      int run = 0;
      while (q < n && ln[q] == fencech) { q++; run++; }
      if (run >= fencelen) { size_t z = q; while (z < n && ln[z] == ' ') z++; if (z == n) { fence = 0; continue; } }
      {
        Para *p;
        size_t off = skip_cols(ln, n, fenceindent);
        para_style(b, b->code);
        if (b->nitems) { b->ppf.list = 0; b->ppf.left += b->items[b->nitems - 1].content * 120; }
        p = para_new(&b->ppf, &b->pcf);
        if (p) para_append(p, ln + off, (u32)(n - off), &b->pcf);
        add_para(b, p, 0);
      }
      continue;
    }
    ind = count_indent(ln, n, &ip);
    /* a blank line */
    if (ip == n) {
      flush_para(b);
      if (tablecols) { tb_end(&tb, d, &b->l, fmt_text_width(d)); tablecols = 0; }
      b->blank = 1;
      continue;
    }
    /* a table row goes on */
    if (tablecols) {
      if (memchr(ln, '|', n)) {
        size_t cs[MAX_COLS], ce[MAX_COLS];
        int k = split_row(ln, n, cs, ce, MAX_COLS), j;
        tb_row(&tb);
        for (j = 0; j < tablecols; j++) {
          Para *p;
          ParaFmt pf = d->styles[0].pf;
          CharFmt cf = d->styles[0].cf;
          tb_cell(&tb, 1, 1, 0);
          pf.align = talign[j];
          pf.left = pf.right = pf.first = 0;
          tb_mark(&tb, &pf);
          p = para_new(&pf, &cf);
          if (p && j < k) {
            /* "\|" in a cell is a pipe; <br> starts another paragraph of the cell */
            Buf cell;
            size_t q;
            buf_init(&cell);
            for (q = cs[j]; q < ce[j]; q++) { if (ln[q] == '\\' && q + 1 < ce[j] && ln[q + 1] == '|') q++; buf_addc(&cell, ln[q]); }
            inlines(&b->m, cell.data ? cell.data : "", cell.len);
            emit_pieces(&b->m, p, &cf);
            buf_free(&cell);
            {
              /* split at line breaks */
              char *v;
              while (p && (v = memchr(p->t, '\v', p->len))) {
                u32 at = (u32)(v - p->t), r, st = 0;
                Para *a = para_new(&pf, &cf), *c2 = para_new(&pf, &cf);
                if (!a || !c2) { para_free(a); para_free(c2); break; }
                for (r = 0; r < p->nr; r++) {
                  u32 e2 = p->r[r].end;
                  if (st < at) para_append(a, p->t + st, (e2 < at ? e2 : at) - st, &p->r[r].f);
                  if (e2 > at + 1) { u32 s2 = st > at + 1 ? st : at + 1; para_append(c2, p->t + s2, e2 - s2, &p->r[r].f); }
                  st = e2;
                }
                add_para(b, a, 0);
                para_free(p); p = c2;
              }
            }
          }
          add_para(b, p, 0);
        }
        continue;
      }
      tb_end(&tb, d, &b->l, fmt_text_width(d));
      tablecols = 0;
    }
    /* list items: which open items does this line belong to? */
    {
      size_t mp = ip;
      int mark = ind < (b->nitems ? b->items[b->nitems - 1].content + 4 : 4) && !thematic(ln, n) ? list_marker(ln + ip, n - ip, &ordered, &num) : 0;
      if (mark && b->para.len && !b->nitems && ordered && num != 1) mark = 0;      /* (a paragraph can't be broken by "2.") */
      if (mark) {
        int lvl, was = b->nitems > 0;
        char delim = ln[ip];
        flush_para(b);
        while (b->nitems && ind < b->items[b->nitems - 1].content) {
          if (ind >= b->items[b->nitems - 1].indent && b->nitems) break;
          b->nitems--;
        }
        if (b->nitems && ind >= b->items[b->nitems - 1].indent && ind < b->items[b->nitems - 1].content) b->nitems--;   /* a sibling */
        if (b->nitems >= 9) b->nitems = 8;
        lvl = b->nitems;
        if (!lvl && ordered) { size_t q = ip; while (q < n && ln[q] >= '0' && ln[q] <= '9') q++; delim = q < n ? ln[q] : '.'; }
        if (!lvl && (!b->list || !was || delim != b->delim)) { b->list = doc_list_preset(d, ordered ? LP_NUMBERS : LP_BULLETS); b->listnew = 1; }
        if (!lvl) b->delim = delim;
        if (b->list) {
          ListLevel *lv = &d->lists[b->list - 1].lvl[lvl];
          int isnum = lv->fmt != NF_BULLET;
          if (isnum != ordered) {
            /* the level takes the kind its first item has */
            int cnt = 0;
            u32 q;
            for (q = 0; q < b->l.np; q++) if (b->l.p[q]->pf.list == b->list && b->l.p[q]->pf.level == lvl) cnt++;
            if (!cnt) {
              if (ordered) { lv->fmt = (u8)(lvl % 3 == 0 ? NF_DECIMAL : lvl % 3 == 1 ? NF_LOWER_LETTER : NF_LOWER_ROMAN); sprintf(lv->text, "%%%d.", lvl + 1); }
              else { lv->fmt = NF_BULLET; strcpy(lv->text, lvl % 3 == 0 ? "\xE2\x80\xA2" : lvl % 3 == 1 ? "o" : "\xE2\x96\xAA"); }
            } else if (!lvl) {
              b->list = doc_list_preset(d, ordered ? LP_NUMBERS : LP_BULLETS);
              lv = &d->lists[b->list - 1].lvl[0];
            }
          }
          if (ordered) {
            int cnt = 0;
            u32 q;
            for (q = 0; q < b->l.np; q++) if (b->l.p[q]->pf.list == b->list && b->l.p[q]->pf.level == lvl) cnt++;
            if (!cnt) lv->start = (u16)(num >= 0 && num < 65536 ? num : 1);
          }
        }
        b->items[b->nitems].indent = ind;
        mw = mark;
        {
          /* the item's text starts after the marker and up to 4 spaces */
          size_t q = ip + (size_t)mw;
          int extra = 0;
          while (q < n && ln[q] == ' ' && extra < 4) { q++; extra++; }
          if (q >= n || extra >= 4) { q = ip + (size_t)mw; extra = 0; }
          b->items[b->nitems].content = ind + mw + extra;
          b->items[b->nitems].ordered = ordered;
          b->nitems++;
          mp = q;
        }
        {
          int st = doc_style(d, "ListParagraph");
          para_style(b, st >= 0 ? st : 0);
          if (b->list) {
            const ListLevel *lv = &d->lists[b->list - 1].lvl[lvl];
            b->ppf.list = (u16)b->list; b->ppf.level = (u8)lvl;
            b->ppf.left = lv->left; b->ppf.first = -lv->hanging;
          }
        }
        b->blank = 0;
        /* what follows the marker is the item's first line (it may be a heading or code fence: kept simple) */
        buf_add(&b->para, ln + mp, n - mp);
        buf_addc(&b->para, '\n');
        continue;
      }
      /* a line inside an item (indented to its text), or lazily continuing its paragraph */
      if (b->nitems) {
        if (ind >= b->items[b->nitems - 1].content) {
          size_t off = skip_cols(ln, n, b->items[b->nitems - 1].content);
          if (b->blank) {
            /* a new paragraph in the item: no label of its own */
            flush_para(b);
            b->ppf.first = 0;
            {
              size_t q = off;
              while (q < n && ln[q] == ' ') q++;
              if (q + 3 <= n && (!strncmp(ln + q, "```", 3) || !strncmp(ln + q, "~~~", 3))) {
                fence = 1; fencech = ln[q]; fencelen = 0;
                while (q < n && ln[q] == fencech) { q++; fencelen++; }
                fenceindent = b->items[b->nitems - 1].content;
                if (b->code < 0) b->code = fmt_code_style(d);
                b->blank = 0;
                continue;
              }
            }
            {
              const ListLevel *lv = b->list ? &d->lists[b->list - 1].lvl[b->nitems - 1] : NULL;
              int st = doc_style(d, "ListParagraph");
              para_style(b, st >= 0 ? st : 0);
              b->ppf.left = lv ? lv->left : 720;
              b->ppf.first = 0;
            }
          }
          b->blank = 0;
          while (off < n && ln[off] == ' ') off++;       /* (a paragraph's lines lose their indentation) */
          buf_add(&b->para, ln + off, n - off);
          buf_addc(&b->para, '\n');
          continue;
        }
        if (!b->blank && b->para.len && !thematic(ln, n) && ln[ip] != '#' && ln[ip] != '>') {
          buf_add(&b->para, ln + ip, n - ip);
          buf_addc(&b->para, '\n');
          continue;
        }
        flush_para(b);
        b->nitems = 0;
      }
    }
    /* footnote definitions "[^label]: text" and their indented continuation */
    if (ind < 4 && n - ip > 4 && ln[ip] == '[' && ln[ip + 1] == '^') {
      size_t k = ip + 2;
      while (k < n && ln[k] != ']') k++;
      if (k + 1 < n && ln[k + 1] == ':') {
        int note = note_for(&b->m, ln + ip + 2, k - ip - 2);
        flush_para(b);
        b->blank = 0;
        if (note >= 0) {
          size_t q = k + 2;
          while (q < n && ln[q] == ' ') q++;
          para_style(b, 0);
          b->pnote = (note & 0xFFFF) + 1 + (note & 0x10000);
          b->noteblock = b->pnote;
          buf_add(&b->para, ln + q, n - q);
          buf_addc(&b->para, '\n');
        }
        continue;
      }
    }
    if (b->noteblock && ind >= 4) {
      if (b->blank) { flush_para(b); para_style(b, 0); b->pnote = b->noteblock; }
      b->pnote = b->noteblock;
      buf_add(&b->para, ln + skip_cols(ln, n, 4), n - skip_cols(ln, n, 4));
      buf_addc(&b->para, '\n');
      b->blank = 0;
      continue;
    }
    if (b->noteblock && b->blank) b->noteblock = 0;
    if (b->noteblock && b->para.len) { buf_add(&b->para, ln + ip, n - ip); buf_addc(&b->para, '\n'); continue; }
    b->noteblock = 0;
    /* indented code (not inside a paragraph) */
    if (ind >= 4 && !b->para.len) {
      Para *p;
      size_t off = skip_cols(ln, n, 4);
      if (b->code < 0) b->code = fmt_code_style(d);
      para_style(b, b->code);
      p = para_new(&b->ppf, &b->pcf);
      if (p) para_append(p, ln + off, (u32)(n - off), &b->pcf);
      add_para(b, p, 0);
      b->blank = 0;
      continue;
    }
    b->blank = 0;
    /* block quotes: their marks taken off; the lines are Quote paragraphs */
    {
      int q = 0;
      while (ip < n && ln[ip] == '>' && ind < 4) {
        ip++;
        if (ip < n && ln[ip] == ' ') ip++;
        q = 1;
        while (ip < n && ln[ip] == ' ') { ip++; }
        ind = 0;
      }
      if (q) {
        if (!b->quote || !b->para.len) { flush_para(b); { int k = fmt_quote_style(d); para_style(b, k >= 0 ? k : 0); } }
        b->quote = 1;
        if (ip >= n) { flush_para(b); continue; }
      } else if (b->quote && !b->para.len) b->quote = 0;
    }
    /* fenced code starts */
    if (n - ip >= 3 && (ln[ip] == '`' || ln[ip] == '~')) {
      size_t q = ip;
      int run = 0;
      char ch = ln[ip];
      while (q < n && ln[q] == ch) { q++; run++; }
      if (run >= 3 && (ch == '~' || !memchr(ln + q, '`', n - q))) {
        flush_para(b);
        fence = 1; fencech = ch; fencelen = run; fenceindent = ind;
        if (b->code < 0) b->code = fmt_code_style(d);
        continue;
      }
    }
    /* thematic break, ATX heading, setext underline */
    if (thematic(ln + ip, n - ip) && ind < 4) {
      int dash = memchr(ln + ip, '-', n - ip) != NULL;
      if (dash && b->para.len && !b->quote && !b->nitems) {
        /* a setext heading 2 */
        para_style(b, fmt_heading_style(d, 2));
        flush_para(b);
        continue;
      }
      flush_para(b);
      {
        ParaFmt pf = d->styles[0].pf;
        Para *p;
        pf.bstyle[PE_BOTTOM] = PB_SINGLE; pf.bwidth[PE_BOTTOM] = 6; pf.bcolor[PE_BOTTOM] = 0x808080;
        p = para_new(&pf, &d->styles[0].cf);
        add_para(b, p, 0);
      }
      continue;
    }
    if (b->para.len && !b->quote && ind < 4) {
      size_t q = ip;
      while (q < n && ln[q] == '=') q++;
      while (q < n && ln[q] == ' ') q++;
      if (q == n && ln[ip] == '=') { para_style(b, fmt_heading_style(d, 1)); flush_para(b); continue; }
    }
    if (ln[ip] == '#' && ind < 4) {
      size_t q = ip;
      int h = 0;
      while (q < n && ln[q] == '#' && h < 7) { q++; h++; }
      if (h <= 6 && (q == n || ln[q] == ' ' || ln[q] == '\t')) {
        size_t a = q, z = n;
        flush_para(b);
        while (a < z && (ln[a] == ' ' || ln[a] == '\t')) a++;
        while (z > a && (ln[z - 1] == ' ' || ln[z - 1] == '\t')) z--;
        {
          /* a closing run of #s goes */
          size_t y = z;
          while (y > a && ln[y - 1] == '#') y--;
          if (y < z && (y == a || ln[y - 1] == ' ' || ln[y - 1] == '\t')) { z = y; while (z > a && (ln[z - 1] == ' ' || ln[z - 1] == '\t')) z--; }
        }
        para_style(b, fmt_heading_style(d, h));
        buf_add(&b->para, ln + a, z - a);
        flush_para(b);
        continue;
      }
    }
    /* a table: this line and a delimiter row under it */
    if (!b->para.len && memchr(ln, '|', n) && pos < len) {
      size_t e2 = pos;
      while (e2 < len && s[e2] != '\n') e2++;
      {
        int k = delim_row(s + pos, e2 - pos, talign, MAX_COLS);
        size_t cs[MAX_COLS], ce[MAX_COLS];
        int hk = split_row(ln, n, cs, ce, MAX_COLS);
        if (k && k == hk) {
          int j;
          tablecols = k;
          tb_begin(&tb, b->l.np, 1);
          tb_row(&tb);
          for (j = 0; j < k; j++) {
            Para *p;
            ParaFmt pf = d->styles[0].pf;
            CharFmt cf = d->styles[0].cf;
            Buf cell;
            size_t q;
            tb_cell(&tb, 1, 1, 1);
            pf.align = talign[j];
            pf.left = pf.right = pf.first = 0;
            tb_mark(&tb, &pf);
            p = para_new(&pf, &cf);
            buf_init(&cell);
            for (q = cs[j]; q < ce[j]; q++) { if (ln[q] == '\\' && q + 1 < ce[j] && ln[q + 1] == '|') q++; buf_addc(&cell, ln[q]); }
            if (p) { inlines(&b->m, cell.data ? cell.data : "", cell.len); emit_pieces(&b->m, p, &cf); }
            buf_free(&cell);
            add_para(b, p, 0);
          }
          pos = e2 + 1;
          continue;
        }
      }
    }
    /* a link reference definition: not text */
    if (!b->para.len && ln[ip] == '[' && ind < 4) {
      size_t l = ip + 1;
      while (l < n && ln[l] != ']') l++;
      if (l + 1 < n && ln[l + 1] == ':' && ln[ip + 1] != '^') continue;
    }
    /* paragraph text */
    if (!b->para.len && !b->quote) para_style(b, 0);
    buf_add(&b->para, ln + ip, n - ip);
    buf_addc(&b->para, '\n');
  }
  flush_para(b);
  if (tablecols) tb_end(&tb, d, &b->l, fmt_text_width(d));
  {
    int k;
    for (k = 0; k < b->m.ndefs; k++) free(b->m.defs[k].url);
    free(b->m.defs);
    free(b->m.notelabels);
    free(b->m.pc);
  }
  buf_free(&b->para); buf_free(&b->m.text); buf_free(&src);
  if (b->l.oom || !pl_to_body(d, &b->l)) { pl_free(&b->l); free(b); doc_free(d); snprintf(err, errcap, "Not enough memory to open the file."); return NULL; }
  free(b);
  doc_clear_undo(d);
  d->dirty = 0;
  return d;
}

const DocFormat fmt_markdown = { "Markdown", "md|markdown", NULL, md_load, md_save, 0 };
