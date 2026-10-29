/* theme.c - see theme.h: the theme's pens, a loader for Quill's own bitmap fonts, Amiga fonts made
   from TrueType outlines, text previews, the interface fonts, and the icons */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>
#include <proto/dos.h>
#include <diskfont/diskfont.h>
#include <dos/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <libraries/mui.h>
#include <clib/alib_protos.h>
#include "theme.h"
#include "../core/fonts.h"
#include "../core/ttraster.h"
#include "lang.h"

extern struct Library *DiskfontBase;
#include <stdio.h>
extern FILE *qlog;
#define FTRACE(...) do { if (qlog) { fprintf(qlog, __VA_ARGS__); fflush(qlog); } } while (0)

/* WordCraft's light theme */
static const ULONG light[T_NPENS] = {
  0xF9F9F9, 0xF0F0F0, 0xE6E6E6, 0xFAFAFA, 0xE1E1E1, 0xC8C8C8, 0x242424, 0x616161, 0xA8A8A8, 0x424242,
  0xE6E6E6, 0xD6D6D6, 0xDDE3F8, 0x3B5BDB, 0xFFFFFF, 0x8A8A8A, 0x707070, 0xFFFFFF, 0xD4D4D4,
  0x707070, 0xF0F0F0, 0xC42B1C, 0x107C10, 0xCA5010, 0xFFE03D, 0xEBEBEB, 0xC8D2F4, 0xFFFFFF, 0xF0F0F0,
  0x2C47B8, 0x5470E0, 0xDCE3FA, 0xFFFFFF
};
/* and its dark one (inputs are dark, text on the lighter accent is dark) */
static const ULONG dark[T_NPENS] = {
  0x292929, 0x1F1F1F, 0x141414, 0x262626, 0x3A3A3A, 0x555555, 0xEEEEEE, 0xC8C8C8, 0x6A6A6A, 0xDDDDDD,
  0x3D3D3D, 0x4A4A4A, 0x2E3A66, 0x7A93F0, 0x1E1E1E, 0x7A7A7A, 0xA0A0A0, 0x454545, 0x2E2E2E,
  0xB4B4B4, 0x1F1F1F, 0xEE6A6A, 0x5BC08A, 0xF0A050, 0xE0C040, 0x333333, 0x3A4A80, 0x2B2B2B, 0x1B1B1B,
  0x5A70C8, 0x93A8F4, 0x27315C, 0x11162E
};
/* the colours in use, the pens obtained for them from pencm (the screen's colour map), and how
   many objects are set up: the pens are held while any is */
static const ULONG *rgb = light;
static LONG pens[T_NPENS];
static struct ColorMap *pencm;
static int users;
/* a pen for each colour. ObtainBestPen shares a pen that already has the colour, or takes a free
   one and sets it, or settles for the nearest: on a screen with few colours the theme comes out
   approximate. Every pen must go back with ReleasePen, or other programs run short. The colour
   map wants 32 bits a gun, hence each 8-bit value repeated four times */
static void obtain_pens(void)
{
  int i;
  for (i = 0; i < T_NPENS; i++) {
    ULONG c = rgb[i];
    pens[i] = ObtainBestPen(pencm, ((c >> 16) & 255) * 0x01010101UL, ((c >> 8) & 255) * 0x01010101UL, (c & 255) * 0x01010101UL,
                            OBP_Precision, PRECISION_GUI, TAG_DONE);
  }
}
/* (see theme.h) if objects are set up, their pens are given back and taken again in the new
   colours; nothing is redrawn here */
void theme_set_dark(int on)
{
  int i;
  if ((rgb == dark) == (on != 0)) return;
  if (users) for (i = 0; i < T_NPENS; i++) ReleasePen(pencm, pens[i]);
  rgb = on ? dark : light;
  if (users) obtain_pens();
}
/* the dark theme is the one in use */
int theme_is_dark(void) { return rgb == dark; }
static struct TextFont *tf_reg;
/* the interface font opened in theme_setup (if not, theme_font() falls back on the system's
   default) */
int theme_fonts_ok(void) { return tf_reg != NULL; }
static int large;
/* large interface text (File > Options > Text size) */
void theme_set_large(int on) { large = on != 0; }     /* takes effect when the window opens next */
static struct TextFont *tf_bold, *tf_big, *tf_small;

/* Quill's own fonts are loaded here, by neither diskfont.library nor LoadSeg(): a size's file is a
   hunk file (one hunk: MOVEQ #-1,D0; RTS, then the DiskFontHeader), read into chip memory (the
   blitter draws the letters from it) with its relocations applied here. Some diskfont.library
   versions won't open fonts from the program's drawer, and LoadSeg() of some Kickstarts doesn't
   take the hunk's memory flags. Loaded sizes stay while in use, and a few unused ones besides,
   until the program ends (theme_free_fonts) */
#define MAXOWN 128
#define KEEPOWN 24                      /* unused sizes kept loaded, up to this many (chip memory) */
#define MAXFAM 24
#define MAXSIZES 48
struct OwnFont { struct TextFont *tf; UBYTE *mem; char name[24]; UWORD ysize, users; };
struct Family { char name[24]; int n; UWORD ys[MAXSIZES]; char file[MAXSIZES][28]; int aa; char aaname[240]; };   /* aa: AAText .otag written (1), couldn't be (-1); its fonts' name then */
static struct OwnFont own[MAXOWN];
static char font_why[120];              /* why the last font couldn't be loaded (for the warning) */
const char *theme_fonts_why(void) { return font_why; }
/* the reason a font couldn't be loaded, kept for theme_fonts_why (the warning at start) and
   logged: what went wrong, the file, and DOS's words for IoErr() if it set one */
static void why(const char *what, const char *path)
{
  char e[60] = "";
  LONG err = IoErr();
  if (err) Fault(err, NULL, (STRPTR)e, sizeof e);
  snprintf(font_why, sizeof font_why, "%s %s%s%s", what, path, e[0] ? ": " : "", e);
  FTRACE("font: %s\n", font_why);
}
/* a font size's hunk file in chip memory, relocated; NULL if it can't be */
static UBYTE *load_hunks(const char *path)
{
  BPTR fh;
  LONG n;
  ULONG *b, *end, *p, size = 0, i;
  UBYTE *mem = NULL;
  if (!(fh = Open((STRPTR)path, MODE_OLDFILE))) { why("can't open", path); return NULL; }
  Seek(fh, 0, OFFSET_END);
  n = Seek(fh, 0, OFFSET_BEGINNING);
  if (n < 32 || n > 1000000 || !(b = (ULONG *)AllocVec((ULONG)n, MEMF_ANY))) { Close(fh); SetIoErr(0); why("can't read", path); return NULL; }
  if (Read(fh, b, n) != n) { Close(fh); FreeVec(b); why("can't read", path); return NULL; }
  Close(fh);
  end = b + n / 4;
  p = b;
  SetIoErr(0);
  /* the header: no resident names, one hunk */
  if (*p++ != 0x3F3) goto bad;
  while (p < end && *p) p += *p + 1;
  if (++p + 3 > end || p[0] != 1 || p[1] != 0 || p[2] != 0) goto bad;
  p += 3;
  size = *p & 0x3FFFFFFF;
  if ((*p++ & 0xC0000000) == 0xC0000000) p++;          /* (memory flags in a longword of their own) */
  if (!size || size > 250000 || !(mem = (UBYTE *)AllocVec(size * 4, MEMF_CHIP | MEMF_CLEAR))) goto bad;
  while (p < end) {
    ULONG t = *p++ & 0x3FFFFFFF;
    if (t == 0x3E9 || t == 0x3EA) {                   /* code or data */
      ULONG k = p < end ? *p++ : 0;
      if (p + k > end) goto bad;
      memcpy(mem, p, (k < size ? k : size) * 4);
      p += k;
    } else if (t == 0x3EB) p++;                        /* bss: cleared already */
    else if (t == 0x3EC) {                             /* relocations within the hunk */
      while (p < end && *p) {
        ULONG c = *p++, h = p < end ? *p++ : 1;
        if (h != 0 || p + c > end) goto bad;
        for (i = 0; i < c; i++, p++) if (*p + 4 <= size * 4) *(ULONG *)(mem + *p) += (ULONG)mem;
      }
      p++;
    } else if (t == 0x3FC) {                           /* the same, in words */
      UWORD *w = (UWORD *)p;
      while ((ULONG *)w < end && *w) {
        UWORD c = *w++, h = *w++;
        if (h != 0) goto bad;
        for (i = 0; i < c; i++, w++) if ((ULONG)*w + 4 <= size * 4) *(ULONG *)(mem + *w) += (ULONG)mem;
      }
      w++;
      p = (ULONG *)(((ULONG)w + 3) & ~3UL);
    } else if (t == 0x3F0 || t == 0x3F1) {             /* symbols, debugging: skipped */
      if (t == 0x3F1) p += (p < end ? *p : 0) + 1;
      else while (p < end && *p) p += (*p & 0xFFFFFF) + 2;
      if (t == 0x3F0) p++;
    } else if (t == 0x3F2) break;                      /* the end */
    else goto bad;
  }
  FreeVec(b);
  return mem;
bad:
  FreeVec(b);
  if (mem) FreeVec(mem);
  why("not a font file:", path);
  return NULL;
}
/* AAText (Aminet util/wb/AAText) draws fonts that have a .otag file antialiased, looking for it
   next to the path in the font's name, else in FONTS:. While it runs, each of Quill's fonts is
   named T:QuillAAText/<family>.font (RAM: if there is no T:), with a .otag there naming the TrueType file the family was
   made from (in the TrueType drawer), so AAText draws Quill's text smoothly on RTG screens; Quill's
   own bitmap fonts are drawn as they are without it */
static const char *const aa_ttf[][2] = {
  { "QuillSerif", "LiberationSerif-Regular.ttf" }, { "QuillSerifB", "LiberationSerif-Bold.ttf" },
  { "QuillSerifI", "LiberationSerif-Italic.ttf" }, { "QuillSerifBI", "LiberationSerif-BoldItalic.ttf" },
  { "QuillSans", "LiberationSans-Regular.ttf" }, { "QuillSansB", "LiberationSans-Bold.ttf" },
  { "QuillSansI", "LiberationSans-Italic.ttf" }, { "QuillSansBI", "LiberationSans-BoldItalic.ttf" },
  { "QuillMono", "LiberationMono-Regular.ttf" }, { "QuillMonoB", "LiberationMono-Bold.ttf" },
  { "QuillMonoI", "LiberationMono-Italic.ttf" }, { "QuillMonoBI", "LiberationMono-BoldItalic.ttf" },
  { "QuillSansC", "Carlito-Regular.ttf" }, { "QuillSansCB", "Carlito-Bold.ttf" },
  { "QuillSansCI", "Carlito-Italic.ttf" }, { "QuillSansCBI", "Carlito-BoldItalic.ttf" },
  { "QuillSerifC", "Caladea-Regular.ttf" }, { "QuillSerifCB", "Caladea-Bold.ttf" },
  { "QuillSerifCI", "Caladea-Italic.ttf" }, { "QuillSerifCBI", "Caladea-BoldItalic.ttf" },
};
static int aa_running = -1;
/* Quill's fonts kept from AAText, as if it weren't running */
void theme_no_aatext(void) { aa_running = 0; }     /* (aatext=off in Quill.prefs) */
static char aadir[200];                 /* the drawer of the .otag files, its whole path */
/* AAText is running: its public port is there (looked for under Forbid, as ports may come and go
   meanwhile). The first call also makes the drawer for the .otag files, in T: or else RAM:; if
   neither can be made, Quill carries on as if AAText weren't there. The answer is kept */
int theme_aatext(void)
{
  if (aa_running < 0) {
    Forbid();
    aa_running = FindPort((STRPTR)"AAText") != NULL;
    Permit();
    if (aa_running) {
      static const char *const dirs[] = { "T:QuillAAText", "RAM:QuillAAText" };
      struct Process *me = (struct Process *)FindTask(NULL);
      APTR win = me->pr_WindowPtr;
      int k;
      me->pr_WindowPtr = (APTR)-1;      /* no "please insert volume T" */
      for (k = 0; k < 2 && !aadir[0]; k++) {
        BPTR l = CreateDir((STRPTR)dirs[k]);
        if (l) UnLock(l);
        if ((l = Lock((STRPTR)dirs[k], ACCESS_READ))) {
          if (!NameFromLock(l, (STRPTR)aadir, sizeof aadir)) aadir[0] = 0;
          UnLock(l);
        }
      }
      me->pr_WindowPtr = win;
      if (!aadir[0]) aa_running = 0;
    }
    FTRACE("font: AAText %s\n", aa_running ? "is running" : "isn't running");
  }
  return aa_running;
}
#define OTAG_IDENT 0x80001001UL           /* OT_FileIdent */
#define OTAG_ENGINE 0x80009002UL          /* OT_Engine */
#define OTAG_FILE 0x80009101UL            /* OT_Spec1: the font file */
#define OTAG_CODEPAGE 0x80009102UL        /* OT_Spec2: character -> Unicode, 256 words */
/* the .otag naming a font file (its whole path; face: its place in a collection) for a font named
   fam (without .font), written in aadir: 1 if written. Its tag list comes first, the tags of the
   strings and the codepage holding offsets to them further on in the file */
static int aa_write_otag(const char *fam, const char *ttf, int face)
{
  static ULONG buf[(48 + 512 + 8 + 256) / 4];
  char path[240];
  UBYTE *b = (UBYTE *)buf;
  UWORD *cp = (UWORD *)(b + 48);
  ULONG *t = buf, n, i;
  BPTR fh;
  if (strlen(ttf) > 250) return 0;
  for (i = 0; i < 256; i++) cp[i] = (UWORD)i;        /* Latin-1, as Quill's fonts are */
  strcpy((char *)b + 48 + 512, "aatext");
  strcpy((char *)b + 48 + 512 + 8, ttf);
  n = 48 + 512 + 8 + strlen(ttf) + 1;
  t[0] = OTAG_IDENT; t[1] = n;
  t[2] = OTAG_ENGINE; t[3] = 48 + 512;
  t[4] = OTAG_FILE; t[5] = 48 + 512 + 8;
  t[6] = OTAG_CODEPAGE; t[7] = 48;
  t[8] = face ? 0x80001106UL : TAG_DONE; t[9] = (ULONG)face;                    /* OT_Spec6: the face */
  t[10] = TAG_DONE; t[11] = 0;
  strcpy(path, aadir);
  if (!AddPart((STRPTR)path, (STRPTR)fam, sizeof path - 5)) return 0;
  strcat(path, ".otag");
  if (!(fh = Open((STRPTR)path, MODE_NEWFILE))) return 0;
  i = Write(fh, b, (LONG)n) == (LONG)n;
  Close(fh);
  FTRACE("font: %s for AAText: %s\n", path, i ? ttf : "not written");
  return (int)i;
}
/* the .otag for one of Quill's families (name without .font); 0 if it can't be written */
static int aa_otag(const char *fam)
{
  char path[240], ttf[256];
  ULONG i, n;
  BPTR l;
  for (i = 0; i < sizeof aa_ttf / sizeof aa_ttf[0] && strcmp(aa_ttf[i][0], fam); i++) ;
  if (i == sizeof aa_ttf / sizeof aa_ttf[0]) return 0;
  snprintf(path, sizeof path, "PROGDIR:TrueType/%s", aa_ttf[i][1]);
  if (!(l = Lock((STRPTR)path, ACCESS_READ))) return 0;
  n = NameFromLock(l, (STRPTR)ttf, sizeof ttf);
  UnLock(l);
  return n ? aa_write_otag(fam, ttf, 0) : 0;
}
static int nown;
static struct Family fams[MAXFAM];
static int nfams;
/* one of Quill's families in PROGDIR:Fonts: its sizes and their files, read from its .font (a
   FontContentsHeader and an entry a size) once and remembered, a failure too: NULL again then */
static struct Family *family(const char *name)
{
  struct Family *f;
  struct FontContentsHeader h;
  struct FontContents fc;
  char path[80];
  BPTR fh;
  int i;
  for (i = 0; i < nfams; i++) if (!strcmp(fams[i].name, name)) return fams[i].n ? &fams[i] : NULL;
  if (nfams >= MAXFAM || strlen(name) >= sizeof fams[0].name) return NULL;
  f = &fams[nfams++];
  memset(f, 0, sizeof *f);
  strcpy(f->name, name);
  snprintf(path, sizeof path, "PROGDIR:Fonts/%s", name);
  if (!(fh = Open((STRPTR)path, MODE_OLDFILE))) { why("can't open", path); return NULL; }
  if (Read(fh, &h, sizeof h) == sizeof h && h.fch_FileID == FCH_ID)
    for (i = 0; i < h.fch_NumEntries && f->n < MAXSIZES; i++) {
      if (Read(fh, &fc, sizeof fc) != sizeof fc) break;
      fc.fc_FileName[MAXFONTPATH - 1] = 0;
      if (strlen(fc.fc_FileName) >= sizeof f->file[0]) continue;
      f->ys[f->n] = fc.fc_YSize;
      strcpy(f->file[f->n++], fc.fc_FileName);
    }
  Close(fh);
  FTRACE("font: %s: %d sizes (id %x)\n", path, f->n, (unsigned)h.fch_FileID);
  if (!f->n) { SetIoErr(0); why("no sizes in", path); }
  return f->n ? f : NULL;
}
/* a size of one of Quill's fonts: the nearest it has (exact: ysize or nothing), shared and counted
   if already loaded. ExtendFont gives it the extension the system's text calls expect; with AAText
   running its name becomes the .font beside its .otag. Close with theme_close_font */
static struct TextFont *load_own(const char *name, UWORD ysize, int exact)
{
  struct Family *f = family(name);
  struct DiskFontHeader *dfh;
  char path[80];
  UBYTE *mem;
  int i, best = -1;
  if (!f) return NULL;
  for (i = 0; i < f->n; i++) {
    int dd = f->ys[i] > ysize ? f->ys[i] - ysize : ysize - f->ys[i];
    int bd = best < 0 ? 0 : (f->ys[best] > ysize ? f->ys[best] - ysize : ysize - f->ys[best]);
    if (best < 0 || dd < bd || (dd == bd && f->ys[i] < f->ys[best])) best = i;
  }
  if (best < 0 || (exact && f->ys[best] != ysize)) return NULL;
  for (i = 0; i < nown; i++) if (own[i].ysize == f->ys[best] && !strcmp(own[i].name, name)) { own[i].users++; return own[i].tf; }
  if (nown >= MAXOWN) return NULL;
  snprintf(path, sizeof path, "PROGDIR:Fonts/%s", f->file[best]);
  if (!(mem = load_hunks(path))) return NULL;
  dfh = (struct DiskFontHeader *)(mem + 4);         /* after MOVEQ #-1,D0; RTS */
  if (dfh->dfh_FileID != DFH_ID || dfh->dfh_TF.tf_YSize == 0) { SetIoErr(0); why("not a font:", path); FreeVec(mem); return NULL; }
  FTRACE("font: loaded %s: %u high, %u-%u\n", path, (unsigned)dfh->dfh_TF.tf_YSize, (unsigned)dfh->dfh_TF.tf_LoChar, (unsigned)dfh->dfh_TF.tf_HiChar);
  dfh->dfh_Segment = 0;
  ExtendFont(&dfh->dfh_TF, NULL);
  own[nown].tf = &dfh->dfh_TF; own[nown].mem = mem; own[nown].ysize = f->ys[best]; own[nown].users = 1;
  if (theme_aatext()) {                  /* named where AAText finds its .otag */
    char fam[24];
    strcpy(fam, name);
    if (strlen(fam) > 5 && !strcmp(fam + strlen(fam) - 5, ".font")) fam[strlen(fam) - 5] = 0;
    if (!f->aa) {
      f->aa = aa_otag(fam) ? 1 : -1;
      strcpy(f->aaname, aadir);
      if (!AddPart((STRPTR)f->aaname, (STRPTR)fam, sizeof f->aaname - 5)) f->aa = -1;
      strcat(f->aaname, ".font");
    }
    if (f->aa > 0) dfh->dfh_TF.tf_Message.mn_Node.ln_Name = f->aaname;
  }
  strcpy(own[nown].name, name);
  return own[nown++].tf;
}
/* own[i] freed (its extension stripped first); the last entry moves into its place */
static void unload(int i)
{
  StripFont(own[i].tf);
  FreeVec(own[i].mem);
  own[i] = own[--nown];
}
/* ------------------------------------------------------------------ */
/* the installed TrueType fonts on screen: through diskfont.library and the system's font engine
   where the font is installed with a .font (hinted, and drawn smoothly by AAText); else made here,
   from the outlines (ttraster.c), as an Amiga font in chip memory */
#define MAXRT 32
#define KEEPRT 8                        /* unused ones kept */
struct RtFont { struct TextFont *tf; Face *face; LONG q; UWORD users; UBYTE disk; UBYTE *chip; };
static struct RtFont rt[MAXRT];
static int nrt;
static TTRaster rast[3];
static int hinting = 1;
/* glyphs rasterised without hints */
void theme_no_hinting(void) { hinting = 0; }      /* (hinting=off in Quill.prefs) */
static Face *rastface[3];
static int rastnext;
/* a rasteriser open on face f: three are kept, the one opened longest ago closed for a fourth face
   (opening one reads the file and, with hinting, prepares the hints, which is slow on a 68k) */
static TTRaster *raster_for(Face *f)
{
  int i;
  for (i = 0; i < 3; i++) if (rastface[i] == f) return &rast[i];
  i = rastnext; rastnext = (rastnext + 1) % 3;
  if (rastface[i]) ttr_close(&rast[i]);
  rastface[i] = NULL;
  if (!ttr_open(&rast[i], f->path, f->index)) return NULL;
  if (hinting && f->loaded) ttr_hint(&rast[i], f->latin_glyph);
  rastface[i] = f;
  return &rast[i];
}
/* a name for AAText (a .otag beside it names the file); "" if AAText isn't running */
static void rt_name(Face *f, char *out, int size)
{
  char fam[48];
  int k = 0;
  const char *p;
  out[0] = 0;
  if (!theme_aatext()) return;
  for (p = f->family; *p && k < 30; p++) if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9')) fam[k++] = *p;
  sprintf(fam + k, "%s%s", f->bold ? "B" : "", f->italic ? "I" : "");
  strcpy(out, aadir);
  if (!AddPart((STRPTR)out, (STRPTR)fam, size - 6) || !aa_write_otag(fam, f->path, f->index)) { out[0] = 0; return; }
  strcat(out, ".font");
}
#define NCH (256 - 32)
/* the time of day in 50ths of a second, for the log's timings */
static LONG tick50(void) { struct DateStamp ds; DateStamp(&ds); return ds.ds_Minute * 3000 + ds.ds_Tick; }
/* an Amiga bitmap font made from f's outlines at ppem64/64 pixels to the em, characters 32-255,
   in the layout graphics.library draws from: every glyph side by side in one strip of chip memory
   (*chipp), tf_CharLoc giving each one's bit offset and width, tf_CharSpace its advance and
   tf_CharKern its left bearing. A pixel is set where the outline covers it enough (112 or more),
   there being no grey in an Amiga font. The TextFont, its tables and its name share one block.
   NULL if it can't be made */
static struct TextFont *rt_build(Face *f, LONG ppem64, UBYTE **chipp)
{
  LONG t0 = tick50(), t1;
  TTRaster *r = raster_for(f);
  TTBitmap *bm;
  struct TextFont *tf;
  ULONG *loc;
  WORD *space, *kern;
  UBYTE *chip, *block;
  char *name;
  LONG a, dsc, ys, bits = 0, modulo, i, j, k, off;
  if (!r || !f->loaded) return NULL;
  a = (f->ascent * ppem64 + f->upm * 32L) / (f->upm * 64L);
  dsc = (f->descent * ppem64 + f->upm * 32L) / (f->upm * 64L);
  if (a < 1) a = 1;
  if (dsc < 0) dsc = 0;
  ys = a + dsc;
  if (!(bm = (TTBitmap *)AllocVec(NCH * sizeof(TTBitmap), MEMF_ANY | MEMF_CLEAR))) return NULL;
  t1 = tick50();
  for (i = 0; i < NCH; i++) {
    if (!ttr_glyph(r, f->latin_glyph[i + 32], ppem64, &bm[i])) memset(&bm[i], 0, sizeof bm[i]);
    if (hinting) ttr_dropout(&bm[i], 112);
    bits += bm[i].w;
  }
  FTRACE("font: %s: file %ld ticks, glyphs %ld ticks\n", f->family, (long)(t1 - t0), (long)(tick50() - t1));
  modulo = ((bits + 15) / 16) * 2;
  if (!modulo) modulo = 2;
  block = (UBYTE *)AllocVec(sizeof(struct TextFont) + (NCH + 1) * (4 + 2 + 2) + 240, MEMF_ANY | MEMF_CLEAR);
  chip = (UBYTE *)AllocVec((ULONG)(modulo * ys), MEMF_CHIP | MEMF_CLEAR);
  if (!block || !chip) { if (block) FreeVec(block); if (chip) FreeVec(chip); for (i = 0; i < NCH; i++) free(bm[i].cov); FreeVec(bm); return NULL; }
  tf = (struct TextFont *)block;
  loc = (ULONG *)(block + sizeof(struct TextFont));
  space = (WORD *)(loc + NCH + 1);
  kern = space + NCH + 1;
  name = (char *)(kern + NCH + 1);
  for (i = 0, off = 0; i < NCH; i++) {
    TTBitmap *b = &bm[i];
    u16 g = f->latin_glyph[i + 32];
    loc[i] = ((ULONG)off << 16) | (UWORD)b->w;
    space[i] = (WORD)((f->adv[g < f->nglyphs ? g : 0] * ppem64 + f->upm * 32L) / (f->upm * 64L));
    kern[i] = (WORD)b->left;
    for (j = 0; j < b->h; j++) {
      LONG row = a - b->top + j;
      if (row < 0 || row >= ys) continue;
      for (k = 0; k < b->w; k++)
        if (b->cov[j * b->w + k] >= 112) chip[row * modulo + (off + k) / 8] |= (UBYTE)(0x80 >> ((off + k) & 7));
    }
    off += b->w;
    free(b->cov);
  }
  FreeVec(bm);
  loc[NCH] = loc['?' - 32]; space[NCH] = space['?' - 32]; kern[NCH] = kern['?' - 32];   /* the character for what isn't there */
  rt_name(f, name, 240);
  tf->tf_Message.mn_Node.ln_Type = NT_FONT;
  tf->tf_Message.mn_Node.ln_Name = name[0] ? name : (char *)f->family;
  tf->tf_YSize = (UWORD)ys;
  tf->tf_Flags = FPF_PROPORTIONAL | FPF_DESIGNED;
  tf->tf_XSize = (UWORD)(space['n' - 32] > 0 ? space['n' - 32] : 1);
  tf->tf_Baseline = (UWORD)(a - 1);
  tf->tf_BoldSmear = 1;
  tf->tf_LoChar = 32; tf->tf_HiChar = 255;
  tf->tf_CharData = chip; tf->tf_Modulo = (UWORD)modulo;
  tf->tf_CharLoc = loc; tf->tf_CharSpace = space; tf->tf_CharKern = kern;
  ExtendFont(tf, NULL);
  *chipp = chip;
  FTRACE("font: made %s at %ld/64 pixels: %ld high\n", f->family, (long)ppem64, (long)ys);
  return tf;
}
/* rt[i] closed (diskfont's) or freed (made here); the last entry takes its place */
static void rt_free(int i)
{
  struct RtFont *e = &rt[i];
  if (e->disk) CloseFont(e->tf);
  else { StripFont(e->tf); FreeVec(e->chip); FreeVec(e->tf); }
  rt[i] = rt[--nrt];
}
/* (see theme.h) fonts are shared by face and size in quarter pixels; with MAXRT open, an unused
   one makes room. A face installed with a .font goes through diskfont first, kept only if
   its widths come within a sixth of the outlines' (an engine may size it by something else), so
   text measures on screen as the layout expects; else it is made here */
struct TextFont *theme_open_face(Face *f, LONG ppem64)
{
  struct TextFont *tf = NULL;
  UBYTE *chip = NULL;
  LONG q = (ppem64 + 8) / 16;
  int i, disk = 0;
  if (!f || ppem64 < 4 * 64) return NULL;
  for (i = 0; i < nrt; i++) if (rt[i].face == f && rt[i].q == q) { rt[i].users++; return rt[i].tf; }
  if (nrt >= MAXRT) {
    for (i = nrt - 1; i >= 0 && rt[i].users; i--) ;
    if (i < 0) return NULL;
    rt_free(i);
  }
  if (f->amiga[0] && DiskfontBase && f->loaded) {
    /* through the font engine: asked for by its line's height, kept if its widths are near */
    struct TextAttr ta;
    struct RastPort trp;
    LONG want = 10 * ((f->adv[f->latin_glyph['n']] * ppem64 + f->upm * 32L) / (f->upm * 64L)), got;
    ta.ta_Name = (STRPTR)f->amiga;
    ta.ta_YSize = (UWORD)(((f->ascent + f->descent) * ppem64 + f->upm * 32L) / (f->upm * 64L));
    ta.ta_Style = 0; ta.ta_Flags = 0;
    if ((tf = OpenDiskFont(&ta))) {
      InitRastPort(&trp);
      SetFont(&trp, tf);
      got = TextLength(&trp, (STRPTR)"nnnnnnnnnn", 10);
      if (got - want > want / 6 + 2 || want - got > want / 6 + 2) { FTRACE("font: %s through diskfont is %ld wide, not %ld\n", f->amiga, (long)got, (long)want); CloseFont(tf); tf = NULL; }
      else disk = 1;
    }
  }
  if (!tf) tf = rt_build(f, ppem64, &chip);
  if (!tf) return NULL;
  rt[nrt].tf = tf; rt[nrt].face = f; rt[nrt].q = q; rt[nrt].users = 1; rt[nrt].disk = (UBYTE)disk; rt[nrt].chip = chip;
  nrt++;
  return tf;
}

/* ------------------------------------------------------------------ */
/* a line of text drawn in a TrueType face without making a whole Amiga font of it (the font
   list's previews): its glyphs only, into a bitmap the blitter draws in the pen asked for; kept
   for the session */
#define MAXPREV 160
struct Preview { Face *face; LONG ppem64; char text[48]; UBYTE *bits; WORD w, h, top, mod; };
static struct Preview prev[MAXPREV];
static int nprev;
/* the preview of text in f at ppem64/64 pixels to the em, made on first asking and kept; NULL if
   it can't be made (or MAXPREV are made). Its bits are NULL when there is nothing to draw (or no
   chip memory for it) */
static struct Preview *preview(Face *f, const char *text, LONG ppem64)
{
  struct Preview *p;
  TTRaster r;
  TTBitmap *g;
  LONG pen = 0, minx = 0, maxx = 0, top = 0, bot = 0, i, n = (LONG)strlen(text), j, k;
  for (i = 0; i < nprev; i++) if (prev[i].face == f && prev[i].ppem64 == ppem64 && !strcmp(prev[i].text, text)) return &prev[i];
  if (nprev >= MAXPREV || n >= (LONG)sizeof prev[0].text || !f || !ttf_load(f) || !ttr_open(&r, f->path, f->index)) return NULL;
  if (hinting) ttr_hint(&r, f->latin_glyph);
  if (!(g = (TTBitmap *)AllocVec((ULONG)(n + 1) * sizeof(TTBitmap), MEMF_ANY | MEMF_CLEAR))) { ttr_close(&r); return NULL; }
  /* the glyphs, and how far they reach */
  for (i = 0; i < n; i++) {
    u16 gi = f->latin_glyph[(UBYTE)text[i]];
    if (!ttr_glyph(&r, gi, ppem64, &g[i])) memset(&g[i], 0, sizeof g[i]);
    else if (hinting) ttr_dropout(&g[i], 112);
    if (g[i].w) {
      if (pen + g[i].left < minx) minx = pen + g[i].left;
      if (pen + g[i].left + g[i].w > maxx) maxx = pen + g[i].left + g[i].w;
      if (g[i].top > top) top = g[i].top;
      if (g[i].h - g[i].top > bot) bot = g[i].h - g[i].top;
    }
    pen += (f->adv[gi < f->nglyphs ? gi : 0] * ppem64 + f->upm * 32L) / (f->upm * 64L);
  }
  ttr_close(&r);
  p = &prev[nprev];
  memset(p, 0, sizeof *p);
  p->w = (WORD)(maxx > pen ? maxx : pen) - (WORD)minx; p->h = (WORD)(top + bot); p->top = (WORD)top;
  p->mod = (WORD)(((p->w + 15) / 16) * 2);
  if (p->w > 0 && p->h > 0 && (p->bits = (UBYTE *)AllocVec((ULONG)(p->mod * p->h), MEMF_CHIP | MEMF_CLEAR))) {
    for (i = 0, pen = -minx; i < n; i++) {
      u16 gi = f->latin_glyph[(UBYTE)text[i]];
      for (j = 0; j < g[i].h; j++)
        for (k = 0; k < g[i].w; k++)
          if (g[i].cov[j * g[i].w + k] >= 112) {
            LONG x = pen + g[i].left + k, y = top - g[i].top + j;
            if (x >= 0 && x < p->w && y >= 0 && y < p->h) p->bits[y * p->mod + x / 8] |= (UBYTE)(0x80 >> (x & 7));
          }
      pen += (f->adv[gi < f->nglyphs ? gi : 0] * ppem64 + f->upm * 32L) / (f->upm * 64L);
    }
  }
  for (i = 0; i < n; i++) free(g[i].cov);
  FreeVec(g);
  p->face = f; p->ppem64 = ppem64; strcpy(p->text, text);
  nprev++;
  return p;
}
/* (see theme.h) the preview's width in pixels, 0 if it can't be made */
LONG theme_preview_width(Face *f, const char *text, LONG ppem64)
{
  struct Preview *p = preview(f, text, ppem64);
  return p ? p->w : 0;
}
/* text at x, its baseline at y; 0 if it can't be drawn (the caller then writes it in its own font) */
int theme_preview(struct RastPort *rp, Face *f, const char *text, LONG ppem64, LONG x, LONG y, LONG maxw, LONG pen)
{
  struct Preview *p = preview(f, text, ppem64);
  if (!p) return 0;
  if (!p->bits) return 1;
  SetAPen(rp, pen); SetDrMd(rp, JAM1);
  BltTemplate((PLANEPTR)p->bits, 0, p->mod, rp, x, y - p->top, p->w < maxw ? p->w : maxw, p->h);
  return 1;
}
/* the previews' bitmaps freed (at the end) */
static void free_previews(void)
{
  while (nprev > 0) { nprev--; if (prev[nprev].bits) FreeVec(prev[nprev].bits); }
}

/* the dialogs: Quill's interface font, held from the first dialog until the end (so it outlives
   the main window closing and opening again), and the ribbon's background */
static struct TextFont *dlgfont;
static int dlgtried;
/* opened on the first call only; if that fails it stays NULL, not tried again */
struct TextFont *theme_dialog_font(void)
{
  if (!dlgtried) { dlgtried = 1; dlgfont = theme_open_font("QuillSans.font", large ? 15 : 11); }
  return dlgfont;
}
/* the ribbon's colour as an MUI image spec ("2:" and three 32-bit guns), so dialogs match it; in a
   static buffer, written again each call */
const char *theme_dialog_back(void)
{
  static char spec[40];
  ULONG c = rgb[T_RIBBON];
  sprintf(spec, "2:%08lx,%08lx,%08lx", ((c >> 16) & 255) * 0x01010101UL, ((c >> 8) & 255) * 0x01010101UL, (c & 255) * 0x01010101UL);
  return spec;
}

/* (see theme.h) a font from theme_open_font or theme_open_face given back: a user fewer, and past
   KEEPRT or KEEPOWN unused ones the extra are freed. A font that isn't one Quill keeps itself
   came from diskfont.library, and is CloseFont()ed */
void theme_close_font(struct TextFont *tf)
{
  int i, idle = 0;
  if (!tf) return;
  for (i = 0; i < nrt && rt[i].tf != tf; i++) ;
  if (i < nrt) {
    if (rt[i].users) rt[i].users--;
    for (i = 0; i < nrt; i++) idle += !rt[i].users;
    for (i = nrt - 1; i >= 0 && idle > KEEPRT; i--) if (!rt[i].users) { rt_free(i); idle--; }
    return;
  }
  for (i = 0; i < nown && own[i].tf != tf; i++) ;
  if (i == nown) { CloseFont(tf); return; }
  if (own[i].users) own[i].users--;
  for (i = 0; i < nown; i++) idle += !own[i].users;
  for (i = nown - 1; i >= 0 && idle > KEEPOWN; i--) if (!own[i].users) { unload(i); idle--; }
}
/* at the end: every font Quill loaded or made, whatever its users, the previews and the
   rasterisers; the families are read again if a font is wanted after */
void theme_free_fonts(void)
{
  int i;
  while (nown > 0) unload(nown - 1);
  dlgfont = NULL; dlgtried = 0;
  while (nrt > 0) rt_free(nrt - 1);
  free_previews();
  for (i = 0; i < 3; i++) if (rastface[i]) { ttr_close(&rast[i]); rastface[i] = NULL; }
  nfams = 0;
}
/* one of Quill's fonts: the nearest size it has, loaded itself; one far bigger than its biggest
   scaled by diskfont.library if it can; else diskfont.library's (from the drawer, then FONTS:) */
struct TextFont *theme_open_font(const char *name, UWORD ysize)
{
  struct TextAttr ta;
  struct TextFont *tf;
  struct Family *f = family(name);
  char path[80];
  if (f) {
    UWORD top = 0;
    int i;
    for (i = 0; i < f->n; i++) if (f->ys[i] > top) top = f->ys[i];
    if (ysize <= top + top / 8 && (tf = load_own(name, ysize, 0))) return tf;
  }
#ifdef QUILL_NODISKFONT
  if (0) {
#else
  if (DiskfontBase) {
#endif
    FTRACE("font: %s %u through diskfont.library\n", name, (unsigned)ysize);
    snprintf(path, sizeof path, "PROGDIR:Fonts/%s", name);
    ta.ta_Name = (STRPTR)path; ta.ta_YSize = ysize; ta.ta_Style = 0; ta.ta_Flags = 0;
    if ((tf = OpenDiskFont(&ta))) return tf;
    ta.ta_Name = (STRPTR)name;
    if ((tf = OpenDiskFont(&ta))) return tf;
  }
  return f ? load_own(name, ysize, 0) : NULL;
}
/* (the same, for theme_setup) */
static struct TextFont *open_font(const char *name, UWORD ysize) { return theme_open_font(name, ysize); }
/* the objects set up (each with its event handler, if any): MUI may send an object Cleanup
   without Setup, or Setup twice, when objects are hidden and shown, so each is counted once */
#define MAXUI 32
static struct { Object *obj; struct MUI_EventHandlerNode *ehn; } ui[MAXUI];
static int nui;
/* obj's place in ui[], -1 if it isn't set up */
static int ui_find(Object *obj)
{
  int i;
  for (i = 0; i < nui && ui[i].obj != obj; i++) ;
  return i < nui ? i : -1;
}
/* (see theme.h) called from each object's MUIM_Setup, when MUI has given it a screen: the first
   obtains the pens on that screen and opens the interface fonts, later ones are only counted.
   Quill's objects are taken to be on one screen, so one set of pens serves them all */
void theme_setup(Object *obj)
{
  struct ColorMap *cm = _screen(obj)->ViewPort.ColorMap;
  if (ui_find(obj) >= 0 || nui >= MAXUI) return;
  ui[nui].obj = obj; ui[nui++].ehn = NULL;
  if (users++) return;
  pencm = cm;
  obtain_pens();
  /* Liberation Sans as an Amiga font (QuillSans), 10 pixels to the em: a 11 pixel line (large: 15) */
  tf_reg = open_font("QuillSans.font", large ? 15 : 11);
  tf_bold = open_font("QuillSansB.font", large ? 15 : 11);
  tf_big = open_font("QuillSans.font", large ? 35 : 27);
  tf_small = open_font("QuillSans.font", large ? 12 : 9);
}
/* the object's event handler added to its window (MUIM_Window_AddEventHandler), once; MUI then
   sends the object MUIM_HandleEvent for the IDCMP events ehn asks for. theme_cleanup removes it */
void theme_handler(Object *obj, struct MUI_EventHandlerNode *ehn)
{
  int k = ui_find(obj);
  if (k < 0 || ui[k].ehn) return;
  ui[k].ehn = ehn;
  DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)ehn);
}
/* (see theme.h) called from MUIM_Cleanup: the event handler removed, and with the last object gone
   the pens released and the interface fonts closed, while the screen is still there */
void theme_cleanup(Object *obj)
{
  int i, k = ui_find(obj);
  if (k < 0) return;
  if (ui[k].ehn) DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)ui[k].ehn);
  ui[k] = ui[--nui];
  if (!users || --users) return;
  for (i = 0; i < T_NPENS; i++) ReleasePen(pencm, pens[i]);
  theme_close_font(tf_reg);
  theme_close_font(tf_bold);
  theme_close_font(tf_big);
  theme_close_font(tf_small);
  tf_reg = tf_bold = tf_big = tf_small = NULL;
}
/* the pen for a T_ colour (meaningful between theme_setup and theme_cleanup; pen 1 for a bad one) */
LONG tpen(int which) { return which >= 0 && which < T_NPENS ? pens[which] : 1; }
/* the system's default font (graphics.library's), and the interface fonts, each falling back on
   another and in the end on it, so they never return NULL */
static struct TextFont *deffont(void) { return ((struct GfxBase *)GfxBase)->DefaultFont; }
struct TextFont *theme_font(void) { return tf_reg ? tf_reg : deffont(); }
struct TextFont *theme_bold(void) { return tf_bold ? tf_bold : theme_font(); }
struct TextFont *theme_big(void) { return tf_big ? tf_big : theme_bold(); }
struct TextFont *theme_small(void) { return tf_small ? tf_small : theme_font(); }
/* s translated (tr) and measured in tf; rp's font is left set to tf */
LONG theme_textlen(struct RastPort *rp, struct TextFont *tf, const char *s)
{
  SetFont(rp, tf);
  s = tr(s);
  return TextLength(rp, (STRPTR)s, (ULONG)strlen(s));
}
/* s translated and drawn in pen with its top at y (Move wants the baseline), plain, the background
   left as it is (JAM1) */
void theme_text(struct RastPort *rp, struct TextFont *tf, LONG x, LONG y, const char *s, LONG pen)
{
  SetFont(rp, tf);
  SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
  SetAPen(rp, pen);
  SetDrMd(rp, JAM1);
  Move(rp, x, y + tf->tf_Baseline);
  s = tr(s);
  Text(rp, (STRPTR)s, (ULONG)strlen(s));
}
/* a one-pixel outline, corners inclusive */
void theme_rect(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, LONG pen)
{
  SetAPen(rp, pen);
  Move(rp, x0, y0); Draw(rp, x1, y0); Draw(rp, x1, y1); Draw(rp, x0, y1); Draw(rp, x0, y0);
}
/* a filled rectangle, corners inclusive; nothing if it is empty */
void theme_fill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, LONG pen)
{
  if (x1 < x0 || y1 < y0) return;
  SetAPen(rp, pen);
  RectFill(rp, x0, y0, x1, y1);
}

/* ------------------------------------------------------------------ */
/* icons: drawn on a 16 x 16 grid scaled to the size asked for. Commands (numbers separated by
   spaces): cX colour (i icon, a accent, r red, g green, o orange, y yellow, d dim, w white, l light,
   t text, s selection blue), L x0 y0 x1 y1 line, R x0 y0 x1 y1 outline, F x0 y0 x1 y1 filled,
   O cx cy r circle, E cx cy r filled circle, P n x0 y0 ... polyline of n points,
   B "text" a letter in the bold font, I "text" italic, U "text" underlined */
static const char *const icons[IC_N] = {
  "",
  /* PASTE */ "ci R 3 2 12 15 F 6 1 9 3 cw F 4 4 11 14 ca R 7 7 14 15 cw F 8 8 13 14 ci L 9 10 12 10 L 9 12 12 12",
  /* CUT */ "ci O 4 12 2 O 11 12 2 L 5 10 11 1 L 10 10 4 1",
  /* COPY */ "ci R 2 1 9 11 cw F 3 2 8 10 ca R 6 5 13 15 cw F 7 6 12 14 ci L 8 8 11 8 L 8 10 11 10 L 8 12 11 12",
  /* BRUSH */ "ca F 3 1 12 5 ci R 3 1 12 5 L 12 3 14 3 L 14 3 14 7 L 14 7 8 7 L 8 7 8 9 F 7 9 9 15",
  /* BOLD */ "B B",
  /* ITALIC */ "I I",
  /* UNDERLINE */ "U U",
  /* STRIKE */ "ci P 4 11 4 5 4 5 7 11 7 P 4 5 10 11 10 11 13 5 13 ca L 2 9 13 9",
  /* SUB */ "ci L 2 3 8 10 L 8 3 2 10 ca L 10 10 13 10 L 13 10 13 12 L 13 12 10 12 L 10 12 10 15 L 10 15 13 15",
  /* SUPER */ "ci L 2 6 8 13 L 8 6 2 13 ca L 10 1 13 1 L 13 1 13 3 L 13 3 10 3 L 10 3 10 6 L 10 6 13 6",
  /* FONTCOLOR */ "ci L 3 11 7 1 L 7 1 11 11 L 5 7 9 7 cr F 1 13 14 15",
  /* HIGHLIGHT */ "ci P 5 4 9 9 4 12 7 7 12 4 9 L 4 9 3 11 cy F 1 13 14 15",
  /* GROW */ "ci L 1 14 5 4 L 5 4 9 14 L 3 10 7 10 ca L 11 5 13 2 L 13 2 15 5",
  /* SHRINK */ "ci L 1 14 5 4 L 5 4 9 14 L 3 10 7 10 ca L 11 2 13 5 L 13 5 15 2",
  /* CASE */ "ci L 1 13 4 4 L 4 4 7 13 L 2 10 6 10 O 11 11 2 L 13 9 13 13",
  /* CLEAR */ "ci L 1 12 4 3 L 4 3 7 12 L 2 9 6 9 cr F 9 10 14 14 cw L 10 13 13 11",
  /* BULLETS */ "ci E 2 3 1 E 2 8 1 E 2 13 1 L 5 3 14 3 L 5 8 14 8 L 5 13 14 13",
  /* NUMBERS */ "ca L 2 1 2 5 L 1 8 3 8 L 3 8 1 11 L 1 11 3 11 ci L 6 3 14 3 L 6 8 14 8 L 6 13 14 13",
  /* OUTDENT */ "ci L 1 2 14 2 L 7 6 14 6 L 7 10 14 10 L 1 14 14 14 ca P 3 4 5 1 8 4 11",
  /* INDENT */ "ci L 1 2 14 2 L 7 6 14 6 L 7 10 14 10 L 1 14 14 14 ca P 3 1 5 4 8 1 11",
  /* MARKS */ "ci E 6 5 3 L 8 2 8 14 L 11 2 11 14 L 6 2 13 2",
  /* LEFT */ "ci L 1 2 14 2 L 1 5 10 5 L 1 8 14 8 L 1 11 10 11 L 1 14 12 14",
  /* CENTER */ "ci L 1 2 14 2 L 3 5 12 5 L 1 8 14 8 L 3 11 12 11 L 2 14 13 14",
  /* RIGHT */ "ci L 1 2 14 2 L 5 5 14 5 L 1 8 14 8 L 5 11 14 11 L 3 14 14 14",
  /* JUSTIFY */ "ci L 1 2 14 2 L 1 5 14 5 L 1 8 14 8 L 1 11 14 11 L 1 14 9 14",
  /* SPACING */ "ci L 7 3 14 3 L 7 8 14 8 L 7 13 14 13 ca L 3 1 3 15 L 1 3 3 1 L 3 1 5 3 L 1 13 3 15 L 3 15 5 13",
  /* FIND */ "ci O 6 6 4 L 9 9 14 14 L 10 9 15 14",
  /* REPLACE */ "ca B b ci L 9 4 14 4 L 12 2 14 4 L 12 6 14 4 ca L 2 12 7 12 L 4 10 2 12 L 4 14 2 12 ci B c",
  /* SELECT */ "ci P 7 3 1 3 13 6 10 9 15 11 14 8 9 12 9 L 12 9 3 1",
  /* PAGEBREAK */ "ci R 3 1 12 6 R 3 10 12 15 cd L 1 8 3 8 L 5 8 7 8 L 9 8 11 8 L 13 8 15 8",
  /* TABLE */ "ci R 1 2 14 14 ca F 2 3 13 5 ci L 1 6 14 6 L 1 10 14 10 L 5 6 5 14 L 10 6 10 14",
  /* PICTURE */ "ci R 1 2 14 13 ca E 5 6 2 cg P 5 2 12 6 8 9 10 12 7 14 12",
  /* COMMENT */ "ci R 1 2 14 11 L 4 11 4 14 L 4 14 7 11 ca L 4 5 11 5 L 4 8 9 8",
  /* HEADER */ "ci R 2 1 13 15 ca F 3 2 12 4 cd L 4 8 11 8 L 4 10 11 10 L 4 12 9 12",
  /* FOOTER */ "ci R 2 1 13 15 cd L 4 3 11 3 L 4 5 11 5 L 4 7 9 7 ca F 3 12 12 14",
  /* PAGENUM */ "ci R 2 1 13 15 ca L 7 9 8 8 L 8 8 8 13 L 7 13 9 13",
  /* DATE */ "ci R 1 3 14 14 F 1 3 14 5 L 4 1 4 3 L 11 1 11 3 ca F 4 8 5 9 F 7 8 8 9 F 10 8 11 9 F 4 11 5 12 F 7 11 8 12",
  /* SYMBOL */ "ci O 7 6 4 L 3 13 5 13 L 9 13 12 13 L 5 10 5 13 L 9 10 9 13",
  /* MARGINS */ "ci R 2 1 13 15 ca L 2 4 13 4 L 2 12 13 12 L 5 1 5 15 L 10 1 10 15",
  /* ORIENT */ "ci R 1 1 8 12 ca R 5 7 15 14",
  /* SIZE */ "ci R 3 1 12 15 ca L 1 1 1 15 L 0 2 1 1 L 2 2 1 1 L 0 14 1 15 L 2 14 1 15",
  /* BREAKS */ "ci R 3 1 12 5 R 3 11 12 15 ca L 1 8 14 8 L 12 6 14 8 L 12 10 14 8",
  /* TOC */ "ci R 2 1 13 15 ca L 4 4 11 4 cd L 4 7 8 7 L 10 7 11 7 L 4 10 8 10 L 10 10 11 10 L 4 13 8 13 L 10 13 11 13",
  /* UPDATE */ "ci R 2 2 9 15 ca O 11 6 4 cw F 11 2 15 6 ca L 11 2 14 2 L 11 2 13 0",
  /* FOOTNOTE */ "ci L 1 4 9 4 L 1 7 9 7 ca L 11 2 12 1 L 12 1 12 5 ci L 1 12 6 12 cd L 1 14 12 14",
  /* ENDNOTE */ "ci R 2 1 13 15 L 4 4 11 4 L 4 6 11 6 ca L 4 11 11 11 cd L 4 13 9 13",
  /* NEXTNOTE */ "ci L 1 4 9 4 L 1 7 9 7 ca L 11 9 14 12 L 14 12 11 15 L 9 12 14 12",
  /* SPELLING */ "ci B abc cg L 5 12 8 15 L 8 15 14 8",
  /* WORDCOUNT */ "ci R 1 1 14 14 ca B 123",
  /* DELETE */ "ci R 2 2 13 13 cr L 4 4 11 11 L 11 4 4 11",
  /* PREV */ "ci R 1 2 12 11 ca L 9 13 4 13 L 6 11 4 13 L 6 15 4 13",
  /* NEXT */ "ci R 1 2 12 11 ca L 4 13 9 13 L 7 11 9 13 L 7 15 9 13",
  /* SHOWCOMMENTS */ "ci R 1 1 14 14 ca F 9 2 13 13",
  /* TRACK */ "ci R 2 1 13 15 cr L 4 4 11 4 L 4 7 9 7 ci L 4 10 11 10 cr L 4 13 11 13",
  /* ACCEPT */ "ci R 2 1 11 15 cg L 7 11 10 14 L 10 14 15 7",
  /* REJECT */ "ci R 2 1 11 15 cr L 8 8 14 14 L 14 8 8 14",
  /* RULER */ "ci R 1 5 14 10 L 3 5 3 7 L 6 5 6 8 L 9 5 9 7 L 12 5 12 8",
  /* NAVPANE */ "ci R 1 2 14 13 ca F 2 3 5 12 cd L 7 5 12 5 L 7 8 12 8 L 7 11 10 11",
  /* ZOOM */ "ci O 6 6 4 L 9 9 14 14 ca L 4 6 8 6 L 6 4 6 8",
  /* ZOOM100 */ "ci R 3 1 12 15 ca B 1",
  /* PAGEWIDTH */ "ci R 3 1 12 15 ca L 1 8 14 8 L 1 8 3 6 L 1 8 3 10 L 14 8 12 6 L 14 8 12 10",
  /* ONEPAGE */ "ci R 3 1 12 15 cd L 5 4 10 4 L 5 6 10 6 L 5 8 10 8 L 5 10 9 10",
  /* ROWABOVE */ "ci R 1 7 14 14 L 1 10 14 10 L 7 7 7 14 ca L 7 1 7 5 L 5 3 9 3",
  /* ROWBELOW */ "ci R 1 1 14 8 L 1 4 14 4 L 7 1 7 8 ca L 7 10 7 14 L 5 12 9 12",
  /* COLLEFT */ "ci R 7 1 14 14 L 10 1 10 14 L 7 7 14 7 ca L 1 7 5 7 L 3 5 3 9",
  /* COLRIGHT */ "ci R 1 1 8 14 L 4 1 4 14 L 1 7 8 7 ca L 10 7 14 7 L 12 5 12 9",
  /* DELROW */ "ci R 1 1 14 14 L 1 5 14 5 L 1 10 14 10 cr L 4 6 11 9 L 11 6 4 9",
  /* MERGE */ "ci R 1 3 14 12 cd L 7 3 7 5 L 7 10 7 12 ca L 3 7 6 7 L 9 7 12 7 L 5 5 7 7 L 5 9 7 7 L 10 5 8 7 L 10 9 8 7",
  /* SPLIT */ "ci R 1 3 14 12 L 7 3 7 12 ca L 2 7 6 7 L 9 7 13 7",
  /* HEADROW */ "ci R 1 1 14 14 ca F 2 2 13 5 ci L 1 9 14 9 L 7 6 7 14",
  /* ALIGNTOP */ "ci R 1 1 14 14 ca L 3 3 12 3 L 3 5 10 5",
  /* ALIGNMID */ "ci R 1 1 14 14 ca L 3 6 12 6 L 3 8 10 8",
  /* ALIGNBOT */ "ci R 1 1 14 14 ca L 3 10 12 10 L 3 12 10 12",
  /* UNDO */ "ci P 4 13 9 13 5 9 2 3 2 L 3 2 5 0 L 3 2 5 4",
  /* REDO */ "ci P 4 2 9 2 5 6 2 12 2 L 12 2 10 0 L 12 2 10 4",
  /* SAVE */ "ci R 1 1 14 14 F 4 1 11 5 R 3 9 12 14",
  /* NEW */ "ci P 5 3 1 10 1 13 4 13 15 3 15 L 3 15 3 1 L 10 1 10 4 L 10 4 13 4",
  /* OPEN */ "ci P 4 1 13 1 4 6 4 7 6 L 7 6 14 6 L 14 6 14 13 L 14 13 1 13 ca P 4 3 13 5 9 15 9 13 13",
  /* PDF */ "ci R 2 1 13 15 cr F 1 7 14 12 cw B PDF",
  /* PRINT */ "ci R 4 1 11 5 R 1 5 14 11 R 4 9 11 15 ca F 11 7 12 7",
  /* INFO */ "ci O 7 7 6 ca F 7 6 8 11 F 7 3 8 4",
  /* OPTIONS */ "ci O 7 7 3 L 7 1 7 3 L 7 11 7 13 L 1 7 3 7 L 11 7 13 7 L 3 3 4 4 L 10 10 11 11 L 3 11 4 10 L 10 4 11 3",
  /* BACK */ "ci L 9 3 5 8 L 5 8 9 13",
  /* ABOUT */ "ci O 7 7 6 ca L 6 5 7 4 L 7 4 9 4 L 9 4 9 6 L 9 6 7 8 L 7 8 7 9 F 7 11 7 11",
  /* SEARCH */ "cd O 6 6 4 L 9 9 13 13",
  /* PICSIZE */ "ci R 1 4 10 13 ca L 8 2 14 2 L 14 2 14 8 L 14 2 9 7",
  /* ARROWDOWN */ "ci L 4 6 8 10 L 8 10 12 6",
  /* ARROWLEFT */ "ci L 10 4 6 8 L 6 8 10 12",
  /* ARROWRIGHT */ "ci L 6 4 10 8 L 10 8 6 12",
  /* ARROWUP */ "ci L 4 10 8 6 L 8 6 12 10",
  /* FOCUS */ "ci L 1 4 1 1 L 1 1 4 1 L 11 1 14 1 L 14 1 14 4 L 14 11 14 14 L 14 14 11 14 L 4 14 1 14 L 1 14 1 11 ca L 4 7 11 7",
  /* PRINTLAYOUT */ "ci R 3 1 12 15 cd L 5 4 10 4 L 5 7 10 7 L 5 10 10 10",
  /* HOME */ "ci L 1 8 8 1 L 8 1 15 8 R 3 7 12 14",
  /* EXPORT */ "ci R 2 4 10 15 ca L 7 9 14 2 L 10 2 14 2 L 14 2 14 6",
  /* SHADING */ "ci P 4 3 9 8 4 13 9 8 14 L 3 9 13 9 ca F 1 13 14 15",
  /* SORT */ "ca L 1 2 4 2 L 1 2 4 6 L 1 6 4 6 ci L 11 1 11 14 L 9 12 11 14 L 13 12 11 14 ca L 1 10 4 10 L 2 10 2 14 L 1 14 4 14",
  /* READ */ "ci P 4 1 3 7 2 7 13 1 12 P 4 15 3 9 2 9 13 15 12 L 8 2 8 14 ca L 3 5 6 5 L 3 7 6 7 L 10 5 13 5 L 10 7 13 7",
  /* WEB */ "ci O 8 8 6 L 2 8 14 8 L 8 2 8 14 ca P 5 8 2 5 5 5 11 8 14 P 5 8 2 11 5 11 11 8 14",
  /* DRAFT */ "ci L 1 2 14 2 L 1 5 14 5 L 1 8 10 8 cd L 1 10 2 10 L 4 10 5 10 L 7 10 8 10 L 10 10 11 10 L 13 10 14 10 ci L 1 13 14 13",
  /* GRID */ "cl L 1 4 14 4 L 1 8 14 8 L 1 12 14 12 L 4 1 4 14 L 8 1 8 14 L 12 1 12 14 ci R 1 1 14 14",
  /* DARK */ "ci O 8 8 6 E 8 8 5 cw E 10 6 4",
  /* MULTIPAGE */ "ci R 1 2 7 14 R 9 2 15 14 cd L 2 5 6 5 L 2 7 6 7 L 2 9 5 9 L 10 5 14 5 L 10 7 14 7 L 10 9 13 9",
  /* COLUMNS */ "ci R 1 1 14 14 cd L 3 4 7 4 L 3 6 7 6 L 3 8 7 8 L 3 10 7 10 L 3 12 6 12 L 9 4 13 4 L 9 6 13 6 L 9 8 13 8 L 9 10 12 10 ca L 8 3 8 13",
  /* LINENUM */ "ca B 1 ci L 7 4 15 4 L 7 8 15 8 L 7 12 13 12 cd L 1 8 4 8 L 1 12 4 12",
  /* HYPHEN */ "ci L 1 4 10 4 L 1 8 12 8 L 1 12 8 12 ca L 10 12 14 12 L 13 6 13 10",
  /* DROPCAP */ "ca F 1 2 7 3 F 3 2 5 10 ci L 9 3 15 3 L 9 6 15 6 L 9 9 15 9 L 1 12 15 12 L 1 15 12 15",
  /* WATERMARK */ "ci R 2 1 13 15 cl L 4 12 11 4 L 5 13 12 5 cd L 4 3 9 3",
  /* PAGEBORDER */ "ci R 2 1 13 15 ca R 4 3 11 13 cd L 6 6 9 6 L 6 8 9 8 L 6 10 8 10",
  /* PAGEALIGN */ "ci R 2 1 13 15 ca L 5 7 10 7 L 5 9 10 9 cd L 7 3 8 3 L 7 13 8 13",
  /* TSTYLES */ "ca F 1 2 14 5 cl F 1 9 14 10 ci R 1 2 14 14 L 1 6 14 6 L 1 10 14 10 L 6 2 6 14 L 10 2 10 14",
  /* TBORDERS */ "cd L 1 7 14 7 L 7 1 7 14 ca R 1 1 14 14 R 2 2 13 13",
  /* PENSTYLE */ "ci L 1 2 14 2 L 1 6 14 6 L 1 8 14 8 cd L 1 12 2 12 L 4 12 5 12 L 7 12 8 12 L 10 12 11 12 L 13 12 14 12",
  /* PENWIDTH */ "ci L 1 2 14 2 F 1 5 14 6 F 1 10 14 13",
  /* PENCOLOR */ "ci L 4 10 11 3 L 6 12 13 5 L 4 10 6 12 L 11 3 13 5 L 4 10 3 13 L 3 13 6 12 cr F 1 14 14 15",
  /* TSELECT */ "ci R 1 5 14 14 L 1 9 14 9 L 7 5 7 14 ca F 8 6 13 13 L 10 0 10 3 L 9 2 11 2",
  /* TGRID */ "cd R 1 1 14 14 L 1 5 14 5 L 1 10 14 10 L 7 1 7 14",
  /* TPROPS */ "ci R 1 1 14 9 L 1 5 14 5 L 7 1 7 9 ca O 11 12 2 L 11 9 11 10 L 11 14 11 15 L 8 12 9 12 L 13 12 14 12",
  /* SPLITTABLE */ "ci R 1 1 14 5 L 7 1 7 5 R 1 10 14 14 L 7 10 7 14 ca L 1 7 3 7 L 5 7 7 7 L 9 7 11 7 L 13 7 14 7",
  /* AUTOFIT */ "ci R 1 4 14 13 L 7 4 7 13 ca L 1 1 14 1 L 1 1 3 0 L 1 1 3 2 L 14 1 12 0 L 14 1 12 2",
  /* DISTROWS */ "ci R 1 1 11 14 L 1 5 11 5 L 1 10 11 10 ca L 14 1 14 14 L 13 2 14 1 L 15 2 14 1 L 13 13 14 14 L 15 13 14 14",
  /* DISTCOLS */ "ci R 1 1 14 11 L 5 1 5 11 L 10 1 10 11 ca L 1 14 14 14 L 2 13 1 14 L 2 15 1 14 L 13 13 14 14 L 13 15 14 14",
  /* CELLMARGINS */ "ci R 1 1 14 14 cd R 4 4 11 11 ca L 1 7 3 7 L 12 7 14 7 L 7 1 7 3 L 7 12 7 14",
  /* TOTEXT */ "ci R 1 1 7 6 L 1 3 7 3 L 4 1 4 6 ca L 9 3 13 3 L 11 1 13 3 L 11 5 13 3 cd L 1 10 14 10 L 1 13 11 13",
  /* FORMULA */ "ca I fx",
  /* TOTABLE */ "cd L 1 2 7 2 L 1 5 6 5 ca L 9 3 13 3 L 11 1 13 3 L 11 5 13 3 ci R 1 8 14 15 L 1 11 14 11 L 7 8 7 15",
  /* ROWH */ "ci R 3 1 14 14 L 3 7 14 7 ca L 1 1 1 7 L 0 2 1 1 L 2 2 1 1 L 0 6 1 7 L 2 6 1 7",
  /* COLW */ "ci R 1 3 14 14 L 7 3 7 14 ca L 1 1 7 1 L 2 0 1 1 L 2 2 1 1 L 6 0 7 1 L 6 2 7 1",
  /* WRAP */ "ca F 9 4 14 10 ci L 1 1 14 1 L 1 4 7 4 L 1 7 7 7 L 1 10 7 10 L 1 13 14 13",
  /* POSITION */ "ci R 2 1 13 14 ca F 8 3 11 6 cd L 4 8 11 8 L 4 10 11 10 L 4 12 11 12",
  /* ROTATE */ "ca F 1 9 7 15 ci P 5 4 6 6 3 10 3 13 6 13 10 P 3 11 8 13 10 15 8",
  /* CROP */ "ca F 5 5 11 11 ci L 4 1 4 12 L 4 12 15 12 L 1 4 12 4 L 12 4 12 15",
  /* ALTTEXT */ "ci R 1 1 14 9 ca E 5 4 1 cg P 3 3 8 7 5 11 8 ct L 2 12 13 12 L 2 14 9 14",
  /* SHAPES */ "ca F 1 1 7 7 cg E 11 5 3 co P 4 3 15 8 8 13 15 3 15",
  /* TEXTBOX */ "ci R 1 2 14 13 ct L 4 5 11 5 L 4 8 11 8 L 4 11 9 11",
  /* SHAPEFILL */ "ci R 3 1 12 10 ca F 4 2 11 9 cr F 1 13 14 15",
  /* SHAPEOUTLINE */ "ci R 3 1 12 10 R 4 2 11 9 cr F 1 13 14 15",
  /* EDITTEXT */ "ci R 1 2 14 13 ct L 4 6 9 6 L 4 9 8 9 ca L 11 4 11 11 L 10 4 12 4 L 10 11 12 11",
  /* LAYOUTOPTS */ "ca F 5 5 10 10 ci L 1 1 14 1 L 1 5 3 5 L 12 5 14 5 L 1 8 3 8 L 12 8 14 8 L 1 13 14 13",
  /* FONTDLG */ "ci L 1 12 5 2 L 5 2 9 12 L 3 8 7 8 ca R 10 10 15 15 L 12 12 15 15 L 15 12 15 15 L 12 15 15 15",
  /* PARADLG */ "ci P 4 7 2 4 2 4 7 7 7 L 7 2 7 13 L 9 2 9 13 L 7 2 11 2 ca R 10 10 15 15 L 12 12 15 15 L 15 12 15 15 L 12 15 15 15",
  /* MULTILIST */ "ca F 1 2 2 3 F 4 7 5 8 F 7 12 8 13 ci L 4 2 14 2 L 7 7 14 7 L 10 12 14 12",
  /* PBORDERS */ "cd R 2 2 13 13 cd L 2 8 13 8 L 8 2 8 13 ca F 2 12 13 14",
  /* PSHADE */ "ci L 4 3 10 3 L 4 3 2 9 L 10 3 12 9 L 2 9 12 9 ca F 3 5 11 8 cr F 1 13 14 15",
  /* STYLESPANE */ "ci R 1 1 14 14 ca B A cd L 9 4 13 4 L 9 7 13 7 L 9 10 13 10",
  /* NEWSTYLE */ "ca B A cg F 10 3 15 4 F 12 1 13 6",
  /* MODSTYLE */ "ca B A ci L 9 14 14 9 L 10 15 15 10 L 9 14 10 15",
  /* STYLESET */ "ca F 1 1 14 4 ci L 1 7 14 7 L 1 10 14 10 L 1 13 10 13",
  /* THEMEFONTS */ "ci L 1 13 4 4 L 4 4 7 13 L 2 10 6 10 ca L 8 13 11 6 L 11 6 14 13 L 9 11 13 11",
  /* PSPACING */ "ci L 6 2 14 2 L 6 6 14 6 L 6 10 14 10 L 6 14 14 14 ca L 2 1 2 15 L 1 3 2 1 L 3 3 2 1 L 1 13 2 15 L 3 13 2 15",
  /* PASTESPEC */ "ci R 3 2 12 15 F 6 1 9 3 cw F 4 4 11 14 ca B T",
  /* PAGECOUNT */ "ci R 2 1 13 15 ca L 6 5 5 12 L 10 5 9 12 L 4 7 11 7 L 4 10 11 10",
  /* CLOSEHF */ "ci R 1 1 12 14 ca F 2 2 11 4 cd L 3 7 9 7 L 3 9 7 9 cr L 9 9 15 15 L 15 9 9 15 L 10 9 15 14 L 9 10 14 15",
  /* GROUP */ "cd R 1 1 14 14 ca F 3 3 8 7 ci R 6 6 12 12",
  /* UNGROUP */ "ca F 1 1 6 5 ci R 9 9 15 15 cd L 7 7 8 8",
  /* LINK: two links of a chain, one through the other */ "ci R 1 5 9 11 R 2 6 8 10 ca R 7 5 15 11 R 8 6 14 10",
  /* BLANKPAGE */ "cw F 4 2 12 14 ci P 5 3 1 10 1 13 4 13 15 3 15 L 3 15 3 1 L 10 1 10 4 L 10 4 13 4",
  /* GOTO */ "ci R 7 1 14 15 cw F 8 2 13 14 ca L 1 8 10 8 L 7 5 10 8 L 7 11 10 8",
  /* ADDTEXT */ "ci L 1 3 8 3 L 3 7 8 7 L 3 11 8 11 L 1 15 8 15 ca L 12 6 12 12 L 9 9 15 9",
  /* FORWARD: the blue square in front of the other */ "cd R 1 1 9 9 cw F 6 6 14 14 ca F 7 7 13 13 ci R 6 6 14 14",
  /* BACKWARD: the blue square behind */ "ca F 2 2 8 8 ci R 1 1 9 9 cw F 6 6 14 14 ci R 6 6 14 14",
  /* SELPANE: a list of objects */ "ci R 1 1 14 14 L 6 4 12 4 L 6 8 12 8 L 6 12 12 12 ca F 3 3 4 5 F 3 7 4 9 F 3 11 4 13",
  /* ALIGNOBJ: bars against a line */ "ci L 2 1 2 15 ca F 3 3 13 6 F 3 9 9 12",
  /* BOOKMARK: a ribbon marker */ "ca F 5 2 11 9 P 6 4 1 12 1 12 15 8 11 4 15 4 1",
  /* COVER: a page with a band and a title */ "ci R 3 1 13 15 ca F 4 2 12 6 cw L 5 4 11 4 ci L 5 9 11 9 L 5 11 9 11",
  /* XREF: a page and an arrow to it */ "ci R 6 1 14 15 L 8 4 12 4 L 8 7 12 7 ca L 1 11 9 11 L 6 8 9 11 L 6 14 9 11",
  /* THEMES: a painter's palette */ "ci O 8 8 7 ca E 5 6 1 cr E 9 4 1 cg E 12 8 1 cy E 6 11 1 cw E 10 12 1",
  /* COLORSETS: three bands of colour */ "cr F 2 3 13 5 cg F 2 7 13 9 ca F 2 11 13 13",
  /* PAGECOLOR: a page filled with colour */ "cy F 4 2 12 14 ci R 3 1 13 15",
  /* SETDEFAULT: a tick */ "cg P 3 2 8 6 12 14 3 P 3 2 9 6 13 14 4",
  /* CBORDER: an A in a box */ "ci L 4 12 7 4 L 7 4 10 12 L 5 9 9 9 ca R 1 1 14 15",
};
/* the next number in an icon description, spaces before it skipped and *p moved past it */
static LONG num(const char **p)
{
  LONG v = 0;
  int neg = 0;
  while (**p == ' ') (*p)++;
  if (**p == '-') { neg = 1; (*p)++; }
  while (**p >= '0' && **p <= '9') { v = v * 10 + (**p - '0'); (*p)++; }
  return neg ? -v : v;
}
static LONG mono = -1;                  /* theme_icon_pen: every part in this pen */
/* (see theme.h) theme_icon with mono set for the one call */
void theme_icon_pen(struct RastPort *rp, int icon, LONG x, LONG y, LONG size, LONG pen)
{
  mono = pen;
  theme_icon(rp, icon, x, y, size, 0);
  mono = -1;
}
/* (see theme.h) icons[icon] carried out command by command, coordinates scaled by size/16; filled
   boxes grow a little at large sizes so strokes keep their weight. dim: every colour but white
   becomes the disabled grey */
void theme_icon(struct RastPort *rp, int icon, LONG x, LONG y, LONG size, int dim)
{
  if (icon <= IC_NONE || icon >= IC_N) return;
  theme_icon_src(rp, icons[icon], x, y, size, dim);
}
/* (see theme.h) an icon from its drawing commands, as in icons[] (the work packages' own icons) */
void theme_icon_src(struct RastPort *rp, const char *src, LONG x, LONG y, LONG size, int dim)
{
  const char *p;
  LONG pen = mono >= 0 ? mono : tpen(dim ? T_DISABLED : T_ICON);
#define SC(v) ((v) * size / 16)
  if (!src) return;
  SetDrMd(rp, JAM1);
  SetAPen(rp, pen);
  for (p = src; *p; ) {
    char c = *p++;
    LONG a, b, e, f;
    switch (c) {
    case 'c': {
      char k = *p ? *p++ : 'i';
      int t = k == 'a' ? T_ACCENT : k == 'r' ? T_RED : k == 'g' ? T_GREEN : k == 'o' ? T_ORANGE : k == 'y' ? T_YELLOW :
              k == 'd' ? T_DIM : k == 'w' ? T_WHITE : k == 'l' ? T_BORDER2 : k == 't' ? T_TEXT : k == 's' ? T_SELECTION : T_ICON;
      if (dim && t != T_WHITE) t = T_DISABLED;
      SetAPen(rp, mono >= 0 ? (t == T_WHITE ? tpen(T_ACCENT) : mono) : tpen(t));
      break; }
    case 'L': a = num(&p); b = num(&p); e = num(&p); f = num(&p); Move(rp, x + SC(a), y + SC(b)); Draw(rp, x + SC(e), y + SC(f)); break;
    case 'R': a = num(&p); b = num(&p); e = num(&p); f = num(&p);
      Move(rp, x + SC(a), y + SC(b)); Draw(rp, x + SC(e), y + SC(b)); Draw(rp, x + SC(e), y + SC(f)); Draw(rp, x + SC(a), y + SC(f)); Draw(rp, x + SC(a), y + SC(b));
      break;
    case 'F': a = num(&p); b = num(&p); e = num(&p); f = num(&p); RectFill(rp, x + SC(a), y + SC(b), x + SC(e) + (size > 16 ? size / 16 - 1 : 0), y + SC(f) + (size > 16 ? size / 16 - 1 : 0)); break;
    case 'O': a = num(&p); b = num(&p); e = num(&p); DrawEllipse(rp, x + SC(a), y + SC(b), SC(e) > 0 ? SC(e) : 1, SC(e) > 0 ? SC(e) : 1); break;
    case 'E': {
      LONG r, dy;
      a = num(&p); b = num(&p); e = num(&p);
      r = SC(e) > 0 ? SC(e) : 1;
      for (dy = -r; dy <= r; dy++) {
        LONG dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r + r) dx++;
        RectFill(rp, x + SC(a) - dx, y + SC(b) + dy, x + SC(a) + dx, y + SC(b) + dy);
      }
      break; }
    case 'P': {
      LONG n = num(&p), i;
      for (i = 0; i < n; i++) {
        a = num(&p); b = num(&p);
        if (i) Draw(rp, x + SC(a), y + SC(b)); else Move(rp, x + SC(a), y + SC(b));
      }
      break; }
    case 'B': case 'I': case 'U': {
      /* letters in the interface font, centred */
      char t[8];
      int n = 0;
      struct TextFont *tf = c == 'B' ? theme_bold() : theme_font();
      LONG w;
      while (*p == ' ') p++;
      while (*p && *p != ' ' && n < 7) t[n++] = *p++;
      t[n] = 0;
      SetFont(rp, tf);
      SetSoftStyle(rp, c == 'I' ? FSF_ITALIC : c == 'U' ? FSF_UNDERLINED : 0, FSF_ITALIC | FSF_UNDERLINED | FSF_BOLD);
      if (c != 'B' && !theme_bold()) SetSoftStyle(rp, FSF_BOLD, FSF_BOLD);
      w = TextLength(rp, (STRPTR)t, (ULONG)n);
      Move(rp, x + (size - w) / 2, y + (size - tf->tf_YSize) / 2 + tf->tf_Baseline);
      Text(rp, (STRPTR)t, (ULONG)n);
      SetSoftStyle(rp, 0, FSF_ITALIC | FSF_UNDERLINED | FSF_BOLD);
      break; }
    default: break;
    }
  }
#undef SC
}

/* (see theme.h) the panes and rulers' bubbles; the ribbon builds its own the same way, with the
   shortcut on the title line */
const char *theme_bubble(const char *name, const char *body)
{
  static char out[500];
  char *o;
  size_t room;
  int col = 0;
  snprintf(out, sizeof out, "\33b%s\33n", name);
  if (!body || !body[0]) return out;
  o = out + strlen(out);
  room = sizeof out - (size_t)(o - out) - 2;
  *o++ = '\n'; room--;
  while (*body && room > 1) {
    const char *sp = body;
    int wl;
    while (*sp && *sp != ' ' && *sp != '\n') sp++;
    wl = (int)(sp - body);
    if (col && col + 1 + wl > 46) { *o++ = '\n'; room--; col = 0; }
    else if (col) { *o++ = ' '; room--; col++; }
    while (body < sp && room > 1) { *o++ = *body++; room--; col++; }
    if (*body == '\n') { *o++ = '\n'; room--; col = 0; body++; }
    while (*body == ' ') body++;
  }
  *o = 0;
  return out;
}
