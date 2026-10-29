/*
 * charfx.h - WP9 and WP10's core helpers (docs/wordcraft-gap.md): run properties kept verbatim,
 * enclosed characters' field codes, the numbering formats WP10 adds, and Sort Text.
 * This file belongs to WP9/WP10 (helper "charpara").
 */
#ifndef QUILL_CHARFX_H
#define QUILL_CHARFX_H

#include "doc.h"
#include "xml.h"

/* ---- run properties Quill doesn't model but keeps (CharFmt.rx): the exact XML of a run's w14 text
   effects (w14:glow, textOutline, ligatures...) and w:effect, written back after the properties
   Quill writes itself. The texts live in one table for the whole program (so runs copied between
   documents keep them) and are only ever added to. rx_add: the number for that text (the same
   text gives the same number), 0 for an empty one or when out of room; rx_text: a number's text
   ("" for 0 or a bad number) */
u16  rx_add(const char *xml, size_t n);
const char *rx_text(u16 rx);
/* rx's text with every element named name (with its prefix, "w14:ligatures") taken out, and add
   (if not NULL or empty) appended: the number of the result (0: nothing left) */
u16  rx_edit(u16 rx, const char *name, const char *add);

/* rx with a typography setting Word 2010 keeps in w14 (TY_): standard ligatures or none, old-style or
   lining numbers; the number of the result */
enum { TY_LIG_STD, TY_LIG_NONE, TY_NUM_OLD, TY_NUM_LINING };
u16  rx_typography(u16 rx, int kind);

/* ---- enclosed characters, as Word stores them: a field eq \o\ac(SYMBOL,TEXT), with "\* enlarge"
   when the symbol is enlarged (else the text is shrunk). The symbols by EN_ (UTF-8) */
const char *enclose_symbol(int shape);
/* a field code: 1 if it is an enclosure, its shape (EN_), whether enlarged and its text (UTF-8, at
   most cap - 1 bytes) */
int  enclose_parse(const char *instr, int *shape, int *enlarge, char *text, size_t cap);
/* the field code for text enclosed so (out at least 64 + strlen(text) bytes) */
void enclose_code(char *out, size_t cap, int shape, int enlarge, const char *text);
/* text holds an enclosure symbol (a field code's run of it) */
int  wp9_symbol_run(const char *s, size_t n);
/* .docx: enclosed characters written as Word does (a field with no result, the symbol and the text
   in runs of their own: the symbol larger when enlarged, the text smaller when shrunk); rpr is the
   run's w:rPr for f (rl bytes). 0 if f has no enclosure (nothing written) */
int  wp9_write_enclosed(Buf *o, const CharFmt *f, const char *rpr, size_t rl, const char *s, u32 n);
/* a bullet's font that .docx should be told (not a symbol font whose bullets were mapped to Unicode) */
int  wp10_bullet_font(const char *font);

/* ---- WP10's special characters in the layout: an optional hyphen U+00AD (no room, shown as a
   hyphen only where a line breaks at it), a nonbreaking hyphen U+2011 (drawn and measured as '-').
   wp10_special: s[0..n) holds one; wp10_plain: s with optional hyphens left out and nonbreaking ones
   made '-', into out (n bytes is enough): its length */
int  wp10_special(const char *s, u32 n);
u32  wp10_plain(const char *s, u32 n, char *out);
/* WP9: an enclosed box's width (fine units) for its text's w: room for the circle or square round
   the text (the text shrunk, or the symbol enlarged) */
i32  wp9_enclosed_w(i32 w, const CharFmt *f);

/* ---- list counters in WP10's numbering formats (NF_DECIMAL_ZERO 01, NF_ORDINAL 1st,
   NF_CARDINAL_TEXT One, NF_ORDINAL_TEXT First): out at least 24 bytes; 0 if fmt isn't one of them */
int  nf_counter(char *out, u32 n, int fmt);
/* a numbering format's .docx name (w:numFmt), and back (-1 if unknown) */
const char *nf_name(int fmt);
int  nf_from_name(const char *name);

/* ---- Sort Text (Home > Sort): paragraphs pa..pb sorted by up to three keys, each the whole
   paragraph (field 0) or its field-th field (fields separated by sep: '\t', ',' or another
   character), compared as text, numbers or dates, ascending or descending. header: the first
   paragraph stays where it is. casesens: capitals before small letters (else case is ignored).
   Equal ones keep their order. One undo step; 0 if nothing could be sorted (a table, one paragraph) */
enum { SORT_TEXT, SORT_NUMBER, SORT_DATE };
typedef struct { int field; int type; int desc; } SortKey;
typedef struct { SortKey key[3]; int nkeys; char sep; int header; int casesens; } SortOpt;
int  doc_sort_text(Doc *d, u32 pa, u32 pb, const SortOpt *o);
/* what a sort compares (for tests): a number field's value (thousandths), a date's day number
   (0: none) */
long sort_number(const char *s, u32 n, int *found);
long sort_date(const char *s, u32 n);

/* ---- WP9's run properties in .docx (docx_charfx.c), for core/docx.c's parse_rpr and w_rpr.
   wp9_read_rpr: a start tag inside w:rPr: 1 if it was WP9's (the caller skips the element), 2 if
   it was and has been read to its end (kept verbatim), 0 if not (w:u: its colour is taken, the
   caller reads the rest). wp9_rpr_diff: the CM_ bits of WP9's in which f differs from base.
   wp9_write_rpr: part 0 the effects (after w:dstrike), 1 shading (after w:bdr), 2 the kept ones
   (last). wp9_ulcolor_attr: w:u's colour attribute */
int  wp9_read_rpr(Xml *x, unsigned *set, CharFmt *cf);
unsigned wp9_rpr_diff(const CharFmt *f, const CharFmt *base);
void wp9_write_rpr(Buf *b, const CharFmt *f, const CharFmt *base, unsigned diff, int full, int part);
void wp9_ulcolor_attr(Buf *b, const CharFmt *f);

#endif
