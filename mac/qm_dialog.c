/*
 * mac/qm_dialog.c - Quill on the classic Mac OS: its modal dialogs. GPL-2.0-only, as Quill.
 *
 * The Amiga's Quill builds its windows from MUI groups; most of them are too tied to its own MUI
 * classes to compile on Riptide's Mac MUI. The Mac's dialogs are few and small (Insert Table, Page
 * Setup, Paragraph, Find and Replace, a footnote's or comment's text), so they are built here the
 * Mac's own way: a Dialog Manager item list made in memory (programs packed from ELF files carry no
 * resources) and run with ModalDialog, as Riptide builds its alerts (riptide src/mac/app/mac_alert.c,
 * whose filter and default-button outline mac_dialog_modal lends us).
 *
 * The Human Interface Guidelines (1992) laid out by the caller and kept here: no title bar (a modal
 * dialog box, p. 185), its prompt at the top in the system font; buttons 20 pixels high, at least 59
 * wide (pp. 204-205), the action at the bottom right with Cancel to its left (p. 196); the default
 * button with the heavy outline, answering Return and Enter, Cancel answering Escape and
 * Command-period (p. 206); check boxes and radio buttons (pp. 210-211); text fields the Dialog Manager
 * edits, Tab moving between them. The dialog sits a third of the way down the main screen.
 */
#include <string.h>
#include <stdlib.h>
#include "mui/mm.h"
#include "quill_mac.h"
#include "qm_dialog.h"

#define BTN_H   20              /* a push button's height (p. 204) */
#define LINE_H  16              /* static text, a check box or a radio button: the system font's line */
#define EDIT_H  16              /* a text field's box (the Dialog Manager frames it 3 pixels outside) */
#define K_BUTTON 4              /* item kinds: ctrlItem + btnCtrl, chkCtrl, radCtrl; statText, editText */
#define K_CHECK  5
#define K_RADIO  6
#define K_TEXT   8
#define K_EDIT   16
#define K_USER   0
#define K_OFF    128            /* itemDisable: the Dialog Manager does not report clicks on it */

/* one item of a Dialog Manager item list (DITL): a placeholder handle, its box, kind, data */
static unsigned char *ditl_item(unsigned char *q, const Rect *r, int kind, const char *data, int len)
{
	short v[4];
	int i;
	v[0] = r->top; v[1] = r->left; v[2] = r->bottom; v[3] = r->right;
	for (i = 0; i < 4; i++)
		*q++ = 0;
	for (i = 0; i < 4; i++) {
		*q++ = (unsigned char)(v[i] >> 8);
		*q++ = (unsigned char)v[i];
	}
	*q++ = (unsigned char)kind;
	*q++ = (unsigned char)len;
	memcpy(q, data, (size_t)len);
	q += len;
	if (len & 1)
		*q++ = 0;
	return q;
}

static void box_of(const DItem *it, Rect *r)
{
	int h = it->kind == DI_BUTTON ? BTN_H : it->kind == DI_EDIT ? EDIT_H : LINE_H;
	r->left = (short)it->x;
	r->top = (short)it->y;
	r->right = (short)(it->x + it->w);
	r->bottom = (short)(it->y + (it->kind == DI_TEXT && it->lines > 1 ? it->lines * LINE_H : h));
}

static void *handle_of(void *dlg, int item)
{
	short kind;
	void *h;
	Rect box;
	GetDItem(dlg, item, &kind, &h, &box);
	return h;
}

/* the radio buttons that share it's value show which one is chosen */
static void radios(void *dlg, const DItem *it, int n)
{
	int k;
	for (k = 0; k < n; k++)
		if (it[k].kind == DI_RADIO)
			SetCtlValue(handle_of(dlg, k + 1), *it[k].value == it[k].ord);
}

void mac_alert_update(void *event);          /* riptide mm_app.c: a window behind a dialog drawn */
#define UPDATE_MASK 0x40                    /* updateMask: update events only */

void qdlg_settle(void)
{
	EventRecord ev;
	while (EventAvail(UPDATE_MASK, &ev) && WaitNextEvent(UPDATE_MASK, &ev, 0, NULL))
		mac_alert_update(&ev);
}

int qdlg_run(int w, int h, DItem *it, int n, int def, int cancel)
{
	unsigned char *q, title[2] = { 0, 0 };
	void **items, *dlg, *wm, *save;
	Rect r, b;
	long size = 2 + 16;
	int k, hit, first_edit = 0;
	/* the item list: the caller's items 1..n, then the default button's outline */
	for (k = 0; k < n; k++)
		size += 14 + 256;
	if (!(items = mac_newhandle(size)))
		return cancel;
	q = (unsigned char *)*items;
	*q++ = (unsigned char)((n + (def >= 0) - 1) >> 8);
	*q++ = (unsigned char)(n + (def >= 0) - 1);
	for (k = 0; k < n; k++) {
		char t[256];
		int kind, len;
		box_of(&it[k], &r);
		switch (it[k].kind) {
		case DI_BUTTON: kind = K_BUTTON; break;
		case DI_CHECK: kind = K_CHECK; break;
		case DI_RADIO: kind = K_RADIO; break;
		case DI_EDIT: kind = K_EDIT; break;
		default: kind = K_TEXT | K_OFF; break;
		}
		if (it[k].kind == DI_EDIT) {
			strncpy(t, it[k].text ? it[k].text : "", 255);
			if (!first_edit)
				first_edit = k + 1;
		} else
			strncpy(t, !it[k].label ? "" : it[k].raw ? it[k].label : tr(it[k].label), 255);
		t[255] = 0;
		if (it[k].kind != DI_EDIT && !it[k].raw) {
			char *u;
			/* the Amiga's keyboard markers go; "..." becomes the ellipsis (Riptide's rules for labels) */
			mm_hig_label(t, sizeof t);
			while ((u = strchr(t, '\n')))
				*u = '\r';
		}
		len = (int)strlen(t);
		q = ditl_item(q, &r, kind, t, len);
	}
	if (def >= 0) {
		box_of(&it[def], &b);
		InsetRect(&b, -4, -4);
		q = ditl_item(q, &b, K_USER | K_OFF, "", 0);
	}
	/* a third of the way down the main screen, under the menu bar (the alert position) */
	GetPort(&save);
	GetWMgrPort(&wm);
	{
		Rect *pr = PORT_RECT(wm);
		int avail = pr->bottom - LM_MBARHEIGHT - h;
		r.left = (short)((pr->right - pr->left - w) / 2);
		r.right = (short)(r.left + w);
		r.top = (short)(LM_MBARHEIGHT + (avail > 0 ? avail / 3 : 0));
		r.bottom = (short)(r.top + h);
	}
	dlg = NewDialog(NULL, &r, title, 1, 1 /* dBoxProc: a modal dialog box */, (void *)-1L, 0, 0, items);
	if (!dlg) {
		SetPort(save);
		return cancel;
	}
	SetPort(dlg);
	for (k = 0; k < n; k++)
		if (it[k].kind == DI_CHECK)
			SetCtlValue(handle_of(dlg, k + 1), *it[k].value != 0);
		else if (it[k].kind == DI_BUTTON && it[k].off)
			HiliteControl(handle_of(dlg, k + 1), 255);
	radios(dlg, it, n);
	if (first_edit)
		SelIText(dlg, first_edit, 0, 32767);
	InitCursor();
	for (;;) {
		hit = mac_dialog_modal(dlg, def + 1, cancel + 1, def >= 0 ? n + 1 : 0) - 1;
		if (hit < 0 || hit >= n)
			continue;
		if (it[hit].kind == DI_CHECK) {
			*it[hit].value = !*it[hit].value;
			SetCtlValue(handle_of(dlg, hit + 1), *it[hit].value);
		} else if (it[hit].kind == DI_RADIO) {
			*it[hit].value = it[hit].ord;
			radios(dlg, it, n);
		} else if (it[hit].kind == DI_BUTTON)
			break;
	}
	/* the text fields' words back to the caller (Mac Roman) */
	for (k = 0; k < n; k++)
		if (it[k].kind == DI_EDIT && it[k].text && hit != cancel) {
			unsigned char s[256];
			GetIText(handle_of(dlg, k + 1), s);
			if (s[0] >= it[k].cap)
				s[0] = (unsigned char)(it[k].cap - 1);
			memcpy(it[k].text, s + 1, s[0]);
			it[k].text[s[0]] = 0;
		}
	DisposDialog(dlg);
	SetPort(save);
	qdlg_settle();
	return hit;
}

int qdlg_room(void)
{
	void *wm;
	GetWMgrPort(&wm);
	return PORT_RECT(wm)->bottom - PORT_RECT(wm)->top - LM_MBARHEIGHT - 8;
}

int qdlg_number(const char *s, int lo, int hi, int *out)
{
	char *e;
	long v = strtol(s, &e, 10);
	while (*e == ' ')
		e++;
	if (e == s || *e || v < lo || v > hi)
		return 0;
	*out = (int)v;
	return 1;
}
