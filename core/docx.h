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
 * text; fields keep their codes and results (core/field.h; page number fields become Quill's own).
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

/* plain text: its encoding (TXT_ in doc.h) from a byte order mark (UTF-8, UTF-16LE/BE), else UTF-16
   by its zero bytes, else UTF-8 if it is valid UTF-8, else Latin-1/Windows-1252 (Amiga and Windows
   text); *skip the mark's length */
int  txt_encoding(const u8 *data, size_t len, size_t *skip);
/* plain text read in its encoding (kept in Doc.txtenc for Save); one paragraph per line */
Doc *txt_load(const u8 *data, size_t len);
/* the text in encoding enc (TXT_, with TXT_BOM: a byte order mark first) */
u8  *txt_save_enc(Doc *d, int enc, size_t *len);
/* latin1: write Latin-1 (for Amiga editors), else UTF-8 */
u8  *txt_save(Doc *d, int latin1, size_t *len);

/* a WordprocessingML number: an integer, or a Strict measure with a unit ("72pt", "1in", "2.5cm", "25mm")
   in twips, a percentage in fiftieths of a per cent; 0 if s is NULL or has no digits */
int  docx_num(const char *s, i32 *out);

u8  *file_read(const char *path, size_t *len);           /* NULL if it can't be read */
int  file_write(const char *path, const void *data, size_t len);   /* 0 if it couldn't be written */
/* the file formats Quill reads and writes (docx.c's doc_formats[]: .docx, the old .doc, plain text,
   and a row for each of WP28a-d's, filled in by its own core file). A format: its name for people,
   its name endings ("docx", or "htm|html": the first is the one a save adds), a test of a file's
   first bytes (NULL: known by the name only), how it is read and written (NULL: it can't be), and DF_
   flags. A row whose load and save are both NULL is left out everywhere */
enum { DF_NATIVE = 1,           /* Save writes back to it without asking: nothing is lost */
       DF_TEXT = 2 };           /* the last resort for files nothing else recognises */
typedef struct DocFormat {
  const char *name;
  const char *exts;
  int  (*sniff)(const u8 *data, size_t len);
  Doc *(*load)(const u8 *data, size_t len, unsigned *lost, char *err, size_t errcap);
  u8  *(*save)(Doc *d, size_t *len);
  unsigned flags;
} DocFormat;
extern const DocFormat *const doc_formats[];
extern const int doc_nformats;
/* the format a file name's ending names that can be read (saving 0) or written (saving 1); NULL if none */
const DocFormat *doc_format_for_name(const char *path, int saving);
/* an AmigaDOS pattern of every name ending that can be read (saving 0) or written: "#?.(docx|txt)" */
void doc_format_pattern(int saving, char *out, size_t cap);

/* read by its contents (each format's sniff in table order), else by its name's ending, else as plain
   text; NULL if unreadable (err says why). *lost as for docx_load */
Doc *doc_load_file(const char *path, unsigned *lost, char *err, size_t errcap);
/* written in the format its name's ending names, else as plain text (in Doc.txtenc); clears the modified
   flag on success, 0 on failure */
int  doc_save_file(Doc *d, const char *path);
/* while doc_load_file reads a file: its path (NULL otherwise), so a format can find the files it
   refers to (an HTML or Markdown file's pictures) */
extern const char *doc_load_path;

/* ---- for the work packages' .docx parts (see core/docx.c's *_ext hooks) */
/* what docx_save is building: a package's write hook adds its parts with docx_out_part */
typedef struct DocxOut DocxOut;
/* a part to write: its name, content type (NULL: none needed, a .rels), the relationship to it (owner ""
   from the package, "word/document.xml" from the document, NULL none; its type and id) and its bytes
   (copied). 0 if out of memory */
int docx_out_part(DocxOut *o, const char *name, const char *ctype, const char *owner, const char *reltype, const char *relid,
                  const void *data, size_t len);
/* a relationship of word/settings.xml (word/_rels/settings.xml.rels, written once with every package's
   entries: WP23's attached template, WP4's mail-merge data source). external: TargetMode="External".
   0 if out of memory */
int docx_out_settings_rel(DocxOut *o, const char *relid, const char *reltype, const char *target, int external);

#endif
