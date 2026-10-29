/*
 * wp_proof.c - WP8 Proofing: grammar, thesaurus, AutoCorrect, AutoFormat, Read Aloud (docs/wordcraft-gap.md).
 * This file belongs to WP8: only that package edits it. Its commands are ID_WP8_FIRST .. ID_WP8_LAST
 * (ids.h); how the hooks below are called: wphooks.h.
 *
 * The Spelling and Grammar window (Review > Spelling & Grammar, F7): from mistake to mistake, each
 * word checked by its own language's dictionary (WP31) and English text by the grammar rules of
 * core/grammar.c; grammar marked as you type with a blue double line (style issues dotted). The
 * Thesaurus window (Shift-F7) and Synonyms on the right-button menu (core/thesaurus.c, the
 * WordNet-made dict/th_en.dat). AutoCorrect and AutoFormat as you type (core/autocorrect.c) with the
 * AutoCorrect Options window and its exceptions, the AutoCorrect submenu for a misspelt word, and
 * Format > AutoFormat Now. Read Aloud through translator.library and narrator.device, a sentence at a
 * time, the sentence being read selected; a message if the Amiga hasn't them.
 * Settings: PROGDIR:Quill.proof (checking) and PROGDIR:Quill.autocorrect (the lists and options).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/inputevent.h>
#include <devices/narrator.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/translator.h>
#include <clib/alib_protos.h>
#include <libraries/iffparse.h>
#include "wphooks.h"
#include "lang.h"
#include "wp_proof.h"
#include "../core/spell.h"
#include "../core/proflang.h"
#include "../core/thesaurus.h"
#include "../core/autocorrect.h"

#undef Label1
#undef Label2
#define Label1(l)  QLabel(l, MUIO_Label_SingleFrame)
#define Label2(l)  QLabel(l, MUIO_Label_DoubleFrame)

enum {
  ID_PR_SPELLGRAM = ID_WP8_FIRST, ID_PR_THES, ID_PR_READ, ID_PR_ACOPTS, ID_PR_AFNOW, ID_PR_LIVESPELL, ID_PR_LIVEGRAM,
  ID_PR_SG_IGNORE, ID_PR_SG_IGNOREALL, ID_PR_SG_ADD, ID_PR_SG_CHANGE, ID_PR_SG_CHANGEALL, ID_PR_SG_AUTOCORR, ID_PR_SG_PICK,
  ID_PR_SG_OPTS,
  ID_PR_TH_LOOKUP, ID_PR_TH_MEANING, ID_PR_TH_SYN, ID_PR_TH_INSERT,
  ID_PR_AC_OK, ID_PR_AC_ADD, ID_PR_AC_DEL, ID_PR_AC_PICK, ID_PR_AC_EXC, ID_PR_EX_ADD, ID_PR_EX_DEL, ID_PR_EX_KIND,
  ID_PR_CTX_SYN, ID_PR_CTX_GSUG, ID_PR_CTX_GIGN, ID_PR_CTX_AC, ID_PR_SPEAK, ID_PR_SG_CLOSE
};
#define WP8S_READING 1                    /* the state bit: Read Aloud is speaking */
#define PREFS8 "PROGDIR:Quill.proof"
#define ACFILE "PROGDIR:Quill.autocorrect"

struct Library *TranslatorBase;
static AutoCorrect ac;
static int ac_ready;
static int chk_grammar = 1, chk_style = 0;   /* the Spelling and Grammar window's (and live grammar's) rules */
static unsigned gen = 1;                     /* bumped when the rules change: the marks' cache is stale */

/* ---- small helpers */
static void bt(Object *o, ULONG id) { if (o) DoMethod(o, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, id); }
static int is_open(Object *w) { ULONG o = 0; if (w) get(w, MUIA_Window_Open, &o); return o != 0; }
static int chk(Object *o) { ULONG v = 0; if (o) get(o, MUIA_Selected, &v); return v != 0; }
static const char *str(Object *o) { char *s = NULL; if (o) get(o, MUIA_String_Contents, &s); return s ? s : ""; }
static LONG active(Object *l) { LONG a = -1; if (l) get(l, MUIA_List_Active, &a); return a; }
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
static Object *mkcheck(const char *label, int on, Object **box)
{
  /* (a text, not a label: "_italic_" must not become a key's underline) */
  return HGroup, Child, *box = CheckMark(on), Child, TextObject, MUIA_Text_Contents, (ULONG)label, MUIA_Text_SetMax, FALSE, End, Child, HSpace(0), End;
}
static void lat(const char *u, char *out, size_t cap) { utf8_to_latin1(u, strlen(u), out, cap); }
/* the selection, or the caret twice */
static void sel_range(Pos *a, Pos *b)
{
  Pos *c = NULL, *an = NULL;
  get(wp_view(), MUIA_Quill_Caret, &c); get(wp_view(), MUIA_Quill_Anchor, &an);
  *a = c ? *c : doc_start(wp_doc()); *b = an ? *an : *a;
  if (pos_cmp(*a, *b) > 0) { Pos t = *a; *a = *b; *b = t; }
}
static void sel_set(Pos a, Pos b) { Pos ab[2]; ab[0] = a; ab[1] = b; DoMethod(wp_view(), MUIM_Quill_Command, CMD_SELECT, (ULONG)ab); }
/* the word at (or just before) p: its extent; 0 if none */
static int word_at(Doc *d, Pos p, Pos *a, Pos *b)
{
  Para *q;
  u32 k = 0, ws = 0, we = 0;
  if (p.para >= d->np) return 0;
  q = d->p[p.para];
  while (spell_word_at(q->t, q->len, k, &ws, &we) && we < p.off) k = we;
  if (we <= ws || ws > p.off || we < p.off) return 0;
  a->para = b->para = p.para; a->off = ws; b->off = we;
  return 1;
}

/* ---- settings */
static void load_prefs8(void)
{
  FILE *f = fopen(PREFS8, "r");
  char line[64];
  if (!f) return;
  while (fgets(line, sizeof line, f)) {
    int v = atoi(line + strcspn(line, " ") + 1);
    if (!strncmp(line, "livespell ", 10)) proof_live_spelling = v;
    else if (!strncmp(line, "livegram ", 9)) proof_live_grammar = v;
    else if (!strncmp(line, "grammar ", 8)) chk_grammar = v;
    else if (!strncmp(line, "style ", 6)) chk_style = v;
  }
  fclose(f);
}
static void save_prefs8(void)
{
  FILE *f = fopen(PREFS8, "w");
  if (!f) return;
  fprintf(f, "livespell %d\nlivegram %d\ngrammar %d\nstyle %d\n", proof_live_spelling, proof_live_grammar, chk_grammar, chk_style);
  fclose(f);
}
static void ac_setup(void)
{
  FILE *f;
  if (ac_ready) return;
  ac_init(&ac);
  ac_ready = 1;
  if ((f = fopen(ACFILE, "rb"))) {
    long n;
    char *buf;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n > 0 && (buf = (char *)malloc((size_t)n))) {
      if (fread(buf, 1, (size_t)n, f) == (size_t)n) ac_load(&ac, buf, (size_t)n);
      free(buf);
    }
    fclose(f);
  }
}
static void ac_store(void)
{
  Buf b;
  FILE *f;
  buf_init(&b);
  ac_save(&ac, &b);
  if (!b.oom && (f = fopen(ACFILE, "wb"))) { fwrite(b.data, 1, b.len, f); fclose(f); }
  buf_free(&b);
}
static unsigned gopts(void) { return (chk_grammar ? GRO_GRAMMAR : 0) | (chk_style ? GRO_STYLE : 0); }

/* ---- grammar marks as you type: each paragraph's issues kept by its version */
#define GCACHE 24
#define GMAX 16
static struct { Para *p; u32 ver; unsigned gen; int n; GramIssue is[GMAX]; } gc[GCACHE];
static int gnext;
int wp8_issues(Para *p, const GramIssue **out)
{
  int i, n, k;
  GramIssue all[GMAX];
  if (!proof_live_grammar || !gopts() || !p || !p->len) return 0;
  for (i = 0; i < GCACHE; i++) if (gc[i].p == p && gc[i].ver == p->ver && gc[i].gen == gen) { *out = gc[i].is; return gc[i].n; }
  n = grammar_check(p->t, p->len, gopts(), all, GMAX);
  i = gnext; gnext = (gnext + 1) % GCACHE;
  gc[i].p = p; gc[i].ver = p->ver; gc[i].gen = gen; gc[i].n = 0;
  for (k = 0; k < n; k++) if (grammar_applies(p, all[k].a) && !grammar_ignored(&all[k], p->t)) gc[i].is[gc[i].n++] = all[k];
  *out = gc[i].is;
  return gc[i].n;
}
void wp8_typed(Doc *d, Pos *caret, const char *s, u32 n, int rid)
{
  ac_setup();
  if (ac.opts) ac_typed(&ac, d, caret, s, n, rid);
}

/* ---- the Spelling and Grammar window */
static Object *sgwin, *sg_what, *sg_ctx, *sg_list, *sg_to, *sg_btn[6], *sg_gram, *sg_style;
static int sg_kind;                       /* 0 nothing found, 1 a word not in the dictionary, 2 a grammar issue */
static Pos sg_a, sg_b;
static GramIssue sg_gi;
static char sg_bad[128];
static Spell *sg_spell;
/* the paragraph round a..b for the window (Latin-1), the flagged part in bold */
static void show_context(Pos a, Pos b)
{
  static char out[400];
  Doc *d = wp_doc();
  Para *q = d->p[a.para];
  u32 s = a.off > 60 ? a.off - 60 : 0, e = b.off + 60 < q->len ? b.off + 60 : q->len;
  char part[300];
  size_t o = 0;
  while (s > 0 && ((u8)q->t[s] & 0xC0) == 0x80) s--;
  while (e < q->len && ((u8)q->t[e] & 0xC0) == 0x80) e++;
  utf8_to_latin1(q->t + s, a.off - s, part, sizeof part); o = (size_t)sprintf(out, "%s%s\33b", s ? "..." : "", part);
  utf8_to_latin1(q->t + a.off, b.off - a.off, part, sizeof part);
  if (b.off == a.off || !part[0]) strcpy(part, " ");
  o += (size_t)sprintf(out + o, "%s\33n", part);
  utf8_to_latin1(q->t + b.off, e - b.off, part, sizeof part);
  sprintf(out + o, "%s%s", part, e < q->len ? "..." : "");
  { char *t; for (t = out; *t; t++) if (*t == '\t' || *t == '\v') *t = ' '; }
  set(sg_ctx, MUIA_Text_Contents, (ULONG)out);
}
static void sg_buttons(void)
{
  set(sg_btn[0], MUIA_Disabled, !sg_kind);                    /* Ignore Once */
  set(sg_btn[1], MUIA_Disabled, !sg_kind);                    /* Ignore All */
  set(sg_btn[2], MUIA_Disabled, sg_kind != 1);                /* Add to Dictionary */
  set(sg_btn[3], MUIA_Disabled, !sg_kind || (sg_kind == 2 && !sg_gi.has_sug));   /* Change */
  set(sg_btn[4], MUIA_Disabled, sg_kind != 1);                /* Change All */
  set(sg_btn[5], MUIA_Disabled, sg_kind != 1);                /* AutoCorrect */
}
/* the next mistake from the caret (from_end: from the selection's end): a word its dictionary doesn't
   know, or a grammar issue, whichever comes first */
static void sg_find(int from_end)
{
  Doc *d = wp_doc();
  Pos a, b, from, sa, sb, ga, gb;
  GramIssue gi;
  int s, g;
  static char what[200];
  sel_range(&a, &b);
  from = from_end ? b : a;
  DoMethod(sg_list, MUIM_List_Clear);
  s = proof_next_spelling((Spell *)wp_spell(), d, from, &sa, &sb);
  g = gopts() ? grammar_next(d, from, gopts(), &ga, &gb, &gi) : 0;
  if (s && g) {
    /* (the one after the caret first, then the nearer) */
    int sw = pos_cmp(sa, from) < 0, gw = pos_cmp(ga, from) < 0;
    if (sw != gw) { if (sw) s = 0; else g = 0; }
    else if (pos_cmp(ga, sa) < 0) s = 0; else g = 0;
  }
  if (!s && !g) {
    sg_kind = 0;
    set(sg_what, MUIA_Text_Contents, (ULONG)tr("\33cThe spelling and grammar check is complete."));
    set(sg_ctx, MUIA_Text_Contents, (ULONG)"");
    set(sg_to, MUIA_String_Contents, (ULONG)"");
    sg_buttons();
    return;
  }
  if (s) {
    char *sug[10], l[130], name[80];
    int n, i;
    u32 len = sb.off - sa.off;
    const CharFmt *f = para_fmt_at(d->p[sa.para], sa.off);
    sg_kind = 1; sg_a = sa; sg_b = sb;
    if (len >= sizeof sg_bad) len = sizeof sg_bad - 1;
    memcpy(sg_bad, d->p[sa.para]->t + sa.off, len); sg_bad[len] = 0;
    sg_spell = proof_spell((Spell *)wp_spell(), f);
    strncpy(name, tr(plang_name(plang_of(f))), sizeof name - 1); name[sizeof name - 1] = 0;
    sprintf(what, tr("\33cNot in Dictionary (%s):"), name);
    set(sg_what, MUIA_Text_Contents, (ULONG)what);
    n = sg_spell ? spell_suggest(sg_spell, sg_bad, (u32)strlen(sg_bad), sug, 10) : 0;
    for (i = 0; i < n; i++) { lat(sug[i], l, sizeof l); DoMethod(sg_list, MUIM_List_InsertSingle, (ULONG)l, MUIV_List_Insert_Bottom); free(sug[i]); }
    if (!n) { DoMethod(sg_list, MUIM_List_InsertSingle, (ULONG)tr("(no suggestions)"), MUIV_List_Insert_Bottom); lat(sg_bad, l, sizeof l); set(sg_to, MUIA_String_Contents, (ULONG)l); }
    else set(sg_list, MUIA_List_Active, 0);
  } else {
    char l[130];
    sg_kind = 2; sg_a = ga; sg_b = gb; sg_gi = gi;
    sprintf(what, "\33c%s", tr(grammar_message(gi.kind)));
    set(sg_what, MUIA_Text_Contents, (ULONG)what);
    if (gi.has_sug) {
      if (gi.sug[0]) lat(gi.sug, l, sizeof l); else strcpy(l, tr("(delete)"));
      DoMethod(sg_list, MUIM_List_InsertSingle, (ULONG)l, MUIV_List_Insert_Bottom);
      set(sg_list, MUIA_List_Active, 0);
    } else {
      DoMethod(sg_list, MUIM_List_InsertSingle, (ULONG)tr("(no suggestions)"), MUIV_List_Insert_Bottom);
      set(sg_to, MUIA_String_Contents, (ULONG)"");
    }
  }
  sel_set(sg_a, sg_b);
  show_context(sg_a, sg_b);
  sg_buttons();
}
void wp8_spelling(void)
{
  if (!wp_spell() && !plang_spell(plang_default)) { wp_ask("OK", "%s", "Quill has no dictionary: it should be in\nthe Dict drawer next to Quill."); return; }
  if (!sgwin) {
    Object *close, *c = VGroup, QDLG,
      Child, sg_what = TextObject, MUIA_Text_Contents, (ULONG)"", End,
      Child, sg_ctx = TextObject, TextFrame, MUIA_Background, MUII_TextBack, MUIA_Text_Contents, (ULONG)"", MUIA_Text_SetMin, FALSE, End,
      Child, HGroup,
        Child, VGroup,
          Child, HGroup, Child, Label2(tr("Change to:")), Child, sg_to = StringObject, StringFrame, MUIA_String_MaxLen, 128, MUIA_CycleChain, 1, End, End,
          Child, QLabel(tr("Suggestions:"), MUIO_Label_LeftAligned), Child, sg_list = mklist(),
        End,
        Child, VGroup, MUIA_HorizWeight, 0,
          Child, sg_btn[0] = QButton(tr("_Ignore Once")), Child, sg_btn[1] = QButton(tr("Ig_nore All")),
          Child, sg_btn[2] = QButton(tr("_Add to Dictionary")), Child, sg_btn[3] = QButton(tr("_Change")),
          Child, sg_btn[4] = QButton(tr("Change A_ll")), Child, sg_btn[5] = QButton(tr("Auto_Correct")),
          Child, VSpace(0),
        End,
      End,
      Child, HGroup,
        Child, mkcheck(tr("Check grammar"), chk_grammar, &sg_gram),
        Child, mkcheck(tr("Style (passive voice, wordy phrases)"), chk_style, &sg_style),
        Child, close = QButton(tr("Cl_ose")),
      End,
    End;
    if (!(sgwin = mkwin("Spelling and Grammar", MAKE_ID('W','0','8','S'), c))) return;
    bt(sg_btn[0], ID_PR_SG_IGNORE); bt(sg_btn[1], ID_PR_SG_IGNOREALL); bt(sg_btn[2], ID_PR_SG_ADD);
    bt(sg_btn[3], ID_PR_SG_CHANGE); bt(sg_btn[4], ID_PR_SG_CHANGEALL); bt(sg_btn[5], ID_PR_SG_AUTOCORR);
    DoMethod(close, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)sgwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(sg_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PR_SG_PICK);
    DoMethod(sg_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PR_SG_CHANGE);
    DoMethod(sg_to, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PR_SG_CHANGE);
    DoMethod(sg_gram, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PR_SG_OPTS);
    DoMethod(sg_style, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PR_SG_OPTS);
  }
  set(sgwin, MUIA_Window_Open, TRUE);
  sg_find(0);
}
static void sg_pick(void)
{
  char *pick = NULL;
  DoMethod(sg_list, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (ULONG)&pick);
  if (pick && pick[0] != '(') set(sg_to, MUIA_String_Contents, (ULONG)pick);
  else if (pick && sg_kind == 2 && sg_gi.has_sug && !sg_gi.sug[0]) set(sg_to, MUIA_String_Contents, (ULONG)"");
}
static void sg_command(ULONG id)
{
  char u[300];
  if (!sg_kind) { sg_find(0); return; }
  switch (id) {
  case ID_PR_SG_CHANGE: case ID_PR_SG_CHANGEALL: case ID_PR_SG_AUTOCORR:
    wp_latin1_utf8(str(sg_to), u, sizeof u);
    if (sg_kind == 2 && sg_gi.has_sug && !u[0] && sg_gi.sug[0]) strcpy(u, sg_gi.sug);
    if (sg_kind == 1 && !u[0]) { DisplayBeep(NULL); return; }
    sel_set(sg_a, sg_b);
    if (id == ID_PR_SG_CHANGEALL && sg_kind == 1) {
      struct QFind q;
      memset(&q, 0, sizeof q);
      q.what = sg_bad; q.with = u; q.flags = FIND_CASE | FIND_WORD;
      DoMethod(wp_view(), MUIM_Quill_Command, CMD_REPLACEALL, (ULONG)&q);
    } else DoMethod(wp_view(), MUIM_Quill_Command, CMD_REPLACE, (ULONG)u);
    if (id == ID_PR_SG_AUTOCORR) { ac_setup(); ac_add(&ac, sg_bad, u); ac_store(); }
    break;
  case ID_PR_SG_IGNORE: sel_set(sg_a, sg_b); break;
  case ID_PR_SG_IGNOREALL:
    if (sg_kind == 1 && sg_spell) spell_add(sg_spell, sg_bad, (u32)strlen(sg_bad));
    if (sg_kind == 2) { grammar_ignore(&sg_gi, wp_doc()->p[sg_a.para]->t); gen++; }
    sel_set(sg_a, sg_b);
    break;
  case ID_PR_SG_ADD: {
    FILE *f = fopen("PROGDIR:Quill.words", "a");
    if (sg_spell) spell_add(sg_spell, sg_bad, (u32)strlen(sg_bad));
    if (f) { fprintf(f, "%s\n", sg_bad); fclose(f); }
    sel_set(sg_a, sg_b);
    break; }
  }
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_REFRESH, 0);
  sg_find(1);
}

/* ---- the thesaurus */
static Thes *thes; static char thes_tag[16];
static ThesResult thr;
static Object *thwin, *th_word, *th_means, *th_syns, *th_head;
static Pos th_a, th_b; static int th_fromdoc;
/* the thesaurus for the caret's language: PROGDIR:Dict/th_xx_YY.dat, th_xx.dat, or the English one */
static Thes *thes_for(u16 lang)
{
  const char *t = plang_tag(lang);
  char path[64];
  if (thes && !strcmp(thes_tag, t)) return thes;
  thes_close(thes); thes = NULL;
  strncpy(thes_tag, t, sizeof thes_tag - 1); thes_tag[sizeof thes_tag - 1] = 0;
  if (strlen(t) == 5) {
    sprintf(path, "PROGDIR:Dict/th_%c%c_%c%c.dat", t[0], t[1], t[3], t[4]);
    if ((thes = thes_open(path))) return thes;
  }
  if (t[0] && t[1]) { sprintf(path, "PROGDIR:Dict/th_%c%c.dat", t[0], t[1]); if ((thes = thes_open(path))) return thes; }
  return thes = thes_open("PROGDIR:Dict/th_en.dat");
}
static u16 caret_lang(void)
{
  Pos *c = wp_caret();
  Doc *d = wp_doc();
  return c && c->para < d->np ? plang_of(para_fmt_at(d->p[c->para], c->off)) : plang_default;
}
static void th_fill_syns(void)
{
  LONG m = active(th_means);
  int k;
  DoMethod(th_syns, MUIM_List_Clear);
  if (m < 0 || m >= thr.n) return;
  for (k = 0; k < thr.m[m].nterms; k++) { char l[100]; lat(thr.m[m].terms[k], l, sizeof l); DoMethod(th_syns, MUIM_List_InsertSingle, (ULONG)l, MUIV_List_Insert_Bottom); }
  set(th_syns, MUIA_List_Active, 0);
}
static void th_lookup(const char *utf8)
{
  static char head[160];
  Thes *t = thes_for(caret_lang());
  int i;
  thes_result_free(&thr);
  DoMethod(th_means, MUIM_List_Clear); DoMethod(th_syns, MUIM_List_Clear);
  if (!t) { set(th_head, MUIA_Text_Contents, (ULONG)tr("No thesaurus: dict/th_en.dat is missing.")); return; }
  if (!thes_lookup(t, utf8, (u32)strlen(utf8), &thr)) { set(th_head, MUIA_Text_Contents, (ULONG)tr("No results.")); return; }
  { char l[60]; lat(thr.head, l, sizeof l); sprintf(head, "\33b%s", l); }
  set(th_head, MUIA_Text_Contents, (ULONG)head);
  for (i = 0; i < thr.n; i++) {
    char l[160], first[100];
    lat(thr.m[i].nterms ? thr.m[i].terms[0] : "", first, sizeof first);
    sprintf(l, "(%s) %s", tr(thr.m[i].pos), first);
    DoMethod(th_means, MUIM_List_InsertSingle, (ULONG)l, MUIV_List_Insert_Bottom);
  }
  set(th_means, MUIA_List_Active, 0);
  th_fill_syns();
}
static void open_thesaurus(void)
{
  Doc *d = wp_doc();
  Pos a, b;
  char w[128], l[128];
  if (!thwin) {
    Object *look, *ins, *close, *c = VGroup, QDLG,
      Child, HGroup, Child, Label2(tr("Search for:")), Child, th_word = StringObject, StringFrame, MUIA_String_MaxLen, 60, MUIA_CycleChain, 1, End,
        Child, look = QButton(tr("_Look Up")), End,
      Child, th_head = TextObject, MUIA_Text_Contents, (ULONG)"", End,
      Child, HGroup,
        Child, VGroup, Child, QLabel(tr("Meanings:"), MUIO_Label_LeftAligned), Child, th_means = mklist(), End,
        Child, VGroup, Child, QLabel(tr("Synonyms:"), MUIO_Label_LeftAligned), Child, th_syns = mklist(), End,
      End,
      Child, HGroup, Child, ins = QButton(tr("_Insert")), Child, HSpace(0), Child, close = QButton(tr("Cl_ose")), End,
    End;
    if (!(thwin = mkwin("Thesaurus", MAKE_ID('W','0','8','T'), c))) return;
    bt(look, ID_PR_TH_LOOKUP); bt(ins, ID_PR_TH_INSERT);
    DoMethod(close, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)thwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(th_word, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PR_TH_LOOKUP);
    DoMethod(th_means, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PR_TH_MEANING);
    DoMethod(th_syns, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PR_TH_SYN);
  }
  /* the selected word, or the caret's */
  sel_range(&a, &b);
  th_fromdoc = 0;
  if ((a.para == b.para && b.off > a.off && b.off - a.off < sizeof w) || word_at(d, a, &a, &b)) {
    u32 n = b.off - a.off < sizeof w - 1 ? b.off - a.off : sizeof w - 1;
    memcpy(w, d->p[a.para]->t + a.off, n); w[n] = 0;
    th_a = a; th_b = b; th_fromdoc = 1;
  } else w[0] = 0;
  lat(w, l, sizeof l);
  set(th_word, MUIA_String_Contents, (ULONG)l);
  set(thwin, MUIA_Window_Open, TRUE);
  if (w[0]) th_lookup(w);
}
static void th_insert(void)
{
  char *pick = NULL, u[200];
  DoMethod(th_syns, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (ULONG)&pick);
  if (!pick) { DisplayBeep(NULL); return; }
  wp_latin1_utf8(pick, u, sizeof u);
  if (th_fromdoc) sel_set(th_a, th_b);
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_REPLACE, (ULONG)u);
  th_fromdoc = 0;
  set(thwin, MUIA_Window_Open, FALSE);
  set(wp_win(), MUIA_Window_Activate, TRUE);
}

/* ---- the AutoCorrect Options window */
static Object *acwin, *ac_chk[6], *af_chk[9], *ac_from, *ac_to, *ac_list;
static Object *exwin, *ex_kind, *ex_list, *ex_word;
static const unsigned ac_bits[6] = { AC_TWOCAPS, AC_SENTENCE, AC_CELLS, AC_DAYS, AC_CAPSLOCK, AC_REPLACE };
static const char *const ac_names[6] = { "Correct TWo INitial CApitals", "Capitalize first letter of sentences", "Capitalize first letter of table cells",
                                         "Capitalize names of days", "Correct accidental usage of cAPS LOCK key", "Replace text as you type" };
static const unsigned af_bits[9] = { AF_QUOTES, AF_FRACTIONS, AF_BOLDITALIC, AF_LINKS, AF_ORDINALS, AF_DASHES, AF_BULLETS, AF_NUMBERS, AF_BORDERS };
static const char *const af_names[9] = { "\"Straight quotes\" with \"smart quotes\"", "Fractions (1/2) with fraction character (\275)",
                                         "*Bold* and _italic_ with real formatting", "Internet and network paths with hyperlinks",
                                         "Ordinals (1st) with superscript", "Hyphens (--) with dash", "Automatic bulleted lists",
                                         "Automatic numbered lists", "Border lines" };
static void ac_fill(void)
{
  int i;
  set(ac_list, MUIA_List_Quiet, TRUE);
  DoMethod(ac_list, MUIM_List_Clear);
  for (i = 0; i < ac.n; i++) {
    char a[80], b[80], l[170];
    lat(ac.from[i], a, sizeof a); lat(ac.to[i], b, sizeof b);
    sprintf(l, "%s  \273  %s", a, b);
    DoMethod(ac_list, MUIM_List_InsertSingle, (ULONG)l, MUIV_List_Insert_Bottom);
  }
  set(ac_list, MUIA_List_Quiet, FALSE);
}
static void open_acopts(void)
{
  int i;
  ac_setup();
  if (!acwin) {
    Object *add, *del, *exc, *g1, *g2, *c;
    static const char *pages[3];
    pages[0] = tr("AutoCorrect"); pages[1] = tr("AutoFormat As You Type"); pages[2] = NULL;
    g1 = VGroup,
      Child, mkcheck(tr(ac_names[0]), 0, &ac_chk[0]), Child, mkcheck(tr(ac_names[1]), 0, &ac_chk[1]),
      Child, mkcheck(tr(ac_names[2]), 0, &ac_chk[2]), Child, mkcheck(tr(ac_names[3]), 0, &ac_chk[3]),
      Child, mkcheck(tr(ac_names[4]), 0, &ac_chk[4]), Child, mkcheck(tr(ac_names[5]), 0, &ac_chk[5]),
      Child, HGroup, Child, Label2(tr("Replace:")), Child, ac_from = StringObject, StringFrame, MUIA_String_MaxLen, 60, MUIA_CycleChain, 1, End,
        Child, Label2(tr("With:")), Child, ac_to = StringObject, StringFrame, MUIA_String_MaxLen, 60, MUIA_CycleChain, 1, End, End,
      Child, ac_list = mklist(),
      Child, HGroup, Child, add = QButton(tr("_Add")), Child, del = QButton(tr("_Delete")), Child, HSpace(0),
        Child, exc = QButton(tr("_Exceptions...")), End,
    End;
    g2 = VGroup,
      Child, mkcheck(tr(af_names[0]), 0, &af_chk[0]), Child, mkcheck(tr(af_names[1]), 0, &af_chk[1]),
      Child, mkcheck(tr(af_names[2]), 0, &af_chk[2]), Child, mkcheck(tr(af_names[3]), 0, &af_chk[3]),
      Child, mkcheck(tr(af_names[4]), 0, &af_chk[4]), Child, mkcheck(tr(af_names[5]), 0, &af_chk[5]),
      Child, mkcheck(tr(af_names[6]), 0, &af_chk[6]), Child, mkcheck(tr(af_names[7]), 0, &af_chk[7]),
      Child, mkcheck(tr(af_names[8]), 0, &af_chk[8]), Child, VSpace(0),
    End;
    c = VGroup, QDLG, Child, RegisterGroup(pages), MUIA_CycleChain, 1, Child, g1, Child, g2, End, End;
    if (!(acwin = wp_dialog("AutoCorrect", MAKE_ID('W','0','8','A'), c, "_OK", ID_PR_AC_OK))) return;
    bt(add, ID_PR_AC_ADD); bt(del, ID_PR_AC_DEL); bt(exc, ID_PR_AC_EXC);
    DoMethod(ac_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PR_AC_PICK);
  }
  for (i = 0; i < 6; i++) set(ac_chk[i], MUIA_Selected, (ac.opts & ac_bits[i]) != 0);
  for (i = 0; i < 9; i++) set(af_chk[i], MUIA_Selected, (ac.opts & af_bits[i]) != 0);
  ac_fill();
  set(acwin, MUIA_Window_Open, TRUE);
}
static void ac_ok(void)
{
  int i;
  unsigned o = 0;
  for (i = 0; i < 6; i++) if (chk(ac_chk[i])) o |= ac_bits[i];
  for (i = 0; i < 9; i++) if (chk(af_chk[i])) o |= af_bits[i];
  ac.opts = o;
  ac_store();
  set(acwin, MUIA_Window_Open, FALSE);
}
static void ex_fill(void)
{
  int i, which = 0, n;
  char **v;
  get(ex_kind, MUIA_Cycle_Active, &which);
  v = which ? ac.exc2 : ac.exc; n = which ? ac.nexc2 : ac.nexc;
  DoMethod(ex_list, MUIM_List_Clear);
  for (i = 0; i < n; i++) { char l[80]; lat(v[i], l, sizeof l); DoMethod(ex_list, MUIM_List_InsertSingle, (ULONG)l, MUIV_List_Insert_Bottom); }
}
static void open_exceptions(void)
{
  static const char *kinds[3];
  if (!exwin) {
    Object *add, *del, *c;
    kinds[0] = tr("First Letter"); kinds[1] = tr("INitial CAps"); kinds[2] = NULL;
    c = VGroup, QDLG,
      Child, ex_kind = CycleObject, MUIA_Cycle_Entries, (ULONG)kinds, MUIA_CycleChain, 1, End,
      Child, HGroup, Child, Label2(tr("Don't correct:")), Child, ex_word = StringObject, StringFrame, MUIA_String_MaxLen, 40, MUIA_CycleChain, 1,
        MUIA_FixWidthTxt, (ULONG)"MMMMMMMMMMMMMMMM", End, End,
      Child, ex_list = mklist(),
      Child, HGroup, Child, add = QButton(tr("_Add")), Child, del = QButton(tr("_Delete")), Child, HSpace(0), End,
    End;
    if (!(exwin = mkwin("AutoCorrect Exceptions", MAKE_ID('W','0','8','E'), c))) return;
    bt(add, ID_PR_EX_ADD); bt(del, ID_PR_EX_DEL);
    DoMethod(ex_kind, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_PR_EX_KIND);
  }
  ex_fill();
  set(exwin, MUIA_Window_Open, TRUE);
}

/* ---- Read Aloud: a sentence at a time, the reply from narrator.device waking an MUI input handler */
#define MUIM_Proof_Spoken (TAG_USER | 0x0451A808UL)
static struct MUI_CustomClass *scls;
static Object *sobj;
static struct MUI_InputHandlerNode sihn;
static int sihn_on, reading;
static struct MsgPort *nport;
static struct narrator_rb *nreq;
static int ndev_open, nbusy;
static Pos rd_pos;                        /* where the next sentence starts */
static Pos rd_end;                        /* the end of what is read (the selection's, or the document's) */
static char phon[1200];
static UBYTE chans[4] = { 3, 5, 10, 12 };
static ULONG sdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  if (msg->MethodID == MUIM_Proof_Spoken) { DoMethod(wp_app(), MUIM_Application_ReturnID, ID_PR_SPEAK); return 0; }
  return DoSuperMethodA(cl, obj, msg);
}
static void speech_close(void)
{
  if (nbusy && nreq) { AbortIO((struct IORequest *)nreq); WaitIO((struct IORequest *)nreq); }
  nbusy = 0;
  if (sihn_on) { DoMethod(wp_app(), MUIM_Application_RemInputHandler, (ULONG)&sihn); sihn_on = 0; }
  if (ndev_open) { CloseDevice((struct IORequest *)nreq); ndev_open = 0; }
  if (nreq) { DeleteIORequest((struct IORequest *)nreq); nreq = NULL; }
  if (nport) { DeleteMsgPort(nport); nport = NULL; }
  if (TranslatorBase) { CloseLibrary(TranslatorBase); TranslatorBase = NULL; }
  reading = 0;
}
/* the next sentence from rd_pos (to . ! ? or the paragraph's end), selected and sent to the narrator;
   0 when there is nothing more */
static int speak_next(void)
{
  Doc *d = wp_doc();
  while (pos_cmp(rd_pos, rd_end) < 0 && rd_pos.para < d->np) {
    Para *q = d->p[rd_pos.para];
    u32 s = rd_pos.off, e = s, lim = rd_pos.para == rd_end.para ? rd_end.off : q->len;
    char text[600];
    Pos a, b;
    while (s < lim && (q->t[s] == ' ' || q->t[s] == '\t')) s++;
    e = s;
    while (e < lim && e - s < 500) {
      char ch = q->t[e++];
      if ((ch == '.' || ch == '!' || ch == '?') && (e >= lim || q->t[e] == ' ')) break;
    }
    a.para = b.para = rd_pos.para; a.off = s; b.off = e;
    if (e >= lim) { if (rd_pos.para + 1 < d->np && pos_cmp(b, rd_end) < 0) { rd_pos.para++; rd_pos.off = 0; } else rd_pos = rd_end; }
    else rd_pos = b;
    if (e <= s) continue;
    {
      /* plain ASCII for the translator: Latin-1 letters lose their accents, the rest becomes spaces */
      size_t i, n = utf8_to_latin1(q->t + s, e - s, text, sizeof text);
      static const char from[] = "\300\301\302\303\304\305\307\310\311\312\313\314\315\316\317\321\322\323\324\325\326\331\332\333\334\335\340\341\342\343\344\345\347\350\351\352\353\354\355\356\357\361\362\363\364\365\366\371\372\373\374\375\377";
      static const char to[] = "AAAAAACEEEEIIIINOOOOOUUUUYaaaaaaceeeeiiiinooooouuuuyy";
      for (i = 0; i < n; i++) {
        if ((u8)text[i] >= 0x80) {
          size_t k;                               /* (a loop: vbcc's strchr misses bytes above 0x7F) */
          for (k = 0; from[k] && (u8)from[k] != (u8)text[i]; k++) ;
          text[i] = from[k] ? to[k] : ' ';
        }
        else if ((u8)text[i] < 32) text[i] = ' ';
      }
      text[n] = 0;
      /* (a negative answer: the phonemes didn't all fit; what did is read) */
      memset(phon, 0, sizeof phon);
      if (Translate((STRPTR)text, (LONG)n, (STRPTR)phon, (LONG)sizeof phon - 1) > 0 || !phon[0]) continue;
    }
    sel_set(a, b);
    nreq->message.io_Command = CMD_WRITE;
    nreq->message.io_Data = (APTR)phon;
    nreq->message.io_Length = strlen(phon);
    nreq->ch_masks = chans; nreq->nm_masks = sizeof chans;
    SendIO((struct IORequest *)nreq);
    nbusy = 1;
    return 1;
  }
  return 0;
}
static void read_aloud(void)
{
  Pos a, b;
  if (reading) { speech_close(); wp_refresh(); return; }
  if (!(TranslatorBase = OpenLibrary((STRPTR)"translator.library", 0))) {
    wp_ask("OK", "%s", "Read Aloud needs translator.library in LIBS: and narrator.device\nin DEVS: (from Workbench 1.3 or 2.0), which this Amiga hasn't.");
    return;
  }
  if (!(nport = CreateMsgPort()) || !(nreq = (struct narrator_rb *)CreateIORequest(nport, sizeof(struct narrator_rb))) ||
      OpenDevice((STRPTR)"narrator.device", 0, (struct IORequest *)nreq, 0)) {
    speech_close();
    wp_ask("OK", "%s", "Read Aloud needs translator.library in LIBS: and narrator.device\nin DEVS: (from Workbench 1.3 or 2.0), which this Amiga hasn't.");
    return;
  }
  ndev_open = 1;
  if (!scls) scls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Notify, NULL, 4, (APTR)sdispatch);
  if (scls && !sobj) sobj = (Object *)NewObject(scls->mcc_Class, NULL, TAG_DONE);
  if (!sobj) { speech_close(); return; }
  memset(&sihn, 0, sizeof sihn);
  sihn.ihn_Object = sobj; sihn.ihn_Signals = 1UL << nport->mp_SigBit; sihn.ihn_Flags = 0; sihn.ihn_Method = MUIM_Proof_Spoken;
  DoMethod(wp_app(), MUIM_Application_AddInputHandler, (ULONG)&sihn);
  sihn_on = 1;
  /* the selection, or from the caret to the end */
  sel_range(&a, &b);
  rd_pos = a; rd_end = pos_cmp(a, b) ? b : doc_end_pos(wp_doc());
  reading = 1;
  if (!speak_next()) speech_close();
  wp_refresh();
}
static void spoken(void)
{
  if (!nport || !nbusy) return;
  if (!CheckIO((struct IORequest *)nreq)) return;
  WaitIO((struct IORequest *)nreq);
  nbusy = 0;
  if (!reading || !speak_next()) { speech_close(); wp_refresh(); }
}

/* ---- the right-button menu: grammar, AutoCorrect for a misspelt word, Synonyms */
static char ctx_syn[16][64], ctx_synl[16][64]; static int ctx_nsyn;
static char ctx_ac[8][64], ctx_acl[8][64], ctx_bad[64]; static int ctx_nac;
static Pos ctx_a, ctx_b, ctx_ga, ctx_gb;
static GramIssue ctx_gi; static int ctx_hasg;
static Object *ctx_submenu(Object *menu, const char *title, ULONG id, char (*labels)[64], int n, const char *last, ULONG lastid)
{
  Object *it = MenuitemObject, MUIA_Menuitem_Title, (ULONG)title, End;
  int i;
  if (!it) return NULL;
  for (i = 0; i < n; i++) {
    Object *s = MenuitemObject, MUIA_Menuitem_Title, (ULONG)labels[i], MUIA_UserData, WP_CTX_LIST(id, i), End;
    if (s) DoMethod(it, MUIM_Family_AddTail, (ULONG)s);
  }
  if (last) {
    Object *s = MenuitemObject, MUIA_Menuitem_Title, (ULONG)NM_BARLABEL, End;
    if (n && s) DoMethod(it, MUIM_Family_AddTail, (ULONG)s);
    s = MenuitemObject, MUIA_Menuitem_Title, (ULONG)last, MUIA_UserData, WP_CTX_ID(lastid), End;
    if (s) DoMethod(it, MUIM_Family_AddTail, (ULONG)s);
  }
  DoMethod(menu, MUIM_Family_AddTail, (ULONG)it);
  return it;
}
static void wp8_ctx(Object *menu, const WpCtx *c)
{
  Doc *d = c->doc;
  Para *q;
  if (!d || c->where == WPC_OBJECT || c->caret.para >= d->np) return;
  q = d->p[c->caret.para];
  ctx_hasg = 0; ctx_nsyn = ctx_nac = 0;
  /* a grammar issue under the pointer */
  if (gopts()) {
    GramIssue is[GMAX];
    int n = grammar_check(q->t, q->len, gopts(), is, GMAX), k;
    for (k = 0; k < n; k++)
      if (is[k].a <= c->caret.off && c->caret.off <= is[k].b && grammar_applies(q, is[k].a) && !grammar_ignored(&is[k], q->t)) {
        static char lab[120], msg[120];
        ctx_gi = is[k]; ctx_hasg = 1;
        ctx_ga.para = ctx_gb.para = c->caret.para; ctx_ga.off = is[k].a; ctx_gb.off = is[k].b;
        wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
        strncpy(msg, tr(grammar_message(is[k].kind)), sizeof msg - 1); msg[sizeof msg - 1] = 0;
        wp_ctx_item(menu, msg, 0, 0);
        if (is[k].has_sug) {
          char s[60];
          lat(is[k].sug, s, sizeof s);
          if (s[0]) sprintf(lab, tr("Change to \"%s\""), s); else strcpy(lab, tr("Delete Repeated Word"));
          wp_ctx_item(menu, lab, WP_CTX_ID(ID_PR_CTX_GSUG), 1);
        }
        wp_ctx_item(menu, tr("Ignore Once"), WP_CTX_ID(ID_PR_CTX_GIGN), 1);
        wp_ctx_item(menu, tr("Spelling & Grammar..."), WP_CTX_ID(ID_PR_SPELLGRAM), 1);
        break;
      }
  }
  if (!word_at(d, c->caret, &ctx_a, &ctx_b) || ctx_b.off - ctx_a.off >= sizeof ctx_bad) return;
  memcpy(ctx_bad, q->t + ctx_a.off, ctx_b.off - ctx_a.off); ctx_bad[ctx_b.off - ctx_a.off] = 0;
  /* a misspelt word: AutoCorrect to one of its suggestions */
  if (c->where == WPC_SPELLING) {
    Spell *s = proof_spell_at((Spell *)wp_spell(), q, ctx_a.off);
    char *sug[8];
    int n = s ? spell_suggest(s, ctx_bad, (u32)strlen(ctx_bad), sug, 8) : 0, i;
    for (i = 0; i < n; i++) {
      strncpy(ctx_ac[i], sug[i], 63); ctx_ac[i][63] = 0;
      lat(sug[i], ctx_acl[i], sizeof ctx_acl[i]);
      free(sug[i]);
    }
    ctx_nac = n;
    wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
    ctx_submenu(menu, tr("AutoCorrect"), ID_PR_CTX_AC, ctx_acl, ctx_nac, tr("AutoCorrect Options..."), ID_PR_ACOPTS);
    return;
  }
  /* Synonyms */
  {
    Thes *t = thes_for(plang_of(para_fmt_at(q, ctx_a.off)));
    ThesResult r;
    int i, k, j;
    if (t && thes_lookup(t, ctx_bad, (u32)strlen(ctx_bad), &r)) {
      for (i = 0; i < r.n && ctx_nsyn < 12; i++)
        for (k = 0; k < r.m[i].nterms && ctx_nsyn < 12; k++) {
          for (j = 0; j < ctx_nsyn && strcmp(ctx_syn[j], r.m[i].terms[k]); j++) ;
          if (j < ctx_nsyn) continue;
          strncpy(ctx_syn[ctx_nsyn], r.m[i].terms[k], 63); ctx_syn[ctx_nsyn][63] = 0;
          lat(ctx_syn[ctx_nsyn], ctx_synl[ctx_nsyn], sizeof ctx_synl[0]);
          ctx_nsyn++;
        }
      thes_result_free(&r);
    }
    wp_ctx_item(menu, (const char *)NM_BARLABEL, 0, 1);
    ctx_submenu(menu, tr("Synonyms"), ID_PR_CTX_SYN, ctx_synl, ctx_nsyn, tr("Thesaurus..."), ID_PR_THES);
  }
}

/* ---- commands, state, keys */
int wp8_command(ULONG id, LONG value)
{
  switch (id) {
  case ID_PR_SPELLGRAM: wp8_spelling(); return WPR_KEEPFOCUS;
  case ID_PR_SG_PICK: sg_pick(); return WPR_KEEPFOCUS;
  case ID_PR_SG_OPTS:
    chk_grammar = chk(sg_gram); chk_style = chk(sg_style); gen++; save_prefs8();
    DoMethod(wp_view(), MUIM_Quill_Command, CMD_REFRESH, 0);
    return WPR_KEEPFOCUS;
  case ID_PR_SG_IGNORE: case ID_PR_SG_IGNOREALL: case ID_PR_SG_ADD: case ID_PR_SG_CHANGE: case ID_PR_SG_CHANGEALL: case ID_PR_SG_AUTOCORR:
    sg_command(id); return WPR_KEEPFOCUS;
  case ID_PR_THES: open_thesaurus(); return WPR_KEEPFOCUS;
  case ID_PR_TH_LOOKUP: { char u[200]; wp_latin1_utf8(str(th_word), u, sizeof u); th_fromdoc = 0; th_lookup(u); return WPR_KEEPFOCUS; }
  case ID_PR_TH_MEANING: th_fill_syns(); return WPR_KEEPFOCUS;
  case ID_PR_TH_SYN: {
    /* a synonym double-clicked: looked up in its turn (Word's way of browsing) */
    char *pick = NULL, u[200];
    DoMethod(th_syns, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (ULONG)&pick);
    if (pick) { set(th_word, MUIA_String_Contents, (ULONG)pick); wp_latin1_utf8(pick, u, sizeof u); th_lookup(u); }
    return WPR_KEEPFOCUS; }
  case ID_PR_TH_INSERT: th_insert(); break;
  case ID_PR_ACOPTS: open_acopts(); return WPR_KEEPFOCUS;
  case ID_PR_AC_OK: ac_ok(); break;
  case ID_PR_AC_PICK: {
    LONG a = active(ac_list);
    char l[80];
    if (a >= 0 && a < ac.n) { lat(ac.from[a], l, sizeof l); set(ac_from, MUIA_String_Contents, (ULONG)l); lat(ac.to[a], l, sizeof l); set(ac_to, MUIA_String_Contents, (ULONG)l); }
    return WPR_KEEPFOCUS; }
  case ID_PR_AC_ADD: {
    char f[160], t[160];
    wp_latin1_utf8(str(ac_from), f, sizeof f); wp_latin1_utf8(str(ac_to), t, sizeof t);
    if (!f[0]) { DisplayBeep(NULL); return WPR_KEEPFOCUS; }
    ac_add(&ac, f, t); ac_store(); ac_fill();
    return WPR_KEEPFOCUS; }
  case ID_PR_AC_DEL: {
    char f[160];
    wp_latin1_utf8(str(ac_from), f, sizeof f);
    if (!ac_remove(&ac, f)) DisplayBeep(NULL); else { ac_store(); ac_fill(); }
    return WPR_KEEPFOCUS; }
  case ID_PR_AC_EXC: open_exceptions(); return WPR_KEEPFOCUS;
  case ID_PR_EX_KIND: ex_fill(); return WPR_KEEPFOCUS;
  case ID_PR_EX_ADD: case ID_PR_EX_DEL: {
    char w[80];
    ULONG which = 0;
    get(ex_kind, MUIA_Cycle_Active, &which);
    if (id == ID_PR_EX_ADD) wp_latin1_utf8(str(ex_word), w, sizeof w);
    else { char *pick = NULL; DoMethod(ex_list, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (ULONG)&pick); if (!pick) return WPR_KEEPFOCUS; wp_latin1_utf8(pick, w, sizeof w); }
    if (id == ID_PR_EX_ADD ? ac_exc_add(&ac, (int)which, w) : ac_exc_remove(&ac, (int)which, w)) { ac_store(); ex_fill(); }
    return WPR_KEEPFOCUS; }
  case ID_PR_AFNOW: {
    Doc *d = wp_doc();
    Pos a, b;
    ac_setup();
    sel_range(&a, &b);
    if (!pos_cmp(a, b)) { a = doc_start(d); b = doc_end_pos(d); }       /* the whole document, as Word does */
    if (!ac_format_now(&ac, d, a.para, b.para)) wp_ask("OK", "%s", "AutoFormat found nothing to change.");
    DoMethod(wp_view(), MUIM_Quill_Relayout);
    break; }
  case ID_PR_LIVESPELL: proof_live_spelling = !proof_live_spelling; save_prefs8(); DoMethod(wp_view(), MUIM_Quill_Relayout); break;
  case ID_PR_LIVEGRAM: proof_live_grammar = !proof_live_grammar; save_prefs8(); DoMethod(wp_view(), MUIM_Quill_Relayout); break;
  case ID_PR_READ: read_aloud(); break;
  case ID_PR_SPEAK: spoken(); return WPR_KEEPFOCUS;
  case ID_PR_CTX_SYN:
    if (value >= 0 && value < ctx_nsyn) { sel_set(ctx_a, ctx_b); DoMethod(wp_view(), MUIM_Quill_Command, CMD_REPLACE, (ULONG)ctx_syn[value]); }
    break;
  case ID_PR_CTX_AC:
    if (value >= 0 && value < ctx_nac) {
      ac_setup(); ac_add(&ac, ctx_bad, ctx_ac[value]); ac_store();
      sel_set(ctx_a, ctx_b); DoMethod(wp_view(), MUIM_Quill_Command, CMD_REPLACE, (ULONG)ctx_ac[value]);
    }
    break;
  case ID_PR_CTX_GSUG: if (ctx_hasg) { sel_set(ctx_ga, ctx_gb); DoMethod(wp_view(), MUIM_Quill_Command, CMD_REPLACE, (ULONG)ctx_gi.sug); } break;
  case ID_PR_CTX_GIGN:
    if (ctx_hasg && ctx_ga.para < wp_doc()->np) { grammar_ignore(&ctx_gi, wp_doc()->p[ctx_ga.para]->t); gen++; DoMethod(wp_view(), MUIM_Quill_Command, CMD_REFRESH, 0); }
    break;
  }
  return WPR_DONE;
}
/* Read Aloud speaking; the menus' ticks kept */
static void wp8_state(ULONG *on, const WpCtx *c)
{
  Object *strip = NULL, *it;
  static const ULONG ids[2] = { ID_PR_LIVESPELL, ID_PR_LIVEGRAM };
  int k;
  if (reading) *on |= 1UL << WP8S_READING;
  if (wp_win()) get(wp_win(), MUIA_Window_Menustrip, &strip);
  for (k = 0; strip && k < 2; k++)
    if ((it = (Object *)DoMethod(strip, MUIM_FindUData, ids[k]))) {
      ULONG ck = 0;
      int want = k ? proof_live_grammar : proof_live_spelling;
      get(it, MUIA_Menuitem_Checked, &ck);
      if ((ck != 0) != (want != 0)) SetAttrs(it, MUIA_NoNotify, TRUE, MUIA_Menuitem_Checked, want, TAG_DONE);
    }
}
/* F7 Spelling & Grammar, Shift-F7 Thesaurus (Word's keys) */
static ULONG wp8_key(UWORD code, UWORD qual, const WpCtx *c)
{
  if (code != 0x56 || (qual & (IEQUALIFIER_CONTROL | IEQUALIFIER_LALT | IEQUALIFIER_RALT))) return 0;
  return (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? ID_PR_THES : ID_PR_SPELLGRAM;
}
static void wp8_init(void) { load_prefs8(); ac_setup(); }
static void wp8_cleanup(void)
{
  speech_close();
  if (sobj) { DisposeObject(sobj); sobj = NULL; }
  if (scls) { MUI_DeleteCustomClass(scls); scls = NULL; }
  thes_close(thes); thes = NULL;
  thes_result_free(&thr);
  if (ac_ready) ac_free(&ac);
  ac_ready = 0;
  grammar_forget();
}

/* the ribbon: Review > Speech (Thesaurus is in Quill's own Proofing group, ribbon.c); the menus */
static const Item speech_items[] = {
  { K_BUTTON, S_LARGE, 0, 0, "Read Aloud", ID_PR_READ, RB_WP(WPI_8, WP8S_READING), 0, 0, 0,
    "Read Aloud: the selection (or from the caret on) read out by the Amiga's narrator, the sentence being read selected. Click again to stop",
    NULL, "ci F 1 6 4 10 P 4 4 6 8 2 8 14 4 10 4 6 ca P 3 10 5 12 8 10 11 P 3 12 3 14 8 12 13", 0 },
};
static const WpGroup groups8[] = { { TAB_REVIEW, 1, { "Speech", G_COLS, speech_items, 1 } } };
static const WpMenu menus8[] = {
  { WM_REVIEW, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_REVIEW, { NM_ITEM, (STRPTR)"Thesaurus...", 0, 0, 0, (APTR)ID_PR_THES } },
  { WM_REVIEW, { NM_ITEM, (STRPTR)"Read Aloud", 0, 0, 0, (APTR)ID_PR_READ } },
  { WM_REVIEW, { NM_ITEM, (STRPTR)"Proofing", 0, 0, 0, 0 } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"Check Spelling as You Type", 0, CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)ID_PR_LIVESPELL } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"Mark Grammar Errors as You Type", 0, CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)ID_PR_LIVEGRAM } },
  { WM_REVIEW, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_REVIEW, { NM_SUB, (STRPTR)"AutoCorrect Options...", 0, 0, 0, (APTR)ID_PR_ACOPTS } },
  { WM_FORMAT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_ITEM, (STRPTR)"AutoFormat Now", 0, 0, 0, (APTR)ID_PR_AFNOW } },
};
const WpPackage wp8_package = {
  "WP8", ID_WP8_FIRST, ID_WP8_LAST,
  wp8_command, wp8_state, NULL, wp8_ctx, wp8_key, wp8_init, wp8_cleanup,
  groups8, 1,
  menus8, (int)(sizeof menus8 / sizeof menus8[0])
};
