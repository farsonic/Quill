/*
 * edcmd.h - editing commands a front end runs on a document and its selection: insert a table or a
 * picture, change a table's rows and columns, breaks and sections, footnotes, fields and text. The
 * Amiga's page view (amiga/view.c, MUIM_Quill_Command) does each of these inline with its own idea
 * of the selection; the classic Mac's front end (mac/) needed the same document work, so it is here
 * once, portable and tested natively (tests/test_wpmac.c), each command one undo step. A front end
 * keeps its selection as an ESel and draws the document again when a command says it changed it.
 */
#ifndef QUILL_EDCMD_H
#define QUILL_EDCMD_H

#include "doc.h"

/* the selection: from anchor to caret (equal: just the caret) */
typedef struct { Pos anchor, caret; } ESel;
Pos  es_lo(const ESel *s);
Pos  es_hi(const ESel *s);

/* the width text has where paragraph pi is: its cell's, or the page's between the margins, less the
   paragraph's indents (at least half an inch) */
i32  ec_text_width(Doc *d, u32 pi);
/* text (UTF-8, '\n' a new paragraph) in place of the selection; the caret after it */
int  ec_text(Doc *d, ESel *s, const char *text);
/* a table of rows x cols at the caret (in place of the selection), as wide as the room there; the
   caret in its first cell */
int  ec_table(Doc *d, ESel *s, int rows, int cols);
/* the caret's table changed (ET_): rows and columns put in or taken out, the selected cells of a row
   merged, a merged cell split, the whole table deleted. 0 if the caret is not in a table or it can't be */
enum { ET_ROW_ABOVE, ET_ROW_BELOW, ET_COL_LEFT, ET_COL_RIGHT, ET_DEL_ROW, ET_DEL_COL, ET_DEL_TABLE, ET_MERGE, ET_SPLIT };
int  ec_table_op(Doc *d, ESel *s, int op);
/* a page break (column: a column break) at the caret: a new paragraph that starts a page or column */
int  ec_break(Doc *d, ESel *s, int column);
/* a section break at the caret: the paragraph before it ends a section like the one it is in, and the
   next section starts as start says (SB_NEXT a new page, SB_CONT on the same page, SB_EVEN, SB_ODD) */
int  ec_section(Doc *d, ESel *s, int start);
/* a picture (PNG or JPEG data, which the document takes) at the caret, no wider than the column:
   its Doc.images index, or -1 if it isn't a picture Quill reads (the data is freed then) */
int  ec_picture(Doc *d, ESel *s, u8 *data, u32 len);
/* a footnote (end: an endnote) at the caret, its text one paragraph of text (UTF-8): its index or -1 */
int  ec_note(Doc *d, ESel *s, int end, const char *text);
/* a field (its code, "DATE \@ \"d MMMM yyyy\"", "PAGE") at the caret, worked out now */
int  ec_field(Doc *d, ESel *s, const char *code);

/* Layout: the whole document's page (Word's Page Setup for the whole document): size and margins,
   twips; sections set up like the document follow it. 1 if anything changed (not in the undo
   history, as on the Amiga) */
int  ec_page_setup(Doc *d, i32 width, i32 height, i32 top, i32 bottom, i32 left, i32 right);
/* the caret's section in cols columns (1 to 3), one undo step; 0 in a table's section */
int  ec_columns(Doc *d, ESel *s, int cols);
/* line numbers (LN_) in every section, one undo step */
int  ec_line_numbers(Doc *d, ESel *s, int ln);
/* the selected paragraphs' PM_ fields in mask set from pf (indents, spacing...), one undo step */
int  ec_para(Doc *d, ESel *s, unsigned mask, const ParaFmt *pf);

/* measurements as people type them: EU_ units; "1.25", "1,25" (a comma for the point), "-0.5"; at
   most two decimals. ec_measure: 1 and *tw, or 0 if it isn't a number */
enum { EU_INCH, EU_CM, EU_PT };
int  ec_measure(const char *s, int unit, i32 *tw);
/* tw in the unit, two decimals at most, trailing zeros left out ("1.25", "2", "-0.5") */
void ec_measure_text(i32 tw, int unit, char *out, int cap);

#endif
