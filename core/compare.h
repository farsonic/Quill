/*
 * compare.h - WP6 (docs/wordcraft-gap.md): Compare and Combine.
 *
 * Compare: the differences between an original and a revised document, as tracked changes in the
 * revised one, named after the reviser (Word's Review > Compare > Compare, "show changes in a new
 * document"). Both are compared as they read with their own tracked changes accepted. The text is
 * split into words (spaces and punctuation each their own piece) or characters, and the two sequences
 * are lined up (paragraph by paragraph first, then patience diff with an exact LCS for small stretches):
 * what only the original has comes back as a deletion, what only the revised one has is marked as an
 * insertion, and text in both whose formatting differs is a formatting change.
 *
 * Combine: two reviewers' copies of a document put together (Review > Compare > Combine): the tracked
 * changes of both are kept with their authors, and whatever differs between them untracked is marked
 * as a change by the name given ("label unmarked changes with").
 *
 * Quill's model has no tracked paragraph marks, so a paragraph break only the original has shows as a
 * deleted line break (accepting every change gives the revised document exactly), and one only an
 * original's tracked insertion has (Combine) as an inserted line break.
 */
#ifndef QUILL_COMPARE_H
#define QUILL_COMPARE_H

#include "doc.h"

typedef struct CompareOpts {
  u8 chars;                 /* show changes at the character level (else whole words) */
  u8 formatting;            /* formatting differences are changes */
  u8 casechg;               /* case changes are changes */
  u8 whitespace;            /* differences in white space are changes */
  u8 tables;                /* tables' text compared (else the revised one's taken as it is) */
  u8 headers;               /* headers and footers compared */
  u8 notes;                 /* footnotes and endnotes compared */
  u8 fields;                /* fields compared (page fields, references) */
  u8 comments;              /* comments' places compared (else the revised document's kept) */
  char author[64];          /* the changes' author: the reviser (Compare), unmarked changes' (Combine) */
  char date[24];            /* their date, ISO 8601 ("" none) */
} CompareOpts;

void compare_defaults(CompareOpts *o);           /* Word's: words, everything compared */

/* rev becomes the comparison of orig and rev: its text and objects, with orig's differences as tracked
   changes (rev's own tracked changes accepted first). orig is not changed. Returns the number of changes
   marked, -1 if out of memory (rev may be half done then) */
int  doc_compare(Doc *orig, Doc *rev, const CompareOpts *o);
/* rev becomes orig and rev combined: both documents' tracked changes kept, their untracked differences
   marked by o->author. -1 if out of memory */
int  doc_combine(Doc *orig, Doc *rev, const CompareOpts *o);

/* a formatting change for text found in both with a different look: when set (by WP5's tracked formatting
   changes), called to record now's old look as a tracked change by rid in d (now is the run's format, to
   be changed: its formatting-change field set); 1 if it did. When NULL, such text is shown as deleted in its
   old look and inserted in its new one */
extern int (*compare_fmt_hook)(Doc *d, int rid, const CharFmt *old, CharFmt *now);

#endif
