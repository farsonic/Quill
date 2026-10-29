/*
 * docbin.c - Word 97-2003 documents (.doc, an OLE compound file; docs/wordcraft-gap.md WP28d). This
 * file belongs to WP28d: only that package edits it. Until it reads them, such a file is refused with
 * a message saying how to convert it (the row's load below). WP7's encrypted .docx files are compound
 * files too: they are told apart from a .doc inside (an EncryptionInfo stream).
 */
#include <stdio.h>
#include "docx.h"

/* an OLE compound file: D0 CF 11 E0 A1 B1 1A E1 */
static int sniff_cfb(const u8 *b, size_t n) { return n >= 8 && b[0] == 0xD0 && b[1] == 0xCF && b[2] == 0x11 && b[3] == 0xE0; }
/* (not read yet: refused, saying what to do) */
static Doc *load_doc(const u8 *data, size_t len, unsigned *lost, char *err, size_t errcap)
{
  (void)data; (void)len; (void)lost;
  snprintf(err, errcap, "This is an old Word 97-2003 document (.doc). Save it as .docx in Word or LibreOffice first.");
  return NULL;
}
const DocFormat fmt_doc = { "Word 97-2003 Document", "doc", sniff_cfb, load_doc, NULL, 0 };
