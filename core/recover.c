/* recover.c - WP24, see recover.h */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "recover.h"
#include "docx.h"

static char recdir[256], verdir[256];
void rec_set_dirs(const char *r, const char *v)
{
  snprintf(recdir, sizeof recdir, "%s", r ? r : "");
  snprintf(verdir, sizeof verdir, "%s", v ? v : "");
}
void rec_join(const char *dir, const char *name, char *out, size_t cap)
{
  size_t n = strlen(dir);
  if (!n || dir[n - 1] == ':' || dir[n - 1] == '/') snprintf(out, cap, "%s%s", dir, name);
  else snprintf(out, cap, "%s/%s", dir, name);
}
/* a whole file into memory (malloc'd), NULL if it can't be read */
static u8 *slurp(const char *path, size_t *n)
{
  FILE *f = fopen(path, "rb");
  u8 *b = NULL;
  long len;
  *n = 0;
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0 && (b = (u8 *)malloc((size_t)len + 1))) {
    if (fread(b, 1, (size_t)len, f) != (size_t)len) { free(b); b = NULL; }
    else *n = (size_t)len;
  }
  fclose(f);
  return b;
}
static int spit(const char *path, const void *data, size_t n)
{
  FILE *f = fopen(path, "wb");
  int ok;
  if (!f) return 0;
  ok = fwrite(data, 1, n, f) == n;
  if (fclose(f) != 0) ok = 0;
  if (!ok) remove(path);
  return ok;
}
/* a field of a tab-separated line (copied, cut at the next tab), the rest after it */
static const char *field(const char *s, char *out, size_t cap)
{
  size_t n = strcspn(s, "\t\r\n");
  if (cap) { size_t m = n < cap - 1 ? n : cap - 1; memcpy(out, s, m); out[m] = 0; }
  s += n;
  return *s == '\t' ? s + 1 : s;
}

/* ---- recovery copies */
static void rec_index(char *out, size_t cap) { rec_join(recdir, "Recovery.index", out, cap); }
void rec_file(int id, char *out, size_t cap)
{
  char nm[32];
  sprintf(nm, "rec%d.docx", id);
  rec_join(recdir, nm, out, cap);
}
int rec_list(RecEntry *out, int max)
{
  char path[300], line[700];
  FILE *f;
  int n = 0;
  rec_index(path, sizeof path);
  if (!(f = fopen(path, "r"))) return 0;
  while (n < max && fgets(line, sizeof line, f)) {
    const char *s = line;
    char id[16];
    s = field(s, id, sizeof id);
    if (!id[0]) continue;
    out[n].id = atoi(id);
    s = field(s, out[n].stamp, sizeof out[n].stamp);
    s = field(s, out[n].path, sizeof out[n].path);
    field(s, out[n].name, sizeof out[n].name);
    n++;
  }
  fclose(f);
  return n;
}
int rec_new_id(void)
{
  RecEntry e[64];
  int n = rec_list(e, 64), i, top = 0;
  for (i = 0; i < n; i++) if (e[i].id > top) top = e[i].id;
  return top + 1;
}
/* the index written again without id's line, then with (keep) a line for it */
static int rec_write_index(int id, const RecEntry *keep)
{
  RecEntry e[64];
  int n = rec_list(e, 64), i;
  char path[300];
  FILE *f;
  rec_index(path, sizeof path);
  if (!(f = fopen(path, "w"))) return 0;
  for (i = 0; i < n; i++) if (e[i].id != id) fprintf(f, "%d\t%s\t%s\t%s\n", e[i].id, e[i].stamp, e[i].path, e[i].name);
  if (keep) fprintf(f, "%d\t%s\t%s\t%s\n", keep->id, keep->stamp, keep->path, keep->name);
  return fclose(f) == 0;
}
/* tabs and line ends out of a field */
static void clean(char *s) { for (; *s; s++) if (*s == '\t' || *s == '\n' || *s == '\r') *s = ' '; }
int rec_save(int id, Doc *d, const char *path, const char *name, const char *stamp)
{
  char file[300];
  size_t n;
  u8 *data = docx_save(d, &n);
  RecEntry e;
  int ok;
  if (!data) return 0;
  rec_file(id, file, sizeof file);
  ok = spit(file, data, n);
  free(data);
  if (!ok) return 0;
  memset(&e, 0, sizeof e);
  e.id = id;
  snprintf(e.stamp, sizeof e.stamp, "%s", stamp ? stamp : ""); snprintf(e.path, sizeof e.path, "%s", path ? path : "");
  snprintf(e.name, sizeof e.name, "%s", name ? name : "");
  clean(e.stamp); clean(e.path); clean(e.name);
  return rec_write_index(id, &e);
}
void rec_drop(int id)
{
  char file[300];
  rec_file(id, file, sizeof file);
  remove(file);
  rec_write_index(id, NULL);
}

/* ---- versions */
/* the key a document's path gives (FNV-1a, case folded as AmigaDOS names are) */
static void ver_key(const char *docpath, char *out)
{
  u32 h = 2166136261UL;
  const char *s;
  for (s = docpath; *s; s++) { u8 c = (u8)*s; if (c >= 'A' && c <= 'Z') c = (u8)(c + 32); h = (h ^ c) * 16777619UL; }
  sprintf(out, "%08lx", (unsigned long)(h & 0xFFFFFFFFUL));
}
static void ver_index(const char *docpath, char *out, size_t cap)
{
  char k[16], nm[24];
  ver_key(docpath, k);
  sprintf(nm, "%s.idx", k);
  rec_join(verdir, nm, out, cap);
}
void ver_file(const char *docpath, int n, char *out, size_t cap)
{
  char k[16], nm[40];
  ver_key(docpath, k);
  sprintf(nm, "%s-%d.docx", k, n);
  rec_join(verdir, nm, out, cap);
}
/* the index's entries, oldest first (and whether its first line names this path) */
static int ver_read(const char *docpath, VerEntry *out, int max)
{
  char path[300], line[400];
  FILE *f;
  int n = 0, first = 1;
  ver_index(docpath, path, sizeof path);
  if (!(f = fopen(path, "r"))) return 0;
  while (n < max && fgets(line, sizeof line, f)) {
    const char *s = line;
    char a[16], sz[24], kind[4];
    if (first) { first = 0; line[strcspn(line, "\r\n")] = 0; if (strcmp(line, docpath)) break; continue; }   /* (another path with the same key) */
    s = field(s, a, sizeof a);
    if (!a[0]) continue;
    out[n].n = atoi(a);
    s = field(s, out[n].stamp, sizeof out[n].stamp);
    s = field(s, sz, sizeof sz);
    field(s, kind, sizeof kind);
    out[n].size = atol(sz); out[n].kind = kind[0] ? kind[0] : 's';
    n++;
  }
  fclose(f);
  return n;
}
static int ver_write(const char *docpath, const VerEntry *e, int n)
{
  char path[300];
  FILE *f;
  int i;
  ver_index(docpath, path, sizeof path);
  if (!(f = fopen(path, "w"))) return 0;
  fprintf(f, "%s\n", docpath);
  for (i = 0; i < n; i++) fprintf(f, "%d\t%s\t%ld\t%c\n", e[i].n, e[i].stamp, e[i].size, e[i].kind);
  return fclose(f) == 0;
}
int ver_list(const char *docpath, VerEntry *out, int max)
{
  VerEntry e[VER_KEEP + 2];
  int n = ver_read(docpath, e, VER_KEEP + 2), i, k = 0;
  for (i = n - 1; i >= 0 && k < max; i--) out[k++] = e[i];
  return k;
}
int ver_add(const char *docpath, const char *stamp, int autosave)
{
  VerEntry e[VER_KEEP + 2];
  int n = ver_read(docpath, e, VER_KEEP + 1), i, top = 0;
  size_t len;
  u8 *data = slurp(docpath, &len);
  char file[300];
  if (!data) return 0;
  for (i = 0; i < n; i++) if (e[i].n > top) top = e[i].n;
  if (autosave && n && e[n - 1].kind == 'a') {
    /* a run of AutoSaves keeps its last */
    ver_file(docpath, e[n - 1].n, file, sizeof file);
    remove(file);
    n--;
  }
  e[n].n = top + 1;
  snprintf(e[n].stamp, sizeof e[n].stamp, "%s", stamp ? stamp : "");
  clean(e[n].stamp);
  e[n].size = (long)len; e[n].kind = autosave ? 'a' : 's';
  ver_file(docpath, e[n].n, file, sizeof file);
  if (!spit(file, data, len)) { free(data); return 0; }
  free(data);
  n++;
  /* the oldest beyond VER_KEEP go */
  while (n > VER_KEEP) {
    ver_file(docpath, e[0].n, file, sizeof file);
    remove(file);
    memmove(e, e + 1, (size_t)(n - 1) * sizeof e[0]);
    n--;
  }
  return ver_write(docpath, e, n);
}
int ver_delete(const char *docpath, int num)
{
  VerEntry e[VER_KEEP + 2];
  int n = ver_read(docpath, e, VER_KEEP + 2), i;
  char file[300];
  for (i = 0; i < n && e[i].n != num; i++) ;
  if (i == n) return 0;
  ver_file(docpath, num, file, sizeof file);
  remove(file);
  memmove(e + i, e + i + 1, (size_t)(n - i - 1) * sizeof e[0]);
  return ver_write(docpath, e, n - 1);
}
