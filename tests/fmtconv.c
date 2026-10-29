/* fmtconv - converts between the formats Quill reads and writes, and compares documents' text (a
   development aid for WP28a-d, used by tools/lo_formats.sh):
     fmtconv in out        read in (any format, by its contents or name), write out (by its name)
     fmtconv -cmp a b      1 if every non-empty paragraph of a is in b, in order (pictures,
                           notes and fields left out of the comparison); prints what is missing */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../core/docx.h"

/* a paragraph's text without object references, fields and white space at its ends */
static void plain(const Para *p, char *out, size_t cap)
{
  u32 i;
  size_t o = 0;
  for (i = 0; i < p->len && o + 1 < cap; i++) {
    if (i + 4 <= p->len && (IS_PIC(p->t + i) || IS_NOTE(p->t + i) || IS_ENOTE(p->t + i) || IS_MARK(p->t + i))) { i += 3; continue; }
    if (i + 3 <= p->len && IS_FIELD(p->t + i)) { i += 2; continue; }
    out[o++] = p->t[i] == '\v' || p->t[i] == '\t' ? ' ' : p->t[i];
  }
  while (o && out[o - 1] == ' ') o--;
  out[o] = 0;
  {
    size_t k = 0;
    while (out[k] == ' ') k++;
    if (k) memmove(out, out + k, o - k + 1);
  }
}
int main(int argc, char **argv)
{
  char err[256];
  unsigned lost = 0;
  if (argc == 4 && !strcmp(argv[1], "-cmp")) {
    Doc *a = doc_load_file(argv[2], &lost, err, sizeof err), *b = doc_load_file(argv[3], &lost, err, sizeof err);
    u32 i, j = 0, n = 0, found = 0;
    static char x[65536], y[65536];
    if (!a || !b) { printf("can't read %s\n", a ? argv[3] : argv[2]); return 2; }
    for (i = 0; i < a->np; i++) {
      u32 k;
      plain(a->p[i], x, sizeof x);
      if (!x[0]) continue;
      n++;
      for (k = j; k < b->np; k++) { plain(b->p[k], y, sizeof y); if (!strcmp(x, y)) break; }
      if (k < b->np) { found++; j = k + 1; } else printf("  missing: %.70s\n", x);
    }
    printf("%lu of %lu paragraphs found\n", (unsigned long)found, (unsigned long)n);
    doc_free(a); doc_free(b);
    return found == n ? 0 : 1;
  }
  if (argc == 3) {
    Doc *d = doc_load_file(argv[1], &lost, err, sizeof err);
    if (!d) { printf("%s\n", err); return 2; }
    if (!doc_save_file(d, argv[2])) { printf("can't write %s\n", argv[2]); doc_free(d); return 2; }
    doc_free(d);
    return 0;
  }
  printf("usage: fmtconv in out | fmtconv -cmp a b\n");
  return 2;
}
