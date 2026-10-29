/*
 * pdf.h - export a laid-out document as PDF.
 *
 * Every word is placed where the layout put it, in the TrueType font layout measured it with,
 * embedded in the file (so the PDF looks the same everywhere and breaks lines exactly as Quill
 * does). Text is encoded as Windows-1252, which covers Western European languages and the usual
 * typographic characters; others print as '?'. Coordinates stay integer twips (a 1/20 scale in the
 * page's matrix), so no floating point is needed here either.
 */
#ifndef QUILL_PDF_H
#define QUILL_PDF_H

#include "layout.h"

/* the whole document (the layout is brought up to date first); NULL if out of memory */
u8 *pdf_export(Layout *L, size_t *len);
/* the same, written to a file; 0 if it can't be */
int pdf_save_file(Layout *L, const char *path);

#endif
