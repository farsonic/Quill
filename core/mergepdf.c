/*
 * mergepdf.c - Finish & Merge to PDF (WP4, core/merge.h): the merged document as one PDF, or a PDF
 * for each record (named from a column, else numbered).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "merge.h"
#include "layout.h"
#include "pdf.h"

/* a file name made safe from a value: no path separators or wildcards */
static void safe_name(const char *v, char *out, size_t cap)
{
  size_t n = 0;
  for (; *v && n + 1 < cap; v++) {
    char c = *v;
    if (c == '/' || c == ':' || c == '\\' || c == '*' || c == '?' || c == '#' || c == '"' || c == '|' || c == '<' || c == '>' || (u8)c < 32) c = '_';
    out[n++] = c;
  }
  out[n] = 0;
}
static int save_pdf(Doc *m, FontSet *fs, const char *path)
{
  Layout *L = lay_new(m, fs);
  int ok;
  if (!L) return 0;
  lay_update(L);
  ok = pdf_save_file(L, path);
  lay_free(L);
  return ok;
}
void merge_pdf_name(const char *path, const MergeInfo *mi, int rec, int namecol, char *out, size_t cap)
{
  char base[512], val[200], safe[200];
  const char *dot = strrchr(path, '.'), *sl = path, *p;
  size_t bl;
  for (p = path; *p; p++) if (*p == '/' || *p == ':') sl = p + 1;
  bl = dot && dot > sl ? (size_t)(dot - path) : strlen(path);
  if (bl >= sizeof base) bl = sizeof base - 1;
  memcpy(base, path, bl); base[bl] = 0;
  val[0] = 0;
  if (namecol >= 0 && mi && mi->data && namecol < mi->data->ncols) {
    int v = merge_active(mi, rec);
    if (v >= 0) snprintf(val, sizeof val, "%s", mi->data->cell[mi->view[v]][namecol]);
  }
  safe_name(val, safe, sizeof safe);
  if (safe[0]) {
    /* the value as the file's name, in the folder path names */
    size_t dl = (size_t)(sl - path);
    if (dl >= sizeof base) dl = sizeof base - 1;
    snprintf(out, cap, "%.*s%s.pdf", (int)dl, path, safe);
    if (!dl) snprintf(out, cap, "%s.pdf", safe);
  } else snprintf(out, cap, "%s_%d.pdf", base, rec + 1);
}
int merge_to_pdf(Doc *d, int from, int to, int each, int namecol, const char *path, FontSet *fs, MergeAsk ask, void *ctx, int *files, Buf *errs)
{
  MergeInfo *mi = d->merge;
  int n = 0, r, got;
  Doc *m;
  if (files) *files = 0;
  if (!mi || !merge_count(mi)) return 0;
  if (!each) {
    if (!(m = merge_build(d, from, to, ask, ctx, &got, errs))) return 0;
    if (save_pdf(m, fs, path)) n = 1;
    doc_free(m);
  } else {
    if (from < 0) from = 0;
    if (to >= merge_count(mi)) to = merge_count(mi) - 1;
    for (r = from; r <= to; r++) {
      char name[600];
      if (!(m = merge_build(d, r, r, ask, ctx, &got, errs))) continue;
      merge_pdf_name(path, mi, r, namecol, name, sizeof name);
      if (save_pdf(m, fs, name)) n++;
      else if (errs) buf_printf(errs, "Quill could not write %s.\n", name);
      doc_free(m);
    }
  }
  if (files) *files = n;
  return n > 0;
}
