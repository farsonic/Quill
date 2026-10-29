/*
 * template.h - the documents the File page offers to start from: a blank one, a sample that shows
 * Quill off, a letter, a resume and a report. Made in code (no files to install).
 */
#ifndef QUILL_TEMPLATE_H
#define QUILL_TEMPLATE_H

#include "doc.h"

enum { TPL_BLANK, TPL_WELCOME, TPL_LETTER, TPL_RESUME, TPL_REPORT, TPL_N };
/* a new document from a template; NULL if out of memory */
Doc *doc_template(int which);
/* its name, UTF-8 */
const char *template_name(int which);
/* a cover page before the document's text: title, subtitle, author and date (UTF-8, any may be "")
   set out in one of the designs (COVER_), the text after it starting a new page. Its paragraphs are
   the hidden bookmark "_Cover", so another design replaces it and doc_cover_remove finds it. One
   undo step; 0 if out of memory */
enum { COVER_SIMPLE, COVER_BANDED, COVER_SIDELINE, COVER_N };
int  doc_cover_page(Doc *d, int design, const char *title, const char *subtitle, const char *author, const char *date);
/* the cover page taken out (one undo step); 0 if there is none */
int  doc_cover_remove(Doc *d);

#endif
