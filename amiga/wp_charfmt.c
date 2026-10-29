/*
 * wp_charfmt.c - WP9 Character formatting (docs/wordcraft-gap.md). This file belongs to WP9: only that
 * package edits it. Its commands are ID_WP9_FIRST .. ID_WP9_LAST (named in wp_charpara.h); how the
 * hooks below are called, and how to add ribbon groups, menu items, context-menu items and keys:
 * wphooks.h.
 *
 * Hidden text, outline, shadow, emboss and engrave, the underline's colour, character shading (apart
 * from the highlight), typography kept for Word (ligatures, number forms) and enclosed characters:
 * on the Home tab's Font group (ribbon.c takes the lists below), in the Format menu, in the Font
 * window (main.c takes wp9_font_group) and in their own Enclose Characters window.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
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

/* ---- the lists */
static const char *const effects[C_EF_N] = { "Outline", "Shadow", "Emboss", "Engrave", "Hidden", "Standard Ligatures", "No Ligatures",
                                             "Old-style Numbers", "Lining Numbers", "Clear Text Effects" };
const RibbonList wp9_effects_list = { effects, C_EF_N, PL_TEXT, NULL };
static const char *const ulcolors[] = { "Automatic", "Black", "Dark Red", "Red", "Orange", "Gold", "Green", "Teal", "Blue", "Dark Blue", "Purple", "Grey" };
static const ULONG ulvals[] = { 0xFF000000, 0x000000, 0xC00000, 0xFF0000, 0xFFA500, 0xBF9000, 0x00B050, 0x008080, 0x0070C0, 0x002060, 0x7030A0, 0x808080 };
const RibbonList wp9_ulcolor_list = { ulcolors, 12, PL_COLOR, ulvals };
static const char *const shades[] = { "No Shading", "Grey 15%", "Grey 25%", "Grey 50%", "Light Yellow", "Light Blue", "Light Green", "Light Red", "Turquoise" };
static const ULONG shadevals[] = { 0xFF000000, 0xD9D9D9, 0xBFBFBF, 0x808080, 0xFFF2CC, 0xDEEAF6, 0xE2EFD9, 0xF8CBAD, 0x00B0F0 };
const RibbonList wp9_shade_list = { shades, 9, PL_COLOR, shadevals };

/* ---- the document */
/* the selection's ends in order (equal: none) */
static void sel_range(Pos *a, Pos *b)
{
  Pos *c = NULL, *an = NULL;
  get(wp_view(), MUIA_Quill_Caret, &c); get(wp_view(), MUIA_Quill_Anchor, &an);
  *a = c ? *c : doc_start(wp_doc()); *b = an ? *an : *a;
  if (pos_cmp(*a, *b) > 0) { Pos t = *a; *a = *b; *b = t; }
}
/* the formatting the commands look at: the selection's first character's, or the text's before the caret */
static const CharFmt *cur_fmt(void)
{
  Doc *d = wp_doc();
  Pos a, b;
  sel_range(&a, &b);
  return pos_cmp(a, b) ? para_fmt_at(d->p[a.para], a.off) : para_fmt_before(d->p[a.para], a.off);
}
/* a format change through the view (the selection, the word at the caret, or the next typing) */
static void charfmt(unsigned mask, const CharFmt *v)
{
  static struct QCharFmt q;
  q.mask = mask; q.cf = *v;
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_CHARFMT, (ULONG)&q);
}
/* fx2 bits set (bits) in the bits mask names */
static void set_fx2(u16 bits, u16 mask)
{
  CharFmt v;
  memset(&v, 0, sizeof v);
  v.fx2 = bits; v.fx2mask = mask;
  charfmt(CM_FX2, &v);
}
/* an effect on, or off if the text has it: emboss and engrave exclude each other and the outline and
   shadow, as in Word */
static void toggle(u16 bit)
{
  int on = !(cur_fmt()->fx2 & bit);
  u16 mask = bit;
  if (on && (bit & (FX2_EMBOSS | FX2_ENGRAVE))) mask = FX2_RELIEF;
  else if (on && (bit & (FX2_OUTLINE | FX2_SHADOW))) mask = (u16)(bit | FX2_EMBOSS | FX2_ENGRAVE);
  set_fx2(on ? bit : 0, mask);
}
/* a typography setting kept for Word (w14): the element named replaced by add (NULL: taken out) */
static void typography(const char *name, const char *add)
{
  CharFmt v = *cur_fmt();
  v.rx = rx_edit(v.rx, name, add);
  charfmt(CM_RX, &v);
}

/* ---- the Enclose Characters window */
static Object *encwin, *enc_style, *enc_text, *enc_shape;
static const char *const enc_styles[] = { "None", "Shrink text", "Enlarge symbol", NULL }, *const enc_shapes[] = { "Circle", "Square", "Triangle", "Diamond", NULL };
static const char *t_styles[4], *t_shapes[5];
static const char **trl(const char *const *en, const char **out) { int i; for (i = 0; en[i]; i++) out[i] = tr(en[i]); out[i] = NULL; return out; }
static void open_enclose(void)
{
  Doc *d = wp_doc();
  Pos a, b;
  const CharFmt *f;
  char t[64] = "";
  if (!encwin) {
    Object *c = VGroup,
      Child, ColGroup(2),
        Child, Label1(tr("Style")), Child, enc_style = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(enc_styles, t_styles), MUIA_CycleChain, 1, End,
        Child, Label2(tr("Text")), Child, enc_text = StringObject, StringFrame, MUIA_String_MaxLen, 12, MUIA_CycleChain, 1, End,
        Child, Label1(tr("Enclosure")), Child, enc_shape = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(enc_shapes, t_shapes), MUIA_CycleChain, 1, End,
      End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("One or two characters, enclosed in a circle, square, triangle or diamond."), MUIA_Font, MUIV_Font_Tiny, End,
    End;
    if (!(encwin = wp_dialog("Enclose Characters", MAKE_ID('E','N','C','L'), c, "_OK", ID_C_ENCGO))) return;
  }
  sel_range(&a, &b);
  f = cur_fmt();
  if (pos_cmp(a, b) && a.para == b.para && b.off - a.off < 40) {
    char u[48];
    memcpy(u, d->p[a.para]->t + a.off, b.off - a.off); u[b.off - a.off] = 0;
    utf8_to_latin1(u, (u32)strlen(u), t, sizeof t);
  }
  set(enc_style, MUIA_Cycle_Active, ENCLOSURE(f) ? (f->fx2 & FX2_ENLARGE ? 2 : 1) : 1);
  set(enc_shape, MUIA_Cycle_Active, ENCLOSURE(f) ? ENCLOSURE(f) - 1 : 0);
  set(enc_text, MUIA_String_Contents, (ULONG)t);
  set(encwin, MUIA_Window_Open, TRUE);
  set(encwin, MUIA_Window_ActiveObject, (ULONG)enc_text);
}
/* OK: the selection enclosed (or no longer, style None); text typed that isn't the selection goes in
   at the caret, enclosed */
static void enclose_done(void)
{
  Doc *d = wp_doc();
  ULONG st = 0, sh = 0;
  char *s = NULL, u[64], sel[64] = "";
  Pos a, b;
  u32 nchars = 0, i;
  u16 bits;
  get(enc_style, MUIA_Cycle_Active, &st); get(enc_shape, MUIA_Cycle_Active, &sh); get(enc_text, MUIA_String_Contents, &s);
  set(encwin, MUIA_Window_Open, FALSE);
  set(wp_win(), MUIA_Window_Activate, TRUE);
  sel_range(&a, &b);
  if (st == 0) { if (pos_cmp(a, b)) set_fx2(0, FX2_ENCLOSE | FX2_ENLARGE); else DisplayBeep(NULL); return; }
  bits = (u16)(((sh + 1) << 5) | (st == 2 ? FX2_ENLARGE : 0));
  wp_latin1_utf8(s ? s : "", u, sizeof u);
  for (i = 0; u[i]; i++) if (((u8)u[i] & 0xC0) != 0x80) nchars++;
  if (pos_cmp(a, b) && a.para == b.para && b.off - a.off < sizeof sel) { memcpy(sel, d->p[a.para]->t + a.off, b.off - a.off); sel[b.off - a.off] = 0; }
  if (!u[0] || !strcmp(u, sel)) {
    /* the selection itself */
    if (!pos_cmp(a, b)) { DisplayBeep(NULL); return; }
    set_fx2(bits, FX2_ENCLOSE | FX2_ENLARGE);
    return;
  }
  if (nchars > 2) { wp_ask("OK", "%s", tr("Only one or two characters can be enclosed.")); return; }
  {
    /* the text given, in at the caret (replacing the selection) */
    CharFmt f = *cur_fmt();
    f.fx2 = (u16)((f.fx2 & ~(FX2_ENCLOSE | FX2_ENLARGE)) | bits);
    doc_begin(d, a);
    if (pos_cmp(a, b)) doc_delete(d, a, b);
    doc_insert(d, &a, u, (u32)strlen(u), &f);
    doc_end(d, a);
    DoMethod(wp_view(), MUIM_Quill_Relayout);
    set(wp_view(), MUIA_Quill_Caret, (ULONG)&a);
  }
}

/* ---- WP9's part of the Font window (main.c): effects, the underline's colour, shading */
static Object *fo_hidden, *fo_outline, *fo_shadow, *fo_emboss, *fo_engrave, *fo_ulcol, *fo_shade;
static const char *t_ulcols[13], *t_shades[10];
static const char *ulcols_z[13], *shades_z[10];
Object *wp9_font_group(void)
{
  int i;
  for (i = 0; i < 12; i++) ulcols_z[i] = ulcolors[i];
  ulcols_z[12] = NULL;
  for (i = 0; i < 9; i++) shades_z[i] = shades[i];
  shades_z[9] = NULL;
  return VGroup,
    Child, ColGroup(6),
      Child, fo_hidden = CheckMark(FALSE), Child, LLabel1(tr("Hidden")),
      Child, fo_outline = CheckMark(FALSE), Child, LLabel1(tr("Outline")),
      Child, fo_shadow = CheckMark(FALSE), Child, LLabel1(tr("Shadow")),
      Child, fo_emboss = CheckMark(FALSE), Child, LLabel1(tr("Emboss")),
      Child, fo_engrave = CheckMark(FALSE), Child, LLabel1(tr("Engrave")),
      Child, HSpace(0), Child, HSpace(0),
    End,
    Child, ColGroup(4),
      Child, Label1(tr("Underline colour")), Child, fo_ulcol = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(ulcols_z, t_ulcols), MUIA_CycleChain, 1, End,
      Child, Label1(tr("Shading")), Child, fo_shade = CycleObject, MUIA_Cycle_Entries, (ULONG)trl(shades_z, t_shades), MUIA_CycleChain, 1, End,
    End,
  End;
}
/* the list entry for a colour (0: automatic or none; an unlisted one shows as the first) */
static int col_entry(const ULONG *vals, int n, u32 c)
{
  int i;
  if (!c) return 0;
  for (i = 1; i < n; i++) if ((vals[i] & 0xFFFFFF) == (c & 0xFFFFFF)) return i;
  return 0;
}
void wp9_font_fill(const CharFmt *f)
{
  if (!fo_hidden) return;
  set(fo_hidden, MUIA_Selected, (f->fx2 & FX2_HIDDEN) != 0); set(fo_outline, MUIA_Selected, (f->fx2 & FX2_OUTLINE) != 0);
  set(fo_shadow, MUIA_Selected, (f->fx2 & FX2_SHADOW) != 0); set(fo_emboss, MUIA_Selected, (f->fx2 & FX2_EMBOSS) != 0);
  set(fo_engrave, MUIA_Selected, (f->fx2 & FX2_ENGRAVE) != 0);
  set(fo_ulcol, MUIA_Cycle_Active, col_entry(ulvals, 12, f->ulcolor));
  set(fo_shade, MUIA_Cycle_Active, col_entry(shadevals, 9, f->shade));
}
static int sel_of(Object *o) { ULONG v = 0; get(o, MUIA_Selected, &v); return v != 0; }
void wp9_font_read(const CharFmt *was, CharFmt *n, unsigned *mask)
{
  static const u16 bits[5] = { FX2_HIDDEN, FX2_OUTLINE, FX2_SHADOW, FX2_EMBOSS, FX2_ENGRAVE };
  Object *const box[5] = { fo_hidden, fo_outline, fo_shadow, fo_emboss, fo_engrave };
  ULONG v = 0;
  int k;
  u16 changed = 0;
  if (!fo_hidden) return;
  /* only the effects changed in the window (a selection of mixed effects keeps the rest) */
  for (k = 0; k < 5; k++) {
    int on = sel_of(box[k]);
    if (on != ((was->fx2 & bits[k]) != 0)) { changed |= bits[k]; if (on) n->fx2 |= bits[k]; else n->fx2 &= (u16)~bits[k]; }
  }
  if (changed) { *mask |= CM_FX2; n->fx2mask = changed; }
  get(fo_ulcol, MUIA_Cycle_Active, &v);
  if (v != (ULONG)col_entry(ulvals, 12, was->ulcolor)) { n->ulcolor = v ? HL_ON | (ulvals[v] & 0xFFFFFF) : 0; *mask |= CM_ULCOLOR; }
  get(fo_shade, MUIA_Cycle_Active, &v);
  if (v != (ULONG)col_entry(shadevals, 9, was->shade)) { n->shade = v ? HL_ON | (shadevals[v] & 0xFFFFFF) : 0; *mask |= CM_SHADE; }
}

/* ---- the commands */
int wp9_command(ULONG id, LONG value)
{
  CharFmt v;
  memset(&v, 0, sizeof v);
  switch (id) {
  case ID_C_HIDDEN: toggle(FX2_HIDDEN); break;
  case ID_C_OUTLINE: toggle(FX2_OUTLINE); break;
  case ID_C_SHADOW: toggle(FX2_SHADOW); break;
  case ID_C_EMBOSS: toggle(FX2_EMBOSS); break;
  case ID_C_ENGRAVE: toggle(FX2_ENGRAVE); break;
  case ID_C_EFFECTS:
    switch (value) {
    case C_EF_OUTLINE: toggle(FX2_OUTLINE); break;
    case C_EF_SHADOW: toggle(FX2_SHADOW); break;
    case C_EF_EMBOSS: toggle(FX2_EMBOSS); break;
    case C_EF_ENGRAVE: toggle(FX2_ENGRAVE); break;
    case C_EF_HIDDEN: toggle(FX2_HIDDEN); break;
    case C_EF_LIGSTD: typography("w14:ligatures", "<w14:ligatures w14:val=\"standard\"/>"); break;
    case C_EF_LIGNONE: typography("w14:ligatures", NULL); break;
    case C_EF_OLDSTYLE: typography("w14:numForm", "<w14:numForm w14:val=\"oldStyle\"/>"); break;
    case C_EF_LINING: typography("w14:numForm", "<w14:numForm w14:val=\"lining\"/>"); break;
    case C_EF_CLEAR:
      /* the relief effects and the kept text effects off (hidden and enclosures stay) */
      v.fx2 = 0; v.fx2mask = FX2_RELIEF; v.rx = 0;
      charfmt(CM_FX2 | CM_RX, &v);
      break;
    }
    break;
  case ID_C_ULCOLOR: {
    /* the colour, and an underline if there is none (as Word) */
    const CharFmt *f = cur_fmt();
    unsigned mask = CM_ULCOLOR;
    v.ulcolor = (ULONG)value == 0xFF000000UL || value < 0 ? 0 : HL_ON | ((u32)value & 0xFFFFFF);
    if (!(f->flags & CF_UNDERLINE)) { v.flags = CF_UNDERLINE; v.ul = UL_SINGLE; mask |= CM_UNDERLINE | CM_UL; }
    charfmt(mask, &v);
    break; }
  case ID_C_SHADE:
    v.shade = (ULONG)value == 0xFF000000UL || value < 0 ? 0 : HL_ON | ((u32)value & 0xFFFFFF);
    charfmt(CM_SHADE, &v);
    break;
  case ID_C_SHADETOGGLE:
    /* Word's button: grey shading (15%), or none if the text has shading */
    v.shade = cur_fmt()->shade ? 0 : HL_ON | 0xD9D9D9;
    charfmt(CM_SHADE, &v);
    break;
  case ID_C_ENCLOSE: open_enclose(); return WPR_KEEPFOCUS;
  case ID_C_ENCGO: enclose_done(); break;
  case ID_C_FONTWIN: DoMethod(wp_app(), MUIM_Application_ReturnID, ID_FONTDLG); break;
  }
  return WPR_DONE;
}

/* the Format menu's Text Effects submenu */
static const WpMenu menus[] = {
  { WM_FORMAT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_ITEM, (STRPTR)"Text Effects", 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Hidden", 0, 0, 0, (APTR)ID_C_HIDDEN } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Outline", 0, 0, 0, (APTR)ID_C_OUTLINE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Shadow", 0, 0, 0, (APTR)ID_C_SHADOW } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Emboss", 0, 0, 0, (APTR)ID_C_EMBOSS } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Engrave", 0, 0, 0, (APTR)ID_C_ENGRAVE } },
  { WM_FORMAT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Character Shading", 0, 0, 0, (APTR)ID_C_SHADETOGGLE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Underline Colour...", 0, 0, 0, (APTR)ID_C_FONTWIN } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Enclose Characters...", 0, 0, 0, (APTR)ID_C_ENCLOSE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Clear Text Effects", 0, 0, 0, (APTR)ID_C_EFFECTS } },
};
/* (Clear Text Effects from the menu: the list's last entry; a menu item comes with WPV_NONE) */
static int command(ULONG id, LONG value)
{
  if (id == ID_C_EFFECTS && value == WPV_NONE) value = C_EF_CLEAR;
  return wp9_command(id, value);
}
/* the right-button menu over text: the effects as a submenu */
static void ctxmenu(Object *menu, const WpCtx *c)
{
  static const char *const labels[5] = { "Outline", "Shadow", "Emboss", "Engrave", "Hidden" };
  if (c->where != WPC_TEXT && c->where != WPC_SPELLING) return;
  wp_ctx_sub(menu, tr("Text Effects"), ID_C_EFFECTS, labels, 5);
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items (its ribbon items are in Quill's own Font group: ribbon.c's g_font) */
const WpPackage wp9_package = {
  "WP9", ID_WP9_FIRST, ID_WP9_LAST,
  command, NULL, NULL, ctxmenu, NULL, NULL, NULL,
  NULL, 0,
  menus, (int)(sizeof menus / sizeof menus[0])
};
