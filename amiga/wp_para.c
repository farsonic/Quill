/*
 * wp_para.c - WP10 Paragraph and Home quick commands (docs/wordcraft-gap.md). This file belongs to WP10:
 * only that package edits it. Its commands are ID_WP10_FIRST .. ID_WP10_LAST (named in wp_charpara.h);
 * how the hooks below are called, and how to add ribbon groups, menu items, context-menu items and
 * keys: wphooks.h.
 *
 * Distributed alignment, Add Space Before and Remove Space After, Horizontal Line, Hanging Indent,
 * the Bullet and Numbering libraries with Define New Bullet and Define New Number Format, Sort Text,
 * nonbreaking spaces and hyphens and optional hyphens, the Symbol window, Paste Special, Copy and
 * Paste Formatting and the Clipboard pane.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <exec/memory.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include <libraries/iffparse.h>
#include "wp_charpara.h"
#include "lang.h"
#include "../core/charfx.h"

#undef Label1
#undef LLabel1
#undef Label2
#define Label1(l)  QLabel(l, MUIO_Label_SingleFrame)
#define LLabel1(l) QLabel(l, MUIO_Label_LeftAligned | MUIO_Label_SingleFrame)
#define Label2(l)  QLabel(l, MUIO_Label_DoubleFrame)

static const char **trl(const char *const *en, const char **out) { int i; for (i = 0; en[i]; i++) out[i] = tr(en[i]); out[i] = NULL; return out; }
static ULONG cyc(Object *o) { ULONG v = 0; get(o, MUIA_Cycle_Active, &v); return v; }
static int chk(Object *o) { ULONG v = 0; get(o, MUIA_Selected, &v); return v != 0; }
static const char *str(Object *o) { char *s = NULL; get(o, MUIA_String_Contents, &s); return s ? s : ""; }

/* ---- the document */
static void sel_range(Pos *a, Pos *b)
{
  Pos *c = NULL, *an = NULL;
  get(wp_view(), MUIA_Quill_Caret, &c); get(wp_view(), MUIA_Quill_Anchor, &an);
  *a = c ? *c : doc_start(wp_doc()); *b = an ? *an : *a;
  if (pos_cmp(*a, *b) > 0) { Pos t = *a; *a = *b; *b = t; }
}
/* the view after the document was changed here: laid out afresh, the caret at c */
static void changed(Pos c)
{
  DoMethod(wp_view(), MUIM_Quill_Relayout);
  set(wp_view(), MUIA_Quill_Caret, (ULONG)&c);
}
/* every selected paragraph changed by fn (its formatting, given the PM_ mask fn returns), one undo step */
static void each_para(unsigned (*fn)(ParaFmt *pf))
{
  Doc *d = wp_doc();
  Pos a, b;
  u32 i;
  sel_range(&a, &b);
  doc_begin(d, a);
  for (i = a.para; i <= b.para && i < d->np; i++) {
    ParaFmt pf = d->p[i]->pf;
    unsigned m = fn(&pf);
    if (m) doc_para_format(d, i, i, m, &pf);
  }
  doc_end(d, a);
  DoMethod(wp_view(), MUIM_Quill_Relayout);
}
static int sb_on;        /* (each_para's toggles: on or off, from the first paragraph) */
static unsigned f_before(ParaFmt *pf) { pf->before = sb_on ? 240 : 0; return PM_SPACING; }
static unsigned f_after(ParaFmt *pf) { pf->after = sb_on ? 160 : 0; return PM_SPACING; }
static unsigned f_hang(ParaFmt *pf) { pf->left += 720; pf->first = -720; return PM_INDENT; }
static unsigned f_unhang(ParaFmt *pf) { pf->left = pf->left > 720 ? pf->left - 720 : 0; pf->first = 0; return PM_INDENT; }
/* text typed at the caret (UTF-8), replacing the selection, in f (NULL: as the text there) */
static void type_text(const char *u, const CharFmt *f)
{
  Doc *d = wp_doc();
  Pos a, b;
  if (!f) { DoMethod(wp_view(), MUIM_Quill_Command, CMD_TEXT, (ULONG)u); return; }
  sel_range(&a, &b);
  doc_begin(d, a);
  if (pos_cmp(a, b)) doc_delete(d, a, b);
  doc_insert(d, &a, u, (u32)strlen(u), f);
  doc_end(d, a);
  changed(a);
}
static void type_cp(u32 cp, const CharFmt *f)
{
  char u[8];
  u[utf8_put(u, cp)] = 0;
  type_text(u, f);
}

/* ---- the Bullets and Numbering lists */
enum { P_BU_DOT, P_BU_CIRCLE, P_BU_SQUARE, P_BU_DIAMOND, P_BU_ARROW, P_BU_TICK, P_BU_DASH, P_BU_NONE, P_BU_DEFINE, P_BU_N };
static const char *const bullets[P_BU_N] = { "\267  Bullet", "o  Circle", "#  Square", "<>  Diamond", ">  Arrowhead", "v  Tick", "-  Dash", "None",
                                             "Define New Bullet..." };
static const u32 bullet_cp[7] = { 0x2022, 0x25CB, 0x25AA, 0x25C6, 0x27A2, 0x2713, 0x2013 };
const RibbonList wp10_bullet_list = { bullets, P_BU_N, PL_TEXT, NULL };
enum { P_NU_DOT, P_NU_PAREN, P_NU_UROMAN, P_NU_ULETTER, P_NU_LLETTER, P_NU_LROMAN, P_NU_RESTART, P_NU_NONE, P_NU_DEFINE, P_NU_N };
static const char *const numbers[P_NU_N] = { "1. 2. 3.", "1) 2) 3)", "I. II. III.", "A. B. C.", "a) b) c)", "i. ii. iii.", "Restart at 1", "None",
                                             "Define New Number Format..." };
const RibbonList wp10_number_list = { numbers, P_NU_N, PL_TEXT, NULL };
/* a list of nine levels: indents 0.5 inch deeper each, the label hanging a quarter inch */
static void list_base(ListDef *l, int numbered)
{
  static const u8 nf[3] = { NF_DECIMAL, NF_LOWER_LETTER, NF_LOWER_ROMAN };
  static const u32 bu[3] = { 0x2022, 0x25CB, 0x25AA };
  int k;
  memset(l, 0, sizeof *l);
  for (k = 0; k < 9; k++) {
    ListLevel *v = &l->lvl[k];
    v->start = 1; v->left = 720 * (k + 1); v->hanging = 360;
    if (numbered) { v->fmt = nf[k % 3]; sprintf(v->text, "%%%d.", k + 1); }
    else { v->fmt = NF_BULLET; v->text[utf8_put(v->text, bu[k % 3])] = 0; }
  }
}
/* the caret paragraph's list, and its level (0, NULL if it isn't in one) */
static const ListDef *caret_list(int *level)
{
  Doc *d = wp_doc();
  Pos a, b;
  const ParaFmt *pf;
  sel_range(&a, &b);
  pf = &d->p[a.para]->pf;
  *level = pf->level;
  return pf->list && pf->list <= d->nlists ? &d->lists[pf->list - 1] : NULL;
}
/* the list to the selected paragraphs, as a list of its own (a new id: numId 0 would mean none) */
static void apply_list(const ListDef *l)
{
  static ListDef c;
  Doc *d = wp_doc();
  int k, maxid = 0;
  c = *l;
  for (k = 0; k < d->nlists; k++) if (d->lists[k].id > maxid) maxid = d->lists[k].id;
  c.id = (u16)(maxid + 1);
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_LISTDEF, (ULONG)&c);
}
/* the selected paragraphs out of their list */
static void list_none(void)
{
  Doc *d = wp_doc();
  Pos a, b;
  sel_range(&a, &b);
  doc_begin(d, a);
  doc_list_apply(d, a.para, b.para, 0);
  doc_end(d, a);
  DoMethod(wp_view(), MUIM_Quill_Relayout);
}
static void bullet_choice(LONG v)
{
  ListDef l;
  int level;
  const ListDef *cl = caret_list(&level);
  if (v == P_BU_NONE) { list_none(); return; }
  if (v < 0 || v > P_BU_DASH) return;
  /* a bulleted list: its level gets the new bullet; else a new list */
  if (cl && cl->lvl[level].fmt == NF_BULLET) l = *cl; else { list_base(&l, 0); level = 0; }
  l.lvl[level].text[utf8_put(l.lvl[level].text, bullet_cp[v])] = 0;
  l.lvl[level].font[0] = 0;
  apply_list(&l);
}
static void number_choice(LONG v)
{
  static const u8 nf[6] = { NF_DECIMAL, NF_DECIMAL, NF_UPPER_ROMAN, NF_UPPER_LETTER, NF_LOWER_LETTER, NF_LOWER_ROMAN };
  static const char *const txt[6] = { "%1.", "%1)", "%1.", "%1.", "%1)", "%1." };
  ListDef l;
  int level;
  const ListDef *cl = caret_list(&level);
  if (v == P_NU_NONE) { list_none(); return; }
  if (v == P_NU_RESTART) { DoMethod(wp_view(), MUIM_Quill_Command, CMD_LISTRESTART, 0); return; }
  if (v < 0 || v > P_NU_LROMAN) return;
  if (cl && cl->lvl[level].fmt != NF_BULLET) l = *cl; else { list_base(&l, 1); level = 0; }
  l.lvl[level].fmt = nf[v];
  sprintf(l.lvl[level].text, "%%%d%s", level + 1, txt[v] + 2);
  apply_list(&l);
}

/* ---- Define New Bullet */
static Object *bulwin, *bu_preset, *bu_code, *bu_font;
static const char *const bu_presets[] = { "Bullet", "White circle", "Small square", "Black diamond", "Arrowhead", "Tick", "En dash",
                                          "Middle dot", "Black square", "White square", "Black star", "Rightwards arrow", "Guillemet", NULL };
static const u32 bu_preset_cp[] = { 0x2022, 0x25CB, 0x25AA, 0x25C6, 0x27A2, 0x2713, 0x2013, 0x00B7, 0x25A0, 0x25A1, 0x2605, 0x2192, 0x00BB };
static const char *t_bu_presets[16];
static const char *fontlist[96], *t_fontlist[96];
/* the font choices: the text's own font, then Quill's list (ribbon.c) */
static const char *const *font_entries(void)
{
  const char *const *f = ribbon_font_names(NULL);
  int i, n = 0;
  if (fontlist[0]) return fontlist;
  fontlist[n++] = "(Text font)";
  for (i = 0; f && f[i] && n < 94; i++) fontlist[n++] = f[i];
  fontlist[n] = NULL;
  return fontlist;
}
static Object *font_cycle(void)
{
  const char *const *f = font_entries();
  int i;
  for (i = 0; f[i]; i++) t_fontlist[i] = i ? f[i] : tr(f[i]);
  t_fontlist[i] = NULL;
  return CycleObject, MUIA_Cycle_Entries, (ULONG)t_fontlist, MUIA_CycleChain, 1, End;
}
/* a code typed as hex (U+, 0x allowed); 0 if none */
static u32 hex_code(const char *s)
{
  u32 v = 0;
  while (*s == ' ') s++;
  if ((s[0] == 'U' || s[0] == 'u') && s[1] == '+') s += 2;
  else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
  for (; *s; s++) {
    int c = *s, dv = c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1;
    if (dv < 0 || v > 0x10FFFF) break;
    v = v * 16 + (u32)dv;
  }
  return v <= 0x10FFFF ? v : 0;
}
static void open_bullet(void)
{
  if (!bulwin) {
    Object *bsym, *c = ColGroup(2),
      Child, Label1(tr("Bullet")), Child, bu_preset = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(bu_presets, t_bu_presets), MUIA_CycleChain, 1, End,
      Child, Label2(tr("Character code")), Child, HGroup,
        Child, bu_code = StringObject, StringFrame, MUIA_String_MaxLen, 10, MUIA_String_Accept, (ULONG)"0123456789abcdefABCDEFxU+", MUIA_CycleChain, 1, End,
        Child, bsym = QButton(tr("Symbol...")),
      End,
      Child, Label1(tr("Font")), Child, bu_font = font_cycle(),
    End;
    if (!c || !(bulwin = wp_dialog("Define New Bullet", MAKE_ID('B','U','L','D'), c, "_OK", ID_P_BULLETGO))) return;
    DoMethod(bsym, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_P_BULLETSYM);
  }
  set(bu_code, MUIA_String_Contents, (ULONG)"");
  set(bulwin, MUIA_Window_Open, TRUE);
}
static void bullet_done(void)
{
  ListDef l;
  int level;
  const ListDef *cl = caret_list(&level);
  u32 cp = hex_code(str(bu_code));
  ULONG p = cyc(bu_preset), fi = cyc(bu_font);
  set(bulwin, MUIA_Window_Open, FALSE);
  set(wp_win(), MUIA_Window_Activate, TRUE);
  if (!cp || cp < 0x20) cp = bu_preset_cp[p < sizeof bu_preset_cp / sizeof bu_preset_cp[0] ? p : 0];
  if (cl && cl->lvl[level].fmt == NF_BULLET) l = *cl; else { list_base(&l, 0); level = 0; }
  l.lvl[level].text[utf8_put(l.lvl[level].text, cp)] = 0;
  if (fi > 0 && fontlist[fi]) { strncpy(l.lvl[level].font, fontlist[fi], sizeof l.lvl[level].font - 1); l.lvl[level].font[sizeof l.lvl[level].font - 1] = 0; }
  else l.lvl[level].font[0] = 0;
  apply_list(&l);
}

/* ---- Define New Number Format */
static Object *numwin, *nu_style, *nu_format, *nu_start, *nu_font;
static const char *const nu_styles[] = { "1, 2, 3, ...", "I, II, III, ...", "i, ii, iii, ...", "A, B, C, ...", "a, b, c, ...", "1st, 2nd, 3rd, ...",
                                         "One, Two, Three, ...", "First, Second, Third, ...", "01, 02, 03, ...", "(none)", NULL };
static const u8 nu_nf[] = { NF_DECIMAL, NF_UPPER_ROMAN, NF_LOWER_ROMAN, NF_UPPER_LETTER, NF_LOWER_LETTER, NF_ORDINAL, NF_CARDINAL_TEXT, NF_ORDINAL_TEXT,
                            NF_DECIMAL_ZERO, NF_NONE };
static const char *t_nu_styles[12];
static void open_number(void)
{
  int level, k;
  const ListDef *cl = caret_list(&level);
  char b[40];
  if (!numwin) {
    Object *c = VGroup, Child, ColGroup(2),
        Child, Label1(tr("Number style")), Child, nu_style = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(nu_styles, t_nu_styles), MUIA_CycleChain, 1, End,
        Child, Label2(tr("Number format")), Child, nu_format = StringObject, StringFrame, MUIA_String_MaxLen, 30, MUIA_CycleChain, 1, End,
        Child, Label2(tr("Start at")), Child, nu_start = StringObject, StringFrame, MUIA_String_MaxLen, 6, MUIA_String_Accept, (ULONG)"0123456789", MUIA_CycleChain, 1, End,
        Child, Label1(tr("Font")), Child, nu_font = font_cycle(),
      End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("In the number format, %1 stands for the number (%2 for the level above's, and so on)."), MUIA_Font, MUIV_Font_Tiny, End,
    End;
    if (!(numwin = wp_dialog("Define New Number Format", MAKE_ID('N','U','M','D'), c, "_OK", ID_P_NUMGO))) return;
  }
  if (cl && cl->lvl[level].fmt != NF_BULLET) {
    const ListLevel *v = &cl->lvl[level];
    for (k = 0; k < (int)sizeof nu_nf && nu_nf[k] != v->fmt; k++) ;
    set(nu_style, MUIA_Cycle_Active, k < (int)sizeof nu_nf ? k : 0);
    utf8_to_latin1(v->text, (u32)strlen(v->text), b, sizeof b); set(nu_format, MUIA_String_Contents, (ULONG)b);
    sprintf(b, "%u", (unsigned)v->start); set(nu_start, MUIA_String_Contents, (ULONG)b);
  } else {
    set(nu_style, MUIA_Cycle_Active, 0);
    set(nu_format, MUIA_String_Contents, (ULONG)"%1.");
    set(nu_start, MUIA_String_Contents, (ULONG)"1");
  }
  set(numwin, MUIA_Window_Open, TRUE);
}
static void number_done(void)
{
  ListDef l;
  int level;
  const ListDef *cl = caret_list(&level);
  ULONG s = cyc(nu_style), fi = cyc(nu_font);
  char u[40];
  long start = atol(str(nu_start));
  set(numwin, MUIA_Window_Open, FALSE);
  set(wp_win(), MUIA_Window_Activate, TRUE);
  if (cl && cl->lvl[level].fmt != NF_BULLET) l = *cl; else { list_base(&l, 1); level = 0; }
  l.lvl[level].fmt = nu_nf[s < sizeof nu_nf ? s : 0];
  wp_latin1_utf8(str(nu_format), u, sizeof u);
  if (!u[0]) sprintf(u, "%%%d.", level + 1);
  strncpy(l.lvl[level].text, u, sizeof l.lvl[level].text - 1); l.lvl[level].text[sizeof l.lvl[level].text - 1] = 0;
  l.lvl[level].start = (u16)(start > 0 && start < 65536 ? start : 1);
  if (fi > 0 && fontlist[fi]) { strncpy(l.lvl[level].font, fontlist[fi], sizeof l.lvl[level].font - 1); l.lvl[level].font[sizeof l.lvl[level].font - 1] = 0; }
  else l.lvl[level].font[0] = 0;
  apply_list(&l);
}

/* ---- Sort Text */
static Object *sortwin, *so_by[3], *so_type[3], *so_desc[3], *so_sep, *so_other, *so_header, *so_case;
static const char *const so_bys[] = { "Paragraphs", "Field 1", "Field 2", "Field 3", "Field 4", "Field 5", "Field 6", "Field 7", "Field 8", "Field 9", NULL },
                  *const so_then[] = { "(none)", "Paragraphs", "Field 1", "Field 2", "Field 3", "Field 4", "Field 5", "Field 6", "Field 7", "Field 8", "Field 9", NULL },
                  *const so_types[] = { "Text", "Number", "Date", NULL }, *const so_orders[] = { "Ascending", "Descending", NULL },
                  *const so_seps[] = { "Tabs", "Commas", "Other", NULL };
static const char *t_so_bys[12], *t_so_then[13], *t_so_types[4], *t_so_orders[3], *t_so_seps[4];
static void open_sort(void)
{
  if (!sortwin) {
    static const char *const keys[3] = { "Sort by", "Then by", "Then by" };
    Object *g = ColGroup(4), End, *c;
    int k;
    trl(so_bys, t_so_bys); trl(so_then, t_so_then); trl(so_types, t_so_types); trl(so_orders, t_so_orders); trl(so_seps, t_so_seps);
    for (k = 0; k < 3; k++) {
      DoMethod(g, OM_ADDMEMBER, (ULONG)Label1(tr(keys[k])));
      DoMethod(g, OM_ADDMEMBER, (ULONG)(so_by[k] = CycleObject, MUIA_Cycle_Entries, (ULONG)(k ? t_so_then : t_so_bys), MUIA_CycleChain, 1, End));
      DoMethod(g, OM_ADDMEMBER, (ULONG)(so_type[k] = CycleObject, MUIA_Cycle_Entries, (ULONG)t_so_types, MUIA_CycleChain, 1, End));
      DoMethod(g, OM_ADDMEMBER, (ULONG)(so_desc[k] = CycleObject, MUIA_Cycle_Entries, (ULONG)t_so_orders, MUIA_CycleChain, 1, End));
    }
    c = VGroup, Child, g,
      Child, HGroup,
        Child, Label1(tr("Separate fields at")), Child, so_sep = CycleObject, MUIA_Cycle_Entries, (ULONG)t_so_seps, MUIA_CycleChain, 1, End,
        Child, so_other = StringObject, StringFrame, MUIA_String_MaxLen, 2, MUIA_FixWidthTxt, (ULONG)"MM", MUIA_CycleChain, 1, End,
      End,
      Child, HGroup,
        Child, so_header = CheckMark(FALSE), Child, LLabel1(tr("My list has a header row")),
        Child, so_case = CheckMark(FALSE), Child, LLabel1(tr("Case sensitive")), Child, HSpace(0),
      End,
    End;
    if (!(sortwin = wp_dialog("Sort Text", MAKE_ID('S','O','R','T'), c, "_OK", ID_P_SORTGO))) return;
  }
  set(sortwin, MUIA_Window_Open, TRUE);
}
static void sort_done(void)
{
  Doc *d = wp_doc();
  SortOpt o;
  Pos a, b;
  int k;
  set(sortwin, MUIA_Window_Open, FALSE);
  set(wp_win(), MUIA_Window_Activate, TRUE);
  sel_range(&a, &b);
  memset(&o, 0, sizeof o);
  for (k = 0; k < 3; k++) {
    int by = (int)cyc(so_by[k]) - (k ? 1 : 0);
    if (by < 0) continue;
    o.key[o.nkeys].field = by; o.key[o.nkeys].type = (int)cyc(so_type[k]); o.key[o.nkeys].desc = (int)cyc(so_desc[k]);
    o.nkeys++;
  }
  o.sep = cyc(so_sep) == 0 ? '\t' : cyc(so_sep) == 1 ? ',' : (str(so_other)[0] ? str(so_other)[0] : '\t');
  o.header = chk(so_header); o.casesens = chk(so_case);
  if (in_table(&d->p[a.para]->pf)) {
    /* in a table: its rows, by the column the first key names (Word's Sort for tables) */
    static struct QSort q;
    q.col = o.key[0].field > 0 ? o.key[0].field - 1 : 0; q.desc = o.key[0].desc; q.numeric = o.key[0].type == SORT_NUMBER; q.header = o.header;
    DoMethod(wp_view(), MUIM_Quill_Command, CMD_TSORT, (ULONG)&q);
    return;
  }
  if (a.para == b.para) { a.para = 0; b.para = d->np - 1; }      /* nothing selected: the whole document's paragraphs, as Word */
  if (!doc_sort_text(d, a.para, b.para, &o)) { DisplayBeep(NULL); return; }
  a.off = 0;
  changed(a);
}

/* ---- the Symbol window */
#define SYM_N 192                         /* 0x20-0x7E and 0xA0-0xFF: what the interface's Latin-1 fonts can show */
#define SYM_RECENT 16
static Object *symwin, *sy_reg, *sy_font, *sy_code, *sy_from, *sy_show, *sy_special, *sy_btn[SYM_N], *sy_rec[SYM_RECENT];
static u32 sy_cur = 0xA9, sy_recent[SYM_RECENT];
static char sy_lab[SYM_N][2], sy_reclab[SYM_RECENT][2];
static int sy_forbullet;                  /* the window was opened from Define New Bullet: the choice goes there */
static const char *const sy_froms[] = { "Unicode (hex)", "ASCII (decimal)", NULL };
static const char *t_sy_froms[3];
/* the special characters page: name, code point, Word's shortcut */
static const struct { const char *name; u32 cp; const char *key; } specials[] = {
  { "Em Dash", 0x2014, "Alt-Ctrl-Num -" }, { "En Dash", 0x2013, "Ctrl-Num -" }, { "Nonbreaking Hyphen", 0x2011, "Ctrl-Shift-_" },
  { "Optional Hyphen", 0x00AD, "Ctrl--" }, { "Em Space", 0x2003, "" }, { "En Space", 0x2002, "" }, { "1/4 Em Space", 0x2005, "" },
  { "Nonbreaking Space", 0x00A0, "Ctrl-Shift-Space" }, { "Copyright", 0x00A9, "Alt-Ctrl-C" }, { "Registered", 0x00AE, "Alt-Ctrl-R" },
  { "Trademark", 0x2122, "Alt-Ctrl-T" }, { "Section", 0x00A7, "" }, { "Paragraph", 0x00B6, "" }, { "Ellipsis", 0x2026, "Alt-Ctrl-." },
  { "Single Opening Quote", 0x2018, "" }, { "Single Closing Quote", 0x2019, "" }, { "Double Opening Quote", 0x201C, "" },
  { "Double Closing Quote", 0x201D, "" }, { "No-Width Optional Break", 0x200C, "" }, { "No-Width Non Break", 0x200D, "" } };
#define NSPECIAL (int)(sizeof specials / sizeof specials[0])
static char special_rows[NSPECIAL][64];
static const char *special_ptrs[NSPECIAL + 1];
static u32 sym_code(int i) { return (u32)(i < 95 ? 0x20 + i : 0xA0 + (i - 95)); }
/* the chosen character shown: its code and look */
static void sym_show(void)
{
  char b[64];
  if (sy_cur >= 0x20 && sy_cur < 0x100 && !(sy_cur >= 0x7F && sy_cur < 0xA0)) sprintf(b, "\33c%c   U+%04lX   %lu", (int)sy_cur, (unsigned long)sy_cur, (unsigned long)sy_cur);
  else sprintf(b, "\33cU+%04lX   %lu", (unsigned long)sy_cur, (unsigned long)sy_cur);
  set(sy_show, MUIA_Text_Contents, (ULONG)b);
}
static Object *sym_button(char *lab, u32 cp, Object *holder)
{
  Object *o;
  lab[0] = (char)cp; lab[1] = 0;
  o = TextObject, ButtonFrame, MUIA_Background, MUII_ButtonBack, MUIA_InputMode, MUIV_InputMode_RelVerify, MUIA_Text_PreParse, (ULONG)"\33c",
    MUIA_Text_Contents, (ULONG)lab, MUIA_FixWidthTxt, (ULONG)"MM", MUIA_UserData, cp, End;
  if (o) {
    DoMethod(o, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)holder, 3, MUIM_Set, MUIA_UserData, cp);
    DoMethod(o, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_P_SYMPICK);
  }
  return o;
}
static void recent_show(void)
{
  int i;
  for (i = 0; i < SYM_RECENT; i++) {
    u32 c = sy_recent[i];
    sy_reclab[i][0] = (char)(c >= 0x20 && c < 0x100 && !(c >= 0x7F && c < 0xA0) ? c : c ? '?' : ' '); sy_reclab[i][1] = 0;
    set(sy_rec[i], MUIA_Text_Contents, (ULONG)sy_reclab[i]);
    set(sy_rec[i], MUIA_UserData, c);
  }
}
static void open_symbol(int forbullet)
{
  int i;
  sy_forbullet = forbullet;
  if (!symwin) {
    static const char *pages[3];
    Object *grid = ColGroup(16), MUIA_Group_Spacing, 1, End, *rec = HGroup, MUIA_Group_Spacing, 1, End, *c;
    if (!grid || !rec) return;
    sy_show = TextObject, TextFrame, MUIA_Background, MUII_TextBack, MUIA_Text_Contents, (ULONG)"", End;
    if (!sy_show) return;
    for (i = 0; i < SYM_N; i++) if ((sy_btn[i] = sym_button(sy_lab[i], sym_code(i), sy_show))) DoMethod(grid, OM_ADDMEMBER, (ULONG)sy_btn[i]);
    for (i = 0; i < SYM_RECENT; i++) if ((sy_rec[i] = sym_button(sy_reclab[i], ' ', sy_show))) DoMethod(rec, OM_ADDMEMBER, (ULONG)sy_rec[i]);
    for (i = 0; i < NSPECIAL; i++) { snprintf(special_rows[i], sizeof special_rows[i], specials[i].key[0] ? "%s   (%s)" : "%s", tr(specials[i].name), specials[i].key[0] ? tr(specials[i].key) : ""); special_ptrs[i] = special_rows[i]; }
    special_ptrs[NSPECIAL] = NULL;
    pages[0] = tr("Symbols"); pages[1] = tr("Special Characters"); pages[2] = NULL;
    c = sy_reg = RegisterGroup(pages), MUIA_CycleChain, 1,
      Child, VGroup,
        Child, HGroup, Child, Label1(tr("Font")), Child, sy_font = font_cycle(), End,
        Child, grid,
        Child, QTitleObject(tr("Recently used symbols")),
        Child, rec,
        Child, HGroup,
          Child, Label2(tr("Character code")),
          Child, sy_code = StringObject, StringFrame, MUIA_String_MaxLen, 9, MUIA_String_Accept, (ULONG)"0123456789abcdefABCDEFxU+", MUIA_CycleChain, 1, End,
          Child, Label1(tr("from")), Child, sy_from = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(sy_froms, t_sy_froms), MUIA_CycleChain, 1, End,
        End,
        Child, sy_show,
      End,
      Child, VGroup,
        Child, sy_special = ListviewObject, MUIA_CycleChain, 1, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_SourceArray, (ULONG)special_ptrs, End, End,
      End,
    End;
    if (!(symwin = wp_dialog("Symbol", MAKE_ID('S','Y','M','B'), c, "_Insert", ID_P_SYMINSERT))) return;
    DoMethod(sy_code, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_P_SYMCODE);
    DoMethod(sy_special, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_P_SYMSPECIAL);
  }
  recent_show();
  sym_show();
  set(symwin, MUIA_Window_Open, TRUE);
}
/* the character the window's code field names, else the one picked */
static u32 sym_typed(void)
{
  const char *s = str(sy_code);
  if (!s[0]) return sy_cur;
  return cyc(sy_from) ? (u32)atol(s) : hex_code(s);
}
static void sym_insert(u32 cp)
{
  ULONG fi = cyc(sy_font);
  int i;
  if (cp < 0x20 || cp > 0x10FFFF) { DisplayBeep(NULL); return; }
  for (i = 0; i < SYM_RECENT - 1 && sy_recent[i] != cp; i++) ;
  memmove(sy_recent + 1, sy_recent, (size_t)i * sizeof sy_recent[0]);
  sy_recent[0] = cp;
  recent_show();
  if (sy_forbullet && bulwin) {
    /* for Define New Bullet: into its code field */
    char b[16];
    sprintf(b, "%lX", (unsigned long)cp);
    set(bu_code, MUIA_String_Contents, (ULONG)b);
    if (fi > 0) set(bu_font, MUIA_Cycle_Active, fi);
    set(symwin, MUIA_Window_Open, FALSE);
    set(bulwin, MUIA_Window_Activate, TRUE);
    return;
  }
  if (fi > 0 && fontlist[fi]) {
    /* in the font chosen (the text after it keeps its own) */
    Doc *d = wp_doc();
    Pos a, b;
    CharFmt f;
    sel_range(&a, &b);
    f = *(pos_cmp(a, b) ? para_fmt_at(d->p[a.para], a.off) : para_fmt_before(d->p[a.para], a.off));
    f.font = (u8)doc_font(d, fontlist[fi]);
    type_cp(cp, &f);
  } else type_cp(cp, NULL);
}

/* ---- Paste Special */
static Object *pswin, *ps_list, *ps_link;
static const char *const ps_formats[] = { "Formatted Text (Quill)", "Text with the Destination's Formatting", "Unformatted Text", "Rich Text Format (RTF)",
                                          "HTML Format", NULL };
static const char *t_ps_formats[6];
/* the clipboard's text read as RTF or HTML (ext "rtf" or "htm") by the file format that reads it (WP28a,
   WP28c), and pasted at the caret with its formatting: 0 if no format reads it or it isn't that */
static int paste_as_format(const char *ext)
{
  long n = 0;
  char *t = clip_read(&n);
  const DocFormat *f = NULL;
  int i, ok = 0;
  for (i = 0; i < doc_nformats; i++) if (doc_formats[i]->load && doc_formats[i]->exts && strstr(doc_formats[i]->exts, ext)) { f = doc_formats[i]; break; }
  if (t && f && (!f->sniff || f->sniff((const u8 *)t, (size_t)n))) {
    unsigned lost = 0;
    char err[80];
    Doc *src = f->load((const u8 *)t, (size_t)n, &lost, err, sizeof err);
    Fragment *fr = src && src->np ? doc_fragment(src, doc_start(src), doc_end_pos(src)) : NULL;
    if (fr) {
      Doc *d = wp_doc();
      Pos a, b;
      sel_range(&a, &b);
      doc_begin(d, a);
      if (pos_cmp(a, b)) doc_delete(d, a, b);
      ok = doc_paste(d, &a, fr, PASTE_KEEP);
      doc_end(d, a);
      fragment_free(fr);
      changed(a);
    }
    doc_free(src);
  }
  free(t);
  return ok;
}
static void open_pastespecial(void)
{
  if (!pswin) {
    static const char *radios[3];
    Object *c;
    radios[0] = tr("Paste"); radios[1] = tr("Paste link"); radios[2] = NULL;
    c = VGroup,
      Child, HGroup,
        Child, ps_link = RadioObject, MUIA_Radio_Entries, (ULONG)radios, MUIA_Disabled, FALSE, End,
        Child, ps_list = ListviewObject, MUIA_CycleChain, 1, MUIA_Listview_List, ListObject, InputListFrame,
          MUIA_List_SourceArray, (ULONG)trl(ps_formats, t_ps_formats), MUIA_List_Active, 0, End, End,
      End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Formatted text keeps its fonts and formatting; unformatted text takes the formatting where it goes.\nPaste link needs linked objects, which Quill doesn't have."),
        MUIA_Font, MUIV_Font_Tiny, End,
    End;
    if (!(pswin = wp_dialog("Paste Special", MAKE_ID('P','S','P','C'), c, "_OK", ID_P_PSGO))) return;
    DoMethod(ps_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_P_PSGO);
  }
  set(ps_link, MUIA_Radio_Active, 0);
  set(pswin, MUIA_Window_Open, TRUE);
}
static void pastespecial_done(void)
{
  static const int mode[3] = { PASTE_KEEP, PASTE_MERGE, PASTE_TEXT };
  LONG a = 0, l = 0;
  get(ps_list, MUIA_List_Active, &a); get(ps_link, MUIA_Radio_Active, &l);
  if (l) { wp_ask("OK", "%s", tr("Quill can't paste a link: paste the text instead.")); return; }
  set(pswin, MUIA_Window_Open, FALSE);
  set(wp_win(), MUIA_Window_Activate, TRUE);
  if (a == 3 || a == 4) {
    if (!paste_as_format(a == 3 ? "rtf" : "htm")) wp_ask("OK", "%s", tr("The clipboard holds no text in that format that Quill can read."));
    return;
  }
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_PASTEAS, (ULONG)mode[a >= 0 && a < 3 ? a : 0]);
}

/* ---- Copy and Paste Formatting */
static CharFmt cf_copied; static ParaFmt pf_copied; static int fmt_have, fmt_para;
static void copy_format(void)
{
  Doc *d = wp_doc();
  Pos a, b;
  sel_range(&a, &b);
  cf_copied = *(pos_cmp(a, b) ? para_fmt_at(d->p[a.para], a.off) : para_fmt_before(d->p[a.para], a.off));
  pf_copied = d->p[a.para]->pf;
  /* the paragraph's formatting too when the selection takes in a paragraph's end, as Word */
  fmt_para = a.para != b.para || (pos_cmp(a, b) && b.off == d->p[b.para]->len && a.off == 0);
  fmt_have = 1;
}
static void paste_format(void)
{
  Doc *d = wp_doc();
  Pos a, b;
  static struct QCharFmt q;
  static struct QParaFmt qp;
  if (!fmt_have) { DisplayBeep(NULL); return; }
  sel_range(&a, &b);
  doc_begin(d, a);
  q.mask = CM_LOOK | CM_CSTYLE; q.cf = cf_copied;
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_CHARFMT, (ULONG)&q);
  if (fmt_para) {
    qp.mask = PM_STYLE | PM_ALIGN | PM_INDENT | PM_SPACING | PM_LINE | PM_TABS | PM_KEEP | PM_BORDER | PM_SHADE | PM_PAGINATION | PM_CONTEXT;
    qp.pf = pf_copied;
    DoMethod(wp_view(), MUIM_Quill_Command, CMD_PARAFMT, (ULONG)&qp);
  }
  doc_end(d, a);
}

/* ---- the Clipboard pane: the last 24 things copied or cut (in any window), docked at the right of
   the page (added to the window's work area while it is open) */
#define CLIP_MAX 24
static Fragment *clips[CLIP_MAX]; static char *clipnames[CLIP_MAX + 1]; static int nclips;
static Object *clippane, *cl_list, *clipwork, *clipwin;
void wp10_copied(Doc *d, Pos a, Pos b)
{
  Fragment *f;
  u32 n, i, k = 0;
  const char *t;
  char *name;
  if (!pos_cmp(a, b) || !(f = doc_fragment(d, a, b))) return;
  t = fragment_text(f, &n);
  if (!(name = (char *)malloc(64))) { fragment_free(f); return; }
  /* its first words, on one line */
  {
    char u[120];
    for (i = 0; i < n && k < sizeof u - 4; i++) u[k++] = t[i] == '\n' || t[i] == '\t' || t[i] == '\v' ? ' ' : t[i];
    u[k] = 0;
    utf8_to_latin1(u, k, name, 60);
    if (n > k) strcat(name, "...");
  }
  if (nclips == CLIP_MAX) { nclips--; fragment_free(clips[nclips]); free(clipnames[nclips]); }
  memmove(clips + 1, clips, (size_t)nclips * sizeof clips[0]);
  memmove(clipnames + 1, clipnames, (size_t)nclips * sizeof clipnames[0]);
  clips[0] = f; clipnames[0] = name; nclips++;
  clipnames[nclips] = NULL;
  if (cl_list) { DoMethod(cl_list, MUIM_List_Clear); DoMethod(cl_list, MUIM_List_Insert, (ULONG)clipnames, nclips, MUIV_List_Insert_Bottom); }
}
static void clip_fill(void)
{
  if (!cl_list) return;
  DoMethod(cl_list, MUIM_List_Clear);
  if (nclips) DoMethod(cl_list, MUIM_List_Insert, (ULONG)clipnames, nclips, MUIV_List_Insert_Bottom);
}
/* item k pasted at the caret (replacing the selection), with its formatting */
static void clip_paste(int k)
{
  Doc *d = wp_doc();
  Pos a, b;
  if (k < 0 || k >= nclips) { DisplayBeep(NULL); return; }
  sel_range(&a, &b);
  doc_begin(d, a);
  if (pos_cmp(a, b)) doc_delete(d, a, b);
  doc_paste(d, &a, clips[k], PASTE_KEEP);
  doc_end(d, a);
  changed(a);
}
/* the work area a window's page view sits in (its parent's parent's parent: main.c's workarea) */
static Object *work_of(Object *view)
{
  Object *o = view;
  int i;
  for (i = 0; i < 3 && o; i++) { Object *p = NULL; get(o, MUIA_Parent, &p); o = p; }
  return o;
}
static void clip_pane(void)
{
  Object *w = work_of(wp_view());
  if (clippane && clipwork) {
    /* open: closed (or moved to this window) */
    Object *was = clipwork;
    DoMethod(clipwork, MUIM_Group_InitChange); DoMethod(clipwork, OM_REMMEMBER, (ULONG)clippane); DoMethod(clipwork, MUIM_Group_ExitChange);
    clipwork = NULL; clipwin = NULL;
    if (was == w) { wp_refresh(); return; }
  }
  if (!w) { DisplayBeep(NULL); return; }
  if (!clippane) {
    Object *pall, *clr, *pst, *del;
    clippane = VGroup, MUIA_HorizWeight, 30, MUIA_Frame, MUIV_Frame_Group, QDLG,
      Child, QTitleObject(tr("Clipboard")),
      Child, HGroup, Child, pall = QButton(tr("Paste All")), Child, clr = QButton(tr("Clear All")), End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Double-click an item to paste it:"), MUIA_Font, MUIV_Font_Tiny, End,
      Child, cl_list = ListviewObject, MUIA_CycleChain, 1, MUIA_Listview_List, ListObject, InputListFrame, End, End,
      Child, HGroup, Child, pst = QButton(tr("Paste")), Child, del = QButton(tr("Delete")), End,
    End;
    if (!clippane) return;
    DoMethod(cl_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_P_CLIPPASTE);
    DoMethod(pst, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_P_CLIPPASTE);
    DoMethod(pall, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_P_CLIPALL);
    DoMethod(clr, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_P_CLIPCLEAR);
    DoMethod(del, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_P_CLIPDEL);
  }
  DoMethod(w, MUIM_Group_InitChange); DoMethod(w, OM_ADDMEMBER, (ULONG)clippane); DoMethod(w, MUIM_Group_ExitChange);
  clipwork = w; clipwin = wp_win();
  clip_fill();
  wp_refresh();
}
static void clip_remove(int k)
{
  if (k < 0 || k >= nclips) return;
  fragment_free(clips[k]); free(clipnames[k]);
  memmove(clips + k, clips + k + 1, (size_t)(nclips - k - 1) * sizeof clips[0]);
  memmove(clipnames + k, clipnames + k + 1, (size_t)(nclips - k - 1) * sizeof clipnames[0]);
  nclips--; clipnames[nclips] = NULL;
  clip_fill();
}
static LONG clip_active(void) { LONG a = -1; if (cl_list) get(cl_list, MUIA_List_Active, &a); return a; }

/* ---- the Home lists' extra entries (main.c) */
void wp10_list_extra(ULONG id, LONG extra)
{
  switch (id) {
  case ID_PASTEDD: case ID_PASTESPECDD: wp_run(extra == 0 ? ID_P_PASTESPECIAL : ID_P_CLIPPANE, WPV_NONE); break;
  case ID_SPACINGDD: wp_run(extra == 0 ? ID_P_SPACEBEFORE : ID_P_SPACEAFTER, WPV_NONE); break;
  case ID_PBORDERDD: wp_run(ID_P_HLINE, WPV_NONE); break;
  case ID_SYMBOLDD: wp_run(ID_P_SYMBOL, WPV_NONE); break;
  }
}

/* ---- the commands */
int wp10_command(ULONG id, LONG value)
{
  Doc *d = wp_doc();
  Pos a, b;
  switch (id) {
  case ID_P_DISTRIBUTE:
    sel_range(&a, &b);
    DoMethod(wp_view(), MUIM_Quill_Command, CMD_ALIGN, (ULONG)(d->p[a.para]->pf.align == AL_DISTRIBUTE ? AL_LEFT : AL_DISTRIBUTE));
    break;
  case ID_P_SPACEBEFORE: sel_range(&a, &b); sb_on = d->p[a.para]->pf.before == 0; each_para(f_before); break;
  case ID_P_SPACEAFTER: sel_range(&a, &b); sb_on = d->p[a.para]->pf.after == 0; each_para(f_after); break;
  case ID_P_HLINE: {
    /* a line under the paragraph, as Word's AutoFormat makes from "---" (1.5 pt) */
    static struct QParaFmt q;
    memset(&q, 0, sizeof q);
    q.mask = PM_BORDER;
    q.pf.bstyle[PE_BOTTOM] = PB_SINGLE; q.pf.bwidth[PE_BOTTOM] = 12; q.pf.bspace[PE_BOTTOM] = 1; q.pf.bcolor[PE_BOTTOM] = COLOR_AUTO;
    DoMethod(wp_view(), MUIM_Quill_Command, CMD_PARAFMT, (ULONG)&q);
    break; }
  case ID_P_HANG: each_para(f_hang); break;
  case ID_P_UNHANG: each_para(f_unhang); break;
  case ID_P_BULLETLIB: if (value == P_BU_DEFINE) { open_bullet(); return WPR_KEEPFOCUS; } bullet_choice(value); break;
  case ID_P_NUMLIB: if (value == P_NU_DEFINE) { open_number(); return WPR_KEEPFOCUS; } number_choice(value); break;
  case ID_P_BULLETDEF: open_bullet(); return WPR_KEEPFOCUS;
  case ID_P_BULLETGO: bullet_done(); break;
  case ID_P_BULLETSYM: open_symbol(1); return WPR_KEEPFOCUS;
  case ID_P_NUMDEF: open_number(); return WPR_KEEPFOCUS;
  case ID_P_NUMGO: number_done(); break;
  case ID_P_SORT: open_sort(); return WPR_KEEPFOCUS;
  case ID_P_SORTGO: sort_done(); break;
  case ID_P_NBSP: type_cp(0xA0, NULL); break;
  case ID_P_NBHYPHEN: type_cp(0x2011, NULL); break;
  case ID_P_SOFTHYPHEN: type_cp(0xAD, NULL); break;
  case ID_P_SYMBOL: open_symbol(0); return WPR_KEEPFOCUS;
  case ID_P_SYMPICK: {
    ULONG c = 0;
    get(sy_show, MUIA_UserData, &c);
    if (c >= 0x20) { sy_cur = (u32)c; set(sy_code, MUIA_String_Contents, (ULONG)""); sym_show(); }
    return WPR_KEEPFOCUS; }
  case ID_P_SYMCODE: { u32 c = sym_typed(); if (c >= 0x20) { sy_cur = c; sym_show(); } return WPR_KEEPFOCUS; }
  case ID_P_SYMINSERT: {
    LONG pg = 0, k = -1;
    get(sy_reg, MUIA_Group_ActivePage, &pg);
    if (pg == 1) { get(sy_special, MUIA_List_Active, &k); if (k >= 0 && k < NSPECIAL) type_cp(specials[k].cp, NULL); else DisplayBeep(NULL); return WPR_KEEPFOCUS; }
    sym_insert(sym_typed());
    return WPR_KEEPFOCUS; }
  case ID_P_SYMSPECIAL: {
    LONG k = -1;
    get(sy_special, MUIA_List_Active, &k);
    if (k >= 0 && k < NSPECIAL) type_cp(specials[k].cp, NULL);
    return WPR_KEEPFOCUS; }
  case ID_P_PASTESPECIAL: open_pastespecial(); return WPR_KEEPFOCUS;
  case ID_P_PSGO: pastespecial_done(); break;
  case ID_P_COPYFMT: copy_format(); break;
  case ID_P_PASTEFMT: paste_format(); break;
  case ID_P_CLIPPANE: clip_pane(); break;
  case ID_P_CLIPPASTE: clip_paste((int)clip_active()); break;
  case ID_P_CLIPALL: {
    int k;
    for (k = nclips - 1; k >= 0; k--) clip_paste(k);     /* the oldest first, as they were copied */
    break; }
  case ID_P_CLIPCLEAR: while (nclips) clip_remove(nclips - 1); break;
  case ID_P_CLIPDEL: clip_remove((int)clip_active()); return WPR_KEEPFOCUS;
  }
  return WPR_DONE;
}

/* the ribbon's state: Distributed, the Clipboard pane open */
static void state(ULONG *on, const WpCtx *c)
{
  if (c->doc && c->caret.para < c->doc->np && c->doc->p[c->caret.para]->pf.align == AL_DISTRIBUTE) *on |= RBIT(P_ST_DISTRIBUTE);
  if (clipwork && clipwin == wp_win()) *on |= RBIT(P_ST_CLIPPANE);
}
/* Word's keys: Ctrl-Shift-J Distributed, Ctrl-0 space before, Ctrl-T and Ctrl-Shift-T hanging indent,
   Ctrl-Shift-C and Ctrl-Shift-V copy and paste formatting, Ctrl-Shift-Space a nonbreaking space,
   Ctrl-Shift-minus a nonbreaking hyphen, Ctrl-minus an optional hyphen */
static ULONG key(UWORD code, UWORD qual, const WpCtx *c)
{
  int ctrl = (qual & IEQUALIFIER_CONTROL) != 0, shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
  if (!ctrl || (code & IECODE_UP_PREFIX) || (qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT))) return 0;
  switch (code) {
  case 0x24: return shift ? ID_P_DISTRIBUTE : 0;                 /* J */
  case 0x0A: return shift ? 0 : ID_P_SPACEBEFORE;                /* 0 */
  case 0x14: return shift ? ID_P_UNHANG : ID_P_HANG;             /* T */
  case 0x33: return shift ? ID_P_COPYFMT : 0;                    /* C */
  case 0x34: return shift ? ID_P_PASTEFMT : 0;                   /* V */
  case 0x40: return shift ? ID_P_NBSP : 0;                       /* Space */
  case 0x0B: case 0x4A: return shift ? ID_P_NBHYPHEN : ID_P_SOFTHYPHEN;   /* - (and the keypad's) */
  }
  return 0;
}
static void cleanup(void)
{
  while (nclips) { nclips--; fragment_free(clips[nclips]); free(clipnames[nclips]); }
}

static const WpMenu menus[] = {
  { WM_EDIT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_EDIT, { NM_ITEM, (STRPTR)"Paste Special...", 0, 0, 0, (APTR)ID_P_PASTESPECIAL } },
  { WM_EDIT, { NM_ITEM, (STRPTR)"Copy Formatting", 0, 0, 0, (APTR)ID_P_COPYFMT } },
  { WM_EDIT, { NM_ITEM, (STRPTR)"Paste Formatting", 0, 0, 0, (APTR)ID_P_PASTEFMT } },
  { WM_EDIT, { NM_ITEM, (STRPTR)"Clipboard Pane", 0, 0, 0, (APTR)ID_P_CLIPPANE } },
  { WM_INSERT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Symbol...", 0, 0, 0, (APTR)ID_P_SYMBOL } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Special Characters", 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Nonbreaking Space", 0, 0, 0, (APTR)ID_P_NBSP } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Nonbreaking Hyphen", 0, 0, 0, (APTR)ID_P_NBHYPHEN } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Optional Hyphen", 0, 0, 0, (APTR)ID_P_SOFTHYPHEN } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Horizontal Line", 0, 0, 0, (APTR)ID_P_HLINE } },
  { WM_FORMAT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_ITEM, (STRPTR)"Paragraph Tools", 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Distributed", 0, 0, 0, (APTR)ID_P_DISTRIBUTE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Add or Remove Space Before", 0, 0, 0, (APTR)ID_P_SPACEBEFORE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Remove or Add Space After", 0, 0, 0, (APTR)ID_P_SPACEAFTER } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Hanging Indent", 0, 0, 0, (APTR)ID_P_HANG } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Remove Hanging Indent", 0, 0, 0, (APTR)ID_P_UNHANG } },
  { WM_FORMAT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Define New Bullet...", 0, 0, 0, (APTR)ID_P_BULLETDEF } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Define New Number Format...", 0, 0, 0, (APTR)ID_P_NUMDEF } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Sort Text...", 0, 0, 0, (APTR)ID_P_SORT } },
};

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items (its ribbon items are in Quill's own Clipboard and Paragraph groups and lists:
   ribbon.c's g_para and the lists main.c hands to wp10_list_extra) */
const WpPackage wp10_package = {
  "WP10", ID_WP10_FIRST, ID_WP10_LAST,
  wp10_command, state, NULL, NULL, key, NULL, cleanup,
  NULL, 0,
  menus, (int)(sizeof menus / sizeof menus[0])
};
