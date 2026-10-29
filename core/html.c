/*
 * html.c - HTML files (docs/wordcraft-gap.md WP28c). This file belongs to WP28c: only that package
 * edits it.
 *
 * Writing: an HTML5 page in UTF-8. Paragraph styles become CSS classes in the page's <style>
 * (headings are <h1>-<h6>), a paragraph's own formatting its style attribute (alignment, indents,
 * spacing, line spacing, shading, borders, page break before), character formatting <b>, <i>, <u>,
 * <s>, <sup>, <sub> and a <span> with the rest (font, size, colour, highlight, small caps, caps,
 * letter spacing); lists as <ul>/<ol> nested by level; tables with merged cells (colspan, rowspan),
 * shading, borders and column widths; links (<a href>) and bookmarks (id); pictures embedded as
 * data: URIs at their size; footnotes and endnotes as numbered notes after the text. Headers,
 * footers, sections and page setup have no place in a web page and are left out.
 *
 * Reading: the same and what other programs write (LibreOffice's <font> tags, CSS in style
 * attributes, <center>, <blockquote>, <pre>, <hr>, definition lists): block elements make paragraphs
 * (h1-h6 the heading styles, pre the Code style, blockquote Quote, class= a style of that id),
 * inline elements and CSS character formatting; tables (nested ones flattened into their cell);
 * pictures from data: URIs or from files next to the page (nothing is fetched from the web);
 * <title> and <meta name="author">; UTF-8, or Windows 1252 when the bytes aren't UTF-8.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "docx.h"
#include "fmtio.h"
#include "codepage.h"
#include "image.h"

/* ================================================================== */
/* writing */

typedef struct {
  Doc *d;
  Buf *b;
  u16 bmark;
  int nnote, nenote;        /* notes referred to so far */
  int *noteorder, *enoteorder;   /* their indexes in that order */
} HW;

static void pt(Buf *b, i32 tw)
{
  long v = tw < 0 ? -(long)tw : tw;
  buf_printf(b, "%s%ld", tw < 0 ? "-" : "", v / 20);
  if (v % 20) buf_printf(b, ".%02ld", (v % 20) * 5);
  buf_adds(b, "pt");
}
static void hex(Buf *b, u32 c) { buf_printf(b, "#%06lx", (unsigned long)(c & 0xFFFFFF)); }
/* text escaped; spaces that HTML would swallow (leading, doubled, trailing) and tabs as character references */
static void esc(Buf *b, const char *s, u32 n, int *prevspace)
{
  u32 i;
  for (i = 0; i < n; i++) {
    char c = s[i];
    if (c == '&') buf_adds(b, "&amp;");
    else if (c == '<') buf_adds(b, "&lt;");
    else if (c == '>') buf_adds(b, "&gt;");
    else if (c == '"') buf_adds(b, "&quot;");
    else if (c == '\t') buf_adds(b, "&#9;");
    else if (c == '\v') buf_adds(b, "<br>\n");
    else if (c == ' ') {
      if (*prevspace || i + 1 == n) buf_adds(b, "&#32;"); else buf_addc(b, ' ');
    } else buf_addc(b, c);
    *prevspace = c == ' ' || c == '\t' || c == '\v';
  }
}
static void esc_attr(Buf *b, const char *s)
{
  int prev = 0;
  esc(b, s, (u32)strlen(s), &prev);
}
static const char *generic(const char *f)
{
  if (fmt_mono_font(f)) return "monospace";
  if (strstr(f, "Times") || strstr(f, "Georgia") || strstr(f, "Cambria") || strstr(f, "Garamond") || strstr(f, "Serif") ||
      strstr(f, "Roman") || strstr(f, "Palatino") || strstr(f, "Book")) return strstr(f, "Sans") ? "sans-serif" : "serif";
  return "sans-serif";
}
/* the CSS of character formatting f where it differs from base (base NULL: all of it) */
static void css_cf(HW *w, Buf *b, const CharFmt *f, const CharFmt *base, int tags)
{
  Doc *d = w->d;
  int bold = f->flags & CF_BOLD, it = f->flags & CF_ITALIC, ul = f->flags & CF_UNDERLINE, st = f->flags & CF_STRIKE;
  if (!base || f->font != base->font) buf_printf(b, "font-family:'%s',%s;", d->fonts[f->font], generic(d->fonts[f->font]));
  if (!base || f->size != base->size) { buf_printf(b, "font-size:%d", f->size / 2); if (f->size & 1) buf_adds(b, ".5"); buf_adds(b, "pt;"); }
  if (!base || f->color != base->color) { if (f->color == COLOR_AUTO) { if (base) buf_adds(b, "color:black;"); } else { buf_adds(b, "color:"); hex(b, f->color); buf_addc(b, ';'); } }
  if ((!base && (f->hl & HL_ON)) || (base && f->hl != base->hl)) { if (f->hl & HL_ON) { buf_adds(b, "background-color:"); hex(b, f->hl); buf_addc(b, ';'); } else buf_adds(b, "background-color:transparent;"); }
  if (!tags || !base) {
    if (!base ? bold : bold != (base->flags & CF_BOLD)) buf_adds(b, bold ? "font-weight:bold;" : "font-weight:normal;");
    if (!base ? it : it != (base->flags & CF_ITALIC)) buf_adds(b, it ? "font-style:italic;" : "font-style:normal;");
    if (!base ? (ul || st) : (ul != (base->flags & CF_UNDERLINE) || st != (base->flags & CF_STRIKE)))
      buf_printf(b, "text-decoration:%s%s%s;", ul ? "underline" : "", ul && st ? " " : "", st ? "line-through" : (ul ? "" : "none"));
  } else {
    /* (tags give the positive differences; only turning off needs CSS) */
    if (!bold && (base->flags & CF_BOLD)) buf_adds(b, "font-weight:normal;");
    if (!it && (base->flags & CF_ITALIC)) buf_adds(b, "font-style:normal;");
    if ((!ul && (base->flags & CF_UNDERLINE)) || (!st && (base->flags & CF_STRIKE))) buf_adds(b, "text-decoration:none;");
  }
  if ((f->fx & FX_SMALLCAPS) != (base ? (base->fx & FX_SMALLCAPS) : 0)) buf_adds(b, (f->fx & FX_SMALLCAPS) ? "font-variant:small-caps;" : "font-variant:normal;");
  if ((f->flags & CF_CAPS) != (base ? (base->flags & CF_CAPS) : 0)) buf_adds(b, (f->flags & CF_CAPS) ? "text-transform:uppercase;" : "text-transform:none;");
  if (f->spacing != (base ? base->spacing : 0)) { buf_adds(b, "letter-spacing:"); pt(b, f->spacing); buf_addc(b, ';'); }
}
static void css_border(Buf *b, const char *edge, int style, int width, u32 color)
{
  static const char *const k[] = { "none", "solid", "double", "solid", "dotted", "dashed" };
  int w8 = width ? width : 4;
  if (style == PB_THICK && w8 < 12) w8 = 12;
  buf_printf(b, "border-%s:", edge);
  pt(b, w8 * 20 / 8);
  buf_printf(b, " %s ", k[style >= 0 && style <= PB_DASHED ? style : 1]);
  if (color == COLOR_AUTO) buf_adds(b, "black"); else hex(b, color);
  buf_addc(b, ';');
}
/* the CSS of paragraph formatting p where it differs from base (NULL: all of it) */
static void css_pf(Buf *b, const ParaFmt *p, const ParaFmt *base)
{
  static const char *const al[4] = { "left", "center", "right", "justify" };
  int k;
  if (!base || p->align != base->align) buf_printf(b, "text-align:%s;", al[p->align & 3]);
  if (!base || p->before != base->before) { buf_adds(b, "margin-top:"); pt(b, p->before); buf_addc(b, ';'); }
  if (!base || p->after != base->after) { buf_adds(b, "margin-bottom:"); pt(b, p->after); buf_addc(b, ';'); }
  if (!base || p->left != base->left) { buf_adds(b, "margin-left:"); pt(b, p->left); buf_addc(b, ';'); }
  if (!base || p->right != base->right) { buf_adds(b, "margin-right:"); pt(b, p->right); buf_addc(b, ';'); }
  if (!base || p->first != base->first) { buf_adds(b, "text-indent:"); pt(b, p->first); buf_addc(b, ';'); }
  if (!base || p->line != base->line || p->linerule != base->linerule) {
    if (p->linerule == LS_AUTO) buf_printf(b, "line-height:%ld%%;", (long)((p->line ? p->line : 240) * 100 / 240));
    else { buf_adds(b, p->linerule == LS_EXACT ? "line-height:" : "min-height:0;line-height:"); pt(b, p->line); buf_addc(b, ';'); }
  }
  if ((!base && (p->shade & HL_ON)) || (base && p->shade != base->shade)) {
    if (p->shade & HL_ON) { buf_adds(b, "background-color:"); hex(b, p->shade); buf_addc(b, ';'); }
    else buf_adds(b, "background-color:transparent;");
  }
  {
    static const char *const edge[4] = { "top", "left", "bottom", "right" };
    for (k = 0; k < 4; k++)
      if (p->bstyle[k] != PB_NONE && (!base || p->bstyle[k] != base->bstyle[k] || p->bwidth[k] != base->bwidth[k] || p->bcolor[k] != base->bcolor[k]))
        css_border(b, edge[k], p->bstyle[k], p->bwidth[k], p->bcolor[k]);
  }
  if (p->pagebreak && (!base || !base->pagebreak)) buf_adds(b, "page-break-before:always;");
  if (p->keepnext && (!base || !base->keepnext)) buf_adds(b, "page-break-after:avoid;");
}
static void class_name(Buf *b, const Style *s)
{
  const char *q;
  for (q = s->id; *q; q++) if ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9') || *q == '-' || *q == '_') buf_addc(b, *q);
}
/* the tag a paragraph of style k is written as */
static const char *para_tag(Doc *d, int k, char *tag)
{
  int h = fmt_heading_level(d, k);
  if (h >= 1 && h <= 6) { sprintf(tag, "h%d", h); return tag; }
  return "p";
}
static int note_number(int *order, int *n, int k)
{
  int i;
  for (i = 0; i < *n; i++) if (order[i] == k) return i + 1;
  order[(*n)++] = k;
  return *n;
}
static void w_runs(HW *w, const Para *p, int notes)
{
  Doc *d = w->d;
  Buf *b = w->b;
  const CharFmt *base = &d->styles[p->pf.style < d->nstyles ? p->pf.style : 0].cf;
  u32 k, start = 0;
  u16 link = 0;
  int prevspace = 1;
  for (k = 0; k < p->nr; k++) {
    const CharFmt *f = &p->r[k].f;
    u32 end = p->r[k].end, i = start;
    Buf css;
    int tb = 0, ti = 0, tu = 0, ts = 0, tsup = 0, tsub = 0, had_span, newbm;
    if (end == start) continue;
    if (f->rev == REV_DEL) { start = end; continue; }            /* a deletion not yet accepted: as Word's "no markup" */
    if (f->link != link) {
      if (link) buf_adds(b, "</a>");
      link = f->link;
      if (link) { buf_adds(b, "<a href=\""); esc_attr(b, doc_link_url(d, link)); buf_adds(b, "\">"); }
    }
    tb = (f->flags & CF_BOLD) && !(base->flags & CF_BOLD);
    ti = (f->flags & CF_ITALIC) && !(base->flags & CF_ITALIC);
    tu = (f->flags & CF_UNDERLINE) && !(base->flags & CF_UNDERLINE) && !f->link;
    ts = (f->flags & CF_STRIKE) && !(base->flags & CF_STRIKE);
    tsup = (f->flags & CF_SUPER) != 0;
    tsub = (f->flags & CF_SUB) != 0;
    buf_init(&css);
    newbm = f->bmark && f->bmark != w->bmark;
    w->bmark = f->bmark;
    {
      CharFmt g = *f;
      if (f->link && f->cstyle && f->cstyle < d->nstyles && d->styles[f->cstyle].type == ST_CHAR) {
        /* a link's own look comes with the <a> */
        if (g.color == d->styles[f->cstyle].cf.color) g.color = base->color;
        if ((d->styles[f->cstyle].ecmask & CM_UNDERLINE) && (g.flags & CF_UNDERLINE)) g.flags = (u8)((g.flags & ~CF_UNDERLINE) | (base->flags & CF_UNDERLINE));
      }
      css_cf(w, &css, &g, base, 1);
    }
    if (tb) buf_adds(b, "<b>");
    if (ti) buf_adds(b, "<i>");
    if (tu) buf_adds(b, "<u>");
    if (ts) buf_adds(b, "<s>");
    if (tsup) buf_adds(b, "<sup>");
    if (tsub) buf_adds(b, "<sub>");
    had_span = css.len > 0;
    if (had_span) { buf_adds(b, "<span style=\""); buf_add(b, css.data, css.len); buf_adds(b, "\">"); }
    buf_free(&css);
    /* a bookmark starting here: the run's text carries its name (one element per bookmark: ids are unique) */
    if (newbm) { buf_adds(b, "<span id=\""); esc_attr(b, doc_bmark_name(d, f->bmark)); buf_adds(b, "\">"); }
    while (i < end) {
      u32 j;
      for (j = i; j < end; j++) {
        u8 c = (u8)p->t[j];
        if ((c == 0xF3 || c == 0xF4) && j + 4 <= end && (IS_PIC(p->t + j) || IS_NOTE(p->t + j) || IS_ENOTE(p->t + j) || IS_MARK(p->t + j))) break;
        if (c == 0xEE && j + 3 <= end && IS_FIELD(p->t + j)) break;
      }
      if (j > i) {
        /* (a space is doubled only within this stretch: a run boundary keeps the state) */
        esc(b, p->t + i, j - i, &prevspace);
      }
      i = j;
      if (i >= end) break;
      if (IS_FIELD(p->t + i)) { i += 3; continue; }
      if (IS_PIC(p->t + i)) {
        int n = pic_index(p->t + i);
        const Image *im = n >= 0 && n < d->nimages ? &d->images[n] : NULL;
        if (im && im->kind == OBJ_PICTURE && im->data && (im->type == IMG_PNG || im->type == IMG_JPEG)) {
          buf_printf(b, "<img src=\"data:image/%s;base64,", im->type == IMG_PNG ? "png" : "jpeg");
          b64_encode(b, im->data, im->len);
          buf_printf(b, "\" width=\"%ld\" height=\"%ld\" alt=\"", (long)(im->cx + 7) / 15, (long)(im->cy + 7) / 15);
          esc_attr(b, im->alt);
          buf_adds(b, "\">");
        }
        prevspace = 0;
        i += 4;
        continue;
      }
      if (IS_NOTE(p->t + i) || IS_ENOTE(p->t + i)) {
        int en = IS_ENOTE(p->t + i), n = note_index(p->t + i);
        if (notes && n >= 0 && n < (en ? d->nenotes : d->nnotes)) {
          int num = en ? note_number(w->enoteorder, &w->nenote, n) : note_number(w->noteorder, &w->nnote, n);
          if (en) buf_printf(b, "<sup><a href=\"#en%d\" id=\"enref%d\">%d</a></sup>", num, num, num);
          else buf_printf(b, "<sup><a href=\"#fn%d\" id=\"fnref%d\">%d</a></sup>", num, num, num);
        }
        prevspace = 0;
        i += 4;
        continue;
      }
      i += 4;
    }
    if (newbm) buf_adds(b, "</span>");
    if (had_span) buf_adds(b, "</span>");
    if (tsub) buf_adds(b, "</sub>");
    if (tsup) buf_adds(b, "</sup>");
    if (ts) buf_adds(b, "</s>");
    if (tu) buf_adds(b, "</u>");
    if (ti) buf_adds(b, "</i>");
    if (tb) buf_adds(b, "</b>");
    start = end;
  }
  if (link) buf_adds(b, "</a>");
}
/* one paragraph as <p>/<hN> (inli: inside a list item, which is the block) */
static void w_para(HW *w, const Para *p, int inli, i32 parent_left)
{
  Doc *d = w->d;
  Buf *b = w->b;
  int k = p->pf.style < d->nstyles ? p->pf.style : 0;
  char tagb[4];
  const char *tag = inli ? NULL : para_tag(d, k, tagb);
  Buf css;
  ParaFmt base = d->styles[k].pf;
  buf_init(&css);
  if (p->pf.list) { base.left = p->pf.left; base.first = p->pf.first; }
  css_pf(&css, &p->pf, &base);
  if (inli) {
    /* a list item's indent from the item it's in (lists have no padding of their own here); the
       hanging indent as a custom property (browsers put the label outside the item anyway) */
    buf_adds(&css, "margin-left:"); pt(&css, p->pf.left - parent_left);
    buf_adds(&css, ";--first:"); pt(&css, p->pf.first); buf_addc(&css, ';');
  }
  if (tag) {
    buf_printf(b, "<%s class=\"", tag);
    class_name(b, &d->styles[k]);
    buf_addc(b, '"');
  } else {
    buf_adds(b, "<li class=\"");
    class_name(b, &d->styles[k]);
    buf_addc(b, '"');
  }
  if (css.len) { buf_adds(b, " style=\""); buf_add(b, css.data, css.len); buf_addc(b, '"'); }
  buf_addc(b, '>');
  buf_free(&css);
  w_runs(w, p, 1);
  if (tag) buf_printf(b, "</%s>\n", tag);
  (void)parent_left;
}
/* lists: each paragraph in one, in <ul>/<ol> nested by level; returns the paragraph after them */
static u32 w_lists(HW *w, Para **p, u32 i, u32 end)
{
  Doc *d = w->d;
  Buf *b = w->b;
  int open[9], depth = 0;           /* the list kinds open: 0 ul, 1 ol */
  i32 lefts[9];                     /* the left indent of the last item at each level */
  u16 list = p[i]->pf.list;
  while (i < end && p[i]->pf.list == list) {
    const Para *q = p[i];
    int lvl = q->pf.level > 8 ? 8 : q->pf.level;
    const ListLevel *lv = &d->lists[list - 1].lvl[lvl];
    int kind = lv->fmt != NF_BULLET;
    while (depth > lvl + 1) { buf_adds(b, "</li>\n"); buf_adds(b, open[--depth] ? "</ol>\n" : "</ul>\n"); }
    if (depth == lvl + 1 && open[depth - 1] != kind) { buf_adds(b, "</li>\n"); buf_adds(b, open[--depth] ? "</ol>\n" : "</ul>\n"); }
    if (depth == lvl + 1) buf_adds(b, "</li>\n");
    while (depth < lvl + 1) {
      const ListLevel *v = &d->lists[list - 1].lvl[depth];
      int kd = depth == lvl ? kind : v->fmt != NF_BULLET;
      if (kd) {
        static const char *const ty[] = { "1", "1", "a", "A", "i", "I", "1" };
        buf_printf(b, "<ol type=\"%s\"", ty[v->fmt <= NF_NONE ? v->fmt : 1]);
        if (v->start > 1) buf_printf(b, " start=\"%d\"", (int)v->start);
        buf_adds(b, " style=\"margin:0;padding:0\">");
      } else buf_adds(b, v->text[0] && strcmp(v->text, "\xE2\x80\xA2") ? (strcmp(v->text, "o") ? "<ul style=\"margin:0;padding:0;list-style-type:square\">" : "<ul style=\"margin:0;padding:0;list-style-type:circle\">") : "<ul style=\"margin:0;padding:0\">");
      open[depth++] = kd;
      if (depth < lvl + 1) { buf_adds(b, "<li style=\"list-style-type:none;margin-left:0\">"); lefts[depth - 1] = depth > 1 ? lefts[depth - 2] : 0; }
    }
    w_para(w, q, 1, lvl ? lefts[lvl - 1] : 0);
    lefts[lvl] = q->pf.left;
    { int z; for (z = lvl + 1; z < 9; z++) lefts[z] = q->pf.left; }
    i++;
  }
  while (depth > 0) { buf_adds(b, "</li>\n"); buf_adds(b, open[--depth] ? "</ol>\n" : "</ul>\n"); }
  return i;
}
static void w_blocks(HW *w, Para **p, u32 a, u32 b, int incell);
/* a table from paragraph i; returns the paragraph after it */
static u32 w_table(HW *w, Para **p, u32 i, u32 end)
{
  Doc *d = w->d;
  Buf *b = w->b;
  const Cell *c0 = top_cell(&p[i]->pf);
  u16 tn = c0->table;
  const Table *t = tn <= d->ntables ? &d->tables[tn - 1] : NULL;
  u32 j, e = i;
  int k, row = -1;
  while (e < end && top_cell(&p[e]->pf)->table == tn) e++;
  buf_adds(b, "<table style=\"border-collapse:collapse");
  if (t && t->jc == AL_CENTER) buf_adds(b, ";margin-left:auto;margin-right:auto");
  else if (t && t->indent) { buf_adds(b, ";margin-left:"); pt(b, t->indent); }
  buf_adds(b, "\">\n");
  if (t) {
    buf_adds(b, "<colgroup>");
    for (k = 0; k < t->ncols; k++) { buf_adds(b, "<col style=\"width:"); pt(b, t->grid[k]); buf_adds(b, "\">"); }
    buf_adds(b, "</colgroup>\n");
  }
  for (j = i; j < e; ) {
    const Cell *c = top_cell(&p[j]->pf);
    u32 ce = j;
    while (ce < e && top_cell(&p[ce]->pf)->row == c->row && top_cell(&p[ce]->pf)->col == c->col) ce++;
    if (c->row != row) {
      if (row >= 0) buf_adds(b, "</tr>\n");
      row = c->row;
      buf_adds(b, "<tr");
      if (c->rowh) { buf_adds(b, " style=\"height:"); pt(b, c->rowh < 0 ? -c->rowh : c->rowh); buf_adds(b, "\""); }
      buf_adds(b, ">");
    }
    if (c->vmerge != VM_CONT) {
      int rs = 1;
      const char *tag = c->header ? "th" : "td";
      if (c->vmerge == VM_START) {
        /* how many rows below continue it */
        u32 q;
        int r = c->row;
        for (q = ce; q < e; q++) {
          const Cell *x = top_cell(&p[q]->pf);
          if (x->col != c->col || x->row <= r) continue;
          if (x->row != r + 1) break;
          if (x->vmerge != VM_CONT) break;
          rs++; r = x->row;
        }
      }
      buf_printf(b, "<%s", tag);
      if (c->span > 1) buf_printf(b, " colspan=\"%d\"", c->span);
      if (rs > 1) buf_printf(b, " rowspan=\"%d\"", rs);
      buf_adds(b, " style=\"vertical-align:");
      buf_adds(b, c->valign == 1 ? "middle" : c->valign == 2 ? "bottom" : "top");
      if (t) { buf_adds(b, ";padding:0 "); pt(b, t->marr); buf_addc(b, ' '); buf_adds(b, "0 "); pt(b, t->marl); }
      if (c->shade != COLOR_AUTO) { buf_adds(b, ";background-color:"); hex(b, c->shade); }
      buf_addc(b, ';');
      {
        static const char *const edge[4] = { "top", "left", "bottom", "right" };
        for (k = 0; k < 4; k++) if (c->borders & (1 << k)) css_border(b, edge[k], c->bstyle[k] ? c->bstyle[k] : PB_SINGLE, c->bwidth[k], c->bcolor[k]);
      }
      buf_adds(b, "\">");
      w_blocks(w, p, j, ce, 1);
      buf_printf(b, "</%s>", tag);
    }
    j = ce;
  }
  if (row >= 0) buf_adds(b, "</tr>\n");
  buf_adds(b, "</table>\n");
  return e;
}
/* paragraphs a..b: tables, lists and paragraphs (a table's cell: the paragraphs inside it, nested
   tables flattened) */
static void w_blocks(HW *w, Para **p, u32 a, u32 b, int incell)
{
  u32 i = a;
  while (i < b) {
    const Para *q = p[i];
    if (!incell && top_cell(&q->pf)->table) { i = w_table(w, p, i, b); continue; }
    if (q->pf.list && q->pf.list <= w->d->nlists) { i = w_lists(w, p, i, b); continue; }
    w_para(w, q, 0, 0);
    i++;
  }
}
static void w_notes(HW *w, int end)
{
  Doc *d = w->d;
  Buf *b = w->b;
  int i, n = end ? w->nenote : w->nnote;
  if (!n) return;
  buf_printf(b, "<hr class=\"notes\">\n<ol class=\"%s\">\n", end ? "endnotes" : "footnotes");
  for (i = 0; i < n; i++) {
    int k = end ? w->enoteorder[i] : w->noteorder[i];
    const Story *s = end ? &d->enotes[k] : &d->notes[k];
    u32 j;
    buf_printf(b, "<li id=\"%s%d\">", end ? "en" : "fn", i + 1);
    for (j = 0; j < s->np; j++) {
      buf_adds(b, "<p class=\""); class_name(b, &d->styles[s->p[j]->pf.style < d->nstyles ? s->p[j]->pf.style : 0]); buf_adds(b, "\">");
      w_runs(w, s->p[j], 0);
      if (j + 1 == s->np) buf_printf(b, " <a href=\"#%sref%d\">&#8617;</a>", end ? "en" : "fn", i + 1);
      buf_adds(b, "</p>");
    }
    buf_adds(b, "</li>\n");
  }
  buf_adds(b, "</ol>\n");
}
static u8 *html_save(Doc *d, size_t *len)
{
  Buf b;
  HW w;
  int k;
  u8 *used;
  u32 i;
  memset(&w, 0, sizeof w);
  w.d = d;
  buf_init(&b);
  w.b = &b;
  w.noteorder = (int *)malloc((size_t)(d->nnotes + 1) * sizeof(int));
  w.enoteorder = (int *)malloc((size_t)(d->nenotes + 1) * sizeof(int));
  used = (u8 *)calloc((size_t)d->nstyles + 1, 1);
  if (!w.noteorder || !w.enoteorder || !used) { free(w.noteorder); free(w.enoteorder); free(used); return NULL; }
  for (i = 0; i < d->np; i++) if (d->p[i]->pf.style < d->nstyles) used[d->p[i]->pf.style] = 1;
  used[0] = 1;
  buf_adds(&b, "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"generator\" content=\"Quill\">\n<title>");
  esc_attr(&b, d->title[0] ? d->title : (d->np ? "" : ""));
  buf_adds(&b, "</title>\n");
  if (d->author[0]) { buf_adds(&b, "<meta name=\"author\" content=\""); esc_attr(&b, d->author); buf_adds(&b, "\">\n"); }
  buf_adds(&b, "<style>\n");
  buf_adds(&b, "body { max-width:");
  pt(&b, d->page.width - d->page.left - d->page.right);
  buf_adds(&b, "; margin:2em auto; }\np, h1, h2, h3, h4, h5, h6, li, td, th { white-space:pre-wrap; }\ntable { border-collapse:collapse; }\n");
  for (k = 0; k < d->nstyles; k++) {
    const Style *s = &d->styles[k];
    Buf css;
    if (s->type != ST_PARA || !used[k]) continue;
    buf_init(&css);
    css_pf(&css, &s->pf, NULL);
    css_cf(&w, &css, &s->cf, NULL, 0);
    buf_adds(&b, "/* "); { const char *q; for (q = s->name; *q; q++) if (*q != '*' && *q != '/') buf_addc(&b, *q); } buf_adds(&b, " */ ");
    buf_addc(&b, '.'); class_name(&b, s);
    buf_adds(&b, " { "); buf_add(&b, css.data, css.len); buf_adds(&b, " }\n");
    buf_free(&css);
  }
  buf_adds(&b, "</style>\n</head>\n<body>\n");
  w_blocks(&w, d->p, 0, d->np, 0);
  w_notes(&w, 0);
  w_notes(&w, 1);
  buf_adds(&b, "</body>\n</html>\n");
  free(w.noteorder); free(w.enoteorder); free(used);
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}

/* ================================================================== */
/* reading */

enum { HK_INLINE, HK_BLOCK, HK_PARA, HK_LIST, HK_ITEM, HK_TABLE, HK_ROW, HK_CELL, HK_PRE, HK_SKIP, HK_VOID };
typedef struct {
  char tag[12];
  u8 kind;
  CharFmt cf;
  ParaFmt pf;
  u32 nparas;               /* paragraphs made before it opened */
  int list, level;          /* a list: its Quill list and depth; the list's items get them */
  u8 ordered;
  u8 pre;
  u8 tdepth;                /* tables open around it */
  int note;                 /* a note's item: its paragraphs go to note (note - 1), endnote if enote */
  u8 enote, notes;          /* notes: the list of notes after the text */
} HFrame;

#define MAXH 200
typedef struct {
  Doc *d;
  const char *s; size_t n, i;
  int utf8;
  HFrame f[MAXH]; int depth;
  ParaList l;
  Para *cur;                /* the paragraph being filled (NULL: none open) */
  int curtext;              /* it has text */
  int space;                /* a space waits (collapsed white space) */
  int brs;                  /* line breaks waiting */
  CharFmt spacecf;
  TableBld tb; int tdepth;  /* tables open; the outermost is built */
  int tbon;                 /* the outermost table has started its rows */
  int incell;
  int listdepth;
  int skip;                 /* inside <head>, <script>, <style>: text goes */
  int intitle;
  Buf title;
  unsigned *lost;
  int code;                 /* the Code style's index (made on first use) */
  u32 pendshade; u8 pendborders;   /* the cell being filled: its shading and lines */
  u32 made;                 /* paragraphs made so far (the body's and notes') */
  int notemap[2][256];      /* a note's number in the page -> its index + 1 (footnotes, endnotes) */
  u32 prevc;                /* the last character of text read (white space after a newline is indentation) */
  i32 colw[MAX_COLS]; int ncolw;   /* the outermost table's <col> widths */
  i32 padl, padr;           /* its cells' padding (-1: none given) */
  i32 tindent; u8 tjc;      /* its place */
} HR;

static HFrame *ftop(HR *r) { return &r->f[r->depth]; }

/* the paragraph being filled ends; force: even if it has nothing (an empty <p>) */
static void h_end(HR *r, int force)
{
  Para *p;
  HFrame *t = ftop(r);
  if (!r->cur && !force) { r->space = 0; r->brs = 0; return; }
  if (!r->cur) { if (!(r->cur = para_new(&t->pf, &t->cf))) return; }
  p = r->cur; r->cur = NULL;
  if (r->tdepth && r->tbon) {
    ParaFmt pf = p->pf;
    tb_mark(&r->tb, &pf);
    pf.cell.shade = r->pendshade;
    if (r->pendborders) pf.cell.borders = BD_ALL;
    p->pf = pf;
  }
  r->made++;
  r->curtext = 0; r->space = 0; r->brs = 0;
  if (t->note) {
    /* a note's paragraph */
    Story *st = t->enote ? &r->d->enotes[t->note - 1] : &r->d->notes[t->note - 1];
    Para **np = (Para **)realloc(st->p, (st->np + 1) * sizeof(Para *));
    if (!np) { para_free(p); return; }
    memset(&p->pf.cell, 0, sizeof p->pf.cell);
    p->pf.list = 0;
    para_normalise(p);
    st->p = np; st->p[st->np++] = p;
    return;
  }
  pl_add(&r->l, p);
}
static Para *h_para(HR *r)
{
  if (!r->cur) {
    HFrame *t = ftop(r);
    r->cur = para_new(&t->pf, &t->cf);
    r->curtext = 0;
  }
  return r->cur;
}
static void h_text(HR *r, const char *u, u32 n, const CharFmt *cf)
{
  Para *p = h_para(r);
  if (!p) return;
  if (r->brs) {
    while (r->brs > 0) { para_append(p, "\v", 1, cf); r->brs--; }
    r->space = 0;
  }
  if (r->space && r->curtext) para_append(p, " ", 1, &r->spacecf);
  r->space = 0;
  para_append(p, u, n, cf);
  r->curtext = 1;
}
static void h_cp(HR *r, u32 cp, int literal)
{
  HFrame *t = ftop(r);
  char u[8];
  if (r->skip) { if (r->intitle) { int k = utf8_put(u, cp); buf_add(&r->title, u, (size_t)k); } return; }
  if (literal && !t->pre && cp == '\t' && r->curtext && !r->space && r->prevc != '\n') {
    /* a tab inside text (not indentation) is kept: it was a tab in the document */
    h_text(r, "\t", 1, &t->cf);
    return;
  }
  if (literal && !t->pre && (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\f')) {
    if (r->curtext && !r->space) { r->space = 1; r->spacecf = t->cf; }
    return;
  }
  if (t->pre && literal && cp == '\r') return;
  if (t->pre && literal && cp == '\n') {
    /* a line of preformatted text ends */
    h_end(r, 1);
    return;
  }
  h_text(r, u, (u32)utf8_put(u, cp), &t->cf);
}

/* ---- attributes */
typedef struct { const char *name; size_t nl; const char *val; size_t vl; } HAttr;
#define MAXATTR 24
static int attr(const HAttr *a, int na, const char *name, const char **v, size_t *vl)
{
  int i;
  size_t k = strlen(name);
  for (i = 0; i < na; i++) if (a[i].nl == k && !strncmp(a[i].name, name, k)) { *v = a[i].val; *vl = a[i].vl; return 1; }
  return 0;
}
/* an attribute's value with entities decoded (UTF-8), into out */
static void attr_text(HR *r, const char *v, size_t vl, char *out, size_t cap)
{
  size_t i = 0, o = 0;
  while (i < vl && o + 5 < cap) {
    int used;
    u32 cp;
    if (v[i] == '&' && (cp = html_entity(v + i, vl - i, &used))) { i += (size_t)used; }
    else if (r->utf8) { cp = utf8_get(v + i, vl - i, &used); i += (size_t)used; }
    else { cp = cp_to_unicode(1252, (u8)v[i]); i++; }
    o += (size_t)utf8_put(out + o, cp);
  }
  out[o] = 0;
}
static int attr_int(const HAttr *a, int na, const char *name, int def)
{
  const char *v;
  size_t vl;
  if (!attr(a, na, name, &v, &vl) || !vl) return def;
  return atoi(v);
}

/* ---- CSS declarations: "name: value; ..." onto a frame */
static int font_size_kw(const char *v, size_t n)
{
  static const struct { const char *k; int hp; } kw[] = { { "xx-small", 14 }, { "x-small", 20 }, { "small", 26 }, { "medium", 24 },
    { "large", 36 }, { "x-large", 48 }, { "xx-large", 64 }, { "larger", 0 }, { "smaller", 0 } };
  size_t i;
  for (i = 0; i < sizeof kw / sizeof kw[0]; i++) if (strlen(kw[i].k) == n && !strncmp(v, kw[i].k, n)) return kw[i].hp ? kw[i].hp : -1;
  return 0;
}
static void set_family(HR *r, CharFmt *cf, const char *v, size_t n)
{
  char name[48];
  size_t k = 0;
  while (n && (*v == ' ' || *v == '"' || *v == '\'')) { v++; n--; }
  while (k < n && v[k] != ',' && v[k] != '"' && v[k] != '\'' && k < sizeof name - 1) { name[k] = v[k]; k++; }
  while (k && name[k - 1] == ' ') k--;
  name[k] = 0;
  if (!name[0]) return;
  if (str_ieq(name, "serif")) strcpy(name, "Times New Roman");
  else if (str_ieq(name, "sans-serif")) strcpy(name, "Arial");
  else if (str_ieq(name, "monospace")) strcpy(name, "Courier New");
  else if (str_ieq(name, "cursive") || str_ieq(name, "fantasy") || str_ieq(name, "inherit") || str_ieq(name, "initial") || str_ieq(name, "system-ui")) return;
  cf->font = (u8)doc_font(r->d, name);
}
static int has_word(const char *v, size_t n, const char *w)
{
  size_t k = strlen(w), i;
  for (i = 0; i + k <= n; i++) if (!strncmp(v + i, w, k)) return 1;
  return 0;
}
static void css_border_in(const char *v, size_t n, u8 *style, u8 *width, u32 *color)
{
  size_t i = 0;
  int ok, st = PB_SINGLE;
  i32 w = 10;
  if (has_word(v, n, "none") || has_word(v, n, "hidden")) { *style = PB_NONE; return; }
  if (has_word(v, n, "double")) st = PB_DOUBLE;
  else if (has_word(v, n, "dotted")) st = PB_DOTTED;
  else if (has_word(v, n, "dashed")) st = PB_DASHED;
  while (i < n) {
    size_t j = i;
    while (j < n && v[j] != ' ') j++;
    if (j > i) {
      i32 t = css_twips(v + i, j - i, &ok);
      if (ok && (v[i] >= '0' && v[i] <= '9')) w = t;
      else { u32 c = css_color(v + i, j - i); if (c != COLOR_AUTO) *color = c; }
    }
    i = j + 1;
  }
  if (w <= 0) { *style = PB_NONE; return; }
  *style = (u8)st;
  {
    long e = ((long)w * 8 + 10) / 20;
    *width = (u8)(e < 1 ? 1 : e > 96 ? 96 : e);
  }
}
static void css_apply(HR *r, HFrame *t, const char *s, size_t n, int block)
{
  size_t i = 0;
  while (i < n) {
    size_t a = i, colon, end, vs, ve;
    while (i < n && s[i] != ':' && s[i] != ';') i++;
    if (i >= n || s[i] == ';') { i++; continue; }
    colon = i;
    end = colon + 1;
    while (end < n && s[end] != ';') end++;
    i = end + 1;
    while (a < colon && (s[a] == ' ' || s[a] == '\n' || s[a] == '\t' || s[a] == '\r')) a++;
    {
      char name[32];
      size_t k = 0;
      int ok;
      CharFmt *cf = &t->cf;
      ParaFmt *pf = &t->pf;
      const char *v;
      size_t vn;
      while (a + k < colon && k < sizeof name - 1 && s[a + k] != ' ') { name[k] = (char)(s[a + k] | 0x20); if (s[a + k] == '-') name[k] = '-'; k++; }
      name[k] = 0;
      vs = colon + 1; ve = end;
      while (vs < ve && (s[vs] == ' ' || s[vs] == '\t' || s[vs] == '\n')) vs++;
      while (ve > vs && (s[ve - 1] == ' ' || s[ve - 1] == '\t' || s[ve - 1] == '\n' || s[ve - 1] == '\r')) ve--;
      v = s + vs; vn = ve - vs;
      if (vn > 10 && !strncmp(v + vn - 10, "!important", 10)) { vn -= 10; while (vn && v[vn - 1] == ' ') vn--; }
      if (!strcmp(name, "font-weight")) {
        int bold = has_word(v, vn, "bold") || (v[0] >= '6' && v[0] <= '9' && vn == 3);
        if (bold) cf->flags |= CF_BOLD; else cf->flags &= (u8)~CF_BOLD;
      } else if (!strcmp(name, "font-style")) {
        if (has_word(v, vn, "italic") || has_word(v, vn, "oblique")) cf->flags |= CF_ITALIC; else cf->flags &= (u8)~CF_ITALIC;
      } else if (!strcmp(name, "text-decoration") || !strcmp(name, "text-decoration-line")) {
        if (has_word(v, vn, "underline")) { cf->flags |= CF_UNDERLINE; cf->ul = UL_SINGLE; }
        if (has_word(v, vn, "line-through")) cf->flags |= CF_STRIKE;
        if (has_word(v, vn, "none")) cf->flags &= (u8)~(CF_UNDERLINE | CF_STRIKE);
      } else if (!strcmp(name, "color")) {
        u32 c = css_color(v, vn);
        if (c != COLOR_AUTO) cf->color = c;
      } else if (!strcmp(name, "background-color") || !strcmp(name, "background")) {
        u32 c = css_color(v, vn);
        if (block && !t->tdepth) { if (c != COLOR_AUTO && c != 0xFFFFFF) pf->shade = HL_ON | c; else if (has_word(v, vn, "transparent")) pf->shade = 0; }
        else if (block) { if (c != COLOR_AUTO) r->pendshade = c; }
        else { if (c != COLOR_AUTO) cf->hl = HL_ON | c; else if (has_word(v, vn, "transparent")) cf->hl = 0; }
      } else if (!strcmp(name, "font-family")) {
        set_family(r, cf, v, vn);
      } else if (!strcmp(name, "font-size")) {
        int kw = font_size_kw(v, vn);
        if (kw > 0) cf->size = (u16)kw;
        else if (kw == 0) {
          i32 tw;
          if (vn && v[vn - 1] == '%') { long pc = atol(v); if (pc > 0) cf->size = (u16)(cf->size * pc / 100); }
          else if (vn > 2 && v[vn - 2] == 'e' && v[vn - 1] == 'm') { tw = css_twips(v, vn, &ok); if (ok) cf->size = (u16)((long)cf->size * tw / 240); }   /* (an em: 240 twips) */
          else { tw = css_twips(v, vn, &ok); if (ok && tw > 0) cf->size = (u16)((tw + 5) / 10); }
        }
        if (cf->size < 2) cf->size = 2;
      } else if (!strcmp(name, "vertical-align") && !block) {
        if (has_word(v, vn, "super")) cf->flags = (u8)((cf->flags & ~CF_SUB) | CF_SUPER);
        else if (has_word(v, vn, "sub")) cf->flags = (u8)((cf->flags & ~CF_SUPER) | CF_SUB);
        else if (has_word(v, vn, "baseline")) cf->flags &= (u8)~(CF_SUPER | CF_SUB);
      } else if (!strcmp(name, "vertical-align") && block && t->kind == HK_CELL) {
        /* (cells: kept in the frame's paragraph format until the cell's paragraphs are marked) */
      } else if (!strcmp(name, "font-variant")) {
        if (has_word(v, vn, "small-caps")) cf->fx |= FX_SMALLCAPS; else cf->fx &= (u8)~FX_SMALLCAPS;
      } else if (!strcmp(name, "text-transform")) {
        if (has_word(v, vn, "uppercase")) cf->flags |= CF_CAPS; else cf->flags &= (u8)~CF_CAPS;
      } else if (!strcmp(name, "letter-spacing")) {
        i32 tw = css_twips(v, vn, &ok);
        cf->spacing = (i16)(ok ? tw : 0);
      } else if (block && !strcmp(name, "text-align")) {
        if (has_word(v, vn, "center")) pf->align = AL_CENTER;
        else if (has_word(v, vn, "right") || has_word(v, vn, "end")) pf->align = AL_RIGHT;
        else if (has_word(v, vn, "justify")) pf->align = AL_JUSTIFY;
        else pf->align = AL_LEFT;
      } else if (block && (!strcmp(name, "margin-left") || !strcmp(name, "margin-right") || !strcmp(name, "margin-top") ||
                           !strcmp(name, "margin-bottom") || !strcmp(name, "text-indent") || !strcmp(name, "padding-left"))) {
        i32 tw = css_twips(v, vn, &ok);
        if (!ok && !has_word(v, vn, "auto")) continue;
        if (has_word(v, vn, "auto")) tw = 0;
        if (!strcmp(name, "margin-left") || !strcmp(name, "padding-left")) pf->left = tw;
        else if (!strcmp(name, "margin-right")) pf->right = tw;
        else if (!strcmp(name, "margin-top")) pf->before = tw;
        else if (!strcmp(name, "margin-bottom")) pf->after = tw;
        else pf->first = tw;
      } else if (block && !strcmp(name, "--first")) {
        i32 tw = css_twips(v, vn, &ok);
        if (ok) pf->first = tw;
      } else if (block && !strcmp(name, "margin")) {
        /* 1 to 4 lengths: top, right, bottom, left */
        i32 m[4];
        int nm = 0;
        size_t q = 0;
        while (q < vn && nm < 4) {
          size_t e2 = q;
          while (e2 < vn && v[e2] != ' ') e2++;
          if (e2 > q) { m[nm] = css_twips(v + q, e2 - q, &ok); if (!ok) m[nm] = 0; nm++; }
          q = e2 + 1;
        }
        if (nm == 1) { m[1] = m[2] = m[3] = m[0]; }
        else if (nm == 2) { m[2] = m[0]; m[3] = m[1]; }
        else if (nm == 3) { m[3] = m[1]; }
        if (nm) { pf->before = m[0]; pf->right = m[1]; pf->after = m[2]; pf->left = m[3]; }
      } else if (block && !strcmp(name, "line-height")) {
        if (vn && v[vn - 1] == '%') { pf->line = (i32)(atol(v) * 240 / 100); pf->linerule = LS_AUTO; }
        else {
          i32 tw = css_twips(v, vn, &ok);
          int unitless = vn && ((v[vn - 1] >= '0' && v[vn - 1] <= '9') || v[vn - 1] == '.');
          if (unitless && ok) { pf->line = (i32)(tw * 240 / 15); pf->linerule = LS_AUTO; }     /* (css_twips took it as px: 15 twips each) */
          else if (ok) { pf->line = tw; pf->linerule = LS_EXACT; }
          else if (has_word(v, vn, "normal")) { pf->line = 240; pf->linerule = LS_AUTO; }
        }
        if (pf->line <= 0) { pf->line = 240; pf->linerule = LS_AUTO; }
      } else if (block && (!strcmp(name, "page-break-before") || !strcmp(name, "break-before"))) {
        pf->pagebreak = (u8)(has_word(v, vn, "always") || has_word(v, vn, "page"));
      } else if (block && (!strcmp(name, "page-break-after") || !strcmp(name, "break-after"))) {
        if (has_word(v, vn, "avoid")) pf->keepnext = 1;
      } else if (block && t->kind == HK_CELL && (!strcmp(name, "padding") || !strcmp(name, "padding-left") || !strcmp(name, "padding-right"))) {
        /* a cell's padding: the table's cell margins (left and right) */
        i32 m[4];
        int nm = 0;
        size_t q = 0;
        while (q < vn && nm < 4) {
          size_t e2 = q;
          while (e2 < vn && v[e2] != ' ') e2++;
          if (e2 > q) { m[nm] = css_twips(v + q, e2 - q, &ok); if (!ok) m[nm] = 0; nm++; }
          q = e2 + 1;
        }
        if (!strcmp(name, "padding-left") && nm) r->padl = m[0];
        else if (!strcmp(name, "padding-right") && nm) r->padr = m[0];
        else if (nm) { r->padr = nm >= 2 ? m[1] : m[0]; r->padl = nm >= 4 ? m[3] : nm >= 2 ? m[1] : m[0]; }
      } else if (block && (!strcmp(name, "border-collapse") || !strcmp(name, "border-spacing"))) {
      } else if (block && !strncmp(name, "border", 6)) {
        static const char *const edge[4] = { "border-top", "border-left", "border-bottom", "border-right" };
        int e;
        if (t->tdepth && (t->kind == HK_CELL)) { if (!has_word(v, vn, "none") && !(vn == 1 && v[0] == '0')) r->pendborders = 1; continue; }
        if (t->kind == HK_TABLE) { if (!has_word(v, vn, "none") && !(vn == 1 && v[0] == '0')) r->tb.borders = 1; continue; }
        for (e = 0; e < 4; e++)
          if (!strcmp(name, "border") || !strcmp(name, edge[e]))
            css_border_in(v, vn, &pf->bstyle[e], &pf->bwidth[e], &pf->bcolor[e]);
      }
    }
  }
}

/* ---- elements */
static const char *const para_tags[] = { "p", "h1", "h2", "h3", "h4", "h5", "h6", "dt", "dd", "caption", "figcaption", "address", "summary", "legend" };
static const char *const block_tags[] = { "div", "blockquote", "center", "section", "article", "header", "footer", "nav", "aside", "main",
  "figure", "dl", "form", "fieldset", "details", "body", "html", "hgroup", "noscript", "tbody", "thead", "tfoot", "colgroup", "menu", "dir" };
static int in_list(const char *t, const char *const *l, int n) { int i; for (i = 0; i < n; i++) if (!strcmp(t, l[i])) return 1; return 0; }
#define NELEM(a) ((int)(sizeof a / sizeof a[0]))

static int void_tag(const char *t)
{
  return !strcmp(t, "br") || !strcmp(t, "img") || !strcmp(t, "hr") || !strcmp(t, "meta") || !strcmp(t, "link") || !strcmp(t, "input") ||
         !strcmp(t, "col") || !strcmp(t, "base") || !strcmp(t, "area") || !strcmp(t, "param") || !strcmp(t, "source") || !strcmp(t, "wbr") || !strcmp(t, "embed");
}

static void h_image(HR *r, const HAttr *a, int na)
{
  const char *v;
  size_t vl;
  char *src;
  u8 *data;
  u32 n = 0;
  i32 cx = 0, cy = 0;
  int k;
  if (!attr(a, na, "src", &v, &vl)) return;
  if (!(src = (char *)malloc(vl + 1))) return;
  {
    /* entities in the address (&amp;) undone; base64 data is ASCII */
    size_t i, o = 0;
    for (i = 0; i < vl; i++) {
      int used;
      u32 cp;
      if (v[i] == '&' && (cp = html_entity(v + i, vl - i, &used)) && cp < 128) { src[o++] = (char)cp; i += (size_t)used - 1; }
      else src[o++] = v[i];
    }
    src[o] = 0;
  }
  data = fmt_fetch(src, &n);
  free(src);
  if (attr(a, na, "width", &v, &vl)) { int ok; cx = css_twips(v, vl, &ok); }
  if (attr(a, na, "height", &v, &vl)) { int ok; cy = css_twips(v, vl, &ok); }
  if (attr(a, na, "style", &v, &vl)) {
    /* width: and height: in the style */
    const char *w = NULL;
    size_t i;
    for (i = 0; i + 6 < vl; i++) {
      if (!strncmp(v + i, "width:", 6) && (i == 0 || v[i - 1] == ' ' || v[i - 1] == ';')) { int ok; size_t e = i + 6; while (e < vl && v[e] != ';') e++; w = v; cx = css_twips(v + i + 6, e - i - 6, &ok); }
      if (!strncmp(v + i, "height:", 7) && (i == 0 || v[i - 1] == ' ' || v[i - 1] == ';')) { int ok; size_t e = i + 7; while (e < vl && v[e] != ';') e++; cy = css_twips(v + i + 7, e - i - 7, &ok); }
    }
    (void)w;
  }
  if (!data) { if (r->lost) *r->lost |= LOST_PICTURES; return; }
  {
    u32 pw, ph;
    if (img_info(data, n, &pw, &ph) != IMG_NONE && pw && ph) {
      /* one side given: the other in proportion */
      if (cx > 0 && cy <= 0) cy = (i32)((long)cx * (long)ph / (long)pw);
      else if (cy > 0 && cx <= 0) cx = (i32)((long)cy * (long)pw / (long)ph);
    }
  }
  k = doc_add_image(r->d, data, n, cx, cy);
  if (k < 0) { if (r->lost) *r->lost |= LOST_PICTURES; return; }
  if (attr(a, na, "alt", &v, &vl)) attr_text(r, v, vl, r->d->images[k].alt, sizeof r->d->images[k].alt);
  {
    char ref[8];
    pic_utf8(ref, k);
    if (r->brs) { Para *p = h_para(r); while (p && r->brs > 0) { para_append(p, "\v", 1, &ftop(r)->cf); r->brs--; } }
    if (r->space && r->curtext) { Para *p = h_para(r); if (p) para_append(p, " ", 1, &r->spacecf); }
    r->space = 0;
    { Para *p = h_para(r); if (p) para_append(p, ref, 4, &ftop(r)->cf); }
    r->curtext = 1;
  }
}

/* a list's Quill list: one per outermost <ul>/<ol>, its levels set as the nested lists come */
static int h_list(HR *r, int ordered, const HAttr *a, int na, int *level)
{
  int i, lvl = 0, list = 0;
  for (i = r->depth - 1; i > 0; i--) if (r->f[i].kind == HK_LIST) { list = r->f[i].list; lvl = r->f[i].level + 1; break; }
  if (lvl > 8) lvl = 8;
  if (!list) list = doc_list_preset(r->d, ordered ? LP_NUMBERS : LP_BULLETS);
  if (list) {
    ListLevel *lv = &r->d->lists[list - 1].lvl[lvl];
    const char *v;
    size_t vl;
    if (ordered) {
      lv->fmt = NF_DECIMAL;
      if (attr(a, na, "type", &v, &vl) && vl) {
        if (v[0] == 'a') lv->fmt = NF_LOWER_LETTER;
        else if (v[0] == 'A') lv->fmt = NF_UPPER_LETTER;
        else if (v[0] == 'i') lv->fmt = NF_LOWER_ROMAN;
        else if (v[0] == 'I') lv->fmt = NF_UPPER_ROMAN;
      }
      if (attr(a, na, "style", &v, &vl)) {
        if (has_word(v, vl, "lower-alpha") || has_word(v, vl, "lower-latin")) lv->fmt = NF_LOWER_LETTER;
        else if (has_word(v, vl, "upper-alpha") || has_word(v, vl, "upper-latin")) lv->fmt = NF_UPPER_LETTER;
        else if (has_word(v, vl, "lower-roman")) lv->fmt = NF_LOWER_ROMAN;
        else if (has_word(v, vl, "upper-roman")) lv->fmt = NF_UPPER_ROMAN;
      }
      sprintf(lv->text, "%%%d.", lvl + 1);
      lv->start = (u16)attr_int(a, na, "start", 1);
    } else {
      lv->fmt = NF_BULLET;
      strcpy(lv->text, "\xE2\x80\xA2");
      if (attr(a, na, "style", &v, &vl) || attr(a, na, "type", &v, &vl)) {
        if (has_word(v, vl, "circle")) strcpy(lv->text, "o");
        else if (has_word(v, vl, "square")) strcpy(lv->text, "\xE2\x96\xAA");
      }
    }
  }
  *level = lvl;
  return list;
}

#define NOVAL ((i32)0x7FFFFF00L)
static void h_open(HR *r, const char *tag, const HAttr *a, int na, int selfclose)
{
  HFrame *p = ftop(r), *t;
  const char *v;
  size_t vl;
  Doc *d = r->d;
  int kind = HK_INLINE, item = 0;
  i32 item_left = 0, item_first = 0, parent_left = 0;
  (void)selfclose;
  if (!strcmp(tag, "head") || !strcmp(tag, "script") || !strcmp(tag, "style") || !strcmp(tag, "title") || !strcmp(tag, "noembed") || !strcmp(tag, "template") || !strcmp(tag, "svg") || !strcmp(tag, "math") || !strcmp(tag, "object") || !strcmp(tag, "iframe") || !strcmp(tag, "select") || !strcmp(tag, "textarea")) kind = HK_SKIP;
  else if (in_list(tag, para_tags, NELEM(para_tags))) kind = HK_PARA;
  else if (!strcmp(tag, "li")) kind = HK_ITEM;
  else if (!strcmp(tag, "ul") || !strcmp(tag, "ol")) kind = HK_LIST;
  else if (!strcmp(tag, "table")) kind = HK_TABLE;
  else if (!strcmp(tag, "tr")) kind = HK_ROW;
  else if (!strcmp(tag, "td") || !strcmp(tag, "th")) kind = HK_CELL;
  else if (!strcmp(tag, "pre") || !strcmp(tag, "listing") || !strcmp(tag, "xmp")) kind = HK_PRE;
  else if (in_list(tag, block_tags, NELEM(block_tags))) kind = HK_BLOCK;
  /* void elements */
  if (!strcmp(tag, "meta")) {
    char name[32], content[160];
    if (attr(a, na, "name", &v, &vl)) {
      attr_text(r, v, vl, name, sizeof name);
      if (str_ieq(name, "author") && attr(a, na, "content", &v, &vl)) { attr_text(r, v, vl, content, sizeof content); strncpy(d->author, content, 127); d->author[127] = 0; }
    }
    if (attr(a, na, "charset", &v, &vl) && vl >= 4 && (strncmp(v, "utf", 3) && strncmp(v, "UTF", 3))) r->utf8 = 0;
    if (attr(a, na, "content", &v, &vl) && has_word(v, vl, "charset=") && !has_word(v, vl, "utf-8") && !has_word(v, vl, "UTF-8") && !has_word(v, vl, "utf8")) r->utf8 = 0;
    return;
  }
  if (r->skip && kind != HK_SKIP) return;
  if (!strcmp(tag, "br")) { if (!r->skip) { h_para(r); r->brs++; r->space = 0; } return; }
  if (!strcmp(tag, "img")) { h_image(r, a, na); return; }
  if (!strcmp(tag, "hr")) {
    ParaFmt pf;
    if (attr(a, na, "class", &v, &vl) && has_word(v, vl, "notes")) return;      /* (the line Quill draws over its notes) */
    if (r->cur) h_end(r, 0);
    pf = ftop(r)->pf;
    {
      Para *q;
      pf.bstyle[PE_BOTTOM] = PB_SINGLE; pf.bwidth[PE_BOTTOM] = 6; pf.bcolor[PE_BOTTOM] = 0x808080;
      pf.before = pf.after = 120;
      q = para_new(&pf, &ftop(r)->cf);
      if (q && r->tdepth && r->tbon) { ParaFmt x = q->pf; tb_mark(&r->tb, &x); q->pf = x; }
      pl_add(&r->l, q);
    }
    return;
  }
  if (!strcmp(tag, "col") && r->tdepth == 1 && r->ncolw < MAX_COLS) {
    int ok = 0, span = attr_int(a, na, "span", 1);
    i32 w = 0;
    if (attr(a, na, "width", &v, &vl)) w = css_twips(v, vl, &ok);
    if (attr(a, na, "style", &v, &vl)) {
      size_t i;
      for (i = 0; i + 6 <= vl; i++) if (!strncmp(v + i, "width:", 6)) { size_t e = i + 6; while (e < vl && v[e] != ';') e++; w = css_twips(v + i + 6, e - i - 6, &ok); break; }
    }
    while (span-- > 0 && r->ncolw < MAX_COLS) r->colw[r->ncolw++] = ok ? w : 0;
    return;
  }
  if (void_tag(tag)) return;
  if (r->depth + 1 >= MAXH) return;
  /* blocks end the paragraph being filled */
  if (kind != HK_INLINE && kind != HK_SKIP && r->cur && (r->curtext || kind != HK_CELL)) h_end(r, 0);
  else if (kind != HK_INLINE && kind != HK_SKIP && r->cur && !r->curtext) { para_free(r->cur); r->cur = NULL; }
  r->f[r->depth + 1] = *p;
  r->depth++;
  t = ftop(r);
  strncpy(t->tag, tag, sizeof t->tag - 1); t->tag[sizeof t->tag - 1] = 0;
  t->kind = (u8)kind;
  t->nparas = r->made;
  t->tdepth = (u8)r->tdepth;
  /* notes as Quill writes them: a reference <a href="#fn1" id="fnref1">, the notes an <ol class="footnotes"> of
     <li id="fn1"> with a link back */
  if (!strcmp(tag, "a") && attr(a, na, "href", &v, &vl) && vl > 3 && v[0] == '#' && (v[1] == 'f' || v[1] == 'e') && v[2] == 'n') {
    const char *idv;
    size_t il;
    int en = v[1] == 'e', num = atoi(v + 3);
    if (v[3] >= '0' && v[3] <= '9' && num > 0 && num < 256 && attr(a, na, "id", &idv, &il) && il > 5 && !strncmp(idv + 2, "ref", 3)) {
      int k = r->notemap[en][num] - 1;
      if (k < 0) { k = en ? doc_add_endnote(d) : doc_add_note(d); r->notemap[en][num] = k + 1; }
      if (k >= 0) {
        char ref[8];
        CharFmt cf = p->cf;
        cf.flags &= (u8)~(CF_SUPER | CF_SUB);
        if (en) enote_utf8(ref, k); else note_utf8(ref, k);
        h_text(r, ref, 4, &cf);
      }
      kind = HK_SKIP; t->kind = HK_SKIP;
    } else if (v[3] == 'r' && t->tdepth == 0 && p->note) { kind = HK_SKIP; t->kind = HK_SKIP; }     /* the link back */
  }
  /* LibreOffice's: <a href="#sdfootnote1sym"> in the text, <div id="sdfootnote1"> after it with a link back */
  if (!strcmp(tag, "a") && attr(a, na, "href", &v, &vl) && vl > 12 && (!strncmp(v, "#sdfootnote", 11) || !strncmp(v, "#sdendnote", 10))) {
    int en = v[3] == 'e';
    const char *q = v + (en ? 10 : 11);
    int num = atoi(q);
    size_t ql = vl - (size_t)(q - v);
    while (ql && *q >= '0' && *q <= '9') { q++; ql--; }
    if (num > 0 && num < 256 && ql == 3 && !strncmp(q, "sym", 3)) {
      int k = r->notemap[en][num] - 1;
      if (k < 0) { k = en ? doc_add_endnote(d) : doc_add_note(d); r->notemap[en][num] = k + 1; }
      if (k >= 0) {
        char ref[8];
        CharFmt cf = p->cf;
        cf.flags &= (u8)~(CF_SUPER | CF_SUB);
        if (en) enote_utf8(ref, k); else note_utf8(ref, k);
        h_text(r, ref, 4, &cf);
      }
      kind = HK_SKIP; t->kind = HK_SKIP;
    } else if (ql == 3 && !strncmp(q, "anc", 3)) { kind = HK_SKIP; t->kind = HK_SKIP; }
  }
  if (!strcmp(tag, "div") && attr(a, na, "id", &v, &vl) && vl > 10 && (!strncmp(v, "sdfootnote", 10) || !strncmp(v, "sdendnote", 9))) {
    int en = v[2] == 'e', num = atoi(v + (en ? 9 : 10));
    if (num > 0 && num < 256) {
      int k = r->notemap[en][num] - 1;
      if (k < 0) { k = en ? doc_add_endnote(d) : doc_add_note(d); r->notemap[en][num] = k + 1; }
      if (k >= 0) { t->note = k + 1; t->enote = (u8)en; t->pf = d->styles[0].pf; t->cf = d->styles[0].cf; }
    }
  }
  if (!strcmp(tag, "ol") && attr(a, na, "class", &v, &vl) && (has_word(v, vl, "footnotes") || has_word(v, vl, "endnotes"))) {
    kind = HK_BLOCK; t->kind = HK_BLOCK; t->notes = (u8)(has_word(v, vl, "endnotes") ? 2 : 1);
  }
  if (!strcmp(tag, "li") && p->notes && attr(a, na, "id", &v, &vl) && vl > 2) {
    int en = p->notes == 2, num = atoi(v + 2);
    if (num > 0 && num < 256) {
      int k = r->notemap[en][num] - 1;
      if (k < 0) { k = en ? doc_add_endnote(d) : doc_add_note(d); r->notemap[en][num] = k + 1; }
      if (k >= 0) { t->note = k + 1; t->enote = (u8)en; t->kind = HK_PARA; kind = HK_PARA; t->pf = d->styles[0].pf; t->cf = d->styles[0].cf; }
    }
  }
  if (kind == HK_SKIP) { r->skip++; if (!strcmp(tag, "title")) { r->intitle = 1; r->title.len = 0; } return; }
  if (kind == HK_TABLE) {
    r->tdepth++;
    t->tdepth = (u8)r->tdepth;
    if (r->tdepth == 1) {
      int border = attr_int(a, na, "border", 0) > 0;
      tb_begin(&r->tb, r->l.np, border);
      r->tbon = 0;
      r->ncolw = 0; r->padl = r->padr = -1; r->tindent = 0; r->tjc = AL_LEFT;
      if (attr(a, na, "align", &v, &vl) && has_word(v, vl, "center")) r->tjc = AL_CENTER;
      if (attr(a, na, "style", &v, &vl)) {
        if (has_word(v, vl, "margin-left:auto") || has_word(v, vl, "margin-left: auto")) r->tjc = AL_CENTER;
        else { size_t i; for (i = 0; i + 12 <= vl; i++) if (!strncmp(v + i, "margin-left:", 12)) { int ok; size_t e = i + 12; while (e < vl && v[e] != ';') e++; r->tindent = css_twips(v + i + 12, e - i - 12, &ok); break; } }
      }
    } else if (r->lost) *r->lost |= LOST_TABLES;
  }
  if (kind == HK_ROW && r->tdepth == 1) {
    if (!r->tbon) { r->tb.first = r->l.np; r->tbon = 1; }
    tb_row(&r->tb);
  }
  if (kind == HK_CELL && r->tdepth == 1) {
    if (!r->tbon) { r->tb.first = r->l.np; r->tbon = 1; tb_row(&r->tb); }
    tb_cell(&r->tb, attr_int(a, na, "colspan", 1), attr_int(a, na, "rowspan", 1), !strcmp(tag, "th"));
    r->pendshade = COLOR_AUTO; r->pendborders = (u8)(r->tb.borders != 0);
    if (attr(a, na, "bgcolor", &v, &vl)) r->pendshade = css_color(v, vl);
    t->pf = d->styles[0].pf; t->cf = d->styles[0].cf;
    t->pf.left = t->pf.first = 0;
    if (!strcmp(tag, "th")) t->cf.flags |= CF_BOLD;
  }
  /* paragraph styles */
  if (tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6' && !tag[2]) {
    int k = fmt_heading_style(d, tag[1] - '0');
    if (k >= 0) { u16 list = t->pf.list; u8 lvl = t->pf.level; t->pf = d->styles[k].pf; t->pf.list = list; t->pf.level = lvl; t->cf = d->styles[k].cf; }
  } else if (kind == HK_PARA && !strcmp(tag, "p") && !r->tdepth && !r->listdepth && p->kind != HK_BLOCK) {
    /* a plain paragraph: Normal */
  }
  if (!strcmp(tag, "blockquote")) {
    int k = fmt_quote_style(d);
    if (k >= 0) { i32 left = t->pf.left; t->pf = d->styles[k].pf; t->pf.left += left; t->cf = d->styles[k].cf; }
  }
  if (kind == HK_PRE) {
    int k = r->code >= 0 ? r->code : (r->code = fmt_code_style(d));
    t->pf = d->styles[k].pf; t->cf = d->styles[k].cf;
    t->pre = 1;
    /* the newline right after <pre> doesn't count */
    if (r->i < r->n && r->s[r->i] == '\n') r->i++;
    else if (r->i + 1 < r->n && r->s[r->i] == '\r' && r->s[r->i + 1] == '\n') r->i += 2;
  }
  if (!strcmp(tag, "center")) t->pf.align = AL_CENTER;
  if (!strcmp(tag, "dd")) t->pf.left += 720;
  if (kind == HK_LIST) {
    int lvl;
    t->list = h_list(r, !strcmp(tag, "ol"), a, na, &lvl);
    t->level = lvl;
    r->listdepth++;
  }
  if (kind == HK_ITEM) {
    int i;
    for (i = r->depth - 1; i > 0 && r->f[i].kind != HK_LIST; i--) ;
    if (i > 0 && r->f[i].list) {
      const ListLevel *lv = &d->lists[r->f[i].list - 1].lvl[r->f[i].level];
      t->pf = d->styles[0].pf;
      {
        int k = doc_style(d, "ListParagraph");
        if (k >= 0) t->pf = d->styles[k].pf;
      }
      t->pf.list = (u16)r->f[i].list; t->pf.level = (u8)r->f[i].level;
      item_left = lv->left; item_first = -lv->hanging;
      t->pf.left = t->pf.first = NOVAL;        /* (CSS may give them: margin-left from the item it's in) */
      for (i = r->depth - 1; i > 0 && r->f[i].kind != HK_ITEM; i--) ;
      parent_left = i > 0 && r->f[i].pf.left != NOVAL ? r->f[i].pf.left : 0;
      if (attr(a, na, "style", &v, &vl) && has_word(v, vl, "list-style-type:none")) t->pf.list = 0;
      item = 1;
    }
  }
  /* class= a style of that id */
  if (attr(a, na, "class", &v, &vl) && (kind == HK_PARA || kind == HK_ITEM || kind == HK_BLOCK)) {
    char cls[40];
    int k;
    size_t q;
    for (q = 0; q < vl && q < sizeof cls - 1 && v[q] != ' '; q++) cls[q] = v[q];
    cls[q] = 0;
    k = doc_style(d, cls);
    if (k >= 0 && d->styles[k].type == ST_PARA) {
      u16 list = t->pf.list; u8 lvl = t->pf.level; i32 left = t->pf.left, first = t->pf.first;
      t->pf = d->styles[k].pf; t->cf = d->styles[k].cf;
      if (kind == HK_ITEM) { t->pf.list = list; t->pf.level = lvl; t->pf.left = left; t->pf.first = first; }
    }
  }
  /* inline formatting */
  if (!strcmp(tag, "b") || !strcmp(tag, "strong")) t->cf.flags |= CF_BOLD;
  else if (!strcmp(tag, "i") || !strcmp(tag, "em") || !strcmp(tag, "cite") || !strcmp(tag, "var") || !strcmp(tag, "dfn") || !strcmp(tag, "address")) t->cf.flags |= CF_ITALIC;
  else if (!strcmp(tag, "u") || !strcmp(tag, "ins")) { t->cf.flags |= CF_UNDERLINE; t->cf.ul = UL_SINGLE; }
  else if (!strcmp(tag, "s") || !strcmp(tag, "strike") || !strcmp(tag, "del")) t->cf.flags |= CF_STRIKE;
  else if (!strcmp(tag, "sup")) t->cf.flags = (u8)((t->cf.flags & ~CF_SUB) | CF_SUPER);
  else if (!strcmp(tag, "sub")) t->cf.flags = (u8)((t->cf.flags & ~CF_SUPER) | CF_SUB);
  else if (!strcmp(tag, "code") || !strcmp(tag, "tt") || !strcmp(tag, "kbd") || !strcmp(tag, "samp")) { if (!t->pre) t->cf.font = (u8)doc_font(d, "Courier New"); }
  else if (!strcmp(tag, "small")) t->cf.size = (u16)(t->cf.size * 5 / 6);
  else if (!strcmp(tag, "big")) t->cf.size = (u16)(t->cf.size * 6 / 5);
  else if (!strcmp(tag, "mark")) t->cf.hl = HL_ON | 0xFFFF00;
  else if (!strcmp(tag, "dt") ) t->cf.flags |= CF_BOLD;
  else if (!strcmp(tag, "font")) {
    char face[48];
    if (attr(a, na, "face", &v, &vl)) { attr_text(r, v, vl, face, sizeof face); set_family(r, &t->cf, face, strlen(face)); }
    if (attr(a, na, "color", &v, &vl)) { u32 c = css_color(v, vl); if (c != COLOR_AUTO) t->cf.color = c; }
    if (attr(a, na, "size", &v, &vl) && vl) {
      static const u16 sz[8] = { 0, 16, 20, 24, 28, 36, 48, 72 };
      int n = atoi(v);
      if (v[0] == '+' || v[0] == '-') n += 3;
      if (n < 1) n = 1;
      if (n > 7) n = 7;
      t->cf.size = sz[n];
    }
  } else if (!strcmp(tag, "a")) {
    if (attr(a, na, "href", &v, &vl) && vl) {
      char url[512];
      attr_text(r, v, vl, url, sizeof url);
      fmt_link_cf(d, &t->cf, url);
    }
  }
  if ((attr(a, na, "id", &v, &vl) || (!strcmp(tag, "a") && attr(a, na, "name", &v, &vl))) && vl && !r->skip) {
    char name[80];
    attr_text(r, v, vl, name, sizeof name);
    if (strncmp(name, "fnref", 5) && strncmp(name, "enref", 5) && !(name[0] == 'f' && name[1] == 'n') && !(name[0] == 'e' && name[1] == 'n' && name[2] >= '0' && name[2] <= '9'))
      t->cf.bmark = (u16)doc_bmark(d, name);
  }
  if (attr(a, na, "align", &v, &vl) && (kind == HK_PARA || kind == HK_BLOCK || kind == HK_CELL)) {
    if (has_word(v, vl, "center")) t->pf.align = AL_CENTER;
    else if (has_word(v, vl, "right")) t->pf.align = AL_RIGHT;
    else if (has_word(v, vl, "justify")) t->pf.align = AL_JUSTIFY;
    else if (has_word(v, vl, "left")) t->pf.align = AL_LEFT;
  }
  if (attr(a, na, "style", &v, &vl)) css_apply(r, t, v, vl, kind != HK_INLINE);
  if (item) {
    t->pf.left = t->pf.left == NOVAL ? item_left : parent_left + t->pf.left;
    if (t->pf.first == NOVAL) t->pf.first = item_first;
  }
}
/* the paragraph rule at a block's end: a <p> or a cell that made no paragraph makes an empty one */
static void h_close_frame(HR *r)
{
  HFrame *t = ftop(r);
  int kind = t->kind;
  if (kind == HK_SKIP) {
    r->skip--;
    if (!strcmp(t->tag, "title")) {
      r->intitle = 0;
      if (r->title.data) { char *s = r->title.data; while (*s == ' ' || *s == '\n') s++; strncpy(r->d->title, s, 127); r->d->title[127] = 0; }
    }
  } else if (kind == HK_PARA || kind == HK_ITEM || kind == HK_CELL || kind == HK_PRE) {
    int none = r->made == t->nparas;
    if (kind == HK_PRE && r->cur && !r->curtext) { para_free(r->cur); r->cur = NULL; }
    else if (r->cur || (none && kind != HK_PRE)) {
      /* (the paragraph keeps the block's formatting, even if inline elements are still open) */
      if (!r->cur && none) r->cur = para_new(&t->pf, &t->cf);
      if (r->cur) r->cur->pf = t->pf;
      h_end(r, 1);
    }
  } else if (kind == HK_BLOCK || kind == HK_LIST || kind == HK_ROW || kind == HK_TABLE) {
    if (r->cur && r->curtext) { r->cur->pf = t->pf; h_end(r, 0); }
    else if (r->cur) { para_free(r->cur); r->cur = NULL; }
  }
  if (kind == HK_LIST) r->listdepth--;
  if (kind == HK_TABLE) {
    if (r->tdepth == 1) {
      if (r->tbon && tb_end(&r->tb, r->d, &r->l, fmt_text_width(r->d)) && r->d->ntables) {
        /* the widths, margins and place it said */
        Table *tt = &r->d->tables[r->d->ntables - 1];
        int k, all = r->ncolw == tt->ncols;
        for (k = 0; k < r->ncolw && all; k++) if (r->colw[k] <= 0) all = 0;
        if (all) for (k = 0; k < tt->ncols; k++) tt->grid[k] = r->colw[k];
        if (r->padl >= 0) tt->marl = r->padl;
        if (r->padr >= 0) tt->marr = r->padr;
        tt->indent = r->tindent; tt->jc = r->tjc;
      }
      else { free(r->tb.busy); r->tb.busy = NULL; }
      r->tbon = 0;
    }
    r->tdepth--;
  }
  r->depth--;
}
static void h_close(HR *r, const char *tag)
{
  int i;
  if (void_tag(tag)) return;
  for (i = r->depth; i > 0; i--) if (!strcmp(r->f[i].tag, tag)) break;
  if (i <= 0) {
    if (!strcmp(tag, "p") && !r->skip) h_end(r, 1);     /* a stray </p>: an empty paragraph, as browsers make */
    return;
  }
  /* (a table cell's or list's end doesn't close past its table or list) */
  {
    int j;
    for (j = r->depth; j > i; j--) {
      int k = r->f[j].kind;
      if ((k == HK_TABLE && strcmp(tag, "table")) || (k == HK_LIST && strcmp(tag, "ul") && strcmp(tag, "ol") && strcmp(tag, "li"))) return;
    }
  }
  while (r->depth >= i) h_close_frame(r);
}

/* a <style> sheet: rules for ".Class" (a style of that id: made if new, named by a comment before
   the rule as Quill writes them), "tag.class" and "p", "h1"... (Normal, the headings) change those
   styles */
static void h_stylesheet(HR *r, const char *s, size_t n)
{
  Doc *d = r->d;
  size_t i = 0;
  char name[48];
  int changed = 0;
  name[0] = 0;
  while (i < n) {
    size_t sel, selend, body, bodyend;
    while (i < n && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++;
    if (i + 1 < n && s[i] == '/' && s[i + 1] == '*') {
      size_t e = i + 2, k = 0;
      while (e + 1 < n && !(s[e] == '*' && s[e + 1] == '/')) e++;
      {
        size_t a = i + 2, b = e;
        while (a < b && s[a] == ' ') a++;
        while (b > a && s[b - 1] == ' ') b--;
        while (a < b && k < sizeof name - 1) name[k++] = s[a++];
        name[k] = 0;
      }
      i = e + 2;
      continue;
    }
    if (i + 3 < n && !strncmp(s + i, "<!--", 4)) { i += 4; continue; }
    if (i + 2 < n && !strncmp(s + i, "-->", 3)) { i += 3; continue; }
    if (i < n && s[i] == '@') {
      /* @media and the like: their rules are skipped */
      int depth = 0;
      while (i < n && s[i] != '{' && s[i] != ';') i++;
      if (i < n && s[i] == ';') { i++; continue; }
      do { if (s[i] == '{') depth++; else if (s[i] == '}') depth--; i++; } while (i < n && depth > 0);
      continue;
    }
    sel = i;
    while (i < n && s[i] != '{') i++;
    selend = i;
    if (i >= n) break;
    body = ++i;
    while (i < n && s[i] != '}') i++;
    bodyend = i;
    if (i < n) i++;
    {
      /* each selector of the rule */
      size_t a = sel;
      while (a < selend) {
        size_t b = a, k = 0;
        char one[48], *dot;
        int st = -1;
        while (b < selend && s[b] != ',') b++;
        while (a < b && (s[a] == ' ' || s[a] == '\n' || s[a] == '\r' || s[a] == '\t')) a++;
        while (a + k < b && k < sizeof one - 1 && s[a + k] != ' ' && s[a + k] != '\n' && s[a + k] != ':') { one[k] = s[a + k]; k++; }
        one[k] = 0;
        dot = strchr(one, '.');
        if (dot && dot[1]) {
          st = doc_style(d, dot + 1);
          if (st < 0 && dot == one && name[0]) {
            /* a class Quill wrote for a style the document doesn't have yet */
            Style ns;
            memset(&ns, 0, sizeof ns);
            strncpy(ns.id, dot + 1, sizeof ns.id - 1);
            strncpy(ns.name, name, sizeof ns.name - 1);
            ns.type = ST_PARA; ns.based = 0; ns.next = (u8)d->nstyles;
            ns.pf = d->styles[0].pf; ns.cf = d->styles[0].cf;
            ns.pf.style = (u8)d->nstyles;
            st = doc_add_style(d, &ns);
            if (st >= 0) { d->styles[st].pf = ns.pf; d->styles[st].cf = ns.cf; }
          }
          if (st >= 0 && d->styles[st].type != ST_PARA) st = -1;
          if (st >= 0 && name[0] && str_ieq(d->styles[st].name, name)) strcpy(d->styles[st].name, name);
          if (dot != one && st < 0) *dot = 0;
        }
        if (st < 0 && !(dot && dot == one)) {
          if (!strcmp(one, "p")) st = 0;
          else if (one[0] == 'h' && one[1] >= '1' && one[1] <= '6' && !one[2]) st = fmt_heading_style(d, one[1] - '0');
        }
        if (st >= 0) {
          HFrame f;
          memset(&f, 0, sizeof f);
          f.kind = HK_PARA;
          f.pf = d->styles[st].pf; f.cf = d->styles[st].cf;
          css_apply(r, &f, s + body, bodyend - body, 1);
          f.pf.style = (u8)st;
          d->styles[st].pf = f.pf; d->styles[st].cf = f.cf;
          changed = 1;
        }
        a = b + 1;
      }
    }
    name[0] = 0;
  }
  if (changed) {
    int k;
    doc_styles_from_effective(d);
    for (k = 0; k <= r->depth; k++) if (r->f[k].kind == HK_BLOCK && r->f[k].pf.style == 0) { r->f[k].pf = d->styles[0].pf; r->f[k].cf = d->styles[0].cf; }
  }
}

/* paragraphs inside the frame being filled get its paragraph format when they start: the open
   paragraph is given a block's format when the block ends (h_close_frame); text outside any block
   gets the body's */
static void parse_html(HR *r)
{
  const char *s = r->s;
  size_t n = r->n;
  while (r->i < n) {
    char c = s[r->i];
    if (c == '<') {
      size_t i = r->i + 1;
      if (i < n && s[i] == '!') {
        /* comments, doctype, CDATA */
        if (i + 2 < n && s[i + 1] == '-' && s[i + 2] == '-') {
          const char *e = NULL;
          size_t k;
          for (k = i + 3; k + 2 < n; k++) if (s[k] == '-' && s[k + 1] == '-' && s[k + 2] == '>') { e = s + k; break; }
          r->i = e ? (size_t)(e - s) + 3 : n;
        } else {
          while (i < n && s[i] != '>') i++;
          r->i = i + 1;
        }
        continue;
      }
      if (i < n && s[i] == '?') { while (i < n && s[i] != '>') i++; r->i = i + 1; continue; }
      {
        int end = 0, selfclose = 0, na = 0;
        char tag[16];
        size_t k = 0;
        HAttr a[MAXATTR];
        if (i < n && s[i] == '/') { end = 1; i++; }
        if (i >= n || !((s[i] | 0x20) >= 'a' && (s[i] | 0x20) <= 'z')) {
          /* not a tag: a literal '<' */
          h_cp(r, '<', 0);
          r->i++;
          continue;
        }
        while (i < n && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9') || s[i] == ':' || s[i] == '-')) {
          if (k < sizeof tag - 1) tag[k++] = (char)((s[i] >= 'A' && s[i] <= 'Z') ? s[i] + 32 : s[i]);
          i++;
        }
        tag[k] = 0;
        /* attributes */
        while (i < n && s[i] != '>') {
          size_t ns, ne;
          if (s[i] == '/' ) { selfclose = 1; i++; continue; }
          if (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') { i++; continue; }
          ns = i;
          while (i < n && s[i] != '=' && s[i] != '>' && s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r' && s[i] != '/') i++;
          ne = i;
          while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
          if (i < n && s[i] == '=') {
            size_t vs, ve;
            i++;
            while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
            if (i < n && (s[i] == '"' || s[i] == '\'')) {
              char q = s[i++];
              vs = i;
              while (i < n && s[i] != q) i++;
              ve = i;
              if (i < n) i++;
            } else {
              vs = i;
              while (i < n && s[i] != ' ' && s[i] != '>' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') i++;
              ve = i;
            }
            if (na < MAXATTR && ne > ns) { a[na].name = s + ns; a[na].nl = ne - ns; a[na].val = s + vs; a[na].vl = ve - vs; na++; }
          } else if (na < MAXATTR && ne > ns) { a[na].name = s + ns; a[na].nl = ne - ns; a[na].val = s + ns; a[na].vl = 0; na++; }
          if (ne == ns && i < n && s[i] != '>') i++;
        }
        {
          /* attribute names in lower case (they point into the file: copied when not) */
          int q;
          static char lower[MAXATTR][24];
          for (q = 0; q < na; q++) {
            size_t z;
            int up = 0;
            for (z = 0; z < a[q].nl; z++) if (a[q].name[z] >= 'A' && a[q].name[z] <= 'Z') up = 1;
            if (up && a[q].nl < 24) {
              for (z = 0; z < a[q].nl; z++) lower[q][z] = (char)((a[q].name[z] >= 'A' && a[q].name[z] <= 'Z') ? a[q].name[z] + 32 : a[q].name[z]);
              a[q].name = lower[q];
            }
          }
        }
        r->i = i < n ? i + 1 : n;
        if (end) h_close(r, tag);
        else {
          h_open(r, tag, a, na, selfclose);
          if (selfclose && !void_tag(tag) && r->depth > 0 && !strcmp(ftop(r)->tag, tag)) h_close(r, tag);
          /* raw text elements: their content up to the closing tag isn't markup */
          if (!strcmp(tag, "script") || !strcmp(tag, "style")) {
            size_t q;
            size_t tl = strlen(tag);
            for (q = r->i; q + tl + 2 < n; q++) if (s[q] == '<' && s[q + 1] == '/' && !strncmp(s + q + 2, tag, tl)) break;
            if (tag[1] == 't') h_stylesheet(r, s + r->i, q - r->i);
            r->i = q;
          }
        }
      }
      continue;
    }
    if (c == '&') {
      int used;
      u32 cp = html_entity(s + r->i, n - r->i, &used);
      if (cp) { r->i += (size_t)used; h_cp(r, cp, 0); r->prevc = cp; continue; }
      h_cp(r, '&', 1);
      r->i++;
      continue;
    }
    {
      int used = 1;
      u32 cp = r->utf8 ? utf8_get(s + r->i, n - r->i, &used) : cp_to_unicode(1252, (u8)c);
      r->i += (size_t)used;
      if (cp == 0xFEFF) continue;
      h_cp(r, cp, 1);
      if (cp != ' ' && cp != '\t') r->prevc = cp;
    }
  }
  while (r->depth > 0) h_close_frame(r);
  if (r->cur && r->curtext) h_end(r, 0);
  else if (r->cur) { para_free(r->cur); r->cur = NULL; }
}

static int valid_utf8(const u8 *s, size_t n)
{
  size_t i = 0;
  while (i < n) {
    u8 c = s[i];
    int k;
    if (c < 0x80) { i++; continue; }
    if (c >= 0xC2 && c <= 0xDF) k = 1; else if (c >= 0xE0 && c <= 0xEF) k = 2; else if (c >= 0xF0 && c <= 0xF4) k = 3; else return 0;
    while (k--) { i++; if (i >= n || (s[i] & 0xC0) != 0x80) return 0; }
    i++;
  }
  return 1;
}

static Doc *html_load(const u8 *data, size_t len, unsigned *lost, char *err, size_t errcap)
{
  HR *r = (HR *)calloc(1, sizeof(HR));
  Doc *d;
  unsigned dummy = 0;
  if (!lost) lost = &dummy;
  if (!r || !(d = doc_new())) { free(r); snprintf(err, errcap, "Not enough memory to open the file."); return NULL; }
  r->d = d; r->s = (const char *)data; r->n = len; r->lost = lost;
  if (len >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) r->i = 3;
  r->utf8 = valid_utf8(data, len);
  r->code = -1;
  buf_init(&r->title);
  r->f[0].kind = HK_BLOCK;
  r->f[0].pf = d->styles[0].pf; r->f[0].cf = d->styles[0].cf;
  strcpy(r->f[0].tag, "#doc");
  parse_html(r);
  buf_free(&r->title);
  free(r->tb.busy);
  if (r->l.oom || !pl_to_body(d, &r->l)) { pl_free(&r->l); free(r); doc_free(d); snprintf(err, errcap, "Not enough memory to open the file."); return NULL; }
  free(r);
  doc_clear_undo(d);
  d->dirty = 0;
  return d;
}
/* (no sniff: an HTML file is known by its name; a page called .txt reads as text) */
const DocFormat fmt_html = { "Web Page", "html|htm", NULL, html_load, html_save, 0 };
