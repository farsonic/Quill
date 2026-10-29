/*
 * inspect.h - WP24: Inspect Document, Check Accessibility and Check Compatibility (File > Info >
 * Check for Issues). What they find is reported in English words that are also the interface's
 * translation keys (the program passes them through tr()), with numbers for their %d.
 */
#ifndef QUILL_INSPECT_H
#define QUILL_INSPECT_H

#include "doc.h"

/* ---- Inspect Document: what a document carries that its author may not want to send */
enum { INS_COMMENTS = 1, INS_REVISIONS = 2, INS_PROPS = 4, INS_HIDDEN = 8, INS_HEADERS = 16, INS_CUSTOMXML = 32 };
typedef struct {
  u32 comments;             /* comments in the text */
  u32 revisions;            /* tracked changes (runs of inserted or deleted text) */
  u32 props;                /* properties and personal information filled in (author, company, comment authors...) */
  u32 hidden;               /* runs of hidden text (when Quill has hidden text, WP9) */
  u32 headers;              /* headers, footers and watermarks with something in them */
  u32 customxml;            /* custom XML data parts kept from the file */
} InspectReport;
void inspect_doc(Doc *d, InspectReport *r);
/* take out what (INS_ bits): comments deleted and tracked changes accepted (undoable), properties and
   personal information cleared (authors of comments and changes become "Author"), hidden text deleted,
   headers, footers and watermarks emptied, custom XML dropped. Returns what was removed */
unsigned inspect_remove(Doc *d, unsigned what);

/* ---- Check Accessibility */
enum { AK_ERROR, AK_WARNING, AK_TIP };
enum { AR_ALTTEXT, AR_TABLEHEADER, AR_HEADINGORDER, AR_CONTRAST, AR_FLOATING, AR_MERGED, AR_BLANK, AR_LINKTEXT, AR_TITLE, AR_N };
typedef struct {
  u8 kind, rule;            /* AK_, AR_ */
  Pos at;                   /* where (the caret goes there) */
  int obj;                  /* the object it is about (Doc.images), -1 none */
  int a, b;                 /* the numbers in its message */
} AccIssue;
/* the issues found, errors first, then warnings, then tips (each in the document's order); how many
   (at most max) */
int acc_check(Doc *d, AccIssue *out, int max);
const char *acc_message(int rule);        /* "Missing alternative text", "Heading level skipped (Heading %d after Heading %d)"... */
const char *acc_fix(int rule);            /* how to fix it */
/* the WCAG contrast of two colours (0xRRGGBB), times 100 (2100 for black on white) */
int acc_contrast(u32 fg, u32 bg);

/* ---- Check Compatibility: what earlier versions of Word (and Quill itself) can't show as it is */
typedef struct { const char *msg; u32 count; } CompatIssue;
int compat_check(Doc *d, CompatIssue *out, int max);

#endif
