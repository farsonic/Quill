/*
 * review.h - WP5 (docs/wordcraft-gap.md): reviewing a document beyond insertions and deletions.
 *
 *  - Tracked formatting changes: a run's (CharFmt.frev) or a paragraph's (ParaFmt.prev) formatting as
 *    it was before, with who changed it and when (Word's w:rPrChange and w:pPrChange). While
 *    formatting is tracked (Doc.rvtrack), every edit that changes how text or paragraphs look records
 *    one; accepting it keeps the new look, rejecting it brings the old one back.
 *  - Property changes of tables, rows, cells, grids and sections (w:tblPrChange ... w:sectPrChange),
 *    and inserted or deleted rows and cells, which Quill doesn't model: kept verbatim with their
 *    table, row and cell and written back; accepting them drops the record.
 *  - Moves (w:moveFrom, w:moveTo): deletions and insertions that are one move, by its name.
 *  - How tracked changes and comments are shown: Display for Review (All Markup, Simple Markup, No
 *    Markup, Original), Show Markup (comments, insertions and deletions, formatting, each reviewer),
 *    balloons in the margin (revisions, or comments and formatting only, or everything in line).
 *  - Comments: replies (threads) and Resolve.
 *  - The Track Changes Options: marks and colours (by author), changed lines, moves.
 */
#ifndef QUILL_REVIEW_H
#define QUILL_REVIEW_H

#include "doc.h"

struct Layout;

/* ---- tracked formatting changes */
typedef struct FmtRev {
  u16 rid;                  /* who and when: Doc.revs */
  u8 para;                  /* a paragraph's (opf), else a run's (ocf) */
  CharFmt ocf;              /* the run's formatting before (its look and character style) */
  ParaFmt opf;              /* the paragraph's before */
  char *raw; u32 rawlen;    /* the old w:rPr or w:pPr exactly as read (NULL: written from ocf or opf) */
} FmtRev;
/* a new tracked formatting change: its number for CharFmt.frev or ParaFmt.prev (index + 1), 0 if out of
   memory. raw (rawlen bytes, may be NULL) is copied */
int  rv_add_fmt(Doc *d, int rid, int para, const CharFmt *ocf, const ParaFmt *opf, const char *raw, u32 rawlen);
const FmtRev *rv_fmt(const Doc *d, u16 n);      /* by its number, NULL if 0 or out of range */
/* which fields of the look (CM_LOOK | CM_CSTYLE) differ between two formats; PM_ groups for paragraphs */
unsigned rv_cf_diff(const CharFmt *a, const CharFmt *b);
unsigned rv_pf_diff(const ParaFmt *a, const ParaFmt *b);
/* (doc.c, when an edit made while formatting is tracked ends) paragraph cur is old after the edit:
   where only its look changed, the old look is recorded as a formatting change by Doc.rvtrack - 1.
   Text inserted while tracking (REV_INS) and anything already recorded are left alone */
void rv_track_para(Doc *d, const Para *old, Para *cur);

/* ---- property changes kept verbatim */
enum { RK_TBL, RK_GRID, RK_TR, RK_TRREV, RK_TC, RK_TCREV, RK_SECT, RK_N };
typedef struct KeptChange {
  u8 kind;                  /* RK_: w:tblPrChange, w:tblGridChange, w:trPrChange, a row's w:ins/w:del, w:tcPrChange,
                               a cell's w:cellIns/w:cellDel/w:cellMerge, w:sectPrChange */
  u16 table, row; u8 col;   /* its table (Doc.tables number), row and column */
  u16 sect;                 /* RK_SECT: its section (Doc.sects number, 0 the last one's, the body's) */
  u16 rid;                  /* who and when */
  char *xml; u32 len;       /* the element exactly as read */
} KeptChange;
int  rv_keep_change(Doc *d, int kind, int table, int row, int col, int sect, const char *xml, u32 len, int rid);
/* the kept elements of a kind for that table, row and column (sect for RK_SECT) appended to o */
void rv_write_kept(Buf *o, const Doc *d, int kind, int table, int row, int col, int sect);
int  rv_has_kept(const Doc *d, int kind, int table, int row, int col, int sect);

/* ---- the people (people.xml) */
typedef struct Person { char author[64]; char *raw; u32 rawlen; } Person;   /* raw: its w15:person element */
int  rv_add_person(Doc *d, const char *author, const char *raw, u32 rawlen);

/* ---- moves */
void rv_set_move(Para *p, u32 a, u32 b, int move);   /* runs a..b of p made part of move (0: none) */
int  rv_move_id(Doc *d, const char *name);       /* a move's number for CharFmt.move (added if new), 0 if none */
const char *rv_move_name(const Doc *d, int id);  /* "" if none */

/* ---- how tracked changes and comments are shown */
enum { RVM_ALL, RVM_SIMPLE, RVM_NONE, RVM_ORIGINAL };           /* Display for Review */
enum { RVH_COMMENTS = 1, RVH_INSDEL = 2, RVH_FORMAT = 4 };      /* Show Markup: kinds not shown */
enum { RVB_FORMAT, RVB_ALL, RVB_INLINE };                       /* balloons: comments and formatting; revisions too; none */
typedef struct RevView {
  u8 mode, hide, balloons;
  char (*people)[64]; int npeople;   /* reviewers whose markup isn't shown */
  u32 sig;                  /* changes whenever any of it does */
} RevView;
RevView *rv_view(Doc *d);                        /* made on first use (defaults); NULL if out of memory */
int  rv_mode(const Doc *d);
int  rv_hidden_kinds(const Doc *d);
int  rv_balloons(const Doc *d);
void rv_set_mode(Doc *d, int mode);
void rv_set_hide(Doc *d, int hide);
void rv_set_balloons(Doc *d, int b);
int  rv_person_shown(const Doc *d, const char *author);
void rv_show_person(Doc *d, const char *author, int shown);   /* author NULL: everyone */
u32  rv_sig(const Doc *d);                       /* layout's cache key for how changes are shown (0: the default) */
/* kinds of change, for marks and bars */
enum { RVK_INS = 1, RVK_DEL, RVK_FMT, RVK_PARA, RVK_MOVE };
/* how a run shows: 1 if it is hidden (No Markup's deletions, Original's insertions, deletions in
   balloons); else *f is what to draw (rev cleared where the change shows as plain text, the old look in
   Original) and *mark the bar beside its line (RVK_, 0 none) */
int  rv_show_run(const Doc *d, CharFmt *f, u8 *mark);
int  rv_show_para(const Doc *d, const ParaFmt *pf);   /* a paragraph formatting change's bar (RVK_PARA), 0 none */
int  rv_show_comments(const Doc *d);             /* comments' balloons and shading shown */
int  rv_shown_change(const Doc *d, const CharFmt *f);   /* a run's change is shown (for Accept All Changes Shown) */

/* ---- balloons: what goes in the margin of a page */
enum { RVI_COMMENT, RVI_DELETE, RVI_FORMAT, RVI_PARA };
typedef struct {
  u8 kind;                  /* RVI_ */
  int k;                    /* the comment (Doc.comments index), else the change's Doc.revs index */
  u16 frev;                 /* RVI_FORMAT, RVI_PARA: the formatting change */
  Pos a, b;                 /* what it is about */
  i32 x, y;                 /* where it is anchored: twips from the page's left edge, and its line's bottom from the page's top */
} RvItem;
int  rv_page_items(struct Layout *L, u32 page, RvItem *out, int max);
/* a balloon's or the Reviewing pane's text (UTF-8): "Deleted: ...", "Formatted: Bold, Italic", a comment's
   text. tr translates Quill's words (NULL: English) */
typedef const char *(*RvTr)(const char *);
void rv_item_text(const Doc *d, const RvItem *it, char *out, size_t cap, RvTr tr);
void rv_fmt_names(const Doc *d, u16 frev, const CharFmt *now, const ParaFmt *pnow, char *out, size_t cap, RvTr tr);
int  rv_any_margin(Doc *d);                      /* anything for the margin's balloons (cached on Doc.ver) */

/* ---- tracked changes as a whole */
int  rv_is_change(const CharFmt *f);             /* rev or frev */
/* the next (dir > 0) or previous change of any kind from p (insertion, deletion, move, a run's or a
   paragraph's formatting): its extent; 0 if none. doc_next_change is this */
int  rv_next_change(Doc *d, Pos p, int dir, Pos *a, Pos *b);
/* accept (else reject) the changes between a and b: insertions, deletions and moves, formatting of runs
   and of the paragraphs a..b touch, the kept changes of their tables and sections (accept only:
   rejecting those leaves them). shownonly: only changes rv_shown_change shows. One undo step;
   0 if there was none. doc_resolve is this with shownonly 0 */
int  rv_resolve(Doc *d, Pos a, Pos b, int accept, int shownonly);
/* the changes in the document, in order, for the Reviewing pane */
enum { RVC_INS, RVC_DEL, RVC_MOVEFROM, RVC_MOVETO, RVC_FORMAT, RVC_PARA, RVC_COMMENT, RVC_TABLE, RVC_SECT };
typedef struct { u8 kind; int k; u16 frev; Pos a, b; } RvChange;   /* k: Doc.revs (Doc.comments for RVC_COMMENT, Doc.kchg for kept) */
int  rv_changes(Doc *d, RvChange *out, int max);  /* how many there are (out gets at most max) */
void rv_change_text(const Doc *d, const RvChange *c, char *out, size_t cap, RvTr tr);   /* "Inserted: ..." */
const char *rv_change_author(const Doc *d, const RvChange *c);
const char *rv_change_date(const Doc *d, const RvChange *c);
/* the authors of changes and comments, each once (for Show Markup's reviewers); how many */
int  rv_reviewers(const Doc *d, const char **out, int max);

/* ---- comments: threads, replies and Resolve */
int  rv_thread_root(const Doc *d, int k);        /* the comment a thread starts with */
/* a reply to comment k's thread (UTF-8 text): its marks go right after the thread's own; its index, -1 if
   it can't. One undo step */
int  rv_reply(Doc *d, int k, const char *text, const char *author, const char *initials, const char *date);
void rv_set_done(Doc *d, int k, int done);       /* the whole thread resolved or open again */
int  rv_delete_thread(Doc *d, int k);            /* a comment and its replies taken out (one undo step) */
int  rv_delete_all_comments(Doc *d);
int  rv_replies(const Doc *d, int k, int *out, int max);   /* comment k's replies, in order; how many */
int  rv_comment_live(Doc *d, int k);             /* its marks are in the text */

/* ---- Track Changes Options (Quill's, for every document) */
enum { RVO_NONE, RVO_COLOR, RVO_BOLD, RVO_ITALIC, RVO_UNDERLINE, RVO_DUNDERLINE, RVO_STRIKE, RVO_DSTRIKE, RVO_HIDDEN, RVO_N };
enum { RVL_NONE, RVL_LEFT, RVL_RIGHT, RVL_OUTSIDE };
typedef struct {
  u8 insmark, delmark, fmtmark;      /* RVO_ */
  u32 inscolor, delcolor, fmtcolor;  /* 0xRRGGBB, or COLOR_AUTO: by author */
  u8 lines;                          /* changed lines: RVL_ */
  u32 linecolor;
  u32 commentcolor;                  /* COLOR_AUTO: by author */
  u8 movefrom, moveto;               /* RVO_ */
  u32 movecolor;
} RvOpts;
/* the document's own (settings.xml): Doc.rvsettings bits. Track moves and Track formatting are the
   document's, as in Word */
enum { RVS_VIEW = 1, RVS_NOMOVES = 2, RVS_NOFMT = 4 };
extern RvOpts rv_opts;
void rv_opts_default(RvOpts *o);
int  rv_opts_load(RvOpts *o, const char *text);  /* from "key=value" lines; how many were understood */
void rv_opts_save(const RvOpts *o, Buf *out);
u32  rv_author_color(const Doc *d, int rid);     /* each reviewer's colour (the same for their comments) */
u32  rv_comment_color(const Doc *d, const char *author);
/* how a change is drawn: its colour and marks (RVO_) */
void rv_ink(const Doc *d, const CharFmt *f, u32 *color, u8 *mark);

/* ---- the document's settings (settings.xml w:revisionView) and freeing */
void rv_free(Doc *d);                            /* everything WP5 adds to a Doc (doc_free does it) */

#endif
