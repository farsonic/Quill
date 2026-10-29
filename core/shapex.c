/*
 * shapex.c - see objfx.h: more of Word's preset shapes (WP18), as outlines of points like
 * layout.c's shape_points (curves as short straight pieces), their DrawingML names and labels, and
 * the icon library's drawings in Quill's icon language. The shapes follow Word's default
 * adjustments (presetShapeDefinitions.xml), approximated; the icons are Quill's own.
 */
#include <stdlib.h>
#include <string.h>
#include "objfx.h"

typedef struct { const char *prst, *label; } ShxName;
static const ShxName names[SHX_END - SHX_FIRST] = {
  { "heptagon", "Heptagon" }, { "decagon", "Decagon" }, { "dodecagon", "Dodecagon" }, { "teardrop", "Teardrop" },
  { "pie", "Partial Circle" }, { "chord", "Chord" }, { "round2SameRect", "Round Same Side Corner Rectangle" },
  { "round2DiagRect", "Round Diagonal Corner Rectangle" }, { "snip1Rect", "Snip Single Corner Rectangle" },
  { "snip2DiagRect", "Snip Diagonal Corner Rectangle" }, { "plaque", "Plaque" }, { "moon", "Moon" },
  { "lightningBolt", "Lightning Bolt" }, { "cloud", "Cloud" }, { "corner", "L-Shape" }, { "diagStripe", "Diagonal Stripe" },
  { "halfFrame", "Half Frame" }, { "wave", "Wave" }, { "doubleWave", "Double Wave" }, { "foldedCorner", "Folded Corner" },
  { "leftRightArrow", "Left-Right Arrow" }, { "upDownArrow", "Up-Down Arrow" }, { "quadArrow", "Quad Arrow" },
  { "leftRightUpArrow", "Left-Right-Up Arrow" }, { "bentArrow", "Bent Arrow" }, { "uturnArrow", "U-Turn Arrow" },
  { "leftUpArrow", "Left-Up Arrow" }, { "bentUpArrow", "Bent-Up Arrow" }, { "notchedRightArrow", "Notched Right Arrow" },
  { "rightArrowCallout", "Right Arrow Callout" }, { "leftArrowCallout", "Left Arrow Callout" },
  { "upArrowCallout", "Up Arrow Callout" }, { "downArrowCallout", "Down Arrow Callout" },
  { "leftRightArrowCallout", "Left-Right Arrow Callout" },
  { "flowChartProcess", "Flowchart: Process" }, { "flowChartAlternateProcess", "Flowchart: Alternate Process" },
  { "flowChartDecision", "Flowchart: Decision" }, { "flowChartInputOutput", "Flowchart: Data" },
  { "flowChartPredefinedProcess", "Flowchart: Predefined Process" }, { "flowChartDocument", "Flowchart: Document" },
  { "flowChartMultidocument", "Flowchart: Multidocument" }, { "flowChartTerminator", "Flowchart: Terminator" },
  { "flowChartPreparation", "Flowchart: Preparation" }, { "flowChartManualInput", "Flowchart: Manual Input" },
  { "flowChartManualOperation", "Flowchart: Manual Operation" }, { "flowChartConnector", "Flowchart: Connector" },
  { "flowChartOffpageConnector", "Flowchart: Off-page Connector" }, { "flowChartPunchedCard", "Flowchart: Card" },
  { "flowChartPunchedTape", "Flowchart: Punched Tape" }, { "flowChartCollate", "Flowchart: Collate" },
  { "flowChartSort", "Flowchart: Sort" }, { "flowChartExtract", "Flowchart: Extract" }, { "flowChartMerge", "Flowchart: Merge" },
  { "flowChartDelay", "Flowchart: Delay" }, { "flowChartDisplay", "Flowchart: Display" },
  { "flowChartOnlineStorage", "Flowchart: Stored Data" },
  { "wedgeRoundRectCallout", "Speech Bubble: Rectangle with Corners Rounded" }, { "wedgeEllipseCallout", "Speech Bubble: Oval" },
  { "cloudCallout", "Thought Bubble: Cloud" },
  { "star6", "Star: 6 Points" }, { "star7", "Star: 7 Points" }, { "star8", "Star: 8 Points" }, { "star10", "Star: 10 Points" },
  { "star12", "Star: 12 Points" }, { "star16", "Star: 16 Points" }, { "star24", "Star: 24 Points" }, { "star32", "Star: 32 Points" },
  { "irregularSeal1", "Explosion: 8 Points" }, { "irregularSeal2", "Explosion: 14 Points" },
  { NULL, "Icon" } };
const char *const shapex_category[SC_N] = { "Basic Shapes", "Block Arrows", "Flowchart", "Callouts", "Stars and Banners" };
static const u8 cat_first[SC_N + 1] = { SHX_HEPTAGON, SHX_LRARROW, SHX_FC_PROCESS, SHX_ROUNDCALLOUT, SHX_STAR6, SHX_ICON };
void shapex_range(int cat, int *first, int *end)
{
  if (cat < 0 || cat >= SC_N) { *first = *end = SHX_FIRST; return; }
  *first = cat_first[cat]; *end = cat_first[cat + 1];
}
const char *shapex_label(int shape) { return shape >= SHX_FIRST && shape < SHX_END ? names[shape - SHX_FIRST].label : "Shape"; }
const char *shapex_prst(int shape) { return shape >= SHX_FIRST && shape < SHX_END ? names[shape - SHX_FIRST].prst : NULL; }
int shapex_from_prst(const char *prst)
{
  int k;
  if (!prst) return -1;
  for (k = 0; k < SHX_END - SHX_FIRST; k++) if (names[k].prst && !strcmp(prst, names[k].prst)) return SHX_FIRST + k;
  /* Word's other names for shapes Quill has */
  if (!strcmp(prst, "flowChartPunchedCard")) return SHX_FC_CARD;
  if (!strcmp(prst, "flowChartMagneticDisk") || !strcmp(prst, "can")) return SHX_FC_STORAGE;
  return -1;
}

#define PT(X, Y) do { if (n < max) { xy[2 * n] = (i32)(X); xy[2 * n + 1] = (i32)(Y); n++; } } while (0)
#define MIN2(a, b) ((a) < (b) ? (a) : (b))
/* points of an ellipse arc round (cx, cy), radii rx, ry, from angle a0 to a1 (tenths of a degree,
   clockwise on the page from the right; a1 < a0 goes the other way) in steps */
static u32 arc(i32 *xy, u32 n, u32 max, long cx, long cy, long rx, long ry, int a0, int a1, int steps)
{
  int i;
  for (i = 0; i <= steps; i++) {
    int a = a0 + (int)((long)(a1 - a0) * i / steps);
    PT(cx + rx * icos(a) / TRIG_ONE, cy + ry * isin(a) / TRIG_ONE);
  }
  return n;
}
/* a star or polygon: m points from the top, every other one at ratio (per thousand) of the radius,
   stretched over the box */
static u32 star(i32 *xy, u32 max, i32 w, i32 h, int m, int ratio)
{
  u32 n = 0;
  int i;
  for (i = 0; i < 2 * m; i++) {
    int a = 2700 + (int)(1800L * i / m);
    long r = i & 1 ? ratio : 1000;
    PT(w / 2 + (long)w / 2 * r / 1000 * icos(a) / TRIG_ONE, h / 2 + (long)h / 2 * r / 1000 * isin(a) / TRIG_ONE);
  }
  return n;
}
/* a regular polygon of m sides from the top, stretched so it fills the box */
static u32 regular(i32 *xy, u32 max, i32 w, i32 h, int m)
{
  i32 px[16], py[16], x0 = 0x7FFFFFFF, x1 = -0x7FFFFFFF, y0 = 0x7FFFFFFF, y1 = -0x7FFFFFFF;
  u32 n = 0;
  int i;
  for (i = 0; i < m; i++) {
    int a = 2700 + (int)(3600L * i / m);
    px[i] = icos(a); py[i] = isin(a);
    if (px[i] < x0) x0 = px[i];
    if (px[i] > x1) x1 = px[i];
    if (py[i] < y0) y0 = py[i];
    if (py[i] > y1) y1 = py[i];
  }
  for (i = 0; i < m; i++) PT((long long)(px[i] - x0) * w / (x1 - x0), (long long)(py[i] - y0) * h / (y1 - y0));
  return n;
}
/* points in a 21600 x 21600 space stretched over the box */
static u32 table(i32 *xy, u32 max, i32 w, i32 h, const i32 *t, int m)
{
  u32 n = 0;
  int i;
  for (i = 0; i < m; i++) PT((long long)t[2 * i] * w / 21600, (long long)t[2 * i + 1] * h / 21600);
  return n;
}
/* a cloud: an ellipse with bumps (the radius 88-100 per cent), stretched over the box */
static u32 cloud(i32 *xy, u32 n, u32 max, i32 w, i32 h)
{
  int i;
  for (i = 0; i < 72; i++) {
    int a = i * 50, b = (i * 50 * 9 / 2) % 3600;
    long r = 880 + 120L * (isin(b) < 0 ? -isin(b) : isin(b)) / TRIG_ONE;
    PT(w / 2 + (long)w / 2 * r / 1000 * icos(a) / TRIG_ONE, h / 2 + (long)h / 2 * r / 1000 * isin(a) / TRIG_ONE);
  }
  return n;
}
/* a wavy edge from x0 to x1 (either way) about y, amplitude amp, periods half-waves */
static u32 wavy(i32 *xy, u32 n, u32 max, i32 x0, i32 x1, i32 y, i32 amp, int periods)
{
  int i;
  for (i = 0; i <= 16; i++) PT(x0 + (long)(x1 - x0) * i / 16, y + (long)amp * isin(i * 1800 * periods / 16) / TRIG_ONE);
  return n;
}

static const i32 bolt[] = { 8472, 0, 12860, 6080, 11050, 6797, 16577, 12007, 14767, 12877, 21600, 21600, 10012, 14915, 12222, 13987,
                            5022, 9705, 7602, 8382, 0, 3890 };
static const i32 seal1[] = { 10800, 5800, 14522, 0, 14155, 5325, 18380, 4457, 16702, 7315, 21097, 8137, 17607, 10475, 21600, 13290,
                             16837, 12942, 18145, 18095, 14020, 14457, 13247, 19737, 10532, 14935, 8485, 21600, 7715, 15627, 4762, 17617,
                             5667, 13937, 135, 14587, 3722, 11775, 0, 8615, 4627, 7617, 370, 2295, 7312, 6320, 8352, 2295 };
static const i32 seal2[] = { 11462, 4342, 14790, 0, 14525, 5777, 18007, 3172, 16380, 6532, 21600, 6645, 16985, 9402, 18270, 11290,
                             16380, 12310, 18877, 15632, 14640, 14350, 14942, 17370, 12180, 15935, 11612, 18842, 9872, 17370, 8700, 19712,
                             7527, 18125, 4917, 21600, 4805, 18240, 1285, 17825, 3330, 15370, 0, 12877, 3935, 11592, 1172, 8270,
                             5372, 7817, 4502, 3625, 8550, 6382, 9722, 1887 };

u32 shapex_points(int shape, i32 w, i32 h, i32 *xy, u32 max, int *closed)
{
  u32 n = 0;
  i32 s = MIN2(w, h), r = s / 6, t, hw, hl, cx = w / 2, cy = h / 2;
  *closed = 1;
  switch (shape) {
  case SHX_HEPTAGON: return regular(xy, max, w, h, 7);
  case SHX_DECAGON: return regular(xy, max, w, h, 10);
  case SHX_DODECAGON: return regular(xy, max, w, h, 12);
  case SHX_TEARDROP: n = arc(xy, n, max, cx, cy, cx, cy, 0, 2700, 24); PT(w, 0); break;
  case SHX_PIE: PT(cx, cy); n = arc(xy, n, max, cx, cy, cx, cy, 0, 2700, 24); break;
  case SHX_CHORD: n = arc(xy, n, max, cx, cy, cx, cy, 450, 2700, 24); break;
  case SHX_ROUND2SAME:
    n = arc(xy, n, max, w - r, r, r, r, 2700, 3600, 6); PT(w, h); PT(0, h); n = arc(xy, n, max, r, r, r, r, 1800, 2700, 6); break;
  case SHX_ROUND2DIAG:
    PT(w, 0); n = arc(xy, n, max, w - r, h - r, r, r, 0, 900, 6); PT(0, h); n = arc(xy, n, max, r, r, r, r, 1800, 2700, 6); break;
  case SHX_SNIP1: PT(0, 0); PT(w - r, 0); PT(w, r); PT(w, h); PT(0, h); break;
  case SHX_SNIP2DIAG: PT(0, 0); PT(w - r, 0); PT(w, r); PT(w, h); PT(r, h); PT(0, h - r); break;
  case SHX_PLAQUE:
    PT(r, 0); PT(w - r, 0); n = arc(xy, n, max, w, 0, r, r, 1800, 900, 6); PT(w, h - r); n = arc(xy, n, max, w, h, r, r, 2700, 1800, 6);
    PT(r, h); n = arc(xy, n, max, 0, h, r, r, 0, -900, 6); PT(0, r); n = arc(xy, n, max, 0, 0, r, r, 900, 0, 6); break;
  case SHX_MOON: n = arc(xy, n, max, w, cy, w, cy, 2700, 900, 24); n = arc(xy, n, max, w, cy, w / 2, cy, 900, 2700, 24); break;
  case SHX_LIGHTNING: return table(xy, max, w, h, bolt, 11);
  case SHX_CLOUD: case SHX_CLOUDCALLOUT: return cloud(xy, 0, max, w, h);
  case SHX_CORNER: t = s / 2; PT(0, 0); PT(t, 0); PT(t, h - t); PT(w, h - t); PT(w, h); PT(0, h); break;
  case SHX_DIAGSTRIPE: PT(w / 2, 0); PT(w, 0); PT(0, h); PT(0, h / 2); break;
  case SHX_HALFFRAME: t = s / 3; PT(0, 0); PT(w, 0); PT(w - t, t); PT(t, t); PT(t, h - t); PT(0, h); break;
  case SHX_WAVE: case SHX_DOUBLEWAVE: case SHX_FC_TAPE: {
    int per = shape == SHX_DOUBLEWAVE ? 4 : 2;
    i32 a = h / 8;
    n = wavy(xy, n, max, 0, w, a, a, per); n = wavy(xy, n, max, w, 0, h - a, -a, per);
    break; }
  case SHX_FOLDED: PT(0, 0); PT(w, 0); PT(w, h - r); PT(w - r, h); PT(0, h); break;
  case SHX_LRARROW: hl = MIN2(s / 2, w / 2); PT(0, cy); PT(hl, 0); PT(hl, h / 4); PT(w - hl, h / 4); PT(w - hl, 0); PT(w, cy); PT(w - hl, h); PT(w - hl, 3 * h / 4); PT(hl, 3 * h / 4); PT(hl, h); break;
  case SHX_UDARROW: hl = MIN2(s / 2, h / 2); PT(cx, 0); PT(w, hl); PT(3 * w / 4, hl); PT(3 * w / 4, h - hl); PT(w, h - hl); PT(cx, h); PT(0, h - hl); PT(w / 4, h - hl); PT(w / 4, hl); PT(0, hl); break;
  case SHX_QUADARROW: case SHX_LRUARROW: {
    i32 sw = s * 225 / 1000 / 2, hh = s * 225 / 1000;
    int down = shape == SHX_QUADARROW;
    hl = s * 225 / 1000;
    if (!down) cy = h - hh;
    PT(cx, 0); PT(cx + hh, hl); PT(cx + sw, hl); PT(cx + sw, cy - sw); PT(w - hl, cy - sw); PT(w - hl, cy - hh); PT(w, cy); PT(w - hl, cy + hh); PT(w - hl, cy + sw);
    if (down) { PT(cx + sw, cy + sw); PT(cx + sw, h - hl); PT(cx + hh, h - hl); PT(cx, h); PT(cx - hh, h - hl); PT(cx - sw, h - hl); PT(cx - sw, cy + sw); }
    PT(hl, cy + sw); PT(hl, cy + hh); PT(0, cy); PT(hl, cy - hh); PT(hl, cy - sw); PT(cx - sw, cy - sw); PT(cx - sw, hl); PT(cx - hh, hl);
    break; }
  case SHX_BENTARROW: {
    i32 yc;
    t = s / 4; hw = s / 2; hl = s / 4; yc = hw / 2;
    PT(0, h); PT(0, yc - t / 2); PT(w - hl, yc - t / 2); PT(w - hl, 0); PT(w, yc); PT(w - hl, hw); PT(w - hl, yc + t / 2); PT(t, yc + t / 2); PT(t, h);
    break; }
  case SHX_UTURN: {
    i32 xr, yb;
    t = s / 4; hw = s / 2; hl = s / 4; xr = w - hw / 2; yb = h * 3 / 4 - hl;
    PT(0, h); PT(0, 0); PT(xr + t / 2, 0); PT(xr + t / 2, yb); PT(w, yb); PT(xr, yb + hl); PT(w - hw, yb); PT(xr - t / 2, yb); PT(xr - t / 2, t); PT(t, t); PT(t, h);
    break; }
  case SHX_LEFTUP: {
    i32 xc, yc;
    t = s / 4; hw = s / 2; hl = s / 4; xc = w - hw / 2; yc = h - hw / 2;
    PT(0, yc); PT(hl, yc - hw / 2); PT(hl, yc - t / 2); PT(xc - t / 2, yc - t / 2); PT(xc - t / 2, hl); PT(xc - hw / 2, hl); PT(xc, 0); PT(xc + hw / 2, hl);
    PT(xc + t / 2, hl); PT(xc + t / 2, yc + t / 2); PT(hl, yc + t / 2); PT(hl, yc + hw / 2);
    break; }
  case SHX_BENTUP: {
    i32 xc;
    t = s / 4; hw = s / 2; hl = s / 4; xc = w - hw / 2;
    PT(0, h - t); PT(xc - t / 2, h - t); PT(xc - t / 2, hl); PT(xc - hw / 2, hl); PT(xc, 0); PT(xc + hw / 2, hl); PT(xc + t / 2, hl); PT(xc + t / 2, h); PT(0, h);
    break; }
  case SHX_NOTCHED: hl = w - s / 2; PT(0, h / 4); PT(hl, h / 4); PT(hl, 0); PT(w, cy); PT(hl, h); PT(hl, 3 * h / 4); PT(0, 3 * h / 4); PT(s / 4, cy); break;
  case SHX_RARROWCALL: case SHX_LARROWCALL: {
    i32 bw = w * 65 / 100, k;
    t = s / 8; hw = s / 4; hl = s / 4;
    PT(0, 0); PT(bw, 0); PT(bw, cy - t); PT(w - hl, cy - t); PT(w - hl, cy - hw); PT(w, cy); PT(w - hl, cy + hw); PT(w - hl, cy + t); PT(bw, cy + t); PT(bw, h); PT(0, h);
    if (shape == SHX_LARROWCALL) for (k = 0; k < (i32)n; k++) xy[2 * k] = w - xy[2 * k];
    break; }
  case SHX_UARROWCALL: case SHX_DARROWCALL: {
    i32 bh = h * 65 / 100, k;
    t = s / 8; hw = s / 4; hl = s / 4;
    PT(0, h); PT(0, h - bh); PT(cx - t, h - bh); PT(cx - t, hl); PT(cx - hw, hl); PT(cx, 0); PT(cx + hw, hl); PT(cx + t, hl); PT(cx + t, h - bh); PT(w, h - bh); PT(w, h);
    if (shape == SHX_DARROWCALL) for (k = 0; k < (i32)n; k++) xy[2 * k + 1] = h - xy[2 * k + 1];
    break; }
  case SHX_LRARROWCALL: {
    i32 b0 = w * 26 / 100, b1 = w - b0;
    t = s / 8; hw = s / 4; hl = s / 4;
    PT(b0, 0); PT(b1, 0); PT(b1, cy - t); PT(w - hl, cy - t); PT(w - hl, cy - hw); PT(w, cy); PT(w - hl, cy + hw); PT(w - hl, cy + t); PT(b1, cy + t); PT(b1, h);
    PT(b0, h); PT(b0, cy + t); PT(hl, cy + t); PT(hl, cy + hw); PT(0, cy); PT(hl, cy - hw); PT(hl, cy - t); PT(b0, cy - t);
    break; }
  case SHX_FC_PROCESS: case SHX_FC_PREDEF: PT(0, 0); PT(w, 0); PT(w, h); PT(0, h); break;
  case SHX_FC_ALT: case SHX_ROUNDCALLOUT: {
    int wedge = shape == SHX_ROUNDCALLOUT;
    n = arc(xy, n, max, w - r, r, r, r, 2700, 3600, 6); n = arc(xy, n, max, w - r, h - r, r, r, 0, 900, 6);
    if (wedge) { PT(w / 3, h); PT(w * 292 / 1000, h + h / 8); PT(w / 6, h); }
    n = arc(xy, n, max, r, h - r, r, r, 900, 1800, 6); n = arc(xy, n, max, r, r, r, r, 1800, 2700, 6);
    break; }
  case SHX_FC_DECISION: case SHX_FC_SORT: PT(cx, 0); PT(w, cy); PT(cx, h); PT(0, cy); break;
  case SHX_FC_DATA: PT(w / 5, 0); PT(w, 0); PT(w - w / 5, h); PT(0, h); break;
  case SHX_FC_DOCUMENT: PT(0, 0); PT(w, 0); PT(w, h * 80 / 100); n = wavy(xy, n, max, w, 0, h * 86 / 100, -h * 9 / 100, 2); break;
  case SHX_FC_MULTIDOC:
    PT(0, h * 14 / 100); PT(w * 7 / 100, h * 14 / 100); PT(w * 7 / 100, h * 7 / 100); PT(w * 14 / 100, h * 7 / 100); PT(w * 14 / 100, 0); PT(w, 0);
    PT(w, h * 72 / 100); PT(w * 93 / 100, h * 72 / 100); PT(w * 93 / 100, h * 79 / 100); PT(w * 86 / 100, h * 79 / 100);
    n = wavy(xy, n, max, w * 86 / 100, 0, h * 87 / 100, -h * 8 / 100, 2);
    break;
  case SHX_FC_TERMINATOR: {
    i32 e = MIN2(cy, cx);
    n = arc(xy, n, max, w - e, cy, e, cy, 2700, 4500, 16); n = arc(xy, n, max, e, cy, e, cy, 900, 2700, 16);
    break; }
  case SHX_FC_PREP: PT(w / 5, 0); PT(w - w / 5, 0); PT(w, cy); PT(w - w / 5, h); PT(w / 5, h); PT(0, cy); break;
  case SHX_FC_MANUALIN: PT(0, h / 5); PT(w, 0); PT(w, h); PT(0, h); break;
  case SHX_FC_MANUALOP: PT(0, 0); PT(w, 0); PT(w - w / 5, h); PT(w / 5, h); break;
  case SHX_FC_CONNECTOR: return arc(xy, 0, max, cx, cy, cx, cy, 0, 3600, 48) - 1;
  case SHX_FC_OFFPAGE: PT(0, 0); PT(w, 0); PT(w, h * 8 / 10); PT(cx, h); PT(0, h * 8 / 10); break;
  case SHX_FC_CARD: PT(w / 5, 0); PT(w, 0); PT(w, h); PT(0, h); PT(0, h / 5); break;
  case SHX_FC_COLLATE: PT(0, 0); PT(w, 0); PT(cx, cy); PT(w, h); PT(0, h); PT(cx, cy); break;
  case SHX_FC_EXTRACT: PT(cx, 0); PT(w, h); PT(0, h); break;
  case SHX_FC_MERGE: PT(0, 0); PT(w, 0); PT(cx, h); break;
  case SHX_FC_DELAY: PT(0, 0); n = arc(xy, n, max, cx, cy, cx, cy, 2700, 4500, 24); PT(0, h); break;
  case SHX_FC_DISPLAY: PT(0, cy); PT(w / 6, 0); n = arc(xy, n, max, w * 5 / 6, cy, w / 6, cy, 2700, 4500, 16); PT(w / 6, h); break;
  case SHX_FC_STORAGE: n = arc(xy, n, max, w / 6, cy, w / 6, cy, 900, 2700, 16); n = arc(xy, n, max, w, cy, w / 6, cy, 2700, 900, 16); break;
  case SHX_OVALCALLOUT: {
    int i;
    for (i = 0; i < 64; i++) {
      int a = i * 3600 / 64;
      if (a > 950 && a < 1250) continue;
      PT(cx + (long)cx * icos(a) / TRIG_ONE, cy + (long)cy * isin(a) / TRIG_ONE);
      if (a <= 950 && (i + 1) * 3600 / 64 > 950) PT(w * 292 / 1000, h + h / 8);
    }
    break; }
  case SHX_STAR6: return star(xy, max, w, h, 6, 577);
  case SHX_STAR7: return star(xy, max, w, h, 7, 692);
  case SHX_STAR8: return star(xy, max, w, h, 8, 765);
  case SHX_STAR10: return star(xy, max, w, h, 10, 851);
  case SHX_STAR12: return star(xy, max, w, h, 12, 750);
  case SHX_STAR16: return star(xy, max, w, h, 16, 750);
  case SHX_STAR24: return star(xy, max, w, h, 24, 750);
  case SHX_STAR32: return star(xy, max, w, h, 32, 800);
  case SHX_SEAL1: return table(xy, max, w, h, seal1, 24);
  case SHX_SEAL2: return table(xy, max, w, h, seal2, 28);
  case SHX_ICON: PT(0, 0); PT(w, 0); PT(w, h); PT(0, h); break;
  default: return 0;
  }
  return n;
}
#undef PT

/* ------------------------------------------------------------------ icons */
/* Quill's own drawings, 16 x 16 like the ribbon's (theme.c's language: L line, R box, F filled box,
   O circle, E filled circle, P lines through points, and here Q a filled polygon; "cw" cuts out what
   follows until the next colour) */
typedef struct { const char *name; u8 cat; const char *src; } ObjIcon;
const char *const objicon_category[IC_CAT_N] = { "Arrows", "Office", "People", "Communication", "Nature", "Symbols" };
static const ObjIcon icons[] = {
  /* arrows */
  { "Arrow Right", 0, "Q 7 1 6 8 6 8 2 14 8 8 14 8 10 1 10" },
  { "Arrow Left", 0, "Q 7 15 6 8 6 8 2 2 8 8 14 8 10 15 10" },
  { "Arrow Up", 0, "Q 7 6 15 6 8 2 8 8 2 14 8 10 8 10 15" },
  { "Arrow Down", 0, "Q 7 6 1 6 8 2 8 8 14 14 8 10 8 10 1" },
  { "Refresh", 0, "O 8 8 6 cw F 9 1 15 7 ci Q 3 9 1 15 1 15 7" },
  { "Expand", 0, "Q 3 1 1 6 1 1 6 Q 3 15 15 10 15 15 10 L 2 2 6 6 L 3 2 6 5 L 2 3 5 6 L 14 14 10 10 L 13 14 10 11 L 14 13 11 10" },
  { "Download", 0, "Q 7 6 1 10 1 10 7 13 7 8 12 3 7 6 7 F 1 14 15 15" },
  { "Upload", 0, "Q 7 6 13 10 13 10 7 13 7 8 2 3 7 6 7 F 1 14 15 15" },
  /* office */
  { "Document", 1, "Q 5 3 1 10 1 13 4 13 15 3 15 cw F 5 6 11 6 F 5 9 11 9 F 5 12 9 12" },
  { "Folder", 1, "Q 6 1 3 6 3 7 5 15 5 15 14 1 14" },
  { "Calendar", 1, "F 1 2 15 4 R 1 2 15 15 F 4 1 5 3 F 11 1 12 3 F 4 7 5 8 F 7 7 8 8 F 10 7 11 8 F 4 10 5 11 F 7 10 8 11 F 10 10 11 11" },
  { "Clock", 1, "O 8 8 7 L 8 8 8 3 L 8 8 11 10" },
  { "Briefcase", 1, "F 1 5 15 14 R 5 2 10 5 cw F 1 8 15 8" },
  { "Chart", 1, "F 2 9 4 14 F 6 5 8 14 F 10 7 12 14 F 1 15 15 15" },
  { "Pie Chart", 1, "E 8 8 7 cw Q 3 8 8 8 0 16 8" },
  { "Pencil", 1, "Q 5 10 2 14 6 6 14 2 14 2 10 cw L 9 3 13 7" },
  { "Lightbulb", 1, "E 8 6 5 F 6 10 10 12 F 6 13 10 13 F 7 15 9 15" },
  { "Printer", 1, "F 4 1 12 4 F 1 5 15 11 F 4 9 12 15 cw F 5 10 11 14 ci L 6 11 10 11 L 6 13 10 13" },
  /* people */
  { "Person", 2, "E 8 4 3 Q 6 2 15 3 10 6 8 10 8 13 10 14 15" },
  { "People", 2, "E 5 5 2 E 11 5 2 Q 5 1 13 2 9 8 9 9 13 1 13 Q 5 7 13 8 9 14 9 15 13 7 13" },
  { "Handshake", 2, "Q 6 1 7 5 4 9 7 11 9 9 11 4 11 Q 5 15 7 11 4 7 7 9 9 15 11" },
  { "Heart", 2, "E 5 6 3 E 11 6 3 Q 3 2 7 14 7 8 14" },
  { "Smile", 2, "O 8 8 7 E 6 6 1 E 10 6 1 P 5 4 10 6 12 10 12 12 10" },
  { "Thumbs Up", 2, "F 1 7 4 15 Q 7 5 7 8 1 10 1 10 6 15 6 14 15 5 15" },
  /* communication */
  { "Speech", 3, "Q 7 1 2 15 2 15 11 8 11 4 15 5 11 1 11" },
  { "Envelope", 3, "R 1 3 15 13 P 3 1 3 8 9 15 3" },
  { "Phone", 3, "Q 9 3 1 6 1 7 5 5 7 9 11 11 9 15 10 15 13 12 15 Q 4 3 1 1 3 1 6 3 4" },
  { "Mobile", 3, "F 4 1 12 15 cw F 5 3 11 11 ci E 8 13 1" },
  { "Wi-Fi", 3, "P 5 1 6 4 3 8 2 12 3 15 6 P 5 3 9 5 7 8 6 11 7 13 9 E 8 12 2" },
  { "Megaphone", 3, "Q 5 2 6 11 2 11 14 2 10 F 3 10 5 14" },
  { "Globe", 3, "O 8 8 7 L 1 8 15 8 L 8 1 8 15 P 5 8 1 5 4 4 8 5 12 8 15 P 5 8 1 11 4 12 8 11 12 8 15" },
  { "Link", 3, "R 1 6 9 10 R 7 6 15 10 F 6 7 10 9" },
  /* nature */
  { "Sun", 4, "E 8 8 3 L 8 1 8 3 L 8 13 8 15 L 1 8 3 8 L 13 8 15 8 L 3 3 4 4 L 12 12 13 13 L 13 3 12 4 L 3 13 4 12" },
  { "Cloud", 4, "E 5 10 3 E 9 7 4 E 12 10 3 F 5 10 12 13" },
  { "Rain", 4, "E 5 6 3 E 9 4 3 E 12 6 3 F 5 6 12 9 L 4 11 3 14 L 8 11 7 14 L 12 11 11 14" },
  { "Snowflake", 4, "L 8 1 8 15 L 2 4 14 12 L 2 12 14 4 L 6 2 8 4 L 10 2 8 4 L 6 14 8 12 L 10 14 8 12" },
  { "Tree", 4, "Q 3 8 1 14 9 2 9 Q 3 8 5 15 13 1 13 F 7 13 9 15" },
  { "Leaf", 4, "Q 6 2 14 3 6 8 2 14 2 13 9 7 14 L 1 15 9 7" },
  { "Flower", 4, "E 8 4 2 E 4 7 2 E 12 7 2 E 6 11 2 E 10 11 2 cw E 8 8 1 ci F 7 12 9 15" },
  { "Mountain", 4, "Q 3 0 15 6 3 12 15 Q 3 7 15 11 7 16 15" },
  /* symbols */
  { "Star", 5, "Q 10 8 1 10 6 15 6 11 9 13 15 8 11 3 15 5 9 1 6 6 6" },
  { "Check", 5, "Q 6 1 8 3 6 6 9 13 2 15 4 6 13" },
  { "Cross", 5, "Q 12 1 3 3 1 8 6 13 1 15 3 10 8 15 13 13 15 8 10 3 15 1 13 6 8" },
  { "Warning", 5, "Q 3 8 1 15 15 1 15 cw F 7 5 9 10 F 7 12 9 13" },
  { "Information", 5, "E 8 8 7 cw F 7 7 9 12 E 8 4 1" },
  { "Question", 5, "E 8 8 7 cw P 5 5 6 6 4 10 4 11 6 8 8 L 8 9 8 10 E 8 12 1" },
  { "Lock", 5, "F 2 7 14 15 P 6 4 7 4 3 6 1 10 1 12 3 12 7 cw E 8 11 1" },
  { "Flag", 5, "F 2 1 3 15 Q 6 3 1 14 1 11 4 14 7 3 7 3 1" },
  { "Location", 5, "E 8 6 5 Q 3 4 9 12 9 8 15 cw E 8 6 2" },
  { "Gear", 5, "E 8 8 5 F 7 1 9 15 F 1 7 15 9 Q 4 3 2 2 3 13 14 14 13 Q 4 13 2 14 3 3 14 2 13 cw E 8 8 2" },
  { "Trophy", 5, "Q 6 4 1 12 1 12 6 8 9 4 6 4 1 F 7 9 9 12 F 5 13 11 15 P 4 4 3 1 3 1 5 4 7 P 4 12 3 15 3 15 5 12 7" },
  { "Music", 5, "E 4 13 2 E 12 11 2 F 5 3 6 13 F 13 1 14 11 Q 4 5 3 14 1 14 3 5 5" },
};
#define NICONS ((int)(sizeof icons / sizeof icons[0]))
int objicon_count(void) { return NICONS; }
const char *objicon_name(int i) { return i >= 0 && i < NICONS ? icons[i].name : ""; }
int objicon_cat(int i) { return i >= 0 && i < NICONS ? icons[i].cat : 0; }
const char *objicon_src(int i) { return i >= 0 && i < NICONS ? icons[i].src : ""; }
int objicon_find(const char *name)
{
  int i;
  for (i = 0; i < NICONS; i++) if (!strcmp(icons[i].name, name)) return i;
  return -1;
}
