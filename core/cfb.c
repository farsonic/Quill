/*
 * cfb.c - OLE compound files read and written (see cfb.h), from [MS-CFB].
 */
#include <stdlib.h>
#include <string.h>
#include "cfb.h"

#define FREESECT 0xFFFFFFFFUL
#define ENDCHAIN 0xFFFFFFFEUL
#define FATSECT  0xFFFFFFFDUL
#define DIFSECT  0xFFFFFFFCUL

static u32 le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static u32 le16(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8; }
static void put32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
static void put16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }

static const u8 sig[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
int cfb_is(const u8 *data, size_t len) { return len >= 512 && !memcmp(data, sig, 8); }

/* ------------------------------------------------------------------ reading */
/* sector s's bytes (NULL if past the file); a short last sector is allowed, *avail says how much there is */
static const u8 *sector(const Cfb *c, u32 s, u32 *avail)
{
  size_t off = (size_t)c->ssize * ((size_t)s + 1);
  if (s >= 0xFFFFFFF0UL || off >= c->len || off / c->ssize != (size_t)s + 1) return NULL;
  *avail = c->len - off < c->ssize ? (u32)(c->len - off) : c->ssize;
  return c->data + off;
}
static u32 nsectors(const Cfb *c) { return (u32)((c->len - c->ssize + c->ssize - 1) / c->ssize); }
/* a chain of sectors (big: in the file, else mini sectors in the mini stream) read into out (size bytes;
   size ~0: as long as the chain, *got how much); 0 if the chain is damaged or too short */
static u8 *read_chain(const Cfb *c, u32 s, int big, size_t size, size_t *got)
{
  u32 unit = big ? c->ssize : 64, limit = big ? nsectors(c) : c->nmfat, steps = 0;
  size_t cap, o = 0;
  u8 *out;
  if (size == (size_t)-1) {
    /* measure first */
    u32 t = s, k = 0;
    while (t != ENDCHAIN) {
      if (t >= (big ? c->nfat : c->nmfat) || ++k > limit) return NULL;
      t = big ? c->fat[t] : c->mfat[t];
    }
    size = (size_t)k * unit;
  }
  cap = size;
  if (!(out = (u8 *)malloc(cap + 1))) return NULL;
  while (o < size) {
    u32 avail, k;
    const u8 *src;
    if (s == ENDCHAIN || ++steps > limit + 1) break;
    if (big) {
      if (s >= c->nfat || !(src = sector(c, s, &avail))) break;
    } else {
      if (s >= c->nmfat || (size_t)s * 64 + 64 > c->nmini) break;
      src = c->mini + (size_t)s * 64; avail = 64;
    }
    k = unit;
    if (k > size - o) k = (u32)(size - o);
    if (k > avail) { memcpy(out + o, src, avail); memset(out + o + avail, 0, k - avail); }
    else memcpy(out + o, src, k);
    o += k;
    s = big ? c->fat[s] : c->mfat[s];
  }
  if (o < size) { free(out); return NULL; }
  out[size] = 0;
  if (got) *got = size;
  return out;
}
/* every entry's parent, walking each storage's tree (cycles and shared nodes refused by marking) */
static void set_parents(Cfb *c)
{
  int *stack, sp = 0, i;
  u8 *seen = (u8 *)calloc((size_t)c->n, 1);
  stack = (int *)malloc(sizeof(int) * (size_t)(c->n * 2 + 2));
  for (i = 0; i < c->n; i++) c->e[i].parent = -2;
  if (!seen || !stack) { free(seen); free(stack); return; }
  c->e[0].parent = -1; seen[0] = 1;
  /* (storage, node) pairs to visit: node's subtree are children of storage */
  if (c->e[0].child < (u32)c->n) { stack[sp++] = 0; stack[sp++] = (int)c->e[0].child; }
  while (sp > 0) {
    int node = stack[--sp], st = stack[--sp];
    CfbEntry *e;
    if (node < 0 || node >= c->n || seen[node]) continue;
    seen[node] = 1;
    e = &c->e[node];
    e->parent = st;
    if (sp + 6 > c->n * 2 + 2) continue;
    if (e->left < (u32)c->n) { stack[sp++] = st; stack[sp++] = (int)e->left; }
    if (e->right < (u32)c->n) { stack[sp++] = st; stack[sp++] = (int)e->right; }
    if (e->type == CFB_STORAGE && e->child < (u32)c->n) { stack[sp++] = node; stack[sp++] = (int)e->child; }
  }
  free(seen); free(stack);
}
void cfb_close(Cfb *c)
{
  free(c->fat); free(c->mfat); free(c->mini); free(c->e);
  memset(c, 0, sizeof *c);
}
int cfb_open(Cfb *c, const u8 *data, size_t len)
{
  const u8 *h = data;
  u32 nfs, firstdir, firstmfat, nmfs, difat, ndif, per, i, k, ns, *fs = NULL, nfsg = 0;
  size_t dl;
  u8 *dir;
  memset(c, 0, sizeof *c);
  if (!cfb_is(data, len)) return 0;
  c->data = data; c->len = len;
  if (le16(h + 30) == 12) c->ssize = 4096; else if (le16(h + 30) == 9) c->ssize = 512; else return 0;
  if (len < c->ssize) return 0;
  c->cutoff = le32(h + 56);
  if (c->cutoff == 0 || c->cutoff > 0x10000UL) c->cutoff = 4096;
  nfs = le32(h + 44); firstdir = le32(h + 48); firstmfat = le32(h + 60); nmfs = le32(h + 64);
  difat = le32(h + 68); ndif = le32(h + 72);
  ns = nsectors(c);
  if (nfs > ns || nfs == 0) return 0;
  per = c->ssize / 4;
  /* the FAT's sectors: 109 in the header, the rest in the DIFAT chain */
  if (!(fs = (u32 *)malloc(sizeof(u32) * nfs))) return 0;
  for (i = 0; i < 109 && nfsg < nfs; i++) { u32 v = le32(h + 76 + 4 * i); if (v < 0xFFFFFFF0UL) fs[nfsg++] = v; }
  for (k = 0; nfsg < nfs && difat < 0xFFFFFFF0UL && k <= ndif && k <= ns; k++) {
    u32 avail;
    const u8 *s = sector(c, difat, &avail);
    if (!s || avail < c->ssize) break;
    for (i = 0; i + 1 < per && nfsg < nfs; i++) { u32 v = le32(s + 4 * i); if (v < 0xFFFFFFF0UL) fs[nfsg++] = v; }
    difat = le32(s + 4 * (per - 1));
  }
  if (nfsg < nfs) { free(fs); return 0; }
  c->nfat = nfs * per;
  if (!(c->fat = (u32 *)malloc(sizeof(u32) * c->nfat))) { free(fs); return 0; }
  for (i = 0; i < nfs; i++) {
    u32 avail, j;
    const u8 *s = sector(c, fs[i], &avail);
    if (!s) { free(fs); cfb_close(c); return 0; }
    for (j = 0; j < per; j++) c->fat[i * per + j] = 4 * j + 4 <= avail ? le32(s + 4 * j) : FREESECT;
  }
  free(fs);
  /* the mini FAT */
  if (nmfs && firstmfat < 0xFFFFFFF0UL) {
    size_t ml;
    u8 *m;
    c->nmfat = 0;   /* (read_chain of big sectors doesn't look at it) */
    if ((m = read_chain(c, firstmfat, 1, (size_t)-1, &ml))) {
      u32 j;
      c->nmfat = (u32)(ml / 4);
      if ((c->mfat = (u32 *)malloc(sizeof(u32) * (c->nmfat ? c->nmfat : 1))))
        for (j = 0; j < c->nmfat; j++) c->mfat[j] = le32(m + 4 * j);
      else c->nmfat = 0;
      free(m);
    }
  }
  /* the directory */
  if (!(dir = read_chain(c, firstdir, 1, (size_t)-1, &dl)) || dl < 128) { free(dir); cfb_close(c); return 0; }
  c->n = (int)(dl / 128);
  if (!(c->e = (CfbEntry *)calloc((size_t)c->n, sizeof(CfbEntry)))) { free(dir); cfb_close(c); return 0; }
  for (i = 0; i < (u32)c->n; i++) {
    const u8 *d = dir + 128 * i;
    CfbEntry *e = &c->e[i];
    u32 nl = le16(d + 64), j, o = 0;
    if (nl > 64) nl = 64;
    for (j = 0; j + 1 < nl; j += 2) {
      u32 ch = le16(d + j);
      char tmp[4];
      int w;
      if (!ch) break;
      w = utf8_put(tmp, ch);
      if (o + (u32)w + 1 > sizeof e->name) break;
      memcpy(e->name + o, tmp, (size_t)w); o += (u32)w;
    }
    e->name[o] = 0;
    e->type = d[66];
    e->left = le32(d + 68); e->right = le32(d + 72); e->child = le32(d + 76);
    memcpy(e->clsid, d + 80, 16);
    e->start = le32(d + 116);
    e->size = le32(d + 120);
    if (le32(d + 124) && c->ssize == 4096) e->size = 0xFFFFFFFFUL;    /* (over 4 GB: refused when read) */
  }
  free(dir);
  if (c->e[0].type != CFB_ROOT) { cfb_close(c); return 0; }
  /* the mini stream: the root's chain */
  if (c->e[0].size && c->e[0].start < 0xFFFFFFF0UL) {
    size_t got;
    c->mini = read_chain(c, c->e[0].start, 1, (size_t)-1, &got);
    if (c->mini) c->nmini = (u32)(got < c->e[0].size ? got : c->e[0].size);
  }
  set_parents(c);
  return 1;
}
static int name_eq(const char *a, const char *b, size_t bn)
{
  size_t i;
  for (i = 0; i < bn; i++) {
    char x = a[i], y = b[i];
    if (!x) return 0;
    if (x >= 'a' && x <= 'z') x = (char)(x - 32);
    if (y >= 'a' && y <= 'z') y = (char)(y - 32);
    if (x != y) return 0;
  }
  return a[bn] == 0;
}
int cfb_child(const Cfb *c, int parent, const char *name)
{
  int i;
  for (i = 1; i < c->n; i++)
    if (c->e[i].parent == parent && c->e[i].type != CFB_EMPTY && name_eq(c->e[i].name, name, strlen(name))) return i;
  return -1;
}
int cfb_find(const Cfb *c, const char *path)
{
  int cur = 0;
  while (*path) {
    const char *sl = strchr(path, '/');
    size_t k = sl ? (size_t)(sl - path) : strlen(path);
    int i, f = -1;
    for (i = 1; i < c->n && f < 0; i++)
      if (c->e[i].parent == cur && c->e[i].type != CFB_EMPTY && name_eq(c->e[i].name, path, k)) f = i;
    if (f < 0) return -1;
    cur = f;
    path += k + (sl ? 1 : 0);
  }
  return cur;
}
u8 *cfb_read(const Cfb *c, int i, size_t *len)
{
  const CfbEntry *e;
  u8 *out;
  if (i < 0 || i >= c->n) return NULL;
  e = &c->e[i];
  if (e->type != CFB_STREAM || e->size == 0xFFFFFFFFUL) return NULL;
  if (e->size == 0) { if ((out = (u8 *)malloc(1))) { out[0] = 0; *len = 0; } return out; }
  if (e->size < c->cutoff) out = read_chain(c, e->start, 0, e->size, len);
  else out = read_chain(c, e->start, 1, e->size, len);
  return out;
}
u8 *cfb_read_path(const Cfb *c, const char *path, size_t *len) { return cfb_read(c, cfb_find(c, path), len); }

/* ------------------------------------------------------------------ writing */
typedef struct { u16 name[32]; int nlen; u8 type; int parent; u8 *data; u32 size; u32 start; u32 left, right, child; u8 color; } WEnt;
struct CfbWriter { WEnt *e; int n, cap; int oom; };

CfbWriter *cfb_writer(void)
{
  CfbWriter *w = (CfbWriter *)calloc(1, sizeof *w);
  if (!w) return NULL;
  if (!(w->e = (WEnt *)calloc(8, sizeof(WEnt)))) { free(w); return NULL; }
  w->cap = 8; w->n = 1;
  w->e[0].type = CFB_ROOT; w->e[0].parent = -1;
  { const char *r = "Root Entry"; int i; for (i = 0; r[i]; i++) w->e[0].name[i] = (u16)r[i]; w->e[0].nlen = i; }
  return w;
}
void cfb_writer_free(CfbWriter *w)
{
  int i;
  if (!w) return;
  for (i = 0; i < w->n; i++) free(w->e[i].data);
  free(w->e); free(w);
}
static int wname(const char *s, size_t n, u16 *out)
{
  size_t i = 0;
  int k = 0;
  while (i < n) {
    int used;
    u32 ch = utf8_get(s + i, n - i, &used);
    if (k >= 31 || ch > 0xFFFF || ch == '/' || ch == '\\' || ch == ':' || ch == '!') return -1;
    out[k++] = (u16)ch;
    i += (size_t)used;
  }
  return k;
}
static int wfind(CfbWriter *w, int parent, const u16 *name, int nlen)
{
  int i;
  for (i = 1; i < w->n; i++)
    if (w->e[i].parent == parent && w->e[i].nlen == nlen && !memcmp(w->e[i].name, name, sizeof(u16) * (size_t)nlen)) return i;
  return -1;
}
static int wnew(CfbWriter *w, int parent, const u16 *name, int nlen, u8 type)
{
  WEnt *e;
  if (w->n >= w->cap) {
    WEnt *ne = (WEnt *)realloc(w->e, sizeof(WEnt) * (size_t)w->cap * 2);
    if (!ne) { w->oom = 1; return -1; }
    w->e = ne; w->cap *= 2;
  }
  e = &w->e[w->n];
  memset(e, 0, sizeof *e);
  memcpy(e->name, name, sizeof(u16) * (size_t)nlen); e->nlen = nlen;
  e->type = type; e->parent = parent;
  return w->n++;
}
int cfb_add(CfbWriter *w, const char *path, const void *data, size_t len)
{
  int cur = 0;
  if (!w || w->oom) return 0;
  while (*path) {
    const char *sl = strchr(path, '/');
    size_t k = sl ? (size_t)(sl - path) : strlen(path);
    u16 nm[32];
    int nl = wname(path, k, nm), f;
    if (nl <= 0) return 0;
    f = wfind(w, cur, nm, nl);
    if (sl) {
      if (f < 0) f = wnew(w, cur, nm, nl, CFB_STORAGE);
      if (f < 0 || w->e[f].type != CFB_STORAGE) return 0;
    } else {
      if (f >= 0) return 0;                 /* (already there) */
      if ((f = wnew(w, cur, nm, nl, CFB_STREAM)) < 0) return 0;
      if (len && !(w->e[f].data = (u8 *)malloc(len))) { w->oom = 1; return 0; }
      if (len) memcpy(w->e[f].data, data, len);
      w->e[f].size = (u32)len;
    }
    cur = f;
    path += k + (sl ? 1 : 0);
  }
  return 1;
}
/* [MS-CFB]'s order of siblings: shorter names first, then by upper-cased characters */
static u16 up16(u16 c) { return (c >= 'a' && c <= 'z') ? (u16)(c - 32) : c; }
static CfbWriter *sort_w;
static int wcmp(const void *a, const void *b)
{
  const WEnt *x = &sort_w->e[*(const int *)a], *y = &sort_w->e[*(const int *)b];
  int i;
  if (x->nlen != y->nlen) return x->nlen - y->nlen;
  for (i = 0; i < x->nlen; i++) if (up16(x->name[i]) != up16(y->name[i])) return (int)up16(x->name[i]) - (int)up16(y->name[i]);
  return 0;
}
/* a balanced tree of sorted ids[lo..hi): its root; nodes on the deepest level red when the tree isn't
   full, so every path has as many black nodes (a valid red-black tree) */
static u32 wtree(CfbWriter *w, const int *ids, int lo, int hi, int depth, int deep)
{
  int mid;
  WEnt *e;
  if (lo >= hi) return CFB_NONE;
  mid = (lo + hi) / 2;
  e = &w->e[ids[mid]];
  e->left = wtree(w, ids, lo, mid, depth + 1, deep);
  e->right = wtree(w, ids, mid + 1, hi, depth + 1, deep);
  e->color = (u8)(depth == deep ? 0 : 1);
  return (u32)ids[mid];
}
static int log2up(int n) { int d = 0; while ((1 << d) < n + 1) d++; return d; }   /* levels of a balanced tree of n */
static void cfb_put_entry(u8 *d, const WEnt *e);
u8 *cfb_finish(CfbWriter *w, size_t *len)
{
  const u32 ss = 512, per = 128;
  u32 nmini = 0, nbig = 0, ndir, nmfs, nms, nfs = 0, ndif = 0, total, s, i, j;
  u32 dir0, mfat0, mini0, big0;
  int *ids = NULL, k;
  u8 *out = NULL, *p;
  u32 *fat = NULL, *mfat = NULL;
  size_t size;
  if (!w || w->oom) { cfb_writer_free(w); return NULL; }
  /* where each stream goes: mini sectors (under 4096 bytes) or sectors */
  for (k = 1; k < w->n; k++) {
    WEnt *e = &w->e[k];
    if (e->type != CFB_STREAM) continue;
    if (!e->size) e->start = ENDCHAIN;
    else if (e->size < 4096) { e->start = nmini; nmini += (e->size + 63) / 64; }
    else { e->start = nbig; nbig += (e->size + ss - 1) / ss; }
  }
  ndir = ((u32)w->n * 128 + ss - 1) / ss;
  nmfs = (nmini * 4 + ss - 1) / ss;
  nms = (nmini * 64 + ss - 1) / ss;
  /* FAT sectors (and DIFAT ones past the header's 109) enough for everything including themselves */
  for (;;) {
    total = nfs + ndif + ndir + nmfs + nms + nbig;
    if (nfs * per >= total) break;
    nfs++;
    ndif = nfs > 109 ? (nfs - 109 + per - 2) / (per - 1) : 0;
  }
  dir0 = nfs + ndif; mfat0 = dir0 + ndir; mini0 = mfat0 + nmfs; big0 = mini0 + nms;
  size = (size_t)ss * (total + 1);
  fat = (u32 *)malloc(sizeof(u32) * nfs * per);
  mfat = (u32 *)malloc(sizeof(u32) * (nmfs * per + 1));
  ids = (int *)malloc(sizeof(int) * (size_t)w->n);
  out = (u8 *)calloc(size, 1);
  if (!fat || !mfat || !ids || !out) goto fail;
  for (i = 0; i < nfs * per; i++) fat[i] = FREESECT;
  for (i = 0; i < nmfs * per; i++) mfat[i] = FREESECT;
  for (i = 0; i < nfs; i++) fat[i] = FATSECT;
  for (i = 0; i < ndif; i++) fat[nfs + i] = DIFSECT;
#define CHAIN(tab, first, n) do { for (j = 0; j < (n); j++) (tab)[(first) + j] = j + 1 < (n) ? (first) + j + 1 : ENDCHAIN; } while (0)
  CHAIN(fat, dir0, ndir);
  CHAIN(fat, mfat0, nmfs);
  CHAIN(fat, mini0, nms);
  for (k = 1; k < w->n; k++) {
    WEnt *e = &w->e[k];
    if (e->type != CFB_STREAM || !e->size) continue;
    if (e->size < 4096) {
      u32 n = (e->size + 63) / 64;
      CHAIN(mfat, e->start, n);
      memcpy(out + ss * (1 + mini0) + 64 * e->start, e->data, e->size);
    } else {
      u32 n = (e->size + ss - 1) / ss;
      e->start += big0;
      CHAIN(fat, e->start, n);
      memcpy(out + ss * (1 + e->start), e->data, e->size);
    }
  }
#undef CHAIN
  w->e[0].start = nms ? mini0 : ENDCHAIN;
  w->e[0].size = nmini * 64;
  /* each storage's children as a tree */
  for (k = 0; k < w->n; k++) {
    int n = 0, m;
    w->e[k].child = CFB_NONE;
    if (w->e[k].type == CFB_STREAM) continue;
    for (m = 1; m < w->n; m++) if (w->e[m].parent == k) ids[n++] = m;
    if (!n) continue;
    sort_w = w;
    qsort(ids, (size_t)n, sizeof(int), wcmp);
    {
      int levels = log2up(n);
      int deep = (1 << levels) - 1 == n ? -1 : levels - 1;   /* (a full tree: all black) */
      w->e[k].child = wtree(w, ids, 0, n, 0, deep);
    }
  }
  w->e[0].left = w->e[0].right = CFB_NONE; w->e[0].color = 1;
  /* the header */
  p = out;
  memcpy(p, sig, 8);
  put16(p + 24, 0x3E); put16(p + 26, 3); put16(p + 28, 0xFFFE); put16(p + 30, 9); put16(p + 32, 6);
  put32(p + 44, nfs); put32(p + 48, dir0); put32(p + 56, 4096);
  put32(p + 60, nmfs ? mfat0 : ENDCHAIN); put32(p + 64, nmfs);
  put32(p + 68, ndif ? nfs : ENDCHAIN); put32(p + 72, ndif);
  for (i = 0; i < 109; i++) put32(p + 76 + 4 * i, i < nfs ? i : FREESECT);
  /* the DIFAT sectors */
  for (i = 0; i < ndif; i++) {
    u8 *d = out + ss * (1 + nfs + i);
    for (j = 0; j < per - 1; j++) { u32 f = 109 + i * (per - 1) + j; put32(d + 4 * j, f < nfs ? f : FREESECT); }
    put32(d + 4 * (per - 1), i + 1 < ndif ? nfs + i + 1 : ENDCHAIN);
  }
  /* the FAT, the mini FAT, the directory */
  for (i = 0; i < nfs * per; i++) put32(out + ss + 4 * i, fat[i]);
  for (i = 0; i < nmfs * per; i++) put32(out + ss * (1 + mfat0) + 4 * i, mfat[i]);
  for (s = 0; s < ndir * 4; s++) {
    u8 *d = out + ss * (1 + dir0) + 128 * s;
    if (s < (u32)w->n) cfb_put_entry(d, &w->e[s]);
    else { put32(d + 68, CFB_NONE); put32(d + 72, CFB_NONE); put32(d + 76, CFB_NONE); }
  }
  free(fat); free(mfat); free(ids);
  cfb_writer_free(w);
  *len = size;
  return out;
fail:
  free(fat); free(mfat); free(ids); free(out);
  cfb_writer_free(w);
  return NULL;
}
static void cfb_put_entry(u8 *d, const WEnt *e)
{
  int i;
  for (i = 0; i < e->nlen; i++) put16(d + 2 * i, e->name[i]);
  put16(d + 64, (u32)(e->nlen + 1) * 2);
  d[66] = e->type; d[67] = e->color;
  put32(d + 68, e->left); put32(d + 72, e->right); put32(d + 76, e->child);
  put32(d + 116, e->type == CFB_STORAGE ? 0 : e->start);
  put32(d + 120, e->type == CFB_STORAGE ? 0 : e->size);
}
