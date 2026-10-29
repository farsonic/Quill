/*
 * wp_objects.h - what view.c asks of WP17 (amiga/wp_picture.c) and WP18 (amiga/wp_shapes.c):
 * drawing an object with its adjustments, fills and effects (core/objfx.c's raster, dithered to the
 * view's colour cube), and cropping a picture by dragging its handles.
 */
#ifndef QUILL_WP_OBJECTS_H
#define QUILL_WP_OBJECTS_H

#include <graphics/rastport.h>
#include "../core/layout.h"

/* t (a simple object, cx x cy turned rot) with its turned box's top left at x, y (pixels), the
   view at zoom per cent: drawn through objfx_render, dithered to the cube's pens (lv levels a
   channel), rows outside band0..band1 (band1 0: none) left alone; 0 if it couldn't be drawn */
int  wp18_draw_rich(struct RastPort *rp, const Image *t, LONG zoom, LONG x, LONG y, const LONG *cube, int lv, LONG band0, LONG band1);
void wp18_flush(void);                   /* the drawings kept for the screen forgotten (pens changed) */
/* whether object k is being cropped by dragging (Picture Format > Crop) */
int  wp17_cropping(int k);
/* a crop dragged: the picture m (its box f on the page) cropped to l, t, r, b (twips on the page) */
void wp17_crop_drop(Image *m, const FloatBox *f, i32 l, i32 t, i32 r, i32 b);

/* WP18's for WP17: the Format pane opened at a page (0 fill and line, 1 effects, 2 size, 3 text box,
   4 picture; -1 as it was), an effect preset (Shape Effects' list) on m, a picture file chosen and kept
   in the document (its Doc.images index, -1 none) */
void wp18_open_pane(int page);
void wp18_effect_preset(Image *m, int v);
int  wp18_load_picture(const char *title);

#endif
