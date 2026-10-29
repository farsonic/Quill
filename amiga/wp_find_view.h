/*
 * wp_find_view.h - WP12's part of the page view, included by view.c (after line_x) so it can use the
 * view's own data and helpers: column (block) selection, several ranges selected at once (Select
 * Similar Formatting), Extend Selection (F8), Select Objects (dragging round floating objects),
 * Highlight All (Find's reading highlight) and pasting a fragment (Insert Spike). view.c calls in
 * from a few places: fv_command (its do_command), fv_quiet (refresh), fv_draw_line (draw_line),
 * fv_hidesel (the selection's drawing), fv_key (key), fv_click and fv_mouse (handle_event) and
 * fv_free (OM_DISPOSE). This file belongs to WP12.
 */
#include "wp_find.h"
#include "../core/findx.h"

/* (view.c's own, defined further on) */
static void refresh(struct Data *d, Object *obj, int edited);
static void ensure_laid(struct Data *d, Object *obj, u32 para);
static void caret_hide(struct Data *d, Object *obj);
static void select_word(struct Data *d);
static void delete_range(struct Data *d, Pos a, Pos b);
static void delete_sel(struct Data *d);
static int obj_hit(struct Data *d, Object *obj, LONG mx, LONG my, Pos *at, FloatBox *box);
static LONG obj_ref(Doc *doc, u32 pi, int img);
static u32 point_page(struct Data *d, Object *obj, LONG mx, LONG my, i32 *px, i32 *py);

enum { FVM_NONE, FVM_BLOCK, FVM_SIMILAR };
#define FV_MAXR 1024
#define FV_MAXHL 2000
typedef struct {
  int mode;                     /* FVM_ */
  Pos *r; u32 nr;               /* the ranges, as pairs (FV_MAXR of them at most) */
  u32 rver; Pos ranchor, rcaret; int rok;   /* what they were worked out for */
  int ext;                      /* Extend Selection: 0 off, 1 on, 2 word, 3 sentence, 4 paragraph, 5 section, 6 document */
  int quiet;                    /* a command done for each range: the view drawn once, after the last */
  FxPattern *hl; Pos *hlr; u32 nhl, hlver; int hlok;   /* Highlight All: the pattern and its matches (as of hlver) */
  int objmode, lasso, lasso_on; LONG lx0, ly0, lx1, ly1;   /* Select Objects, and the rectangle being dragged */
} FvState;

static FvState *fv(struct Data *d)
{
  FvState *s = (FvState *)d->wp12;
  if (!s && (s = (FvState *)calloc(1, sizeof *s))) {
    if (!(s->r = (Pos *)malloc(2 * FV_MAXR * sizeof(Pos)))) { free(s); return NULL; }
    d->wp12 = s;
  }
  return s;
}
static void fv_free(struct Data *d)
{
  FvState *s = (FvState *)d->wp12;
  if (!s) return;
  free(s->r); free(s->hl); free(s->hlr); free(s);
  d->wp12 = NULL;
}
static void fv_hint(struct Data *d, const char *text)
{
  if (d->tb && d->tb->statusbar) DoMethod(d->tb->statusbar, MUIM_Status_Hint, (ULONG)text);
}
/* the status bar's line for the modes on */
static void fv_show_mode(struct Data *d)
{
  FvState *s = (FvState *)d->wp12;
  static char b[96];
  if (!s) return;
  if (s->objmode) fv_hint(d, tr("Select Objects: drag round objects to choose them (Esc ends it)"));
  else if (s->mode == FVM_BLOCK) fv_hint(d, tr("Column selection: extend it with the cursor keys (Esc ends it)"));
  else if (s->ext) fv_hint(d, tr("Extend Selection: the cursor keys extend it, F8 selects more (Esc ends it)"));
  else if (s->mode == FVM_SIMILAR) { sprintf(b, tr("%lu places with this formatting selected"), (unsigned long)s->nr); fv_hint(d, b); }
  else fv_hint(d, "");
}
static int fv_quiet(struct Data *d) { return d->wp12 && ((FvState *)d->wp12)->quiet; }
/* the ranges now (a block worked out again when the caret, anchor or text changed); 0: none, and the
   mode is over (the selection moved away from the similar ranges) */
static u32 fv_ranges(struct Data *d)
{
  FvState *s = (FvState *)d->wp12;
  if (!s || s->mode == FVM_NONE) return 0;
  if (s->mode == FVM_BLOCK) {
    if (!s->rok || s->rver != d->doc->ver || pos_cmp(s->ranchor, d->anchor) || pos_cmp(s->rcaret, d->caret)) {
      s->nr = d->lay ? fx_block(d->lay, d->anchor, d->caret, s->r, FV_MAXR) : 0;
      s->rver = d->doc->ver; s->ranchor = d->anchor; s->rcaret = d->caret; s->rok = 1;
    }
    return s->nr;
  }
  if (s->rver != d->doc->ver || pos_cmp(s->ranchor, d->anchor) || pos_cmp(s->rcaret, d->caret)) { s->mode = FVM_NONE; s->nr = 0; fv_show_mode(d); return 0; }
  return s->nr;
}
/* a block is shown by its ranges instead of the selection */
static int fv_hidesel(struct Data *d) { return d->wp12 && ((FvState *)d->wp12)->mode == FVM_BLOCK; }
/* Highlight All's matches, found again when the text changed */
static u32 fv_highlights(struct Data *d)
{
  FvState *s = (FvState *)d->wp12;
  if (!s || !s->hl) return 0;
  if (!s->hlok || s->hlver != d->doc->ver) {
    if (!s->hlr && !(s->hlr = (Pos *)malloc(2 * FV_MAXHL * sizeof(Pos)))) return 0;
    s->nhl = fx_find_all(d->doc, s->hl, NULL, s->hlr, FV_MAXHL);
    s->hlver = d->doc->ver; s->hlok = 1;
  }
  return s->nhl;
}
/* the ranges (pairs, in order) on line ln of paragraph pc->para filled with pen */
static void fv_fill(struct Data *d, struct RastPort *rp, const Pos *r, u32 n, ParaLay *pl, Placed *pc, Line *ln, LONG lx, LONG top, LONG pen)
{
  u32 lo = 0, hi = n, i;
  u32 ls = ln->start, le = pc->line + 1 < pl->nlines ? pl->lines[pc->line + 1].start : pl->p->len;
  /* the first range ending in or after this paragraph */
  while (lo < hi) { u32 m = (lo + hi) / 2; if (r[2 * m + 1].para < pc->para) lo = m + 1; else hi = m; }
  for (i = lo; i < n && r[2 * i].para <= pc->para; i++) {
    u32 s = r[2 * i].para == pc->para ? r[2 * i].off : 0, e = r[2 * i + 1].para == pc->para ? r[2 * i + 1].off : pl->p->len;
    LONG x0, x1;
    if (s < ls) s = ls;
    if (e > le) e = le;
    if (e <= s) continue;
    x0 = lx + tx(d, line_x(d->lay, pl, ln, s)); x1 = lx + tx(d, line_x(d->lay, pl, ln, e));
    if (x1 > x0) { SetAPen(rp, pen); RectFill(rp, x0, top, x1 - 1, top + tx(d, ln->height) - 1); }
  }
}
/* behind a line's text: Highlight All's matches (yellow), then the block's or similar ranges */
static void fv_draw_line(struct Data *d, Object *obj, struct RastPort *rp, ParaLay *pl, Placed *pc, Line *ln, LONG lx, LONG top, int sel)
{
  FvState *s = (FvState *)d->wp12;
  u32 n;
  if (!s || pc->para >= d->doc->np || pl->p != d->doc->p[pc->para]) return;
  if ((n = fv_highlights(d))) fv_fill(d, rp, s->hlr, n, pl, pc, ln, lx, top, pen_bg(d, obj, 0xFFEE55));
  if (sel && (n = fv_ranges(d))) fv_fill(d, rp, s->r, n, pl, pc, ln, lx, top, d->pen_sel);
}

/* the ranges deleted (the last first), one undo step; the caret where the first began */
static void fv_delete(struct Data *d, u32 n)
{
  FvState *s = (FvState *)d->wp12;
  u32 i;
  doc_begin(d->doc, d->caret);
  for (i = n; i-- > 0; ) delete_range(d, s->r[2 * i], s->r[2 * i + 1]);
  doc_end(d->doc, s->r[0]);
  d->caret = d->anchor = pos_clamp(d->doc, s->r[0]);
  s->mode = FVM_NONE; s->nr = 0;
}
/* the ranges' text on the clipboard, a line each */
static void fv_copy(struct Data *d, u32 n)
{
  FvState *s = (FvState *)d->wp12;
  u32 len;
  char *t = fx_block_text(d->doc, s->r, n, &len), *l;
  if (!t) return;
  if ((l = (char *)malloc(len * 3 + 4))) { size_t m = utf8_to_latin1(t, len, l, len * 3 + 4); clip_write(l, (long)m); free(l); }
  free(t);
  fragment_free(d->frag); d->frag = NULL;
}
/* commands each range takes in turn */
static int fv_each(ULONG cmd)
{
  switch (cmd) {
  case CMD_BOLD: case CMD_ITALIC: case CMD_UNDERLINE: case CMD_STRIKE: case CMD_SUPER: case CMD_SUB: case CMD_CAPS:
  case CMD_SIZE: case CMD_FONT: case CMD_COLOR: case CMD_HIGHLIGHT: case CMD_GROW: case CMD_CLEARFMT: case CMD_CASE:
  case CMD_CHARSTYLE: case CMD_CHARFMT: case CMD_CBORDER: case CMD_ALIGN: case CMD_STYLE: case CMD_SPACING: case CMD_PARAFMT:
    return 1;
  }
  return 0;
}
/* commands that leave the modes alone (they don't act on the selection) */
static int fv_neutral(ULONG cmd)
{
  return cmd == CMD_REFRESH || cmd == CMD_MARKS || cmd == CMD_TGRID || cmd == CMD_PAGESETUP || cmd == CMD_DESIGN || cmd == CMD_HYPHENATE ||
         cmd == CMD_BRUSH;
}
/* the unit Extend Selection's level stands for, round the selection's start */
static void fv_extend_to(struct Data *d, Object *obj, int level)
{
  Pos lo = sel_lo(d);
  Doc *doc = d->doc;
  d->caret = d->anchor = lo;
  switch (level) {
  case 2: select_word(d); break;
  case 3: case 4: {
    Para *p = doc->p[lo.para];
    u32 a = lo.off, b = lo.off;
    if (level == 4) { a = 0; b = p->len; }
    else {
      while (a > 0 && !((p->t[a - 1] == ' ' || p->t[a - 1] == '\t') && a >= 2 && strchr(".!?", p->t[a - 2]))) a--;
      while (b < p->len && !strchr(".!?", p->t[b])) b++;
      while (b < p->len && strchr(".!?\"')", p->t[b])) b++;
      while (b < p->len && p->t[b] == ' ') b++;
    }
    d->anchor.off = a; d->caret.off = b;
    if (level == 4 && lo.para + 1 < doc->np) { d->caret.para = lo.para + 1; d->caret.off = 0; }
    break; }
  case 5: {
    u32 first = lo.para, last = doc_sect_end(doc, lo.para);
    while (first > 0 && !doc->p[first - 1]->pf.sect) first--;
    d->anchor.para = first; d->anchor.off = 0; d->caret.para = last; d->caret.off = doc->p[last]->len;
    break; }
  case 6: d->anchor = doc_start(doc); d->caret = doc_end_pos(doc); break;
  }
  ensure_laid(d, obj, d->caret.para);
}
/* WP12's commands, and every command while ranges are selected: 1 if done here */
static int fv_command(struct Data *d, Object *obj, struct MUIP_Quill_Command *m)
{
  FvState *s;
  u32 n, i;
  if (IS_FV_CMD(m->cmd)) {
    if (!(s = fv(d))) return 1;
    caret_hide(d, obj);
    switch (m->cmd) {
    case CMD_FV_EXTEND:
      if (!m->arg) s->ext = 0;
      else if (m->arg > 0) {
        if (!s->ext) s->ext = 1;
        else if (s->ext < 6) fv_extend_to(d, obj, ++s->ext);
      } else if (s->ext > 2) fv_extend_to(d, obj, --s->ext);
      else if (s->ext) { s->ext = 1; d->anchor = d->caret; }
      if (s->ext) s->mode = s->mode == FVM_SIMILAR ? FVM_NONE : s->mode;
      break;
    case CMD_FV_COLUMN:
      if (s->mode == FVM_BLOCK) { s->mode = FVM_NONE; d->anchor = d->caret; }
      else { s->mode = FVM_BLOCK; s->rok = 0; s->ext = 0; s->objmode = 0; }
      break;
    case CMD_FV_SIMILAR:
      n = fx_similar(d->doc, sel_lo(d), s->r, FV_MAXR);
      if (!n) { DisplayBeep(NULL); break; }
      s->mode = FVM_SIMILAR; s->nr = n; s->ext = 0;
      d->anchor = s->r[0]; d->caret = s->r[1];
      s->rver = d->doc->ver; s->ranchor = d->anchor; s->rcaret = d->caret;
      break;
    case CMD_FV_OBJECTS:
      s->objmode = !s->objmode;
      if (s->objmode) { s->mode = FVM_NONE; s->ext = 0; }
      break;
    case CMD_FV_HILITE:
      if (!m->arg) { free(s->hl); s->hl = NULL; s->nhl = 0; break; }
      if (!s->hl && !(s->hl = (FxPattern *)malloc(sizeof(FxPattern)))) break;
      memcpy(s->hl, (const FxPattern *)m->arg, sizeof(FxPattern));
      s->hlok = 0;
      break;
    case CMD_FV_PASTEFRAG:
      doc_begin(d->doc, d->caret);
      delete_sel(d);
      doc_paste(d->doc, &d->caret, (const Fragment *)m->arg, PASTE_KEEP);
      doc_end(d->doc, d->caret);
      d->anchor = d->caret; s->mode = FVM_NONE;
      fv_show_mode(d);
      refresh(d, obj, 1);
      return 1;
    case CMD_FV_INFO: {
      struct FvInfo *fi = (struct FvInfo *)m->arg;
      fi->ext = s->ext; fi->column = s->mode == FVM_BLOCK; fi->similar = s->mode == FVM_SIMILAR; fi->objects = s->objmode;
      fi->nranges = (int)fv_ranges(d); fi->highlights = (int)fv_highlights(d);
      return 1; }
    }
    fv_show_mode(d);
    refresh(d, obj, 0);
    MUI_Redraw(obj, MADF_DRAWOBJECT);        /* (the ranges and highlights are in the picture, not the caret) */
    return 1;
  }
  if (!(s = (FvState *)d->wp12) || s->quiet || fv_neutral(m->cmd)) return 0;
  if (s->ext && m->cmd != CMD_SELECT) { s->ext = 0; fv_show_mode(d); }
  if (!(n = fv_ranges(d))) return 0;
  if (m->cmd == CMD_COPY || m->cmd == CMD_CUT || m->cmd == CMD_DELETE) {
    caret_hide(d, obj);
    if (m->cmd != CMD_DELETE) fv_copy(d, n);
    if (m->cmd != CMD_COPY) fv_delete(d, n);
    fv_show_mode(d);
    refresh(d, obj, m->cmd != CMD_COPY);
    return 1;
  }
  if (fv_each(m->cmd)) {
    Pos a = d->anchor, c = d->caret;
    caret_hide(d, obj);
    s->quiet = 1;
    doc_begin(d->doc, c);
    for (i = 0; i < n; i++) {
      d->anchor = s->r[2 * i]; d->caret = s->r[2 * i + 1];
      DoMethod(obj, MUIM_Quill_Command, m->cmd, m->arg);
    }
    doc_end(d->doc, c);
    s->quiet = 0;
    d->anchor = a; d->caret = c;
    s->rver = d->doc->ver;                   /* (formatting moves no text: the ranges still hold) */
    if (s->mode == FVM_BLOCK) s->rok = 0;
    refresh(d, obj, 1);
    s->rver = d->doc->ver;
    return 1;
  }
  /* anything else: the ranges are given up, the selection collapsed so it isn't taken as one stretch */
  s->mode = FVM_NONE; s->nr = 0;
  d->anchor = d->caret;
  fv_show_mode(d);
  return 0;
}

/* a key before the view's own handling: Esc ends the modes; the cursor keys extend in Extend Selection
   and column selection; Backspace and Del delete the ranges; a character typed in Extend Selection
   extends to the next one like it. 1 if used (drawn here); *shift set to extend */
static int fv_key(struct Data *d, Object *obj, struct IntuiMessage *im, int *shift)
{
  FvState *s = (FvState *)d->wp12;
  UWORD code = im->Code;
  int moving = (code >= 0x4C && code <= 0x4F) || code == 0x70 || code == 0x71 || code == 0x48 || code == 0x49;
  u32 n;
  if (!s || (!s->ext && s->mode == FVM_NONE && !s->objmode)) return 0;
  if (code >= 0x50 && code <= 0x5F) return 0;                  /* function keys, Help: commands */
  if (code == 0x60 || code == 0x61 || code == 0x62 || code == 0x63 || code == 0x64 || code == 0x65 || code == 0x66 || code == 0x67) return 0;  /* qualifiers alone */
  if (code == 0x45) {                                           /* Esc */
    caret_hide(d, obj);
    s->ext = 0; s->objmode = 0;
    if (s->mode != FVM_NONE) { s->mode = FVM_NONE; s->nr = 0; d->anchor = d->caret; }
    fv_show_mode(d);
    refresh(d, obj, 0);
    MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 1;
  }
  if (moving) {
    if (s->ext || s->mode == FVM_BLOCK) { *shift = 1; return 0; }
    if (s->mode == FVM_SIMILAR) { s->mode = FVM_NONE; fv_show_mode(d); }
    return 0;
  }
  if ((code == 0x41 || code == 0x46) && (n = fv_ranges(d))) {
    caret_hide(d, obj);
    fv_delete(d, n);
    fv_show_mode(d);
    refresh(d, obj, 1);
    return 1;
  }
  if (s->ext && code != 0x41 && code != 0x46 && code != 0x44 && code != 0x43 && code != 0x42) {
    /* a character: the selection goes on to just past the next one like it */
    struct InputEvent ie;
    char buf[8];
    LONG k;
    memset(&ie, 0, sizeof ie);
    ie.ie_Class = IECLASS_RAWKEY; ie.ie_Code = code; ie.ie_Qualifier = im->Qualifier & ~IEQUALIFIER_CONTROL;
    ie.ie_EventAddress = (APTR *)*((ULONG *)im->IAddress);
    k = MapRawKey(&ie, (STRPTR)buf, sizeof buf, NULL);
    if (k == 1 && (u8)buf[0] >= 32 && (u8)buf[0] != 0x9B) {
      char u[4];
      int ul = utf8_put(u, (u8)buf[0]);
      Pos p = d->caret;
      u[ul] = 0;
      for (; p.para < d->doc->np; p.para++, p.off = 0) {
        char *hit = strstr(d->doc->p[p.para]->t + p.off, u);
        if (hit) { p.off = (u32)(hit - d->doc->p[p.para]->t) + (u32)ul; break; }
      }
      caret_hide(d, obj);
      if (p.para < d->doc->np) d->caret = p; else DisplayBeep(NULL);
      refresh(d, obj, 0);
      return 1;
    }
    return 1;
  }
  /* anything else ends the modes first (typing doesn't replace a whole stretch) */
  s->ext = 0;
  if (s->mode != FVM_NONE) { s->mode = FVM_NONE; s->nr = 0; d->anchor = d->caret; }
  fv_show_mode(d);
  return 0;
}
/* a click in the text: Shift is implied in Extend Selection; Alt starts a column selection */
static void fv_click(struct Data *d, struct IntuiMessage *im, int *shift)
{
  FvState *s = (FvState *)d->wp12;
  int alt = (im->Qualifier & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) != 0;
  if (alt && (s || (s = fv(d)))) { s->mode = FVM_BLOCK; s->rok = 0; s->ext = 0; fv_show_mode(d); return; }
  if (!s) return;
  if (s->ext) *shift = 1;
  else if (s->mode != FVM_NONE && !*shift) { s->mode = FVM_NONE; s->nr = 0; fv_show_mode(d); }
}
/* the lasso of Select Objects (XOR on the window) */
static void fv_lasso_draw(struct Data *d, Object *obj)
{
  FvState *s = (FvState *)d->wp12;
  struct RastPort *rp = _rp(obj);
  LONG x0 = s->lx0 < s->lx1 ? s->lx0 : s->lx1, x1 = s->lx0 < s->lx1 ? s->lx1 : s->lx0;
  LONG y0 = s->ly0 < s->ly1 ? s->ly0 : s->ly1, y1 = s->ly0 < s->ly1 ? s->ly1 : s->ly0;
  SetDrMd(rp, COMPLEMENT);
  Move(rp, x0, y0); Draw(rp, x1, y0); Draw(rp, x1, y1); Draw(rp, x0, y1); Draw(rp, x0, y0 + 1);
  SetDrMd(rp, JAM1);
  s->lasso_on = !s->lasso_on;
}
/* the mouse in Select Objects: a press off any object starts the lasso, letting go chooses the
   floating objects it touches. Returns MUI_EventHandlerRC_Eat when it used the event, else 0 */
static ULONG fv_mouse(struct Data *d, Object *obj, struct IntuiMessage *im)
{
  FvState *s = (FvState *)d->wp12;
  if (!s || (!s->objmode && !s->lasso)) return 0;
  if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN && !s->lasso) {
    Pos at;
    FloatBox fb;
    if (im->MouseX < _mleft(obj) || im->MouseX > _mright(obj) || im->MouseY < _mtop(obj) || im->MouseY > _mbottom(obj)) return 0;
    if (obj_hit(d, obj, im->MouseX, im->MouseY, &at, &fb) >= 0) return 0;      /* an object: as usual */
    caret_hide(d, obj);
    s->lasso = 1; s->lasso_on = 0;
    s->lx0 = s->lx1 = im->MouseX; s->ly0 = s->ly1 = im->MouseY;
    DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
    d->ehn.ehn_Events |= IDCMP_MOUSEMOVE;
    DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
    return MUI_EventHandlerRC_Eat;
  }
  if (!s->lasso) return 0;
  if (im->Class == IDCMP_MOUSEMOVE) {
    if (s->lasso_on) fv_lasso_draw(d, obj);
    s->lx1 = im->MouseX; s->ly1 = im->MouseY;
    fv_lasso_draw(d, obj);
    return MUI_EventHandlerRC_Eat;
  }
  if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTUP) {
    i32 ax, ay, bx, by, t;
    u32 pa, pb, k;
    int first = 1;
    if (s->lasso_on) fv_lasso_draw(d, obj);
    s->lasso = 0;
    DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
    d->ehn.ehn_Events &= ~IDCMP_MOUSEMOVE;
    DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
    if (!d->lay) return MUI_EventHandlerRC_Eat;
    pa = point_page(d, obj, s->lx0, s->ly0, &ax, &ay);
    pb = point_page(d, obj, s->lx1, s->ly1, &bx, &by);
    if (pa != pb) { if (pb > pa) { by = 0x7FFFFFF; } else { ay = 0; } }
    if (ax > bx) { t = ax; ax = bx; bx = t; }
    if (ay > by) { t = ay; ay = by; by = t; }
    d->nmsel = 0;
    for (k = 0; k < d->lay->nfloats; k++) {
      const FloatBox *f = &d->lay->floats[k];
      LONG off;
      Pos at;
      if (f->page != pa || f->x > bx || f->x + f->w < ax || f->y > by || f->y + f->h < ay) continue;
      if ((off = obj_ref(d->doc, f->para, f->img)) < 0) continue;
      at.para = f->para; at.off = (u32)off;
      if (first) { d->anchor = at; d->caret = at; d->caret.off += 4; first = 0; }
      if (d->nmsel < 16) d->msel[d->nmsel++] = at;
    }
    if (d->nmsel < 2) d->nmsel = 0;
    if (first) DisplayBeep(NULL);
    refresh(d, obj, 0);
    MUI_Redraw(obj, MADF_DRAWOBJECT);
    return MUI_EventHandlerRC_Eat;
  }
  return 0;
}
