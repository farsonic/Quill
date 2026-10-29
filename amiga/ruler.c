/*
 * ruler.c - the ruler above the page, a MUI custom class (a subclass of Area).
 *
 * It shows the page's margins and the caret paragraph's indents and tab stops (in a table, the
 * cell's text area), as the view tells it with MUIM_Ruler_Set. The indent markers can be dragged
 * (the first-line triangle at the top, the left and right ones at the bottom); a click on the lower
 * half adds a tab stop, and a tab stop dragged off the ruler goes. In a table, the column edges show
 * as small blocks that drag the columns wider or narrower. Changes go to the view as commands, so
 * they are undoable like any other.
 *
 * Made with MUIA_Ruler_Vertical, it is the ruler beside the page instead: the caret's page's top and
 * bottom margins, as MUIM_Ruler_SetV tells it. Both are drawn flat, in WordCraft's colours.
 */
#include <string.h>
#include <stdio.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "quill.h"
#include "theme.h"
#include "lang.h"

/* a ruler's instance data (MUI keeps one per object, found with INST_DATA): the view's last
   MUIM_Ruler_Set in r, its tab arrays copied into tabs, kinds and leads so they outlive the
   message, or the vertical ruler's MUIM_Ruler_SetV in v. shown: between MUIM_Show and MUIM_Hide
   (events are ignored otherwise); valid: a Set has come, so there is something to draw */
struct RData {
  struct MUIP_Ruler_Set r;
  struct MUIP_Ruler_SetV v;
  int vertical;
  int shown;
  i32 tabs[8];
  u8 kinds[8], leads[8];
  int valid;
  int drag;                 /* 0 none, 1 first line, 2 left, 3 right, 10 + k: tab k, 100 + k: table column edge k */
  struct MUI_EventHandlerNode ehn;
};
static struct MUI_CustomClass *rcls;
static int tab_choice;              /* the kind of tab stop a click on the ruler makes (the selector at the corner) */
/* the height of the box at the top of the vertical ruler (the tab kind chooser), level with the
   horizontal ruler beside it */
static LONG ruler_h(void) { return 13 + 6; }

/* twips to pixels at the view's zoom, and back (a zoom of 0, before the first Set, taken as 100) */
static LONG rx(struct RData *d, i32 t) { return (LONG)((t * d->r.zoom) / 2000); }
static i32 rt(struct RData *d, LONG px) { return (i32)((px * 2000) / (d->r.zoom ? d->r.zoom : 100)); }
/* t (twips) rounded to the nearest ruler step, as Word snaps a dragged marker */
static i32 snap(struct RData *d, i32 t)
{
  i32 q = d->r.metric ? 142 : 180;          /* a quarter centimetre, an eighth of an inch */
  return t < 0 ? -((-t + q / 2) / q) * q : ((t + q / 2) / q) * q;
}
/* a small filled triangle with its point at x, y, pointing up or down (marker draws the indent
   markers now) */
static void tri(struct RastPort *rp, LONG x, LONG y, int up)
{
  int i;
  for (i = 0; i < 4; i++) { Move(rp, x - i, up ? y + i : y - i); Draw(rp, x + i, up ? y + i : y - i); }
}
/* a number at the tick (centred on x, or on y for the vertical ruler) */
static void tick_number(struct RastPort *rp, struct TextFont *tf, LONG x, LONG y, LONG n)
{
  char num[8];
  LONG tl;
  sprintf(num, "%ld", (long)(n < 0 ? -n : n));
  tl = theme_textlen(rp, tf, num);
  theme_text(rp, tf, x - tl / 2, y - tf->tf_YSize / 2, num, tpen(T_RULERTICK));
}
/* the scale along one axis: a from..b the window range, o the text area's start, len its length,
   p0..p1 the page; across: c0..c1 the band it is drawn in */
static void scale(struct RData *d, struct RastPort *rp, int vert, LONG a, LONG b, LONG o, i32 len, LONG p0, LONG p1, LONG c0, LONG c1, int metric)
{
  LONG k, unit = metric ? 567 : 1440, nsub = metric ? 4 : 8, mid = (c0 + c1) / 2, e = o + rx(d, len) - 1;
  struct TextFont *tf = theme_small();
  /* the page in the margin colour, the text area white */
  LONG q0 = p0 > a ? p0 : a, q1 = p1 < b ? p1 : b;
  if (q1 > q0) { if (vert) theme_fill(rp, c0, q0, c1, q1, tpen(T_RULERMARGIN)); else theme_fill(rp, q0, c0, q1, c1, tpen(T_RULERMARGIN)); }
  q0 = o > a ? o : a; q1 = e < b ? e : b;
  if (q1 > q0) { if (vert) theme_fill(rp, c0, q0, c1, q1, tpen(T_RULER)); else theme_fill(rp, q0, c0, q1, c1, tpen(T_RULER)); }
  SetAPen(rp, tpen(T_RULERTICK));
  for (k = -((rt(d, o - p0) / unit) + 1) * nsub; ; k++) {
    LONG z = o + rx(d, (i32)(k * unit / nsub));
    if (z > b || z > p1) break;
    if (z < a || z < p0) continue;
    if (k % nsub == 0) {
      if (k && rx(d, unit) > 14) { if (vert) tick_number(rp, tf, mid, z, k / nsub); else tick_number(rp, tf, z, mid, k / nsub); }
    } else if (k % (nsub / 2) == 0) {
      if (vert) { Move(rp, mid - 2, z); Draw(rp, mid + 2, z); } else { Move(rp, z, mid - 2); Draw(rp, z, mid + 2); }
    } else if (rx(d, unit / nsub) >= 4) {
      if (vert) { Move(rp, mid, z); Draw(rp, mid, z); } else { Move(rp, z, mid); Draw(rp, z, mid); }
    }
  }
}
/* an indent marker: a small house shape, pointing down (first line) or up */
static void marker(struct RastPort *rp, LONG x, LONG y, int up)
{
  int i;
  SetAPen(rp, tpen(T_WHITE));
  for (i = 0; i < 4; i++) { Move(rp, x - i, up ? y + i : y - i); Draw(rp, x + i, up ? y + i : y - i); }
  SetAPen(rp, tpen(T_DIM));
  Move(rp, x - 4, up ? y + 4 : y - 4); Draw(rp, x, y); Draw(rp, x + 4, up ? y + 4 : y - 4);
  Move(rp, x - 4, up ? y + 4 : y - 4); Draw(rp, x + 4, up ? y + 4 : y - 4);
}
/* a tab stop's mark, its foot at x, y: an L for left, reversed for right, an upside-down T for
   centred, with a dot for decimal */
static void tab_glyph(struct RastPort *rp, int kind, LONG x, LONG y)
{
  SetAPen(rp, tpen(T_TEXT));
  switch (kind) {
  case TAB_RIGHT: Move(rp, x, y - 4); Draw(rp, x, y); Draw(rp, x - 4, y); Move(rp, x - 1, y - 4); Draw(rp, x - 1, y - 1); Draw(rp, x - 4, y - 1); break;
  case TAB_CENTER: case TAB_DECIMAL:
    Move(rp, x, y - 4); Draw(rp, x, y); Move(rp, x - 3, y); Draw(rp, x + 3, y); Move(rp, x - 3, y - 1); Draw(rp, x + 3, y - 1);
    if (kind == TAB_DECIMAL) { WritePixel(rp, x + 2, y - 3); WritePixel(rp, x + 3, y - 3); }
    break;
  default: Move(rp, x, y - 4); Draw(rp, x, y); Draw(rp, x + 4, y); Move(rp, x + 1, y - 4); Draw(rp, x + 1, y - 1); Draw(rp, x + 4, y - 1); break;
  }
}
static void draw_marks(struct RData *d, Object *obj, LONG l, LONG t, LONG w, LONG h);
/* MUIM_Draw's work: the whole ruler painted afresh. The vertical one also draws the tab kind box
   at its top; the horizontal one is clipped to the object (MUI_AddClipping), as the page and its
   markers can reach past the ruler's ends when the view is scrolled sideways */
static void draw(struct RData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG l = _mleft(obj), t = _mtop(obj), w = _mwidth(obj), h = _mheight(obj), x, y, k, x0;
  struct MUIP_Ruler_Set *r = &d->r;
  SetDrMd(rp, JAM1);
  theme_fill(rp, l, t, l + w - 1, t + h - 1, tpen(T_CANVAS));
  if (d->vertical) {
    struct MUIP_Ruler_SetV *v = &d->v;
    if (!v->zoom) return;
    d->r.zoom = v->zoom;
    scale(d, rp, 1, t + ruler_h(), t + h - 1, v->y0 + rx(d, v->mtop), v->pageh - v->mtop - v->mbottom, v->y0, v->y0 + rx(d, v->pageh) - 1, l + 2, l + w - 3, v->metric);
    /* the corner beside the ruler: which kind of tab stop a click makes */
    theme_fill(rp, l, t, l + w - 1, t + ruler_h() - 1, tpen(T_CANVAS));
    theme_fill(rp, l + 1, t + 2, l + w - 3, t + ruler_h() - 3, tpen(T_WHITE));
    theme_rect(rp, l + 1, t + 2, l + w - 3, t + ruler_h() - 3, tpen(T_BORDER2));
    tab_glyph(rp, tab_choice, l + w / 2 - (tab_choice == TAB_LEFT ? 2 : tab_choice == TAB_RIGHT ? -2 : 0), t + ruler_h() - 7);
    SetAPen(rp, tpen(T_BORDER));
    Move(rp, l + w - 1, t + ruler_h()); Draw(rp, l + w - 1, t + h - 1);
    return;
  }
  if (!d->valid) return;
  {
    APTR clip = MUI_AddClipping(muiRenderInfo(obj), (WORD)l, (WORD)t, (WORD)w, (WORD)h);
    draw_marks(d, obj, l, t, w, h);
    MUI_RemoveClipping(muiRenderInfo(obj), clip);
  }
}
/* the horizontal ruler's scale, indents and tabs */
static void draw_marks(struct RData *d, Object *obj, LONG l, LONG t, LONG w, LONG h)
{
  struct RastPort *rp = _rp(obj);
  struct MUIP_Ruler_Set *r = &d->r;
  LONG x, y, k, x0;
  x0 = r->x0;
  scale(d, rp, 0, l, l + w - 1, x0, r->width, r->page0, r->page0 + r->pagew - 1, t + 2, t + h - 3, r->metric);
  /* indents and tabs */
  marker(rp, x0 + rx(d, r->left + r->first), t + 6, 0);
  marker(rp, x0 + rx(d, r->left), t + h - 7, 1);
  marker(rp, x0 + rx(d, r->width - r->right), t + h - 7, 1);
  for (k = 0; k < 8 && d->tabs[k]; k++) {
    if (d->tabs[k] < 0) continue;
    x = x0 + rx(d, r->left + d->tabs[k]);
    y = t + h - 4;
    tab_glyph(rp, d->kinds[k], x, y);
  }
  /* a table's column edges: grey blocks with a grid in them */
  for (k = 0; k < r->ncolx; k++) {
    x = r->tblx0 + rx(d, r->colx[k]);
    theme_fill(rp, x - 3, t + 3, x + 3, t + h - 4, tpen(T_RULERMARGIN));
    theme_rect(rp, x - 3, t + 3, x + 3, t + h - 4, tpen(T_DIM));
    SetAPen(rp, tpen(T_DIM));
    Move(rp, x, t + 4); Draw(rp, x, t + h - 5);
  }
  SetAPen(rp, tpen(T_BORDER));
  Move(rp, l, t + h - 1); Draw(rp, l + w - 1, t + h - 1);
}
/* the drag just finished, sent to the view as a command so it is undoable: a table column edge
   (CMD_COLEDGE), the tab stops (CMD_TABS: in order, those dragged off left out) or the three
   indents (CMD_INDENTS) */
static void send(struct RData *d)
{
  if (!d->r.view) return;
  if (d->drag >= 100) {
    struct QColEdge q;
    q.table = d->r.table; q.edge = d->drag - 100; q.x = d->r.colx[q.edge];
    DoMethod(d->r.view, MUIM_Quill_Command, CMD_COLEDGE, (ULONG)&q);
  } else if (d->drag >= 10) {
    struct QTabs q;
    int i, n = 0;
    memset(&q, 0, sizeof q);
    for (i = 0; i < 8; i++) if (d->tabs[i] > 0) { q.pos[n] = d->tabs[i]; q.kind[n] = d->kinds[i]; q.lead[n] = d->leads[i]; n++; }
    /* in order */
    for (i = 1; i < n; i++) {
      int j = i;
      while (j > 0 && q.pos[j - 1] > q.pos[j]) {
        i32 x = q.pos[j]; u8 kk = q.kind[j], ll = q.lead[j];
        q.pos[j] = q.pos[j - 1]; q.kind[j] = q.kind[j - 1]; q.lead[j] = q.lead[j - 1];
        q.pos[j - 1] = x; q.kind[j - 1] = kk; q.lead[j - 1] = ll;
        j--;
      }
    }
    DoMethod(d->r.view, MUIM_Quill_Command, CMD_TABS, (ULONG)&q);
  } else {
    i32 v[3];
    v[0] = d->r.left; v[1] = d->r.first; v[2] = d->r.right;
    DoMethod(d->r.view, MUIM_Quill_Command, CMD_INDENTS, (ULONG)v);
  }
}
/* the marker being dragged moved to the pointer at mx, my (window pixels), snapped, and the ruler
   redrawn; nothing goes to the view until the button is let go. A column edge stays at least 240
   twips (a sixth of an inch) from its neighbours, and the left indent doesn't go below 0 */
static void dragto(struct RData *d, LONG mx, LONG my, Object *obj)
{
  i32 t = snap(d, rt(d, mx - d->r.x0));
  struct MUIP_Ruler_Set *r = &d->r;
  if (d->drag >= 100) {
    int k = d->drag - 100;
    i32 c = snap(d, rt(d, mx - r->tblx0));
    if (c < r->colx[k - 1] + 240) c = r->colx[k - 1] + 240;
    if (k + 1 < r->ncolx && c > r->colx[k + 1] - 240) c = r->colx[k + 1] - 240;
    r->colx[k] = c;
  }
  else if (d->drag == 1) r->first = t - r->left;
  else if (d->drag == 2) { i32 abs1 = r->left + r->first; r->left = t < 0 ? 0 : t; r->first = abs1 - r->left; }
  else if (d->drag == 3) r->right = r->width - t;
  else if (d->drag >= 10) {
    int k = d->drag - 10;
    /* off the ruler: the tab stop goes */
    d->tabs[k] = (my < _top(obj) - 8 || my > _bottom(obj) + 8 || t - r->left <= 0) ? -1 : t - r->left;
  }
  MUI_Redraw(obj, MADF_DRAWOBJECT);
}
/* MUIM_HandleEvent: the IntuiMessages the window hands the ruler's event handler. A press picks
   the marker near the pointer (or makes a new tab stop of the chosen kind) and starts a drag. Mouse
   moves are asked for only while dragging, by removing the handler and adding it again with
   IDCMP_MOUSEMOVE, so the ruler isn't woken by every move. MUI_EventHandlerRC_Eat keeps an event
   from the other objects; 0 lets it go on. On the vertical ruler a click on its top box changes the
   kind of tab stop a click makes */
static ULONG events(struct IClass *cl, Object *obj, struct MUIP_HandleEvent *m)
{
  struct RData *d = INST_DATA(cl, obj);
  struct IntuiMessage *im = m->imsg;
  if (!im || !d->shown) return 0;
  if (d->vertical) {
    if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN && im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj) &&
        im->MouseY >= _mtop(obj) && im->MouseY < _mtop(obj) + ruler_h()) {
      tab_choice = (tab_choice + 1) % 4;
      MUI_Redraw(obj, MADF_DRAWOBJECT);
      return MUI_EventHandlerRC_Eat;
    }
    return 0;
  }
  if (!d->valid) return 0;
  if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN && im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj) && im->MouseY >= _mtop(obj) && im->MouseY <= _mbottom(obj)) {
    LONG mx = im->MouseX, my = im->MouseY, best = 6, x;
    int k;
    struct MUIP_Ruler_Set *r = &d->r;
    int top = my < _mtop(obj) + _mheight(obj) / 2;
    d->drag = 0;
    for (k = 1; k < r->ncolx; k++) { x = r->tblx0 + rx(d, r->colx[k]); if (mx - x <= 3 && x - mx <= 3) d->drag = 100 + k; }
    if (d->drag) ;
    else if (top) { x = r->x0 + rx(d, r->left + r->first); if (mx - x < best && x - mx < best) d->drag = 1; }
    else {
      for (k = 0; k < 8 && d->tabs[k]; k++) { x = r->x0 + rx(d, r->left + d->tabs[k]); if (mx - x < 4 && x - mx < 4) { d->drag = 10 + k; best = 0; } }
      if (!d->drag) {
        x = r->x0 + rx(d, r->left); if (mx - x < best && x - mx < best) { d->drag = 2; best = mx - x < 0 ? x - mx : mx - x; }
        x = r->x0 + rx(d, r->width - r->right); if (mx - x < best && x - mx < best) d->drag = 3;
      }
      if (!d->drag && mx > r->x0 && mx < r->x0 + rx(d, r->width)) {       /* a new tab stop */
        for (k = 0; k < 8 && d->tabs[k]; k++) ;
        if (k < 8) { d->tabs[k] = 1; d->kinds[k] = (u8)tab_choice; d->leads[k] = LEAD_NONE; d->drag = 10 + k; }
      }
    }
    if (d->drag) {
      dragto(d, mx, my, obj);
      DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
      d->ehn.ehn_Events |= IDCMP_MOUSEMOVE;
      DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
    }
    return MUI_EventHandlerRC_Eat;
  }
  if (im->Class == IDCMP_MOUSEMOVE && d->drag) { dragto(d, im->MouseX, im->MouseY, obj); return MUI_EventHandlerRC_Eat; }
  if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTUP && d->drag) {
    DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
    d->ehn.ehn_Events &= ~IDCMP_MOUSEMOVE;
    DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
    send(d);
    d->drag = 0;
    return MUI_EventHandlerRC_Eat;
  }
  return 0;
}
/* the bubble for the part of the ruler under the pointer: which marker it is and what dragging it
   does, or (anywhere else on the text's width) that a click makes a tab stop */
static const char *ruler_help(struct RData *d, Object *obj, LONG mx, LONG my)
{
  static const char *const kinds[4] = { "Left tab", "Centre tab", "Right tab", "Decimal tab" };
  struct MUIP_Ruler_Set *r = &d->r;
  LONG x;
  int k;
  if (d->vertical) {
    if (my < _mtop(obj) + ruler_h())
      return theme_bubble(tr(kinds[tab_choice & 3]), tr("The kind of tab stop a click on the ruler makes. Click here to change it: left, centre, right or decimal."));
    return NULL;
  }
  if (!d->valid) return NULL;
  for (k = 1; k < r->ncolx; k++) { x = r->tblx0 + rx(d, r->colx[k]); if (mx - x <= 3 && x - mx <= 3) return theme_bubble(tr("Column edge"), tr("Drag to make the table's columns wider or narrower.")); }
  if (my < _mtop(obj) + _mheight(obj) / 2) {
    x = r->x0 + rx(d, r->left + r->first);
    if (mx - x < 6 && x - mx < 6) return theme_bubble(tr("First Line Indent"), tr("Drag to move the first line of the paragraph in or out on its own."));
  } else {
    for (k = 0; k < 8 && d->tabs[k]; k++) { x = r->x0 + rx(d, r->left + d->tabs[k]); if (mx - x < 4 && x - mx < 4) return theme_bubble(tr("Tab stop"), tr("Drag to move it; drag it off the ruler to take it away.")); }
    x = r->x0 + rx(d, r->left); if (mx - x < 6 && x - mx < 6) return theme_bubble(tr("Left Indent"), tr("Drag to move the paragraph's left edge in or out (the first line moves with it)."));
    x = r->x0 + rx(d, r->width - r->right); if (mx - x < 6 && x - mx < 6) return theme_bubble(tr("Right Indent"), tr("Drag to move the paragraph's right edge."));
  }
  if (mx > r->x0 && mx < r->x0 + rx(d, r->width)) return theme_bubble(tr("Ruler"), tr("Click to put a tab stop here, of the kind shown at the corner. The grey ends are the margins."));
  return NULL;
}
/* the ruler class's dispatcher. MUIM_AskMinMax gives its size (a fixed height and any width, or
   the other way round for the vertical one). MUIM_Setup and MUIM_Cleanup bracket the time it has a
   window and screen, so the theme's pens and fonts and the event handler are taken and let go
   there. MUIM_Ruler_Set and SetV take the view's figures and redraw only if something changed, as
   the view sends them every time the caret moves; a Set during a drag is ignored */
static ULONG rdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct RData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS;
    d->vertical = (int)GetTagData(MUIA_Ruler_Vertical, FALSE, ((struct opSet *)msg)->ops_AttrList);
    return (ULONG)obj;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    LONG fh = 13;
    DoSuperMethodA(cl, obj, msg);
    d = INST_DATA(cl, obj);
    if (d->vertical) {
      m->MinMaxInfo->MinWidth += fh + 2; m->MinMaxInfo->DefWidth += fh + 2; m->MinMaxInfo->MaxWidth += fh + 2;
      m->MinMaxInfo->MinHeight += 40; m->MinMaxInfo->DefHeight += 300; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
      return 0;
    }
    m->MinMaxInfo->MinWidth += 60; m->MinMaxInfo->DefWidth += 400; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += fh + 6; m->MinMaxInfo->DefHeight += fh + 6; m->MinMaxInfo->MaxHeight += fh + 6;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct RData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup:
    theme_cleanup(obj);
    break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & MADF_DRAWOBJECT) draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Show: ((struct RData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct RData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_HandleEvent:
    return events(cl, obj, (struct MUIP_HandleEvent *)msg);
  case MUIM_CreateShortHelp: {
    struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
    return (ULONG)ruler_help(INST_DATA(cl, obj), obj, m->mx, m->my);
  }
  case MUIM_DeleteShortHelp: return 0;
  case MUIM_Ruler_SetV: {
    struct MUIP_Ruler_SetV *m = (struct MUIP_Ruler_SetV *)msg;
    d = INST_DATA(cl, obj);
    if (!memcmp(&d->v, m, sizeof *m)) return 0;
    d->v = *m;
    if (_win(obj)) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  }
  case MUIM_Ruler_Set: {
    struct MUIP_Ruler_Set *m = (struct MUIP_Ruler_Set *)msg;
    int same;
    d = INST_DATA(cl, obj);
    if (d->drag) return 0;
    same = d->valid && !memcmp(&d->r.x0, &m->x0, (char *)&m->tabs - (char *)&m->x0) && !memcmp(d->tabs, m->tabs, sizeof d->tabs) &&
           (!m->tabkind || !memcmp(d->kinds, m->tabkind, sizeof d->kinds)) &&
           d->r.page0 == m->page0 && d->r.pagew == m->pagew && d->r.metric == m->metric &&
           d->r.ncolx == m->ncolx && d->r.tblx0 == m->tblx0 && !memcmp(d->r.colx, m->colx, sizeof m->colx);
    d->r = *m;
    memcpy(d->tabs, m->tabs, sizeof d->tabs);
    if (m->tabkind) memcpy(d->kinds, m->tabkind, sizeof d->kinds); else memset(d->kinds, 0, sizeof d->kinds);
    if (m->tablead) memcpy(d->leads, m->tablead, sizeof d->leads); else memset(d->leads, 0, sizeof d->leads);
    d->r.tabs = d->tabs; d->r.tabkind = d->kinds; d->r.tablead = d->leads;
    d->valid = 1;
    if (!same && _win(obj)) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
/* the class, made on first use; NULL if MUI couldn't make it */
struct MUI_CustomClass *ruler_class(void)
{
  if (!rcls) rcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct RData), (APTR)rdispatch);
  return rcls;
}
/* at the end, once every ruler has been disposed of */
void ruler_class_free(void)
{
  if (rcls) { MUI_DeleteCustomClass(rcls); rcls = NULL; }
}
