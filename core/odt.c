/*
 * odt.c - ODT files (docs/wordcraft-gap.md WP28b). This file belongs to WP28b: only that package
 * edits it. Read and write .odt (styles, lists, tables, pictures, headers and footers, notes). ODT is a zip too: give this row a sniff that checks the mimetype entry (application/vnd.oasis.opendocument.text), as it is tried before .docx.
 * Fill in the row below (docx.h DocFormat: sniff, load, save, flags); docx.c's doc_formats[] lists it
 * already, and Open/Save As take it up from there. While load and save are NULL the row is left out.
 */
#include "docx.h"

const DocFormat fmt_odt = { "OpenDocument Text", "odt", NULL, NULL, NULL, 0 };
