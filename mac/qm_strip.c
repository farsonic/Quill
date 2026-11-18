/*
 * mac/qm_strip.c - Quill on the classic Mac OS: the tool strip, which the View menu calls the Ribbon,
 * as Word for the Mac called its formatting bar. GPL-2.0-only, as Quill.
 *
 * System 7 has no ribbon and Apple's guidelines would not welcome one. Cornerstone's classic Mac
 * chapter says what becomes of it: the ribbon's most used tools go into one row inside the document
 * window, under the title bar (Human Interface Guidelines, 1992, p. 97: a palette never goes where
 * the scroll bars or the close box are), drop-down lists become pop-up menus (pp. 82-91), and every
 * item is a menu command with the menu's state. So the strip holds what the Amiga's Home tab uses
 * most (amiga/ribbon.c, g_font and g_para): the paragraph style, font and size as pop-up menus, Bold,
 * Italic and Underline, the four alignments, bulleted and numbered lists, and the indent buttons.
 * Show Paragraph Marks is left out: the Mac's page view draws pages as their PDF looks
 * (pdf_page_rgb), which has no marks to show.
 *
 * Each item names a menu command (quill_mac.h) and draws itself from qm_state(), the answer the
 * menus' check marks take, so an item and its menu item never disagree; a click runs the same
 * command the menu would. A pop-up shows the same items as its menu (Format > Paragraph Style, Font,
 * Size), the current one checked, and the box shows the caret's value even when the menu has no
 * such item (an 11-point size, a document's own font).
 *
 * The strip is drawn by the page view inside its box (mac/qm_view.c), in Geneva 9 as palettes are
 * (Cornerstone: fh 12, a one-row toolbar fh + 12 high). Black and white first (p. 263): on a one-bit
 * screen the band is white, the buttons framed in black, a button that is on is inverted (as a
 * palette shows its chosen tool, p. 96) and one held down is inverted while the pointer is on it.
 * From four bits up the band is light grey and the icons take their colours. The icons are the
 * Amiga's (amiga/theme.c), in Cornerstone's icon language (chapter 12), drawn with QuickDraw.
 *
 * At 640 pixels the whole strip fits (about 540 pixels). A narrower window drops whole groups from
 * the right end, least used last: indents, then lists, alignment, the character styles; their
 * commands stay in the menus.
 */
#include <string.h>
#include <stdio.h>
#include "mui/mm.h"
#include "quill_mac.h"

enum { SK_POPUP, SK_TOGGLE, SK_BUTTON };
#define BW        22            /* an icon button's width (Cornerstone ch. 4: an icon-only item, 22) */
#define BH        20            /* its height, the band's less two above and one with the line below */
#define GAP       8             /* between groups */
#define POPGAP    4             /* between the pop-ups */
#define MARGIN    6             /* before the first item */
#define TEXTW     18            /* a pop-up's room besides its text: the triangle and the edges */
#define GENEVA    3             /* the Font Manager's font numbers: Geneva, Times */
#define TIMES     20
#define MENU_ID   251           /* the pop-up menu's id while it is up (Riptide's Cycle uses 250) */
#define BG_COLOUR MM_RGB(0xDD, 0xDD, 0xDD)  /* the band from 4 bits up: light grey, white on 1 bit */
#define ON_COLOUR MM_RGB(0x33, 0x33, 0x33)  /* a button that is on, from 4 bits up (black on 1 bit) */

typedef struct {
	unsigned char kind, group, which;   /* which: QP_ for a pop-up */
	short width;
	unsigned long id;                   /* the menu command (0 for a pop-up: its menu's) */
	const char *icon;                   /* the Amiga's icon (amiga/theme.c, the same names) */
} SItem;

static const SItem items[] = {
	{ SK_POPUP, 0, QP_STYLE, 96, 0, NULL },
	{ SK_POPUP, 0, QP_FONT, 112, 0, NULL },
	{ SK_POPUP, 0, QP_SIZE, 40, 0, NULL },
	/* BOLD, ITALIC, UNDERLINE: the letter in the interface font, bold, slanted, underlined */
	{ SK_TOGGLE, 1, 0, BW, ID_BOLD, "B B" },
	{ SK_TOGGLE, 1, 0, BW, ID_ITALIC, "I I" },
	{ SK_TOGGLE, 1, 0, BW, ID_UNDERLINE, "U U" },
	/* LEFT, CENTER, RIGHT, JUSTIFY: lines flush left, centred, flush right, flush both */
	{ SK_TOGGLE, 2, 0, BW, ID_LEFT, "ci L 1 2 14 2 L 1 5 10 5 L 1 8 14 8 L 1 11 10 11 L 1 14 12 14" },
	{ SK_TOGGLE, 2, 0, BW, ID_CENTER, "ci L 1 2 14 2 L 3 5 12 5 L 1 8 14 8 L 3 11 12 11 L 2 14 13 14" },
	{ SK_TOGGLE, 2, 0, BW, ID_RIGHT, "ci L 1 2 14 2 L 5 5 14 5 L 1 8 14 8 L 5 11 14 11 L 3 14 14 14" },
	{ SK_TOGGLE, 2, 0, BW, ID_JUSTIFY, "ci L 1 2 14 2 L 1 5 14 5 L 1 8 14 8 L 1 11 14 11 L 1 14 9 14" },
	/* BULLETS: three dots and their lines; NUMBERS: 1, 2, 3 in the accent and their lines */
	{ SK_TOGGLE, 3, 0, BW, ID_BULLETS, "ci E 2 3 1 E 2 8 1 E 2 13 1 L 5 3 14 3 L 5 8 14 8 L 5 13 14 13" },
	{ SK_TOGGLE, 3, 0, BW, ID_NUMBERS, "ca L 2 1 2 5 L 1 8 3 8 L 3 8 1 11 L 1 11 3 11 ci L 6 3 14 3 L 6 8 14 8 L 6 13 14 13" },
	/* OUTDENT, INDENT: lines moved out or in, an arrow pointing that way */
	{ SK_BUTTON, 4, 0, BW, ID_OUTDENT, "ci L 1 2 14 2 L 7 6 14 6 L 7 10 14 10 L 1 14 14 14 ca P 3 4 5 1 8 4 11" },
	{ SK_BUTTON, 4, 0, BW, ID_INDENT, "ci L 1 2 14 2 L 7 6 14 6 L 7 10 14 10 L 1 14 14 14 ca P 3 1 5 4 8 1 11" },
};
#define NITEMS ((int)(sizeof items / sizeof items[0]))

static Rect band;                           /* the strip's box; empty before qs_layout */
static Rect box[NITEMS];
static unsigned char shown[NITEMS];
static unsigned drawn_state[NITEMS];        /* what each item was drawn as, + 1 (0: not yet) */
static char drawn_text[NITEMS][48];

/* ---- the icon language (Cornerstone chapter 12; amiga/theme.c draws the same strings) ---- */

/* an ink as RGB, from four bits up; on fewer every ink but white is black */
static unsigned long ink_rgb(char c)
{
	switch (c) {
	case 'a': return MM_RGB(0x1F, 0x6F, 0xBF);     /* accent: MUI's fill blue, as the Mac MUI's */
	case 'r': return MM_RGB(0xCC, 0x22, 0x22);
	case 'g': return MM_RGB(0x22, 0x88, 0x22);
	case 'o': return MM_RGB(0xEE, 0x88, 0x00);
	case 'y': return MM_RGB(0xEE, 0xCC, 0x00);
	case 'd': return MM_RGB(0x80, 0x80, 0x80);
	case 'l': return MM_RGB(0xC0, 0xC0, 0xC0);
	case 's': return MM_RGB(0x99, 0xBB, 0xEE);
	case 'w': return MM_RGB(0xFF, 0xFF, 0xFF);
	default:  return 0;                            /* i, t: black */
	}
}

static void pen_rgb(unsigned long rgb)
{
	RGBColor c;
	c.red = (unsigned short)(((rgb >> 16) & 0xFF) * 257);
	c.green = (unsigned short)(((rgb >> 8) & 0xFF) * 257);
	c.blue = (unsigned short)((rgb & 0xFF) * 257);
	RGBForeColor(&c);
}

/* the ink for what follows: colour (4 bits up), or black and white; pen: every ink white (a button
   that is on: one-pen drawing, as Cornerstone's cs_icon_pen) */
static void ink(char c, int colour, int pen)
{
	if (pen)
		ForeColor(30);              /* whiteColor */
	else if (colour)
		pen_rgb(ink_rgb(c));
	else
		ForeColor(c == 'w' ? 30 : 33);
}

static int num(const char **s)
{
	int v = 0, neg = 0;
	while (**s == ' ')
		(*s)++;
	if (**s == '-') {
		neg = 1;
		(*s)++;
	}
	while (**s >= '0' && **s <= '9')
		v = v * 10 + (*(*s)++ - '0');
	return neg ? -v : v;
}

/* the icon string s drawn at 16 pixels with its top left at ox, oy: one grid unit, one pixel */
static void icon(const char *s, int ox, int oy, int colour, int pen)
{
	Rect r;
	int a, b, c, d, n;
	ink('i', colour, pen);
	if (*s == '>')
		s++;                        /* (mirrored for right-to-left: the Mac's menus run left to right) */
	while (*s) {
		char op = *s++;
		switch (op) {
		case 'c':
			ink(*s ? *s++ : 'i', colour, pen);
			break;
		case 'L':
			a = num(&s); b = num(&s); c = num(&s); d = num(&s);
			MoveTo(ox + a, oy + b);
			LineTo(ox + c, oy + d);
			break;
		case 'R': case 'F':
			a = num(&s); b = num(&s); c = num(&s); d = num(&s);
			r.left = (short)(ox + a); r.top = (short)(oy + b); r.right = (short)(ox + c + 1); r.bottom = (short)(oy + d + 1);
			if (op == 'R')
				FrameRect(&r);
			else
				PaintRect(&r);
			break;
		case 'O': case 'E':
			a = num(&s); b = num(&s); c = num(&s);
			r.left = (short)(ox + a - c); r.top = (short)(oy + b - c); r.right = (short)(ox + a + c + 1); r.bottom = (short)(oy + b + c + 1);
			if (op == 'O')
				FrameOval(&r);
			else
				PaintOval(&r);
			break;
		case 'P':
			n = num(&s);
			a = num(&s); b = num(&s);
			MoveTo(ox + a, oy + b);
			while (--n > 0) {
				a = num(&s); b = num(&s);
				LineTo(ox + a, oy + b);
			}
			break;
		case 'B': case 'I': case 'U': {
			/* letters in Times 12, centred (Geneva's italic I is a bare stroke); they keep their
			   size, as Cornerstone's do */
			char t[8];
			int k = 0, w;
			while (*s == ' ')
				s++;
			while (*s && *s != ' ' && k < 7)
				t[k++] = *s++;
			TextFont(TIMES);
			TextSize(12);
			TextFace(op == 'B' ? 1 : op == 'I' ? 2 : 4);          /* bold, italic, underline */
			TextMode(1);                                          /* srcOr */
			w = TextWidth(t, 0, k);
			MoveTo(ox + 8 - w / 2, oy + 12);
			DrawText(t, 0, k);
			TextFace(0);
			break;
		}
		default:
			break;
		}
	}
	ForeColor(33);
}

/* ---- the items ---- */

static int depth_of(void *port)
{
	return mm_mono_port(port) ? 1 : qm_port_depth(port);
}

static void text9(void)
{
	TextFont(GENEVA);
	TextSize(9);
	TextFace(0);
	TextMode(1);
}

/* item k as it is now; pressed: held down under the pointer */
static void draw_item(void *port, int k, int pressed)
{
	const SItem *it = &items[k];
	Rect r = box[k];
	int colour = depth_of(port) >= 4;
	SetPort(port);
	if (it->kind == SK_POPUP) {
		/* Guidelines p. 85: a box with a one-pixel shadow, its value, the triangle at the right */
		unsigned long first;
		char t[48];
		int n, w, x, y;
		qm_popup(it->which, &first, t, sizeof t);
		strcpy(drawn_text[k], t);
		r.right--;
		r.bottom--;
		mm_fill(port, &r, MM_RGB(0xFF, 0xFF, 0xFF));
		mm_frame(port, &r, 0);
		mm_line(port, r.left + 2, r.bottom, r.right, r.bottom, 0);
		mm_line(port, r.right, r.top + 2, r.right, r.bottom, 0);
		text9();
		ForeColor(33);
		n = (int)strlen(t);
		/* cut to fit, an ellipsis at the end */
		if (TextWidth(t, 0, n) > r.right - r.left - TEXTW) {
			while (n > 0 && TextWidth(t, 0, n) + TextWidth("\xC9", 0, 1) > r.right - r.left - TEXTW)
				n--;
			t[n++] = (char)0xC9;
		}
		MoveTo(r.left + 5, r.top + 12);
		DrawText(t, 0, n);
		x = r.right - 12;
		y = r.top + (r.bottom - r.top) / 2 - 2;
		for (w = 0; w < 4; w++) {
			MoveTo(x + w, y + w);
			LineTo(x + 6 - w, y + w);
		}
		if (pressed)
			InvertRect(&r);
		return;
	}
	{
		unsigned st = qm_state(it->id);
		int on = (st & QS_CHECKED) != 0;
		Rect in = r;
		InsetRect(&in, 1, 1);
		mm_fill(port, &r, MM_RGB(0xFF, 0xFF, 0xFF));
		mm_frame(port, &r, 0);
		if (on) {
			if (colour)
				mm_fill(port, &in, ON_COLOUR);
			else {
				ForeColor(33);
				PaintRect(&in);
			}
		}
		icon(it->icon, r.left + (r.right - r.left - 16) / 2, r.top + (r.bottom - r.top - 16) / 2, colour, on);
		if (!(st & QS_ENABLED)) {
			/* dimmed: a quarter of the ink taken away, as the Mac MUI dims (patBic) */
			static const unsigned char grey[8] = { 0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22 };
			PenPat(grey);
			PenMode(11);
			PaintRect(&in);
			PenNormal();
		}
		if (pressed)
			InvertRect(&in);
	}
}

void qs_layout(short left, short top, short right)
{
	int k, x = left + MARGIN, g = 0, drop = 0;
	if (band.left == left && band.top == top && band.right == right)
		return;                     /* (laid out already: what is drawn stays known) */
	band.left = left;
	band.top = top;
	band.right = right;
	band.bottom = (short)(top + QS_HEIGHT);
	for (k = 0; k < NITEMS; k++) {
		const SItem *it = &items[k];
		if (it->group != g) {
			/* a new group: all of it fits, or it and the ones after it go */
			int j, w = 0;
			for (j = k; j < NITEMS && items[j].group == it->group; j++)
				w += items[j].width - (items[j].kind == SK_POPUP ? -POPGAP : 1);
			x += GAP;
			if (x + w > right - MARGIN)
				drop = 1;
			g = it->group;
		}
		shown[k] = (unsigned char)(!drop && x + it->width <= right - MARGIN);
		box[k].left = (short)x;
		box[k].right = (short)(x + it->width);
		if (it->kind == SK_POPUP) {
			box[k].top = (short)(top + 3);
			box[k].bottom = (short)(top + 3 + BH - 1);
			x += it->width + POPGAP;
		} else {
			box[k].top = (short)(top + 2);
			box[k].bottom = (short)(top + 2 + BH);
			x += it->width - 1;          /* neighbours share their frame line */
		}
		drawn_state[k] = 0;
	}
}

static int meets(const Rect *a, const Rect *b)
{
	return a->left < b->right && b->left < a->right && a->top < b->bottom && b->top < a->bottom;
}

void qs_draw(void *port, const void *area)
{
	const Rect *ar = (const Rect *)area;
	Rect r = band;
	int k, colour;
	if (band.right <= band.left || !meets(&band, ar))
		return;
	colour = depth_of(port) >= 4;
	r.bottom--;
	mm_fill(port, &r, colour ? BG_COLOUR : MM_RGB(0xFF, 0xFF, 0xFF));
	mm_line(port, band.left, band.bottom - 1, band.right, band.bottom - 1, 0);
	for (k = 0; k < NITEMS; k++)
		if (shown[k] && meets(&box[k], ar)) {
			draw_item(port, k, 0);
			drawn_state[k] = items[k].kind == SK_POPUP ? 1 : qm_state(items[k].id) + 1;
		}
}

void qs_update(void *port)
{
	int k;
	if (band.right <= band.left)
		return;
	for (k = 0; k < NITEMS; k++) {
		if (!shown[k] || !drawn_state[k])
			continue;
		if (items[k].kind == SK_POPUP) {
			unsigned long first;
			char t[48];
			qm_popup(items[k].which, &first, t, sizeof t);
			if (!strcmp(t, drawn_text[k]))
				continue;
		} else if (qm_state(items[k].id) + 1 == drawn_state[k])
			continue;
		draw_item(port, k, 0);
		drawn_state[k] = items[k].kind == SK_POPUP ? 1 : qm_state(items[k].id) + 1;
	}
}

/* a pop-up menu of the menu's items under the box, its current item over the box (p. 85) */
static void popup(void *port, int k)
{
	unsigned char pb[256];
	unsigned long first;
	char t[48];
	void *m;
	Point p;
	long r;
	int i, n, cur = 1;
	n = qm_popup(items[k].which, &first, t, sizeof t);
	if (n <= 0 || !(m = NewMenu(MENU_ID, mac_pstr(pb, ""))))
		return;
	for (i = 0; i < n; i++) {
		unsigned st = qm_state(first + (unsigned long)i);
		AppendMenu(m, mac_pstr(pb, " "));
		SetItem(m, i + 1, mac_pstr(pb, qm_label(first + (unsigned long)i)));
		if (st & QS_CHECKED) {
			CheckItem(m, i + 1, 1);
			cur = i + 1;
		}
		if (!(st & QS_ENABLED))
			DisableItem(m, i + 1);
	}
	InsertMenu(m, -1);
	p.h = box[k].left;
	p.v = box[k].top;
	SetPort(port);
	LocalToGlobal(&p);
	draw_item(port, k, 1);
	r = PopUpMenuSelect(m, p.v + 1, p.h + 1, cur);
	DeleteMenu(MENU_ID);
	DisposeMenu(m);
	draw_item(port, k, 0);
	if ((r >> 16) == MENU_ID && (r & 0xFFFF))
		qm_command(first + (unsigned long)((r & 0xFFFF) - 1));
}

int qs_click(void *port, int x, int y)
{
	int k, in = 1, was;
	Point m;
	if (x < band.left || x >= band.right || y < band.top || y >= band.bottom)
		return 0;
	for (k = 0; k < NITEMS; k++)
		if (shown[k] && x >= box[k].left && x < box[k].right && y >= box[k].top && y < box[k].bottom)
			break;
	if (k == NITEMS)
		return 1;                   /* the band itself: nothing */
	if (items[k].kind == SK_POPUP) {
		popup(port, k);
		return 1;
	}
	if (!(qm_state(items[k].id) & QS_ENABLED))
		return 1;
	/* a button: inverted while held with the pointer on it, its command if let go there (p. 207) */
	SetPort(port);
	draw_item(port, k, 1);
	while (StillDown()) {
		GetMouse(&m);
		was = in;
		in = m.h >= box[k].left && m.h < box[k].right && m.v >= box[k].top && m.v < box[k].bottom;
		if (in != was)
			draw_item(port, k, in);
	}
	draw_item(port, k, 0);
	drawn_state[k] = qm_state(items[k].id) + 1;
	if (in)
		qm_command(items[k].id);
	return 1;
}
