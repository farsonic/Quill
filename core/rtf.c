/*
 * rtf.c - RTF files (docs/wordcraft-gap.md WP28a). This file belongs to WP28a: only that package
 * edits it. Read and write .rtf: text, character and paragraph formatting, styles, lists, tables, pictures, fields, code pages.
 * Fill in the row below (docx.h DocFormat: sniff, load, save, flags); docx.c's doc_formats[] lists it
 * already, and Open/Save As take it up from there. While load and save are NULL the row is left out.
 */
#include "docx.h"

const DocFormat fmt_rtf = { "Rich Text Format", "rtf", NULL, NULL, NULL, 0 };
