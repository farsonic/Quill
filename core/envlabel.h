/*
 * envlabel.h - envelopes and labels (WP4): Word's envelope sizes and label products, an envelope as a
 * page (a document of its own, or a section at the start of a document) and a page of labels as a
 * table, for one address, a full page of the same label, or a mail merge (NEXT fields).
 * Printing (feed, trays) is left out: Quill makes PDFs.
 */
#ifndef QUILL_ENVLABEL_H
#define QUILL_ENVLABEL_H

#include "doc.h"

typedef struct { const char *name; i32 w, h; } EnvSize;          /* twips, the long side first */
extern const EnvSize env_sizes[]; extern const int env_nsizes;

typedef struct {
  const char *vendor, *product, *desc;
  i32 pagew, pageh;             /* the sheet */
  i32 top, side;                /* the first label's top and left edges from the sheet's */
  i32 w, h;                     /* a label */
  i32 hpitch, vpitch;           /* from one label's left (top) edge to the next one's */
  u8 cols, rows;
} LabelDef;
extern const LabelDef label_defs[]; extern const int label_ndefs;
extern const char *const label_vendors[]; extern const int label_nvendors;

/* the envelope's text: delivery and return addresses (UTF-8, '\n' between lines; return NULL or ""
   none), size (env_sizes index) */
typedef struct {
  int size;
  const char *delivery, *ret;
  i32 dleft, dtop;              /* where the delivery address goes (0: Word's places for the size) */
} EnvSpec;
/* a document holding the envelope (one page, landscape) */
Doc *env_doc(const EnvSpec *e);
/* the envelope added as its own section at the start of d (Word's Add to Document); 0 on failure.
   One undo step */
int  env_add_to_doc(Doc *d, const EnvSpec *e);
/* where an envelope's delivery address goes by Word's rules (about the middle, a little right) */
void env_delivery_place(int size, i32 *left, i32 *top);

/* a page of labels: text in every label (single 0), or only in the one at row, col (single 1); merge:
   the labels for a mail merge (each but the first starting with a NEXT field, empty otherwise; text
   ignored). A new document, NULL oom */
Doc *label_doc(const LabelDef *l, const char *text, int single, int row, int col, int merge);
/* Update Labels: the first label's contents copied into all the others, each starting with a NEXT
   field (one undo step); how many labels were filled, 0 if the document isn't a page of labels */
int  label_update(Doc *d);

#endif
