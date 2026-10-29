/*
 * wp_find.c - WP12 Find, Go To, selection and editing keys (docs/wordcraft-gap.md). This file belongs to WP12: only that
 * package edits it. Its commands are ID_WP12_FIRST .. ID_WP12_LAST (ids.h, named in wp_find.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * What it adds: the Find and Replace window with Word's Advanced Find (special characters,
 * wildcards, formatting and styles to find and to give, match prefix and suffix, ignoring
 * punctuation and white space, searching down, up or the selection only, Highlight All) and its Go
 * To page (page, section, line, bookmark, comment, footnote, endnote, field, table, graphic, object,
 * heading); Go Back (Shift-F5), Repeat (F4), the Undo and Redo lists, Spike and Insert Spike
 * (Ctrl-F3, Ctrl-Shift-F3), Toggle Unicode Character (Ctrl-X: Alt-X types a character on the Amiga),
 * Extend Selection (F8, Shift-F8), column selection (Ctrl-Shift-F8, or Alt with the mouse), Select
 * Objects and Select Similar Formatting. The searching is core/findx.c's; the selections the view
 * draws are amiga/wp_find_view.h's.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include <devices/inputevent.h>
#include "wphooks.h"
#include "wp_find.h"
#include "lang.h"
#include "../core/findx.h"

#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif
#undef Label1
#undef Label2
#undef LLabel1
#define Label2(l) QLabel(l, MUIO_Label_DoubleFrame)
#define Label1(l) QLabel(l, MUIO_Label_SingleFrame)
#define LLabel1(l) QLabel(l, MUIO_Label_SingleFrame | MUIO_Label_LeftAligned)

static FxPattern fpat;                  /* the last search, compiled */
static int fpat_ok;
static FxFormat ffmt, rfmt;             /* formatting to find and to give */
static char withu[1024];                /* Replace with (UTF-8) */
static int search_dir, search_sel;      /* Search: 0 All, 1 Down, 2 Up; Find in: 0 the document, 1 the selection */
static Pos selrange[2]; static int selrange_ok; static Pos lastmatch[2];   /* the selection searched in, and the last match selected */
static Doc *spike;                      /* the Spike's text (a document of its own) */
static int gokind;                      /* Go To's target (GT_) */

/* ------------------------------------------------------------------ */
/* small helpers */
static Pos get_pos(ULONG attr)
{
  Pos *p = NULL, z;
  get(wp_view(), attr, &p);
  if (p) return *p;
  z.para = z.off = 0;
  return z;
}
static void sel(Pos a, Pos b)
{
  Pos ab[2];
  ab[0] = a; ab[1] = b;
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_SELECT, (ULONG)ab);
}
/* the document changed behind the view's back: laid out and drawn again, the selection put at a..b */
static void changed(Pos a, Pos b)
{
  DoMethod(wp_view(), MUIM_Quill_Relayout);
  sel(a, b);
}
static Layout *layout(void)
{
  Layout *L = NULL;
  get(wp_view(), MUIA_Quill_Layout, &L);
  return L;
}
static void beep(void) { DisplayBeep(NULL); }

/* ------------------------------------------------------------------ */
/* the Find and Replace window */
static Object *fwin, *freg, *f_what, *f_with, *f_ffmt, *f_rfmt, *f_msg, *f_cb[7], *f_dir, *f_in, *f_special, *f_active;
static Object *g_list, *g_label, *g_str;
enum { CB_CASE, CB_WORD, CB_WILD, CB_PREFIX, CB_SUFFIX, CB_PUNCT, CB_SPACE };
static const char *const cb_names[7] = { "Match case", "Find whole words only", "Use wildcards", "Match prefix", "Match suffix",
                                          "Ignore punctuation characters", "Ignore white-space characters" };
/* Special: what each entry puts in */
static const char *const special_labels[] = {
  "Paragraph Mark", "Tab Character", "Any Character", "Any Digit", "Any Letter", "Caret Character", "White Space",
  "Manual Line Break", "Manual Page Break", "Column Break", "Section Break", "Em Dash", "En Dash", "Nonbreaking Space",
  "Nonbreaking Hyphen", "Optional Hyphen", "Section Character", "Paragraph Character", "Graphic", "Footnote Mark",
  "Endnote Mark", "Comment Mark", "Field", "Find What Text", "Clipboard Contents",
  "Any Character (wildcards)", "Zero or More Characters (wildcards)", "One of These Characters (wildcards)",
  "Not These Characters (wildcards)", "Character in Range (wildcards)", "Beginning of Word (wildcards)",
  "End of Word (wildcards)", "Expression (wildcards)", "One or More Occurrences (wildcards)", "Occurrences (wildcards)" };
static const char *const special_codes[] = {
  "^p", "^t", "^?", "^#", "^$", "^^", "^w", "^l", "^m", "^n", "^b", "^+", "^=", "^s", "^~", "^-", "^%", "^v", "^g", "^f",
  "^e", "^a", "^d", "^&", "^c", "?", "*", "[]", "[!]", "[a-z]", "<", ">", "()", "@", "{1,}" };
#define NSPECIAL (int)(sizeof special_codes / sizeof special_codes[0])
static const char *special_tr[NSPECIAL + 1];
static const char *const dir_names[] = { "All", "Down", "Up" };
static const char *dir_tr[4];
static const char *const in_names[] = { "Main Document", "Current Selection" };
static const char *in_tr[3];
static const char *const go_names[GT_N] = { "Page", "Section", "Line", "Bookmark", "Comment", "Footnote", "Endnote", "Field",
                                            "Table", "Graphic", "Object", "Heading" };
static const char *go_tr[GT_N + 1];
static const char *const go_prompts[GT_N] = {
  "Enter page number:", "Enter section number:", "Enter line number:", "Enter bookmark name:", "Enter reviewer's name:",
  "Enter footnote number:", "Enter endnote number:", "Enter field number:", "Enter table number:", "Enter graphic number:",
  "Enter object number:", "Enter heading number:" };

/* the formatting's description, as Word shows it under Find what ("Font: Bold, Not Italic...") */
static void describe(const FxFormat *f, char *out, size_t cap)
{
  static const struct { unsigned flag; const char *name; } fl[] = {
    { CF_BOLD, "Bold" }, { CF_ITALIC, "Italic" }, { CF_UNDERLINE, "Underline" }, { CF_STRIKE, "Strikethrough" },
    { CF_SUPER, "Superscript" }, { CF_SUB, "Subscript" }, { CF_CAPS, "All caps" } };
  static const char *const al[] = { "Left", "Centred", "Right", "Justified" };
  char b[512], t[96], l1[64];
  size_t i;
  b[0] = 0;
#define ADD(s) do { if (b[0]) strcat(b, ", "); strncat(b, s, sizeof b - strlen(b) - 1); } while (0)
  if (f->font[0]) { utf8_to_latin1(f->font, strlen(f->font), l1, sizeof l1); ADD(l1); }
  if (f->size) { sprintf(t, "%d%s pt", f->size / 2, f->size & 1 ? ".5" : ""); ADD(t); }
  for (i = 0; i < sizeof fl / sizeof fl[0]; i++) {
    if (f->set & fl[i].flag) ADD(tr(fl[i].name));
    if (f->clear & fl[i].flag) { sprintf(t, tr("Not %s"), tr(fl[i].name)); ADD(t); }
  }
  if (f->fxset & FX_SMALLCAPS) ADD(tr("Small caps"));
  if (f->fxclear & FX_SMALLCAPS) { sprintf(t, tr("Not %s"), tr("Small caps")); ADD(t); }
  if (f->hl > 0) ADD(tr("Highlight"));
  if (f->hl == 0) ADD(tr("Not Highlight"));
  if (f->color != FXC_ANY) { if (f->color == COLOR_AUTO) ADD(tr("Automatic colour")); else { sprintf(t, tr("Colour %06lX"), (unsigned long)f->color); ADD(t); } }
  if (f->align >= 0 && f->align < 4) ADD(tr(al[(int)f->align]));
  if (f->pstyle[0]) { utf8_to_latin1(f->pstyle, strlen(f->pstyle), l1, sizeof l1); sprintf(t, tr("Style: %s"), l1); ADD(t); }
  if (f->cstyle[0]) { utf8_to_latin1(f->cstyle, strlen(f->cstyle), l1, sizeof l1); sprintf(t, tr("Character style: %s"), l1); ADD(t); }
#undef ADD
  if (!b[0]) { out[0] = 0; return; }
  snprintf(out, cap, "%s %s", tr("Format:"), b);
}
static void show_formats(void)
{
  static char a[600], b[600];
  if (!fwin) return;
  describe(&ffmt, a, sizeof a); describe(&rfmt, b, sizeof b);
  set(f_ffmt, MUIA_Text_Contents, (ULONG)a);
  set(f_rfmt, MUIA_Text_Contents, (ULONG)b);
}
static void message(const char *m) { if (f_msg) set(f_msg, MUIA_Text_Contents, (ULONG)m); }
static void go_kind(int k)
{
  gokind = k < 0 || k >= GT_N ? 0 : k;
  if (g_label) set(g_label, MUIA_Text_Contents, (ULONG)tr(go_prompts[gokind]));
}
static Object *checkrow(int i)
{
  return HGroup, Child, f_cb[i] = CheckMark(FALSE), Child, LLabel1(tr(cb_names[i])), Child, HSpace(0), End;
}
static void make_window(void)
{
  static const char *pages[3];
  Object *bnext, *bprev, *brepl, *ball, *bhl, *bclr, *bclose, *bfmt, *brfmt, *bnofmt, *bins, *gprev, *gnext, *ggo, *gclose;
  int i;
  if (fwin) return;
  fx_format_none(&ffmt); fx_format_none(&rfmt);
  for (i = 0; i < NSPECIAL; i++) special_tr[i] = tr(special_labels[i]);
  for (i = 0; i < 3; i++) dir_tr[i] = tr(dir_names[i]);
  for (i = 0; i < 2; i++) in_tr[i] = tr(in_names[i]);
  for (i = 0; i < GT_N; i++) go_tr[i] = tr(go_names[i]);
  pages[0] = tr("Find and Replace"); pages[1] = tr("Go To"); pages[2] = NULL;
  fwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Find and Replace"), MUIA_Window_ID, MAKE_ID('W','1','2','F'),
    WindowContents, VGroup, QDLG,
      Child, freg = RegisterGroup(pages), MUIA_CycleChain, 1,
        /* Find and Replace */
        Child, VGroup,
          Child, ColGroup(2),
            Child, Label2(tr("Find what:")), Child, f_what = StringObject, StringFrame, MUIA_String_MaxLen, 250, MUIA_CycleChain, 1, End,
            Child, HSpace(0), Child, f_ffmt = TextObject, MUIA_Text_Contents, (ULONG)"", MUIA_Text_SetMin, FALSE, End,
            Child, Label2(tr("Replace with:")), Child, f_with = StringObject, StringFrame, MUIA_String_MaxLen, 250, MUIA_CycleChain, 1, End,
            Child, HSpace(0), Child, f_rfmt = TextObject, MUIA_Text_Contents, (ULONG)"", MUIA_Text_SetMin, FALSE, End,
          End,
          Child, HGroup,
            Child, bfmt = QButton(tr("_Format...")),
            Child, brfmt = QButton(tr("Replace F_ormat...")),
            Child, bnofmt = QButton(tr("No Formattin_g")),
          End,
          Child, HGroup,
            Child, Label1(tr("Special:")),
            Child, f_special = CycleObject, MUIA_Cycle_Entries, (ULONG)special_tr, MUIA_CycleChain, 1, End,
            Child, bins = QButton(tr("_Insert")),
          End,
          Child, QTitleObject(tr("Search Options")),
          Child, ColGroup(2),
            Child, checkrow(CB_CASE), Child, checkrow(CB_PREFIX),
            Child, checkrow(CB_WORD), Child, checkrow(CB_SUFFIX),
            Child, checkrow(CB_WILD), Child, checkrow(CB_PUNCT),
            Child, HSpace(0), Child, checkrow(CB_SPACE),
          End,
          Child, HGroup,
            Child, Label1(tr("Search:")), Child, f_dir = CycleObject, MUIA_Cycle_Entries, (ULONG)dir_tr, MUIA_CycleChain, 1, End,
            Child, Label1(tr("Find in:")), Child, f_in = CycleObject, MUIA_Cycle_Entries, (ULONG)in_tr, MUIA_CycleChain, 1, End,
          End,
          Child, f_msg = TextObject, MUIA_Text_Contents, (ULONG)"", End,
          Child, HGroup,
            Child, bnext = QButton(tr("Find _Next")),
            Child, bprev = QButton(tr("_Previous")),
            Child, brepl = QButton(tr("_Replace")),
            Child, ball = QButton(tr("Replace _All")),
          End,
          Child, HGroup,
            Child, bhl = QButton(tr("_Highlight All")),
            Child, bclr = QButton(tr("Clear Highligh_ting")),
            Child, bclose = QButton(tr("_Close")),
          End,
        End,
        /* Go To */
        Child, VGroup,
          Child, HGroup,
            Child, VGroup,
              Child, LLabel1(tr("Go to what:")),
              Child, g_list = ListviewObject, MUIA_CycleChain, 1, MUIA_Listview_List, ListObject, InputListFrame,
                MUIA_List_SourceArray, (ULONG)go_tr, MUIA_List_Active, 0, End, End,
            End,
            Child, VGroup,
              Child, g_label = TextObject, MUIA_Text_Contents, (ULONG)"", End,
              Child, g_str = StringObject, StringFrame, MUIA_String_MaxLen, 100, MUIA_CycleChain, 1, End,
              Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Enter + and - to move relative to where you are.\nFor example, +4 moves four ahead."), End,
              Child, VSpace(0),
            End,
          End,
          Child, HGroup,
            Child, gprev = QButton(tr("Pre_vious")),
            Child, gnext = QButton(tr("Ne_xt")),
            Child, ggo = QButton(tr("_Go To")),
            Child, HSpace(0),
            Child, gclose = QButton(tr("C_lose")),
          End,
        End,
      End,
    End, End;
  if (!fwin) return;
  DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)fwin);
  DoMethod(fwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)fwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  DoMethod(bclose, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)fwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  DoMethod(gclose, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)fwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
#define RET(o, a, v, id) DoMethod(o, MUIM_Notify, a, v, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, id)
  RET(f_what, MUIA_String_Acknowledge, MUIV_EveryTime, ID_FX_FINDNEXT);
  RET(bnext, MUIA_Pressed, FALSE, ID_FX_FINDNEXT);
  RET(bprev, MUIA_Pressed, FALSE, ID_FX_FINDPREV);
  RET(brepl, MUIA_Pressed, FALSE, ID_FX_REPL1);
  RET(ball, MUIA_Pressed, FALSE, ID_FX_REPLALL);
  RET(bhl, MUIA_Pressed, FALSE, ID_FX_HILITE);
  RET(bclr, MUIA_Pressed, FALSE, ID_FX_UNHILITE);
  RET(bfmt, MUIA_Pressed, FALSE, ID_FX_FORMAT);
  RET(brfmt, MUIA_Pressed, FALSE, ID_FX_RFORMAT);
  RET(bnofmt, MUIA_Pressed, FALSE, ID_FX_NOFMT);
  RET(bins, MUIA_Pressed, FALSE, ID_FX_SPECIAL);
  RET(fwin, MUIA_Window_ActiveObject, MUIV_EveryTime, ID_FX_ACTIVE);
  RET(g_list, MUIA_List_Active, MUIV_EveryTime, ID_FX_GOKIND);
  RET(g_str, MUIA_String_Acknowledge, MUIV_EveryTime, ID_FX_GOGO);
  RET(ggo, MUIA_Pressed, FALSE, ID_FX_GOGO);
  RET(gnext, MUIA_Pressed, FALSE, ID_FX_GONEXT);
  RET(gprev, MUIA_Pressed, FALSE, ID_FX_GOPREV);
#undef RET
  f_active = f_what;
  go_kind(0);
  show_formats();
}
/* the window open on a page (0 Find and Replace, 1 Go To), the keyboard in field */
static int open_window(int page, Object **field)
{
  make_window();
  if (!fwin) { beep(); return WPR_DONE; }
  set(freg, MUIA_Group_ActivePage, page);
  if (page == 0) {
    /* a short selection in one paragraph is what to find, as in Word */
    Pos a = get_pos(MUIA_Quill_Anchor), c = get_pos(MUIA_Quill_Caret);
    Doc *d = wp_doc();
    if (pos_cmp(a, c) > 0) { Pos t = a; a = c; c = t; }
    if (d && a.para == c.para && c.off > a.off && c.off - a.off < 80 && a.para < d->np) {
      char l1[256];
      size_t n = utf8_to_latin1(d->p[a.para]->t + a.off, c.off - a.off, l1, sizeof l1 - 1);
      l1[n] = 0;
      if (!strchr(l1, '\t') && !strchr(l1, '\v')) set(f_what, MUIA_String_Contents, (ULONG)l1);
    }
  }
  set(fwin, MUIA_Window_Open, TRUE);
  set(fwin, MUIA_Window_ActiveObject, (ULONG)*field);
  return WPR_KEEPFOCUS;
}
/* the window's settings read and Find what compiled (into fpat); 0 with a message if it can't be */
static int read_search(int back)
{
  static char u[1024];
  char *s = NULL, *w = NULL;
  ULONG v[7], dir = 0, in = 0;
  unsigned flags = 0;
  int i;
  if (!fwin) return fpat_ok;
  get(f_what, MUIA_String_Contents, &s);
  get(f_with, MUIA_String_Contents, &w);
  for (i = 0; i < 7; i++) { v[i] = 0; get(f_cb[i], MUIA_Selected, &v[i]); }
  get(f_dir, MUIA_Cycle_Active, &dir);
  get(f_in, MUIA_Cycle_Active, &in);
  search_dir = (int)dir; search_sel = (int)in;
  if (v[CB_CASE]) flags |= FX_CASE;
  if (v[CB_WORD]) flags |= FX_WORD;
  if (v[CB_WILD]) flags |= FX_WILD;
  if (v[CB_PREFIX]) flags |= FX_PREFIX;
  if (v[CB_SUFFIX]) flags |= FX_SUFFIX;
  if (v[CB_PUNCT]) flags |= FX_NOPUNCT;
  if (v[CB_SPACE]) flags |= FX_NOSPACE;
  if (dir == 2 || back) flags |= FX_BACK;
  if (dir) flags |= FX_NOWRAP;
  wp_latin1_utf8(s ? s : "", u, sizeof u);
  wp_latin1_utf8(w ? w : "", withu, sizeof withu);
  fpat_ok = fx_compile(&fpat, u, flags, &ffmt);
  if (!fpat_ok) message(tr(fpat.err));
  return fpat_ok;
}
/* the range searched: the selection (as it was when the search in it began), or the document (NULL) */
static const Pos *range(void)
{
  Pos a, c;
  if (!search_sel) { selrange_ok = 0; return NULL; }
  a = get_pos(MUIA_Quill_Anchor); c = get_pos(MUIA_Quill_Caret);
  if (pos_cmp(a, c) > 0) { Pos t = a; a = c; c = t; }
  if (!selrange_ok || pos_cmp(a, lastmatch[0]) || pos_cmp(c, lastmatch[1])) {
    if (!pos_cmp(a, c)) { selrange_ok = 0; return NULL; }
    selrange[0] = a; selrange[1] = c; selrange_ok = 1;
  }
  return selrange;
}
static int find_next(int back)
{
  Doc *d = wp_doc();
  Pos a = get_pos(MUIA_Quill_Anchor), c = get_pos(MUIA_Quill_Caret), from, fa, fb;
  const Pos *r;
  if (!d || !fpat_ok) return 0;
  if (back) fpat.flags |= FX_BACK; else if (search_dir != 2) fpat.flags &= ~FX_BACK;
  r = range();
  if (pos_cmp(a, c) > 0) { Pos t = a; a = c; c = t; }
  from = (fpat.flags & FX_BACK) ? a : c;
  if (r && !pos_cmp(a, r[0]) && !pos_cmp(c, r[1])) from = (fpat.flags & FX_BACK) ? r[1] : r[0];   /* (the first search in it starts at its start) */
  if (!fx_find(d, &fpat, from, r, &fa, &fb)) {
    message(tr(r ? "Quill has finished searching the selection." : (fpat.flags & FX_NOWRAP) ? "Quill has finished searching the document." : "Not found."));
    beep();
    return 0;
  }
  lastmatch[0] = fa; lastmatch[1] = fb;
  sel(fa, fb);
  message("");
  return 1;
}
/* does the selection match the search (as Replace needs)? */
static int sel_matches(void)
{
  Doc *d = wp_doc();
  Pos a = get_pos(MUIA_Quill_Anchor), c = get_pos(MUIA_Quill_Caret), e;
  if (pos_cmp(a, c) > 0) { Pos t = a; a = c; c = t; }
  if (!pos_cmp(a, c)) return 0;
  if (fpat.empty) {
    for (e = a; pos_cmp(e, c) < 0; ) { Pos f, g; Pos rr[2]; rr[0] = e; rr[1] = c; if (!fx_find(d, &fpat, e, rr, &f, &g) || pos_cmp(f, e)) return 0; e = g; }
    return 1;
  }
  return fx_match_at(d, &fpat, a, &e, NULL) && !pos_cmp(e, c);
}
static char *clip_utf8(void)
{
  long n = 0;
  char *l = clip_read(&n), *u = NULL;
  if (l && (u = (char *)malloc((size_t)n * 2 + 1))) wp_latin1_utf8(l, u, (size_t)n * 2 + 1);
  free(l);
  return u;
}
static void replace_one(void)
{
  Doc *d = wp_doc();
  Pos a = get_pos(MUIA_Quill_Anchor), c = get_pos(MUIA_Quill_Caret), e;
  if (pos_cmp(a, c) > 0) { Pos t = a; a = c; c = t; }
  if (sel_matches()) {
    char *clip = strstr(withu, "^c") ? clip_utf8() : NULL;
    if (fx_replace(d, &fpat, a, c, withu, clip, &rfmt, &e)) {
      if (selrange_ok && search_sel && a.para == selrange[1].para && selrange[1].off >= c.off) selrange[1].off = selrange[1].off - c.off + e.off;
      changed(e, e);
    }
    free(clip);
  }
  find_next(0);
}
static void replace_all(void)
{
  static char m[80];
  Doc *d = wp_doc();
  const Pos *r = range();
  char *clip = strstr(withu, "^c") ? clip_utf8() : NULL;
  Pos c = get_pos(MUIA_Quill_Caret);
  u32 n = fx_replace_all(d, &fpat, r, withu, clip, &rfmt);
  free(clip);
  sprintf(m, tr("%lu replaced."), (unsigned long)n);
  message(m);
  if (!n) beep();
  else changed(c, c);
  selrange_ok = 0;
}
static void highlight_all(void)
{
  static char m[80];
  struct FvInfo fi;
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_FV_HILITE, (ULONG)&fpat);
  memset(&fi, 0, sizeof fi);
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_FV_INFO, (ULONG)&fi);
  sprintf(m, tr("%ld found and highlighted."), (long)fi.highlights);
  message(m);
  if (!fi.highlights) beep();
}
/* Special: the code put into Find what or Replace with (whichever had the keyboard last) */
static void insert_special(void)
{
  ULONG k = 0;
  char *s = NULL, b[300];
  Object *o = f_active == f_with ? f_with : f_what;
  get(f_special, MUIA_Cycle_Active, &k);
  if ((int)k >= NSPECIAL) return;
  get(o, MUIA_String_Contents, &s);
  snprintf(b, sizeof b, "%s%s", s ? s : "", special_codes[k]);
  set(o, MUIA_String_Contents, (ULONG)b);
  set(fwin, MUIA_Window_ActiveObject, (ULONG)o);
  if (k >= 25) set(f_cb[CB_WILD], MUIA_Selected, TRUE);      /* (a wildcard needs Use wildcards) */
}

/* ------------------------------------------------------------------ */
/* the Format window (for Find what or Replace with) */
static Object *mwin, *m_font, *m_size, *m_cy[9], *m_color, *m_align, *m_pstyle, *m_cstyle;
static int m_target;                    /* 0 Find what, 1 Replace with */
static const char *const tri_names[] = { "(any)", "On", "Off" };
static const char *tri_tr[4];
static const char *const m_names[9] = { "Bold", "Italic", "Underline", "Strikethrough", "Superscript", "Subscript", "All caps", "Small caps", "Highlight" };
static const unsigned m_flags[7] = { CF_BOLD, CF_ITALIC, CF_UNDERLINE, CF_STRIKE, CF_SUPER, CF_SUB, CF_CAPS };
static const char *const col_names[] = { "(any)", "Automatic", "Black", "Dark Red", "Red", "Orange", "Yellow", "Green", "Blue", "Dark Blue", "Purple", "Grey" };
static const ULONG col_rgb[] = { FXC_ANY, COLOR_AUTO, 0x000000, 0xC00000, 0xFF0000, 0xFFC000, 0xFFFF00, 0x00B050, 0x0070C0, 0x002060, 0x7030A0, 0x808080 };
#define NCOL (int)(sizeof col_rgb / sizeof col_rgb[0])
static const char *col_tr[NCOL + 1];
static const char *const al_names[] = { "(any)", "Left", "Centred", "Right", "Justified" };
static const char *al_tr[6];
static void open_format(int target)
{
  FxFormat *f = target ? &rfmt : &ffmt;
  char l1[64], b[16];
  int i;
  m_target = target;
  if (!mwin) {
    Object *c;
    for (i = 0; i < 3; i++) tri_tr[i] = tr(tri_names[i]);
    for (i = 0; i < NCOL; i++) col_tr[i] = tr(col_names[i]);
    for (i = 0; i < 5; i++) al_tr[i] = tr(al_names[i]);
    c = VGroup,
      Child, ColGroup(4),
        Child, Label2(tr("Font:")), Child, m_font = StringObject, StringFrame, MUIA_String_MaxLen, 46, MUIA_CycleChain, 1, End,
        Child, Label2(tr("Size:")), Child, m_size = StringObject, StringFrame, MUIA_String_MaxLen, 6, MUIA_String_Accept, (ULONG)"0123456789.,", MUIA_CycleChain, 1, End,
      End,
      Child, ColGroup(6),
        Child, Label1(tr(m_names[0])), Child, m_cy[0] = CycleObject, MUIA_Cycle_Entries, (ULONG)tri_tr, MUIA_CycleChain, 1, End,
        Child, Label1(tr(m_names[1])), Child, m_cy[1] = CycleObject, MUIA_Cycle_Entries, (ULONG)tri_tr, MUIA_CycleChain, 1, End,
        Child, Label1(tr(m_names[2])), Child, m_cy[2] = CycleObject, MUIA_Cycle_Entries, (ULONG)tri_tr, MUIA_CycleChain, 1, End,
        Child, Label1(tr(m_names[3])), Child, m_cy[3] = CycleObject, MUIA_Cycle_Entries, (ULONG)tri_tr, MUIA_CycleChain, 1, End,
        Child, Label1(tr(m_names[4])), Child, m_cy[4] = CycleObject, MUIA_Cycle_Entries, (ULONG)tri_tr, MUIA_CycleChain, 1, End,
        Child, Label1(tr(m_names[5])), Child, m_cy[5] = CycleObject, MUIA_Cycle_Entries, (ULONG)tri_tr, MUIA_CycleChain, 1, End,
        Child, Label1(tr(m_names[6])), Child, m_cy[6] = CycleObject, MUIA_Cycle_Entries, (ULONG)tri_tr, MUIA_CycleChain, 1, End,
        Child, Label1(tr(m_names[7])), Child, m_cy[7] = CycleObject, MUIA_Cycle_Entries, (ULONG)tri_tr, MUIA_CycleChain, 1, End,
        Child, Label1(tr(m_names[8])), Child, m_cy[8] = CycleObject, MUIA_Cycle_Entries, (ULONG)tri_tr, MUIA_CycleChain, 1, End,
      End,
      Child, ColGroup(4),
        Child, Label1(tr("Font colour:")), Child, m_color = CycleObject, MUIA_Cycle_Entries, (ULONG)col_tr, MUIA_CycleChain, 1, End,
        Child, Label1(tr("Alignment:")), Child, m_align = CycleObject, MUIA_Cycle_Entries, (ULONG)al_tr, MUIA_CycleChain, 1, End,
        Child, Label2(tr("Style:")), Child, m_pstyle = StringObject, StringFrame, MUIA_String_MaxLen, 46, MUIA_CycleChain, 1, End,
        Child, Label2(tr("Character style:")), Child, m_cstyle = StringObject, StringFrame, MUIA_String_MaxLen, 46, MUIA_CycleChain, 1, End,
      End,
    End;
    if (!(mwin = wp_dialog("Format", MAKE_ID('W','1','2','M'), c, "_OK", ID_FX_FMTOK))) { beep(); return; }
  }
  set(mwin, MUIA_Window_Title, (ULONG)tr(target ? "Replace Format" : "Find Format"));
  utf8_to_latin1(f->font, strlen(f->font), l1, sizeof l1); set(m_font, MUIA_String_Contents, (ULONG)l1);
  if (f->size) sprintf(b, "%d%s", f->size / 2, f->size & 1 ? ".5" : ""); else b[0] = 0;
  set(m_size, MUIA_String_Contents, (ULONG)b);
  for (i = 0; i < 7; i++) set(m_cy[i], MUIA_Cycle_Active, (f->set & m_flags[i]) ? 1 : (f->clear & m_flags[i]) ? 2 : 0);
  set(m_cy[7], MUIA_Cycle_Active, (f->fxset & FX_SMALLCAPS) ? 1 : (f->fxclear & FX_SMALLCAPS) ? 2 : 0);
  set(m_cy[8], MUIA_Cycle_Active, f->hl > 0 ? 1 : f->hl == 0 ? 2 : 0);
  for (i = 0; i < NCOL && col_rgb[i] != f->color; i++) ;
  set(m_color, MUIA_Cycle_Active, i < NCOL ? i : 0);
  set(m_align, MUIA_Cycle_Active, f->align + 1);
  utf8_to_latin1(f->pstyle, strlen(f->pstyle), l1, sizeof l1); set(m_pstyle, MUIA_String_Contents, (ULONG)l1);
  utf8_to_latin1(f->cstyle, strlen(f->cstyle), l1, sizeof l1); set(m_cstyle, MUIA_String_Contents, (ULONG)l1);
  set(mwin, MUIA_Window_Open, TRUE);
  set(mwin, MUIA_Window_ActiveObject, (ULONG)m_font);
}
static void format_done(void)
{
  FxFormat *f = m_target ? &rfmt : &ffmt;
  char *s = NULL;
  ULONG v = 0;
  int i;
  fx_format_none(f);
  get(m_font, MUIA_String_Contents, &s); if (s) wp_latin1_utf8(s, f->font, sizeof f->font);
  s = NULL; get(m_size, MUIA_String_Contents, &s);
  if (s && *s) {
    long whole = atol(s);
    const char *dot = strpbrk(s, ".,");
    f->size = (u16)(whole * 2 + (dot && dot[1] >= '5' ? 1 : 0));
  }
  for (i = 0; i < 7; i++) { v = 0; get(m_cy[i], MUIA_Cycle_Active, &v); if (v == 1) f->set |= m_flags[i]; else if (v == 2) f->clear |= m_flags[i]; }
  v = 0; get(m_cy[7], MUIA_Cycle_Active, &v); if (v == 1) f->fxset |= FX_SMALLCAPS; else if (v == 2) f->fxclear |= FX_SMALLCAPS;
  v = 0; get(m_cy[8], MUIA_Cycle_Active, &v); f->hl = v == 1 ? 1 : v == 2 ? 0 : -1;
  v = 0; get(m_color, MUIA_Cycle_Active, &v); f->color = (int)v < NCOL ? col_rgb[v] : FXC_ANY;
  v = 0; get(m_align, MUIA_Cycle_Active, &v); f->align = (signed char)((int)v - 1);
  s = NULL; get(m_pstyle, MUIA_String_Contents, &s); if (s) wp_latin1_utf8(s, f->pstyle, sizeof f->pstyle);
  s = NULL; get(m_cstyle, MUIA_String_Contents, &s); if (s) wp_latin1_utf8(s, f->cstyle, sizeof f->cstyle);
  set(mwin, MUIA_Window_Open, FALSE);
  show_formats();
}

/* ------------------------------------------------------------------ */
/* Go To */
static void go(int dir)
{
  Doc *d = wp_doc();
  char *s = NULL, u[256];
  Pos a, b;
  LONG k = 0;
  if (!d) return;
  if (g_list) { get(g_list, MUIA_List_Active, &k); go_kind((int)k); }
  if (g_str) get(g_str, MUIA_String_Contents, &s);
  wp_latin1_utf8(s ? s : "", u, sizeof u);
  if (!fx_goto(d, (gokind == GT_PAGE || gokind == GT_LINE) ? layout() : NULL, gokind, u, dir, get_pos(MUIA_Quill_Caret), &a, &b)) { beep(); return; }
  sel(a, b);
}

/* ------------------------------------------------------------------ */
/* Go Back: the last places edited (the undo history's), four at most, in turn */
static int goback_i;
static u32 goback_top;
static void go_back(void)
{
  Doc *d = wp_doc();
  Pos at[4], c = get_pos(MUIA_Quill_Caret), af;
  int n = 0, i, k;
  u32 top = 0;
  if (!d) return;
  for (i = 0; i < doc_undo_count(d, 0) && n < 4; i++) {
    doc_undo_step(d, 0, i, NULL, &af, i ? NULL : &top);
    af = pos_clamp(d, af);
    for (k = 0; k < n && !(at[k].para == af.para && (at[k].off > af.off ? at[k].off - af.off : af.off - at[k].off) < 8); k++) ;
    if (k == n) at[n++] = af;
  }
  if (!n) { beep(); return; }
  if (top != goback_top) { goback_top = top; goback_i = 0; }
  k = goback_i % n;
  if (!pos_cmp(at[k], c) && n > 1) k = (k + 1) % n;
  goback_i = k + 1;
  sel(at[k], at[k]);
}

/* ------------------------------------------------------------------ */
/* Repeat (F4): the last command that changed the document again, or the text typed since */
static struct { ULONG id; LONG v; u32 serial0; Doc *doc; int ok; } pend, rep;
static char *rep_text; static u32 rep_text_serial; static Doc *rep_text_doc;   /* text Repeat typed last, and the step it made */
static u32 top_serial(Doc *d) { u32 s = 0; if (d && doc_undo_count(d, 0)) doc_undo_step(d, 0, 0, NULL, NULL, &s); return s; }
static int never_repeat(ULONG id)
{
  return id == ID_UNDO || id == ID_REDO || id == ID_FX_REPEAT || id == ID_FX_UNDOLIST || id == ID_FX_REDOLIST || id == ID_FX_ULOK ||
         id == ID_FX_GOBACK || id == ID_SAVE || id == ID_SAVEAS || id == ID_OPEN || id == ID_NEW || id == ID_CLOSE;
}
/* the pending command finished: it is the one to repeat if it made an undo step */
static void settle(void)
{
  if (pend.ok && pend.doc == wp_doc() && top_serial(pend.doc) > pend.serial0) { rep = pend; }
  pend.ok = 0;
}
/* (main.c: each command and ribbon choice as it starts) */
void wp12_note(ULONG id, LONG v)
{
  settle();
  if (never_repeat(id) || !wp_doc()) return;
  pend.id = id; pend.v = v; pend.doc = wp_doc(); pend.serial0 = top_serial(pend.doc); pend.ok = 1;
}
void wp12_replay(ULONG id, LONG v);       /* (main.c) */
static void repeat(void)
{
  Doc *d = wp_doc();
  int i, n;
  u32 since = 0, s;
  settle();
  if (!d) return;
  if (rep.ok && rep.doc == d) {
    /* the step the command made: the oldest one newer than it started */
    n = doc_undo_count(d, 0);
    since = 0;
    for (i = 0; i < n; i++) { doc_undo_step(d, 0, i, NULL, NULL, &s); if (s > rep.serial0) since = s; else break; }
  }
  if (rep_text && rep_text_doc == d && top_serial(d) == rep_text_serial) {
    /* F4 again after repeating typing: the same text again */
    DoMethod(wp_view(), MUIM_Quill_Command, CMD_TEXT, (ULONG)rep_text);
    rep_text_serial = top_serial(d);
    return;
  }
  if (doc_undo_count(d, 0) && doc_undo_step(d, 0, 0, NULL, NULL, &s) == UK_TYPING && (!rep.ok || rep.doc != d || s > since)) {
    char *t = doc_undo_typed(d, rep.ok && rep.doc == d ? since : 0);
    if (t) {
      DoMethod(wp_view(), MUIM_Quill_Command, CMD_TEXT, (ULONG)t);
      free(rep_text); rep_text = t; rep_text_doc = d; rep_text_serial = top_serial(d);
      return;
    }
  }
  if (rep.ok && rep.doc == d) { wp12_replay(rep.id, rep.v); settle(); return; }
  beep();
}

/* ------------------------------------------------------------------ */
/* Spike */
static void spike_cut(void)
{
  Doc *d = wp_doc();
  Pos a = get_pos(MUIA_Quill_Anchor), c = get_pos(MUIA_Quill_Caret), e;
  Fragment *f;
  if (pos_cmp(a, c) > 0) { Pos t = a; a = c; c = t; }
  if (!d || !pos_cmp(a, c)) { beep(); return; }
  if (!spike && !(spike = doc_new())) { beep(); return; }
  if (!(f = doc_fragment(d, a, c))) { beep(); return; }
  e = doc_end_pos(spike);
  if (e.para || e.off) doc_insert(spike, &e, "\n", 1, NULL);     /* each piece on paragraphs of its own */
  doc_paste(spike, &e, f, PASTE_KEEP);
  doc_clear_undo(spike);
  fragment_free(f);
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_DELETE, 0);
}
static void spike_insert(void)
{
  Fragment *f;
  if (!spike || (spike->np == 1 && !spike->p[0]->len) || !(f = doc_fragment(spike, doc_start(spike), doc_end_pos(spike)))) {
    wp_ask("OK", "%s", tr("The Spike is empty."));
    return;
  }
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_FV_PASTEFRAG, (ULONG)f);
  fragment_free(f);
  doc_free(spike); spike = NULL;                                  /* (Insert Spike empties it, as in Word) */
}

/* ------------------------------------------------------------------ */
/* Toggle Unicode Character */
static void toggle_unicode(void)
{
  Doc *d = wp_doc();
  Pos c;
  if (!d || !fx_toggle_unicode(d, get_pos(MUIA_Quill_Anchor), get_pos(MUIA_Quill_Caret), &c)) { beep(); return; }
  changed(c, c);
}

/* ------------------------------------------------------------------ */
/* the Undo and Redo lists: choosing a step undoes (redoes) it and every one after it */
static Object *uwin, *u_list;
static int u_redo;
static char u_text[40][48];
static const char *u_rows[41];
static const char *const kind_names[UK_N] = { "", "Edit", "Typing", "Insert", "Delete", "Replace", "Formatting", "Paragraph Formatting",
                                             "Style", "Replace All" };
static void open_undolist(int redo)
{
  Doc *d = wp_doc();
  int n, i;
  if (!d) return;
  n = doc_undo_count(d, redo);
  if (!n) { beep(); return; }
  if (n > 40) n = 40;
  u_redo = redo;
  if (!uwin) {
    Object *c = VGroup,
      Child, u_list = ListviewObject, MUIA_CycleChain, 1, MUIA_Listview_MultiSelect, MUIV_Listview_MultiSelect_None,
        MUIA_Listview_List, ListObject, InputListFrame, End, End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Choose a step: it and the ones above it are taken back."), End,
    End;
    if (!(uwin = wp_dialog("Undo", MAKE_ID('W','1','2','U'), c, "_OK", ID_FX_ULOK))) { beep(); return; }
    DoMethod(u_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_FX_ULOK);
  }
  for (i = 0; i < n; i++) {
    int k = doc_undo_step(d, redo, i, NULL, NULL, NULL);
    snprintf(u_text[i], sizeof u_text[i], "%d. %s", i + 1, tr(kind_names[k > 0 && k < UK_N ? k : UK_EDIT]));
    u_rows[i] = u_text[i];
  }
  u_rows[n] = NULL;
  set(uwin, MUIA_Window_Title, (ULONG)tr(redo ? "Redo" : "Undo"));
  DoMethod(u_list, MUIM_List_Clear);
  DoMethod(u_list, MUIM_List_Insert, (ULONG)u_rows, n, MUIV_List_Insert_Bottom);
  set(u_list, MUIA_List_Active, 0);
  set(uwin, MUIA_Window_Open, TRUE);
  set(uwin, MUIA_Window_ActiveObject, (ULONG)u_list);
}
static void undolist_done(void)
{
  LONG k = 0, i;
  get(u_list, MUIA_List_Active, &k);
  set(uwin, MUIA_Window_Open, FALSE);
  if (k < 0) return;
  for (i = 0; i <= k; i++) DoMethod(wp_view(), MUIM_Quill_Command, u_redo ? CMD_REDO : CMD_UNDO, 0);
}

/* ------------------------------------------------------------------ */
/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp12_command(ULONG id, LONG value)
{
  switch (id) {
  case ID_FX_ADVFIND: return open_window(0, &f_what);
  case ID_FX_REPLACE: make_window(); return fwin ? open_window(0, &f_with) : WPR_DONE;
  case ID_FX_GOTO: make_window(); return fwin ? open_window(1, &g_str) : WPR_DONE;
  case ID_FX_FINDNEXT: case ID_FX_FINDPREV:
    if (read_search(id == ID_FX_FINDPREV)) find_next(id == ID_FX_FINDPREV);
    return WPR_KEEPFOCUS;
  case ID_FX_REPEATFIND:                                    /* Shift-F4: the last Find or Go To again */
    if (fpat_ok) find_next(0);
    else if (fwin) go(1);
    else return open_window(0, &f_what);
    return WPR_DONE;
  case ID_FX_REPL1: if (read_search(0)) replace_one(); return WPR_KEEPFOCUS;
  case ID_FX_REPLALL: if (read_search(0)) replace_all(); return WPR_KEEPFOCUS;
  case ID_FX_HILITE: if (read_search(0)) highlight_all(); return WPR_KEEPFOCUS;
  case ID_FX_UNHILITE: DoMethod(wp_view(), MUIM_Quill_Command, CMD_FV_HILITE, 0); message(""); return WPR_KEEPFOCUS;
  case ID_FX_FORMAT: case ID_FX_RFORMAT: open_format(id == ID_FX_RFORMAT); return WPR_KEEPFOCUS;
  case ID_FX_FMTOK: format_done(); return WPR_KEEPFOCUS;
  case ID_FX_NOFMT:
    if (f_active == f_with) fx_format_none(&rfmt); else fx_format_none(&ffmt);
    show_formats();
    return WPR_KEEPFOCUS;
  case ID_FX_SPECIAL: insert_special(); return WPR_KEEPFOCUS;
  case ID_FX_ACTIVE: {
    Object *o = NULL;
    get(fwin, MUIA_Window_ActiveObject, &o);
    if (o == f_what || o == f_with) f_active = o;
    return WPR_KEEPFOCUS; }
  case ID_FX_GOKIND: { LONG k = 0; get(g_list, MUIA_List_Active, &k); go_kind((int)k); return WPR_KEEPFOCUS; }
  case ID_FX_GOGO: go(0); return WPR_KEEPFOCUS;
  case ID_FX_GONEXT: go(1); return WPR_KEEPFOCUS;
  case ID_FX_GOPREV: go(-1); return WPR_KEEPFOCUS;
  case ID_FX_GOBACK: go_back(); break;
  case ID_FX_REPEAT: repeat(); break;
  case ID_FX_SELOBJ: DoMethod(wp_view(), MUIM_Quill_Command, CMD_FV_OBJECTS, 0); break;
  case ID_FX_SIMILAR: DoMethod(wp_view(), MUIM_Quill_Command, CMD_FV_SIMILAR, 0); break;
  case ID_FX_EXTEND: DoMethod(wp_view(), MUIM_Quill_Command, CMD_FV_EXTEND, 1); break;
  case ID_FX_SHRINK: DoMethod(wp_view(), MUIM_Quill_Command, CMD_FV_EXTEND, -1); break;
  case ID_FX_COLUMN: DoMethod(wp_view(), MUIM_Quill_Command, CMD_FV_COLUMN, 0); break;
  case ID_FX_SPIKE: spike_cut(); break;
  case ID_FX_INSSPIKE: spike_insert(); break;
  case ID_FX_UNICODE: toggle_unicode(); break;
  case ID_FX_UNDOLIST: case ID_FX_REDOLIST: open_undolist(id == ID_FX_REDOLIST); return uwin ? WPR_KEEPFOCUS : WPR_DONE;
  case ID_FX_ULOK: undolist_done(); break;
  }
  return WPR_DONE;
}

/* keys (raw codes): F4 Repeat, Shift-F4 repeat Find; F5 Go To, Shift-F5 Go Back (Ctrl-Alt-Z too); F8
   Extend Selection, Shift-F8 shrink it, Ctrl-Shift-F8 column selection; Ctrl-F3 Spike, Ctrl-Shift-F3
   Insert Spike; Ctrl-X Toggle Unicode Character */
static ULONG key(UWORD code, UWORD qual, const WpCtx *c)
{
  int sh = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0, ct = (qual & IEQUALIFIER_CONTROL) != 0;
  int al = (qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) != 0;
  switch (code) {
  case 0x53: return al || ct ? 0 : sh ? ID_FX_REPEATFIND : ID_FX_REPEAT;
  case 0x54: return al || ct ? 0 : sh ? ID_FX_GOBACK : ID_FX_GOTO;
  case 0x57: return al ? 0 : ct ? (sh ? ID_FX_COLUMN : 0) : sh ? ID_FX_SHRINK : ID_FX_EXTEND;
  case 0x52: return ct && !al ? (sh ? ID_FX_INSSPIKE : ID_FX_SPIKE) : 0;
  case 0x32: return ct && !al && !sh ? ID_FX_UNICODE : 0;
  case 0x31: return ct && al && !sh ? ID_FX_GOBACK : 0;
  }
  return 0;
}

static void cleanup(void)
{
  free(rep_text); rep_text = NULL;
  if (spike) { doc_free(spike); spike = NULL; }
}

/* the Edit menu's Find and Select submenu */
static const WpMenu menus[] = {
  { WM_EDIT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_EDIT, { NM_ITEM, (STRPTR)"Find and Select", 0, 0, 0, 0 } },
  { WM_EDIT, { NM_SUB, (STRPTR)"Advanced Find...", 0, 0, 0, (APTR)ID_FX_ADVFIND } },
  { WM_EDIT, { NM_SUB, (STRPTR)"Replace...", 0, 0, 0, (APTR)ID_FX_REPLACE } },
  { WM_EDIT, { NM_SUB, (STRPTR)"Go To... (F5)", 0, 0, 0, (APTR)ID_FX_GOTO } },
  { WM_EDIT, { NM_SUB, (STRPTR)"Go Back (Shift-F5)", 0, 0, 0, (APTR)ID_FX_GOBACK } },
  { WM_EDIT, { NM_SUB, (STRPTR)"Highlight All", 0, 0, 0, (APTR)ID_FX_HILITE } },
  { WM_EDIT, { NM_SUB, (STRPTR)"Clear Highlighting", 0, 0, 0, (APTR)ID_FX_UNHILITE } },
  { WM_EDIT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_EDIT, { NM_SUB, (STRPTR)"Select Objects", 0, 0, 0, (APTR)ID_FX_SELOBJ } },
  { WM_EDIT, { NM_SUB, (STRPTR)"Select Similar Formatting", 0, 0, 0, (APTR)ID_FX_SIMILAR } },
  { WM_EDIT, { NM_SUB, (STRPTR)"Extend Selection (F8)", 0, 0, 0, (APTR)ID_FX_EXTEND } },
  { WM_EDIT, { NM_SUB, (STRPTR)"Column Selection (Ctrl-Shift-F8)", 0, 0, 0, (APTR)ID_FX_COLUMN } },
  { WM_EDIT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_EDIT, { NM_SUB, (STRPTR)"Repeat (F4)", 0, 0, 0, (APTR)ID_FX_REPEAT } },
  { WM_EDIT, { NM_SUB, (STRPTR)"Undo List...", 0, 0, 0, (APTR)ID_FX_UNDOLIST } },
  { WM_EDIT, { NM_SUB, (STRPTR)"Redo List...", 0, 0, 0, (APTR)ID_FX_REDOLIST } },
  { WM_EDIT, { NM_SUB, (STRPTR)"Spike (Ctrl-F3)", 0, 0, 0, (APTR)ID_FX_SPIKE } },
  { WM_EDIT, { NM_SUB, (STRPTR)"Insert Spike (Ctrl-Shift-F3)", 0, 0, 0, (APTR)ID_FX_INSSPIKE } },
  { WM_EDIT, { NM_SUB, (STRPTR)"Toggle Unicode Character (Ctrl-X)", 0, 0, 0, (APTR)ID_FX_UNICODE } },
};

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp12_package = {
  "WP12", ID_WP12_FIRST, ID_WP12_LAST,
  wp12_command, NULL, NULL, NULL, key, NULL, cleanup,
  NULL, 0,
  menus, (int)(sizeof menus / sizeof menus[0])
};
