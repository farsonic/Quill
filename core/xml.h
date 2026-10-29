/*
 * xml.h - a small pull parser for the XML in documents (.docx, .odt). Names are reported without
 * their namespace prefix ("w:p" is "p"): the vocabularies Quill reads don't clash on local names.
 * Text and attribute values come decoded (entities, character references, CDATA), in UTF-8.
 * It never fails hard: damaged input ends the stream (XT_EOF) after what could be read.
 */
#ifndef QUILL_XML_H
#define QUILL_XML_H

#include "util.h"

enum { XT_EOF, XT_START, XT_END, XT_TEXT };
#define XML_MAXATTR 32

/* a parser's state: s[0..n) is the input (not copied), i where it has got to */
typedef struct {
  const char *s; size_t n, i;
  int type;                         /* the last token, as xml_next returned it */
  char name[64];                    /* local name of a START or END */
  int nattr;                        /* how many attributes the START has (at most XML_MAXATTR) */
  char *an[XML_MAXATTR], *av[XML_MAXATTR];    /* attribute local names and values (in abuf) */
  Buf abuf;
  Buf text;                         /* XT_TEXT */
  int pending_end;                  /* <x/>: an END follows the START */
  int depth;                        /* open elements: up by one after a START, down after an END */
} Xml;

/* start parsing s[0..n); s must stay valid while the parser is used. xml_free releases its buffers. */
void xml_init(Xml *x, const char *s, size_t n);
void xml_free(Xml *x);
int  xml_next(Xml *x);              /* the next token's type (also in x->type); its data lasts until the next call */
const char *xml_attr(Xml *x, const char *name);   /* by local name; NULL if absent */
/* skip to the END that closes the START just read */
void xml_skip(Xml *x);
/* append to out the XML text s with &, <, > and " escaped */
void xml_escape(Buf *out, const char *s, size_t n);

#endif
