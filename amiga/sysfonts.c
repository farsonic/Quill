/* sysfonts.c - see sysfonts.h */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include "sysfonts.h"

extern FILE *qlog;
#define MAXFOUND 300
/* a face found in a font file while scanning; size and date tell the cache whether the file has
   changed since it was last looked at */
typedef struct Found {
  char ttf[256];                 /* the font file (.ttf, .otf, .ttc), its whole path */
  int index;                     /* the face in it (a collection's) */
  char amiga[40];                /* the .font next to its .otag ("" for a bare file in _ttf...) */
  LONG size;
  struct DateStamp date;
  char family[48];
  int bold, italic, ok;          /* ok: 1 a TrueType font, 0 not one, -1 not known yet */
} Found;
static Found *found;
static int nfound;

/* s ends with tail, ignoring case as AmigaDOS names do; font_file: s names a TrueType, OpenType
   or collection file */
static int ends_with(const char *s, const char *tail)
{
  size_t a = strlen(s), b = strlen(tail);
  return a >= b && !Stricmp((STRPTR)s + a - b, (STRPTR)tail);
}
static int font_file(const char *s) { return ends_with(s, ".ttf") || ends_with(s, ".otf") || ends_with(s, ".ttc"); }
/* the file an .otag names (OT_Spec1: the font file; OT_Spec6: the face in a collection), as
   AAText and the engines find it: 1 with its whole path in out, 0 if it names none or the file isn't
   there. An .otag is a tag list of big-endian longwords whose string tags hold offsets into the file */
static int otag_file(BPTR dir, const char *otag, char *out, int size, int *face)
{
  UBYTE buf[2048];
  BPTR old = CurrentDir(dir), fh = Open((STRPTR)otag, MODE_OLDFILE), l = 0;
  LONG n = 0, i;
  const char *file = NULL;
  CurrentDir(old);
  if (!fh) return 0;
  n = Read(fh, buf, sizeof buf - 1);
  Close(fh);
  if (n < 16) return 0;
  buf[n] = 0;
  for (i = 0; i + 8 <= n; i += 8) {
    ULONG tag = ((ULONG)buf[i] << 24) | ((ULONG)buf[i + 1] << 16) | ((ULONG)buf[i + 2] << 8) | buf[i + 3];
    ULONG data = ((ULONG)buf[i + 4] << 24) | ((ULONG)buf[i + 5] << 16) | ((ULONG)buf[i + 6] << 8) | buf[i + 7];
    if (tag == 0) break;
    if (tag == 0x80009101UL && data < (ULONG)n) file = (const char *)buf + data;     /* OT_Spec1 */
    if (tag == 0x80001106UL) *face = (int)data;                                        /* OT_Spec6 */
  }
  if (!file || !file[0]) return 0;
  /* as written; else in FONTS:, by the part after "fonts/" (a renamed boot volume) */
  {
    const char *tries[3];
    char alt[256];
    int k;
    const char *p, *rest = file;
    tries[0] = strchr(file, ':') ? file : NULL;
    snprintf(alt, sizeof alt, "FONTS:%s", strchr(file, ':') ? "" : file);
    tries[1] = strchr(file, ':') ? NULL : alt;
    tries[2] = NULL;
    for (p = file; *p; p++) if (*p == ':') rest = p + 1;
    if (strchr(file, ':'))
      for (p = rest; *p; p++)
        if ((p == rest || p[-1] == '/') && !Strnicmp((STRPTR)p, (STRPTR)"fonts/", 6)) { snprintf(alt, sizeof alt, "FONTS:%s", p + 6); tries[1] = alt; break; }
    for (k = 0; k < 2 && !l; k++) if (tries[k]) l = Lock((STRPTR)tries[k], ACCESS_READ);
  }
  if (!l) return 0;
  i = NameFromLock(l, (STRPTR)out, size);
  UnLock(l);
  return i != 0;
}
/* face index of the file ttf in the list, added if it is new (with the file's size and date, for
   the cache); amiga: the .font beside the .otag that led to it, or "". NULL when the list is full
   or the path too long */
static Found *add_found(const char *ttf, int index, const char *amiga)
{
  int i;
  BPTR l;
  struct FileInfoBlock *fib;
  Found *f;
  for (i = 0; i < nfound; i++)
    if (found[i].index == index && !Stricmp((STRPTR)found[i].ttf, (STRPTR)ttf)) {
      if (amiga && amiga[0] && !found[i].amiga[0]) strcpy(found[i].amiga, amiga);      /* the .otag's name wins */
      return &found[i];
    }
  if (nfound >= MAXFOUND || strlen(ttf) >= sizeof found[0].ttf) return NULL;
  if (!(fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL))) return NULL;
  f = &found[nfound];
  memset(f, 0, sizeof *f);
  strcpy(f->ttf, ttf);
  f->index = index;
  if (amiga && strlen(amiga) < sizeof f->amiga) strcpy(f->amiga, amiga);
  f->ok = -1;
  if ((l = Lock((STRPTR)ttf, ACCESS_READ))) {
    if (Examine(l, fib)) { f->size = fib->fib_Size; f->date = fib->fib_Date; }
    UnLock(l);
  }
  FreeDosObject(DOS_FIB, fib);
  nfound++;
  return f;
}
/* every .otag in a drawer of FONTS: (with its .font: diskfont opens the font by it), and every
   font file in its _ttf, _otf and _ttc drawers (each face of a collection) */
static void scan_dir(BPTR dir)
{
  static const char *const subs[] = { "_ttf", "_otf", "_ttc" };
  struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
  char ttf[256], name[40];
  int k;
  if (!fib) return;
  if (Examine(dir, fib))
    while (ExNext(dir, fib)) {
      const char *fn = (const char *)fib->fib_FileName;
      int face = 0;
      if (fib->fib_DirEntryType < 0 && ends_with(fn, ".otag") && strlen(fn) < sizeof name - 1 && otag_file(dir, fn, ttf, sizeof ttf, &face) && font_file(ttf)) {
        strcpy(name, fn);
        strcpy(name + strlen(name) - 5, ".font");
        add_found(ttf, face, name);
      }
    }
  for (k = 0; k < 3; k++) {
    BPTR old = CurrentDir(dir), sub = Lock((STRPTR)subs[k], ACCESS_READ);
    CurrentDir(old);
    if (!sub) continue;
    if (Examine(sub, fib) && fib->fib_DirEntryType > 0)
      while (ExNext(sub, fib))
        if (fib->fib_DirEntryType < 0 && font_file((const char *)fib->fib_FileName) && NameFromLock(sub, (STRPTR)ttf, sizeof ttf) &&
            AddPart((STRPTR)ttf, fib->fib_FileName, sizeof ttf)) {
          int n = ends_with(ttf, ".ttc") ? fonts_count(ttf) : 1, i;
          for (i = 0; i < n && i < 32; i++) add_found(ttf, i, "");
        }
    UnLock(sub);
  }
  FreeDosObject(DOS_FIB, fib);
}
/* PROGDIR:Quill.fontcache: "#2", then a line a face,
   "path<tab>face<tab>size<tab>days minutes ticks<tab>ok<tab>bold<tab>italic<tab>family" */
#define CACHE "PROGDIR:Quill.fontcache"
/* what the cache knows about the faces found: an entry counts only when path, face, size and date
   all match, so a changed file is looked at again */
static void read_cache(void)
{
  FILE *fp = fopen(CACHE, "r");
  char line[400];
  if (!fp) return;
  if (!fgets(line, sizeof line, fp) || strncmp(line, "#2", 2)) { fclose(fp); return; }    /* (an older one: all looked at again) */
  while (fgets(line, sizeof line, fp)) {
    char *fld[8], *s = line;
    int n = 0, i, face;
    long sz, d, m, t;
    line[strcspn(line, "\r\n")] = 0;
    while (n < 8) { fld[n++] = s; if (!(s = strchr(s, '\t'))) break; *s++ = 0; }
    if (n < 8 || sscanf(fld[3], "%ld %ld %ld", &d, &m, &t) != 3) continue;
    face = atoi(fld[1]);
    sz = atol(fld[2]);
    for (i = 0; i < nfound; i++) {
      Found *f = &found[i];
      if (f->ok < 0 && f->index == face && !Stricmp((STRPTR)f->ttf, (STRPTR)fld[0]) && f->size == sz &&
          f->date.ds_Days == d && f->date.ds_Minute == m && f->date.ds_Tick == t) {
        f->ok = atoi(fld[4]); f->bold = atoi(fld[5]); f->italic = atoi(fld[6]);
        strncpy(f->family, fld[7], sizeof f->family - 1);
        break;
      }
    }
  }
  fclose(fp);
}
/* the cache written afresh with every face whose kind is known */
static void write_cache(void)
{
  FILE *fp = fopen(CACHE, "w");
  int i;
  if (!fp) return;
  fprintf(fp, "#2\n");
  for (i = 0; i < nfound; i++) {
    Found *f = &found[i];
    if (f->ok < 0) continue;
    fprintf(fp, "%s\t%d\t%ld\t%ld %ld %ld\t%d\t%d\t%d\t%s\n", f->ttf, f->index, (long)f->size, (long)f->date.ds_Days, (long)f->date.ds_Minute, (long)f->date.ds_Tick,
            f->ok, f->bold, f->italic, f->family);
  }
  fclose(fp);
}
/* (see sysfonts.h) FONTS: may be assigned to several drawers, and GetDeviceProc walks them all.
   DOS requesters are turned off meanwhile, so an assign to a volume not in a drive doesn't stop
   the start with "please insert" */
int sysfonts_scan(FontSet *fs, void (*step)(int done, int total))
{
  struct DevProc *dp = NULL;
  struct Process *me = (struct Process *)FindTask(NULL);
  APTR win = me->pr_WindowPtr;
  int i, k, added = 0, fresh = 0, shipped = fs->nfaces;
  if (!(found = (Found *)AllocVec(MAXFOUND * sizeof(Found), MEMF_ANY))) return 0;
  nfound = 0;
  me->pr_WindowPtr = (APTR)-1;                 /* no requesters for missing volumes */
  /* every drawer FONTS: is assigned to */
  while ((dp = GetDeviceProc((STRPTR)"FONTS:", dp))) {
    if (dp->dvp_Lock) scan_dir(dp->dvp_Lock);
    if (!(dp->dvp_Flags & DVPF_ASSIGN)) break;
  }
  if (dp) FreeDeviceProc(dp);
  read_cache();
  /* what the new and changed files are */
  for (i = 0; i < nfound; i++) {
    Found *f = &found[i];
    if (step && !(i & 3)) step(i, nfound);
    if (f->ok >= 0) continue;
    f->ok = fonts_probe(f->ttf, f->index, f->family, sizeof f->family, &f->bold, &f->italic);
    fresh++;
  }
  me->pr_WindowPtr = win;
  if (fresh) write_cache();
  /* into the set: not a family Quill ships (its own are made to match its screen fonts) */
  for (i = 0; i < nfound; i++) {
    Found *f = &found[i];
    int idx;
    if (f->ok != 1) continue;
    for (k = 0; k < shipped && Stricmp((STRPTR)fs->faces[k].family, (STRPTR)f->family); k++) ;
    if (k < shipped) continue;
    if ((idx = fonts_add(fs, f->ttf, f->family, f->bold, f->italic)) < 0) break;
    fs->faces[idx].installed = 1;
    fs->faces[idx].index = (u8)f->index;
    strcpy(fs->faces[idx].amiga, f->amiga);
    added++;
  }
  if (qlog) { fprintf(qlog, "fonts: %d installed faces (TrueType, OpenType, collections), %d new or changed, %d added\n", nfound, fresh, added); fflush(qlog); }
  FreeVec(found);
  found = NULL;
  return added;
}
