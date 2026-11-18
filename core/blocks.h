/*
 * blocks.h - (WP23) templates, building blocks and inserting other files.
 *
 * Templates: a .dotx (or .dotm) is read as a document whose tplkind says so; opened, it becomes a new
 * untitled document attached to it (Doc.attached). Save as Template writes the template content type.
 * The attached template is w:attachedTemplate in settings.xml (with its relationship), and
 * w:linkStyles has its styles copied in when the document is opened.
 *
 * Building blocks: the glossary document (word/glossary/document.xml, kept by core/keep.c with the
 * parts it leads to) read into Doc.gloss: each w:docPart's name, gallery, category, behaviour and
 * description, its XML kept verbatim. Changing the list writes the glossary part afresh (keep.c
 * then saves it); a document that had none gets one (with a copy of its styles). A block's content
 * is read as a document of its own (the glossary part, with its relationships and media, read by the
 * .docx reader) and pasted where it goes in.
 */
#ifndef QUILL_BLOCKS_H
#define QUILL_BLOCKS_H

#include "doc.h"
#include "xml.h"

enum { TK_DOC, TK_DOTX, TK_DOTM };
enum { BB_CONTENT, BB_PARA, BB_PAGE };          /* how a block goes in: in the text, as paragraphs, on a page of its own */
typedef struct {
  char name[64];                    /* UTF-8 */
  char gallery[24];                 /* Word's: "docParts" (Quick Parts), "autoTxt" (AutoText), "coverPg"... */
  char category[48];                /* "General" */
  char desc[128];
  u8 behavior;                      /* BB_ */
  char *xml; size_t len;            /* the w:docPart as it is in the file */
} Block;
typedef struct Glossary {
  Block *b; int n;
  char *head, *tail;                /* the glossary document round its w:docParts (its root's namespaces...) */
} Glossary;

/* the main part's content type for the document (a template's for .dotx and .dotm) */
const char *blocks_main_ctype(const Doc *d);
/* galleries: Word's name for one of Quill's lists, and back (for people: Quick Parts, AutoText...) */
enum { GAL_QUICKPARTS, GAL_AUTOTEXT, GAL_COVERPAGES, GAL_HEADERS, GAL_FOOTERS, GAL_PAGENUMBERS, GAL_TEXTBOXES, GAL_TABLES,
       GAL_WATERMARKS, GAL_EQUATIONS, GAL_OTHER, GAL_N };
const char *gallery_word(int g);
const char *gallery_label(int g);               /* English, for tr() */
int  gallery_of(const char *word);              /* GAL_, GAL_OTHER for one Quill has no list for */

/* ---- reading and writing (core/docx.c's hooks) */
void blocks_read(Doc *d);                       /* the glossary kept with the document, read into d->gloss */
void blocks_read_setting(Doc *d, Xml *x);       /* a settings.xml element: w:attachedTemplate, w:linkStyles */
/* after reading: the template's kind from the main part's content type, the attached template's
   file from settings.xml's relationships (rels: their text, or NULL) */
void blocks_read_parts(Doc *d, const char *ct, size_t ctn, const char *setrels, size_t srn);
void blocks_write_settings(Doc *d, Buf *b);     /* w:attachedTemplate, w:linkStyles (and WP13's Styles pane settings) */
/* word/_rels/settings.xml.rels when there is an attached template (malloc'd; NULL none) */
char *blocks_settings_rels(const Doc *d, size_t *len);
void blocks_free(Doc *d);

/* ---- building blocks */
int  blocks_find(const Doc *d, const char *name, int gallery);   /* by name (case ignored) in a gallery (-1 any); -1 if none */
/* a block from src's text a..b (pictures, notes and comments left out), added to d's glossary (one of
   the same name and gallery replaced); its index, -1 if out of memory or nothing to keep */
int  blocks_add(Doc *d, Doc *src, Pos a, Pos b, const char *name, int gallery, const char *category, const char *desc, int behavior);
int  blocks_delete(Doc *d, int i);
/* a block's name, category, description or behaviour changed (gallery -1: kept); 0 if no such block */
int  blocks_set(Doc *d, int i, const char *name, int gallery, const char *category, const char *desc, int behavior);
/* the block's content as a document of its own; NULL if it can't be read */
Doc *blocks_doc(const Doc *owner, int i);
/* block i of owner's glossary put in at *at (moved past it): in the text, as paragraphs, or on its own
   page as its behaviour says. One undo step; 0 if nothing went in */
int  blocks_insert(Doc *d, Pos *at, const Doc *owner, int i);
/* the glossary part written afresh from d->gloss (done by the functions above) */
int  blocks_store(Doc *d);

/* ---- templates */
/* a template read from path becomes a new document based on it (untitled, attached to path) */
void template_to_new(Doc *d, const char *path);
/* d saved as a template (kind TK_DOTX or TK_DOTM) to path; 0 if it couldn't be */
int  template_save(Doc *d, const char *path, int kind);
/* the styles of src named in names (n of them; NULL: all) put into d, replacing those of the same
   name (Organizer, and Automatically update document styles): one undo step; how many */
int  doc_copy_styles(Doc *d, const Doc *src, const char *const *names, int n);
/* the attached template set (UTF-8 path; NULL or "": none), app.xml's Template with it */
void doc_attach_template(Doc *d, const char *path);

/* ---- another file at the caret: its text and formatting (any format doc_load_file reads; only the
   bookmark's text when one is named). One undo step; 0 (and err) if it couldn't be read */
int  doc_insert_file(Doc *d, Pos *at, const char *path, const char *bookmark, char *err, size_t errcap);

#endif
