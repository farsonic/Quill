/*
 * mathml.h - equations as MathML (WP28b, for .odt formula objects): Presentation MathML read into
 * Quill's equation model (core/math.h) and written from it. This file belongs to WP28b.
 *
 * Reading maps MathML's layout schemata onto OMML's structures: mfrac (bevelled, linethickness 0),
 * msub/msup/msubsup, munder/mover/munderover (an n-ary operator as base makes an n-ary structure, an
 * accent mover an accent, a lim/max/min base a lower or upper limit, a brace a group character, a
 * bar a bar), msqrt/mroot, fenced mrows and mfenced (delimiters, with their separators), function
 * application (U+2061) after a name, mtable (a matrix), menclose (a border box), mphantom,
 * mmultiscripts with prescripts; tokens mi/mn/mo join into runs, mtext becomes normal text; mstyle,
 * mpadded, semantics (its first child; annotations are skipped) and unknown elements give their
 * content. Writing gives the same elements, each run split into mi, mn and mo tokens.
 */
#ifndef QUILL_MATHML_H
#define QUILL_MATHML_H

#include "math.h"

/* x just after the START of a <math> element: the equation, read to its END (display="block" makes
   a display equation); NULL if out of memory */
MathEq *mathml_read(Xml *x);
/* the first <math> element in a whole XML document (a formula object's content.xml); NULL if none */
MathEq *mathml_parse(const char *xml, size_t n);
/* the equation as a <math> element with the MathML namespace declared on it */
void    mathml_write(Buf *o, const MathEq *e);

#endif
