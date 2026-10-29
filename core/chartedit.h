/*
 * chartedit.h - making and changing charts (WP15b) and SmartArt (WP15c), on WP15a's model
 * (chart.h): a chart's data as a grid, its kind, layout, style and elements changed, and written as
 * a c:chartSpace part (or cx:chartSpace for Excel 2016's kinds) with its values both cached and in
 * an embedded workbook (so Word's Edit Data opens them); a SmartArt as an outline of items with a
 * layout, colours and a style, laid out by Quill and written as the five diagram parts (data,
 * layout, quick style, colours and the drawing every reader shows).
 *
 * A chart or SmartArt that came in a file is kept byte for byte (chartkeep.c) until it is changed;
 * a change writes its parts afresh from the model (what the model doesn't hold is then lost: a
 * chart's own text formatting, a SmartArt's per-shape changes). Each change is a new ObjPack put in
 * a new Doc.images entry (the old one stays for undo).
 */
#ifndef QUILL_CHARTEDIT_H
#define QUILL_CHARTEDIT_H

#include "chart.h"

/* ------------------------------------------------------------------ charts */
/* the data as a sheet: rows x cols cells of UTF-8 text (NULL empty). Row 0 holds the series' names
   (cell 0,0 unused), column 0 the categories, each other column a series' values. Scatter: column 0
   the X values; bubble: two columns a series, its Y values and its bubble sizes */
typedef struct { int rows, cols; char **cell; } CGrid;
CGrid *cgrid_new(int rows, int cols);
void   cgrid_free(CGrid *g);
const char *cgrid_get(const CGrid *g, int r, int c);     /* "" when empty or outside */
int    cgrid_set(CGrid *g, int r, int c, const char *utf8);   /* 0 out of memory or outside */
int    cgrid_resize(CGrid *g, int rows, int cols);       /* cells kept where they fit; 0 out of memory */
CGrid *chart_to_grid(const Chart *c);
/* the chart's series made afresh from the grid (its other settings kept); 0 out of memory */
int    chart_from_grid(Chart *c, const CGrid *g);
double chart_atof(const char *s);                        /* a number typed (CV_NONE if none) */

/* a new chart of a kind with Word's sample data (Categories 1-4, Series 1-3; pie: one series) */
Chart *chart_sample(int kind);
/* the kind changed, the data reshaped where the new kind needs it (pie: its first series) */
void   chart_set_kind(Chart *c, int kind);
int    chart_switch_rowcol(Chart *c);                    /* series become categories; 0 out of memory */
#define CHART_QUICK_LAYOUTS 10
void   chart_quick_layout(Chart *c, int n);              /* Word's Quick Layout n (0-9) */
/* Add Chart Element's choices (the list chart_element_names gives, in this order) */
enum { CEL_XTITLE, CEL_YTITLE, CEL_TITLE_NONE, CEL_TITLE_ABOVE, CEL_LABELS_NONE, CEL_LABELS_SHOW, CEL_HGRID, CEL_VGRID,
       CEL_LEGEND_NONE, CEL_LEGEND_RIGHT, CEL_LEGEND_TOP, CEL_LEGEND_LEFT, CEL_LEGEND_BOTTOM, CEL_N };
extern const char *const chart_element_names[CEL_N];
void   chart_add_element(Chart *c, int which);
int    chart_element_on(const Chart *c, int which);      /* the choice is how the chart is now */
int    chart_is_ex(int kind);                            /* written as a chartex part */

/* the chart part (c:chartSpace, or cx:chartSpace for chart_is_ex kinds); embed: it refers to its
   workbook through relationship rId1. 0 out of memory */
int    chart_write_xml(const Chart *c, int embed, Buf *o);
/* the workbook holding its data (an .xlsx: Sheet1 laid out as chart_to_grid's grid) */
int    chart_write_xlsx(const Chart *c, Buf *o);
/* a pack for the chart (its part, the part's relationships and the workbook; the theme, font and the
   colour and style parts of old, the pack it replaces, kept). The pack holds its own copy of c. NULL
   out of memory */
ObjPack *chart_pack_make(const Chart *c, const ObjPack *old);

/* a new Doc.images entry for a pack (kind OBJ_CHART or OBJ_SMARTART, in line, cx x cy twips); it
   takes pk (freed on failure). Its index, -1 out of memory */
int    doc_add_graphic(Doc *d, ObjPack *pk, int kind, i32 cx, i32 cy);

/* ------------------------------------------------------------------ SmartArt */
enum { SAC_LIST, SAC_PROCESS, SAC_CYCLE, SAC_HIERARCHY, SAC_RELATIONSHIP, SAC_MATRIX, SAC_PYRAMID, SAC_N };
extern const char *const sa_category_names[SAC_N];
int    sa_layout_count(void);
const char *sa_layout_name(int k);                       /* "Basic Block List" */
const char *sa_layout_desc(int k);                       /* what it is for */
int    sa_layout_cat(int k);                             /* SAC_ */
#define SA_COLORS 7                                      /* Accent 1 .. Accent 6, Colorful */
extern const char *const sa_color_names[SA_COLORS];
#define SA_STYLES 5                                      /* Simple Fill, White Outline, Subtle, Moderate, Intense Effect */
extern const char *const sa_style_names[SA_STYLES];

#define SA_MAX_ITEMS 200
#define SA_MAX_LEVEL 8
typedef struct { char *text; u8 level; } SAItem;          /* UTF-8; level 0 the top */
typedef struct {
  u8 layout, colors, style, rtl;
  int n; SAItem *it;
} SAModel;
SAModel *sa_sample(int layout);                          /* a new graphic with sample text ("[Text]" items) */
SAModel *sa_copy(const SAModel *m);
void     sa_free(SAModel *m);
/* the model of a SmartArt (its data part's items and levels, layout, colours, style, direction);
   NULL if it has no data part. A layout Quill doesn't have becomes the first of its category */
SAModel *sa_from_pack(const ObjPack *pk);
/* editing the outline (the text pane, Add Shape, Promote...). Each returns the index of the item
   to select afterwards, -1 if nothing changed */
enum { SA_AFTER, SA_BEFORE, SA_ABOVE, SA_BELOW };
int  sa_add(SAModel *m, int i, int where, const char *text);   /* i -1: at the end */
int  sa_delete(SAModel *m, int i);                       /* its items under it move up a level */
int  sa_set_text(SAModel *m, int i, const char *text);
int  sa_promote(SAModel *m, int i);
int  sa_demote(SAModel *m, int i);
int  sa_move(SAModel *m, int i, int down);               /* with the items under it, past its neighbour */
void sa_normalise(SAModel *m);                            /* levels that follow on, text cleaned */
/* the parts written for it (cx x cy twips): data (drawing relId drawrel), layout, quick style, colours, drawing */
int  sa_data_xml(const SAModel *m, const char *drawrel, Buf *o);
int  sa_layout_xml(const SAModel *m, Buf *o);
int  sa_quickstyle_xml(const SAModel *m, Buf *o);
int  sa_colors_xml(const SAModel *m, Buf *o);
int  sa_drawing_xml(const SAModel *m, i32 cx, i32 cy, Buf *o);
/* a pack for it (old: the one replaced, its theme and font kept). NULL out of memory */
ObjPack *sa_pack_make(const SAModel *m, i32 cx, i32 cy, const ObjPack *old);

#endif
