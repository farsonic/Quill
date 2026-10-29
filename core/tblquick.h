/*
 * tblquick.h - WP21a: ready-made tables (Insert > Quick Tables) and tables from CSV text (Insert >
 * Excel Spreadsheet, as WordCraft makes it).
 *
 * Both put a whole table in at *at as one undo step (the caret goes into its first cell) and return
 * 1, or 0 inside a table cell's table or out of memory. Their words come from the caller, in UTF-8,
 * so they follow the interface language.
 */
#ifndef QUILL_TBLQUICK_H
#define QUILL_TBLQUICK_H

#include "doc.h"

/* the words the ready-made tables use (UTF-8; any may be NULL for the English one) */
typedef struct {
  const char *month[12];           /* January ... */
  const char *wday[7];             /* short weekday names, Monday first: Mo Tu ... */
  const char *week, *item, *amount, *total, *point, *needed, *subhead;
} QuickWords;
enum { QT_CAL1, QT_CAL2, QT_CAL3, QT_DOUBLE, QT_MATRIX, QT_TABULAR, QT_SUBHEADS1, QT_SUBHEADS2, QT_N };
/* ready-made table kind (QT_) width twips wide; the calendars show month (1-12) of year */
int tbl_quick(Doc *d, Pos *at, int kind, int year, int month, const QuickWords *w, i32 width);
/* the day of the week of a date, 0 Monday ... 6 Sunday; and how many days a month has */
int date_wday(int year, int month, int day);
int month_days(int year, int month);

/* CSV text (n bytes; UTF-8, or Latin-1 if it isn't valid UTF-8) as a table width twips wide: fields
   separated by commas, semicolons or tabs (whichever the first line has most of outside quotes),
   "quoted" fields may hold separators, line ends and "" for a quote. Numbers are set to the right.
   At most MAX_COLS columns and 4000 rows */
int tbl_from_csv(Doc *d, Pos *at, const char *csv, size_t n, i32 width);

#endif
