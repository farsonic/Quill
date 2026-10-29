/*
 * wp_templates.c - WP23 Templates, building blocks, inserting files (docs/wordcraft-gap.md). This file belongs to WP23: only that
 * package edits it. Its commands are ID_WP23_FIRST .. ID_WP23_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * Templates: opening a .dotx or .dotm makes a new, untitled document attached to it (core/blocks.c);
 * File > Save as Template writes one, by default into the personal templates folder
 * (PROGDIR:Templates), whose templates the File page's New list offers. Templates and Add-ins:
 * the attached template, and whether its styles are copied in when the document opens.
 * Building blocks (Quick Parts, AutoText and Word's other galleries) come from three places: the
 * document's own glossary, the personal Building Blocks file (PROGDIR:Templates/Building Blocks.dotx)
 * and the attached template. The Quick Parts and AutoText lists insert them; the Building Blocks
 * Organizer lists, inserts, edits and deletes them; Save Selection makes one. F3 puts in the AutoText
 * entry named by the word before the caret. Document Property puts in one of the document's
 * properties (a DOCPROPERTY field, core/field.h). Text from File and Object put in another file's text
 * and formatting (any format Quill reads), or a picture.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dos/dos.h>
#include <libraries/iffparse.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "wp_templates.h"
#include "lang.h"
#include "theme.h"
#include "../core/blocks.h"
#include "../core/info.h"
#include "../core/image.h"
#include "../core/field.h"

#undef Label1
#undef Label2
#undef LLabel1
#define Label1(l)  QLabel(l, MUIO_Label_SingleFrame)
#define Label2(l)  QLabel(l, MUIO_Label_DoubleFrame)
#define LLabel1(l) QLabel(l, MUIO_Label_LeftAligned | MUIO_Label_SingleFrame)

/* main.c's (see there) */
int  wp_open_doc(Doc *d, const char *path, int changed);
void wp_backstage_hide(void);
const char *wp_docpath(void);
void wp13_organizer(const char *path);   /* (WP13's Organizer, wp_styles.c) */

#define TPLDIR "PROGDIR:Templates"
#define BBFILE "PROGDIR:Templates/Building Blocks.dotx"

enum {
  ID_TP_QUICK = ID_TP_FIRSTOTHER, ID_TP_AUTOTEXT, ID_TP_OBJECT, ID_TP_TEXTFILE, ID_TP_ORGANIZER, ID_TP_SAVEQP, ID_TP_SAVEAT, ID_TP_DOCPROP,
  ID_TP_TEMPLATES, ID_TP_TPLOK, ID_TP_ATTACH, ID_TP_TPLORG, ID_TP_BBINSERT, ID_TP_BBEDIT, ID_TP_BBDELETE, ID_TP_BBPICK,
  ID_TP_NEWOK, ID_TP_PROPOK, ID_TP_FILEOK, ID_TP_FILEBROWSE, ID_TP_OBJOK, ID_TP_OBJBROWSE, ID_TP_F3, ID_TP_SAVETPL
};

/* ------------------------------------------------------------------ */
/* small helpers */
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
static Object *mkcycle(const char **e) { return CycleObject, MUIA_Cycle_Entries, (ULONG)e, MUIA_CycleChain, 1, End; }
static Object *mkcheck(int on) { return ImageObject, ImageButtonFrame, MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Image_Spec, MUII_CheckMark,
                                   MUIA_Background, MUII_ButtonBack, MUIA_ShowSelState, FALSE, MUIA_Selected, on, MUIA_CycleChain, 1, End; }
static LONG active(Object *l) { LONG a = MUIV_List_Active_Off; if (l) get(l, MUIA_List_Active, &a); return a; }
static ULONG cyc(Object *o) { ULONG v = 0; if (o) get(o, MUIA_Cycle_Active, &v); return v; }
static int chk(Object *o) { ULONG v = 0; if (o) get(o, MUIA_Selected, &v); return v != 0; }
static void lat(const char *utf8, char *out, size_t cap) { utf8_to_latin1(utf8, strlen(utf8), out, cap); }
static void set_lat(Object *o, const char *utf8) { char b[300]; lat(utf8, b, sizeof b); set(o, MUIA_String_Contents, (ULONG)b); }
static void get_utf8(Object *o, char *out, size_t cap) { STRPTR s = NULL; get(o, MUIA_String_Contents, &s); wp_latin1_utf8(s ? (const char *)s : "", out, cap); }
static const char *get_str(Object *o) { STRPTR s = NULL; get(o, MUIA_String_Contents, &s); return s ? (const char *)s : ""; }
static void beep(void) { DisplayBeep(NULL); }
static const char **trl(const char *const *src, const char **dst)
{
  int i;
  for (i = 0; src[i]; i++) dst[i] = tr(src[i]);
  dst[i] = NULL;
  return dst;
}
static Pos get_pos(ULONG attr)
{
  Pos *p = NULL, z;
  get(wp_view(), attr, &p);
  if (p) return *p;
  z.para = z.off = 0;
  return z;
}
/* the document changed behind the view's back: laid out again, the caret put at p */
static void changed_at(Pos p)
{
  static Pos ab[2];
  ab[0] = p; ab[1] = p;
  DoMethod(wp_view(), MUIM_Quill_Relayout);
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_SELECT, (ULONG)ab);
  set(wp_view(), MUIA_Quill_Changed, TRUE);
  wp_refresh();
}
static const char *file_part(const char *path)
{
  const char *a = strrchr(path, '/'), *b = strrchr(path, ':');
  if (b > a) a = b;
  return a ? a + 1 : path;
}
static int ends(const char *s, const char *e) { size_t a = strlen(s), b = strlen(e); return a >= b && str_ieq(s + a - b, e); }
/* a folder made if it isn't there */
static void make_dir(const char *path)
{
  BPTR l = Lock((STRPTR)path, ACCESS_READ);
  if (l) { UnLock(l); return; }
  if ((l = CreateDir((STRPTR)path))) UnLock(l);
}

/* ------------------------------------------------------------------ */
/* the personal templates folder */
#define MAXTPL 32
static char tplnames[MAXTPL][108];
static int ntpl = -1;
static void scan_templates(void)
{
  BPTR l = Lock((STRPTR)TPLDIR, ACCESS_READ);
  struct FileInfoBlock *fib;
  ntpl = 0;
  if (!l) return;
  if ((fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL))) {
    if (Examine(l, fib))
      while (ExNext(l, fib) && ntpl < MAXTPL) {
        const char *n = (const char *)fib->fib_FileName;
        if (fib->fib_DirEntryType >= 0 || !(ends(n, ".dotx") || ends(n, ".dotm") || ends(n, ".docx"))) continue;
        if (str_ieq(n, "Building Blocks.dotx")) continue;
        snprintf(tplnames[ntpl++], sizeof tplnames[0], "%s", n);
      }
    FreeDosObject(DOS_FIB, fib);
  }
  UnLock(l);
}
int wp23_personal(int i, char *name, size_t cap)
{
  char *dot;
  if (ntpl < 0) scan_templates();
  if (i < 0 || i >= ntpl) return 0;
  snprintf(name, cap, "%s", tplnames[i]);
  if ((dot = strrchr(name, '.'))) *dot = 0;
  return 1;
}
/* a new document from a personal template */
static void new_from_personal(int i)
{
  char path[300], err[200];
  unsigned lost;
  Doc *d;
  if (ntpl < 0) scan_templates();
  if (i < 0 || i >= ntpl) return;
  snprintf(path, sizeof path, "%s/%s", TPLDIR, tplnames[i]);
  wp_backstage_hide();
  if (!(d = doc_load_file(path, &lost, err, sizeof err))) { wp_ask("OK", "%s couldn't be read.", file_part(path)); return; }
  template_to_new(d, path);
  wp_open_doc(d, "", 0);
}
/* (main.c's open_path) a template read opens as a new untitled document attached to it; a document
   whose styles follow its template's gets them. The path the window is to have */
const char *wp23_opened(Doc *d, const char *path)
{
  if (d->tplkind) { template_to_new(d, path); return ""; }
  if (d->linkstyles && d->attached && d->attached[0]) {
    unsigned lost;
    char err[200];
    Doc *t = doc_load_file(d->attached, &lost, err, sizeof err);
    if (t) { doc_copy_styles(d, t, NULL, 0); doc_free(t); doc_clear_undo(d); d->dirty = 0; }
  }
  return path;
}
/* File > Save as Template: a copy of the document as a .dotx (or .dotm), in the personal templates folder by default */
static void save_template(void)
{
  Doc *d = wp_doc();
  char path[300];
  const char *dp = wp_docpath();
  if (!d) return;
  wp_backstage_hide();
  make_dir(TPLDIR);
  snprintf(path, sizeof path, "%s/%s", TPLDIR, dp && dp[0] ? file_part(dp) : "Template");
  { char *dot = strrchr(path, '.'); if (dot && dot > path + strlen(TPLDIR)) *dot = 0; }
  strcat(path, ".dotx");
  if (!wp_file_request(1, "Save as Template", path, sizeof path, "#?.(dotx|dotm)")) return;
  if (!ends(path, ".dotx") && !ends(path, ".dotm") && strlen(path) + 6 < sizeof path) strcat(path, ".dotx");
  if (!template_save(d, path, ends(path, ".dotm") ? TK_DOTM : TK_DOTX)) wp_ask("OK", "%s couldn't be saved.", file_part(path));
  scan_templates();
}

/* ------------------------------------------------------------------ */
/* where building blocks come from: the document, the Building Blocks file, the attached template */
enum { SRC_DOC, SRC_BB, SRC_TPL, SRC_N };
static Doc *bbdoc, *tpldoc;
static char tplpath[300];
static int bbtried;
static Doc *src_doc(int s)
{
  Doc *d = wp_doc();
  if (s == SRC_DOC) return d;
  if (s == SRC_BB) {
    if (!bbdoc && !bbtried) {
      unsigned lost;
      char err[200];
      bbtried = 1;
      bbdoc = doc_load_file(BBFILE, &lost, err, sizeof err);
    }
    return bbdoc;
  }
  if (!d || !d->attached || !d->attached[0]) return NULL;
  if (!tpldoc || strcmp(tplpath, d->attached)) {
    unsigned lost;
    char err[200];
    doc_free(tpldoc);
    snprintf(tplpath, sizeof tplpath, "%s", d->attached);
    tpldoc = doc_load_file(tplpath, &lost, err, sizeof err);
  }
  return tpldoc;
}
/* a source's blocks written back to its file (the document's are saved with it) */
static void src_save(int s)
{
  if (s == SRC_BB) {
    if (!bbdoc && (bbdoc = doc_new())) bbtried = 1;
    if (bbdoc) { make_dir(TPLDIR); if (!template_save(bbdoc, BBFILE, TK_DOTX)) wp_ask("OK", "%s couldn't be saved.", "Building Blocks.dotx"); }
  } else if (s == SRC_TPL && tpldoc) {
    if (!template_save(tpldoc, tplpath, tpldoc->tplkind == TK_DOTM ? TK_DOTM : TK_DOTX)) wp_ask("OK", "%s couldn't be saved.", file_part(tplpath));
  } else if (s == SRC_DOC) set(wp_view(), MUIA_Quill_Changed, TRUE);
}
static const char *src_label(int s)
{
  static char b[120];
  if (s == SRC_DOC) return tr("This document");
  if (s == SRC_BB) return "Building Blocks.dotx";
  lat(file_part(tplpath[0] ? tplpath : "Template"), b, sizeof b);
  return b;
}
/* every block of the galleries asked for (gal -1: all), in source order */
#define MAXBB 200
typedef struct { u8 src; u16 i; } BRef;
static BRef refs[MAXBB];
static int nrefs;
static int collect(int gal)
{
  int s, i;
  nrefs = 0;
  for (s = 0; s < SRC_N; s++) {
    Doc *d = src_doc(s);
    if (!d || !d->gloss) continue;
    for (i = 0; i < d->gloss->n && nrefs < MAXBB; i++) {
      if (gal >= 0 && gallery_of(d->gloss->b[i].gallery) != gal) continue;
      if (!strcmp(d->gloss->b[i].gallery, "placeholder")) continue;      /* (content controls' placeholder texts) */
      refs[nrefs].src = (u8)s; refs[nrefs].i = (u16)i; nrefs++;
    }
  }
  return nrefs;
}
static Block *ref_block(const BRef *r) { Doc *d = src_doc(r->src); return d && d->gloss && r->i < d->gloss->n ? &d->gloss->b[r->i] : NULL; }
/* a block put in at the caret (the selection replaced) */
static void insert_ref(const BRef *r)
{
  Doc *d = wp_doc(), *o = src_doc(r->src);
  Pos c = get_pos(MUIA_Quill_Caret), a = get_pos(MUIA_Quill_Anchor);
  if (!d || !o) return;
  wp_story_close();
  if (pos_cmp(a, c)) { DoMethod(wp_view(), MUIM_Quill_Command, CMD_DELETE, 0); c = get_pos(MUIA_Quill_Caret); }
  if (!blocks_insert(d, &c, o, r->i)) { beep(); return; }
  changed_at(c);
}

/* ---- the ribbon's lists: the Quick Parts (and AutoText) blocks, then the commands */
#define NQP 16
static char qp_buf[NQP][64], at_buf[NQP][64];
static const char *qp_lab[NQP + 6], *at_lab[NQP + 3];
static BRef qp_ref[NQP], at_ref[NQP];
static int nqp, nat;
static RibbonList rl_quick, rl_auto, rl_object;
static const char *const obj_lab[] = { "Object...", "Text from File..." };
static Doc *lists_doc; static int lists_n = -1;
static void lists_make(void)
{
  Doc *d = wp_doc();
  int i, n;
  lists_doc = d; lists_n = d && d->gloss ? d->gloss->n : 0;
  n = collect(GAL_QUICKPARTS);
  for (i = 0, nqp = 0; i < n && nqp < NQP; i++) { lat(ref_block(&refs[i])->name, qp_buf[nqp], sizeof qp_buf[0]); qp_lab[nqp] = qp_buf[nqp]; qp_ref[nqp] = refs[i]; nqp++; }
  i = nqp;
  qp_lab[i++] = "AutoText"; qp_lab[i++] = "Document Property..."; qp_lab[i++] = "Building Blocks Organizer..."; qp_lab[i++] = "Save Selection to Quick Part Gallery...";
  rl_quick.labels = qp_lab; rl_quick.n = i; rl_quick.kind = PL_TEXT;
  n = collect(GAL_AUTOTEXT);
  for (i = 0, nat = 0; i < n && nat < NQP; i++) { lat(ref_block(&refs[i])->name, at_buf[nat], sizeof at_buf[0]); at_lab[nat] = at_buf[nat]; at_ref[nat] = refs[i]; nat++; }
  i = nat;
  at_lab[i++] = "Save Selection to AutoText Gallery...";
  rl_auto.labels = at_lab; rl_auto.n = i; rl_auto.kind = PL_TEXT;
  rl_object.labels = obj_lab; rl_object.n = 2; rl_object.kind = PL_TEXT;
}

/* ------------------------------------------------------------------ */
/* the Building Blocks Organizer */
static Object *bbw, *bb_list, *bb_prev;
static BRef bb_refs[MAXBB];
static int bb_n;
static void bb_fill(void)
{
  int i;
  if (!bbw) return;
  collect(-1);
  memcpy(bb_refs, refs, sizeof refs); bb_n = nrefs;
  set(bb_list, MUIA_List_Quiet, TRUE);
  DoMethod(bb_list, MUIM_List_Clear);
  for (i = 0; i < bb_n; i++) {
    Block *b = ref_block(&bb_refs[i]);
    char line[260], nm[64], ca[48];
    lat(b->name, nm, sizeof nm); lat(b->category, ca, sizeof ca);
    snprintf(line, sizeof line, "%s\t%s\t%s\t%s", nm, tr(gallery_label(gallery_of(b->gallery))), ca, src_label(bb_refs[i].src));
    DoMethod(bb_list, MUIM_List_InsertSingle, (ULONG)line, MUIV_List_Insert_Bottom);
  }
  set(bb_list, MUIA_List_Quiet, FALSE);
  set(bb_prev, MUIA_Text_Contents, (ULONG)"");
}
/* the list's lines are "name\tgallery\tcategory\tsource": shown in columns */
static ULONG __saveds bb_display(__reg("a0") struct Hook *h, __reg("a2") char **cols, __reg("a1") char *line)
{
  static char b[260], hb[4][40];
  static const char *const heads[4] = { "Name", "Gallery", "Category", "Template" };
  int k;
  if (!line) {
    for (k = 0; k < 4; k++) { snprintf(hb[k], sizeof hb[k], "\33b%s", tr(heads[k])); cols[k] = hb[k]; }
    return 0;
  }
  snprintf(b, sizeof b, "%s", line);
  cols[0] = b;
  for (k = 1; k < 4; k++) { char *t = strchr(cols[k - 1], '\t'); if (t) { *t = 0; cols[k] = t + 1; } else cols[k] = (char *)""; }
  return 0;
}
static struct Hook bb_hook;
static void bb_preview(void)
{
  LONG a = active(bb_list);
  Doc *b;
  char txt[300], *t;
  u32 n;
  if (a < 0 || a >= bb_n) return;
  txt[0] = 0;
  if ((b = blocks_doc(src_doc(bb_refs[a].src), bb_refs[a].i))) {
    if ((t = doc_text(b, doc_start(b), doc_end_pos(b), &n))) {
      u32 i;
      for (i = 0; i < n; i++) if (t[i] == '\n' || t[i] == '\t') t[i] = ' ';
      lat(t, txt, sizeof txt > 120 ? 120 : sizeof txt);
      free(t);
    }
    doc_free(b);
  }
  set(bb_prev, MUIA_Text_Contents, (ULONG)txt);
}
static void open_organizer(void)
{
  if (!bbw) {
    Object *ins, *ed, *del, *cl;
    bb_hook.h_Entry = (HOOKFUNC)bb_display;
    bbw = mkwin("Building Blocks Organizer", MAKE_ID('B','B','O','R'), VGroup, QDLG,
      Child, bb_list = ListviewObject, MUIA_CycleChain, 1, MUIA_Listview_DoubleClick, TRUE,
        MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
          MUIA_List_DestructHook, MUIV_List_DestructHook_String, MUIA_List_DisplayHook, (ULONG)&bb_hook, MUIA_List_Format, (ULONG)"BAR,BAR,BAR,", MUIA_List_Title, TRUE, End, End,
      Child, bb_prev = TextObject, TextFrame, MUIA_Background, MUII_TextBack, MUIA_Text_SetMin, FALSE, MUIA_Text_Contents, (ULONG)"", End,
      Child, HGroup, Child, ins = QButton(tr("_Insert")), Child, ed = QButton(tr("_Edit Properties...")), Child, del = QButton(tr("_Delete")),
        Child, HSpace(0), Child, cl = QButton(tr("_Close")), End,
    End);
    if (!bbw) return;
    bt(ins, ID_TP_BBINSERT); bt(ed, ID_TP_BBEDIT); bt(del, ID_TP_BBDELETE);
    DoMethod(cl, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)bbw, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(bb_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_TP_BBPICK);
    DoMethod(bb_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_TP_BBINSERT);
  }
  bb_fill();
  set(bbw, MUIA_Window_Open, TRUE);
}

/* ---- Create New Building Block (and Modify Building Block) */
static Object *nbw, *nb_name, *nb_gal, *nb_cat, *nb_desc, *nb_save, *nb_opt;
static int nb_edit = -1;                 /* bb_refs index being changed, -1: a new one from the selection */
static const char *nb_saves[4];
static int nb_srcs[3], nb_nsrc;
static void open_newblock(int gallery, int edit)
{
  static const char *gals[GAL_N + 1], *opts[] = { "Insert content only", "Insert content in its own paragraph", "Insert content in its own page", NULL };
  static const char *t_opt[4];
  Doc *d = wp_doc();
  int g;
  if (!d) return;
  if (edit < 0) {
    Pos c = get_pos(MUIA_Quill_Caret), a = get_pos(MUIA_Quill_Anchor);
    if (!pos_cmp(a, c)) { wp_ask("OK", "%s", "Select the text to keep as a building block first."); return; }
  }
  if (nbw) { set(nbw, MUIA_Window_Open, FALSE); DoMethod(wp_app(), OM_REMMEMBER, (ULONG)nbw); MUI_DisposeObject(nbw); nbw = NULL; }
  for (g = 0; g < GAL_OTHER; g++) gals[g] = tr(gallery_label(g));
  gals[g] = NULL;
  nb_nsrc = 0;
  if (edit < 0) {
    nb_srcs[nb_nsrc] = SRC_BB; nb_saves[nb_nsrc++] = src_label(SRC_BB);
    nb_srcs[nb_nsrc] = SRC_DOC; nb_saves[nb_nsrc++] = src_label(SRC_DOC);
    if (src_doc(SRC_TPL)) { nb_srcs[nb_nsrc] = SRC_TPL; nb_saves[nb_nsrc++] = src_label(SRC_TPL); }
  } else { nb_srcs[0] = bb_refs[edit].src; nb_saves[0] = src_label(bb_refs[edit].src); nb_nsrc = 1; }
  nb_saves[nb_nsrc] = NULL;
  nb_edit = edit;
  {
    Object *c = ColGroup(2),
      Child, Label2(tr("Name")), Child, nb_name = mkstring(60),
      Child, Label1(tr("Gallery")), Child, nb_gal = mkcycle(gals),
      Child, Label2(tr("Category")), Child, nb_cat = mkstring(46),
      Child, Label2(tr("Description")), Child, nb_desc = mkstring(120),
      Child, Label1(tr("Save in")), Child, nb_save = mkcycle(nb_saves),
      Child, Label1(tr("Options")), Child, nb_opt = mkcycle(trl(opts, t_opt)),
    End;
    if (!(nbw = wp_dialog(edit < 0 ? "Create New Building Block" : "Modify Building Block", MAKE_ID('N','B','B','K'), c, "_OK", ID_TP_NEWOK))) return;
  }
  if (edit < 0) {
    /* the name: the selection's first words */
    Pos c = get_pos(MUIA_Quill_Caret), a = get_pos(MUIA_Quill_Anchor);
    u32 n;
    char *t = doc_text(d, pos_cmp(a, c) < 0 ? a : c, pos_cmp(a, c) < 0 ? c : a, &n), nm[60];
    size_t k = 0;
    if (t) { while (k < n && k < 30 && t[k] != '\n') k++; t[k] = 0; snprintf(nm, sizeof nm, "%s", t); free(t); set_lat(nb_name, nm); }
    set(nb_gal, MUIA_Cycle_Active, gallery);
    set_lat(nb_cat, "General");
    set(nb_save, MUIA_Cycle_Active, 0);
  } else {
    Block *b = ref_block(&bb_refs[edit]);
    set_lat(nb_name, b->name); set_lat(nb_cat, b->category); set_lat(nb_desc, b->desc);
    g = gallery_of(b->gallery);
    set(nb_gal, MUIA_Cycle_Active, g < GAL_OTHER ? g : 0);
    set(nb_opt, MUIA_Cycle_Active, b->behavior);
  }
  set(nbw, MUIA_Window_Open, TRUE);
  set(nbw, MUIA_Window_ActiveObject, (ULONG)nb_name);
}
static void newblock_ok(void)
{
  Doc *d = wp_doc(), *o;
  char name[64], cat[48], desc[128];
  int src = nb_srcs[cyc(nb_save) % (ULONG)(nb_nsrc ? nb_nsrc : 1)], ok;
  if (!d) return;
  get_utf8(nb_name, name, sizeof name); get_utf8(nb_cat, cat, sizeof cat); get_utf8(nb_desc, desc, sizeof desc);
  if (!name[0]) { beep(); return; }
  set(nbw, MUIA_Window_Open, FALSE);
  if (src == SRC_BB && !src_doc(SRC_BB)) { bbdoc = doc_new(); bbtried = 1; }
  if (!(o = src_doc(src))) { beep(); return; }
  if (nb_edit >= 0) ok = blocks_set(o, bb_refs[nb_edit].i, name, (int)cyc(nb_gal), cat, desc, (int)cyc(nb_opt));
  else {
    Pos c = get_pos(MUIA_Quill_Caret), a = get_pos(MUIA_Quill_Anchor);
    if (blocks_find(o, name, (int)cyc(nb_gal)) >= 0 && wp_ask("_Yes|_No", "Do you want to redefine the building block \"%s\"?", get_str(nb_name)) != 1) return;
    ok = blocks_add(o, d, pos_cmp(a, c) < 0 ? a : c, pos_cmp(a, c) < 0 ? c : a, name, (int)cyc(nb_gal), cat, desc, (int)cyc(nb_opt)) >= 0;
  }
  if (!ok) { beep(); return; }
  src_save(src);
  lists_make();
  bb_fill();
}
static void bb_cmd(ULONG id)
{
  LONG a = active(bb_list);
  if (id == ID_TP_BBPICK) { bb_preview(); return; }
  if (a < 0 || a >= bb_n) { beep(); return; }
  if (id == ID_TP_BBINSERT) insert_ref(&bb_refs[a]);
  else if (id == ID_TP_BBEDIT) open_newblock(0, (int)a);
  else if (id == ID_TP_BBDELETE) {
    Block *b = ref_block(&bb_refs[a]);
    char nm[64];
    lat(b->name, nm, sizeof nm);
    if (wp_ask("_Delete|_Cancel", "Delete the building block \"%s\"?", nm) != 1) return;
    if (blocks_delete(src_doc(bb_refs[a].src), bb_refs[a].i)) src_save(bb_refs[a].src);
    lists_make();
    bb_fill();
  }
}
/* F3: the AutoText (or Quick Part) entry named by the word before the caret, in its place */
static void autotext_f3(void)
{
  Doc *d = wp_doc();
  Pos c = get_pos(MUIA_Quill_Caret), w;
  Para *p;
  char name[64];
  int s, k, g;
  if (!d || c.para >= d->np) return;
  p = d->p[c.para];
  w = c;
  while (w.off > 0 && p->t[w.off - 1] != ' ' && p->t[w.off - 1] != '\t') w.off--;
  if (w.off == c.off || c.off - w.off >= sizeof name) { beep(); return; }
  memcpy(name, p->t + w.off, c.off - w.off); name[c.off - w.off] = 0;
  for (g = 0; g < 2; g++)
    for (s = 0; s < SRC_N; s++) {
      Doc *o = src_doc(s);
      if (o && (k = blocks_find(o, name, g ? GAL_QUICKPARTS : GAL_AUTOTEXT)) >= 0) {
        doc_begin(d, c);
        doc_delete(d, w, c);
        if (!blocks_insert(d, &w, o, k)) beep();
        doc_end(d, w);
        changed_at(w);
        return;
      }
    }
  beep();
}

/* ------------------------------------------------------------------ */
/* Document Property: one of the document's properties as text at the caret */
static Object *prw, *pr_list;
static const char *const prop_names[] = { "Title", "Author", "Subject", "Keywords", "Comments", "Category", "Status", "Company", "Manager", "Publish Date", NULL };
static const char *prop_value(Doc *d, int i)
{
  static const int ip[] = { -1, -1, IP_SUBJECT, IP_KEYWORDS, IP_DESCRIPTION, IP_CATEGORY, IP_STATUS, IP_COMPANY, IP_MANAGER, IP_MODIFIED };
  if (i == 0) return d->title;
  if (i == 1) return d->author;
  return info_str(d, ip[i]);
}
static void open_props(void)
{
  Doc *d = wp_doc();
  int i;
  if (!d) return;
  if (!prw) {
    Object *c = VGroup, Child, pr_list = mklist(), End;
    if (!(prw = wp_dialog("Document Property", MAKE_ID('D','P','R','P'), c, "_Insert", ID_TP_PROPOK))) return;
    DoMethod(pr_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_TP_PROPOK);
  }
  set(pr_list, MUIA_List_Quiet, TRUE);
  DoMethod(pr_list, MUIM_List_Clear);
  for (i = 0; prop_names[i]; i++) {
    char line[200], v[120];
    lat(prop_value(d, i), v, sizeof v);
    snprintf(line, sizeof line, "%s: %s", tr(prop_names[i]), v[0] ? v : tr("(empty)"));
    DoMethod(pr_list, MUIM_List_InsertSingle, (ULONG)line, MUIV_List_Insert_Bottom);
  }
  set(pr_list, MUIA_List_Quiet, FALSE);
  set(pr_list, MUIA_List_Active, 0);
  set(prw, MUIA_Window_Open, TRUE);
}
static void props_ok(void)
{
  /* a DOCPROPERTY field (WP1, core/field.h) showing the property, as Word's Document Property quick parts do */
  static const char *const codes[] = { "TITLE", "AUTHOR", "SUBJECT", "KEYWORDS", "COMMENTS", "DOCPROPERTY Category", "DOCPROPERTY Status",
    "DOCPROPERTY Company", "DOCPROPERTY Manager", "SAVEDATE \\@ \"yyyy-MM-dd\"" };
  Doc *d = wp_doc();
  LONG a = active(pr_list);
  FieldEnv e;
  Pos c;
  set(prw, MUIA_Window_Open, FALSE);
  if (!d || a < 0 || a >= (LONG)(sizeof codes / sizeof codes[0])) return;
  wp_story_close();
  if (pos_cmp(get_pos(MUIA_Quill_Caret), get_pos(MUIA_Quill_Anchor))) DoMethod(wp_view(), MUIM_Quill_Command, CMD_DELETE, 0);
  c = get_pos(MUIA_Quill_Caret);
  field_env(&e);
  if (!field_insert(d, &c, codes[a], &e, NULL)) { beep(); return; }
  changed_at(c);
}

/* ------------------------------------------------------------------ */
/* Text from File and Object */
static Object *fiw, *fi_path, *fi_range, *obw, *ob_path, *ob_link, *ob_icon;
static void open_textfile(void)
{
  if (!fiw) {
    Object *br;
    Object *c = ColGroup(3),
      Child, Label2(tr("File name")), Child, fi_path = mkstring(250), Child, br = QButton(tr("_Browse...")),
      Child, Label2(tr("Range")), Child, fi_range = mkstring(60), Child, HSpace(0),
    End;
    if (!(fiw = wp_dialog("Insert File", MAKE_ID('I','N','F','L'), c, "_Insert", ID_TP_FILEOK))) return;
    bt(br, ID_TP_FILEBROWSE);
  }
  set(fiw, MUIA_Window_Open, TRUE);
  set(fiw, MUIA_Window_ActiveObject, (ULONG)fi_path);
}
static void browse_into(Object *str, const char *pattern)
{
  char path[300], pat[200];
  snprintf(path, sizeof path, "%s", get_str(str));
  if (pattern) snprintf(pat, sizeof pat, "%s", pattern); else doc_format_pattern(0, pat, sizeof pat);
  if (wp_file_request(0, "Insert File", path, sizeof path, pat)) set(str, MUIA_String_Contents, (ULONG)path);
}
static void insert_file(const char *path, const char *range)
{
  Doc *d = wp_doc();
  Pos c;
  char err[300], u[120];
  if (!d || !path[0]) { beep(); return; }
  wp_story_close();
  if (pos_cmp(get_pos(MUIA_Quill_Caret), get_pos(MUIA_Quill_Anchor))) DoMethod(wp_view(), MUIM_Quill_Command, CMD_DELETE, 0);
  c = get_pos(MUIA_Quill_Caret);
  wp_latin1_utf8(range ? range : "", u, sizeof u);
  set(wp_app(), MUIA_Application_Sleep, TRUE);
  if (!doc_insert_file(d, &c, path, u, err, sizeof err)) {
    char e2[300];
    set(wp_app(), MUIA_Application_Sleep, FALSE);
    lat(err, e2, sizeof e2);
    wp_ask("OK", "%s", e2);
    return;
  }
  set(wp_app(), MUIA_Application_Sleep, FALSE);
  changed_at(c);
}
static void open_object(void)
{
  if (!obw) {
    Object *br;
    Object *c = VGroup,
      Child, QTitleObject(tr("Create from File")),
      Child, HGroup, Child, Label2(tr("File name")), Child, ob_path = mkstring(250), Child, br = QButton(tr("_Browse...")), End,
      Child, HGroup, Child, ob_link = mkcheck(0), Child, LLabel1(tr("Link to file")), Child, ob_icon = mkcheck(0), Child, LLabel1(tr("Display as icon")), Child, HSpace(0), End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("A document's text and formatting, or a picture, goes in at the caret."), End,
    End;
    if (!(obw = wp_dialog("Object", MAKE_ID('O','B','J','T'), c, "_OK", ID_TP_OBJOK))) return;
    bt(br, ID_TP_OBJBROWSE);
    set(ob_link, MUIA_Disabled, TRUE); set(ob_icon, MUIA_Disabled, TRUE);    /* (no OLE: nothing to link or show as an icon) */
  }
  set(obw, MUIA_Window_Open, TRUE);
  set(obw, MUIA_Window_ActiveObject, (ULONG)ob_path);
}
static void object_ok(void)
{
  Doc *d = wp_doc();
  const char *path = get_str(ob_path);
  size_t n;
  u8 *data;
  u32 w, h;
  set(obw, MUIA_Window_Open, FALSE);
  if (!d || !path[0]) return;
  /* a picture goes in as one */
  if ((data = file_read(path, &n)) && img_info(data, (u32)n, &w, &h) != IMG_NONE) {
    int k = doc_add_image(d, data, (u32)n, 0, 0);
    if (k < 0) { beep(); return; }
    DoMethod(wp_view(), MUIM_Quill_Command, CMD_PICTURE, (ULONG)k);
    return;
  }
  free(data);
  insert_file(path, NULL);
}

/* ------------------------------------------------------------------ */
/* Templates and Add-ins */
static Object *taw, *ta_path, *ta_auto, *ta_list;
static void open_templates(void)
{
  Doc *d = wp_doc();
  int i;
  if (!d) return;
  if (!taw) {
    Object *att, *org;
    Object *c = VGroup,
      Child, QTitleObject(tr("Document template")),
      Child, HGroup, Child, ta_path = mkstring(250), Child, att = QButton(tr("_Attach...")), End,
      Child, HGroup, Child, ta_auto = mkcheck(0), Child, LLabel1(tr("Automatically update document styles")), Child, HSpace(0), End,
      Child, QTitleObject(tr("Personal templates")),
      Child, ta_list = ListviewObject, MUIA_Listview_Input, FALSE, MUIA_Listview_List, ListObject, ReadListFrame,
        MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Their building blocks, and the attached template's, are offered in Quick Parts."), End,
      Child, HGroup, Child, org = QButton(tr("_Organizer...")), Child, HSpace(0), End,
    End;
    if (!(taw = wp_dialog("Templates and Add-ins", MAKE_ID('T','P','A','D'), c, "_OK", ID_TP_TPLOK))) return;
    bt(att, ID_TP_ATTACH); bt(org, ID_TP_TPLORG);
  }
  set_lat(ta_path, d->attached ? d->attached : "");
  set(ta_auto, MUIA_Selected, d->linkstyles != 0);
  scan_templates();
  DoMethod(ta_list, MUIM_List_Clear);
  for (i = 0; i < ntpl; i++) DoMethod(ta_list, MUIM_List_InsertSingle, (ULONG)tplnames[i], MUIV_List_Insert_Bottom);
  DoMethod(ta_list, MUIM_List_InsertSingle, (ULONG)"Building Blocks.dotx", MUIV_List_Insert_Bottom);
  set(taw, MUIA_Window_Open, TRUE);
}
static void templates_ok(void)
{
  Doc *d = wp_doc();
  char path[300];
  int autoup;
  set(taw, MUIA_Window_Open, FALSE);
  if (!d) return;
  get_utf8(ta_path, path, sizeof path);
  autoup = chk(ta_auto);
  if (strcmp(path, d->attached ? d->attached : "") || autoup != d->linkstyles) {
    doc_attach_template(d, path);
    d->linkstyles = (u8)autoup;
    set(wp_view(), MUIA_Quill_Changed, TRUE);
  }
  if (autoup && path[0]) {
    /* the template's styles, now */
    unsigned lost;
    char err[200];
    Doc *t = doc_load_file(path, &lost, err, sizeof err);
    if (!t) { wp_ask("OK", "%s couldn't be read.", file_part(path)); return; }
    doc_copy_styles(d, t, NULL, 0);
    doc_free(t);
    DoMethod(wp_view(), MUIM_Quill_Relayout);
    wp_refresh();
  }
  lists_make();
}

/* ------------------------------------------------------------------ */
/* the ribbon, the menus and the commands */
static const Item qp_items[] = {
  { K_DROP, S_LARGE, 0, 0, "Quick Parts", ID_TP_QUICK, 0, 0, 0, 0, "Building blocks of text kept for reuse; AutoText, document properties and the Building Blocks Organizer", &rl_quick, "ci R 2 1 13 15 ca F 4 3 11 6 cd L 4 9 11 9 L 4 12 9 12", 0 },
  { K_DROP, S_SMALL, 0, 0, "AutoText", ID_TP_AUTOTEXT, 0, 0, F_NEWCOL, 0, "Text kept for reuse: pick one, or type its name and press F3", &rl_auto, "ca B A ci L 9 12 15 12 L 12 9 12 15", 0 },
  { K_DROP, S_SMALL, 0, 0, "Object", ID_TP_OBJECT, 0, 0, F_SPLIT, 0, "Another file's text and formatting, or a picture, at the caret", &rl_object, "ci R 1 1 14 14 ca F 3 3 7 7 cd L 9 4 12 4 L 3 10 12 10", ID_TP_OBJECT },
  { K_BUTTON, S_SMALL, IC_OPEN, 0, "Text from File", ID_TP_TEXTFILE, 0, 0, 0, 0, "A document's text and formatting at the caret (.docx, .txt and the other formats Quill reads)", NULL, NULL, 0 },
};
static const Item tpl_items[] = {
  { K_BUTTON, S_LARGE, IC_NEW, 0, "Document Template", ID_TP_TEMPLATES, 0, 0, 0, 0, "Templates and Add-ins: the template the document is attached to, and its styles", NULL, NULL, 0 },
};
static const WpGroup groups23[] = {
  { TAB_INSERT, 7, { "Quick Parts", G_COLS, qp_items, 4 } },
  { TAB_DEVELOPER, -1, { "Templates", G_COLS, tpl_items, 1 } },
};
static const WpMenu menus23[] = {
  { WM_FILE, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FILE, { NM_ITEM, (STRPTR)"Save as Template...", 0, 0, 0, (APTR)ID_TP_SAVETPL } },
  { WM_FILE, { NM_ITEM, (STRPTR)"Templates and Add-ins...", 0, 0, 0, (APTR)ID_TP_TEMPLATES } },
  { WM_INSERT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_ITEM, (STRPTR)"Quick Parts", 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Building Blocks Organizer...", 0, 0, 0, (APTR)ID_TP_ORGANIZER } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Save Selection to Quick Part Gallery...", 0, 0, 0, (APTR)ID_TP_SAVEQP } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Save Selection to AutoText Gallery...", 0, 0, 0, (APTR)ID_TP_SAVEAT } },
  { WM_INSERT, { NM_SUB, (STRPTR)"AutoText (F3)", 0, 0, 0, (APTR)ID_TP_F3 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Document Property...", 0, 0, 0, (APTR)ID_TP_DOCPROP } },
  { WM_INSERT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Object...", 0, 0, 0, (APTR)ID_TP_OBJECT } },
  { WM_INSERT, { NM_SUB, (STRPTR)"Text from File...", 0, 0, 0, (APTR)ID_TP_TEXTFILE } },
};

int wp23_command(ULONG id, LONG value)
{
  if (id >= ID_TP_PERSONAL0 && id < ID_TP_PERSONAL0 + WP23_NPERSONAL) { new_from_personal((int)(id - ID_TP_PERSONAL0)); return WPR_DONE; }
  if (!wp_doc()) return WPR_DONE;
  switch (id) {
  case ID_TP_SAVEAS: case ID_TP_SAVETPL: save_template(); break;
  case ID_TP_QUICK:
    if (value == WPV_NONE) { open_organizer(); return WPR_KEEPFOCUS; }
    if (value < nqp) { insert_ref(&qp_ref[value]); break; }
    value -= nqp;
    if (value == 0) { open_newblock(GAL_AUTOTEXT, -1); return WPR_KEEPFOCUS; }
    if (value == 1) { open_props(); return WPR_KEEPFOCUS; }
    if (value == 2) { open_organizer(); return WPR_KEEPFOCUS; }
    open_newblock(GAL_QUICKPARTS, -1); return WPR_KEEPFOCUS;
  case ID_TP_AUTOTEXT:
    if (value >= 0 && value < nat) { insert_ref(&at_ref[value]); break; }
    open_newblock(GAL_AUTOTEXT, -1); return WPR_KEEPFOCUS;
  case ID_TP_OBJECT: if (value == 1) { open_textfile(); return WPR_KEEPFOCUS; } open_object(); return WPR_KEEPFOCUS;
  case ID_TP_TEXTFILE: open_textfile(); return WPR_KEEPFOCUS;
  case ID_TP_ORGANIZER: open_organizer(); return WPR_KEEPFOCUS;
  case ID_TP_SAVEQP: open_newblock(GAL_QUICKPARTS, -1); return WPR_KEEPFOCUS;
  case ID_TP_SAVEAT: open_newblock(GAL_AUTOTEXT, -1); return WPR_KEEPFOCUS;
  case ID_TP_DOCPROP: open_props(); return WPR_KEEPFOCUS;
  case ID_TP_PROPOK: props_ok(); break;
  case ID_TP_F3: autotext_f3(); break;
  case ID_TP_BBINSERT: case ID_TP_BBEDIT: case ID_TP_BBDELETE: case ID_TP_BBPICK: bb_cmd(id); return id == ID_TP_BBINSERT ? WPR_DONE : WPR_KEEPFOCUS;
  case ID_TP_NEWOK: newblock_ok(); break;
  case ID_TP_FILEBROWSE: browse_into(fi_path, NULL); return WPR_KEEPFOCUS;
  case ID_TP_FILEOK: set(fiw, MUIA_Window_Open, FALSE); insert_file(get_str(fi_path), get_str(fi_range)); break;
  case ID_TP_OBJBROWSE: browse_into(ob_path, NULL); return WPR_KEEPFOCUS;
  case ID_TP_OBJOK: object_ok(); break;
  case ID_TP_TEMPLATES: open_templates(); return WPR_KEEPFOCUS;
  case ID_TP_ATTACH: {
    char path[300];
    snprintf(path, sizeof path, "%s", get_str(ta_path)[0] ? get_str(ta_path) : TPLDIR "/");
    if (wp_file_request(0, "Attach Template", path, sizeof path, "#?.(dotx|dotm|docx)")) set(ta_path, MUIA_String_Contents, (ULONG)path);
    return WPR_KEEPFOCUS; }
  case ID_TP_TPLORG: { char path[300]; get_utf8(ta_path, path, sizeof path); wp13_organizer(path); return WPR_KEEPFOCUS; }
  }
  return WPR_DONE;
}
/* the Quick Parts lists follow the document's (and its template's) building blocks */
static void wp23_state(ULONG *on, const WpCtx *c)
{
  if (c->doc != lists_doc || (c->doc && (c->doc->gloss ? c->doc->gloss->n : 0) != lists_n)) lists_make();
}
static ULONG wp23_key(UWORD code, UWORD qual, const WpCtx *c)
{
  if (code == 0x52 && !(qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT | IEQUALIFIER_CONTROL | IEQUALIFIER_LALT | IEQUALIFIER_RALT))) return ID_TP_F3;   /* F3 */
  return 0;
}
static void wp23_init(void) { scan_templates(); lists_make(); }
static void wp23_cleanup(void)
{
  doc_free(bbdoc); bbdoc = NULL;
  doc_free(tpldoc); tpldoc = NULL;
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp23_package = {
  "WP23", ID_WP23_FIRST, ID_WP23_LAST,
  wp23_command, wp23_state, NULL, NULL, wp23_key, wp23_init, wp23_cleanup,
  groups23, (int)(sizeof groups23 / sizeof groups23[0]),
  menus23, (int)(sizeof menus23 / sizeof menus23[0])
};
