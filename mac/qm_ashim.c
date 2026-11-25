/*
 * mac/qm_ashim.c - what Quill's Amiga front-end files (amiga/ribbon.c, theme.c, panes.c,
 * backstage.c, wphooks.c and the work packages' ribbon tables) call that Riptide's MUI for the Mac
 * does not have: amiga.lib's varargs calls, utility.library's tag and string calls, a few dos
 * calls, MapRawKey, DoubleClick, clipping, and the dispatcher thunk that lets a class written for
 * vbcc's registers run on the Mac. The experimental ribbon tree only (docs/mac.md, "The
 * experimental ribbon tree"). GPL-2.0-only, as Quill.
 *
 * One thing is not a plain stand-in: DoMethod(app, MUIM_Application_ReturnID, id) from those files
 * adds QM_AMIGA_ID to the id, so Octavo's main loop tells the Amiga's command numbers (ids.h) from
 * its own menus' (mac/quill_mac.h); mac/qm_ribbon.c maps them.
 */
#include <stdarg.h>
#include "qm_amiga.h"
#include "wphooks.h"
#include "lang.h"
#include "qm_ribbon.h"

/* ---- bases: present (theme.c asks DiskfontBase before OpenDiskFont) ---- */
static struct Library lib_dummy;
struct Library *DiskfontBase = &lib_dummy, *UtilityBase = &lib_dummy, *KeymapBase = &lib_dummy, *LayersBase = &lib_dummy,
               *AslBase = &lib_dummy;
static struct GfxBase gfx_base;
struct GfxBase *qm_GfxBase = &gfx_base;
void qm_ashim_init(void)
{
	gfx_base.DefaultFont = &mm_font;
}
/* main.c's on the Amiga: the public screen (none: the Mac's one) and the log (none) */
char pub_screen[64];
FILE *qlog;

/* ---- BOOPSI: amiga.lib's varargs forms (the arguments lie on the stack as a tag list does) ---- */
__attribute__((noinline)) ULONG DoMethod(Object *obj, ULONG id, ...)
{
	if (!obj)
		return 0;
	if (id == MUIM_Application_ReturnID) {
		ULONG m[2];
		va_list ap;
		va_start(ap, id);
		m[0] = id;
		m[1] = va_arg(ap, ULONG) + QM_AMIGA_ID;
		va_end(ap);
		return DoMethodA(obj, (Msg)m);
	}
	return DoMethodA(obj, (Msg)&id);
}
__attribute__((noinline)) ULONG DoSuperMethod(struct IClass *cl, Object *obj, ULONG id, ...)
{
	return DoSuperMethodA(cl, obj, (Msg)&id);
}
__attribute__((noinline)) ULONG CoerceMethod(struct IClass *cl, Object *obj, ULONG id, ...)
{
	return CoerceMethodA(cl, obj, (Msg)&id);
}
__attribute__((noinline)) APTR NewObject(struct IClass *cl, CONST_STRPTR id, ULONG tag1, ...)
{
	return NewObjectA(cl, id, (const struct TagItem *)&tag1);
}
__attribute__((noinline)) ULONG SetAttrs(APTR obj, ULONG tag1, ...)
{
	return obj ? SetAttrsA(obj, (const struct TagItem *)&tag1) : 0;
}
__attribute__((noinline)) APTR MUI_NewObject(CONST_STRPTR cls, ULONG tag1, ...)
{
	return MUI_NewObjectA(cls, (const struct TagItem *)&tag1);
}
__attribute__((noinline)) APTR MUI_MakeObject(LONG type, ...)
{
	return MUI_MakeObjectA(type, (ULONG *)(&type + 1));
}
__attribute__((noinline)) LONG MUI_Request(APTR app, APTR win, ULONG flags, CONST_STRPTR title, CONST_STRPTR gadgets, CONST_STRPTR format, ...)
{
	return MUI_RequestA(app, win, flags, title, gadgets, format, (APTR)(&format + 1));
}
__attribute__((noinline)) LONG ObtainBestPen(struct ColorMap *cm, ULONG r, ULONG g, ULONG b, ULONG tag1, ...)
{
	return ObtainBestPenA(cm, r, g, b, (const struct TagItem *)&tag1);
}

/* A class written for vbcc takes (cl, obj, msg) in A0, A2, A1; here it is a C function. The class
   is made with qm_cdispatch_thunk (mac/qm_thunks.S), which calls the hook's h_SubEntry with the
   three as arguments: the C function, put there now */
extern char qm_cdispatch_thunk[];
struct MUI_CustomClass *qm_create_class(struct Library *base, CONST_STRPTR super, struct MUI_CustomClass *supermcc,
                                        LONG datasize, APTR dispatcher)
{
	struct MUI_CustomClass *mcc = (MUI_CreateCustomClass)(base, super, supermcc, datasize, (APTR)qm_cdispatch_thunk);
	if (mcc)
		mcc->mcc_Class->cl_Dispatcher.h_SubEntry = (ULONG (*)(void))dispatcher;
	return mcc;
}

/* ---- utility.library ---- */
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : (c >= 0xC0 && c <= 0xDE && c != 0xD7) ? c + 32 : c; }
LONG Stricmp(CONST_STRPTR a, CONST_STRPTR b)
{
	const unsigned char *p = (const unsigned char *)a, *q = (const unsigned char *)b;
	for (;; p++, q++) {
		int d = lower(*p) - lower(*q);
		if (d || !*p)
			return d;
	}
}
LONG Strnicmp(CONST_STRPTR a, CONST_STRPTR b, LONG n)
{
	const unsigned char *p = (const unsigned char *)a, *q = (const unsigned char *)b;
	for (; n > 0; n--, p++, q++) {
		int d = lower(*p) - lower(*q);
		if (d || !*p)
			return d;
	}
	return 0;
}
struct TagItem *NextTagItem(struct TagItem **tp)
{
	struct TagItem *t;
	if (!tp || !*tp)
		return NULL;
	for (;;) {
		t = *tp;
		switch (t->ti_Tag) {
		case TAG_DONE:
			*tp = NULL;
			return NULL;
		case TAG_IGNORE:
			*tp = t + 1;
			continue;
		case TAG_MORE:
			*tp = (struct TagItem *)t->ti_Data;
			if (!*tp)
				return NULL;
			continue;
		case TAG_SKIP:
			*tp = t + 1 + t->ti_Data;
			continue;
		default:
			*tp = t + 1;
			return t;
		}
	}
}
struct TagItem *FindTagItem(ULONG tag, const struct TagItem *tl)
{
	struct TagItem *p = (struct TagItem *)tl, *t;
	while ((t = NextTagItem(&p)))
		if (t->ti_Tag == tag)
			return t;
	return NULL;
}
ULONG GetTagData(ULONG tag, ULONG def, const struct TagItem *tl)
{
	struct TagItem *t = FindTagItem(tag, tl);
	return t ? t->ti_Data : def;
}

/* ---- dos ---- */
STRPTR FilePart(CONST_STRPTR path)
{
	const char *p = (const char *)path, *s;
	if (!p)
		return NULL;
	for (s = p; *s; s++)
		if (*s == '/' || *s == ':')
			p = s + 1;
	return (STRPTR)p;
}
STRPTR PathPart(CONST_STRPTR path)
{
	return FilePart(path);
}
static LONG ioerr_set;
BOOL SetIoErr(LONG code)
{
	ioerr_set = code;
	return TRUE;
}
BOOL Fault(LONG code, CONST_STRPTR header, STRPTR buf, LONG len)
{
	if (len > 0)
		snprintf((char *)buf, (size_t)len, "%s%s%s", header ? (const char *)header : "", header ? ": " : "", mm_os_errtext(code));
	return TRUE;
}
BOOL NameFromLock(BPTR lock, STRPTR buf, LONG len)
{
	(void)lock;
	if (len > 0)
		buf[0] = 0;
	return FALSE;
}

/* ---- graphics: the Amiga's own font files are not read on the Mac (OpenDiskFont gives Mac fonts) ---- */
void ExtendFont(struct TextFont *tf, const struct TagItem *tags)
{
	(void)tf;
	(void)tags;
}
void StripFont(struct TextFont *tf)
{
	(void)tf;
}

/* clipping: QuickDraw's clip region of the window, kept and given back (one level is all the
   classes use: the navigation pane's list) */
APTR MUI_AddClipping(struct MUI_RenderInfo *mri, WORD l, WORD t, WORD w, WORD h)
{
	void *port = mri && mri->mri_RastPort ? mm_rp_port(mri->mri_RastPort) : NULL;
	void *old;
	Rect r;
	if (!port)
		return NULL;
	SetPort(port);
	old = NewRgn();
	GetClip(old);
	r.left = l;
	r.top = t;
	r.right = (short)(l + w);
	r.bottom = (short)(t + h);
	ClipRect(&r);
	return old;
}
void MUI_RemoveClipping(struct MUI_RenderInfo *mri, APTR h)
{
	void *port = mri && mri->mri_RastPort ? mm_rp_port(mri->mri_RastPort) : NULL;
	if (!h)
		return;
	if (port) {
		SetPort(port);
		SetClip(h);
	}
	DisposeRgn(h);
}

/* ---- input ---- */
/* Intuition's time stamps (Riptide stamps its messages from TickCount): within the Mac's
   double-click time (DoubleTime, low memory 0x2F0, in ticks) */
BOOL DoubleClick(ULONG s1, ULONG m1, ULONG s2, ULONG m2)
{
	long ticks = (long)((s2 - s1) * 60 + ((long)m2 - (long)m1) / 16667);
	long dbl = *(volatile long *)0x2F0;
	return ticks >= 0 && ticks <= (dbl > 0 && dbl < 120 ? dbl : 30);
}
void CurrentTime(ULONG *secs, ULONG *micros)
{
	unsigned long t = (unsigned long)TickCount();
	if (secs)
		*secs = t / 60;
	if (micros)
		*micros = (t % 60) * 16667;
}
/* the Amiga's raw key codes (Riptide turns the Mac's keys into them) back into characters, as
   the US keymap has them; Latin-1 beyond the keyboard is not typed this way */
LONG MapRawKey(const struct InputEvent *ie, STRPTR buf, LONG len, APTR keymap)
{
	/* raw keys 0x00-0x40: the US keyboard's rows (0: no character), unshifted and shifted */
	static const char lo[0x41] = {
		'`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\\', 0, '0',
		'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, '1', '2', '3',
		'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', 0, 0, '4', '5', '6',
		0, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '.', '7', '8', '9', ' ' };
	static const char hi[0x41] = {
		'~', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '|', 0, '0',
		'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0, '1', '2', '3',
		'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', 0, 0, '4', '5', '6',
		0, 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '.', '7', '8', '9', ' ' };
	UWORD c = ie->ie_Code;
	char ch;
	(void)keymap;
	if (len < 1 || (c & IECODE_UP_PREFIX) || c > 0x40)
		return 0;
	ch = (ie->ie_Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? hi[c] : lo[c];
	if (!ch)
		return 0;
	buf[0] = (UBYTE)ch;
	return 1;
}
void DisplayBeep(struct Screen *s)
{
	(void)s;
	SysBeep(5);
}

/* ---- amiga/lang.c's beyond tr(): the codes and each language's name in itself ---- */
const char *const ui_languages[UI_NLANG] = { "English", "Deutsch", "Fran\215ais", "Espa\226ol", "Italiano" };   /* (Mac Roman) */
const char *lang_code(int lang)
{
	static const char *const codes[UI_NLANG] = { "en", "de", "fr", "es", "it" };
	return codes[lang >= 0 && lang < UI_NLANG ? lang : 0];
}

/* ---- the work packages whose Amiga code the Mac does not have (customizing, Tell Me, the Developer
   tab, personal templates, AutoSave, the Options window): what ribbon.c, panes.c and backstage.c ask
   them, answered as a fresh Quill with nothing customized would ---- */
int wp26_qat(ULONG *ids, int max)
{
	static const ULONG q[] = { ID_SAVE, ID_UNDO, ID_REDO };
	int i;
	for (i = 0; i < 3 && i < max; i++)
		ids[i] = q[i];
	return i;
}
int wp26_tellme_box(void) { return 0; }
void wp26_tellme_open(LONG x, LONG y) { (void)x; (void)y; }
int wp26_tab_hidden(const char *tab) { (void)tab; return 0; }
int wp26_group_hidden(const char *tab, const char *group) { (void)tab; (void)group; return 0; }
int wp26_custom_tab(const char **name, const Group **groups) { (void)name; (void)groups; return 0; }
const Item *ribbon_item_of(ULONG id);
const char *wp26_cmd_name(ULONG id)
{
	const Item *it = ribbon_item_of(id);
	return it && it->label ? it->label : "";
}
int wp16_devtab_on(void) { return 0; }
void wp16_devtab_set(int on) { (void)on; }
int wp23_personal(int i, char *name, size_t cap) { (void)i; if (cap) name[0] = 0; return 0; }
int wp24_autosave_on(void) { return 0; }
void wp24_autosave_set(int on) { (void)on; }
void wp_options_open(int pane) { (void)pane; }
/* (WP31) the caret's proofing language: the Mac checks British English only (docs/mac.md) */
const char *wp31_status_lang(void) { return tr("English (United Kingdom)"); }
/* Riptide's FindPort knows the TCP/IP stack's port (RiptidePrefs has it); Octavo has none, and
   theme.c only asks for AAText's */
struct MsgPort *mm_riptide_port(void) { return NULL; }
