/*
 * wp_intl.c - WP27 Right-to-left and East Asian (docs/wordcraft-gap.md). This file belongs to WP27: only that
 * package edits it. Its commands are ID_WP27_FIRST .. ID_WP27_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * Home: Left-to-Right and Right-to-Left Text Direction (the paragraph's), Phonetic Guide, Asian
 * Layout (Combine Characters, Two Lines in One, Horizontal in Vertical, emphasis marks). Layout: Text
 * Direction and Section Direction of the caret's section, Document Grid, Asian Typography. Table
 * Layout: Table Direction. Format menu: the same and Asian and Complex Script Fonts; File menu:
 * Right-to-Left and Asian Options (the Numeral option, kept in PROGDIR:Quill.intl, and the document's
 * Chinese/Japanese line breaking settings). The model is core/intl.h, the algorithms core/bidi.c.
 *
 * The screen is Latin-1: text the gadgets can't show (a guide in kana, a font name in Chinese) is
 * shown and typed as U+XXXX (U+304B for ka).
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
#include "../core/intl.h"
#include "../core/bidi.h"

enum { ID_I_LTR = ID_WP27_FIRST, ID_I_RTL, ID_I_PHONETIC, ID_I_PH_OK, ID_I_PH_REMOVE,
       ID_I_ASIAN, ID_I_COMBINE, ID_I_TWOLINES, ID_I_TL_OK, ID_I_HVERT, ID_I_EALCLEAR,
       ID_I_EM0, ID_I_EM1, ID_I_EM2, ID_I_EM3, ID_I_EM4,
       ID_I_TYPO, ID_I_TYPO_OK, ID_I_GRID, ID_I_GRID_OK, ID_I_SECTRTL, ID_I_TEXTDIR, ID_I_TD0, ID_I_TD1, ID_I_TD2,
       ID_I_TABLERTL, ID_I_OPTIONS, ID_I_OPT_OK, ID_I_FONTS, ID_I_FONTS_OK, ID_I_RUNRTL };
/* the state bits */
enum { S_RTL = 1, S_LTR, S_SECTRTL, S_TABLERTL, S_INTABLE, S_HVERT, S_RUNRTL, S_TDV };
#define PREFS "PROGDIR:Quill.intl"

static WpCtx now;                         /* the caret and selection as the last state call had them */

/* ------------------------------------------------------------------ */
/* helpers */
static Object *str_obj(int max) { return StringObject, StringFrame, MUIA_String_MaxLen, max, MUIA_CycleChain, 1, End; }
static Object *num_obj(void) { return StringObject, StringFrame, MUIA_String_MaxLen, 6, MUIA_String_Accept, (ULONG)"0123456789", MUIA_CycleChain, 1, MUIA_FixWidthTxt, (ULONG)"000000", End; }
static Object *cyc_obj(const char **e) { return CycleObject, MUIA_Cycle_Entries, (ULONG)e, MUIA_CycleChain, 1, End; }
static Object *chk_obj(void) { return ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
                                 MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_CycleChain, 1, End; }
static LONG geti(Object *o, ULONG attr) { LONG v = 0; get(o, attr, &v); return v; }
static long get_num(Object *o) { char *s = NULL; get(o, MUIA_String_Contents, &s); return s ? atol(s) : 0; }
static void set_num(Object *o, long v) { char b[16]; sprintf(b, "%ld", v); set(o, MUIA_String_Contents, (ULONG)b); }
static const char *get_str(Object *o) { char *s = NULL; get(o, MUIA_String_Contents, &s); return s ? s : ""; }
static void relayout(void) { DoMethod(wp_view(), MUIM_Quill_Command, CMD_DESIGN, 0); wp_refresh(); }
static void changed(void) { Doc *d = wp_doc(); if (d) d->dirty = 1; set(wp_view(), MUIA_Quill_Changed, TRUE); relayout(); }
/* UTF-8 as Latin-1 for a gadget, characters it hasn't as U+XXXX */
static void to_gadget(const char *u, char *out, size_t cap)
{
  size_t i = 0, n = strlen(u), o = 0;
  while (i < n && o + 8 < cap) {
    int used;
    u32 cp = utf8_get(u + i, n - i, &used);
    i += (size_t)used;
    if (cp < 0x100) out[o++] = (char)cp;
    else o += (size_t)sprintf(out + o, "U+%04lX", (unsigned long)cp);
  }
  out[o] = 0;
}
/* a gadget's Latin-1 text as UTF-8, U+XXXX read as that character */
static void from_gadget(const char *l, char *out, size_t cap)
{
  size_t o = 0;
  while (*l && o + 5 < cap) {
    u32 cp = (u8)*l++;
    if ((cp == 'U' || cp == 'u') && *l == '+') {
      char *e;
      unsigned long v = strtoul(l + 1, &e, 16);
      if (e > l + 1 && v > 0 && v < 0x110000) { cp = (u32)v; l = e; }
    }
    o += (size_t)utf8_put(out + o, cp);
  }
  out[o] = 0;
}
/* the character format at the caret (the one before it) */
static const CharFmt *caret_cf(void)
{
  Doc *d = now.doc;
  if (!d || now.caret.para >= d->np) return NULL;
  return para_fmt_at(d->p[now.caret.para], now.caret.off > 0 ? now.caret.off - 1 : 0);
}
/* character formatting through the view (the selection, or what is typed next; one undo step) */
static void char_fmt(unsigned mask, const CharFmt *cf)
{
  struct QCharFmt q;
  q.mask = mask; q.cf = *cf;
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_CHARFMT, (ULONG)&q);
  relayout();
}
static void para_fmt(unsigned mask, const ParaFmt *pf)
{
  struct QParaFmt q;
  q.mask = mask; q.pf = *pf;
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_PARAFMT, (ULONG)&q);
  relayout();
}
/* the selection's ends in order (a = b: none) */
static void sel(Pos *a, Pos *b)
{
  Pos c = now.caret, n = now.anchor;
  if (n.para < c.para || (n.para == c.para && n.off < c.off)) { *a = n; *b = c; } else { *a = c; *b = n; }
}

/* ------------------------------------------------------------------ */
/* the Numeral option */
static void load_prefs(void)
{
  FILE *f = fopen(PREFS, "r");
  char line[64];
  if (!f) return;
  while (fgets(line, sizeof line, f))
    if (!strncmp(line, "numerals=", 9)) { int v = atoi(line + 9); if (v >= NUM_ARABIC && v <= NUM_SYSTEM) bidi_numerals = v; }
  fclose(f);
}
static void save_prefs(void)
{
  FILE *f = fopen(PREFS, "w");
  if (!f) return;
  fprintf(f, "numerals=%d\n", bidi_numerals);
  fclose(f);
}

/* ------------------------------------------------------------------ */
/* Phonetic Guide */
static Object *phwin, *ph_base, *ph_text, *ph_align, *ph_offset, *ph_size, *ph_font;
static const char *ph_aligns[RA_N + 1];
static void open_phonetic(void)
{
  Doc *d = now.doc;
  Pos a, b;
  char base[256], g[256];
  const CharFmt *f = caret_cf();
  const Ruby *r = f && f->ruby ? intl_ruby(d, f->ruby) : NULL;
  if (!d) return;
  sel(&a, &b);
  if (!phwin) {
    Object *c, *rem;
    static const char *const al[RA_N] = { "Centered", "Distributed", "Distributed with space", "Left", "Right", "Right (vertical)" };
    int k;
    for (k = 0; k < RA_N; k++) ph_aligns[k] = tr(al[k]);
    c = VGroup, QDLG,
      Child, ColGroup(2),
        Child, QLabel(tr("Base text"), MUIO_Label_SingleFrame), Child, ph_base = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", End,
        Child, QLabel(tr("Ruby text"), MUIO_Label_DoubleFrame), Child, ph_text = str_obj(200),
        Child, QLabel(tr("Alignment"), MUIO_Label_DoubleFrame), Child, ph_align = cyc_obj(ph_aligns),
        Child, QLabel(tr("Offset (pt)"), MUIO_Label_DoubleFrame), Child, ph_offset = num_obj(),
        Child, QLabel(tr("Size (pt)"), MUIO_Label_DoubleFrame), Child, ph_size = num_obj(),
        Child, QLabel(tr("Font"), MUIO_Label_DoubleFrame), Child, ph_font = str_obj(47),
      End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Characters beyond Latin-1 as U+XXXX (U+304B: ka)"), End,
      Child, HGroup, Child, rem = SimpleButton(tr("_Remove")), Child, HSpace(0), End,
    End;
    if (!c || !(phwin = wp_dialog("Phonetic Guide", MAKE_ID('W','2','7','P'), c, "_OK", ID_I_PH_OK))) return;
    DoMethod(rem, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_I_PH_REMOVE);
  }
  base[0] = 0;
  if (a.para == b.para && b.off > a.off && b.off - a.off < 120) {
    char u[128];
    memcpy(u, d->p[a.para]->t + a.off, b.off - a.off); u[b.off - a.off] = 0;
    to_gadget(u, base, sizeof base);
  }
  set(ph_base, MUIA_Text_Contents, (ULONG)base);
  to_gadget(r ? r->text : "", g, sizeof g);
  set(ph_text, MUIA_String_Contents, (ULONG)g);
  set(ph_align, MUIA_Cycle_Active, r ? r->align : RA_CENTER);
  set_num(ph_offset, r && r->raise ? (r->raise + 1) / 2 : 0);
  set_num(ph_size, (r && r->hps ? r->hps : (f ? (f->size + 1) / 2 : 10)) / 2);
  set(ph_font, MUIA_String_Contents, (ULONG)(r ? r->font : ""));
  set(phwin, MUIA_Window_Open, TRUE);
}
static void phonetic_ok(int remove)
{
  Doc *d = now.doc;
  Pos a, b;
  Ruby r;
  set(phwin, MUIA_Window_Open, FALSE);
  if (!d) return;
  sel(&a, &b);
  if (a.para == b.para && a.off == b.off) {
    /* no selection: the guide the caret is in, all of it */
    const CharFmt *f = caret_cf();
    u32 o;
    Para *p = d->p[a.para];
    if (!f || !f->ruby) { DisplayBeep(NULL); return; }
    for (o = a.off; o > 0 && para_fmt_at(p, (u32)utf8_prev(p->t, o))->ruby == f->ruby; o = (u32)utf8_prev(p->t, o)) ;
    a.off = o;
    for (o = b.off; o < p->len && para_fmt_at(p, o)->ruby == f->ruby; o = (u32)utf8_next(p->t, p->len, o)) ;
    b.off = o;
  }
  memset(&r, 0, sizeof r);
  from_gadget(get_str(ph_text), r.text, sizeof r.text);
  r.align = (u8)geti(ph_align, MUIA_Cycle_Active);
  r.raise = (u16)(get_num(ph_offset) * 2);
  r.hps = (u16)(get_num(ph_size) * 2);
  from_gadget(get_str(ph_font), r.font, sizeof r.font);
  if (intl_set_ruby(d, a, b, remove || !r.text[0] ? NULL : &r)) changed();
}

/* ------------------------------------------------------------------ */
/* Asian layout: two lines in one, combine characters, horizontal in vertical, emphasis marks */
static Object *tlwin, *tl_br;
static const char *tl_brs[EB_N + 1];
static void eal_set(int eal)
{
  CharFmt cf;
  static u16 ids = 1;
  memset(&cf, 0, sizeof cf);
  cf.eal = (u8)eal; cf.ealid = (u16)(eal ? ids++ : 0);
  cf.intl_mask = IM_EAL;
  char_fmt(CM_EA, &cf);
}
static void open_twolines(void)
{
  if (!tlwin) {
    Object *c;
    static const char *const b[EB_N] = { "None", "()", "[]", "<>", "{}" };
    int k;
    for (k = 0; k < EB_N; k++) tl_brs[k] = k ? b[k] : tr(b[k]);
    c = VGroup, QDLG,
      Child, HGroup, Child, QLabel(tr("Enclose in brackets"), MUIO_Label_DoubleFrame), Child, tl_br = cyc_obj(tl_brs), End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("The selected text is shown in two lines in the line's height."), End,
    End;
    if (!c || !(tlwin = wp_dialog("Two Lines in One", MAKE_ID('W','2','7','2'), c, "_OK", ID_I_TL_OK))) return;
  }
  {
    const CharFmt *f = caret_cf();
    set(tl_br, MUIA_Cycle_Active, f ? EAL_BRACKETS(f) : 0);
  }
  set(tlwin, MUIA_Window_Open, TRUE);
}
static void em_set(int em)
{
  CharFmt cf;
  memset(&cf, 0, sizeof cf);
  cf.em = (u8)em; cf.intl_mask = IM_EM;
  char_fmt(CM_EA, &cf);
}

/* ------------------------------------------------------------------ */
/* Asian Typography (the paragraph's) */
static Object *tywin, *ty_chk[7], *ty_align;
static const char *ty_aligns[6];
static const struct { const char *label; u8 bit; u8 inv; } tyrows[7] = {
  { "Use East Asian rules for first and last characters", AS_NOKINSOKU, 1 },
  { "Allow Latin text to wrap in the middle of a word", AS_NOWORDWRAP, 0 },
  { "Allow punctuation to extend past the margin", AS_NOOVERFLOW, 1 },
  { "Compress punctuation at the start of a line", AS_TOPLINE, 0 },
  { "Adjust space between Asian and Latin text", AS_NOSPACEDE, 1 },
  { "Adjust space between Asian text and numbers", AS_NOSPACEDN, 1 },
  { "Snap to grid when a document grid is defined", AS_NOSNAP, 1 } };
static void open_typo(void)
{
  Doc *d = now.doc;
  const ParaFmt *pf;
  int k;
  if (!d || now.caret.para >= d->np) return;
  pf = &d->p[now.caret.para]->pf;
  if (!tywin) {
    Object *c, *rows;
    static const char *const al[5] = { "Auto", "Top", "Centered", "Baseline", "Bottom" };
    for (k = 0; k < 5; k++) ty_aligns[k] = tr(al[k]);
    rows = ColGroup(2), End;
    c = VGroup, QDLG,
      Child, rows,
      Child, HGroup, Child, QLabel(tr("Text alignment"), MUIO_Label_DoubleFrame), Child, ty_align = cyc_obj(ty_aligns), End,
    End;
    if (!c) return;
    for (k = 0; k < 7; k++) {
      ty_chk[k] = chk_obj();
      DoMethod(rows, OM_ADDMEMBER, (ULONG)ty_chk[k]);
      DoMethod(rows, OM_ADDMEMBER, (ULONG)QLabel(tr(tyrows[k].label), MUIO_Label_LeftAligned | MUIO_Label_SingleFrame));
    }
    if (!(tywin = wp_dialog("Asian Typography", MAKE_ID('W','2','7','T'), c, "_OK", ID_I_TYPO_OK))) return;
  }
  for (k = 0; k < 7; k++) set(ty_chk[k], MUIA_Selected, ((pf->asian & tyrows[k].bit) != 0) != tyrows[k].inv);
  set(ty_align, MUIA_Cycle_Active, pf->talign);
  set(tywin, MUIA_Window_Open, TRUE);
}
static void typo_ok(void)
{
  ParaFmt pf;
  int k;
  set(tywin, MUIA_Window_Open, FALSE);
  memset(&pf, 0, sizeof pf);
  for (k = 0; k < 7; k++) if ((geti(ty_chk[k], MUIA_Selected) != 0) != tyrows[k].inv) pf.asian |= tyrows[k].bit;
  pf.talign = (u8)geti(ty_align, MUIA_Cycle_Active);
  para_fmt(PM_ASIAN, &pf);
}

/* ------------------------------------------------------------------ */
/* sections: direction, text direction, the document grid */
typedef struct { int what, v; i32 pitch; i32 cs; int grid; int tdir; } SectArg;
static void sect_fn(SectFmt *s, void *arg)
{
  SectArg *a = (SectArg *)arg;
  if (a->what == 0) s->bidi = (u8)a->v;
  else if (a->what == 1) s->tdir = (u8)a->v;
  else { s->grid = (u8)a->grid; s->linepitch = a->pitch; s->charspace = a->cs; s->tdir = (u8)a->tdir; }
}
static void sect_set(int what, int v, int whole)
{
  SectArg a;
  Doc *d = now.doc;
  memset(&a, 0, sizeof a);
  a.what = what; a.v = v;
  wp_story_close();
  if (d && intl_sect_apply(d, now.caret.para, whole, sect_fn, &a)) changed();
}
static Object *grwin, *gr_type, *gr_lines, *gr_chars, *gr_dir, *gr_apply, *gr_info;
static const char *gr_types[5], *gr_dirs[3], *gr_applies[3];
/* the caret's section's text area (twips) */
static void text_area(i32 *w, i32 *h)
{
  Doc *d = now.doc;
  const SectFmt *s = doc_sect(d, now.caret.para);
  const PageSetup *pg = sect_page(d, s, doc_last_sect(d, now.caret.para));
  *w = pg->width - pg->left - pg->right; *h = pg->height - pg->top - pg->bottom;
  if (*w < 1440) *w = 1440;
  if (*h < 1440) *h = 1440;
}
static void open_grid(void)
{
  Doc *d = now.doc;
  const SectFmt *s;
  i32 w, h, lp, cp;
  if (!d) return;
  s = doc_sect(d, now.caret.para);
  if (!grwin) {
    Object *c;
    static const char *const t[4] = { "No grid", "Specify line grid only", "Specify line and character grid", "Text snaps to character grid" };
    static const char *const dr[2] = { "Horizontal", "Vertical" };
    static const char *const ap[2] = { "This section", "Whole document" };
    int k;
    for (k = 0; k < 4; k++) gr_types[k] = tr(t[k]);
    for (k = 0; k < 2; k++) { gr_dirs[k] = tr(dr[k]); gr_applies[k] = tr(ap[k]); }
    c = VGroup, QDLG,
      Child, ColGroup(2),
        Child, QLabel(tr("Text direction"), MUIO_Label_DoubleFrame), Child, gr_dir = cyc_obj(gr_dirs),
        Child, QLabel(tr("Grid"), MUIO_Label_DoubleFrame), Child, gr_type = cyc_obj(gr_types),
        Child, QLabel(tr("Lines per page"), MUIO_Label_DoubleFrame), Child, gr_lines = num_obj(),
        Child, QLabel(tr("Characters per line"), MUIO_Label_DoubleFrame), Child, gr_chars = num_obj(),
        Child, QLabel(tr("Apply to"), MUIO_Label_DoubleFrame), Child, gr_apply = cyc_obj(gr_applies),
      End,
      Child, gr_info = TextObject, MUIA_Text_Contents, (ULONG)"", End,
    End;
    if (!c || !(grwin = wp_dialog("Document Grid", MAKE_ID('W','2','7','G'), c, "_OK", ID_I_GRID_OK))) return;
  }
  text_area(&w, &h);
  intl_grid(s, &lp, &cp, d->styles[0].cf.size);
  set(gr_dir, MUIA_Cycle_Active, s && s->tdir == STD_TBRL ? 1 : 0);
  set(gr_type, MUIA_Cycle_Active, s ? s->grid & 3 : 0);
  set_num(gr_lines, lp > 0 ? h / lp : h / 360);
  set_num(gr_chars, cp > 0 ? w / cp : w / ((d->styles[0].cf.size ? d->styles[0].cf.size : 21) * 10));
  set(gr_apply, MUIA_Cycle_Active, 0);
  {
    static char info[120];
    sprintf(info, tr("Font size of the Normal style: %d pt"), (int)((d->styles[0].cf.size + 1) / 2));
    set(gr_info, MUIA_Text_Contents, (ULONG)info);
  }
  set(grwin, MUIA_Window_Open, TRUE);
}
static void grid_ok(void)
{
  Doc *d = now.doc;
  SectArg a;
  i32 w, h;
  long lines = get_num(gr_lines), chars = get_num(gr_chars);
  set(grwin, MUIA_Window_Open, FALSE);
  if (!d) return;
  text_area(&w, &h);
  memset(&a, 0, sizeof a);
  a.what = 2;
  a.grid = (int)geti(gr_type, MUIA_Cycle_Active);
  a.tdir = geti(gr_dir, MUIA_Cycle_Active) ? STD_TBRL : STD_LRTB;
  a.pitch = lines > 0 ? h / (i32)lines : 360;
  if (a.grid == GRID_NONE && a.pitch == 360) a.pitch = 0;
  if ((a.grid == GRID_LINESCHARS || a.grid == GRID_SNAPCHARS) && chars > 0) {
    i32 pitch = w / (i32)chars, base = (d->styles[0].cf.size ? d->styles[0].cf.size : 21) * 10;
    a.cs = (pitch - base) * 4096 / 20;
  }
  wp_story_close();
  if (intl_sect_apply(d, now.caret.para, geti(gr_apply, MUIA_Cycle_Active) == 1, sect_fn, &a)) changed();
}

/* ------------------------------------------------------------------ */
/* tables */
static void table_rtl(void)
{
  Doc *d = now.doc;
  Table *t;
  if (!d || now.caret.para >= d->np || !(t = doc_table(d, &d->p[now.caret.para]->pf))) { DisplayBeep(NULL); return; }
  if (is_nested(&d->p[now.caret.para]->pf)) t = &d->tables[d->p[now.caret.para]->pf.outer.table - 1];
  t->bidivisual = (u8)!t->bidivisual;
  changed();
}

/* ------------------------------------------------------------------ */
/* Right-to-Left and Asian Options (File menu) */
static Object *opwin, *op_num, *op_level, *op_after, *op_before, *op_csc;
static const char *op_nums[5], *op_levels[4], *op_cscs[4];
static void open_options(void)
{
  Doc *d = now.doc;
  IntlSettings *is = d ? d->intl : NULL;
  char b[400];
  if (!opwin) {
    Object *c;
    static const char *const nm[4] = { "Arabic", "Hindi", "Context", "System" };
    static const char *const lv[3] = { "Standard", "Strict", "Custom" };
    static const char *const cs[3] = { "Don't compress", "Compress punctuation only", "Compress punctuation and Japanese kana" };
    int k;
    for (k = 0; k < 4; k++) op_nums[k] = tr(nm[k]);
    for (k = 0; k < 3; k++) { op_levels[k] = tr(lv[k]); op_cscs[k] = tr(cs[k]); }
    c = VGroup, QDLG,
      Child, HGroup, Child, QLabel(tr("Numeral"), MUIO_Label_DoubleFrame), Child, op_num = cyc_obj(op_nums), End,
      Child, VGroup, GroupFrameT(tr("Chinese/Japanese line breaking (this document)")),
        Child, ColGroup(2),
          Child, QLabel(tr("First and last characters"), MUIO_Label_DoubleFrame), Child, op_level = cyc_obj(op_levels),
          Child, QLabel(tr("Not at the start of a line"), MUIO_Label_DoubleFrame), Child, op_before = str_obj(380),
          Child, QLabel(tr("Not at the end of a line"), MUIO_Label_DoubleFrame), Child, op_after = str_obj(380),
          Child, QLabel(tr("Character spacing control"), MUIO_Label_DoubleFrame), Child, op_csc = cyc_obj(op_cscs),
        End,
      End,
    End;
    if (!c || !(opwin = wp_dialog("Right-to-Left and Asian Options", MAKE_ID('W','2','7','O'), c, "_OK", ID_I_OPT_OK))) return;
  }
  set(op_num, MUIA_Cycle_Active, bidi_numerals);
  set(op_level, MUIA_Cycle_Active, is && (is->nobefore[0] || is->noafter[0]) ? 2 : is && is->strict ? 1 : 0);
  to_gadget(is ? is->nobefore : "", b, sizeof b); set(op_before, MUIA_String_Contents, (ULONG)b);
  to_gadget(is ? is->noafter : "", b, sizeof b); set(op_after, MUIA_String_Contents, (ULONG)b);
  set(op_csc, MUIA_Cycle_Active, is ? is->cspacing : 0);
  set(opwin, MUIA_Window_Open, TRUE);
}
static void options_ok(void)
{
  Doc *d = now.doc;
  int n = (int)geti(op_num, MUIA_Cycle_Active), lv = (int)geti(op_level, MUIA_Cycle_Active), csc = (int)geti(op_csc, MUIA_Cycle_Active);
  set(opwin, MUIA_Window_Open, FALSE);
  if (n != bidi_numerals) { bidi_numerals = n; save_prefs(); }
  if (d) {
    IntlSettings *is = d->intl;
    if (lv || csc || is) {
      if ((is = intl_settings(d))) {
        is->strict = (u8)(lv == 1);
        if (lv == 2) {
          from_gadget(get_str(op_before), is->nobefore, sizeof is->nobefore);
          from_gadget(get_str(op_after), is->noafter, sizeof is->noafter);
        } else is->nobefore[0] = is->noafter[0] = 0;
        is->cspacing = (u8)csc; is->cspset = 1;
        d->dirty = 1;
      }
    }
  }
  changed();
}

/* ------------------------------------------------------------------ */
/* Asian and Complex Script Fonts (Format menu): the selection's East Asian and complex script fonts,
   complex script size, bold and italic, right-to-left run */
static Object *fnwin, *fn_ea, *fn_cs, *fn_size, *fn_b, *fn_i, *fn_rtl;
static void open_fonts(void)
{
  Doc *d = now.doc;
  const CharFmt *f = caret_cf();
  char b[200];
  if (!d) return;
  if (!fnwin) {
    Object *c;
    c = VGroup, QDLG,
      Child, ColGroup(2),
        Child, QLabel(tr("Asian text font"), MUIO_Label_DoubleFrame), Child, fn_ea = str_obj(47),
        Child, QLabel(tr("Complex scripts font"), MUIO_Label_DoubleFrame), Child, fn_cs = str_obj(47),
        Child, QLabel(tr("Complex scripts size (pt)"), MUIO_Label_DoubleFrame), Child, fn_size = num_obj(),
      End,
      Child, HGroup, Child, fn_b = chk_obj(), Child, QLabel(tr("Complex scripts bold"), MUIO_Label_SingleFrame),
                     Child, fn_i = chk_obj(), Child, QLabel(tr("Complex scripts italic"), MUIO_Label_SingleFrame), Child, HSpace(0), End,
      Child, HGroup, Child, fn_rtl = chk_obj(), Child, QLabel(tr("Right-to-left text"), MUIO_Label_SingleFrame), Child, HSpace(0), End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Empty: the text's own font and size"), End,
    End;
    if (!c || !(fnwin = wp_dialog("Asian and Complex Script Fonts", MAKE_ID('W','2','7','F'), c, "_OK", ID_I_FONTS_OK))) return;
  }
  to_gadget(f && f->fontea && f->fontea <= d->nfonts ? d->fonts[f->fontea - 1] : "", b, sizeof b); set(fn_ea, MUIA_String_Contents, (ULONG)b);
  to_gadget(f && f->fontcs && f->fontcs <= d->nfonts ? d->fonts[f->fontcs - 1] : "", b, sizeof b); set(fn_cs, MUIA_String_Contents, (ULONG)b);
  if (f && f->sizecs) set_num(fn_size, (f->sizecs + 1) / 2); else set(fn_size, MUIA_String_Contents, (ULONG)"");
  set(fn_b, MUIA_Selected, f && (f->cs & CS_OWNB ? (f->cs & CS_BOLD) != 0 : (f->flags & CF_BOLD) != 0));
  set(fn_i, MUIA_Selected, f && (f->cs & CS_OWNI ? (f->cs & CS_ITALIC) != 0 : (f->flags & CF_ITALIC) != 0));
  set(fn_rtl, MUIA_Selected, f && f->rtl);
  set(fnwin, MUIA_Window_Open, TRUE);
}
static void fonts_ok(void)
{
  Doc *d = now.doc;
  CharFmt cf;
  const CharFmt *f = caret_cf();
  char u[200];
  long sz = get_num(fn_size);
  int b = geti(fn_b, MUIA_Selected) != 0, i = geti(fn_i, MUIA_Selected) != 0;
  set(fnwin, MUIA_Window_Open, FALSE);
  if (!d) return;
  memset(&cf, 0, sizeof cf);
  from_gadget(get_str(fn_ea), u, sizeof u);
  cf.fontea = (u8)(u[0] ? doc_font(d, u) + 1 : 0);
  from_gadget(get_str(fn_cs), u, sizeof u);
  cf.fontcs = (u8)(u[0] ? doc_font(d, u) + 1 : 0);
  cf.sizecs = (u16)(sz > 0 && sz < 1600 ? sz * 2 : 0);
  cf.rtl = (u8)(geti(fn_rtl, MUIA_Selected) != 0);
  /* bold and italic of their own only where they differ from the text's */
  if (f && b != ((f->flags & CF_BOLD) != 0)) cf.cs |= CS_OWNB | (b ? CS_BOLD : 0);
  if (f && i != ((f->flags & CF_ITALIC) != 0)) cf.cs |= CS_OWNI | (i ? CS_ITALIC : 0);
  if (f) cf.cs |= f->cs & CS_ALL;
  cf.intl_mask = IM_RTL | IM_CSFL | IM_FONTCS | IM_SIZECS | IM_FONTEA;
  char_fmt(CM_CS | CM_EA, &cf);
}

/* ------------------------------------------------------------------ */
int wp27_command(ULONG id, LONG value)
{
  Doc *d = now.doc;
  switch (id) {
  case ID_I_LTR: case ID_I_RTL: {
    ParaFmt pf;
    memset(&pf, 0, sizeof pf);
    pf.bidi = (u8)(id == ID_I_RTL);
    para_fmt(PM_BIDI, &pf);
    break; }
  case ID_I_RUNRTL: {
    CharFmt cf;
    const CharFmt *f = caret_cf();
    memset(&cf, 0, sizeof cf);
    cf.rtl = (u8)!(f && f->rtl); cf.intl_mask = IM_RTL;
    char_fmt(CM_CS, &cf);
    break; }
  case ID_I_PHONETIC: open_phonetic(); return WPR_KEEPFOCUS;
  case ID_I_PH_OK: phonetic_ok(0); break;
  case ID_I_PH_REMOVE: phonetic_ok(1); break;
  case ID_I_ASIAN:
    /* the Asian Layout list */
    switch (value) {
    case 0: open_phonetic(); return WPR_KEEPFOCUS;
    case 1: eal_set(EAL_COMBINE); break;
    case 2: open_twolines(); return WPR_KEEPFOCUS;
    case 3: { const CharFmt *f = caret_cf(); eal_set(f && (f->eal & EAL_VERT) ? 0 : EAL_VERT | EAL_VCOMPRESS); break; }
    case 4: case 5: case 6: case 7: em_set(value - 3); break;
    case 8: em_set(EM_NONE); eal_set(0); break;
    default: open_phonetic(); return WPR_KEEPFOCUS;
    }
    break;
  case ID_I_COMBINE: eal_set(EAL_COMBINE); break;
  case ID_I_TWOLINES: open_twolines(); return WPR_KEEPFOCUS;
  case ID_I_TL_OK: set(tlwin, MUIA_Window_Open, FALSE); eal_set(EAL_COMBINE | ((int)geti(tl_br, MUIA_Cycle_Active) << 4)); break;
  case ID_I_HVERT: { const CharFmt *f = caret_cf(); eal_set(f && (f->eal & EAL_VERT) ? 0 : EAL_VERT | EAL_VCOMPRESS); break; }
  case ID_I_EALCLEAR: eal_set(0); em_set(EM_NONE); break;
  case ID_I_EM0: case ID_I_EM1: case ID_I_EM2: case ID_I_EM3: case ID_I_EM4: em_set((int)(id - ID_I_EM0)); break;
  case ID_I_TYPO: open_typo(); return WPR_KEEPFOCUS;
  case ID_I_TYPO_OK: typo_ok(); break;
  case ID_I_GRID: open_grid(); return WPR_KEEPFOCUS;
  case ID_I_GRID_OK: grid_ok(); break;
  case ID_I_SECTRTL: { const SectFmt *s = d ? doc_sect(d, now.caret.para) : NULL; sect_set(0, !(s && s->bidi), 0); break; }
  case ID_I_TEXTDIR: sect_set(1, value == 1 ? STD_TBRL : value == 2 ? STD_BTLR : STD_LRTB, 0); break;
  case ID_I_TD0: sect_set(1, STD_LRTB, 0); break;
  case ID_I_TD1: sect_set(1, STD_TBRL, 0); break;
  case ID_I_TD2: sect_set(1, STD_BTLR, 0); break;
  case ID_I_TABLERTL: table_rtl(); break;
  case ID_I_OPTIONS: open_options(); return WPR_KEEPFOCUS;
  case ID_I_OPT_OK: options_ok(); break;
  case ID_I_FONTS: open_fonts(); return WPR_KEEPFOCUS;
  case ID_I_FONTS_OK: fonts_ok(); break;
  }
  return WPR_DONE;
}
static void wp27_state(ULONG *on, const WpCtx *c)
{
  const CharFmt *f;
  now = *c;
  if (!c->doc || c->caret.para >= c->doc->np) return;
  *on |= 1UL << (c->doc->p[c->caret.para]->pf.bidi ? S_RTL : S_LTR);
  {
    const SectFmt *s = doc_sect(c->doc, c->caret.para);
    if (s && s->bidi) *on |= 1UL << S_SECTRTL;
    if (s && s->tdir == STD_TBRL) *on |= 1UL << S_TDV;
  }
  if (c->intable) {
    Table *t = doc_table(c->doc, &c->doc->p[c->caret.para]->pf);
    *on |= 1UL << S_INTABLE;
    if (t && is_nested(&c->doc->p[c->caret.para]->pf)) t = &c->doc->tables[c->doc->p[c->caret.para]->pf.outer.table - 1];
    if (t && t->bidivisual) *on |= 1UL << S_TABLERTL;
  }
  f = caret_cf();
  if (f && (f->eal & EAL_VERT)) *on |= 1UL << S_HVERT;
  if (f && f->rtl) *on |= 1UL << S_RUNRTL;
}
static void wp27_init(void) { load_prefs(); }

/* ------------------------------------------------------------------ */
/* the ribbon and the menus */
#define IC_LTRD "ci L 6 2 6 13 L 9 2 9 13 L 6 2 11 2 P 5 6 2 11 2 11 4 8 4 6 ca L 2 15 14 15 L 12 13 14 15 L 12 13 12 15"
#define IC_RTLD "ci L 7 2 7 13 L 10 2 10 13 L 7 2 12 2 P 5 7 2 12 2 12 4 9 4 7 ca L 2 15 14 15 L 4 13 2 15 L 4 13 4 15"
static const char *const asian_entries[] = { "Phonetic Guide...", "Combine Characters", "Two Lines in One...", "Horizontal in Vertical",
                                             "Emphasis Mark: Dot", "Emphasis Mark: Comma", "Emphasis Mark: Circle", "Emphasis Mark: Under Dot", "Remove Asian Layout" };
static const RibbonList asian_list = { asian_entries, 9, PL_TEXT, NULL };
static const char *const td_entries[] = { "Horizontal", "Vertical", "Rotate All Text 270\260" };
static const RibbonList td_list = { td_entries, 3, PL_TEXT, NULL };
static const Item home_items[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Left-to-Right", ID_I_LTR, RB_WP(WPI_27, S_LTR), 0, 0, 0,
    "Left-to-Right Text Direction: the paragraph reads from left to right", NULL, IC_LTRD, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Right-to-Left", ID_I_RTL, RB_WP(WPI_27, S_RTL), 0, 0, 0,
    "Right-to-Left Text Direction: the paragraph reads from right to left (Hebrew, Arabic): its indents and alignment from the right", NULL, IC_RTLD, 0 },
  { K_DROP, S_SMALL, 0, 0, "Asian Layout", ID_I_ASIAN, 0, 0, F_SPLIT, 0,
    "Phonetic Guide, Combine Characters, Two Lines in One, Horizontal in Vertical, emphasis marks", &asian_list,
    "ci L 3 12 6 3 L 6 3 9 12 L 4 9 8 9 ca L 10 4 14 4 L 12 2 12 14 L 10 8 14 8", ID_I_PHONETIC },
};
static const Item layout_items[] = {
  { K_DROP, S_LARGE, 0, 0, "Text Direction", ID_I_TEXTDIR, RB_WP(WPI_27, S_TDV), 0, 0, 0,
    "Text Direction: the section's text across the page, or in vertical lines from the right (East Asian)", &td_list,
    "ci L 3 3 13 3 L 8 3 8 13 ca L 12 6 12 14 L 10 12 12 14 L 14 12 12 14", 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Section Direction", ID_I_SECTRTL, RB_WP(WPI_27, S_SECTRTL), 0, F_NEWCOL, 0,
    "Section Direction right to left: the section's columns from the right", NULL, IC_RTLD, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Document Grid...", ID_I_GRID, 0, 0, 0, 0,
    "Document Grid: lines per page and characters per line, the section's text direction", NULL,
    "ci R 2 2 14 14 cl L 2 6 14 6 L 2 10 14 10 L 6 2 6 14 L 10 2 10 14", 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Asian Typography...", ID_I_TYPO, 0, 0, 0, 0,
    "Asian Typography: line breaking rules, punctuation, spacing between Asian and Latin text, the grid", NULL,
    "ci L 2 4 7 4 L 4 2 4 9 L 2 9 7 9 ca L 9 6 14 6 L 9 10 14 10 L 9 14 14 14", 0 },
};
static const Item table_items[] = {
  { K_BUTTON, S_LARGE, 0, 0, "Table Direction", ID_I_TABLERTL, RB_WP(WPI_27, S_TABLERTL), RB_WP(WPI_27, S_INTABLE), 0, 0,
    "Table Direction right to left: the table's first column on the right", NULL,
    "ci R 1 3 15 11 L 6 3 6 11 L 10 3 10 11 L 1 7 15 7 ca L 3 14 13 14 L 5 12 3 14 L 5 16 3 14", 0 },
};
static const WpGroup groups27[] = {
  { TAB_HOME, 3, { "Direction", G_COLS, home_items, 3 } },
  { TAB_LAYOUT, -1, { "Direction & Grid", G_COLS, layout_items, 4 } },
  { TAB_TABLELAYOUT, -1, { "Direction", G_COLS, table_items, 1 } },
};
static const WpMenu menus27[] = {
  { WM_FILE, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FILE, { NM_ITEM, (STRPTR)"Right-to-Left and Asian Options...", 0, 0, 0, (APTR)ID_I_OPTIONS } },
  { WM_FORMAT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_ITEM, (STRPTR)"Text Direction", 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Left-to-Right", 0, 0, 0, (APTR)ID_I_LTR } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Right-to-Left", 0, 0, 0, (APTR)ID_I_RTL } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Right-to-Left Text (Run)", 0, 0, 0, (APTR)ID_I_RUNRTL } },
  { WM_FORMAT, { NM_ITEM, (STRPTR)"Asian Layout", 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Phonetic Guide...", 0, 0, 0, (APTR)ID_I_PHONETIC } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Combine Characters", 0, 0, 0, (APTR)ID_I_COMBINE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Two Lines in One...", 0, 0, 0, (APTR)ID_I_TWOLINES } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Horizontal in Vertical", 0, 0, 0, (APTR)ID_I_HVERT } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Emphasis Mark: Dot", 0, 0, 0, (APTR)ID_I_EM1 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Emphasis Mark: Comma", 0, 0, 0, (APTR)ID_I_EM2 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Emphasis Mark: Circle", 0, 0, 0, (APTR)ID_I_EM3 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Emphasis Mark: Under Dot", 0, 0, 0, (APTR)ID_I_EM4 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Remove Asian Layout", 0, 0, 0, (APTR)ID_I_EALCLEAR } },
  { WM_FORMAT, { NM_ITEM, (STRPTR)"Asian Typography...", 0, 0, 0, (APTR)ID_I_TYPO } },
  { WM_FORMAT, { NM_ITEM, (STRPTR)"Asian and Complex Script Fonts...", 0, 0, 0, (APTR)ID_I_FONTS } },
  { WM_TABLE, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_TABLE, { NM_ITEM, (STRPTR)"Table Direction Right-to-Left", 0, 0, 0, (APTR)ID_I_TABLERTL } },
  { WM_LAYOUT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_LAYOUT, { NM_ITEM, (STRPTR)"Text Direction", 0, 0, 0, 0 } },
  { WM_LAYOUT, { NM_SUB, (STRPTR)"Horizontal", 0, 0, 0, (APTR)ID_I_TD0 } },
  { WM_LAYOUT, { NM_SUB, (STRPTR)"Vertical", 0, 0, 0, (APTR)ID_I_TD1 } },
  { WM_LAYOUT, { NM_SUB, (STRPTR)"Rotate All Text 270\260", 0, 0, 0, (APTR)ID_I_TD2 } },
  { WM_LAYOUT, { NM_ITEM, (STRPTR)"Section Direction Right-to-Left", 0, 0, 0, (APTR)ID_I_SECTRTL } },
  { WM_LAYOUT, { NM_ITEM, (STRPTR)"Document Grid...", 0, 0, 0, (APTR)ID_I_GRID } },
};
/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp27_package = {
  "WP27", ID_WP27_FIRST, ID_WP27_LAST,
  wp27_command, wp27_state, NULL, NULL, NULL, wp27_init, NULL,
  groups27, (int)(sizeof groups27 / sizeof groups27[0]),
  menus27, (int)(sizeof menus27 / sizeof menus27[0])
};
