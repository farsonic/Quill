/*
 * cite.h - citations and bibliography (WP3): Word's sources, CITATION and BIBLIOGRAPHY fields and
 * citation styles.
 *
 * SOURCES are kept as Word keeps them: a b:Sources document (namespace
 * http://schemas.openxmlformats.org/officeDocument/2006/bibliography), the document's own "current
 * list" in a customXml part of the .docx, the "master list" in a file of the same form (Word's
 * Sources.xml; Quill's is PROGDIR:Sources.xml). A source is its simple elements (Tag, SourceType,
 * Title, Year, City, Publisher ...) as name/value pairs in the order read, its contributors (b:Author
 * holds b:Author, b:Editor, b:Translator, b:Composer ... each a list of people or a corporate name),
 * and whatever else it holds kept verbatim. A source with no SourceType is a placeholder (Word's
 * "Add New Placeholder"): its tag stands in for it.
 *
 * CITATIONS are CITATION fields (core/field.h): " CITATION Smi20 \l 1033 \p 23 \m Doe19 \n \y \t
 * \f "see " \s " for more" \v 2 ": the source's tag, \p pages, \v volume, \m another source cited in
 * the same place (as often as needed), \n \y \t suppress the author, year and title, \f and \s a
 * prefix and suffix, \l the language. Their results are worked out in the document's style whenever
 * fields are updated (field.c asks cite_eval).
 *
 * THE BIBLIOGRAPHY is a BIBLIOGRAPHY field whose result is one paragraph per source of the current
 * list (placeholders left out) in the Bibliography style, ordered and formatted as the style says,
 * rebuilt by cite_update_bibs (field_update calls it).
 *
 * STYLES: Word's (APA 6th, Chicago 16th, GB7714, GOST name and title sort, Harvard - Anglia, IEEE,
 * ISO 690 first element and numerical, MLA 7th, SIST02, Turabian) and the newer editions people ask
 * for (APA 7th, MLA 8th and 9th, Chicago 17th), each formatted by its own rules here (Word's
 * style sheets are XSL, which Quill doesn't run). The document's style is the b:Sources part's
 * SelectedStyle, StyleName and Version attributes, as in Word.
 */
#ifndef QUILL_CITE_H
#define QUILL_CITE_H

#include "doc.h"

typedef struct { char *last, *first, *middle; } CitePerson;
typedef struct { char role[24]; char *corp; CitePerson *p; int np; } CiteRole;   /* corp: a corporate name (no people) */
typedef struct { char *k, *v; } CiteKV;
typedef struct {
  CiteKV *kv; int nkv;      /* the simple elements: "Tag", "SourceType", "Title", ... (UTF-8) */
  CiteRole *r; int nr;      /* b:Author's children */
  char *extra;              /* other elements with children, verbatim; NULL none */
} CiteSource;
typedef struct {
  CiteSource *s; int n;
  char *selstyle, *stylename, *version;   /* b:Sources' attributes (NULL: none) */
} CiteList;

/* ---- sources */
void cite_source_free(CiteSource *s);
int  cite_source_copy(CiteSource *to, const CiteSource *from);    /* 0 if out of memory */
const char *cite_get(const CiteSource *s, const char *k);          /* "" if it hasn't that element */
int  cite_set(CiteSource *s, const char *k, const char *v);         /* v NULL or "": taken out */
/* a contributor role ("Author", "Editor", ...): NULL if the source has none (create: made empty) */
CiteRole *cite_role(CiteSource *s, const char *role, int create);
const CiteRole *cite_role_c(const CiteSource *s, const char *role);
/* a role from text: "Last, First Middle; First Last; ..." (corp: the whole text is a corporate name);
   empty text takes the role out. 0 if out of memory */
int  cite_set_names(CiteSource *s, const char *role, const char *text, int corp);
/* a role as that text ("" if none) */
void cite_names_text(const CiteSource *s, const char *role, char *out, size_t cap);
int  cite_is_placeholder(const CiteSource *s);
/* a new tag for s in l, as Word makes them: the first three letters of the first author's surname
   (or the title) and the year's last two digits ("Smi20"), made unique with a number */
void cite_make_tag(const CiteList *l, const CiteSource *s, char *out, size_t cap);
/* a new source's Guid ("{8 4 4 4 12 hex}") */
void cite_make_guid(char *out);

/* ---- Word's source types and their fields (Create Source): each type's XML name, its label, the
   fields Word shows for it (roles and simple elements by their XML names) and those "Show All
   Bibliography Fields" adds */
typedef struct { const char *xml, *label; const char *const *fields, *const *more; } CiteTypeDef;
extern const CiteTypeDef cite_types[];
extern const int cite_ntypes;
int  cite_type_index(const char *xml);                 /* -1 if none (a placeholder) */
const char *cite_field_label(const char *k);           /* "Name of Web Page" ... (English, for tr()) */
int  cite_is_role(const char *k);                      /* Author, Editor, Composer ... */

/* ---- lists */
void cite_list_free(CiteList *l);
int  cite_find(const CiteList *l, const char *tag);    /* its index (tags compared without case), -1 */
/* a copy of s added to l, replacing one with the same tag; its index, -1 if out of memory */
int  cite_add(CiteList *l, const CiteSource *s);
void cite_remove(CiteList *l, int i);
/* b:Sources XML into l (emptied first): 0 if it isn't a b:Sources document */
int  cite_parse(CiteList *l, const char *xml, size_t n);
char *cite_xml(const CiteList *l, size_t *n);         /* back (malloc'd), NULL if out of memory */
int  cite_load_file(CiteList *l, const char *path);   /* 0 if missing or not sources */
int  cite_save_file(const CiteList *l, const char *path);

/* ---- styles */
enum { CS_APA6, CS_APA7, CS_CHICAGO16, CS_CHICAGO17, CS_GB7714, CS_GOSTNAME, CS_GOSTTITLE, CS_HARVARD, CS_IEEE,
       CS_ISO690, CS_ISO690NUM, CS_MLA7, CS_MLA8, CS_MLA9, CS_SIST02, CS_TURABIAN, CITE_NSTYLES };
typedef struct {
  const char *name;          /* as Word's style list shows it ("APA Sixth Edition") */
  const char *shortname;     /* for the ribbon ("APA 6th") */
  const char *xsl, *stylename, *version;   /* b:Sources' SelectedStyle, StyleName, Version */
  const char *bibtitle;      /* what its bibliography is usually called */
} CiteStyleDef;
extern const CiteStyleDef cite_styles[CITE_NSTYLES];
int  cite_style_of(const CiteList *l);                /* CS_ (APA 6th, Word's default, when unknown) */
int  cite_list_set_style(CiteList *l, int cs);
int  cite_numbered(int cs);                           /* cites by number ([1]) */

/* ---- formatting (UTF-8; CITE_I1 .. CITE_I0 around italic text in an entry) */
#define CITE_I1 '\x0E'
#define CITE_I0 '\x0F'
/* source i of l as a bibliography entry in style cs (n: its number in numbered styles; yearsfx: the
   letter after the year when one author has several sources that year, 0 none) */
void cite_entry(const CiteList *l, int i, int cs, int n, Buf *out);
/* a citation of source i (pages, suppress: CITE_NO_ bits); first: the first citation of it in the
   document (APA 6th lists up to five authors then); n its number */
enum { CITE_NO_AUTHOR = 1, CITE_NO_YEAR = 2, CITE_NO_TITLE = 4 };
void cite_cite_one(const CiteList *l, int i, int cs, int n, const char *pages, const char *volume, int suppress, int first, Buf *out);
/* the letter that tells apart one author's sources of the same year (a, b, ...) in author-date
   styles, 0 if none needed */
int  cite_year_suffix(const CiteList *l, int i, int cs);
/* the current list's sources in bibliography order for style cs: their indexes into order (n of
   them, placeholders left out), num: each one's number (numbered styles: cited ones first, in the
   order first cited in d) */
int  cite_bib_order(Doc *d, int cs, int *order, int *num, int max);

/* ---- the document */
typedef struct CiteDoc {
  CiteList list;            /* the current list */
  char part[64];            /* its customXml part among the kept ones (core/keep.h); "" none yet */
  char *readxml;            /* the list as it was read, in cite_xml's form (unchanged: the part's own bytes stay) */
} CiteDoc;
CiteDoc *cite_doc(Doc *d);                             /* Doc.cite, made if needed (NULL: out of memory) */
void cite_free(Doc *d);                                /* doc_free */
int  cite_doc_style(Doc *d);                           /* the document's style (CS_) */
/* CITATION's result for code (flat, no nested fields) at at: FE_TEXT and the text (core/field.h), or
   FE_KEEP when there are no sources at all */
int  cite_eval(Doc *d, const char *code, Pos at, char *out, size_t cap);
/* every BIBLIOGRAPHY field in the body whose start is in a..b or round a rebuilt (part of the
   caller's undo step); how many */
int  cite_update_bibs(Doc *d, Pos a, Pos b);
/* the body's CITATION fields: how many, their start marks in at (up to max) */
int  cite_list_citations(Doc *d, Pos *at, int max);
/* the field at p if it is a CITATION (BIBLIOGRAPHY: bib 1): its record and marks; -1 if not */
int  cite_field_at(Doc *d, Pos p, int bib, Pos *s, Pos *e);
/* a citation of tag (with pages, suppress bits; more: other tags for \m, NULL none) at *at: one undo
   step, its record or -1 */
int  cite_insert(Doc *d, Pos *at, const char *tag, const char *pages, int suppress, const char *const *more, int nmore);
/* the code of a citation of tag with these settings (into out) */
void cite_code(char *out, size_t cap, const char *tag, const char *pages, int suppress, const char *const *more, int nmore);
/* a bibliography at *at: kind 0 the field alone, 1 "Bibliography", 2 "References", 3 "Works Cited"
   heading first (Heading 1). One undo step; its record or -1 */
int  cite_insert_bib(Doc *d, Pos *at, int kind);
/* every citation and bibliography brought up to date (one undo step); how many changed */
int  cite_update_all(Doc *d);
/* the citations (bib: and bibliographies) in a..b, or the one round a, made plain text; how many */
int  cite_to_static(Doc *d, Pos a, Pos b, int bib);
/* a source's tag renamed everywhere it is cited */
int  cite_retag(Doc *d, const char *old, const char *nw);
/* whether the document cites tag */
int  cite_is_cited(Doc *d, const char *tag);

/* ---- .docx (core/docx.c's hooks) */
void cite_docx_read(Doc *d);                             /* the kept customXml part with b:Sources, read */
/* the current list written into its kept part (made if the document has none and the list isn't
   empty): call after every change to Doc.cite's list or style. 0 if out of memory */
int  cite_sync(Doc *d);

#endif
