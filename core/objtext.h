/*
 * objtext.h - WP18: a text box's text laid out with its direction (turned a quarter either way),
 * its anchoring (top, middle, bottom), its insets, and in a chain of linked text boxes, the part of
 * the chain's text that falls in each box. layout.c's lay_textbox calls tb_layout.
 */
#ifndef QUILL_OBJTEXT_H
#define QUILL_OBJTEXT_H

#include "layout.h"
#include "objfx.h"

typedef int (*TbPlain)(Layout *L, const Image *im, i32 w, Furn *f);
/* im's text laid out (by plain, at the width it gives) for a box cx wide (0: its own width): y from
   the box's top, as it would be drawn unturned (a turned box's text is laid out across its height
   and turned when drawn) */
int tb_layout(Layout *L, const Image *im, i32 cx, Furn *f, TbPlain plain);
/* the chain's boxes in order: the indexes of the objects (in the text) linked as link, up to max;
   how many */
int tb_chain(const Doc *d, int link, int *out, int max);
/* a number for a new chain, not used by any object */
int tb_new_link(const Doc *d);

#endif
