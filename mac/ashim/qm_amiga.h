/*
 * mac/ashim/qm_amiga.h - the Amiga's system headers as Quill's Amiga front end includes them
 * (<proto/graphics.h>, <libraries/mui.h> ...), for the experimental ribbon tree of Octavo, which
 * compiles amiga/ribbon.c, theme.c, panes.c and backstage.c for the Mac unchanged against
 * Riptide's MUI for the Mac (riptide src/mac/mui). Every file in mac/ashim/ includes this one.
 * GPL-2.0-only, as Quill.
 *
 * What is here: Riptide's own MUI subset (prefs/mui.h, os/intuition.h, the generated calls) and
 * what Quill uses beyond it: more MUI numbers, graphics.library's TextExtent, TextFit and
 * DrawEllipse, the intuition and utility calls of the classes, and the vbcc register syntax of
 * the dispatchers (__reg) with a thunk that turns BOOPSI's registers into C arguments
 * (mac/qm_ashim.c, mac/qm_thunks.S). docs/mac.md, "The experimental ribbon tree", has the list
 * of what compiled unchanged and what needed these.
 */
#ifndef QM_ASHIM_AMIGA_H
#define QM_ASHIM_AMIGA_H

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define GfxBase qm_GfxBase      /* (before Riptide's headers: their GfxBase is a struct Library) */
#include "mm.h"                 /* Riptide: prefs/mui.h, os/intuition.h, the Toolbox */
#include "gen/exec_calls.h"
#include "gen/dos_calls.h"
#include "gen/intuition_calls.h"
#include "gen/graphics_calls.h"

/* ---- exec, dos, utility ---- */
#define NT_FONT          12
#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif
extern struct Library *DiskfontBase, *UtilityBase, *KeymapBase, *LayersBase, *AslBase;
LONG  Stricmp(CONST_STRPTR a, CONST_STRPTR b);
LONG  Strnicmp(CONST_STRPTR a, CONST_STRPTR b, LONG n);
STRPTR FilePart(CONST_STRPTR path);
STRPTR PathPart(CONST_STRPTR path);
BOOL  AddPart(STRPTR dir, CONST_STRPTR file, ULONG size);
struct TagItem *NextTagItem(struct TagItem **tp);
ULONG GetTagData(ULONG tag, ULONG def, const struct TagItem *tl);
struct TagItem *FindTagItem(ULONG tag, const struct TagItem *tl);
BOOL  SetIoErr(LONG code);
#ifndef TAG_IGNORE
#define TAG_IGNORE 1UL
#endif
#ifndef TAG_MORE
#define TAG_MORE 2UL
#endif
#ifndef TAG_SKIP
#define TAG_SKIP 3UL
#endif

/* ---- graphics ---- */
/* graphics.library's base: only the default font is read (theme.c's last fallback) */
struct GfxBase { struct Library LibNode; struct TextFont *DefaultFont; };
extern struct GfxBase *qm_GfxBase;
LONG  ObtainBestPen(struct ColorMap *cm, ULONG r, ULONG g, ULONG b, ULONG tag1, ...);
void  ExtendFont(struct TextFont *tf, const struct TagItem *tags);
void  StripFont(struct TextFont *tf);
#define SetRast(rp, pen) ((void)(rp), (void)(pen))
/* the font files theme.c reads itself on the Amiga (the Mac has none: OpenDiskFont gives Mac fonts) */
#define DFH_ID  0x0F80
#define FCH_ID  0x0F00
#define MAXFONTPATH 256
struct DiskFontHeader {
	struct Node dfh_DF;
	UWORD  dfh_FileID;
	UWORD  dfh_Revision;
	LONG   dfh_Segment;
	char   dfh_Name[32];
	struct TextFont dfh_TF;
};
struct FontContents { char fc_FileName[MAXFONTPATH]; UWORD fc_YSize; UBYTE fc_Style; UBYTE fc_Flags; };
struct FontContentsHeader { UWORD fch_FileID; UWORD fch_NumEntries; };

/* ---- intuition, input ---- */
#define SELECTDOWN   0x68
#define SELECTUP     0xE8
#define MENUDOWN     0x69
#define MENUUP       0xE9
#define IDCMP_MOUSEBUTTONS 0x00000008UL
#define IDCMP_MOUSEMOVE    0x00000010UL
#define IDCMP_RAWKEY       0x00000400UL
#define IDCMP_VANILLAKEY   0x00200000UL
#define IDCMP_INTUITICKS   0x00400000UL
#define IECLASS_RAWKEY     0x01
#define IEQUALIFIER_RCOMMAND 0x0080
#define IEQUALIFIER_RALT     0x0020
#define IEQUALIFIER_CAPSLOCK 0x0004
#define IEQUALIFIER_NUMERICPAD 0x0100
#define IEQUALIFIER_LEFTBUTTON 0x4000
#define IEQUALIFIER_REPEAT   0x0200
struct InputEvent {
	struct InputEvent *ie_NextEvent;
	UBYTE  ie_Class, ie_SubClass;
	UWORD  ie_Code, ie_Qualifier;
	APTR   ie_EventAddress;
	ULONG  ie_Seconds, ie_Micros;
};
LONG  MapRawKey(const struct InputEvent *ie, STRPTR buf, LONG len, APTR keymap);
BOOL  DoubleClick(ULONG s1, ULONG m1, ULONG s2, ULONG m2);
void  CurrentTime(ULONG *secs, ULONG *micros);
void  DisplayBeep(struct Screen *s);

/* ---- BOOPSI and amiga.lib's varargs calls (mac/qm_ashim.c) ---- */
ULONG DoMethod(Object *obj, ULONG id, ...);
ULONG DoSuperMethod(struct IClass *cl, Object *obj, ULONG id, ...);
ULONG CoerceMethod(struct IClass *cl, Object *obj, ULONG id, ...);
APTR  NewObject(struct IClass *cl, CONST_STRPTR id, ULONG tag1, ...);
ULONG SetAttrs(APTR obj, ULONG tag1, ...);
APTR  MUI_NewObject(CONST_STRPTR cls, ULONG tag1, ...);
APTR  MUI_MakeObject(LONG type, ...);
LONG  MUI_Request(APTR app, APTR win, ULONG flags, CONST_STRPTR title, CONST_STRPTR gadgets, CONST_STRPTR format, ...);
#define get(obj, attr, store) GetAttr((ULONG)(attr), (APTR)(obj), (ULONG *)(store))
#define set(obj, attr, value) SetAttrs((APTR)(obj), (ULONG)(attr), (ULONG)(value), TAG_DONE)
#define nnset(obj, attr, value) SetAttrs((APTR)(obj), MUIA_NoNotify, TRUE, (ULONG)(attr), (ULONG)(value), TAG_DONE)
/* MUI_CreateCustomClass: the dispatcher is a C function here (vbcc's __reg is gone), so the class
   is made with a thunk that passes BOOPSI's registers to it as arguments (mac/qm_ashim.c) */
struct MUI_CustomClass *qm_create_class(struct Library *base, CONST_STRPTR super, struct MUI_CustomClass *supermcc,
                                        LONG datasize, APTR dispatcher);
#define MUI_CreateCustomClass(b, s, m, n, f) qm_create_class((b), (s), (m), (n), (APTR)(f))
APTR  MUI_AddClipping(struct MUI_RenderInfo *mri, WORD l, WORD t, WORD w, WORD h);
void  MUI_RemoveClipping(struct MUI_RenderInfo *mri, APTR h);
APTR  MUI_AddClipRegion(struct MUI_RenderInfo *mri, APTR region);

/* ---- MUI: what Quill uses beyond Riptide's numbers (MUI's own values where known; on the Mac only
   their being distinct matters, as Riptide's classes ignore what they do not know) ---- */
#define MUIC_Menustrip  "Menustrip.mui"
#define MUIC_Menu       "Menu.mui"
#define MUIC_Menuitem   "Menuitem.mui"
#define MUIC_Scrollbar  "Scrollbar.mui"
#define MUIC_Prop       "Prop.mui"
#define MUIC_Virtgroup  "Virtgroup.mui"
#define MUIC_Scrollgroup "Scrollgroup.mui"
#define MUIC_Register   "Register.mui"
#define MUIC_Slider     "Slider.mui"
#define MUIC_Popstring  "Popstring.mui"
#define MUIC_Gauge      "Gauge.mui"
#define MUIA_Window_CloseGadget   0x8042a110UL
#define MUIA_Window_DepthGadget   0x80421923UL
#define MUIA_Window_SizeGadget    0x8042e33dUL
#define MUIA_Window_DragBar       0x8042045dUL
#define MUIA_Window_Backdrop      0x8042c0bbUL
#define MUIA_Window_NoMenus       0x80429df5UL
#define MUIA_Window_UseRightBorderScroller 0x8042c7d7UL
#define MUIA_Window_UseBottomBorderScroller 0x80424e79UL
#define MUIA_Window_DefaultObject 0x804294d7UL
#define MUIA_Window_Width_Default 0
#define MUIA_Family_Child         0x8042c696UL
#define MUIM_Family_AddTail       0x8042d752UL
#define MUIM_Family_Remove        0x8042f8a9UL
#define MUIA_Menu_Title           0x8042a0e3UL
#define MUIA_Menu_Enabled         0x8042ed48UL
#define MUIA_Menuitem_Title       0x804218beUL
#define MUIA_Menuitem_Shortcut    0x80422030UL
#define MUIA_Menuitem_Checkit     0x80425aceUL
#define MUIA_ContextMenu          0x8042b704UL
#define MUIA_ContextMenuTrigger   0x8042a2c1UL
#define MUIM_ContextMenuBuild     0x80429d2eUL
#define MUIM_ContextMenuChoice    0x80420f0eUL
struct MUIP_ContextMenuBuild { ULONG MethodID; LONG mx; LONG my; };
struct MUIP_ContextMenuChoice { ULONG MethodID; Object *item; };
#define MUIA_Prop_First           0x8042d4b2UL
#define MUIA_Prop_Entries         0x8042fbdbUL
#define MUIA_Prop_Visible         0x8042fea6UL
#define MUIA_Prop_Horiz           0x8042f4f3UL
#define MUIA_Font                 0x8042be50UL
#define MUIA_Weight               0x80421d1fUL
#define MUIA_Group_SameWidth      0x8042b3ecUL
#define MUIA_Group_SameHeight     0x8037e5bcUL
#define MUIA_Text_Contents        0x8042f8dcUL
#define MUIA_Text_PreParse        0x8042566dUL
#define MUIA_Text_SetMin          0x80424e10UL
#define MUIA_DoubleBuffer         0x8042a9c7UL
#define MUIA_KeepFocus            0
#define MUIV_Font_Normal          (-1L)
#define MUIV_Font_Tiny            (-2L)
#define MUIV_Window_Width_Default 0
#define MUII_WindowBack           0
#define MUII_BACKGROUND           0
#define MUIV_InputMode_None       0
#define MUIV_InputMode_Immediate  2
#define MUIV_Frame_Text           3
#define MUIV_Frame_Group          9
#define MUIV_Frame_Virtual        12
#define MADF_DRAWALL              MADF_DRAWOBJECT
#define IDCMP_ACTIVEWINDOW        0x00040000UL
#define IDCMP_INACTIVEWINDOW      0x00080000UL
#define InnerSpacing(x, y) MUIA_InnerLeft, (x), MUIA_InnerRight, (x), MUIA_InnerTop, (y), MUIA_InnerBottom, (y)
/* the object macros (MUI's libraries/mui.h: each a MUI_NewObject call with its tags up to End) */
#define End               TAG_DONE)
#define Child             MUIA_Group_Child
#define WindowContents    MUIA_Window_RootObject
#define MenuChild         MUIA_Family_Child
#define ApplicationObject MUI_NewObject(MUIC_Application
#define WindowObject      MUI_NewObject(MUIC_Window
#define GroupObject       MUI_NewObject(MUIC_Group
#define VGroup            MUI_NewObject(MUIC_Group
#define HGroup            MUI_NewObject(MUIC_Group, MUIA_Group_Horiz, TRUE
#define RectangleObject   MUI_NewObject(MUIC_Rectangle
#define TextObject        MUI_NewObject(MUIC_Text
#define StringObject      MUI_NewObject(MUIC_String
#define ListObject        MUI_NewObject(MUIC_List
#define ListviewObject    MUI_NewObject(MUIC_Listview
#define CycleObject       MUI_NewObject(MUIC_Cycle
#define MenustripObject   MUI_NewObject(MUIC_Menustrip
#define MenuObject        MUI_NewObject(MUIC_Menu
#define MenuitemObject    MUI_NewObject(MUIC_Menuitem
#define HVSpace           MUI_NewObject(MUIC_Rectangle, TAG_DONE)
#define HSpace(x)         MUI_MakeObject(30 /* MUIO_HSpace */, (ULONG)(x))
#define VSpace(x)         MUI_MakeObject(31 /* MUIO_VSpace */, (ULONG)(x))

/* Quill's theme (amiga/theme.h) beside Riptide's (src/amiga/prefs/mui_theme.c, which Riptide's MUI
   for the Mac draws its own classes with): the same names, so Quill's are called qt_ here */
#define theme_aatext             qt_theme_aatext
#define theme_big                qt_theme_big
#define theme_bold               qt_theme_bold
#define theme_bubble             qt_theme_bubble
#define theme_cleanup            qt_theme_cleanup
#define theme_close_font         qt_theme_close_font
#define theme_dialog_back        qt_theme_dialog_back
#define theme_dialog_font        qt_theme_dialog_font
#define theme_fill               qt_theme_fill
#define theme_font               qt_theme_font
#define theme_fonts_ok           qt_theme_fonts_ok
#define theme_fonts_why          qt_theme_fonts_why
#define theme_free_fonts         qt_theme_free_fonts
#define theme_handler            qt_theme_handler
#define theme_icon               qt_theme_icon
#define theme_icon_pen           qt_theme_icon_pen
#define theme_icon_src           qt_theme_icon_src
#define theme_is_dark            qt_theme_is_dark
#define theme_no_aatext          qt_theme_no_aatext
#define theme_no_hinting         qt_theme_no_hinting
#define theme_open_face          qt_theme_open_face
#define theme_open_font          qt_theme_open_font
#define theme_preview            qt_theme_preview
#define theme_preview_width      qt_theme_preview_width
#define theme_rect               qt_theme_rect
#define theme_set_dark           qt_theme_set_dark
#define theme_set_large          qt_theme_set_large
#define theme_setup              qt_theme_setup
#define theme_small              qt_theme_small
#define theme_text               qt_theme_text
#define theme_textlen            qt_theme_textlen
#define tpen                     qt_tpen

/* ribbon.c dims a command the Mac does not have yet (mac/qm_ribbon.c) */
int qr_can(unsigned long amiga_id);
#define RIBBON_CAN(id) qr_can(id)

/* vbcc's register arguments: a plain C function on the Mac, reached through qm_create_class's thunk */
#define __reg(r)
#define __saveds
#define __asm
#define __stdargs
#define __far
#define __chip

#endif
