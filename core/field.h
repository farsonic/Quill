/*
 * field.h - fields (WP1): Word's DATE, PAGE, SEQ, REF, MERGEFIELD, ADDIN... A field is a code (its
 * instruction, "DATE \@ "d MMMM yyyy"") and a result (what it shows), as in Word's files.
 *
 * THE MODEL (for every package that builds on fields: refs, cite, mailings, controls)
 *  - In the text a field is its result between two marks without width, like the comment marks:
 *    a start mark U+104000 + k and an end mark U+108000 + k (4 bytes of UTF-8 each: F4 84-87 xx xx
 *    and F4 88-8B xx xx; endnotes have F4 80-83, equations (WP14a) F4 8C-8F), k being its record in Doc.fields. So results move, copy and undo with the
 *    text, and may hold anything text holds (formatting, pictures, other fields; a result may run
 *    over several paragraphs, as a bibliography's does). A field with no result (XE, TC) is the two
 *    marks side by side.
 *  - The record (Field) holds the code and the field's settings. Records are never changed once
 *    made, only added (as Doc.images): editing a code or locking a field makes a new record and
 *    puts its marks in place of the old ones (one undo step), so undo needs nothing more and copies
 *    of a field may share a record. (showcode alone is changed in place: it is how it is shown.)
 *  - The page-number placeholders of headers (FIELD_PAGE, FIELD_PAGES, FIELD_PAGEREF in doc.h) are
 *    still the way a number that depends on the page is shown: a PAGE, NUMPAGES, SECTION,
 *    SECTIONPAGES or PAGEREF field's result is the placeholder, which the layout fills in for each
 *    page with the field's own format switches (field_placeholder). A bare placeholder (no marks) is a
 *    plain PAGE or NUMPAGES field, as Quill had them before.
 *  - A field inside another's code (IF { MERGEFIELD x } = ...) stays in the code as
 *    FLD_OPEN code FLD_SEP result FLD_CLOSE (control characters, so they can't clash with text).
 *  - The table of contents is still Quill's own (core/toc.h: paragraphs in the toc styles); its code
 *    from the file is kept in FieldInfo.toccode and written back round it.
 *
 * .docx: every field is read with its code and result (w:fldChar/w:instrText, w:fldSimple, nested,
 * locked, dirty, with w:ffData kept verbatim) and written back as complex fields. Evaluated (Update
 * Fields): DATE, TIME, CREATEDATE, SAVEDATE, PRINTDATE, EDITTIME, AUTHOR, TITLE, SUBJECT, KEYWORDS,
 * COMMENTS, LASTSAVEDBY, TEMPLATE, REVNUM, FILENAME, FILESIZE (kept), NUMPAGES, NUMWORDS, NUMCHARS,
 * PAGE, SECTION, SECTIONPAGES, PAGEREF, REF, SEQ, DOCPROPERTY, INFO, MERGEFIELD (through
 * FieldEnv.merge), IF, QUOTE, SYMBOL, USERNAME, USERINITIALS, with the \* \# and \@ switches (the
 * document's properties from WP24's core/info.h). Others
 * (ADDIN, TOC, INDEX, CITATION, HYPERLINK, XE, TC, ...) keep their result.
 */
#ifndef QUILL_FIELD_H
#define QUILL_FIELD_H

#include "doc.h"

struct Layout;

/* ---- the marks */
#define FIELD_MAX 12288             /* (the marks of 12288 and up are WP16's content controls, core/sdt.h) */
#define IS_FSTART(t) ((u8)(t)[0] == 0xF4 && ((u8)(t)[1] & 0xFC) == 0x84)
#define IS_FEND(t)   ((u8)(t)[0] == 0xF4 && ((u8)(t)[1] & 0xFC) == 0x88)
#define IS_FMARK(t)  ((u8)(t)[0] == 0xF4 && (u8)(t)[1] >= 0x84 && (u8)(t)[1] <= 0x8B)       /* either */
void fmark_utf8(char *out, int k, int end);      /* field k's start (end: its end) mark: 4 bytes and a NUL */
int  fmark_index(const char *t);                 /* the field a mark names (t at its first byte) */

/* nested fields inside a code */
#define FLD_OPEN  '\x01'
#define FLD_SEP   '\x02'
#define FLD_CLOSE '\x03'

typedef struct Field {
  char *code;               /* UTF-8, as written in the file (" DATE \@ "d MMMM yyyy" ") */
  char *ffdata;             /* the children of w:fldChar begin, verbatim (w:ffData of a form field,
                               w:fldData); NULL none */
  u8 locked;                /* not updated (w:fldLock) */
  u8 dirty;                 /* Word should update it when it opens the file (w:dirty) */
  u8 nosep;                 /* read without a separator (a field that never had a result: XE, TC) */
  u8 showcode;              /* this field shown the other way round from Doc.fieldcodes (Shift+F9) */
} Field;

/* what the fields keep of the file besides the records: the table of contents' code, the file's name,
   settings.xml's w:updateFields; Doc.finfo, made when first needed. (The document's properties that
   AUTHOR, SUBJECT, DOCPROPERTY... show are WP24's DocInfo, core/info.h) */
typedef struct FieldInfo {
  char *toccode;            /* the table of contents' code as read ("TOC \o "1-3" \h \z \u"); NULL: Quill's own */
  char filename[256];       /* the file it was last read from or saved to (FILENAME) */
  u8 updatefields;          /* settings.xml w:updateFields: Word updates the fields when it opens it */
} FieldInfo;
FieldInfo *field_info(Doc *d);
void field_set_filename(Doc *d, const char *path);   /* the file it was read from or is saved to (FILENAME) */                   /* Doc.finfo, made if it isn't there yet (NULL: out of memory) */
void field_free_all(Doc *d);                     /* Doc.fields and Doc.finfo freed (doc_free) */

/* ---- records */
/* a new record for code; its index (for the marks), -1 if the table is full or out of memory */
int  field_new(Doc *d, const char *code, int locked);
const Field *field_get(const Doc *d, int k);     /* NULL if k isn't a record */
/* the field's name: the code's first word in capitals ("DATE", "=", "MERGEFIELD"), "" if none */
void field_name(const char *code, char *out, size_t cap);
/* the n'th argument after the name (from 0; switches and their values skipped), quotes taken off; 0 if none */
int  field_arg(const char *code, int n, char *out, size_t cap);
/* the switch \c (c: '@', '#', '*' or a letter; a letter's case matters as in Word), its value (quotes
   taken off; "" for a switch without one) into out; the which'th one (from 0) when it is there more
   than once (\* MERGEFORMAT \* Upper). 0 if it isn't there */
int  field_switch(const char *code, char c, int which, char *out, size_t cap);
/* the code with nested fields shown as Word shows them ({ MERGEFIELD x }), into out */
void field_code_text(const char *code, char *out, size_t cap);
/* a code typed by a person (nested fields written { ... }) in the stored form; malloc'd */
char *field_code_parse(const char *typed);

/* ---- formats */
typedef struct { int year, month, day, hour, min, sec, wday; } FieldTime;   /* month 1-12, wday 0 Sunday */
void field_time_now(FieldTime *t);               /* the clock (field_clock if set, else time()) */
extern void (*field_clock)(FieldTime *t);        /* the Amiga's clock, set by the front end */
int  field_parse_time(const char *w3c, FieldTime *t);   /* "2026-10-29T10:00:00Z": 0 if it isn't one */
void field_time_w3c(const FieldTime *t, char *out);     /* back: 21 bytes */
/* a date picture (\@ "dddd, d MMMM yyyy", "HH:mm"...) applied to t */
void field_format_date(const char *pic, const FieldTime *t, char *out, size_t cap);
/* a number with the code's \* (Arabic, roman, ROMAN, alphabetic, ALPHABETIC, Ordinal, CardText,
   OrdText, Hex, DollarText, ArabicDash) and \# switches; 0 if it has none (out holds the digits) */
int  field_format_number(const char *code, long n, char *out, size_t cap);
/* the code's \* Upper, Lower, FirstCap, Caps applied to s (in place; Latin letters) */
void field_format_case(const char *code, char *s, size_t cap);
/* a number picture (\# "#,##0.00") applied to n (an integer) */
void field_number_picture(const char *pic, long n, char *out, size_t cap);
/* the date pictures Insert > Date and Time and the Field window offer (Word's), and the \* formats the
   Field window offers ("" first: none; Upper, Lower, FirstCap, Caps, Arabic, alphabetic, ALPHABETIC, roman,
   ROMAN, Ordinal, CardText, OrdText, Hex) */
extern const char *const field_date_pics[]; extern const int field_ndate_pics;
extern const char *const field_formats[]; extern const int field_nformats;
/* the date and week names (English unless the front end gives others: 12 months, 7 days, Sunday
   first) and their short forms */
extern const char *const *field_months, *const *field_months3, *const *field_days, *const *field_days3;
/* a document property by DOCPROPERTY's (or INFO's) name: Title, Subject, Author, Keywords,
   Comments, Category, Company, Manager, LastSavedBy, CreateTime, LastSaveTime, LastPrinted,
   RevisionNumber, Template, TotalEditingTime, or a custom property; 0 if it has none */
int  field_prop(Doc *d, const char *name, char *out, size_t cap);
/* the names Insert > Document Property offers (Word's), n of them */
extern const char *const field_prop_names[]; extern const int field_nprop_names;

/* ---- evaluating */
typedef struct FieldEnv {
  FieldTime now;            /* DATE and TIME */
  const char *filename;     /* FILENAME (NULL: FieldInfo.filename) */
  const char *user, *initials;   /* USERNAME, USERINITIALS (NULL: the author) */
  struct Layout *L;         /* the document laid out (pages, sections); NULL: those fields kept */
  /* a mail merge's value for a merge field (WP4): 1 and the value, or 0 (the field shows «name») */
  int (*merge)(void *ctx, const char *name, char *out, size_t cap);
  void *ctx;
} FieldEnv;
void field_env(FieldEnv *e);                     /* now from the clock, everything else none */
/* what field_eval gives */
enum { FE_KEEP,             /* Quill doesn't work it out: the result stays as it is */
       FE_TEXT,             /* out holds the result */
       FE_PLACE };          /* out holds a page placeholder (FIELD_PAGE...), filled in by the layout */
/* field k's result if it were at at (SEQ counts the ones before it there); out at least 256 bytes */
int  field_eval(Doc *d, int k, Pos at, const FieldEnv *e, char *out, size_t cap);
/* the same for a code (not a record) */
int  field_eval_code(Doc *d, const char *code, Pos at, const FieldEnv *e, char *out, size_t cap);

/* ---- where fields are (the paragraphs being edited: the body, or a story in its place) */
/* the innermost field round p (a caret just before its start mark or just after its end mark counts):
   its record, and where its start and end marks are; -1 if none */
int  field_at(Doc *d, Pos p, Pos *s, Pos *e);
/* the next field from p (dir > 0: starting after p; < 0: the last one starting before it), its marks;
   -1 if none (no wrapping round) */
int  field_next(Doc *d, Pos p, int dir, Pos *s, Pos *e);
/* where field k's end mark is, from its start mark s (the first matching one after it); 0 if none */
int  field_end(Doc *d, Pos s, Pos *e);
/* the fields whose start marks are in a..b (and the one round a, if any): how many, their start
   marks in at (up to max) */
int  field_list(Doc *d, Pos a, Pos b, Pos *at, int max);

/* ---- edits (each one undo step; 0 if nothing was done) */
/* a new field with code at *at (moved past it): its marks and its result worked out (a page
   placeholder, «name» for a merge field, nothing for XE...), the result formatted f (NULL: as the
   text at *at). Its record, -1 if it couldn't be put in */
int  field_insert(Doc *d, Pos *at, const char *code, const FieldEnv *e, const CharFmt *f);
/* the same with a result given (UTF-8, one paragraph; NULL: worked out) */
int  field_insert_result(Doc *d, Pos *at, const char *code, const char *result, const FieldEnv *e, const CharFmt *f);
/* the fields in a..b brought up to date (locked ones and those Quill can't work out stay); how many
   changed. The result keeps the formatting of its first character */
int  field_update(Doc *d, Pos a, Pos b, const FieldEnv *e);
/* every field: the text (field_update over all of it) and the headers, footers, notes, comments and text
   boxes (not undoable, as editing them isn't); how many changed */
int  field_update_all(Doc *d, const FieldEnv *e);
/* the fields in a..b replaced by their results (page placeholders by the page's number, with L) */
int  field_unlink(Doc *d, Pos a, Pos b, const FieldEnv *e);
/* the fields in a..b locked (lock 1) or unlocked */
int  field_lock(Doc *d, Pos a, Pos b, int lock);
/* the field whose start mark is at s gets a new code (its result kept until it is updated) */
int  field_set_code(Doc *d, Pos s, const char *code);

/* ---- how they are shown */
/* every field's code instead of its result (Alt+F9), or results again (each field's own Shift+F9 forgotten) */
void field_show_codes(Doc *d, int on);
/* whether any field shows its code: 0 none, 1 some (Shift+F9), 2 all (Doc.fieldcodes) */
int  field_codes_any(const Doc *d);
/* the fields in a..b (or the one round a) shown the other way round (Shift+F9); how many */
int  field_toggle(Doc *d, Pos a, Pos b);
/* whether field k shows its code */
int  field_code_shown(const Doc *d, int k);
/* what a field showing its code shows: "{ DATE \@ "d MMMM yyyy" }", cut short with "..." to fit cap */
void field_code_display(const Doc *d, int k, char *out, size_t cap);
/* in paragraph p, the end mark matching the start mark at off (its offset; p->len if it is in a later
   paragraph) */
u32  field_end_in_para(const Para *p, u32 off);
/* a page placeholder at off of paragraph p: what the field round it (in the same paragraph) asks for
   (FK_ below; FK_NONE: no field round it, the placeholder's own meaning) and that field's code */
enum { FK_NONE, FK_PAGE, FK_NUMPAGES, FK_SECTION, FK_SECTIONPAGES, FK_PAGEREF };
int  field_placeholder(const Doc *d, const Para *p, u32 off, const char **code);
/* field shading on screen (Word's Options > Advanced): never, always, or the field the caret is in */
enum { FS_NEVER, FS_ALWAYS, FS_SELECTED };
extern int field_shading;
/* how many fields are open at the start of paragraph para (marks before it not closed) */
int  field_depth_before(Doc *d, u32 para);

/* ---- for saving (core/docx.c) */
/* the marks in every story whose partner is missing or crossed (left out when saving, so the file's
   fields nest properly): their addresses in the paragraphs' text, sorted; *n how many. Malloc'd, NULL
   when there are none */
const char **field_orphans(Doc *d, u32 *n);
int  field_is_orphan(const char *const *o, u32 n, const char *t);
/* what kind of field a code is for Quill's reading and writing: FK_ for the page fields and PAGEREF,
   FC_ below for the others */
enum { FC_PLAIN = 16, FC_HYPERLINK, FC_REF, FC_TOC };
int  field_kind(const char *code);
/* a field Quill reads without marks, as it always has: a plain PAGE or NUMPAGES (no switch but
   \* MERGEFORMAT) or PAGEREF name \h (a bare placeholder), or HYPERLINK "url" \l "place" (a link): what
   Quill writes back is the same field */
int  field_is_bare(const char *code);

#endif
