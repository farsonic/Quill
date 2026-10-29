/*
 * keep.h - parts of a .docx that Quill doesn't read, kept byte for byte with the document and
 * written back when it is saved (custom XML, the glossary, extended properties ...), so they aren't
 * lost. docx.c keeps some itself (see docx_read_parts_ext); a work package that reads a part it
 * used to leave alone takes it out again with keep_remove, and one that wants to keep a part the
 * document's text refers to (a chart, say) keeps it with keep_add and writes the relationship id
 * keep_add gives back.
 */
#ifndef QUILL_KEEP_H
#define QUILL_KEEP_H

#include "doc.h"

/* a kept part: its name in the package ("customXml/item1.xml"), its content type (NULL for .rels
   files, which the package's defaults cover), the data, and the relationship that leads to it:
   owner "" from the package itself (_rels/.rels), owner "word/document.xml" from the document's
   (word/_rels/document.xml.rels), owner NULL none (a part reached through another kept part's own
   .rels, which is kept with it verbatim); reltype is the relationship's Type, relid its Id */
typedef struct KeptPart {
  char *name, *ctype, *owner, *reltype, *relid;
  u8 *data; size_t len;
} KeptPart;

/* keeps a copy of data as part name. relid is the Id it had: kept if Quill's own parts can't be
   using it (else a new one, "rIdK" and a number). Returns the part's index in d->kept, -1 if out
   of memory or a part of that name is kept already */
int keep_add(Doc *d, const char *name, const char *ctype, const char *owner, const char *reltype, const char *relid,
             const void *data, size_t len);
int  keep_find(const Doc *d, const char *name);       /* its index, or -1 */
void keep_remove(Doc *d, const char *name);           /* no longer kept (a package reads it now) */
void keep_free(Doc *d);                               /* all of them (doc_free does this) */
/* whether a relationship id is one Quill's writer uses itself in that owner's .rels (rId1, rIdImg3...) */
int  keep_id_reserved(const char *relid);

#endif
