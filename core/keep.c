/* keep.c - see keep.h */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "keep.h"

/* the ids docx_save gives its own relationships: rId1-rId3 (styles, numbering, settings; and the
   package's officeDocument and core properties), rIdH.., rIdF.., rIdFn, rIdEn, rIdCm, rIdLk.., rIdImg..,
   and rIdK.., which keep_add hands out */
int keep_id_reserved(const char *id)
{
  static const char *const pre[] = { "rIdH", "rIdF", "rIdEn", "rIdCm", "rIdLk", "rIdImg", "rIdK" };
  size_t i;
  if (!id || !id[0]) return 1;
  if (!strcmp(id, "rId1") || !strcmp(id, "rId2") || !strcmp(id, "rId3")) return 1;
  for (i = 0; i < sizeof pre / sizeof pre[0]; i++) if (!strncmp(id, pre[i], strlen(pre[i]))) return 1;
  return 0;
}
int keep_find(const Doc *d, const char *name)
{
  int i;
  for (i = 0; i < d->nkept; i++) if (!strcmp(d->kept[i].name, name)) return i;
  return -1;
}
/* (a copy of s, or NULL for NULL; *bad set when memory runs out) */
static char *dup0(const char *s, int *bad)
{
  char *r;
  if (!s) return NULL;
  if (!(r = str_dup(s))) *bad = 1;
  return r;
}
static void part_free(KeptPart *k)
{
  free(k->name); free(k->ctype); free(k->owner); free(k->reltype); free(k->relid); free(k->data);
}
int keep_add(Doc *d, const char *name, const char *ctype, const char *owner, const char *reltype, const char *relid,
             const void *data, size_t len)
{
  KeptPart k, *nk;
  int bad = 0, i;
  char idbuf[24];
  if (!name || keep_find(d, name) >= 0) return -1;
  if (owner) {
    /* an id of its own in its owner's relationships */
    int taken = keep_id_reserved(relid);
    for (i = 0; i < d->nkept && !taken; i++)
      if (d->kept[i].owner && !strcmp(d->kept[i].owner, owner) && d->kept[i].relid && !strcmp(d->kept[i].relid, relid)) taken = 1;
    if (taken) {
      int n = 1;
      for (;;) {
        snprintf(idbuf, sizeof idbuf, "rIdK%d", n++);
        for (i = 0; i < d->nkept; i++) if (d->kept[i].relid && !strcmp(d->kept[i].relid, idbuf)) break;
        if (i == d->nkept) break;
      }
      relid = idbuf;
    }
  } else relid = NULL, reltype = NULL;
  memset(&k, 0, sizeof k);
  k.name = dup0(name, &bad); k.ctype = dup0(ctype, &bad); k.owner = dup0(owner, &bad);
  k.reltype = dup0(reltype, &bad); k.relid = dup0(relid, &bad);
  if (!bad && !(k.data = (u8 *)malloc(len ? len : 1))) bad = 1;
  if (!bad && !(nk = (KeptPart *)realloc(d->kept, (size_t)(d->nkept + 1) * sizeof *nk))) bad = 1;
  if (bad) { part_free(&k); return -1; }
  if (len) memcpy(k.data, data, len);
  k.len = len;
  d->kept = nk;
  d->kept[d->nkept] = k;
  return d->nkept++;
}
void keep_remove(Doc *d, const char *name)
{
  int i = keep_find(d, name);
  if (i < 0) return;
  part_free(&d->kept[i]);
  memmove(&d->kept[i], &d->kept[i + 1], (size_t)(d->nkept - i - 1) * sizeof d->kept[0]);
  d->nkept--;
}
void keep_free(Doc *d)
{
  int i;
  for (i = 0; i < d->nkept; i++) part_free(&d->kept[i]);
  free(d->kept);
  d->kept = NULL; d->nkept = 0;
}
