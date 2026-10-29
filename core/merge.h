/*
 * merge.h - mail merge (WP4): a recipient list (the data source), the merge fields of a main
 * document worked out for a record, previewing, merging to a new document, and the w:mailMerge
 * settings of a .docx.
 *
 * THE MODEL
 *  - Doc.merge (MergeInfo) is made when a document becomes a main document (Start Mail Merge) or a
 *    file's settings.xml has w:mailMerge. It holds what the file keeps: the main document's kind
 *    (letters, envelopes, labels, directory, e-mail), the data source's path and type, the query
 *    (Quill writes its filter and sort there as SQL, as Word does, and reads them back), the
 *    Match Fields table (w:odso/w:fieldMapData) and the other children verbatim; and what is only
 *    there while Quill runs: the recipients (MergeData, read from the data source when needed), which
 *    are checked, and the record being previewed.
 *  - The merge fields are WP1's fields (core/field.h): MERGEFIELD, ADDRESSBLOCK, GREETINGLINE and the
 *    rules ASK, FILLIN, IF, MERGEREC, MERGESEQ, NEXT, NEXTIF, SET, SKIPIF (REF or a bare bookmark name
 *    shows what SET or ASK gave it). Previewing puts a record's values in their results; merging copies
 *    the main document once for each record (several records for labels, NEXT moving on), with the
 *    merge fields replaced by their results as plain text.
 *  - Recipient lists: CSV or tab-separated text (UTF-8 or Latin-1; the first row names the columns),
 *    .xlsx (the first sheet), a .docx (its first table, the first row naming the columns), or a list
 *    typed in Quill (saved as CSV).
 */
#ifndef QUILL_MERGE_H
#define QUILL_MERGE_H

#include "doc.h"
#include "field.h"
#include "xml.h"
#include "zip.h"
#include "docx.h"

/* ---- the recipient list */
typedef struct MergeData {
  int ncols;
  char **cols;              /* the columns' names (UTF-8) */
  int nrows, rcap;
  char ***cell;             /* cell[row][col], UTF-8, never NULL */
  u8 *on;                   /* checked (merged), per row */
} MergeData;

MergeData *md_new(int ncols, const char *const *names);   /* empty; NULL if out of memory */
void md_free(MergeData *m);
int  md_add_row(MergeData *m, const char *const *cells); /* a row (cells NULL: empty ones), checked; its index, -1 oom */
int  md_set(MergeData *m, int row, int col, const char *v); /* 0 oom */
int  md_delete_row(MergeData *m, int row);
int  md_add_col(MergeData *m, const char *name);         /* its index, -1 oom */
int  md_col(const MergeData *m, const char *name);       /* by name (case and '_'/' ' ignored), -1 none */
/* readers: text with the delimiter found from the first line (tab, ';' or ','); .xlsx (its first
   sheet); a .docx (its first table). md_load: by the file's first bytes, else its name. NULL (err says
   why) if it can't be read */
MergeData *md_load_text(const u8 *data, size_t len);
MergeData *md_load_xlsx(const u8 *data, size_t len, char *err, size_t errcap);
MergeData *md_load_docx(const u8 *data, size_t len, char *err, size_t errcap);
MergeData *md_load_file(const char *path, char *err, size_t errcap);
/* the list as CSV (UTF-8, quoted where needed, CRLF), malloc'd */
char *md_csv(const MergeData *m, size_t *len);
/* Word's columns for a new list (Type a New List) */
extern const char *const md_new_cols[]; extern const int md_nnew_cols;

/* ---- Word's address fields (Match Fields, Address Block, Greeting Line), in w:odso's order */
enum { AF_ID, AF_TITLE, AF_FIRST, AF_MIDDLE, AF_LAST, AF_SUFFIX, AF_NICK, AF_JOB, AF_COMPANY, AF_ADDR1, AF_ADDR2,
       AF_CITY, AF_STATE, AF_POSTAL, AF_COUNTRY, AF_BPHONE, AF_BFAX, AF_HPHONE, AF_HFAX, AF_EMAIL, AF_WEB,
       AF_STITLE, AF_SFIRST, AF_SMIDDLE, AF_SLAST, AF_SNICK, AF_PFIRST, AF_PLAST, AF_ADDR3, AF_DEPT, AF_N };
extern const char *const af_names[AF_N];

/* ---- the main document's settings and the recipients */
enum { MT_NONE, MT_LETTERS, MT_ENVELOPES, MT_LABELS, MT_DIRECTORY, MT_EMAIL, MT_FAX };
/* a filter's comparisons (Word's Filter and Sort) */
enum { MF_EQ, MF_NE, MF_LT, MF_GT, MF_LE, MF_GE, MF_BLANK, MF_NOTBLANK, MF_CONTAINS, MF_NOTCONTAINS, MF_N };
#define MERGE_NFILT 6
#define MERGE_NSORT 3
typedef struct { char col[64]; u8 op, or_; char val[128]; } MergeFilter;   /* or_: joined to the one before by OR */
typedef struct MergeInfo {
  u8 type;                  /* MT_ */
  char src[256];            /* the data source's file (w:dataSource, w:odso/w:src) */
  char dtype[24];           /* w:dataType ("textFile", "spreadsheet", "native"...) */
  char query[1024];         /* w:query */
  char connect[512];        /* w:connectString */
  char table[128];          /* w:odso/w:table */
  u8 linkquery;             /* w:linkToQuery */
  u8 viewmerged;            /* w:viewMergedData: previewing */
  u8 checkerrors;           /* w:checkErrors (1-3) */
  u8 hdr;                   /* w:odso/w:fHdr: the first row names the columns */
  u8 coldelim;              /* w:odso/w:colDelim (9, 44...), 0 none */
  i32 active;               /* w:activeRecord (from 1), 0 none */
  char *extra;              /* other children of w:mailMerge, verbatim */
  char *odsox;              /* other children of w:odso, verbatim (w:recipientData is dropped) */
  char map[AF_N][64];       /* the column matched to each address field ("" none: found by name) */
  MergeFilter filt[MERGE_NFILT]; int nfilt;
  char sortcol[MERGE_NSORT][64]; u8 sortdesc[MERGE_NSORT];
  /* while Quill runs */
  MergeData *data;          /* NULL: not read yet */
  int *view, nview;         /* the rows passing the filter, sorted (indexes into data) */
  int rec;                  /* the record previewed: an index into view */
  u8 highlight;             /* Highlight Merge Fields */
  u8 loaded;                /* tried reading src */
} MergeInfo;

MergeInfo *merge_info(Doc *d);                 /* Doc.merge, made if missing (NULL oom) */
void merge_free(Doc *d);                       /* doc_free */
/* the recipients given to the document (taken over; NULL: none); the view made afresh */
void merge_set_data(Doc *d, MergeData *m);
/* the data source read from mi->src if it hasn't been (1 if there are recipients) */
int  merge_ensure(Doc *d, char *err, size_t errcap);
/* the view (filter, sort) made afresh from mi->filt and mi->sortcol; the query written from them */
void merge_apply_view(MergeInfo *mi);
void merge_make_query(MergeInfo *mi);
int  merge_parse_query(MergeInfo *mi);         /* filt and sortcol from query (1 if it had any) */
int  merge_row_passes(const MergeInfo *mi, int row);
/* the records that will be merged (checked rows of the view): how many; merge_active(mi, i): the view
   index of the i'th */
int  merge_count(const MergeInfo *mi);
int  merge_active(const MergeInfo *mi, int i);
/* the column for an address field (matched, else by Word's names for it), -1 none */
int  merge_af_col(const MergeInfo *mi, int af);
void merge_auto_match(MergeInfo *mi);          /* empty map entries filled by name where a column fits */
/* a value of view row v by a merge field's name (a column, else an address field's name): 1 if known */
int  merge_value(const MergeInfo *mi, int v, const char *name, char *out, size_t cap);
/* the text an ADDRESSBLOCK or GREETINGLINE code gives for view row v (v < 0: Word's sample, Joshua
   Randall); lines are separated by '\v' */
void merge_address(const MergeInfo *mi, int v, const char *code, char *out, size_t cap);
void merge_greeting(const MergeInfo *mi, int v, const char *code, char *out, size_t cap);
/* the formats offered by the Address Block and Greeting Line windows: their pictures ("Joshua Randall
   Jr.") and the \f strings they stand for */
extern const char *const merge_name_pics[], *const merge_name_fmts[]; extern const int merge_nname;
extern const char *const merge_greet_pics[], *const merge_greet_fmts[]; extern const int merge_ngreet;
/* the codes the windows make */
void merge_address_code(int name, int company, int postal, int country, const char *excluded, char *out, size_t cap);   /* name -1: none; country: 0 never, 1 always, 2 unless excluded */
void merge_greeting_code(int before, int name, int after, const char *invalid, char *out, size_t cap);   /* before: "Dear", "To", none; after: ',' ':' none */

/* ---- the fields */
/* whether a field's name is one of the mail merge fields (Highlight Merge Fields) */
int  merge_is_field(const char *name);
int  merge_is_field_k(const Doc *d, int k);
/* what ASK and FILLIN ask: 1 and the answer, 0 to use the default */
typedef int (*MergeAsk)(void *ctx, const char *prompt, const char *def, char *out, size_t cap);
/* the merge fields of the body shown for view row mi->rec (on), or as Word shows them without data
   («Name», «AddressBlock»...): one undo step; how many changed */
int  merge_preview(Doc *d, int on);
/* what a merge field with code shows without data ("\xC2\xABName\xC2\xBB"...): 1 if it is one */
int  merge_placeholder(const char *code, char *out, size_t cap);
/* the document merged: records from..to (indexes among merge_count's, inclusive), each from a new page
   (sections; not for MT_DIRECTORY), ASK and FILLIN through ask (NULL: their defaults). A new document,
   NULL if no record was merged; *n how many were. errs (may be NULL): what went wrong, a line each */
Doc *merge_build(Doc *d, int from, int to, MergeAsk ask, void *ctx, int *n, Buf *errs);
/* Check for Errors: the problems found (merge fields with no column, no data source...), a line each
   in errs; how many */
int  merge_check(Doc *d, Buf *errs);
/* the fields' names the document uses that no column has (Check for Errors, Match Fields) */
int  merge_unknown_fields(Doc *d, char names[][64], int max);
/* Find Recipient: the next view row from v (after it) with what in a field (col -1: any); -1 none */
int  merge_find(const MergeInfo *mi, int v, const char *what, int col);
/* Find Duplicates: rows equal to an earlier one (ignoring case and spaces at the ends) marked in dup
   (nrows bytes); how many */
int  merge_duplicates(const MergeData *m, u8 *dup);
/* Validate Addresses (only checks that each checked row has a name, a street, a town and a postcode
   or country, by Match Fields): rows lacking one marked in bad; how many */
int  merge_validate(const MergeInfo *mi, u8 *bad);

/* ---- Finish & Merge to PDF (core/mergepdf.c): the records from..to merged into one PDF at path, or
   (each) a PDF a record, named from column namecol's value in path's folder (-1: path's name with
   _1, _2 ...). 1 if anything was written; *files how many */
struct FontSet;
int  merge_to_pdf(Doc *d, int from, int to, int each, int namecol, const char *path, struct FontSet *fs, MergeAsk ask, void *ctx, int *files, Buf *errs);
void merge_pdf_name(const char *path, const MergeInfo *mi, int rec, int namecol, char *out, size_t cap);

/* ---- .docx (core/docx.c's hooks): settings.xml's w:mailMerge (read at its start tag, written in
   slot 1, before w:revisionView), and word/_rels/settings.xml.rels for the data source's path */
void merge_read_setting(Doc *d, Xml *x);
void merge_read_parts(Doc *d, ZipReader *z);
void merge_write_settings(Doc *d, Buf *b, int where);
void merge_write_parts(Doc *d, DocxOut *o);

#endif
