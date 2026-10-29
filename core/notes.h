/*
 * notes.h - WP22b: footnote and endnote options (Word's Footnote and Endnote window) and the
 * document's own note separators.
 *
 * Options (NotePr, doc.h) come from settings.xml's w:footnotePr/w:endnotePr (the document's:
 * Doc.fnpr, Doc.enpr) and from each section's w:sectPr (SectFmt.fnpr, enpr): where footnotes go
 * (the page's foot or just below its text) and endnotes (the end of each section or of the
 * document), the numbers' format, the first number, and whether numbering starts again each
 * section or (footnotes) each page. A section's own settings win over the document's, the
 * document's over Word's defaults.
 *
 * Separators: footnotes.xml and endnotes.xml start with special notes: the separator (the short
 * line above the notes), the continuation separator (the long line above a note continued from the
 * page before) and the continuation notice (words under a note that goes on overleaf). A document
 * may have its own: no line, words, a different paragraph. Quill keeps each as its paragraphs and
 * which line it holds, draws it, and writes it back.
 */
#ifndef QUILL_NOTES_H
#define QUILL_NOTES_H

#include "doc.h"
#include "xml.h"

enum { NPOS_PAGEBOTTOM, NPOS_BENEATHTEXT, NPOS_SECTEND, NPOS_DOCEND };       /* NotePr.pos */
enum { NNF_DECIMAL, NNF_LOWER_LETTER, NNF_UPPER_LETTER, NNF_LOWER_ROMAN, NNF_UPPER_ROMAN, NNF_SYMBOL, NNF_OTHER, NNF_N };
enum { NRS_CONT, NRS_SECTION, NRS_PAGE };                                   /* NotePr.restart */

/* the options in force for footnotes (end: endnotes) in section s (NULL: the last, plain one): every
   field filled in (out->set has every bit) */
void notes_eff(const Doc *d, const SectFmt *s, int end, NotePr *out);
/* number n (1...) as format fmt (NNF_) writes it: "3", "c", "C", "iii", "III", "\xE2\x80\xA1" (the
   symbols: * dagger double-dagger section, then doubled...). out needs 24 bytes */
void notes_label(int fmt, u32 n, char *out);
/* the w:numFmt names and back (unknown names: NNF_OTHER) */
const char *notes_fmt_name(int fmt);
int  notes_fmt_parse(const char *name);

/* .docx: a w:footnotePr or w:endnotePr just started in x read into np (to its end; the special
   notes' w:footnote/w:endnote ids in settings.xml's are left to the caller's defaults); and np written
   as element elem ("footnotePr"), with inner (the settings' note ids, or NULL) after the options.
   Nothing is written when np sets nothing and inner is NULL */
void notes_read_pr(Xml *x, NotePr *np);
void notes_write_pr(Buf *b, const NotePr *np, int end, const char *elem, const char *inner);

/* the separators: which (NSEP_), for footnotes or (end) endnotes */
enum { NSEP_SEP, NSEP_CONT, NSEP_NOTICE, NSEP_N };
enum { NSF_LINE = 1, NSF_LONG = 2 };       /* the note holds w:separator (a short line) / w:continuationSeparator (a long one) */
typedef struct NoteSeps {
  u8 have[2][NSEP_N];       /* read from the document (else Word's own is meant) */
  u8 flags[2][NSEP_N];      /* NSF_ */
  Story text[2][NSEP_N];    /* its paragraphs as read (their formatting; the line itself isn't text) */
} NoteSeps;
/* what separator which looks like: NSF_ flags (Word's: the separator a short line, the continuation
   separator a long one, no notice), and *text its paragraphs when they hold words (else NULL) */
int  notes_sep(const Doc *d, int end, int which, const Story **text);
/* the separator which of footnotes (end: endnotes) set as read: flags and paragraphs (taken over) */
int  notes_sep_set(Doc *d, int end, int which, int flags, Story *text);
/* whether the document has a separator of its own (one that isn't Word's default) */
int  notes_sep_own(const Doc *d, int end, int which);
void notes_free(Doc *d);                  /* the separators (doc_free does this) */

/* the Footnote and Endnote window's Apply: np's fields (NPS_ bits in np->set) for footnotes (end:
   endnotes) given to the section paragraph para is in (whole: to the document, every section's own
   settings of those fields dropped). Positions are always the document's (as in Word). Undoable for
   the sections; the paragraphs with notes are laid out again. 0 if out of memory */
int  notes_apply(Doc *d, u32 para, int end, int whole, const NotePr *np);

#endif
