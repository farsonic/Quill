/*
 * mac/qm_view.c - Quill on the classic Mac OS: the page view, where a document is read and typed.
 * GPL-2.0-only, as Quill.
 *
 * The Amiga's page view (amiga/view.c) draws text itself with screen fonts made from Quill's
 * TrueType fonts. The Mac has neither those fonts nor graphics.library, but Quill's core can
 * draw a whole page exactly as its PDF export shows it (pdf_page_rgb: core/raster.c, integer
 * arithmetic only, so no FPU is needed). This view asks for each page it shows, at the zoom's
 * resolution, keeps the picture, and copies it to the window with CopyBits through an offscreen
 * 32-bit GWorld a band of rows at a time. CopyBits fits the colours to whatever the screen is,
 * 1-bit black and white to 32-bit millions; on 1-bit and indexed screens it dithers (ditherCopy).
 * Nothing is drawn a pixel at a time through the Toolbox.
 *
 * Everything the view shows goes through one routine, blit(): a rectangle of one page, its
 * pixels from the kept picture with the selection and the caret mixed in as they are converted.
 * So the caret blinks by copying a one-pixel column again, a selection that grows by a line is
 * redrawn by that line's rows, and an update event (a window uncovered) redraws what the Window
 * Manager says is invalid, no more. After an edit the page is drawn again in memory, compared
 * row by row with the picture it replaces, and only the rows that differ reach the screen.
 *
 * Geometry, in window pixels: pages stacked top to bottom with GAP between them, centred across
 * when narrower than the view; a vertical scroll bar on the right, and along the bottom the page
 * number (in the place Word 5 for the Mac gave it) beside the horizontal scroll bar, leaving the
 * size box its corner (Human Interface Guidelines, pp. 152-160). A page is drawn at dpi dots per
 * inch: 72 at 100%, the Mac screen's own resolution, so a 12-point letter is 12 pixels high.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "mui/mm.h"
#include "quill_mac.h"
#include "qm_mac.h"
#include "../core/pdf.h"

#define GAP      12             /* desk between and around pages, pixels */
#define BARW     16             /* a scroll bar's width, the Toolbox's standard */
#define STATUSW  96             /* the page number box left of the horizontal scroll bar */
#define BAND     32             /* rows converted into the GWorld per CopyBits */
#define NCACHE   3              /* pages kept drawn: the one being typed in and its neighbours */
#define LINE     16             /* a scroll arrow moves this many pixels */
#define DESK     MM_RGB(0x88, 0x88, 0x88)   /* the desk around the pages (a 50% grey on 1-bit) */

enum { inUpButton = 20, inDownButton = 21, inPageUp = 22, inPageDown = 23, inThumb = 129 };
#define SCROLLBARPROC 16
#define DITHERCOPY    64        /* CopyBits mode: srcCopy with dithering (32-bit QuickDraw) */
#define SRCCOPY       0
#define IBEAM_CURSOR  1         /* the System's cursors ('CURS' resources) */
#define LM_HILITERGB  ((const unsigned short *)0xDA0)   /* the highlight colour (Color control panel) */

/* a page as drawn, kept: RGB, 3 bytes a pixel, rows top down (pdf_page_rgb's) */
typedef struct {
	unsigned char *rgb;
	u32 page, w, h;
	int dpi;
	unsigned long ver;          /* Doc.ver it shows */
	unsigned long used;         /* when last used, for choosing which to drop */
} Drawn;

/* the selection in page pixels, for mixing it into a band: from (pa, xa, ya..ya+ha) to (pb, xb..)
   and the text column on each page between them */
typedef struct {
	int on;
	u32 pa, pb;
	long xa, ya, ha, xb, yb, hb;
} Sel;

struct qv_data {
	struct MUI_EventHandlerNode ehn;
	struct MUI_InputHandlerNode ihn;
	int zoom;                   /* per cent; 0: the page's width fits the view */
	int dpi;
	long sx, sy;                /* scrolled: the view's top left in document pixels */
	Drawn cache[NCACHE];
	unsigned long clock;
	void *gw, **gwpm;           /* the 32-bit GWorld a band is converted into */
	int gw_w;
	void *vbar, *hbar;          /* ControlHandles */
	int bar_scale;              /* scroll bar value = pixels / bar_scale (controls are 16-bit) */
	int caret_on, blink_off;    /* drawn now; hidden until the next blink */
	unsigned long blink_at;
	int dragging;
	unsigned long last_click;
	Pos last_click_pos;
	int stale;                  /* the document changed since it was last drawn */
	unsigned long changed_at;
	int follow;                 /* bring the caret into view once laid out */
	long want_x;                /* up and down arrows keep to this x (twips), -1 none */
	int shown;
	u32 status_page, status_of;  /* what the page box says now (0: not drawn yet) */
};

static struct MUI_CustomClass *qv_mcc;
static unsigned char ink[256];  /* the levels on a 1-bit screen (blit) */
static int ink_ready;
static Object *qv_obj;          /* the view (one window, one view) */

/* ---- geometry ---- */

/* twips as pixels at dpi, rounded as core/raster.c does */
static long px(long tw, int dpi)
{
	return (tw * dpi + 720) / 1440;
}

static long page_w(u32 k, int dpi) { return px(lay_page(qd.lay, k)->width, dpi); }
static long page_h(u32 k, int dpi) { return px(lay_page(qd.lay, k)->height, dpi); }

/* the view's parts in window coordinates (inclusive rectangles as Rects: right, bottom exclusive) */
static void parts(Object *obj, Rect *pages, Rect *vbar, Rect *hbar, Rect *status)
{
	short l = _mleft(obj), t = _mtop(obj), r = _mleft(obj) + _mwidth(obj), b = _mtop(obj) + _mheight(obj);
	if (pages) { pages->left = l; pages->top = t; pages->right = r - (BARW - 1); pages->bottom = b - (BARW - 1); }
	/* the bars overlap the window's edge by a pixel, so their frames and the window's are one line */
	if (vbar) { vbar->left = r - BARW + 1; vbar->top = t - 1; vbar->right = r + 1; vbar->bottom = b - (BARW - 2); }
	if (hbar) { hbar->left = l + STATUSW - 1; hbar->top = b - BARW + 1; hbar->right = r - (BARW - 2); hbar->bottom = b + 1; }
	if (status) { status->left = l; status->top = b - BARW + 2; status->right = l + STATUSW - 1; status->bottom = b; }
}

static int zoom_dpi(struct qv_data *d, Object *obj)
{
	int dpi;
	if (d->zoom > 0)
		dpi = 72 * d->zoom / 100;
	else {
		/* fit: the widest page with the desk either side */
		long w = 0;
		u32 k;
		Rect pr;
		parts(obj, &pr, NULL, NULL, NULL);
		for (k = 0; k < qd.lay->npages; k++)
			if (lay_page(qd.lay, k)->width > w)
				w = lay_page(qd.lay, k)->width;
		dpi = w ? (int)((long)(pr.right - pr.left - 2 * GAP) * 1440 / w) : 72;
	}
	return dpi < 18 ? 18 : dpi > 288 ? 288 : dpi;
}

/* a page's top left in document pixels; the document's size */
static void page_at(struct qv_data *d, Object *obj, u32 k, long *x, long *y)
{
	Rect pr;
	long top = GAP, wide = 0;
	u32 i;
	parts(obj, &pr, NULL, NULL, NULL);
	for (i = 0; i < qd.lay->npages; i++)
		if (page_w(i, d->dpi) > wide)
			wide = page_w(i, d->dpi);
	for (i = 0; i < k && i < qd.lay->npages; i++)
		top += page_h(i, d->dpi) + GAP;
	*y = top;
	/* centred in the document's width, which is at least the view's */
	wide += 2 * GAP;
	if (wide < pr.right - pr.left)
		wide = pr.right - pr.left;
	*x = (wide - page_w(k, d->dpi)) / 2;
}

static void doc_size(struct qv_data *d, Object *obj, long *w, long *h)
{
	long x, y, wide = 0;
	u32 i, n = qd.lay->npages;
	for (i = 0; i < n; i++)
		if (page_w(i, d->dpi) > wide)
			wide = page_w(i, d->dpi);
	*w = wide + 2 * GAP;
	page_at(d, obj, n ? n - 1 : 0, &x, &y);
	*h = n ? y + page_h(n - 1, d->dpi) + GAP : GAP;
}

/* where the view shows document pixel (x, y): window coordinates */
static long win_x(struct qv_data *d, Object *obj, long x) { return _mleft(obj) + x - d->sx; }
static long win_y(struct qv_data *d, Object *obj, long y) { return _mtop(obj) + y - d->sy; }

/* ---- pages drawn ---- */

static void cache_drop_all(struct qv_data *d)
{
	int i;
	for (i = 0; i < NCACHE; i++) {
		free(d->cache[i].rgb);
		d->cache[i].rgb = NULL;
	}
}

/* the kept picture of page k at the view's resolution, NULL if none */
static Drawn *cache_find(struct qv_data *d, u32 k)
{
	int i;
	for (i = 0; i < NCACHE; i++)
		if (d->cache[i].rgb && d->cache[i].page == k && d->cache[i].dpi == d->dpi) {
			d->cache[i].used = ++d->clock;
			return &d->cache[i];
		}
	return NULL;
}

/* a point inside the view's box */
static int inside(Object *obj, int x, int y)
{
	return x >= _mleft(obj) && x < _mleft(obj) + _mwidth(obj) && y >= _mtop(obj) && y < _mtop(obj) + _mheight(obj);
}

/* Page k drawn now if its picture is missing or older than the document; the rows that changed
   (for a picture that was already there) in *y0..*y1, all of them for a new one. NULL if out of
   memory. Drawing a page takes long on a 68030 (a second or so), so the watch shows meanwhile. */
static Drawn *page_drawn(struct qv_data *d, u32 k, long *y0, long *y1)
{
	Drawn *c = cache_find(d, k), *old = c;
	unsigned char *rgb;
	unsigned long t0;
	u32 w, h;
	int i;
	if (y0) { *y0 = 0; *y1 = -1; }
	if (c && c->ver == qd.doc->ver)
		return c;
	set_watch_cursor(1);
	t0 = qm_ticks();
	rgb = pdf_page_rgb(qd.lay, k, d->dpi, &w, &h);
	set_watch_cursor(0);
	/* the log says how long a page takes: the measure of whether a Mac is fast enough */
	mm_log("page %lu drawn at %d dpi in %lu ticks%s", (unsigned long)k + 1, d->dpi, qm_ticks() - t0, rgb ? "" : " (out of memory)");
	if (!rgb) {
		/* out of memory: give the others' room to this one and try once more */
		for (i = 0; i < NCACHE; i++)
			if (&d->cache[i] != c) {
				free(d->cache[i].rgb);
				d->cache[i].rgb = NULL;
			}
		rgb = pdf_page_rgb(qd.lay, k, d->dpi, &w, &h);
		if (!rgb)
			return NULL;
	}
	if (old && old->w == w && old->h == h) {
		/* the rows that differ, so only they are copied to the screen */
		long row = (long)w * 3, a = 0, b = (long)h - 1;
		while (a <= b && !memcmp(old->rgb + a * row, rgb + a * row, (size_t)row))
			a++;
		while (b >= a && !memcmp(old->rgb + b * row, rgb + b * row, (size_t)row))
			b--;
		if (y0) { *y0 = a; *y1 = b; }
	} else if (y0) {
		*y0 = 0;
		*y1 = (long)h - 1;
	}
	if (!c) {
		/* the least recently used place */
		c = &d->cache[0];
		for (i = 1; i < NCACHE; i++)
			if (!d->cache[i].rgb || (c->rgb && d->cache[i].used < c->used))
				c = &d->cache[i];
	}
	free(c->rgb);
	c->rgb = rgb;
	c->page = k;
	c->w = w;
	c->h = h;
	c->dpi = d->dpi;
	c->ver = qd.doc->ver;
	c->used = ++d->clock;
	return c;
}

/* ---- the selection and the caret ---- */

static int has_sel(void)
{
	return pos_cmp(qd.anchor, qd.caret) != 0;
}

static void caret_px(struct qv_data *d, Pos p, u32 *page, long *x, long *y, long *h)
{
	i32 tx, ty, th;
	lay_caret(qd.lay, p, page, &tx, &ty, &th);
	*x = px(tx, d->dpi);
	*y = px(ty, d->dpi);
	*h = px(th, d->dpi);
	if (*h < 1)
		*h = 1;
}

static void sel_now(struct qv_data *d, Sel *s)
{
	Pos a = qd.anchor, b = qd.caret;
	s->on = has_sel();
	if (!s->on)
		return;
	if (pos_cmp(a, b) > 0) {
		Pos t = a;
		a = b;
		b = t;
	}
	caret_px(d, a, &s->pa, &s->xa, &s->ya, &s->ha);
	caret_px(d, b, &s->pb, &s->xb, &s->yb, &s->hb);
}

/* the part of row y of page k the selection covers: [*x0, *x1), 0 if none. Lines are taken
   whole between the selection's ends, from the left margin to the right (a column's width). */
static int sel_span(struct qv_data *d, const Sel *s, u32 k, long y, long *x0, long *x1)
{
	const PageSetup *ps;
	long lm, rm;
	if (!s->on || k < s->pa || k > s->pb)
		return 0;
	ps = lay_page(qd.lay, k);
	lm = px(ps->left, d->dpi);
	rm = px(ps->width - ps->right, d->dpi);
	if (s->pa == s->pb && s->ya == s->yb) {            /* on one line */
		if (y < s->ya || y >= s->ya + s->ha)
			return 0;
		*x0 = s->xa;
		*x1 = s->xb;
		return *x1 > *x0;
	}
	if (k == s->pa && y < s->ya)
		return 0;
	if (k == s->pb && y >= s->yb + s->hb)
		return 0;
	if (k == s->pa && y < s->ya + s->ha) {             /* the first line: from the start on */
		*x0 = s->xa;
		*x1 = rm;
	} else if (k == s->pb && y >= s->yb) {             /* the last: up to the end */
		*x0 = lm;
		*x1 = s->xb;
	} else {
		*x0 = lm;
		*x1 = rm;
	}
	return *x1 > *x0;
}

/* ---- copying to the window ---- */

static int port_depth(void *port)
{
	/* a CGrafPort's portVersion (offset 6) has its two top bits set; its pixel map's pixelSize
	   (offset 32 of the PixMap) is the depth */
	if ((*(unsigned short *)((char *)port + 6) & 0xC000) == 0xC000) {
		char **pm = *(char ***)((char *)port + 2);
		return *(short *)(*pm + 32);
	}
	return 1;
}

static int gw_ready(struct qv_data *d, int w)
{
	Rect b;
	if (d->gw && d->gw_w >= w)
		return 1;
	if (d->gw)
		qm_disposegworld(d->gw);
	d->gw = NULL;
	/* made once, wide enough for a screen's width (a band of 1280 pixels is 160 KB), so that it
	   is not made again for every page width and the heap is not cut up by it */
	if (w < 1280)
		w = 1280;
	b.left = 0; b.top = 0; b.right = (short)w; b.bottom = BAND;
	if (qm_newgworld(&d->gw, 32, &b) != 0 || !d->gw) {
		mm_log("no offscreen GWorld %d pixels wide (32-bit QuickDraw missing, or memory)", w);
		d->gw = NULL;
		return 0;
	}
	d->gwpm = qm_gworldpixmap(d->gw);
	d->gw_w = w;
	return 1;
}

/* Rows y0..y1 (inclusive) and columns x0..x1 of page k, as drawn now, to the window: converted
   into the GWorld a band at a time (the selection and the caret mixed in) and copied. The port
   is the window's, its clip the page area. */
static void blit(struct qv_data *d, Object *obj, u32 k, long x0, long x1, long y0, long y1)
{
	Drawn *c = cache_find(d, k);
	void *port = mm_win_of(obj)->macwin;
	int depth = port_depth(port), invert;
	unsigned short hr, hg, hb;
	long px0, py0, y;
	Sel s;
	u32 ck = 0;
	long cx = 0, cy = 0, ch = 0;
	int caret = 0;
	if (!c)
		return;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 >= (long)c->w) x1 = (long)c->w - 1;
	if (y1 >= (long)c->h) y1 = (long)c->h - 1;
	if (x0 > x1 || y0 > y1 || !gw_ready(d, (int)(x1 - x0 + 1)))
		return;
	page_at(d, obj, k, &px0, &py0);
	sel_now(d, &s);
	if (!s.on && d->caret_on) {
		caret_px(d, qd.caret, &ck, &cx, &cy, &ch);
		caret = ck == k;
	}
	/* the selection: in the highlight colour (multiplied in, so black text stays black), or
	   inverted where the highlight is black or white or the screen has one bit */
	hr = LM_HILITERGB[0] >> 8; hg = LM_HILITERGB[1] >> 8; hb = LM_HILITERGB[2] >> 8;
	invert = depth == 1 || (hr + hg + hb < 0x60) || (hr > 0xF0 && hg > 0xF0 && hb > 0xF0);
	/* One bit per pixel: the greys of smoothed text edges, dithered as they are, make letters
	   speckled and thin. Each level is stretched first (up to 72 black, from 200 white, the
	   rest spread between), so text comes out solid and pictures keep their dithered shades. */
	if (depth == 1 && !ink_ready) {
		int v;
		for (v = 0; v < 256; v++)
			ink[v] = (unsigned char)(v <= 72 ? 0 : v >= 200 ? 255 : (v - 72) * 255 / (200 - 72));
		ink_ready = 1;
	}
	ForeColor(33);              /* blackColor, whiteColor: CopyBits colours by them */
	BackColor(30);
	for (y = y0; y <= y1; y += BAND) {
		long n = y1 - y + 1 > BAND ? BAND : y1 - y + 1, r, i;
		unsigned char *base;
		long rowb;
		Rect sr, dr;
		if (!qm_lockpixels(d->gwpm)) {
			mm_log("the GWorld's pixels could not be locked");
			return;
		}
		base = qm_pixbaseaddr(d->gwpm);
		rowb = *(unsigned short *)((char *)*d->gwpm + 4) & 0x3FFF;
		for (r = 0; r < n; r++) {
			const unsigned char *sp = c->rgb + ((y + r) * (long)c->w + x0) * 3;
			unsigned long *dp = (unsigned long *)(base + r * rowb);
			long sx0, sx1;
			int sel = sel_span(d, &s, k, y + r, &sx0, &sx1);
			for (i = x0; i <= x1; i++, sp += 3) {
				unsigned long R = sp[0], G = sp[1], B = sp[2];
				if (depth == 1) {
					R = ink[R]; G = ink[G]; B = ink[B];
				}
				if (sel && i >= sx0 && i < sx1) {
					if (invert) {
						R ^= 0xFF; G ^= 0xFF; B ^= 0xFF;
					} else {
						R = R * hr / 255; G = G * hg / 255; B = B * hb / 255;
					}
				}
				*dp++ = (R << 16) | (G << 8) | B;
			}
			if (caret && cx >= x0 && cx <= x1 && y + r >= cy && y + r < cy + ch)
				((unsigned long *)(base + r * rowb))[cx - x0] = 0;
		}
		sr.left = 0; sr.top = 0; sr.right = (short)(x1 - x0 + 1); sr.bottom = (short)n;
		dr.left = (short)win_x(d, obj, px0 + x0); dr.top = (short)win_y(d, obj, py0 + y);
		dr.right = (short)(dr.left + sr.right); dr.bottom = (short)(dr.top + n);
		CopyBits(*d->gwpm, (char *)port + 2, &sr, &dr, depth <= 8 ? DITHERCOPY : SRCCOPY, NULL);
		qm_unlockpixels(d->gwpm);
	}
}

/* the clip: the page area only (the scroll bars and the page box are drawn apart); restored with
   clip_end */
static void *clip_pages(Object *obj)
{
	void *save = NewRgn();
	Rect pr;
	SetPort(mm_win_of(obj)->macwin);
	GetClip(save);
	parts(obj, &pr, NULL, NULL, NULL);
	ClipRect(&pr);
	return save;
}

static void clip_end(void *save)
{
	SetClip(save);
	DisposeRgn(save);
}

/* the desk and the pages inside window rectangle area (window coordinates): pages drawn first
   where their pictures are missing or old */
static void draw_area(struct qv_data *d, Object *obj, const Rect *area)
{
	void *port = mm_win_of(obj)->macwin;
	Rect pr, r;
	u32 k;
	parts(obj, &pr, NULL, NULL, NULL);
	r = *area;
	if (r.left < pr.left) r.left = pr.left;
	if (r.top < pr.top) r.top = pr.top;
	if (r.right > pr.right) r.right = pr.right;
	if (r.bottom > pr.bottom) r.bottom = pr.bottom;
	if (r.left >= r.right || r.top >= r.bottom)
		return;
	for (k = 0; k < qd.lay->npages; k++) {
		long x, y, w = page_w(k, d->dpi), h = page_h(k, d->dpi), wx, wy;
		Rect pg, part;
		page_at(d, obj, k, &x, &y);
		wx = win_x(d, obj, x);
		wy = win_y(d, obj, y);
		if (wy > r.bottom)
			break;
		if (wy + h < r.top || wx > r.right || wx + w < r.left)
			continue;
		pg.left = (short)wx; pg.top = (short)wy; pg.right = (short)(wx + w); pg.bottom = (short)(wy + h);
		part.left = pg.left > r.left ? pg.left : r.left;
		part.top = pg.top > r.top ? pg.top : r.top;
		part.right = pg.right < r.right ? pg.right : r.right;
		part.bottom = pg.bottom < r.bottom ? pg.bottom : r.bottom;
		if (part.left < part.right && part.top < part.bottom) {
			if (!page_drawn(d, k, NULL, NULL))
				mm_fill(port, &part, MM_RGB(0xFF, 0xFF, 0xFF));
			blit(d, obj, k, part.left - wx, part.right - 1 - wx, part.top - wy, part.bottom - 1 - wy);
		}
	}
	/* the desk: what is left of r around the pages, a band at a time between their tops */
	{
		Rect band;
		long top = r.top;
		for (k = 0; k <= qd.lay->npages && top < r.bottom; k++) {
			long x, y, wy, bottom;
			if (k < qd.lay->npages) {
				page_at(d, obj, k, &x, &y);
				wy = win_y(d, obj, y);
				bottom = wy;
			} else {
				x = 0;
				wy = r.bottom;
				bottom = r.bottom;
			}
			if (bottom > top) {
				band.left = r.left; band.right = r.right;
				band.top = (short)top; band.bottom = (short)(bottom < r.bottom ? bottom : r.bottom);
				mm_fill(port, &band, DESK);
			}
			if (k < qd.lay->npages) {
				long wx = win_x(d, obj, x), w = page_w(k, d->dpi), pb = wy + page_h(k, d->dpi);
				long t = wy > r.top ? wy : r.top, b = pb < r.bottom ? pb : r.bottom;
				if (b > t) {
					band.top = (short)t; band.bottom = (short)b;
					band.left = r.left; band.right = (short)(wx < r.right ? wx : r.right);
					if (band.right > band.left)
						mm_fill(port, &band, DESK);
					band.left = (short)(wx + w > r.left ? wx + w : r.left); band.right = r.right;
					if (band.right > band.left)
						mm_fill(port, &band, DESK);
				}
				top = pb > top ? pb : top;
			}
		}
	}
}

/* the page box: "Page 2 of 5" (Guidelines p. 160: status beside the horizontal scroll bar). As in
   Word for the Mac, the page is the one at the top of the window, so it follows the scroll bar. */
static u32 page_in_view(struct qv_data *d, Object *obj)
{
	u32 k;
	long x, y;
	for (k = 0; k + 1 < qd.lay->npages; k++) {
		page_at(d, obj, k, &x, &y);
		if (y + page_h(k, d->dpi) + GAP / 2 > d->sy)
			break;
	}
	return k + 1;
}

static void draw_status(struct qv_data *d, Object *obj)
{
	void *port = mm_win_of(obj)->macwin;
	Rect st;
	char t[64];
	u32 n = qd.lay->npages ? qd.lay->npages : 1;
	u32 k = page_in_view(d, obj);
	parts(obj, NULL, NULL, NULL, &st);
	mm_fill(port, &st, MM_RGB(0xFF, 0xFF, 0xFF));
	mm_line(port, st.left, st.top - 1, st.right, st.top - 1, 0);            /* the line above it */
	mm_line(port, st.right, st.top - 1, st.right, st.bottom, 0);            /* and the bar's left edge */
	snprintf(t, sizeof t, tr("Page %lu of %lu%s"), (unsigned long)k, (unsigned long)n, "");
	mm_text(port, st.left + 6, st.bottom - 3, t, (int)strlen(t), 0, 0);
	d->status_page = k;
	d->status_of = n;
}

/* after a scroll or a new layout: the page box again if what it says has changed. Called while an
   update is being drawn, drawing outside the updated area would be clipped away; the box is then
   marked for the next update instead. */
static void status_check(struct qv_data *d, Object *obj, int in_update)
{
	Rect st;
	u32 n = qd.lay->npages ? qd.lay->npages : 1;
	if (page_in_view(d, obj) == d->status_page && n == d->status_of)
		return;
	if (!in_update) {
		draw_status(d, obj);
		return;
	}
	parts(obj, NULL, NULL, NULL, &st);
	SetPort(mm_win_of(obj)->macwin);       /* (InvalRect works on the current port) */
	InvalRect(&st);
}

/* ---- scroll bars ---- */

static void bars_set(struct qv_data *d, Object *obj)
{
	long w, h;
	Rect pr;
	int vmax, hmax;
	if (!d->vbar)
		return;
	parts(obj, &pr, NULL, NULL, NULL);
	doc_size(d, obj, &w, &h);
	d->bar_scale = h / 30000 + 1;
	if (d->sy > h - (pr.bottom - pr.top)) d->sy = h - (pr.bottom - pr.top);
	if (d->sy < 0) d->sy = 0;
	if (d->sx > w - (pr.right - pr.left)) d->sx = w - (pr.right - pr.left);
	if (d->sx < 0) d->sx = 0;
	vmax = (int)((h - (pr.bottom - pr.top)) / d->bar_scale);
	hmax = (int)((w - (pr.right - pr.left)) / d->bar_scale);
	SetCtlMax(d->vbar, vmax > 0 ? vmax : 0);
	SetCtlValue(d->vbar, (int)(d->sy / d->bar_scale));
	SetCtlMax(d->hbar, hmax > 0 ? hmax : 0);
	SetCtlValue(d->hbar, (int)(d->sx / d->bar_scale));
}

/* a control where r says, if it is not there already (a ControlRecord's contrlRect is at 8) */
static void place_bar(void *ctl, const Rect *r)
{
	const Rect *now = (const Rect *)(*(char **)ctl + 8);
	if (now->left != r->left || now->top != r->top)
		MoveControl(ctl, r->left, r->top);
	if (now->right - now->left != r->right - r->left || now->bottom - now->top != r->bottom - r->top)
		SizeControl(ctl, r->right - r->left, r->bottom - r->top);
}

static void bars_place(struct qv_data *d, Object *obj)
{
	void *win = mm_win_of(obj)->macwin;
	Rect vr, hr;
	unsigned char e[1] = { 0 };
	parts(obj, NULL, &vr, &hr, NULL);
	if (!d->vbar) {
		d->vbar = NewControl(win, &vr, e, 1, 0, 0, 0, SCROLLBARPROC, 1);
		d->hbar = NewControl(win, &hr, e, 1, 0, 0, 0, SCROLLBARPROC, 2);
	} else {
		/* only when the window's size changed: moving a control hides it first, which adds its
		   place to the update region, and drawing again from that update would never end */
		place_bar(d->vbar, &vr);
		place_bar(d->hbar, &hr);
	}
	bars_set(d, obj);
}

/* scrolled to (nx, ny): the pixels still shown moved with ScrollRect, the rest drawn */
static void scroll_to(struct qv_data *d, Object *obj, long nx, long ny)
{
	long w, h, dx, dy;
	Rect pr;
	void *save, *upd;
	parts(obj, &pr, NULL, NULL, NULL);
	doc_size(d, obj, &w, &h);
	if (ny > h - (pr.bottom - pr.top)) ny = h - (pr.bottom - pr.top);
	if (ny < 0) ny = 0;
	if (nx > w - (pr.right - pr.left)) nx = w - (pr.right - pr.left);
	if (nx < 0) nx = 0;
	dx = d->sx - nx;
	dy = d->sy - ny;
	if (!dx && !dy)
		return;
	d->sx = nx;
	d->sy = ny;
	save = clip_pages(obj);
	if (dx > -(pr.right - pr.left) && dx < pr.right - pr.left && dy > -(pr.bottom - pr.top) && dy < pr.bottom - pr.top) {
		upd = NewRgn();
		ScrollRect(&pr, (int)dx, (int)dy, upd);
		draw_area(d, obj, RGN_BBOX((void **)upd));
		DisposeRgn(upd);
	} else
		draw_area(d, obj, &pr);
	clip_end(save);
	SetCtlValue(d->vbar, (int)(d->sy / d->bar_scale));
	SetCtlValue(d->hbar, (int)(d->sx / d->bar_scale));
	status_check(d, obj, 0);
}

/* TrackControl's action (mm_ctl_action): the arrows and the grey parts scroll while held */
static void bar_action(void *ctl, int part)
{
	struct qv_data *d = INST_DATA(qv_mcc->mcc_Class, qv_obj);
	Rect pr;
	long step;
	int vert = ctl == d->vbar;
	parts(qv_obj, &pr, NULL, NULL, NULL);
	switch (part) {
	case inUpButton: step = -LINE; break;
	case inDownButton: step = LINE; break;
	case inPageUp: step = -((vert ? pr.bottom - pr.top : pr.right - pr.left) - LINE); break;
	case inPageDown: step = (vert ? pr.bottom - pr.top : pr.right - pr.left) - LINE; break;
	default: return;
	}
	if (vert)
		scroll_to(d, qv_obj, d->sx, d->sy + step);
	else
		scroll_to(d, qv_obj, d->sx + step, d->sy);
}

/* the caret into view: scrolled the least that shows its line with a little room */
static void follow_caret(struct qv_data *d, Object *obj)
{
	u32 k;
	long x, y, h, ppx, ppy, nx = d->sx, ny = d->sy;
	Rect pr;
	parts(obj, &pr, NULL, NULL, NULL);
	caret_px(d, qd.caret, &k, &x, &y, &h);
	page_at(d, obj, k, &ppx, &ppy);
	x += ppx;
	y += ppy;
	if (y < d->sy + LINE)
		ny = y - LINE * 2;
	else if (y + h > d->sy + (pr.bottom - pr.top) - LINE)
		ny = y + h - (pr.bottom - pr.top) + LINE * 2;
	if (x < d->sx + LINE)
		nx = x - LINE * 4;
	else if (x > d->sx + (pr.right - pr.left) - LINE)
		nx = x - (pr.right - pr.left) + LINE * 4;
	scroll_to(d, obj, nx, ny);
}

/* ---- redrawing after a change ---- */

/* rows y0..y1 of page k again, in the window (clip: the pages) */
static void redraw_rows(struct qv_data *d, Object *obj, u32 k, long y0, long y1)
{
	long x, y;
	Rect r, pr;
	if (y1 < y0)
		return;
	page_at(d, obj, k, &x, &y);
	parts(obj, &pr, NULL, NULL, NULL);
	r.left = pr.left; r.right = pr.right;
	r.top = (short)(win_y(d, obj, y + y0) < pr.top ? pr.top : win_y(d, obj, y + y0));
	r.bottom = (short)(win_y(d, obj, y + y1 + 1) > pr.bottom ? pr.bottom : win_y(d, obj, y + y1 + 1));
	if (r.top < r.bottom)
		blit(d, obj, k, pr.left - win_x(d, obj, x), pr.right - 1 - win_x(d, obj, x), r.top - win_y(d, obj, y), r.bottom - 1 - win_y(d, obj, y));
}

/* the rows between two positions' lines (both lines too), on every page they cross */
static void redraw_between(struct qv_data *d, Object *obj, Pos a, Pos b)
{
	u32 ka, kb, k;
	long xa, ya, ha, xb, yb, hb;
	caret_px(d, a, &ka, &xa, &ya, &ha);
	caret_px(d, b, &kb, &xb, &yb, &hb);
	if (ka > kb || (ka == kb && ya > yb)) {
		u32 t = ka; long ty = ya, th = ha;
		ka = kb; ya = yb; ha = hb;
		kb = t; yb = ty; hb = th;
	}
	for (k = ka; k <= kb; k++)
		redraw_rows(d, obj, k, k == ka ? ya : 0, k == kb ? yb + hb - 1 : page_h(k, d->dpi) - 1);
}

/* The document changed (or only the selection): laid out, the visible pages drawn again, the
   rows that changed copied. Typing calls this once the keys stop coming (the timer), so a burst
   of typing costs one drawing of the page, not one per key. */
static void refresh(struct qv_data *d, Object *obj)
{
	Rect pr;
	void *save;
	u32 k;
	int dpi;
	if (!d->shown || !mm_win_of(obj) || !mm_win_of(obj)->macwin)
		return;
	lay_update(qd.lay);
	dpi = zoom_dpi(d, obj);
	if (dpi != d->dpi) {
		d->dpi = dpi;
		cache_drop_all(d);
	}
	bars_set(d, obj);
	if (d->follow)
		follow_caret(d, obj);
	d->follow = 0;
	parts(obj, &pr, NULL, NULL, NULL);
	save = clip_pages(obj);
	for (k = 0; k < qd.lay->npages; k++) {
		long x, y, y0, y1;
		page_at(d, obj, k, &x, &y);
		if (win_y(d, obj, y) > pr.bottom)
			break;
		if (win_y(d, obj, y + page_h(k, d->dpi)) < pr.top)
			continue;
		{
			Drawn *had = cache_find(d, k);
			int same = had && had->ver == qd.doc->ver;
			if (!same && page_drawn(d, k, &y0, &y1))
				redraw_rows(d, obj, k, y0, y1);
		}
	}
	/* pages that went (the document got shorter) leave desk behind */
	{
		long w, h;
		doc_size(d, obj, &w, &h);
		if (win_y(d, obj, h) < pr.bottom) {
			Rect r = pr;
			r.top = (short)(win_y(d, obj, h) > pr.top ? win_y(d, obj, h) : pr.top);
			mm_fill(mm_win_of(obj)->macwin, &r, DESK);
		}
	}
	clip_end(save);
	status_check(d, obj, 0);
	d->stale = 0;
}

/* the caret shown or hidden: its column copied again */
static void caret_show(struct qv_data *d, Object *obj, int on)
{
	u32 k;
	long x, y, h, ppx, ppy;
	void *save;
	if (d->caret_on == on || has_sel() || !d->shown || d->stale)
		return;
	d->caret_on = on;
	caret_px(d, qd.caret, &k, &x, &y, &h);
	page_at(d, obj, k, &ppx, &ppy);
	if (!cache_find(d, k))
		return;
	save = clip_pages(obj);
	blit(d, obj, k, x, x, y, y + h - 1);
	clip_end(save);
}

/* ---- editing ---- */

static void sel_order(Pos *a, Pos *b)
{
	*a = qd.anchor;
	*b = qd.caret;
	if (pos_cmp(*a, *b) > 0) {
		Pos t = *a;
		*a = *b;
		*b = t;
	}
}

/* the selection's text gone (typing over it), the caret where it began */
static void delete_sel(void)
{
	Pos a, b;
	if (!has_sel())
		return;
	sel_order(&a, &b);
	doc_delete(qd.doc, a, b);
	qd.caret = qd.anchor = a;
}

/* the caret moved to p (shift: the selection extended to it); old: where it was, so the rows
   between are redrawn */
static void move_to(struct qv_data *d, Object *obj, Pos p, int extend)
{
	Pos oa = qd.anchor, oc = qd.caret;
	void *save;
	caret_show(d, obj, 0);
	qd.caret = p;
	if (!extend)
		qd.anchor = p;
	qm_typing_reset();
	save = clip_pages(obj);
	if (pos_cmp(oa, oc) != 0 || has_sel()) {
		/* what was selected and what is: the rows from the first of the four to the last */
		Pos lo = oa, hi = oa, all[3];
		int i;
		all[0] = oc; all[1] = qd.anchor; all[2] = qd.caret;
		for (i = 0; i < 3; i++) {
			if (pos_cmp(all[i], lo) < 0) lo = all[i];
			if (pos_cmp(all[i], hi) > 0) hi = all[i];
		}
		if (extend && pos_cmp(oa, qd.anchor) == 0)
			redraw_between(d, obj, oc, qd.caret);    /* only the end that moved */
		else
			redraw_between(d, obj, lo, hi);
	}
	clip_end(save);
	d->blink_at = qm_ticks() + LM_CARETTIME;
	follow_caret(d, obj);
	caret_show(d, obj, 1);
	status_check(d, obj, 0);
	qm_status();
}

void qv_changed(Object *view, int scroll_to_caret)
{
	struct qv_data *d = INST_DATA(qv_mcc->mcc_Class, view);
	d->caret_on = 0;
	d->stale = 1;
	d->changed_at = qm_ticks();
	if (scroll_to_caret)
		d->follow = 1;
	qm_status();
}

/* text typed: Mac Roman c as UTF-8, over the selection */
static void type_char(struct qv_data *d, Object *obj, unsigned char c)
{
	char u[4];
	int n = macroman_to_utf8(c, u);
	delete_sel();
	doc_insert(qd.doc, &qd.caret, u, (u32)n, qm_typing_fmt());
	qd.anchor = qd.caret;
	qv_changed(obj, 1);
}

static void key(struct qv_data *d, Object *obj, int c, UWORD qual)
{
	int shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
	int opt = (qual & IEQUALIFIER_LALT) != 0;
	Pos p = qd.caret;
	Rect pr;
	parts(obj, &pr, NULL, NULL, NULL);
	if (d->stale) {
		/* moving needs the lines where they are now */
		lay_update(qd.lay);
	}
	if (c != 0x1E && c != 0x1F)
		d->want_x = -1;
	switch (c) {
	case 0x1C:                              /* left arrow; Option: a word */
		if (has_sel() && !shift) {
			Pos a, b;
			sel_order(&a, &b);
			p = a;
		} else
			p = opt ? pos_word_left(qd.doc, p) : pos_left(qd.doc, p);
		break;
	case 0x1D:                              /* right */
		if (has_sel() && !shift) {
			Pos a, b;
			sel_order(&a, &b);
			p = b;
		} else
			p = opt ? pos_word_right(qd.doc, p) : pos_right(qd.doc, p);
		break;
	case 0x1E:                              /* up and down: on the same x; Option: the line's ends */
	case 0x1F: {
		u32 k;
		i32 x, y, h;
		if (opt) {
			p = c == 0x1E ? lay_line_start(qd.lay, p) : lay_line_end(qd.lay, p);
			break;
		}
		lay_caret(qd.lay, p, &k, &x, &y, &h);
		if (d->want_x < 0)
			d->want_x = x;
		p = lay_updown(qd.lay, p, (i32)d->want_x, c == 0x1E ? -1 : 1);
		break;
	}
	case 1:                                 /* Home and End (extended keyboard): the document's ends */
		p.para = 0;
		p.off = 0;
		break;
	case 4:
		p.para = qd.doc->np ? qd.doc->np - 1 : 0;
		p.off = qd.doc->np ? qd.doc->p[p.para]->len : 0;
		break;
	case 11:                                /* Page Up and Page Down: the view, not the caret (p. 159) */
	case 12:
		scroll_to(d, obj, d->sx, d->sy + (c == 11 ? -1 : 1) * ((pr.bottom - pr.top) - LINE));
		return;
	case 8:                                 /* Delete: the selection, or the character before */
	case 0x7F:                              /* forward delete (extended keyboard) */
		if (!has_sel()) {
			Pos q = c == 8 ? (opt ? pos_word_left(qd.doc, p) : pos_left(qd.doc, p))
			               : (opt ? pos_word_right(qd.doc, p) : pos_right(qd.doc, p));
			if (pos_cmp(q, p) == 0)
				return;
			qd.anchor = q;
		}
		caret_show(d, obj, 0);
		delete_sel();
		qv_changed(obj, 1);
		return;
	case 13:                                /* Return: a new paragraph; Shift-Return a line break */
	case 3:                                 /* Enter */
		caret_show(d, obj, 0);
		delete_sel();
		doc_insert(qd.doc, &qd.caret, shift ? "\v" : "\n", 1, qm_typing_fmt());
		qd.anchor = qd.caret;
		qv_changed(obj, 1);
		return;
	case 9:
		caret_show(d, obj, 0);
		type_char(d, obj, '\t');
		return;
	case 27:
	case 16:                                /* Escape, function keys: nothing */
		return;
	default:
		if (c >= 0x20) {
			caret_show(d, obj, 0);
			type_char(d, obj, (unsigned char)c);
		}
		return;
	}
	move_to(d, obj, p, shift);
}

/* a click on the pages: the position under it, or -1 for the desk */
static int hit(struct qv_data *d, Object *obj, int mx, int my, Pos *p)
{
	u32 k;
	for (k = 0; k < qd.lay->npages; k++) {
		long x, y;
		page_at(d, obj, k, &x, &y);
		x = win_x(d, obj, x);
		y = win_y(d, obj, y);
		if (my >= y && my < y + page_h(k, d->dpi) + GAP) {
			*p = lay_hit(qd.lay, k, (i32)((mx - x) * 1440 / d->dpi), (i32)((my - y) * 1440 / d->dpi));
			return 1;
		}
	}
	return 0;
}

/* a word's start and end around p: letters, digits and anything not ASCII (UTF-8 bytes) */
static void word_at(Pos p, Pos *a, Pos *b)
{
	const Para *pa = qd.doc->p[p.para];
	u32 s = p.off, e = p.off;
#define WORDCH(c) ((unsigned char)(c) >= 0x80 || ((c) >= '0' && (c) <= '9') || ((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z') || (c) == '\'')
	while (s > 0 && WORDCH(pa->t[s - 1]))
		s--;
	while (e < pa->len && WORDCH(pa->t[e]))
		e++;
#undef WORDCH
	a->para = b->para = p.para;
	a->off = s;
	b->off = e;
}

static void click(struct qv_data *d, Object *obj, struct IntuiMessage *im)
{
	void *win = mm_win_of(obj)->macwin, *ctl = NULL;
	Point pt;
	long lp;
	int part;
	Pos p;
	pt.h = im->MouseX;
	pt.v = im->MouseY;
	lp = ((long)pt.v << 16) | (unsigned short)pt.h;
	part = FindControl(lp, win, &ctl);
	if (ctl && (ctl == d->vbar || ctl == d->hbar)) {
		if (part == inThumb) {
			if (TrackControl(ctl, lp, NULL)) {
				long v = (long)GetCtlValue(ctl) * d->bar_scale;
				if (ctl == d->vbar)
					scroll_to(d, obj, d->sx, v);
				else
					scroll_to(d, obj, v, d->sy);
			}
		} else if (part) {
			mm_ctl_action = bar_action;
			TrackControl(ctl, lp, (void *)pas_ctl_action);
			mm_ctl_action = NULL;
		}
		return;
	}
	if (d->stale)
		refresh(d, obj);
	if (!hit(d, obj, im->MouseX, im->MouseY, &p))
		return;
	/* a second click on the same place soon after: the word (Guidelines p. 236) */
	if (qm_ticks() - d->last_click < LM_DOUBLETIME && pos_cmp(p, d->last_click_pos) == 0) {
		Pos a, b;
		word_at(p, &a, &b);
		qd.anchor = a;
		move_to(d, obj, b, 1);
		d->last_click = 0;
		return;
	}
	d->last_click = qm_ticks();
	d->last_click_pos = p;
	move_to(d, obj, p, (im->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0);
	d->dragging = 1;
}

static void drag(struct qv_data *d, Object *obj, int mx, int my)
{
	Pos p;
	Rect pr;
	parts(obj, &pr, NULL, NULL, NULL);
	/* past the top or bottom: scrolled that way while the button is held */
	if (my < pr.top)
		scroll_to(d, obj, d->sx, d->sy - LINE);
	else if (my >= pr.bottom)
		scroll_to(d, obj, d->sx, d->sy + LINE);
	if (hit(d, obj, mx, my, &p) && pos_cmp(p, qd.caret) != 0)
		move_to(d, obj, p, 1);
}

/* ---- the class ---- */

/* the timer, ten times a second: draws what typing changed once the keys pause, blinks the caret */
static void tick(struct qv_data *d, Object *obj)
{
	EventRecord ev;
	unsigned long now = qm_ticks();
	if (!d->shown)
		return;
	if (d->dragging) {
		Point m;
		if (!StillDown()) {
			d->dragging = 0;
		} else {
			SetPort(mm_win_of(obj)->macwin);
			GetMouse(&m);
			drag(d, obj, m.h, m.v);
		}
	}
	if (d->stale) {
		/* keys still waiting: they are taken first, unless the page has not shown for a second */
		if (EventAvail(0x0028 /* keyDownMask | autoKeyMask */, &ev) && now - d->changed_at < 60)
			return;
		refresh(d, obj);
		d->blink_at = now + LM_CARETTIME;
		caret_show(d, obj, 1);
		return;
	}
	if ((long)(now - d->blink_at) >= 0) {
		caret_show(d, obj, !d->caret_on);
		d->blink_at = now + LM_CARETTIME;
	}
}

/* the pointer: the I-beam over the pages (Guidelines p. 286), the arrow elsewhere */
static void pointer(Object *obj, int x, int y)
{
	static int ibeam = -1;
	Rect pr;
	int want;
	parts(obj, &pr, NULL, NULL, NULL);
	want = x >= pr.left && x < pr.right && y >= pr.top && y < pr.bottom;
	if (want != ibeam) {
		if (want)
			SetCursor(*(void **)GetCursor(IBEAM_CURSOR));
		else
			InitCursor();
		ibeam = want;
	}
}

DISPATCHER(qv_dispatch)
{
	struct qv_data *d;
	switch (msg->MethodID) {
	case OM_NEW:
		if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg)))
			return 0;
		d = INST_DATA(cl, obj);
		memset(d, 0, sizeof *d);
		d->zoom = 100;
		d->dpi = 72;
		d->want_x = -1;
		qv_obj = obj;
		return (ULONG)obj;
	case OM_DISPOSE:
		d = INST_DATA(cl, obj);
		cache_drop_all(d);
		if (d->gw)
			qm_disposegworld(d->gw);
		break;
	case MUIM_AskMinMax: {
		struct MUI_MinMax *mm;
		DoSuperMethodA(cl, obj, msg);
		mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
		/* room for the page number box, the bars, a little of a page; by default a US Letter
		   page's width at 100% and the screen's height (the window is clipped to the screen) */
		mm->MinWidth += STATUSW + 3 * BARW;
		mm->MinHeight += 6 * BARW;
		mm->DefWidth += 612 + 2 * GAP + BARW;
		mm->DefHeight += 2000;
		mm->MaxWidth += MUI_MAXMAX;
		mm->MaxHeight += MUI_MAXMAX;
		return 0;
	}
	case MUIM_Setup:
		if (!DoSuperMethodA(cl, obj, msg))
			return FALSE;
		d = INST_DATA(cl, obj);
		d->ehn.ehn_Priority = 0;
		d->ehn.ehn_Flags = 0;
		d->ehn.ehn_Object = obj;
		d->ehn.ehn_Class = cl;
		d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_VANILLAKEY;
		mui_do(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
		d->ihn.ihn_Object = obj;
		d->ihn.ihn_Flags = MUIIHNF_TIMER;
		d->ihn.ihn_Method = MUIM_QV_Tick;
		d->ihn.ihn_millis = 100;
		mui_do(_app(obj), MUIM_Application_AddInputHandler, (ULONG)&d->ihn);
		return TRUE;
	case MUIM_Cleanup:
		d = INST_DATA(cl, obj);
		mui_do(_app(obj), MUIM_Application_RemInputHandler, (ULONG)&d->ihn);
		mui_do(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
		/* the window and its controls go with it */
		d->vbar = d->hbar = NULL;
		break;
	case MUIM_Show:
		d = INST_DATA(cl, obj);
		d->shown = 1;
		break;
	case MUIM_Hide:
		d = INST_DATA(cl, obj);
		d->shown = 0;
		break;
	case MUIM_Draw: {
		Rect pr, area;
		void *save, *port;
		DoSuperMethodA(cl, obj, msg);
		if (!(((struct MUIP_Draw *)msg)->flags & MADF_DRAWOBJECT))
			return 0;
		d = INST_DATA(cl, obj);
		port = mm_win_of(obj)->macwin;
		lay_update(qd.lay);
		if (zoom_dpi(d, obj) != d->dpi) {
			d->dpi = zoom_dpi(d, obj);
			cache_drop_all(d);
		}
		bars_place(d, obj);
		if (d->follow) {
			/* (scrolled without drawing: this draws everything anyway) */
			u32 k;
			long x, y, h, ppx, ppy;
			parts(obj, &pr, NULL, NULL, NULL);
			caret_px(d, qd.caret, &k, &x, &y, &h);
			page_at(d, obj, k, &ppx, &ppy);
			if (ppy + y < d->sy || ppy + y + h > d->sy + (pr.bottom - pr.top))
				d->sy = ppy + y - (pr.bottom - pr.top) / 3;
			bars_set(d, obj);
			d->follow = 0;
		}
		/* only what the update (or the caller) left drawable: the visible region's box */
		parts(obj, &pr, NULL, NULL, NULL);
		area = *RGN_BBOX(*(void ***)((char *)port + 24));      /* a GrafPort's visRgn is at 24 */
		save = clip_pages(obj);
		d->stale = 0;
		draw_area(d, obj, &area);
		clip_end(save);
		Draw1Control(d->vbar);
		Draw1Control(d->hbar);
		{
			Rect st;
			parts(obj, NULL, NULL, NULL, &st);
			if (st.left < area.right && area.left < st.right && st.top < area.bottom && area.top < st.bottom)
				draw_status(d, obj);
		}
		status_check(d, obj, 1);
		return 0;
	}
	case MUIM_HandleEvent: {
		struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
		d = INST_DATA(cl, obj);
		if (!im || !d->shown)
			return 0;
		switch (im->Class) {
		case IDCMP_MOUSEBUTTONS:
			if (im->Code == 0x68 && inside(obj, im->MouseX, im->MouseY)) {
				click(d, obj, im);
				return MUI_EventHandlerRC_Eat;
			}
			if (im->Code == 0xE8)
				d->dragging = 0;
			return 0;
		case IDCMP_MOUSEMOVE:
			pointer(obj, im->MouseX, im->MouseY);
			if (d->dragging)
				drag(d, obj, im->MouseX, im->MouseY);
			return 0;
		case IDCMP_VANILLAKEY:
			ObscureCursor();
			key(d, obj, im->Code, im->Qualifier);
			return MUI_EventHandlerRC_Eat;
		}
		return 0;
	}
	case MUIM_QV_Tick:
		tick(INST_DATA(cl, obj), obj);
		return 0;
	}
	return DoSuperMethodA(cl, obj, msg);
}

struct MUI_CustomClass *qv_class(void)
{
	if (!qv_mcc)
		qv_mcc = MUI_CreateCustomClass(NULL, (CONST_STRPTR)MUIC_Area, NULL, sizeof(struct qv_data), DISPATCHER_REF(qv_dispatch));
	return qv_mcc;
}

void qv_zoom(Object *view, int percent)
{
	struct qv_data *d = INST_DATA(qv_mcc->mcc_Class, view);
	d->zoom = percent;
	d->follow = 1;
	cache_drop_all(d);
	if (d->shown)
		MUI_Redraw(view, MADF_DRAWOBJECT);
}

void qv_reset(Object *view)
{
	struct qv_data *d = INST_DATA(qv_mcc->mcc_Class, view);
	cache_drop_all(d);
	d->sx = d->sy = 0;
	d->caret_on = 0;
	d->stale = 0;
	d->want_x = -1;
	if (d->shown) {
		void *port = mm_win_of(view)->macwin;
		Rect r;
		parts(view, &r, NULL, NULL, NULL);
		SetPort(port);
		InvalRect(&r);          /* the update event draws it */
		lay_update(qd.lay);     /* (laid out now, so the page box counts the pages) */
		d->status_page = 0;     /* a new document: the page box again */
		status_check(d, view, 0);
		bars_set(d, view);
	}
}

void qv_redraw_all(Object *view)
{
	struct qv_data *d = INST_DATA(qv_mcc->mcc_Class, view);
	if (d->shown) {
		Rect r;
		parts(view, &r, NULL, NULL, NULL);
		SetPort(mm_win_of(view)->macwin);
		InvalRect(&r);
	}
}
