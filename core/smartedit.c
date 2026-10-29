/*
 * smartedit.c - see chartedit.h: SmartArt made and changed (WP15c). A graphic is an outline of items
 * (text and level) with a layout, colours, a style and a direction. Quill lays it out itself and
 * writes the five diagram parts (ECMA-376 Part 1 21.4): the data model (a document point, a point a
 * item, connections giving each its parent), a layout definition, quick style and colour definition
 * of its own (named by the identifiers Office uses for the matching built-in ones) and the drawing
 * (dsp:drawing) with every shape placed, which Word, LibreOffice and Quill itself show.
 *
 * Ported in part from WordCraft (crates/docx/src/smart_art.rs and crates/doc/src/smart_art.rs,
 * MIT licence, (c) 2026 ArtCraft Team and the WordCraft contributors): the data model, layout,
 * colour and quick style parts' shape, the drawing's shape markup, and the block list, process,
 * cycle, hierarchy, radial, Venn, matrix and pyramid layouts. The other layouts are Quill's. Lengths
 * here are twips (1/20 pt) in long integers, and angles tenths of a degree through util.c's tables:
 * no floating point.
 *
 * WordCraft's licence (for the parts ported from it):
 *   MIT License. Copyright (c) 2026 ArtCraft Team and the WordCraft contributors.
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
 *   associated documentation files (the "Software"), to deal in the Software without restriction,
 *   including without limitation the rights to use, copy, modify, merge, publish, distribute,
 *   sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 *   furnished to do so, subject to the following conditions: The above copyright notice and this
 *   permission notice shall be included in all copies or substantial portions of the Software.
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 *   NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
 *   OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "chartedit.h"

#define NS_DGM "http://schemas.openxmlformats.org/drawingml/2006/diagram"
#define NS_DSP "http://schemas.microsoft.com/office/drawing/2008/diagram"
#define NS_A "http://schemas.openxmlformats.org/drawingml/2006/main"
#define NS_R "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
#define CT_DATA "application/vnd.openxmlformats-officedocument.drawingml.diagramData+xml"
#define CT_LAYOUT "application/vnd.openxmlformats-officedocument.drawingml.diagramLayout+xml"
#define CT_STYLE "application/vnd.openxmlformats-officedocument.drawingml.diagramStyle+xml"
#define CT_COLORS "application/vnd.openxmlformats-officedocument.drawingml.diagramColors+xml"
#define CT_DRAWING "application/vnd.ms-office.drawingml.diagramDrawing+xml"
#define RT_DATA "http://schemas.openxmlformats.org/officeDocument/2006/relationships/diagramData"
#define RT_LAYOUT "http://schemas.openxmlformats.org/officeDocument/2006/relationships/diagramLayout"
#define RT_STYLE "http://schemas.openxmlformats.org/officeDocument/2006/relationships/diagramQuickStyle"
#define RT_COLORS "http://schemas.openxmlformats.org/officeDocument/2006/relationships/diagramColors"
#define RT_DRAWING "http://schemas.microsoft.com/office/2007/relationships/diagramDrawing"
#define DRAWREL "rIdQdrw"            /* the drawing's relationship id the data part names (renumbered on saving) */
#define MAXTEXT 400

static void xesc(Buf *o, const char *s) { xml_escape(o, s ? s : "", s ? strlen(s) : 0); }
static long lmin(long a, long b) { return a < b ? a : b; }
static long lmax(long a, long b) { return a > b ? a : b; }

/* ------------------------------------------------------------------ the layouts Quill has */
enum { A_BLOCKS, A_VBULLET, A_HBULLET, A_PROCESS, A_VPROCESS, A_CHEVRON, A_CYCLE, A_TEXTCYCLE, A_RADIAL, A_ORG, A_HIER, A_HHIER,
       A_VENN, A_TARGET, A_LINVENN, A_MATRIX, A_TMATRIX, A_GMATRIX, A_PYRAMID, A_INVPYR, A_PYRLIST, A_N };
typedef struct { const char *name, *id, *desc; u8 cat; } SALayout;
static const SALayout layouts[A_N] = {
  { "Basic Block List", "default", "Blocks of information in rows.", SAC_LIST },
  { "Vertical Bullet List", "vList2", "Headings in bars with their points under them.", SAC_LIST },
  { "Horizontal Bullet List", "hList1", "Headings side by side with their points under them.", SAC_LIST },
  { "Basic Process", "process1", "Steps of a task from left to right.", SAC_PROCESS },
  { "Vertical Process", "process2", "Steps of a task from top to bottom.", SAC_PROCESS },
  { "Basic Chevron Process", "chevron1", "Steps of a task as chevrons, left to right.", SAC_PROCESS },
  { "Basic Cycle", "cycle2", "Stages that repeat, round a circle.", SAC_CYCLE },
  { "Text Cycle", "cycle1", "Stages that repeat, as text round a circle.", SAC_CYCLE },
  { "Basic Radial", "radial1", "Parts round a central idea.", SAC_CYCLE },
  { "Organization Chart", "orgChart1", "Who reports to whom.", SAC_HIERARCHY },
  { "Hierarchy", "hierarchy1", "A tree from the top down.", SAC_HIERARCHY },
  { "Horizontal Hierarchy", "hierarchy2", "A tree from left to right.", SAC_HIERARCHY },
  { "Basic Venn", "venn1", "Overlapping ideas.", SAC_RELATIONSHIP },
  { "Basic Target", "target1", "Ideas inside one another.", SAC_RELATIONSHIP },
  { "Linear Venn", "venn3", "Overlapping ideas in a row.", SAC_RELATIONSHIP },
  { "Basic Matrix", "matrix3", "Parts of a whole in quadrants.", SAC_MATRIX },
  { "Titled Matrix", "matrix1", "Quadrants round a title.", SAC_MATRIX },
  { "Grid Matrix", "matrix2", "Quadrants on two axes.", SAC_MATRIX },
  { "Basic Pyramid", "pyramid1", "Levels from the narrow top down.", SAC_PYRAMID },
  { "Inverted Pyramid", "pyramid3", "Levels from the wide top down.", SAC_PYRAMID },
  { "Pyramid List", "pyramid2", "Points on a pyramid.", SAC_PYRAMID },
};
const char *const sa_category_names[SAC_N] = { "List", "Process", "Cycle", "Hierarchy", "Relationship", "Matrix", "Pyramid" };
static const char *const cat_ids[SAC_N] = { "list", "process", "cycle", "hierarchy", "relationship", "matrix", "pyramid" };
const char *const sa_color_names[SA_COLORS] = { "Accent 1", "Accent 2", "Accent 3", "Accent 4", "Accent 5", "Accent 6", "Colorful" };
const char *const sa_style_names[SA_STYLES] = { "Simple Fill", "White Outline", "Subtle Effect", "Moderate Effect", "Intense Effect" };
int sa_layout_count(void) { return A_N; }
const char *sa_layout_name(int k) { return k >= 0 && k < A_N ? layouts[k].name : ""; }
const char *sa_layout_desc(int k) { return k >= 0 && k < A_N ? layouts[k].desc : ""; }
int sa_layout_cat(int k) { return k >= 0 && k < A_N ? layouts[k].cat : 0; }

/* ------------------------------------------------------------------ the model */
void sa_free(SAModel *m)
{
  int i;
  if (!m) return;
  for (i = 0; i < m->n; i++) free(m->it[i].text);
  free(m->it);
  free(m);
}
SAModel *sa_copy(const SAModel *m)
{
  SAModel *c = (SAModel *)calloc(1, sizeof *c);
  int i;
  if (!c) return NULL;
  *c = *m;
  c->it = NULL; c->n = 0;
  if (m->n && !(c->it = (SAItem *)calloc((size_t)m->n, sizeof(SAItem)))) { free(c); return NULL; }
  for (i = 0; i < m->n; i++) {
    if (!(c->it[i].text = str_dup(m->it[i].text ? m->it[i].text : ""))) { sa_free(c); return NULL; }
    c->it[i].level = m->it[i].level;
    c->n = i + 1;
  }
  return c;
}
/* text as an item's: control characters as spaces, the ends trimmed, cut at a character boundary */
static char *clean(const char *s)
{
  size_t n, i;
  char *t;
  if (!s) s = "";
  while (*s == ' ' || (u8)*s < 32) { if (!*s) break; s++; }
  n = strlen(s);
  if (n > MAXTEXT) { n = MAXTEXT; while (n && ((u8)s[n] & 0xC0) == 0x80) n--; }
  while (n && (s[n - 1] == ' ' || (u8)s[n - 1] < 32)) n--;
  if (!(t = str_ndup(s, n))) return NULL;
  for (i = 0; i < n; i++) if ((u8)t[i] < 32) t[i] = ' ';
  return t;
}
void sa_normalise(SAModel *m)
{
  int i, prev = -1;
  for (i = 0; i < m->n; i++) {
    int most = prev < 0 ? 0 : prev + 1;
    if (most > SA_MAX_LEVEL) most = SA_MAX_LEVEL;
    if (m->it[i].level > most) m->it[i].level = (u8)most;
    prev = m->it[i].level;
  }
  if (m->layout >= A_N) m->layout = 0;
  if (m->colors >= SA_COLORS) m->colors = 0;
  if (m->style >= SA_STYLES) m->style = 0;
}
static int ins(SAModel *m, int at, const char *text, int level)
{
  SAItem *n;
  char *t;
  if (m->n >= SA_MAX_ITEMS || at < 0 || at > m->n || !(t = clean(text))) return -1;
  if (!(n = (SAItem *)realloc(m->it, (size_t)(m->n + 1) * sizeof(SAItem)))) { free(t); return -1; }
  m->it = n;
  memmove(&m->it[at + 1], &m->it[at], (size_t)(m->n - at) * sizeof(SAItem));
  m->it[at].text = t; m->it[at].level = (u8)(level < 0 ? 0 : level > SA_MAX_LEVEL ? SA_MAX_LEVEL : level);
  m->n++;
  return at;
}
SAModel *sa_sample(int layout)
{
  static const char *const hier[6] = { "Lead", "Design", "Sketches", "Build", "Code", "Tests" };
  static const u8 hierl[6] = { 0, 1, 2, 1, 2, 2 };
  static const char *const radial[5] = { "Goal", "Plan", "Build", "Test", "Ship" };
  static const char *const steps[4] = { "Plan", "Build", "Test", "Ship" };
  static const char *const three[3] = { "People", "Process", "Tools" };
  static const char *const quad[4] = { "Strengths", "Weaknesses", "Chances", "Risks" };
  static const char *const list[5] = { "Ideas", "Notes", "Drafts", "Edits", "Final" };
  static const char *const bullets[6] = { "Plan", "Goals", "Budget", "Build", "Parts", "Tests" };
  static const u8 bulletl[6] = { 0, 1, 1, 0, 1, 1 };
  SAModel *m = (SAModel *)calloc(1, sizeof *m);
  int i, ok = 1;
  if (!m) return NULL;
  m->layout = (u8)(layout >= 0 && layout < A_N ? layout : 0);
  switch (m->layout) {
  case A_ORG: case A_HIER: case A_HHIER: for (i = 0; i < 6 && ok; i++) ok = ins(m, m->n, hier[i], hierl[i]) >= 0; break;
  case A_RADIAL: for (i = 0; i < 5 && ok; i++) ok = ins(m, m->n, radial[i], i ? 1 : 0) >= 0; break;
  case A_PROCESS: case A_VPROCESS: case A_CHEVRON: case A_CYCLE: case A_TEXTCYCLE: for (i = 0; i < 4 && ok; i++) ok = ins(m, m->n, steps[i], 0) >= 0; break;
  case A_VENN: case A_TARGET: case A_LINVENN: case A_PYRAMID: case A_INVPYR: case A_PYRLIST: for (i = 0; i < 3 && ok; i++) ok = ins(m, m->n, three[i], 0) >= 0; break;
  case A_TMATRIX: ok = ins(m, 0, "Title", 0) >= 0; for (i = 0; i < 4 && ok; i++) ok = ins(m, m->n, quad[i], 0) >= 0; break;
  case A_MATRIX: case A_GMATRIX: for (i = 0; i < 4 && ok; i++) ok = ins(m, m->n, quad[i], 0) >= 0; break;
  case A_VBULLET: case A_HBULLET: for (i = 0; i < 6 && ok; i++) ok = ins(m, m->n, bullets[i], bulletl[i]) >= 0; break;
  default: for (i = 0; i < 5 && ok; i++) ok = ins(m, m->n, list[i], 0) >= 0; break;
  }
  if (!ok) { sa_free(m); return NULL; }
  return m;
}
/* the end of item i's run of items under it (the first after it at its level or above) */
static int sub_end(const SAModel *m, int i)
{
  int j = i + 1;
  while (j < m->n && m->it[j].level > m->it[i].level) j++;
  return j;
}
int sa_add(SAModel *m, int i, int where, const char *text)
{
  int lv, at, j, e;
  if (i < 0 || i >= m->n) return ins(m, m->n, text, 0);
  lv = m->it[i].level; e = sub_end(m, i);
  switch (where) {
  case SA_BEFORE: return ins(m, i, text, lv);
  case SA_ABOVE:
    /* a new item in its place, it and its items a level under the new one */
    if ((at = ins(m, i, text, lv)) < 0) return -1;
    for (j = i + 1; j <= e; j++) if (m->it[j].level < SA_MAX_LEVEL) m->it[j].level++;
    return at;
  case SA_BELOW: return ins(m, e, text, lv + 1 > SA_MAX_LEVEL ? SA_MAX_LEVEL : lv + 1);
  default: return ins(m, e, text, lv);
  }
}
int sa_delete(SAModel *m, int i)
{
  int j, e;
  if (i < 0 || i >= m->n) return -1;
  e = sub_end(m, i);
  for (j = i + 1; j < e; j++) if (m->it[j].level) m->it[j].level--;
  free(m->it[i].text);
  memmove(&m->it[i], &m->it[i + 1], (size_t)(m->n - i - 1) * sizeof(SAItem));
  m->n--;
  sa_normalise(m);
  return i < m->n ? i : m->n - 1;
}
int sa_set_text(SAModel *m, int i, const char *text)
{
  char *t;
  if (i < 0 || i >= m->n || !(t = clean(text))) return -1;
  free(m->it[i].text);
  m->it[i].text = t;
  return i;
}
int sa_promote(SAModel *m, int i)
{
  int j, e;
  if (i < 0 || i >= m->n || !m->it[i].level) return -1;
  e = sub_end(m, i);
  for (j = i; j < e; j++) m->it[j].level--;
  return i;
}
int sa_demote(SAModel *m, int i)
{
  int j, e;
  if (i <= 0 || i >= m->n || m->it[i - 1].level < m->it[i].level || m->it[i].level >= SA_MAX_LEVEL) return -1;
  e = sub_end(m, i);
  for (j = i; j < e; j++) if (m->it[j].level < SA_MAX_LEVEL) m->it[j].level++;
  return i;
}
int sa_move(SAModel *m, int i, int down)
{
  int a0, a1, b0, b1, n, lv;
  SAItem *t;
  if (i < 0 || i >= m->n) return -1;
  lv = m->it[i].level;
  if (down) {
    a0 = i; a1 = sub_end(m, i);
    if (a1 >= m->n || m->it[a1].level != lv) return -1;
    b0 = a1; b1 = sub_end(m, b0);
  } else {
    int p = i - 1;
    while (p >= 0 && m->it[p].level > lv) p--;
    if (p < 0 || m->it[p].level != lv) return -1;
    a0 = p; a1 = i; b0 = i; b1 = sub_end(m, i);
  }
  /* blocks [a0, a1) and [b0, b1) swapped */
  n = b1 - a0;
  if (!(t = (SAItem *)malloc((size_t)n * sizeof(SAItem)))) return -1;
  memcpy(t, &m->it[b0], (size_t)(b1 - b0) * sizeof(SAItem));
  memcpy(t + (b1 - b0), &m->it[a0], (size_t)(a1 - a0) * sizeof(SAItem));
  memcpy(&m->it[a0], t, (size_t)n * sizeof(SAItem));
  free(t);
  return down ? a0 + (b1 - b0) : a0;
}
/* the parent of each item (-1 at the top) */
static void parents(const SAModel *m, int *par)
{
  int last[SA_MAX_LEVEL + 2], i;
  for (i = 0; i < m->n; i++) {
    int l = m->it[i].level;
    par[i] = l ? last[l - 1] : -1;
    last[l] = i;
  }
}

/* ------------------------------------------------------------------ laying out (twips) */
enum { P_NONE, P_ACCENT, P_LIGHT };
typedef struct {
  const char *prst;
  long x, y, w, h;                 /* box */
  int rot;                          /* tenths of a degree clockwise */
  u8 flipH, flipV;
  long adj; u8 hasadj;
  u8 fill, line;                    /* P_ */
  u8 fa, la;                        /* accent number (0-5) */
  int ftint, ltint;                 /* thousandths of a per cent of the colour kept (0: all) */
  int alpha;                        /* fill opacity, thousandths of a per cent (0: opaque) */
  long lw;                          /* line width, twips */
  int item;                         /* its text: item number, -1 none */
  u8 kids;                          /* the items under it too, as bullets */
  u8 dark;                          /* dark text (an unfilled shape) */
  u8 left;                          /* text from the left */
  long tx, ty, tw, th;              /* the text's box */
} Sh;
typedef struct { Sh *s; int n, cap; int oom; const SAModel *m; long W, H; } Lay;
static Sh *add(Lay *L, const char *prst, long x, long y, long w, long h, int accent)
{
  Sh *s;
  if (L->n >= L->cap) {
    int nc = L->cap ? L->cap * 2 : 32;
    Sh *n = (Sh *)realloc(L->s, (size_t)nc * sizeof(Sh));
    if (!n) { L->oom = 1; return NULL; }
    L->s = n; L->cap = nc;
  }
  s = &L->s[L->n++];
  memset(s, 0, sizeof *s);
  s->prst = prst; s->x = x; s->y = y; s->w = w > 0 ? w : 0; s->h = h > 0 ? h : 0;
  s->fill = P_ACCENT; s->line = P_LIGHT; s->fa = s->la = (u8)accent; s->lw = 20;
  s->item = -1;
  s->tx = x + s->w * 6 / 100; s->ty = y + s->h * 6 / 100; s->tw = s->w * 88 / 100; s->th = s->h * 88 / 100;
  return s;
}
static void text_of(Sh *s, int item, int kids) { if (s) { s->item = item; s->kids = (u8)kids; } }
static void tbox(Sh *s, long x, long y, long w, long h) { if (s) { s->tx = x; s->ty = y; s->tw = w > 0 ? w : 0; s->th = h > 0 ? h : 0; } }
static void inset(Sh *s, int pct) { if (s) tbox(s, s->x + s->w * pct / 100, s->y + s->h * pct / 100, s->w * (100 - 2 * pct) / 100, s->h * (100 - 2 * pct) / 100); }
static Sh *line(Lay *L, long x0, long y0, long x1, long y1, int accent)
{
  Sh *s = add(L, "line", lmin(x0, x1), lmin(y0, y1), labs(x1 - x0), labs(y1 - y0), accent);
  if (!s) return NULL;
  s->flipH = (u8)(((x1 - x0) < 0) != ((y1 - y0) < 0) && x1 != x0 && y1 != y0);
  s->fill = P_NONE; s->line = P_ACCENT; s->ltint = 60000; s->lw = 30;
  return s;
}
static int accent_of(const SAModel *m, int k) { return m->colors == SA_COLORS - 1 ? k % 6 : m->colors; }
/* the top-level items */
static int tops(const SAModel *m, int *t)
{
  int i, n = 0;
  for (i = 0; i < m->n; i++) if (!m->it[i].level) t[n++] = i;
  return n;
}
/* the point at r from (cx, cy) at tenths of a degree a (clockwise from three o'clock) */
static void polar(long cx, long cy, long r, int a, long *x, long *y)
{
  a %= 3600; if (a < 0) a += 3600;
  *x = cx + r * icos(a) / TRIG_ONE;
  *y = cy + r * isin(a) / TRIG_ONE;
}
static int angle(int k, int n) { return -900 + 3600 * k / (n > 0 ? n : 1); }
/* sine of pi / n, times 1000 */
static long sin_pi_n(int n) { return n > 0 ? (long)isin(1800 / n) * 1000 / TRIG_ONE : 0; }

/* blocks of 3:2 in rows, as large as fit; the last row centred */
static void basic_list(Lay *L, long w, long h)
{
  const SAModel *m = L->m;
  int *t = (int *)malloc((size_t)(m->n + 1) * sizeof(int)), n, cols = 1, c, rows, k;
  long best = -1, bw, bh, gx, gy, y0;
  if (!t) { L->oom = 1; return; }
  n = tops(m, t);
  for (c = 1; c <= (n ? n : 1); c++) {
    int r = (n + c - 1) / c;
    long b = lmin(w * 9 / (10 * c), h * 27 / (20 * (r ? r : 1)));
    if (b > best) { best = b; cols = c; }
  }
  rows = (n + cols - 1) / cols;
  bw = best; bh = bw * 2 / 3; gx = bw / 10; gy = bh / 10;
  y0 = (h - (rows * bh + (rows - 1) * gy)) / 2;
  for (k = 0; k < n; k++) {
    int r = k / cols, cc = k % cols, inrow = r + 1 == rows ? n - r * cols : cols;
    long x0 = (w - (inrow * bw + (inrow - 1) * gx)) / 2;
    text_of(add(L, "rect", x0 + cc * (bw + gx), y0 + r * (bh + gy), bw, bh, accent_of(m, k)), t[k], 1);
  }
  free(t);
}
/* bars full width, each item's points as bullets under it */
static void vbullet(Lay *L, long w, long h)
{
  const SAModel *m = L->m;
  int *t = (int *)malloc((size_t)(m->n + 1) * sizeof(int)), n, k;
  long units = 0, u, y = 0;
  if (!t) { L->oom = 1; return; }
  n = tops(m, t);
  for (k = 0; k < n; k++) units += 10 + 6 * (sub_end(m, t[k]) - t[k] - 1) + (k ? 2 : 0);
  u = units ? h / units : h;
  if (u > h / 12 && n) u = lmin(u, h / (n * 10 + 1) + 1);
  for (k = 0; k < n; k++) {
    int kids = sub_end(m, t[k]) - t[k] - 1;
    Sh *s;
    if (k) y += 2 * u;
    s = add(L, "roundRect", 0, y, w, 10 * u, accent_of(m, k));
    text_of(s, t[k], 0);
    if (s) { s->left = 1; tbox(s, w / 30, y, w - w / 15, 10 * u); }
    y += 10 * u;
    if (kids) {
      s = add(L, "rect", w / 20, y, w - w / 20, 6 * u * kids, accent_of(m, k));
      if (s) { s->fill = P_NONE; s->line = P_NONE; s->dark = 1; s->left = 1; s->item = t[k]; s->kids = 2; tbox(s, w / 20, y, w - w / 20, 6 * u * kids); }
      y += 6 * u * kids;
    }
  }
  free(t);
}
/* headings side by side, their points in a lighter box under each */
static void hbullet(Lay *L, long w, long h)
{
  const SAModel *m = L->m;
  int *t = (int *)malloc((size_t)(m->n + 1) * sizeof(int)), n, k;
  long cw, gap, hh;
  if (!t) { L->oom = 1; return; }
  n = tops(m, t);
  if (!n) { free(t); return; }
  gap = w / (n * 20 + 1);
  cw = (w - gap * (n - 1)) / n;
  hh = h * 3 / 10;
  for (k = 0; k < n; k++) {
    long x = k * (cw + gap);
    Sh *s = add(L, "rect", x, 0, cw, hh, accent_of(m, k));
    text_of(s, t[k], 0);
    s = add(L, "rect", x, hh, cw, h - hh, accent_of(m, k));
    if (s) { s->ftint = 40000; s->line = P_ACCENT; s->ltint = 40000; s->dark = 1; s->left = 1; s->item = t[k]; s->kids = 2; inset(s, 5); }
  }
  free(t);
}
/* steps left to right with an arrow in each gap (or top to bottom) */
static void process(Lay *L, long w, long h, int vert)
{
  const SAModel *m = L->m;
  int *t = (int *)malloc((size_t)(m->n + 1) * sizeof(int)), n, k;
  long nw, nh, gap;
  if (!t) { L->oom = 1; return; }
  n = tops(m, t);
  if (!n) { free(t); return; }
  if (!vert) {
    nw = w * 100 / (n * 100 + (n - 1) * 45);
    nh = lmin(nw * 6 / 10, h * 9 / 10);
    gap = nw * 45 / 100;
    for (k = 1; k < n; k++) {
      long gx = k * (nw + gap) - gap, aw = gap * 62 / 100, ah = lmin(nh * 35 / 100, gap * 7 / 10);
      Sh *a = add(L, "rightArrow", gx + (gap - aw) / 2, h / 2 - ah / 2, aw, ah, accent_of(m, k - 1));
      if (a) { a->ftint = 60000; a->line = P_NONE; }
    }
    for (k = 0; k < n; k++) text_of(add(L, "roundRect", k * (nw + gap), (h - nh) / 2, nw, nh, accent_of(m, k)), t[k], 1);
  } else {
    nh = h * 100 / (n * 100 + (n - 1) * 45);
    nw = lmin(w * 9 / 10, nh * 3);
    gap = nh * 45 / 100;
    for (k = 1; k < n; k++) {
      long gy = k * (nh + gap) - gap, ah = gap * 62 / 100, aw = lmin(nw * 25 / 100, gap * 7 / 10);
      Sh *a = add(L, "downArrow", w / 2 - aw / 2, gy + (gap - ah) / 2, aw, ah, accent_of(m, k - 1));
      if (a) { a->ftint = 60000; a->line = P_NONE; }
    }
    for (k = 0; k < n; k++) text_of(add(L, "roundRect", (w - nw) / 2, k * (nh + gap), nw, nh, accent_of(m, k)), t[k], 1);
  }
  free(t);
}
/* chevrons left to right, each one's point into the next */
static void chevrons(Lay *L, long w, long h)
{
  const SAModel *m = L->m;
  int *t = (int *)malloc((size_t)(m->n + 1) * sizeof(int)), n, k;
  long cw, ch, step;
  if (!t) { L->oom = 1; return; }
  n = tops(m, t);
  if (!n) { free(t); return; }
  /* each chevron cw wide, the next starting cw - ch/2 + a little on */
  ch = lmin(h * 9 / 10, w * 100 / (n * 200 + 50));
  cw = (w + (n - 1) * (ch / 2 - ch / 10)) / n;
  if (cw < ch) { ch = cw; }
  step = cw - ch / 2 + ch / 10;
  for (k = 0; k < n; k++) {
    Sh *s = add(L, "chevron", k * step, (h - ch) / 2, cw, ch, accent_of(m, k));
    text_of(s, t[k], 1);
    if (s) tbox(s, s->x + ch / 2, s->y + ch / 10, cw - ch, ch * 8 / 10);
  }
  free(t);
}
/* circles round a circle, clockwise from the top, an arrow between each (text: no circles) */
static void cycle(Lay *L, long w, long h, int textonly)
{
  const SAModel *m = L->m;
  int *t = (int *)malloc((size_t)(m->n + 1) * sizeof(int)), n, k, i;
  long mm = lmin(w, h), cx = w / 2, cy = h / 2, step, d, r;
  if (!t) { L->oom = 1; return; }
  n = tops(m, t);
  if (!n) { free(t); return; }
  step = sin_pi_n(n);
  d = mm * 32 / 100;
  for (i = 0; i < 4; i++) { r = (mm - d) / 2; d = n == 1 ? mm / 2 : lmin(mm * 32 / 100, 2 * r * step / 1000 * 62 / 100); }
  r = (mm - d) / 2;
  if (n > 1) {
    long chord = 2 * r * step / 1000, len = (chord - d) * 6 / 10;
    if (textonly) len = chord * 4 / 10;
    if (len > 40) {
      long thick = lmin(len * 6 / 10, d / 4);
      for (k = 0; k < n; k++) {
        int mid = angle(k, n) + 1800 / n;
        long x, y;
        Sh *a;
        polar(cx, cy, r, mid, &x, &y);
        a = add(L, "rightArrow", x - len / 2, y - thick / 2, len, thick, accent_of(m, k));
        if (a) { a->rot = ((mid + 900) % 3600 + 3600) % 3600; a->ftint = 60000; a->line = P_NONE; }
      }
    }
  }
  for (k = 0; k < n; k++) {
    long x = cx, y = cy;
    Sh *s;
    if (n > 1) polar(cx, cy, r, angle(k, n), &x, &y);
    s = add(L, textonly ? "rect" : "ellipse", x - d / 2, y - d / 2, d, d, accent_of(m, k));
    text_of(s, t[k], 1);
    if (s) { inset(s, 15); if (textonly) { s->fill = P_NONE; s->line = P_NONE; s->dark = 1; inset(s, 2); } }
  }
  free(t);
}
/* the first item in the middle; the items under it (and later top-level ones) round it, each joined by a line */
static void radial(Lay *L, long w, long h)
{
  const SAModel *m = L->m;
  int *par = (int *)malloc((size_t)(m->n + 1) * sizeof(int)), *sp = (int *)malloc((size_t)(m->n + 1) * sizeof(int)), n = 0, k, i;
  long mm = lmin(w, h), cx = w / 2, cy = h / 2, step, ds, r, dc;
  Sh *s;
  if (!par || !sp) { free(par); free(sp); L->oom = 1; return; }
  parents(m, par);
  for (i = 1; i < m->n; i++) if (par[i] == 0 || par[i] < 0) sp[n++] = i;
  if (!n) {
    long d = mm / 2;
    if (m->n) { s = add(L, "ellipse", cx - d / 2, cy - d / 2, d, d, accent_of(m, 0)); text_of(s, 0, 0); inset(s, 15); }
    free(par); free(sp);
    return;
  }
  step = n == 1 ? 1000 : sin_pi_n(n);
  ds = mm * 24 / 100;
  for (i = 0; i < 4; i++) { r = (mm - ds) / 2; ds = lmin(mm * 24 / 100, 2 * r * step / 1000 * 85 / 100); }
  r = (mm - ds) / 2;
  dc = lmax(lmin(mm * 36 / 100, 2 * (r - ds / 2 - mm * 3 / 100)), mm / 20);
  for (k = 0; k < n; k++) { long x, y; polar(cx, cy, r, angle(k, n), &x, &y); line(L, cx, cy, x, y, accent_of(m, 0)); }
  s = add(L, "ellipse", cx - dc / 2, cy - dc / 2, dc, dc, accent_of(m, 0)); text_of(s, 0, 0); inset(s, 15);
  for (k = 0; k < n; k++) {
    long x, y;
    polar(cx, cy, r, angle(k, n), &x, &y);
    s = add(L, "ellipse", x - ds / 2, y - ds / 2, ds, ds, accent_of(m, k + 1)); text_of(s, sp[k], 1); inset(s, 15);
  }
  free(par); free(sp);
}
/* a tree, a row (or a column) per level: each item centred on the items under it, joined by elbow lines */
static void tree(Lay *L, long w, long h, int horiz, int org)
{
  const SAModel *m = L->m;
  int n = m->n, i, k, depth = 1;
  int *par = (int *)malloc((size_t)(n + 1) * sizeof(int)), *leaves = (int *)malloc((size_t)(n + 1) * sizeof(int));
  long *start = (long *)malloc((size_t)(n + 1) * sizeof(long)), *cur = (long *)malloc((size_t)(n + 1) * sizeof(long));
  long total = 0, unit, rh, nw, nh, along = horiz ? h : w, down = horiz ? w : h, at = 0;
  if (!par || !leaves || !start || !cur) { free(par); free(leaves); free(start); free(cur); L->oom = 1; return; }
  parents(m, par);
  for (i = 0; i < n; i++) { leaves[i] = 0; if (m->it[i].level + 1 > depth) depth = m->it[i].level + 1; }
  /* leaves under each item (children come after their parents, so from the end) */
  for (i = n - 1; i >= 0; i--) { if (!leaves[i]) leaves[i] = 1; if (par[i] >= 0) leaves[par[i]] += leaves[i]; }
  for (i = 0; i < n; i++) if (par[i] < 0) total += leaves[i];
  /* (a parent's count began at 0 and gathered its children's; a leaf's is 1) */
  if (!total) total = 1;
  unit = along / total;
  rh = down / depth;
  if (!horiz) { nw = lmin(unit * 88 / 100, rh * 62 * 18 / 1000); nh = lmin(rh * 62 / 100, nw * 8 / 10); }
  else { nh = lmin(unit * 80 / 100, rh * 45 / 100); nw = lmin(rh * 70 / 100, nh * 22 / 10); }
  for (i = 0; i < n; i++) if (par[i] < 0) { start[i] = at; at += leaves[i] * unit; }
  for (i = 0; i < n; i++) cur[i] = start[i];
  for (i = 0; i < n; i++) if (par[i] >= 0) { start[i] = cur[par[i]]; cur[par[i]] += leaves[i] * unit; cur[i] = start[i]; }
#define MID(i) (start[i] + leaves[i] * unit / 2)
#define LV0(i) ((long)m->it[i].level * rh + (rh - (horiz ? nw : nh)) / 2)
  for (i = 0; i < n; i++) {
    long pm, pe, ce, mid, lo, hi;
    int first = -1, ac = org ? accent_of(m, 0) : accent_of(m, i);
    for (k = i + 1; k < n && m->it[k].level > m->it[i].level; k++) if (par[k] == i && first < 0) first = k;
    if (first < 0) continue;
    pm = MID(i); pe = LV0(i) + (horiz ? nw : nh); ce = LV0(first);
    mid = (pe + ce) / 2;
    lo = hi = pm;
    for (k = i + 1; k < n && m->it[k].level > m->it[i].level; k++) if (par[k] == i) { if (MID(k) < lo) lo = MID(k); if (MID(k) > hi) hi = MID(k); }
    if (!horiz) {
      line(L, pm, pe, pm, mid, ac);
      if (hi - lo > 10) line(L, lo, mid, hi, mid, ac);
      for (k = i + 1; k < n && m->it[k].level > m->it[i].level; k++) if (par[k] == i) line(L, MID(k), mid, MID(k), ce, ac);
    } else {
      line(L, pe, pm, mid, pm, ac);
      if (hi - lo > 10) line(L, mid, lo, mid, hi, ac);
      for (k = i + 1; k < n && m->it[k].level > m->it[i].level; k++) if (par[k] == i) line(L, mid, MID(k), ce, MID(k), ac);
    }
  }
  for (i = 0; i < n; i++) {
    Sh *s = horiz ? add(L, "roundRect", LV0(i), MID(i) - nh / 2, nw, nh, accent_of(m, org ? 0 : i))
                  : add(L, org ? "rect" : "roundRect", MID(i) - nw / 2, LV0(i), nw, nh, accent_of(m, org ? 0 : i));
    text_of(s, i, 0);
  }
#undef MID
#undef LV0
  free(par); free(leaves); free(start); free(cur);
}
/* overlapping circles round the middle; each one's text on its outer side */
static void venn(Lay *L, long w, long h)
{
  const SAModel *m = L->m;
  int *t = (int *)malloc((size_t)(m->n + 1) * sizeof(int)), n, j;
  long mm = lmin(w, h), cx = w / 2, cy = h / 2, k1000, r;
  int start;
  if (!t) { L->oom = 1; return; }
  n = tops(m, t);
  if (!n) { free(t); return; }
  /* ring radius against circle radius (x1000): neighbours' centres 1.5 radii apart */
  k1000 = n == 1 ? 0 : lmax(750000 / lmax(sin_pi_n(n), 1), 600);
  r = mm * 1000 / (2 * (k1000 + 1000));
  start = n == 2 ? 1800 : -900;
#define PLACE(j, x, y) polar(cx, cy, r * k1000 / 1000, start + 3600 * (j) / n, x, y)
  for (j = 0; j < n; j++) {
    long x, y;
    Sh *s;
    PLACE(j, &x, &y);
    s = add(L, "ellipse", x - r, y - r, 2 * r, 2 * r, accent_of(m, j));
    if (s) { s->ftint = 40000; s->alpha = 60000; s->line = P_NONE; }
  }
  for (j = 0; j < n; j++) {
    long x, y;
    Sh *s;
    PLACE(j, &x, &y);
    s = add(L, "ellipse", x - r, y - r, 2 * r, 2 * r, accent_of(m, j));
    if (s) { s->fill = P_NONE; s->line = P_ACCENT; s->lw = 30; }
  }
  for (j = 0; j < n; j++) {
    long x, y, tx, ty, bw = r * 11 / 10, bh = r * 8 / 10;
    Sh *s;
    PLACE(j, &x, &y);
    tx = x; ty = y;
    if (n > 1) polar(x, y, r * 35 / 100, start + 3600 * j / n, &tx, &ty);
    s = add(L, "rect", tx - bw / 2, ty - bh / 2, bw, bh, accent_of(m, j));
    text_of(s, t[j], 1);
    if (s) { s->fill = P_NONE; s->line = P_NONE; s->dark = 1; tbox(s, s->x, s->y, bw, bh); }
  }
#undef PLACE
  free(t);
}
/* circles in a row overlapping by a quarter */
static void linvenn(Lay *L, long w, long h)
{
  const SAModel *m = L->m;
  int *t = (int *)malloc((size_t)(m->n + 1) * sizeof(int)), n, j;
  long d, step, x0;
  if (!t) { L->oom = 1; return; }
  n = tops(m, t);
  if (!n) { free(t); return; }
  d = lmin(w * 100 / (n * 75 + 25), h);
  step = d * 75 / 100;
  x0 = (w - (step * (n - 1) + d)) / 2;
  for (j = 0; j < n; j++) {
    Sh *s = add(L, "ellipse", x0 + j * step, (h - d) / 2, d, d, accent_of(m, j));
    if (s) { s->alpha = 50000; s->line = P_LIGHT; text_of(s, t[j], 1); tbox(s, s->x + d / 6, s->y + d / 4, d * 2 / 3, d / 2); }
  }
  free(t);
}
/* rings inside one another: the first item the outermost, each one's text in its band at the top */
static void target(Lay *L, long w, long h)
{
  const SAModel *m = L->m;
  int *t = (int *)malloc((size_t)(m->n + 1) * sizeof(int)), n, j;
  long mm = lmin(w, h), cx = w / 2, cy = h / 2, band;
  if (!t) { L->oom = 1; return; }
  n = tops(m, t);
  if (!n) { free(t); return; }
  band = mm / (2 * n);
  for (j = 0; j < n; j++) {
    long rr = mm / 2 - j * band;
    Sh *s = add(L, "ellipse", cx - rr, cy - rr, 2 * rr, 2 * rr, accent_of(m, j));
    text_of(s, t[j], 0);
    if (s) {
      if (j + 1 < n) tbox(s, cx - rr * 6 / 10, cy - rr + band / 10, rr * 12 / 10, band * 8 / 10);
      else inset(s, 15);
    }
  }
  free(t);
}
/* quadrants of a square (more items: a larger grid); titled: the first item over the middle; grid: lighter, close */
static void matrix(Lay *L, long w, long h, int kind)
{
  const SAModel *m = L->m;
  int *t = (int *)malloc((size_t)(m->n + 1) * sizeof(int)), n, cols = 1, rows, k, k0 = kind == 1 ? 1 : 0;
  long cell, gap, x0, y0;
  if (!t) { L->oom = 1; return; }
  n = tops(m, t);
  if (n - k0 <= 0) {
    if (n) text_of(add(L, "roundRect", w / 4, h / 4, w / 2, h / 2, accent_of(m, 0)), t[0], 1);
    free(t);
    return;
  }
  while (cols * cols < n - k0) cols++;
  rows = (n - k0 + cols - 1) / cols;
  cell = lmin(w / cols, h / rows);
  gap = kind == 2 ? cell / 100 : cell * 4 / 100;
  x0 = (w - cols * cell) / 2; y0 = (h - rows * cell) / 2;
  for (k = k0; k < n; k++) {
    int r = (k - k0) / cols, c = (k - k0) % cols;
    Sh *s = add(L, kind == 2 ? "rect" : "roundRect", x0 + c * cell + gap / 2, y0 + r * cell + gap / 2, cell - gap, cell - gap, accent_of(m, k));
    text_of(s, t[k], 1);
    inset(s, 10);
    if (s && kind == 2) { s->ftint = 40000; s->dark = 1; s->line = P_LIGHT; }
    if (s && kind == 1) {
      /* the text away from the middle the title covers */
      long hw = s->w / 2, hh = s->h / 2;
      tbox(s, s->x + (c % 2 ? hw / 2 : s->w / 20), s->y + (r % 2 ? hh : s->h / 20), hw * 13 / 10, hh * 9 / 10);
    }
  }
  if (kind == 1) {
    long tw = cols * cell * 4 / 10, th = rows * cell / 4;
    Sh *s = add(L, "roundRect", w / 2 - tw / 2, h / 2 - th / 2, tw, th, accent_of(m, 0));
    text_of(s, t[0], 0);
    if (s) { s->ftint = 60000; s->dark = 1; s->line = P_LIGHT; s->lw = 40; }
  }
  if (kind == 2) {
    line(L, w / 2, lmax(y0 - cell / 20, 0), w / 2, lmin(y0 + rows * cell + cell / 20, h), accent_of(m, 0));
    line(L, lmax(x0 - cell / 20, 0), h / 2, lmin(x0 + cols * cell + cell / 20, w), h / 2, accent_of(m, 0));
  }
  free(t);
}
/* bands stacked into a triangle: the first item at the narrow top (inverted: at the wide top) */
static void pyramid(Lay *L, long w, long h, int inv)
{
  const SAModel *m = L->m;
  int *t = (int *)malloc((size_t)(m->n + 1) * sizeof(int)), n, k;
  long tw = lmin(w, h * 13 / 10), x0 = (w - lmin(w, h * 13 / 10)) / 2, bh;
  if (!t) { L->oom = 1; return; }
  n = tops(m, t);
  if (!n) { free(t); return; }
  bh = h / n;
  for (k = 0; k < n; k++) {
    int b = inv ? n - 1 - k : k;      /* its band from the narrow end */
    long topw = tw * b / n, botw = tw * (b + 1) / n, y = inv ? (long)k * bh : (long)b * bh;
    Sh *s;
    if (b == 0) {
      s = add(L, "triangle", x0 + (tw - botw) / 2, y, botw, bh, accent_of(m, k));
      if (s) {
        if (inv) { s->flipV = 1; tbox(s, s->x + botw * 22 / 100, y + bh * 4 / 100, botw * 56 / 100, bh * 56 / 100); }
        else tbox(s, s->x + botw * 22 / 100, y + bh * 40 / 100, botw * 56 / 100, bh * 56 / 100);
      }
    } else {
      long ix = (botw - topw) / 2, ss = lmax(lmin(botw, bh), 1), midw = (topw + botw) / 2 * 8 / 10;
      s = add(L, "trapezoid", x0 + (tw - botw) / 2, y, botw, bh, accent_of(m, k));
      if (s) { s->adj = ix * 1000 / ss * 100; s->hasadj = 1; s->flipV = (u8)inv; tbox(s, w / 2 - midw / 2, y + bh * 8 / 100, midw, bh * 84 / 100); }
    }
    if (inv && s) {
      /* (an upside-down shape would turn its text over: the text goes in a box of its own on it) */
      long bx = s->tx, by = s->ty, bw = s->tw, bh2 = s->th;
      int ac = s->fa;
      s = add(L, "rect", bx, by, bw, bh2, ac);
      if (s) { s->fill = P_NONE; s->line = P_NONE; tbox(s, bx, by, bw, bh2); }
    }
    text_of(s, t[k], 1);
  }
  free(t);
}
/* a pyramid behind, the items in boxes on it */
static void pyrlist(Lay *L, long w, long h)
{
  const SAModel *m = L->m;
  int *t = (int *)malloc((size_t)(m->n + 1) * sizeof(int)), n, k;
  long tw = lmin(w, h * 13 / 10), x0 = (w - tw) / 2, bw, bh, y0, step;
  Sh *s;
  if (!t) { L->oom = 1; return; }
  n = tops(m, t);
  s = add(L, "triangle", x0, 0, tw, h, accent_of(m, 0));
  if (s) { s->ftint = 60000; s->line = P_NONE; }
  if (!n) { free(t); return; }
  bw = tw * 55 / 100;
  step = h * 66 / 100 / n;
  bh = step * 85 / 100;
  y0 = h * 30 / 100;
  for (k = 0; k < n; k++) {
    s = add(L, "roundRect", w / 2 - bw / 2, y0 + k * step, bw, bh, accent_of(m, k));
    text_of(s, t[k], 1);
    if (s) { s->fill = P_LIGHT; s->line = P_ACCENT; s->dark = 1; s->lw = 20; }
  }
  free(t);
}
/* the shapes of the graphic laid out in w x h twips, back to front; right to left mirrored */
static void lay_out(Lay *L, const SAModel *m, long w, long h)
{
  int i;
  memset(L, 0, sizeof *L);
  L->m = m; L->W = w; L->H = h;
  if (!m->n || w < 20 || h < 20) return;
  switch (m->layout) {
  case A_VBULLET: vbullet(L, w, h); break;
  case A_HBULLET: hbullet(L, w, h); break;
  case A_PROCESS: process(L, w, h, 0); break;
  case A_VPROCESS: process(L, w, h, 1); break;
  case A_CHEVRON: chevrons(L, w, h); break;
  case A_CYCLE: cycle(L, w, h, 0); break;
  case A_TEXTCYCLE: cycle(L, w, h, 1); break;
  case A_RADIAL: radial(L, w, h); break;
  case A_ORG: tree(L, w, h, 0, 1); break;
  case A_HIER: tree(L, w, h, 0, 0); break;
  case A_HHIER: tree(L, w, h, 1, 0); break;
  case A_VENN: venn(L, w, h); break;
  case A_TARGET: target(L, w, h); break;
  case A_LINVENN: linvenn(L, w, h); break;
  case A_MATRIX: matrix(L, w, h, 0); break;
  case A_TMATRIX: matrix(L, w, h, 1); break;
  case A_GMATRIX: matrix(L, w, h, 2); break;
  case A_PYRAMID: pyramid(L, w, h, 0); break;
  case A_INVPYR: pyramid(L, w, h, 1); break;
  case A_PYRLIST: pyrlist(L, w, h); break;
  default: basic_list(L, w, h); break;
  }
  if (m->rtl)
    for (i = 0; i < L->n; i++) {
      Sh *s = &L->s[i];
      s->x = w - s->x - s->w; s->tx = w - s->tx - s->tw;
      if (strcmp(s->prst, "ellipse") && strcmp(s->prst, "rect") && strcmp(s->prst, "roundRect") && strcmp(s->prst, "triangle") && strcmp(s->prst, "trapezoid")) s->flipH = (u8)!s->flipH;
      if (s->rot) s->rot = (3600 - s->rot) % 3600;
    }
}

/* ------------------------------------------------------------------ text sizes */
/* a text's width at size (half points), twips: an average letter half the size wide */
static long text_w(const char *s, size_t n, int hp)
{
  size_t i, ch = 0;
  for (i = 0; i < n; i++) if (((u8)s[i] & 0xC0) != 0x80) ch++;
  return (long)ch * hp * 11 / 2;         /* hp/2 pt * 0.55 em * 20 twips */
}
/* lines the text wraps to in width w at size hp; -1 if a word is wider than w */
static int wrap_lines(const char *s, int hp, long w)
{
  int lines = 1;
  long x = 0, sp = text_w(" ", 1, hp);
  while (*s) {
    const char *e;
    long ww;
    while (*s == ' ') s++;
    if (!*s) break;
    for (e = s; *e && *e != ' '; e++) ;
    ww = text_w(s, (size_t)(e - s), hp);
    if (ww > w) return -1;
    if (x && x + sp + ww > w) { lines++; x = ww; }
    else x += (x ? sp : 0) + ww;
    s = e;
  }
  return lines;
}
static int child_hp(int hp) { return hp * 8 / 10 > 8 ? hp * 8 / 10 : 8; }
/* the paragraphs a shape shows fit its text box at size hp */
static int fits(const SAModel *m, const Sh *s, int hp)
{
  long total = 0;
  int j, e, n;
  if (s->item < 0) return 1;
  e = s->kids ? sub_end(m, s->item) : s->item + 1;
  for (j = s->kids == 2 ? s->item + 1 : s->item; j < e; j++) {
    int h = j == s->item ? hp : child_hp(hp);
    char t[MAXTEXT + 8];
    snprintf(t, sizeof t, "%s%s", j == s->item ? "" : "\xE2\x80\xA2 ", m->it[j].text);
    if ((n = wrap_lines(t, h, s->tw)) < 0) return 0;
    total += (long)n * h * 12;          /* lines 1.2 times the size: hp/2 * 1.2 * 20 */
  }
  return total <= s->th;
}
static int text_size(const SAModel *m, const Lay *L)
{
  int hp = 48, i, ok;
  for (; hp > 10; hp--) {
    for (ok = 1, i = 0; i < L->n && ok; i++) ok = fits(m, &L->s[i], hp);
    if (ok) break;
  }
  return hp;
}

/* ------------------------------------------------------------------ the parts */
static const u32 office_accents[6] = { 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47 };
static u32 mix(u32 c, int keep)
{
  long r = (long)((c >> 16) & 255), g = (long)((c >> 8) & 255), b = (long)(c & 255);
  if (keep <= 0 || keep >= 100000) return c;
  r = 255 - (255 - r) * keep / 100000; g = 255 - (255 - g) * keep / 100000; b = 255 - (255 - b) * keep / 100000;
  return ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}
static void paint(Buf *o, int p, int accent, int tint, int alpha)
{
  u32 c;
  if (p == P_NONE) { buf_adds(o, "<a:noFill/>"); return; }
  c = p == P_LIGHT ? 0xFFFFFF : mix(office_accents[accent % 6], tint);
  if (alpha) buf_printf(o, "<a:solidFill><a:srgbClr val=\"%06lX\"><a:alpha val=\"%d\"/></a:srgbClr></a:solidFill>", (unsigned long)c, alpha);
  else buf_printf(o, "<a:solidFill><a:srgbClr val=\"%06lX\"/></a:solidFill>", (unsigned long)c);
}
static void model_id(char *out, long n) { sprintf(out, "{5743A57A-0000-4000-8000-%012lX}", (unsigned long)n); }
#define EMU(t) ((long)(t) * 635L)
static void shape_xml(Buf *o, const SAModel *m, const Sh *s, long id, int hp)
{
  char mid[48];
  int j, e;
  const char *tc = s->dark ? "000000" : "FFFFFF";
  model_id(mid, id);
  buf_printf(o, "<dsp:sp modelId=\"%s\"><dsp:nvSpPr><dsp:cNvPr id=\"0\" name=\"\"/><dsp:cNvSpPr/></dsp:nvSpPr><dsp:spPr><a:xfrm", mid);
  if (s->rot) buf_printf(o, " rot=\"%ld\"", (long)s->rot * 6000L);
  if (s->flipH) buf_adds(o, " flipH=\"1\"");
  if (s->flipV) buf_adds(o, " flipV=\"1\"");
  buf_printf(o, "><a:off x=\"%ld\" y=\"%ld\"/><a:ext cx=\"%ld\" cy=\"%ld\"/></a:xfrm><a:prstGeom prst=\"%s\">", EMU(s->x), EMU(s->y), EMU(s->w), EMU(s->h), s->prst);
  if (s->hasadj) buf_printf(o, "<a:avLst><a:gd name=\"adj\" fmla=\"val %ld\"/></a:avLst>", s->adj);
  else buf_adds(o, "<a:avLst/>");
  buf_adds(o, "</a:prstGeom>");
  paint(o, s->fill, s->fa, s->ftint, s->alpha);
  {
    /* the outline: as the quick style has it for filled shapes */
    int st = m->style, ln = s->line;
    long lw = s->lw;
    if (s->fill != P_NONE && ln == P_LIGHT && s->item >= 0) {
      if (st == 1) lw = 60;
      else if (st == 2) ln = P_NONE;
    }
    if (ln != P_NONE && lw > 0) {
      int lt = s->ltint;
      if (s->fill != P_NONE && ln == P_LIGHT && s->item >= 0 && st >= 3)     /* (moderate and intense: a darker edge) */
        buf_printf(o, "<a:ln w=\"%ld\"><a:solidFill><a:srgbClr val=\"%06lX\"/></a:solidFill>", EMU(st == 4 ? 30 : 15), (unsigned long)((office_accents[s->fa % 6] >> 1) & 0x7F7F7F));
      else { buf_printf(o, "<a:ln w=\"%ld\">", EMU(lw)); paint(o, ln, s->la, lt, 0); }
      buf_adds(o, "</a:ln>");
    } else buf_adds(o, "<a:ln><a:noFill/></a:ln>");
    if (st >= 2 && s->fill != P_NONE && s->item >= 0)
      buf_printf(o, "<a:effectLst><a:outerShdw blurRad=\"%d\" dist=\"%d\" dir=\"5400000\" rotWithShape=\"0\"><a:srgbClr val=\"000000\"><a:alpha val=\"%d\"/></a:srgbClr></a:outerShdw></a:effectLst>",
                 st == 2 ? 38100 : st == 3 ? 57150 : 76200, st == 2 ? 12700 : st == 3 ? 19050 : 38100, st == 4 ? 50000 : 35000);
  }
  buf_printf(o, "</dsp:spPr><dsp:style><a:lnRef idx=\"2\"><a:scrgbClr r=\"0\" g=\"0\" b=\"0\"/></a:lnRef><a:fillRef idx=\"1\"><a:scrgbClr r=\"0\" g=\"0\" b=\"0\"/></a:fillRef>"
                "<a:effectRef idx=\"0\"><a:scrgbClr r=\"0\" g=\"0\" b=\"0\"/></a:effectRef><a:fontRef idx=\"minor\"><a:schemeClr val=\"%s\"/></a:fontRef></dsp:style>", s->dark ? "tx1" : "lt1");
  if (s->item >= 0) {
    e = s->kids ? sub_end(m, s->item) : s->item + 1;
    buf_printf(o, "<dsp:txBody><a:bodyPr spcFirstLastPara=\"0\" vert=\"horz\" wrap=\"square\" lIns=\"0\" tIns=\"0\" rIns=\"0\" bIns=\"0\" numCol=\"1\" spcCol=\"0\" anchor=\"%s\" anchorCtr=\"0\"><a:noAutofit/></a:bodyPr><a:lstStyle/>",
               s->kids == 2 ? "t" : "ctr");
    for (j = s->kids == 2 ? s->item + 1 : s->item; j < e; j++) {
      int child = j != s->item, h = child ? child_hp(hp) : hp;
      const char *algn = child || s->left ? (m->rtl ? "r" : "l") : "ctr";
      buf_printf(o, "<a:p><a:pPr lvl=\"%d\" algn=\"%s\"><a:buNone/></a:pPr>", child ? 1 : 0, algn);
      if (!m->it[j].text[0] && !child) buf_printf(o, "<a:endParaRPr lang=\"en-US\" sz=\"%d\"/>", h * 50);
      else {
        buf_printf(o, "<a:r><a:rPr lang=\"en-US\" sz=\"%d\"><a:solidFill><a:srgbClr val=\"%s\"/></a:solidFill></a:rPr><a:t>", h * 50, tc);
        if (child) buf_adds(o, "\xE2\x80\xA2 ");
        xesc(o, m->it[j].text);
        buf_adds(o, "</a:t></a:r>");
      }
      buf_adds(o, "</a:p>");
    }
    buf_printf(o, "</dsp:txBody><dsp:txXfrm><a:off x=\"%ld\" y=\"%ld\"/><a:ext cx=\"%ld\" cy=\"%ld\"/></dsp:txXfrm>", EMU(s->tx), EMU(s->ty), EMU(s->tw), EMU(s->th));
  }
  buf_adds(o, "</dsp:sp>");
}
int sa_drawing_xml(const SAModel *m, i32 cx, i32 cy, Buf *o)
{
  Lay L;
  int i, hp;
  lay_out(&L, m, cx, cy);
  if (L.oom) { free(L.s); return 0; }
  hp = text_size(m, &L);
  buf_adds(o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<dsp:drawing xmlns:dgm=\"" NS_DGM "\" xmlns:dsp=\"" NS_DSP "\" xmlns:a=\"" NS_A "\">"
              "<dsp:spTree><dsp:nvGrpSpPr><dsp:cNvPr id=\"0\" name=\"\"/><dsp:cNvGrpSpPr/></dsp:nvGrpSpPr><dsp:grpSpPr/>");
  for (i = 0; i < L.n; i++) shape_xml(o, m, &L.s[i], 0x10000L + i, hp);
  buf_adds(o, "</dsp:spTree></dsp:drawing>");
  free(L.s);
  return !o->oom;
}
static void layout_id(const SAModel *m, char *out, size_t cap) { snprintf(out, cap, "urn:microsoft.com/office/officeart/2005/8/layout/%s", layouts[m->layout < A_N ? m->layout : 0].id); }
static void colors_id(const SAModel *m, char *id, size_t cap, char *cat, size_t ccap)
{
  if (m->colors >= SA_COLORS - 1) { snprintf(id, cap, "urn:microsoft.com/office/officeart/2005/8/colors/colorful1"); snprintf(cat, ccap, "colorful"); }
  else { snprintf(id, cap, "urn:microsoft.com/office/officeart/2005/8/colors/accent%d_2", m->colors + 1); snprintf(cat, ccap, "accent%d", m->colors + 1); }
}
static const char *const qs_ids[SA_STYLES] = { "simple1", "simple2", "simple3", "simple4", "simple5" };
static void empty_t(Buf *o) { buf_adds(o, "<dgm:t><a:bodyPr/><a:lstStyle/><a:p><a:endParaRPr lang=\"en-US\"/></a:p></dgm:t>"); }
int sa_data_xml(const SAModel *m, const char *drawrel, Buf *o)
{
  char lo[120], cs[120], cc[24], doc[48], a[48], b[48], c[48], d[48], src[48];
  int i, *par, ord[SA_MAX_ITEMS + 1];
  if (!(par = (int *)malloc((size_t)(m->n + 1) * sizeof(int)))) return 0;
  parents(m, par);
  layout_id(m, lo, sizeof lo);
  colors_id(m, cs, sizeof cs, cc, sizeof cc);
  model_id(doc, 0);
  buf_adds(o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<dgm:dataModel xmlns:dgm=\"" NS_DGM "\" xmlns:a=\"" NS_A "\"><dgm:ptLst>");
  buf_printf(o, "<dgm:pt modelId=\"%s\" type=\"doc\"><dgm:prSet loTypeId=\"%s\" loCatId=\"%s\" qsTypeId=\"urn:microsoft.com/office/officeart/2005/8/quickstyle/%s\" qsCatId=\"simple\" csTypeId=\"%s\" csCatId=\"%s\"",
             doc, lo, cat_ids[layouts[m->layout < A_N ? m->layout : 0].cat], qs_ids[m->style < SA_STYLES ? m->style : 0], cs, cc);
  if (m->rtl) buf_adds(o, "><dgm:presLayoutVars><dgm:dir val=\"rev\"/></dgm:presLayoutVars></dgm:prSet>");
  else buf_adds(o, "/>");
  buf_adds(o, "<dgm:spPr/>");
  empty_t(o);
  buf_adds(o, "</dgm:pt>");
  for (i = 0; i < m->n; i++) {
    model_id(a, 1 + 4L * i); model_id(b, 2 + 4L * i); model_id(c, 3 + 4L * i); model_id(d, 4 + 4L * i);
    buf_printf(o, "<dgm:pt modelId=\"%s\"><dgm:prSet/><dgm:spPr/><dgm:t><a:bodyPr/><a:lstStyle/><a:p><a:r><a:rPr lang=\"en-US\"/><a:t>", a);
    xesc(o, m->it[i].text);
    buf_adds(o, "</a:t></a:r></a:p></dgm:t></dgm:pt>");
    buf_printf(o, "<dgm:pt modelId=\"%s\" type=\"parTrans\" cxnId=\"%s\"><dgm:prSet/><dgm:spPr/>", c, b); empty_t(o); buf_adds(o, "</dgm:pt>");
    buf_printf(o, "<dgm:pt modelId=\"%s\" type=\"sibTrans\" cxnId=\"%s\"><dgm:prSet/><dgm:spPr/>", d, b); empty_t(o); buf_adds(o, "</dgm:pt>");
  }
  buf_adds(o, "</dgm:ptLst><dgm:cxnLst>");
  memset(ord, 0, sizeof ord);
  for (i = 0; i < m->n; i++) {
    int slot = par[i] < 0 ? 0 : par[i] + 1;
    model_id(a, 1 + 4L * i); model_id(b, 2 + 4L * i); model_id(c, 3 + 4L * i); model_id(d, 4 + 4L * i);
    if (par[i] < 0) strcpy(src, doc); else model_id(src, 1 + 4L * par[i]);
    buf_printf(o, "<dgm:cxn modelId=\"%s\" srcId=\"%s\" destId=\"%s\" srcOrd=\"%d\" destOrd=\"0\" parTransId=\"%s\" sibTransId=\"%s\"/>", b, src, a, ord[slot]++, c, d);
  }
  buf_adds(o, "</dgm:cxnLst><dgm:bg/><dgm:whole/><dgm:extLst><a:ext uri=\"http://schemas.microsoft.com/office/drawing/2008/diagram\">"
              "<dsp:dataModelExt xmlns:dsp=\"" NS_DSP "\" relId=\"");
  xesc(o, drawrel);
  buf_adds(o, "\" minVer=\"http://schemas.openxmlformats.org/drawingml/2006/diagram\"/></a:ext></dgm:extLst></dgm:dataModel>");
  free(par);
  return !o->oom;
}
/* a layout node: an algorithm with its parameters ("type=val;..."), a shape (NULL none), what text it shows */
static void lnode_open(Buf *o, const char *name, const char *style, const char *alg, const char *params, const char *shape, const char *presof)
{
  buf_printf(o, "<dgm:layoutNode name=\"%s\"", name);
  if (style) buf_printf(o, " styleLbl=\"%s\"", style);
  buf_adds(o, ">");
  if (!params || !params[0]) buf_printf(o, "<dgm:alg type=\"%s\"/>", alg);
  else {
    const char *p = params;
    buf_printf(o, "<dgm:alg type=\"%s\">", alg);
    while (*p) {
      const char *eq = strchr(p, '='), *end = strchr(p, ';');
      if (!end) end = p + strlen(p);
      if (eq && eq < end) buf_printf(o, "<dgm:param type=\"%.*s\" val=\"%.*s\"/>", (int)(eq - p), p, (int)(end - eq - 1), eq + 1);
      p = *end ? end + 1 : end;
    }
    buf_adds(o, "</dgm:alg>");
  }
  if (shape) buf_printf(o, "<dgm:shape type=\"%s\" xmlns:r=\"" NS_R "\" r:blip=\"\"><dgm:adjLst/></dgm:shape>", shape);
  else buf_adds(o, "<dgm:shape xmlns:r=\"" NS_R "\" r:blip=\"\"><dgm:adjLst/></dgm:shape>");
  if (presof) buf_printf(o, "<dgm:presOf %s/>", presof); else buf_adds(o, "<dgm:presOf/>");
  buf_adds(o, "<dgm:constrLst/><dgm:ruleLst/>");
}
#define ITEM_TEXT "axis=\"desc;self\" ptType=\"node;node\" st=\"1;1\" cnt=\"0;1\""
int sa_layout_xml(const SAModel *m, Buf *o)
{
  static const char *const shapes[A_N] = { "rect", "roundRect", "rect", "roundRect", "roundRect", "chevron", "ellipse", "rect", "ellipse", "rect", "roundRect", "roundRect",
    "ellipse", "ellipse", "ellipse", "roundRect", "roundRect", "rect", "trapezoid", "trapezoid", "roundRect" };
  int l = m->layout < A_N ? m->layout : 0;
  char id[120];
  const char *alg, *params;
  layout_id(m, id, sizeof id);
  switch (l) {
  case A_BLOCKS: case A_MATRIX: case A_TMATRIX: case A_GMATRIX: alg = "snake"; params = "grDir=tL;flowDir=row;contDir=sameDir"; break;
  case A_VBULLET: case A_VPROCESS: case A_TARGET: case A_PYRLIST: alg = "lin"; params = "linDir=fromT"; break;
  case A_HBULLET: case A_PROCESS: case A_CHEVRON: case A_LINVENN: alg = "lin"; params = "linDir=fromL;nodeVertAlign=mid"; break;
  case A_CYCLE: case A_TEXTCYCLE: case A_VENN: alg = "cycle"; params = "stAng=0;spanAng=360"; break;
  case A_RADIAL: alg = "cycle"; params = "stAng=0;spanAng=360;ctrShpMap=fNode"; break;
  case A_ORG: case A_HIER: alg = "hierChild"; params = "linDir=fromL;chAlign=t"; break;
  case A_HHIER: alg = "hierChild"; params = "linDir=fromT;chAlign=l"; break;
  case A_INVPYR: alg = "pyra"; params = "linDir=fromB"; break;
  default: alg = "pyra"; params = "linDir=fromT"; break;
  }
  buf_printf(o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<dgm:layoutDef xmlns:dgm=\"" NS_DGM "\" xmlns:a=\"" NS_A "\" xmlns:r=\"" NS_R "\" uniqueId=\"%s\">", id);
  buf_adds(o, "<dgm:title val=\""); xesc(o, layouts[l].name); buf_adds(o, "\"/><dgm:desc val=\""); xesc(o, layouts[l].desc);
  buf_printf(o, "\"/><dgm:catLst><dgm:cat type=\"%s\" pri=\"1000\"/></dgm:catLst>", cat_ids[layouts[l].cat]);
  lnode_open(o, "diagram", NULL, alg, params, NULL, NULL);
  if (l == A_ORG || l == A_HIER || l == A_HHIER) {
    buf_adds(o, "<dgm:forEach name=\"branches\" axis=\"ch\" ptType=\"node\">");
    lnode_open(o, "branch", NULL, "hierRoot", NULL, NULL, NULL);
    lnode_open(o, "item", "node1", "tx", NULL, shapes[l], "axis=\"self\""); buf_adds(o, "</dgm:layoutNode>");
    lnode_open(o, "below", NULL, "hierChild", l == A_HHIER ? "chAlign=l;linDir=fromT" : "chAlign=t", NULL, NULL);
    buf_adds(o, "<dgm:forEach name=\"joins\" axis=\"ch\" ptType=\"parTrans\">");
    lnode_open(o, "join", "connector", "conn", l == A_HHIER ? "dim=1D;connRout=bend;begPts=midR;endPts=midL" : "dim=1D;connRout=bend;begPts=bCtr;endPts=tCtr", "conn", "axis=\"self\"");
    buf_adds(o, "</dgm:layoutNode></dgm:forEach><dgm:forEach name=\"deeper\" ref=\"branches\"/></dgm:layoutNode></dgm:layoutNode></dgm:forEach>");
  } else if (l == A_RADIAL) {
    buf_adds(o, "<dgm:forEach name=\"middle\" axis=\"ch\" ptType=\"node\" cnt=\"1\">");
    lnode_open(o, "centre", "node1", "tx", NULL, shapes[l], "axis=\"self\"");
    buf_adds(o, "<dgm:forEach name=\"around\" axis=\"ch\" ptType=\"node\">");
    lnode_open(o, "spoke", "node1", "tx", NULL, shapes[l], ITEM_TEXT);
    buf_adds(o, "</dgm:layoutNode></dgm:forEach></dgm:layoutNode></dgm:forEach>");
  } else {
    if (l == A_PYRLIST) { lnode_open(o, "background", "bgShp", "sp", NULL, "triangle", NULL); buf_adds(o, "</dgm:layoutNode>"); }
    buf_adds(o, "<dgm:forEach name=\"items\" axis=\"ch\" ptType=\"node\">");
    lnode_open(o, "item", l == A_VENN || l == A_LINVENN ? "venn" : "node1", "tx", NULL, shapes[l], ITEM_TEXT);
    buf_adds(o, "</dgm:layoutNode>");
    if (l == A_PROCESS || l == A_VPROCESS || l == A_CYCLE || l == A_TEXTCYCLE) {
      buf_adds(o, "<dgm:forEach name=\"arrows\" axis=\"followSib\" ptType=\"sibTrans\" cnt=\"1\">");
      lnode_open(o, "arrow", "connector", "conn", "dim=2D", l == A_VPROCESS ? "downArrow" : "rightArrow", "axis=\"self\"");
      buf_adds(o, "</dgm:layoutNode></dgm:forEach>");
    }
    buf_adds(o, "</dgm:forEach>");
  }
  buf_adds(o, "</dgm:layoutNode></dgm:layoutDef>");
  return !o->oom;
}
static const char *const style_lbls[4] = { "node1", "connector", "venn", "bgShp" };
static void scheme_list(Buf *o, const char *tag, const char *meth, const SAModel *m, int light)
{
  int k;
  buf_printf(o, "<dgm:%s meth=\"%s\">", tag, meth);
  if (light) buf_adds(o, "<a:schemeClr val=\"lt1\"/>");
  else if (m->colors >= SA_COLORS - 1) for (k = 1; k <= 6; k++) buf_printf(o, "<a:schemeClr val=\"accent%d\"/>", k);
  else buf_printf(o, "<a:schemeClr val=\"accent%d\"/>", m->colors + 1);
  buf_printf(o, "</dgm:%s>", tag);
}
int sa_colors_xml(const SAModel *m, Buf *o)
{
  char id[120], cat[24];
  int i;
  colors_id(m, id, sizeof id, cat, sizeof cat);
  buf_printf(o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<dgm:colorsDef xmlns:dgm=\"" NS_DGM "\" xmlns:a=\"" NS_A "\" uniqueId=\"%s\"><dgm:title val=\"%s\"/><dgm:desc val=\"\"/>"
                "<dgm:catLst><dgm:cat type=\"%s\" pri=\"1000\"/></dgm:catLst>", id, sa_color_names[m->colors < SA_COLORS ? m->colors : 0], cat);
  for (i = 0; i < 4; i++) {
    buf_printf(o, "<dgm:styleLbl name=\"%s\">", style_lbls[i]);
    scheme_list(o, "fillClrLst", m->colors >= SA_COLORS - 1 ? "cycle" : "repeat", m, 0);
    scheme_list(o, "linClrLst", "repeat", m, 1);
    buf_adds(o, "<dgm:effectClrLst/><dgm:txLinClrLst/>");
    buf_printf(o, "<dgm:txFillClrLst meth=\"repeat\"><a:schemeClr val=\"%s\"/></dgm:txFillClrLst><dgm:txEffectClrLst/></dgm:styleLbl>", i == 2 ? "tx1" : "lt1");
  }
  buf_adds(o, "</dgm:colorsDef>");
  return !o->oom;
}
int sa_quickstyle_xml(const SAModel *m, Buf *o)
{
  int i, st = m->style < SA_STYLES ? m->style : 0;
  static const char scene[] = "<dgm:scene3d><a:camera prst=\"orthographicFront\"/><a:lightRig rig=\"threePt\" dir=\"t\"/></dgm:scene3d>";
  buf_printf(o, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<dgm:styleDef xmlns:dgm=\"" NS_DGM "\" xmlns:a=\"" NS_A "\" uniqueId=\"urn:microsoft.com/office/officeart/2005/8/quickstyle/%s\">"
                "<dgm:title val=\"%s\"/><dgm:desc val=\"\"/><dgm:catLst><dgm:cat type=\"simple\" pri=\"%d\"/></dgm:catLst>%s", qs_ids[st], sa_style_names[st], 10100 + 100 * st, scene);
  for (i = 0; i < 4; i++)
    buf_printf(o, "<dgm:styleLbl name=\"%s\">%s<dgm:sp3d/><dgm:txPr/><dgm:style><a:lnRef idx=\"%d\"><a:scrgbClr r=\"0\" g=\"0\" b=\"0\"/></a:lnRef><a:fillRef idx=\"1\"><a:scrgbClr r=\"0\" g=\"0\" b=\"0\"/></a:fillRef>"
                  "<a:effectRef idx=\"%d\"><a:scrgbClr r=\"0\" g=\"0\" b=\"0\"/></a:effectRef><a:fontRef idx=\"minor\"><a:schemeClr val=\"%s\"/></a:fontRef></dgm:style></dgm:styleLbl>",
               style_lbls[i], scene, st == 1 ? 3 : 2, st >= 2 ? st - 1 : 0, i == 2 ? "tx1" : "lt1");
  buf_adds(o, "</dgm:styleDef>");
  return !o->oom;
}
ObjPack *sa_pack_make(const SAModel *m, i32 cx, i32 cy, const ObjPack *old)
{
  ObjPack *pk = pack_new(PK_SMARTART);
  Buf b[5];
  int i, ok, k;
  static const char *const names[5] = { "word/diagrams/data1.xml", "word/diagrams/layout1.xml", "word/diagrams/quickStyle1.xml", "word/diagrams/colors1.xml", "word/diagrams/drawing1.xml" };
  static const char *const cts[5] = { CT_DATA, CT_LAYOUT, CT_STYLE, CT_COLORS, CT_DRAWING };
  static const char *const rts[5] = { RT_DATA, RT_LAYOUT, RT_STYLE, RT_COLORS, RT_DRAWING };
  static const u8 roles[5] = { RO_DM, RO_LO, RO_QS, RO_CS, RO_DRAWING };
  if (!pk) return NULL;
  if (old) { snprintf(pk->font, sizeof pk->font, "%s", old->font); if (old->theme) { pk->theme = old->theme; pk->theme->refs++; } }
  for (i = 0; i < 5; i++) buf_init(&b[i]);
  ok = sa_data_xml(m, DRAWREL, &b[0]) && sa_layout_xml(m, &b[1]) && sa_quickstyle_xml(m, &b[2]) && sa_colors_xml(m, &b[3]) && sa_drawing_xml(m, cx, cy, &b[4]);
  for (i = 0; i < 5 && ok; i++) ok = pack_add(pk, names[i], cts[i], rts[i], i == 4 ? DRAWREL : NULL, roles[i], -1, b[i].data, b[i].len) >= 0;
  if (ok && (k = pack_find(pk, names[4])) >= 0) pk->art = smartart_read((const char *)pk->p[k].data, pk->p[k].len, NULL);
  for (i = 0; i < 5; i++) buf_free(&b[i]);
  if (!ok) { pack_unref(pk); return NULL; }
  return pk;
}

/* ------------------------------------------------------------------ reading a SmartArt's model back */
typedef struct { char id[64]; u8 type; char *text; } RPt;          /* type: 0 node, 1 doc, 2 other */
typedef struct { char src[64], dst[64]; int ord; } RCx;
SAModel *sa_from_pack(const ObjPack *pk)
{
  const PackPart *dp = NULL;
  Xml x;
  RPt *pt = NULL; RCx *cx = NULL;
  int npt = 0, ncx = 0, i, k, intext = 0, cur = -1, docpt = -1, inpr = 0;
  char lo[160] = "", locat[40] = "", cs[160] = "", qs[160] = "";
  int rev = 0;
  Buf tb;
  SAModel *m;
  for (i = 0; pk && i < pk->np; i++) if (pk->p[i].role == RO_DM) dp = &pk->p[i];
  if (!dp || !(m = (SAModel *)calloc(1, sizeof *m))) return NULL;
  buf_init(&tb);
  xml_init(&x, (const char *)dp->data, dp->len);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START) {
      if (!strcmp(x.name, "pt") && npt < 4 * SA_MAX_ITEMS + 8) {
        const char *id = xml_attr(&x, "modelId"), *ty = xml_attr(&x, "type");
        RPt *n = (RPt *)realloc(pt, (size_t)(npt + 1) * sizeof(RPt));
        if (!n) break;
        pt = n; cur = npt++;
        memset(&pt[cur], 0, sizeof pt[cur]);
        snprintf(pt[cur].id, sizeof pt[cur].id, "%s", id ? id : "");
        pt[cur].type = (u8)(!ty || !strcmp(ty, "node") || !strcmp(ty, "asst") ? 0 : !strcmp(ty, "doc") ? 1 : 2);
        if (pt[cur].type == 1 && docpt < 0) docpt = cur;
        tb.len = 0; if (tb.data) tb.data[0] = 0;
      } else if (!strcmp(x.name, "prSet") && cur >= 0 && cur == docpt) {
        const char *a;
        if ((a = xml_attr(&x, "loTypeId"))) snprintf(lo, sizeof lo, "%s", a);
        if ((a = xml_attr(&x, "loCatId"))) snprintf(locat, sizeof locat, "%s", a);
        if ((a = xml_attr(&x, "csTypeId"))) snprintf(cs, sizeof cs, "%s", a);
        if ((a = xml_attr(&x, "qsTypeId"))) snprintf(qs, sizeof qs, "%s", a);
        inpr = 1;
      } else if (!strcmp(x.name, "dir") && inpr) { const char *v = xml_attr(&x, "val"); if (v && !strcmp(v, "rev")) rev = 1; }
      else if (!strcmp(x.name, "t") && cur >= 0) intext++;        /* dgm:t, then a:t inside it */ else if (!strcmp(x.name, "p") && intext && tb.len) buf_addc(&tb, ' ');
      else if (!strcmp(x.name, "cxn") && ncx < 4 * SA_MAX_ITEMS + 8) {
        const char *ty = xml_attr(&x, "type"), *s = xml_attr(&x, "srcId"), *d = xml_attr(&x, "destId"), *o = xml_attr(&x, "srcOrd");
        RCx *n;
        if ((ty && strcmp(ty, "parOf")) || !s || !d) continue;
        if (!(n = (RCx *)realloc(cx, (size_t)(ncx + 1) * sizeof(RCx)))) break;
        cx = n;
        snprintf(cx[ncx].src, sizeof cx[ncx].src, "%s", s); snprintf(cx[ncx].dst, sizeof cx[ncx].dst, "%s", d);
        cx[ncx].ord = o ? atoi(o) : 0;
        ncx++;
      }
      continue;
    }
    if (x.type == XT_TEXT && intext >= 2 && cur >= 0) { buf_add(&tb, x.text.data, x.text.len); continue; }
    if (x.type == XT_END) {
      if (!strcmp(x.name, "t") && intext) { intext--; if (!intext && cur >= 0 && !pt[cur].text) pt[cur].text = str_dup(tb.data ? tb.data : ""); }
      else if (!strcmp(x.name, "prSet")) inpr = 0;
      else if (!strcmp(x.name, "pt")) { if (cur >= 0 && !pt[cur].text) pt[cur].text = str_dup(tb.data ? tb.data : ""); cur = -1; intext = 0; }
    }
  }
  xml_free(&x);
  buf_free(&tb);
  /* the tree from the document point down, each point's children in their order */
  if (docpt >= 0) {
    int stack[SA_MAX_LEVEL + 2], sl[SA_MAX_LEVEL + 2], depth = 0, guard = 0;
    stack[0] = docpt; sl[0] = -1;
    /* depth-first: for the point on top, its next child after the order last taken */
    while (depth >= 0 && guard++ < 20000 && m->n < SA_MAX_ITEMS) {
      int best = -1, bo = 0, p = stack[depth];
      for (k = 0; k < ncx; k++) {
        if (strcmp(cx[k].src, pt[p].id) || cx[k].ord <= sl[depth]) continue;
        if (best < 0 || cx[k].ord < bo) { best = k; bo = cx[k].ord; }
      }
      if (best < 0) { depth--; continue; }
      sl[depth] = bo;
      for (i = 0; i < npt; i++) if (pt[i].type == 0 && !strcmp(pt[i].id, cx[best].dst)) break;
      if (i == npt) continue;
      if (ins(m, m->n, pt[i].text, depth) < 0) break;
      if (depth < SA_MAX_LEVEL) { depth++; stack[depth] = i; sl[depth] = -1; }
    }
  }
  for (i = 0; i < npt; i++) free(pt[i].text);
  free(pt); free(cx);
  /* its layout, colours, style */
  {
    const char *sl = strrchr(lo, '/');
    int found = 0;
    for (k = 0; sl && k < A_N; k++) if (!strcmp(sl + 1, layouts[k].id)) { m->layout = (u8)k; found = 1; break; }
    if (!found) {
      for (k = 0; k < SAC_N; k++) if (!strcmp(locat, cat_ids[k])) break;
      if (k < SAC_N) for (i = 0; i < A_N; i++) if (layouts[i].cat == k) { m->layout = (u8)i; break; }
    }
    if (strstr(cs, "colorful")) m->colors = SA_COLORS - 1;
    else if ((sl = strstr(cs, "/accent")) && sl[7] >= '1' && sl[7] <= '6') m->colors = (u8)(sl[7] - '1');
    else m->colors = 0;
    for (k = 0; k < SA_STYLES; k++) if ((sl = strrchr(qs, '/')) && !strcmp(sl + 1, qs_ids[k])) m->style = (u8)k;
    m->rtl = (u8)rev;
  }
  sa_normalise(m);
  return m;
}
