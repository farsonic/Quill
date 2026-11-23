/*
 * mac/qm_qdtext.c - Octavo on the classic Mac OS: the page's text drawn with the Mac's own screen
 * fonts, as MacWrite and Word for the Mac drew it. GPL-2.0-only, as Quill.
 *
 * Until 23 November 2026 the page view showed each page exactly as Quill's PDF export draws it
 * (pdf_page_rgb: Quill's TrueType rasteriser, smoothed), and on the SE's one-bit screen the smoothed
 * edges were thresholded or dithered: fuzzy, uneven letters. A Mac program of the time drew its text
 * with QuickDraw in the System's bitmap fonts (Chicago, Geneva, New York, Monaco, Times, Helvetica,
 * Courier, at 9, 10, 12, 14, 18 and 24 points), each glyph a crisp pattern of pixels drawn for the
 * screen. That is what this file does.
 *
 * How layout and screen fonts agree. The page is laid out by Quill's core with Quill's TrueType
 * metrics, as before: line breaks, page breaks and every word's place stay exactly what the PDF,
 * the .docx in Word and the Amiga show. The core draws the page's picture without its text
 * (pdf_page_rgb_text, core/pdf.c: pictures, shapes, charts, borders, shading as always) and hands
 * over each run of text with the place the layout gave it (a run is a word, or a word's part in one
 * format). Each run is drawn here with QuickDraw at its layout position, in the Mac family that
 * stands for the document's font, at the run's size in pixels: Word for the Mac with fractional
 * widths off drew this way. Within a word the letters keep the bitmap font's own spacing; between
 * words the layout's spaces absorb the difference. The other way, laying the document out with the
 * Mac fonts' widths, would have changed where lines break on the Mac alone, so Octavo's pages would
 * no longer be the ones its PDF and Word show. docs/mac.md has the reasons and the measurements.
 *
 * So that words neither overlap nor leave gaps: a family whose letters are wider than the layout's
 * (Helvetica for Calibri) is drawn a size smaller when that fits better (each family, size and style
 * decided once, from the first page's runs: one size per kind of text, not per word); a word still
 * wider than its room (by a pixel; two from 16 pixels up) has its letters drawn a little closer, each at its
 * place scaled to the layout's width. Bitmap sizes the System has are favoured (choose_size); without
 * one the TrueType outline is drawn (System 7 has Times, Helvetica, Courier ... as TrueType too), and
 * System 7's QuickDraw draws it without smoothing, crisp on a one-bit screen.
 *
 * The text is drawn into a one-bit offscreen bitmap the page's size (a GrafPort of our own), one
 * colour at a time, and each set pixel put into the page's picture in that colour. On a one-bit
 * screen every run is black (white when it is white or nearly), so coloured headings are solid,
 * not dithered. The view then shows the picture as before: its caret, selection, row comparison
 * and copying are unchanged. What it needs to put the caret between the letters as drawn is kept
 * per run (qt_map_x, qt_unmap_x).
 *
 * Not drawn here, the core's rasteriser draws them as before: text in turned frames, small capitals,
 * characters Mac Roman lacks (Greek, Cyrillic, East Asian, the euro), scaled text, embossed and
 * engraved text, WordArt, equations, the text inside charts drawn as pictures.
 */
#include <string.h>
#include <stdlib.h>
#include "mui/mm.h"
#include "quill_mac.h"
#include "qm_mac.h"
#include "../core/pdf.h"

#define FACE_BOLD 1
#define FACE_ITALIC 2
#define FACE_OUTLINE 8
#define FACE_SHADOW 16

/* a run as recorded while the core lays out the page's content, then drawn */
typedef struct {
	long x, base, w;            /* page pixels: the layout's start and width, the baseline */
	short fam, want, size, face;    /* the Mac family; the size in pixels asked, drawn; QuickDraw's face */
	unsigned long color, ulcolor;
	signed char kind, ul, strike;
	short n;                    /* Mac Roman characters */
	long text, lo, mo;          /* in the pools: the characters; n + 1 layout offsets from x; n + 1 drawn */
	const void *key;            /* the core's face and size (FaceSize), for the size decided once */
} QRun;

struct QtWords {
	QRun *r;
	long n, cap;
	char *ch;
	long nch, chcap;
	short *sh;
	long nsh, shcap;
	int oom;
};

/* ---- the Mac's families ---- */

/* the bitmap sizes a family has (plain style), and whether it has a TrueType outline: its 'FOND''s
   font association table (Inside Macintosh: Text, "The Font Family Resource") */
typedef struct {
	char name[32];
	short num;                  /* -1 not installed */
	unsigned char bitmap[128];  /* bitmap[s]: a plain bitmap face of s points */
	int tt;
} Family;
#define NFAM 16
static Family fams[NFAM];
static int nfams;

static Family *family(const char *name)
{
	int i;
	unsigned char pn[64];
	short num = 0;
	unsigned char **h;
	Family *f;
	for (i = 0; i < nfams; i++)
		if (!strcmp(fams[i].name, name))
			return &fams[i];
	if (nfams >= NFAM)
		return NULL;
	f = &fams[nfams++];
	memset(f, 0, sizeof *f);
	strncpy(f->name, name, sizeof f->name - 1);
	GetFNum(mac_pstr(pn, name), &num);
	/* GetFNum answers 0 for a family it lacks; Chicago (the system font) is 0 by right */
	f->num = num || !strcmp(name, "Chicago") ? num : -1;
	if (f->num < 0)
		return f;
	h = (unsigned char **)GetResource(MAC_TYPE('F', 'O', 'N', 'D'), f->num);
	if (h && *h) {
		const unsigned char *p = *h;
		int k, n = *(const short *)(p + 52) + 1;
		for (k = 0; k < n && k < 64; k++) {
			short sz = *(const short *)(p + 54 + 6 * k), st = *(const short *)(p + 56 + 6 * k);
			if (sz == 0)
				f->tt = 1;
			else if (sz > 0 && sz < 128 && st == 0)
				f->bitmap[sz] = 1;
		}
	} else
		f->bitmap[12] = 1;      /* no table: an old FONT, say 12 */
	mm_log("Mac font %s: number %d%s, bitmap sizes%s%s%s%s%s%s%s", name, f->num, f->tt ? ", TrueType" : "",
	       f->bitmap[9] ? " 9" : "", f->bitmap[10] ? " 10" : "", f->bitmap[12] ? " 12" : "", f->bitmap[14] ? " 14" : "",
	       f->bitmap[18] ? " 18" : "", f->bitmap[20] ? " 20" : "", f->bitmap[24] ? " 24" : "");
	return f;
}

static int lower_eq(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		char x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
		if (x != y)
			return 0;
	}
	return !*a && !*b;
}

/* the document's families as the Mac's (lower case): serif ones become Times, sans serif ones
   Helvetica, typewriter ones Courier, each the nearest in its widths (Times New Roman, Arial and
   Courier New were drawn to Times', Helvetica's and Courier's) */
static const struct { const char *doc, *mac; } fammap[] = {
	{ "times new roman", "Times" }, { "liberation serif", "Times" }, { "cambria", "Times" },
	{ "caladea", "Times" }, { "georgia", "Times" }, { "garamond", "Times" }, { "book antiqua", "Palatino" },
	{ "palatino linotype", "Palatino" }, { "constantia", "Times" }, { "century schoolbook", "New Century Schlbk" },
	{ "baskerville old face", "Times" }, { "tinos", "Times" }, { "dejavu serif", "Times" }, { "noto serif", "Times" },
	{ "arial", "Helvetica" }, { "liberation sans", "Helvetica" }, { "calibri", "Helvetica" }, { "carlito", "Helvetica" },
	{ "calibri light", "Helvetica" }, { "aptos", "Helvetica" }, { "verdana", "Geneva" }, { "tahoma", "Geneva" },
	{ "segoe ui", "Helvetica" }, { "trebuchet ms", "Helvetica" }, { "arimo", "Helvetica" }, { "arial narrow", "Helvetica" },
	{ "dejavu sans", "Geneva" }, { "noto sans", "Helvetica" }, { "open sans", "Helvetica" }, { "gill sans mt", "Helvetica" },
	{ "courier new", "Courier" }, { "liberation mono", "Courier" }, { "consolas", "Courier" }, { "cousine", "Courier" },
	{ "lucida console", "Monaco" }, { "dejavu sans mono", "Monaco" }, { "menlo", "Monaco" },
};

/* the Mac family number for a run: the document's family itself when the Mac has it (Palatino,
   Geneva, Times...), else the table's, else by the face the layout used (Liberation Serif, Carlito
   and so on: Quill's substitutes); -1 if none of those is installed */
static int mac_family(const char *docfam, const char *coreface)
{
	static struct { char doc[32], core[32]; short num; } memo[24];
	static int nmemo;
	const char *name = NULL;
	Family *f;
	int i;
	for (i = 0; i < nmemo; i++)
		if (!strcmp(memo[i].doc, docfam) && !strcmp(memo[i].core, coreface))
			return memo[i].num;
	f = family(docfam);
	if (!f || f->num < 0) {
		for (i = 0; i < (int)(sizeof fammap / sizeof fammap[0]) && !name; i++)
			if (lower_eq(fammap[i].doc, docfam))
				name = fammap[i].mac;
		for (i = 0; i < (int)(sizeof fammap / sizeof fammap[0]) && !name; i++)
			if (lower_eq(fammap[i].doc, coreface))
				name = fammap[i].mac;
		if (!name)
			name = strstr(coreface, "Mono") ? "Courier" : strstr(coreface, "Sans") ? "Helvetica" : "Times";
		f = family(name);
		if ((!f || f->num < 0) && strcmp(name, "Times"))
			f = family(!strcmp(name, "Courier") ? "Monaco" : !strcmp(name, "Helvetica") ? "Geneva" : "Times");
		if (!f || f->num < 0)
			f = family("New York");
	}
	i = f && f->num >= 0 ? f->num : -1;
	if (nmemo < 24) {
		strncpy(memo[nmemo].doc, docfam, 31);
		strncpy(memo[nmemo].core, coreface, 31);
		memo[nmemo++].num = (short)i;
	}
	return i;
}

static Family *family_of(int num)
{
	int i;
	for (i = 0; i < nfams; i++)
		if (fams[i].num == num)
			return &fams[i];
	return NULL;
}

/* ---- recording the page's runs (the core's PdfTextFn) ---- */

unsigned char qm_roman_of(unsigned long u);

typedef struct {
	QtWords *w;
	Layout *L;
	int dpi;
	long declined;
} Rec;

static long pxl(long tw, int dpi) { return (tw * dpi + 720) / 1440; }

static int grow(void **p, long *cap, long need, long size)
{
	void *n;
	long c;
	if (need <= *cap)
		return 1;
	c = *cap ? *cap * 2 : 256;
	while (c < need)
		c *= 2;
	if (!(n = realloc(*p, (size_t)(c * size))))
		return 0;
	*p = n;
	*cap = c;
	return 1;
}

static QRun *new_run(QtWords *w)
{
	QRun *r;
	if (!grow((void **)&w->r, &w->cap, w->n + 1, sizeof(QRun))) {
		w->oom = 1;
		return NULL;
	}
	r = &w->r[w->n++];
	memset(r, 0, sizeof *r);
	return r;
}

static int take_run(void *ctx, const PdfTextRun *t)
{
	Rec *rc = (Rec *)ctx;
	QtWords *w = rc->w;
	const CharFmt *f = t->f;
	QRun *r;
	long i, k;
	char mac[256];
	unsigned short at[257];     /* each Mac character's first byte in t->s */
	int dpi = rc->dpi;
	if (w->oom)
		return 0;
	if (t->kind == PT_LINE) {
		if (!(r = new_run(w)))
			return 0;
		r->kind = PT_LINE;
		r->x = pxl(t->x, dpi);
		r->w = pxl(t->x + t->w, dpi) - r->x;
		r->base = pxl(t->pageh - t->y, dpi);
		r->want = r->size = (short)pxl(t->size, dpi);
		r->color = r->ulcolor = t->color;
		r->ul = (signed char)t->ul;
		r->fam = -1;
		return 1;
	}
	/* what QuickDraw can't draw as the core does: left to the core */
	if ((f->scale && f->scale != 100) || (f->fx & FX_SMALLCAPS) || (f->fx2 & (FX2_EMBOSS | FX2_ENGRAVE)) || t->n > 250 || !t->z)
		goto decline;
	for (i = 0, k = 0; i < (long)t->n && k < 250; ) {
		int used;
		u32 cp = utf8_get(t->s + i, t->n - (u32)i, &used);
		unsigned char m;
		if (cp == 0xAD) {               /* an optional hyphen shows only where a line breaks */
			i += used;
			continue;
		}
		if (cp == '\t' || cp == '\v' || cp == 0xA0)
			cp = ' ';
		if (cp == 0x2011)
			cp = '-';
		if ((f->flags & CF_CAPS) && ((cp >= 'a' && cp <= 'z') || (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7)))
			cp -= 32;
		/* Mac Roman, less the euro: System 7's fonts draw 0xDB as the currency sign */
		m = cp == 0x20AC ? 0 : qm_roman_of(cp);
		if (!m || m < 0x20)
			goto decline;
		at[k] = (unsigned short)i;
		mac[k++] = (char)m;
		i += used;
	}
	if (!k)
		goto decline;
	at[k] = (unsigned short)t->n;
	if (!(r = new_run(w)))
		return 0;
	if (!grow((void **)&w->ch, &w->chcap, w->nch + k, 1) || !grow((void **)&w->sh, &w->shcap, w->nsh + 2 * (k + 1), sizeof(short))) {
		w->oom = 1;
		w->n--;
		return 0;
	}
	{
		const char *docfam = f->font < qd.doc->nfonts ? qd.doc->fonts[f->font] : "Times New Roman";
		r->fam = (short)mac_family(docfam, t->z->face->family);
	}
	if (r->fam < 0) {
		w->n--;
		goto decline;
	}
	r->kind = PT_TEXT;
	r->x = pxl(t->x, dpi);
	r->w = pxl(t->x + t->w, dpi) - r->x;
	r->base = pxl(t->pageh - t->y, dpi);
	r->want = r->size = (short)(pxl(t->size, dpi) < 4 ? 4 : pxl(t->size, dpi));
	r->face = (short)(((f->flags & CF_BOLD) ? FACE_BOLD : 0) | ((f->flags & CF_ITALIC) ? FACE_ITALIC : 0) |
	                  ((f->fx2 & FX2_OUTLINE) ? FACE_OUTLINE : 0) | ((f->fx2 & FX2_SHADOW) ? FACE_SHADOW : 0));
	r->color = t->color;
	r->ulcolor = t->ulcolor;
	r->ul = (signed char)t->ul;
	r->strike = (signed char)t->strike;
	r->n = (short)k;
	r->key = t->z;
	r->text = w->nch;
	memcpy(w->ch + w->nch, mac, (size_t)k);
	w->nch += k;
	/* the layout's place of each character, as the caret has it (lay_text_x: the run measured up to it) */
	r->lo = w->nsh;
	r->mo = w->nsh + k + 1;
	for (i = 0; i <= k; i++) {
		long tw = at[i] ? lay_measure(rc->L, f, t->s, at[i]) : 0;
		w->sh[r->lo + i] = (short)(pxl(t->x + tw, dpi) - r->x);
		w->sh[r->mo + i] = w->sh[r->lo + i];
	}
	w->nsh += 2 * (k + 1);
	return 1;
decline:
	rc->declined++;
	return 0;
}

/* ---- drawing ---- */

/* our own GrafPort over a one-bit bitmap (Inside Macintosh: Imaging With QuickDraw, "Basic QuickDraw":
   portBits at 2, visRgn at 24) */
static short port_rec[54];      /* a GrafPort is 108 bytes, word aligned */
static int port_open;
typedef struct { void *base; short rowbytes; Rect bounds; } BitMap;

static void *bits_port(BitMap *bm, int w, int h)
{
	Rect r;
	void *rgn;
	if (!port_open) {
		OpenPort(port_rec);
		port_open = 1;
	}
	SetPort(port_rec);
	SetPortBits(bm);
	PortSize((short)w, (short)h);
	r.left = 0; r.top = 0; r.right = (short)w; r.bottom = (short)h;
	rgn = *(void **)((char *)port_rec + 24);
	RectRgn(rgn, &r);
	ClipRect(&r);
	TextMode(1);                /* srcOr */
	PenNormal();
	return port_rec;
}

/* the size a family, size and style is drawn at: decided once from the runs of a page, and kept.
   Each size from the one asked down to a fifth smaller (three pixels at least) is measured: its runs'
   widths in the Mac font against the layout's. Wider costs twice what narrower does (a run wider than
   its room has its letters closed up; a narrower one only leaves its space a little wider), and a
   bitmap size the System has is let off 3 per cent (it is drawn for the screen; an outline size is
   made from TrueType). The cheapest wins. In the testbed (23 November 2026) that made Cambria 11
   Times 11, Calibri 26 Helvetica 24 and Calibri 14 bold Helvetica 11 bold: Helvetica's letters are
   wider than Calibri's, its bold ones more so. Without an outline only bitmap sizes are tried,
   the nearest when none is in range. */
typedef struct { short fam, want, face; const void *key; short size; } SizeMemo;
static SizeMemo sizes_memo[96];
static int nsizes_memo;
static long squeezed_runs, drawn_runs, max_over, max_under, page_squeezed;

/* the runs of g's kind in size: their widths in the Mac font and in the layout, in per mille */
static long ratio_at(QtWords *w, const QRun *g, int size)
{
	long i, mac = 0, lay = 0, seen = 0;
	TextSize((short)size);
	for (i = 0; i < w->n && seen < 80; i++) {
		QRun *r = &w->r[i];
		if (r->kind != PT_TEXT || r->fam != g->fam || r->want != g->want || r->face != g->face || r->key != g->key)
			continue;
		mac += TextWidth(w->ch + r->text, 0, r->n);
		lay += r->w;
		seen++;
	}
	return lay > 0 ? mac * 1000 / lay : 1000;
}

static short choose_size(QtWords *w, const QRun *g)
{
	Family *f = family_of(g->fam);
	int i, s, best = -1, span = g->want / 5 > 3 ? g->want / 5 : 3, lo = g->want - span;
	long r, rbest = 0, cost, cbest = 0x7FFFFFFL;
	for (i = 0; i < nsizes_memo; i++)
		if (sizes_memo[i].fam == g->fam && sizes_memo[i].want == g->want && sizes_memo[i].face == g->face && sizes_memo[i].key == g->key)
			return sizes_memo[i].size;
	if (lo < 4)
		lo = 4;
	TextFont(g->fam);
	TextFace(g->face);
	for (s = g->want; s >= lo; s--) {
		if (f && !f->tt && !f->bitmap[s])
			continue;
		r = ratio_at(w, g, s);
		cost = r > 1000 ? 2 * (r - 1000) : 1000 - r;
		if (f && f->bitmap[s])
			cost -= 30;
		if (cost < cbest) {
			cbest = cost;
			best = s;
			rbest = r;
		}
	}
	for (s = 0; best < 0 && s < 40; s++) {
		/* bitmaps only and none in range: the nearest, rather than one QuickDraw would scale */
		if (g->want - s > 3 && f && f->bitmap[g->want - s]) best = g->want - s;
		else if (g->want + s < 128 && f && f->bitmap[g->want + s]) best = g->want + s;
	}
	if (best < 0)
		best = g->want;
	if (nsizes_memo < (int)(sizeof sizes_memo / sizeof sizes_memo[0])) {
		SizeMemo *m = &sizes_memo[nsizes_memo++];
		m->fam = g->fam; m->want = g->want; m->face = g->face; m->key = g->key; m->size = (short)best;
	}
	mm_log("text: %s %d px%s%s drawn at %d%s (its runs %ld per mille of the layout's width)",
	       f ? f->name : "?", g->want, g->face & FACE_BOLD ? " bold" : "", g->face & FACE_ITALIC ? " italic" : "", best,
	       f && f->bitmap[best] ? ", a bitmap size" : ", the outline", rbest);
	return (short)best;
}

/* one run's glyphs (and its own underline and strike lines) */
static void draw_run(QtWords *w, QRun *r)
{
	const char *t = w->ch + r->text;
	short *mo = w->sh + r->mo;
	long mw, i, slack;
	TextFont(r->fam);
	TextFace(r->face);
	TextSize(r->size);
	mw = TextWidth(t, 0, r->n);
	/* a run wider than its room by a pixel would eat a third of a space at 11 points: closed up from
	   the first pixel (two from 16 pixels up) */
	slack = r->size >= 16 ? 1 : 0;
	drawn_runs++;
	if (r->w - mw > max_under)
		max_under = r->w - mw;
	if (mw > r->w + slack && r->n > 1 && r->w > 0) {
		/* wider than its room: each letter at its place scaled to the layout's width */
		long acc = 0;
		squeezed_runs++;
		page_squeezed++;
		if (mw - r->w > max_over)
			max_over = mw - r->w;
		for (i = 0; i < r->n; i++) {
			long at = acc * r->w / mw;
			mo[i] = (short)at;
			MoveTo((short)(r->x + at), (short)r->base);
			DrawText(t, (short)i, 1);
			acc += TextWidth(t, (short)i, 1);
		}
		mo[r->n] = (short)r->w;
	} else {
		long acc = 0, at = 0;
		/* one letter wider than its room ("A" before a word): half the excess taken from the space on
		   either side, rather than all from the next */
		if (r->n == 1 && mw > r->w + slack && r->w > 0)
			at = -(mw - r->w) / 2;
		MoveTo((short)(r->x + at), (short)r->base);
		DrawText(t, 0, r->n);
		for (i = 0; i < r->n; i++) {
			mo[i] = (short)(acc + at);
			acc += TextWidth(t, (short)i, 1);
		}
		mo[r->n] = (short)(acc + at);
	}
}

/* an underline of a kind (UL_) from x, w pixels wide, under baseline base for text of size px */
static void draw_ul(long x, long w, long base, int size, int kind)
{
	long y = base + (size >= 9 ? (size + 5) / 9 : 1), i;
	int thick = size >= 30 ? 2 : 1;
	if (w <= 0)
		return;
	PenSize(1, (short)(kind == UL_THICK ? thick + 1 : thick));
	switch (kind) {
	case UL_DOUBLE:
		MoveTo((short)x, (short)y); LineTo((short)(x + w - 1), (short)y);
		MoveTo((short)x, (short)(y + 2)); LineTo((short)(x + w - 1), (short)(y + 2));
		break;
	case UL_DOTTED: case UL_DASHED:
		for (i = 0; i < w; i += kind == UL_DOTTED ? 2 : 5) {
			MoveTo((short)(x + i), (short)y);
			LineTo((short)(x + i + (kind == UL_DOTTED ? 0 : 2 < w - i - 1 ? 2 : w - i - 1)), (short)y);
		}
		break;
	case UL_WAVE:
		for (i = 0; i < w; i += 2) {
			MoveTo((short)(x + i), (short)(y + ((i / 2) & 1)));
			LineTo((short)(x + i + (i + 1 < w)), (short)(y + ((i / 2) & 1)));
		}
		break;
	default:
		MoveTo((short)x, (short)y); LineTo((short)(x + w - 1), (short)y);
	}
	PenSize(1, 1);
}

static void draw_strike(long x, long w, long base, int size, int twice)
{
	long y = base - (size * 3 + 5) / 10;
	if (w <= 0)
		return;
	PenSize(1, 1);
	if (twice) {
		MoveTo((short)x, (short)(y - 1)); LineTo((short)(x + w - 1), (short)(y - 1));
		MoveTo((short)x, (short)(y + 1)); LineTo((short)(x + w - 1), (short)(y + 1));
	} else {
		MoveTo((short)x, (short)y); LineTo((short)(x + w - 1), (short)y);
	}
}

/* the colour a run is shown in: its own; on a one-bit screen black, or white when it is nearly white */
static unsigned long shown(unsigned long c, int mono)
{
	if (!mono)
		return c & 0xFFFFFF;
	return ((c >> 16 & 255) * 30 + (c >> 8 & 255) * 59 + (c & 255) * 11) / 100 > 200 ? 0xFFFFFF : 0;
}

/* which of a run's parts are drawn in colour c: 1 its glyphs and strike lines, 2 its underline. A
   colour past the sixteen kept goes with the last pass */
static int colour_in(const QRun *r, unsigned long c, int mono, int last, const unsigned long *cols, int ncol)
{
	int k, here = 0;
	unsigned long a = shown(r->color, mono), b = shown(r->ulcolor, mono);
	if (a == c)
		here |= 1;
	if (r->ul >= 0 && b == c)
		here |= 2;
	if (last) {
		for (k = 0; k < ncol && cols[k] != a; k++)
			;
		if (k == ncol)
			here |= 1;
		for (k = 0; k < ncol && cols[k] != b; k++)
			;
		if (k == ncol && r->ul >= 0)
			here |= 2;
	}
	return here;
}

/* see quill_mac.h */
unsigned char *qt_page_rgb(Layout *L, u32 page, int dpi, int mono, u32 *w, u32 *h, QtWords **words)
{
	QtWords *q = calloc(1, sizeof *q);
	Rec rc;
	unsigned char *rgb, *bits = NULL;
	BitMap bm;
	void *save;
	long i, rowb;
	unsigned long cols[16];
	int ncol = 0, ci;
	*words = NULL;
	if (!q)
		return pdf_page_rgb(L, page, dpi, w, h);
	memset(&rc, 0, sizeof rc);
	rc.w = q; rc.L = L; rc.dpi = dpi;
	rgb = pdf_page_rgb_text(L, page, dpi, w, h, take_run, &rc);
	if (!rgb || q->oom || !q->n) {
		if (rgb && q->oom) {
			/* the runs taken are missing from the picture: draw it again as before */
			free(rgb);
			rgb = pdf_page_rgb(L, page, dpi, w, h);
		}
		qt_words_free(q);
		return rgb;
	}
	rowb = ((long)*w + 15) / 16 * 2;
	if (!(bits = malloc((size_t)(rowb * *h)))) {
		free(rgb);
		qt_words_free(q);
		return pdf_page_rgb(L, page, dpi, w, h);
	}
	GetPort(&save);
	bm.base = bits; bm.rowbytes = (short)rowb;
	bm.bounds.left = 0; bm.bounds.top = 0; bm.bounds.right = (short)*w; bm.bounds.bottom = (short)*h;
	bits_port(&bm, (int)*w, (int)*h);
	page_squeezed = 0;
	/* the sizes, one per kind of text */
	for (i = 0; i < q->n; i++)
		if (q->r[i].kind == PT_TEXT)
			q->r[i].size = choose_size(q, &q->r[i]);
	/* the colours (sixteen at most: any more are drawn in the last), then each in turn: its runs and
	   lines drawn into the bitmap, its pixels put into the picture */
	for (i = 0; i < q->n; i++) {
		unsigned long two[2];
		int j, k;
		two[0] = shown(q->r[i].color, mono);
		two[1] = shown(q->r[i].ulcolor, mono);
		for (j = 0; j < (q->r[i].ul >= 0 ? 2 : 1); j++) {
			for (k = 0; k < ncol && cols[k] != two[j]; k++)
				;
			if (k == ncol && ncol < 16)
				cols[ncol++] = two[j];
		}
	}
	for (ci = 0; ci < ncol; ci++) {
		unsigned long c = cols[ci];
		long y0 = 0x7FFF, y1 = -1, x, y;
		int last = ci == ncol - 1;
		for (i = 0; i < q->n; i++) {
			QRun *r = &q->r[i];
			long top = r->base - r->size - 2, bot = r->base + r->size / 2 + 3;
			if (!colour_in(r, c, mono, last, cols, ncol))
				continue;
			if (top < y0) y0 = top;
			if (bot > y1) y1 = bot;
		}
		if (y0 < 0) y0 = 0;
		if (y1 >= (long)*h) y1 = (long)*h - 1;
		if (y1 < y0)
			continue;
		memset(bits + y0 * rowb, 0, (size_t)((y1 - y0 + 1) * rowb));
		for (i = 0; i < q->n; i++) {
			QRun *r = &q->r[i];
			int here = colour_in(r, c, mono, last, cols, ncol);
			if (r->kind == PT_TEXT && (here & 1)) {
				draw_run(q, r);
				if (r->strike)
					draw_strike(r->x, r->w, r->base, r->size, r->strike == 2);
			}
			if (here & 2)
				draw_ul(r->x, r->w, r->base, r->size, r->ul);
		}
		/* the bitmap's black pixels into the picture */
		for (y = y0; y <= y1; y++) {
			const unsigned char *bp = bits + y * rowb;
			unsigned char *p = rgb + (size_t)y * *w * 3;
			for (x = 0; x < rowb; x++) {
				int m = bp[x], bit;
				if (!m)
					continue;
				for (bit = 0; bit < 8; bit++)
					if (m & (0x80 >> bit)) {
						long px = x * 8 + bit;
						if (px < (long)*w) {
							p[px * 3] = (unsigned char)(c >> 16);
							p[px * 3 + 1] = (unsigned char)(c >> 8);
							p[px * 3 + 2] = (unsigned char)c;
						}
					}
			}
		}
	}
	SetPort(save);
	free(bits);
	/* the measure of whether words overlap or gap: how many runs were wider than the layout's room and had
	   their letters closed up (by how much at most), and how much narrower than it a run was at most */
	mm_log("text: page %lu at %d dpi: %ld runs in QuickDraw, %ld closed up (%ld so far of %ld; at most %ld px over), "
	       "at most %ld px short of the layout's width; %ld left to the core",
	       (unsigned long)page + 1, dpi, q->n, page_squeezed, squeezed_runs, drawn_runs, max_over, max_under, rc.declined);
	*words = q;
	return rgb;
}

void qt_words_free(QtWords *q)
{
	if (!q)
		return;
	free(q->r);
	free(q->ch);
	free(q->sh);
	free(q);
}

/* the run of text on the line whose baseline is in y..y+h-1 that x (page pixels) is inside: by the
   layout's places (drawn 0) or as drawn (drawn 1) */
static QRun *run_at(QtWords *q, long x, long y, long h, int drawn, long *ix)
{
	long i, k;
	for (i = 0; i < q->n; i++) {
		QRun *r = &q->r[i];
		const short *o;
		long best = -1, bd = 0x7FFFFFFL;
		if (r->kind != PT_TEXT || r->base < y || r->base >= y + h)
			continue;
		o = q->sh + (drawn ? r->mo : r->lo);
		if (x < r->x + o[0] || x > r->x + o[r->n])
			continue;
		for (k = 0; k <= r->n; k++) {
			long dd = r->x + o[k] - x;
			if (dd < 0)
				dd = -dd;
			if (dd < bd) { bd = dd; best = k; }
		}
		*ix = best;
		return r;
	}
	return NULL;
}

long qt_map_x(QtWords *q, long x, long y, long h)
{
	long k;
	QRun *r;
	long i;
	if (!q)
		return x;
	/* a run starting there wins: its start is the layout's own */
	for (i = 0; i < q->n; i++)
		if (q->r[i].kind == PT_TEXT && q->r[i].x == x && q->r[i].base >= y && q->r[i].base < y + h)
			return x;
	if (!(r = run_at(q, x, y, h, 0, &k)))
		return x;
	return r->x + q->sh[r->mo + k];
}

long qt_unmap_x(QtWords *q, long x, long y)
{
	long k, i;
	if (!q)
		return x;
	for (i = 0; i < q->n; i++) {
		QRun *r = &q->r[i];
		if (r->kind == PT_TEXT && y >= r->base - r->size && y < r->base + r->size / 2 + 1) {
			QRun *f = run_at(q, x, r->base, 1, 1, &k);
			return f ? f->x + q->sh[f->lo + k] : x;
		}
	}
	return x;
}

/* a new document: the sizes decided for the last one forgotten */
void qt_forget(void)
{
	nsizes_memo = 0;
}
