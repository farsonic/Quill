/*
 * sdt.h - content controls (WP16): Word's w:sdt (structured document tags): rich text, plain text,
 * picture, check box, combo box, drop-down list, date picker, building block gallery, repeating
 * section (and its items), group, and the kinds Word makes itself (citation, bibliography, equation,
 * document part such as a table of contents or a cover page); and the legacy form fields of WP1's
 * field model (FORMTEXT, FORMCHECKBOX, FORMDROPDOWN with their w:ffData).
 *
 * THE MODEL (it mirrors the field model, core/field.h)
 *  - In the text a control is its content between two marks without width: a start mark
 *    U+107000 + k and an end mark U+10B000 + k (F4 87 xx xx and F4 8B xx xx), k being its record in
 *    the document's SdtSet. These code points are the top quarter of the field marks' range (field
 *    records stop at FIELD_MAX = 12288), so everything that already treats field marks as marks
 *    without width (layout, the caret, copying and pasting, the other formats' writers) treats
 *    controls the same; core/field.c leaves them out of what it counts as fields. No private range
 *    at U+F0000 and above is free otherwise (pictures, notes, comments, endnotes, fields and
 *    equations fill planes 15 and 16).
 *  - A control at block level (around paragraphs and tables), row level (around table rows) or
 *    cell level (around cells) has its start mark at the start of its first paragraph and its end
 *    mark at the end of its last one; its record says which level it is, and the .docx writer puts
 *    the w:sdt round the w:p, w:tbl, w:tr or w:tc elements those paragraphs make.
 *  - Records are only added to (as Doc.fields): a control whose properties change gets a new record
 *    and its marks are replaced (one undo step). What depends on the content is worked out from the
 *    content when saving: a check box's state (its symbol), a list's last value, whether the
 *    placeholder is showing (the content is the placeholder text).
 *  - Everything of w:sdtPr is kept: what Quill models (title, tag, id, lock, placeholder, temporary,
 *    showing placeholder, appearance and colour, the type and its settings, check box symbols,
 *    list items, date format, gallery, repeating section settings), w:rPr and w:dataBinding
 *    verbatim, and every other child verbatim; w:sdtEndPr verbatim. Bound custom XML parts are
 *    kept by core/keep.c.
 */
#ifndef QUILL_SDT_H
#define QUILL_SDT_H

#include "doc.h"

/* ---- the marks */
#define SDT_K0 0x3000                  /* (the field index the first control's mark would be) */
#define SDT_MAX 4096
#define IS_SSTART(t) ((u8)(t)[0] == 0xF4 && (u8)(t)[1] == 0x87)
#define IS_SEND(t)   ((u8)(t)[0] == 0xF4 && (u8)(t)[1] == 0x8B)
#define IS_SMARK(t)  (IS_SSTART(t) || IS_SEND(t))
void smark_utf8(char *out, int k, int end);      /* control k's start (end: its end) mark: 4 bytes and a NUL */
int  smark_index(const char *t);                 /* the control a mark names (t at its first byte) */

/* ---- records */
enum { SDT_RICH, SDT_TEXT, SDT_PICTURE, SDT_CHECK, SDT_COMBO, SDT_DROP, SDT_DATE, SDT_GALLERY,
       SDT_REPEAT, SDT_REPITEM, SDT_GROUP, SDT_DOCPART, SDT_CITATION, SDT_BIBLIO, SDT_EQUATION, SDT_N };
enum { SL_INLINE, SL_BLOCK, SL_ROW, SL_CELL };
enum { SLK_DEL = 1, SLK_EDIT = 2 };              /* Sdt.lock bits: can't be deleted (sdtLocked), contents can't be edited */
enum { SAP_BOX, SAP_TAGS, SAP_HIDDEN };          /* w15:appearance */
typedef struct { char *text, *value; } SdtItem;  /* a list entry: shown text, value (UTF-8) */
typedef struct Sdt {
  u8 type, level, lock;
  u8 depth;                 /* how many table cells it is inside (0: the body); a row or cell control: its table's */
  u8 showplc;               /* read with w:showingPlcHdr: the content is the placeholder (plctext) */
  u8 temporary;             /* w:temporary: removed when its content is edited */
  u8 multiline;             /* plain text: w:multiLine */
  u8 appearance, hasapp;    /* SAP_, and whether w15:appearance was given */
  u8 hascolor;              /* w15:color given */
  u8 nodelitem;             /* repeating section: w15:doNotAllowInsertDeleteSection */
  u8 dpunique;              /* gallery / document part: w:docPartUnique */
  u8 dplist;                /* gallery: w:docPartList (else w:docPartObj) */
  u8 richtag;               /* w:richText was written (rich text is also what no type element means) */
  u8 checked0;              /* check box: w14:checked as read (the state its symbol shows otherwise) */
  u32 color;                /* w15:color 0xRRGGBB */
  i32 id;                   /* w:id */
  char *alias, *tag;        /* title, tag (UTF-8; NULL none) */
  char *plcpart;            /* w:placeholder's w:docPart (the glossary entry), NULL none */
  char *plctext;            /* the placeholder text (what the content is while it shows), NULL none */
  char *rpr;                /* w:rPr of w:sdtPr, verbatim (the whole element) */
  char *binding;            /* w:dataBinding, verbatim (the whole element) */
  char *other;              /* every other child of w:sdtPr, verbatim, in order */
  char *endpr;              /* the children of w:sdtEndPr, verbatim */
  char *nsdecl;             /* namespace declarations its verbatim parts need (" xmlns:w15=..."), NULL none */
  SdtItem *items; int nitems;   /* combo box, drop-down list */
  char *lastvalue;          /* their w:lastValue as read */
  char *datefmt, *lid, *storeas, *calendar, *fulldate;   /* date: w:dateFormat, w:lid, w:storeMappedDataAs, w:calendar, w:fullDate */
  u32 chk_on, chk_off;      /* check box: the checked and unchecked symbols (Unicode) */
  char chk_onfont[32], chk_offfont[32];
  char *gallery, *category; /* building block gallery: w:docPartGallery, w:docPartCategory */
  char *sectitle;           /* repeating section: w15:sectionTitle */
} Sdt;

/* the document's controls (Doc.sdt, made when first needed) */
typedef struct SdtSet {
  Sdt *r; int n;
  u8 design;                /* Design Mode (not saved) */
  char *rootns;             /* the namespace declarations of the document part read (for verbatim XML) */
} SdtSet;
SdtSet *sdt_set(Doc *d);                         /* made if not there yet (NULL: out of memory) */
void sdt_free_all(Doc *d);                       /* (doc_free) */
int  sdt_add(Doc *d);                            /* a blank record (for reading); -1 if full */
int  sdt_new(Doc *d, int type, int level);       /* a new record with Word's defaults for type; -1 if full */
int  sdt_clone(Doc *d, int k);
void sdt_copy(Sdt *dst, const Sdt *src);        /* a deep copy (free it with sdt_rec_free) */                   /* a copy of record k as a new one; -1 */
Sdt *sdt_get(const Doc *d, int k);               /* NULL if k isn't a record */
int  sdt_count(const Doc *d);
void sdt_rec_free(Sdt *s);
const char *sdt_type_name(int type);             /* "Rich Text", "Check Box" ... (English, for tr()) */

/* ---- .docx (core/docx.c) */
/* a w:sdtPr element (the whole element's text) read into s (level and nsdecl left alone) */
void sdt_parse_pr(Sdt *s, const char *xml, size_t n);
/* w:sdtPr written for record k with this content (UTF-8 text of the content, for the state that
   depends on it); id: the w:id to write */
void sdt_write_pr(Buf *o, const Doc *d, int k, const char *content, i32 id);
/* the content's showing-placeholder state: 1 if the content text is its placeholder */
int  sdt_showing_plc(const Sdt *s, const char *content);

/* ---- where controls are (the paragraphs being edited: the body, or a story in its place) */
/* the innermost control round p (p just after its start mark up to just before its end mark):
   its record, its start and end marks' places; -1 if none. outer: skip that many inner ones */
int  sdt_at(Doc *d, Pos p, Pos *s, Pos *e);
int  sdt_at_depth(Doc *d, Pos p, int outer, Pos *s, Pos *e);
/* the end mark of the control whose start mark is at s; 0 if none */
int  sdt_end(Doc *d, Pos s, Pos *e);
/* the content's text (UTF-8, paragraphs joined by '\n', marks left out) into out */
void sdt_text(Doc *d, Pos s, Pos e, char *out, size_t cap);
void sdt_text_paras(Para *const *p, u32 np, Pos s, Pos e, char *out, size_t cap);   /* the same in a story */
/* the next control from p (dir > 0 after p, < 0 before it): its record and marks; -1 none */
int  sdt_next(Doc *d, Pos p, int dir, Pos *s, Pos *e);
/* the controls in the document, start marks in at (up to max): how many */
int  sdt_all(Doc *d, Pos *at, int max);

/* ---- edits (each one undo step; 0 / -1 if nothing was done) */
/* a new control of type over a..b (a == b: one with its placeholder text at a); at block level when
   a..b is whole paragraphs (or block is set); its record */
int  sdt_insert(Doc *d, Pos a, Pos b, int type, int block);
/* the control whose start mark is at s taken out, its content kept (keep) or with it */
int  sdt_remove(Doc *d, Pos s, int keep);
/* its content replaced by text (UTF-8, '\n' between paragraphs), formatted as its first character */
int  sdt_set_text(Doc *d, Pos s, const char *text);
/* a check box toggled (its symbol); 1 if done */
int  sdt_toggle(Doc *d, Pos s);
int  sdt_checked(Doc *d, Pos s);                 /* 1 checked, 0 not, -1 not a check box */
/* a drop-down's or combo box's entry i chosen */
int  sdt_choose(Doc *d, Pos s, int i);
/* a date picker's date (y, m 1-12, day): its text in its format and w:fullDate */
int  sdt_set_date(Doc *d, Pos s, int y, int m, int day);
/* the record of the control at s replaced by nr (a new record made from it); its marks swapped */
int  sdt_set_props(Doc *d, Pos s, const Sdt *nr);
/* a picture control's picture: object index img (a Doc.images entry) in place of its content */
int  sdt_set_picture(Doc *d, Pos s, int img);
/* repeating section: the item whose start mark is at item copied after it (before: before it), or
   taken out; the new item's start in *out */
int  sdt_item_add(Doc *d, Pos item, int before, Pos *out);
int  sdt_item_remove(Doc *d, Pos item);
/* Group: a..b made a group control (locks its text against editing but its controls); Ungroup: the
   group round p taken out */
int  sdt_group(Doc *d, Pos a, Pos b);
int  sdt_ungroup(Doc *d, Pos p);
/* typing is about to start at *at in a control: a placeholder showing is taken out (the typing takes
   its place) and a temporary control ("remove when edited") is taken out, its content kept; *at moves
   to where the typing goes. 1 if anything changed (each its own undo step) */
int  sdt_typing(Doc *d, Pos *at);

/* ---- the checks the edit paths make (core/doc.c) */
extern int sdt_bypass;                           /* > 0: no checks (Quill's own changes to controls) */
int  sdt_insert_ok(Doc *d, Pos at);              /* typing at at is allowed by the controls' locks */
int  sdt_delete_ok(Doc *d, Pos a, Pos b);        /* deleting a..b */
/* Restrict Editing's "filling in forms" (WP7, core/protect.h): a..b lies in a form's fillable part
   (a content control's content that isn't locked, or a text form field's result); at p (a == b) */
int  sdt_forms_ok(Doc *d, Pos a, Pos b);
int  sdt_form_at(const Doc *d, Pos p);           /* the same for the character after p (WP7's prot_form_at) */
/* the place a forms-protected document's Tab goes to from p: the next fillable part (wrapping); 0 none */
int  sdt_forms_next(Doc *d, Pos p, int dir, Pos *a, Pos *b);

/* ---- legacy form fields (WP1's fields with w:ffData) */
enum { FF_NONE, FF_TEXT, FF_CHECK, FF_DROP };
int  ff_kind(const Doc *d, int k);               /* field record k's form field kind */
int  ff_checked(const Doc *d, int k);            /* a check box form field's state */
int  ff_result(const Doc *d, int k);             /* a drop-down form field's chosen entry (0 first) */
/* a drop-down form field's entries: how many; entry i's text into out */
int  ff_entries(const Doc *d, int k, int i, char *out, size_t cap);
/* what a check box or drop-down form field shows (they have no result in the text): its symbol or
   chosen entry; 0 if k isn't one */
int  ff_box(const Doc *d, int k, char *out, size_t cap);
/* the form field whose start mark is at s: toggled, entry i chosen, or its ffData changed to these
   settings (a new field record, its marks swapped: one undo step) */
int  ff_toggle(Doc *d, Pos s);
int  ff_choose(Doc *d, Pos s, int i);
int  ff_set_data(Doc *d, Pos s, const char *ffdata);
/* a new form field at *at (moved past it): FF_ kind */
int  ff_insert(Doc *d, Pos *at, int kind);
/* ffData text helpers: an element's w:val (e.g. "default" inside "checkBox"), "" none */
int  ff_val(const char *ffdata, const char *elem, char *out, size_t cap);
/* ffData made from old (its other settings kept: enabled, help text...) with this name and kind element
   (<w:textInput>..., <w:checkBox>..., <w:ddList>...); malloc'd */
char *ff_build(const char *old, const char *name, const char *kindxml);
/* Restrict Editing (WP7) tells whether "filling in forms" is enforced (NULL: never); then Tab goes from
   form field to form field and only they can be edited */
extern int (*sdt_forms_protected)(const Doc *d);
/* the Form Field Options window's settings made into ffData (old: the field's, its other settings kept;
   entries: a drop-down's, UTF-8); malloc'd */
char *ff_options(const char *old, int kind, const char *name, const char *deftext, int maxlen, int defchecked, int checked,
                 const char *const *entries, int n, int result);
int  ff_maxlen(const Doc *d, int k);             /* a text form field's maximum length, 0 none */
int  ff_name(const Doc *d, int k, char *out, size_t cap);   /* its bookmark name */
int  ff_default_text(const Doc *d, int k, char *out, size_t cap);
int  ff_default_checked(const Doc *d, int k);
/* the date picker's calendars and how it stores its date (w:calendar, w:storeMappedDataAs), NULL ended */
extern const char *const sdt_calendars[], *const sdt_store_as[];

#endif
