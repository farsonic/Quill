/*
 * backstage.c - the File page, as WordCraft has it: a blue column of commands down the left, and
 * beside it the page they open: Home (the templates and the recent documents), New, Open, Info,
 * Export, Options and About. It covers the window while shown; the arrow at the top (or Esc) goes
 * back to the document. A MUI custom class drawn by Quill.
 *
 * What it does is up to main.c: a click is a MUIM_Application_ReturnID of an ID_ (ids.h), with
 * MUIA_Back_Value saying which template or recent document.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/keymap.h>
#include <clib/alib_protos.h>
#include "quill.h"
#include "theme.h"
#include "ids.h"
#include "wp_info.h"   /* (WP24) */
#include "lang.h"
#include "../core/template.h"

enum { BP_HOME, BP_NEW, BP_OPEN, BP_INFO, BP_EXPORT, BP_OPTIONS, BP_ABOUT };
/* the column: a page, or a command (id) */
typedef struct { const char *label; int icon; int page; ULONG id; } Side;
static const Side side[] = {
  { "Home", IC_HOME, BP_HOME, 0 }, { "New", IC_NEW, BP_NEW, 0 }, { "Open", IC_OPEN, BP_OPEN, 0 }, { "Info", IC_INFO, BP_INFO, 0 },
  { "Save", IC_SAVE, -1, ID_SAVE }, { "Save As", IC_SAVE, -1, ID_SAVEAS }, { "Print", IC_PRINT, -1, ID_PRINT },
  { "Export", IC_EXPORT, BP_EXPORT, 0 }, { "Options", IC_OPTIONS, BP_OPTIONS, 0 }, { "About", IC_ABOUT, BP_ABOUT, 0 },
};
#define NSIDE ((int)(sizeof side / sizeof side[0]))
#define NBOTTOM 2                /* the last two sit at the bottom */
#define SBW sbw                  /* the column's width: 104, or wider for long words */
#define MAXR 10
#define MAXHOT 40
typedef struct { WORD x0, y0, x1, y1; } BBox;
enum { H_NONE, H_BACK, H_SIDE, H_TEMPLATE, H_RECENT, H_BROWSE, H_PDF, H_TEXT, H_CM, H_IN, H_NAME, H_THEME, H_LANG, H_SIZE, H_WINDOW,
       H_AUTOSAVE, H_INFO0 };   /* (WP24: AutoSave in Options; H_INFO0 + n: the Info page's buttons) */
/* (WP24) the Info page's commands: Properties, Check for Issues, Version History, Manage Document */
static const struct { int icon; const char *title, *sub; ULONG id; } infobtn[] = {
  { IC_INFO, "Properties", "Title, author, keywords and your own", ID_INFO_PROPS },
  { IC_FIND, "Inspect Document", "Comments, changes, personal information", ID_INFO_INSPECT },
  { IC_ALTTEXT, "Check Accessibility", "What makes it hard to read", ID_INFO_ACC },
  { IC_SPELLING, "Check Compatibility", "What earlier versions can't show", ID_INFO_COMPAT },
  { IC_UNDO, "Version History", "Earlier saves of this document", ID_INFO_VERSIONS },
  { IC_OPEN, "Recover Unsaved", "Documents Quill kept when it ended", ID_INFO_RECOVER } };
#define NINFOBTN ((int)(sizeof infobtn / sizeof infobtn[0]))
enum { H_WP29_IMAGE = 200, H_WP29_OPTIONS };      /* (WP29: Export Image, Export Options: its first two ids) */
#define H_WP26_MORE 210                               /* (WP26: the Options window, ID_WP26_FIRST + 3) */
typedef struct { BBox b; int kind, n; } Hot;
/* the File page's instance data. hot: the clickable areas found while drawing, which the events
   look the pointer up in; editing: the name field has the keyboard; metric, dark, lang, large: the
   options as shown (main.c reads them back with OM_GET); value: the template or recent document
   clicked last */
struct BData {
  int shown;
  int page;
  char recent[MAXR][256]; int nrecent;
  char info[1024];               /* "label\tvalue" lines */
  char name[64]; int namelen, editing;
  int metric, dark, lang, large;
  LONG value;
  Hot hot[MAXHOT]; int nhot;
  int over;                      /* hot area under the pointer, or -1 */
  struct MUI_EventHandlerNode ehn;
};
static struct MUI_CustomClass *bcls;

/* the interface font's height, which the page's spacing goes by */
static int fh(void) { return theme_font()->tf_YSize; }
/* a clickable area noted while drawing (window pixels, corners inclusive). They are collected
   afresh on every draw, so they always match what is on screen; past MAXHOT they are dropped */
static void add_hot(struct BData *d, LONG x0, LONG y0, LONG x1, LONG y1, int kind, int n)
{
  if (d->nhot >= MAXHOT) return;
  d->hot[d->nhot].b.x0 = (WORD)x0; d->hot[d->nhot].b.y0 = (WORD)y0; d->hot[d->nhot].b.x1 = (WORD)x1; d->hot[d->nhot].b.y1 = (WORD)y1;
  d->hot[d->nhot].kind = kind; d->hot[d->nhot].n = n;
  d->nhot++;
}
/* the pointer is over hot area kind, n (it is drawn highlighted) */
static int is_over(struct BData *d, int kind, int n) { return d->over >= 0 && d->hot[d->over].kind == kind && d->hot[d->over].n == n; }
/* as much of s as fits in maxw pixels, its top at y, plain; returns the width drawn. Unlike
   theme_text it doesn't translate: callers pass tr()'s words, or names that mustn't be */
static LONG text_fit(struct RastPort *rp, struct TextFont *tf, LONG x, LONG y, const char *s, LONG maxw, LONG pen)
{
  struct TextExtent te;
  ULONG n;
  SetFont(rp, tf);
  SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
  n = TextFit(rp, (STRPTR)s, (ULONG)strlen(s), &te, NULL, 1, (ULONG)(maxw > 0 ? maxw : 0), 100);
  SetAPen(rp, pen); SetDrMd(rp, JAM1);
  Move(rp, x, y + tf->tf_Baseline);
  Text(rp, (STRPTR)s, n);
  return te.te_Width;
}

/* a template's picture: a page with its kind of content sketched on it */
static void sketch(struct RastPort *rp, int t, LONG x, LONG y, LONG w, LONG h)
{
  LONG i, l = x + w / 8, r = x + w - w / 8 - 1, yy = y + h / 9;
  theme_fill(rp, x + 1, y + 1, x + w, y + h, tpen(T_BORDER2));
  theme_fill(rp, x, y, x + w - 1, y + h - 1, tpen(T_WHITE));
  theme_rect(rp, x, y, x + w - 1, y + h - 1, tpen(T_BORDER));
#define BAR(x0, y0, x1, pen) theme_fill(rp, x0, y0, x1, (y0) + 1, tpen(pen))
  switch (t) {
  case TPL_BLANK: break;
  case TPL_WELCOME:
    theme_fill(rp, l, yy, l + (r - l) * 3 / 4, yy + 4, tpen(T_TEXT)); yy += 8;
    BAR(l, yy, l + (r - l) / 2, T_DIM); yy += 7;
    BAR(l, yy, l + (r - l) / 3, T_ACCENT); yy += 5;
    for (i = 0; i < 4; i++, yy += 4) BAR(l, yy, i == 3 ? l + (r - l) / 2 : r, T_BORDER2);
    yy += 2;
    for (i = 0; i < 3; i++, yy += 4) { theme_fill(rp, l, yy, l + 1, yy + 1, tpen(T_TEXT)); BAR(l + 4, yy, r - 6, T_BORDER2); }
    yy += 2;
    theme_fill(rp, l, yy, r, yy + 3, tpen(T_SELECTION));
    theme_rect(rp, l, yy, r, yy + 12, tpen(T_BORDER2));
    SetAPen(rp, tpen(T_BORDER2)); Move(rp, (l + r) / 2, yy); Draw(rp, (l + r) / 2, yy + 12); Move(rp, l, yy + 8); Draw(rp, r, yy + 8);
    break;
  case TPL_LETTER:
    for (i = 0; i < 3; i++, yy += 4) BAR(r - (r - l) / 3, yy, r, i ? T_BORDER2 : T_TEXT);
    yy += 4; BAR(l, yy, l + (r - l) / 4, T_BORDER2); yy += 7;
    for (i = 0; i < 3; i++, yy += 4) BAR(l, yy, l + (r - l) / 3, T_BORDER2);
    yy += 4; BAR(l, yy, l + (r - l) / 3, T_DIM); yy += 6;
    for (i = 0; i < 7; i++, yy += 4) BAR(l, yy, i == 3 || i == 6 ? l + (r - l) * 2 / 3 : r, T_BORDER2);
    yy += 4; BAR(l, yy, l + (r - l) / 4, T_DIM);
    break;
  case TPL_RESUME:
    theme_fill(rp, l, yy, l + (r - l) / 2, yy + 4, tpen(T_TEXT)); yy += 7;
    BAR(l, yy, r, T_DIM); yy += 6;
    for (i = 0; i < 3; i++) {
      BAR(l, yy, l + (r - l) / 3, T_ACCENT); yy += 3;
      SetAPen(rp, tpen(T_BORDER)); Move(rp, l, yy); Draw(rp, r, yy); yy += 3;
      BAR(l, yy, r, T_BORDER2); yy += 4;
      theme_fill(rp, l + 1, yy, l + 2, yy + 1, tpen(T_TEXT)); BAR(l + 5, yy, r - 4, T_BORDER2); yy += 4;
      theme_fill(rp, l + 1, yy, l + 2, yy + 1, tpen(T_TEXT)); BAR(l + 5, yy, r - 10, T_BORDER2); yy += 6;
    }
    break;
  case TPL_REPORT:
    yy += 2; theme_fill(rp, l + (r - l) / 6, yy, r - (r - l) / 6, yy + 4, tpen(T_TEXT)); yy += 8;
    BAR(l + (r - l) / 4, yy, r - (r - l) / 4, T_DIM); yy += 8;
    for (i = 0; i < 2; i++) {
      BAR(l, yy, l + (r - l) / 3, T_ACCENT); yy += 5;
      BAR(l, yy, r, T_BORDER2); yy += 4; BAR(l, yy, r, T_BORDER2); yy += 4; BAR(l, yy, l + (r - l) / 2, T_BORDER2); yy += 6;
    }
    theme_fill(rp, l, yy, r, yy + 3, tpen(T_SELECTION));
    theme_rect(rp, l, yy, r, yy + 12, tpen(T_BORDER2));
    SetAPen(rp, tpen(T_BORDER2)); Move(rp, l + (r - l) / 3, yy); Draw(rp, l + (r - l) / 3, yy + 12); Move(rp, l + (r - l) * 2 / 3, yy); Draw(rp, l + (r - l) * 2 / 3, yy + 12);
    break;
  }
#undef BAR
  {
    /* the page number line of the report */
    if (t == TPL_REPORT) theme_fill(rp, x + w / 2 - 3, y + h - 6, x + w / 2 + 3, y + h - 5, tpen(T_BORDER2));
  }
}
/* the template cards in a row (wrapping), from x, y; returns the y below them */
static LONG cards(struct BData *d, struct RastPort *rp, LONG x, LONG y, LONG maxx, LONG cw)
{
  LONG ch = cw * 1414 / 1000, cx = x;
  int t;
  for (t = 0; t < TPL_N; t++) {
    char nm[40];
    LONG nw;
    if (cx + cw > maxx && cx > x) { cx = x; y += ch + fh() + 18; }
    if (is_over(d, H_TEMPLATE, t)) theme_rect(rp, cx - 3, y - 3, cx + cw + 2, y + ch + 2, tpen(T_ACCENT));
    sketch(rp, t, cx, y, cw, ch);
    static const char *const shortname[TPL_N] = { "Blank", "Welcome", "Letter", "R\351sum\351", "Report" };
    strcpy(nm, tr(shortname[t]));
    nw = theme_textlen(rp, theme_font(), nm);
    text_fit(rp, theme_font(), cx + (nw < cw ? (cw - nw) / 2 : 0), y + ch + 5, nm, cw + 8, tpen(T_TEXT));
    add_hot(d, cx - 3, y - 3, cx + cw + 2, y + ch + fh() + 6, H_TEMPLATE, t);
    cx += cw + 14;
  }
  return y + ch + fh() + 14;
}
/* the recent documents from y down to maxy, each its name in bold over its drawer (or a line
   saying where they will appear); returns the y below them */
static LONG recent_list(struct BData *d, struct RastPort *rp, LONG x, LONG y, LONG maxx, LONG maxy)
{
  int i;
  if (!d->nrecent) { text_fit(rp, theme_font(), x, y, tr("Documents you open or save appear here."), maxx - x, tpen(T_DIM)); return y + fh() + 6; }
  for (i = 0; i < d->nrecent && y + 2 * fh() + 6 < maxy; i++) {
    const char *full = d->recent[i], *name = (const char *)FilePart((STRPTR)full);
    char dir[256];
    LONG rh = 2 * fh() + 8;
    size_t n = (size_t)((const char *)name - full);
    if (n >= sizeof dir) n = sizeof dir - 1;
    memcpy(dir, full, n); dir[n] = 0;
    if (is_over(d, H_RECENT, i)) theme_fill(rp, x - 4, y - 2, maxx, y + rh - 4, tpen(T_HOVER));
    theme_icon(rp, IC_NEW, x, y + (rh - 22) / 2, 16, 0);
    text_fit(rp, theme_bold(), x + 24, y + 1, name, maxx - x - 28, tpen(T_TEXT));
    text_fit(rp, theme_font(), x + 24, y + fh() + 2, dir[0] ? dir : full, maxx - x - 28, tpen(T_DIM));
    add_hot(d, x - 4, y - 2, maxx, y + rh - 4, H_RECENT, i);
    y += rh;
  }
  return y;
}
/* a big button with an icon, a title and a line under it */
static LONG big_button(struct BData *d, struct RastPort *rp, LONG x, LONG y, LONG w, int icon, const char *title, const char *sub, int kind)
{
  LONG h = 2 * fh() + 14;
  theme_fill(rp, x, y, x + w - 1, y + h - 1, tpen(is_over(d, kind, 0) ? T_HOVER : T_WHITE));
  theme_rect(rp, x, y, x + w - 1, y + h - 1, tpen(is_over(d, kind, 0) ? T_ACCENT : T_BORDER2));
  theme_icon(rp, icon, x + 8, y + (h - 24) / 2, 24, 0);
  text_fit(rp, theme_bold(), x + 40, y + 5, tr(title), w - 46, tpen(T_TEXT));
  text_fit(rp, theme_font(), x + 40, y + fh() + 7, tr(sub), w - 46, tpen(T_DIM));
  add_hot(d, x, y, x + w - 1, y + h - 1, kind, 0);
  return y + h + 8;
}
/* a heading and a row of buttons, one of them chosen (hot areas: kind, the button's number); the y below */
static LONG choices(struct BData *d, struct RastPort *rp, LONG x, LONG y, const char *title, const char *const *labs, int n, int cur, int kind)
{
  LONG bx = x, bw;
  int i;
  theme_text(rp, theme_bold(), x, y, title, tpen(T_TEXT));
  y += fh() + 6;
  for (i = 0; i < n; i++) {
    int on = i == cur;
    bw = theme_textlen(rp, theme_font(), labs[i]) + 20;
    theme_fill(rp, bx, y, bx + bw - 1, y + fh() + 7, tpen(on ? T_CHECKED : is_over(d, kind, i) ? T_HOVER : T_WHITE));
    theme_rect(rp, bx, y, bx + bw - 1, y + fh() + 7, tpen(on ? T_ACCENT : T_BORDER2));
    theme_text(rp, theme_font(), bx + 10, y + 4, labs[i], tpen(on ? T_ACCENT : T_TEXT));
    add_hot(d, bx, y, bx + bw - 1, y + fh() + 7, kind, i);
    bx += bw + 6;
  }
  return y + fh() + 20;
}
/* the whole File page drawn afresh (MUIM_Draw): the blue column, then the page chosen. Drawing
   also collects the hot areas, the column's width growing to fit the longest command */
static void back_draw(struct BData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG l = _mleft(obj), t = _mtop(obj), r = _mright(obj), b = _mbottom(obj), x, y;
  int i;
  static const char *const titles[] = { "Home", "New", "Open", "Info", "Export", "Options", "About" };
  LONG sbw = 104;
  d->nhot = 0;
  for (i = 0; i < NSIDE; i++) { LONG w = theme_textlen(rp, theme_font(), side[i].label) + 42; if (w > sbw) sbw = w; }
  /* the column */
  theme_fill(rp, l, t, l + SBW - 1, b, tpen(T_ACCENT));
  if (is_over(d, H_BACK, 0)) theme_fill(rp, l + 6, t + 6, l + 30, t + 28, tpen(T_ACCENTHOVER));
  theme_icon_pen(rp, IC_BACK, l + 10, t + 9, 16, tpen(T_ONACCENT));
  add_hot(d, l + 6, t + 6, l + 30, t + 28, H_BACK, 0);
  y = t + 40;
  for (i = 0; i < NSIDE; i++) {
    /* the last few at the bottom, unless the window is too short: then straight after the others */
    LONG ih = fh() + 12, bottom0 = b - NBOTTOM * ih - 6, top_end = t + 40 + (NSIDE - NBOTTOM) * ih + 8;
    LONG iy = i >= NSIDE - NBOTTOM ? (bottom0 > top_end ? bottom0 : top_end) + (i - (NSIDE - NBOTTOM)) * ih : y;
    int sel = side[i].page == d->page;
    if (i == NSIDE - NBOTTOM) { SetAPen(rp, tpen(T_ACCENTHOVER)); Move(rp, l + 10, iy - 6); Draw(rp, l + SBW - 11, iy - 6); }
    if (sel) theme_fill(rp, l, iy, l + SBW - 1, iy + ih - 1, tpen(T_ACCENTDARK));
    else if (is_over(d, H_SIDE, i)) theme_fill(rp, l, iy, l + SBW - 1, iy + ih - 1, tpen(T_ACCENTHOVER));
    if (sel) theme_fill(rp, l, iy, l + 2, iy + ih - 1, tpen(T_ONACCENT));
    theme_icon_pen(rp, side[i].icon, l + 12, iy + (ih - 14) / 2, 14, tpen(T_ONACCENT));
    theme_text(rp, theme_font(), l + 32, iy + (ih - fh()) / 2, side[i].label, tpen(T_ONACCENT));
    add_hot(d, l, iy, l + SBW - 1, iy + ih - 1, H_SIDE, i);
    y += ih;
  }
  /* the page */
  theme_fill(rp, l + SBW, t, r, b, tpen(T_RIBBON));
  x = l + SBW + 20;
  y = t + 12;
  theme_text(rp, theme_big(), x, y, titles[d->page], tpen(T_TEXT));
  y += theme_big()->tf_YSize + 12;
  switch (d->page) {
  case BP_HOME:
    theme_text(rp, theme_bold(), x, y, "New", tpen(T_TEXT));
    y = cards(d, rp, x, y + fh() + 8, r - 12, 62) + 6;
    theme_text(rp, theme_bold(), x, y, "Recent", tpen(T_TEXT));
    SetAPen(rp, tpen(T_BORDER)); Move(rp, x, y + fh() + 3); Draw(rp, r - 16, y + fh() + 3);
    recent_list(d, rp, x, y + fh() + 9, r - 16, b - 6);
    break;
  case BP_NEW:
    text_fit(rp, theme_font(), x, y, tr("Start from a blank page or one of these."), r - x - 12, tpen(T_DIM));
    cards(d, rp, x, y + fh() + 10, r - 12, 80);
    break;
  case BP_OPEN:
    y = big_button(d, rp, x, y, (r - x - 16 < 300 ? r - x - 16 : 300), IC_OPEN, "Browse", "Open a document from a disk or drawer", H_BROWSE) + 6;
    theme_text(rp, theme_bold(), x, y, "Recent", tpen(T_TEXT));
    SetAPen(rp, tpen(T_BORDER)); Move(rp, x, y + fh() + 3); Draw(rp, r - 16, y + fh() + 3);
    recent_list(d, rp, x, y + fh() + 9, r - 16, b - 6);
    break;
  case BP_INFO: {
    /* the lines main.c gave: "label\tvalue", beside WP24's buttons (when the page is wide enough) */
    const char *p = d->info;
    {
      LONG bw = (r - x - 16) / 2 - 8, by = y;
      int k;
      if (bw > 250) bw = 250;
      for (k = 0; k < NINFOBTN && by + 2 * fh() + 14 < b; k++) by = big_button(d, rp, x, by, bw, infobtn[k].icon, infobtn[k].title, infobtn[k].sub, H_INFO0 + k) - 2;
      x += bw + 16;
    }
    theme_text(rp, theme_bold(), x, y, "Properties", tpen(T_TEXT));
    y += fh() + 8;
    while (*p && y + fh() < b) {
      char line[200], *tab;
      size_t n = strcspn(p, "\n");
      if (n >= sizeof line) n = sizeof line - 1;
      memcpy(line, p, n); line[n] = 0;
      p += n; if (*p) p++;
      if ((tab = strchr(line, '\t'))) {
        *tab = 0;
        text_fit(rp, theme_font(), x, y, tr(line), 76, tpen(T_DIM));
        text_fit(rp, theme_font(), x + 80, y, tab + 1, r - x - 92, tpen(T_TEXT));
      } else text_fit(rp, theme_font(), x, y, line, r - x - 12, tpen(T_TEXT));
      y += fh() + 6;
    }
    break; }
  case BP_EXPORT: {
    LONG w = r - x - 16 < 320 ? r - x - 16 : 320;
    y = big_button(d, rp, x, y, w, IC_PDF, "Export to PDF", "The pages as they look here, fonts and pictures in it", H_PDF);
    y = big_button(d, rp, x, y, w, IC_EXPORT, "Save as plain text", "The words alone, as a .txt file", H_TEXT);
    y = big_button(d, rp, x, y, w, IC_PICTURE, "Export Image", "Each page as a PNG or IFF ILBM picture", H_WP29_IMAGE);
    big_button(d, rp, x, y, w, IC_OPTIONS, "Export Options", "Pages, picture resolution, bookmarks, subject", H_WP29_OPTIONS);
    break; }
  case BP_OPTIONS: {
    static const char *const units[] = { "Centimetres", "Inches" }, *const themes[] = { "Light", "Dark" }, *const sizes[] = { "Normal", "Large" };
    LONG bw;
    y = choices(d, rp, x, y, "Measurement units", units, 2, !d->metric, H_CM);
    y = choices(d, rp, x, y, "Theme", themes, 2, d->dark, H_THEME);
    y = choices(d, rp, x, y, "Text size", sizes, 2, d->large, H_SIZE);
    y = choices(d, rp, x, y, "Language", ui_languages, UI_NLANG, d->lang, H_LANG);
    { static const char *const onoff[] = { "Off", "On" };      /* (WP24) */
      y = choices(d, rp, x, y, "AutoSave", onoff, 2, wp24_autosave_on(), H_AUTOSAVE); }
    y -= 14;
    y += fh() + 20;
    theme_text(rp, theme_bold(), x, y, "Your name", tpen(T_TEXT));
    y += fh() + 2;
    text_fit(rp, theme_font(), x, y, tr("Shown with your comments and tracked changes."), r - x - 12, tpen(T_DIM));
    y += fh() + 6;
    bw = r - x - 16 < 220 ? r - x - 16 : 220;
    theme_fill(rp, x, y, x + bw - 1, y + fh() + 7, tpen(T_WHITE));
    theme_rect(rp, x, y, x + bw - 1, y + fh() + 7, tpen(d->editing ? T_ACCENT : T_INPUTBORDER));
    {
      LONG w = text_fit(rp, theme_font(), x + 5, y + 4, d->name, bw - 12, tpen(T_TEXT));
      if (d->editing) theme_fill(rp, x + 6 + w, y + 3, x + 6 + w, y + fh() + 4, tpen(T_TEXT));
    }
    add_hot(d, x, y, x + bw - 1, y + fh() + 7, H_NAME, 0);
    y += fh() + 20;
    y = big_button(d, rp, x, y, r - x - 16 < 320 ? r - x - 16 : 320, IC_OPTIONS, "Window and screen", "Where Quill's window opens, and on which screen", H_WINDOW);
    big_button(d, rp, x, y, r - x - 16 < 320 ? r - x - 16 : 320, IC_OPTIONS, "More Options", "Every setting, the ribbon, quick access and keys", H_WP26_MORE);   /* (WP26) */
    break; }
  case BP_ABOUT: {
    static const char *const lines[] = {
      "Quill 0.14", "A word processor for the Amiga (68040 or better, MUI).", "",
      "Reads and writes Word documents (.docx), exports PDF.", "Its look follows WordCraft's; its icons are Quill's own.", "",
      "Text is measured with the Liberation, Carlito and Caladea fonts", "(SIL Open Font License), so lines break as they do in Word.", NULL };
    for (i = 0; lines[i] && y + fh() < b; i++, y += fh() + 5)
      text_fit(rp, i == 0 ? theme_bold() : theme_font(), x, y, tr(lines[i]), r - x - 12, tpen(i == 0 ? T_TEXT : T_DIM));
    break; }
  }
}
/* another page shown (a click in the column): no highlight, and the name no longer edited */
static void back_page(struct BData *d, Object *obj, int page)
{
  d->page = page; d->over = -1; d->editing = 0;
  if (key_grab == obj) key_grab = NULL;
  MUI_Redraw(obj, MADF_DRAWOBJECT);
}
/* MUIM_HandleEvent. Pointer moves highlight what is under the pointer; clicks act, most by
   MUIM_Application_ReturnID so main.c does the work from its loop; while the name is edited the
   keys type into it, key_grab telling the other objects' handlers to leave them alone. Raw key
   codes: 0x45 Esc, 0x41 Backspace, 0x44 Return, 0x43 Enter; MapRawKey turns the rest into Latin-1
   through the user's keymap. Leaving the name field sends ID_OPTIONS so main.c picks it up */
static ULONG back_events(struct IClass *cl, Object *obj, struct MUIP_HandleEvent *m)
{
  struct BData *d = INST_DATA(cl, obj);
  struct IntuiMessage *im = m->imsg;
  int k, h = -1;
  if (!im || !d->shown) return 0;
  if (im->Class == IDCMP_MOUSEMOVE || im->Class == IDCMP_MOUSEBUTTONS)
    for (k = 0; k < d->nhot; k++) {
      BBox *b = &d->hot[k].b;
      if (im->MouseX >= b->x0 && im->MouseX <= b->x1 && im->MouseY >= b->y0 && im->MouseY <= b->y1) { h = k; break; }
    }
  if (im->Class == IDCMP_MOUSEMOVE) {
    if (h != d->over) { d->over = h; MUI_Redraw(obj, MADF_DRAWOBJECT); }
    return 0;
  }
  if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN) {
    Hot *ht = h >= 0 ? &d->hot[h] : NULL;
    if (d->editing && (!ht || ht->kind != H_NAME)) {
      d->editing = 0; key_grab = NULL;
      DoMethod(_app(obj), MUIM_Application_ReturnID, ID_OPTIONS);
      MUI_Redraw(obj, MADF_DRAWOBJECT);
    }
    if (!ht) return MUI_EventHandlerRC_Eat;
    switch (ht->kind) {
    case H_BACK: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_BACKSTAGE); break;
    case H_SIDE:
      if (side[ht->n].page >= 0) back_page(d, obj, side[ht->n].page);
      else DoMethod(_app(obj), MUIM_Application_ReturnID, side[ht->n].id);
      break;
    case H_TEMPLATE: d->value = ht->n; DoMethod(_app(obj), MUIM_Application_ReturnID, ID_TEMPLATE); break;
    case H_RECENT: d->value = ht->n; DoMethod(_app(obj), MUIM_Application_ReturnID, ID_RECENT); break;
    case H_BROWSE: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_OPEN); break;
    case H_PDF: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_PDF); break;
    case H_TEXT: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_SAVETXT); break;
    case H_WP29_IMAGE: case H_WP29_OPTIONS: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_WP29_FIRST + (ht->kind - H_WP29_IMAGE)); break;
    case H_WINDOW: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_WINPREFS); break;
    case H_WP26_MORE: DoMethod(_app(obj), MUIM_Application_ReturnID, ID_WP26_FIRST + 3); break;
    case H_AUTOSAVE: wp24_autosave_set(ht->n); MUI_Redraw(obj, MADF_DRAWOBJECT); break;   /* (WP24) */
    case H_CM: case H_THEME: case H_LANG: case H_SIZE:
      if (ht->kind == H_CM) d->metric = ht->n == 0;
      else if (ht->kind == H_THEME) d->dark = ht->n;
      else if (ht->kind == H_SIZE) d->large = ht->n;
      else d->lang = ht->n;
      DoMethod(_app(obj), MUIM_Application_ReturnID, ID_OPTIONS);
      MUI_Redraw(obj, MADF_DRAWOBJECT);
      break;
    case H_NAME: d->editing = 1; key_grab = obj; MUI_Redraw(obj, MADF_DRAWOBJECT); break;
    default:
      if (ht->kind >= H_INFO0 && ht->kind < H_INFO0 + NINFOBTN) DoMethod(_app(obj), MUIM_Application_ReturnID, infobtn[ht->kind - H_INFO0].id);   /* (WP24) */
    }
    return MUI_EventHandlerRC_Eat;
  }
  if (im->Class == IDCMP_RAWKEY && !(im->Code & IECODE_UP_PREFIX)) {
    if (im->Code == 0x45) {                                          /* Esc */
      if (d->editing) { d->editing = 0; key_grab = NULL; DoMethod(_app(obj), MUIM_Application_ReturnID, ID_OPTIONS); MUI_Redraw(obj, MADF_DRAWOBJECT); }
      else DoMethod(_app(obj), MUIM_Application_ReturnID, ID_BACKSTAGE);
      return MUI_EventHandlerRC_Eat;
    }
    if (!d->editing) return 0;
    if (im->Code == 0x41) { if (d->namelen) d->name[--d->namelen] = 0; }
    else if (im->Code == 0x44 || im->Code == 0x43) { d->editing = 0; key_grab = NULL; DoMethod(_app(obj), MUIM_Application_ReturnID, ID_OPTIONS); }
    else {
      struct InputEvent ie;
      char buf[8];
      LONG n;
      memset(&ie, 0, sizeof ie);
      ie.ie_Class = IECLASS_RAWKEY; ie.ie_Code = im->Code; ie.ie_Qualifier = im->Qualifier;
      ie.ie_EventAddress = (APTR *)*((ULONG *)im->IAddress);
      n = MapRawKey(&ie, (STRPTR)buf, sizeof buf, NULL);
      if (n == 1 && (u8)buf[0] >= 32 && !((u8)buf[0] >= 127 && (u8)buf[0] < 160) && d->namelen < (int)sizeof d->name - 1) {
        d->name[d->namelen++] = buf[0]; d->name[d->namelen] = 0;
      }
    }
    MUI_Redraw(obj, MADF_DRAWOBJECT);
    return MUI_EventHandlerRC_Eat;
  }
  return 0;
}
/* the File page's dispatcher. OM_GET answers main.c (what was clicked, the options as set);
   OM_SET with MUIA_Back_Page opens a page; MUIM_Back_Set brings in the recent documents, the
   document's details and the current options. MUIM_Setup and MUIM_Cleanup bracket the time it has
   a window, so the theme's pens and the event handler are taken and let go there */
static ULONG bdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct BData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->over = -1; d->metric = 1;
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY;
    return (ULONG)obj;
  case OM_GET: {
    struct opGet *g = (struct opGet *)msg;
    d = INST_DATA(cl, obj);
    if (g->opg_AttrID == MUIA_Back_Value) { *g->opg_Storage = (ULONG)d->value; return TRUE; }
    if (g->opg_AttrID == MUIA_Back_Metric) { *g->opg_Storage = (ULONG)d->metric; return TRUE; }
    if (g->opg_AttrID == MUIA_Back_UserName) { *g->opg_Storage = (ULONG)d->name; return TRUE; }
    if (g->opg_AttrID == MUIA_Back_Dark) { *g->opg_Storage = (ULONG)d->dark; return TRUE; }
    if (g->opg_AttrID == MUIA_Back_Large) { *g->opg_Storage = (ULONG)d->large; return TRUE; }
    if (g->opg_AttrID == MUIA_Back_Language) { *g->opg_Storage = (ULONG)d->lang; return TRUE; }
    break;
  }
  case OM_SET: {
    struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList, *t;
    d = INST_DATA(cl, obj);
    while ((t = NextTagItem(&tags))) if (t->ti_Tag == MUIA_Back_Page) { d->page = (int)t->ti_Data; d->editing = 0; d->over = -1; }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 300; m->MinMaxInfo->DefWidth += 640; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += 200; m->MinMaxInfo->DefHeight += 400; m->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    theme_setup(obj);
    theme_handler(obj, &((struct BData *)INST_DATA(cl, obj))->ehn);
    return TRUE;
  case MUIM_Cleanup:
    if (key_grab == obj) key_grab = NULL;
    theme_cleanup(obj);
    break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) back_draw(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Show: ((struct BData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct BData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_HandleEvent:
    return back_events(cl, obj, (struct MUIP_HandleEvent *)msg);
  case MUIM_Back_Set: {
    struct MUIP_Back_Set *m = (struct MUIP_Back_Set *)msg;
    int i;
    d = INST_DATA(cl, obj);
    d->nrecent = 0;
    for (i = 0; i < m->nrecent && i < MAXR; i++) { strncpy(d->recent[i], m->recent[i], sizeof d->recent[i] - 1); d->recent[i][sizeof d->recent[i] - 1] = 0; d->nrecent++; }
    strncpy(d->info, m->info ? m->info : "", sizeof d->info - 1); d->info[sizeof d->info - 1] = 0;
    strncpy(d->name, m->username ? m->username : "", sizeof d->name - 1); d->name[sizeof d->name - 1] = 0;
    d->namelen = (int)strlen(d->name);
    d->metric = m->metric; d->dark = m->dark; d->lang = m->lang; d->large = m->large;
    if (_win(obj)) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
/* the class, made on first use; NULL if MUI couldn't make it */
struct MUI_CustomClass *backstage_class(void)
{
  if (!bcls) bcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct BData), (APTR)bdispatch);
  return bcls;
}
/* at the end, once the File page object has been disposed of */
void backstage_class_free(void)
{
  if (bcls) { MUI_DeleteCustomClass(bcls); bcls = NULL; }
}
