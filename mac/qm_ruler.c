/*
 * mac/qm_ruler.c - Quill on the classic Mac OS: the rulers. GPL-2.0-only, as Quill.
 *
 * The Amiga's ruler (amiga/ruler.c) is a MUI class drawn flat in WordCraft's colours. The Mac's
 * draws the same things with QuickDraw inside the page view's box (mac/qm_view.c puts it there and
 * hands it the caret's ruler, core/ruler.c, which also decides what a click and a drag do, so both
 * rulers behave alike):
 *
 *   - the horizontal ruler under the tool strip, its scale lined up with the page as it is scrolled
 *     and zoomed: inches in eighths on a US Letter page, centimetres in quarters on any other (as on
 *     the Amiga), numbered from the text's left edge; outside the page the window's ground, the
 *     margins shaded, the text area white; the first-line indent hanging from the top, the left and
 *     right indents standing on the bottom, the caret paragraph's tab stops, and in a table the
 *     column edges;
 *   - the vertical ruler down the left: the caret's page's top and bottom margins, as on the Amiga;
 *   - the box where they meet: the kind of tab stop a click on the ruler makes (left, centre, right,
 *     decimal), changed by clicking it, as the Amiga's box in the same place.
 *
 * Dragging a marker is tracked here, as the Mac tracks a control (Guidelines p. 207): the ruler
 * follows the pointer and a dotted line over the pages shows where the marker will be; the change is
 * made when the button goes up, as one undoable edit (ruler_apply). A tab stop dragged off the
 * ruler goes. Black and white first: on a one-bit screen the margins are a light grey pattern and
 * the markers white with black edges; from two bits up the greys are greys.
 */
#include <string.h>
#include <stdio.h>
#include "mui/mm.h"
#include "../core/ruler.h"
#include "quill_mac.h"

#define GENEVA     3                         /* the Font Manager's number for Geneva */
#define GROUND     MM_RGB(0xDD, 0xDD, 0xDD)  /* outside the page (white on one bit) */
#define MARGIN_RGB MM_RGB(0xB4, 0xB4, 0xB4)  /* the margins: a light grey pattern on one bit */
#define SLOP_PX    6                         /* how near a click must be to a marker, as the Amiga's */
#define OFF_PX     8                         /* a tab stop dragged this far above or below the ruler goes */
#define TOPHALF    9                         /* the rows where the first-line marker is */

static int tab_kind = TAB_LEFT;              /* what a click on the ruler makes (the corner box) */

/* twips as pixels at dpi, rounded both ways from zero (as core/raster.c rounds) */
static long px(long tw, int dpi)
{
	return tw >= 0 ? (tw * dpi + 720) / 1440 : -((-tw * dpi + 720) / 1440);
}

static long tw_of(long pixels, int dpi)
{
	return pixels * 1440 / dpi;
}

/* a line or a box along the ruler: u along it, v across (the vertical ruler swaps them) */
static void seg(int vert, int u0, int v0, int u1, int v1)
{
	if (vert) {
		MoveTo(v0, u0);
		LineTo(v1, u1);
	} else {
		MoveTo(u0, v0);
		LineTo(u1, v1);
	}
}

static void band(void *port, int vert, long u0, long u1, int v0, int v1, unsigned long rgb)
{
	Rect r;
	if (u1 <= u0)
		return;
	if (vert) {
		r.left = (short)v0; r.right = (short)v1; r.top = (short)u0; r.bottom = (short)u1;
	} else {
		r.left = (short)u0; r.right = (short)u1; r.top = (short)v0; r.bottom = (short)v1;
	}
	mm_fill(port, &r, rgb);
}

/* The scale along one ruler: a..b its length in window pixels, v0..v1 the band across it the page
   is shown in; p0..p1 the page, o..e the text area (numbered from o). Pages in the margin shade, the
   text area white, ticks and numbers in black. */
static void scale(void *port, int vert, long a, long b, int v0, int v1, long p0, long p1, long o, long e, int dpi, int metric)
{
	i32 unit;
	int nsub, mid = (v0 + v1) / 2;
	long k, z, step;
	char t[8];
	ruler_unit(metric, &unit, &nsub);
	band(port, vert, p0 > a ? p0 : a, p1 < b ? p1 : b, v0, v1, MARGIN_RGB);
	band(port, vert, o > a ? o : a, e < b ? e : b, v0, v1, MM_RGB(0xFF, 0xFF, 0xFF));
	ForeColor(33);
	TextFont(GENEVA);
	TextSize(9);
	TextFace(0);
	TextMode(1);
	step = px(unit, dpi);
	for (k = -(tw_of(o - p0, dpi) / unit + 1) * nsub;; k++) {
		z = o + px(k * unit / nsub, dpi);
		if (z > b || z > p1)
			break;
		if (z < a || z < p0)
			continue;
		if (k % nsub == 0) {
			if (k && step > 14) {
				/* the number, centred on its place (its size, not its sign: Word numbers both ways) */
				int n = snprintf(t, sizeof t, "%ld", k < 0 ? -k / nsub : k / nsub), w = TextWidth(t, 0, n);
				if (vert)
					MoveTo(mid - w / 2 + 1, (int)z + 4);
				else
					MoveTo((int)z - w / 2 + 1, mid + 4);
				DrawText(t, 0, n);
			}
		} else if (k % (nsub / 2) == 0) {
			seg(vert, (int)z, mid - 2, (int)z, mid + 2);
		} else if (px(unit / nsub, dpi) >= 4) {
			seg(vert, (int)z, mid, (int)z, mid);
		}
	}
}

/* an indent marker: a small triangle, white with a black edge, its point at x, y, pointing down (the
   first line's, hanging from the top) or up */
static void marker(int x, int y, int up)
{
	int i, d = up ? 1 : -1;
	ForeColor(30);
	for (i = 0; i < 4; i++) {
		MoveTo(x - i, y + d * i);
		LineTo(x + i, y + d * i);
	}
	ForeColor(33);
	MoveTo(x - 4, y + d * 4);
	LineTo(x, y);
	LineTo(x + 4, y + d * 4);
	LineTo(x - 4, y + d * 4);
}

/* a tab stop's mark with its foot at x, y: an L for left, reversed for right, an upside-down T for
   centred, with a dot for decimal (the Amiga's shapes) */
static void tab_glyph(int kind, int x, int y)
{
	ForeColor(33);
	switch (kind) {
	case TAB_RIGHT:
		MoveTo(x, y - 4); LineTo(x, y); LineTo(x - 4, y);
		MoveTo(x - 1, y - 4); LineTo(x - 1, y - 1); LineTo(x - 4, y - 1);
		break;
	case TAB_CENTER: case TAB_DECIMAL:
		MoveTo(x, y - 4); LineTo(x, y);
		MoveTo(x - 3, y); LineTo(x + 3, y);
		MoveTo(x - 3, y - 1); LineTo(x + 3, y - 1);
		if (kind == TAB_DECIMAL) {
			MoveTo(x + 2, y - 3);
			LineTo(x + 3, y - 3);
		}
		break;
	default:
		MoveTo(x, y - 4); LineTo(x, y); LineTo(x + 4, y);
		MoveTo(x + 1, y - 4); LineTo(x + 1, y - 1); LineTo(x + 4, y - 1);
		break;
	}
}

/* drawing clipped to r, the clip given back after */
static void *clip_to(void *port, const Rect *r)
{
	void *save = NewRgn();
	SetPort(port);
	GetClip(save);
	ClipRect(r);
	return save;
}

static void unclip(void *save)
{
	SetClip(save);
	DisposeRgn(save);
}

void qr_draw_h(void *port, const void *rv, const RulerGeom *g, long pagex, int dpi)
{
	const Rect *r = (const Rect *)rv;
	void *save = clip_to(port, r);
	int mono = mm_mono_port(port), t = r->top, k;
	long x0 = pagex + px(g->x0, dpi);
	Rect in = *r;
	in.bottom--;
	mm_fill(port, &in, mono ? MM_RGB(0xFF, 0xFF, 0xFF) : GROUND);
	scale(port, 0, r->left, r->right, t + 3, t + 16, pagex, pagex + px(g->pagew, dpi), x0, x0 + px(g->width, dpi), dpi, g->metric);
	/* a table's column edges: grey blocks with a line down them */
	for (k = 1; k < g->ncolx; k++) {
		int x = (int)(pagex + px(g->tblx0 + g->colx[k], dpi));
		Rect c;
		c.left = (short)(x - 3); c.right = (short)(x + 4); c.top = (short)(t + 3); c.bottom = (short)(t + 16);
		mm_fill(port, &c, MARGIN_RGB);
		mm_frame(port, &c, 0);
		MoveTo(x, t + 4);
		LineTo(x, t + 14);
	}
	/* the indents: the first line from the top, left and right on the bottom */
	marker((int)(x0 + px(g->left + g->first, dpi)), t + 6, 0);
	marker((int)(x0 + px(g->left, dpi)), t + 12, 1);
	marker((int)(x0 + px(g->width - g->right, dpi)), t + 12, 1);
	for (k = 0; k < RULER_TABS && g->tabs[k]; k++)
		if (g->tabs[k] > 0)
			tab_glyph(g->tabkind[k], (int)(x0 + px(g->left + g->tabs[k], dpi)), t + 16);
	mm_line(port, r->left, r->bottom - 1, r->right, r->bottom - 1, 0);
	unclip(save);
}

void qr_draw_v(void *port, const void *rv, const RulerGeom *g, long pagey, int dpi)
{
	const Rect *r = (const Rect *)rv;
	void *save = clip_to(port, r);
	int mono = mm_mono_port(port);
	Rect in = *r;
	in.right--;
	mm_fill(port, &in, mono ? MM_RGB(0xFF, 0xFF, 0xFF) : GROUND);
	scale(port, 1, r->top, r->bottom, r->left + 2, r->right - 3, pagey, pagey + px(g->pageh, dpi),
	      pagey + px(g->mtop, dpi), pagey + px(g->pageh - g->mbottom, dpi), dpi, g->metric);
	mm_line(port, r->right - 1, r->top, r->right - 1, r->bottom, 0);
	unclip(save);
}

void qr_draw_corner(void *port, const void *rv)
{
	const Rect *r = (const Rect *)rv;
	int mono = mm_mono_port(port);
	Rect in = *r, b;
	in.right--;
	in.bottom--;
	mm_fill(port, &in, mono ? MM_RGB(0xFF, 0xFF, 0xFF) : GROUND);
	b.left = (short)(r->left + 2); b.right = (short)(r->right - 3); b.top = (short)(r->top + 3); b.bottom = (short)(r->bottom - 4);
	mm_fill(port, &b, MM_RGB(0xFF, 0xFF, 0xFF));
	mm_frame(port, &b, 0);
	tab_glyph(tab_kind, (b.left + b.right) / 2 - (tab_kind == TAB_LEFT ? 2 : tab_kind == TAB_RIGHT ? -2 : 0), b.bottom - 4);
	mm_line(port, r->left, r->bottom - 1, r->right, r->bottom - 1, 0);
	mm_line(port, r->right - 1, r->top, r->right - 1, r->bottom, 0);
}

void qr_click_corner(void *port, const void *r)
{
	tab_kind = (tab_kind + 1) % 4;
	qr_draw_corner(port, r);
}

/* where a part is, twips from the page's left edge */
static long part_x(const RulerGeom *g, int part)
{
	if (part >= RP_COL)
		return g->tblx0 + g->colx[part - RP_COL];
	if (part >= RP_TAB)
		return g->x0 + g->left + (g->tabs[part - RP_TAB] > 0 ? g->tabs[part - RP_TAB] : 0);
	if (part == RP_FIRST)
		return g->x0 + g->left + g->first;
	if (part == RP_LEFT)
		return g->x0 + g->left;
	return g->x0 + g->width - g->right;
}

/* the dotted line over the pages at window x (drawn again, it goes: patXor) */
static void guide_line(void *port, const Rect *guide, int x)
{
	static const unsigned char grey[8] = { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 };
	void *save = clip_to(port, guide);
	PenPat(grey);
	PenMode(10);            /* patXor */
	MoveTo(x, guide->top);
	LineTo(x, guide->bottom - 1);
	PenNormal();
	unclip(save);
}

int qr_track_h(void *port, const void *rv, const void *guidev, RulerGeom *g, long pagex, int dpi, int x, int y)
{
	const Rect *r = (const Rect *)rv, *guide = (const Rect *)guidev;
	int top = y < r->top + TOPHALF, part, gx, lastx = x, off = 0, moved = 0;
	Point m;
	part = ruler_hit(g, tw_of(x - pagex, dpi), top, tw_of(SLOP_PX, dpi));
	if (!part && !top && (part = ruler_add_tab(g, tw_of(x - pagex, dpi), tab_kind)) != RP_NONE) {
		/* a new tab stop: where the click was */
		ruler_drag(g, part, tw_of(x - pagex, dpi), 0);
		qr_draw_h(port, r, g, pagex, dpi);
		moved = 1;
	}
	if (!part)
		return RP_NONE;
	gx = (int)(pagex + px(part_x(g, part), dpi));
	guide_line(port, guide, gx);
	while (StillDown()) {
		int now_off;
		SetPort(port);
		GetMouse(&m);
		now_off = m.v < r->top - OFF_PX || m.v >= r->bottom + OFF_PX;
		if (m.h == lastx && now_off == off)
			continue;
		lastx = m.h;
		off = now_off;
		moved = 1;
		guide_line(port, guide, gx);
		ruler_drag(g, part, tw_of(m.h - pagex, dpi), off);
		qr_draw_h(port, r, g, pagex, dpi);
		gx = (int)(pagex + px(part_x(g, part), dpi));
		guide_line(port, guide, gx);
	}
	guide_line(port, guide, gx);
	return moved ? part : RP_NONE;
}
