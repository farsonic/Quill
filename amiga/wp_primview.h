/*
 * wp_primview.h - a picture of a chart or SmartArt in a dialog (WP15b's Insert Chart and WP15c's Insert
 * SmartArt previews): a MUI area that lays out what it is given (core/chart.h) at its own size and
 * paints the primitives. Shared by amiga/wp_chart.c and amiga/wp_smartart.c (WP15b and WP15c).
 */
#ifndef QUILL_WP_PRIMVIEW_H
#define QUILL_WP_PRIMVIEW_H

#include "wphooks.h"
#include "../core/chartedit.h"

#define MUIA_PrimView_Chart (TAG_USER | 0x515B0001UL)   /* Chart *: shown, the view's own from then on (NULL none) */
#define MUIA_PrimView_Art   (TAG_USER | 0x515B0002UL)   /* SAModel *: shown, the view's own from then on */
#define MUIA_PrimView_Text  (TAG_USER | 0x515B0003UL)   /* const char *: shown when there is nothing else (copied) */

/* a new view at least minw x minh pixels (NULL if the class can't be made) */
Object *primview_new(int minw, int minh);
void    primview_cleanup(void);       /* the class freed (at the end; its objects must be gone) */
/* the primitives painted with their top left at x, y; pen gives a pen for 0xRRGGBB */
typedef LONG (*PvPen)(void *ctx, ULONG rgb);
void    pv_paint(struct RastPort *rp, const PrimList *pl, LONG x, LONG y, PvPen pen, void *ctx);
i32     pv_text_width(void *ctx, const char *utf8, int px, int bold);

#endif
