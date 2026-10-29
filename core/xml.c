/* xml.c - see xml.h */
#include <string.h>
#include <stdlib.h>
#include "xml.h"

/* start parsing s[0..n). The text is not copied, so it must outlive the parser; a leading UTF-8
   byte order mark is skipped. Pair with xml_free. */
void xml_init(Xml *x, const char *s, size_t n)
{
  memset(x, 0, sizeof *x);
  x->s = s; x->n = n;
  buf_init(&x->abuf); buf_init(&x->text);
  /* a UTF-8 byte order mark */
  if (n >= 3 && (u8)s[0] == 0xEF && (u8)s[1] == 0xBB && (u8)s[2] == 0xBF) x->i = 3;
}
/* release the attribute and text buffers (the input text belongs to the caller) */
void xml_free(Xml *x) { buf_free(&x->abuf); buf_free(&x->text); }

/* decode text from s[a..b) (entities and character references) onto out */
/* decode text from s[a..b) (entities and character references) onto out. Only the five XML
   entities are known; anything else (an HTML entity, a bad or zero character reference) is copied
   through literally rather than dropped, so damaged text still shows. */
static void decode(Buf *out, const char *s, size_t a, size_t b)
{
  size_t i = a;
  while (i < b) {
    if (s[i] == '&') {
      size_t e = i + 1;
      while (e < b && e - i < 12 && s[e] != ';') e++;
      if (e < b && s[e] == ';') {
        const char *ent = s + i + 1;
        size_t el = e - i - 1;
        u32 cp = 0;
        int known = 1;
        if (el == 2 && !memcmp(ent, "lt", 2)) cp = '<';
        else if (el == 2 && !memcmp(ent, "gt", 2)) cp = '>';
        else if (el == 3 && !memcmp(ent, "amp", 3)) cp = '&';
        else if (el == 4 && !memcmp(ent, "quot", 4)) cp = '"';
        else if (el == 4 && !memcmp(ent, "apos", 4)) cp = '\'';
        else if (el > 1 && ent[0] == '#') {
          size_t k = 1;
          int hex = ent[1] == 'x' || ent[1] == 'X';
          if (hex) k = 2;
          for (; k < el; k++) {
            int c = ent[k], v;
            if (c >= '0' && c <= '9') v = c - '0';
            else if (hex && c >= 'a' && c <= 'f') v = c - 'a' + 10;
            else if (hex && c >= 'A' && c <= 'F') v = c - 'A' + 10;
            else { known = 0; break; }
            cp = cp * (hex ? 16 : 10) + (u32)v;
            if (cp > 0x10FFFF) { known = 0; break; }
          }
          if (cp == 0) known = 0;
        } else known = 0;
        if (known) {
          char u[4];
          buf_add(out, u, (size_t)utf8_put(u, cp));
          i = e + 1;
          continue;
        }
      }
    }
    buf_addc(out, s[i]);
    i++;
  }
}
/* copy the name s[0..n) into dst without any namespace prefix ("w:val" becomes "val"), truncated
   to fit cap bytes and NUL-terminated */
static void local_name(char *dst, size_t cap, const char *s, size_t n)
{
  const char *c = memchr(s, ':', n);
  if (c) { n -= (size_t)(c + 1 - s); s = c + 1; }
  if (n >= cap) n = cap - 1;
  memcpy(dst, s, n);
  dst[n] = 0;
}
/* the XML whitespace characters, and what may appear in a tag or attribute name (loosely: anything
   that doesn't end one) */
static int is_space(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static int name_char(int c) { return c && !is_space(c) && c != '>' && c != '/' && c != '=' && c != '<'; }

/* read the next token: a start tag (name and attributes filled in), an end tag (name), text
   (decoded into x->text) or XT_EOF. Comments, declarations and processing instructions are skipped;
   CDATA comes back as text, verbatim. <x/> gives a START and then an END on the following call.
   Name, attributes and text are only valid until the next call. Attributes past XML_MAXATTR are
   dropped, and all of them are if memory runs out. */
int xml_next(Xml *x)
{
  const char *s = x->s;
  size_t n = x->n;
  if (x->pending_end) { x->pending_end = 0; x->type = XT_END; x->depth--; return XT_END; }
  for (;;) {
    size_t i = x->i;
    if (i >= n) return x->type = XT_EOF;
    if (s[i] != '<') {                                  /* text up to the next tag */
      size_t e = i;
      while (e < n && s[e] != '<') e++;
      x->text.len = 0;
      if (x->text.data) x->text.data[0] = 0;
      decode(&x->text, s, i, e);
      x->i = e;
      return x->type = XT_TEXT;
    }
    if (i + 4 <= n && !memcmp(s + i, "<!--", 4)) {      /* comment */
      size_t e = i + 4;
      while (e + 3 <= n && memcmp(s + e, "-->", 3)) e++;
      x->i = e + 3 <= n ? e + 3 : n;
      continue;
    }
    if (i + 9 <= n && !memcmp(s + i, "<![CDATA[", 9)) {
      size_t e = i + 9;
      while (e + 3 <= n && memcmp(s + e, "]]>", 3)) e++;
      x->text.len = 0;
      buf_add(&x->text, s + i + 9, (e < n ? e : n) - (i + 9));
      x->i = e + 3 <= n ? e + 3 : n;
      return x->type = XT_TEXT;
    }
    if (i + 1 < n && (s[i + 1] == '?' || s[i + 1] == '!')) {   /* declarations, processing instructions */
      size_t e = i + 2;
      while (e < n && s[e] != '>') e++;
      x->i = e < n ? e + 1 : n;
      continue;
    }
    if (i + 1 < n && s[i + 1] == '/') {                 /* end tag */
      size_t a = i + 2, e = a;
      while (e < n && name_char((u8)s[e])) e++;
      local_name(x->name, sizeof x->name, s + a, e - a);
      while (e < n && s[e] != '>') e++;
      x->i = e < n ? e + 1 : n;
      x->depth--;
      return x->type = XT_END;
    }
    {                                                   /* start tag with attributes */
      size_t a = i + 1, e = a;
      while (e < n && name_char((u8)s[e])) e++;
      if (e == a) { x->i = n; return x->type = XT_EOF; }
      local_name(x->name, sizeof x->name, s + a, e - a);
      x->nattr = 0;
      x->abuf.len = 0;
      {
        /* attribute values go into abuf; pointers are fixed up afterwards (abuf may move) */
        size_t offs[XML_MAXATTR * 2];
        int k;
        for (;;) {
          size_t na, ne, q, ve;
          char quote;
          while (e < n && is_space((u8)s[e])) e++;
          if (e >= n) { x->i = n; return x->type = XT_EOF; }
          if (s[e] == '>' || s[e] == '/') break;
          na = e;
          while (e < n && name_char((u8)s[e])) e++;
          ne = e;
          while (e < n && is_space((u8)s[e])) e++;
          if (e >= n || s[e] != '=') { if (e == na) e++; continue; }
          e++;
          while (e < n && is_space((u8)s[e])) e++;
          if (e >= n || (s[e] != '"' && s[e] != '\'')) continue;
          quote = s[e];
          q = ++e;
          while (e < n && s[e] != quote) e++;
          ve = e;
          if (e < n) e++;
          if (x->nattr < XML_MAXATTR) {
            char ln[64];
            local_name(ln, sizeof ln, s + na, ne - na);
            offs[2 * x->nattr] = x->abuf.len;
            buf_add(&x->abuf, ln, strlen(ln) + 1);
            offs[2 * x->nattr + 1] = x->abuf.len;
            decode(&x->abuf, s, q, ve);
            buf_addc(&x->abuf, 0);
            x->nattr++;
          }
        }
        if (x->abuf.oom) x->nattr = 0;
        for (k = 0; k < x->nattr; k++) { x->an[k] = x->abuf.data + offs[2 * k]; x->av[k] = x->abuf.data + offs[2 * k + 1]; }
      }
      x->depth++;
      if (s[e] == '/') { x->pending_end = 1; while (e < n && s[e] != '>') e++; }
      x->i = e < n ? e + 1 : n;
      return x->type = XT_START;
    }
  }
}
/* the value of the current start tag's attribute with this local name (prefix ignored), decoded;
   NULL if absent. Valid until the next xml_next. */
const char *xml_attr(Xml *x, const char *name)
{
  int k;
  for (k = 0; k < x->nattr; k++) if (!strcmp(x->an[k], name)) return x->av[k];
  return NULL;
}
/* call right after a START: reads on until its matching END has been consumed (or the input ends),
   so the caller can ignore an element and all its children */
void xml_skip(Xml *x)
{
  int d = x->depth;
  while (x->depth >= d && xml_next(x) != XT_EOF) ;
}
/* append s[0..n) to out with &, <, > and " replaced by entities; safe for both element text and
   double-quoted attribute values. Copies unchanged runs in one go. */
void xml_escape(Buf *out, const char *s, size_t n)
{
  size_t i, start = 0;
  for (i = 0; i < n; i++) {
    const char *r = NULL;
    switch (s[i]) {
    case '&': r = "&amp;"; break;
    case '<': r = "&lt;"; break;
    case '>': r = "&gt;"; break;
    case '"': r = "&quot;"; break;
    }
    if (r) { buf_add(out, s + start, i - start); buf_adds(out, r); start = i + 1; }
  }
  buf_add(out, s + start, n - start);
}
