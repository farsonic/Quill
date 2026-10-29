/*
 * docx.h - Word documents (.docx, Office Open XML WordprocessingML, ECMA-376) in and out,
 * plain text in and out, and whole-file reading and writing.
 *
 * What is read: paragraphs and runs with their character formatting (theme fonts and colours
 * resolved); paragraph, character and table styles with inheritance; paragraph formatting, borders,
 * shading, tabs and drop caps; lists; tables (one level of nesting; deeper ones, and any in headers
 * and footers, are flattened); pictures, shapes, text boxes and groups (DrawingML); sections with
 * page setup, columns, line numbers and page borders; headers and footers (with a text watermark);
 * footnotes, endnotes, comments and tracked changes; title and author. Hyperlinks and content controls keep their
 * text; field codes are left out (results kept, page number fields become Quill's own).
 * docx_load reports what it had to leave out.
 */
#ifndef QUILL_DOCX_H
#define QUILL_DOCX_H

#include "doc.h"

/* what docx_load could not keep: tables nested too deep, pictures it couldn't read (old VML, OLE
   objects, missing files), header or footer content, references to notes or comments that
   aren't there, tracked changes (LOST_HEADERS and LOST_TRACKED are no longer set, now that
   both are read) */
enum { LOST_TABLES = 1, LOST_PICTURES = 2, LOST_HEADERS = 4, LOST_NOTES = 8, LOST_COMMENTS = 16, LOST_TRACKED = 32 };

/* a document from a .docx in memory; NULL if it isn't one (err says why). *lost: LOST_ bits */
Doc *docx_load(const u8 *data, size_t len, unsigned *lost, char *err, size_t errcap);
/* the document as a .docx (malloc'd, caller frees), *len its size; NULL if out of memory. Not
   re-entrant: it keeps its working state in statics while it runs. */
u8  *docx_save(Doc *d, size_t *len);
/* while saving, a cross-reference to a bookmark's page (PAGEREF) is written with the page this
   returns (from 1; 0 or no function: 1), so Word shows the right number before it updates fields.
   The page view sets it, with its layout as ctx */
extern int (*docx_pageref)(void *ctx, const char *bookmark);
extern void *docx_pageref_ctx;

/* plain text: UTF-8 if it is valid UTF-8, else Latin-1 (Amiga text); one paragraph per line */
Doc *txt_load(const u8 *data, size_t len);
/* latin1: write Latin-1 (for Amiga editors), else UTF-8 */
u8  *txt_save(Doc *d, int latin1, size_t *len);

u8  *file_read(const char *path, size_t *len);           /* NULL if it can't be read */
int  file_write(const char *path, const void *data, size_t len);   /* 0 if it couldn't be written */
/* by the file's contents: a zip is a .docx, an old .doc is refused, anything else is plain text;
   NULL if unreadable (err says why). *lost as for docx_load */
Doc *doc_load_file(const char *path, unsigned *lost, char *err, size_t errcap);
/* by the name's extension: .docx, else plain text in Latin-1; clears the modified flag on success,
   0 on failure */
int  doc_save_file(Doc *d, const char *path);

#endif
