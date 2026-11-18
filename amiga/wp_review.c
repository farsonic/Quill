/*
 * wp_review.c - WP5 Review: comments and markup (docs/wordcraft-gap.md). This file belongs to WP5: only that
 * package edits it. Its commands are ID_WP5_FIRST .. ID_WP5_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * Replies to comments and Resolve, the Comments pane (threads), Display for Review (Simple Markup, All
 * Markup, No Markup, Original), Show Markup (comments, insertions and deletions, formatting, balloons,
 * reviewers), the Reviewing pane, Track Changes Options, Accept/Reject All Changes Shown. The model
 * and the .docx side are core/review.c; the balloons and marks are drawn by view.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "lang.h"
#include "../core/review.h"

#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif

enum {
  ID_RV_REPLY = ID_WP5_FIRST, ID_RV_REPLYGO, ID_RV_RESOLVE, ID_RV_CPANE, ID_RV_CPICK, ID_RV_CREPLY, ID_RV_CRESOLVE, ID_RV_CDELETE,
  ID_RV_CREFRESH, ID_RV_DELALL,
  ID_RV_MARKUP, ID_RV_MSIMPLE, ID_RV_MALL, ID_RV_MNONE, ID_RV_MORIG,
  ID_RV_SHOWMARKUP, ID_RV_SHCOMMENTS, ID_RV_SHINSDEL, ID_RV_SHFORMAT, ID_RV_BLALL, ID_RV_BLFORMAT, ID_RV_BLINLINE, ID_RV_PEOPLE, ID_RV_PEOPLEGO,
  ID_RV_RPANE, ID_RV_RPICK, ID_RV_RACCEPT, ID_RV_RREJECT, ID_RV_RREFRESH,
  ID_RV_OPTIONS, ID_RV_OPTGO,
  ID_RV_ACCEPTSHOWN, ID_RV_REJECTSHOWN, ID_RV_ACCEPT1, ID_RV_REJECT1
};
/* the state bits (RB_WP(WPI_5, n)) */
enum { ST_RESOLVED = 1, ST_INCOMMENT = 2 };

/* ---- icons, in theme.c's icon language */
#define IC_REPLY   "ci R 1 2 14 11 L 4 11 4 14 L 4 14 7 11 ca P 3 7 4 4 7 7 10 L 5 7 11 7 L 11 7 11 9"
#define IC_RESOLVE "ci R 1 2 14 11 L 4 11 4 14 L 4 14 7 11 cg P 3 4 6 7 9 12 4"
#define IC_CPANE   "ci R 1 1 14 14 L 5 1 5 14 ca L 7 4 12 4 L 7 7 12 7 L 9 10 12 10"
#define IC_MARKUP  "ci R 3 1 12 15 cr L 5 5 10 5 ci L 5 8 10 8 cr L 5 11 10 11"
#define IC_SHOW    "ci P 5 1 8 4 5 8 4 12 5 15 8 P 5 1 8 4 11 8 12 12 11 15 8 ca E 8 8 2"
#define IC_RPANE   "ci R 1 1 14 14 cr L 3 4 12 4 cg L 3 7 12 7 ci L 3 10 12 10"
#define IC_OPTIONS "ci O 8 8 5 ca E 8 8 2 ci L 8 1 8 3 L 8 13 8 15 L 1 8 3 8 L 13 8 15 8"

/* Display for Review, in Word's order (its entries' RVM_) */
static const char *const markups[] = { "Simple Markup", "All Markup", "No Markup", "Original" };
static const int markup_mode[4] = { RVM_SIMPLE, RVM_ALL, RVM_NONE, RVM_ORIGINAL };
static const RibbonList l_markup = { markups, 4, PL_TEXT, NULL };
/* Show Markup: its entries, ticked (×) while on; made afresh as the document changes */
#define NSHOW 7
static const char *const shownames[NSHOW] = { "Comments", "Insertions and Deletions", "Formatting", "Show Revisions in Balloons",
                                              "Show Only Comments and Formatting in Balloons", "Show All Revisions Inline", "Specific People..." };
static char showlab[NSHOW][96];
static const char *showp[NSHOW];
static const RibbonList l_show = { showp, NSHOW, PL_TEXT, NULL };

static const Item i_comm[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Reply", ID_RV_REPLY, 0, RB_WP(WPI_5, ST_INCOMMENT), 0, 0, "Reply to the comment at the caret", NULL, IC_REPLY, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Resolve", ID_RV_RESOLVE, RB_WP(WPI_5, ST_RESOLVED), RB_WP(WPI_5, ST_INCOMMENT), 0, 0, "Mark the comment's thread resolved, or open it again", NULL, IC_RESOLVE, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Comments Pane", ID_RV_CPANE, 0, 0, 0, 0, "Every comment and its replies in a pane", NULL, IC_CPANE, 0 } };
static const Item i_track[] = {
  { K_FIELD, S_SMALL, 0, 0, NULL, ID_RV_MARKUP, 0, 0, 0, 92, "Display for Review: how changes and comments are shown", &l_markup, IC_MARKUP, 0 },
  { K_DROP, S_SMALL, 0, 0, "Show Markup", ID_RV_SHOWMARKUP, 0, 0, 0, 0, "Which kinds of markup and whose are shown, and balloons", &l_show, IC_SHOW, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Reviewing Pane", ID_RV_RPANE, 0, 0, 0, 0, "Every change and comment in a list", NULL, IC_RPANE, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Options", ID_RV_OPTIONS, 0, 0, 0, 0, "Track Changes Options: marks, colours, changed lines, moves", NULL, IC_OPTIONS, 0 } };
static const WpGroup groups[] = {
  { TAB_REVIEW, 2, { "Comments", G_COLS, i_comm, 3 } },
  { TAB_REVIEW, 3, { "Tracking", G_COLS, i_track, 4 } } };
static const WpMenu menus[] = {
  { WM_REVIEW, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_REVIEW, { NM_ITEM, (STRPTR)"Comments", 0, 0, 0, 0 } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"Reply...", 0, 0, 0, (APTR)ID_RV_REPLY } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"Resolve", 0, 0, 0, (APTR)ID_RV_RESOLVE } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"Comments Pane", 0, 0, 0, (APTR)ID_RV_CPANE } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"Delete All Comments", 0, 0, 0, (APTR)ID_RV_DELALL } },
  { WM_REVIEW, { NM_ITEM, (STRPTR)"Display for Review", 0, 0, 0, 0 } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"Simple Markup", 0, 0, 0, (APTR)ID_RV_MSIMPLE } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"All Markup", 0, 0, 0, (APTR)ID_RV_MALL } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"No Markup", 0, 0, 0, (APTR)ID_RV_MNONE } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"Original", 0, 0, 0, (APTR)ID_RV_MORIG } },
  { WM_REVIEW, { NM_ITEM, (STRPTR)"Show Markup", 0, 0, 0, 0 } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"Comments", 0, 0, 0, (APTR)ID_RV_SHCOMMENTS } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"Insertions and Deletions", 0, 0, 0, (APTR)ID_RV_SHINSDEL } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"Formatting", 0, 0, 0, (APTR)ID_RV_SHFORMAT } },
  { WM_REVIEW, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"Show Revisions in Balloons", 0, 0, 0, (APTR)ID_RV_BLALL } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"Show Only Comments and Formatting in Balloons", 0, 0, 0, (APTR)ID_RV_BLFORMAT } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"Show All Revisions Inline", 0, 0, 0, (APTR)ID_RV_BLINLINE } },
  { WM_REVIEW, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"Specific People...", 0, 0, 0, (APTR)ID_RV_PEOPLE } },
  { WM_REVIEW, { NM_ITEM, (STRPTR)"Reviewing Pane", 0, 0, 0, (APTR)ID_RV_RPANE } },
  { WM_REVIEW, { NM_ITEM, (STRPTR)"Track Changes Options...", 0, 0, 0, (APTR)ID_RV_OPTIONS } },
  { WM_REVIEW, { NM_ITEM, (STRPTR)"Accept All Changes Shown", 0, 0, 0, (APTR)ID_RV_ACCEPTSHOWN } },
  { WM_REVIEW, { NM_ITEM, (STRPTR)"Reject All Changes Shown", 0, 0, 0, (APTR)ID_RV_REJECTSHOWN } } };

/* ------------------------------------------------------------------ */
/* small helpers */
static const char *trs(const char *s) { return tr(s); }
/* the user's name (File > Options, PROGDIR:Quill.prefs "name=") as UTF-8, "Quill" if none; and initials */
static void who(char *au, size_t cap, char *ini)   /* (ini: 8 bytes) */
{
  char line[200], name[100] = "";
  BPTR f = Open((STRPTR)"PROGDIR:Quill.prefs", MODE_OLDFILE);
  int k = 0;
  const char *n;
  if (f) {
    while (FGets(f, (STRPTR)line, sizeof line)) {
      line[strcspn(line, "\r\n")] = 0;
      if (!strncmp(line, "name=", 5)) { strncpy(name, line + 5, sizeof name - 1); name[sizeof name - 1] = 0; }
    }
    Close(f);
  }
  if (!name[0]) strcpy(name, "Quill");
  wp_latin1_utf8(name, au, cap);
  for (n = name; *n && k < 4; n++) if (*n != ' ' && (n == name || n[-1] == ' ')) ini[k++] = *n;
  ini[k] = 0;
  { const char *own = wp_pref("initials"); if (own && *own) { strncpy(ini, own, 7); ini[7] = 0; } }   /* (Options > General) */
}
/* the date and time now, as .docx has them: 2026-10-24T09:30:00Z */
static void now_iso(char *out)
{
  static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  struct DateStamp ds;
  LONG days, y = 1978, m;
  DateStamp(&ds);
  days = ds.ds_Days;
  for (;;) { LONG yl = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365; if (days < yl) break; days -= yl; y++; }
  for (m = 0; m < 12; m++) {
    LONG ml = mdays[m] + (m == 1 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)));
    if (days < ml) break;
    days -= ml;
  }
  sprintf(out, "%04ld-%02ld-%02ldT%02ld:%02ld:00Z", (long)y, (long)m + 1, (long)days + 1, (long)(ds.ds_Minute / 60), (long)(ds.ds_Minute % 60));
}
/* the view's selection */
static void selection(Pos *a, Pos *b)
{
  Pos *c = NULL, *an = NULL;
  get(wp_view(), MUIA_Quill_Caret, &c); get(wp_view(), MUIA_Quill_Anchor, &an);
  *a = c ? *c : doc_start(wp_doc()); *b = an ? *an : *a;
  if (pos_cmp(*a, *b) > 0) { Pos t = *a; *a = *b; *b = t; }
}
/* the comment (thread's first) at the caret, or -1 */
static int comment_here(void)
{
  Doc *d = wp_doc();
  Pos a, b;
  int k;
  if (!d) return -1;
  selection(&a, &b);
  k = doc_comment_at(d, a, NULL, NULL);
  if (k < 0 && pos_cmp(a, b) && a.off + 4 <= d->p[a.para]->len) { a.off += 4; k = doc_comment_at(d, a, NULL, NULL); }
  return k < 0 ? -1 : rv_thread_root(d, k);
}
/* the document changed outside the view: laid out and drawn afresh, a..b selected */
static void changed(Pos a, Pos b)
{
  Pos s[2];
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_DESIGN, 0);
  s[0] = a; s[1] = b;
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_SELECT, (ULONG)s);
}
/* how changes are shown changed: laid out afresh (the text is the same) */
static void shown(void) { DoMethod(wp_view(), MUIM_Quill_Command, CMD_PAGESETUP, 0); }
static void cpane_fill(void), rpane_fill(void);
static void panes_fill(void) { cpane_fill(); rpane_fill(); }

/* ------------------------------------------------------------------ */
/* Reply */
static Object *rpwin, *rpstr;
static int rp_to = -1;
static void open_reply(int k)
{
  Doc *d = wp_doc();
  char t[160], au[80];
  if (!d || k < 0 || k >= d->ncomments) { DisplayBeep(NULL); return; }
  if (!rpwin) {
    Object *c = HGroup, Child, Label2(tr("Reply")), Child, rpstr = StringObject, StringFrame, MUIA_String_MaxLen, 500, MUIA_CycleChain, 1, End, End;
    if (!(rpwin = wp_dialog("Reply", MAKE_ID('W', '5', 'R', 'P'), c, "_Reply", ID_RV_REPLYGO))) return;
    DoMethod(rpstr, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_RV_REPLYGO);
  }
  rp_to = k;
  utf8_to_latin1(d->comments[k].author, (u32)strlen(d->comments[k].author), au, sizeof au);
  snprintf(t, sizeof t, tr("Reply to %s"), au[0] ? au : "?");
  {
    static char title[160];
    strcpy(title, t);
    set(rpwin, MUIA_Window_Title, (ULONG)title);
  }
  set(rpstr, MUIA_String_Contents, (ULONG)"");
  set(rpwin, MUIA_Window_Open, TRUE);
  set(rpwin, MUIA_Window_ActiveObject, (ULONG)rpstr);
}
static void reply_done(void)
{
  Doc *d = wp_doc();
  char *t = NULL, u[1600], au[200], ini[8], date[24];
  int n;
  Pos s, e;
  get(rpstr, MUIA_String_Contents, &t);
  set(rpwin, MUIA_Window_Open, FALSE);
  if (!d || !t || !*t || rp_to < 0 || rp_to >= d->ncomments) return;
  wp_story_close();
  wp_latin1_utf8(t, u, sizeof u);
  who(au, sizeof au, ini);
  now_iso(date);
  if ((n = rv_reply(d, rp_to, u, au, ini, date)) < 0) { DisplayBeep(NULL); return; }
  if (!doc_comment_mark(d, n, 0, &s)) s = doc_start(d);
  if (!doc_comment_mark(d, n, 1, &e)) e = s;
  s.off += 4;
  changed(s, pos_cmp(e, s) > 0 ? e : s);
  set(wp_win(), MUIA_Window_Activate, TRUE);
  panes_fill();
}

/* ------------------------------------------------------------------ */
/* the Comments pane: each thread, its replies under it */
#define CP_MAX 300
static Object *cpwin, *cplist;
static int cpk[CP_MAX], cpn;
static ULONG cp_sig;
/* what the panes show depends on: the document and its version */
static ULONG doc_sig(Doc *d) { return d ? (ULONG)d->serial * 7919UL + d->ver * 31UL + (ULONG)d->ncomments + 1 : 1; }
static void cpane_fill(void)
{
  Doc *d = wp_doc();
  int k, j, reps[64], nr;
  char line[320], au[80], txt[220];
  if (!cpwin || !cplist || (cp_sig == doc_sig(d) && cpn)) return;
  cp_sig = doc_sig(d);
  set(cplist, MUIA_List_Quiet, TRUE);
  DoMethod(cplist, MUIM_List_Clear);
  cpn = 0;
  for (k = 0; d && k < d->ncomments && cpn < CP_MAX; k++) {
    char *s;
    if (d->comments[k].parent || !rv_comment_live(d, k)) continue;
    nr = rv_replies(d, k, reps, 64);
    for (j = -1; j < nr && cpn < CP_MAX; j++) {
      int c = j < 0 ? k : reps[j];
      utf8_to_latin1(d->comments[c].author, (u32)strlen(d->comments[c].author), au, sizeof au);
      s = story_text(&d->comments[c].text);
      utf8_to_latin1(s ? s : "", s ? (u32)strlen(s) : 0, txt, sizeof txt);
      free(s);
      snprintf(line, sizeof line, "%s%s%s: %s", j < 0 ? "" : "      ", au, j < 0 && d->comments[k].done ? tr(" (Resolved)") : "", txt);
      cpk[cpn++] = c;
      DoMethod(cplist, MUIM_List_InsertSingle, (ULONG)line, MUIV_List_Insert_Bottom);
    }
  }
  if (!cpn) DoMethod(cplist, MUIM_List_InsertSingle, (ULONG)tr("No comments"), MUIV_List_Insert_Bottom);
  set(cplist, MUIA_List_Quiet, FALSE);
}
static void open_cpane(void)
{
  if (!cpwin) {
    Object *rep, *res, *del, *close;
    cpwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Comments"), MUIA_Window_ID, MAKE_ID('W', '5', 'C', 'P'),
      MUIA_Window_Width, MUIV_Window_Width_Visible(45), MUIA_Window_Height, MUIV_Window_Height_Visible(50),
      WindowContents, VGroup, QDLG,
        Child, cplist = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
          MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
        Child, HGroup, Child, rep = QButton(tr("_Reply...")), Child, res = QButton(tr("Re_solve")), Child, del = QButton(tr("_Delete")), End,
        Child, close = QButton(tr("_Close")),
      End, End;
    if (!cpwin) return;
    DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)cpwin);
    DoMethod(cpwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)cpwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(close, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)cpwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(cplist, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_RV_CPICK);
    DoMethod(rep, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_RV_CREPLY);
    DoMethod(res, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_RV_CRESOLVE);
    DoMethod(del, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_RV_CDELETE);
    DoMethod(cpwin, MUIM_Notify, MUIA_Window_Activate, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_RV_CREFRESH);
  }
  cp_sig = 0;
  cpane_fill();
  set(cpwin, MUIA_Window_Open, TRUE);
}
/* the comment of the pane's chosen line, or -1 */
static int cpane_sel(void)
{
  LONG a = -1;
  if (!cpwin || !cplist) return -1;
  get(cplist, MUIA_List_Active, &a);
  return a >= 0 && a < cpn ? cpk[a] : -1;
}
/* a comment's range selected in the document */
static void show_comment(int k)
{
  Doc *d = wp_doc();
  Pos s[2];
  if (!d || k < 0 || !doc_comment_mark(d, k, 0, &s[0])) return;
  if (!doc_comment_mark(d, k, 1, &s[1])) s[1] = s[0];
  s[0].off += 4;
  if (pos_cmp(s[1], s[0]) < 0) s[1] = s[0];
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_SELECT, (ULONG)s);
}
static void resolve(int k)
{
  Doc *d = wp_doc();
  Pos a, b;
  if (!d || k < 0) { DisplayBeep(NULL); return; }
  rv_set_done(d, k, !d->comments[rv_thread_root(d, k)].done);
  selection(&a, &b);
  changed(a, b);
  panes_fill();
}
static void delete_thread(int k)
{
  Doc *d = wp_doc();
  Pos a, b;
  if (!d || k < 0) { DisplayBeep(NULL); return; }
  wp_story_close();
  selection(&a, &b);
  if (!rv_delete_thread(d, k)) { DisplayBeep(NULL); return; }
  changed(a, a);
  panes_fill();
}

/* ------------------------------------------------------------------ */
/* the Reviewing pane: every change and comment, with a summary */
#define RP_MAX 400
static Object *rvwin, *rvlist, *rvsum;
static RvChange *rvch;
static int rvn;
static ULONG rv_sig_shown;
static void rpane_fill(void)
{
  Doc *d = wp_doc();
  int i, n, cnt[9];
  char line[400], au[80], txt[300], sum[300];
  static char sumshown[300];
  if (!rvwin || !rvlist || (rv_sig_shown == doc_sig(d) && rvn)) return;
  rv_sig_shown = doc_sig(d);
  if (!rvch && !(rvch = (RvChange *)malloc(RP_MAX * sizeof(RvChange)))) return;
  n = d ? rv_changes(d, rvch, RP_MAX) : 0;
  rvn = n < RP_MAX ? n : RP_MAX;
  memset(cnt, 0, sizeof cnt);
  set(rvlist, MUIA_List_Quiet, TRUE);
  DoMethod(rvlist, MUIM_List_Clear);
  for (i = 0; i < rvn; i++) {
    const char *a = rv_change_author(d, &rvch[i]);
    char u[600];
    if (rvch[i].kind < 9) cnt[rvch[i].kind]++;
    utf8_to_latin1(a, (u32)strlen(a), au, sizeof au);
    rv_change_text(d, &rvch[i], u, sizeof u, trs);
    utf8_to_latin1(u, (u32)strlen(u), txt, sizeof txt);
    snprintf(line, sizeof line, "%s: %s", au[0] ? au : "?", txt);
    DoMethod(rvlist, MUIM_List_InsertSingle, (ULONG)line, MUIV_List_Insert_Bottom);
  }
  if (!rvn) DoMethod(rvlist, MUIM_List_InsertSingle, (ULONG)tr("No changes or comments"), MUIV_List_Insert_Bottom);
  set(rvlist, MUIA_List_Quiet, FALSE);
  snprintf(sum, sizeof sum, tr("%d revisions: %d insertions, %d deletions, %d moves, %d formatting, %d comments"),
           n - cnt[RVC_COMMENT], cnt[RVC_INS], cnt[RVC_DEL], cnt[RVC_MOVEFROM] + cnt[RVC_MOVETO], cnt[RVC_FORMAT] + cnt[RVC_PARA] + cnt[RVC_TABLE] + cnt[RVC_SECT],
           cnt[RVC_COMMENT]);
  strcpy(sumshown, sum);
  set(rvsum, MUIA_Text_Contents, (ULONG)sumshown);
}
static void open_rpane(void)
{
  if (!rvwin) {
    Object *acc, *rej, *close;
    rvwin = QWindowObject, MUIA_Window_Title, (ULONG)tr("Revisions"), MUIA_Window_ID, MAKE_ID('W', '5', 'R', 'V'),
      MUIA_Window_Width, MUIV_Window_Width_Visible(50), MUIA_Window_Height, MUIV_Window_Height_Visible(50),
      WindowContents, VGroup, QDLG,
        Child, rvsum = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", End,
        Child, rvlist = ListviewObject, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
          MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, MUIA_CycleChain, 1, End,
        Child, HGroup, Child, acc = QButton(tr("_Accept")), Child, rej = QButton(tr("Re_ject")), Child, HSpace(0), Child, close = QButton(tr("_Close")), End,
      End, End;
    if (!rvwin) return;
    DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)rvwin);
    rv_sig_shown = 0;
    DoMethod(rvwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)rvwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(close, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)rvwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(rvlist, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_RV_RPICK);
    DoMethod(acc, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_RV_RACCEPT);
    DoMethod(rej, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_RV_RREJECT);
    DoMethod(rvwin, MUIM_Notify, MUIA_Window_Activate, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_RV_RREFRESH);
  }
  rv_sig_shown = 0;
  rpane_fill();
  set(rvwin, MUIA_Window_Open, TRUE);
}
static int rpane_sel(void)
{
  LONG a = -1;
  if (!rvwin || !rvlist) return -1;
  get(rvlist, MUIA_List_Active, &a);
  return a >= 0 && a < rvn ? (int)a : -1;
}
static void rpane_pick(void)
{
  int i = rpane_sel();
  Pos s[2];
  if (i < 0) return;
  if (rvch[i].kind == RVC_COMMENT) { show_comment(rvch[i].k); return; }
  s[0] = rvch[i].a; s[1] = rvch[i].b;
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_SELECT, (ULONG)s);
}
/* the chosen line's change accepted (else rejected); a comment is resolved (or deleted) */
static void rpane_resolve(int accept)
{
  Doc *d = wp_doc();
  int i = rpane_sel();
  RvChange c;
  if (!d || i < 0) { DisplayBeep(NULL); return; }
  c = rvch[i];
  wp_story_close();
  if (c.kind == RVC_COMMENT) { if (accept) { rv_set_done(d, c.k, 1); changed(c.a, c.a); } else delete_thread(c.k); panes_fill(); return; }
  if ((c.kind == RVC_TABLE || c.kind == RVC_SECT) && !accept) { wp_ask("OK", "%s", tr("Quill keeps this change as it was read: it can be accepted, not rejected.")); return; }
  if (!rv_resolve(d, c.a, c.b, accept, 0)) { DisplayBeep(NULL); return; }
  changed(c.a, c.a);
  panes_fill();
}

/* ------------------------------------------------------------------ */
/* Specific People: whose markup is shown */
#define PP_MAX 12
static Object *ppwin, *ppall, *ppchk[PP_MAX], *pplab[PP_MAX], *pprow[PP_MAX];
static char ppname[PP_MAX][64];
static int ppn;
static void open_people(void)
{
  Doc *d = wp_doc();
  const char *r[PP_MAX];
  int i, n;
  if (!d) return;
  if (!ppwin) {
    Object *g = VGroup, Child, HGroup, Child, ppall = CheckMark(TRUE), Child, QLabel(tr("All Reviewers"), 0), Child, HSpace(0), End, End;
    if (!g) return;
    for (i = 0; i < PP_MAX; i++) {
      pprow[i] = HGroup, Child, ppchk[i] = CheckMark(TRUE), Child, pplab[i] = TextObject, MUIA_Text_Contents, (ULONG)"", End, End;
      if (pprow[i]) DoMethod(g, OM_ADDMEMBER, (ULONG)pprow[i]);
    }
    if (!(ppwin = wp_dialog("Reviewers", MAKE_ID('W', '5', 'P', 'P'), g, "_OK", ID_RV_PEOPLEGO))) return;
  }
  n = rv_reviewers(d, r, PP_MAX);
  ppn = n < PP_MAX ? n : PP_MAX;
  for (i = 0; i < PP_MAX; i++) {
    if (i < ppn) {
      static char lab[PP_MAX][80];
      strncpy(ppname[i], r[i], 63); ppname[i][63] = 0;
      utf8_to_latin1(r[i][0] ? r[i] : "?", (u32)strlen(r[i][0] ? r[i] : "?"), lab[i], sizeof lab[i]);
      set(pplab[i], MUIA_Text_Contents, (ULONG)lab[i]);
      set(ppchk[i], MUIA_Selected, rv_person_shown(d, r[i]));
    }
    set(pprow[i], MUIA_ShowMe, i < ppn);
  }
  set(ppall, MUIA_Selected, !d->rview || !d->rview->npeople);
  set(ppwin, MUIA_Window_Open, TRUE);
}
static void people_done(void)
{
  Doc *d = wp_doc();
  LONG all = 0, on = 0;
  int i;
  set(ppwin, MUIA_Window_Open, FALSE);
  if (!d) return;
  get(ppall, MUIA_Selected, &all);
  rv_show_person(d, NULL, 1);
  if (!all) for (i = 0; i < ppn; i++) { get(ppchk[i], MUIA_Selected, &on); if (!on) rv_show_person(d, ppname[i], 0); }
  shown();
}

/* ------------------------------------------------------------------ */
/* Track Changes Options */
static const char *const marks[] = { "(none)", "Color only", "Bold", "Italic", "Underline", "Double underline", "Strikethrough", "Double strikethrough", "Hidden", NULL };
static const char *const colors[] = { "By author", "Red", "Dark Red", "Blue", "Green", "Violet", "Teal", "Gray", NULL };
static const u32 colorv[] = { COLOR_AUTO, 0xC00000, 0x800000, 0x1F4FC8, 0x008040, 0x8A2BB0, 0x007A87, 0x808080 };
static const char *const lines[] = { "(none)", "Left border", "Right border", "Outside border", NULL };
static const char *const balloons[] = { "Always", "Only for comments/formatting", "Never", NULL };
static const int balloonv[3] = { RVB_ALL, RVB_FORMAT, RVB_INLINE };
static Object *opwin, *opins, *opinsc, *opdel, *opdelc, *opfmt, *opfmtc, *oplines, *oplinec, *opcom, *opmoves, *opmfrom, *opmto, *opmovc, *optrfmt, *opbal;
static const char *trl[5][12];          /* the cycles' entries translated (made once) */
static const char **tr_list(int which, const char *const *src)
{
  int i;
  for (i = 0; src[i] && i < 11; i++) trl[which][i] = tr(src[i]);
  trl[which][i] = NULL;
  return trl[which];
}
static int color_index(u32 c) { int i; for (i = 0; i < 8; i++) if (colorv[i] == c) return i; return 0; }
static Object *cyc(const char **e) { return CycleObject, MUIA_Cycle_Entries, (ULONG)e, MUIA_CycleChain, 1, End; }
static void opts_load(void)
{
  BPTR f = Open((STRPTR)"PROGDIR:Quill.review", MODE_OLDFILE);
  char buf[800];
  LONG n;
  if (!f) return;
  n = Read(f, buf, sizeof buf - 1);
  Close(f);
  if (n > 0) { buf[n] = 0; rv_opts_load(&rv_opts, buf); }
}
static void opts_save(void)
{
  Buf b;
  BPTR f;
  buf_init(&b);
  rv_opts_save(&rv_opts, &b);
  if (!b.oom && (f = Open((STRPTR)"PROGDIR:Quill.review", MODE_NEWFILE))) { Write(f, b.data, (LONG)b.len); Close(f); }
  buf_free(&b);
}
static void open_options(void)
{
  Doc *d = wp_doc();
  int i;
  if (!opwin) {
    const char **mk = tr_list(0, marks), **co = tr_list(1, colors), **li = tr_list(2, lines), **ba = tr_list(3, balloons);
    Object *c = VGroup,
      Child, QTitleObject(tr("Markup")), Child, ColGroup(4),
        Child, Label1(tr("Insertions")), Child, opins = cyc(mk), Child, Label1(tr("Color")), Child, opinsc = cyc(co),
        Child, Label1(tr("Deletions")), Child, opdel = cyc(mk), Child, Label1(tr("Color")), Child, opdelc = cyc(co),
        Child, Label1(tr("Changed lines")), Child, oplines = cyc(li), Child, Label1(tr("Color")), Child, oplinec = cyc(co),
        Child, Label1(tr("Comments")), Child, HSpace(0), Child, Label1(tr("Color")), Child, opcom = cyc(co),
      End,
      Child, QTitleObject(tr("Moves")), Child, ColGroup(4),
        Child, Label1(tr("Track moves")), Child, HGroup, Child, opmoves = CheckMark(TRUE), Child, HSpace(0), End, Child, Label1(tr("Color")), Child, opmovc = cyc(co),
        Child, Label1(tr("Moved from")), Child, opmfrom = cyc(mk), Child, Label1(tr("Moved to")), Child, opmto = cyc(mk),
      End,
      Child, QTitleObject(tr("Formatting")), Child, ColGroup(4),
        Child, Label1(tr("Track formatting")), Child, HGroup, Child, optrfmt = CheckMark(TRUE), Child, HSpace(0), End,
        Child, Label1(tr("Formatting")), Child, opfmt = cyc(mk),
        Child, HSpace(0), Child, HSpace(0), Child, Label1(tr("Color")), Child, opfmtc = cyc(co),
      End,
      Child, QTitleObject(tr("Balloons")), Child, HGroup, Child, Label1(tr("Use balloons")), Child, opbal = cyc(ba), End,
    End;
    if (!(opwin = wp_dialog("Track Changes Options", MAKE_ID('W', '5', 'O', 'P'), c, "_OK", ID_RV_OPTGO))) return;
  }
  set(opins, MUIA_Cycle_Active, rv_opts.insmark); set(opinsc, MUIA_Cycle_Active, color_index(rv_opts.inscolor));
  set(opdel, MUIA_Cycle_Active, rv_opts.delmark); set(opdelc, MUIA_Cycle_Active, color_index(rv_opts.delcolor));
  set(opfmt, MUIA_Cycle_Active, rv_opts.fmtmark); set(opfmtc, MUIA_Cycle_Active, color_index(rv_opts.fmtcolor));
  set(oplines, MUIA_Cycle_Active, rv_opts.lines); set(oplinec, MUIA_Cycle_Active, color_index(rv_opts.linecolor));
  set(opcom, MUIA_Cycle_Active, color_index(rv_opts.commentcolor));
  set(opmfrom, MUIA_Cycle_Active, rv_opts.movefrom); set(opmto, MUIA_Cycle_Active, rv_opts.moveto); set(opmovc, MUIA_Cycle_Active, color_index(rv_opts.movecolor));
  set(opmoves, MUIA_Selected, !(d && (d->rvsettings & RVS_NOMOVES)));
  set(optrfmt, MUIA_Selected, !(d && (d->rvsettings & RVS_NOFMT)));
  for (i = 0; i < 3 && balloonv[i] != (d ? rv_balloons(d) : RVB_FORMAT); i++) ;
  set(opbal, MUIA_Cycle_Active, i < 3 ? i : 1);
  set(opwin, MUIA_Window_Open, TRUE);
}
static LONG cv(Object *o) { LONG v = 0; get(o, MUIA_Cycle_Active, &v); return v; }
static void options_done(void)
{
  Doc *d = wp_doc();
  LONG on = 0, b;
  set(opwin, MUIA_Window_Open, FALSE);
  rv_opts.insmark = (u8)cv(opins); rv_opts.inscolor = colorv[cv(opinsc) & 7];
  rv_opts.delmark = (u8)cv(opdel); rv_opts.delcolor = colorv[cv(opdelc) & 7];
  rv_opts.fmtmark = (u8)cv(opfmt); rv_opts.fmtcolor = colorv[cv(opfmtc) & 7];
  rv_opts.lines = (u8)cv(oplines); rv_opts.linecolor = cv(oplinec) ? colorv[cv(oplinec) & 7] : 0x808080;
  rv_opts.commentcolor = colorv[cv(opcom) & 7];
  rv_opts.movefrom = (u8)cv(opmfrom); rv_opts.moveto = (u8)cv(opmto); rv_opts.movecolor = cv(opmovc) ? colorv[cv(opmovc) & 7] : 0x008000;
  {
    /* (rv_opts_load bumps the layout's key: options like Hidden change what is laid out) */
    Buf t;
    buf_init(&t);
    rv_opts_save(&rv_opts, &t);
    if (!t.oom) rv_opts_load(&rv_opts, t.data);
    buf_free(&t);
  }
  opts_save();
  if (d) {
    get(opmoves, MUIA_Selected, &on);
    d->rvsettings = (u8)(on ? d->rvsettings & ~RVS_NOMOVES : d->rvsettings | RVS_NOMOVES);
    get(optrfmt, MUIA_Selected, &on);
    d->rvsettings = (u8)(on ? d->rvsettings & ~RVS_NOFMT : d->rvsettings | RVS_NOFMT);
    b = cv(opbal);
    if (b >= 0 && b < 3) rv_set_balloons(d, balloonv[b]);
  }
  shown();
}

/* ------------------------------------------------------------------ */
/* Show Markup's entries, ticked as they are on */
static void show_labels(void)
{
  Doc *d = wp_doc();
  int hide = d ? rv_hidden_kinds(d) : 0, bl = d ? rv_balloons(d) : RVB_FORMAT, i;
  int on[NSHOW];
  on[0] = !(hide & RVH_COMMENTS); on[1] = !(hide & RVH_INSDEL); on[2] = !(hide & RVH_FORMAT);
  on[3] = bl == RVB_ALL; on[4] = bl == RVB_FORMAT; on[5] = bl == RVB_INLINE;
  on[6] = d && d->rview && d->rview->npeople;
  for (i = 0; i < NSHOW; i++) {
    snprintf(showlab[i], sizeof showlab[i], "%s %s", on[i] ? "\xD7" : "  ", tr(shownames[i]));
    showp[i] = showlab[i];
  }
}
static void set_mode(int m)
{
  Doc *d = wp_doc();
  if (!d || rv_mode(d) == m) return;
  rv_set_mode(d, m);
  show_labels();
  shown();
}
static void toggle_hide(int bit)
{
  Doc *d = wp_doc();
  if (!d) return;
  rv_set_hide(d, rv_hidden_kinds(d) ^ bit);
  show_labels();
  shown();
}
static void set_balloons(int b)
{
  Doc *d = wp_doc();
  if (!d) return;
  rv_set_balloons(d, b);
  show_labels();
  shown();
}
static void resolve_shown(int accept)
{
  Doc *d = wp_doc();
  Pos a, b;
  if (!d) return;
  wp_story_close();
  selection(&a, &b);
  if (!rv_resolve(d, doc_start(d), doc_end_pos(d), accept, 1)) { DisplayBeep(NULL); return; }
  changed(a, a);
  panes_fill();
}

/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp5_command(ULONG id, LONG value)
{
  switch (id) {
  case ID_RV_REPLY: open_reply(comment_here()); return WPR_KEEPFOCUS;
  case ID_RV_REPLYGO: reply_done(); break;
  case ID_RV_RESOLVE: resolve(comment_here()); break;
  case ID_RV_CPANE: open_cpane(); return WPR_KEEPFOCUS;
  case ID_RV_CPICK: show_comment(cpane_sel()); break;
  case ID_RV_CREPLY: open_reply(cpane_sel() >= 0 ? rv_thread_root(wp_doc(), cpane_sel()) : comment_here()); return WPR_KEEPFOCUS;
  case ID_RV_CRESOLVE: resolve(cpane_sel() >= 0 ? cpane_sel() : comment_here()); break;
  case ID_RV_CDELETE: delete_thread(cpane_sel() >= 0 ? cpane_sel() : comment_here()); break;
  case ID_RV_CREFRESH: cpane_fill(); break;
  case ID_RV_DELALL:
    if (wp_doc() && wp_doc()->ncomments && wp_ask("_Delete|_Cancel", "%s", tr("Delete every comment in the document?")) == 1) {
      Pos a, b;
      wp_story_close();
      selection(&a, &b);
      if (rv_delete_all_comments(wp_doc())) changed(a, a); else DisplayBeep(NULL);
      panes_fill();
    }
    break;
  case ID_RV_MARKUP: if (value >= 0 && value < 4) set_mode(markup_mode[value]); break;
  case ID_RV_MSIMPLE: set_mode(RVM_SIMPLE); break;
  case ID_RV_MALL: set_mode(RVM_ALL); break;
  case ID_RV_MNONE: set_mode(RVM_NONE); break;
  case ID_RV_MORIG: set_mode(RVM_ORIGINAL); break;
  case ID_RV_SHOWMARKUP:
    switch (value) {
    case 0: toggle_hide(RVH_COMMENTS); break;
    case 1: toggle_hide(RVH_INSDEL); break;
    case 2: toggle_hide(RVH_FORMAT); break;
    case 3: set_balloons(RVB_ALL); break;
    case 4: set_balloons(RVB_FORMAT); break;
    case 5: set_balloons(RVB_INLINE); break;
    case 6: open_people(); return WPR_KEEPFOCUS;
    }
    break;
  case ID_RV_SHCOMMENTS: toggle_hide(RVH_COMMENTS); break;
  case ID_RV_SHINSDEL: toggle_hide(RVH_INSDEL); break;
  case ID_RV_SHFORMAT: toggle_hide(RVH_FORMAT); break;
  case ID_RV_BLALL: set_balloons(RVB_ALL); break;
  case ID_RV_BLFORMAT: set_balloons(RVB_FORMAT); break;
  case ID_RV_BLINLINE: set_balloons(RVB_INLINE); break;
  case ID_RV_PEOPLE: open_people(); return WPR_KEEPFOCUS;
  case ID_RV_PEOPLEGO: people_done(); break;
  case ID_RV_RPANE: open_rpane(); return WPR_KEEPFOCUS;
  case ID_RV_RPICK: rpane_pick(); break;
  case ID_RV_RACCEPT: rpane_resolve(1); break;
  case ID_RV_RREJECT: rpane_resolve(0); break;
  case ID_RV_RREFRESH: rpane_fill(); break;
  case ID_RV_OPTIONS: open_options(); return WPR_KEEPFOCUS;
  case ID_RV_OPTGO: options_done(); break;
  case ID_RV_ACCEPTSHOWN: resolve_shown(1); break;
  case ID_RV_REJECTSHOWN: resolve_shown(0); break;
  case ID_RV_ACCEPT1: case ID_RV_REJECT1:
    DoMethod(wp_view(), MUIM_Quill_Command, CMD_RESOLVE, (ULONG)(id == ID_RV_ACCEPT1));
    panes_fill();
    break;
  }
  return WPR_DONE;
}
/* the state bits: the caret in a comment, its thread resolved; Show Markup's ticks kept up */
static void wp5_state(ULONG *on, const WpCtx *c)
{
  int k = -1;
  if (c->doc && c->doc->ncomments && c->caret.para < c->doc->np) {
    k = doc_comment_at(c->doc, c->caret, NULL, NULL);
    if (k >= 0) k = rv_thread_root(c->doc, k);
  }
  if (k >= 0) *on |= RBIT(ST_INCOMMENT);
  if (k >= 0 && c->doc->comments[k].done) *on |= RBIT(ST_RESOLVED);
  show_labels();
}
/* Display for Review's field: the way changes are shown now */
static void wp5_field(ULONG id, const struct MUIP_Ribbon_State *st, char *out, size_t cap)
{
  Doc *d = st && st->doc ? st->doc : wp_doc();
  int m = d ? rv_mode(d) : RVM_ALL, i;
  if (id != ID_RV_MARKUP) { if (cap) out[0] = 0; return; }
  for (i = 0; i < 4 && markup_mode[i] != m; i++) ;
  snprintf(out, cap, "%s", tr(markups[i < 4 ? i : 1]));
}
/* the right-button menu: Reply and Resolve in a comment, Accept and Reject at a change */
static void wp5_ctxmenu(Object *menu, const WpCtx *c)
{
  int k = -1, change = 0;
  if (c->where != WPC_TEXT && c->where != WPC_SPELLING) return;
  if (c->doc && c->doc->ncomments && c->caret.para < c->doc->np) { k = doc_comment_at(c->doc, c->caret, NULL, NULL); if (k >= 0) k = rv_thread_root(c->doc, k); }
  if (c->doc && c->caret.para < c->doc->np) {
    const Para *p = c->doc->p[c->caret.para];
    change = rv_is_change(para_fmt_before((Para *)p, c->caret.off)) || rv_is_change(para_fmt_at((Para *)p, c->caret.off)) || p->pf.prev;
  }
  if (k < 0 && !change) return;
  wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
  if (k >= 0) {
    wp_ctx_item(menu, tr("Reply to Comment"), WP_CTX_ID(ID_RV_REPLY), 1);
    wp_ctx_item(menu, tr(c->doc->comments[k].done ? "Reopen Comment" : "Resolve Comment"), WP_CTX_ID(ID_RV_RESOLVE), 1);
  }
  if (change) {
    wp_ctx_item(menu, tr("Accept Change"), WP_CTX_ID(ID_RV_ACCEPT1), 1);
    wp_ctx_item(menu, tr("Reject Change"), WP_CTX_ID(ID_RV_REJECT1), 1);
  }
}
static void wp5_init(void)
{
  opts_load();
  show_labels();
}
static void wp5_cleanup(void)
{
  free(rvch); rvch = NULL;
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp5_package = {
  "WP5", ID_WP5_FIRST, ID_WP5_LAST,
  wp5_command, wp5_state, wp5_field, wp5_ctxmenu, NULL, wp5_init, wp5_cleanup,
  groups, (int)(sizeof groups / sizeof groups[0]),
  menus, (int)(sizeof menus / sizeof menus[0])
};
