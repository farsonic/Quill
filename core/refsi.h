/* refsi.h - (WP2) what core/refs.c and core/refsgen.c share; not for other files */
#ifndef QUILL_REFSI_H
#define QUILL_REFSI_H

#include "refs.h"
#include "field.h"
#include "layout.h"

/* paragraphs being made */
typedef struct { Para **p; u32 n, cap; int oom; } PList;
int   ri_add(PList *l, Para *p);              /* takes p (NULL: out of memory noted); 0 if out of memory */
void  ri_free(PList *l);
Para *ri_para(Doc *d, int style);             /* an empty paragraph in a style, its text in the style's look */
int   ri_text(Para *p, const char *s, const CharFmt *f);
/* the next field mark in q at or after off (q->len if none) */
u32   ri_next_mark(const Para *q, u32 off);
/* the record of the field starting at off of q (-1 if there is no start mark there), its name */
int   ri_field(const Doc *d, const Para *q, u32 off, char *name, size_t cap);
/* the end mark in q of the field starting at off (q->len if it ends in a later paragraph) */
u32   ri_field_end(const Para *q, u32 off);
/* paragraphs that are inside a block field's result (TOC, INDEX, TOA): 1 each; malloc'd (np bytes), NULL none */
u8   *ri_block_mask(Doc *d);
/* the page (from 0) a place is on, -1 if the layout can't say; its number as the document shows it */
long  ri_page(Doc *d, struct Layout *L, Pos p);
void  ri_pagenum(Doc *d, long page, char *out);
/* a sorting key for an index entry or citation in language l (accents folded, case ignored) */
void  ri_fold(int l, const char *s, char *out, size_t cap);
/* paragraphs after the one block field updated last: where the layout's paragraphs moved (see ri_page) */
extern u32 ri_moved_from; extern long ri_moved_by;
/* the text width of the section paragraph pi is in (columns: one column's) */
i32   ri_text_width(Doc *d, u32 pi);

#endif
