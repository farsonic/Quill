/*
 * wp_protect.c - WP7 Protection and encryption (docs/wordcraft-gap.md). This file belongs to WP7: only that
 * package edits it. Its commands are ID_WP7_FIRST .. ID_WP7_LAST (ids.h, named in wp_protect.h); how the
 * hooks below are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * Review > Restrict Editing opens the Restrict Editing pane (a window beside the document): formatting and
 * editing restrictions, exceptions for everyone or named people on the text selected, and starting or
 * stopping enforcement with a password (core/protect.c enforces it in every edit). File > Info > Protect
 * Document > Encrypt with Password sets the password the document is encrypted with when saved
 * (core/encrypt.c); an encrypted document asks for its password as it is opened.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dos/dos.h>
#include <libraries/iffparse.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "wp_protect.h"
#include "lang.h"
#include "theme.h"
#include "splash.h"
#include "../core/crypt.h"

#undef Label1
#undef Label2
#undef LLabel1
#define Label1(l)  QLabel(l, MUIO_Label_SingleFrame)
#define Label2(l)  QLabel(l, MUIO_Label_DoubleFrame)
#define LLabel1(l) QLabel(l, MUIO_Label_LeftAligned | MUIO_Label_SingleFrame)

const char *wp_username(void);           /* (main.c) */

static int shade_on = 1;                 /* Highlight the regions I can edit */
static Doc *sdoc;                        /* the document and selection the view last showed (state hook) */
static Pos scaret, sanchor;
static u32 seen_serial;                  /* the last document whose protection was looked at */

/* ---- small helpers */
static void bt(Object *o, ULONG id) { if (o) DoMethod(o, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, id); }
static Object *mkwin(const char *title, ULONG wid, Object *contents)
{
  Object *w;
  if (!contents) return NULL;
  w = QWindowObject, MUIA_Window_Title, (ULONG)tr(title), MUIA_Window_ID, wid, WindowContents, contents, End;
  if (!w) return NULL;
  DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)w);
  DoMethod(w, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)w, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  return w;
}
static int is_open(Object *w) { ULONG o = 0; if (w) get(w, MUIA_Window_Open, &o); return o != 0; }
static LONG getv(Object *o, ULONG attr) { LONG v = 0; if (o) get(o, attr, &v); return v; }
static void user_now(void)
{
  char u[64];
  wp_latin1_utf8(wp_username()[0] ? wp_username() : "", u, sizeof u);
  strcpy(prot_user, u);
}
static void sel_range(Pos *a, Pos *b)
{
  *a = sanchor; *b = scaret;
  if (pos_cmp(*a, *b) > 0) { Pos t = *a; *a = *b; *b = t; }
}
static void doc_changed(void)
{
  set(wp_view(), MUIA_Quill_Changed, TRUE);
  wp_refresh();
}

/* ---- the password requesters: run modally (the document window asleep meanwhile) */
static Object *pwwin, *pwtext, *pw1, *pw2, *pw2grp;
static int pw_modal(const char *title, const char *text, int confirm, char *out, size_t cap)
{
  ULONG sigs = 0;
  int r = -1;
  if (!wp_app()) return 0;
  if (!pwwin) {
    Object *ok, *cancel;
    pwwin = QWindowObject, MUIA_Window_Title, (ULONG)"", MUIA_Window_ID, MAKE_ID('W','7','P','W'),
      WindowContents, VGroup, QDLG,
        Child, pwtext = TextObject, MUIA_Text_Contents, (ULONG)"", End,
        Child, ColGroup(2),
          Child, Label2(tr("Password")),
          Child, pw1 = StringObject, StringFrame, MUIA_String_Secret, TRUE, MUIA_String_MaxLen, 256, MUIA_CycleChain, 1, End,
        End,
        Child, pw2grp = ColGroup(2),
          Child, Label2(tr("Reenter password")),
          Child, pw2 = StringObject, StringFrame, MUIA_String_Secret, TRUE, MUIA_String_MaxLen, 256, MUIA_CycleChain, 1, End,
        End,
        Child, HGroup, Child, ok = QButton(tr("_OK")), Child, HSpace(0), Child, cancel = QButton(tr("_Cancel")), End,
      End, End;
    if (!pwwin) return 0;
    DoMethod(wp_app(), OM_ADDMEMBER, (ULONG)pwwin);
    bt(ok, ID_PROT_PWOK); bt(cancel, ID_PROT_PWCANCEL);
    DoMethod(pwwin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PROT_PWCANCEL);
    DoMethod(pw1, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PROT_PWOK);
    DoMethod(pw2, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PROT_PWOK);
  }
  for (;;) {
    set(pwwin, MUIA_Window_Title, (ULONG)tr(title));
    set(pwtext, MUIA_Text_Contents, (ULONG)text);
    set(pw1, MUIA_String_Contents, (ULONG)""); set(pw2, MUIA_String_Contents, (ULONG)"");
    set(pw2grp, MUIA_ShowMe, confirm != 0);
    splash_close();                        /* (a document opened as Quill starts: the splash window would cover it) */
    if (wp_win()) set(wp_win(), MUIA_Window_Sleep, TRUE);
    set(pwwin, MUIA_Window_Open, TRUE);
    DoMethod(pwwin, MUIM_Window_ToFront);
    set(pwwin, MUIA_Window_ActiveObject, (ULONG)pw1);
    r = -1;
    while (r < 0) {
      LONG id = (LONG)DoMethod(wp_app(), MUIM_Application_NewInput, (ULONG)&sigs);
      if (id == ID_PROT_PWOK) r = 1;
      else if (id == ID_PROT_PWCANCEL || id == MUIV_Application_ReturnID_Quit) r = 0;
      if (r < 0 && sigs) { sigs = Wait(sigs | SIGBREAKF_CTRL_C); if (sigs & SIGBREAKF_CTRL_C) r = 0; }
    }
    set(pwwin, MUIA_Window_Open, FALSE);
    if (wp_win()) set(wp_win(), MUIA_Window_Sleep, FALSE);
    DoMethod(wp_app(), MUIM_Application_InputBuffered);   /* (what the requester covered drawn again before any long work) */
    if (r == 1) {
      STRPTR a = NULL, b = NULL;
      get(pw1, MUIA_String_Contents, &a); get(pw2, MUIA_String_Contents, &b);
      if (confirm && strcmp(a ? (char *)a : "", b ? (char *)b : "")) { wp_ask("OK", "The passwords don't match.", NULL); continue; }
      wp_latin1_utf8(a ? (const char *)a : "", out, cap);
    }
    /* (what was typed doesn't stay in the gadgets) */
    set(pw1, MUIA_String_Contents, (ULONG)""); set(pw2, MUIA_String_Contents, (ULONG)"");
    return r == 1;
  }
}
/* core/protect.c's question while an encrypted document is read */
static char askbuf[260];
static int asleep;                       /* the window asleep while a password's key is worked out */
static void sleep_now(int on)
{
  if (wp_win()) set(wp_win(), MUIA_Window_Sleep, on ? TRUE : FALSE);
  asleep = on;
}
static const char *ask_open(int attempt)
{
  char text[300];
  const char *name = doc_load_path ? (const char *)FilePart((STRPTR)doc_load_path) : "";
  snprintf(text, sizeof text, attempt ? tr("The password is not correct.\nEnter the password to open\n%s") : tr("Enter the password to open\n%s"), name);
  crypt_wipe(askbuf, sizeof askbuf);
  if (asleep) sleep_now(0);
  if (!pw_modal("Password", text, 0, askbuf, sizeof askbuf)) return NULL;
  sleep_now(1);                          /* (until the view shows the document: wp7_state) */
  return askbuf;
}
static void denied(Doc *d) { if (d) DisplayBeep(NULL); }

/* ---- Encrypt with Password, and the Protect Document window */
static void encrypt(void)
{
  Doc *d = wp_doc();
  char pw[260];
  if (!d) return;
  if (!pw_modal("Encrypt Document", tr("Encrypt the contents of this file.\nLeave the password empty to remove the encryption.\nIf you lose or forget the password, it can't be recovered."), 1, pw, sizeof pw)) return;
  prot_set_password(d, pw[0] ? pw : NULL);
  crypt_wipe(pw, sizeof pw);
  doc_changed();
}
static Object *pdwin, *pdenc, *pdres;
static void pd_fill(void)
{
  Doc *d = wp_doc();
  int k = d ? prot_enforced(d) : PR_NONE;
  static const char *const kinds[5] = { "Anyone can change any part of this document.", "Only reading is allowed (with any exceptions).",
                                        "Only comments can be added.", "Every change is tracked.", "Only forms can be filled in." };
  if (!pdwin) return;
  set(pdenc, MUIA_Text_Contents, (ULONG)tr(d && prot_password(d) ? "A password is required to open this document." : "Anyone can open this document."));
  set(pdres, MUIA_Text_Contents, (ULONG)tr(k == PR_NONE && d && prot_limits_formatting(d) ? "Formatting is limited to a selection of styles." : kinds[k <= PR_FORMS ? k : 0]));
}
static void open_pd(void)
{
  if (!pdwin) {
    Object *enc, *res, *cl;
    pdwin = mkwin("Protect Document", MAKE_ID('W','7','P','D'), VGroup, QDLG,
      Child, HGroup, Child, enc = QButton(tr("_Encrypt with Password...")), Child, pdenc = TextObject, MUIA_Text_Contents, (ULONG)"", End, End,
      Child, HGroup, Child, res = QButton(tr("_Restrict Editing...")), Child, pdres = TextObject, MUIA_Text_Contents, (ULONG)"", End, End,
      Child, HGroup, Child, HSpace(0), Child, cl = QButton(tr("_Close")), End,
    End);
    if (!pdwin) return;
    bt(enc, ID_PROT_ENCRYPT); bt(res, ID_PROT_DOCPANE);
    DoMethod(cl, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)pdwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  }
  pd_fill();
  set(pdwin, MUIA_Window_Open, TRUE);
}

/* ---- the Restrict Editing pane */
static Object *rwin, *rpages, *r_fmt, *r_only, *r_kind, *r_every, *r_users, *r_user, *r_state, *r_shade;
static const char *rkinds[5];
static int users_k[64]; static int nusers;    /* the list's people: their exceptions */
static void users_fill(Doc *d)
{
  DocProtect *p = prot_peek(d);
  int i;
  DoMethod(r_users, MUIM_List_Clear);
  nusers = 0;
  if (!p) return;
  for (i = 0; i < p->nperms && nusers < 64; i++)
    if (p->perms[i].ed[0]) {
      char l[80];
      utf8_to_latin1(p->perms[i].ed, strlen(p->perms[i].ed), l, sizeof l);
      DoMethod(r_users, MUIM_List_InsertSingle, (ULONG)l, MUIV_List_Insert_Bottom);
      users_k[nusers++] = i + 1;
    }
}
static int everyone_perm(Doc *d) { return prot_perm(d, NULL, "everyone"); }
/* the pane shown as it is for the document and the caret */
static void pane_fill(void)
{
  Doc *d = sdoc ? sdoc : wp_doc();
  DocProtect *p = d ? prot_peek(d) : NULL;
  int k;
  if (!rwin || !d) return;
  k = p && p->enforced ? 1 : 0;
  set(rpages, MUIA_Group_ActivePage, k);
  if (!k) {
    int perms[16], n, i, every = 0;
    Pos a, b;
    nnset(r_fmt, MUIA_Selected, p && p->formatting);
    nnset(r_only, MUIA_Selected, p && p->kind != PR_NONE);
    { static const int place[5] = { 0, 0, 2, 1, 3 }; nnset(r_kind, MUIA_Cycle_Active, p && p->kind <= PR_FORMS ? place[p->kind] : 0); }
    sel_range(&a, &b);
    n = p ? prot_perms_in(d, a, pos_cmp(a, b) ? b : a, perms, 16) : 0;
    for (i = 0; i < n; i++) if (p->perms[perms[i] - 1].grp[0]) every = 1;
    nnset(r_every, MUIA_Selected, every);
  } else {
    const char *msg;
    int free_here = prot_free_at(d, scaret);
    switch (p->kind) {
    case PR_TRACKED: msg = "You may edit this document, but all changes will be tracked."; break;
    case PR_COMMENTS: msg = free_here ? "You can edit this region." : "You may only insert comments into this region."; break;
    case PR_FORMS: msg = free_here ? "You can edit this region." : "You may only fill in forms in this region."; break;
    case PR_NONE: msg = "You may edit this document, but formatting is limited to a selection of styles."; break;
    default: msg = free_here ? "You can edit this region." : "You may only view this region."; break;
    }
    {
      char t[300];
      snprintf(t, sizeof t, "%s\n%s", tr("This document is protected from unintentional editing."), tr(msg));
      set(r_state, MUIA_Text_Contents, (ULONG)t);
    }
    nnset(r_shade, MUIA_Selected, shade_on);
  }
}
static void open_pane(void)
{
  if (!rwin) {
    Object *add, *apply, *del, *clr, *start, *stop, *next, *all, *cl;
    rkinds[0] = tr("No changes (Read only)"); rkinds[1] = tr("Tracked changes"); rkinds[2] = tr("Comments"); rkinds[3] = tr("Filling in forms"); rkinds[4] = NULL;
    rwin = mkwin("Restrict Editing", MAKE_ID('W','7','R','E'), VGroup, QDLG,
      Child, rpages = PageGroup,
        /* setting it up */
        Child, VGroup,
          Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33b1. Formatting restrictions"), End,
          Child, HGroup, Child, r_fmt = CheckMark(FALSE), Child, LLabel1(tr("Limit formatting to a selection of styles")), Child, HSpace(0), End,
          Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33b2. Editing restrictions"), End,
          Child, HGroup, Child, r_only = CheckMark(FALSE), Child, LLabel1(tr("Allow only this type of editing in the document:")), Child, HSpace(0), End,
          Child, r_kind = CycleObject, MUIA_Cycle_Entries, (ULONG)rkinds, MUIA_CycleChain, 1, End,
          Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Exceptions (optional)\nSelect parts of the document and choose users\nwho are allowed to freely edit them."), End,
          Child, HGroup, Child, r_every = CheckMark(FALSE), Child, LLabel1(tr("Everyone")), Child, HSpace(0), End,
          Child, r_users = ListviewObject, MUIA_CycleChain, 1, MUIA_FixHeightTxt, (ULONG)"\n\n\n", MUIA_Listview_List, ListObject, InputListFrame,
            MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
          Child, HGroup, Child, r_user = StringObject, StringFrame, MUIA_String_MaxLen, 60, MUIA_CycleChain, 1, End, Child, add = QButton(tr("_Add User")), End,
          Child, HGroup, Child, apply = QButton(tr("A_llow Selection")), Child, del = QButton(tr("_Delete User")), Child, clr = QButton(tr("_Remove Exceptions")), End,
          Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33b3. Start enforcement"), End,
          Child, start = QButton(tr("_Yes, Start Enforcing Protection")),
        End,
        /* enforced */
        Child, VGroup,
          Child, r_state = TextObject, MUIA_Text_Contents, (ULONG)"", End,
          Child, next = QButton(tr("_Find Next Region I Can Edit")),
          Child, all = QButton(tr("_Show All Regions I Can Edit")),
          Child, HGroup, Child, r_shade = CheckMark(TRUE), Child, LLabel1(tr("Highlight the regions I can edit")), Child, HSpace(0), End,
          Child, VSpace(0),
          Child, stop = QButton(tr("S_top Protection")),
        End,
      End,
      Child, HGroup, Child, HSpace(0), Child, cl = QButton(tr("_Close")), End,
    End);
    if (!rwin) return;
    DoMethod(r_fmt, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PROT_FORMAT);
    DoMethod(r_only, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PROT_ONLY);
    DoMethod(r_kind, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PROT_KIND);
    DoMethod(r_every, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PROT_EVERYONE);
    DoMethod(r_shade, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PROT_SHADE);
    DoMethod(r_user, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PROT_USERADD);
    DoMethod(r_users, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PROT_USERAPPLY);
    bt(add, ID_PROT_USERADD); bt(apply, ID_PROT_USERAPPLY); bt(del, ID_PROT_USERDEL); bt(clr, ID_PROT_CLEAR);
    bt(start, ID_PROT_START); bt(stop, ID_PROT_STOP); bt(next, ID_PROT_NEXT); bt(all, ID_PROT_ALL);
    DoMethod(cl, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)rwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  }
  if (wp_doc()) users_fill(wp_doc());
  pane_fill();
  set(rwin, MUIA_Window_Open, TRUE);
  wp_refresh();
}
static void set_kind(void)
{
  Doc *d = wp_doc();
  int only = getv(r_only, MUIA_Selected) != 0, k = (int)getv(r_kind, MUIA_Cycle_Active);
  static const int map[4] = { PR_READONLY, PR_TRACKED, PR_COMMENTS, PR_FORMS };
  if (!d) return;
  prot_set_kind(d, only ? map[k & 3] : PR_NONE, getv(r_fmt, MUIA_Selected) != 0);
  set(wp_view(), MUIA_Quill_Changed, TRUE);
}
/* the selection made free to edit for exception k (0: the exceptions there taken off) */
static void mark(int k)
{
  Doc *d = wp_doc();
  Pos a, b;
  if (!d) return;
  sel_range(&a, &b);
  if (!pos_cmp(a, b)) { DisplayBeep(NULL); wp_ask("OK", "Select the part of the document that may be edited first.", NULL); pane_fill(); return; }
  if (prot_mark(d, a, b, k)) doc_changed();
  pane_fill();
}
static void start(void)
{
  Doc *d = wp_doc();
  char pw[260];
  if (!d) return;
  set_kind();
  if (!prot_peek(d) || prot_peek(d)->kind == PR_NONE) { wp_ask("OK", "Choose the kind of editing to allow first (Allow only this type of editing).", NULL); return; }
  if (!pw_modal("Start Enforcing Protection", tr("Enter a new password (optional):\nWithout one, anyone can stop the protection."), 1, pw, sizeof pw)) return;
  wp_story_close();
  sleep_now(1);
  prot_start(d, pw);
  sleep_now(0);
  crypt_wipe(pw, sizeof pw);
  doc_changed();
  pane_fill();
}
static void stop(void)
{
  Doc *d = wp_doc();
  char pw[260] = "";
  int r;
  if (!d) return;
  if (prot_check(d, "") != PC_NOPASSWORD && !pw_modal("Unprotect Document", tr("Password:"), 0, pw, sizeof pw)) return;
  sleep_now(1);
  r = prot_stop(d, pw);
  sleep_now(0);
  crypt_wipe(pw, sizeof pw);
  if (r == PC_INCORRECT) wp_ask("OK", "The password is incorrect.", NULL);
  else if (r == PC_UNSUPPORTED) wp_ask("OK", "Quill can't check this document's password (its hash is of a kind Quill doesn't know), so the protection stays on.", NULL);
  else doc_changed();
  pane_fill();
}
static void next_region(void)
{
  Doc *d = wp_doc();
  Pos ab[2];
  if (!d || !prot_next_free(d, scaret, &ab[0], &ab[1])) { DisplayBeep(NULL); wp_ask("OK", "There are no regions you can edit.", NULL); return; }
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_SELECT, (ULONG)ab);
}

int wp7_shade(Doc *d) { int k = prot_enforced(d); return shade_on && k != PR_NONE && k != PR_TRACKED; }

/* every command of the package's id block, from a menu, the ribbon, a key or a dialog (value: the
   list entry chosen, or WPV_NONE) */
int wp7_command(ULONG id, LONG value)
{
  Doc *d = wp_doc();
  user_now();
  switch (id) {
  case ID_PROT_PANE: if (is_open(rwin)) { set(rwin, MUIA_Window_Open, FALSE); wp_refresh(); } else open_pane(); break;
  case ID_PROT_SHOWPANE: case ID_PROT_DOCPANE: open_pane(); break;
  case ID_PROT_DOC: open_pd(); break;
  case ID_PROT_ENCRYPT: encrypt(); pd_fill(); break;
  case ID_PROT_FORMAT: case ID_PROT_ONLY: case ID_PROT_KIND: set_kind(); break;
  case ID_PROT_EVERYONE: if (d) mark(getv(r_every, MUIA_Selected) ? everyone_perm(d) : 0); break;
  case ID_PROT_USERADD: {
    STRPTR s = NULL;
    char u[64];
    get(r_user, MUIA_String_Contents, &s);
    if (!d || !s || !s[0]) { DisplayBeep(NULL); break; }
    wp_latin1_utf8((const char *)s, u, sizeof u);
    if (prot_perm(d, u, NULL)) { users_fill(d); set(r_user, MUIA_String_Contents, (ULONG)""); set(r_users, MUIA_List_Active, nusers - 1); }
    break; }
  case ID_PROT_USERAPPLY: { LONG a = getv(r_users, MUIA_List_Active); if (a >= 0 && a < nusers) mark(users_k[a]); else DisplayBeep(NULL); break; }
  case ID_PROT_USERDEL: {
    /* the person's exception taken off the whole document */
    LONG a = getv(r_users, MUIA_List_Active);
    DocProtect *p = prot_peek(d);
    u32 pi, i;
    if (!d || !p || a < 0 || a >= nusers) { DisplayBeep(NULL); break; }
    doc_begin(d, scaret);
    for (pi = 0; pi < d->np; pi++)
      for (i = 0; i < d->p[pi]->nr; i++)
        if (d->p[pi]->r[i].f.perm == users_k[a]) {
          Pos x, y;
          x.para = y.para = pi; x.off = i ? d->p[pi]->r[i - 1].end : 0; y.off = d->p[pi]->r[i].end;
          if (y.off > x.off && prot_mark(d, x, y, 0)) i = (u32)-1;    /* (the runs changed: this paragraph again) */
          else if (y.off == x.off) d->p[pi]->r[i].f.perm = 0;
        }
    doc_end(d, scaret);
    p->perms[users_k[a] - 1].ed[0] = 0; strcpy(p->perms[users_k[a] - 1].grp, "-");   /* (no one's any more) */
    users_fill(d);
    doc_changed();
    break; }
  case ID_PROT_CLEAR: mark(0); break;
  case ID_PROT_START: start(); break;
  case ID_PROT_STOP: stop(); break;
  case ID_PROT_NEXT: next_region(); break;
  case ID_PROT_ALL: shade_on = 1; nnset(r_shade, MUIA_Selected, TRUE); wp_refresh(); next_region(); break;
  case ID_PROT_SHADE: shade_on = getv(r_shade, MUIA_Selected) != 0; DoMethod(wp_view(), MUIM_Quill_Command, CMD_PAGESETUP, 0); break;
  }
  return WPR_DONE;
}

/* the ribbon's state (bit 1: the pane open); the pane follows the caret; a protected document coming up
   shows the pane, as in Word */
static void wp7_state(ULONG *on, const WpCtx *c)
{
  if (asleep) sleep_now(0);
  if (is_open(rwin)) *on |= RBIT(1);
  if (c->doc != sdoc || pos_cmp(c->caret, scaret) || pos_cmp(c->anchor, sanchor)) {
    int newdoc = c->doc != sdoc;
    sdoc = c->doc; scaret = c->caret; sanchor = c->anchor;
    if (is_open(rwin)) { if (newdoc && sdoc) users_fill(sdoc); pane_fill(); }
  }
  if (c->doc && c->doc->serial != seen_serial) {
    seen_serial = c->doc->serial;
    if (prot_enforced(c->doc) && !is_open(rwin)) DoMethod(wp_app(), MUIM_Application_ReturnID, ID_PROT_SHOWPANE);
  }
}
static void wp7_init(void)
{
  prot_ask = ask_open;
  prot_on_denied = denied;
  user_now();
  /* (salts and keys: something of this machine's state stirred in) */
  { struct DateStamp ds; DateStamp(&ds); crypt_stir(&ds, sizeof ds); crypt_stir(&ds, (size_t)&ds & 15); }
}

/* the ribbon: Review > Protect; the menus */
static const Item prot_items[] = {
  { K_BUTTON, S_LARGE, 0, 0, "Restrict Editing", ID_PROT_PANE, RB_WP(WPI_7, 1), 0, 0, 0,
    "Restrict Editing: limit how others may change the document (read only, comments, tracked changes, forms), with exceptions",
    NULL, "ci R 3 7 12 14 ca F 4 8 11 13 ci P 6 4 7 4 4 5 3 10 3 11 4 11 7 cw F 7 9 8 11", 0 },
};
static const WpGroup groups7[] = { { TAB_REVIEW, -1, { "Protect", G_COLS, prot_items, 1 } } };
static const WpMenu menus7[] = {
  { WM_FILE, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FILE, { NM_ITEM, (STRPTR)"Protect Document", 0, 0, 0, 0 } },
  { WM_FILE, { NM_SUB, (STRPTR)"Encrypt with Password...", 0, 0, 0, (APTR)ID_PROT_ENCRYPT } },
  { WM_FILE, { NM_SUB, (STRPTR)"Restrict Editing...", 0, 0, 0, (APTR)ID_PROT_SHOWPANE } },
  { WM_REVIEW, { NM_ITEM, (STRPTR)"Restrict Editing", 0, 0, 0, (APTR)ID_PROT_PANE } },
};

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp7_package = {
  "WP7", ID_WP7_FIRST, ID_WP7_LAST,
  wp7_command, wp7_state, NULL, NULL, NULL, wp7_init, NULL,
  groups7, 1,
  menus7, (int)(sizeof menus7 / sizeof menus7[0])
};
