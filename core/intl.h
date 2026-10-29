/*
 * intl.h - WP27's document model: right-to-left paragraphs, runs, sections and tables, complex
 * script and East Asian formatting, phonetic guides (ruby), Asian layout (two lines in one,
 * horizontal in vertical), Asian typography, the document grid (intl.c; the algorithms are bidi.c's).
 */
#ifndef QUILL_INTL_H
#define QUILL_INTL_H

#include "doc.h"

/* WP27's CM_ and PM_ bits (CM_CS and CM_EA in CM_LOOK and CM_ALL; CM_RUBY in neither, as links) */
#define CM_CS    0x00800000U    /* rtl, cs, fontcs, sizecs, hint */
#define CM_EA    0x01000000U    /* fontea, em, eal, ealid */
#define CM_RUBY  0x80000000U    /* ruby */
#define PM_BIDI  0x200000U      /* bidi */
#define PM_ASIAN 0x400000U      /* asian, talign */
/* CharFmt.cs: w:cs (the whole run in its complex script look), w:bCs and w:iCs (with their own values when
   CS_OWNB / CS_OWNI, else as w:b and w:i) */
enum { CS_ALL = 1, CS_BOLD = 2, CS_ITALIC = 4, CS_OWNB = 8, CS_OWNI = 16 };
/* CharFmt.intl_mask in a value given to cf_apply: which of CM_CS's and CM_EA's fields change (0 all) */
enum { IM_RTL = 1, IM_CSFL = 2, IM_FONTCS = 4, IM_SIZECS = 8, IM_HINT = 16, IM_FONTEA = 32, IM_EM = 64, IM_EAL = 128 };
enum { EM_NONE, EM_DOT, EM_COMMA, EM_CIRCLE, EM_UNDERDOT, EM_N };     /* w:em */
/* CharFmt.eal: w:combine (two lines in one) with w:combineBrackets (EAL_BRK, >> 4: EB_), w:vert (horizontal
   in vertical) with w:vertCompress */
enum { EAL_COMBINE = 1, EAL_VERT = 2, EAL_VCOMPRESS = 4, EAL_BRK = 0x70 };
enum { EB_NONE, EB_ROUND, EB_SQUARE, EB_ANGLE, EB_CURLY, EB_N };
#define EAL_BRACKETS(f) (((f)->eal & EAL_BRK) >> 4)
enum { HINT_DEFAULT, HINT_EA, HINT_CS };       /* w:rFonts w:hint */
/* ParaFmt.asian: Asian typography, each bit the opposite of Word's default */
enum { AS_NOKINSOKU = 1,        /* w:kinsoku off: East Asian line breaking rules not used */
       AS_NOWORDWRAP = 2,       /* w:wordWrap off: Latin text may wrap in the middle of a word */
       AS_NOOVERFLOW = 4,       /* w:overflowPunct off: punctuation may not hang past the margin */
       AS_TOPLINE = 8,          /* w:topLinePunct: punctuation at a line's start compressed */
       AS_NOSPACEDE = 16,       /* w:autoSpaceDE off: no space between Asian and Latin text */
       AS_NOSPACEDN = 32,       /* w:autoSpaceDN off: no space between Asian text and numbers */
       AS_NOSNAP = 64 };        /* w:snapToGrid off: not on the document grid */
enum { TXA_AUTO, TXA_TOP, TXA_CENTER, TXA_BASELINE, TXA_BOTTOM };      /* w:textAlignment */
/* SectFmt.tdir: w:textDirection (lrTb across; tbRl vertical, lines right to left; btLr; and the V kinds) */
enum { STD_LRTB, STD_TBRL, STD_BTLR, STD_LRTBV, STD_TBRLV, STD_TBLRV, STD_N };
/* SectFmt.grid: w:docGrid w:type: none, lines only, lines and characters, characters snapped */
enum { GRID_NONE, GRID_LINES, GRID_LINESCHARS, GRID_SNAPCHARS };

/* a phonetic guide (w:ruby): the guide text over its base (the runs whose CharFmt.ruby names it) */
enum { RA_CENTER, RA_DLETTER, RA_DSPACE, RA_LEFT, RA_RIGHT, RA_RIGHTV, RA_N };    /* w:rubyAlign */
typedef struct Ruby {
  char text[96];            /* the guide (UTF-8) */
  u8 align;                 /* RA_ */
  u16 hps;                  /* its size, half-points (w:hps); 0 half the base's */
  u16 raise;                /* its distance up from the base, half-points (w:hpsRaise) */
  u16 hpsbase;              /* the base text's size (w:hpsBaseText) */
  char lid[16];             /* w:lid */
  char font[48];            /* its font ("" the base's) */
  char *rtraw;              /* w:rt's inside as read (written back while text is unchanged), malloc'd; NULL none */
  char *rtsrc;              /* the text rtraw had (for that check) */
} Ruby;

/* East Asian settings of settings.xml (Chinese/Japanese line break options) */
enum { CSC_DONOT, CSC_PUNCT, CSC_PUNCTKANA };   /* w:characterSpacingControl: no compression, punctuation, punctuation and kana */
typedef struct IntlSettings {
  u8 cspacing;              /* CSC_ */
  u8 cspset;                /* w:characterSpacingControl was given */
  u8 strict;                /* w:strictFirstAndLastChars */
  char nobefore[160];       /* w:noLineBreaksBefore (UTF-8: may not start a line; "" Word's own list) */
  char noafter[96];         /* w:noLineBreaksAfter (may not end a line) */
  char lang[16];            /* their w:lang */
} IntlSettings;

/* the document's phonetic guide k (CharFmt.ruby), NULL if none */
const Ruby *intl_ruby(const Doc *d, u16 k);
/* a new phonetic guide (copied; rtraw and rtsrc taken over); its CharFmt.ruby number, 0 if out of memory */
u16  intl_add_ruby(Doc *d, Ruby *r);
/* the East Asian settings, made if none (NULL if out of memory) */
IntlSettings *intl_settings(Doc *d);
void intl_free(Doc *d);                      /* what WP27 keeps in d freed (doc_free) */

/* f as text of script scr (bidi.h SCR_) shows: the complex script font, size, bold and italic, or the
   East Asian font; a run with w:rtl or w:cs uses its complex script look for all its text */
void intl_look(const Doc *d, CharFmt *f, int scr);
/* the run's text is right to left (w:rtl) */
#define CF_RTL(f) ((f)->rtl != 0)

/* editing (each one undo step; 0 if nothing changed) */
/* paragraphs pa..pb made right to left (rtl 1) or left to right (0) */
int  intl_set_dir(Doc *d, u32 pa, u32 pb, int rtl);
/* the text a..b made a right-to-left run (or not), with complex script formatting */
int  intl_set_rtl(Doc *d, Pos a, Pos b, int rtl);
/* a phonetic guide over a..b (r NULL: guides removed) */
int  intl_set_ruby(Doc *d, Pos a, Pos b, const Ruby *r);
/* two lines in one (EAL_COMBINE with brackets EB_), horizontal in vertical (EAL_VERT), or neither (0) on a..b */
int  intl_set_eal(Doc *d, Pos a, Pos b, int eal);
/* an emphasis mark on a..b */
int  intl_set_em(Doc *d, Pos a, Pos b, int em);
/* the complex script or East Asian font (family; "" the run's own) of a..b */
int  intl_set_font(Doc *d, Pos a, Pos b, int ea, const char *family);

/* fn(s, arg) on the format of the section paragraph para is in (whole: every section): a new format
   for each, one undo step. 0 if nothing could be done (in a table, out of memory) */
int  intl_sect_apply(Doc *d, u32 para, int whole, void (*fn)(SectFmt *s, void *arg), void *arg);
/* the section format paragraph i is in (its own or the default one: NULL) */
const SectFmt *intl_sect_of(const Doc *d, u32 i);
/* the document grid of a section: line pitch (twips, 0 none) and character pitch (twips per wide
   character, 0 none) for text of size half-points */
void intl_grid(const SectFmt *s, i32 *linepitch, i32 *charpitch, u16 size);

#endif
