/*
 * ribbon.c - the ribbon, as WordCraft has it: a strip of tabs, and under it the tab's groups of
 * tools with their captions, all drawn by Quill (a MUI custom class) in WordCraft's light colours and
 * a sans-serif font. Lists (fonts, sizes, colours...) open in a small borderless window (PopList).
 *
 * The tools are data: tabs of groups of items. A group lays its items out in columns (a large
 * button, or small ones three to a column) or in rows (the Font and Paragraph groups), or is the
 * styles gallery.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/diskfont.h>
#include <clib/alib_protos.h>
#include "quill.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "../core/tstyle.h"
#include "ribbondef.h"
#include "wp_charpara.h"                /* (WP9, WP10: their items in the Font and Paragraph groups) */
#include "wphooks.h"

/* ------------------------------------------------------------------ */
/* the lists */
/* (the PL_ kinds are in ribbondef.h; the PL_LOOKs: look_info kinds 0-2, and last) */
#define TS_COLS 3                /* the table styles list: in three columns */
/* a drop-down list: its entries (Latin-1), how many, how they are drawn (PL_), and for PL_COLOR
   the 0xRRGGBB beside each (0xFF000000 is COLOR_AUTO: automatic, or none). open_list fills in
   the entries of the styles, table styles and fonts lists itself when one opens */
typedef RibbonList List;
/* (quill.h) Word's usual families, which every font list starts with */
const char *const ribbon_fonts[] = { "Times New Roman", "Arial", "Courier New", "Calibri", "Cambria", "Liberation Serif", "Liberation Sans",
                                     "Liberation Mono", "Carlito", "Caladea", NULL };
/* all the families: Word's usual ones (measured with the fonts Quill ships), then the installed ones */
#define MAXFONTNAMES 200
static const char *fontnames[MAXFONTNAMES + 1];
static int nfontnames;
/* the font list, made on the first call and kept to the end: ribbon_fonts, then each installed
   family not already there (NULL-terminated). The installed names point into fs, so fs must
   outlive the ribbon; later calls ignore fs and return the same list */
const char *const *ribbon_font_names(FontSet *fs)
{
  const char *fam[MAXFONTNAMES];
  int i, k, n;
  if (nfontnames) return fontnames;
  for (i = 0; ribbon_fonts[i] && nfontnames < MAXFONTNAMES; i++) fontnames[nfontnames++] = ribbon_fonts[i];
  n = fs ? fonts_families(fs, fam, MAXFONTNAMES) : 0;
  for (i = 0; i < n && nfontnames < MAXFONTNAMES; i++) {
    for (k = 0; k < nfontnames && Stricmp((STRPTR)fontnames[k], (STRPTR)fam[i]); k++) ;
    if (k == nfontnames) fontnames[nfontnames++] = fam[i];
  }
  fontnames[nfontnames] = NULL;
  return fontnames;
}
/* entry i of the font list, or NULL out of range; before the list is made, the first ten are
   ribbon_fonts' (the list always starts with them) */
const char *ribbon_font_name(int i) { return i >= 0 && i < nfontnames ? fontnames[i] : i >= 0 && i < 10 ? ribbon_fonts[i] : NULL; }
#define FONTPX 13                /* the font list's previews: pixels to the em */
/* the entries of the lists, in Latin-1 (the interface font's), shown through tr() where they are
   words. A list's choice reaches main.c as the entry's number in MUIA_Ribbon_Value (a colour list
   gives the colour, the sizes list half-points), so main.c's handling depends on this order */
static const char *const sizes[] = { "8", "9", "10", "10.5", "11", "12", "14", "16", "18", "20", "22", "24", "26", "28", "36", "48", "72" };
static const char *const colornames[] = { "Automatic", "Black", "Dark Red", "Red", "Orange", "Gold", "Green", "Teal", "Blue", "Dark Blue",
                                          "Purple", "Grey", "Slate", "Steel Blue", "Sky Blue", "Burnt Orange", "Sunflower", "Leaf Green",
                                          "More Colours..." };
static const ULONG colorvals[] = { 0xFF000000, 0x000000, 0xC00000, 0xFF0000, 0xFFA500, 0xBF9000, 0x00B050, 0x008080, 0x0070C0, 0x002060,
                                   0x7030A0, 0x808080, 0x44546A, 0x4472C4, 0x5B9BD5, 0xED7D31, 0xFFC000, 0x70AD47, COLOR_MORE };
static const char *const hlnames[] = { "No Colour", "Yellow", "Bright Green", "Turquoise", "Pink", "Blue", "Red", "Dark Blue", "Teal",
                                       "Green", "Violet", "Dark Red", "Dark Yellow", "Grey 50%", "Grey 25%", "Black" };
static const ULONG hlvals[] = { 0xFF000000, 0xFFFF00, 0x00FF00, 0x00FFFF, 0xFF00FF, 0x0000FF, 0xFF0000, 0x000080, 0x008080,
                                0x008000, 0x800080, 0x800000, 0x808000, 0x808080, 0xC0C0C0, 0x000000 };
static const char *const casenames[] = { "Sentence case.", "lowercase", "UPPERCASE", "Capitalise Each Word", "tOGGLE cASE" };
static const char *const spacings[] = { "1.0", "1.15", "1.5", "2.0", "2.5", "3.0", "Add or Remove Space Before", "Remove or Add Space After" };   /* (the last two: WP10) */
static const char *const selects[] = { "Select All", "Select Sentence", "Select Paragraph" };
static const char *const margins[] = { "Normal: 2.54 cm all round", "Narrow: 1.27 cm all round", "Moderate: 1.91 cm at the sides",
                                       "Wide: 5.08 cm at the sides", "Roomy: 3.17 cm at the sides", "Custom Margins..." };
static const char *const headers[] = { "Edit Header", "Remove Header" };
static const char *const footers[] = { "Edit Footer", "Remove Footer" };
static const char *const tocs[] = { "Insert or Update Table of Contents", "Remove Table of Contents" };
static const char *const addtexts[] = { "Do Not Show in Table of Contents", "Level 1", "Level 2", "Level 3" };
static const char *const finds[] = { "Find...", "Go To..." };
static const char *const forwards[] = { "Bring Forward", "Bring to Front" };
static const char *const backwards[] = { "Send Backward", "Send to Back" };
static const char *const alignobjs[] = { "Align Left", "Align Centre", "Align Right", "Align Top", "Align Middle", "Align Bottom",
                                         "Distribute Horizontally", "Distribute Vertically" };
static const char *const groupings[] = { "Group", "Ungroup" };
static const char *const pagenums[] = { "Top of Page, Left", "Top of Page, Centre", "Top of Page, Right", "Bottom of Page, Left", "Bottom of Page, Centre",
                                        "Bottom of Page, Right", "Page X of Y", "Format Page Numbers...", "Remove Page Numbers" };
static const char *const covers[] = { "Simple", "Banded", "Side Line", "Remove Current Cover Page" };
static const char *const orients[] = { "Portrait", "Landscape" };
static const char *const papers[] = { "A4  21 x 29.7 cm", "Letter  8.5 x 11 in", "A5  14.8 x 21 cm", "Legal  8.5 x 14 in", "A3  29.7 x 42 cm",
                                      "A6  10.5 x 14.8 cm", "B5  17.6 x 25 cm", "Executive  7.25 x 10.5 in", "Tabloid  11 x 17 in", "More Paper Sizes..." };
static const char *const breaks[] = { "Page break", "Column break", "Line break (text wrapping)", "Section break: next page",
                                      "Section break: continuous", "Section break: even page", "Section break: odd page" };
static const char *const columns[] = { "One", "Two", "Three", "Line Between" };
static const char *const linenums[] = { "None", "Continuous", "Restart Each Page", "Restart Each Section", "Line Numbering Options..." };
static const char *const hyphens[] = { "None", "Automatic" };
static const char *const pagealigns[] = { "Top", "Centre", "Bottom" };
static const char *const dropcaps[] = { "None", "Dropped", "In Margin", "Drop Cap Options..." };
static const char *const watermarks[] = { "No Watermark", "DRAFT", "CONFIDENTIAL", "SAMPLE", "DO NOT COPY", "URGENT", "Custom Watermark..." };
static const char *const pageborders[] = { "No Border", "Box", "Double Box", "Thick Box", "Dotted Box", "Dashed Box" };
static const char *const indents[] = { "0 cm", "0.5 cm", "1 cm", "1.5 cm", "2 cm", "2.5 cm", "3 cm", "4 cm" };
static const char *const spaces[] = { "0 pt", "3 pt", "6 pt", "8 pt", "10 pt", "12 pt", "18 pt", "24 pt" };
static const char *const zooms[] = { "Page width", "50%", "75%", "100%", "125%", "150%", "200%" };
static const char *const accepts[] = { "Accept and Move to Next", "Accept This Change", "Accept All Changes" };
static const char *const rejects[] = { "Reject and Move to Next", "Reject This Change", "Reject All Changes" };
/* (the interface font is Latin-1: the symbols outside it are named) */
static const char *const symbols[] = { "\251  Copyright", "\256  Registered", "TM  Trade mark", "\247  Section", "\266  Pilcrow",
                                       "EUR  Euro", "\243  Pound", "\260  Degree", "\261  Plus or minus", "\327  Times", "\367  Divide",
                                       "\275  One half", "\274  One quarter", "--  Em dash", "-  En dash", "...  Ellipsis", "\267  Bullet",
                                       "More Symbols..." };   /* (the last: WP10's Symbol window) */
static const char *const deltables[] = { "Delete Rows", "Delete Columns", "Delete Table" };
static const char *const shadenames[] = { "No Colour", "Light Grey", "Grey", "Light Blue", "Light Green", "Light Yellow", "Light Red", "More Colours..." };
static const ULONG shadevals[] = { 0xFF000000, 0xE7E6E6, 0xBFBFBF, 0xDEEAF6, 0xE2EFD9, 0xFFF2CC, 0xF8CBAD, COLOR_MORE };
static const char *const tselects[] = { "Select Cell", "Select Column", "Select Row", "Select Table" };
static const char *const autofits[] = { "AutoFit Contents", "AutoFit Window", "Fixed Column Width" };
/* in TB_ order (core/table.h) */
static const char *const tborders[] = { "All Borders", "No Border", "Outside Borders", "Inside Borders", "Inside Horizontal Border",
                                        "Inside Vertical Border", "Top Border", "Bottom Border", "Left Border", "Right Border" };
static const char *const penstyles[] = { "Single line", "Double line", "Thick line", "Dotted line", "Dashed line" };
static const char *const penwidths[] = { "1/4 pt", "1/2 pt", "3/4 pt", "1 pt", "1 1/2 pt", "2 1/4 pt", "3 pt" };
const u8 ribbon_penwidths[] = { 2, 4, 6, 8, 12, 18, 24 };
static const char *const rowheights[] = { "Automatic", "0.5 cm", "0.75 cm", "1 cm", "1.5 cm", "2 cm", "3 cm" };
const i32 ribbon_rowheights[] = { 0, 283, 425, 567, 850, 1134, 1701 };
static const char *const colwidths[] = { "1 cm", "1.5 cm", "2 cm", "2.5 cm", "3 cm", "4 cm", "5 cm", "6 cm", "8 cm", "10 cm" };
const i32 ribbon_colwidths[] = { 567, 850, 1134, 1417, 1701, 2268, 2835, 3402, 4536, 5670 };
/* objects: in SH_ order (core/doc.h), WRAP_ order and so on */
static const char *const shapenames[] = { "Rectangle", "Rounded Rectangle", "Oval", "Line", "Arrow", "Triangle", "Diamond", "Right Arrow", "Star",
  "Pentagon", "Hexagon", "Octagon", "Parallelogram", "Trapezoid", "Right Triangle", "Left Arrow", "Up Arrow", "Down Arrow",
  "Plus", "Heart", "Speech Bubble", "Chevron", "Pentagon Arrow", "Four-Point Star" };
static const char *const fillnames[] = { "No Fill", "Blue", "Light Blue", "Dark Blue", "Orange", "Gold", "Light Yellow", "Green", "Red", "Grey",
                                         "White", "Black", "More Colours..." };
static const ULONG fillvals[] = { 0xFF000000, 0x4472C4, 0x5B9BD5, 0x203864, 0xED7D31, 0xFFC000, 0xFFF2CC, 0x70AD47, 0xC00000, 0xA5A5A5,
                                  0xFFFFFF, 0x000000, COLOR_MORE };
static const char *const linenames[] = { "No Outline", "Blue", "Light Blue", "Dark Blue", "Orange", "Gold", "Light Yellow", "Green", "Red", "Grey",
                                         "White", "Black", "More Colours..." };
static const char *const positions[] = { "In Line with Text", "Top Left", "Top Centre", "Top Right", "Middle Left", "Middle Centre",
                                         "Middle Right", "Bottom Left", "Bottom Centre", "Bottom Right" };
static const char *const wraps[] = { "In Line with Text", "Square", "Tight", "Top and Bottom", "Behind Text", "In Front of Text",
                                     "More Layout Options..." };
static const char *const pastespecs[] = { "Keep Source Formatting", "Merge Formatting", "Keep Text Only", "Paste Special...", "Clipboard Pane" };   /* (the last two: WP10) */
static const char *const multilists[] = { "Bullets", "1. a. i.", "1) a) i)", "I. A. 1. a)", "1. 1.1. 1.1.1.", "Headings 1, 1.1, 1.1.1", "No Heading Numbering",
                                          "Restart at 1", "Continue Numbering", "Set Numbering Value...", "Define New List...",
                                          "Next List Level", "Previous List Level" };
static const char *const pborders[] = { "No Border", "Bottom Border", "Top Border", "Left Border", "Right Border", "Outside Borders", "All Borders",
                                        "Inside Borders", "Borders and Shading...", "Horizontal Line" };   /* (the last: WP10) */
static const char *const stylesets[] = { "Quill", "Modern", "Classic", "Elegant", "Simple", "Bold" };
static const char *const themenames[] = { "Quill", "Amiga", "Workbench", "Boing", "Kickstart", "Paper", "Lagoon", "Velvet" };   /* (TH_ order) */
static const char *const colorsets[] = { "Quill", "Blue", "Blue Green", "Green", "Orange", "Red", "Violet", "Grey" };          /* (CS_ order) */
static const char *const pagecolornames[] = { "No Colour", "Light Grey", "Cream", "Light Blue", "Light Green", "Light Yellow", "Light Pink",
                                              "Lavender", "More Colours..." };
static const ULONG pagecolorvals[] = { 0xFF000000, 0xF2F2F2, 0xFFF8E7, 0xDEEAF6, 0xE2EFD9, 0xFFF2CC, 0xFBE4D5, 0xE8E0F0, COLOR_MORE };
static const char *const underlines[] = { "Underline", "Double Underline", "Thick Underline", "Dotted Underline", "Dashed Underline", "Wavy Underline",
                                          "Words Only", "More Underlines..." };   /* (UL_ order, then the Font window) */
static const char *const themefonts[] = { "Times New Roman / Arial", "Calibri / Cambria", "Calibri", "Cambria / Calibri", "Arial", "Times New Roman" };
static const char *const pspacings[] = { "No Paragraph Space", "Compact", "Tight", "Open", "Relaxed", "Double" };
static const char *const rotates[] = { "Rotate Right 90\260", "Rotate Left 90\260", "Flip Vertical", "Flip Horizontal", "More Rotation Options..." };
/* the lists by number (Item.list), in the order of lists[] below: the two must be kept in step */
enum { L_NONE, L_FONTS, L_SIZES, L_COLORS, L_HIGHLIGHT, L_CASE, L_SPACING, L_SELECT, L_MARGINS, L_ORIENT, L_PAPER, L_BREAKS,
       L_INDENT, L_SPACE, L_ZOOM, L_ACCEPT, L_REJECT, L_SYMBOLS, L_DELTABLE, L_SHADING, L_STYLES,
       L_COLUMNS, L_LINENUM, L_HYPHEN, L_PAGEALIGN, L_DROPCAP, L_WATERMARK, L_BORDERS,
       L_TSELECT, L_AUTOFIT, L_TBORDERS, L_PENSTYLE, L_PENWIDTH, L_PENCOLOR, L_ROWH, L_COLW, L_TSTYLES,
       L_SHAPES, L_OBJFILL, L_OBJLINE, L_POSITION, L_WRAP, L_ROTATE,
       L_PASTESPEC, L_MULTILIST, L_PBORDERS, L_STYLESETS, L_THEMEFONTS, L_PSPACING,
       L_HEADERS, L_FOOTERS, L_TOCS, L_ADDTEXT, L_FINDS, L_FORWARD, L_BACKWARD, L_ALIGNOBJ, L_GROUPS, L_PAGENUMS, L_COVERS, L_THEMES, L_COLORSETS, L_PAGECOLORS, L_UNDERLINES };
#define N(a) ((int)(sizeof(a) / sizeof(a[0])))
/* every list, indexed by L_ (entry 0, L_NONE, is for items with no list) */
static const List lists[] = {
  { NULL, 0, PL_TEXT, NULL }, { ribbon_fonts, 10, PL_FONTS, NULL }, { sizes, N(sizes), PL_TEXT, NULL },
  { colornames, N(colornames), PL_COLOR, colorvals }, { hlnames, N(hlnames), PL_COLOR, hlvals }, { casenames, N(casenames), PL_TEXT, NULL },
  { spacings, N(spacings), PL_TEXT, NULL }, { selects, N(selects), PL_TEXT, NULL }, { margins, N(margins), PL_TEXT, NULL },
  { orients, N(orients), PL_TEXT, NULL }, { papers, N(papers), PL_TEXT, NULL }, { breaks, N(breaks), PL_TEXT, NULL },
  { indents, N(indents), PL_TEXT, NULL }, { spaces, N(spaces), PL_TEXT, NULL }, { zooms, N(zooms), PL_TEXT, NULL },
  { accepts, N(accepts), PL_TEXT, NULL }, { rejects, N(rejects), PL_TEXT, NULL }, { symbols, N(symbols), PL_TEXT, NULL },
  { deltables, N(deltables), PL_TEXT, NULL }, { shadenames, N(shadenames), PL_COLOR, shadevals }, { NULL, 0, PL_STYLES, NULL },
  { columns, N(columns), PL_TEXT, NULL }, { linenums, N(linenums), PL_TEXT, NULL }, { hyphens, N(hyphens), PL_TEXT, NULL },
  { pagealigns, N(pagealigns), PL_TEXT, NULL }, { dropcaps, N(dropcaps), PL_TEXT, NULL }, { watermarks, N(watermarks), PL_TEXT, NULL },
  { pageborders, N(pageborders), PL_TEXT, NULL },
  { tselects, N(tselects), PL_TEXT, NULL }, { autofits, N(autofits), PL_TEXT, NULL }, { tborders, N(tborders), PL_TEXT, NULL },
  { penstyles, N(penstyles), PL_TEXT, NULL }, { penwidths, N(penwidths), PL_TEXT, NULL }, { colornames, N(colornames), PL_COLOR, colorvals },
  { rowheights, N(rowheights), PL_TEXT, NULL }, { colwidths, N(colwidths), PL_TEXT, NULL }, { NULL, 0, PL_TSTYLES, NULL },
  { shapenames, N(shapenames), PL_TEXT, NULL }, { fillnames, N(fillnames), PL_COLOR, fillvals }, { linenames, N(linenames), PL_COLOR, fillvals },
  { positions, N(positions), PL_TEXT, NULL }, { wraps, N(wraps), PL_TEXT, NULL }, { rotates, N(rotates), PL_TEXT, NULL },
  { pastespecs, N(pastespecs), PL_TEXT, NULL }, { multilists, N(multilists), PL_TEXT, NULL }, { pborders, N(pborders), PL_TEXT, NULL },
  { stylesets, N(stylesets), PL_LOOKSET, NULL }, { themefonts, N(themefonts), PL_TEXT, NULL }, { pspacings, N(pspacings), PL_TEXT, NULL },
  { headers, N(headers), PL_TEXT, NULL }, { footers, N(footers), PL_TEXT, NULL }, { tocs, N(tocs), PL_TEXT, NULL },
  { addtexts, N(addtexts), PL_TEXT, NULL }, { finds, N(finds), PL_TEXT, NULL },
  { forwards, N(forwards), PL_TEXT, NULL }, { backwards, N(backwards), PL_TEXT, NULL }, { alignobjs, N(alignobjs), PL_TEXT, NULL },
  { groupings, N(groupings), PL_TEXT, NULL }, { pagenums, N(pagenums), PL_TEXT, NULL }, { covers, N(covers), PL_TEXT, NULL },
  { themenames, N(themenames), PL_LOOKTHEME, NULL }, { colorsets, N(colorsets), PL_LOOKCOLOR, NULL }, { pagecolornames, N(pagecolornames), PL_COLOR, pagecolorvals },
  { underlines, N(underlines), PL_UNDERLINE, NULL },
};

/* ------------------------------------------------------------------ */
/* the tools */
/* the item kinds, sizes, flags, Item and Group are in ribbondef.h (shared with the work packages).
   A tab: its name, its groups, and whether it is contextual: 0 always shown, else shown only while
   a state bit is on (see tab_visible: 1 in a table, 2 a picture selected, 3 a shape, 4 a header or
   footer edited, 5 a note, comment or text box edited; RB_WP_BASE and up: that package state bit) */
typedef struct { const char *name; const Group *groups; int n; UWORD contextual; } Tab;

/* the Home tab: Clipboard, Font and Paragraph (in two rows), the styles gallery and Editing. Each
   Item reads: kind, size, icon, list, label, id, state shown as checked, state needed, flags, field
   width, help (the bubble's title and shortcut; see tips below for the longer text) */
static const Item g_clip[] = {
  { K_DROP, S_LARGE, IC_PASTE, L_PASTESPEC, "Paste", ID_PASTEDD, 0, 0, F_SPLIT, 0, "Paste (Amiga-V)" },
  { K_BUTTON, S_SMALL, IC_CUT, 0, "Cut", ID_CUT, 0, RB_SELECTION, 0, 0, "Cut (Amiga-X)" },
  { K_BUTTON, S_SMALL, IC_COPY, 0, "Copy", ID_COPY, 0, RB_SELECTION, 0, 0, "Copy (Amiga-C)" },
  { K_BUTTON, S_SMALL, IC_BRUSH, 0, "Format", ID_BRUSH, RB_BRUSH, 0, 0, 0, "Copy the formatting at the caret to the next text you select" },
};
/* Save, Undo and Redo: not a group on a tab but the quick access buttons at the right end of the
   tab strip, always in reach and taking no room from the tabs' tools (see qa below) */
#define NQA 3
static const Item g_undo[NQA] = {
  { K_BUTTON, S_SMALL, IC_SAVE, 0, "Save", ID_SAVE, 0, 0, 0, 0, "Save (Amiga-S)" },
  { K_BUTTON, S_SMALL, IC_UNDO, 0, "Undo", ID_UNDO, 0, RB_CANUNDO, 0, 0, "Undo (Amiga-Z)" },
  { K_BUTTON, S_SMALL, IC_REDO, 0, "Redo", ID_REDO, 0, RB_CANREDO, 0, 0, "Redo (Amiga-Y)" },
};
static const Item g_font[] = {
  { K_FIELD, S_SMALL, 0, L_FONTS, NULL, ID_FONTDD, 0, 0, 0, 98, "Font" },
  { K_FIELD, S_SMALL, 0, L_SIZES, NULL, ID_SIZEDD, 0, 0, 0, 34, "Font size" },
  { K_BUTTON, S_ICON, IC_GROW, 0, NULL, ID_GROW, 0, 0, 0, 0, "Grow font" },
  { K_BUTTON, S_ICON, IC_SHRINK, 0, NULL, ID_SHRINK, 0, 0, 0, 0, "Shrink font" },
  { K_DROP, S_ICON, IC_CASE, L_CASE, NULL, ID_CASE, 0, 0, 0, 0, "Change case" },
  { K_BUTTON, S_ICON, IC_CLEAR, 0, NULL, ID_CLEARFMT, 0, 0, 0, 0, "Clear all formatting" },
  { K_BUTTON, S_ICON, IC_CBORDER, 0, NULL, ID_CBORDER, 0, 0, 0, 0, "Character border" },
  { K_BUTTON, S_ICON, IC_BOLD, 0, NULL, ID_BOLD, RB_BOLD, 0, F_ROW1, 0, "Bold (Amiga-B)" },
  { K_BUTTON, S_ICON, IC_ITALIC, 0, NULL, ID_ITALIC, RB_ITALIC, 0, F_ROW1, 0, "Italic (Amiga-I)" },
  { K_DROP, S_ICON, IC_UNDERLINE, L_UNDERLINES, NULL, ID_UNDERLINEDD, RB_UNDERLINE, 0, F_ROW1 | F_SPLIT, 0, "Underline (Amiga-U)" },
  { K_BUTTON, S_ICON, IC_STRIKE, 0, NULL, ID_STRIKE, RB_STRIKE, 0, F_ROW1, 0, "Strikethrough" },
  { K_BUTTON, S_ICON, IC_SUB, 0, NULL, ID_SUB, RB_SUB, 0, F_ROW1, 0, "Subscript (Amiga--)" },
  { K_BUTTON, S_ICON, IC_SUPER, 0, NULL, ID_SUPER, RB_SUPER, 0, F_ROW1, 0, "Superscript (Amiga-+)" },
  { K_DROP, S_ICON, IC_HIGHLIGHT, L_HIGHLIGHT, NULL, ID_HIGHLIGHT, 0, 0, F_ROW1, 0, "Text highlight colour" },
  { K_DROP, S_ICON, IC_FONTCOLOR, L_COLORS, NULL, ID_FONTCOLOR, 0, 0, F_ROW1, 0, "Font colour" },
  { K_DROP, S_ICON, 0, 0, NULL, ID_C_EFFECTS, 0, 0, F_ROW1, 0, "Text effects and typography", &wp9_effects_list, WP9_ICON_EFFECTS },   /* (WP9) */
  { K_BUTTON, S_ICON, IC_FONTDLG, 0, NULL, ID_FONTDLG, 0, 0, F_ROW1, 0, "Font: effects, underlines, character spacing" },
};
static const Item g_para[] = {
  { K_DROP, S_ICON, IC_BULLETS, 0, NULL, ID_P_BULLETLIB, RB_BULLETS, 0, F_SPLIT, 0, "Bullets", &wp10_bullet_list, NULL, ID_BULLETS },   /* (lists: WP10) */
  { K_DROP, S_ICON, IC_NUMBERS, 0, NULL, ID_P_NUMLIB, RB_NUMBERS, 0, F_SPLIT, 0, "Numbering", &wp10_number_list, NULL, ID_NUMBERS },
  { K_DROP, S_ICON, IC_MULTILIST, L_MULTILIST, NULL, ID_MULTILISTDD, 0, 0, 0, 0, "Multilevel lists, heading numbering, restarting the numbering" },
  { K_BUTTON, S_ICON, IC_OUTDENT, 0, NULL, ID_OUTDENT, 0, 0, 0, 0, "Decrease indent (Amiga-K)" },
  { K_BUTTON, S_ICON, IC_INDENT, 0, NULL, ID_INDENT, 0, 0, 0, 0, "Increase indent (Amiga-M)" },
  { K_BUTTON, S_ICON, IC_SORT, 0, NULL, ID_SORT, 0, 0, 0, 0, "Sort" },
  { K_BUTTON, S_ICON, IC_MARKS, 0, NULL, ID_MARKS, RB_MARKS, 0, 0, 0, "Show paragraph marks and other formatting symbols" },
  { K_BUTTON, S_ICON, IC_LEFT, 0, NULL, ID_LEFT, RB_ALIGN0, 0, F_ROW1, 0, "Align left (Amiga-[)" },
  { K_BUTTON, S_ICON, IC_CENTER, 0, NULL, ID_CENTER, RB_ALIGN1, 0, F_ROW1, 0, "Centre (Amiga-E)" },
  { K_BUTTON, S_ICON, IC_RIGHT, 0, NULL, ID_RIGHT, RB_ALIGN2, 0, F_ROW1, 0, "Align right (Amiga-])" },
  { K_BUTTON, S_ICON, IC_JUSTIFY, 0, NULL, ID_JUSTIFY, RB_ALIGN3, 0, F_ROW1, 0, "Justify (Amiga-J)" },
  { K_DROP, S_ICON, IC_SPACING, L_SPACING, NULL, ID_SPACINGDD, 0, 0, F_ROW1, 0, "Line spacing" },
  { K_DROP, S_ICON, IC_PSHADE, L_SHADING, NULL, ID_PSHADEDD, 0, 0, F_ROW1, 0, "Shading behind the paragraph" },
  { K_DROP, S_ICON, IC_PBORDERS, L_PBORDERS, NULL, ID_PBORDERDD, 0, 0, F_ROW1, 0, "Borders round the paragraph" },
  { K_BUTTON, S_ICON, IC_PARADLG, 0, NULL, ID_PARADLG, 0, 0, F_ROW1, 0, "Paragraph: indents, spacing, line and page breaks" },
};
static const Item g_styles[] = { { K_GALLERY, S_LARGE, 0, L_STYLES, NULL, ID_STYLEPICK, 0, 0, 0, 0, "Styles" } };
static const Item g_stools[] = {
  { K_BUTTON, S_SMALL, IC_STYLESPANE, 0, "Manage", ID_STYLESPANE, 0, 0, 0, 0, "Every style: apply, change, make or delete them" },
  { K_BUTTON, S_SMALL, IC_NEWSTYLE, 0, "New", ID_NEWSTYLE, 0, 0, 0, 0, "A new style from the formatting at the caret" },
  { K_BUTTON, S_SMALL, IC_MODSTYLE, 0, "Modify", ID_MODSTYLE, 0, 0, 0, 0, "Change the caret's paragraph style (every paragraph in it follows)" } };
static const Item g_edit[] = {
  { K_DROP, S_SMALL, IC_FIND, L_FINDS, "Find", ID_FINDDD, 0, 0, F_SPLIT, 0, "Find (Amiga-F)" },
  { K_BUTTON, S_SMALL, IC_REPLACE, 0, "Replace", ID_REPLACE, 0, 0, 0, 0, "Replace" },
  { K_DROP, S_SMALL, IC_SELECT, L_SELECT, "Select", ID_SELECTDD, 0, 0, 0, 0, "Select" },
};
/* the longer explanations in the help bubbles (the Item's own help says less, or nothing more
   than its name); tools not listed here show their Item help */
static const struct { UWORD id; const char *tip; } tips[] = {
  { ID_SAVE, "Saves the document under its name (the first time, asks for one)." },
  { ID_UNDO, "Takes back your last change. Press it again to go further back, one step at a time." },
  { ID_REDO, "Puts back a change you undid." },
  { ID_PASTE, "Puts what you cut or copied at the caret, replacing any selected text. The arrow offers other ways to paste, such as text only." },
  { ID_CUT, "Removes the selection and keeps it on the clipboard, ready to paste somewhere else." },
  { ID_COPY, "Copies the selection to the clipboard, leaving the document as it is." },
  { ID_BRUSH, "Picks up the formatting at the caret. Then select other text to give it the same look." },
  { ID_FONTDD, "The typeface of the selected text. Type a name or pick one from the list; each is shown in its own design." },
  { ID_SIZEDD, "The size of the selected text, in points. Type any size or pick one from the list." },
  { ID_GROW, "Makes the selected text a size bigger." },
  { ID_SHRINK, "Makes the selected text a size smaller." },
  { ID_CASE, "Changes the selected text to lower case, UPPER CASE, Title Case or Sentence case." },
  { ID_CLEARFMT, "Takes the selected text back to its paragraph style's plain look: no bold, colours or other changes." },
  { ID_BOLD, "Makes the selected text bold, or plain again if it already is." },
  { ID_ITALIC, "Slants the selected text, or straightens it again." },
  { ID_UNDERLINE, "Underlines the selected text. The arrow beside it has other kinds: double, thick, dotted, dashed, wavy, or the words only." },
  { ID_CBORDER, "A thin box round the selected characters. Click again to take it off." },
  { ID_STRIKE, "Draws a line through the selected text." },
  { ID_SUB, "Makes the selected text small and low, as in H2O." },
  { ID_SUPER, "Makes the selected text small and raised, as in 10 squared or 1st." },
  { ID_HIGHLIGHT, "Marks the selected text with a colour behind it, like a highlighter pen." },
  { ID_FONTCOLOR, "The colour of the selected text. Automatic is black on a light page." },
  { ID_FONTDLG, "Opens the Font window: every font setting at once, with effects such as small caps, kinds of underline and the spacing between letters." },
  { ID_BULLETS, "Starts a bulleted list, or ends it, for the selected paragraphs." },
  { ID_NUMBERS, "Starts a numbered list, or ends it, for the selected paragraphs." },
  { ID_MULTILISTDD, "Lists with several levels (1, 1.1, 1.1.1), numbered headings, and starting a list again from 1." },
  { ID_OUTDENT, "Moves the paragraph back towards the margin; in a list, up a level." },
  { ID_INDENT, "Moves the paragraph further in from the margin; in a list, down a level." },
  { ID_SORT, "Puts the selected paragraphs in order: alphabetically, by number or by date, by the whole paragraph or by a field of it." },
  { ID_C_ENCLOSE, "Puts one or two characters in a circle, square, triangle or diamond." },
  { ID_C_SHADETOGGLE, "Shades the background of the selected characters (grey, or the colour chosen with the arrow)." },
  { ID_C_EFFECTS, "Outline, shadow, emboss and engrave; hidden text; ligatures and number forms Word uses." },
  { ID_P_DISTRIBUTE, "Spreads the characters of every line evenly between the margins, the last line too." },
  { ID_P_BULLETLIB, "Starts a bulleted list. The arrow offers other bullets and Define New Bullet." },
  { ID_P_NUMLIB, "Starts a numbered list. The arrow offers other numbers and Define New Number Format." },
  { ID_MARKS, "Shows the marks that don't print: paragraph ends, tabs, spaces and breaks. Useful for seeing why text sits where it does." },
  { ID_LEFT, "Lines the paragraph up against the left margin, with a ragged right edge." },
  { ID_CENTER, "Centres each line of the paragraph between the margins, as for a title." },
  { ID_RIGHT, "Lines the paragraph up against the right margin." },
  { ID_JUSTIFY, "Spreads the words so both edges of the paragraph are straight, like a newspaper column." },
  { ID_SPACINGDD, "How far apart the lines of the paragraph are: single, one and a half, double and so on." },
  { ID_PSHADEDD, "A colour behind the whole paragraph." },
  { ID_PBORDERDD, "Lines above, below or around the paragraph." },
  { ID_PARADLG, "Opens the Paragraph window: exact indents and spacing, and settings that keep lines together across pages." },
  { ID_STYLEPICK, "Styles give text a consistent look in one click: headings, quotes, titles. Change a style and every paragraph using it follows." },
  { ID_FIND, "Looks for a word or phrase in the document, with options for matching case and whole words. The arrow offers Go To, for a page by its number." },
  { ID_REPLACE, "Finds text and replaces it with something else, one at a time or all at once." },
  { ID_SELECTDD, "Selects all the text, or all text formatted like the selection." },
  { ID_TABLE, "Puts a table at the caret: pick how many rows and columns. Inside a cell it puts a table in the cell." },
  { ID_PICTURE, "Adds a PNG or JPEG picture from disk at the caret. Click it afterwards to move, size, turn or wrap text round it." },
  { ID_SHAPESDD, "Draws a shape on the page: rectangles, arrows, stars, hearts, speech bubbles and more. Shapes can hold text." },
  { ID_TEXTBOX, "A box of text you can place anywhere, with the page's text flowing round it. Double-click it to type in it." },
  { ID_COMMENT, "Adds a note beside the selected text for you or others to read; it doesn't print as part of the text." },
  { ID_PAGEBREAK, "Starts a new page at the caret; the text after it moves to the top of the next page." },
  { ID_MARGINSDD, "The empty space round the edges of the page." },
  { ID_ORIENTDD, "Portrait (tall) or landscape (wide) pages." },
  { ID_PAPERDD, "The paper the document is for: A4, Letter, A5 or Legal." },
  { ID_COLUMNSDD, "Puts the text of this section in two or three columns, like a newsletter." },
  { ID_BREAKSDD, "Breaks that start a new page, column or section. A new section can have its own page size, columns and headers." },
  { ID_ZOOMDD, "How big the page looks on screen. It doesn't change the document." },
  { ID_TRACK, "While on, every insertion and deletion is marked with who made it, so changes can be reviewed and accepted or rejected." },
  { ID_SPELL, "Goes through the document word by word, stopping at words not in the dictionary and offering corrections." },
  { ID_TOC, "Builds a table of contents from the headings, with page numbers. Use Heading styles for the headings you want listed. The arrow can take it out again." },
  { ID_ADDTEXTDD, "Puts the caret's paragraph in the table of contents at level 1, 2 or 3 (by giving it a Heading style), or leaves it out." },
  { ID_THEMEDD, "Gives the whole document a look: a style set, fonts for the text and headings, and colours, chosen to go together." },
  { ID_COLORSETDD, "Colours the headings, the title and the subtitle, from a set that goes together." },
  { ID_PAGECOLORDD, "A colour behind the text on every page, on the screen, in the PDF and in Word." },
  { ID_SETDEFAULT, "Remembers this document's style set, fonts, spacing and colours for new blank documents." },
  { ID_COVERDD, "Puts a cover page before the text: the title, a subtitle, your name and the date, set out in the design chosen. Choosing again swaps the design." },
  { ID_PAGENUMDD, "Puts the page number at the top or foot of every page, or \"Page 1 of 3\". Format Page Numbers sets roman numerals or letters and the first number." },
  { ID_BOOKMARK, "Names the selected text (or the caret's paragraph) so you can go back to it, link to it or refer to it. Lists, adds and removes bookmarks." },
  { ID_XREF, "Types a reference to a bookmark or heading: its text, or the page it is on, which keeps up to date. Ctrl-click one to go there." },
  { ID_FORWARDDD, "Moves the selected object up one in the stack, in front of the one above it. The arrow offers Bring to Front." },
  { ID_BACKWARDDD, "Moves the selected object down one in the stack, behind the one below it. The arrow offers Send to Back." },
  { ID_SELPANE, "Lists the floating objects on each page, the topmost first. Choose one to select it, or move it up and down the stack." },
  { ID_ALIGNOBJDD, "Lines a floating object up with the margins. With several chosen (Shift-click), lines them up with each other or spaces them evenly." },
  { ID_BLANKPAGE, "Puts an empty page at the caret: the text after it moves on to the page after." },
  { ID_NOTE, "Puts a small number at the caret and a note with the same number at the foot of the page." },
  { ID_HFHEADER, "Text at the top of every page, such as a title or chapter name. Double-click a header on the page to edit it." },
  { ID_HFFOOTER, "Text at the bottom of every page, often the page number. Double-click a footer on the page to edit it." },
  { ID_HEADERDD, "Text at the top of every page, such as a title or chapter name. Double-click a header on the page to edit it. The arrow can remove it." },
  { ID_FOOTERDD, "Text at the bottom of every page, often the page number. Double-click a footer on the page to edit it. The arrow can remove it." },
  { ID_OBJWRAPDD, "How the page's text flows round the object: square, tight to its outline, above and below only, or behind or in front." },
  { ID_OBJROTDD, "Turns the object a quarter turn or flips it. For any angle, drag the round handle above it, or use More Rotation Options." },
  { ID_GROUP, "Joins the objects you chose with Shift-click into one, to move, size and turn together." },
  { ID_UNGROUP, "Takes a group apart again, leaving each object where it was." },
  { ID_VREAD, "Shows two pages side by side with the tools hidden, for reading. Press Esc to come back." },
  { ID_DARKPAGE, "Shows the page dark with light text, easier on the eyes at night. The document and printouts aren't changed." }
};
/* the tips entry for a command id, or NULL (a linear search: the bubble only wants it once) */
static ULONG split_id(UWORD id);
static const char *tip_for(UWORD id)
{
  int i;
  for (i = 0; i < (int)N(tips); i++) if (tips[i].id == id) return tips[i].tip;
  if (split_id(id) != id) return tip_for((UWORD)split_id(id));     /* a split button: its command's */
  return NULL;
}
/* the Home tab's groups; g_styles' one item is the whole gallery */
static const Group home[] = { { "Clipboard", G_COLS, g_clip, N(g_clip) }, { "Font", G_ROWS, g_font, N(g_font) },
                              { "Paragraph", G_ROWS, g_para, N(g_para) }, { "Styles", G_GALLERY, g_styles, 1 },
                              { "Editing", G_COLS, g_edit, N(g_edit) } };

/* the Insert tab */
static const Item i_pages[] = {
  { K_BUTTON, S_LARGE, IC_PAGEBREAK, 0, "Page Break", ID_PAGEBREAK, 0, 0, 0, 0, "Start a new page (Amiga-P)" },
  { K_DROP, S_SMALL, IC_COVER, L_COVERS, "Cover Page", ID_COVERDD, 0, 0, F_NEWCOL, 0, "A title page before the text, in one of three designs" },
  { K_BUTTON, S_SMALL, IC_BLANKPAGE, 0, "Blank Page", ID_BLANKPAGE, 0, 0, 0, 0, "An empty page at the caret" },
  { K_BUTTON, S_SMALL, IC_BREAKS, 0, "Line Break", ID_LINEBREAK, 0, 0, 0, 0, "A new line in the paragraph (Shift-Return)" } };
static const Item i_tables[] = { { K_BUTTON, S_LARGE, IC_TABLE, 0, "Table", ID_TABLE, 0, 0, 0, 0, "Insert a table (Amiga-T); in a cell, a table inside it" },
  { K_BUTTON, S_SMALL, IC_TOTABLE, 0, "Text to Table", ID_TOTABLE, 0, RB_SELECTION, F_NEWCOL, 0, "The selected paragraphs as a table, split at tabs or commas" } };
static const Item i_ill[] = {
  { K_BUTTON, S_LARGE, IC_PICTURE, 0, "Pictures", ID_PICTURE, 0, 0, 0, 0, "A PNG or JPEG picture from a file" },
  { K_DROP, S_LARGE, IC_SHAPES, L_SHAPES, "Shapes", ID_SHAPESDD, 0, 0, 0, 0, "A rectangle, oval, arrow or other shape, in front of the text" },
  { K_BUTTON, S_SMALL, IC_TEXTBOX, 0, "Text Box", ID_TEXTBOX, 0, 0, F_NEWCOL, 0, "A box of text the page's text runs round" },
  { K_BUTTON, S_SMALL, IC_PICSIZE, 0, "Size", ID_PICSIZE, 0, RB_PICTURE, 0, 0, "The size of the picture by the caret" } };
static const Item i_link[] = { { K_BUTTON, S_LARGE, IC_LINK, 0, "Link", ID_LINK, 0, 0, 0, 0, "A link to a web page or e-mail address" },
  { K_BUTTON, S_SMALL, IC_BOOKMARK, 0, "Bookmark", ID_BOOKMARK, 0, 0, F_NEWCOL, 0, "A name for the selected text, to go back to or link to" },
  { K_BUTTON, S_SMALL, IC_XREF, 0, "Cross-reference", ID_XREF, 0, 0, 0, 0, "The text of a bookmark or heading, or the page it is on" } };
static const Item i_comm[] = { { K_BUTTON, S_LARGE, IC_COMMENT, 0, "Comment", ID_COMMENT, 0, 0, 0, 0, "A comment on the selection" } };
static const Item i_hf[] = {
  { K_DROP, S_SMALL, IC_HEADER, L_HEADERS, "Header", ID_HEADERDD, 0, 0, F_SPLIT, 0, "Edit the page's header (Amiga-H, or double-click it)" },
  { K_DROP, S_SMALL, IC_FOOTER, L_FOOTERS, "Footer", ID_FOOTERDD, 0, 0, F_SPLIT, 0, "Edit the page's footer (or double-click it)" },
  { K_DROP, S_SMALL, IC_PAGENUM, L_PAGENUMS, "Page Number", ID_PAGENUMDD, 0, 0, 0, 0, "Page numbers at the top or foot of the page, their format, or none" } };
static const Item i_text[] = { { K_BUTTON, S_LARGE, IC_DATE, 0, "Date", ID_DATE, 0, 0, 0, 0, "Today's date" },
  { K_DROP, S_LARGE, IC_DROPCAP, L_DROPCAP, "Drop Cap", ID_DROPCAPDD, 0, 0, 0, 0, "The paragraph's first letter large, over three lines" } };
static const Item i_sym[] = { { K_DROP, S_LARGE, IC_SYMBOL, L_SYMBOLS, "Symbol", ID_SYMBOLDD, 0, 0, 0, 0, "A symbol" } };
static const Group insert[] = { { "Pages", G_COLS, i_pages, N(i_pages) }, { "Tables", G_COLS, i_tables, N(i_tables) }, { "Illustrations", G_COLS, i_ill, N(i_ill) },
                                { "Links", G_COLS, i_link, N(i_link) }, { "Comments", G_COLS, i_comm, 1 }, { "Header & Footer", G_COLS, i_hf, N(i_hf) }, { "Text", G_COLS, i_text, N(i_text) },
                                { "Symbols", G_COLS, i_sym, 1 } };

/* the Layout tab: page setup, the paragraph's indents and spacing as fields, tab stops */
static const Item l_setup[] = {
  { K_DROP, S_LARGE, IC_MARGINS, L_MARGINS, "Margins", ID_MARGINSDD, 0, 0, 0, 0, "Margins" },
  { K_DROP, S_LARGE, IC_ORIENT, L_ORIENT, "Orientation", ID_ORIENTDD, 0, 0, 0, 0, "Portrait or landscape" },
  { K_DROP, S_LARGE, IC_SIZE, L_PAPER, "Size", ID_PAPERDD, 0, 0, 0, 0, "Paper size" },
  { K_DROP, S_LARGE, IC_COLUMNS, L_COLUMNS, "Columns", ID_COLUMNSDD, 0, 0, 0, 0, "The section's text in columns" },
  { K_DROP, S_SMALL, IC_BREAKS, L_BREAKS, "Breaks", ID_BREAKSDD, 0, 0, F_NEWCOL, 0, "Page, column, line and section breaks" },
  { K_DROP, S_SMALL, IC_LINENUM, L_LINENUM, "Line Numbers", ID_LINENUMDD, 0, 0, 0, 0, "Numbers beside the lines" },
  { K_DROP, S_SMALL, IC_HYPHEN, L_HYPHEN, "Hyphenation", ID_HYPHENDD, 0, 0, 0, 0, "Words broken with a hyphen at the ends of lines" },
  { K_DROP, S_SMALL, IC_PAGEALIGN, L_PAGEALIGN, "Vertical Align", ID_PAGEALIGNDD, 0, 0, F_NEWCOL, 0, "The text at the top, middle or bottom of the page" },
  { K_BUTTON, S_SMALL, IC_MARGINS, 0, "Page Setup", ID_PAGESETUPDLG, 0, 0, 0, 0, "The page's size and margins exactly, for the whole document or the caret's section" } };
static const Item l_para[] = {
  { K_LABEL, S_SMALL, 0, 0, "Indent", 0, 0, 0, 0, 0, NULL },
  { K_FIELD, S_SMALL, 0, L_INDENT, "Left:", ID_INDENTL, 0, 0, 0, 54, "Left indent" },
  { K_FIELD, S_SMALL, 0, L_INDENT, "Right:", ID_INDENTR, 0, 0, 0, 54, "Right indent" },
  { K_LABEL, S_SMALL, 0, 0, "Spacing", 0, 0, 0, F_NEWCOL, 0, NULL },
  { K_FIELD, S_SMALL, 0, L_SPACE, "Before:", ID_SPACEB, 0, 0, 0, 46, "Space before the paragraph" },
  { K_FIELD, S_SMALL, 0, L_SPACE, "After:", ID_SPACEA, 0, 0, 0, 46, "Space after the paragraph" } };
static const Item l_tabs[] = { { K_BUTTON, S_LARGE, IC_RULER, 0, "Tabs", ID_TABS, 0, 0, 0, 0, "Tab stops: where, how they align, and their leaders" } };
/* Arrange: for the selected floating object (or those chosen together with Shift-click) */
static const Item l_arrange[] = {
  { K_DROP, S_SMALL, IC_POSITION, L_POSITION, "Position", ID_OBJPOSDD, 0, RB_FLOATSEL, 0, 0, "Where on the page it goes, the text wrapping round it" },
  { K_DROP, S_SMALL, IC_WRAP, L_WRAP, "Wrap Text", ID_OBJWRAPDD, 0, RB_FLOATSEL, 0, 0, "How the text runs round it" },
  { K_DROP, S_SMALL, IC_FORWARD, L_FORWARD, "Bring Forward", ID_FORWARDDD, 0, RB_FLOATSEL, F_SPLIT, 0, "In front of the object above it" },
  { K_DROP, S_SMALL, IC_BACKWARD, L_BACKWARD, "Send Backward", ID_BACKWARDDD, 0, RB_FLOATSEL, F_NEWCOL | F_SPLIT, 0, "Behind the object below it" },
  { K_BUTTON, S_SMALL, IC_SELPANE, 0, "Selection Pane", ID_SELPANE, 0, 0, 0, 0, "Every floating object, top first: choose one, or change their order" },
  { K_DROP, S_SMALL, IC_ALIGNOBJ, L_ALIGNOBJ, "Align", ID_ALIGNOBJDD, 0, RB_FLOATSEL, 0, 0, "Line objects up with the margins, or with each other" },
  { K_DROP, S_SMALL, IC_GROUP, L_GROUPS, "Group", ID_GROUPDD, 0, RB_FLOATSEL, F_NEWCOL, 0, "Join the objects chosen with Shift-click, or take a group apart" },
  { K_DROP, S_SMALL, IC_ROTATE, L_ROTATE, "Rotate", ID_OBJROTDD, 0, RB_FLOATSEL, 0, 0, "Turned by any angle, or flipped" } };
static const Group layout[] = { { "Page Setup", G_COLS, l_setup, N(l_setup) }, { "Paragraph", G_COLS, l_para, N(l_para) }, { "Tabs", G_COLS, l_tabs, 1 },
                                { "Arrange", G_COLS, l_arrange, N(l_arrange) } };

/* the Design tab: the whole document's look, the styles' tools and the page background */
static const Item d_bg[] = {
  { K_DROP, S_LARGE, IC_WATERMARK, L_WATERMARK, "Watermark", ID_WATERMARKDD, 0, 0, 0, 0, "Words behind the text on every page" },
  { K_DROP, S_LARGE, IC_PAGECOLOR, L_PAGECOLORS, "Page Color", ID_PAGECOLORDD, 0, 0, 0, 0, "A colour behind every page" },
  { K_DROP, S_LARGE, IC_PAGEBORDER, L_BORDERS, "Page Borders", ID_BORDERDD, 0, 0, 0, 0, "A line round every page" } };
static const Item d_theme[] = {
  { K_DROP, S_LARGE, IC_THEMES, L_THEMES, "Themes", ID_THEMEDD, 0, 0, 0, 0, "A style set, fonts and colours that go together" } };
static const Item d_fmt[] = {
  { K_DROP, S_LARGE, IC_STYLESET, L_STYLESETS, "Style Set", ID_STYLESETDD, 0, 0, 0, 0, "A look for the whole document: fonts, colours and spacing of its styles" },
  { K_DROP, S_SMALL, IC_COLORSETS, L_COLORSETS, "Colors", ID_COLORSETDD, 0, 0, F_NEWCOL, 0, "The headings' and title's colours" },
  { K_DROP, S_SMALL, IC_THEMEFONTS, L_THEMEFONTS, "Fonts", ID_THEMEFONTDD, 0, 0, 0, 0, "Fonts for the text and the headings" },
  { K_DROP, S_SMALL, IC_PSPACING, L_PSPACING, "Paragraph Spacing", ID_PSPACINGDD, 0, 0, 0, 0, "The space between paragraphs and lines, for the whole document" },
  { K_BUTTON, S_SMALL, IC_SETDEFAULT, 0, "Set as Default", ID_SETDEFAULT, 0, 0, F_NEWCOL, 0, "New documents start with this look" } };
static const Group design[] = { { "Themes", G_COLS, d_theme, 1 }, { "Document Formatting", G_COLS, d_fmt, N(d_fmt) }, { "Styles", G_COLS, g_stools, N(g_stools) },
                                { "Page Background", G_COLS, d_bg, N(d_bg) } };

/* the References tab */
static const Item r_toc[] = {
  { K_DROP, S_LARGE, IC_TOC, L_TOCS, "Table of Contents", ID_TOCDD, 0, 0, F_SPLIT, 0, "A table of contents from the headings" },
  { K_DROP, S_SMALL, IC_ADDTEXT, L_ADDTEXT, "Add Text", ID_ADDTEXTDD, 0, 0, F_NEWCOL, 0, "The caret's paragraph in the table of contents, at a level, or left out" },
  { K_BUTTON, S_SMALL, IC_UPDATE, 0, "Update Table", ID_TOCUPDATE, 0, 0, 0, 0, "Bring the table of contents up to date" } };
static const Item r_notes[] = {
  { K_BUTTON, S_LARGE, IC_FOOTNOTE, 0, "Insert Footnote", ID_NOTE, 0, 0, 0, 0, "A footnote at the caret (or the text of the one before it)" },
  { K_BUTTON, S_SMALL, IC_ENDNOTE, 0, "Insert Endnote", ID_ENDNOTE, 0, 0, F_NEWCOL, 0, "An endnote at the caret" },
  { K_BUTTON, S_SMALL, IC_NEXTNOTE, 0, "Next Footnote", ID_NEXTNOTE, 0, 0, 0, 0, "Go to the next footnote" } };
static const Group refs[] = { { "Table of Contents", G_COLS, r_toc, N(r_toc) }, { "Footnotes", G_COLS, r_notes, N(r_notes) } };

/* the Review tab: spelling, comments and tracked changes (several need RB_COMMENTS or
   RB_CHANGES: some comments or changes in the document) */
static const Item v_proof[] = {
  { K_BUTTON, S_LARGE, IC_SPELLING, 0, "Spelling", ID_SPELL, 0, 0, 0, 0, "Check the spelling" },
  { K_BUTTON, S_SMALL, IC_WORDCOUNT, 0, "Word Count", ID_WORDCOUNT, 0, 0, F_NEWCOL, 0, "Words, characters and pages" } };
static const Item v_comm[] = {
  { K_BUTTON, S_LARGE, IC_COMMENT, 0, "New Comment", ID_COMMENT, 0, 0, 0, 0, "A comment on the selection" },
  { K_BUTTON, S_SMALL, IC_DELETE, 0, "Delete", ID_DELCOMMENT, 0, RB_COMMENTS, F_NEWCOL, 0, "Delete the comment at the caret" },
  { K_BUTTON, S_SMALL, IC_PREV, 0, "Previous", ID_PREVCOMMENT, 0, RB_COMMENTS, 0, 0, "The previous comment" },
  { K_BUTTON, S_SMALL, IC_NEXT, 0, "Next", ID_NEXTCOMMENT, 0, RB_COMMENTS, 0, 0, "The next comment" },
  { K_BUTTON, S_LARGE, IC_SHOWCOMMENTS, 0, "Show Comments", ID_SHOWCOMMENTS, RB_SHOWCOMMENTS, 0, 0, 0, "Comments beside the page" } };
static const Item v_track[] = { { K_BUTTON, S_LARGE, IC_TRACK, 0, "Track Changes", ID_TRACK, RB_TRACK, 0, 0, 0, "Keep track of changes" } };
static const Item v_changes[] = {
  { K_DROP, S_LARGE, IC_ACCEPT, L_ACCEPT, "Accept", ID_ACCEPTDD, 0, RB_CHANGES, 0, 0, "Accept changes" },
  { K_DROP, S_LARGE, IC_REJECT, L_REJECT, "Reject", ID_REJECTDD, 0, RB_CHANGES, 0, 0, "Reject changes" },
  { K_BUTTON, S_ICON, IC_PREV, 0, NULL, ID_PREVCHANGE, 0, RB_CHANGES, F_NEWCOL, 0, "The previous change" },
  { K_BUTTON, S_ICON, IC_NEXT, 0, NULL, ID_NEXTCHANGE, 0, RB_CHANGES, 0, 0, "The next change" } };
static const Group review[] = { { "Proofing", G_COLS, v_proof, N(v_proof) }, { "Comments", G_COLS, v_comm, N(v_comm) },
                                { "Tracking", G_COLS, v_track, 1 }, { "Changes", G_COLS, v_changes, N(v_changes) } };

#define VM(m) (100 + (m))           /* an item's st: checked while the view is in that mode */
/* the View tab */
static const Item w_views[] = {
  { K_BUTTON, S_LARGE, IC_READ, 0, "Read Mode", ID_VREAD, VM(VM_READ), 0, 0, 0, "Two pages to read, without the tools (Esc returns)" },
  { K_BUTTON, S_LARGE, IC_ONEPAGE, 0, "Print Layout", ID_VPRINT, VM(VM_PRINT), 0, 0, 0, "The pages as they print" },
  { K_BUTTON, S_SMALL, IC_WEB, 0, "Web Layout", ID_VWEB, VM(VM_WEB), 0, F_NEWCOL, 0, "The text the window's width, without pages" },
  { K_BUTTON, S_SMALL, IC_DRAFT, 0, "Draft", ID_VDRAFT, VM(VM_DRAFT), 0, 0, 0, "Just the text, without headers, footers or margins" } };
static const Item w_show[] = {
  { K_BUTTON, S_SMALL, IC_RULER, 0, "Ruler", ID_RULER, RB_RULER, 0, 0, 0, "Show the rulers" },
  { K_BUTTON, S_SMALL, IC_GRID, 0, "Gridlines", ID_GRID, RB_GRID, 0, 0, 0, "Gridlines over the page, to line things up" },
  { K_BUTTON, S_SMALL, IC_NAVPANE, 0, "Navigation Pane", ID_OUTLINE, RB_NAVPANE, 0, 0, 0, "Show the navigation pane" } };
static const Item w_zoom[] = {
  { K_DROP, S_LARGE, IC_ZOOM, L_ZOOM, "Zoom", ID_ZOOMDD, 0, 0, 0, 0, "Zoom" },
  { K_BUTTON, S_SMALL, IC_ONEPAGE, 0, "One Page", ID_ONEPAGE, 0, 0, F_NEWCOL, 0, "A whole page in the window" },
  { K_BUTTON, S_SMALL, IC_MULTIPAGE, 0, "Multiple Pages", ID_MULTIPAGE, 0, 0, 0, 0, "Two pages side by side" },
  { K_BUTTON, S_SMALL, IC_PAGEWIDTH, 0, "Page Width", ID_PAGEWIDTH, 0, 0, 0, 0, "The page's width fills the window" } };
static const Item w_dark[] = {
  { K_BUTTON, S_LARGE, IC_DARK, 0, "Dark Mode", ID_DARKPAGE, RB_DARKPAGE, 0, 0, 0, "A dark page with light text (the document is not changed)" } };
static const Group view[] = { { "Views", G_COLS, w_views, N(w_views) }, { "Show", G_COLS, w_show, N(w_show) }, { "Zoom", G_COLS, w_zoom, N(w_zoom) },
                              { "Dark Mode", G_COLS, w_dark, 1 } };

/* Table Design */
static const Item td_opts[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Header Row", ID_LK_FIRSTROW, RB_LK_FIRSTROW, RB_INTABLE, F_CHECK, 0, "The style's look for the first row" },
  { K_BUTTON, S_SMALL, 0, 0, "Total Row", ID_LK_LASTROW, RB_LK_LASTROW, RB_INTABLE, F_CHECK, 0, "The style's look for the last row" },
  { K_BUTTON, S_SMALL, 0, 0, "Banded Rows", ID_LK_BANDROWS, RB_LK_BANDROWS, RB_INTABLE, F_CHECK, 0, "Every other row shaded, to read across" },
  { K_BUTTON, S_SMALL, 0, 0, "First Column", ID_LK_FIRSTCOL, RB_LK_FIRSTCOL, RB_INTABLE, F_CHECK | F_NEWCOL, 0, "The style's look for the first column" },
  { K_BUTTON, S_SMALL, 0, 0, "Last Column", ID_LK_LASTCOL, RB_LK_LASTCOL, RB_INTABLE, F_CHECK, 0, "The style's look for the last column" },
  { K_BUTTON, S_SMALL, 0, 0, "Banded Columns", ID_LK_BANDCOLS, RB_LK_BANDCOLS, RB_INTABLE, F_CHECK, 0, "Every other column shaded" } };
static const Item td_styles[] = {
  { K_DROP, S_LARGE, IC_TSTYLES, L_TSTYLES, "Table Styles", ID_TSTYLEDD, 0, RB_INTABLE, 0, 0, "A look for the whole table: lines, fills and the heading row's text" },
  { K_DROP, S_LARGE, IC_SHADING, L_SHADING, "Shading", ID_SHADINGDD, 0, RB_INTABLE, 0, 0, "The selected cells' colour" } };
static const Item td_borders[] = {
  { K_FIELD, S_SMALL, 0, L_PENSTYLE, NULL, ID_PENSTYLE, 0, RB_INTABLE, 0, 96, "The kind of line borders get" },
  { K_FIELD, S_SMALL, 0, L_PENWIDTH, NULL, ID_PENWIDTH, 0, RB_INTABLE, 0, 96, "How thick borders are" },
  { K_DROP, S_SMALL, IC_PENCOLOR, L_PENCOLOR, "Pen Colour", ID_PENCOLOR, 0, RB_INTABLE, 0, 0, "The colour borders get" },
  { K_DROP, S_LARGE, IC_TBORDERS, L_TBORDERS, "Borders", ID_TBORDERSDD, 0, RB_INTABLE, 0, 0, "Lines round and between the selected cells, in the pen chosen" } };
static const Group tdesign[] = { { "Table Style Options", G_COLS, td_opts, N(td_opts) }, { "Table Styles", G_COLS, td_styles, N(td_styles) },
                                 { "Borders", G_COLS, td_borders, N(td_borders) } };
/* Table Layout */
static const Item tl_table[] = {
  { K_DROP, S_SMALL, IC_TSELECT, L_TSELECT, "Select", ID_TSELECTDD, 0, RB_INTABLE, 0, 0, "Select the cell, its column, its row or the table" },
  { K_BUTTON, S_SMALL, IC_TGRID, 0, "View Gridlines", ID_TGRID, RB_TGRID, 0, 0, 0, "Dotted lines where cells have no borders" },
  { K_BUTTON, S_SMALL, IC_TPROPS, 0, "Properties", ID_TPROPS, 0, RB_INTABLE, 0, 0, "The table's alignment and indent, cell margins, rows" } };
static const Item t_rows[] = {
  { K_DROP, S_LARGE, IC_DELROW, L_DELTABLE, "Delete", ID_DELTABLEDD, 0, RB_INTABLE, 0, 0, "Delete rows, columns or the table" },
  { K_BUTTON, S_LARGE, IC_ROWABOVE, 0, "Insert Above", ID_ROW_ABOVE, 0, RB_INTABLE, 0, 0, "Insert a row above" },
  { K_BUTTON, S_LARGE, IC_ROWBELOW, 0, "Insert Below", ID_ROW_BELOW, 0, RB_INTABLE, 0, 0, "Insert a row below" },
  { K_BUTTON, S_SMALL, IC_COLLEFT, 0, "Insert Left", ID_COL_LEFT, 0, RB_INTABLE, F_NEWCOL, 0, "Insert a column to the left" },
  { K_BUTTON, S_SMALL, IC_COLRIGHT, 0, "Insert Right", ID_COL_RIGHT, 0, RB_INTABLE, 0, 0, "Insert a column to the right" } };
static const Item t_merge[] = {
  { K_BUTTON, S_SMALL, IC_MERGE, 0, "Merge Cells", ID_MERGE, 0, RB_INTABLE, 0, 0, "The selected cells (across and down) one cell" },
  { K_BUTTON, S_SMALL, IC_SPLIT, 0, "Split Cells", ID_SPLIT, 0, RB_INTABLE, 0, 0, "The cell split into columns and rows" },
  { K_BUTTON, S_SMALL, IC_SPLITTABLE, 0, "Split Table", ID_SPLITTABLE, 0, RB_INTABLE, 0, 0, "Two tables: the second from the caret's row" } };
static const Item t_size[] = {
  { K_DROP, S_LARGE, IC_AUTOFIT, L_AUTOFIT, "AutoFit", ID_AUTOFITDD, 0, RB_INTABLE, 0, 0, "Columns as wide as their text, or the table as wide as the page" },
  { K_FIELD, S_SMALL, IC_ROWH, L_ROWH, "Height:", ID_ROWH, 0, RB_INTABLE, 0, 62, "The row's height" },
  { K_FIELD, S_SMALL, IC_COLW, L_COLW, "Width:", ID_COLW, 0, RB_INTABLE, 0, 62, "The column's width" },
  { K_BUTTON, S_SMALL, IC_DISTROWS, 0, "Distribute Rows", ID_DISTROWS, 0, RB_INTABLE, F_NEWCOL, 0, "The selected rows the same height" },
  { K_BUTTON, S_SMALL, IC_DISTCOLS, 0, "Distribute Columns", ID_DISTCOLS, 0, RB_INTABLE, 0, 0, "The selected columns the same width" } };
static const Item t_align[] = {
  { K_BUTTON, S_SMALL, IC_ALIGNTOP, 0, "Top", ID_VALIGN0, RB_VALIGN0, RB_INTABLE, 0, 0, "Text at the top of the cell" },
  { K_BUTTON, S_SMALL, IC_ALIGNMID, 0, "Centre", ID_VALIGN1, RB_VALIGN1, RB_INTABLE, 0, 0, "Text in the middle of the cell" },
  { K_BUTTON, S_SMALL, IC_ALIGNBOT, 0, "Bottom", ID_VALIGN2, RB_VALIGN2, RB_INTABLE, 0, 0, "Text at the bottom of the cell" },
  { K_BUTTON, S_SMALL, IC_CELLMARGINS, 0, "Cell Margins", ID_TPROPS, 0, RB_INTABLE, F_NEWCOL, 0, "The space between the cells' edges and their text" } };
static const Item t_data[] = {
  { K_BUTTON, S_SMALL, IC_SORT, 0, "Sort", ID_TSORT, 0, RB_INTABLE, 0, 0, "The rows sorted by a column" },
  { K_BUTTON, S_SMALL, IC_HEADROW, 0, "Repeat Header Rows", ID_HEADERROW, RB_HEADROW, RB_INTABLE, 0, 0, "The row repeats at the top of each page" },
  { K_BUTTON, S_SMALL, IC_TOTEXT, 0, "Convert to Text", ID_TOTEXT, 0, RB_INTABLE, 0, 0, "The table as paragraphs, cells separated by tabs or commas" },
  { K_BUTTON, S_SMALL, IC_FORMULA, 0, "Formula", ID_FORMULA, 0, RB_INTABLE, F_NEWCOL, 0, "A sum or other figure worked out from the cells (=SUM(ABOVE))" } };
static const Group tlayout[] = { { "Table", G_COLS, tl_table, N(tl_table) }, { "Rows & Columns", G_COLS, t_rows, N(t_rows) },
                                 { "Merge", G_COLS, t_merge, N(t_merge) }, { "Cell Size", G_COLS, t_size, N(t_size) },
                                 { "Alignment", G_COLS, t_align, N(t_align) }, { "Data", G_COLS, t_data, N(t_data) } };

/* Picture Format and Shape Format (an object selected) */
static const Item o_pstyle[] = {
  { K_DROP, S_LARGE, IC_SHAPEOUTLINE, L_OBJLINE, "Border", ID_OBJLINEDD, 0, RB_PICSEL, 0, 0, "A line round the picture, and its colour" },
  { K_DROP, S_SMALL, IC_PENWIDTH, L_PENWIDTH, "Weight", ID_OBJWEIGHTDD, 0, RB_PICSEL, F_NEWCOL, 0, "How thick the line round it is" },
  { K_DROP, S_SMALL, IC_PENSTYLE, L_PENSTYLE, "Dashes", ID_OBJDASHDD, 0, RB_PICSEL, 0, 0, "The kind of line round it" } };
static const Item o_sstyle[] = {
  { K_DROP, S_LARGE, IC_SHAPEFILL, L_OBJFILL, "Shape Fill", ID_OBJFILLDD, 0, RB_ONESHAPE, 0, 0, "The colour inside the shape" },
  { K_DROP, S_LARGE, IC_SHAPEOUTLINE, L_OBJLINE, "Shape Outline", ID_OBJLINEDD, 0, RB_ONESHAPE, 0, 0, "The colour of the shape's outline" },
  { K_DROP, S_SMALL, IC_PENWIDTH, L_PENWIDTH, "Weight", ID_OBJWEIGHTDD, 0, RB_ONESHAPE, F_NEWCOL, 0, "How thick the outline is" },
  { K_DROP, S_SMALL, IC_PENSTYLE, L_PENSTYLE, "Dashes", ID_OBJDASHDD, 0, RB_ONESHAPE, 0, 0, "The kind of line the outline is" } };
static const Item o_sinsert[] = {
  { K_DROP, S_LARGE, IC_SHAPES, L_SHAPES, "Shapes", ID_SHAPESDD, 0, 0, 0, 0, "Another shape" },
  { K_BUTTON, S_SMALL, IC_TEXTBOX, 0, "Text Box", ID_TEXTBOX, 0, 0, F_NEWCOL, 0, "A box of text the page's text runs round" },
  { K_BUTTON, S_SMALL, IC_EDITTEXT, 0, "Edit Text", ID_EDITTEXT, 0, RB_ONESHAPE, 0, 0, "The text in the text box or shape" } };
static const Item o_parrange[] = {
  { K_DROP, S_LARGE, IC_POSITION, L_POSITION, "Position", ID_OBJPOSDD, 0, RB_PICSEL, 0, 0, "Where on the page it goes, the text wrapping round it" },
  { K_DROP, S_LARGE, IC_WRAP, L_WRAP, "Wrap Text", ID_OBJWRAPDD, 0, RB_PICSEL, 0, 0, "How the text runs round it" },
  { K_DROP, S_SMALL, IC_ROTATE, L_ROTATE, "Rotate", ID_OBJROTDD, 0, RB_PICSEL, F_NEWCOL, 0, "Turned by any angle (or drag its round handle), or flipped" },
  { K_BUTTON, S_SMALL, IC_LAYOUTOPTS, 0, "Layout Options", ID_OBJLAYOUT, 0, RB_PICSEL, 0, 0, "Its position, wrapping and distance from the text" },
  { K_BUTTON, S_SMALL, IC_GROUP, 0, "Group", ID_GROUP, 0, RB_MULTISEL, 0, 0, "The objects chosen with Shift-click made one" },
  { K_DROP, S_SMALL, IC_FORWARD, L_FORWARD, "Bring Forward", ID_FORWARDDD, 0, RB_FLOATSEL, F_NEWCOL | F_SPLIT, 0, "In front of the object above it" },
  { K_DROP, S_SMALL, IC_BACKWARD, L_BACKWARD, "Send Backward", ID_BACKWARDDD, 0, RB_FLOATSEL, F_SPLIT, 0, "Behind the object below it" },
  { K_DROP, S_SMALL, IC_ALIGNOBJ, L_ALIGNOBJ, "Align", ID_ALIGNOBJDD, 0, RB_FLOATSEL, 0, 0, "Line objects up with the margins, or with each other" },
  { K_BUTTON, S_SMALL, IC_SELPANE, 0, "Selection Pane", ID_SELPANE, 0, 0, F_NEWCOL, 0, "Every floating object, top first: choose one, or change their order" } };
static const Item o_sarrange[] = {
  { K_DROP, S_LARGE, IC_POSITION, L_POSITION, "Position", ID_OBJPOSDD, 0, RB_SHAPESEL, 0, 0, "Where on the page it goes, the text wrapping round it" },
  { K_DROP, S_LARGE, IC_WRAP, L_WRAP, "Wrap Text", ID_OBJWRAPDD, 0, RB_SHAPESEL, 0, 0, "How the text runs round it" },
  { K_DROP, S_SMALL, IC_ROTATE, L_ROTATE, "Rotate", ID_OBJROTDD, 0, RB_SHAPESEL, F_NEWCOL, 0, "Turned by any angle (or drag its round handle), or flipped" },
  { K_BUTTON, S_SMALL, IC_LAYOUTOPTS, 0, "Layout Options", ID_OBJLAYOUT, 0, RB_SHAPESEL, 0, 0, "Its position, wrapping and distance from the text" },
  { K_BUTTON, S_SMALL, IC_GROUP, 0, "Group", ID_GROUP, 0, RB_MULTISEL, F_NEWCOL, 0, "The objects chosen with Shift-click made one" },
  { K_BUTTON, S_SMALL, IC_UNGROUP, 0, "Ungroup", ID_UNGROUP, 0, RB_GROUPSEL, 0, 0, "The group taken apart into its objects" },
  { K_DROP, S_SMALL, IC_FORWARD, L_FORWARD, "Bring Forward", ID_FORWARDDD, 0, RB_FLOATSEL, F_NEWCOL | F_SPLIT, 0, "In front of the object above it" },
  { K_DROP, S_SMALL, IC_BACKWARD, L_BACKWARD, "Send Backward", ID_BACKWARDDD, 0, RB_FLOATSEL, F_SPLIT, 0, "Behind the object below it" },
  { K_DROP, S_SMALL, IC_ALIGNOBJ, L_ALIGNOBJ, "Align", ID_ALIGNOBJDD, 0, RB_FLOATSEL, 0, 0, "Line objects up with the margins, or with each other" },
  { K_BUTTON, S_SMALL, IC_SELPANE, 0, "Selection Pane", ID_SELPANE, 0, 0, F_NEWCOL, 0, "Every floating object, top first: choose one, or change their order" } };
static const Item o_psize[] = {
  { K_BUTTON, S_LARGE, IC_CROP, 0, "Crop", ID_OBJCROP, 0, RB_PICSEL, 0, 0, "Cut away the picture's edges" },
  { K_BUTTON, S_SMALL, IC_PICSIZE, 0, "Size", ID_OBJSIZE, 0, RB_PICSEL, F_NEWCOL, 0, "Its height and width" } };
static const Item o_ssize[] = { { K_BUTTON, S_LARGE, IC_PICSIZE, 0, "Size", ID_OBJSIZE, 0, RB_SHAPESEL, 0, 0, "Its height and width" } };
static const Item o_palt[] = { { K_BUTTON, S_LARGE, IC_ALTTEXT, 0, "Alt Text", ID_ALTTEXT, 0, RB_PICSEL, 0, 0, "A description for people who can't see it" } };
static const Item o_salt[] = { { K_BUTTON, S_LARGE, IC_ALTTEXT, 0, "Alt Text", ID_ALTTEXT, 0, RB_SHAPESEL, 0, 0, "A description for people who can't see it" } };
static const Group pformat[] = { { "Picture Styles", G_COLS, o_pstyle, N(o_pstyle) }, { "Arrange", G_COLS, o_parrange, N(o_parrange) },
                                 { "Size", G_COLS, o_psize, N(o_psize) }, { "Accessibility", G_COLS, o_palt, 1 } };
static const Group sformat[] = { { "Insert Shapes", G_COLS, o_sinsert, N(o_sinsert) }, { "Shape Styles", G_COLS, o_sstyle, N(o_sstyle) },
                                 { "Arrange", G_COLS, o_sarrange, N(o_sarrange) }, { "Size", G_COLS, o_ssize, 1 }, { "Accessibility", G_COLS, o_salt, 1 } };

/* Header & Footer (a header or footer edited), and Edit Text (a note's, comment's or text box's text) */
static const Item h_hf[] = {
  { K_BUTTON, S_LARGE, IC_HEADER, 0, "Header", ID_HFHEADER, 0, 0, 0, 0, "Edit the page's header" },
  { K_BUTTON, S_LARGE, IC_FOOTER, 0, "Footer", ID_HFFOOTER, 0, 0, 0, 0, "Edit the page's footer" } };
static const Item h_ins[] = {
  { K_BUTTON, S_LARGE, IC_PAGENUM, 0, "Page Number", ID_HFPAGE, 0, 0, 0, 0, "The page's number at the caret" },
  { K_BUTTON, S_SMALL, IC_PAGECOUNT, 0, "Number of Pages", ID_HFPAGES, 0, 0, F_NEWCOL, 0, "How many pages the document has, at the caret" },
  { K_BUTTON, S_SMALL, IC_DATE, 0, "Date", ID_DATE, 0, 0, 0, 0, "Today's date" },
  { K_BUTTON, S_SMALL, IC_PICTURE, 0, "Picture", ID_PICTURE, 0, 0, 0, 0, "A PNG or JPEG picture from a file" } };
static const Item h_nav[] = {
  { K_BUTTON, S_SMALL, IC_PREV, 0, "Previous", ID_HFPREV, 0, 0, 0, 0, "The section before's header or footer" },
  { K_BUTTON, S_SMALL, IC_NEXT, 0, "Next", ID_HFNEXT, 0, 0, 0, 0, "The next section's header or footer" } };
static const Item h_opt[] = {
  { K_BUTTON, S_SMALL, 0, 0, "Different First Page", ID_HFFIRST, RB_FIRSTPG, 0, F_CHECK, 0, "The section's first page has a header and footer of its own" },
  { K_BUTTON, S_SMALL, 0, 0, "Different Odd & Even Pages", ID_HFEVEN, RB_EVENODD, 0, F_CHECK, 0, "Even pages have a header and footer of their own" },
  { K_BUTTON, S_SMALL, 0, 0, "This Section's Own", ID_HFOWN, RB_OWNHF, RB_SECTHF, F_CHECK, 0, "The section has headers and footers of its own (else the document's)" } };
static const Item h_pos[] = { { K_BUTTON, S_LARGE, IC_MARGINS, 0, "Page Setup", ID_PAGESETUPDLG, 0, 0, 0, 0, "The section's page: size, margins, the header's and footer's distance from the edge" } };
static const Item h_close[] = {
  { K_BUTTON, S_LARGE, IC_CLOSEHF, 0, "Close", ID_STORYCLOSE, 0, 0, 0, 0, "Back to the document's text (Esc)" },
  { K_BUTTON, S_SMALL, IC_DELETE, 0, "Discard Changes", ID_STORYDISCARD, 0, 0, F_NEWCOL, 0, "Back to the text, leaving this as it was" } };
static const Group hfedit[] = { { "Header & Footer", G_COLS, h_hf, N(h_hf) }, { "Insert", G_COLS, h_ins, N(h_ins) },
                                { "Navigation", G_COLS, h_nav, N(h_nav) }, { "Options", G_COLS, h_opt, N(h_opt) },
                                { "Position", G_COLS, h_pos, 1 }, { "Close", G_COLS, h_close, N(h_close) } };
static const Item e_ins[] = {
  { K_BUTTON, S_LARGE, IC_PICTURE, 0, "Picture", ID_PICTURE, 0, 0, 0, 0, "A PNG or JPEG picture from a file" },
  { K_BUTTON, S_SMALL, IC_DATE, 0, "Date", ID_DATE, 0, 0, F_NEWCOL, 0, "Today's date" },
  { K_DROP, S_SMALL, IC_SYMBOL, L_SYMBOLS, "Symbol", ID_SYMBOLDD, 0, 0, 0, 0, "A symbol" } };
static const Group stedit[] = { { "Insert", G_COLS, e_ins, N(e_ins) }, { "Close", G_COLS, h_close, N(h_close) } };

/* the tab strip, left to right, in TAB_ order (ribbondef.h). File isn't a tab of tools: a click on it
   opens the File page (ID_FILETAB). A tab's index here is MUIA_Ribbon_Tab's value, and events()
   takes 1 to be Home. Draw, Mailings and Developer have no groups of their own: the work packages
   give them theirs (wphooks.h), and a tab with no groups stays out of the strip. Equation, Chart
   Design, SmartArt Design and Outlining show while their package says so (its WPS_TAB bit) */
static const Tab basetabs[] = {
  { "File", NULL, 0, 0 }, { "Home", home, N(home), 0 }, { "Insert", insert, N(insert), 0 }, { "Draw", NULL, 0, 0 },
  { "Design", design, N(design), 0 }, { "Layout", layout, N(layout), 0 }, { "References", refs, N(refs), 0 }, { "Mailings", NULL, 0, 0 },
  { "Review", review, N(review), 0 }, { "View", view, N(view), 0 }, { "Developer", NULL, 0, 0 },
  { "Table Design", tdesign, N(tdesign), 1 }, { "Table Layout", tlayout, N(tlayout), 1 },
  { "Picture Format", pformat, N(pformat), 2 }, { "Shape Format", sformat, N(sformat), 3 },
  { "Header & Footer", hfedit, N(hfedit), 4 }, { "Edit Text", stedit, N(stedit), 5 },
  { "Equation", NULL, 0, RB_WP(WPI_14B, WPS_TAB) }, { "Chart Design", NULL, 0, RB_WP(WPI_15B, WPS_TAB) },
  { "SmartArt Design", NULL, 0, RB_WP(WPI_15C, WPS_TAB) }, { "Outlining", NULL, 0, RB_WP(WPI_11, WPS_TAB) },
};
#define NTABS TAB_N
/* the tabs as shown: basetabs with the packages' groups added (build_tabs, when the class is made) */
static Tab tabs[NTABS];
#define MAXTABGROUPS 400
static Group tabgroups[MAXTABGROUPS];
/* each tab's groups: its own, with every package's WpGroups for it put in before the group they name
   (or after the last), in wphooks.c's package order. Called once, from ribbon_class() */
static void build_tabs(void)
{
  int t, used = 0;
  for (t = 0; t < NTABS; t++) {
    const Tab *b = &basetabs[t];
    int g, start = used, p, k;
    tabs[t] = *b;
    for (g = 0; g <= b->n; g++) {
      for (p = 0; p < WP_NPKG; p++) {
        const WpPackage *pk = wp_package(p);
        for (k = 0; pk && k < pk->ngroups; k++) {
          const WpGroup *wg = &pk->groups[k];
          int at = wg->before < 0 || wg->before > b->n ? b->n : wg->before;
          if (wg->tab == t && at == g && used < MAXTABGROUPS) tabgroups[used++] = wg->g;
        }
      }
      if (g < b->n && used < MAXTABGROUPS) tabgroups[used++] = b->groups[g];
    }
    if (used > start) { tabs[t].groups = &tabgroups[start]; tabs[t].n = used - start; }
  }
}

/* ------------------------------------------------------------------ */
/* the instance */
#define MAXI 56
#define MAXGAL 24
typedef struct { WORD x0, y0, x1, y1; } RBox;
struct RData {
  int shown;                     /* between MUIM_Show and MUIM_Hide: hidden objects keep their event handlers */
  int tab;                       /* shown */
  struct MUIP_Ribbon_State st;
  char font[48];
  RBox tabbox[NTABS];
  LONG tabpad, tabcap;           /* the room round each tab's name (16, less when crowded); a cut-short name's width (0: none) */
  RBox qa[NQA];                  /* the quick access Save, Undo and Redo buttons in the tab strip (hot: 200 up) */
  RBox item[MAXI]; int nitem;     /* the shown tab's items, in order */
  RBox group[12]; int ngroup;
  RBox gal; int galfirst, galn, galw;          /* the styles gallery: its box, first style shown, how many fit */
  int galstyle[MAXGAL]; int ngal;             /* the styles it offers (Doc.styles indexes) */
  RBox galup, galdown, galmore;
  int galcompact;                /* no room for the gallery: a Styles button with the list instead */
  int hot, pressed;              /* item under the pointer / pressed (-1 none); tabs are 100 + n */
  LONG value;
  LONG width;
  struct MUI_EventHandlerNode ehn;
  Object *popwin, *poplist;
  int popitem;                   /* the item whose list is open */
  struct TextFont *galfont[MAXGAL];
  int penstyle, penwidth, pencolor;   /* the borders' pen: list entries chosen */
  int tsn; int tsval[64]; const TableStyle *tsp[64];   /* the table styles list: each entry's value and style */
};
static struct MUI_CustomClass *rcls, *pcls;

/* the measures, from the interface font's height: the font, a tab, a row of small buttons, the
   body (three rows) and the group captions under it, all in pixels */
static int fh(void) { return theme_font()->tf_YSize; }
static int tabh(void) { return fh() + 10; }
static int rowh(void) { return fh() + 7; }
static int bodyh(void) { return 3 * rowh() + 4; }
static int caph(void) { return fh() + 3; }

/* whether state bit b (RB_) is on: 0-31 in st.on, 32 and up in st.on2 */
/* (RB_WP_BASE and up: a work package's own, in st.wpon: a word for each package) */
static int bit_on(struct RData *d, int b)
{
  if (b >= RB_WP_BASE) { b -= RB_WP_BASE; return b / 32 < WP_NPKG && (d->st.wpon[b / 32] & RBIT(b % 32)) != 0; }
  return b >= 32 ? (d->st.on2 & RBIT(b - 32)) != 0 : (d->st.on & RBIT(b)) != 0;
}
/* whether an item shows checked: its state bit, or for VM() the view mode */
static int item_on(struct RData *d, const Item *it) { return it->st >= 100 && it->st < RB_WP_BASE ? d->st.vmode == it->st - 100 : it->st && bit_on(d, it->st); }
/* whether an item can be used: it needs no state, or its state is on (else it is greyed out) */
static int item_enabled(struct RData *d, const Item *it) { return !it->en || bit_on(d, it->en); }
/* whether tab t is in the strip: contextual tabs only while what they work on is there */
static int tab_visible(struct RData *d, int t)
{
  static const u8 needs[6] = { 0, RB_INTABLE, RB_PICSEL, RB_SHAPESEL, RB_HFEDIT, RB_STORYEDIT };
  UWORD c = tabs[t].contextual;
  if (t != TAB_FILE && !tabs[t].n) return 0;          /* a package tab nobody has filled yet */
  return !c || bit_on(d, c >= RB_WP_BASE ? c : needs[c < 6 ? c : 0]);
}

/* too little room for the tab (a narrow window, a language with long words): small buttons show
   their icons alone and group names are cut short */
static int squeeze;
/* whether an item shows as its icon alone (squeezed: small buttons and drop-downs with icons) */
static int bare(const Item *it) { return squeeze && it->size == S_SMALL && (it->icon || it->xicon) && (it->kind == K_BUTTON || it->kind == K_DROP); }
/* an item's icon at x, y in a size-pixel square: its IC_, or a package's own drawing (Item.xicon) */
static void item_icon(struct RastPort *rp, const Item *it, LONG x, LONG y, LONG size, int dim)
{
  if (it->icon) theme_icon(rp, it->icon, x, y, size, dim);
  else if (it->xicon) theme_icon_src(rp, it->xicon, x, y, size, dim);
}
/* the list an item opens: a package's own (Item.xlist) or one of lists[] by number */
static const List *item_list(const Item *it);
/* a large button's label on two lines when squeezed: the space where it breaks (0: one line), and
   the wider line's width */
static int split_at(struct RastPort *rp, const char *s, LONG *w)
{
  int n = (int)strlen(s), best = 0, i;
  LONG bw;
  SetFont(rp, theme_font());
  bw = TextLength(rp, (STRPTR)s, (ULONG)n);
  for (i = 1; i < n - 1; i++)
    if (s[i] == ' ') {
      LONG a = TextLength(rp, (STRPTR)s, (ULONG)i), b = TextLength(rp, (STRPTR)s + i + 1, (ULONG)(n - i - 1));
      if ((a > b ? a : b) < bw) { bw = a > b ? a : b; best = i; }
    }
  *w = bw;
  return best;
}
/* the pixels an item needs across, at the current squeeze; its height comes from its size */
static LONG item_w(struct RastPort *rp, const Item *it)
{
  LONG lw = it->label ? theme_textlen(rp, theme_font(), it->label) : 0;
  if (bare(it)) return 22 + (it->kind == K_DROP ? 9 : 0);
  if (squeeze && it->size == S_LARGE && it->label) split_at(rp, tr(it->label), &lw);
  if (it->kind == K_LABEL) return lw + 6;
  if (it->kind == K_FIELD) return it->fieldw + (it->label ? lw + 4 : 0);
  if (it->size == S_LARGE) return (lw > 30 ? lw : 30) + 10 + (it->kind == K_DROP ? 0 : 0);
  if (it->size == S_ICON) return (squeeze ? 20 : 22) + (it->kind == K_DROP ? 9 : 0);     /* (two pixels less when squeezed) */
  return 16 + 6 + lw + 8 + (it->kind == K_DROP ? 9 : 0);
}
/* the styles the gallery offers: the usual ones first */
/* the styles the gallery offers, as Doc.styles indexes in d->galstyle: the usual ones first (those
   the document has), then the others marked for the gallery (Style.quick), tables of contents' left out */
static void gallery_styles(struct RData *d)
{
  static const char *const first[] = { "Normal", "No Spacing", "Heading 1", "Title", "Heading 2", "Subtitle", "Heading 3", "Quote", "List Paragraph" };
  Doc *doc = d->st.doc;
  int i, k;
  d->ngal = 0;
  if (!doc) return;
  for (k = 0; k < N(first) && d->ngal < MAXGAL; k++)
    for (i = 0; i < doc->nstyles; i++)
      if (doc->styles[i].type == ST_PARA && str_ieq(doc->styles[i].name, first[k])) { d->galstyle[d->ngal++] = i; break; }
  for (i = 0; i < doc->nstyles && d->ngal < MAXGAL; i++) {
    int j, dup = 0;
    /* the styles marked for the gallery (character styles too) */
    if ((doc->styles[i].type != ST_PARA && doc->styles[i].type != ST_CHAR) || !doc->styles[i].quick) continue;
    for (j = 0; j < d->ngal; j++) if (d->galstyle[j] == i) dup = 1;
    if (!dup && strncmp(doc->styles[i].name, "toc ", 4) && strncmp(doc->styles[i].name, "TOC", 3)) d->galstyle[d->ngal++] = i;
  }
}
/* each group's natural width (the gallery takes what is left); their total */
/* each group's natural width (the gallery takes what is left); their total. G_COLS piles small
   items three to a column, a large item taking a column of its own; a group is at least as wide
   as its caption unless squeezed */
static LONG group_widths(struct RastPort *rp, const Tab *tab, LONG *gw)
{
  LONG total = 0;
  int g, i;
  for (g = 0; g < tab->n && g < 12; g++) {
    const Group *gr = &tab->groups[g];
    LONG w = 0, cap = theme_textlen(rp, theme_font(), gr->name) + 12;
    if (gr->mode == G_ROWS) {
      LONG r0 = 0, r1 = 0;
      for (i = 0; i < gr->n; i++) { if (gr->items[i].flags & F_ROW1) r1 += item_w(rp, &gr->items[i]) + 1; else r0 += item_w(rp, &gr->items[i]) + 1; }
      w = (r0 > r1 ? r0 : r1) + 8;
    } else if (gr->mode == G_COLS) {
      LONG colw = 0;
      int row = 0;
      for (i = 0; i < gr->n; i++) {
        const Item *it = &gr->items[i];
        if (it->size == S_LARGE) { if (colw) { w += colw; colw = 0; row = 0; } w += item_w(rp, it) + 2; continue; }
        if (row == 3 || (it->flags & F_NEWCOL)) { w += colw + 2; colw = 0; row = 0; }
        if (item_w(rp, it) > colw) colw = item_w(rp, it);
        row++;
      }
      w += colw + 8;
    } else w = 0;
    if (w < cap && !squeeze) w = cap;
    gw[g] = w; total += w;
  }
  return total;
}
/* lay out the shown tab: boxes of tabs, groups and items */
/* works out every box of the shown tab in window pixels: the tabs, the quick access buttons, then
   the groups left to right with their items numbered in table order (nth_item counts the same
   way). If the groups don't fit it lays out again squeezed; the gallery takes the room left over,
   or turns into a single Styles button when there isn't enough. Done on every draw, so it follows
   the window's width and the state (contextual tabs come and go). Called only while the object
   has a RastPort (between MUIM_Setup and MUIM_Cleanup) */
static void layout_ribbon(struct RData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG x = _mleft(obj), y0 = _mtop(obj) + tabh() + 2, total = 0, extra;
  int t, g, i, n = 0;
  const Tab *tab;
  LONG gw[12];
  d->width = _mwidth(obj);
  /* the tab strip. Each tab has 16 pixels round its name; when that doesn't fit the window (more tabs
     from the work packages, a language with long words) the room round the names shrinks to 6, then
     the longest names are cut short (tabcap, drawn ending in a dot) */
  {
    LONG sum = 0, avail = _mwidth(obj) - 4, cap = 0, lo, hi;
    int nv = 0;
    for (t = 0; t < NTABS; t++) if (tab_visible(d, t)) { sum += theme_textlen(rp, theme_font(), tabs[t].name); nv++; }
    d->tabpad = 16;
    if (nv && sum + nv * 16 > avail) {
      d->tabpad = (avail - sum) / nv;
      if (d->tabpad < 6) {
        d->tabpad = 6;
        /* the widest name that lets the strip fit */
        lo = 8; hi = 400;
        while (lo < hi) {
          LONG mid = (lo + hi + 1) / 2, tot = 0;
          for (t = 0; t < NTABS; t++) if (tab_visible(d, t)) { LONG w = theme_textlen(rp, theme_font(), tabs[t].name); tot += (w < mid ? w : mid) + 6; }
          if (tot <= avail) lo = mid; else hi = mid - 1;
        }
        cap = lo;
      }
    }
    d->tabcap = cap;
  }
  for (t = 0; t < NTABS; t++) {
    LONG w = theme_textlen(rp, theme_font(), tabs[t].name);
    if (d->tabcap && w > d->tabcap) w = d->tabcap;
    w += d->tabpad;
    if (!tab_visible(d, t)) { d->tabbox[t].x0 = d->tabbox[t].x1 = -1; continue; }
    d->tabbox[t].x0 = (WORD)x; d->tabbox[t].x1 = (WORD)(x + w - 1);
    d->tabbox[t].y0 = (WORD)_mtop(obj); d->tabbox[t].y1 = (WORD)(_mtop(obj) + tabh() - 1);
    x += w;
  }
  /* Save, Undo and Redo at the strip's right end (left out if the tabs reach that far) */
  for (t = 0; t < NQA; t++) {
    LONG qx1 = _mright(obj) - 4 - (NQA - 1 - t) * 24;
    if (qx1 - 21 <= x) { d->qa[t].x0 = d->qa[t].x1 = -1; continue; }
    d->qa[t].x0 = (WORD)(qx1 - 21); d->qa[t].x1 = (WORD)qx1;
    d->qa[t].y0 = (WORD)(_mtop(obj) + 1); d->qa[t].y1 = (WORD)(_mtop(obj) + tabh() - 1);
  }
  if (!tab_visible(d, d->tab)) {
    /* its tab gone: from one object's tab to the other's (a picture grouped into a shape), else Home */
    int c = tabs[d->tab].contextual, n = 1;
    if (c == 2 || c == 3) for (t = 0; t < NTABS; t++) if ((tabs[t].contextual == 2 || tabs[t].contextual == 3) && tab_visible(d, t)) n = t;
    d->tab = n;
  }
  tab = &tabs[d->tab];
  d->nitem = 0; d->ngroup = 0;
  if (!tab->groups) return;
  /* each group's natural width; too wide for the window, squeezed */
  squeeze = 0;
  total = group_widths(rp, tab, gw);
  {
    LONG need = total;                       /* the gallery can shrink to a button */
    for (g = 0; g < tab->n && g < 12; g++) if (tab->groups[g].mode == G_GALLERY) need += 56 - gw[g];
    if (need > _mwidth(obj) + 8) { squeeze = 1; total = group_widths(rp, tab, gw); }   /* + 8: the last group's padding can go */
  }
  extra = _mwidth(obj) - total;
  x = _mleft(obj) + 2;
  for (g = 0; g < tab->n && g < 12; g++) {
    const Group *gr = &tab->groups[g];
    LONG w = gw[g], cx, top = y0 + 2;
    if (gr->mode == G_GALLERY) {
      LONG bw = 66;
      w = gw[g] + extra - 4;
      if (w > 7 * bw + 30) w = 7 * bw + 30;
      d->galcompact = w < 2 * bw + 30;
      if (d->galcompact) {
        w = 56;
        d->gal.x0 = d->gal.x1 = d->galup.x0 = d->galup.x1 = d->galdown.x0 = d->galdown.x1 = d->galmore.x0 = d->galmore.x1 = -1;
        d->galn = 0;
        d->item[n].x0 = (WORD)(x + 3); d->item[n].x1 = (WORD)(x + w - 4); d->item[n].y0 = (WORD)top; d->item[n].y1 = (WORD)(top + 3 * rowh() - 1);
        n++;
        extra -= w - gw[g];
        goto placed;
      }
      extra -= w - gw[g];
      d->gal.x0 = (WORD)(x + 4); d->gal.y0 = (WORD)top; d->gal.x1 = (WORD)(x + w - 18); d->gal.y1 = (WORD)(top + 3 * rowh() - 1);
      d->galw = (int)bw;
      d->galn = (int)((d->gal.x1 - d->gal.x0 + 1) / bw);
      if (d->galn < 1) d->galn = 1;
      d->galup.x0 = d->galdown.x0 = d->galmore.x0 = (WORD)(d->gal.x1 + 2);
      d->galup.x1 = d->galdown.x1 = d->galmore.x1 = (WORD)(x + w - 4);
      d->galup.y0 = (WORD)top; d->galup.y1 = (WORD)(top + rowh() - 1);
      d->galdown.y0 = (WORD)(top + rowh()); d->galdown.y1 = (WORD)(top + 2 * rowh() - 1);
      d->galmore.y0 = (WORD)(top + 2 * rowh()); d->galmore.y1 = (WORD)(top + 3 * rowh() - 1);
      d->item[n].x0 = d->gal.x0; d->item[n].y0 = d->gal.y0; d->item[n].x1 = d->gal.x1; d->item[n].y1 = d->gal.y1;
      n++;
    } else if (gr->mode == G_ROWS) {
      LONG rx[2];
      rx[0] = rx[1] = x + 4;
      for (i = 0; i < gr->n && n < MAXI; i++) {
        const Item *it = &gr->items[i];
        int r = (it->flags & F_ROW1) ? 1 : 0;
        LONG iw = item_w(rp, it), iy = top + 4 + r * (rowh() + 6);
        d->item[n].x0 = (WORD)rx[r]; d->item[n].x1 = (WORD)(rx[r] + iw - 1); d->item[n].y0 = (WORD)iy; d->item[n].y1 = (WORD)(iy + rowh() - 1);
        rx[r] += iw + 1; n++;
      }
    } else {
      LONG colw = 0;
      int row = 0, colstart = n;
      cx = x + 4;
      for (i = 0; i < gr->n && n < MAXI; i++) {
        const Item *it = &gr->items[i];
        LONG iw = item_w(rp, it);
        if (it->size == S_LARGE) {
          int k;
          if (colw) { for (k = colstart; k < n; k++) d->item[k].x1 = (WORD)(d->item[k].x0 + colw - 1); cx += colw + 2; colw = 0; row = 0; }
          d->item[n].x0 = (WORD)cx; d->item[n].x1 = (WORD)(cx + iw - 1); d->item[n].y0 = (WORD)top; d->item[n].y1 = (WORD)(top + 3 * rowh() - 1);
          cx += iw + 2; n++; colstart = n;
          continue;
        }
        if (row == 3 || (it->flags & F_NEWCOL)) {
          int k;
          for (k = colstart; k < n; k++) d->item[k].x1 = (WORD)(d->item[k].x0 + colw - 1);
          cx += colw + 2; colw = 0; row = 0; colstart = n;
        }
        d->item[n].x0 = (WORD)cx; d->item[n].x1 = (WORD)(cx + iw - 1);
        d->item[n].y0 = (WORD)(top + row * rowh()); d->item[n].y1 = (WORD)(top + (row + 1) * rowh() - 1);
        if (iw > colw) colw = iw;
        row++; n++;
      }
      { int k; for (k = colstart; k < n; k++) if (d->item[k].x1 < d->item[k].x0 + colw - 1 && tab->groups[g].items[gr->n - (n - k)].kind != K_FIELD) d->item[k].x1 = (WORD)(d->item[k].x0 + colw - 1); }
    }
  placed:
    d->group[d->ngroup].x0 = (WORD)x; d->group[d->ngroup].x1 = (WORD)(x + w - 1);
    d->group[d->ngroup].y0 = (WORD)y0; d->group[d->ngroup].y1 = (WORD)(y0 + bodyh() + caph());
    d->ngroup++;
    x += w;
  }
  d->nitem = n;
}
/* the tab's items in order (gallery included), as layout_ribbon numbered them */
/* item k of the shown tab and its group's number (in *group, if given), or NULL */
static const Item *nth_item(struct RData *d, int k, int *group)
{
  const Tab *tab = &tabs[d->tab];
  int g, i, n = 0;
  if (!tab->groups) return NULL;
  for (g = 0; g < tab->n; g++)
    for (i = 0; i < tab->groups[g].n; i++) { if (n == k) { if (group) *group = g; return &tab->groups[g].items[i]; } n++; }
  return NULL;
}

/* ------------------------------------------------------------------ */
/* drawing */
/* the text a field shows for the current state: the font, the size in points, indents in cm or
   inches (from twips), spacing in points, the borders' pen, the row height and column width */
static void field_text(struct RData *d, const Item *it, char *out, size_t cap)
{
  i32 v = 0;
  out[0] = 0;
  switch (it->id) {
  case ID_FONTDD: utf8_to_latin1(d->st.font ? d->st.font : "", d->st.font ? (u32)strlen(d->st.font) : 0, out, cap); break;
  case ID_SIZEDD: sprintf(out, "%d%s", d->st.size / 2, d->st.size & 1 ? ".5" : ""); break;
  case ID_INDENTL: case ID_INDENTR:
    v = it->id == ID_INDENTL ? d->st.left : d->st.right;
    if (d->st.metric) sprintf(out, "%ld.%ld cm", (long)(v * 254 / 144000), (long)(v * 254 / 14400 % 10));
    else sprintf(out, "%ld.%02ld\"", (long)(v / 1440), (long)(v % 1440 * 100 / 1440));
    break;
  case ID_SPACEB: case ID_SPACEA:
    v = it->id == ID_SPACEB ? d->st.before : d->st.after;
    sprintf(out, "%ld pt", (long)(v / 20));
    break;
  case ID_PENSTYLE: strncpy(out, tr(penstyles[d->penstyle % N(penstyles)]), cap - 1); out[cap - 1] = 0; break;
  case ID_PENWIDTH: strncpy(out, penwidths[d->penwidth % N(penwidths)], cap - 1); out[cap - 1] = 0; break;
  case ID_ROWH: case ID_COLW:
    v = it->id == ID_ROWH ? d->st.rowh : d->st.colw;
    if (it->id == ID_ROWH && !v) { strncpy(out, tr("Auto"), cap - 1); out[cap - 1] = 0; break; }
    if (v < 0) v = -v;
    if (d->st.metric) sprintf(out, "%ld.%02ld cm", (long)(v * 254 / 144000), (long)(v * 254 / 1440 % 100));
    else sprintf(out, "%ld.%02ld\"", (long)(v / 1440), (long)(v % 1440 * 100 / 1440));
    break;
  default:
    if (it->id >= ID_WP_FIRST && it->id <= ID_WP_LAST) wp_field_text(it->id, &d->st, out, cap);     /* a package's field */
    break;
  }
}
/* draws item k (its box from layout_ribbon) as it is now: hovered, pressed, checked, greyed.
   Called from draw_all and on its own for hover and press changes, so it paints its whole box */
static void draw_item(struct RData *d, Object *obj, int k)
{
  struct RastPort *rp = _rp(obj);
  int g;
  const Item *it = nth_item(d, k, &g);
  RBox *b = &d->item[k];
  int en, on, hot, pr;
  LONG bg;
  if (!it || k >= d->nitem) return;
  en = item_enabled(d, it); on = item_on(d, it); hot = d->hot == k && en; pr = d->pressed == k && en;
  if (it->kind == K_GALLERY && d->galcompact) {
    /* a button: a sample of the paragraph's style, "Styles" under it */
    LONG lw = theme_textlen(rp, theme_font(), "Styles"), sw;
    theme_fill(rp, b->x0, b->y0, b->x1, b->y1, pr ? tpen(T_PRESSED) : hot ? tpen(T_HOVER) : tpen(T_RIBBON));
    SetFont(rp, theme_big());
    sw = theme_textlen(rp, theme_big(), "Aa");
    theme_text(rp, theme_big(), b->x0 + (b->x1 - b->x0 + 1 - sw) / 2, b->y0 + 1, "Aa", tpen(T_ACCENT));
    theme_text(rp, theme_font(), b->x0 + (b->x1 - b->x0 + 1 - lw) / 2, b->y0 + 31, "Styles", tpen(T_TEXT));
    theme_icon(rp, IC_ARROWDOWN, b->x0 + (b->x1 - b->x0 + 1 - 8) / 2, b->y0 + 31 + fh(), 8, 0);
    return;
  }
  if (it->kind == K_GALLERY) {
    /* the styles: each a box with a sample and its name */
    Doc *doc = d->st.doc;
    int i;
    theme_fill(rp, b->x0, b->y0, b->x1, b->y1, tpen(T_RIBBON));
    for (i = 0; i < d->galn && d->galfirst + i < d->ngal && doc; i++) {
      int si = d->galstyle[d->galfirst + i];
      const Style *s = &doc->styles[si];
      LONG bx = b->x0 + i * d->galw, by = b->y0, ex = bx + d->galw - 3, ey = b->y1;
      struct TextFont *tf = d->galfont[d->galfirst + i] ? d->galfont[d->galfirst + i] : theme_font();
      char name[24];
      ULONG col = s->cf.color;
      LONG pen = tpen(T_TEXT);
      if (si == d->st.style) { theme_fill(rp, bx, by, ex, ey, tpen(T_CHECKED)); theme_rect(rp, bx, by, ex, ey, tpen(T_ACCENT)); }
      else theme_rect(rp, bx, by, ex, ey, tpen(T_BORDER));
      if (col != COLOR_AUTO && col) pen = (col & 0xFF) > 0x80 && ((col >> 16) & 0xFF) < 0x80 ? tpen(T_ACCENT) : (col >> 16) > 0xA0 ? tpen(T_RED) : tpen(T_TEXT);
      SetFont(rp, tf);
      SetSoftStyle(rp, (s->cf.flags & CF_BOLD) && !strstr(tf->tf_Message.mn_Node.ln_Name ? (const char *)FilePart((STRPTR)tf->tf_Message.mn_Node.ln_Name) : "", "B") ? FSF_BOLD : 0, FSF_BOLD);
      SetAPen(rp, pen);
      {
        LONG ty = by + (ey - by - caph() - tf->tf_YSize) / 2 + tf->tf_Baseline;
        struct TextExtent te;
        ULONG fit = TextFit(rp, (STRPTR)"AaBbCcDd", 8, &te, NULL, 1, (ULONG)(d->galw - 8), 100);
        Move(rp, bx + 4, ty);
        Text(rp, (STRPTR)"AaBbCcDd", fit);
      }
      SetSoftStyle(rp, 0, FSF_BOLD);
      utf8_to_latin1(s->name, (u32)strlen(s->name), name, sizeof name);
      if (name[0] >= 'a' && name[0] <= 'z') name[0] -= 32;
      {
        struct TextExtent te;
        ULONG fit;
        SetFont(rp, theme_font());
        fit = TextFit(rp, (STRPTR)name, (ULONG)strlen(name), &te, NULL, 1, (ULONG)(d->galw - 6), 100);
        theme_text(rp, theme_font(), bx + (d->galw - 3 - te.te_Width) / 2, ey - caph() + 1, "", tpen(T_TEXT));
        Move(rp, bx + (d->galw - 3 - te.te_Width) / 2, ey - caph() + 1 + theme_font()->tf_Baseline);
        SetAPen(rp, tpen(T_TEXT));
        Text(rp, (STRPTR)name, fit);
      }
    }
    /* the scroll buttons and the whole list */
    theme_fill(rp, d->galup.x0, d->galup.y0, d->galmore.x1, d->galmore.y1, tpen(T_RIBBON));
    theme_rect(rp, d->galup.x0, d->galup.y0, d->galmore.x1, d->galmore.y1, tpen(T_BORDER));
    theme_icon(rp, IC_ARROWUP, d->galup.x0 + (d->galup.x1 - d->galup.x0 - 12) / 2, d->galup.y0 + 2, 12, d->galfirst == 0);
    theme_icon(rp, IC_ARROWDOWN, d->galdown.x0 + (d->galdown.x1 - d->galdown.x0 - 12) / 2, d->galdown.y0 + 2, 12, d->galfirst + d->galn >= d->ngal);
    theme_text(rp, theme_font(), d->galmore.x0 + 1, d->galmore.y0 + 2, "\267\267\267", tpen(T_ICON));
    return;
  }
  if (it->flags & F_CHECK) {
    /* a check box and its label */
    LONG bx = b->x0 + 4, by = b->y0 + (rowh() - 11) / 2;
    theme_fill(rp, b->x0, b->y0, b->x1, b->y1, pr ? tpen(T_PRESSED) : hot ? tpen(T_HOVER) : tpen(T_RIBBON));
    theme_fill(rp, bx, by, bx + 10, by + 10, tpen(T_WHITE));
    theme_rect(rp, bx, by, bx + 10, by + 10, tpen(en ? T_INPUTBORDER : T_DISABLED));
    if (on) {
      SetAPen(rp, tpen(en ? T_ACCENT : T_DISABLED));
      Move(rp, bx + 2, by + 5); Draw(rp, bx + 4, by + 7); Draw(rp, bx + 8, by + 3);
      Move(rp, bx + 2, by + 6); Draw(rp, bx + 4, by + 8); Draw(rp, bx + 8, by + 4);
    }
    if (it->label) theme_text(rp, theme_font(), b->x0 + 22, b->y0 + (rowh() - fh()) / 2, it->label, tpen(en ? T_TEXT : T_DISABLED));
    return;
  }
  bg = pr ? tpen(T_PRESSED) : on ? tpen(T_CHECKED) : hot ? tpen(T_HOVER) : tpen(T_RIBBON);
  theme_fill(rp, b->x0, b->y0, b->x1, b->y1, tpen(T_RIBBON));
  if (it->kind == K_LABEL) { theme_text(rp, theme_font(), b->x0 + 2, b->y0 + (rowh() - fh()) / 2, it->label, tpen(T_DIM)); return; }
  if (it->kind == K_FIELD) {
    char t[64];
    LONG fx = b->x0, lw = 0;
    if (it->label) { lw = theme_textlen(rp, theme_font(), it->label) + 4; theme_text(rp, theme_font(), fx, b->y0 + (rowh() - fh()) / 2, it->label, tpen(T_TEXT)); }
    fx += lw;
    theme_fill(rp, fx, b->y0 + 1, b->x1, b->y1 - 1, tpen(T_WHITE));
    theme_rect(rp, fx, b->y0 + 1, b->x1, b->y1 - 1, hot ? tpen(T_ACCENT) : tpen(T_INPUTBORDER));
    field_text(d, it, t, sizeof t);
    {
      struct TextExtent te;
      ULONG fit;
      SetFont(rp, theme_font());
      fit = TextFit(rp, (STRPTR)t, (ULONG)strlen(t), &te, NULL, 1, (ULONG)(b->x1 - fx - 14), 100);
      SetAPen(rp, tpen(T_TEXT));
      Move(rp, fx + 3, b->y0 + (rowh() - fh()) / 2 + theme_font()->tf_Baseline);
      Text(rp, (STRPTR)t, fit);
    }
    theme_icon(rp, IC_ARROWDOWN, b->x1 - 11, b->y0 + (rowh() - 10) / 2, 10, 0);
    return;
  }
  if (bg != tpen(T_RIBBON)) theme_fill(rp, b->x0, b->y0, b->x1, b->y1, bg);
  if (on) theme_rect(rp, b->x0, b->y0, b->x1, b->y1, tpen(T_SELECTION));
  if (it->size == S_LARGE) {
    LONG lw = it->label ? theme_textlen(rp, theme_font(), it->label) : 0, ix = b->x0 + (b->x1 - b->x0 + 1 - 24) / 2;
    item_icon(rp, it, ix, b->y0 + 4, 24, !en);
    const char *lab = it->label ? tr(it->label) : NULL;
    LONG w2;
    int sp = lab && squeeze ? split_at(rp, lab, &w2) : 0;
    if (sp) {                                  /* two lines, the arrow after the second */
      int skip = 1;
      LONG a = TextLength(rp, (STRPTR)lab, (ULONG)sp), c = TextLength(rp, (STRPTR)lab + sp + skip, (ULONG)strlen(lab + sp + skip));
      LONG cx = b->x0 + (b->x1 - b->x0 + 1 - c - (it->kind == K_DROP ? 10 : 0)) / 2;
      SetAPen(rp, tpen(en ? T_TEXT : T_DISABLED)); SetDrMd(rp, JAM1);
      Move(rp, b->x0 + (b->x1 - b->x0 + 1 - a) / 2, b->y0 + 30 + theme_font()->tf_Baseline); Text(rp, (STRPTR)lab, (ULONG)sp);
      Move(rp, cx, b->y0 + 30 + fh() + theme_font()->tf_Baseline); Text(rp, (STRPTR)lab + sp + skip, (ULONG)strlen(lab + sp + skip));
      if (it->kind == K_DROP) theme_icon(rp, IC_ARROWDOWN, cx + c + 2, b->y0 + 30 + fh() + (fh() - 8) / 2, 8, !en);
    } else {
      if (lab) theme_text(rp, theme_font(), b->x0 + (b->x1 - b->x0 + 1 - lw) / 2, b->y0 + 31, it->label, tpen(en ? T_TEXT : T_DISABLED));
      if (it->kind == K_DROP) theme_icon(rp, IC_ARROWDOWN, b->x0 + (b->x1 - b->x0 + 1 - 8) / 2, b->y0 + 31 + fh(), 8, !en);
    }
  } else {
    LONG iy = b->y0 + (rowh() - 16) / 2;
    item_icon(rp, it, b->x0 + 3, iy, 16, !en);
    if (it->size == S_SMALL && it->label && !bare(it)) theme_text(rp, theme_font(), b->x0 + 22, b->y0 + (rowh() - fh()) / 2, it->label, tpen(en ? T_TEXT : T_DISABLED));
    if (it->kind == K_DROP) theme_icon(rp, IC_ARROWDOWN, b->x1 - 9, b->y0 + (rowh() - 8) / 2, 8, !en);
  }
  if ((it->flags & F_SPLIT) && (hot || pr)) {
    /* a split button shows its two halves while the pointer is on it */
    SetAPen(rp, tpen(T_BORDER2));
    if (it->size == S_LARGE) { Move(rp, b->x0 + 2, b->y0 + 28); Draw(rp, b->x1 - 2, b->y0 + 28); }
    else { Move(rp, b->x1 - 12, b->y0 + 2); Draw(rp, b->x1 - 12, b->y1 - 2); }
  }
}
/* the tab strip: the tabs' names (contextual ones in the accent colour, the shown one underlined)
   and the quick access Undo and Redo */
static void draw_tabs(struct RData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  int t;
  theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mtop(obj) + tabh() + 1, tpen(T_TABSTRIP));
  for (t = 0; t < NTABS; t++) {
    RBox *b = &d->tabbox[t];
    LONG w;
    int act = t == d->tab;
    if (b->x0 < 0) continue;
    if (d->hot == 100 + t && !act) theme_fill(rp, b->x0 + 2, b->y0 + 2, b->x1 - 2, b->y1 - 1, tpen(T_HOVER));
    w = theme_textlen(rp, theme_font(), tabs[t].name);
    if (d->tabcap && w > d->tabcap) {
      /* crowded: as much of the name as fits, and a dot */
      const char *nm = tr(tabs[t].name);
      struct TextExtent te;
      ULONG fit;
      SetFont(rp, theme_font());
      fit = TextFit(rp, (STRPTR)nm, (ULONG)strlen(nm), &te, NULL, 1, (ULONG)(d->tabcap - TextLength(rp, (STRPTR)".", 1)), 100);
      w = d->tabcap;
      SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
      SetAPen(rp, tpen(act || tabs[t].contextual ? T_ACCENT : T_TEXT)); SetDrMd(rp, JAM1);
      Move(rp, b->x0 + (b->x1 - b->x0 + 1 - w) / 2, b->y0 + (tabh() - fh()) / 2 + theme_font()->tf_Baseline);
      Text(rp, (STRPTR)nm, fit); Text(rp, (STRPTR)".", 1);
    } else
      theme_text(rp, theme_font(), b->x0 + (b->x1 - b->x0 + 1 - w) / 2, b->y0 + (tabh() - fh()) / 2, tabs[t].name,
                 tpen(act || tabs[t].contextual ? T_ACCENT : T_TEXT));
    if (act) theme_fill(rp, b->x0 + (b->x1 - b->x0 + 1 - w) / 2, b->y1 - 1, b->x0 + (b->x1 - b->x0 + 1 + w) / 2, b->y1, tpen(T_ACCENT));
  }
  for (t = 0; t < NQA; t++) {
    RBox *b = &d->qa[t];
    int en = item_enabled(d, &g_undo[t]);
    if (b->x0 < 0) continue;
    if (d->hot == 200 + t && en) theme_fill(rp, b->x0, b->y0 + 1, b->x1, b->y1 - 1, tpen(T_HOVER));
    theme_icon(rp, g_undo[t].icon, b->x0 + 3, b->y0 + (b->y1 - b->y0 + 1 - 16) / 2, 16, !en);
  }
}
/* the whole ribbon, laid out afresh: tabs, items, then each group's caption and the line after it */
static void draw_all(struct RData *d, Object *obj)
{
  struct RastPort *rp = _rp(obj);
  LONG y0 = _mtop(obj) + tabh() + 2;
  int g, k;
  const Tab *tab;
  layout_ribbon(d, obj);
  draw_tabs(d, obj);
  theme_fill(rp, _mleft(obj), y0, _mright(obj), _mbottom(obj), tpen(T_RIBBON));
  SetAPen(rp, tpen(T_BORDER));
  Move(rp, _mleft(obj), _mbottom(obj)); Draw(rp, _mright(obj), _mbottom(obj));
  tab = &tabs[d->tab];
  if (!tab->groups) return;
  for (k = 0; k < d->nitem; k++) draw_item(d, obj, k);
  for (g = 0; g < d->ngroup && g < tab->n; g++) {
    RBox *b = &d->group[g];
    const char *name = tr(tab->groups[g].name);
    struct TextExtent te;
    ULONG fit;
    SetFont(rp, theme_font());
    fit = TextFit(rp, (STRPTR)name, (ULONG)strlen(name), &te, NULL, 1, (ULONG)(b->x1 - b->x0 - 3 > 0 ? b->x1 - b->x0 - 3 : 0), 100);
    SetAPen(rp, tpen(T_GROUPLABEL)); SetDrMd(rp, JAM1);
    Move(rp, b->x0 + (b->x1 - b->x0 + 1 - te.te_Width) / 2, y0 + bodyh() + theme_font()->tf_Baseline);
    Text(rp, (STRPTR)name, fit);
    SetAPen(rp, tpen(T_BORDER));
    Move(rp, b->x1, y0 + 6); Draw(rp, b->x1, _mbottom(obj) - 5);
  }
}

/* ------------------------------------------------------------------ */
/* the gallery's fonts: each style's family, bold and size (shrunk to fit), from Quill's screen fonts.
   The old ones are given back first; one that won't open stays NULL and the interface font is used */
static void gallery_fonts(struct RData *d)
{
  Doc *doc = d->st.doc;
  int i;
  for (i = 0; i < MAXGAL; i++) if (d->galfont[i]) { theme_close_font(d->galfont[i]); d->galfont[i] = NULL; }
  if (!doc) return;
  for (i = 0; i < d->ngal; i++) {
    const Style *s = &doc->styles[d->galstyle[i]];
    const char *fam = s->cf.font < doc->nfonts ? doc->fonts[s->cf.font] : "Times New Roman", *sub = d->st.fs ? fonts_substitute(d->st.fs, fam) : fam;
    char name[40];
    struct TextAttr ta;
    LONG em = s->cf.size * 7 / 20;             /* about 70% of the size, in pixels */
    const char *af = strstr(sub, "Carlito") ? "QuillSansC" : strstr(sub, "Caladea") ? "QuillSerifC" : strstr(sub, "Mono") ? "QuillMono" :
                     (strstr(sub, "Serif") && !strstr(sub, "Sans")) ? "QuillSerif" : "QuillSans";
    if (em < 8) em = 8;
    if (em > 18) em = 18;
    sprintf(name, "%s%s.font", af, (s->cf.flags & CF_BOLD) ? ((s->cf.flags & CF_ITALIC) ? "BI" : "B") : (s->cf.flags & CF_ITALIC) ? "I" : "");
    (void)ta;
    d->galfont[i] = theme_open_font(name, (UWORD)(em + em / 8 + 1));
  }
}

/* ------------------------------------------------------------------ */
/* the list window */
/* the list window's list object: which ribbon and list it shows, how many entries, the one under
   the pointer, its columns and width; labels for text lists (the font list included) */
struct PData { struct RData *r; Object *ribbon; const List *l; int n, hot, cols; LONG w; const char *const *labels; char **dyn; };
#define MUIM_PopList_Open (QTAG + 0x320)   /* (not sent at present) */
/* declared ahead: open_list makes the list object and sizes it */
static ULONG popdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg);
static LONG pop_rowh(struct PData *p);
/* opens item k's list under it, in a borderless window of its own. The window is made on first use
   and added to the application (OM_ADDMEMBER), then reused; a notification closes it when it is no
   longer the active window, so a click anywhere else dismisses it. The width is worked out here
   before opening because MUI asks the list's size (MUIM_AskMinMax) as the window opens. The font
   list measures each name in its own font, which is slow, so the window sleeps (busy pointer) meanwhile */
static void open_list(struct RData *d, Object *obj, int k)
{
  int g;
  const Item *it = nth_item(d, k, &g);
  const List *l;
  struct PData *pd;
  LONG x, y, w = 150;
  struct Window *win = _window(obj);
  if (!it || !win) return;
  l = item_list(it);
  if (!d->popwin) {
    d->poplist = NewObject(pcls->mcc_Class, NULL, MUIA_FillArea, FALSE, TAG_DONE);
    d->popwin = QWindowObject, MUIA_Window_Borderless, TRUE, MUIA_Window_CloseGadget, FALSE, MUIA_Window_DepthGadget, FALSE,
      MUIA_Window_SizeGadget, FALSE, MUIA_Window_DragBar, FALSE, MUIA_Window_Activate, TRUE,
      WindowContents, VGroup, InnerSpacing(0, 0), MUIA_Group_Spacing, 0, Child, d->poplist, End, End;
    if (!d->popwin) { d->poplist = NULL; return; }
    DoMethod(_app(obj), OM_ADDMEMBER, (ULONG)d->popwin);
    DoMethod(d->popwin, MUIM_Notify, MUIA_Window_Activate, FALSE, (ULONG)d->popwin, 3, MUIM_Set, MUIA_Window_Open, FALSE);
  }
  set(d->popwin, MUIA_Window_Open, FALSE);
  pd = INST_DATA(pcls->mcc_Class, d->poplist);
  pd->r = d; pd->ribbon = obj; pd->l = l; pd->hot = -1;
  if (l->kind == PL_STYLES) { pd->n = d->ngal; pd->labels = NULL; }
  else if (l->kind == PL_TSTYLES) {
    /* Quill's styles, then the document's own (from Word) */
    Doc *doc = d->st.doc;
    int i, k;
    d->tsn = 0;
    d->tsval[d->tsn] = -1; d->tsp[d->tsn++] = NULL;                  /* none */
    for (i = 0; i < tstyle_builtin_count() && d->tsn < 64; i++) { d->tsval[d->tsn] = i; d->tsp[d->tsn++] = tstyle_builtin(i); }
    for (k = 1; doc && k <= doc->ntstyles && d->tsn < 64; k++) {
      const TableStyle *ts = &doc->tstyles[k - 1];
      int dup = !strcmp(ts->id, "TableNormal"), used = 0;
      for (i = 0; i < doc->ntables && !used; i++) if (doc->tables[i].tstyle == k) used = 1;
      if (!used) continue;                       /* (a file from Word may carry a hundred it doesn't use) */
      for (i = 0; i < tstyle_builtin_count() && !dup; i++) if (!strcmp(tstyle_builtin(i)->id, ts->id)) dup = 1;
      if (!dup) { d->tsval[d->tsn] = 1000 + k; d->tsp[d->tsn++] = ts; }
    }
    pd->n = d->tsn; pd->labels = NULL;
  }
  else if (l->kind == PL_FONTS) { pd->labels = ribbon_font_names(d->st.fs); for (pd->n = 0; pd->labels[pd->n]; pd->n++) ; }
  else { pd->n = l->n; pd->labels = l->labels; }
  d->popitem = k;
  pd->cols = 1;
  if (l->kind == PL_FONTS) {
    /* each name in its own font; in as many columns as keep the list on the screen */
    int i, rows;
    LONG cw = 120, room = _screen(obj)->Height - (win->TopEdge + d->item[k].y1 + 1) - 8;
    set(_win(obj), MUIA_Window_Sleep, TRUE);
    for (i = 0; i < pd->n; i++) {
      Face *f = d->st.fs ? fonts_face(d->st.fs, pd->labels[i], 0, 0) : NULL;
      char t[64];
      LONG tw;
      utf8_to_latin1(pd->labels[i], (u32)strlen(pd->labels[i]), t, sizeof t);
      tw = f ? theme_preview_width(f, t, FONTPX * 64) : 0;
      if (!tw) tw = theme_textlen(_rp(obj), theme_font(), t);
      if (tw + 16 > cw) cw = tw + 16;
    }
    set(_win(obj), MUIA_Window_Sleep, FALSE);
    rows = (int)(room / pop_rowh(pd));
    if (rows < 4) rows = 4;
    pd->cols = (pd->n + rows - 1) / rows;
    if (pd->cols < 1) pd->cols = 1;
    if (cw * pd->cols > _screen(obj)->Width) cw = _screen(obj)->Width / pd->cols;
    w = cw * pd->cols;
  }
  {
    int i;
    struct RastPort *rp = _rp(obj);
    for (i = 0; i < pd->n && l->kind != PL_FONTS; i++) {
      char t[64];
      LONG tw;
      if (l->kind == PL_STYLES) utf8_to_latin1(d->st.doc->styles[d->galstyle[i]].name, (u32)strlen(d->st.doc->styles[d->galstyle[i]].name), t, sizeof t);
      else if (l->kind == PL_TSTYLES) { if (d->tsp[i]) utf8_to_latin1(d->tsp[i]->name, (u32)strlen(d->tsp[i]->name), t, sizeof t); else strcpy(t, tr("No Style")); }
      else strncpy(t, pd->labels[i], sizeof t - 1), t[sizeof t - 1] = 0;
      tw = theme_textlen(rp, theme_font(), t) + (l->kind == PL_COLOR ? 26 : l->kind == PL_TSTYLES ? 56 : l->kind == PL_LOOKCOLOR ? 44 :
                                                 l->kind == PL_LOOKSET || l->kind == PL_LOOKTHEME ? 150 : 12);
      if (tw > w) w = tw;
    }
  }
  if (l->kind == PL_TSTYLES) w = w * TS_COLS;
  if (d->item[k].x1 - d->item[k].x0 + 1 > w) w = d->item[k].x1 - d->item[k].x0 + 1;
  pd->w = w;
  x = win->LeftEdge + d->item[k].x0; y = win->TopEdge + d->item[k].y1 + 1;
  if (it->kind == K_GALLERY && !d->galcompact) { x = win->LeftEdge + d->galmore.x0 - w + 12; y = win->TopEdge + d->galmore.y1 + 1; }
  if (x + w > _screen(obj)->Width) x = _screen(obj)->Width - w;
  if (x < 0) x = 0;
  SetAttrs(d->popwin, MUIA_Window_LeftEdge, x, MUIA_Window_TopEdge, y, TAG_DONE);
  /* a fresh layout for the new contents */
  DoMethod(d->popwin, MUIM_Window_ScreenToFront);
  set(d->popwin, MUIA_Window_Open, TRUE);
}
/* a choice from a list (or a style from the gallery) */
static void chosen(Object *obj, struct RData *d, int k, LONG value)
{
  int g;
  const Item *it = nth_item(d, k, &g);
  if (!it) return;
  d->value = value;
  if (it->id == ID_PENSTYLE) d->penstyle = (int)value;          /* the pen shows in its fields */
  if (it->id == ID_PENWIDTH) d->penwidth = (int)value;
  DoMethod(_app(obj), MUIM_Application_ReturnID, (ULONG)it->id);
}

/* a list entry's height: a table style's picture is taller than a line of text; table styles go
   in columns of this width (three of them), so the list stays on the screen */
static LONG pop_rowh(struct PData *p) { return p->l && p->l->kind == PL_TSTYLES ? 26 : p->l && p->l->kind == PL_FONTS ? FONTPX + 8 :
                                               p->l && (p->l->kind == PL_LOOKSET || p->l->kind == PL_LOOKTHEME) ? 30 : fh() + 5; }
/* how many columns the list has, and so how many rows */
static int pop_cols(struct PData *p) { return p->l && p->l->kind == PL_TSTYLES ? TS_COLS : p->l && p->l->kind == PL_FONTS ? p->cols : 1; }
static int pop_rows(struct PData *p) { int c = pop_cols(p); return (p->n + c - 1) / c; }
/* a picture of a table style: 5 columns by 4 rows of its lines and fills */
static void tstyle_picture(Object *obj, struct RastPort *rp, const TableStyle *ts, LONG x, LONG y, LONG w, LONG h)
{
  int r, c, e;
  LONG cw = w / 5, ch = h / 4;
  struct ColorMap *cm = _screen(obj)->ViewPort.ColorMap;
  theme_fill(rp, x, y, x + 5 * cw, y + 4 * ch, tpen(T_WHITE));
  for (r = 0; r < 4; r++)
    for (c = 0; c < 5; c++) {
      CellLook lk;
      LONG x0 = x + c * cw, y0 = y + r * ch, x1 = x0 + cw, y1 = y0 + ch;
      tstyle_preview(ts, LK_DEFAULT, r, c, 4, 5, &lk);
      if (!ts) { memset(&lk, 0, sizeof lk); lk.shade = COLOR_AUTO; }
      if (lk.shade != COLOR_AUTO) {
        ULONG v = lk.shade;
        LONG pen = ObtainBestPen(cm, ((v >> 16) & 255) * 0x01010101UL, ((v >> 8) & 255) * 0x01010101UL, (v & 255) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
        theme_fill(rp, x0, y0, x1, y1, pen);
        ReleasePen(cm, pen);
      }
      /* the text: a dash, white on a dark fill */
      {
        ULONG v = lk.shade == COLOR_AUTO ? 0xFFFFFF : lk.shade;
        int dark = ((v >> 16) & 255) + ((v >> 8) & 255) + (v & 255) < 384;
        SetAPen(rp, dark ? tpen(T_WHITE) : (lk.cmask & CM_BOLD) && (lk.cf.flags & CF_BOLD) ? tpen(T_TEXT) : tpen(T_DIM));
        Move(rp, x0 + 2, (y0 + y1) / 2); Draw(rp, x1 - 3, (y0 + y1) / 2);
      }
      for (e = 0; e < 4; e++) {
        LONG pen;
        ULONG v = lk.bcolor[e];
        if (!(lk.borders & (1 << e))) continue;
        pen = v == COLOR_AUTO || !v ? tpen(T_TEXT) : ObtainBestPen(cm, ((v >> 16) & 255) * 0x01010101UL, ((v >> 8) & 255) * 0x01010101UL, (v & 255) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
        SetAPen(rp, pen);
        if (e == E_TOP) { Move(rp, x0, y0); Draw(rp, x1, y0); }
        else if (e == E_BOTTOM) { Move(rp, x0, y1); Draw(rp, x1, y1); }
        else if (e == E_LEFT) { Move(rp, x0, y0); Draw(rp, x0, y1); }
        else { Move(rp, x1, y0); Draw(rp, x1, y1); }
        if (!(v == COLOR_AUTO || !v)) ReleasePen(cm, pen);
      }
    }
}
/* the list's dispatcher. It takes the mouse the older MUI way, asking for IDCMP events with
   MUI_RequestIDCMP and getting MUIM_HandleInput, which does in a window of its own. A choice
   closes the window through MUIM_Application_PushMethod (done after this method returns: a window
   shouldn't be closed from inside its own input handling) and goes to chosen() */
static ULONG popdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct PData *p;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    p = INST_DATA(cl, obj);
    memset(p, 0, sizeof *p);
    p->hot = -1;
    return (ULONG)obj;
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    MUI_RequestIDCMP(obj, IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE);
    return TRUE;
  case MUIM_Cleanup:
    MUI_RejectIDCMP(obj, IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE);
    break;
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    LONG h;
    p = INST_DATA(cl, obj);
    DoSuperMethodA(cl, obj, msg);
    h = (p->n ? pop_rows(p) : 1) * pop_rowh(p) + 4;
    m->MinMaxInfo->MinWidth += p->w ? p->w : 150; m->MinMaxInfo->DefWidth += p->w ? p->w : 150; m->MinMaxInfo->MaxWidth += p->w ? p->w : 150;
    m->MinMaxInfo->MinHeight += h; m->MinMaxInfo->DefHeight += h; m->MinMaxInfo->MaxHeight += h;
    return 0;
  }
  case MUIM_Draw: {
    struct RastPort *rp;
    int i;
    DoSuperMethodA(cl, obj, msg);
    p = INST_DATA(cl, obj);
    rp = _rp(obj);
    theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_WHITE));
    theme_rect(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(T_BORDER2));
    for (i = 0; i < p->n; i++) {
      LONG cw = (_mwidth(obj) - 2) / pop_cols(p), y = _mtop(obj) + 2 + (i / pop_cols(p)) * pop_rowh(p), x0 = _mleft(obj) + 1 + (i % pop_cols(p)) * cw, x = x0 + 5;
      char t[64];
      if (i == p->hot) theme_fill(rp, x0, y, pop_cols(p) > 1 ? x0 + cw - 1 : _mright(obj) - 1, y + pop_rowh(p) - 1, tpen(T_HOVER));
      if (p->l->kind == PL_STYLES) utf8_to_latin1(p->r->st.doc->styles[p->r->galstyle[i]].name, (u32)strlen(p->r->st.doc->styles[p->r->galstyle[i]].name), t, sizeof t);
      else if (p->l->kind == PL_TSTYLES) {
        const TableStyle *ts = p->r->tsp[i];
        if (ts) utf8_to_latin1(ts->name, (u32)strlen(ts->name), t, sizeof t); else strcpy(t, tr("No Style"));
        tstyle_picture(obj, rp, ts, x, y + 2, 40, 20);
        theme_text(rp, theme_font(), x + 48, y + (pop_rowh(p) - fh()) / 2, t, tpen(T_TEXT));
        continue;
      }
      else if (p->l->kind == PL_FONTS) {
        Face *f = p->r->st.fs ? fonts_face(p->r->st.fs, p->labels[i], 0, 0) : NULL;
        utf8_to_latin1(p->labels[i], (u32)strlen(p->labels[i]), t, sizeof t);
        if (!f || !theme_preview(rp, f, t, FONTPX * 64, x, y + 4 + FONTPX * 4 / 5, cw - 10, tpen(T_TEXT)))
          theme_text(rp, theme_font(), x, y + (pop_rowh(p) - fh()) / 2, t, tpen(T_TEXT));
        continue;
      }
      else if (p->l->kind >= PL_LOOKSET) {
        /* a look: "Title" and "Heading" in its fonts and colours (a colour set: two swatches), then its name */
        const char *hf, *bf;
        u32 hc, tc;
        struct ColorMap *cm = _screen(obj)->ViewPort.ColorMap;
        LONG tp, hp;
        look_info(p->l->kind - PL_LOOKSET, i, &hf, &bf, &hc, &tc);
        tp = tc == COLOR_AUTO ? -1 : ObtainBestPen(cm, ((tc >> 16) & 255) * 0x01010101UL, ((tc >> 8) & 255) * 0x01010101UL, (tc & 255) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
        hp = hc == COLOR_AUTO ? -1 : ObtainBestPen(cm, ((hc >> 16) & 255) * 0x01010101UL, ((hc >> 8) & 255) * 0x01010101UL, (hc & 255) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
        if (p->l->kind == PL_LOOKCOLOR) {
          theme_fill(rp, x, y + 2, x + 13, y + fh() + 1, tp >= 0 ? tp : tpen(T_TEXT));
          theme_fill(rp, x + 16, y + 2, x + 29, y + fh() + 1, hp >= 0 ? hp : tpen(T_TEXT));
          theme_rect(rp, x, y + 2, x + 13, y + fh() + 1, tpen(T_BORDER2)); theme_rect(rp, x + 16, y + 2, x + 29, y + fh() + 1, tpen(T_BORDER2));
          theme_text(rp, theme_font(), x + 38, y + 2, p->labels[i], tpen(T_TEXT));
        } else {
          Face *fh1 = p->r->st.fs ? fonts_face(p->r->st.fs, hf, 0, 0) : NULL;
          if (!fh1 || !theme_preview(rp, fh1, "Title", 18 * 64, x, y + 21, 64, tp >= 0 ? tp : tpen(T_TEXT)))
            theme_text(rp, theme_font(), x, y + 8, "Title", tp >= 0 ? tp : tpen(T_TEXT));
          if (!fh1 || !theme_preview(rp, fh1, "Heading", 11 * 64, x + 66, y + 21, 64, hp >= 0 ? hp : tpen(T_TEXT)))
            theme_text(rp, theme_font(), x + 66, y + 10, "Heading", hp >= 0 ? hp : tpen(T_TEXT));
          theme_text(rp, theme_font(), x + 138, y + (30 - fh()) / 2, p->labels[i], tpen(T_TEXT));
        }
        if (tp >= 0) ReleasePen(cm, tp);
        if (hp >= 0) ReleasePen(cm, hp);
        continue;
      }
      else if (p->l->kind == PL_UNDERLINE) {
        /* each kind under its own name (Words Only: under the words, not the space) */
        LONG tw = theme_textlen(rp, theme_font(), p->labels[i]), uy = y + 3 + theme_font()->tf_Baseline, j;
        theme_text(rp, theme_font(), x, y + 2, p->labels[i], tpen(T_TEXT));
        SetAPen(rp, tpen(T_TEXT));
        switch (i) {
        case UL_SINGLE: Move(rp, x, uy); Draw(rp, x + tw - 1, uy); break;
        case UL_DOUBLE: Move(rp, x, uy); Draw(rp, x + tw - 1, uy); Move(rp, x, uy + 2); Draw(rp, x + tw - 1, uy + 2); break;
        case UL_THICK: RectFill(rp, x, uy, x + tw - 1, uy + 1); break;
        case UL_DOTTED: for (j = 0; j < tw; j += 2) WritePixel(rp, x + j, uy); break;
        case UL_DASHED: for (j = 0; j < tw; j += 6) { Move(rp, x + j, uy); Draw(rp, x + (j + 3 < tw ? j + 3 : tw - 1), uy); } break;
        case UL_WAVE: Move(rp, x, uy); for (j = 2; j < tw; j += 2) Draw(rp, x + j, uy + ((j / 2) & 1 ? 1 : -1)); break;
        case UL_WORDS: {
          /* under the first word and what follows its space (the label as translated) */
          const char *lab = tr(p->labels[i]), *gap = strchr(lab, ' ');
          char w1[32];
          size_t n1 = gap ? (size_t)(gap - lab) : strlen(lab);
          if (n1 >= sizeof w1) n1 = sizeof w1 - 1;
          memcpy(w1, lab, n1); w1[n1] = 0;
          Move(rp, x, uy); Draw(rp, x + theme_textlen(rp, theme_font(), w1) - 1, uy);
          if (gap) { strncat(w1, " ", sizeof w1 - 1 - n1); Move(rp, x + theme_textlen(rp, theme_font(), w1), uy); Draw(rp, x + tw - 1, uy); }
          break; }
        }
        continue;
      }
      else { strncpy(t, p->labels[i], sizeof t - 1); t[sizeof t - 1] = 0; }
      if (p->l->kind == PL_COLOR) {
        ULONG c = p->l->colors[i];
        if (c == COLOR_AUTO) theme_rect(rp, x, y + 2, x + 13, y + fh() + 1, tpen(T_DIM));
        else if (c == COLOR_MORE) {
          /* More Colours...: four small swatches */
          LONG my = y + 2 + (fh() - 1) / 2;
          theme_fill(rp, x, y + 2, x + 6, my, tpen(T_RED)); theme_fill(rp, x + 7, y + 2, x + 13, my, tpen(T_GREEN));
          theme_fill(rp, x, my + 1, x + 6, y + fh() + 1, tpen(T_ACCENT)); theme_fill(rp, x + 7, my + 1, x + 13, y + fh() + 1, tpen(T_YELLOW));
        }
        else {
          LONG pen = ObtainBestPen(_screen(obj)->ViewPort.ColorMap, ((c >> 16) & 255) * 0x01010101UL, ((c >> 8) & 255) * 0x01010101UL, (c & 255) * 0x01010101UL,
                                   OBP_Precision, PRECISION_IMAGE, TAG_DONE);
          theme_fill(rp, x, y + 2, x + 13, y + fh() + 1, pen);
          theme_rect(rp, x, y + 2, x + 13, y + fh() + 1, tpen(T_BORDER2));
          ReleasePen(_screen(obj)->ViewPort.ColorMap, pen);
        }
        x += 20;
      }
      theme_text(rp, theme_font(), x, y + 2, t, tpen(T_TEXT));
    }
    return 0;
  }
  case MUIM_HandleInput: {
    struct MUIP_HandleInput *m = (struct MUIP_HandleInput *)msg;
    p = INST_DATA(cl, obj);
    if (m->imsg && (m->imsg->Class == IDCMP_MOUSEMOVE || m->imsg->Class == IDCMP_MOUSEBUTTONS)) {
      WORD mx = m->imsg->MouseX, my = m->imsg->MouseY;
      int i = -1;
      if (mx >= _mleft(obj) && mx <= _mright(obj) && my >= _mtop(obj) + 2) {
        LONG cw = (_mwidth(obj) - 2) / pop_cols(p);
        i = (int)((my - _mtop(obj) - 2) / pop_rowh(p)) * pop_cols(p) + (int)((mx - _mleft(obj) - 1) / (cw > 0 ? cw : 1));
        if ((mx - _mleft(obj) - 1) / (cw > 0 ? cw : 1) >= pop_cols(p)) i = -1;
      }
      if (i >= p->n) i = -1;
      if (m->imsg->Class == IDCMP_MOUSEMOVE && i != p->hot) { p->hot = i; MUI_Redraw(obj, MADF_DRAWOBJECT); }
      if (m->imsg->Class == IDCMP_MOUSEBUTTONS && m->imsg->Code == SELECTUP && i >= 0) {
        Object *win = _win(obj);
        /* the value: a style's index, a colour, a size in half-points, else the entry's number */
        LONG value = p->l->kind == PL_STYLES ? p->r->galstyle[i] : p->l->kind == PL_TSTYLES ? p->r->tsval[i] : p->l->kind == PL_COLOR ? (LONG)p->l->colors[i] : i;
        if (p->l == &lists[L_SIZES]) value = atoi(p->labels[i]) * 2 + (strchr(p->labels[i], '.') ? 1 : 0);
        DoMethod(_app(obj), MUIM_Application_PushMethod, (ULONG)win, 3, MUIM_Set, MUIA_Window_Open, FALSE);
        chosen(p->ribbon, p->r, p->r->popitem, value);
      }
    }
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}

/* a split button's own command (the list is the rest of what it does) */
static const List *item_list(const Item *it) { return it->xlist ? it->xlist : &lists[it->list < N(lists) ? it->list : 0]; }
static ULONG split_id(UWORD id)
{
  switch (id) {
  case ID_PASTEDD: return ID_PASTE;
  case ID_FINDDD: return ID_FIND;
  case ID_HEADERDD: return ID_HFHEADER;
  case ID_FOOTERDD: return ID_HFFOOTER;
  case ID_TOCDD: return ID_TOC;
  case ID_FORWARDDD: return ID_FORWARD;
  case ID_BACKWARDDD: return ID_BACKWARD;
  case ID_UNDERLINEDD: return ID_UNDERLINE;
  }
  return id;
}
/* whether x, y is on a split button's arrow: under the icon of a large one (its label and arrow),
   the last eleven pixels of a small one */
static int on_arrow(struct RData *d, const Item *it, int k, WORD x, WORD y)
{
  RBox *b = &d->item[k];
  return it->size == S_LARGE ? y >= b->y0 + 29 : x >= b->x1 - 11;
}

/* ------------------------------------------------------------------ */
/* the pointer */
/* what is at window point mx, my: 200 + n a quick access button, 100 + n a tab, else an item's
   number, or -1 (the gallery's scroll buttons are outside every item: events() tests those) */
static int hit(struct RData *d, WORD mx, WORD my)
{
  int k, t;
  for (t = 0; t < NQA; t++)
    if (d->qa[t].x0 >= 0 && mx >= d->qa[t].x0 && mx <= d->qa[t].x1 && my >= d->qa[t].y0 && my <= d->qa[t].y1) return 200 + t;
  for (t = 0; t < NTABS; t++)
    if (d->tabbox[t].x0 >= 0 && mx >= d->tabbox[t].x0 && mx <= d->tabbox[t].x1 && my >= d->tabbox[t].y0 && my <= d->tabbox[t].y1) return 100 + t;
  for (k = 0; k < d->nitem; k++)
    if (mx >= d->item[k].x0 && mx <= d->item[k].x1 && my >= d->item[k].y0 && my <= d->item[k].y1) return k;
  return -1;
}
/* whether x, y is in b, edges included */
static int in_box(RBox *b, WORD x, WORD y) { return x >= b->x0 && x <= b->x1 && y >= b->y0 && y <= b->y1; }
/* after the hover moved from old to d->hot, only what changed drawn again (the tabs, or two items) */
static void redraw_hot(struct RData *d, Object *obj, int old)
{
  if (old >= 100 || d->hot >= 100) draw_tabs(d, obj);
  if (old >= 0 && old < 100) draw_item(d, obj, old);
  if (d->hot >= 0 && d->hot < 100) draw_item(d, obj, d->hot);
}
/* the ribbon's MUIM_HandleEvent (its handler is added in MUIM_Setup by theme_handler). MUI hands
   it every mouse event in the window, so it checks the pointer is over the ribbon first. A button
   acts when the mouse button is let go over it, as MUI's own do; the quick access buttons, tabs
   and gallery act at once. MUI_EventHandlerRC_Eat keeps the click from other objects */
static ULONG events(struct IClass *cl, Object *obj, struct MUIP_HandleEvent *m)
{
  struct RData *d = INST_DATA(cl, obj);
  struct IntuiMessage *im = m->imsg;
  int h, old;
  if (!im || !d->shown) return 0;
  if (im->Class == IDCMP_MOUSEMOVE) {
    h = (im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj) && im->MouseY >= _mtop(obj) && im->MouseY <= _mbottom(obj)) ? hit(d, im->MouseX, im->MouseY) : -1;
    if (h != d->hot) { old = d->hot; d->hot = h; redraw_hot(d, obj, old); }
    return 0;
  }
  if (im->Class != IDCMP_MOUSEBUTTONS) return 0;
  if (im->Code == SELECTDOWN) {
    if (im->MouseX < _mleft(obj) || im->MouseX > _mright(obj) || im->MouseY < _mtop(obj) || im->MouseY > _mbottom(obj)) return 0;
    h = hit(d, im->MouseX, im->MouseY);
    if (h >= 200) {
      /* quick access Undo or Redo */
      if (item_enabled(d, &g_undo[h - 200])) DoMethod(_app(obj), MUIM_Application_ReturnID, (ULONG)g_undo[h - 200].id);
      return MUI_EventHandlerRC_Eat;
    }
    if (h >= 100) {
      int t = h - 100;
      if (t == 0) { DoMethod(_app(obj), MUIM_Application_ReturnID, ID_FILETAB); return MUI_EventHandlerRC_Eat; }
      if (t != d->tab) { d->tab = t; d->hot = -1; MUI_Redraw(obj, MADF_DRAWOBJECT); }
      return MUI_EventHandlerRC_Eat;
    }
    if (h >= 0) {
      int g;
      const Item *it = nth_item(d, h, &g);
      if (!it || !item_enabled(d, it) || it->kind == K_LABEL) return MUI_EventHandlerRC_Eat;
      if (it->kind == K_GALLERY && !d->galcompact) {
        WORD x = im->MouseX, y = im->MouseY;
        if (in_box(&d->gal, x, y)) {
          int i = (x - d->gal.x0) / d->galw;
          if (d->galfirst + i < d->ngal) chosen(obj, d, h, d->galstyle[d->galfirst + i]);
        }
        return MUI_EventHandlerRC_Eat;
      }
      d->pressed = h;
      draw_item(d, obj, h);
      return MUI_EventHandlerRC_Eat;
    }
    /* the gallery's buttons lie outside the item boxes */
    if (d->tab == 1) {
      int k;
      for (k = 0; k < d->nitem; k++) {
        int g;
        const Item *it = nth_item(d, k, &g);
        if (!it || it->kind != K_GALLERY) continue;
        if (in_box(&d->galup, im->MouseX, im->MouseY) && d->galfirst > 0) { d->galfirst -= d->galn; if (d->galfirst < 0) d->galfirst = 0; draw_item(d, obj, k); }
        else if (in_box(&d->galdown, im->MouseX, im->MouseY) && d->galfirst + d->galn < d->ngal) { d->galfirst += d->galn; draw_item(d, obj, k); }
        else if (in_box(&d->galmore, im->MouseX, im->MouseY)) open_list(d, obj, k);
      }
    }
    return MUI_EventHandlerRC_Eat;
  }
  if (im->Code == SELECTUP && d->pressed >= 0) {
    int k = d->pressed, g;
    const Item *it = nth_item(d, k, &g);
    d->pressed = -1;
    draw_item(d, obj, k);
    if (it && hit(d, im->MouseX, im->MouseY) == k) {
      if ((it->flags & F_SPLIT) && !on_arrow(d, it, k, im->MouseX, im->MouseY)) DoMethod(_app(obj), MUIM_Application_ReturnID, it->split ? (ULONG)it->split : split_id(it->id));
      else if (it->kind == K_DROP || it->kind == K_FIELD || it->kind == K_GALLERY) open_list(d, obj, k);
      else DoMethod(_app(obj), MUIM_Application_ReturnID, (ULONG)it->id);
    }
    return MUI_EventHandlerRC_Eat;
  }
  return 0;
}

/* the ribbon class's dispatcher. MUI sends every method here and what isn't handled goes to Area;
   the __reg()s are how vbcc takes BOOPSI's register arguments. MUIM_Ribbon_State is sent by the
   view as the caret moves: it is ignored when nothing shown has changed, so the ribbon isn't
   redrawn on every key. The bubble text is built in a static buffer, which MUI only reads until
   MUIM_DeleteShortHelp (nothing to free) */
static ULONG dispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct RData *d;
  switch (msg->MethodID) {
  case OM_NEW:
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->tab = 1; d->hot = d->pressed = -1;
    d->penwidth = 1;                                             /* half a point */
    d->ehn.ehn_Object = obj; d->ehn.ehn_Class = cl; d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
    return (ULONG)obj;
  case OM_DISPOSE: {
    int i;
    d = INST_DATA(cl, obj);
    for (i = 0; i < MAXGAL; i++) if (d->galfont[i]) theme_close_font(d->galfont[i]);
    break;
  }
  case OM_GET: {
    struct opGet *g = (struct opGet *)msg;
    d = INST_DATA(cl, obj);
    if (g->opg_AttrID == MUIA_Ribbon_Value) { *g->opg_Storage = (ULONG)d->value; return TRUE; }
    if (g->opg_AttrID == MUIA_Ribbon_Tab) { *g->opg_Storage = (ULONG)d->tab; return TRUE; }
    break;
  }
  case OM_SET: {
    struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList, *t;
    d = INST_DATA(cl, obj);
    while ((t = NextTagItem(&tags)))
      if (t->ti_Tag == MUIA_Ribbon_Tab && t->ti_Data < NTABS) { d->tab = (int)t->ti_Data; if (_win(obj)) MUI_Redraw(obj, MADF_DRAWOBJECT); }
    break;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    LONG h = tabh() + 2 + bodyh() + caph() + 3;
    DoSuperMethodA(cl, obj, msg);
    m->MinMaxInfo->MinWidth += 200; m->MinMaxInfo->DefWidth += 640; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += h; m->MinMaxInfo->DefHeight += h; m->MinMaxInfo->MaxHeight += h;
    return 0;
  }
  case MUIM_Setup:
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    d = INST_DATA(cl, obj);
    theme_setup(obj);
    theme_handler(obj, &d->ehn);
    return TRUE;
  case MUIM_Cleanup:
    d = INST_DATA(cl, obj);
    theme_cleanup(obj);
    break;
  case MUIM_Draw:
    DoSuperMethodA(cl, obj, msg);
    if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) draw_all(INST_DATA(cl, obj), obj);
    return 0;
  case MUIM_Show: ((struct RData *)INST_DATA(cl, obj))->shown = 1; break;
  case MUIM_Hide: ((struct RData *)INST_DATA(cl, obj))->shown = 0; break;
  case MUIM_HandleEvent:
    return events(cl, obj, (struct MUIP_HandleEvent *)msg);
  case MUIM_CreateShortHelp: {
    /* bubble help for the tool under the pointer: its name in bold with its shortcut, then what it
       does in a sentence or two, wrapped to a readable width (squeezed tools show only icons, so
       the bubble is the only place their name appears) */
    struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
    static char help[600];
    char name[80], keys[32], body[400];
    const char *h, *open, *tip;
    const Item *it;
    int g, k;
    d = INST_DATA(cl, obj);
    k = hit(d, (WORD)m->mx, (WORD)m->my);
    if (k >= 200 && k < 200 + NQA) it = &g_undo[k - 200];
    else if (k < 0 || k >= 100 || !(it = nth_item(d, k, &g)) || !(it->help || it->label)) return 0;
    h = it->help ? tr(it->help) : "";
    keys[0] = 0;
    /* a shortcut written as "(Amiga-B)" or "(Shift-Return)" at the end of the help moves to the title line */
    if ((open = strrchr(h, '(')) && (!strncmp(open, "(Amiga-", 7) || !strncmp(open, "(Shift-", 7)) && open[strlen(open) - 1] == ')') {
      strncpy(keys, open, sizeof keys - 1); keys[sizeof keys - 1] = 0;
    } else open = NULL;
    if (it->label) { strncpy(name, tr(it->label), sizeof name - 1); name[sizeof name - 1] = 0; }
    else {
      /* no label: the help (up to the shortcut, or a colon) names it */
      size_t n = open ? (size_t)(open - h) : strlen(h);
      const char *colon = strchr(h, ':');
      if (colon && (size_t)(colon - h) < n) n = (size_t)(colon - h);
      while (n && h[n - 1] == ' ') n--;
      if (n >= sizeof name) n = sizeof name - 1;
      memcpy(name, h, n); name[n] = 0;
    }
    body[0] = 0;
    if ((tip = tip_for(it->id))) { strncpy(body, tr(tip), sizeof body - 1); body[sizeof body - 1] = 0; }
    else if (it->help) {
      /* the help says more than the name: it becomes the explanation */
      size_t n = open ? (size_t)(open - h) : strlen(h);
      while (n && h[n - 1] == ' ') n--;
      if (n >= sizeof body) n = sizeof body - 1;
      memcpy(body, h, n); body[n] = 0;
      if (!strcmp(body, name)) body[0] = 0;
      else if (!strncmp(body, name, strlen(name)) && body[strlen(name)] == ':') memmove(body, body + strlen(name) + 2, strlen(body + strlen(name) + 2) + 1);
    }
    if (it->kind == K_DROP && !it->label && body[0] == 0) strcpy(body, tr("Click to choose from a list."));
    snprintf(help, sizeof help, "\33b%s\33n%s%s", name, keys[0] ? "  " : "", keys);
    if (body[0]) {
      /* wrapped at about 46 characters a line, at spaces */
      char *o = help + strlen(help);
      const char *b = body;
      size_t room = sizeof help - (size_t)(o - help) - 2;
      int col = 0;
      *o++ = '\n'; room--;
      while (*b && room > 1) {
        const char *sp = b;
        int wl;
        while (*sp && *sp != ' ') sp++;
        wl = (int)(sp - b);
        if (col && col + 1 + wl > 46) { *o++ = '\n'; room--; col = 0; }
        else if (col) { *o++ = ' '; room--; col++; }
        while (b < sp && room > 1) { *o++ = *b++; room--; col++; }
        while (*b == ' ') b++;
      }
      *o = 0;
    }
    return (ULONG)help;
  }
  case MUIM_DeleteShortHelp: return 0;
  case MUIM_Ribbon_State: {
    struct MUIP_Ribbon_State *m = (struct MUIP_Ribbon_State *)msg;
    int restyle, relayout;
    d = INST_DATA(cl, obj);
    restyle = m->doc != d->st.doc || (m->doc && d->ngal == 0) || m->stylesig != d->st.stylesig;
    relayout = (m->on ^ d->st.on) & RBIT(RB_INTABLE);
    if (!restyle && !relayout && m->on == d->st.on && m->on2 == d->st.on2 && m->rowh == d->st.rowh && m->colw == d->st.colw && m->vmode == d->st.vmode && m->size == d->st.size && m->style == d->st.style && m->left == d->st.left &&
        m->right == d->st.right && m->before == d->st.before && m->after == d->st.after && m->font && !strcmp(m->font, d->font) &&
        !memcmp(m->wpon, d->st.wpon, sizeof m->wpon)) return 0;
    d->st = *m;
    if (m->font) { strncpy(d->font, m->font, sizeof d->font - 1); d->font[sizeof d->font - 1] = 0; }
    d->st.font = d->font;
    if (restyle) { gallery_styles(d); gallery_fonts(d); d->galfirst = 0; }
    if (_win(obj)) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
/* both classes made on first call (the ribbon and its list window's list); NULL unless both are */
struct MUI_CustomClass *ribbon_class(void)
{
  if (!rcls) { build_tabs(); rcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct RData), (APTR)dispatch); }
  if (!pcls) pcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct PData), (APTR)popdispatch);
  return rcls && pcls ? rcls : NULL;
}
/* at the end, after the window (and the list window with the application) has gone */
void ribbon_class_free(void)
{
  if (rcls) { MUI_DeleteCustomClass(rcls); rcls = NULL; }
  if (pcls) { MUI_DeleteCustomClass(pcls); pcls = NULL; }
}
