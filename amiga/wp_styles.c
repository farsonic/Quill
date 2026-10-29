/*
 * wp_styles.c - WP13 Styles and themes (docs/wordcraft-gap.md). This file belongs to WP13: only that
 * package edits it. Its commands are ID_WP13_FIRST .. ID_WP13_LAST (ids.h); how the hooks below
 * are called, and how to add ribbon groups, menu items, context-menu items and keys: wphooks.h.
 *
 * The Styles pane: docked at the right of the document window (in its work area, beside the page),
 * drawn by Quill: every style (recommended, in use, in the document or all; sorted as Word sorts
 * them), each name in its own font, size and colour when Show Preview is on, the caret's style
 * marked; a click applies it (a linked style's character half on a selection inside a paragraph,
 * unless Disable Linked Styles), a double-click opens Modify Style. Style Inspector and Reveal
 * Formatting describe the formatting at the caret; Modify Style (also New Style) has Word's Format
 * pages: paragraph, tabs, borders, numbering, frame, language and shortcut key; Manage Styles (edit,
 * recommend, restrict, set defaults) and the Organizer (styles copied from or to another file).
 * Design > Office Theme: Office's themes, theme colours, theme fonts and effects (core/doctheme.c),
 * with Customize Colors and Customize Fonts, Browse for Themes and Save Current Theme.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <devices/inputevent.h>
#include <libraries/iffparse.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "wphooks.h"
#include "lang.h"
#include "theme.h"
#include "../core/doctheme.h"
#include "../core/blocks.h"
#include "../core/fonts.h"
#include "../core/proflang.h"

#undef Label1
#undef Label2
#undef LLabel1
#define Label1(l)  QLabel(l, MUIO_Label_SingleFrame)
#define Label2(l)  QLabel(l, MUIO_Label_DoubleFrame)
#define LLabel1(l) QLabel(l, MUIO_Label_LeftAligned | MUIO_Label_SingleFrame)

/* main.c's (see there) */
Object *wp_workarea(void);
Object *wp_window_obj(int i);
FontSet *wp_fonts(void);
const char *wp_docpath(void);

enum {
  ID_SP_PANE = ID_WP13_FIRST, ID_SP_INSPECT, ID_SP_REVEAL, ID_SP_MANAGE, ID_SP_OPTIONS, ID_SP_OPTGO,
  ID_SI_RESETP, ID_SI_CLEARP, ID_SI_RESETC, ID_SI_CLEARC, ID_SI_NEW,
  ID_SD_OK, ID_SD_TABSET, ID_SD_TABCLR, ID_SD_TABALL, ID_SD_TABPICK, ID_SD_PAGE, ID_SD_TYPE,
  ID_MS_MODIFY, ID_MS_DELETE, ID_MS_NEW, ID_MS_ORGANIZER, ID_MS_UP, ID_MS_DOWN, ID_MS_LAST, ID_MS_ASSIGN, ID_MS_SHOW, ID_MS_HIDEUSED,
  ID_MS_HIDE, ID_MS_LOCK, ID_MS_UNLOCK, ID_MS_DEFAULTS, ID_MS_PAGE, ID_MS_PICK,
  ID_OR_OPEN, ID_OR_TORIGHT, ID_OR_TOLEFT, ID_OR_DELL, ID_OR_DELR, ID_OR_CLOSE,
  ID_TH_THEMES, ID_TH_COLORS, ID_TH_FONTS, ID_TH_EFFECTS, ID_TH_WINDOW, ID_TH_APPLY, ID_TH_BROWSE, ID_TH_SAVE, ID_TH_RESET,
  ID_TC_OPEN, ID_TC_PICK, ID_TC_SAVE, ID_TF_OPEN, ID_TF_SAVE,
  ID_SP_KEY0                           /* + style index... (the shortcut keys; not menu items) */
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
static int is_open(Object *w) { ULONG o = 0; if (w) get(w, MUIA_Window_Open, &o); return o != 0; }
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
static void changed_now(void) { set(wp_view(), MUIA_Quill_Changed, TRUE); DoMethod(wp_view(), MUIM_Quill_Relayout); wp_refresh(); }
/* a style's name as shown: Latin-1, Word's built-in lower-case names with a capital */
static void shown(const Doc *d, int k, char *out, size_t cap)
{
  lat(d->styles[k].name, out, cap);
  if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 32);
}
static int style_ok(const Doc *d, int k) { return d && k >= 0 && k < d->nstyles && (d->styles[k].type == ST_PARA || d->styles[k].type == ST_CHAR); }
/* lengths as the dialogs show them: centimetres with two decimals, or points */
static void tw_text(i32 tw, char *out, int pt)
{
  long v = pt ? ((long)tw * 10 + (tw < 0 ? -10 : 10)) / 20 : ((long)tw * 100 + (tw < 0 ? -283 : 283)) / 567;   /* tenths of a point, hundredths of a cm */
  long a = v < 0 ? -v : v;
  if (pt) { if (a % 10) sprintf(out, "%s%ld.%ld pt", v < 0 ? "-" : "", a / 10, a % 10); else sprintf(out, "%s%ld pt", v < 0 ? "-" : "", a / 10); }
  else sprintf(out, "%s%ld.%02ld cm", v < 0 ? "-" : "", a / 100, a % 100);
}
/* a length typed: a number with cm, mm, in (or "), pt; none: cm (pt for spacing). Twips */
static i32 parse_tw(const char *s, int pt)
{
  long ip = 0, fp = 0, fd = 1;
  int neg = 0;
  while (*s == ' ') s++;
  if (*s == '-') { neg = 1; s++; }
  while (*s >= '0' && *s <= '9') ip = ip * 10 + (*s++ - '0');
  if (*s == '.' || *s == ',') { s++; while (*s >= '0' && *s <= '9') { if (fd < 1000) { fp = fp * 10 + (*s - '0'); fd *= 10; } s++; } }
  while (*s == ' ') s++;
  {
    long milli = ip * 1000 + fp * 1000 / fd, tw;
    if (!strncmp(s, "mm", 2)) tw = milli * 567 / 10000;
    else if (!strncmp(s, "cm", 2)) tw = milli * 567 / 1000;
    else if (!strncmp(s, "in", 2) || *s == '"') tw = milli * 1440 / 1000;
    else if (!strncmp(s, "pt", 2)) tw = milli * 20 / 1000;
    else tw = pt ? milli * 20 / 1000 : milli * 567 / 1000;
    return (i32)(neg ? -tw : tw);
  }
}
/* a colour for people: a theme colour's name ("Accent 1, Darker 25%"), else its hex */
static void color_name(const Doc *d, u32 rgb, u32 ref, char *out, size_t cap)
{
  if (rgb == COLOR_AUTO) { snprintf(out, cap, "%s", tr("Automatic")); return; }
  if (theme_ref_ok(d, ref, rgb)) {
    int m = THR_MOD(ref), a = THR_A(ref), pc = m == THM_SHADE || m == THM_TINT ? (255 - a) * 100 / 255 : m == THM_LUM ? (THR_B(ref) ? THR_B(ref) : 100 - a) : 0;
    char b[60];
    snprintf(b, sizeof b, "%s", tr(thc_label(THR_COLOR(ref))));
    if (pc && (m == THM_SHADE || (m == THM_LUM && !THR_B(ref)) || m == THM_DSHADE)) snprintf(out, cap, "%s, %s %d%%", b, tr("Darker"), (pc + 2) / 5 * 5);
    else if (pc) snprintf(out, cap, "%s, %s %d%%", b, tr("Lighter"), (pc + 2) / 5 * 5);
    else snprintf(out, cap, "%s", b);
    return;
  }
  snprintf(out, cap, "#%06lX", (unsigned long)(rgb & 0xFFFFFF));
}

/* ------------------------------------------------------------------ */
/* descriptions of formatting (Style Inspector, Reveal Formatting, the pane's bubbles, Modify Style) */
static void addf(char *out, size_t cap, const char *s)
{
  size_t n = strlen(out);
  if (n && n + 2 < cap) { strcat(out, ", "); n += 2; }
  if (n + strlen(s) < cap) strcat(out, s);
}
/* what a character format says (fields mask), Latin-1 */
static void cf_text(const Doc *d, const CharFmt *c, unsigned mask, char *out, size_t cap)
{
  char b[100];
  out[0] = 0;
  if ((mask & CM_FONT) && c->font < d->nfonts) {
    lat(d->fonts[c->font], b, sizeof b);
    if (theme_font_ok(d, c->thfont, c->font)) { strcat(b, " ("); strcat(b, tr(c->thfont == THF_MAJOR ? "Headings" : "Body")); strcat(b, ")"); }
    addf(out, cap, b);
  }
  if (mask & CM_SIZE) { sprintf(b, c->size & 1 ? "%u.5 pt" : "%u pt", (unsigned)(c->size / 2)); addf(out, cap, b); }
  if ((mask & CM_BOLD)) addf(out, cap, tr(c->flags & CF_BOLD ? "Bold" : "Not Bold"));
  if ((mask & CM_ITALIC)) addf(out, cap, tr(c->flags & CF_ITALIC ? "Italic" : "Not Italic"));
  if ((mask & CM_UNDERLINE)) addf(out, cap, tr(c->flags & CF_UNDERLINE ? "Underline" : "No underline"));
  if ((mask & CM_STRIKE) && (c->flags & CF_STRIKE)) addf(out, cap, tr("Strikethrough"));
  if ((mask & CM_CAPS) && (c->flags & CF_CAPS)) addf(out, cap, tr("All caps"));
  if ((mask & CM_SMALLCAPS) && (c->fx & FX_SMALLCAPS)) addf(out, cap, tr("Small caps"));
  if ((mask & CM_SUPER) && (c->flags & CF_SUPER)) addf(out, cap, tr("Superscript"));
  if ((mask & CM_SUB) && (c->flags & CF_SUB)) addf(out, cap, tr("Subscript"));
  if (mask & CM_COLOR) { char cn[60]; color_name(d, c->color, c->thcolor, cn, sizeof cn); snprintf(b, sizeof b, "%s: %s", tr("Font colour"), cn); addf(out, cap, b); }
  if ((mask & CM_HL) && c->hl) { snprintf(b, sizeof b, "%s #%06lX", tr("Highlight"), (unsigned long)(c->hl & 0xFFFFFF)); addf(out, cap, b); }
  if ((mask & CM_SPACING) && c->spacing) { tw_text(c->spacing, b + 40, 1); snprintf(b, 40, "%s %s", tr(c->spacing > 0 ? "Expanded by" : "Condensed by"), b + 40); addf(out, cap, b); }
  if ((mask & CM_POSITION) && c->position) { snprintf(b, sizeof b, "%s %d pt", tr(c->position > 0 ? "Raised by" : "Lowered by"), (c->position < 0 ? -c->position : c->position) / 2); addf(out, cap, b); }
}
/* what a paragraph format says (groups mask), Latin-1 */
static void pf_text(const Doc *d, const ParaFmt *p, unsigned mask, char *out, size_t cap)
{
  static const char *const al[4] = { "Left", "Centred", "Right", "Justified" };
  char b[120], v[40], w[40];
  int k;
  out[0] = 0;
  if (mask & PM_ALIGN) addf(out, cap, tr(al[p->align & 3]));
  if (mask & PM_INDENT) {
    tw_text(p->left, v, 0); snprintf(b, sizeof b, "%s: %s", tr("Left"), v); addf(out, cap, b);
    if (p->right) { tw_text(p->right, v, 0); snprintf(b, sizeof b, "%s: %s", tr("Right"), v); addf(out, cap, b); }
    if (p->first) { tw_text(p->first < 0 ? -p->first : p->first, v, 0); snprintf(b, sizeof b, "%s: %s", tr(p->first < 0 ? "Hanging" : "First line"), v); addf(out, cap, b); }
  }
  if (mask & PM_SPACING) { tw_text(p->before, v, 1); tw_text(p->after, w, 1); snprintf(b, sizeof b, "%s: %s, %s: %s", tr("Space Before"), v, tr("After"), w); addf(out, cap, b); }
  if (mask & PM_LINE) {
    if (p->linerule == LS_AUTO) {
      if (p->line == 240) snprintf(b, sizeof b, "%s", tr("Line spacing: single"));
      else snprintf(b, sizeof b, "%s %ld.%02ld %s", tr("Line spacing:"), (long)(p->line / 240), (long)(p->line % 240 * 100 / 240), tr("lines"));
    } else { tw_text(p->line, v, 1); snprintf(b, sizeof b, "%s %s %s", tr("Line spacing:"), tr(p->linerule == LS_EXACT ? "exactly" : "at least"), v); }
    addf(out, cap, b);
  }
  if ((mask & PM_KEEP) && p->keepnext) addf(out, cap, tr("Keep with next"));
  if ((mask & PM_PAGINATION) && p->keeplines) addf(out, cap, tr("Keep lines together"));
  if ((mask & PM_BREAK) && p->pagebreak) addf(out, cap, tr("Page break before"));
  if ((mask & PM_CONTEXT) && p->contextual) addf(out, cap, tr("Don't add space between paragraphs of the same style"));
  if ((mask & PM_TABS) && p->tabs[0]) {
    snprintf(b, sizeof b, "%s:", tr("Tab stops"));
    for (k = 0; k < 8 && p->tabs[k]; k++) { tw_text(p->tabs[k], v, 0); if (strlen(b) + strlen(v) + 3 < sizeof b) { strcat(b, " "); strcat(b, v); } }
    addf(out, cap, b);
  }
  if ((mask & PM_BORDER) && (p->bstyle[0] || p->bstyle[1] || p->bstyle[2] || p->bstyle[3] || p->bstyle[4])) addf(out, cap, tr("Border"));
  if ((mask & PM_SHADE) && p->shade) { char cn[60]; color_name(d, p->shade & 0xFFFFFF, p->thshade, cn, sizeof cn); snprintf(b, sizeof b, "%s: %s", tr("Shading"), cn); addf(out, cap, b); }
  if ((mask & PM_LIST) && p->list) { snprintf(b, sizeof b, "%s %u", tr("Numbered or bulleted, level"), (unsigned)p->level + 1); addf(out, cap, b); }
}
/* a style's formatting as Word describes it in its windows */
static void style_text(const Doc *d, int k, char *out, size_t cap)
{
  const Style *s = &d->styles[k];
  char b[400], n[60];
  out[0] = 0;
  if (s->type == ST_PARA) {
    cf_text(d, &s->cf, CM_FONT | CM_SIZE | (s->cmask & (CM_BOLD | CM_ITALIC | CM_UNDERLINE | CM_CAPS | CM_SMALLCAPS | CM_STRIKE)) | (s->cf.color != COLOR_AUTO ? CM_COLOR : 0), b, sizeof b);
    addf(out, cap, b);
    pf_text(d, &s->pf, s->pmask | PM_ALIGN | PM_SPACING | PM_LINE, b, sizeof b);
    addf(out, cap, b);
  } else { cf_text(d, &s->cf, s->ecmask, b, sizeof b); addf(out, cap, b); }
  if (s->x.frame.set) addf(out, cap, tr("Frame"));
  if (s->cf.lang) { snprintf(b, sizeof b, "%s: %s", tr("Language"), plang_tag(s->cf.lang)); addf(out, cap, b); }
  if (s->based != NO_STYLE && s->based < d->nstyles) { shown(d, s->based, n, sizeof n); snprintf(b, sizeof b, "%s: %s", tr("Based on"), n); addf(out, cap, b); }
  if (s->type == ST_PARA && s->next < d->nstyles && s->next != k) { shown(d, s->next, n, sizeof n); snprintf(b, sizeof b, "%s: %s", tr("Following style"), n); addf(out, cap, b); }
  if (s->x.link && style_ok(d, s->x.link - 1)) { shown(d, s->x.link - 1, n, sizeof n); snprintf(b, sizeof b, "%s: %s", tr("Linked"), n); addf(out, cap, b); }
  if (s->quick) addf(out, cap, tr("Show in the Styles gallery"));
  if (s->x.flags & SXF_AUTOREDEFINE) addf(out, cap, tr("Automatically update"));
  if (s->x.priority) { snprintf(b, sizeof b, "%s: %u", tr("Priority"), (unsigned)(s->x.priority - 1)); addf(out, cap, b); }
  if (s->x.key) { char kn[24]; stylekey_name(s->x.key, kn, sizeof kn); snprintf(b, sizeof b, "%s: %s", tr("Shortcut key"), kn); addf(out, cap, b); }
}
/* the PM_ groups and CM_ fields a paragraph or text differs from its style in (direct formatting) */
static unsigned pf_diff(const ParaFmt *a, const ParaFmt *b)
{
  unsigned m = 0;
  int k;
  if (a->align != b->align) m |= PM_ALIGN;
  if (a->left != b->left || a->right != b->right || a->first != b->first) m |= PM_INDENT;
  if (a->before != b->before || a->after != b->after) m |= PM_SPACING;
  if (a->line != b->line || a->linerule != b->linerule) m |= PM_LINE;
  for (k = 0; k < 8; k++) if (a->tabs[k] != b->tabs[k] || a->tabkind[k] != b->tabkind[k]) m |= PM_TABS;
  if (a->keepnext != b->keepnext) m |= PM_KEEP;
  if (a->keeplines != b->keeplines || a->nowidow != b->nowidow) m |= PM_PAGINATION;
  if (a->pagebreak != b->pagebreak) m |= PM_BREAK;
  if (a->contextual != b->contextual) m |= PM_CONTEXT;
  if (memcmp(a->bstyle, b->bstyle, sizeof a->bstyle) || memcmp(a->bcolor, b->bcolor, sizeof a->bcolor)) m |= PM_BORDER;
  if (a->shade != b->shade) m |= PM_SHADE;
  if (a->list != b->list || a->level != b->level) m |= PM_LIST;
  return m;
}
static unsigned cf_diff(const CharFmt *a, const CharFmt *b)
{
  unsigned m = (unsigned)(a->flags ^ b->flags) & 0x7F;
  if (a->font != b->font) m |= CM_FONT;
  if (a->size != b->size) m |= CM_SIZE;
  if (a->color != b->color) m |= CM_COLOR;
  if (a->hl != b->hl) m |= CM_HL;
  if ((a->fx ^ b->fx) & FX_SMALLCAPS) m |= CM_SMALLCAPS;
  if (a->spacing != b->spacing) m |= CM_SPACING;
  if (a->position != b->position) m |= CM_POSITION;
  return m;
}
/* the text's formatting at the caret: its paragraph's style, character style, and what the text has
   beyond its styles */
static const CharFmt *caret_cf(Doc *d)
{
  Pos c = get_pos(MUIA_Quill_Caret), a = get_pos(MUIA_Quill_Anchor);
  Pos lo = pos_cmp(a, c) < 0 ? a : c;
  if (lo.para >= d->np) return &d->p[0]->r[0].f;
  return pos_cmp(a, c) ? para_fmt_at(d->p[lo.para], lo.off) : para_fmt_before(d->p[lo.para], lo.off);
}
static const CharFmt *style_cf_of(Doc *d, const CharFmt *c, int ps, CharFmt *tmp)
{
  /* what the text would have from its styles alone */
  *tmp = d->styles[ps < d->nstyles ? ps : 0].cf;
  if (c->cstyle && style_ok(d, c->cstyle)) cf_apply(tmp, d->styles[c->cstyle].ecmask, &d->styles[c->cstyle].cf);
  return tmp;
}

/* ------------------------------------------------------------------ */
/* the Styles pane: a class of its own, docked in the window's work area */
#define MAXROWS (MAX_STYLES + 1)
typedef struct { WORD x0, y0, x1, y1; } SBox;
struct SPData {
  int shown;
  int rows[MAXROWS]; int nrows;         /* style indices (-1: Clear All) */
  int first, hot;
  int cur, ccur;                        /* the caret's paragraph and character styles */
  Doc *doc; u32 ver; int nst;
  SBox close, body, prev, nolink, bnew, binsp, bman, opts;
  struct MUI_EventHandlerNode ehn;
  ULONG lastsec, lastmic; int lastrow;
};
static struct MUI_CustomClass *spcls;
static Object *pane, *panewin;          /* the pane, and the window it is docked in */
static int sp_preview = 1, sp_nolink, sp_show, sp_sort;   /* options: preview; linked styles off; show (0 recommended, 1 in use, 2 in the document, 3 all); sort (0 alphabetical, 1 as recommended, 2 font, 3 based on, 4 type) */
static int fh(void) { return theme_font()->tf_YSize; }
static int in_box(const SBox *b, WORD x, WORD y) { return x >= b->x0 && x <= b->x1 && y >= b->y0 && y <= b->y1; }
static LONG row_h(void) { return fh() + (sp_preview ? 9 : 5); }
/* whether a style is in use: a paragraph has it, text has it, or it's a linked style's other half */
static void in_use(Doc *d, u8 *used)
{
  u32 i, r;
  memset(used, 0, MAX_STYLES);
  for (i = 0; i < d->np; i++) {
    Para *p = d->p[i];
    if (p->pf.style < MAX_STYLES) used[p->pf.style] = 1;
    for (r = 0; r < p->nr; r++) if (p->r[r].f.cstyle < MAX_STYLES) used[p->r[r].f.cstyle] = 1;
  }
  used[0] = 1;
}
static Doc *sp_doc;                     /* (for the sort) */
static int prio(const Style *s) { return s->x.priority ? s->x.priority - 1 : 100; }
static int sp_cmp(const void *pa, const void *pb)
{
  int a = *(const int *)pa, b = *(const int *)pb, c = 0;
  const Style *x = &sp_doc->styles[a], *y = &sp_doc->styles[b];
  char na[60], nb[60];
  if (sp_sort == 1) c = prio(x) - prio(y);
  else if (sp_sort == 2) c = strcmp(sp_doc->fonts[x->cf.font], sp_doc->fonts[y->cf.font]);
  else if (sp_sort == 3) c = (x->based == NO_STYLE ? -1 : x->based) - (y->based == NO_STYLE ? -1 : y->based);
  else if (sp_sort == 4) c = (int)x->type - (int)y->type;
  if (c) return c;
  shown(sp_doc, a, na, sizeof na); shown(sp_doc, b, nb, sizeof nb);
  for (c = 0; na[c] && (na[c] | 0x20) == (nb[c] | 0x20); c++) ;
  return (int)(u8)(na[c] | 0x20) - (int)(u8)(nb[c] | 0x20);
}
/* the styles the pane lists, by its options */
static int sp_rows(Doc *d, int *rows)
{
  u8 used[MAX_STYLES];
  int k, n = 0;
  in_use(d, used);
  rows[n++] = -1;
  for (k = 0; k < d->nstyles; k++) {
    const Style *s = &d->styles[k];
    int linkedhalf;
    if (!style_ok(d, k) || !strncmp(s->name, "toc ", 4)) continue;
    /* a linked pair is one entry (its paragraph half), unless linked styles are off */
    linkedhalf = s->type == ST_CHAR && s->x.link && style_ok(d, s->x.link - 1) && d->styles[s->x.link - 1].type == ST_PARA;
    if (linkedhalf && !sp_nolink) continue;
    if (sp_show == 0 && !used[k] && (s->x.flags & (SXF_SEMIHIDDEN | SXF_HIDDEN)) && !s->quick) continue;
    if (sp_show == 1 && !used[k]) continue;
    rows[n++] = k;
  }
  sp_doc = d;
  qsort(rows + 1, (size_t)(n - 1), sizeof(int), sp_cmp);
  return n;
}
static int rows_fit(struct SPData *d) { LONG n = (d->body.y1 - d->body.y0 - 2) / row_h(); return n < 1 ? 1 : (int)n; }
static void sp_layout(struct SPData *d, Object *obj)
{
  LONG l = _mleft(obj), t = _mtop(obj), r = _mright(obj), b = _mbottom(obj), y;
  d->close.x0 = (WORD)(r - 18); d->close.y0 = (WORD)(t + 3); d->close.x1 = (WORD)(r - 4); d->close.y1 = (WORD)(t + 17);
  y = b - 4 - 20;                                       /* the buttons */
  d->bnew.x0 = (WORD)(l + 4); d->bnew.x1 = (WORD)(l + 25); d->bnew.y0 = (WORD)y; d->bnew.y1 = (WORD)(y + 19);
  d->binsp = d->bnew; d->binsp.x0 += 26; d->binsp.x1 += 26;
  d->bman = d->binsp; d->bman.x0 += 26; d->bman.x1 += 26;
  d->opts.x0 = (WORD)(d->bman.x1 + 6); d->opts.x1 = (WORD)(r - 4); d->opts.y0 = (WORD)y; d->opts.y1 = (WORD)(y + 19);
  y -= fh() + 6;
  d->nolink.x0 = (WORD)(l + 4); d->nolink.x1 = (WORD)(r - 4); d->nolink.y0 = (WORD)y; d->nolink.y1 = (WORD)(y + fh() + 3);
  y -= fh() + 6;
  d->prev = d->nolink; d->prev.y0 = (WORD)y; d->prev.y1 = (WORD)(y + fh() + 3);
  d->body.x0 = (WORD)(l + 2); d->body.x1 = (WORD)(r - 2); d->body.y0 = (WORD)(t + 22); d->body.y1 = (WORD)(y - 4);
}
static void checkbox(struct RastPort *rp, const SBox *b, int on, const char *label, int hot)
{
  LONG y = b->y0 + 1;
  if (hot) theme_fill(rp, b->x0, b->y0, b->x1, b->y1, tpen(T_HOVER));
  theme_fill(rp, b->x0 + 1, y + 1, b->x0 + fh(), y + fh(), tpen(T_WHITE));
  theme_rect(rp, b->x0 + 1, y + 1, b->x0 + fh(), y + fh(), tpen(T_INPUTBORDER));
  if (on) { SetAPen(rp, tpen(T_ACCENT)); Move(rp, b->x0 + 3, y + fh() / 2 + 1); Draw(rp, b->x0 + fh() / 2, y + fh() - 2); Draw(rp, b->x0 + fh() - 2, y + 3);
            Move(rp, b->x0 + 3, y + fh() / 2); Draw(rp, b->x0 + fh() / 2, y + fh() - 3); Draw(rp, b->x0 + fh() - 2, y + 2); }
  theme_text(rp, theme_font(), b->x0 + fh() + 6, y, label, tpen(T_TEXT));
}
static const char *const ic_inspect = "ca B A ci O 11 11 3 L 13 13 15 15";
static void sp_draw(struct SPData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG l = _mleft(obj), t = _mtop(obj), r = _mright(obj), b = _mbottom(obj), y, rh = row_h();
  struct ColorMap *cm = _screen(obj)->ViewPort.ColorMap;
  FontSet *fs = wp_fonts();
  int i, n;
  Doc *doc = d->doc;
  sp_layout(d, obj);
  theme_fill(rp, l, t, r, b, tpen(T_PANEL));
  SetAPen(rp, tpen(T_BORDER)); Move(rp, l, t); Draw(rp, l, b);
  theme_text(rp, theme_bold(), l + 6, t + 4, tr("Styles"), tpen(T_TEXT));
  if (d->hot == -2) theme_fill(rp, d->close.x0, d->close.y0, d->close.x1, d->close.y1, tpen(T_HOVER));
  SetAPen(rp, tpen(T_ICON));
  Move(rp, d->close.x0 + 3, d->close.y0 + 3); Draw(rp, d->close.x1 - 3, d->close.y1 - 3);
  Move(rp, d->close.x1 - 3, d->close.y0 + 3); Draw(rp, d->close.x0 + 3, d->close.y1 - 3);
  theme_fill(rp, d->body.x0, d->body.y0, d->body.x1, d->body.y1, tpen(T_WHITE));
  theme_rect(rp, d->body.x0, d->body.y0, d->body.x1, d->body.y1, tpen(T_BORDER2));
  n = rows_fit(d);
  if (d->first > d->nrows - n) d->first = d->nrows - n;
  if (d->first < 0) d->first = 0;
  for (i = d->first, y = d->body.y0 + 2; doc && i < d->nrows && y + rh <= d->body.y1; i++, y += rh) {
    int k = d->rows[i], linked;
    char name[64];
    LONG tx = d->body.x0 + 6, maxw = d->body.x1 - 30 - tx;
    if (i == d->hot) theme_fill(rp, d->body.x0 + 1, y, d->body.x1 - 8, y + rh - 1, tpen(T_HOVER));
    if (k >= 0 && (k == d->cur || k == d->ccur)) { theme_fill(rp, d->body.x0 + 1, y, d->body.x1 - 8, y + rh - 1, tpen(T_CHECKED)); theme_rect(rp, d->body.x0 + 1, y, d->body.x1 - 8, y + rh - 1, tpen(T_ACCENT)); }
    if (k < 0) { theme_text(rp, theme_font(), tx, y + (rh - fh()) / 2, tr("Clear All"), tpen(T_TEXT)); continue; }
    shown(doc, k, name, sizeof name);
    linked = doc->styles[k].x.link && style_ok(doc, doc->styles[k].x.link - 1) && !sp_nolink;
    {
      const CharFmt *c = &doc->styles[k].cf;
      int drawn = 0;
      LONG pen = -1;
      if (sp_preview) {
        Face *f = fs && c->font < doc->nfonts ? fonts_face(fs, doc->fonts[c->font], (c->flags & CF_BOLD) != 0, (c->flags & CF_ITALIC) != 0) : NULL;
        LONG px = c->size * 2 / 3;                    /* half-points to pixels at about 96 dpi, kept to the row */
        if (px > rh - 2) px = rh - 2;
        if (px < 8) px = 8;
        if (c->color != COLOR_AUTO && c->color != 0)
          pen = ObtainBestPen(cm, ((c->color >> 16) & 255) * 0x01010101UL, ((c->color >> 8) & 255) * 0x01010101UL, (c->color & 255) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
        if (f && theme_preview(rp, f, name, px * 64, tx, y + rh - 4, maxw, pen >= 0 ? pen : tpen(T_TEXT))) {
          drawn = 1;
          if (c->flags & CF_UNDERLINE) { LONG w = theme_preview_width(f, name, px * 64); SetAPen(rp, pen >= 0 ? pen : tpen(T_TEXT)); Move(rp, tx, y + rh - 2); Draw(rp, tx + (w < maxw ? w : maxw), y + rh - 2); }
        }
      }
      if (!drawn) {
        struct TextFont *tf = sp_preview && (c->flags & CF_BOLD) ? theme_bold() : theme_font();
        LONG ww = theme_textlen(rp, tf, name);
        theme_text(rp, tf, tx, y + (rh - fh()) / 2, name, pen >= 0 ? pen : tpen(T_TEXT));
        if (sp_preview && (c->flags & CF_UNDERLINE)) { SetAPen(rp, pen >= 0 ? pen : tpen(T_TEXT)); Move(rp, tx, y + (rh + fh()) / 2); Draw(rp, tx + ww, y + (rh + fh()) / 2); }
      }
      if (pen >= 0) ReleasePen(cm, pen);
    }
    /* the kind: a paragraph mark, an "a", or both for a linked style */
    theme_text(rp, theme_font(), d->body.x1 - 26, y + (rh - fh()) / 2, linked ? "\266a" : doc->styles[k].type == ST_CHAR ? "a" : "\266", tpen(T_DIM));
  }
  /* a scroll bar when the list is longer than the room */
  if (d->nrows > n) {
    LONG h = d->body.y1 - d->body.y0 - 4, th = h * n / d->nrows, ty = d->body.y0 + 2 + h * d->first / d->nrows;
    if (th < 8) th = 8;
    theme_fill(rp, d->body.x1 - 6, ty, d->body.x1 - 3, ty + th, tpen(T_BORDER));
  }
  checkbox(rp, &d->prev, sp_preview, tr("Show Preview"), d->hot == -3);
  checkbox(rp, &d->nolink, sp_nolink, tr("Disable Linked Styles"), d->hot == -4);
  if (d->hot == -5) theme_fill(rp, d->bnew.x0, d->bnew.y0, d->bnew.x1, d->bnew.y1, tpen(T_HOVER));
  if (d->hot == -6) theme_fill(rp, d->binsp.x0, d->binsp.y0, d->binsp.x1, d->binsp.y1, tpen(T_HOVER));
  if (d->hot == -7) theme_fill(rp, d->bman.x0, d->bman.y0, d->bman.x1, d->bman.y1, tpen(T_HOVER));
  theme_icon(rp, IC_NEWSTYLE, d->bnew.x0 + 3, d->bnew.y0 + 2, 16, 0);
  theme_icon_src(rp, ic_inspect, d->binsp.x0 + 3, d->binsp.y0 + 2, 16, 0);
  theme_icon(rp, IC_STYLESPANE, d->bman.x0 + 3, d->bman.y0 + 2, 16, 0);
  theme_text(rp, theme_font(), d->opts.x0 + 2, d->opts.y0 + (20 - fh()) / 2, tr("Options..."), tpen(d->hot == -8 ? T_ACCENT : T_ACCENTTEXT));
}
static int sp_row(struct SPData *d, WORD x, WORD y)
{
  int i;
  if (!in_box(&d->body, x, y) || x > d->body.x1 - 8) return -1;
  i = d->first + (int)((y - d->body.y0 - 2) / row_h());
  return i >= 0 && i < d->nrows ? i : -1;
}
static int sp_hit(struct SPData *d, WORD x, WORD y)
{
  if (in_box(&d->close, x, y)) return -2;
  if (in_box(&d->prev, x, y)) return -3;
  if (in_box(&d->nolink, x, y)) return -4;
  if (in_box(&d->bnew, x, y)) return -5;
  if (in_box(&d->binsp, x, y)) return -6;
  if (in_box(&d->bman, x, y)) return -7;
  if (in_box(&d->opts, x, y)) return -8;
  return sp_row(d, x, y);
}
static void sp_apply(int k, int dbl);
static void sp_update(Object *obj, int force);
static ULONG sp_events(struct IClass *cl, Object *obj, struct MUIP_HandleEvent *m)
{
  struct SPData *d = INST_DATA(cl, obj);
  struct IntuiMessage *im = m->imsg;
  int inside;
  if (!im || !d->shown) return 0;
  inside = im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj) && im->MouseY >= _mtop(obj) && im->MouseY <= _mbottom(obj);
  if (im->Class == IDCMP_MOUSEMOVE) {
    int h = inside ? sp_hit(d, im->MouseX, im->MouseY) : -1;
    if (h == -1 && inside) h = -1;
    if (h != d->hot) { d->hot = h; MUI_Redraw(obj, MADF_DRAWOBJECT); }
    return 0;
  }
  if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN && inside) {
    int h = sp_hit(d, im->MouseX, im->MouseY);
    switch (h) {
    case -2: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_SP_PANE); break;
    case -3: sp_preview = !sp_preview; MUI_Redraw(obj, MADF_DRAWOBJECT); break;
    case -4: sp_nolink = !sp_nolink; sp_update(obj, 1); break;
    case -5: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_SI_NEW); break;
    case -6: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_SP_INSPECT); break;
    case -7: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_SP_MANAGE); break;
    case -8: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_SP_OPTIONS); break;
    case -1:
      if (in_box(&d->body, im->MouseX, im->MouseY) && im->MouseX > d->body.x1 - 8) {
        int n = rows_fit(d);
        d->first += im->MouseY < (d->body.y0 + d->body.y1) / 2 ? -n : n;
        MUI_Redraw(obj, MADF_DRAWOBJECT);
      }
      break;
    default: {
      int dbl = h == d->lastrow && DoubleClick(d->lastsec, d->lastmic, im->Seconds, im->Micros);
      d->lastrow = h; d->lastsec = im->Seconds; d->lastmic = im->Micros;
      sp_apply(d->rows[h], dbl);
      break; }
    }
    return MUI_EventHandlerRC_Eat;
  }
  if (im->Class == IDCMP_RAWKEY && (im->Code == 0x7A || im->Code == 0x7B) && inside) {
    d->first += im->Code == 0x7A ? -3 : 3;
    MUI_Redraw(obj, MADF_DRAWOBJECT);
    return MUI_EventHandlerRC_Eat;
  }
  return 0;
}
static const char *sp_help(struct SPData *d, WORD x, WORD y)
{
  static char body[500];
  int h = sp_hit(d, x, y);
  switch (h) {
  case -2: return theme_bubble(tr("Close"), tr("Hides the Styles pane (View > Styles Pane brings it back)."));
  case -3: return theme_bubble(tr("Show Preview"), tr("Each style's name in its own font, size and colour."));
  case -4: return theme_bubble(tr("Disable Linked Styles"), tr("Linked styles act as paragraph styles only; their character halves are listed apart."));
  case -5: return theme_bubble(tr("New Style"), tr("A new style from the formatting at the caret."));
  case -6: return theme_bubble(tr("Style Inspector"), tr("The paragraph and text formatting at the caret: its styles and what was added to them."));
  case -7: return theme_bubble(tr("Manage Styles"), tr("Edit, recommend, restrict styles and set the document's defaults."));
  case -8: return theme_bubble(tr("Options"), tr("Which styles the pane shows and in what order."));
  }
  if (h >= 0 && d->doc) {
    int k = d->rows[h];
    char name[64], desc[400];
    if (k < 0) return theme_bubble(tr("Clear All"), tr("The selection back to the Normal style, without its own formatting."));
    shown(d->doc, k, name, sizeof name);
    style_text(d->doc, k, desc, sizeof desc);
    snprintf(body, sizeof body, "%s", desc);
    return theme_bubble(name, body);
  }
  return NULL;
}
static ULONG spdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct SPData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->hot = -1; d->cur = d->ccur = -1; d->lastrow = -9;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Priority = 1;
    d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY;
    return (ULONG)obj;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 130; m->MinMaxInfo->DefWidth += 160; m->MinMaxInfo->MaxWidth += 200;
    m->MinMaxInfo->MinHeight += 160; m->MinMaxInfo->DefHeight += 300; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct SPData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup:
    theme_cleanup(obj);
    break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) sp_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Show: ((struct SPData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct SPData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_HandleEvent: return sp_events(cl, obj, (struct MUIP_HandleEvent *)msg);
  case MUIM_CreateShortHelp: {
    struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
    return (ULONG)sp_help(INST_DATA(cl, obj), (WORD)m->mx, (WORD)m->my);
  }
  case MUIM_DeleteShortHelp: return 0;
  }
  return DoSuperMethodA(cl, obj, msg);
}
/* the pane brought up to date with the current document and caret (force: its list made afresh) */
static void sp_update(Object *obj, int force)
{
  struct SPData *d;
  Doc *doc = wp_doc();
  int cur = -1, ccur = -1;
  if (!obj || !spcls) return;
  d = INST_DATA(spcls->mcc_Class, obj);
  if (doc) {
    const CharFmt *c = caret_cf(doc);
    Pos cp = get_pos(MUIA_Quill_Caret);
    cur = cp.para < doc->np ? doc->p[cp.para]->pf.style : -1;
    ccur = c->cstyle ? c->cstyle : -1;
    if (ccur >= 0 && !sp_nolink && doc->styles[ccur].x.link) ccur = doc->styles[ccur].x.link - 1;
  }
  if (force || doc != d->doc || !doc || doc->ver != d->ver || doc->nstyles != d->nst) {
    d->doc = doc;
    d->ver = doc ? doc->ver : 0; d->nst = doc ? doc->nstyles : 0;
    d->nrows = doc ? sp_rows(doc, d->rows) : 0;
    d->cur = cur; d->ccur = ccur;
    if (_win(obj) && d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
  } else if (cur != d->cur || ccur != d->ccur) {
    d->cur = cur; d->ccur = ccur;
    if (_win(obj) && d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
  }
}
/* whether the pane's window is still one of Quill's (closing a window disposes of what is docked in it) */
static int pane_alive(void)
{
  int i;
  if (!pane) return 0;
  for (i = 0; i < 16; i++) if (wp_window_obj(i) == panewin && panewin) return 1;
  pane = panewin = NULL;
  return 0;
}
static int pane_here(void) { return pane_alive() && panewin == wp_win(); }
/* View > Styles Pane: docked at the right of this window's page, or taken away */
static void toggle_pane(void)
{
  Object *wa = wp_workarea();
  if (pane_alive()) {
    Object *p = pane;
    int here = panewin == wp_win();
    /* (from the window it is in: that window's work area is the current one's only when it is the current window) */
    if (here && wa) {
      DoMethod(wa, MUIM_Group_InitChange);
      DoMethod(wa, OM_REMMEMBER, (ULONG)p);
      DoMethod(wa, MUIM_Group_ExitChange);
      MUI_DisposeObject(p);
      pane = panewin = NULL;
      wp_refresh();
      return;
    }
    beep();                              /* (shown in another window: close it there) */
    return;
  }
  if (!wa) return;
  if (!spcls && !(spcls = MUI_CreateCustomClass(NULL, MUIC_Area, NULL, sizeof(struct SPData), (APTR)spdispatch))) return;
  if (!(pane = NewObject(spcls->mcc_Class, NULL, MUIA_ShortHelp, (ULONG)" ", TAG_DONE))) return;
  panewin = wp_win();                    /* (first: laying the window out again asks the state hook, which checks it) */
  DoMethod(wa, MUIM_Group_InitChange);
  DoMethod(wa, OM_ADDMEMBER, (ULONG)pane);
  DoMethod(wa, MUIM_Group_ExitChange);
  sp_update(pane, 1);
  wp_refresh();
}

/* applying a style from the pane (dbl: open Modify Style) */
int wp13_style_dialog(int k, int newtype);
static void sp_apply(int k, int dbl)
{
  Doc *d = wp_doc();
  Pos c, a;
  if (!d) return;
  if (k < 0) {
    /* Clear All: Normal, and the text's own formatting gone */
    DoMethod(wp_view(), MUIM_Quill_Command, CMD_STYLE, 0);
    DoMethod(wp_view(), MUIM_Quill_Command, CMD_CLEARFMT, 0);
    return;
  }
  if (dbl) { wp13_style_dialog(k, 0); return; }
  c = get_pos(MUIA_Quill_Caret); a = get_pos(MUIA_Quill_Anchor);
  if (d->styles[k].type == ST_CHAR) { DoMethod(wp_view(), MUIM_Quill_Command, CMD_CHARSTYLE, (ULONG)k); return; }
  /* a linked style on part of one paragraph: its character half, as Word does */
  if (!sp_nolink && d->styles[k].x.link && style_ok(d, d->styles[k].x.link - 1) && pos_cmp(a, c) && a.para == c.para) {
    Pos lo = pos_cmp(a, c) < 0 ? a : c, hi = pos_cmp(a, c) < 0 ? c : a;
    if (lo.off > 0 || hi.off < d->p[hi.para]->len) { DoMethod(wp_view(), MUIM_Quill_Command, CMD_CHARSTYLE, (ULONG)(d->styles[k].x.link - 1)); return; }
  }
  DoMethod(wp_view(), MUIM_Quill_Command, CMD_STYLE, (ULONG)k);
}

/* ---- the Style Pane Options window */
static Object *optwin, *o_show, *o_sort, *o_doc;
static void open_options(void)
{
  static const char *shows[] = { "Recommended", "In use", "In current document", "All styles", NULL }, *sorts[] = { "Alphabetical", "As Recommended", "Font", "Based on", "By type", NULL };
  static const char *tshow[6], *tsort[7];
  Doc *d = wp_doc();
  if (!optwin) {
    Object *c = VGroup,
      Child, ColGroup(2),
        Child, Label1(tr("Select styles to show")), Child, o_show = mkcycle(trl(shows, tshow)),
        Child, Label1(tr("Select how list is sorted")), Child, o_sort = mkcycle(trl(sorts, tsort)),
      End,
      Child, HGroup, Child, o_doc = mkcheck(1), Child, LLabel1(tr("Only in this document")), Child, HSpace(0), End,
    End;
    if (!(optwin = wp_dialog("Style Pane Options", MAKE_ID('S','P','O','P'), c, "_OK", ID_SP_OPTGO))) return;
  }
  /* the document's own choice (kept in its settings), else the pane's */
  if (d && d->spsort) sp_sort = d->spsort == SPS_PRIORITY ? 1 : d->spsort == SPS_FONT ? 2 : d->spsort == SPS_BASEDON ? 3 : d->spsort == SPS_TYPE ? 4 : 0;
  if (d && d->spfilter) sp_show = (d->spfilter & 1) ? 3 : (d->spfilter & 8) ? 1 : (d->spfilter & 4) ? 2 : 0;
  set(o_show, MUIA_Cycle_Active, sp_show); set(o_sort, MUIA_Cycle_Active, sp_sort);
  set(optwin, MUIA_Window_Open, TRUE);
}
static void options_done(void)
{
  static const u8 sorts[5] = { SPS_NAME, SPS_PRIORITY, SPS_FONT, SPS_BASEDON, SPS_TYPE };
  static const u16 shows[4] = { 0x3F00 | 0x2000, 0x0008 | 0x0F00, 0x0004 | 0x0F00, 0x0001 | 0x0F00 };
  Doc *d = wp_doc();
  set(optwin, MUIA_Window_Open, FALSE);
  sp_show = (int)cyc(o_show); sp_sort = (int)cyc(o_sort);
  if (d && chk(o_doc)) {
    /* kept in the document's settings (w:stylePaneFormatFilter, w:stylePaneSortMethod), as Word keeps them */
    d->spsort = sorts[sp_sort]; d->spfilter = shows[sp_show];
    set(wp_view(), MUIA_Quill_Changed, TRUE);
  }
  if (pane_here()) sp_update(pane, 1);
}

/* ------------------------------------------------------------------ */
/* Style Inspector */
static Object *insw, *in_pstyle, *in_pplus, *in_cstyle, *in_cplus;
static void inspect_fill(void)
{
  Doc *d = wp_doc();
  Pos cp;
  const CharFmt *c;
  CharFmt sc;
  char b[400], n[64];
  int ps;
  unsigned m;
  if (!insw || !is_open(insw) || !d) return;
  cp = get_pos(MUIA_Quill_Caret);
  if (cp.para >= d->np) return;
  ps = d->p[cp.para]->pf.style;
  shown(d, ps < d->nstyles ? ps : 0, n, sizeof n);
  set(in_pstyle, MUIA_Text_Contents, (ULONG)n);
  m = pf_diff(&d->p[cp.para]->pf, &d->styles[ps < d->nstyles ? ps : 0].pf);
  pf_text(d, &d->p[cp.para]->pf, m, b, sizeof b);
  { char t[420]; snprintf(t, sizeof t, "%s %s", tr("Plus:"), b[0] ? b : tr("<none>")); set(in_pplus, MUIA_Text_Contents, (ULONG)t); }
  c = caret_cf(d);
  if (c->cstyle && style_ok(d, c->cstyle)) shown(d, c->cstyle, n, sizeof n); else strcpy(n, tr("Default Paragraph Font"));
  set(in_cstyle, MUIA_Text_Contents, (ULONG)n);
  m = cf_diff(c, style_cf_of(d, c, ps, &sc));
  cf_text(d, c, m, b, sizeof b);
  { char t[420]; snprintf(t, sizeof t, "%s %s", tr("Plus:"), b[0] ? b : tr("<none>")); set(in_cplus, MUIA_Text_Contents, (ULONG)t); }
}
static void open_inspector(void)
{
  if (!insw) {
    Object *rp, *cp, *rc, *cc, *nw, *rv, *cl;
    Object *c = VGroup, QDLG,
      Child, QTitleObject(tr("Paragraph formatting")),
      Child, HGroup, Child, in_pstyle = TextObject, TextFrame, MUIA_Background, MUII_TextBack, MUIA_Text_Contents, (ULONG)"", End,
        Child, rp = QButton(tr("Reset to _Normal")), End,
      Child, HGroup, Child, in_pplus = TextObject, MUIA_Text_Contents, (ULONG)"", MUIA_Text_SetMin, FALSE, End, Child, cp = QButton(tr("Clear _paragraph formatting")), End,
      Child, QTitleObject(tr("Text level formatting")),
      Child, HGroup, Child, in_cstyle = TextObject, TextFrame, MUIA_Background, MUII_TextBack, MUIA_Text_Contents, (ULONG)"", End,
        Child, rc = QButton(tr("_Default Paragraph Font")), End,
      Child, HGroup, Child, in_cplus = TextObject, MUIA_Text_Contents, (ULONG)"", MUIA_Text_SetMin, FALSE, End, Child, cc = QButton(tr("Clear _character formatting")), End,
      Child, HGroup, Child, nw = QButton(tr("_New Style...")), Child, rv = QButton(tr("_Reveal Formatting...")), Child, HSpace(0), Child, cl = QButton(tr("_Close")), End,
    End;
    if (!(insw = mkwin("Style Inspector", MAKE_ID('S','I','N','S'), c))) return;
    bt(rp, ID_SI_RESETP); bt(cp, ID_SI_CLEARP); bt(rc, ID_SI_RESETC); bt(cc, ID_SI_CLEARC); bt(nw, ID_SI_NEW); bt(rv, ID_SP_REVEAL);
    DoMethod(cl, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)insw, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  }
  set(insw, MUIA_Window_Open, TRUE);
  inspect_fill();
}
/* the inspector's buttons: the paragraph back to Normal or to its style, the text's character style
   off, or its own formatting off */
static void inspect_cmd(ULONG id)
{
  Doc *d = wp_doc();
  Pos cp = get_pos(MUIA_Quill_Caret);
  if (!d || cp.para >= d->np) return;
  if (id == ID_SI_RESETP) DoMethod(wp_view(), MUIM_Quill_Command, CMD_STYLE, 0);
  else if (id == ID_SI_CLEARP) {
    struct QParaFmt q;
    int ps = d->p[cp.para]->pf.style;
    q.mask = pf_diff(&d->p[cp.para]->pf, &d->styles[ps < d->nstyles ? ps : 0].pf) & PM_ALL;
    q.pf = d->styles[ps < d->nstyles ? ps : 0].pf;
    if (q.mask) DoMethod(wp_view(), MUIM_Quill_Command, CMD_PARAFMT, (ULONG)&q);
  }
  else if (id == ID_SI_RESETC) DoMethod(wp_view(), MUIM_Quill_Command, CMD_CHARSTYLE, 0);
  else if (id == ID_SI_CLEARC) DoMethod(wp_view(), MUIM_Quill_Command, CMD_CLEARFMT, 0);
  inspect_fill();
}

/* ---- Reveal Formatting */
static Object *revw, *rv_list, *rv_src;
static void rv_line(const char *s) { DoMethod(rv_list, MUIM_List_InsertSingle, (ULONG)s, MUIV_List_Insert_Bottom); }
static void rv_head(const char *s) { char b[80]; snprintf(b, sizeof b, "\33b%s", s); rv_line(b); }
static void rv_wrap(const char *head, const char *s)
{
  /* "Head: a, b, c" broken at the commas into lines of about 46 characters */
  char line[120];
  const char *p = s;
  snprintf(line, sizeof line, "  %s", head);
  rv_line(line);
  while (*p) {
    size_t n = strlen(p), cut = n;
    if (n > 44) { cut = 44; while (cut > 10 && p[cut] != ' ') cut--; if (cut <= 10) cut = 44; }
    snprintf(line, sizeof line, "      %.*s", (int)cut, p);
    rv_line(line);
    p += cut;
    while (*p == ' ') p++;
  }
}
static void reveal_fill(void)
{
  Doc *d = wp_doc();
  Pos cp;
  const CharFmt *c;
  CharFmt sc;
  const ParaFmt *pf;
  const SectFmt *sf;
  const PageSetup *pg;
  char b[600], v[40], w[40], x[40], y[40];
  int ps, src;
  if (!revw || !is_open(revw) || !d) return;
  cp = get_pos(MUIA_Quill_Caret);
  if (cp.para >= d->np) return;
  src = chk(rv_src);
  set(rv_list, MUIA_List_Quiet, TRUE);
  DoMethod(rv_list, MUIM_List_Clear);
  pf = &d->p[cp.para]->pf;
  ps = pf->style < d->nstyles ? pf->style : 0;
  c = caret_cf(d);
  rv_head(tr("Font"));
  cf_text(d, c, CM_FONT | CM_SIZE | CM_COLOR, b, sizeof b);
  rv_wrap(tr("Font:"), b);
  cf_text(d, c, (c->flags & 0x7F) | ((c->fx & FX_SMALLCAPS) ? CM_SMALLCAPS : 0) | (c->hl ? CM_HL : 0) | CM_SPACING | CM_POSITION, b, sizeof b);
  if (b[0]) rv_wrap(tr("Effects:"), b);
  if (src) {
    unsigned m = cf_diff(c, style_cf_of(d, c, ps, &sc));
    shown(d, ps, v, sizeof v);
    cf_text(d, c, m, b, sizeof b);
    snprintf(w, sizeof w, "%s", c->cstyle && style_ok(d, c->cstyle) ? "" : "-");
    if (c->cstyle && style_ok(d, c->cstyle)) shown(d, c->cstyle, w, sizeof w);
    { char t[200]; snprintf(t, sizeof t, "%s %s%s%s", tr("Style source:"), v, w[0] && strcmp(w, "-") ? " + " : "", strcmp(w, "-") ? w : ""); rv_wrap(t, b[0] ? b : tr("(no direct formatting)")); }
  }
  rv_head(tr("Paragraph"));
  shown(d, ps, v, sizeof v);
  { char t[100]; snprintf(t, sizeof t, "%s %s", tr("Paragraph Style:"), v); rv_wrap(t, ""); }
  pf_text(d, pf, PM_ALIGN, b, sizeof b); rv_wrap(tr("Alignment:"), b);
  pf_text(d, pf, PM_INDENT, b, sizeof b); rv_wrap(tr("Indentation:"), b);
  pf_text(d, pf, PM_SPACING | PM_LINE, b, sizeof b); rv_wrap(tr("Spacing:"), b);
  pf_text(d, pf, PM_KEEP | PM_PAGINATION | PM_BREAK | PM_CONTEXT, b, sizeof b); if (b[0]) rv_wrap(tr("Line and Page Breaks:"), b);
  pf_text(d, pf, PM_TABS | PM_BORDER | PM_SHADE | PM_LIST, b, sizeof b); if (b[0]) rv_wrap(tr("Other:"), b);
  if (src) {
    unsigned m = pf_diff(pf, &d->styles[ps].pf);
    pf_text(d, pf, m, b, sizeof b);
    rv_wrap(tr("Not from the style:"), b[0] ? b : tr("(no direct formatting)"));
  }
  rv_head(tr("Section"));
  sf = doc_sect(d, cp.para);
  pg = sect_page(d, sf, doc_last_sect(d, cp.para));
  tw_text(pg->left, v, 0); tw_text(pg->right, w, 0); tw_text(pg->top, x, 0); tw_text(pg->bottom, y, 0);
  snprintf(b, sizeof b, "%s %s, %s %s, %s %s, %s %s", tr("Left"), v, tr("Right"), w, tr("Top"), x, tr("Bottom"), y);
  rv_wrap(tr("Margins:"), b);
  tw_text(pg->width, v, 0); tw_text(pg->height, w, 0);
  snprintf(b, sizeof b, "%s x %s, %s %u", v, w, tr("Columns:"), (unsigned)(sf && sf->cols ? sf->cols : 1));
  rv_wrap(tr("Layout:"), b);
  set(rv_list, MUIA_List_Quiet, FALSE);
}
static void open_reveal(void)
{
  if (!revw) {
    Object *marks, *cl;
    Object *c = VGroup, QDLG,
      Child, QTitleObject(tr("Formatting of selected text")),
      Child, rv_list = ListviewObject, MUIA_Listview_Input, FALSE, MUIA_Listview_List, ListObject, ReadListFrame,
        MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
      Child, HGroup, Child, rv_src = mkcheck(0), Child, LLabel1(tr("Distinguish style source")), Child, HSpace(0), End,
      Child, HGroup, Child, marks = QButton(tr("Show _all formatting marks")), Child, HSpace(0), Child, cl = QButton(tr("_Close")), End,
    End;
    if (!(revw = mkwin("Reveal Formatting", MAKE_ID('R','E','V','F'), c))) return;
    DoMethod(rv_src, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_SP_REVEAL);
    DoMethod(marks, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_MARKS);
    DoMethod(cl, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)revw, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  }
  set(revw, MUIA_Window_Open, TRUE);
  reveal_fill();
}

/* ------------------------------------------------------------------ */
/* Modify Style / New Style: Word's window, with its Format pages */
#define NCOLORS 34
static const char *col_lab[NCOLORS + 2];
static char col_buf[NCOLORS][48];
static u32 col_rgb[NCOLORS], col_ref[NCOLORS];
static int ncol;
/* the colour list: the current colour first (if it isn't one of the others), Automatic, the theme's
   text and accent colours (and their darker and lighter shades), Word's standard colours */
static void colors_make(const Doc *d, u32 cur, u32 curref)
{
  static const u32 std[10] = { 0xC00000, 0xFF0000, 0xFFC000, 0xFFFF00, 0x92D050, 0x00B050, 0x00B0F0, 0x0070C0, 0x002060, 0x7030A0 };
  static const char *const stdn[10] = { "Dark Red", "Red", "Orange", "Yellow", "Light Green", "Green", "Light Blue", "Blue", "Dark Blue", "Purple" };
  int i, c;
  ncol = 0;
  col_rgb[ncol] = COLOR_AUTO; col_ref[ncol] = 0; snprintf(col_buf[ncol], 48, "%s", tr("Automatic")); ncol++;
  for (c = THC_TX1; c <= THC_TX2; c += 2) { col_ref[ncol] = THR(c, THM_NONE, 0, 0); col_rgb[ncol] = theme_rgb(theme_of(d), col_ref[ncol]); snprintf(col_buf[ncol], 48, "%s", tr(thc_label(c))); ncol++; }
  for (c = THC_ACCENT1; c <= THC_ACCENT6; c++) {
    static const struct { u8 m, a; } v[3] = { { THM_NONE, 0 }, { THM_SHADE, 0xBF }, { THM_TINT, 0x99 } };
    for (i = 0; i < 3; i++) {
      col_ref[ncol] = THR(c, v[i].m, v[i].a, 0); col_rgb[ncol] = theme_rgb(theme_of(d), col_ref[ncol]);
      color_name(d, col_rgb[ncol], col_ref[ncol], col_buf[ncol], 48);
      ncol++;
    }
  }
  for (i = 0; i < 10; i++) { col_rgb[ncol] = std[i]; col_ref[ncol] = 0; snprintf(col_buf[ncol], 48, "%s", tr(stdn[i])); ncol++; }
  /* the colour it has now, if it is none of those */
  for (i = 0; i < ncol && !(col_rgb[i] == cur && (col_ref[i] == curref || !theme_ref_ok(d, curref, cur) || !col_ref[i])); i++) ;
  if (i == ncol && ncol < NCOLORS) {
    memmove(col_rgb + 1, col_rgb, sizeof(u32) * (size_t)ncol); memmove(col_ref + 1, col_ref, sizeof(u32) * (size_t)ncol);
    memmove(col_buf + 1, col_buf, 48 * (size_t)ncol);
    col_rgb[0] = cur; col_ref[0] = theme_ref_ok(d, curref, cur) ? curref : 0; color_name(d, cur, col_ref[0], col_buf[0], 48);
    ncol++;
  }
  for (i = 0; i < ncol; i++) col_lab[i] = col_buf[i];
  col_lab[ncol] = NULL;
}
static int color_index(u32 rgb, u32 ref, const Doc *d)
{
  int i;
  for (i = 0; i < ncol; i++) if (col_rgb[i] == rgb && (!col_ref[i] || (theme_ref_ok(d, ref, rgb) && col_ref[i] == ref))) return i;
  for (i = 0; i < ncol; i++) if (col_rgb[i] == rgb) return i;
  return 0;
}
static const char *const langs[] = { "en-US", "en-GB", "de-DE", "de-AT", "de-CH", "fr-FR", "fr-CH", "es-ES", "it-IT", "nl-NL", "sv-SE", "da-DK", "nb-NO",
  "fi-FI", "pt-BR", "pt-PT", "pl-PL", "cs-CZ", "hu-HU", NULL };
static const char *const lang_names[] = { "(none)", "English (United States)", "English (United Kingdom)", "German (Germany)", "German (Austria)", "German (Switzerland)",
  "French (France)", "French (Switzerland)", "Spanish (Spain)", "Italian (Italy)", "Dutch (Netherlands)", "Swedish (Sweden)", "Danish", "Norwegian (Bokm\345l)",
  "Finnish", "Portuguese (Brazil)", "Portuguese (Portugal)", "Polish", "Czech", "Hungarian", NULL };
/* the shortcut keys a style can have: Word's modifiers (2 Ctrl, 4 Alt, 1 Shift) with a digit */
static u16 key_of(int i) { return i <= 0 ? 0 : i <= 10 ? (u16)(0x0600 | (i == 10 ? '0' : '0' + i)) : (u16)(0x0300 | (i == 20 ? '0' : '0' + i - 10)); }
static const char *keys_lab[23];
static char keys_buf[21][24];

static Object *sdw, *sd_name, *sd_type, *sd_based, *sd_next, *sd_font, *sd_size, *sd_bold, *sd_ital, *sd_ul, *sd_color, *sd_align, *sd_line,
              *sd_before, *sd_after, *sd_pages, *sd_page, *sd_quick, *sd_auto, *sd_tpl, *sd_desc,
              *sd_left, *sd_right, *sd_special, *sd_by, *sd_keep, *sd_lines, *sd_brk, *sd_widow, *sd_ctx,
              *sd_tabs, *sd_tpos, *sd_talign, *sd_tlead,
              *sd_bside[5], *sd_bstyle, *sd_bwidth, *sd_bcolor, *sd_bspace, *sd_shade,
              *sd_nkind, *sd_nlevel,
              *sd_frame, *sd_fw, *sd_fhrule, *sd_fh, *sd_fhpos, *sd_fx, *sd_fhrel, *sd_fvpos, *sd_fy, *sd_fvrel, *sd_fwrap, *sd_fhd, *sd_fvd,
              *sd_lang, *sd_key, *sd_keynow;
static int sd_k, sd_type0, sd_nk0;        /* the style changed (-1 new), its kind, its numbering's kind as it was */
static Style sd_s;                        /* what it starts as */
static ParaFmt sd_tabpf;                  /* the tab stops being edited */
static const char *sd_bn[MAX_STYLES + 2], *sd_nn[MAX_STYLES + 2];
static int sd_bi[MAX_STYLES + 2], sd_ni[MAX_STYLES + 2];
static char sd_names[MAX_STYLES][48];
static void tabs_fill(void)
{
  int k;
  static const char *const al[4] = { "Left", "Centre", "Right", "Decimal" };
  DoMethod(sd_tabs, MUIM_List_Clear);
  for (k = 0; k < 8 && sd_tabpf.tabs[k]; k++) {
    char b[80], v[24];
    tw_text(sd_tabpf.tabs[k], v, 0);
    snprintf(b, sizeof b, "%s  %s", v, tr(al[sd_tabpf.tabkind[k] & 3]));
    DoMethod(sd_tabs, MUIM_List_InsertSingle, (ULONG)b, MUIV_List_Insert_Bottom);
  }
}
static void tabs_cmd(ULONG id)
{
  ParaFmt *p = &sd_tabpf;
  int k, n;
  for (n = 0; n < 8 && p->tabs[n]; n++) ;
  if (id == ID_SD_TABALL) { memset(p->tabs, 0, sizeof p->tabs); memset(p->tabkind, 0, sizeof p->tabkind); memset(p->tablead, 0, sizeof p->tablead); }
  else if (id == ID_SD_TABCLR) {
    LONG a = active(sd_tabs);
    if (a < 0 || a >= n) { beep(); return; }
    for (k = (int)a; k < 7; k++) { p->tabs[k] = p->tabs[k + 1]; p->tabkind[k] = p->tabkind[k + 1]; p->tablead[k] = p->tablead[k + 1]; }
    p->tabs[7] = 0;
  } else if (id == ID_SD_TABPICK) {
    LONG a = active(sd_tabs);
    char v[24];
    if (a < 0 || a >= n) return;
    tw_text(p->tabs[a], v, 0); set(sd_tpos, MUIA_String_Contents, (ULONG)v);
    set(sd_talign, MUIA_Cycle_Active, p->tabkind[a] & 3); set(sd_tlead, MUIA_Cycle_Active, p->tablead[a] & 3);
    return;
  } else {
    i32 pos = parse_tw(get_str(sd_tpos), 0);
    if (pos <= 0) { beep(); return; }
    for (k = 0; k < n && p->tabs[k] != pos; k++) ;
    if (k == n) { if (n >= 8) { beep(); return; } n++; }
    p->tabs[k] = pos; p->tabkind[k] = (u8)cyc(sd_talign); p->tablead[k] = (u8)cyc(sd_tlead);
    /* in order of position */
    for (k = 1; k < n; k++) {
      int j = k;
      while (j > 0 && p->tabs[j - 1] > p->tabs[j]) {
        i32 t = p->tabs[j]; u8 a = p->tabkind[j], l = p->tablead[j];
        p->tabs[j] = p->tabs[j - 1]; p->tabkind[j] = p->tabkind[j - 1]; p->tablead[j] = p->tablead[j - 1];
        p->tabs[j - 1] = t; p->tabkind[j - 1] = a; p->tablead[j - 1] = l;
        j--;
      }
    }
  }
  tabs_fill();
}
static void sd_keynow_fill(void)
{
  Doc *d = wp_doc();
  u16 key = key_of((int)cyc(sd_key));
  char b[120], n[60];
  int k;
  b[0] = 0;
  for (k = 0; d && key && k < d->nstyles; k++)
    if (k != sd_k && style_ok(d, k) && d->styles[k].x.key == key) { shown(d, k, n, sizeof n); snprintf(b, sizeof b, "%s %s", tr("Currently assigned to:"), n); }
  if (!b[0]) snprintf(b, sizeof b, "%s", key ? tr("Currently assigned to: [unassigned]") : "");
  set(sd_keynow, MUIA_Text_Contents, (ULONG)b);
}
static Object *num_field(void) { return StringObject, StringFrame, MUIA_String_MaxLen, 14, MUIA_CycleChain, 1, MUIA_FixWidthTxt, (ULONG)"00000000000", End; }
static Object *style_dialog_build(int type)
{
  static const char *types[] = { "Paragraph", "Character", "Linked (paragraph and character)", NULL };
  static const char *aligns[] = { "Left", "Centred", "Right", "Justified", NULL }, *lines[] = { "Single", "1.15", "1.5 lines", "Double", "At least", "Exactly", NULL };
  static const char *pages[] = { "Paragraph", "Tabs", "Borders", "Numbering", "Frame", "Language", "Shortcut key", NULL };
  static const char *specials[] = { "(none)", "First line", "Hanging", NULL }, *tal[] = { "Left", "Centre", "Right", "Decimal", NULL },
               *tld[] = { "None", "......", "------", "______", NULL }, *sides[5] = { "Top", "Bottom", "Left", "Right", "Between" },
               *bst[] = { "Single", "Double", "Thick", "Dotted", "Dashed", NULL }, *bwd[] = { "\275 pt", "\276 pt", "1 pt", "1\275 pt", "2\275 pt", "3 pt", NULL },
               *nkinds[] = { "None", "Bullets", "1. a. i.", "1) a) i)", "I. A. 1.", "1. 1.1. 1.1.1.", "Heading numbering", NULL },
               *levels[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", NULL },
               *hrules[] = { "Auto", "At least", "Exactly", NULL }, *hpos[] = { "Position", "Left", "Centre", "Right", "Inside", "Outside", NULL },
               *vpos[] = { "Position", "Top", "Centre", "Bottom", "Inside", "Outside", NULL }, *hrel[] = { "Column", "Margin", "Page", NULL },
               *vrel[] = { "Paragraph", "Margin", "Page", NULL }, *wraps[] = { "Around", "None", NULL };
  static const char *t_types[4], *t_al[5], *t_ln[7], *t_pg[8], *t_sp[4], *t_tal[5], *t_bst[6], *t_nk[8], *t_hr[4], *t_hp[7], *t_vp[7], *t_hrel[4], *t_vrel[4], *t_wr[3], *t_ln2[22];
  Object *c, *tset, *tclr, *tall, *pg_para, *pg_tabs, *pg_bord, *pg_num, *pg_frame, *pg_lang, *pg_key, *bsides;
  int i;
  for (i = 0; lang_names[i]; i++) t_ln2[i] = tr(lang_names[i]);
  t_ln2[i] = NULL;
  keys_lab[0] = tr("(none)");
  for (i = 1; i <= 20; i++) { stylekey_name(key_of(i), keys_buf[i], sizeof keys_buf[i]); keys_lab[i] = keys_buf[i]; }
  keys_lab[21] = NULL;
  pg_para = ColGroup(4),
    Child, Label2(tr("Left indent")), Child, sd_left = num_field(), Child, Label2(tr("Right indent")), Child, sd_right = num_field(),
    Child, Label1(tr("Special")), Child, sd_special = mkcycle(trl(specials, t_sp)), Child, Label2(tr("By")), Child, sd_by = num_field(),
    Child, HGroup, Child, sd_keep = mkcheck(0), Child, LLabel1(tr("Keep with next")), End, Child, HSpace(0),
    Child, HGroup, Child, sd_lines = mkcheck(0), Child, LLabel1(tr("Keep lines together")), End, Child, HSpace(0),
    Child, HGroup, Child, sd_brk = mkcheck(0), Child, LLabel1(tr("Page break before")), End, Child, HSpace(0),
    Child, HGroup, Child, sd_widow = mkcheck(1), Child, LLabel1(tr("Widow/Orphan control")), End, Child, HSpace(0),
    Child, HGroup, Child, sd_ctx = mkcheck(0), Child, LLabel1(tr("No space between paragraphs of the same style")), End, Child, HSpace(0),
    Child, HSpace(0), Child, HSpace(0),
  End;
  pg_tabs = HGroup,
    Child, sd_tabs = ListviewObject, MUIA_CycleChain, 1, MUIA_Listview_List, ListObject, InputListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
      MUIA_List_DestructHook, MUIV_List_DestructHook_String, End, End,
    Child, VGroup,
      Child, ColGroup(2), Child, Label2(tr("Tab stop position")), Child, sd_tpos = num_field(),
        Child, Label1(tr("Alignment")), Child, sd_talign = mkcycle(trl(tal, t_tal)),
        Child, Label1(tr("Leader")), Child, sd_tlead = mkcycle(tld), End,
      Child, HGroup, Child, tset = QButton(tr("_Set")), Child, tclr = QButton(tr("C_lear")), Child, tall = QButton(tr("Clear _All")), End,
    End,
  End;
  bsides = HGroup, End;
  for (i = 0; i < 5 && bsides; i++) {
    Object *cb = mkcheck(0), *lb = LLabel1(tr(sides[i]));
    sd_bside[i] = cb;
    if (cb && lb) { DoMethod(bsides, OM_ADDMEMBER, (ULONG)cb); DoMethod(bsides, OM_ADDMEMBER, (ULONG)lb); }
  }
  pg_bord = VGroup,
    Child, bsides,
    Child, ColGroup(4),
      Child, Label1(tr("Style")), Child, sd_bstyle = mkcycle(trl(bst, t_bst)), Child, Label1(tr("Width")), Child, sd_bwidth = mkcycle(bwd),
      Child, Label1(tr("Colour")), Child, sd_bcolor = mkcycle(col_lab), Child, Label2(tr("Spacing")), Child, sd_bspace = num_field(),
      Child, Label1(tr("Shading")), Child, sd_shade = mkcycle(col_lab), Child, HSpace(0), Child, HSpace(0),
    End,
  End;
  pg_num = ColGroup(2), Child, Label1(tr("Numbering")), Child, sd_nkind = mkcycle(trl(nkinds, t_nk)), Child, Label1(tr("List level")), Child, sd_nlevel = mkcycle(levels), End;
  pg_frame = VGroup,
    Child, HGroup, Child, sd_frame = mkcheck(0), Child, LLabel1(tr("Frame round the paragraphs")), Child, HSpace(0), End,
    Child, ColGroup(6),
      Child, Label2(tr("Width")), Child, sd_fw = num_field(), Child, Label1(tr("Height")), Child, sd_fhrule = mkcycle(trl(hrules, t_hr)), Child, Label2(tr("At")), Child, sd_fh = num_field(),
      Child, Label1(tr("Horizontal")), Child, sd_fhpos = mkcycle(trl(hpos, t_hp)), Child, Label2(tr("At")), Child, sd_fx = num_field(), Child, Label1(tr("Relative to")), Child, sd_fhrel = mkcycle(trl(hrel, t_hrel)),
      Child, Label1(tr("Vertical")), Child, sd_fvpos = mkcycle(trl(vpos, t_vp)), Child, Label2(tr("At")), Child, sd_fy = num_field(), Child, Label1(tr("Relative to")), Child, sd_fvrel = mkcycle(trl(vrel, t_vrel)),
      Child, Label1(tr("Text wrapping")), Child, sd_fwrap = mkcycle(trl(wraps, t_wr)), Child, Label2(tr("Distance")), Child, sd_fhd = num_field(), Child, Label2(tr("Vertical")), Child, sd_fvd = num_field(),
    End,
  End;
  pg_lang = ColGroup(2), Child, Label1(tr("Language")), Child, sd_lang = mkcycle(t_ln2), Child, HSpace(0), Child, HSpace(0), End;
  pg_key = VGroup, Child, ColGroup(2), Child, Label1(tr("Shortcut key")), Child, sd_key = mkcycle(keys_lab), End,
    Child, sd_keynow = TextObject, MUIA_Text_Contents, (ULONG)"", End, End;
  c = VGroup,
    Child, QTitleObject(tr("Properties")),
    Child, ColGroup(4),
      Child, Label2(tr("Name")), Child, sd_name = mkstring(46), Child, Label1(tr("Style type")), Child, sd_type = mkcycle(trl(types, t_types)),
      Child, Label1(tr("Style based on")), Child, sd_based = mkcycle(sd_bn), Child, Label1(tr("Following")), Child, sd_next = mkcycle(sd_nn),
    End,
    Child, QTitleObject(tr("Formatting")),
    Child, ColGroup(6),
      Child, Label2(tr("Font")), Child, sd_font = mkstring(46), Child, Label2(tr("Size")), Child, sd_size = num_field(), Child, Label1(tr("Colour")), Child, sd_color = mkcycle(col_lab),
      Child, Label1(tr("Alignment")), Child, sd_align = mkcycle(trl(aligns, t_al)), Child, Label1(tr("Line spacing")), Child, sd_line = mkcycle(trl(lines, t_ln)),
        Child, HGroup, Child, sd_bold = mkcheck(0), Child, LLabel1(tr("Bold")), Child, sd_ital = mkcheck(0), Child, LLabel1(tr("Italic")), End,
        Child, HGroup, Child, sd_ul = mkcheck(0), Child, LLabel1(tr("Underline")), End,
      Child, Label2(tr("Before")), Child, sd_before = num_field(), Child, Label2(tr("After")), Child, sd_after = num_field(), Child, HSpace(0), Child, HSpace(0),
    End,
    Child, HGroup, Child, Label1(tr("Format")), Child, sd_pages = mkcycle(trl(pages, t_pg)), Child, HSpace(0), End,
    Child, sd_page = VGroup, GroupFrame, MUIA_Group_PageMode, TRUE,
      Child, pg_para, Child, pg_tabs, Child, pg_bord, Child, pg_num, Child, pg_frame, Child, pg_lang, Child, pg_key,
    End,
    Child, HGroup, Child, sd_quick = mkcheck(1), Child, LLabel1(tr("Add to the Styles gallery")), Child, sd_auto = mkcheck(0), Child, LLabel1(tr("Automatically update")),
      Child, sd_tpl = mkcheck(0), Child, LLabel1(tr("New documents based on this template")), Child, HSpace(0), End,
    Child, sd_desc = TextObject, TextFrame, MUIA_Background, MUII_TextBack, MUIA_Text_SetMin, FALSE, MUIA_Text_Contents, (ULONG)"", End,
  End;
  if (!c) return NULL;
  bt(tset, ID_SD_TABSET); bt(tclr, ID_SD_TABCLR); bt(tall, ID_SD_TABALL);
  DoMethod(sd_tabs, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_SD_TABPICK);
  DoMethod(sd_pages, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)sd_page, 3, MUIM_Set, MUIA_Group_ActivePage, MUIV_TriggerValue);
  DoMethod(sd_key, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_SD_PAGE);
  DoMethod(sd_type, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_SD_TYPE);
  return c;
}
/* the controls a character style has no use for */
static void sd_enable(int para)
{
  Object *const po[] = { sd_next, sd_align, sd_line, sd_before, sd_after, sd_pages, sd_page, sd_auto };
  unsigned i;
  for (i = 0; i < sizeof po / sizeof po[0]; i++) if (po[i]) set(po[i], MUIA_Disabled, !para);
}
/* (main.c's ID_MODSTYLE, ID_NEWSTYLE and ID_STYLELIST) the window for style k (-1: a new one of type
   newtype, from the formatting at the caret); 0 if it couldn't be made */
int wp13_style_dialog(int k, int newtype)
{
  static const i32 linevals[4] = { 240, 276, 360, 480 };
  Doc *d = wp_doc();
  Style *s = &sd_s;
  Pos cp = get_pos(MUIA_Quill_Caret);
  int i, nb = 0, nn = 0, type;
  char b[100];
  if (!d) return 0;
  if (sdw) { set(sdw, MUIA_Window_Open, FALSE); DoMethod(wp_app(), OM_REMMEMBER, (ULONG)sdw); MUI_DisposeObject(sdw); sdw = NULL; }
  if (k >= 0 && !style_ok(d, k)) return 0;
  sd_k = k;
  if (k >= 0) *s = d->styles[k];
  else {
    int base = cp.para < d->np ? d->p[cp.para]->pf.style : 0;
    doc_style_from(d, cp.para < d->np ? cp : doc_start(d), base, s);
    if (newtype == ST_CHAR) {
      const CharFmt *f = caret_cf(d), *pc = &d->styles[base < d->nstyles ? base : 0].cf;
      memset(s, 0, sizeof *s);
      s->type = ST_CHAR; s->based = NO_STYLE; s->quick = 1;
      s->cf = *f; s->ocf = *f;
      if (f->font != pc->font) s->cmask |= CM_FONT;
      if (f->size != pc->size) s->cmask |= CM_SIZE;
      if (f->color != pc->color) s->cmask |= CM_COLOR;
      s->cmask |= (f->flags ^ pc->flags) & 0x7F;
    }
    strcpy(s->name, newtype == ST_CHAR ? "Style1" : "Style1");
    for (i = 2; doc_style_by_name(d, s->name) >= 0 && i < 999; i++) sprintf(s->name, "Style%d", i);
    memset(&s->x, 0, sizeof s->x);
  }
  type = s->type;
  sd_type0 = type;
  /* the styles it can be based on, and follow */
  sd_bn[nb] = tr("(no style)"); sd_bi[nb++] = NO_STYLE;
  for (i = 0; i < d->nstyles; i++) {
    if (!style_ok(d, i)) continue;
    shown(d, i, sd_names[i], sizeof sd_names[i]);
    if (d->styles[i].type == type && i != k) { sd_bn[nb] = sd_names[i]; sd_bi[nb++] = i; }
    if (d->styles[i].type == ST_PARA) { sd_nn[nn] = sd_names[i]; sd_ni[nn++] = i; }
  }
  sd_bn[nb] = NULL; sd_nn[nn] = NULL;
  colors_make(d, s->cf.color, s->cf.thcolor);
  {
    Object *c = style_dialog_build(type);
    if (!c || !(sdw = wp_dialog(k >= 0 ? "Modify Style" : "Create New Style from Formatting", MAKE_ID('W','S','T','Y'), c, "_OK", ID_SD_OK))) return 0;
  }
  if (k >= 0) shown(d, k, b, sizeof b); else lat(s->name, b, sizeof b);
  set(sd_name, MUIA_String_Contents, (ULONG)b);
  set(sd_type, MUIA_Cycle_Active, type == ST_CHAR ? 1 : s->x.link ? 2 : 0);
  set(sd_type, MUIA_Disabled, k >= 0);
  for (i = 0; i < nb && sd_bi[i] != s->based; i++) ;
  set(sd_based, MUIA_Cycle_Active, i < nb ? i : 0);
  for (i = 0; i < nn && sd_ni[i] != (k >= 0 ? s->next : (cp.para < d->np ? d->styles[d->p[cp.para]->pf.style].next : 0)); i++) ;
  set(sd_next, MUIA_Cycle_Active, i < nn ? i : 0);
  set_lat(sd_font, s->cf.font < d->nfonts ? d->fonts[s->cf.font] : "Times New Roman");
  sprintf(b, s->cf.size & 1 ? "%u.5" : "%u", (unsigned)(s->cf.size / 2)); set(sd_size, MUIA_String_Contents, (ULONG)b);
  set(sd_color, MUIA_Cycle_Active, color_index(s->cf.color, s->cf.thcolor, d));
  set(sd_bold, MUIA_Selected, (s->cf.flags & CF_BOLD) != 0); set(sd_ital, MUIA_Selected, (s->cf.flags & CF_ITALIC) != 0);
  set(sd_ul, MUIA_Selected, (s->cf.flags & CF_UNDERLINE) != 0);
  set(sd_align, MUIA_Cycle_Active, s->pf.align & 3);
  for (i = 0; i < 4 && !(s->pf.linerule == LS_AUTO && linevals[i] == s->pf.line); i++) ;
  set(sd_line, MUIA_Cycle_Active, i < 4 ? i : s->pf.linerule == LS_EXACT ? 5 : 4);
  tw_text(s->pf.before, b, 1); set(sd_before, MUIA_String_Contents, (ULONG)b);
  tw_text(s->pf.after, b, 1); set(sd_after, MUIA_String_Contents, (ULONG)b);
  tw_text(s->pf.left, b, 0); set(sd_left, MUIA_String_Contents, (ULONG)b);
  tw_text(s->pf.right, b, 0); set(sd_right, MUIA_String_Contents, (ULONG)b);
  set(sd_special, MUIA_Cycle_Active, s->pf.first > 0 ? 1 : s->pf.first < 0 ? 2 : 0);
  tw_text(s->pf.first < 0 ? -s->pf.first : s->pf.first, b, 0); set(sd_by, MUIA_String_Contents, (ULONG)b);
  set(sd_keep, MUIA_Selected, s->pf.keepnext != 0); set(sd_lines, MUIA_Selected, s->pf.keeplines != 0);
  set(sd_brk, MUIA_Selected, s->pf.pagebreak != 0); set(sd_widow, MUIA_Selected, !s->pf.nowidow); set(sd_ctx, MUIA_Selected, s->pf.contextual != 0);
  sd_tabpf = s->pf;
  tabs_fill();
  {
    /* borders: the sides that have a line, the first one's line */
    static const int pe[5] = { PE_TOP, PE_BOTTOM, PE_LEFT, PE_RIGHT, PE_BETWEEN };
    static const u8 widths[6] = { 4, 6, 8, 12, 20, 24 };
    int first = -1, w;
    for (i = 0; i < 5; i++) { set(sd_bside[i], MUIA_Selected, s->pf.bstyle[pe[i]] != PB_NONE); if (first < 0 && s->pf.bstyle[pe[i]]) first = pe[i]; }
    if (first >= 0) {
      set(sd_bstyle, MUIA_Cycle_Active, s->pf.bstyle[first] > 0 ? s->pf.bstyle[first] - 1 : 0);
      for (w = 0; w < 5 && widths[w] < s->pf.bwidth[first]; w++) ;
      set(sd_bwidth, MUIA_Cycle_Active, w);
      set(sd_bcolor, MUIA_Cycle_Active, color_index(s->pf.bcolor[first], s->pf.thbcolor[first], d));
      sprintf(b, "%u pt", (unsigned)s->pf.bspace[first]); set(sd_bspace, MUIA_String_Contents, (ULONG)b);
    } else set(sd_bspace, MUIA_String_Contents, (ULONG)"1 pt");
    set(sd_shade, MUIA_Cycle_Active, s->pf.shade ? color_index(s->pf.shade & 0xFFFFFF, s->pf.thshade, d) : 0);
  }
  {
    /* numbering: none, or the kind of the style's list */
    int nk = 0;
    if ((s->pmask & PM_LIST) && s->pf.list && s->pf.list <= d->nlists) {
      const ListDef *l = &d->lists[s->pf.list - 1];
      const ListLevel *lv = &l->lvl[0];
      nk = lv->fmt == NF_BULLET ? 1 : lv->fmt == NF_UPPER_ROMAN ? 4 : strstr(l->lvl[1].text, "%1.%2") ? 5 : strchr(lv->text, ')') ? 3 : 2;
    }
    sd_nk0 = nk;
    set(sd_nkind, MUIA_Cycle_Active, nk);
    set(sd_nlevel, MUIA_Cycle_Active, s->pf.level < 9 ? s->pf.level : 0);
  }
  {
    const StyleFrame *f = &s->x.frame;
    set(sd_frame, MUIA_Selected, f->set != 0);
    if (f->w) tw_text(f->w, b, 0); else strcpy(b, tr("Auto"));
    set(sd_fw, MUIA_String_Contents, (ULONG)b);
    set(sd_fhrule, MUIA_Cycle_Active, !f->h ? 0 : f->h > 0 ? 1 : 2);
    tw_text(f->h < 0 ? -f->h : f->h, b, 0); set(sd_fh, MUIA_String_Contents, (ULONG)b);
    set(sd_fhpos, MUIA_Cycle_Active, f->xalign < 6 ? f->xalign : 0); tw_text(f->x, b, 0); set(sd_fx, MUIA_String_Contents, (ULONG)b);
    set(sd_fhrel, MUIA_Cycle_Active, f->hanchor == 0 ? 0 : f->hanchor == 1 ? 1 : 2);
    set(sd_fvpos, MUIA_Cycle_Active, f->yalign < 6 ? f->yalign : 0); tw_text(f->y, b, 0); set(sd_fy, MUIA_String_Contents, (ULONG)b);
    set(sd_fvrel, MUIA_Cycle_Active, f->vanchor == 0 ? 0 : f->vanchor == 1 ? 1 : 2);
    set(sd_fwrap, MUIA_Cycle_Active, f->wrap == 2 ? 1 : 0);
    tw_text(f->hspace, b, 0); set(sd_fhd, MUIA_String_Contents, (ULONG)b);
    tw_text(f->vspace, b, 0); set(sd_fvd, MUIA_String_Contents, (ULONG)b);
  }
  for (i = 0; langs[i] && !str_ieq(langs[i], plang_tag(s->cf.lang)); i++) ;
  set(sd_lang, MUIA_Cycle_Active, s->cf.lang && langs[i] ? i + 1 : 0);
  for (i = 1; i <= 20 && key_of(i) != s->x.key; i++) ;
  set(sd_key, MUIA_Cycle_Active, s->x.key && i <= 20 ? i : 0);
  sd_keynow_fill();
  set(sd_quick, MUIA_Selected, s->quick != 0);
  set(sd_auto, MUIA_Selected, (s->x.flags & SXF_AUTOREDEFINE) != 0);
  set(sd_tpl, MUIA_Disabled, !(d->attached && d->attached[0]));
  if (k >= 0) { char desc[400]; style_text(d, k, desc, sizeof desc); set(sd_desc, MUIA_Text_Contents, (ULONG)desc); }
  sd_enable(type == ST_PARA);
  set(sdw, MUIA_Window_Open, TRUE);
  set(sdw, MUIA_Window_ActiveObject, (ULONG)sd_name);
  return 1;
}
/* a field set by the style itself only where it differs from its base's */
#define OWN_C(bit, cond, assign) do { if (cond) { s->cmask |= (bit); assign; } else s->cmask &= ~(unsigned)(bit); } while (0)
#define OWN_P(bit) do { if (pf_diff(&n, bp) & (bit)) { s->pmask |= (bit); pf_apply(&s->opf, (bit), &n); } else s->pmask &= ~(unsigned)(bit); } while (0)
/* the template the document is attached to gets the style too ("New documents based on this template") */
static void style_to_template(Doc *d, const char *name)
{
  unsigned lost;
  char err[200];
  Doc *t;
  const char *names[1];
  if (!d->attached || !d->attached[0] || !(t = doc_load_file(d->attached, &lost, err, sizeof err))) { beep(); return; }
  names[0] = name;
  if (doc_copy_styles(t, d, names, 1) && !template_save(t, d->attached, t->tplkind == TK_DOTM ? TK_DOTM : TK_DOTX)) beep();
  doc_free(t);
}
static void style_ok_cmd(void)
{
  static const i32 linevals[4] = { 240, 276, 360, 480 };
  static const int pe[5] = { PE_TOP, PE_BOTTOM, PE_LEFT, PE_RIGHT, PE_BETWEEN };
  static const u8 widths[6] = { 4, 6, 8, 12, 20, 24 };
  Doc *d = wp_doc();
  struct QStyle q;
  Style *s = &q.s;
  const ParaFmt *bp;
  const CharFmt *bc;
  ParaFmt n;
  int linked, i;
  i32 sz;
  if (!d) return;
  set(sdw, MUIA_Window_Open, FALSE);
  q.k = sd_k; *s = sd_s;
  get_utf8(sd_name, s->name, sizeof s->name);
  if (!s->name[0]) { beep(); set(sdw, MUIA_Window_Open, TRUE); return; }
  if (sd_k >= 0 && str_ieq(s->name, d->styles[sd_k].name)) strcpy(s->name, d->styles[sd_k].name);
  if (sd_k < 0) s->id[0] = 0;
  if (doc_style_by_name(d, s->name) >= 0 && doc_style_by_name(d, s->name) != sd_k) { beep(); set(sdw, MUIA_Window_Open, TRUE); return; }
  linked = sd_k < 0 && cyc(sd_type) == 2;
  if (sd_k < 0) s->type = (u8)(cyc(sd_type) == 1 ? ST_CHAR : ST_PARA);
  s->based = (u8)sd_bi[cyc(sd_based)];
  if (sd_k == 0) s->based = NO_STYLE;
  bp = s->based != NO_STYLE && s->based < d->nstyles ? &d->styles[s->based].pf : &d->defpf;
  bc = s->based != NO_STYLE && s->based < d->nstyles ? &d->styles[s->based].cf : &d->defcf;
  if (s->type == ST_PARA) s->next = (u8)sd_ni[cyc(sd_next)];
  s->quick = (u8)chk(sd_quick);
  if (chk(sd_auto)) s->x.flags |= SXF_AUTOREDEFINE; else s->x.flags &= (u8)~SXF_AUTOREDEFINE;
  /* character formatting */
  {
    char fam[100];
    u8 font;
    get_utf8(sd_font, fam, sizeof fam);
    font = (u8)doc_font(d, fam[0] ? fam : "Times New Roman");
    OWN_C(CM_FONT, font != bc->font, (s->ocf.font = font, s->ocf.thfont = 0));
    if (font == s->cf.font && theme_font_ok(d, s->cf.thfont, s->cf.font)) s->ocf.thfont = s->cf.thfont;    /* (the theme's font kept) */
  }
  {
    const char *t = get_str(sd_size);
    long ip = 0; int half = 0;
    while (*t >= '0' && *t <= '9') ip = ip * 10 + (*t++ - '0');
    if ((*t == '.' || *t == ',') && t[1] >= '5') half = 1;
    sz = (i32)(ip * 2 + half);
    if (sz < 2 || sz > 3276) sz = s->cf.size;
  }
  OWN_C(CM_SIZE, sz != bc->size, s->ocf.size = (u16)sz);
  i = (int)cyc(sd_color);
  OWN_C(CM_COLOR, col_rgb[i] != bc->color || (col_ref[i] && col_ref[i] != bc->thcolor), (s->ocf.color = col_rgb[i], s->ocf.thcolor = col_ref[i]));
  OWN_C(CM_BOLD, (chk(sd_bold) ? CF_BOLD : 0) != (bc->flags & CF_BOLD), s->ocf.flags = (u8)((s->ocf.flags & ~CF_BOLD) | (chk(sd_bold) ? CF_BOLD : 0)));
  OWN_C(CM_ITALIC, (chk(sd_ital) ? CF_ITALIC : 0) != (bc->flags & CF_ITALIC), s->ocf.flags = (u8)((s->ocf.flags & ~CF_ITALIC) | (chk(sd_ital) ? CF_ITALIC : 0)));
  OWN_C(CM_UNDERLINE, (chk(sd_ul) ? CF_UNDERLINE : 0) != (bc->flags & CF_UNDERLINE), s->ocf.flags = (u8)((s->ocf.flags & ~CF_UNDERLINE) | (chk(sd_ul) ? CF_UNDERLINE : 0)));
  /* paragraph formatting, with the Format pages */
  if (s->type == ST_PARA) {
    int ln = (int)cyc(sd_line), sp = (int)cyc(sd_special), nk = (int)cyc(sd_nkind);
    n = s->pf;
    n.align = (u8)cyc(sd_align);
    if (ln < 4) { n.line = linevals[ln]; n.linerule = LS_AUTO; }
    else { n.linerule = (u8)(ln == 5 ? LS_EXACT : LS_ATLEAST); if (s->pf.linerule == LS_AUTO) n.line = 240; }
    n.before = parse_tw(get_str(sd_before), 1); n.after = parse_tw(get_str(sd_after), 1);
    n.left = parse_tw(get_str(sd_left), 0); n.right = parse_tw(get_str(sd_right), 0);
    n.first = sp == 0 ? 0 : sp == 1 ? parse_tw(get_str(sd_by), 0) : -parse_tw(get_str(sd_by), 0);
    n.keepnext = (u8)chk(sd_keep); n.keeplines = (u8)chk(sd_lines); n.pagebreak = (u8)chk(sd_brk); n.nowidow = (u8)!chk(sd_widow); n.contextual = (u8)chk(sd_ctx);
    memcpy(n.tabs, sd_tabpf.tabs, sizeof n.tabs); memcpy(n.tabkind, sd_tabpf.tabkind, sizeof n.tabkind); memcpy(n.tablead, sd_tabpf.tablead, sizeof n.tablead);
    {
      int bi = (int)cyc(sd_bcolor), si = (int)cyc(sd_shade);
      for (i = 0; i < 5; i++) {
        int e = pe[i];
        if (chk(sd_bside[i])) {
          n.bstyle[e] = (u8)(cyc(sd_bstyle) + 1); n.bwidth[e] = widths[cyc(sd_bwidth) % 6];
          n.bcolor[e] = col_rgb[bi] == COLOR_AUTO ? COLOR_AUTO : col_rgb[bi]; n.thbcolor[e] = col_ref[bi];
          n.bspace[e] = (u8)(parse_tw(get_str(sd_bspace), 1) / 20);
        } else { n.bstyle[e] = PB_NONE; n.bwidth[e] = 0; n.bspace[e] = 0; n.bcolor[e] = 0; n.thbcolor[e] = 0; }
      }
      n.shade = si && col_rgb[si] != COLOR_AUTO ? HL_ON | col_rgb[si] : 0; n.thshade = si ? col_ref[si] : 0;
    }
    OWN_P(PM_ALIGN); OWN_P(PM_LINE); OWN_P(PM_SPACING); OWN_P(PM_INDENT); OWN_P(PM_KEEP); OWN_P(PM_PAGINATION); OWN_P(PM_BREAK); OWN_P(PM_CONTEXT);
    OWN_P(PM_TABS); OWN_P(PM_BORDER); OWN_P(PM_SHADE);
    /* numbering: a list made for the style (the kind chosen), or none */
    {
      static const int presets[7] = { -1, LP_BULLETS, LP_NUMBERS, LP_PARENS, LP_OUTLINE, LP_LEGAL, LP_HEADINGS };
      int had = (sd_s.pmask & PM_LIST) && sd_s.pf.list;
      if (nk == sd_nk0) { if (had) s->opf.level = (u8)cyc(sd_nlevel); }      /* (the style's list kept) */
      else if (nk == 0) { if (had || bp->list) { s->pmask |= PM_LIST; s->opf.list = 0; s->opf.level = 0; } }
      else {
        s->pmask |= PM_LIST;
        s->opf.list = (u16)doc_list_preset(d, presets[nk]);
        s->opf.level = (u8)cyc(sd_nlevel);
        if (!s->opf.list) s->pmask &= ~(unsigned)PM_LIST;
      }
    }
    /* the frame */
    {
      StyleFrame *f = &s->x.frame;
      memset(f, 0, sizeof *f);
      if (chk(sd_frame)) {
        int hr = (int)cyc(sd_fhrule);
        f->set = 1;
        f->w = !strcmp(get_str(sd_fw), tr("Auto")) ? 0 : parse_tw(get_str(sd_fw), 0);
        f->h = hr == 0 ? 0 : hr == 1 ? parse_tw(get_str(sd_fh), 0) : -parse_tw(get_str(sd_fh), 0);
        f->xalign = (u8)cyc(sd_fhpos); f->x = parse_tw(get_str(sd_fx), 0);
        f->hanchor = (u8)cyc(sd_fhrel);
        f->yalign = (u8)cyc(sd_fvpos); f->y = parse_tw(get_str(sd_fy), 0);
        f->vanchor = (u8)(cyc(sd_fvrel) == 0 ? 0 : cyc(sd_fvrel));
        f->wrap = (u8)(cyc(sd_fwrap) == 1 ? 2 : 1);
        f->hspace = parse_tw(get_str(sd_fhd), 0); f->vspace = parse_tw(get_str(sd_fvd), 0);
      }
    }
  }
  i = (int)cyc(sd_lang);
  {
    /* the style's language (WP31's CharFmt.lang, w:lang) */
    u16 lg = i > 0 && langs[i - 1] ? plang_find(langs[i - 1]) : 0;
    if (lg != bc->lang) { s->cmask |= CM_LANG; s->ocf.lang = lg; } else s->cmask &= ~(unsigned)CM_LANG;
  }
  s->x.key = key_of((int)cyc(sd_key));
  if (s->x.key) for (i = 0; i < d->nstyles; i++) if (i != sd_k && style_ok(d, i) && d->styles[i].x.key == s->x.key) d->styles[i].x.key = 0;   /* (a key belongs to one style) */
  if (!DoMethod(wp_view(), MUIM_Quill_Command, CMD_SETSTYLE, (ULONG)&q) || q.k < 0) { beep(); return; }
  if (linked) {
    /* a linked style: its character half too ("<name> Char"), the two naming each other */
    struct QStyle c;
    int pk = q.k;
    memset(&c, 0, sizeof c);
    c.k = -1; c.s.type = ST_CHAR; c.s.based = NO_STYLE;
    snprintf(c.s.name, sizeof c.s.name, "%.40s Char", s->name);
    c.s.cmask = s->cmask; c.s.ocf = d->styles[pk].cf;
    c.s.x.link = (u8)(pk + 1);
    if (DoMethod(wp_view(), MUIM_Quill_Command, CMD_SETSTYLE, (ULONG)&c) && c.k >= 0) {
      q.k = pk; q.s = d->styles[pk]; q.s.x.link = (u8)(c.k + 1);
      DoMethod(wp_view(), MUIM_Quill_Command, CMD_SETSTYLE, (ULONG)&q);
    }
    q.k = pk;
  }
  if (sd_k < 0 && s->type == ST_PARA) DoMethod(wp_view(), MUIM_Quill_Command, CMD_STYLE, (ULONG)q.k);   /* a new paragraph style: the caret's paragraph gets it */
  if (chk(sd_tpl)) style_to_template(d, d->styles[q.k].name);
  set(wp_view(), MUIA_Quill_Changed, TRUE);
  if (pane_here()) sp_update(pane, 1);
  set(wp_win(), MUIA_Window_Activate, TRUE);
}
#undef OWN_C
#undef OWN_P

/* ------------------------------------------------------------------ */
/* Manage Styles: Edit, Recommend, Restrict, Set Defaults */
static Object *msw, *ms_list, *ms_pg, *ms_page, *ms_sort, *ms_recs, *ms_desc, *ms_prio, *ms_dfont, *ms_dsize, *ms_dbefore, *ms_dafter, *ms_dline;
static int ms_idx[MAX_STYLES], ms_n;
static void ms_fill(void)
{
  Doc *d = wp_doc();
  int rows[MAXROWS], n, i, page = (int)cyc(ms_page), keep = (int)active(ms_list);
  int show = sp_show, sort = sp_sort;
  if (!msw || !d) return;
  sp_show = chk(ms_recs) ? 0 : 3; sp_sort = (int)cyc(ms_sort);
  if (page == 1) sp_sort = 1;
  n = sp_rows(d, rows);
  sp_show = show; sp_sort = sort;
  set(ms_list, MUIA_List_Quiet, TRUE);
  DoMethod(ms_list, MUIM_List_Clear);
  ms_n = 0;
  for (i = 1; i < n; i++) {
    int k = rows[i];
    const Style *s = &d->styles[k];
    char b[120], nm[60];
    shown(d, k, nm, sizeof nm);
    if (page == 1) snprintf(b, sizeof b, "%3d  %s%s", prio(s), nm, s->x.flags & SXF_HIDDEN ? tr(" (Hide)") : s->x.flags & SXF_SEMIHIDDEN ? tr(" (Hide until used)") : "");
    else if (page == 2) snprintf(b, sizeof b, "%s%s", s->x.flags & SXF_LOCKED ? tr("[Restricted] ") : "", nm);
    else snprintf(b, sizeof b, "%s%s", nm, s->type == ST_CHAR ? "  a" : s->x.link ? "  \266a" : "  \266");
    DoMethod(ms_list, MUIM_List_InsertSingle, (ULONG)b, MUIV_List_Insert_Bottom);
    ms_idx[ms_n++] = k;
  }
  set(ms_list, MUIA_List_Quiet, FALSE);
  if (keep >= 0 && keep < ms_n) set(ms_list, MUIA_List_Active, keep);
}
static int ms_sel(void) { LONG a = active(ms_list); return a >= 0 && a < ms_n ? ms_idx[a] : -1; }
static void ms_pick(void)
{
  Doc *d = wp_doc();
  int k = ms_sel();
  char desc[400];
  if (!d) return;
  desc[0] = 0;
  if (k >= 0) style_text(d, k, desc, sizeof desc);
  set(ms_desc, MUIA_Text_Contents, (ULONG)desc);
  if (k >= 0) { char b[12]; sprintf(b, "%d", prio(&d->styles[k])); set(ms_prio, MUIA_String_Contents, (ULONG)b); }
}
static void ms_defaults_fill(void)
{
  Doc *d = wp_doc();
  char b[40];
  if (!d) return;
  set_lat(ms_dfont, d->defcf.font < d->nfonts ? d->fonts[d->defcf.font] : "");
  sprintf(b, d->defcf.size & 1 ? "%u.5" : "%u", (unsigned)(d->defcf.size / 2)); set(ms_dsize, MUIA_String_Contents, (ULONG)b);
  tw_text(d->defpf.before, b, 1); set(ms_dbefore, MUIA_String_Contents, (ULONG)b);
  tw_text(d->defpf.after, b, 1); set(ms_dafter, MUIA_String_Contents, (ULONG)b);
  set(ms_dline, MUIA_Cycle_Active, d->defpf.line <= 240 ? 0 : d->defpf.line <= 264 ? 1 : d->defpf.line <= 276 ? 2 : d->defpf.line <= 360 ? 3 : 4);
}
static void open_manage(void)
{
  static const char *pages[] = { "Edit", "Recommend", "Restrict", "Set Defaults", NULL }, *sorts[] = { "Alphabetical", "As Recommended", "Font", "Based on", "By type", NULL },
               *lines[] = { "Single", "1.1", "1.15", "1.5 lines", "Double", NULL };
  static const char *t_pg[5], *t_so[6], *t_li[6];
  if (!msw) {
    Object *mod, *del, *nw, *org, *up, *down, *last, *assign, *show, *hideu, *hide, *lock, *unlock, *defs, *cl, *pedit, *prec, *pres, *pdef;
    pedit = HGroup, Child, mod = QButton(tr("_Modify...")), Child, del = QButton(tr("_Delete")), Child, nw = QButton(tr("_New Style...")), Child, org = QButton(tr("_Import/Export...")), End;
    prec = VGroup, Child, HGroup, Child, up = QButton(tr("Move _Up")), Child, down = QButton(tr("Move _Down")), Child, last = QButton(tr("Move to _Last")),
        Child, ms_prio = StringObject, StringFrame, MUIA_String_MaxLen, 4, MUIA_String_Accept, (ULONG)"0123456789", MUIA_FixWidthTxt, (ULONG)"0000", End, Child, assign = QButton(tr("_Assign Value")), End,
      Child, HGroup, Child, show = QButton(tr("_Show")), Child, hideu = QButton(tr("Hide _until used")), Child, hide = QButton(tr("_Hide")), End, End;
    pres = VGroup, Child, HGroup, Child, lock = QButton(tr("_Restrict")), Child, unlock = QButton(tr("_Permit")), Child, HSpace(0), End,
      Child, TextObject, MUIA_Text_Contents, (ULONG)tr("Restricted styles can't be applied while editing is restricted."), End, End;
    pdef = VGroup, Child, ColGroup(4),
        Child, Label2(tr("Font")), Child, ms_dfont = mkstring(46), Child, Label2(tr("Size")), Child, ms_dsize = num_field(),
        Child, Label2(tr("Before")), Child, ms_dbefore = num_field(), Child, Label2(tr("After")), Child, ms_dafter = num_field(),
        Child, Label1(tr("Line spacing")), Child, ms_dline = mkcycle(trl(lines, t_li)), Child, HSpace(0), Child, defs = QButton(tr("_Set Defaults")),
      End, End;
    msw = mkwin("Manage Styles", MAKE_ID('M','S','T','Y'), VGroup, QDLG,
      Child, HGroup, Child, Label1(tr("Page")), Child, ms_page = mkcycle(trl(pages, t_pg)), Child, Label1(tr("Sort order")), Child, ms_sort = mkcycle(trl(sorts, t_so)), End,
      Child, HGroup, Child, ms_recs = mkcheck(0), Child, LLabel1(tr("Show recommended styles only")), Child, HSpace(0), End,
      Child, ms_list = mklist(),
      Child, ms_desc = TextObject, TextFrame, MUIA_Background, MUII_TextBack, MUIA_Text_SetMin, FALSE, MUIA_Text_Contents, (ULONG)"", End,
      Child, ms_pg = VGroup, MUIA_Group_PageMode, TRUE, Child, pedit, Child, prec, Child, pres, Child, pdef, End,
      Child, HGroup, Child, HSpace(0), Child, cl = QButton(tr("_Close")), End,
    End);
    if (!msw) return;
    bt(mod, ID_MS_MODIFY); bt(del, ID_MS_DELETE); bt(nw, ID_MS_NEW); bt(org, ID_MS_ORGANIZER); bt(up, ID_MS_UP); bt(down, ID_MS_DOWN); bt(last, ID_MS_LAST);
    bt(assign, ID_MS_ASSIGN); bt(show, ID_MS_SHOW); bt(hideu, ID_MS_HIDEUSED); bt(hide, ID_MS_HIDE); bt(lock, ID_MS_LOCK); bt(unlock, ID_MS_UNLOCK); bt(defs, ID_MS_DEFAULTS);
    DoMethod(cl, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)msw, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(ms_page, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)ms_pg, 3, MUIM_Set, MUIA_Group_ActivePage, MUIV_TriggerValue);
    DoMethod(ms_page, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_MS_PAGE);
    DoMethod(ms_sort, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_MS_PAGE);
    DoMethod(ms_recs, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_MS_PAGE);
    DoMethod(ms_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_MS_PICK);
    DoMethod(ms_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_MS_MODIFY);
  }
  ms_fill(); ms_defaults_fill();
  set(msw, MUIA_Window_Open, TRUE);
}
/* a style's own settings changed (undoable, the view laid out afresh) */
static void style_put(int k, const Style *s)
{
  struct QStyle q;
  q.k = k; q.s = *s;
  if (!DoMethod(wp_view(), MUIM_Quill_Command, CMD_SETSTYLE, (ULONG)&q) || q.k < 0) beep();
}
/* the document's defaults (docDefaults: font, size, spacing) changed, and every style with them */
static CharFmt def_cf; static ParaFmt def_pf;
static void set_defaults(Doc *d, void *arg) { d->defcf = def_cf; d->defpf = def_pf; }
static void ms_cmd(ULONG id)
{
  Doc *d = wp_doc();
  int k = ms_sel();
  Style s;
  if (!d) return;
  switch (id) {
  case ID_MS_PAGE: ms_fill(); return;
  case ID_MS_PICK: ms_pick(); return;
  case ID_MS_MODIFY: if (k >= 0) wp13_style_dialog(k, 0); else beep(); return;
  case ID_MS_NEW: wp13_style_dialog(-1, ST_PARA); return;
  case ID_MS_DELETE:
    if (k <= 0) { beep(); return; }
    { char nm[64]; shown(d, k, nm, sizeof nm);
      if (wp_ask("_Delete|_Cancel", "Delete the style \"%s\"? Its text takes the style it was based on.", nm) == 1) DoMethod(wp_view(), MUIM_Quill_Command, CMD_DELSTYLE, (ULONG)k); }
    break;
  case ID_MS_DEFAULTS: {
    char fam[100];
    const char *t = get_str(ms_dsize);
    long sz = atol(t) * 2 + (strchr(t, '.') && strchr(t, '.')[1] >= '5' ? 1 : 0);
    static const i32 lines[5] = { 240, 264, 276, 360, 480 };
    def_cf = d->defcf; def_pf = d->defpf;
    get_utf8(ms_dfont, fam, sizeof fam);
    if (fam[0] && (d->defcf.font >= d->nfonts || !str_ieq(fam, d->fonts[d->defcf.font]))) { def_cf.font = (u8)doc_font(d, fam); def_cf.thfont = 0; }
    if (sz >= 2 && sz < 3277) def_cf.size = (u16)sz;
    def_pf.before = parse_tw(get_str(ms_dbefore), 1); def_pf.after = parse_tw(get_str(ms_dafter), 1);
    def_pf.line = lines[cyc(ms_dline) % 5]; def_pf.linerule = LS_AUTO;
    if (!doc_restyle(d, set_defaults, NULL)) beep();
    changed_now();
    break; }
  default:
    if (k < 0) { beep(); return; }
    s = d->styles[k];
    if (id == ID_MS_UP || id == ID_MS_DOWN) { int p = prio(&s) + (id == ID_MS_UP ? -1 : 1); s.x.priority = (u8)((p < 0 ? 0 : p > 99 ? 99 : p) + 1); }
    else if (id == ID_MS_LAST) s.x.priority = 100;
    else if (id == ID_MS_ASSIGN) { long p = atol(get_str(ms_prio)); s.x.priority = (u8)((p < 0 ? 0 : p > 99 ? 99 : p) + 1); }
    else if (id == ID_MS_SHOW) s.x.flags &= (u8)~(SXF_SEMIHIDDEN | SXF_HIDDEN | SXF_UNHIDE);
    else if (id == ID_MS_HIDEUSED) { s.x.flags |= SXF_SEMIHIDDEN | SXF_UNHIDE; s.x.flags &= (u8)~SXF_HIDDEN; }
    else if (id == ID_MS_HIDE) { s.x.flags |= SXF_SEMIHIDDEN; s.x.flags &= (u8)~SXF_UNHIDE; }
    else if (id == ID_MS_LOCK) s.x.flags |= SXF_LOCKED;
    else if (id == ID_MS_UNLOCK) s.x.flags &= (u8)~SXF_LOCKED;
    style_put(k, &s);
    break;
  }
  ms_fill();
  if (pane_here()) sp_update(pane, 1);
}

/* ---- the Organizer: styles copied between this document and another file */
static Object *orw, *or_left, *or_right, *or_file;
static Doc *or_doc;                      /* the other file, read */
static char or_path[300];
static int or_changed;
static void or_list(Object *l, Doc *d)
{
  int k;
  set(l, MUIA_List_Quiet, TRUE);
  DoMethod(l, MUIM_List_Clear);
  for (k = 0; d && k < d->nstyles; k++) {
    char nm[64];
    if (!style_ok(d, k)) continue;
    shown(d, k, nm, sizeof nm);
    DoMethod(l, MUIM_List_InsertSingle, (ULONG)nm, MUIV_List_Insert_Bottom);
  }
  set(l, MUIA_List_Quiet, FALSE);
}
static int or_sel(Object *l, Doc *d)
{
  LONG a = active(l);
  int k, i = 0;
  for (k = 0; d && k < d->nstyles; k++) if (style_ok(d, k)) { if (i == a) return k; i++; }
  return -1;
}
static void or_fill(void)
{
  char b[340];
  or_list(or_left, wp_doc()); or_list(or_right, or_doc);
  snprintf(b, sizeof b, "%s %s", tr("In:"), or_doc ? (const char *)FilePart((STRPTR)or_path) : tr("(no file open)"));
  set(or_file, MUIA_Text_Contents, (ULONG)b);
}
static void or_close_file(void)
{
  if (or_doc && or_changed && wp_ask("_Save|_Don't Save", "Save the styles copied to %s?", (const char *)FilePart((STRPTR)or_path)) == 1) {
    if (!doc_save_file(or_doc, or_path)) wp_ask("OK", "%s couldn't be saved.", (const char *)FilePart((STRPTR)or_path));
  }
  doc_free(or_doc); or_doc = NULL; or_changed = 0;
}
static void open_organizer(const char *path)
{
  if (!orw) {
    Object *open, *tor, *tol, *dl, *dr, *cl;
    orw = mkwin("Organizer", MAKE_ID('O','R','G','S'), VGroup, QDLG,
      Child, HGroup,
        Child, VGroup, Child, TextObject, MUIA_Text_Contents, (ULONG)tr("In this document:"), End, Child, or_left = mklist(), Child, dl = QButton(tr("_Delete")), End,
        Child, VGroup, Child, VSpace(0), Child, tor = QButton(tr("_Copy ->")), Child, tol = QButton(tr("<- C_opy")), Child, VSpace(0), End,
        Child, VGroup, Child, or_file = TextObject, MUIA_Text_Contents, (ULONG)"", End, Child, or_right = mklist(), Child, dr = QButton(tr("D_elete")), End,
      End,
      Child, HGroup, Child, open = QButton(tr("Open _File...")), Child, HSpace(0), Child, cl = QButton(tr("C_lose")), End,
    End);
    if (!orw) return;
    bt(open, ID_OR_OPEN); bt(tor, ID_OR_TORIGHT); bt(tol, ID_OR_TOLEFT); bt(dl, ID_OR_DELL); bt(dr, ID_OR_DELR); bt(cl, ID_OR_CLOSE);
    DoMethod(orw, MUIM_KillNotify, MUIA_Window_CloseRequest);
    DoMethod(orw, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_OR_CLOSE);
  }
  if (path && path[0] && (!or_doc || strcmp(path, or_path))) {
    unsigned lost;
    char err[200];
    or_close_file();
    snprintf(or_path, sizeof or_path, "%s", path);
    or_doc = doc_load_file(or_path, &lost, err, sizeof err);
    if (!or_doc) wp_ask("OK", "%s couldn't be read.", (const char *)FilePart((STRPTR)or_path));
  }
  or_fill();
  set(orw, MUIA_Window_Open, TRUE);
}
/* (WP23's Templates and Add-ins) the Organizer with another file's styles */
void wp13_organizer(const char *path) { open_organizer(path); }
static void or_cmd(ULONG id)
{
  Doc *d = wp_doc();
  int k;
  const char *names[1];
  if (!d) return;
  switch (id) {
  case ID_OR_OPEN: {
    char path[300];
    snprintf(path, sizeof path, "%s", or_path[0] ? or_path : d->attached ? d->attached : "PROGDIR:Templates/");
    if (wp_file_request(0, "Open a file with styles", path, sizeof path, "#?.(docx|dotx|dotm)")) open_organizer(path);
    return; }
  case ID_OR_CLOSE: or_close_file(); set(orw, MUIA_Window_Open, FALSE); return;
  case ID_OR_TORIGHT:
    if (!or_doc || (k = or_sel(or_left, d)) < 0) { beep(); return; }
    names[0] = d->styles[k].name;
    if (doc_copy_styles(or_doc, d, names, 1)) or_changed = 1;
    break;
  case ID_OR_TOLEFT:
    if (!or_doc || (k = or_sel(or_right, or_doc)) < 0) { beep(); return; }
    names[0] = or_doc->styles[k].name;
    if (doc_copy_styles(d, or_doc, names, 1)) changed_now();
    break;
  case ID_OR_DELL:
    if ((k = or_sel(or_left, d)) <= 0) { beep(); return; }
    DoMethod(wp_view(), MUIM_Quill_Command, CMD_DELSTYLE, (ULONG)k);
    break;
  case ID_OR_DELR:
    if (!or_doc || (k = or_sel(or_right, or_doc)) <= 0) { beep(); return; }
    if (doc_delete_style(or_doc, k)) or_changed = 1;
    break;
  }
  or_fill();
  ms_fill();
}

/* ------------------------------------------------------------------ */
/* Office themes: the Design tab's lists and the windows behind them */
#define MAXSETS 32
static const char *thl_labels[MAXSETS + 4], *cl_labels[MAXSETS + 2], *fl_labels[MAXSETS + 2], *el_labels[MAXSETS];
static char cl_buf[MAXSETS][40], fl_buf[MAXSETS][80];
static ULONG cl_colors[MAXSETS + 2];
static RibbonList rl_themes, rl_colors, rl_fonts, rl_effects;
static int nthemes, ncolors, nfonts, neffects;
static void lists_make(void)
{
  int i;
  for (i = 0; i < theme_nthemes() && i < MAXSETS; i++) thl_labels[i] = theme_office_name(i);
  nthemes = i;
  thl_labels[i++] = "Reset to Theme from Template"; thl_labels[i++] = "Browse for Themes..."; thl_labels[i++] = "Save Current Theme...";
  rl_themes.labels = thl_labels; rl_themes.n = i; rl_themes.kind = PL_TEXT;
  for (i = 0; i < theme_ncolorsets() && i < MAXSETS; i++) {
    snprintf(cl_buf[i], sizeof cl_buf[i], "%s", theme_colorset_name(i));
    cl_labels[i] = cl_buf[i]; cl_colors[i] = theme_colorset(i)[THC_ACCENT1];
  }
  ncolors = i;
  cl_labels[i] = "Customize Colors..."; cl_colors[i++] = 0xFF000000UL;
  rl_colors.labels = cl_labels; rl_colors.n = i; rl_colors.kind = PL_COLOR; rl_colors.colors = cl_colors;
  for (i = 0; i < theme_nfontsets() && i < MAXSETS; i++) {
    const char *ma, *mi;
    theme_fontset(i, &ma, &mi);
    snprintf(fl_buf[i], sizeof fl_buf[i], "%s: %s / %s", theme_fontset_name(i), ma, mi);
    fl_labels[i] = fl_buf[i];
  }
  nfonts = i;
  fl_labels[i++] = "Customize Fonts...";
  rl_fonts.labels = fl_labels; rl_fonts.n = i; rl_fonts.kind = PL_TEXT;
  for (i = 0; i < theme_neffects() && i < MAXSETS; i++) el_labels[i] = theme_effect_name(i);
  neffects = i;
  rl_effects.labels = el_labels; rl_effects.n = i; rl_effects.kind = PL_TEXT;
}
static void retheme_done(int ok)
{
  if (!ok) { beep(); return; }
  changed_now();
  if (pane_here()) sp_update(pane, 1);
}
static void browse_theme(void)
{
  Doc *d = wp_doc();
  char path[300] = "PROGDIR:Templates/";
  size_t n;
  u8 *data;
  if (!d || !wp_file_request(0, "Choose Theme or Themed Document", path, sizeof path, "#?.(thmx|docx|dotx|dotm|xml)")) return;
  if (!(data = file_read(path, &n))) { beep(); return; }
  if (!doc_theme_load(d, data, n)) wp_ask("OK", "%s has no Office theme in it.", (const char *)FilePart((STRPTR)path));
  else retheme_done(1);
  free(data);
}
static void save_theme(void)
{
  Doc *d = wp_doc();
  char path[300] = "PROGDIR:Templates/Theme.thmx";
  size_t n;
  u8 *data;
  if (!d || !wp_file_request(1, "Save Current Theme", path, sizeof path, "#?.thmx")) return;
  if (!strchr(FilePart((STRPTR)path), '.') && strlen(path) + 6 < sizeof path) strcat(path, ".thmx");
  if (!(data = theme_thmx(d, &n)) || !file_write(path, data, n)) wp_ask("OK", "%s couldn't be saved.", (const char *)FilePart((STRPTR)path));
  free(data);
}
/* the theme of the template the document is attached to, again */
static void reset_theme(void)
{
  Doc *d = wp_doc();
  size_t n;
  u8 *data;
  if (!d || !d->attached || !(data = file_read(d->attached, &n))) { wp_ask("OK", "%s", "The document isn't attached to a template Quill can read."); return; }
  retheme_done(doc_theme_load(d, data, n));
  free(data);
}

/* ---- Customize Colors: the twelve colours and a name */
static Object *tcw, *tc_hex[12], *tc_name, *tc_set;
static const char *const tc_names[12] = { "Text/Background - Dark 1", "Text/Background - Light 1", "Text/Background - Dark 2", "Text/Background - Light 2",
  "Accent 1", "Accent 2", "Accent 3", "Accent 4", "Accent 5", "Accent 6", "Hyperlink", "Followed Hyperlink" };
static void tc_fill(const u32 *c, const char *name)
{
  int i;
  for (i = 0; i < 12; i++) { char b[10]; sprintf(b, "%06lX", (unsigned long)(c[i] & 0xFFFFFF)); set(tc_hex[i], MUIA_String_Contents, (ULONG)b); }
  set_lat(tc_name, name);
}
static void open_colors(void)
{
  Doc *d = wp_doc();
  static const char *sets[MAXSETS + 2];
  int i;
  if (!d) return;
  if (!tcw) {
    Object *g = ColGroup(4), End, *ok, *cl;
    for (i = 0; i < theme_ncolorsets() && i < MAXSETS; i++) sets[i] = theme_colorset_name(i);
    sets[i] = NULL;
    for (i = 0; i < 12 && g; i++) {
      Object *l = Label2(tr(tc_names[i]));
      tc_hex[i] = StringObject, StringFrame, MUIA_String_MaxLen, 7, MUIA_String_Accept, (ULONG)"0123456789abcdefABCDEF", MUIA_FixWidthTxt, (ULONG)"DDDDDDD", MUIA_CycleChain, 1, End;
      if (l && tc_hex[i]) { DoMethod(g, OM_ADDMEMBER, (ULONG)l); DoMethod(g, OM_ADDMEMBER, (ULONG)tc_hex[i]); }
    }
    tcw = mkwin("Create New Theme Colors", MAKE_ID('T','H','C','L'), VGroup, QDLG,
      Child, HGroup, Child, Label1(tr("Start from")), Child, tc_set = mkcycle(sets), End,
      Child, QTitleObject(tr("Theme colors")), Child, g,
      Child, HGroup, Child, Label2(tr("Name")), Child, tc_name = mkstring(40), End,
      Child, HGroup, Child, ok = QButton(tr("_Save")), Child, HSpace(0), Child, cl = QButton(tr("_Cancel")), End,
    End);
    if (!tcw) return;
    bt(ok, ID_TC_SAVE);
    DoMethod(cl, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)tcw, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(tc_set, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_TC_PICK);
  }
  tc_fill(theme_of(d)->clr, "Custom 1");
  i = theme_colorset_of(d);
  nnset(tc_set, MUIA_Cycle_Active, i >= 0 ? i : 0);
  set(tcw, MUIA_Window_Open, TRUE);
}
static void colors_save(void)
{
  Doc *d = wp_doc();
  u32 c[12];
  char name[64];
  int i;
  if (!d) return;
  for (i = 0; i < 12; i++) {
    const char *s = get_str(tc_hex[i]);
    if (strlen(s) != 6) { beep(); return; }
    c[i] = (u32)strtoul(s, NULL, 16);
  }
  get_utf8(tc_name, name, sizeof name);
  set(tcw, MUIA_Window_Open, FALSE);
  retheme_done(doc_theme_set_colors(d, c, name[0] ? name : "Custom 1"));
}

/* ---- Customize Fonts: heading and body fonts and a name */
static Object *tfw, *tf_major, *tf_minor, *tf_name;
static void open_fonts(void)
{
  Doc *d = wp_doc();
  if (!d) return;
  if (!tfw) {
    Object *ok, *cl;
    tfw = mkwin("Create New Theme Fonts", MAKE_ID('T','H','F','N'), VGroup, QDLG,
      Child, ColGroup(2),
        Child, Label2(tr("Heading font")), Child, tf_major = mkstring(46),
        Child, Label2(tr("Body font")), Child, tf_minor = mkstring(46),
        Child, Label2(tr("Name")), Child, tf_name = mkstring(40),
      End,
      Child, HGroup, Child, ok = QButton(tr("_Save")), Child, HSpace(0), Child, cl = QButton(tr("_Cancel")), End,
    End);
    if (!tfw) return;
    bt(ok, ID_TF_SAVE);
    DoMethod(cl, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)tfw, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  }
  set_lat(tf_major, theme_of(d)->major); set_lat(tf_minor, theme_of(d)->minor); set_lat(tf_name, "Custom 1");
  set(tfw, MUIA_Window_Open, TRUE);
  set(tfw, MUIA_Window_ActiveObject, (ULONG)tf_major);
}
static void fonts_save(void)
{
  Doc *d = wp_doc();
  char ma[64], mi[64], name[64];
  if (!d) return;
  get_utf8(tf_major, ma, sizeof ma); get_utf8(tf_minor, mi, sizeof mi); get_utf8(tf_name, name, sizeof name);
  if (!ma[0] || !mi[0]) { beep(); return; }
  set(tfw, MUIA_Window_Open, FALSE);
  retheme_done(doc_theme_set_fonts(d, ma, mi, name[0] ? name : "Custom 1"));
}

/* ---- the Theme window (the menus' way to all of it) */
static Object *thw, *th_theme, *th_colors, *th_fonts, *th_effects, *th_now;
static void th_fill(void)
{
  Doc *d = wp_doc();
  const DocTheme *t = theme_of(d);
  char b[300], n1[70], n2[70], n3[70], n4[70];
  int i;
  if (!thw || !d) return;
  i = theme_colorset_of(d); nnset(th_colors, MUIA_Cycle_Active, i >= 0 ? i : 0);
  i = theme_fontset_of(d); nnset(th_fonts, MUIA_Cycle_Active, i >= 0 ? i : 0);
  for (i = 0; i < neffects && strcmp(el_labels[i], t->ename); i++) ;
  nnset(th_effects, MUIA_Cycle_Active, i < neffects ? i : 0);
  lat(t->name, n1, sizeof n1); lat(t->cname, n2, sizeof n2); lat(t->fname, n3, sizeof n3); lat(t->ename, n4, sizeof n4);
  snprintf(b, sizeof b, "%s %s\n%s %s\n%s %s (%s / %s)\n%s %s", tr("Theme:"), n1, tr("Colours:"), n2, tr("Fonts:"), n3, t->major, t->minor, tr("Effects:"), n4);
  set(th_now, MUIA_Text_Contents, (ULONG)b);
}
static void open_theme(void)
{
  if (!wp_doc()) return;
  if (!thw) {
    static const char *tl[MAXSETS + 1], *cl2[MAXSETS + 1], *fl[MAXSETS + 1], *el[MAXSETS + 1];
    Object *apply, *cc, *cf, *br, *sv, *rs, *cl;
    int i;
    for (i = 0; i < nthemes; i++) tl[i] = thl_labels[i];
    tl[i] = NULL;
    for (i = 0; i < ncolors; i++) cl2[i] = cl_labels[i];
    cl2[i] = NULL;
    for (i = 0; i < nfonts; i++) fl[i] = fl_labels[i];
    fl[i] = NULL;
    for (i = 0; i < neffects; i++) el[i] = el_labels[i];
    el[i] = NULL;
    thw = mkwin("Office Theme", MAKE_ID('T','H','E','M'), VGroup, QDLG,
      Child, th_now = TextObject, TextFrame, MUIA_Background, MUII_TextBack, MUIA_Text_Contents, (ULONG)"\n\n\n", End,
      Child, ColGroup(3),
        Child, Label1(tr("Office theme")), Child, th_theme = mkcycle(tl), Child, apply = QButton(tr("_Apply")),
        Child, Label1(tr("Colors")), Child, th_colors = mkcycle(cl2), Child, cc = QButton(tr("C_ustomize...")),
        Child, Label1(tr("Fonts")), Child, th_fonts = mkcycle(fl), Child, cf = QButton(tr("Cu_stomize...")),
        Child, Label1(tr("Effects")), Child, th_effects = mkcycle(el), Child, HSpace(0),
      End,
      Child, HGroup, Child, br = QButton(tr("_Browse for Themes...")), Child, sv = QButton(tr("Sa_ve Current Theme...")), End,
      Child, HGroup, Child, rs = QButton(tr("_Reset to Theme from Template")), Child, HSpace(0), Child, cl = QButton(tr("_Close")), End,
    End);
    if (!thw) return;
    bt(apply, ID_TH_APPLY); bt(cc, ID_TC_OPEN); bt(cf, ID_TF_OPEN); bt(br, ID_TH_BROWSE); bt(sv, ID_TH_SAVE); bt(rs, ID_TH_RESET);
    DoMethod(cl, MUIM_Notify, MUIA_Pressed, FALSE, (ULONG)thw, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(th_colors, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_TH_COLORS);
    DoMethod(th_fonts, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_TH_FONTS);
    DoMethod(th_effects, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (ULONG)wp_app(), 2, MUIM_Application_ReturnID, ID_TH_EFFECTS);
  }
  th_fill();
  set(thw, MUIA_Window_Open, TRUE);
}

/* ------------------------------------------------------------------ */
/* the ribbon, the menus and the commands */
static const Item design_items[] = {
  { K_DROP, S_LARGE, IC_THEMES, 0, "Office Themes", ID_TH_THEMES, 0, 0, 0, 0, "Office's themes: colours, fonts and effects that go together (theme1.xml)", &rl_themes, NULL, 0 },
  { K_DROP, S_SMALL, IC_COLORSETS, 0, "Theme Colors", ID_TH_COLORS, 0, 0, F_NEWCOL, 0, "The theme's twelve colours: text that uses them changes with them", &rl_colors, NULL, 0 },
  { K_DROP, S_SMALL, IC_THEMEFONTS, 0, "Theme Fonts", ID_TH_FONTS, 0, 0, 0, 0, "The theme's heading and body fonts: text set in them changes with them", &rl_fonts, NULL, 0 },
  { K_DROP, S_SMALL, 0, 0, "Effects", ID_TH_EFFECTS, 0, 0, 0, 0, "The theme's shadows, glow and bevels for shapes", &rl_effects, "ci R 2 2 10 10 cd F 5 5 13 13 ca R 4 4 12 12", 0 },
};
static const Item style_items[] = {
  { K_BUTTON, S_SMALL, IC_STYLESPANE, 0, "Styles Pane", ID_SP_PANE, RB_WP(WPI_13, 1), 0, 0, 0, "Every style in a pane beside the page, with previews: click one to apply it", NULL, NULL, 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Style Inspector", ID_SP_INSPECT, 0, 0, 0, 0, "The paragraph and text formatting at the caret: its styles and what was added to them", NULL, "ca B A ci O 11 11 3 L 13 13 15 15", 0 },
  { K_BUTTON, S_SMALL, 0, 0, "Reveal Formatting", ID_SP_REVEAL, 0, 0, 0, 0, "Every setting of the text at the caret: font, paragraph and section", NULL, "ci L 1 4 7 4 L 1 8 7 8 L 1 12 5 12 ca O 11 9 3 E 11 9 1", 0 },
  { K_BUTTON, S_SMALL, IC_MODSTYLE, 0, "Manage Styles", ID_SP_MANAGE, 0, 0, F_NEWCOL, 0, "Edit, recommend and restrict styles; the document's default font and spacing", NULL, NULL, 0 },
};
static const WpGroup groups13[] = {
  { TAB_DESIGN, 2, { "Office Theme", G_COLS, design_items, 4 } },
  { TAB_DESIGN, 3, { "Style Tools", G_COLS, style_items, 4 } },
};
static const WpMenu menus13[] = {
  { WM_FORMAT, { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_ITEM, (STRPTR)"Styles and Themes", 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Styles Pane", 0, 0, 0, (APTR)ID_SP_PANE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Style Inspector...", 0, 0, 0, (APTR)ID_SP_INSPECT } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Reveal Formatting...", 0, 0, 0, (APTR)ID_SP_REVEAL } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Manage Styles...", 0, 0, 0, (APTR)ID_SP_MANAGE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Style Pane Options...", 0, 0, 0, (APTR)ID_SP_OPTIONS } },
  { WM_FORMAT, { NM_SUB, NM_BARLABEL, 0, 0, 0, 0 } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Office Theme...", 0, 0, 0, (APTR)ID_TH_WINDOW } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Customize Colors...", 0, 0, 0, (APTR)ID_TC_OPEN } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Customize Fonts...", 0, 0, 0, (APTR)ID_TF_OPEN } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Browse for Themes...", 0, 0, 0, (APTR)ID_TH_BROWSE } },
  { WM_FORMAT, { NM_SUB, (STRPTR)"Save Current Theme...", 0, 0, 0, (APTR)ID_TH_SAVE } },
  { WM_VIEW, { NM_ITEM, (STRPTR)"Styles Pane", 0, 0, 0, (APTR)ID_SP_PANE } },
};

int wp13_command(ULONG id, LONG value)
{
  Doc *d = wp_doc();
  if (!d) return WPR_DONE;
  switch (id) {
  case ID_SP_PANE: toggle_pane(); break;
  case ID_SP_INSPECT: open_inspector(); break;
  case ID_SP_REVEAL: open_reveal(); break;
  case ID_SP_MANAGE: open_manage(); break;
  case ID_SP_OPTIONS: open_options(); break;
  case ID_SP_OPTGO: options_done(); break;
  case ID_SI_RESETP: case ID_SI_CLEARP: case ID_SI_RESETC: case ID_SI_CLEARC: inspect_cmd(id); break;
  case ID_SI_NEW: wp13_style_dialog(-1, ST_PARA); return WPR_KEEPFOCUS;
  case ID_SD_OK: style_ok_cmd(); break;
  case ID_SD_TABSET: case ID_SD_TABCLR: case ID_SD_TABALL: case ID_SD_TABPICK: tabs_cmd(id); return WPR_KEEPFOCUS;
  case ID_SD_PAGE: sd_keynow_fill(); return WPR_KEEPFOCUS;
  case ID_SD_TYPE: if (sd_k < 0) sd_enable(cyc(sd_type) != 1); return WPR_KEEPFOCUS;
  case ID_MS_MODIFY: case ID_MS_DELETE: case ID_MS_NEW: case ID_MS_UP: case ID_MS_DOWN: case ID_MS_LAST: case ID_MS_ASSIGN: case ID_MS_SHOW:
  case ID_MS_HIDEUSED: case ID_MS_HIDE: case ID_MS_LOCK: case ID_MS_UNLOCK: case ID_MS_DEFAULTS: case ID_MS_PAGE: case ID_MS_PICK:
    ms_cmd(id); return WPR_KEEPFOCUS;
  case ID_MS_ORGANIZER: open_organizer(d->attached); return WPR_KEEPFOCUS;
  case ID_OR_OPEN: case ID_OR_TORIGHT: case ID_OR_TOLEFT: case ID_OR_DELL: case ID_OR_DELR: case ID_OR_CLOSE: or_cmd(id); return WPR_KEEPFOCUS;
  case ID_TH_THEMES:
    if (value == WPV_NONE) { open_theme(); return WPR_KEEPFOCUS; }
    if (value < nthemes) retheme_done(doc_theme_office(d, (int)value));
    else if (value == nthemes) reset_theme();
    else if (value == nthemes + 1) browse_theme();
    else save_theme();
    break;
  case ID_TH_COLORS:
    if (value == WPV_NONE && thw && is_open(thw)) value = (LONG)cyc(th_colors);
    if (value == WPV_NONE || value >= ncolors) { open_colors(); return WPR_KEEPFOCUS; }
    retheme_done(doc_theme_set_colors(d, theme_colorset((int)value), theme_colorset_name((int)value)));
    th_fill();
    break;
  case ID_TH_FONTS:
    if (value == WPV_NONE && thw && is_open(thw)) value = (LONG)cyc(th_fonts);
    if (value == WPV_NONE || value >= nfonts) { open_fonts(); return WPR_KEEPFOCUS; }
    { const char *ma, *mi; theme_fontset((int)value, &ma, &mi); retheme_done(doc_theme_set_fonts(d, ma, mi, theme_fontset_name((int)value))); }
    th_fill();
    break;
  case ID_TH_EFFECTS:
    if (value == WPV_NONE && thw && is_open(thw)) value = (LONG)cyc(th_effects);
    if (value == WPV_NONE) { open_theme(); return WPR_KEEPFOCUS; }
    if (doc_theme_set_effects(d, (int)value)) set(wp_view(), MUIA_Quill_Changed, TRUE);
    th_fill();
    break;
  case ID_TH_WINDOW: open_theme(); return WPR_KEEPFOCUS;
  case ID_TH_APPLY: retheme_done(doc_theme_office(d, (int)cyc(th_theme))); th_fill(); break;
  case ID_TH_BROWSE: browse_theme(); th_fill(); break;
  case ID_TH_SAVE: save_theme(); break;
  case ID_TH_RESET: reset_theme(); th_fill(); break;
  case ID_TC_OPEN: open_colors(); return WPR_KEEPFOCUS;
  case ID_TC_PICK: tc_fill(theme_colorset((int)cyc(tc_set)), theme_colorset_name((int)cyc(tc_set))); return WPR_KEEPFOCUS;
  case ID_TC_SAVE: colors_save(); th_fill(); break;
  case ID_TF_OPEN: open_fonts(); return WPR_KEEPFOCUS;
  case ID_TF_SAVE: fonts_save(); th_fill(); break;
  default:
    if (id >= ID_SP_KEY0 && id <= ID_WP13_LAST) {
      /* a style's shortcut key (its index is in skey) */
      extern int wp13_keystyle;
      if (style_ok(d, wp13_keystyle)) sp_apply(wp13_keystyle, 0);
    }
    break;
  }
  return WPR_DONE;
}
int wp13_keystyle = -1;
/* the panes and windows that show the caret's formatting follow it */
static void wp13_state(ULONG *on, const WpCtx *c)
{
  if (pane_here()) { *on |= 1UL << 1; sp_update(pane, 0); }
  inspect_fill();
  reveal_fill();
}
/* a style's shortcut key (Ctrl+Alt+digit, Ctrl+Shift+digit; Word's modifiers in StyleX.key) */
static ULONG wp13_key(UWORD code, UWORD qual, const WpCtx *c)
{
  static const char digits[11] = "1234567890";
  u16 mods = 0, key;
  int k;
  if (!c->doc || (code & IECODE_UP_PREFIX) || code < 0x01 || code > 0x0A) return 0;
  if (qual & IEQUALIFIER_CONTROL) mods |= 0x200;
  if (qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) mods |= 0x400;
  if (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) mods |= 0x100;
  if (!(mods & 0x200)) return 0;
  key = (u16)(mods | (u8)digits[code - 1]);
  for (k = 0; k < c->doc->nstyles; k++)
    if (style_ok(c->doc, k) && c->doc->styles[k].x.key == key) { wp13_keystyle = k; return ID_SP_KEY0; }
  return 0;
}
static void wp13_init(void) { lists_make(); }
static void wp13_cleanup(void)
{
  if (or_doc) { doc_free(or_doc); or_doc = NULL; }
  /* (the pane went with its window) */
  pane = panewin = NULL;
  if (spcls) { MUI_DeleteCustomClass(spcls); spcls = NULL; }
}

/* the package: its tag, id block, hooks (command, state, field, ctxmenu, key, init, cleanup), ribbon
   groups and menu items */
const WpPackage wp13_package = {
  "WP13", ID_WP13_FIRST, ID_WP13_LAST,
  wp13_command, wp13_state, NULL, NULL, wp13_key, wp13_init, wp13_cleanup,
  groups13, (int)(sizeof groups13 / sizeof groups13[0]),
  menus13, (int)(sizeof menus13 / sizeof menus13[0])
};
