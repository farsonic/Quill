/*
 * wp_tables2.c - WP21b Table drawing and cell options (docs/wordcraft-gap.md). This file belongs to WP21b: only that
 * package edits it. Its commands are ID_WP21B_FIRST .. ID_WP21B_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * Draw Table and the Eraser (strokes on the page, which view.c hands to wp21_stroke while the tool is
 * on; Esc puts it away), Delete Cells, the 9-way cell alignment, text direction in cells, a cell's own
 * margins (Cell Options), a table's alternative text and Table Positioning (a floating table with the
 * text round it). The work itself is in core/table.c and core/tbldraw.c.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "lang.h"
#include "wp_tables.h"
#include "../core/table.h"
#include "../core/tbldraw.h"
#include <libraries/iffparse.h>          /* MAKE_ID */

enum { ID_TB_DRAWTABLE = ID_WP21B_FIRST, ID_TB_ERASER, ID_TB_TOOLOFF, ID_TB_DELCELLS, ID_TB_DELCELLS_OK,
       ID_TB_CELLALIGN, ID_TB_AL_FIRST, ID_TB_AL_LAST = ID_TB_AL_FIRST + 8,
       ID_TB_TEXTDIR, ID_TB_TD_FIRST, ID_TB_TD_LAST = ID_TB_TD_FIRST + 2,
       ID_TB_CELLMAR, ID_TB_CELLMAR_OK, ID_TB_ALTTEXT, ID_TB_ALTTEXT_OK, ID_TB_TPOS, ID_TB_TPOS_OK };

/* ---- what WP21a and view.c share (wp_tables.h) */
Pos wp21_caret, wp21_anchor;
int wp21_intable;
static int tool;
int wp21_tool(void) { return tool; }
void wp21_set_tool(int t) { tool = tool == t ? WP21_NONE : t; wp_refresh(); }
int wp21_stroke(Doc *d, Layout *L, u32 page, i32 x0, i32 y0, i32 x1, i32 y1, Pos *caret)
{
  switch (tool) {
  case WP21_DRAW: { const struct QBorders *p = wp_pen(); return tbl_draw_stroke(d, L, page, x0, y0, x1, y1, p->style, p->width, p->color, caret); }
  case WP21_ERASE: return tbl_erase_stroke(d, L, page, x0, y0, x1, y1, caret);
  case WP21_PAINT: {
    const struct QBorders *p = wp_pen();
    return tbl_paint_stroke(d, L, page, x0, y0, x1, y1, p->style, p->width, p->color, caret); }
  }
  return 0;
}
/* lengths: centimetres for an A4 (not Letter) page, as Quill's own dialogs choose by default */
static int metric(void) { Doc *d = wp_doc(); return !d || d->page.width != 12240; }
void wp21_len_text(i32 tw, char *out)
{
  if (tw < 0) { *out++ = '-'; tw = -tw; }
  if (metric()) sprintf(out, "%ld.%02ld cm", (long)(tw * 254L / 144000L), (long)(tw * 254L / 1440L % 100));
  else sprintf(out, "%ld.%02ld\"", (long)(tw / 1440), (long)(tw % 1440 * 100 / 1440));
}
i32 wp21_len_parse(const char *s)
{
  long whole = 0, frac = 0, div = 1;
  int inch = !metric(), neg = 0;
  while (*s == ' ') s++;
  if (*s == '-') { neg = 1; s++; }
  while (*s >= '0' && *s <= '9') whole = whole * 10 + (*s++ - '0');
  if (*s == '.' || *s == ',') { s++; while (*s >= '0' && *s <= '9' && div < 10000) { frac = frac * 10 + (*s++ - '0'); div *= 10; } }
  while (*s == ' ') s++;
  if (*s == '"' || !strncmp(s, "in", 2)) inch = 1;
  else if (!strncmp(s, "cm", 2)) inch = 0;
  else if (!strncmp(s, "mm", 2)) { inch = 0; frac = (whole % 10) * div + frac; div *= 10; whole /= 10; }
  else if (!strncmp(s, "pt", 2)) return (i32)((neg ? -1 : 1) * (whole * 20 + frac * 20 / div));
  if (inch) return (i32)((neg ? -1 : 1) * (whole * 1440 + frac * 1440 / div));
  return (i32)((neg ? -1 : 1) * ((whole * 1440 * 100 + frac * 1440 * 100 / div) / 254));
}
void wp21_changed(Pos at)
{
  /* the caret and the selection's other end put right first (the old ones may be past the document's
     end now), then the view lays the document out afresh (CMD_DESIGN: "the document changed") */
  Pos *c = NULL, *a = NULL;
  get(wp_view(), MUIA_Quill_Caret, &c);
  get(wp_view(), MUIA_Quill_Anchor, &a);
  if (c) *c = at;
  if (a) *a = at;
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_DESIGN, 0);
}
/* the caret (and selection) where the hooks saw it, NULL unless it is in a table */
static Doc *table_doc(void)
{
  Doc *d = wp_doc();
  if (!d || wp21_caret.para >= d->np || !in_table(&d->p[wp21_caret.para]->pf)) { DisplayBeep(NULL); return NULL; }
  if (wp21_anchor.para >= d->np) wp21_anchor = wp21_caret;
  return d;
}

/* ---- dialogs */
static Object *dcwin, *dcradio;                                  /* Delete Cells */
static Object *cmwin, *cmsame, *cmstr[4];                        /* Cell Options: the cell's margins */
static Object *atwin, *attitle, *atdesc;                         /* Alt Text */
static Object *tpwin, *tpwrap, *tphpos, *tphstr, *tphrel, *tpvpos, *tpvstr, *tpvrel, *tpdist[4], *tpoverlap;   /* Table Positioning */

static void open_delcells(void)
{
  static const char *how[5];
  if (!table_doc()) return;
  if (!dcwin) {
    how[0] = tr("Shift cells left"); how[1] = tr("Shift cells up"); how[2] = tr("Delete entire row"); how[3] = tr("Delete entire column"); how[4] = NULL;
    dcwin = wp_dialog("Delete Cells", MAKE_ID('W','2','D','C'), VGroup,
      Child, dcradio = RadioObject, MUIA_Radio_Entries, (ULONG)how, MUIA_CycleChain, 1, End,
    End, "_OK", ID_TB_DELCELLS_OK);
    if (!dcwin) return;
  }
  set(dcwin, MUIA_Window_Open, TRUE);
}
static void delcells_done(void)
{
  Doc *d = table_doc();
  ULONG v = 0;
  Pos c;
  set(dcwin, MUIA_Window_Open, FALSE);
  if (!d) return;
  get(dcradio, MUIA_Radio_Active, &v);
  c = wp21_caret;
  if (tbl_delete_cells(d, wp21_anchor, wp21_caret, (int)v, &c)) wp21_changed(c); else DisplayBeep(NULL);
}
/* a cell's own margins: E_ order in the dialog's Top, Left, Bottom, Right */
static void open_cellmar(void)
{
  static const char *const names[4] = { "Top", "Left", "Bottom", "Right" };
  Doc *d = table_doc();
  const Cell *c;
  Table *t;
  int k;
  char b[24];
  if (!d) return;
  c = &d->p[wp21_caret.para]->pf.cell;
  t = doc_table(d, &d->p[wp21_caret.para]->pf);
  if (!cmwin) {
    Object *g = ColGroup(4), End;
    for (k = 0; k < 4 && g; k++) {
      Object *l = QLabel(tr(names[k]), MUIO_Label_DoubleFrame);
      cmstr[k] = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End;
      if (l) DoMethod(g, OM_ADDMEMBER, (ULONG)l);
      if (cmstr[k]) DoMethod(g, OM_ADDMEMBER, (ULONG)cmstr[k]);
    }
    cmwin = wp_dialog("Cell Options", MAKE_ID('W','2','C','M'), VGroup,
      Child, QTitleObject(tr("Cell margins")),
      Child, HGroup, Child, cmsame = CheckMark(TRUE), Child, QLabel(tr("Same as the whole table"), MUIO_Label_LeftAligned | MUIO_Label_SingleFrame), Child, HSpace(0), End,
      Child, g,
    End, "_OK", ID_TB_CELLMAR_OK);
    if (!cmwin) return;
  }
  set(cmsame, MUIA_Selected, !(c->marset & 15));
  for (k = 0; k < 4; k++) {
    i32 v = (c->marset & (1 << k)) ? c->mar[k] : !t ? 0 : k == E_LEFT ? t->marl : k == E_RIGHT ? t->marr : k == E_TOP ? t->mart : t->marb;
    wp21_len_text(v, b);
    set(cmstr[k], MUIA_String_Contents, (ULONG)b);
  }
  set(cmwin, MUIA_Window_Open, TRUE);
}
static void cellmar_done(void)
{
  Doc *d = table_doc();
  ULONG same = 0;
  i16 m[4];
  int k;
  Pos c;
  set(cmwin, MUIA_Window_Open, FALSE);
  if (!d) return;
  get(cmsame, MUIA_Selected, &same);
  for (k = 0; k < 4; k++) {
    char *s = NULL;
    i32 v;
    get(cmstr[k], MUIA_String_Contents, &s);
    v = s ? wp21_len_parse(s) : 0;
    m[k] = (i16)(v < 0 ? 0 : v > 4000 ? 4000 : v);
  }
  c = wp21_caret;
  if (tbl_cell_mar(d, wp21_anchor, wp21_caret, same ? 0 : 15, m)) wp21_changed(c); else DisplayBeep(NULL);
}
/* Alt Text: the table's title (w:tblCaption) and description (w:tblDescription), Latin-1 in the gadgets */
static void open_alttext(void)
{
  Doc *d = table_doc();
  Table *t;
  char b[300];
  if (!d || !(t = doc_table(d, &d->p[wp21_caret.para]->pf))) return;
  if (!atwin) {
    atwin = wp_dialog("Alt Text", MAKE_ID('W','2','A','T'), VGroup,
      Child, QLabel(tr("How would you describe this table to someone who can't see it?"), MUIO_Label_LeftAligned),
      Child, ColGroup(2),
        Child, QLabel(tr("Title"), MUIO_Label_DoubleFrame), Child, attitle = StringObject, StringFrame, MUIA_String_MaxLen, 120, MUIA_CycleChain, 1, End,
        Child, QLabel(tr("Description"), MUIO_Label_DoubleFrame), Child, atdesc = StringObject, StringFrame, MUIA_String_MaxLen, 250, MUIA_CycleChain, 1, MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMMMMMMMMMMMMMMMM", End,
      End,
    End, "_OK", ID_TB_ALTTEXT_OK);
    if (!atwin) return;
  }
  utf8_to_latin1(t->caption, strlen(t->caption), b, sizeof b); set(attitle, MUIA_String_Contents, (ULONG)b);
  utf8_to_latin1(t->descr, strlen(t->descr), b, sizeof b); set(atdesc, MUIA_String_Contents, (ULONG)b);
  set(atwin, MUIA_Window_Open, TRUE);
}
static void alttext_done(void)
{
  Doc *d = table_doc();
  Table *t, nt;
  char *s = NULL;
  Pos c;
  set(atwin, MUIA_Window_Open, FALSE);
  if (!d || !(t = doc_table(d, &d->p[wp21_caret.para]->pf))) return;
  nt = *t;
  get(attitle, MUIA_String_Contents, &s); wp_latin1_utf8(s ? s : "", nt.caption, sizeof nt.caption);
  s = NULL;
  get(atdesc, MUIA_String_Contents, &s); wp_latin1_utf8(s ? s : "", nt.descr, sizeof nt.descr);
  c = wp21_caret;
  if (tbl_set_table(d, &c, &nt)) wp21_changed(c); else DisplayBeep(NULL);
}
/* Table Positioning: wrapping (none: in line; around: floating), across and down: aligned or at a
   distance, from the margin, the page, or the column (across) and the paragraph (down); the text's
   distance; overlapping other floating tables */
static void open_tpos(void)
{
  static const char *wraps[3], *hp[5], *hr[4], *vp[5], *vr[4];
  static const char *const dn[4] = { "Top", "Left", "Bottom", "Right" };
  Doc *d = table_doc();
  Table *t;
  char b[24];
  int k;
  if (!d || !(t = doc_table(d, &d->p[wp21_caret.para]->pf))) return;
  if (!tpwin) {
    Object *g = ColGroup(4), End;
    wraps[0] = tr("None"); wraps[1] = tr("Around"); wraps[2] = NULL;
    hp[0] = tr("Position:"); hp[1] = tr("Left"); hp[2] = tr("Centre"); hp[3] = tr("Right"); hp[4] = NULL;
    hr[0] = tr("Column"); hr[1] = tr("Margin"); hr[2] = tr("Page"); hr[3] = NULL;
    vp[0] = tr("Position:"); vp[1] = tr("Top"); vp[2] = tr("Centre"); vp[3] = tr("Bottom"); vp[4] = NULL;
    vr[0] = tr("Paragraph"); vr[1] = tr("Margin"); vr[2] = tr("Page"); vr[3] = NULL;
    for (k = 0; k < 4 && g; k++) {
      Object *l = QLabel(tr(dn[k]), MUIO_Label_DoubleFrame);
      tpdist[k] = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End;
      if (l) DoMethod(g, OM_ADDMEMBER, (ULONG)l);
      if (tpdist[k]) DoMethod(g, OM_ADDMEMBER, (ULONG)tpdist[k]);
    }
    tpwin = wp_dialog("Table Positioning", MAKE_ID('W','2','T','P'), VGroup,
      Child, HGroup, Child, QLabel(tr("Text wrapping"), MUIO_Label_SingleFrame), Child, tpwrap = CycleObject, MUIA_Cycle_Entries, (ULONG)wraps, MUIA_CycleChain, 1, End, Child, HSpace(0), End,
      Child, QTitleObject(tr("Horizontal")),
      Child, ColGroup(3),
        Child, tphpos = CycleObject, MUIA_Cycle_Entries, (ULONG)hp, MUIA_CycleChain, 1, End,
        Child, tphstr = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
        Child, HGroup, Child, QLabel(tr("Relative to"), MUIO_Label_SingleFrame), Child, tphrel = CycleObject, MUIA_Cycle_Entries, (ULONG)hr, MUIA_CycleChain, 1, End, End,
      End,
      Child, QTitleObject(tr("Vertical")),
      Child, ColGroup(3),
        Child, tpvpos = CycleObject, MUIA_Cycle_Entries, (ULONG)vp, MUIA_CycleChain, 1, End,
        Child, tpvstr = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_CycleChain, 1, End,
        Child, HGroup, Child, QLabel(tr("Relative to"), MUIO_Label_SingleFrame), Child, tpvrel = CycleObject, MUIA_Cycle_Entries, (ULONG)vr, MUIA_CycleChain, 1, End, End,
      End,
      Child, QTitleObject(tr("Distance from surrounding text")),
      Child, g,
      Child, HGroup, Child, tpoverlap = CheckMark(TRUE), Child, QLabel(tr("Allow overlap"), MUIO_Label_LeftAligned | MUIO_Label_SingleFrame), Child, HSpace(0), End,
    End, "_OK", ID_TB_TPOS_OK);
    if (!tpwin) return;
  }
  set(tpwrap, MUIA_Cycle_Active, t->floating ? 1 : 0);
  set(tphpos, MUIA_Cycle_Active, t->xspec & 3);
  set(tpvpos, MUIA_Cycle_Active, t->yspec & 3);
  /* a table not floating yet: Word's defaults (from the column, down from the paragraph, a fifth of an inch round it) */
  set(tphrel, MUIA_Cycle_Active, t->floating ? t->hanchor % 3 : TA_TEXT);
  set(tpvrel, MUIA_Cycle_Active, t->floating ? t->vanchor % 3 : TA_TEXT);
  wp21_len_text(t->floating ? t->tx : 0, b); set(tphstr, MUIA_String_Contents, (ULONG)b);
  wp21_len_text(t->floating ? t->ty : 0, b); set(tpvstr, MUIA_String_Contents, (ULONG)b);
  {
    i32 dd[4];
    dd[0] = t->floating ? t->dt : 0; dd[1] = t->floating ? t->dl : 180; dd[2] = t->floating ? t->db : 0; dd[3] = t->floating ? t->dr : 180;
    for (k = 0; k < 4; k++) { wp21_len_text(dd[k], b); set(tpdist[k], MUIA_String_Contents, (ULONG)b); }
  }
  set(tpoverlap, MUIA_Selected, !t->nooverlap);
  set(tpwin, MUIA_Window_Open, TRUE);
}
static i32 str_len(Object *o) { char *s = NULL; get(o, MUIA_String_Contents, &s); return s ? wp21_len_parse(s) : 0; }
static void tpos_done(void)
{
  Doc *d = table_doc();
  Table *t, nt;
  ULONG v = 0;
  Pos c;
  set(tpwin, MUIA_Window_Open, FALSE);
  if (!d || !(t = doc_table(d, &d->p[wp21_caret.para]->pf))) return;
  nt = *t;
  get(tpwrap, MUIA_Cycle_Active, &v); nt.floating = (u8)(v != 0);
  get(tphpos, MUIA_Cycle_Active, &v); nt.xspec = (u8)v;
  get(tpvpos, MUIA_Cycle_Active, &v); nt.yspec = (u8)v;
  get(tphrel, MUIA_Cycle_Active, &v); nt.hanchor = (u8)v;
  get(tpvrel, MUIA_Cycle_Active, &v); nt.vanchor = (u8)v;
  nt.tx = str_len(tphstr); nt.ty = str_len(tpvstr);
  nt.dt = str_len(tpdist[0]); nt.dl = str_len(tpdist[1]); nt.db = str_len(tpdist[2]); nt.dr = str_len(tpdist[3]);
  get(tpoverlap, MUIA_Selected, &v); nt.nooverlap = (u8)!v;
  c = wp21_caret;
  if (tbl_set_table(d, &c, &nt)) wp21_changed(c); else DisplayBeep(NULL);
}

/* ---- the 9-way alignment and text direction */
static void cell_align(int v)
{
  static const u8 al[3] = { AL_LEFT, AL_CENTER, AL_RIGHT };
  Doc *d = table_doc();
  Pos c;
  if (!d || v < 0 || v > 8) return;
  c = wp21_caret;
  if (tbl_cell_align(d, wp21_anchor, wp21_caret, v / 3, al[v % 3])) wp21_changed(c); else DisplayBeep(NULL);
}
static void text_dir(int v)
{
  Doc *d = table_doc();
  Pos c;
  if (!d || v < 0 || v > 2) return;
  c = wp21_caret;
  if (tbl_cell_dir(d, wp21_anchor, wp21_caret, v)) wp21_changed(c); else DisplayBeep(NULL);
}

int wp21b_command(ULONG id, LONG value)
{
  if (id >= ID_TB_AL_FIRST && id <= ID_TB_AL_LAST) { cell_align((int)(id - ID_TB_AL_FIRST)); return WPR_DONE; }
  if (id >= ID_TB_TD_FIRST && id <= ID_TB_TD_LAST) { text_dir((int)(id - ID_TB_TD_FIRST)); return WPR_DONE; }
  switch (id) {
  case ID_TB_DRAWTABLE: wp21_set_tool(WP21_DRAW); break;
  case ID_TB_ERASER: wp21_set_tool(WP21_ERASE); break;
  case ID_TB_TOOLOFF: if (tool) { tool = WP21_NONE; wp_refresh(); } break;
  case ID_TB_DELCELLS: open_delcells(); return WPR_KEEPFOCUS;
  case ID_TB_DELCELLS_OK: delcells_done(); break;
  case ID_TB_CELLALIGN: if (value >= 0) cell_align((int)value); break;
  case ID_TB_TEXTDIR: if (value >= 0) text_dir((int)value); break;
  case ID_TB_CELLMAR: open_cellmar(); return WPR_KEEPFOCUS;
  case ID_TB_CELLMAR_OK: cellmar_done(); break;
  case ID_TB_ALTTEXT: open_alttext(); return WPR_KEEPFOCUS;
  case ID_TB_ALTTEXT_OK: alttext_done(); break;
  case ID_TB_TPOS: open_tpos(); return WPR_KEEPFOCUS;
  case ID_TB_TPOS_OK: tpos_done(); break;
  }
  return WPR_DONE;
}
/* the state word: bit 1 Draw Table on, bit 2 the Eraser; and where the caret is, for the commands */
static void wp21b_state(ULONG *on, const WpCtx *c)
{
  wp21_caret = c->caret; wp21_anchor = c->anchor; wp21_intable = c->intable;
  if (tool == WP21_DRAW) *on |= RBIT(1);
  if (tool == WP21_ERASE) *on |= RBIT(2);
}
/* Esc puts a table tool away */
static ULONG wp21b_key(UWORD code, UWORD qual, const WpCtx *c)
{
  return code == 0x45 && tool ? ID_TB_TOOLOFF : 0;
}
static const char *const aligns9[] = { "Align Top Left", "Align Top Centre", "Align Top Right", "Align Centre Left", "Align Centre",
                                       "Align Centre Right", "Align Bottom Left", "Align Bottom Centre", "Align Bottom Right" };
static const char *const tdirs[] = { "Horizontal", "Rotate all text 90\260", "Rotate all text 270\260" };
/* the right-button menu in a table: Delete Cells, the alignment and direction, Cell Options, Alt Text */
static void wp21b_ctxmenu(Object *menu, const WpCtx *c)
{
  if (!c->intable || c->where != WPC_TEXT) return;
  wp21_caret = c->caret; wp21_anchor = c->anchor;
  wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
  wp_ctx_item(menu, tr("Delete Cells..."), WP_CTX_ID(ID_TB_DELCELLS), 1);
  wp_ctx_sub(menu, tr("Cell Alignment"), ID_TB_CELLALIGN, aligns9, 9);
  wp_ctx_sub(menu, tr("Text Direction"), ID_TB_TEXTDIR, tdirs, 3);
  wp_ctx_item(menu, tr("Cell Options..."), WP_CTX_ID(ID_TB_CELLMAR), 1);
  wp_ctx_item(menu, tr("Alt Text..."), WP_CTX_ID(ID_TB_ALTTEXT), 1);
}

/* icons (theme.c's icon language) */
#define IC_DRAWT WP21_IC_DRAWTABLE
#define IC_ERASE "cd R 0 0 9 9 L 4 0 4 9 L 0 4 9 4 cr F 7 8 14 13 ci R 7 8 14 13 L 10 8 10 13"
#define IC_ALIGN9 "ci R 1 1 14 14 ca F 3 3 5 5 cd F 7 3 8 5 F 10 3 12 5 F 3 7 5 8 F 7 7 8 8 F 10 7 12 8 F 3 10 5 12 F 7 10 8 12 F 10 10 12 12"
#define IC_TDIR "ci R 1 1 14 14 L 4 3 4 12 L 4 3 7 3 L 4 7 6 7 ca L 10 3 10 12 L 8 10 10 12 L 12 10 10 12"
#define IC_CELLMAR "ci R 1 1 14 14 cd R 4 4 11 11 ca L 1 7 4 7 L 11 7 14 7 L 7 1 7 4 L 7 11 7 14"
#define IC_DELCELLS "ci R 1 1 14 14 L 1 7 14 7 L 7 1 7 14 cr L 9 9 13 13 L 13 9 9 13"
#define IC_ALTTEXT "ci R 1 2 14 13 L 3 5 12 5 L 3 8 12 8 L 3 11 8 11 ca F 10 10 13 12"
#define IC_TPOS "cd L 1 1 14 1 L 1 4 4 4 L 1 7 4 7 L 1 10 4 10 L 1 14 14 14 ci R 6 3 14 11 L 6 7 14 7 L 10 3 10 11"
static const RibbonList l_align = { aligns9, 9, PL_TEXT, NULL };
static const RibbonList l_tdir = { tdirs, 3, PL_TEXT, NULL };
static const Item i_cell[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Draw Table", ID_TB_DRAWTABLE, RB_WP(WPI_21B, 1), 0, 0, 0, "Draw a table, or lines that split cells, with the mouse (Esc when done)", NULL, IC_DRAWT, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Eraser", ID_TB_ERASER, RB_WP(WPI_21B, 2), RB_INTABLE, 0, 0, "Click the line between two cells to merge them (Esc when done)", NULL, IC_ERASE, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Delete Cells", ID_TB_DELCELLS, 0, RB_INTABLE, 0, 0, "The selected cells out, the others moving left or up", NULL, IC_DELCELLS, 0 },
  { K_DROP, S_SMALL, 0, 0, "Align", ID_TB_CELLALIGN, 0, RB_INTABLE, F_NEWCOL, 0, "Where the text sits in the cells: top, centre or bottom, left, centre or right", &l_align, IC_ALIGN9, 0 },
  { K_DROP, S_SMALL, 0, 0, "Text Direction", ID_TB_TEXTDIR, 0, RB_INTABLE, 0, 0, "The cells' text across, or turned to read down or up", &l_tdir, IC_TDIR, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Cell Options", ID_TB_CELLMAR, 0, RB_INTABLE, 0, 0, "The selected cells' own margins", NULL, IC_CELLMAR, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Alt Text", ID_TB_ALTTEXT, 0, RB_INTABLE, F_NEWCOL, 0, "A title and description of the table for those who can't see it", NULL, IC_ALTTEXT, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Positioning", ID_TB_TPOS, 0, RB_INTABLE, 0, 0, "The table floating, with the text wrapped round it", NULL, IC_TPOS, 0 } };
static const WpGroup groups[] = {
  { TAB_TABLELAYOUT, 5, { "Cells", G_COLS, i_cell, 8 } } };
static const WpMenu menus[] = {
  { WM_TABLE, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_TABLE, { NM_ITEM, (STRPTR)"Draw Table", 0, 0, 0, (APTR)ID_TB_DRAWTABLE } },
  { WM_TABLE, { NM_ITEM, (STRPTR)"Eraser", 0, 0, 0, (APTR)ID_TB_ERASER } },
  { WM_TABLE, { NM_ITEM, (STRPTR)"Delete Cells...", 0, 0, 0, (APTR)ID_TB_DELCELLS } },
  { WM_TABLE, { NM_ITEM, (STRPTR)"Cell Alignment", 0, 0, 0, 0 } },
  { WM_TABLE, { NM_SUB, (STRPTR)"Align Top Left", 0, 0, 0, (APTR)(ID_TB_AL_FIRST + 0) } },
  { WM_TABLE, { NM_SUB, (STRPTR)"Align Top Centre", 0, 0, 0, (APTR)(ID_TB_AL_FIRST + 1) } },
  { WM_TABLE, { NM_SUB, (STRPTR)"Align Top Right", 0, 0, 0, (APTR)(ID_TB_AL_FIRST + 2) } },
  { WM_TABLE, { NM_SUB, (STRPTR)"Align Centre Left", 0, 0, 0, (APTR)(ID_TB_AL_FIRST + 3) } },
  { WM_TABLE, { NM_SUB, (STRPTR)"Align Centre", 0, 0, 0, (APTR)(ID_TB_AL_FIRST + 4) } },
  { WM_TABLE, { NM_SUB, (STRPTR)"Align Centre Right", 0, 0, 0, (APTR)(ID_TB_AL_FIRST + 5) } },
  { WM_TABLE, { NM_SUB, (STRPTR)"Align Bottom Left", 0, 0, 0, (APTR)(ID_TB_AL_FIRST + 6) } },
  { WM_TABLE, { NM_SUB, (STRPTR)"Align Bottom Centre", 0, 0, 0, (APTR)(ID_TB_AL_FIRST + 7) } },
  { WM_TABLE, { NM_SUB, (STRPTR)"Align Bottom Right", 0, 0, 0, (APTR)(ID_TB_AL_FIRST + 8) } },
  { WM_TABLE, { NM_ITEM, (STRPTR)"Text Direction", 0, 0, 0, 0 } },
  { WM_TABLE, { NM_SUB, (STRPTR)"Horizontal", 0, 0, 0, (APTR)(ID_TB_TD_FIRST + 0) } },
  { WM_TABLE, { NM_SUB, (STRPTR)"Rotate all text 90\260", 0, 0, 0, (APTR)(ID_TB_TD_FIRST + 1) } },
  { WM_TABLE, { NM_SUB, (STRPTR)"Rotate all text 270\260", 0, 0, 0, (APTR)(ID_TB_TD_FIRST + 2) } },
  { WM_TABLE, { NM_ITEM, (STRPTR)"Cell Options...", 0, 0, 0, (APTR)ID_TB_CELLMAR } },
  { WM_TABLE, { NM_ITEM, (STRPTR)"Alt Text...", 0, 0, 0, (APTR)ID_TB_ALTTEXT } },
  { WM_TABLE, { NM_ITEM, (STRPTR)"Table Positioning...", 0, 0, 0, (APTR)ID_TB_TPOS } } };

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp21b_package = {
  "WP21b", ID_WP21B_FIRST, ID_WP21B_LAST,
  wp21b_command, wp21b_state, NULL, wp21b_ctxmenu, wp21b_key, NULL, NULL,
  groups, (int)(sizeof groups / sizeof groups[0]),
  menus, (int)(sizeof menus / sizeof menus[0])
};
