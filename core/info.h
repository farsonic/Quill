/*
 * info.h - WP24: the document's properties, as Word's Properties window has them.
 *
 * docProps/core.xml: title and author (Doc.title, Doc.author), subject, keywords, comments
 * (dc:description), category, status, last saved by, revision, created, modified, last printed,
 * language, identifier, version; children Quill doesn't know are kept verbatim.
 * docProps/app.xml: company, manager, hyperlink base and template are kept and editable; the
 * statistics (pages, words, characters, paragraphs) are written afresh; everything else in it
 * (heading pairs, titles of parts, the application's version...) is kept verbatim in its order.
 * docProps/custom.xml: custom properties (text, number, date, yes or no; other kinds and linked
 * properties kept as they are).
 * These parts used to be kept whole by core/keep.c; now they are read and written here.
 */
#ifndef QUILL_INFO_H
#define QUILL_INFO_H

#include "doc.h"

enum { IP_SUBJECT, IP_KEYWORDS, IP_DESCRIPTION, IP_CATEGORY, IP_STATUS, IP_LASTBY, IP_REVISION, IP_CREATED, IP_MODIFIED,
       IP_LASTPRINTED, IP_LANGUAGE, IP_IDENTIFIER, IP_VERSION,          /* core.xml */
       IP_COMPANY, IP_MANAGER, IP_HLINKBASE, IP_TEMPLATE, IP_N };       /* app.xml */
enum { CP_TEXT, CP_NUMBER, CP_DATE, CP_BOOL, CP_OTHER };
typedef struct {
  char *name, *value;       /* UTF-8; a date as 2026-10-29T12:00:00Z, yes or no as "1" / "0" */
  u8 type;                  /* CP_ */
  char *raw;                /* CP_OTHER: its value element as read (vt:vector...), written back */
  char *link;               /* linkTarget (a bookmark it shows), or NULL */
} CustProp;
typedef struct { char *name, *raw; } RawEl;                 /* an element kept verbatim (its local name) */
typedef struct DocInfo {
  char *v[IP_N];            /* malloc'd UTF-8, NULL: empty */
  RawEl *core; int ncore;   /* core.xml's other children */
  char *coreroot;           /* its root start tag as read (NULL: Quill's own) */
  RawEl *app; int napp;     /* app.xml's children in their order (the ones Quill writes afresh too) */
  char *approot;
  CustProp *cust; int ncust;
  char *custroot;
  u32 pages;                /* set before saving (the layout's pages); 0: as read */
} DocInfo;

DocInfo *info_get(Doc *d);                               /* d's, made if it has none (NULL if out of memory) */
const char *info_str(const Doc *d, int which);           /* a property, "" if none */
int  info_set(Doc *d, int which, const char *utf8);      /* (empty or NULL: none); 0 if out of memory */
void info_free(Doc *d);                                  /* (doc_free does this) */
/* custom properties: added (or the one of that name changed), taken out */
int  info_cust_set(Doc *d, const char *name, int type, const char *value);
int  info_cust_find(const Doc *d, const char *name);     /* its index, -1 if none */
void info_cust_remove(Doc *d, int k);

/* the clock and user name saving writes as modified and last saved by (the program sets them; the
   tests leave the clock alone: then "modified" is what was read). iso: 2026-10-29T12:00:00Z */
void info_set_clock(const char *iso);
void info_set_user(const char *utf8);

/* the parts read (core.xml, app.xml, custom.xml) */
void info_read_core(Doc *d, const char *xml, size_t n);
void info_read_app(Doc *d, const char *xml, size_t n);
void info_read_custom(Doc *d, const char *xml, size_t n);
/* and written: core.xml (whole, into b), app.xml, custom.xml (malloc'd; custom NULL when there are
   no custom properties). Saving sets created once if it is empty */
void  info_write_core(Doc *d, Buf *b);
char *info_app_xml(Doc *d, size_t *len);
char *info_custom_xml(Doc *d, size_t *len);

/* the statistics written to app.xml: words, characters without and with spaces, paragraphs (the body's) */
void info_stats(Doc *d, u32 *words, u32 *chars, u32 *charsp, u32 *paras);

#endif
