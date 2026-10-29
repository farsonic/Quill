/*
 * layout.h - lines and pages, in twips, from the document and the fonts' metrics.
 *
 * Each paragraph is laid out into lines of boxes (a word or part of one in one format, a run of
 * spaces, a tab), cached by the paragraph's (pointer, version), so typing re-lays out only the
 * paragraph typed in. Pagination then places the lines on pages (cheap: a walk over line heights).
 * Coordinates: x from the page's left edge, y from the page's top edge, both twips.
 */
#ifndef QUILL_LAYOUT_H
#define QUILL_LAYOUT_H

#include "doc.h"
#include "fonts.h"

enum { BX_TEXT, BX_SPACE, BX_TAB, BX_BREAK, BX_PIC, BX_NOTE, BX_MARK, BX_ENOTE, BX_DROP, BX_FLOAT, BX_FIELD,
       BX_REVHIDE };   /* BX_REVHIDE (WP5): text a tracked change hides as shown (Display for Review), no width */   /* BX_FIELD: a page
                                                             field (FIELD_PAGE, FIELD_PAGES) in the body or a story being edited */   /* BX_FLOAT: a floating
                                                             object's reference, no width (the object is in Layout.floats) */   /* BX_BREAK: a line break ('\v'), drawn as nothing;
                                                             BX_PIC: a picture; BX_NOTE: a footnote's number;
                                                             BX_MARK: a comment's start or end, no width;
                                                             BX_ENOTE: an endnote's number; BX_DROP: a drop cap (in its
                                                             own size, its baseline lay_drop_dy below the first line's) */
typedef struct {
  u32 off;                    /* bytes in the paragraph */
  u16 len;                    /* bytes of text it covers */
  u8 kind;                    /* BX_ */
  u8 nspace;                  /* BX_SPACE: how many spaces */
  i32 x, w;                   /* from the line's start (its indent), after alignment */
  CharFmt f;
  u8 rvmark;                  /* WP5: a tracked change as shown (a bar beside its line: RVK_ kind + 1), 0 none */
} Box;

typedef struct {
  u32 start, end;             /* bytes of the paragraph on this line */
  u32 box0, nbox;
  i32 left;                   /* indent of the line from the text area's left edge */
  i32 width;                  /* of the text, trailing spaces left out */
  i32 height, ascent;         /* line box and baseline below its top */
} Line;

typedef struct {
  Para *p;                    /* the paragraph and its version when laid out: the cache key */
  u32 ver;
  i32 avail;                  /* text area width it was laid out for */
  Line *lines; u32 nlines;
  Box *boxes; u32 nboxes;
  u32 placed0;                /* index of its first line in Layout.placed */
  char label[32];             /* list label (bullet or number), UTF-8; "" if none */
  i32 labelw;                 /* its width */
  u8 laid;                    /* 0: not laid out yet (a partial update stopped before it) */
  u32 notebase;               /* footnotes before this paragraph (its first note's number is one more) */
  u32 enotebase;              /* the same for endnotes */
  u8 hyph;                    /* laid out with hyphenation */
  u8 shaped;                  /* its lines made room for floating objects (laid out afresh next time) */
  struct Layout *lay;         /* (WP22b) the layout it belongs to (its notes' numbers as shown) */
  u8 hidden;                  /* WP9: it has hidden text, laid out shown (1) or left out (0); 2: it has none */
  u32 rvsig;                  /* WP5: how tracked changes were shown when it was laid out (rv_sig) */
} ParaLay;
/* an object's size on the page (turned: the box round it) */
#define OBJ_W(im) obj_w(im)
#define OBJ_H(im) obj_h(im)
/* a floating object placed: x, y from the page's top left corner; w, h as drawn (turned) */
typedef struct { u32 page; i32 x, y, w, h; int img; u32 para; u8 wrap, early; } FloatBox;   /* early: from the last pass, its anchor not reached yet */

/* a placed line. y: its top from the page's top; x, w: the column it is in (the text area, a
   section's column, or a table cell's), from the text area's left edge. lnum: the line number shown
   beside it (0: none), its right edge lnd left of x */
typedef struct { u32 para, line; u32 page; i32 y; i32 x, w; u32 lnum; i32 lnd; } Placed;
/* the line between two columns, x from the text area's left */
typedef struct { u32 page; i32 x, y0, y1; } ColSep;
/* a paragraph's border box and shading on one page: the box's edges (twips on the page), which of
   its lines to draw (PB_TOPLINE... ), and where a line between it and the paragraph before goes */
enum { PB_TOPLINE = 1, PB_BOTTOMLINE = 2, PB_BETWEENLINE = 4 };
typedef struct { u32 page, para; i32 x0, x1, y0, y1, ybetween; u8 lines; } ParaBox;

/* a table cell's box on a page, from the page's top left corner, for its borders and fill */
typedef struct {
  u32 page; i32 x, y, w, h; u8 borders; u32 shade; u8 bstyle[4], bwidth[4]; u32 bcolor[4];
  u8 depth;                   /* 1: a nested table's */
  u16 table, row; u8 col, span;   /* the cell it is */
} CellBox;   /* depth 1: a nested table's */

/* each page's page setup and section; first: that section's first page; last: it is the document's last section */
typedef struct { const PageSetup *pg; const SectFmt *sec; u32 first; u8 last; } PageInfo;

/* (WP22b) notes: each one's number as shown (0: not referred to yet), its format (NNF_, core/notes.h),
   first number and whether it is counted afresh on each page; a long footnote's lines l0..l1 shown on
   a page (the rest overleaf, under the continuation separator); a line above endnotes (cont: the
   continuation separator, at the top of a page they go on to) */
typedef struct NoteNum { u32 lab; u16 start; u8 fmt, perpage; } NoteNum;
typedef struct NoteCut { u32 page, note, l0, l1; } NoteCut;
typedef struct NoteSepAt { u32 page; i32 y; u8 cont; } NoteSepAt;
typedef struct Layout {
  Doc *d;
  FontSet *fs;
  ParaLay *pl; u32 npl;       /* one per paragraph, in order */
  Placed *placed; u32 nplaced, placedcap;
  u32 *page_first;            /* index into placed of each page's first line */
  u32 *page_end;              /* one past its last (a table row that runs onto the next page puts
                                 lines of two pages between these: check Placed.page) */
  u32 npages, pagecap;
  CellBox *cells; u32 ncells, cellcap;
  int complete;               /* 0: the last update stopped early; the pages so far are right */
  u32 *notenum;               /* each footnote's number (0: not referred to) */
  struct { u32 page, note; } *pn; u32 npn, pncap;    /* the footnotes on each page, in order */
  u32 *enotenum;              /* each endnote's number (0: not referred to) */
  struct { u32 note, para, page; i32 y; } *en; u32 nen;   /* where each endnote paragraph goes, after the text */
  u32 ensep_page; i32 ensep_y; /* the short line above the endnotes */
  ColSep *seps; u32 nseps, sepcap;   /* lines between columns */
  ParaBox *pbox; u32 npbox, pboxcap;   /* paragraphs' borders and shading */
  FloatBox *floats; u32 nfloats, floatcap;   /* floating objects, in the order their anchors come */
  FloatBox *early; u32 nearly;  /* those placed from the page or margin the text wraps round: the lines on their page before their anchors wrap round them too */
  const SectFmt **psec; u32 npsec;   /* each paragraph's section format (for the last update) */
  u32 lastfirst;              /* the last section's first paragraph */
  PageInfo *pinfo; u32 pinfocap;     /* each page's (npages of them) */
  const PageSetup *pg;        /* (while paginating: the page setup of the page being filled) */
  int oom;
  i32 webw;                   /* web layout: 0 for pages; else one long page, text this wide (twips), no page breaks,
                                 headers, footers or footnotes */
  u32 laid;                   /* paragraphs laid out afresh by the last update (for tests and timing) */
  struct { u32 key; FaceSize *z; } fcache[64];      /* format -> metrics (font substitution is slow) */
  int nfcache;
  /* (WP22b) */
  NoteNum *fnn, *enn;         /* each footnote's and endnote's number as shown */
  NoteCut *ncut; u32 nncut, ncutcap;   /* footnotes split across pages */
  NoteSepAt *enseps; u32 nenseps, ensepcap;   /* the lines above endnotes (each section's, or the document's) */
  u32 encap;                  /* room in en */
  i32 sepw;                   /* the line lay_notes or lay_endnotes last gave: its length (twips), 0 none */
  u8 notesep_cont;            /* ... and it is a continuation separator (the text area's width) */
  u8 showhidden;              /* WP9: hidden text laid out (formatting marks shown), else it takes no room (and the PDF leaves it out) */
} Layout;
/* WP9: a box of hidden text that takes no room in L */
#define LAY_HIDDEN(L, b) (((b)->f.fx2 & FX2_HIDDEN) && !(L)->showhidden)

/* a header or footer laid out for one page (its page fields filled in) */
typedef struct { Para *p; ParaLay pl; i32 y; } FPara;     /* y: the top of its first line from the page top */
typedef struct { FPara *f; u32 n; FloatBox *fb; u32 nfb; } Furn;    /* fb: floating objects in a header or footer */

/* a new, empty layout of d measured with fs (neither is copied or freed by it); NULL if out of
   memory. Call lay_update to lay it out, lay_free when done */
Layout *lay_new(Doc *d, FontSet *fs);
/* a page's page setup (its section's, or the document's), and its header (footer: footer) */
const PageSetup *lay_page(Layout *L, u32 page);
Story *lay_page_story(Layout *L, u32 page, int footer);
/* which of its section's headers and footers a page has (HF_), and whose: the section format's
   number (Doc.sects) if it has its own, 0 the document's */
void lay_page_hf(Layout *L, u32 page, int *which, int *sect);
/* the header (footer: the footer) of a page; 0 if it has none (or out of memory). x of lines: from
   the left margin, as body text */
int  lay_furniture(Layout *L, u32 page, int footer, Furn *f);
void lay_furniture_free(Furn *f);    /* frees what lay_furniture, lay_notes, lay_endnotes and lay_textbox fill in */
/* the footnotes at the bottom of a page (or under its text), laid out (y set), and where the line
   above them goes (-1: none; L->sepw its length); 0 if the page has none */
int  lay_notes(Layout *L, u32 page, Furn *f, i32 *sepy);
/* the endnotes on a page (after the document's text), laid out (y set), and where the line above them
   goes (-1: not on this page); 0 if the page has none */
int  lay_endnotes(Layout *L, u32 page, Furn *f, i32 *sepy);
/* the number a BX_NOTE box shows (counted in the text); the label a BX_NOTE or BX_ENOTE box shows ("3",
   "iii", or as the Footnote and Endnote window set it: core/notes.h) */
u32  lay_note_number(ParaLay *pl, u32 box);
void lay_note_label(ParaLay *pl, u32 box, char *out);
void lay_roman(u32 n, char *out);    /* lower case: i, ii, iii, iv...; out at least 16 bytes */
/* what a BX_FIELD box shows on a page: its number, or the number of pages (a story being edited: those it was opened with) */
void lay_field_text(Layout *L, const Para *p, const Box *b, u32 page, char *out);    /* page from 0 */
void lay_free(Layout *L);           /* L and everything it holds (not its Doc or FontSet) */
/* bring the layout up to date with the document; 0 if out of memory (the layout then stays usable,
   complete as far as there was room) */
int  lay_update(Layout *L);
/* the same, but laying out at most budget paragraphs afresh (0: no limit). The pages up to the
   first paragraph not laid out are complete; L->complete says whether that was all of them */
int  lay_update_part(Layout *L, u32 budget);
/* the placed lines of a page: [*a, *b), with Placed.page to be checked */
void lay_page_lines(Layout *L, u32 page, u32 *a, u32 *b);
/* where the caret goes for a position: page, x, y (top of the line), h (line height) */
void lay_caret(Layout *L, Pos p, u32 *page, i32 *x, i32 *y, i32 *h);
/* the position nearest to a point on a page */
Pos  lay_hit(Layout *L, u32 page, i32 x, i32 y);
/* the position on the line above or below p, at x (for the cursor keys); stays put at the ends */
Pos  lay_updown(Layout *L, Pos p, i32 x, int dir);
/* the start and end of the line p is on */
Pos  lay_line_start(Layout *L, Pos p);
Pos  lay_line_end(Layout *L, Pos p);
/* the metrics of a box's format (super/subscript at two thirds size) */
FaceSize *lay_face(Layout *L, const CharFmt *f);
i32  lay_text_x(Layout *L, const Box *b, const char *t, u32 off);   /* x within a box of byte off */
/* a text box's (or shape's) text laid out inside it, cx wide (0: its own width): y from its top, the
   lines' x from its left (in FPara.pl lines' left); 0 if it has none */
int  lay_textbox(Layout *L, const Image *im, i32 cx, Furn *f);
#define TB_INSET_X 144                /* a text box's text inset from its sides and top (Word's 0.1 and 0.05 inches) */
#define TB_INSET_Y 72
/* a shape's (SH_) outline as points: xy pairs in twips from the top left of its w x h (unrotated),
   at most max points; returns the count, *closed 0 for an open line. Curves come as short segments. */
u32  shape_points(int shape, i32 w, i32 h, i32 *xy, u32 max, int *closed);
/* SH_ARROW's head at the line's end (w, h): three points, for an outline lw twips wide */
void shape_arrowhead(i32 w, i32 h, i32 lw, i32 *xy);
#define SHAPE_MAXPTS 128                /* (WP18: 72 before more shapes came) */
/* an object's outline in its box (twips, turned): its wrap outline, a shape's own, or the box; how many
   points (at most max), 0 if none */
int  obj_outline(const Image *im, i32 *xy, int max);
/* a floating object (fb, laid out from im) dragged so its box's top left is at nx, ny (twips on its
   page): im's position changed to put it there (across from the margin; down from its paragraph if
   it was placed from it, else from the margin) */
void lay_float_moved(Layout *L, const FloatBox *fb, Image *im, i32 nx, i32 ny);
/* AutoFit for paragraph pi's table: the column widths (into grid; returns how many) its text wants
   (no wider than avail, the longest words fitting where they can), or with window, its columns in
   their proportions across avail. 0 if not in a table or out of memory */
int  lay_autofit(Layout *L, u32 pi, int window, i32 avail, i32 *grid);
/* the floating objects fb[0..n) of L's document in the order they stack, bottom first (by their
   Image.z, then in fb's order): a malloc'd list of indexes into fb, NULL if out of memory (then fb's
   own order will do) */
u32 *lay_stack_order(const Layout *L, const FloatBox *fb, u32 n);
/* the page (from 1) bookmark name starts on, 0 if it isn't in the text or isn't laid out yet */
int  lay_bmark_page(Layout *L, const char *name);
/* the floating object L->floats[fi] moved among the objects on its page (behind and in front of the
   text alike): up one (1), down one (-1), to the top (2) or the bottom (-2). The page's objects are
   numbered afresh, one undo step. 1 if it moved, 0 if it was there already (or out of memory) */
int  lay_restack(Layout *L, u32 fi, int how);
/* how far a drop cap's baseline is below its line's (its paragraph's first) */
i32  lay_drop_dy(const ParaLay *pl, const Box *b);

#endif
