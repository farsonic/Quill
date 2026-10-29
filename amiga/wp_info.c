/*
 * wp_info.c - WP24 Document safety and information (docs/wordcraft-gap.md). This file belongs to WP24: only that
 * package edits it. Its commands are ID_WP24_FIRST .. ID_WP24_LAST (ids.h, named in wp_info.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * AutoSave: a timer (an MUI input handler, once a second) saves the current document where it is
 * after a pause in typing, when it has a .docx of its own. Recovery copies: every minute each window's
 * unsaved document is written to PROGDIR:Recovery (core/recover.c); copies left from a session that
 * ended without saving are offered at the next start and on File > Info > Recover Unsaved. Version
 * History: each save keeps a copy in PROGDIR:Versions. Properties, Inspect Document, Check
 * Accessibility (with the Alt Text pane), Check Compatibility: windows of their own.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dos/dos.h>
#include <utility/date.h>
#include <libraries/iffparse.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "wp_info.h"
#include "lang.h"
#include "theme.h"
#include "../core/info.h"
#include "../core/inspect.h"
#include "../core/recover.h"

#undef Label1
#undef Label2
#undef LLabel1
#define Label1(l)  QLabel(l, MUIO_Label_SingleFrame)
#define Label2(l)  QLabel(l, MUIO_Label_DoubleFrame)
#define LLabel1(l) QLabel(l, MUIO_Label_LeftAligned | MUIO_Label_SingleFrame)

/* main.c's (see there) */
Doc *wp_window_doc(int i, const char **path, int *changed);
int  wp_save_current(void);
int  wp_open_doc(Doc *d, const char *path, int changed);
void wp_replace_doc(Doc *d, int changed);
const char *wp_username(void);

#define RECDIR "PROGDIR:Recovery"
#define VERDIR "PROGDIR:Versions"
#define PREFS  "PROGDIR:Quill.save"
#define AUTOSAVE_PAUSE 3                 /* seconds without typing before AutoSave saves */
#define AUTOSAVE_EVERY 30                /* and at most this often */
#define RECOVER_EVERY 60                 /* seconds between recovery copies */

static int autosave = 1, autosaving;
static ULONG ticks, lastchange, lastauto;
static Doc *lastdoc; static u32 lastver;
static int base_id;                      /* recovery copies from earlier sessions have ids below this */
static int offered;                      /* the start's offer of recovery copies made */
static int cur_obj = -1;                 /* the object selected (from the state hook) */

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
static Object *mklist(void)
{
  return ListviewObject, MUIA_CycleChain, 1, MUIA_Listview_DoubleClick, TRUE,
    MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
      MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End;
}
static Object *mkstring(int max) { return StringObject, StringFrame, MUIA_String_MaxLen, max, MUIA_CycleChain, 1, End; }
static Object *mktext(void) { return TextObject, MUIA_Text_Contents, (ULONG)"", End; }
static LONG active(Object *l) { LONG a = MUIV_List_Active_Off; if (l) get(l, MUIA_List_Active, &a); return a; }
/* UTF-8 to the interface's Latin-1 and back */
static void lat(const char *utf8, char *out, size_t cap) { utf8_to_latin1(utf8, strlen(utf8), out, cap); }
static void set_lat(Object *o, const char *utf8) { char b[600]; lat(utf8, b, sizeof b); set(o, MUIA_String_Contents, (ULONG)b); }
static void get_utf8(Object *o, char *out, size_t cap) { STRPTR s = NULL; get(o, MUIA_String_Contents, &s); wp_latin1_utf8(s ? (const char *)s : "", out, cap); }
static void changed_now(void) { Doc *d = wp_doc(); if (d) { d->dirty = 1; d->ver++; } set(wp_view(), MUIA_Quill_Changed, TRUE); wp_refresh(); }
static int is_open(Object *w) { ULONG o = 0; if (w) get(w, MUIA_Window_Open, &o); return o != 0; }
/* now, as ISO 8601 (for the document's properties) and for people ("2026-10-29 14:05") */
static void now(char *iso, char *human)
{
  struct DateStamp ds;
  struct ClockData cd;
  DateStamp(&ds);
  Amiga2Date((ULONG)ds.ds_Days * 86400UL + (ULONG)ds.ds_Minute * 60UL + (ULONG)ds.ds_Tick / 50UL, &cd);
  if (iso) sprintf(iso, "%04d-%02d-%02dT%02d:%02d:%02dZ", (int)cd.year, (int)cd.month, (int)cd.mday, (int)cd.hour, (int)cd.min, (int)cd.sec);
  if (human) sprintf(human, "%04d-%02d-%02d %02d:%02d", (int)cd.year, (int)cd.month, (int)cd.mday, (int)cd.hour, (int)cd.min);
}

/* ---- AutoSave's setting */
int wp24_autosave_on(void) { return autosave; }
void wp24_autosave_set(int on)
{
  FILE *f;
  autosave = on != 0;
  if ((f = fopen(PREFS, "w"))) { fprintf(f, "autosave=%d\n", autosave); fclose(f); }
  if (wp_view()) wp_refresh();
}
static void load_prefs24(void)
{
  FILE *f = fopen(PREFS, "r");
  char line[80];
  if (!f) return;
  while (fgets(line, sizeof line, f)) if (!strncmp(line, "autosave=", 9)) autosave = atoi(line + 9) != 0;
  fclose(f);
}

/* ---- recovery copies: each window's unsaved document, by its Doc.serial */
#define MAXTRK 16
static struct { u32 serial; int id; u32 ver; u8 alive; } trk[MAXTRK];
static int ntrk;
static int trk_find(u32 serial) { int i; for (i = 0; i < ntrk; i++) if (trk[i].serial == serial) return i; return -1; }
static void trk_drop(int i) { rec_drop(trk[i].id); trk[i] = trk[--ntrk]; }
static void recover_tick(void)
{
  char human[24];
  int i, t;
  now(NULL, human);
  for (t = 0; t < ntrk; t++) trk[t].alive = 0;
  for (i = 0; i < 64; i++) {
    const char *path = "";
    int ch = 0;
    Doc *d = wp_window_doc(i, &path, &ch);
    if (!d) continue;
    t = trk_find(d->serial);
    if (t >= 0) trk[t].alive = 1;
    if (d->edit.kind) continue;                    /* (a header or note is being edited in its body's place) */
    if (ch) {
      if (t < 0 && ntrk < MAXTRK) { t = ntrk++; trk[t].serial = d->serial; trk[t].id = rec_new_id(); trk[t].ver = d->ver - 1; trk[t].alive = 1; }
      if (t >= 0 && trk[t].ver != d->ver) {
        const char *name = path[0] ? (const char *)FilePart((STRPTR)path) : tr("Untitled");
        if (rec_save(trk[t].id, d, path, name, human)) trk[t].ver = d->ver;
      }
    } else if (t >= 0) trk_drop(t);              /* saved (or changes undone) */
  }
  for (t = ntrk - 1; t >= 0; t--) if (!trk[t].alive) trk_drop(t);   /* its window closed */
}
/* (main.c) a document saved: the save kept as a version, its recovery copy no longer needed */
void wp24_saved(Doc *d, const char *path)
{
  char human[24];
  const DocFormat *f = doc_format_for_name(path, 1);
  int t;
  now(NULL, human);
  if (f && (f->flags & DF_NATIVE)) ver_add(path, human, autosaving);
  if (d && (t = trk_find(d->serial)) >= 0) trk_drop(t);
}

/* ---- Recover Unsaved Documents: copies from sessions that ended without saving them */
static Object *recwin, *reclist;
static RecEntry recs[32]; static int nrec;
static int old_recs(RecEntry *out, int max)
{
  RecEntry all[32];
  int n = rec_list(all, 32), i, k = 0;
  for (i = 0; i < n && k < max; i++) if (all[i].id < base_id) out[k++] = all[i];
  return k;
}
static void rec_fill(void)
{
  int i;
  nrec = old_recs(recs, 32);
  DoMethod(reclist, MUIM_List_Clear);
  for (i = 0; i < nrec; i++) {
    char line[200], name[120];
    lat(recs[i].name, name, sizeof name);
    snprintf(line, sizeof line, "%s   %s", recs[i].stamp, name);
    DoMethod(reclist, MUIM_List_InsertSingle, (ULONG)line, MUIV_List_Insert_Bottom);
  }
  if (nrec) set(reclist, MUIA_List_Active, 0);
}
/* copy k opened in a window of its own (as unsaved, its own path kept), and dropped from the drawer */
static int rec_open(int k)
{
  char file[300], err[200];
  unsigned lost;
  Doc *d;
  rec_file(recs[k].id, file, sizeof file);
  if (!(d = doc_load_file(file, &lost, err, sizeof err))) { wp_ask("OK", "Quill could not open the copy:\n%s", file); return 0; }
  if (!wp_open_doc(d, recs[k].path, 1)) return 0;
  rec_drop(recs[k].id);
  return 1;
}
static void open_recover(void)
{
  if (!recwin) {
    Object *op, *del, *cl;
    recwin = mkwin("Recover Unsaved Documents", MAKE_ID('W','2','4','R'), VGroup, QDLG,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Copies Quill kept of documents that were not saved when it ended."), End,
      Child, reclist = mklist(),
      Child, HGroup, Child, op = QButton(tr("_Open")), Child, del = QButton(tr("_Delete")), Child, HSpace(0), Child, cl = QButton(tr("_Close")), End,
    End);
    if (!recwin) return;
    bt(op, ID_INFO_RECOPEN); bt(del, ID_INFO_RECDEL);
    DoMethod(reclist, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_INFO_RECOPEN);
    DoMethod(cl, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)recwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  }
  rec_fill();
  set(recwin, MUIA_Window_Open, TRUE);
}
/* the start: copies left from before offered back */
static void offer_recovery(void)
{
  RecEntry e[32];
  int n = old_recs(e, 32), i;
  char num[12];
  if (!n) return;
  sprintf(num, "%d", n);
  switch (wp_ask("_Recover|_Later|_Discard", "Quill ended without saving %s document(s).\nRecover them now?\n(Later: File > Info > Recover Unsaved.)", num)) {
  case 1:
    memcpy(recs, e, sizeof recs); nrec = n;
    for (i = 0; i < n; i++) rec_open(i);
    break;
  case 0: for (i = 0; i < n; i++) rec_drop(e[i].id); break;
  default: break;
  }
}

/* ---- the timer: an MUI input handler once a second on a small object of its own */
#define MUIM_Info_Tick (TAG_USER | 0x0451A240UL)
static struct MUI_CustomClass *tcls;
static Object *tobj;
static struct MUI_InputHandlerNode ihn;
static int ihn_on;
static ULONG tdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  if (msg->MethodID == MUIM_Info_Tick) { DoMethod(wp_app(), MUIM_Application_ReturnID, ID_INFO_TICK); return 0; }
  return DoSuperMethodA(cl, obj, msg);
}
static void tick(void)
{
  char iso[32];
  Doc *d = wp_doc();
  ULONG ch = 0;
  Layout *L = NULL;
  ticks++;
  now(iso, NULL);
  info_set_clock(iso);                             /* (what saving writes as modified) */
  info_set_user(wp_username());
  if (!offered && ticks >= 2) { offered = 1; offer_recovery(); return; }
  if (!d) return;
  if (d != lastdoc || d->ver != lastver) { lastdoc = d; lastver = d->ver; lastchange = ticks; }
  get(wp_view(), MUIA_Quill_Layout, &L);
  if (L && L->complete && L->npages && info_get(d)) d->info->pages = L->npages;   /* (app.xml's Pages) */
  get(wp_view(), MUIA_Quill_Changed, &ch);
  if (autosave && ch && !d->edit.kind && ticks - lastchange >= AUTOSAVE_PAUSE && ticks - lastauto >= AUTOSAVE_EVERY) {
    lastauto = ticks;
    autosaving = 1;
    wp_save_current();                             /* (nothing for a document without a .docx of its own) */
    autosaving = 0;
  }
  if (ticks % RECOVER_EVERY == 0) recover_tick();
}

/* ---- Properties */
static Object *prwin, *pr_s[9], *pr_stat[8], *pr_cname, *pr_ctype, *pr_cval, *pr_clist;
static const int pr_which[9] = { -1, IP_SUBJECT, -2, IP_MANAGER, IP_COMPANY, IP_CATEGORY, IP_KEYWORDS, IP_DESCRIPTION, IP_HLINKBASE };   /* -1 title, -2 author */
static void cust_fill(void)
{
  Doc *d = wp_doc();
  static const char *const types[4] = { "Text", "Date", "Number", "Yes or no" };
  int i;
  DoMethod(pr_clist, MUIM_List_Clear);
  for (i = 0; d && d->info && i < d->info->ncust; i++) {
    const CustProp *c = &d->info->cust[i];
    char line[300], v[200], n[80];
    int ty = c->type == CP_DATE ? 1 : c->type == CP_NUMBER ? 2 : c->type == CP_BOOL ? 3 : 0;
    lat(c->name, n, sizeof n);
    if (c->type == CP_BOOL) strcpy(v, tr(strcmp(c->value, "1") ? "No" : "Yes"));
    else if (c->type == CP_OTHER) strcpy(v, "...");
    else lat(c->value, v, sizeof v);
    snprintf(line, sizeof line, "%s: %s (%s)", n, v, tr(types[ty]));
    DoMethod(pr_clist, MUIM_List_InsertSingle, (ULONG)line, MUIV_List_Insert_Bottom);
  }
}
static void open_props(void)
{
  Doc *d = wp_doc();
  int i;
  char b[64];
  if (!d) return;
  if (!prwin) {
    static const char *titles[4], *types[5];
    static const char *const lab[9] = { "Title", "Subject", "Author", "Manager", "Company", "Category", "Keywords", "Comments", "Hyperlink base" };
    static const char *const slab[8] = { "Created", "Modified", "Last saved by", "Revision number", "Pages", "Words", "Characters", "Paragraphs" };
    Object *sum, *stat, *cust, *add, *del;
    titles[0] = tr("Summary"); titles[1] = tr("Statistics"); titles[2] = tr("Custom"); titles[3] = NULL;
    types[0] = tr("Text"); types[1] = tr("Date"); types[2] = tr("Number"); types[3] = tr("Yes or no"); types[4] = NULL;
    sum = ColGroup(2), End;
    stat = ColGroup(2), End;
    if (!sum || !stat) return;
    for (i = 0; i < 9; i++) { DoMethod(sum, OM_ADDMEMBER, (ULONG)Label2(tr(lab[i]))); DoMethod(sum, OM_ADDMEMBER, (ULONG)(pr_s[i] = mkstring(i == 7 ? 500 : 250))); }
    for (i = 0; i < 8; i++) { DoMethod(stat, OM_ADDMEMBER, (ULONG)Label1(tr(slab[i]))); DoMethod(stat, OM_ADDMEMBER, (ULONG)(pr_stat[i] = mktext())); }
    cust = VGroup,
      Child, ColGroup(2),
        Child, Label2(tr("Name")), Child, pr_cname = mkstring(60),
        Child, Label1(tr("Type")), Child, pr_ctype = CycleObject, MUIA_Cycle_Entries, (ULONG)types, MUIA_CycleChain, 1, End,
        Child, Label2(tr("Value")), Child, pr_cval = mkstring(120),
      End,
      Child, HGroup, Child, add = QButton(tr("_Add")), Child, del = QButton(tr("_Delete")), Child, HSpace(0), End,
      Child, pr_clist = mklist(),
    End;
    prwin = wp_dialog("Properties", MAKE_ID('W','2','4','P'), RegisterGroup(titles), MUIA_CycleChain, 1,
      Child, sum, Child, stat, Child, cust, End, "_OK", ID_INFO_PROPSOK);
    if (!prwin) return;
    bt(add, ID_INFO_CUSTADD); bt(del, ID_INFO_CUSTDEL);
    DoMethod(pr_clist, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_INFO_CUSTPICK);
  }
  for (i = 0; i < 9; i++) set_lat(pr_s[i], pr_which[i] == -1 ? d->title : pr_which[i] == -2 ? d->author : info_str(d, pr_which[i]));
  {
    u32 w, c, cs, p;
    Layout *L = NULL;
    const char *v[4];
    info_stats(d, &w, &c, &cs, &p);
    v[0] = info_str(d, IP_CREATED); v[1] = info_str(d, IP_MODIFIED); v[2] = info_str(d, IP_LASTBY); v[3] = info_str(d, IP_REVISION);
    for (i = 0; i < 4; i++) { char t[200]; lat(v[i], t, sizeof t); set(pr_stat[i], MUIA_Text_Contents, (ULONG)(t[0] ? t : "-")); }
    get(wp_view(), MUIA_Quill_Layout, &L);
    sprintf(b, "%lu", (unsigned long)(L ? L->npages : 0)); set(pr_stat[4], MUIA_Text_Contents, (ULONG)b);
    sprintf(b, "%lu", (unsigned long)w); set(pr_stat[5], MUIA_Text_Contents, (ULONG)b);
    sprintf(b, "%lu", (unsigned long)c); set(pr_stat[6], MUIA_Text_Contents, (ULONG)b);
    sprintf(b, "%lu", (unsigned long)p); set(pr_stat[7], MUIA_Text_Contents, (ULONG)b);
  }
  cust_fill();
  set(prwin, MUIA_Window_Open, TRUE);
}
static void props_ok(void)
{
  Doc *d = wp_doc();
  int i;
  char u[1100];
  if (!d) return;
  for (i = 0; i < 9; i++) {
    get_utf8(pr_s[i], u, sizeof u);
    if (pr_which[i] == -1) { strncpy(d->title, u, sizeof d->title - 1); d->title[sizeof d->title - 1] = 0; }
    else if (pr_which[i] == -2) { strncpy(d->author, u, sizeof d->author - 1); d->author[sizeof d->author - 1] = 0; }
    else info_set(d, pr_which[i], u);
  }
  set(prwin, MUIA_Window_Open, FALSE);
  changed_now();
}
static void cust_add(void)
{
  static const int types[4] = { CP_TEXT, CP_DATE, CP_NUMBER, CP_BOOL };
  char n[200], v[400];
  ULONG ty = 0;
  get_utf8(pr_cname, n, sizeof n); get_utf8(pr_cval, v, sizeof v);
  get(pr_ctype, MUIA_Cycle_Active, &ty);
  if (!n[0]) { DisplayBeep(NULL); return; }
  if (types[ty & 3] == CP_BOOL) strcpy(v, (!v[0] || v[0] == '0' || v[0] == 'n' || v[0] == 'N') ? "0" : "1");
  if (types[ty & 3] == CP_NUMBER && !v[0]) strcpy(v, "0");
  info_cust_set(wp_doc(), n, types[ty & 3], v);
  cust_fill();
  changed_now();
}
static void cust_pick(void)
{
  Doc *d = wp_doc();
  LONG a = active(pr_clist);
  if (!d || !d->info || a < 0 || a >= d->info->ncust) return;
  set_lat(pr_cname, d->info->cust[a].name);
  set_lat(pr_cval, d->info->cust[a].type == CP_OTHER ? "" : d->info->cust[a].value);
  set(pr_ctype, MUIA_Cycle_Active, d->info->cust[a].type == CP_DATE ? 1 : d->info->cust[a].type == CP_NUMBER ? 2 : d->info->cust[a].type == CP_BOOL ? 3 : 0);
}

/* ---- Inspect Document */
#define NINS 5
static Object *inwin, *in_chk[NINS], *in_res[NINS];
static const unsigned in_bits[NINS] = { INS_COMMENTS, INS_REVISIONS, INS_PROPS, INS_HEADERS, INS_CUSTOMXML };
static void inspect_now(void)
{
  InspectReport r;
  u32 v[NINS];
  int i;
  static const char *const found[NINS] = { "%lu comment(s) found", "%lu tracked change(s) found", "%lu propertie(s) and personal details found",
                                           "%lu header(s), footer(s) or watermark(s) found", "%lu custom XML part(s) found" };
  if (!wp_doc()) return;
  inspect_doc(wp_doc(), &r);
  v[0] = r.comments; v[1] = r.revisions; v[2] = r.props; v[3] = r.headers; v[4] = r.customxml;
  for (i = 0; i < NINS; i++) {
    char t[120];
    if (v[i]) snprintf(t, sizeof t, tr(found[i]), (unsigned long)v[i]); else snprintf(t, sizeof t, "%s", tr("None found"));
    set(in_res[i], MUIA_Text_Contents, (ULONG)t);
  }
}
static void open_inspect(void)
{
  if (!inwin) {
    static const char *const cats[NINS] = { "Comments", "Revisions (tracked changes)", "Document properties and personal information",
                                            "Headers, footers and watermarks", "Custom XML data" };
    Object *g = ColGroup(3), End, *rem;
    int i;
    if (!g) return;
    for (i = 0; i < NINS; i++) {
      DoMethod(g, OM_ADDMEMBER, (ULONG)(in_chk[i] = CheckMark(TRUE)));
      DoMethod(g, OM_ADDMEMBER, (ULONG)LLabel1(tr(cats[i])));
      DoMethod(g, OM_ADDMEMBER, (ULONG)(in_res[i] = mktext()));
    }
    inwin = wp_dialog("Document Inspector", MAKE_ID('W','2','4','I'), VGroup,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Check the document for content you may not want to share:"), End,
      Child, g,
      Child, HGroup, Child, HSpace(0), Child, rem = QButton(tr("_Remove Checked")), End,
    End, "_Inspect", ID_INFO_INSPGO);
    if (!inwin) return;
    bt(rem, ID_INFO_INSPREMOVE);
  }
  inspect_now();
  set(inwin, MUIA_Window_Open, TRUE);
}
static void inspect_remove_now(void)
{
  unsigned what = 0;
  int i;
  for (i = 0; i < NINS; i++) { ULONG s = 0; get(in_chk[i], MUIA_Selected, &s); if (s) what |= in_bits[i]; }
  if (!what || !wp_doc()) return;
  if (wp_ask("_Remove|_Cancel", "%s", tr("Remove what is checked from the document?\nHeaders, footers and properties can't be brought back with Undo.")) != 1) return;
  wp_story_close();
  inspect_remove(wp_doc(), what);
  changed_now();
  inspect_now();
}

/* ---- Accessibility: the checker's pane and the Alt Text pane */
static Object *acwin, *aclist, *acfix;
static AccIssue acc[100]; static int nacc;
static void acc_fill(void)
{
  int i;
  static const char *const kinds[3] = { "Error", "Warning", "Tip" };
  if (!wp_doc()) return;
  nacc = acc_check(wp_doc(), acc, 100);
  DoMethod(aclist, MUIM_List_Clear);
  for (i = 0; i < nacc; i++) {
    char m[160], line[220];
    snprintf(m, sizeof m, tr(acc_message(acc[i].rule)), acc[i].a, acc[i].b);
    if (acc[i].rule == AR_CONTRAST) snprintf(m, sizeof m, "%s (%d.%02d:1)", tr(acc_message(acc[i].rule)), acc[i].a / 100, acc[i].a % 100);
    snprintf(line, sizeof line, "%s: %s", tr(kinds[acc[i].kind % 3]), m);
    DoMethod(aclist, MUIM_List_InsertSingle, (ULONG)line, MUIV_List_Insert_Bottom);
  }
  if (!nacc) DoMethod(aclist, MUIM_List_InsertSingle, (ULONG)tr("No accessibility issues found."), MUIV_List_Insert_Bottom);
  set(acfix, MUIA_Text_Contents, (ULONG)"");
  if (nacc) set(aclist, MUIA_List_Active, 0);
}
static void open_acc(void)
{
  if (!acwin) {
    Object *go, *alt, *dec, *again, *cl;
    acwin = mkwin("Accessibility", MAKE_ID('W','2','4','A'), VGroup, QDLG,
      Child, aclist = mklist(),
      Child, acfix = TextObject, MUIA_Text_Contents, (ULONG)"", MUIA_Text_SetMin, FALSE, End,
      Child, HGroup, Child, go = QButton(tr("_Go To")), Child, alt = QButton(tr("_Alt Text...")), Child, dec = QButton(tr("_Mark as Decorative")), End,
      Child, HGroup, Child, again = QButton(tr("Check _Again")), Child, HSpace(0), Child, cl = QButton(tr("_Close")), End,
    End);
    if (!acwin) return;
    bt(go, ID_INFO_ACCGO); bt(alt, ID_INFO_ACCALT); bt(dec, ID_INFO_ACCDECOR); bt(again, ID_INFO_ACCAGAIN);
    DoMethod(cl, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)acwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(aclist, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_INFO_ACCPICK);
    DoMethod(aclist, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_INFO_ACCGO);
  }
  wp_story_close();
  acc_fill();
  set(acwin, MUIA_Window_Open, TRUE);
}
/* the issue chosen: the caret there (an object selected) */
static int acc_go(void)
{
  LONG a = active(aclist);
  Pos ab[2];
  Doc *d = wp_doc();
  if (a < 0 || a >= nacc || !d || acc[a].at.para >= d->np) return 0;
  ab[0] = ab[1] = acc[a].at;
  if (acc[a].obj >= 0) ab[1].off += 4;
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_SELECT, (ULONG)ab);
  if (acc[a].obj >= 0) cur_obj = acc[a].obj;
  return 1;
}
static Object *altwin, *alt_kind, *alt_str, *alt_dec;
static void alt_fill(void)
{
  Doc *d = wp_doc();
  static const char *const kinds[4] = { "Picture", "Shape", "Text box", "Group" };
  if (!altwin) return;
  if (!d || cur_obj < 0 || cur_obj >= d->nimages) {
    set(alt_kind, MUIA_Text_Contents, (ULONG)tr("Select a picture or shape to describe it."));
    set(alt_str, MUIA_String_Contents, (ULONG)""); set(alt_str, MUIA_Disabled, TRUE); set(alt_dec, MUIA_Disabled, TRUE);
    return;
  }
  set(alt_kind, MUIA_Text_Contents, (ULONG)tr(kinds[d->images[cur_obj].kind & 3]));
  set(alt_str, MUIA_Disabled, FALSE); set(alt_dec, MUIA_Disabled, FALSE);
  set_lat(alt_str, d->images[cur_obj].alt);
  nnset(alt_dec, MUIA_Selected, d->images[cur_obj].decorative != 0);
}
static void open_alt(void)
{
  if (!altwin) {
    Object *ap, *cl;
    altwin = mkwin("Alt Text", MAKE_ID('W','2','4','T'), VGroup, QDLG,
      Child, alt_kind = TextObject, MUIA_Text_Contents, (ULONG)"", End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("How would you describe this object and its context\nto someone who can't see it?"), End,
      Child, alt_str = mkstring(159),
      Child, HGroup, Child, alt_dec = CheckMark(FALSE), Child, LLabel1(tr("Mark as decorative")), Child, HSpace(0), End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("\33iDecorative objects add visual interest but aren't\ninformative (stylistic borders, for example)."), End,
      Child, HGroup, Child, ap = QButton(tr("_Apply")), Child, HSpace(0), Child, cl = QButton(tr("_Close")), End,
    End);
    if (!altwin) return;
    bt(ap, ID_INFO_ALTSET);
    DoMethod(alt_str, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_INFO_ALTSET);
    DoMethod(alt_dec, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_INFO_ALTDECOR);
    DoMethod(cl, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)altwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  }
  alt_fill();
  set(altwin, MUIA_Window_Open, TRUE);
}
/* (main.c's Alt Text button and menu item) the pane, for the object selected */
int wp24_alt_pane(void) { open_alt(); return altwin != NULL; }
/* the selected object's alternative text and decorative mark (decor: from the check box; -1 as it is) */
static void alt_apply(int decor)
{
  Doc *d = wp_doc();
  Image m;
  if (!d || cur_obj < 0 || cur_obj >= d->nimages) { DisplayBeep(NULL); return; }
  m = d->images[cur_obj];
  get_utf8(alt_str, m.alt, sizeof m.alt);
  if (decor >= 0) m.decorative = (u8)decor;
  else { ULONG s = 0; get(alt_dec, MUIA_Selected, &s); m.decorative = (u8)(s != 0); }
  if (!strcmp(m.alt, d->images[cur_obj].alt) && m.decorative == d->images[cur_obj].decorative) return;
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_OBJSET, (ULONG)&m);
  set(wp_view(), MUIA_Quill_Changed, TRUE);
}

/* ---- Check Compatibility */
static void compat_now(void)
{
  CompatIssue c[16];
  int n, i;
  char msg[1400], line[200];
  if (!wp_doc()) return;
  n = compat_check(wp_doc(), c, 16);
  snprintf(msg, sizeof msg, "%s\n", tr("Compatibility Checker"));
  if (!n) strcat(msg, tr("\nNo compatibility issues were found."));
  for (i = 0; i < n; i++) {
    snprintf(line, sizeof line, tr(c[i].msg), (int)c[i].count);
    if (strlen(msg) + strlen(line) + 6 < sizeof msg) { strcat(msg, "\n - "); strcat(msg, line); }
  }
  wp_ask("OK", "%s", msg);
}

/* ---- Version History */
static Object *verwin, *verlist, *verfile;
static VerEntry vers[VER_KEEP + 1]; static int nver;
static char verpath[512];
static void ver_fill(void)
{
  int i;
  nver = ver_list(verpath, vers, VER_KEEP + 1);
  DoMethod(verlist, MUIM_List_Clear);
  for (i = 0; i < nver; i++) {
    char line[120];
    snprintf(line, sizeof line, "%s   %ld KB%s", vers[i].stamp, (vers[i].size + 1023) / 1024, vers[i].kind == 'a' ? tr("   (AutoSave)") : "");
    DoMethod(verlist, MUIM_List_InsertSingle, (ULONG)line, MUIV_List_Insert_Bottom);
  }
  if (!nver) DoMethod(verlist, MUIM_List_InsertSingle, (ULONG)tr("No earlier versions yet: one is kept each time the document is saved."), MUIV_List_Insert_Bottom);
  else set(verlist, MUIA_List_Active, 0);
}
static void open_versions(void)
{
  const char *path = "";
  int i;
  for (i = 0; i < 64; i++) { const char *p; int ch; Doc *d = wp_window_doc(i, &p, &ch); if (d && d == wp_doc()) { path = p; break; } }
  if (!path[0]) { wp_ask("OK", "%s", tr("Save the document first: Quill keeps a version each time it is saved.")); return; }
  snprintf(verpath, sizeof verpath, "%s", path);
  if (!verwin) {
    Object *op, *rs, *del, *cl;
    verwin = mkwin("Version History", MAKE_ID('W','2','4','V'), VGroup, QDLG,
      Child, verfile = TextObject, MUIA_Text_Contents, (ULONG)"", End,
      Child, verlist = mklist(),
      Child, HGroup, Child, op = QButton(tr("_Open Version")), Child, rs = QButton(tr("_Restore")), Child, del = QButton(tr("_Delete")),
        Child, HSpace(0), Child, cl = QButton(tr("_Close")), End,
    End);
    if (!verwin) return;
    bt(op, ID_INFO_VEROPEN); bt(rs, ID_INFO_VERRESTORE); bt(del, ID_INFO_VERDEL);
    DoMethod(verlist, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_INFO_VEROPEN);
    DoMethod(cl, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)verwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  }
  set(verfile, MUIA_Text_Contents, (ULONG)verpath);
  ver_fill();
  set(verwin, MUIA_Window_Open, TRUE);
}
static Doc *ver_doc(int k)
{
  char file[300], err[200];
  unsigned lost;
  Doc *d;
  ver_file(verpath, vers[k].n, file, sizeof file);
  if (!(d = doc_load_file(file, &lost, err, sizeof err))) wp_ask("OK", "Quill could not open the version:\n%s", file);
  return d;
}
static void ver_cmd(ULONG id)
{
  LONG a = active(verlist);
  Doc *d;
  if (a < 0 || a >= nver) { DisplayBeep(NULL); return; }
  if (id == ID_INFO_VEROPEN) { if ((d = ver_doc(a))) wp_open_doc(d, "", 0); return; }     /* (a copy: Save asks for a name) */
  if (id == ID_INFO_VERDEL) {
    if (wp_ask("_Delete|_Cancel", "Delete the version saved %s?", vers[a].stamp) == 1) { ver_delete(verpath, vers[a].n); ver_fill(); }
    return;
  }
  if (wp_ask("_Restore|_Cancel", "Replace the document with the version saved %s?\n(Save it to keep it; what is replaced can't be undone.)", vers[a].stamp) != 1) return;
  if ((d = ver_doc(a))) { wp_replace_doc(d, 1); set(verwin, MUIA_Window_Open, FALSE); }
}

/* ---- the package */
int wp24_command(ULONG id, LONG value)
{
  (void)value;
  switch (id) {
  case ID_INFO_TICK: tick(); return WPR_KEEPFOCUS;
  case ID_INFO_AUTOSAVE: wp24_autosave_set(!autosave); break;
  case ID_INFO_PROPS: open_props(); return WPR_KEEPFOCUS;
  case ID_INFO_PROPSOK: props_ok(); break;
  case ID_INFO_CUSTADD: cust_add(); return WPR_KEEPFOCUS;
  case ID_INFO_CUSTDEL: { Doc *d = wp_doc(); LONG a = active(pr_clist); if (d && a >= 0) { info_cust_remove(d, (int)a); cust_fill(); changed_now(); } return WPR_KEEPFOCUS; }
  case ID_INFO_CUSTPICK: cust_pick(); return WPR_KEEPFOCUS;
  case ID_INFO_INSPECT: open_inspect(); return WPR_KEEPFOCUS;
  case ID_INFO_INSPGO: inspect_now(); return WPR_KEEPFOCUS;
  case ID_INFO_INSPREMOVE: inspect_remove_now(); return WPR_KEEPFOCUS;
  case ID_INFO_ACC: open_acc(); return WPR_KEEPFOCUS;
  case ID_INFO_ACCAGAIN: acc_fill(); return WPR_KEEPFOCUS;
  case ID_INFO_ACCPICK: { LONG a = active(aclist); set(acfix, MUIA_Text_Contents, (ULONG)(a >= 0 && a < nacc ? tr(acc_fix(acc[a].rule)) : "")); return WPR_KEEPFOCUS; }
  case ID_INFO_ACCGO: acc_go(); break;
  case ID_INFO_ACCALT: if (acc_go()) open_alt(); return WPR_KEEPFOCUS;
  case ID_INFO_ACCDECOR:
    if (acc_go() && cur_obj >= 0) { Doc *d = wp_doc(); Image m = d->images[cur_obj]; m.decorative = 1; DoMethod(wp_view(), MUIM_Quill_Command, CMD_OBJSET, (ULONG)&m);
      set(wp_view(), MUIA_Quill_Changed, TRUE); acc_fill(); }
    return WPR_KEEPFOCUS;
  case ID_INFO_COMPAT: compat_now(); break;
  case ID_INFO_VERSIONS: open_versions(); return WPR_KEEPFOCUS;
  case ID_INFO_VEROPEN: case ID_INFO_VERRESTORE: case ID_INFO_VERDEL: ver_cmd(id); return WPR_KEEPFOCUS;
  case ID_INFO_RECOVER: open_recover(); return WPR_KEEPFOCUS;
  case ID_INFO_RECOPEN: { LONG a = active(reclist); if (a >= 0 && a < nrec && rec_open((int)a)) rec_fill(); return WPR_KEEPFOCUS; }
  case ID_INFO_RECDEL: { LONG a = active(reclist); if (a >= 0 && a < nrec) { rec_drop(recs[a].id); rec_fill(); } return WPR_KEEPFOCUS; }
  case ID_INFO_ALTPANE: open_alt(); return WPR_KEEPFOCUS;
  case ID_INFO_ALTSET: alt_apply(-1); return WPR_KEEPFOCUS;
  case ID_INFO_ALTDECOR: { ULONG s = 0; get(alt_dec, MUIA_Selected, &s); alt_apply(s != 0); return WPR_KEEPFOCUS; }
  }
  return WPR_DONE;
}
/* AutoSave's switch; the object selected followed by the Alt Text pane; the menu's tick kept */
static void wp24_state(ULONG *on, const WpCtx *c)
{
  Object *strip = NULL, *it;
  if (autosave) *on |= 1UL << WP24S_AUTOSAVE;
  if (c->obj != cur_obj && c->obj >= 0) { cur_obj = c->obj; if (is_open(altwin)) alt_fill(); }
  else if (c->obj < 0 && cur_obj >= 0 && !c->hassel) { cur_obj = -1; if (is_open(altwin)) alt_fill(); }
  if (wp_win()) get(wp_win(), MUIA_Window_Menustrip, &strip);
  if (strip && (it = (Object *)DoMethod(strip, MUIM_FindUData, ID_INFO_AUTOSAVE))) {
    ULONG ck = 0;
    get(it, MUIA_Menuitem_Checked, &ck);
    if ((ck != 0) != autosave) SetAttrs(it, MUIA_NoNotify, TRUE, MUIA_Menuitem_Checked, autosave, TAG_DONE);
  }
}
static void wp24_ctx(Object *menu, const WpCtx *c)
{
  if (c->where != WPC_OBJECT) return;
  wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
  wp_ctx_item(menu, tr("Edit Alt Text..."), WP_CTX_ID(ID_INFO_ALTPANE), 1);
}
static void wp24_init(void)
{
  BPTR l;
  load_prefs24();
  if ((l = Lock((STRPTR)RECDIR, ACCESS_READ))) UnLock(l); else if ((l = CreateDir((STRPTR)RECDIR))) UnLock(l);
  if ((l = Lock((STRPTR)VERDIR, ACCESS_READ))) UnLock(l); else if ((l = CreateDir((STRPTR)VERDIR))) UnLock(l);
  rec_set_dirs(RECDIR, VERDIR);
  base_id = rec_new_id();
  if (!tcls) tcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Notify, NULL, 4, (APTR)tdispatch);
  if (tcls && (tobj = (Object *)NewObject(tcls->mcc_Class, NULL, TAG_DONE))) {
    memset(&ihn, 0, sizeof ihn);
    ihn.ihn_Object = tobj; ihn.ihn_Millis = 1000; ihn.ihn_Flags = MUIIHNF_TIMER; ihn.ihn_Method = MUIM_Info_Tick;
    DoMethod(wp_app(), MUIM_Application_AddInputHandler, (ULONG)&ihn);
    ihn_on = 1;
  }
}
static void wp24_cleanup(void)
{
  /* (the windows are closed: the documents were saved or let go, so their copies go) */
  while (ntrk) trk_drop(ntrk - 1);
  if (ihn_on && wp_app()) DoMethod(wp_app(), MUIM_Application_RemInputHandler, (ULONG)&ihn);
  ihn_on = 0;
  if (tobj) { DisposeObject(tobj); tobj = NULL; }
  if (tcls) { MUI_DeleteCustomClass(tcls); tcls = NULL; }
}

/* the ribbon: Review > Accessibility (Word's place for it); the menus */
static const Item acc_items[] = {
  { K_BUTTON, S_LARGE, IC_ALTTEXT, 0, "Check Accessibility", ID_INFO_ACC, 0, 0, 0, 0, "Check Accessibility: what makes the document hard to read or to use with a screen reader", NULL, NULL, 0 },
  { K_BUTTON, S_SMALL, IC_ALTTEXT, 0, "Alt Text", ID_INFO_ALTPANE, 0, 0, 0, 0, "The Alt Text pane: describe the picture or shape selected, or mark it decorative", NULL, NULL, 0 },
};
static const WpGroup groups24[] = { { TAB_REVIEW, 1, { "Accessibility", G_COLS, acc_items, 2 } } };
static const WpMenu menus24[] = {
  { WM_FILE, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FILE, { NM_ITEM, (STRPTR)"Info", 0, 0, 0, 0 } },
  { WM_FILE, { NM_SUB, (STRPTR)"Properties...", 0, 0, 0, (APTR)ID_INFO_PROPS } },
  { WM_FILE, { NM_SUB, (STRPTR)"Inspect Document...", 0, 0, 0, (APTR)ID_INFO_INSPECT } },
  { WM_FILE, { NM_SUB, (STRPTR)"Check Accessibility...", 0, 0, 0, (APTR)ID_INFO_ACC } },
  { WM_FILE, { NM_SUB, (STRPTR)"Check Compatibility...", 0, 0, 0, (APTR)ID_INFO_COMPAT } },
  { WM_FILE, { NM_SUB, (STRPTR)"Version History...", 0, 0, 0, (APTR)ID_INFO_VERSIONS } },
  { WM_FILE, { NM_SUB, (STRPTR)"Recover Unsaved Documents...", 0, 0, 0, (APTR)ID_INFO_RECOVER } },
  { WM_FILE, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FILE, { NM_SUB, (STRPTR)"AutoSave", 0, CHECKIT | MENUTOGGLE, 0, (APTR)ID_INFO_AUTOSAVE } },
  { WM_VIEW, { NM_ITEM, (STRPTR)"Alt Text Pane", 0, 0, 0, (APTR)ID_INFO_ALTPANE } },
};
const WpPackage wp24_package = {
  "WP24", ID_WP24_FIRST, ID_WP24_LAST,
  wp24_command, wp24_state, NULL, wp24_ctx, NULL, wp24_init, wp24_cleanup,
  groups24, 1,
  menus24, (int)(sizeof menus24 / sizeof menus24[0])
};
