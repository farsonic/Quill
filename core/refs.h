/*
 * refs.h - (WP2) captions, tables of contents and figures, the index, the table of authorities and
 * cross-references, built on the field model (core/field.h). Written from Word's documented behaviour
 * (ECMA-376 part 1, 17.16.5: SEQ, STYLEREF, REF, NOTEREF, PAGEREF, TOC, TC, INDEX, XE, TOA, TA).
 *
 * THE MODEL
 *  - A caption is a paragraph in the "caption" style: the label, a SEQ field counting that label
 *    ("Figure { SEQ Figure \* ARABIC }"), with a chapter number before it when asked
 *    ("{ STYLEREF 1 \s }-{ SEQ Figure \* ARABIC \s 1 }"), then its text. The labels and how each is
 *    numbered are settings.xml's w:captions (RefsInfo), so they go with the document as in Word.
 *  - Tables of contents and figures (TOC), the index (INDEX) and tables of authorities (TOA) are
 *    "block fields": a field whose result is whole paragraphs, its start mark at the start of the
 *    first and its end mark at the end of the last. Updating one (Update Fields, Update Table...)
 *    builds the paragraphs afresh from the document and puts them between the marks. A table of
 *    contents' or figures' entries link to hidden "_Toc" bookmarks on their paragraphs and show
 *    their pages with page-reference placeholders, so the numbers are always right; the index's and
 *    authorities' page numbers are worked out from the layout when they are updated.
 *    An old table of contents (Quill's own run of "toc N" paragraphs, before WP2) becomes such a
 *    field the first time it is updated.
 *  - Entries are marked with fields that have no result: XE (index), TA (authorities), TC (contents).
 *    They show as their codes while formatting marks are shown (as Word's hidden text does).
 *  - Cross-references are REF, NOTEREF and PAGEREF fields to bookmarks: hidden _Ref bookmarks made on
 *    the caption, heading, numbered paragraph or note reference (Word's way), with every one of
 *    Word's kinds (text, number, page, above/below, number without and with context).
 * Quill keeps one bookmark per character: a reference needing a stretch another bookmark already
 * covers in part reuses or moves only the _Toc ones (refs_target); otherwise it says it can't.
 */
#ifndef QUILL_REFS_H
#define QUILL_REFS_H

#include "doc.h"
#include "xml.h"

struct Layout;
struct FieldEnv;

/* ---- languages (the index's sorting and headings, the words fields put in) */
enum { RL_EN, RL_DE, RL_FR, RL_ES, RL_IT, RL_N };
extern int refs_lang;                         /* the default (the interface's language; RL_EN) */
int  refs_lang_lcid(int l);                   /* 1033, 1031, 1036, 3082, 1040 */
int  refs_lang_of_lcid(int lcid);             /* back (RL_EN for others) */
const char *refs_word(int l, int w);          /* a word in language l (UTF-8): RW_ */
enum { RW_ABOVE, RW_BELOW, RW_ONPAGE, RW_SEE, RW_SEEALSO, RW_PASSIM, RW_NOTOC, RW_NOTOF, RW_NOINDEX, RW_NOTOA,
       RW_CONTENTS, RW_INDEX, RW_AUTHORITIES, RW_NOSTYLE, RW_SYMBOLS, RW_N };

/* ---- the document's caption labels and authority categories (Doc.refs) */
enum { CP_BELOW, CP_ABOVE };
enum { CN_ARABIC, CN_ALPHA, CN_ALPHA_UP, CN_ROMAN, CN_ROMAN_UP, CN_N };     /* 1 2 3, a b c, A B C, i ii iii, I II III */
enum { CSEP_HYPHEN, CSEP_PERIOD, CSEP_COLON, CSEP_EMDASH, CSEP_ENDASH, CSEP_N };
typedef struct {
  char name[40];              /* UTF-8: "Figure" */
  u8 pos;                     /* CP_ */
  u8 fmt;                     /* CN_ */
  u8 chap;                    /* with the chapter's number */
  u8 heading;                 /* the chapter starts with Heading N (1-9) */
  u8 sep;                     /* CSEP_ between chapter and number */
  u8 nolabel;                 /* the label left out of the caption */
} CapLabel;
#define REFS_MAXLAB 24
#define REFS_MAXAC 8
#define REFS_NCAT 16
typedef struct {
  char name[64];              /* what is inserted ("Microsoft Word Table", "Quill Table", "Picture") */
  char label[40];             /* the caption label it gets */
  u8 on;
} AutoCap;
typedef struct RefsInfo {
  CapLabel lab[REFS_MAXLAB]; int nlab;   /* Figure, Table, Equation, then the document's own */
  u8 fromfile;                /* w:captions was read (so it is written back) */
  AutoCap ac[REFS_MAXAC]; int nac;       /* AutoCaption: inserting a table or picture adds a caption */
  char cat[REFS_NCAT][40];    /* the table of authorities' categories (UTF-8; "" Word's name) */
} RefsInfo;
RefsInfo *refs_info(Doc *d);                  /* Doc.refs, made with Word's three labels when first needed */
void refs_free(Doc *d);
int  refs_label(Doc *d, const char *name);    /* its index (case ignored), -1 none */
int  refs_add_label(Doc *d, const char *name);   /* a new label (or the one there): its index, -1 full */
int  refs_delete_label(Doc *d, const char *name);   /* not the built-in three: 0 */
const char *refs_category(Doc *d, int c);     /* category c (1-16) as shown: the document's name or Word's */
const char *refs_sep_text(int sep);           /* CSEP_ as text: "-" ".", ":" ... (UTF-8) */
/* .docx: settings.xml's w:captions (w:caption, w:autoCaptions) */
void refs_read_setting(Doc *d, Xml *x);
void refs_write_settings(Doc *d, Buf *b);

/* ---- captions */
/* the code of a caption's number for a label: "SEQ Figure \* ARABIC", with \s n when numbered by chapter */
void refs_seq_code(const CapLabel *l, char *out, size_t cap);
/* a caption for label l (index into RefsInfo) with text after its number (UTF-8, may be ""), as
   a paragraph of its own: above or below the table or the paragraph at *at (pos CP_; where -1: at
   the caret's paragraph, the empty one itself or a new one after it). Then every caption of the
   label numbered again. One undo step; *at gets the caption's end. 0 if it couldn't */
int  refs_insert_caption(Doc *d, Pos *at, int l, const char *text, int pos, const struct FieldEnv *e);
/* what the caption would show now ("Figure 3", "Table 2-1"), UTF-8 */
void refs_caption_preview(Doc *d, Pos at, int l, char *out, size_t cap);
/* every SEQ field of label (NULL: all) and the STYLEREF fields beside them brought up to date */
int  refs_update_seq(Doc *d, const char *label, const struct FieldEnv *e);
/* AutoCaption: when the document has one more table or picture than before (at the caret), its caption */
int  refs_autocaption(Doc *d, Pos at, int table, const struct FieldEnv *e);

/* ---- block fields: TOC, INDEX, TOA */
enum { BK_NONE, BK_TOC, BK_TOF, BK_INDEX, BK_TOA };
int  refs_block_kind(const char *code);       /* BK_ (BK_TOF: a TOC with \c or \a, a table of figures) */
/* the next block field of kind (BK_NONE: any) from paragraph from on: its record, its marks; -1 none */
int  refs_block_find(Doc *d, int kind, u32 from, Pos *st, Pos *en);
/* the block field whose marks are st..en made afresh (L: laid out, for the index's and
   authorities' pages; leader: LEAD_ for the entries' tab, -1 as they had it). One undo step;
   *en (if given) moved to its new end mark. 0 if it couldn't be */
int  refs_block_update(Doc *d, Pos st, Pos *en, struct Layout *L, int leader);
/* a new block field with code at *at (paragraphs of its own; title: a "TOC Heading" paragraph before
   it, NULL none), its result made. One undo step; *at gets its end. Its record, -1 if it couldn't be */
int  refs_block_insert(Doc *d, Pos *at, const char *code, const char *title, struct Layout *L, int leader);
/* the block field at st taken out with its paragraphs (and title, a TOC Heading paragraph just before) */
int  refs_block_remove(Doc *d, Pos st);
/* every block field of kind brought up to date (Update Index, Update Table...); how many */
int  refs_update_kind(Doc *d, int kind, struct Layout *L);
/* the leader the block field's entries use now (LEAD_), -1 if it has none */
int  refs_block_leader(Doc *d, Pos st, Pos en);
/* for field.c: a block field in the body updated by Update Fields */
int  refs_field_update(Doc *d, Pos st, Pos en, const struct FieldEnv *e);
/* for layout.c: field k is an entry mark with no result (XE, TC, TA) shown as its code with the marks */
int  refs_mark_field(const Doc *d, int k);
/* the table of contents' styles' looks (Word's Formats: From template, Classic, Distinctive, Fancy,
   Modern, Formal, Simple) given to its "toc 1-9" styles (kind BK_TOC), the table of figures' (BK_TOF)
   or the index's (BK_INDEX); 0 if out of memory */
enum { RF_TEMPLATE, RF_CLASSIC, RF_DISTINCTIVE, RF_FANCY, RF_MODERN, RF_FORMAL, RF_SIMPLE, RF_N };
int  refs_apply_format(Doc *d, int kind, int fmt);
/* the style for an entry: "toc N", "table of figures", "index N", "index heading", "table of
   authorities", "toa heading", "caption", "TOC Heading": made if the document hasn't got it */
enum { RS_TOC, RS_TOF, RS_INDEX, RS_INDEXHEAD, RS_TOA, RS_TOAHEAD, RS_CAPTION, RS_TOCHEAD };
int  refs_style(Doc *d, int kind, int level);

/* ---- marking entries */
/* text for a field code argument: quotes and backslashes escaped (and, for an index entry's levels,
   colons written \:) into out, without the quotes round it */
void refs_quote(const char *s, int colons, char *out, size_t cap);
/* an XE field after the selection a..b (or at a): main entry, subentry (may be ""), options: xref
   ("See ..." instead of a page; "" none), bookmark (a page range; "" none), bold, italic. One undo
   step; *a moves after it. Its record, -1 failed */
int  refs_mark_entry(Doc *d, Pos *at, const char *main, const char *sub, const char *xref, const char *bm, int bold, int italic);
/* Mark All: an XE after the first occurrence of what (case and whole word matched) in every paragraph
   of the body (not in tables of contents or the index); how many */
int  refs_mark_all(Doc *d, const char *what, const char *main, const char *sub, const char *xref, const char *bm, int bold, int italic);
/* AutoMark: entries from a concordance document (a two-column table: the text to find, its entry,
   empty for the text itself; or plain text, a tab between them): Mark All for each row. How many marked */
int  refs_automark(Doc *d, Doc *conc);
/* a TA field after a..b: long citation (may be ""), short citation, category 1-16 (one undo step). Its record or -1 */
int  refs_mark_citation(Doc *d, Pos *at, const char *longc, const char *shortc, int cat);
/* Mark All for a citation: every occurrence of the long citation and of the short one gets a TA (the
   first the long one, the others the short); how many */
int  refs_mark_all_citations(Doc *d, const char *longc, const char *shortc, int cat);
/* the next likely citation from p (Word's: "v.", "In re", "Id.", "Supra", "§", "Act", "U.S.C."...): its stretch; 0 none */
int  refs_next_citation(Doc *d, Pos p, Pos *a, Pos *b);
/* the long citations marked so far (UTF-8, malloc'd, sorted; n of them; free each and the array) */
char **refs_citations(Doc *d, int *n);

/* ---- cross-references */
enum { RT_NUMBERED, RT_HEADING, RT_BOOKMARK, RT_FOOTNOTE, RT_ENDNOTE, RT_LABEL };   /* RT_LABEL + n: caption label n */
enum { XK_TEXT, XK_PAGE, XK_NUMBER, XK_NUMNOCTX, XK_NUMFULL, XK_ABOVEBELOW,       /* text, page, number (relative) ... */
       XK_NOTENUM, XK_NOTEFMT,                                                    /* a note's number, as a note reference */
       XK_CAPTION, XK_LABELNUM, XK_CAPTEXT };                                     /* entire caption, label and number, text */
typedef struct { u32 para; u32 off; int note; char text[96]; u8 level; } RefTarget;   /* what a list line names */
/* what can be referred to: the numbered paragraphs, headings, bookmarks (not hidden ones), notes, or
   captions of label n (type RT_LABEL + n): up to max of them into t; how many */
int  refs_targets(Doc *d, int type, RefTarget *t, int max);
/* the kinds of reference a type offers, XK_ in Word's order into k; how many */
int  refs_kinds(int type, int *k);
/* a cross-reference at *at to target t of type (its hidden bookmark made if it has none): kind XK_,
   as a link (\h), with "above"/"below" (\p), numbers separated by sep ("" none, full context). One
   undo step; *at after it. Its record, -1 if it can't be (the stretch has another bookmark) */
int  refs_insert_xref(Doc *d, Pos *at, int type, const RefTarget *t, int kind, int link, int above, const char *sep, const struct FieldEnv *e);
/* a bookmark covering exactly a..b of paragraph pi (exact; else any in the paragraph, made over the
   whole of it if none): the one there, a _Toc one moved, or a new one named prefix + number; its name
   into name (64 bytes). 0 if another bookmark is in the way */
int  refs_target(Doc *d, u32 pi, u32 a, u32 b, int exact, const char *prefix, char *name);

/* ---- for field.c: REF with \n \r \w \p \d, NOTEREF, STYLEREF, PAGEREF \p; -1 when it isn't one of these */
int  refs_eval(Doc *d, const char *name, const char *flat, Pos at, const struct FieldEnv *e, char *out, size_t cap);
/* paragraph pi's list number: as shown (RN_PLAIN, trailing periods off), with its levels above (RN_FULL), as
   seen from paragraph from (RN_REL), or digits and separators only (RN_SUPPRESS); "" not numbered */
enum { RN_PLAIN, RN_FULL, RN_REL, RN_SUPPRESS };
void refs_para_number(Doc *d, u32 pi, int mode, u32 from, char *out, size_t cap);
/* the plain text of paragraph pi's bytes a..b (field results in, marks, pictures and codes out) */
void refs_plain(const Doc *d, u32 pi, u32 a, u32 b, char *out, size_t cap);

#endif
