/*
 * wp_notes.c - WP22b Footnote and endnote options (docs/wordcraft-gap.md). This file belongs to WP22b: only that
 * package edits it. Its commands are ID_WP22B_FIRST .. ID_WP22B_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * The Footnote and Endnote window (Word's): where footnotes go (the page's foot or under its text)
 * and endnotes (the document's end or each section's), the numbers' format, the first number,
 * numbering continuous or starting again each section or page, for the whole document or the
 * caret's section (core/notes.c). Show Notes: from the text to the next note and back.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libraries/iffparse.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "lang.h"
#include "../core/notes.h"

#undef Label1
#undef Label2
#define Label1(l)  QLabel(l, MUIO_Label_SingleFrame)
#define Label2(l)  QLabel(l, MUIO_Label_DoubleFrame)

enum { ID_NOTES_OPTIONS = ID_WP22B_FIRST, ID_NOTES_SHOW, ID_NOTES_APPLY, ID_NOTES_INSERT, ID_NOTES_KIND };

static Object *nwin, *n_fpos, *n_epos, *n_kind, *n_fmt, *n_start, *n_restart, *n_scope;
static const u8 fmts[6] = { NNF_DECIMAL, NNF_LOWER_LETTER, NNF_UPPER_LETTER, NNF_LOWER_ROMAN, NNF_UPPER_ROMAN, NNF_SYMBOL };

static ULONG cyc(Object *o) { ULONG v = 0; get(o, MUIA_Cycle_Active, &v); return v; }
/* the body paragraph the caret is in (a note or header being edited: put away first) */
static u32 caret_para(void)
{
  Pos *c;
  wp_story_close();
  c = wp_caret();
  return c ? c->para : 0;
}
/* the fields from the options in force at the caret for the kind chosen */
static void fill(void)
{
  Doc *d = wp_doc();
  NotePr fp, ep, np;
  int end = (int)cyc(n_kind), k;
  char b[12];
  const SectFmt *s;
  if (!d || !d->np) return;
  s = doc_sect(d, caret_para());
  notes_eff(d, s, 0, &fp); notes_eff(d, s, 1, &ep);
  np = end ? ep : fp;
  nnset(n_fpos, MUIA_Cycle_Active, fp.pos == NPOS_BENEATHTEXT);
  nnset(n_epos, MUIA_Cycle_Active, ep.pos == NPOS_SECTEND);
  for (k = 0; k < 6 && fmts[k] != np.fmt; k++) ;
  nnset(n_fmt, MUIA_Cycle_Active, k < 6 ? k : 0);
  sprintf(b, "%u", (unsigned)np.start);
  set(n_start, MUIA_String_Contents, (ULONG)b);
  nnset(n_restart, MUIA_Cycle_Active, np.restart);
}
static void open_options(void)
{
  if (!nwin) {
    static const char *fpos[3], *epos[3], *kinds[3], *fmtl[7], *rst[4], *scope[3];
    Object *c, *ins, *ap, *cancel;
    fpos[0] = tr("Bottom of page"); fpos[1] = tr("Below text"); fpos[2] = NULL;
    epos[0] = tr("End of document"); epos[1] = tr("End of section"); epos[2] = NULL;
    kinds[0] = tr("Footnotes"); kinds[1] = tr("Endnotes"); kinds[2] = NULL;
    fmtl[0] = "1, 2, 3, ..."; fmtl[1] = "a, b, c, ..."; fmtl[2] = "A, B, C, ..."; fmtl[3] = "i, ii, iii, ..."; fmtl[4] = "I, II, III, ...";
    fmtl[5] = tr("Symbols (*, ...)"); fmtl[6] = NULL;
    rst[0] = tr("Continuous"); rst[1] = tr("Restart each section"); rst[2] = tr("Restart each page"); rst[3] = NULL;
    scope[0] = tr("Whole document"); scope[1] = tr("This section"); scope[2] = NULL;
    c = VGroup, QDLG,
      Child, QTitleObject(tr("Location")),
      Child, ColGroup(2),
        Child, Label1(tr("Footnotes")), Child, n_fpos = CycleObject, MUIA_Cycle_Entries, (ULONG)fpos, MUIA_CycleChain, 1, End,
        Child, Label1(tr("Endnotes")), Child, n_epos = CycleObject, MUIA_Cycle_Entries, (ULONG)epos, MUIA_CycleChain, 1, End,
      End,
      Child, QTitleObject(tr("Format")),
      Child, ColGroup(2),
        Child, Label1(tr("Change")), Child, n_kind = CycleObject, MUIA_Cycle_Entries, (ULONG)kinds, MUIA_CycleChain, 1, End,
        Child, Label1(tr("Number format")), Child, n_fmt = CycleObject, MUIA_Cycle_Entries, (ULONG)fmtl, MUIA_CycleChain, 1, End,
        Child, Label2(tr("Start at")), Child, n_start = StringObject, StringFrame, MUIA_String_MaxLen, 6, MUIA_String_Accept, (ULONG)"0123456789",
          MUIA_CycleChain, 1, End,
        Child, Label1(tr("Numbering")), Child, n_restart = CycleObject, MUIA_Cycle_Entries, (ULONG)rst, MUIA_CycleChain, 1, End,
      End,
      Child, QTitleObject(tr("Apply changes")),
      Child, ColGroup(2),
        Child, Label1(tr("Apply changes to")), Child, n_scope = CycleObject, MUIA_Cycle_Entries, (ULONG)scope, MUIA_CycleChain, 1, End,
      End,
      Child, HGroup, Child, ins = QButton(tr("_Insert")), Child, ap = QButton(tr("_Apply")), Child, HSpace(0), Child, cancel = QButton(tr("_Cancel")), End,
    End;
    if (!c) return;
    nwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Footnote and Endnote"), MUIA_Window_ID, MAKE_ID('W','2','2','B'), WindowContents, c, End;
    if (!nwin) return;
    DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)nwin);
    DoMethod(nwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)nwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(cancel, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)nwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(ins, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_NOTES_INSERT);
    DoMethod(ap, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_NOTES_APPLY);
    DoMethod(n_kind, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_NOTES_KIND);
  }
  /* (the caret in an endnote: endnotes) */
  { Doc *d = wp_doc(); nnset(n_kind, MUIA_Cycle_Active, d && d->edit.kind == SK_ENOTE); }
  fill();
  set(nwin, MUIA_Window_Open, TRUE);
}
/* the window's settings given to the document (1), or 0 if there is none */
static int apply(void)
{
  Doc *d = wp_doc();
  NotePr np, pos;
  STRPTR s = NULL;
  u32 para;
  int end;
  long start;
  if (!d || !nwin) return 0;
  para = caret_para();
  end = (int)cyc(n_kind);
  /* the positions are the document's (both, as shown) */
  memset(&pos, 0, sizeof pos);
  pos.set = NPS_POS; pos.pos = (u8)(cyc(n_fpos) ? NPOS_BENEATHTEXT : NPOS_PAGEBOTTOM);
  if (!((d->fnpr.set & NPS_POS) && d->fnpr.pos == pos.pos) && (cyc(n_fpos) || (d->fnpr.set & NPS_POS))) notes_apply(d, para, 0, 1, &pos);
  pos.pos = (u8)(cyc(n_epos) ? NPOS_SECTEND : NPOS_DOCEND);
  if (!((d->enpr.set & NPS_POS) && d->enpr.pos == pos.pos) && (cyc(n_epos) || (d->enpr.set & NPS_POS))) notes_apply(d, para, 1, 1, &pos);
  memset(&np, 0, sizeof np);
  np.set = NPS_FMT | NPS_START | NPS_RESTART;
  np.fmt = fmts[cyc(n_fmt) % 6];
  get(n_start, MUIA_String_Contents, &s);
  start = s ? atol((const char *)s) : 1;
  np.start = (u16)(start < 1 ? 1 : start > 32000 ? 32000 : start);
  np.restart = (u8)cyc(n_restart);
  if (end && np.restart == NRS_PAGE) np.restart = NRS_CONT;     /* (endnotes don't start again each page) */
  notes_apply(d, para, end, cyc(n_scope) == 0, &np);
  set(wp_view(), MUIA_Quill_Changed, TRUE);
  wp_refresh();
  return 1;
}
/* the caret put at para, off (a function of its own: vbcc -O2 miscompiled the Pos pair built inline) */
static void caret_to(u32 para, u32 off)
{
  static Pos ab[2];
  ab[0].para = para; ab[0].off = off; ab[1].para = para; ab[1].off = off;
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_SELECT, (ULONG)ab);
}
/* Show Notes: from a note being edited back to its reference; else to the next note after the caret
   (footnotes or endnotes, asked when the document has both), opened for editing */
static void show_notes(void)
{
  Doc *d = wp_doc();
  Pos *c;
  int kind = 0, end, have[2] = { 0, 0 }, k;
  u32 i, j, from;
  if (!d) return;
  if (d->edit.kind == SK_NOTE || d->edit.kind == SK_ENOTE) {
    end = d->edit.kind == SK_ENOTE; k = d->edit.index;
    wp_story_close();
    for (i = 0; i < d->np; i++)
      for (j = 0; j + 4 <= d->p[i]->len; j++)
        if ((end ? IS_ENOTE(d->p[i]->t + j) : IS_NOTE(d->p[i]->t + j)) && note_index(d->p[i]->t + j) == k) {
          caret_to(i, j + 4);
          return;
        }
    return;
  }
  wp_story_close();
  for (i = 0; i < d->np; i++)
    for (j = 0; j + 4 <= d->p[i]->len; j++) { if (IS_NOTE(d->p[i]->t + j)) have[0] = 1; else if (IS_ENOTE(d->p[i]->t + j)) have[1] = 1; }
  if (!have[0] && !have[1]) { wp_ask("OK", "%s", tr("The document has no footnotes or endnotes.")); return; }
  if (have[0] && have[1]) {
    switch (wp_ask("_Footnotes|_Endnotes|_Cancel", "%s", tr("The document has footnotes and endnotes.\nWhich do you want to see?"))) {
    case 1: kind = 0; break;
    case 2: kind = 1; break;
    default: return;
    }
  } else kind = have[1];
  c = wp_caret();
  from = c ? c->para : 0;
  /* the first reference after the caret, round to the start */
  for (k = 0; k < 2; k++)
    for (i = k ? 0 : from; i < d->np; i++)
      for (j = (!k && c && i == c->para) ? c->off : 0; j + 4 <= d->p[i]->len; j++)
        if (kind ? IS_ENOTE(d->p[i]->t + j) : IS_NOTE(d->p[i]->t + j)) {
          caret_to(i, j + 4);
          DoMethod(wp_app(), MUIM_Application_ReturnID, kind ? ID_ENDNOTE : ID_NOTE);      /* (main.c opens the note just before the caret) */
          return;
        }
}

int wp22b_command(ULONG id, LONG value)
{
  (void)value;
  switch (id) {
  case ID_NOTES_OPTIONS: open_options(); return WPR_KEEPFOCUS;
  case ID_NOTES_KIND: fill(); return WPR_KEEPFOCUS;
  case ID_NOTES_APPLY: apply(); set(nwin, MUIA_Window_Open, FALSE); break;
  case ID_NOTES_INSERT:
    if (apply()) { set(nwin, MUIA_Window_Open, FALSE); DoMethod(wp_app(), MUIM_Application_ReturnID, cyc(n_kind) ? ID_ENDNOTE : ID_NOTE); }
    return WPR_KEEPFOCUS;
  case ID_NOTES_SHOW: show_notes(); return WPR_KEEPFOCUS;
  }
  return WPR_DONE;
}

/* References > Notes: Show Notes and the window; the Insert menu */
static const Item note_items[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Show Notes", ID_NOTES_SHOW, 0, 0, 0, 0, "Show Notes: to the footnotes or endnotes, and back to the text", NULL,
    "ci R 3 1 13 15 L 5 4 11 4 L 5 6 11 6 ca L 5 10 8 10 L 5 12 11 12 L 5 14 10 14", 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Footnote Options", ID_NOTES_OPTIONS, 0, 0, 0, 0, "Footnote and Endnote: where notes go, how they are numbered", NULL,
    "ci R 1 1 11 15 L 3 4 9 4 L 3 6 9 6 ca L 3 11 6 11 L 3 13 8 13 cd O 12 11 3 L 12 7 12 15 L 8 11 15 11", 0 },
};
static const WpGroup groups22b[] = { { TAB_REFERENCES, -1, { "Notes", G_COLS, note_items, 2 } } };
static const WpMenu menus22b[] = {
  { WM_INSERT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Footnote and Endnote...", 0, 0, 0, (APTR)ID_NOTES_OPTIONS } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Show Notes", 0, 0, 0, (APTR)ID_NOTES_SHOW } },
};
const WpPackage wp22b_package = {
  "WP22b", ID_WP22B_FIRST, ID_WP22B_LAST,
  wp22b_command, NULL, NULL, NULL, NULL, NULL, NULL,
  groups22b, 1,
  menus22b, 3
};
