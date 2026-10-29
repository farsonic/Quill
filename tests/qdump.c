/* qdump - print what Quill reads from a document (a development aid): qdump file [layout | pdf out.pdf] */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "../core/docx.h"
#include "../core/layout.h"
#include "../core/pdf.h"

int main(int argc, char **argv)
{
  char err[256];
  unsigned lost;
  Doc *d;
  u32 i, k;
  if (argc < 2) return 1;
  if (!(d = doc_load_file(argv[1], &lost, err, sizeof err))) { printf("error: %s\n", err); return 1; }
  printf("page %ldx%ld margins %ld/%ld/%ld/%ld, %lu paragraphs, %d styles, %d lists, %d fonts, lost %#x, title [%s] author [%s]\n",
         (long)d->page.width, (long)d->page.height, (long)d->page.top, (long)d->page.right, (long)d->page.bottom, (long)d->page.left,
         (unsigned long)d->np, d->nstyles, d->nlists, d->nfonts, lost, d->title, d->author);
  if (argc > 3 && !strcmp(argv[2], "pdf")) {
    FontSet fs; Layout *L; clock_t t0; int r;
    fonts_init(&fs, "fonts");
    L = lay_new(d, &fs);
    t0 = clock();
    r = pdf_save_file(L, argv[3]);
    printf("pdf: %s, %lu pages in %.3f s CPU\n", r ? "written" : "FAILED", (unsigned long)L->npages, (double)(clock() - t0) / CLOCKS_PER_SEC);
    lay_free(L); fonts_free(&fs); doc_free(d);
    return !r;
  }
  if (argc > 2 && !strcmp(argv[2], "layout")) {
    FontSet fs; Layout *L; clock_t t0;
    fonts_init(&fs, "fonts");
    L = lay_new(d, &fs);
    t0 = clock();
    lay_update(L);
    printf("layout: %lu pages, %lu lines in %.3f s CPU\n", (unsigned long)L->npages, (unsigned long)L->nplaced, (double)(clock() - t0) / CLOCKS_PER_SEC);
    {
      u32 nb = 0;
      for (i = 0; i < L->npl; i++) nb += L->pl[i].nboxes;
      printf("boxes: %lu (%lu KB)\n", (unsigned long)nb, (unsigned long)(nb * sizeof(Box) / 1024));
    }
    lay_free(L); fonts_free(&fs); doc_free(d);
    return 0;
  }
  for (i = 0; i < d->np && i < 60; i++) {
    Para *p = d->p[i];
    printf("%2lu [%s] al%d L%ld F%ld B%ld A%ld ln%ld%s list%u.%u%s%s:", (unsigned long)i, d->styles[p->pf.style].name, p->pf.align, (long)p->pf.left,
           (long)p->pf.first, (long)p->pf.before, (long)p->pf.after, (long)p->pf.line, p->pf.linerule ? "x" : "", (unsigned)p->pf.list, (unsigned)p->pf.level,
           p->pf.keepnext ? " keep" : "", p->pf.pagebreak ? " BREAK" : "");
    {
      u32 s = 0;
      for (k = 0; k < p->nr; k++) {
        const CharFmt *f = &p->r[k].f;
        printf(" <%s %u%s%s%s%s%s>", d->fonts[f->font], (unsigned)f->size, f->flags & CF_BOLD ? " B" : "", f->flags & CF_ITALIC ? " I" : "",
               f->flags & CF_UNDERLINE ? " U" : "", f->flags & (CF_SUPER | CF_SUB) ? " sup/sub" : "", f->color != COLOR_AUTO ? " col" : "");
        printf("%.*s", (int)(p->r[k].end - s > 60 ? 60 : p->r[k].end - s), p->t + s);
        s = p->r[k].end;
      }
      if (!p->len) printf(" <%s %u>", d->fonts[p->r[0].f.font], (unsigned)p->r[0].f.size);
    }
    printf("\n");
  }
  for (k = 0; k < (u32)d->nlists && k < 4; k++) printf("list %u: fmt %d text [%s] left %ld hang %ld\n", (unsigned)d->lists[k].id, d->lists[k].lvl[0].fmt, d->lists[k].lvl[0].text, (long)d->lists[k].lvl[0].left, (long)d->lists[k].lvl[0].hanging);
  doc_free(d);
  return 0;
}
