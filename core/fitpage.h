/*
 * fitpage.h - Layout > Page Setup > Fit One Page (WordCraft #6433): a document that runs a little onto
 * one more page brought back a page by tightening it as little as will do. The steps are tried in
 * order of how little they change the look: the space before and after paragraphs and the line
 * spacing a little tighter (94, 92, 90, 88 per cent: lines never closer than 94 per cent, paragraph
 * spacing not under 85), then the text half a point or a point smaller with spacing near normal or a
 * little tighter, then two points smaller; text already at 8.5 points or less is not made smaller.
 * Each step is laid out first on copies of the paragraphs; the first that saves a page is made, as
 * one undo step.
 *
 * It refuses (and changes nothing) when the document is on one page already, when page, column or
 * section breaks (other than continuous ones) force its pages, when the last page holds more than 12
 * lines or is more than 30 per cent full, while formatting changes are being tracked (Doc.rvtrack),
 * or when no step saves a page.
 */
#ifndef QUILL_FITPAGE_H
#define QUILL_FITPAGE_H

#include "doc.h"
#include "fonts.h"

enum { FIT_OK, FIT_ONEPAGE, FIT_BREAKS, FIT_TOOFULL, FIT_TRACKED, FIT_CANT, FIT_OOM };
/* the document fitted on a page less (laid out with fs). *before, *after (either may be NULL): its
   pages before and after. Returns FIT_ */
int fit_one_page(Doc *d, FontSet *fs, u32 *before, u32 *after);
/* (for the tests) the pages d takes, laid out with fs (0 if out of memory) */
u32 fit_pages(Doc *d, FontSet *fs);

#endif
