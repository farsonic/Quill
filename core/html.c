/*
 * html.c - HTML files (docs/wordcraft-gap.md WP28c). This file belongs to WP28c: only that package
 * edits it. Read and write .html (headings, lists, tables, links, pictures, bold/italic/code).
 * Fill in the row below (docx.h DocFormat: sniff, load, save, flags); docx.c's doc_formats[] lists it
 * already, and Open/Save As take it up from there. While load and save are NULL the row is left out.
 */
#include "docx.h"

const DocFormat fmt_html = { "Web Page", "html|htm", NULL, NULL, NULL, 0 };
