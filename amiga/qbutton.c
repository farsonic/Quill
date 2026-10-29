/*
 * Quill - the dialogs' buttons, flat as the ribbon's: a face in the input colour with a thin
 * border, the accent's tint while pressed, the label in the dialog's font with its key
 * underlined. And, in the dark theme, the dialogs' labels and section titles: MUI draws its own
 * in its text pen (black), which can't be read on the dark background.
 */
#include <string.h>
#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <libraries/mui.h>
#include <clib/alib_protos.h>
#include "quill.h"
#include "theme.h"
#include "lang.h"

#define MUIA_QButton_Label (QTAG + 0x700)
#define MUIA_QLabel_Flags  (QTAG + 0x701)        /* MUIO_Label_ flags, and QL_BOLD */
#define QL_BOLD 0x10000
struct QBData { char label[64]; LONG key; };     /* key: the underlined character's place, or -1 */
static struct MUI_CustomClass *qcls;

/* the button class's dispatcher: every method sent to a QButton arrives here (BOOPSI passes class,
   object and message in a0, a2 and a1). OM_NEW strips the '_' from the label, remembers which
   letter it marked, and asks Area for a RelVerify button (it acts when released over it) in the
   cycle chain with that letter as MUIA_ControlChar, so the key presses it. MUIM_AskMinMax tells MUI
   the button's smallest, usual and largest size: Area adds its frame and spacing first, then the
   label's room goes on top. MUIM_Draw paints it, after Area has drawn its background. Everything
   else goes on to Area */
static ULONG qdispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct QBData *d;
  switch (msg->MethodID) {
  case OM_NEW: {
    struct opSet *m = (struct opSet *)msg;
    const char *l = (const char *)GetTagData(MUIA_QButton_Label, (ULONG)"", m->ops_AttrList);
    struct TagItem more[] = { { MUIA_InputMode, MUIV_InputMode_RelVerify }, { MUIA_CycleChain, 1 }, { MUIA_ControlChar, 0 },
                              { TAG_MORE, (ULONG)m->ops_AttrList } };
    char lab[64];
    LONG key = -1, k = 0;
    const char *p;
    for (p = l; *p && k < 63; p++) {
      if (*p == '_' && p[1] && key < 0) { key = k; continue; }
      lab[k++] = *p;
    }
    lab[k] = 0;
    if (key >= 0) more[2].ti_Data = (ULONG)ToLower((ULONG)(UBYTE)lab[key]);
    else more[2].ti_Tag = TAG_IGNORE;
    {
      struct opSet sup = { OM_NEW, more, m->ops_GInfo };
      if (!(obj = (Object *)DoSuperMethodA(cl, obj, (Msg)&sup))) return 0;
    }
    d = INST_DATA(cl, obj);
    strcpy(d->label, lab);
    d->key = key;
    return (ULONG)obj;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    struct RastPort trp;
    struct TextFont *tf = _font(obj) ? _font(obj) : theme_font();
    LONG w;
    DoSuperMethodA(cl, obj, msg);
    d = INST_DATA(cl, obj);
    InitRastPort(&trp);
    SetFont(&trp, tf);
    w = TextLength(&trp, (STRPTR)d->label, (ULONG)strlen(d->label)) + 24;
    m->MinMaxInfo->MinWidth += w; m->MinMaxInfo->DefWidth += w; m->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    m->MinMaxInfo->MinHeight += tf->tf_YSize + 8; m->MinMaxInfo->DefHeight += tf->tf_YSize + 8; m->MinMaxInfo->MaxHeight += tf->tf_YSize + 8;
    return 0;
  }
  case MUIM_Draw: {
    struct RastPort *rp = _rp(obj);
    struct TextFont *tf = _font(obj) ? _font(obj) : theme_font();
    ULONG sel = 0;
    LONG tw, x, y;
    DoSuperMethodA(cl, obj, msg);
    get(obj, MUIA_Selected, &sel);
    if (!(((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) return 0;
    d = INST_DATA(cl, obj);
    theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(sel ? T_CHECKED : T_WHITE));
    theme_rect(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(sel ? T_ACCENT : T_BORDER2));
    SetFont(rp, tf);
    SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    SetDrMd(rp, JAM1);
    SetAPen(rp, tpen(T_TEXT));
    tw = TextLength(rp, (STRPTR)d->label, (ULONG)strlen(d->label));
    x = _mleft(obj) + (_mwidth(obj) - tw) / 2;
    y = _mtop(obj) + (_mheight(obj) - tf->tf_YSize) / 2 + tf->tf_Baseline;
    Move(rp, x, y);
    Text(rp, (STRPTR)d->label, (ULONG)strlen(d->label));
    if (d->key >= 0) {
      LONG ux = x + TextLength(rp, (STRPTR)d->label, (ULONG)d->key), uw = TextLength(rp, (STRPTR)d->label + d->key, 1);
      Move(rp, ux, y + 1); Draw(rp, ux + uw - 1, y + 1);
    }
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
/* the dark theme's labels: right (or left) aligned text, as tall as the gadget beside it */
struct QLData { char label[100]; LONG key; ULONG flags; };
static struct MUI_CustomClass *lcls;
/* the label class's dispatcher: as qdispatch, without the input. A label is as wide as its text
   (a bold section title may stretch) and, given MUIO_Label_SingleFrame or DoubleFrame, as tall as
   a framed gadget's text, so it sits on the same baseline as the gadget beside it */
static ULONG ldispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, __reg("a1") Msg msg)
{
  struct QLData *d;
  switch (msg->MethodID) {
  case OM_NEW: {
    struct opSet *m = (struct opSet *)msg;
    const char *l = (const char *)GetTagData(MUIA_QButton_Label, (ULONG)"", m->ops_AttrList);
    LONG k = 0, key = -1;
    const char *p;
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg))) return 0;
    d = INST_DATA(cl, obj);
    for (p = l; *p && k < 99; p++) {
      if (*p == '_' && p[1] && key < 0) { key = k; continue; }
      d->label[k++] = *p;
    }
    d->label[k] = 0;
    d->key = key;
    d->flags = GetTagData(MUIA_QLabel_Flags, 0, m->ops_AttrList);
    return (ULONG)obj;
  }
  case MUIM_AskMinMax: {
    struct MUIP_AskMinMax *m = (struct MUIP_AskMinMax *)msg;
    struct RastPort trp;
    struct TextFont *tf = _font(obj) ? _font(obj) : theme_font();
    LONG w, h;
    DoSuperMethodA(cl, obj, msg);
    d = INST_DATA(cl, obj);
    InitRastPort(&trp);
    SetFont(&trp, tf);
    SetSoftStyle(&trp, d->flags & QL_BOLD ? FSF_BOLD : 0, FSF_BOLD);
    w = TextLength(&trp, (STRPTR)d->label, (ULONG)strlen(d->label)) + 2;
    /* as tall as a framed gadget's text, so the words line up with it */
    h = tf->tf_YSize + ((d->flags & MUIO_Label_DoubleFrame) ? 6 : (d->flags & MUIO_Label_SingleFrame) ? 4 : 0);
    m->MinMaxInfo->MinWidth += w; m->MinMaxInfo->DefWidth += w; m->MinMaxInfo->MaxWidth += (d->flags & QL_BOLD) ? MUI_MAXMAX : w;
    m->MinMaxInfo->MinHeight += h; m->MinMaxInfo->DefHeight += h; m->MinMaxInfo->MaxHeight += h;
    return 0;
  }
  case MUIM_Draw: {
    struct RastPort *rp = _rp(obj);
    struct TextFont *tf = _font(obj) ? _font(obj) : theme_font();
    LONG tw, x, y;
    DoSuperMethodA(cl, obj, msg);
    if (!(((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) return 0;
    d = INST_DATA(cl, obj);
    SetFont(rp, tf);
    SetSoftStyle(rp, d->flags & QL_BOLD ? FSF_BOLD : 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    SetDrMd(rp, JAM1);
    SetAPen(rp, tpen(T_TEXT));
    tw = TextLength(rp, (STRPTR)d->label, (ULONG)strlen(d->label));
    x = (d->flags & (MUIO_Label_LeftAligned | QL_BOLD)) ? _mleft(obj) : _mright(obj) - tw;
    y = _mtop(obj) + (_mheight(obj) - tf->tf_YSize) / 2 + tf->tf_Baseline;
    Move(rp, x, y);
    Text(rp, (STRPTR)d->label, (ULONG)strlen(d->label));
    if (d->key >= 0) {
      LONG ux = x + TextLength(rp, (STRPTR)d->label, (ULONG)d->key), uw = TextLength(rp, (STRPTR)d->label + d->key, 1);
      Move(rp, ux, y + 1); Draw(rp, ux + uw - 1, y + 1);
    }
    SetSoftStyle(rp, 0, FSF_BOLD);
    return 0;
  }
  }
  return DoSuperMethodA(cl, obj, msg);
}
/* both classes made on first call (subclasses of Area); returns the button class, NULL if MUI
   couldn't make it. The label class is lcls, which may be NULL even when the button class isn't */
struct MUI_CustomClass *qbutton_class(void)
{
  if (!qcls) qcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct QBData), (APTR)qdispatch);
  if (!lcls) lcls = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, sizeof(struct QLData), (APTR)ldispatch);
  return qcls;
}
/* at the end, once every object of these classes has been disposed of */
void qbutton_class_free(void)
{
  if (qcls) { MUI_DeleteCustomClass(qcls); qcls = NULL; }
  if (lcls) { MUI_DeleteCustomClass(lcls); lcls = NULL; }
}
/* a dialog button with a label ("_OK": O is its key); MUI's plain button if the class couldn't be
   made */
Object *QButton(const char *label)
{
  if (!qbutton_class()) return SimpleButton(label);
  return (Object *)NewObject(qcls->mcc_Class, NULL, MUIA_QButton_Label, (ULONG)label, TAG_DONE);
}
/* a label (MUIO_Label flags): MUI's own in the light theme, Quill's in the dark one */
Object *QLabel(const char *label, ULONG flags)
{
  if (!theme_is_dark() || (qbutton_class(), !lcls)) return MUI_MakeObject(MUIO_Label, (ULONG)label, flags);
  return (Object *)NewObject(lcls->mcc_Class, NULL, MUIA_QButton_Label, (ULONG)label, MUIA_QLabel_Flags, flags, TAG_DONE);
}
/* a dialog section's title, in bold (in the light theme an MUI Text object, ESC b being MUI's
   text code for bold) */
Object *QTitleObject(const char *t)
{
  char b[100];
  if (theme_is_dark() && (qbutton_class(), lcls))
    return (Object *)NewObject(lcls->mcc_Class, NULL, MUIA_QButton_Label, (ULONG)t, MUIA_QLabel_Flags, QL_BOLD, TAG_DONE);
  b[0] = 27; b[1] = 'b'; strncpy(b + 2, t, sizeof b - 3); b[sizeof b - 1] = 0;
  return TextObject, MUIA_Text_Contents, (ULONG)b, MUIA_Text_SetVMax, TRUE, End;
}
