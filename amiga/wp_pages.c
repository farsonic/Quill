/*
 * wp_pages.c - WP22a Pages, sections and columns (docs/wordcraft-gap.md). This file belongs to WP22a: only that
 * package edits it. Its commands are ID_WP22A_FIRST .. ID_WP22A_LAST (ids.h, named in wp_pages.h); how the hooks
 * below are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * The windows of Word's page layout: Columns (presets One, Two, Three, Left, Right; up to six columns
 * of their own widths and spacing, a line between, applied to the whole document, the section, from
 * the caret on or the selected text), Page Setup (margins with a gutter on the left or top, mirror
 * margins, two pages per sheet, book fold; paper; the section's start, headers and footers, vertical
 * alignment with Justified), Page Borders (each side on or off, every line kind and a few art ones,
 * colour, width, the space from the text or the page's edge, the section's first page only or all but
 * it, round the header and footer or not), Printed Watermark (a picture with its scale and washout,
 * or words in a font and colour, solid or half transparent), Line Numbers with Apply to, and
 * Hyphenation options (zone, capitals, consecutive hyphens) with Manual Hyphenation, which goes
 * through the words that could break at the ends of lines and puts an optional hyphen where asked.
 * The core of it (layout, drawing, .docx) is core/pages.c.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <libraries/iffparse.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "lang.h"
#include "wp_pages.h"
#include "../core/pages.h"
#include "../core/hyph.h"
#include "../core/docx.h"
#include "../core/fitpage.h"
#include "wp_windows.h"

/* the dialogs' labels in Quill's theme, as main.c makes them */
#undef Label
#undef Label1
#undef Label2
#undef LLabel1
#define Label1(l)  QLabel(l, MUIO_Label_SingleFrame)
#define Label2(l)  QLabel(l, MUIO_Label_DoubleFrame)
#define LLabel1(l) QLabel(l, MUIO_Label_LeftAligned | MUIO_Label_SingleFrame)

void wp_len_text(i32 tw, char *out);        /* main.c: a length as the user reads it ("2.54 cm"), and back */
i32  wp_len_parse(const char *s);

static WpCtx last;                          /* the caret and selection as the view last told (state hook) */

/* ------------------------------------------------------------------ */
/* small helpers */
static Object *str_obj(int max) { return StringObject, StringFrame, MUIA_String_MaxLen, max, MUIA_CycleChain, 1, MUIA_FixWidthTxt, (ULONG)"0000000000", End; }
static Object *cyc_obj(const char **e) { return CycleObject, MUIA_Cycle_Entries, (ULONG)e, MUIA_CycleChain, 1, End; }
static void notify_id(Object *o, ULONG attr, ULONG id) { DoMethod(o, MUIM_Notify, attr, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, id); }
static LONG geti(Object *o, ULONG attr) { LONG v = 0; get(o, attr, &v); return v; }
static void set_len(Object *o, i32 tw) { char b[32]; wp_len_text(tw, b); set(o, MUIA_String_Contents, (ULONG)b); }
static i32 get_len(Object *o) { char *s = NULL; get(o, MUIA_String_Contents, &s); return s ? wp_len_parse(s) : 0; }
static void set_num(Object *o, long v) { char b[16]; sprintf(b, "%ld", v); set(o, MUIA_String_Contents, (ULONG)b); }
static long get_num(Object *o) { char *s = NULL; get(o, MUIA_String_Contents, &s); return s ? atol(s) : 0; }
/* points as text ("4 pt") and back, for border spaces */
static void set_pt(Object *o, i32 tw) { char b[16]; sprintf(b, "%ld pt", (long)(tw / 20)); set(o, MUIA_String_Contents, (ULONG)b); }
static i32 get_pt(Object *o) { long v = get_num(o); return (i32)(v < 0 ? 0 : v > 31 ? 31 * 20 : v * 20); }
static Doc *D(void) { return wp_doc(); }
static Pos caret(void) { Pos *c = wp_caret(); Pos p; p.para = c ? c->para : 0; p.off = c ? c->off : 0; return p; }
static void relayout(void) { DoMethod(wp_view(), MUIM_Quill_Command, CMD_DESIGN, 0); wp_refresh(); }
/* the caret's section's format, and whether it is the last */
static const SectFmt *caret_sect(int *lastp) { Doc *d = D(); Pos c = caret(); if (lastp) *lastp = doc_last_sect(d, c.para); return doc_sect(d, c.para); }
/* its page setup as it is kept (not as laid out), and the text width across it */
static const PageSetup *caret_pg(void) { int l; const SectFmt *s = caret_sect(&l); return sect_page(D(), s, l); }
static i32 caret_body(void) { const PageSetup *pg = caret_pg(); i32 b = pg->width - pg->left - pg->right - pg->gutter; return b < 1440 ? 1440 : b; }

/* ------------------------------------------------------------------ */
/* section changes: Apply to the whole document, the caret's section, from the caret on (a section
   break there first) or the selected text (breaks round it), each section format changed by fn, one
   undo step */
enum { AP_WHOLE, AP_SECTION, AP_FORWARD, AP_SELECTED };
typedef void (*SectFn)(SectFmt *s, int last, void *arg);
/* a section break before p (in the paragraph: it is split): the part before keeps the section's
   format (and its page setup, if it was the last's), the part from p starts as start says */
static int break_at(Doc *d, Pos *p, int start)
{
  SectFmt s, t;
  ParaFmt pf;
  u32 e;
  int wasl = doc_last_sect(d, p->para);
  if (in_table(&d->p[p->para]->pf)) return 0;
  s = *doc_sect(d, p->para);
  s.titlepg = (u8)sect_titlepg(d, &s, wasl);
  if (wasl) { s.haspage = 1; s.page = *sect_page(d, &s, 1); }
  if (p->off > 0 || p->para == 0) doc_insert(d, p, "\n", 1, NULL);
  else if (d->p[p->para - 1]->pf.sect || in_table(&d->p[p->para - 1]->pf)) goto startonly;
  memset(&pf, 0, sizeof pf);
  if (!(pf.sect = (u16)doc_add_sect(d, &s))) return 0;
  doc_para_format(d, p->para - 1, p->para - 1, PM_SECT, &pf);
startonly:
  e = doc_sect_end(d, p->para);
  if (in_table(&d->p[e]->pf)) return 1;
  t = *doc_sect(d, e);
  t.start = (u8)start;
  memset(&pf, 0, sizeof pf);
  if ((pf.sect = (u16)doc_add_sect(d, &t))) doc_para_format(d, e, e, PM_SECT, &pf);
  return 1;
}
static int apply_sect(int scope, int start, SectFn fn, void *arg)
{
  Doc *d;
  Pos c, a, *ap = NULL;
  u32 i, from, to;
  wp_story_close();
  d = D();
  c = caret();
  get(wp_view(), MUIA_Quill_Anchor, &ap);
  a = ap && ap->para < d->np ? *ap : c;
  doc_begin(d, c);
  if (scope == AP_SELECTED && (a.para != c.para || a.off != c.off)) {
    Pos lo = a.para < c.para || (a.para == c.para && a.off < c.off) ? a : c, hi = lo.para == a.para && lo.off == a.off ? c : a;
    if (!break_at(d, &hi, SB_CONT)) { doc_end(d, c); DisplayBeep(NULL); return 0; }
    if (!break_at(d, &lo, SB_CONT)) { doc_end(d, c); DisplayBeep(NULL); return 0; }
    c = lo;
  } else if (scope == AP_FORWARD || scope == AP_SELECTED) {
    if (!break_at(d, &c, start)) { doc_end(d, c); DisplayBeep(NULL); return 0; }
  }
  from = scope == AP_WHOLE ? 0 : doc_sect_end(d, c.para);
  to = scope == AP_WHOLE ? d->np - 1 : from;
  for (i = from; i <= to; i++) {
    SectFmt s;
    ParaFmt pf;
    if (!d->p[i]->pf.sect && i != d->np - 1) continue;
    if (in_table(&d->p[i]->pf)) continue;
    s = *doc_sect(d, i);
    fn(&s, doc_last_sect(d, i), arg);
    memset(&pf, 0, sizeof pf);
    if (!(pf.sect = (u16)doc_add_sect(d, &s))) break;
    doc_para_format(d, i, i, PM_SECT, &pf);
  }
  doc_end(d, c);
  set(wp_view(), MUIA_Quill_Caret, (ULONG)&c);
  d->dirty = 1;
  set(wp_view(), MUIA_Quill_Changed, TRUE);
  relayout();
  return 1;
}
/* the section formats whose page setup or border is changed in place (not undoable, as Quill's page
   setup is not): the whole document's, or the caret's section's (the last section's is the
   document's: the others keep what they had) */
static PageSetup *page_to_change(int whole)
{
  Doc *d = D();
  u32 pi = caret().para, e;
  int k;
  SectFmt *s;
  if (whole) { for (k = 0; k < d->nsects; k++) d->sects[k].haspage = 0; return &d->page; }
  if (doc_last_sect(d, pi)) {
    for (k = 0; k < d->nsects; k++) if (!d->sects[k].haspage) { d->sects[k].haspage = 1; d->sects[k].page = d->page; }
    return &d->page;
  }
  e = doc_sect_end(d, pi);
  s = &d->sects[d->p[e]->pf.sect - 1];
  if (!s->haspage) { s->haspage = 1; s->page = d->page; }
  return &s->page;
}
static PageBorder *border_to_change(int whole)
{
  Doc *d = D();
  u32 pi = caret().para, e;
  int k;
  if (whole) { for (k = 0; k < d->nsects; k++) d->sects[k].pbown = 0; return &d->border; }
  if (doc_last_sect(d, pi)) {
    for (k = 0; k < d->nsects; k++) if (!d->sects[k].pbown) { d->sects[k].pbown = 1; d->sects[k].pb = d->border; }
    return &d->border;
  }
  e = doc_sect_end(d, pi);
  if (!d->p[e]->pf.sect) return &d->border;
  if (!d->sects[d->p[e]->pf.sect - 1].pbown) { d->sects[d->p[e]->pf.sect - 1].pbown = 1; d->sects[d->p[e]->pf.sect - 1].pb = d->border; }
  return &d->sects[d->p[e]->pf.sect - 1].pb;
}
static void changed(void)
{
  Doc *d = D();
  int k;
  for (k = 0; k < d->nsects; k++) if (d->sects[k].haspage && !memcmp(&d->sects[k].page, &d->page, sizeof(PageSetup))) d->sects[k].haspage = 0;
  d->dirty = 1; d->ver++;
  set(wp_view(), MUIA_Quill_Changed, TRUE);
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_PAGESETUP, 0);
  wp_refresh();
}
static const char *applies[5];
static void applies_init(void)
{
  if (applies[0]) return;
  applies[0] = tr("Whole document"); applies[1] = tr("This section"); applies[2] = tr("This point forward"); applies[3] = tr("Selected text"); applies[4] = NULL;
}

/* ------------------------------------------------------------------ */
/* Columns */
static Object *colwin, *c_pre, *c_num, *c_line, *c_equal, *c_apply, *c_w[MAX_SECT_COLS], *c_sp[MAX_SECT_COLS];
static void col_show(const SectFmt *s, i32 body)
{
  i32 x[MAX_SECT_COLS], w[MAX_SECT_COLS];
  int n = pages_cols(s, body, x, w), k, pre = pages_col_preset_of(s, body);
  if (s->cols > 1 && n == 1) n = s->cols;          /* (too narrow to lay out: shown as asked) */
  set(c_num, MUIA_Numeric_Value, n);
  set(c_equal, MUIA_Selected, !s->colunequal);
  set(c_line, MUIA_Selected, s->colline);
  nnset(c_pre, MUIA_Cycle_Active, pre < 0 ? CP_N : pre);
  for (k = 0; k < MAX_SECT_COLS; k++) {
    i32 cw = k < n ? (s->colunequal ? s->colw[k] : (body - (n - 1) * s->colgap) / n) : 0, sp = k + 1 < n ? (s->colunequal ? s->colsp[k] : s->colgap) : 0;
    set_len(c_w[k], cw); set_len(c_sp[k], sp);
    set(c_w[k], MUIA_Disabled, k >= n || (!s->colunequal && k > 0));
    set(c_sp[k], MUIA_Disabled, k + 1 >= n || (!s->colunequal && k > 0));
  }
}
/* the window's columns as a section format (s's other settings kept) */
static void col_read(SectFmt *s)
{
  int n = (int)geti(c_num, MUIA_Numeric_Value), k, eq = (int)geti(c_equal, MUIA_Selected);
  i32 w[MAX_SECT_COLS], sp[MAX_SECT_COLS];
  for (k = 0; k < MAX_SECT_COLS; k++) { w[k] = get_len(c_w[k]); sp[k] = get_len(c_sp[k]); if (w[k] < 360) w[k] = 360; if (sp[k] < 0) sp[k] = 0; }
  pages_col_set(s, n, w, sp, eq, n > 1 ? sp[0] : -1);
  s->colline = (u8)geti(c_line, MUIA_Selected);
}
static void open_columns(void)
{
  static const char *pres[CP_N + 2];
  int k, hassel = last.hassel;
  if (!colwin) {
    Object *g, *grid;
    applies_init();
    pres[0] = tr("One"); pres[1] = tr("Two"); pres[2] = tr("Three"); pres[3] = tr("Left"); pres[4] = tr("Right"); pres[5] = tr("Custom"); pres[6] = NULL;
    grid = ColGroup(3), Child, LLabel1(tr("Col #")), Child, LLabel1(tr("Width")), Child, LLabel1(tr("Spacing")), End;
    for (k = 0; k < MAX_SECT_COLS; k++) {
      char num[4];
      sprintf(num, "%d:", k + 1);
      DoMethod(grid, OM_ADDMEMBER, (ULONG)Label2(num));
      DoMethod(grid, OM_ADDMEMBER, (ULONG)(c_w[k] = str_obj(16)));
      DoMethod(grid, OM_ADDMEMBER, (ULONG)(c_sp[k] = str_obj(16)));
    }
    g = VGroup,
      Child, ColGroup(2),
        Child, Label1(tr("Presets")), Child, c_pre = cyc_obj(pres),
        Child, Label1(tr("Number of columns")), Child, c_num = SliderObject, MUIA_Numeric_Min, 1, MUIA_Numeric_Max, MAX_SECT_COLS, MUIA_Numeric_Value, 1, MUIA_CycleChain, 1, End,
        Child, Label1(tr("Line between")), Child, HGroup, Child, c_line = CheckMark(FALSE), Child, HSpace(0), End,
      End,
      Child, QTitleObject(tr("Width and spacing")),
      Child, grid,
      Child, HGroup, Child, c_equal = CheckMark(TRUE), Child, LLabel1(tr("Equal column width")), Child, HSpace(0), End,
      Child, HGroup, Child, Label1(tr("Apply to")), Child, c_apply = cyc_obj(applies), Child, HSpace(0), End,
    End;
    if (!g || !(colwin = wp_dialog("Columns", MAKE_ID('C','O','L','S'), g, "_OK", ID_PG_COLGO))) return;
    notify_id(c_pre, MUIA_Cycle_Active, ID_PG_COLPRE);
    notify_id(c_num, MUIA_Numeric_Value, ID_PG_COLNUM);
    notify_id(c_equal, MUIA_Selected, ID_PG_COLNUM);
  }
  col_show(caret_sect(NULL), caret_body());
  set(c_apply, MUIA_Cycle_Active, hassel ? AP_SELECTED : AP_SECTION);
  set(colwin, MUIA_Window_Open, TRUE);
}
/* a preset picked, or the number of columns or Equal changed: the widths shown again */
static void col_update(int preset)
{
  SectFmt s;
  i32 body = caret_body();
  s = *caret_sect(NULL);
  if (preset >= 0 && preset < CP_N) pages_col_preset(&s, preset, body);
  else if (preset < 0) {
    int n = (int)geti(c_num, MUIA_Numeric_Value), k, eq = (int)geti(c_equal, MUIA_Selected);
    i32 gap = get_len(c_sp[0]), w[MAX_SECT_COLS], sp[MAX_SECT_COLS];
    if (gap <= 0 || gap > 2880) gap = 720;
    for (k = 0; k < MAX_SECT_COLS; k++) { w[k] = (body - (n - 1) * gap) / n; sp[k] = gap; }
    pages_col_set(&s, n, w, sp, eq, gap);
    s.colline = (u8)geti(c_line, MUIA_Selected);
  } else return;
  col_show(&s, body);
}
static SectFmt colnew;
static void col_fn(SectFmt *s, int lastsec, void *arg)
{
 
  s->cols = colnew.cols; s->colunequal = colnew.colunequal; s->colgap = colnew.colgap; s->colline = colnew.colline;
  memcpy(s->colw, colnew.colw, sizeof s->colw); memcpy(s->colsp, colnew.colsp, sizeof s->colsp);
}
static void col_done(void)
{
  int scope = (int)geti(c_apply, MUIA_Cycle_Active);
  colnew = *caret_sect(NULL);
  col_read(&colnew);
  set(colwin, MUIA_Window_Open, FALSE);
  if (scope == AP_SELECTED && !last.hassel) scope = AP_FORWARD;
  apply_sect(scope, SB_CONT, col_fn, NULL);
}
/* the ribbon's Left and Right presets, for the caret's section */
static void col_preset(int preset)
{
  colnew = *caret_sect(NULL);
  pages_col_preset(&colnew, preset, caret_body());
  colnew.colline = caret_sect(NULL)->colline;
  apply_sect(last.hassel ? AP_SELECTED : AP_SECTION, SB_CONT, col_fn, NULL);
}

/* ------------------------------------------------------------------ */
/* Page Setup */
static const i32 papers[][2] = { { 11906, 16838 }, { 12240, 15840 }, { 12240, 20160 }, { 8391, 11906 }, { 16838, 23811 }, { 5953, 8391 },
                                 { 9979, 14175 }, { 10440, 15120 }, { 15840, 24480 } };
#define NPAPERS (int)(sizeof papers / sizeof papers[0])
static Object *pswin, *p_top, *p_bot, *p_left, *p_right, *p_gut, *p_gutpos, *p_orient, *p_multi, *p_sheets, *p_paper, *p_w, *p_h,
              *p_start, *p_evenodd, *p_first, *p_hdr, *p_ftr, *p_valign, *p_apply, *p_ll, *p_lr, *p_lsheets;
static const UWORD sheets[] = { 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40 };
#define NSHEETS (int)(sizeof sheets / sizeof sheets[0])
/* the margins' names follow the pages: inside and outside when they face each other */
static void ps_labels(void)
{
  LONG mp = geti(p_multi, MUIA_Cycle_Active);
  set(p_ll, MUIA_Text_Contents, (ULONG)(mp == MP_MIRROR || mp == MP_BOOKFOLD ? tr("Inside") : tr("Left")));
  set(p_lr, MUIA_Text_Contents, (ULONG)(mp == MP_MIRROR || mp == MP_BOOKFOLD ? tr("Outside") : tr("Right")));
  set(p_sheets, MUIA_Disabled, mp != MP_BOOKFOLD);
  set(p_gutpos, MUIA_Disabled, mp == MP_MIRROR || mp == MP_BOOKFOLD);
}
static void open_setup(int tab)
{
  static const char *titles[4], *gutpos[3], *orients[3], *multis[5], *paperl[NPAPERS + 2], *starts[6], *valigns[5], *sheetl[NSHEETS + 1];
  static char sheettxt[NSHEETS][8];
  static Object *reg;
  Doc *d;
  const PageSetup *pg;
  int k, lastsec, land, pk = NPAPERS;
  const SectFmt *s;
  if (!pswin) {
    Object *g, *m, *pa, *la;
    applies_init();
    titles[0] = tr("Margins"); titles[1] = tr("Paper"); titles[2] = tr("Layout"); titles[3] = NULL;
    gutpos[0] = tr("Left"); gutpos[1] = tr("Top"); gutpos[2] = NULL;
    orients[0] = tr("Portrait"); orients[1] = tr("Landscape"); orients[2] = NULL;
    multis[0] = tr("Normal"); multis[1] = tr("Mirror margins"); multis[2] = tr("2 pages per sheet"); multis[3] = tr("Book fold"); multis[4] = NULL;
    paperl[0] = "A4"; paperl[1] = "Letter"; paperl[2] = "Legal"; paperl[3] = "A5"; paperl[4] = "A3"; paperl[5] = "A6"; paperl[6] = "B5";
    paperl[7] = "Executive"; paperl[8] = "Tabloid"; paperl[9] = tr("Custom size"); paperl[10] = NULL;
    starts[0] = tr("New page"); starts[1] = tr("Continuous"); starts[2] = tr("Even page"); starts[3] = tr("Odd page"); starts[4] = NULL;
    valigns[0] = tr("Top"); valigns[1] = tr("Centre"); valigns[2] = tr("Bottom"); valigns[3] = tr("Justified"); valigns[4] = NULL;
    sheetl[0] = tr("All");
    for (k = 1; k < NSHEETS; k++) { sprintf(sheettxt[k], "%u", (unsigned)sheets[k]); sheetl[k] = sheettxt[k]; }
    sheetl[NSHEETS] = NULL;
    m = VGroup,
      Child, ColGroup(4),
        Child, Label2(tr("Top")), Child, p_top = str_obj(16), Child, Label2(tr("Bottom")), Child, p_bot = str_obj(16),
        Child, p_ll = TextObject, MUIA_Text_Contents, (ULONG)tr("Left"), MUIA_Text_PreParse, (ULONG)"\33r", MUIA_FixWidthTxt, (ULONG)"Outside ", End, Child, p_left = str_obj(16),
        Child, p_lr = TextObject, MUIA_Text_Contents, (ULONG)tr("Right"), MUIA_Text_PreParse, (ULONG)"\33r", MUIA_FixWidthTxt, (ULONG)"Outside ", End, Child, p_right = str_obj(16),
        Child, Label2(tr("Gutter")), Child, p_gut = str_obj(16), Child, Label1(tr("Gutter position")), Child, p_gutpos = cyc_obj(gutpos),
      End,
      Child, ColGroup(2),
        Child, Label1(tr("Orientation")), Child, p_orient = cyc_obj(orients),
        Child, Label1(tr("Multiple pages")), Child, p_multi = cyc_obj(multis),
        Child, p_lsheets = Label1(tr("Sheets per booklet")), Child, p_sheets = cyc_obj(sheetl),
      End,
    End;
    pa = ColGroup(2),
      Child, Label1(tr("Paper size")), Child, p_paper = cyc_obj(paperl),
      Child, Label2(tr("Width")), Child, p_w = str_obj(16),
      Child, Label2(tr("Height")), Child, p_h = str_obj(16),
    End;
    la = VGroup,
      Child, ColGroup(2),
        Child, Label1(tr("Section start")), Child, p_start = cyc_obj(starts),
        Child, Label1(tr("Different odd and even")), Child, HGroup, Child, p_evenodd = CheckMark(FALSE), Child, HSpace(0), End,
        Child, Label1(tr("Different first page")), Child, HGroup, Child, p_first = CheckMark(FALSE), Child, HSpace(0), End,
        Child, Label2(tr("Header from edge")), Child, p_hdr = str_obj(16),
        Child, Label2(tr("Footer from edge")), Child, p_ftr = str_obj(16),
        Child, Label1(tr("Vertical alignment")), Child, p_valign = cyc_obj(valigns),
      End,
    End;
    g = VGroup,
      Child, reg = RegisterGroup(titles), MUIA_CycleChain, 1, Child, m, Child, pa, Child, la, End,
      Child, HGroup, Child, Label1(tr("Apply to")), Child, p_apply = cyc_obj(applies), Child, HSpace(0), End,
    End;
    if (!g || !(pswin = wp_dialog("Page Setup", MAKE_ID('P','G','S','2'), g, "_OK", ID_PG_SETUPGO))) return;
    notify_id(p_multi, MUIA_Cycle_Active, ID_PG_SETUPUPD);
    notify_id(p_paper, MUIA_Cycle_Active, ID_PG_SETUPUPD);
    notify_id(p_orient, MUIA_Cycle_Active, ID_PG_SETUPUPD);
  }
  d = D();
  s = caret_sect(&lastsec);
  pg = sect_page(d, s, lastsec);
  set_len(p_top, pg->top); set_len(p_bot, pg->bottom); set_len(p_left, pg->left); set_len(p_right, pg->right); set_len(p_gut, pg->gutter);
  set(p_gutpos, MUIA_Cycle_Active, d->gutterattop ? 1 : 0);
  land = pg->width > pg->height;
  nnset(p_orient, MUIA_Cycle_Active, land);
  nnset(p_multi, MUIA_Cycle_Active, pages_multi(d));
  for (k = 0; k < NSHEETS; k++) if (sheets[k] == d->bfsheets) set(p_sheets, MUIA_Cycle_Active, k);
  for (k = 0; k < NPAPERS; k++)
    if ((papers[k][0] == pg->width && papers[k][1] == pg->height) || (papers[k][1] == pg->width && papers[k][0] == pg->height)) pk = k;
  nnset(p_paper, MUIA_Cycle_Active, pk);
  set_len(p_w, pg->width); set_len(p_h, pg->height);
  set(p_start, MUIA_Cycle_Active, s->start & 3);
  set(p_evenodd, MUIA_Selected, d->evenodd);
  set(p_first, MUIA_Selected, sect_titlepg(d, s, lastsec));
  set_len(p_hdr, pg->header); set_len(p_ftr, pg->footer);
  set(p_valign, MUIA_Cycle_Active, s->valign & 3);
  set(p_apply, MUIA_Cycle_Active, AP_WHOLE);
  if (tab >= 0) set(reg, MUIA_Group_ActivePage, tab);
  ps_labels();
  set(pswin, MUIA_Window_Open, TRUE);
}
/* a paper size or orientation picked: the width and height follow */
static void ps_update(void)
{
  LONG pk = geti(p_paper, MUIA_Cycle_Active), land = geti(p_orient, MUIA_Cycle_Active);
  i32 w = get_len(p_w), h = get_len(p_h), a, b;
  if (pk < NPAPERS) { a = papers[pk][0]; b = papers[pk][1]; }
  else { a = w < h ? w : h; b = w < h ? h : w; }
  if (land) { w = b; h = a; } else { w = a; h = b; }
  set_len(p_w, w); set_len(p_h, h);
  ps_labels();
}
static struct { int start, valign, first; } psnew;
static void ps_fn(SectFmt *s, int lastsec, void *arg)
{

  s->start = (u8)psnew.start; s->valign = (u8)psnew.valign;
  if (!lastsec) s->titlepg = (u8)psnew.first;
}
static void setup_done(void)
{
  Doc *d = D();
  i32 v[9];
  int scope = (int)geti(p_apply, MUIA_Cycle_Active), mp = (int)geti(p_multi, MUIA_Cycle_Active);
  PageSetup *pg, np;
  v[0] = get_len(p_w); v[1] = get_len(p_h); v[2] = get_len(p_top); v[3] = get_len(p_bot); v[4] = get_len(p_left); v[5] = get_len(p_right);
  v[6] = get_len(p_hdr); v[7] = get_len(p_ftr); v[8] = get_len(p_gut);
  if (v[0] < 1440 || v[1] < 1440 || v[0] > 31680 || v[1] > 31680 || v[8] < 0 || v[4] + v[5] + v[8] > v[0] - 720 || v[2] + v[3] > v[1] - 720) {
    wp_ask("OK", "The page must be 1 to 22 inches (2.5 to 55 cm) each way, with room between the margins.", NULL);
    return;
  }
  set(pswin, MUIA_Window_Open, FALSE);
  psnew.start = (int)geti(p_start, MUIA_Cycle_Active); psnew.valign = (int)geti(p_valign, MUIA_Cycle_Active); psnew.first = (int)geti(p_first, MUIA_Selected);
  if (scope == AP_SELECTED) scope = last.hassel ? AP_SELECTED : AP_FORWARD;
  /* the section's start, vertical alignment and first page (one undo step, with the breaks needed) */
  apply_sect(scope, SB_NEXT, ps_fn, NULL);
  /* the page setup (in place) for what was chosen: the caret is now in the section it applies to */
  pg = page_to_change(scope == AP_WHOLE);
  np = *pg;
  np.width = v[0]; np.height = v[1]; np.top = v[2]; np.bottom = v[3]; np.left = v[4]; np.right = v[5]; np.header = v[6]; np.footer = v[7]; np.gutter = v[8];
  *pg = np;
  if (doc_last_sect(d, caret().para)) d->titlepg = (u8)psnew.first;
  pages_set_multi(d, mp);
  d->gutterattop = (u8)(mp != MP_MIRROR && mp != MP_BOOKFOLD && geti(p_gutpos, MUIA_Cycle_Active) == 1);
  d->bfsheets = sheets[geti(p_sheets, MUIA_Cycle_Active) % NSHEETS];
  d->evenodd = (u8)geti(p_evenodd, MUIA_Selected);
  changed();
}

/* ------------------------------------------------------------------ */
/* Page Borders */
static const char *const kindnames[] = { "Single", "Dotted", "Dashed", "Dashed, small gaps", "Dot dash", "Dot dot dash", "Double", "Triple",
  "Thin-thick, small gap", "Thick-thin, small gap", "Thin-thick-thin, small gap", "Thin-thick, medium gap", "Thick-thin, medium gap",
  "Thin-thick-thin, medium gap", "Thin-thick, large gap", "Thick-thin, large gap", "Thin-thick-thin, large gap", "Wave", "Double wave",
  "Dash dot stroked", "3-D emboss", "3-D engrave", "Outset", "Inset", "Thick" };
static const char *const artnames[] = { "Black dots", "Black squares", "Thin lines", "Wide outline", "Checked bar", "Dot dash border", "Triangles",
  "Zigzag", "Apples", "Hearts", "Stars", "Weaving braid" };
static const u32 colvals[] = { COLOR_AUTO, 0x000000, 0x7F7F7F, 0xC00000, 0xFF0000, 0xFFC000, 0x00B050, 0x0070C0, 0x002060, 0x7030A0 };
static const char *const colnames[] = { "Automatic", "Black", "Grey", "Dark Red", "Red", "Gold", "Green", "Blue", "Dark Blue", "Purple" };
#define NCOLS (int)(sizeof colvals / sizeof colvals[0])
static const u8 widths8[] = { 2, 4, 6, 8, 12, 18, 24, 36, 48 };
static const char *const widthnames[] = { "1/4 pt", "1/2 pt", "3/4 pt", "1 pt", "1 1/2 pt", "2 1/4 pt", "3 pt", "4 1/2 pt", "6 pt" };
#define NWIDTHS (int)(sizeof widths8 / sizeof widths8[0])
static Object *pbwin, *b_style, *b_art, *b_col, *b_width, *b_side[4], *b_apply, *b_from, *b_sp[4], *b_hdr, *b_ftr, *b_front;
static void open_borders(void)
{
  static const char *kinds[32], *arts[16], *cols[NCOLS + 1], *wl[NWIDTHS + 1], *papply[5], *froms[3];
  Doc *d = D();
  int k, lastsec;
  const SectFmt *s;
  const PageBorder *pb;
  PBSide sd;
  if (!pbwin) {
    Object *g;
    papply[0] = tr("Whole document"); papply[1] = tr("This section"); papply[2] = tr("This section - First page only");
    papply[3] = tr("This section - All except first page"); papply[4] = NULL;
    froms[0] = tr("Edge of page"); froms[1] = tr("Text"); froms[2] = NULL;
    kinds[0] = tr("None");
    for (k = 0; k < pages_pb_nvals; k++) kinds[k + 1] = tr(kindnames[k]);
    kinds[pages_pb_nvals + 1] = NULL;
    arts[0] = tr("None");
    for (k = 0; k < pages_pb_narts; k++) arts[k + 1] = tr(artnames[k]);
    arts[pages_pb_narts + 1] = NULL;
    for (k = 0; k < NCOLS; k++) cols[k] = tr(colnames[k]);
    cols[NCOLS] = NULL;
    for (k = 0; k < NWIDTHS; k++) wl[k] = widthnames[k];
    wl[NWIDTHS] = NULL;
    g = VGroup,
      Child, ColGroup(2),
        Child, Label1(tr("Style")), Child, b_style = cyc_obj(kinds),
        Child, Label1(tr("Art")), Child, b_art = cyc_obj(arts),
        Child, Label1(tr("Colour")), Child, b_col = cyc_obj(cols),
        Child, Label1(tr("Width")), Child, b_width = cyc_obj(wl),
      End,
      Child, QTitleObject(tr("Sides")),
      Child, HGroup,
        Child, b_side[PBS_TOP] = CheckMark(TRUE), Child, LLabel1(tr("Top")),
        Child, b_side[PBS_BOTTOM] = CheckMark(TRUE), Child, LLabel1(tr("Bottom")),
        Child, b_side[PBS_LEFT] = CheckMark(TRUE), Child, LLabel1(tr("Left")),
        Child, b_side[PBS_RIGHT] = CheckMark(TRUE), Child, LLabel1(tr("Right")), Child, HSpace(0),
      End,
      Child, QTitleObject(tr("Options")),
      Child, ColGroup(4),
        Child, Label2(tr("Top")), Child, b_sp[PBS_TOP] = str_obj(8), Child, Label2(tr("Bottom")), Child, b_sp[PBS_BOTTOM] = str_obj(8),
        Child, Label2(tr("Left")), Child, b_sp[PBS_LEFT] = str_obj(8), Child, Label2(tr("Right")), Child, b_sp[PBS_RIGHT] = str_obj(8),
      End,
      Child, ColGroup(2),
        Child, Label1(tr("Measure from")), Child, b_from = cyc_obj(froms),
        Child, Label1(tr("Surround header")), Child, HGroup, Child, b_hdr = CheckMark(TRUE), Child, HSpace(0), End,
        Child, Label1(tr("Surround footer")), Child, HGroup, Child, b_ftr = CheckMark(TRUE), Child, HSpace(0), End,
        Child, Label1(tr("Always display in front")), Child, HGroup, Child, b_front = CheckMark(TRUE), Child, HSpace(0), End,
        Child, Label1(tr("Apply to")), Child, b_apply = cyc_obj(papply),
      End,
    End;
    if (!g || !(pbwin = wp_dialog("Page Borders", MAKE_ID('P','B','D','R'), g, "_OK", ID_PG_BORDERGO))) return;
  }
  s = caret_sect(&lastsec);
  pb = pages_sect_border(d, s, lastsec);
  set(b_style, MUIA_Cycle_Active, 1); set(b_art, MUIA_Cycle_Active, 0); set(b_col, MUIA_Cycle_Active, 0); set(b_width, MUIA_Cycle_Active, 1);
  for (k = 0; k < 4; k++) {
    pages_pb_side(pb, k, &sd);
    set(b_side[k], MUIA_Selected, pb->style == PB_NONE || sd.style != PB_NONE);
    set_pt(b_sp[k], pb->style == PB_NONE ? (k == PBS_LEFT || k == PBS_RIGHT ? 480 : 480) : sd.space);
  }
  for (k = 0; k < 4; k++) {
    int j;
    pages_pb_side(pb, k, &sd);
    if (sd.style == PB_NONE) continue;
    if (pages_pb_art(sd.val)) { for (j = 0; j < pages_pb_narts; j++) if (!strcmp(sd.val, pages_pb_arts[j])) set(b_art, MUIA_Cycle_Active, j + 1); }
    else for (j = 0; j < pages_pb_nvals; j++) if (!strcmp(sd.val, pages_pb_vals[j])) set(b_style, MUIA_Cycle_Active, j + 1);
    for (j = 0; j < NCOLS; j++) if (colvals[j] == sd.color) set(b_col, MUIA_Cycle_Active, j);
    for (j = 0; j < NWIDTHS; j++) if (widths8[j] == sd.width) set(b_width, MUIA_Cycle_Active, j);
    break;
  }
  if (pb->style == PB_NONE) set(b_style, MUIA_Cycle_Active, 0);
  set(b_from, MUIA_Cycle_Active, pb->style == PB_NONE || pb->frompage ? 0 : 1);
  set(b_hdr, MUIA_Selected, !d->pbnohdr); set(b_ftr, MUIA_Selected, !d->pbnoftr); set(b_front, MUIA_Selected, !pb->back);
  set(b_apply, MUIA_Cycle_Active, s && s->pbown && !lastsec ? 1 + pb->display : AP_WHOLE);
  set(pbwin, MUIA_Window_Open, TRUE);
}
static void borders_done(void)
{
  Doc *d = D();
  LONG st = geti(b_style, MUIA_Cycle_Active), art = geti(b_art, MUIA_Cycle_Active), ap = geti(b_apply, MUIA_Cycle_Active);
  int mask = 0, k;
  PageBorder *pb, nb;
  const char *val = art > 0 ? pages_pb_arts[(art - 1) % pages_pb_narts] : st > 0 ? pages_pb_vals[(st - 1) % pages_pb_nvals] : "none";
  int width = art > 0 ? (int)widths8[geti(b_width, MUIA_Cycle_Active) % NWIDTHS] * 2 : (int)widths8[geti(b_width, MUIA_Cycle_Active) % NWIDTHS];
  for (k = 0; k < 4; k++) if (geti(b_side[k], MUIA_Selected)) mask |= 1 << k;
  set(pbwin, MUIA_Window_Open, FALSE);
  wp_story_close();
  memset(&nb, 0, sizeof nb);
  nb.display = (u8)(ap >= 2 ? ap - 1 : PBD_ALL);
  nb.back = (u8)!geti(b_front, MUIA_Selected);
  pages_pb_set(&nb, val, art > 0 ? (width > 31 ? 31 : width < 4 ? 4 : width) : width, colvals[geti(b_col, MUIA_Cycle_Active) % NCOLS], get_pt(b_sp[0]), mask, geti(b_from, MUIA_Cycle_Active) == 0);
  /* each side's own space */
  if (nb.style != PB_NONE) {
    PBSide sd[4];
    for (k = 0; k < 4; k++) pages_pb_side(&nb, k, &sd[k]);
    nb.own = 1;
    for (k = 0; k < 4; k++) { nb.side[k] = sd[k]; nb.side[k].space = get_pt(b_sp[k]); }
  }
  pb = border_to_change(ap == AP_WHOLE);
  *pb = nb;
  d->pbnohdr = (u8)!geti(b_hdr, MUIA_Selected); d->pbnoftr = (u8)!geti(b_ftr, MUIA_Selected);
  d->dirty = 1; d->ver++;
  set(wp_view(), MUIA_Quill_Changed, TRUE);
  relayout();
}

/* ------------------------------------------------------------------ */
/* Printed Watermark */
static Object *wmwin, *w_kind, *w_file, *w_scale, *w_wash, *w_text, *w_font, *w_col, *w_semi, *w_lay;
static char wm_path[256];
static const char *const fonts[] = { "Calibri", "Arial", "Times New Roman", "Courier New", "Georgia", "Verdana" };
#define NFONTS (int)(sizeof fonts / sizeof fonts[0])
static const UWORD scales[] = { 0, 50, 100, 150, 200, 500 };
#define NSCALES (int)(sizeof scales / sizeof scales[0])
static const u32 wmcols[] = { 0xC0C0C0, 0x808080, 0x000000, 0xFF0000, 0x0070C0, 0x00B050 };
static const char *const wmcolnames[] = { "Silver", "Grey", "Black", "Red", "Blue", "Green" };
#define NWMCOLS (int)(sizeof wmcols / sizeof wmcols[0])
static void wm_enable(void)
{
  LONG k = geti(w_kind, MUIA_Cycle_Active);
  set(w_file, MUIA_Disabled, k != 1); set(w_scale, MUIA_Disabled, k != 1); set(w_wash, MUIA_Disabled, k != 1);
  set(w_text, MUIA_Disabled, k != 2); set(w_font, MUIA_Disabled, k != 2); set(w_col, MUIA_Disabled, k != 2);
  set(w_semi, MUIA_Disabled, k != 2); set(w_lay, MUIA_Disabled, k != 2);
}
static void open_watermark(void)
{
  static const char *kinds[4], *fl[NFONTS + 1], *sl[NSCALES + 1], *cl[NWMCOLS + 1], *lays[3];
  static char scaletxt[NSCALES][8];
  Doc *d = D();
  char lat[80];
  int k;
  if (!wmwin) {
    Object *g, *pick;
    kinds[0] = tr("No watermark"); kinds[1] = tr("Picture watermark"); kinds[2] = tr("Text watermark"); kinds[3] = NULL;
    for (k = 0; k < NFONTS; k++) fl[k] = fonts[k];
    fl[NFONTS] = NULL;
    sl[0] = tr("Auto");
    for (k = 1; k < NSCALES; k++) { sprintf(scaletxt[k], "%u%%", (unsigned)scales[k]); sl[k] = scaletxt[k]; }
    sl[NSCALES] = NULL;
    for (k = 0; k < NWMCOLS; k++) cl[k] = tr(wmcolnames[k]);
    cl[NWMCOLS] = NULL;
    lays[0] = tr("Diagonal"); lays[1] = tr("Horizontal"); lays[2] = NULL;
    g = VGroup,
      Child, HGroup, Child, w_kind = cyc_obj(kinds), Child, HSpace(0), End,
      Child, QTitleObject(tr("Picture")),
      Child, ColGroup(2),
        Child, Label1(tr("Picture")), Child, HGroup,
          Child, w_file = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMM", End,
          Child, pick = QButton(tr("_Select Picture...")),
        End,
        Child, Label1(tr("Scale")), Child, HGroup, Child, w_scale = cyc_obj(sl), Child, w_wash = CheckMark(TRUE), Child, LLabel1(tr("Washout")), Child, HSpace(0), End,
      End,
      Child, QTitleObject(tr("Text")),
      Child, ColGroup(2),
        Child, Label2(tr("Text")), Child, w_text = StringObject, StringFrame, MUIA_String_MaxLen, 60, MUIA_CycleChain, 1, End,
        Child, Label1(tr("Font")), Child, w_font = cyc_obj(fl),
        Child, Label1(tr("Colour")), Child, HGroup, Child, w_col = cyc_obj(cl), Child, w_semi = CheckMark(TRUE), Child, LLabel1(tr("Semitransparent")), Child, HSpace(0), End,
        Child, Label1(tr("Layout")), Child, w_lay = cyc_obj(lays),
      End,
    End;
    if (!g || !(wmwin = wp_dialog("Printed Watermark", MAKE_ID('W','M','K','2'), g, "_OK", ID_PG_WMGO))) return;
    notify_id(w_kind, MUIA_Cycle_Active, ID_PG_WMUPD);
    DoMethod(pick, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PG_WMPICK);
  }
  wm_path[0] = 0;
  set(w_kind, MUIA_Cycle_Active, d->wm.pic ? 1 : d->wm.text[0] ? 2 : 0);
  set(w_file, MUIA_Text_Contents, (ULONG)(d->wm.pic ? tr("(the document's)") : ""));
  set(w_scale, MUIA_Cycle_Active, 0);
  for (k = 0; k < NSCALES; k++) if (scales[k] == d->wm.scale) set(w_scale, MUIA_Cycle_Active, k);
  set(w_wash, MUIA_Selected, d->wm.pic ? d->wm.washout : 1);
  utf8_to_latin1(d->wm.text[0] ? d->wm.text : "ASAP", d->wm.text[0] ? (u32)strlen(d->wm.text) : 4, lat, sizeof lat);
  set(w_text, MUIA_String_Contents, (ULONG)lat);
  set(w_font, MUIA_Cycle_Active, 0);
  for (k = 0; k < NFONTS; k++) if (!strcmp(fonts[k], d->wm.font)) set(w_font, MUIA_Cycle_Active, k);
  set(w_col, MUIA_Cycle_Active, 0);
  for (k = 0; k < NWMCOLS; k++) if (wmcols[k] == d->wm.color) set(w_col, MUIA_Cycle_Active, k);
  set(w_semi, MUIA_Selected, !d->wm.opaque);
  set(w_lay, MUIA_Cycle_Active, d->wm.text[0] && d->wm.horizontal ? 1 : 0);
  wm_enable();
  set(wmwin, MUIA_Window_Open, TRUE);
}
static void wm_pick(void)
{
  if (!wp_file_request(0, "Select Picture", wm_path, sizeof wm_path, "#?.(png|jpg|jpeg)")) return;
  set(w_file, MUIA_Text_Contents, (ULONG)FilePart((STRPTR)wm_path));
  set(w_kind, MUIA_Cycle_Active, 1);
}
static void wm_done(void)
{
  Doc *d = D();
  LONG k = geti(w_kind, MUIA_Cycle_Active);
  set(wmwin, MUIA_Window_Open, FALSE);
  wp_story_close();
  if (k == 0) pages_wm_clear(d);
  else if (k == 1) {
    if (wm_path[0]) {
      size_t n = 0;
      u8 *data = file_read(wm_path, &n);
      if (!data || !pages_wm_picture(d, data, (u32)n)) { free(data); wp_ask("OK", "That picture can't be read (PNG or JPEG only).", NULL); return; }
      free(data);
    } else if (!d->wm.pic) { DisplayBeep(NULL); return; }
    d->wm.scale = scales[geti(w_scale, MUIA_Cycle_Active) % NSCALES];
    d->wm.washout = (u8)geti(w_wash, MUIA_Selected);
  } else {
    char *t = NULL;
    get(w_text, MUIA_String_Contents, &t);
    if (!t || !t[0]) { DisplayBeep(NULL); return; }
    free(d->wm.pic); d->wm.pic = NULL; d->wm.piclen = 0;
    wp_latin1_utf8(t, d->wm.text, sizeof d->wm.text);
    strcpy(d->wm.font, fonts[geti(w_font, MUIA_Cycle_Active) % NFONTS]);
    d->wm.color = wmcols[geti(w_col, MUIA_Cycle_Active) % NWMCOLS];
    d->wm.opaque = (u8)!geti(w_semi, MUIA_Selected);
    d->wm.horizontal = (u8)(geti(w_lay, MUIA_Cycle_Active) == 1);
  }
  d->dirty = 1; d->ver++;
  set(wp_view(), MUIA_Quill_Changed, TRUE);
  relayout();
}

/* ------------------------------------------------------------------ */
/* Line Numbers, with Apply to */
static Object *lnwin, *l_on, *l_start, *l_dist, *l_by, *l_restart, *l_apply;
static void open_linenum(void)
{
  static const char *rs[4];
  const SectFmt *sf = caret_sect(NULL);
  char b[16];
  if (!lnwin) {
    Object *g;
    applies_init();
    rs[0] = tr("Restart Each Page"); rs[1] = tr("Restart Each Section"); rs[2] = tr("Continuous"); rs[3] = NULL;
    g = VGroup,
      Child, HGroup, Child, l_on = CheckMark(FALSE), Child, LLabel1(tr("Add line numbers")), Child, HSpace(0), End,
      Child, ColGroup(2),
        Child, Label2(tr("Start at")), Child, l_start = StringObject, StringFrame, MUIA_String_MaxLen, 6, MUIA_String_Accept, (ULONG)"0123456789", MUIA_CycleChain, 1, End,
        Child, Label2(tr("From text")), Child, l_dist = str_obj(12),
        Child, Label2(tr("Count by")), Child, l_by = StringObject, StringFrame, MUIA_String_MaxLen, 4, MUIA_String_Accept, (ULONG)"0123456789", MUIA_CycleChain, 1, End,
        Child, Label1(tr("Numbering")), Child, l_restart = cyc_obj(rs),
        Child, Label1(tr("Apply to")), Child, l_apply = cyc_obj(applies),
      End, End;
    if (!g || !(lnwin = wp_dialog("Line Numbers", MAKE_ID('L','N','U','2'), g, "_OK", ID_PG_LINENUMGO))) return;
  }
  set(l_on, MUIA_Selected, sf->linenum != LN_NONE);
  sprintf(b, "%u", (unsigned)(sf->lnstart > 1 ? sf->lnstart : 1)); set(l_start, MUIA_String_Contents, (ULONG)b);
  sprintf(b, "%u", (unsigned)(sf->lnby ? sf->lnby : 1)); set(l_by, MUIA_String_Contents, (ULONG)b);
  set_len(l_dist, sf->lndist > 0 ? sf->lndist : 360);
  set(l_restart, MUIA_Cycle_Active, sf->linenum == LN_SECTION ? 1 : sf->linenum == LN_CONT ? 2 : 0);
  set(l_apply, MUIA_Cycle_Active, last.hassel ? AP_SELECTED : AP_WHOLE);
  set(lnwin, MUIA_Window_Open, TRUE);
}
static struct { u8 linenum, by; u16 start; i32 dist; } lnnew;
static void ln_fn(SectFmt *s, int lastsec, void *arg) { s->linenum = lnnew.linenum; s->lnby = lnnew.by; s->lnstart = lnnew.start; s->lndist = lnnew.dist; }
static void linenum_done(void)
{
  static const u8 rs[3] = { LN_PAGE, LN_SECTION, LN_CONT };
  long v;
  int scope = (int)geti(l_apply, MUIA_Cycle_Active);
  set(lnwin, MUIA_Window_Open, FALSE);
  lnnew.linenum = geti(l_on, MUIA_Selected) ? rs[geti(l_restart, MUIA_Cycle_Active) % 3] : LN_NONE;
  v = get_num(l_start); lnnew.start = (u16)(v < 1 ? 1 : v > 32000 ? 32000 : v);
  v = get_num(l_by); lnnew.by = (u8)(v < 1 ? 1 : v > 100 ? 100 : v);
  lnnew.dist = get_len(l_dist);
  if (lnnew.dist <= 0 || lnnew.dist >= 2880) lnnew.dist = 360;
  if (scope == AP_SELECTED && !last.hassel) scope = AP_FORWARD;
  apply_sect(scope, SB_CONT, ln_fn, NULL);
}

/* ------------------------------------------------------------------ */
/* Hyphenation options and manual hyphenation */
static Object *hywin, *h_auto, *h_caps, *h_zone, *h_limit;
static int hyph_patterns(void)
{
  if (!hyph_ready()) hyph_load("PROGDIR:Dict/hyph_en_GB.dic");
  return hyph_ready();
}
static void open_hyphopts(void)
{
  Doc *d = D();
  if (!hywin) {
    Object *g, *man;
    g = VGroup,
      Child, ColGroup(2),
        Child, Label1(tr("Automatically hyphenate document")), Child, HGroup, Child, h_auto = CheckMark(FALSE), Child, HSpace(0), End,
        Child, Label1(tr("Hyphenate words in CAPS")), Child, HGroup, Child, h_caps = CheckMark(TRUE), Child, HSpace(0), End,
        Child, Label2(tr("Hyphenation zone")), Child, h_zone = str_obj(12),
        Child, Label2(tr("Limit consecutive hyphens to")), Child, h_limit = StringObject, StringFrame, MUIA_String_MaxLen, 3, MUIA_String_Accept, (ULONG)"0123456789", MUIA_CycleChain, 1, End,
      End,
      Child, LLabel1(tr("(0: no limit)")),
      Child, HGroup, Child, man = QButton(tr("_Manual...")), Child, HSpace(0), End,
    End;
    if (!g || !(hywin = wp_dialog("Hyphenation", MAKE_ID('H','Y','P','H'), g, "_OK", ID_PG_HYPHGO))) return;
    DoMethod(man, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PG_MANUAL);
  }
  set(h_auto, MUIA_Selected, d->hyphenate);
  set(h_caps, MUIA_Selected, !d->hynocaps);
  set_len(h_zone, pages_hyph_zone(d));
  set_num(h_limit, d->hylimit);
  set(hywin, MUIA_Window_Open, TRUE);
}
static void hyph_done(void)
{
  Doc *d = D();
  i32 z = get_len(h_zone);
  long lim = get_num(h_limit);
  set(hywin, MUIA_Window_Open, FALSE);
  if (geti(h_auto, MUIA_Selected)) hyph_patterns();
  d->hyphenate = (u8)geti(h_auto, MUIA_Selected);
  d->hynocaps = (u8)!geti(h_caps, MUIA_Selected);
  d->hyzone = z > 0 && z < 14400 && z != 360 ? z : 0;
  d->hylimit = (u8)(lim < 0 ? 0 : lim > 99 ? 99 : lim);
  pages_hyph_changed(d);
  d->dirty = 1;
  set(wp_view(), MUIA_Quill_Changed, TRUE);
  relayout();
}
static Object *mhwin, *m_word, *m_at;
static Pos mh_at;                            /* the word asked about */
static u32 mh_len, mh_sug, mh_n;
static u8 mh_brk[40];
static char mh_text[40][48];                 /* the word with a hyphen at each place it may go */
static u32 mh_off[40];
/* the choice shown: the word with the hyphen where the slider says */
static void manual_show(void)
{
  LONG a = geti(m_at, MUIA_Numeric_Value) - 1;
  char lat[64];
  if (a < 0 || (u32)a >= mh_n) a = 0;
  utf8_to_latin1(mh_text[a], (u32)strlen(mh_text[a]), lat, sizeof lat);
  set(m_word, MUIA_Text_Contents, (ULONG)lat);
}
/* the next word from p on, asked about (the window filled); none: done */
static void manual_next(Pos p)
{
  Layout *L = NULL;
  Doc *d = D();
  u32 k, n = 0;
  int act = 0;
  get(wp_view(), MUIA_Quill_Layout, &L);
  if (!L || !pages_next_manual(L, p, &mh_at, &mh_len, mh_brk, &mh_sug)) {
    if (mhwin) set(mhwin, MUIA_Window_Open, FALSE);
    wp_ask("OK", "Hyphenation is complete.", NULL);
    return;
  }
  for (k = 1; k < mh_len && n < 40; k++) {
    u32 j, o = 0;
    if (!mh_brk[k]) continue;
    for (j = 0; j < mh_len && o < 45; j++) { if (j == k) mh_text[n][o++] = '-'; mh_text[n][o++] = d->p[mh_at.para]->t[mh_at.off + j]; }
    mh_text[n][o] = 0;
    mh_off[n] = k;
    if (k == mh_sug) act = (int)n;
    n++;
  }
  mh_n = n;
  if (!mhwin) {
    Object *g, *yes, *no, *cancel;
    g = VGroup, QDLG, Child, ColGroup(2),
        Child, Label1(tr("Hyphenate at")), Child, m_word = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMMMMMM", End,
        Child, Label1(tr("Place")), Child, m_at = SliderObject, MUIA_Numeric_Min, 1, MUIA_Numeric_Max, 2, MUIA_Numeric_Value, 1, MUIA_CycleChain, 1, End,
      End,
      Child, HGroup, Child, yes = QButton(tr("_Yes")), Child, no = QButton(tr("_No")), Child, HSpace(0), Child, cancel = QButton(tr("_Cancel")), End,
    End;
    if (!g) return;
    mhwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Manual Hyphenation"), MUIA_Window_ID, MAKE_ID('M','H','Y','P'), WindowContents, g, End;
    if (!mhwin) return;
    DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)mhwin);
    DoMethod(mhwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)mhwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(cancel, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)mhwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(yes, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PG_MYES);
    DoMethod(no, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PG_MNO);
    notify_id(m_at, MUIA_Numeric_Value, ID_PG_MUPD);
  }
  nnset(m_at, MUIA_Numeric_Max, n > 1 ? n : 2);
  nnset(m_at, MUIA_Numeric_Value, act + 1);
  set(m_at, MUIA_Disabled, n < 2);
  manual_show();
  set(wp_view(), MUIA_Quill_Caret, (ULONG)&mh_at);
  set(mhwin, MUIA_Window_Open, TRUE);
}
static void manual_answer(int yes)
{
  Doc *d = D();
  Pos p = mh_at;
  if (yes) {
    LONG a = geti(m_at, MUIA_Numeric_Value) - 1;
    Pos at = mh_at;
    at.off += mh_off[a >= 0 && (u32)a < mh_n ? a : 0];
    doc_begin(d, at);
    doc_insert(d, &at, "\xC2\xAD", 2, NULL);       /* an optional hyphen (U+00AD) */
    doc_end(d, at);
    p.off += mh_len + 2;
    relayout();
  } else p.off += mh_len;
  manual_next(p);
}
static void open_manual(void)
{
  if (!hyph_patterns()) { wp_ask("OK", "The hyphenation patterns (Dict/hyph_en_GB.dic) can't be read.", NULL); return; }
  if (hywin) set(hywin, MUIA_Window_Open, FALSE);
  wp_story_close();
  manual_next(caret());
}

/* ------------------------------------------------------------------ */
/* the package */
static void fit_page(void);
int wp22a_command(ULONG id, LONG value)
{
  switch (id) {
  case ID_PG_COLUMNS: open_columns(); return WPR_KEEPFOCUS;
  case ID_PG_COLLIST:
    if (value == 4) col_preset(CP_LEFT);
    else if (value == 5) col_preset(CP_RIGHT);
    else if (value == 6) { open_columns(); return WPR_KEEPFOCUS; }
    break;
  case ID_PG_COLGO: col_done(); break;
  case ID_PG_COLPRE: col_update((int)geti(c_pre, MUIA_Cycle_Active)); return WPR_KEEPFOCUS;
  case ID_PG_COLNUM: col_update(-1); return WPR_KEEPFOCUS;
  case ID_PG_SETUP: open_setup(value >= 0 && value < 3 ? (int)value : -1); return WPR_KEEPFOCUS;
  case ID_PG_SETUPGO: setup_done(); break;
  case ID_PG_SETUPUPD: ps_update(); return WPR_KEEPFOCUS;
  case ID_PG_BORDERS: open_borders(); return WPR_KEEPFOCUS;
  case ID_PG_BORDERGO: borders_done(); break;
  case ID_PG_WATERMARK: open_watermark(); return WPR_KEEPFOCUS;
  case ID_PG_WMGO: wm_done(); break;
  case ID_PG_WMPICK: wm_pick(); return WPR_KEEPFOCUS;
  case ID_PG_WMUPD: wm_enable(); return WPR_KEEPFOCUS;
  case ID_PG_HYPHOPTS: open_hyphopts(); return WPR_KEEPFOCUS;
  case ID_PG_HYPHGO: hyph_done(); break;
  case ID_PG_HYPHLIST: if (value == 2) open_manual(); else if (value == 3) open_hyphopts(); return WPR_KEEPFOCUS;
  case ID_PG_MANUAL: open_manual(); return WPR_KEEPFOCUS;
  case ID_PG_MYES: manual_answer(1); return WPR_KEEPFOCUS;
  case ID_PG_MNO: manual_answer(0); return WPR_KEEPFOCUS;
  case ID_PG_MUPD: manual_show(); return WPR_KEEPFOCUS;
  case ID_PG_LINENUM: open_linenum(); return WPR_KEEPFOCUS;
  case ID_PG_LINENUMGO: linenum_done(); break;
  case ID_PG_FITPAGE: fit_page(); break;
  case ID_PG_JUSTIFY: psnew.start = caret_sect(NULL)->start; psnew.valign = VA_JUSTIFY; psnew.first = caret_sect(NULL)->titlepg; apply_sect(AP_SECTION, SB_NEXT, ps_fn, NULL); break;
  }
  return WPR_DONE;
}
/* Fit One Page: the document a page shorter, or why not */
static void fit_page(void)
{
  Doc *d = wp_doc();
  u32 before = 0, after = 0;
  char n[16];
  int r;
  if (!d) return;
  wp_story_close();
  d = wp_doc();
  d->rvtrack = 0;
  { struct Toolbar *tb = wp_toolbar(); if (tb && (tb->uibits & RBIT(RB_TRACK))) d->rvtrack = 1; }
  r = fit_one_page(d, wp_fonts(), &before, &after);
  switch (r) {
  case FIT_OK: { Pos c = doc_start(d); DoMethod(wp_view(), MUIM_Quill_Relayout); set(wp_view(), MUIA_Quill_Caret, (ULONG)&c); break; }
  case FIT_ONEPAGE: wp_ask("OK", "The document already fits on one page.", NULL); break;
  case FIT_BREAKS: wp_ask("OK", "Page, column or section breaks make the pages: Fit One Page can't take one away.", NULL); break;
  case FIT_TOOFULL: sprintf(n, "%lu", (unsigned long)before); wp_ask("OK", "The last of the %s pages holds too much to fit onto the pages before it.", n); break;
  case FIT_TRACKED: wp_ask("OK", "Fit One Page can't be tracked: turn Track Changes off first.", NULL); break;
  default: wp_ask("OK", "The document can't be brought onto a page less without making its text too small.", NULL); break;
  }
}
/* the caret and selection, kept for Apply to's choices */
static void wp22a_state(ULONG *on, const WpCtx *c)
{
  last = *c;
  *on = 0;
}
static const WpMenu wp22a_menus[] = {
  { WM_LAYOUT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_LAYOUT, { NM_ITEM, (STRPTR)"Columns...", 0, 0, 0, (APTR)ID_PG_COLUMNS } },
  { WM_LAYOUT, { NM_ITEM, (STRPTR)"Line Numbering Options...", 0, 0, 0, (APTR)ID_PG_LINENUM } },
  { WM_LAYOUT, { NM_ITEM, (STRPTR)"Fit One Page", 0, 0, 0, (APTR)ID_PG_FITPAGE } },
  { WM_LAYOUT, { NM_ITEM, (STRPTR)"Hyphenation", 0, 0, 0, 0 } },
  { WM_LAYOUT, { NM_SUB, (STRPTR)"Hyphenation Options...", 0, 0, 0, (APTR)ID_PG_HYPHOPTS } },
  { WM_LAYOUT, { NM_SUB, (STRPTR)"Manual...", 0, 0, 0, (APTR)ID_PG_MANUAL } },
  { WM_LAYOUT, { NM_ITEM, (STRPTR)"Page Background", 0, 0, 0, 0 } },
  { WM_LAYOUT, { NM_SUB, (STRPTR)"Page Borders...", 0, 0, 0, (APTR)ID_PG_BORDERS } },
  { WM_LAYOUT, { NM_SUB, (STRPTR)"Printed Watermark...", 0, 0, 0, (APTR)ID_PG_WATERMARK } },
};
/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp22a_package = {
  "WP22a", ID_WP22A_FIRST, ID_WP22A_LAST,
  wp22a_command, wp22a_state, NULL, NULL, NULL, NULL, NULL,
  NULL, 0,
  wp22a_menus, (int)(sizeof wp22a_menus / sizeof wp22a_menus[0])
};
