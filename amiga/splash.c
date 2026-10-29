/*
 * Quill - the window shown while Quill starts: what it is doing, and a bar for how far it has got.
 * A plain Intuition window (MUI isn't running yet), on the screen Quill's window will open on.
 */
#include <string.h>
#include <stdio.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include "splash.h"

extern FILE *qlog;
/* sw: the window (NULL if it didn't open); sdri: its screen's DrawInfo, for the standard pens;
   t0: when splash_open was called; last: the percentage shown (the bar never goes back) */
static struct Window *sw;
static struct DrawInfo *sdri;
static LONG t0;
static int last = -1;

/* the time of day in ticks (50ths of a second): for the log's timings only, so a start that runs
   past midnight just logs an odd number */
static LONG now(void)
{
  struct DateStamp ds;
  DateStamp(&ds);
  return ds.ds_Minute * 3000 + ds.ds_Tick;
}
/* the window opened, centred on the public screen named (NULL or "": the default one), its size
   from the screen's font. If it can't open, the other calls only log. The screen is locked just
   while the window opens (an open window keeps its screen from closing); WA_RMBTrap keeps the
   right button from showing the screen's menus, the window having none */
void splash_open(const char *screen, const char *title)
{
  struct Screen *scr = LockPubScreen((STRPTR)(screen && screen[0] ? screen : NULL));
  WORD fh, w, h;
  t0 = now();
  if (!scr) return;
  fh = scr->RastPort.TxHeight;
  w = 34 * scr->RastPort.Font->tf_XSize > 300 ? 34 * scr->RastPort.Font->tf_XSize : 300;
  if (w > scr->Width - 20) w = scr->Width - 20;
  h = scr->WBorTop + fh + 1 + 12 + fh + 8 + 12 + 12 + scr->WBorBottom;
  sw = OpenWindowTags(NULL, WA_PubScreen, (ULONG)scr, WA_Left, (scr->Width - w) / 2, WA_Top, (scr->Height - h) / 2,
                      WA_Width, w, WA_Height, h, WA_Title, (ULONG)title, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                      WA_Activate, FALSE, WA_RMBTrap, TRUE, WA_SmartRefresh, TRUE, WA_NoCareRefresh, TRUE, TAG_DONE);
  if (sw) sdri = GetScreenDrawInfo(scr);
  UnlockPubScreen(NULL, scr);
}
/* how far (0-100) and what now; also into the DEBUG= log, with the time so far */
void splash_step(int pct, const char *what)
{
  struct RastPort *rp;
  WORD l, t, r, fh, by, bx;
  UWORD *pens;
  if (qlog) { fprintf(qlog, "startup: %3d%% %s (%ld ticks)\n", pct, what, (long)(now() - t0)); fflush(qlog); }
  if (!sw || !sdri) return;
  if (pct < last) pct = last;
  last = pct;
  rp = sw->RPort;
  pens = sdri->dri_Pens;
  fh = rp->TxHeight;
  l = sw->BorderLeft + 12; r = sw->Width - sw->BorderRight - 13; t = sw->BorderTop + 12;
  /* the step */
  SetAPen(rp, pens[BACKGROUNDPEN]);
  RectFill(rp, l, t, r, t + fh);
  SetAPen(rp, pens[TEXTPEN]); SetDrMd(rp, JAM1);
  {
    struct TextExtent te;
    ULONG n = TextFit(rp, (STRPTR)what, (ULONG)strlen(what), &te, NULL, 1, (ULONG)(r - l + 1), (ULONG)fh + 1);
    Move(rp, l, t + rp->TxBaseline);
    Text(rp, (STRPTR)what, n);
  }
  /* the bar */
  by = t + fh + 8;
  SetAPen(rp, pens[SHADOWPEN]);
  Move(rp, l, by + 11); Draw(rp, l, by); Draw(rp, r, by);
  SetAPen(rp, pens[SHINEPEN]);
  Move(rp, r, by + 1); Draw(rp, r, by + 11); Draw(rp, l + 1, by + 11);
  bx = l + 2 + (WORD)((LONG)(r - l - 4) * pct / 100);
  SetAPen(rp, pens[FILLPEN]);
  if (bx > l + 2) RectFill(rp, l + 2, by + 2, bx - 1, by + 9);
  SetAPen(rp, pens[BACKGROUNDPEN]);
  if (bx <= r - 2) RectFill(rp, bx, by + 2, r - 2, by + 9);
}
/* in front of Quill's window (while the document opens) */
void splash_front(void)
{
  if (sw) WindowToFront(sw);
}
/* the window closed and the DrawInfo given back; safe to call when it never opened */
void splash_close(void)
{
  if (qlog) { fprintf(qlog, "startup: done (%ld ticks)\n", (long)(now() - t0)); fflush(qlog); }
  if (!sw) return;
  if (sdri) FreeScreenDrawInfo(sw->WScreen, sdri);
  CloseWindow(sw);
  sw = NULL; sdri = NULL;
}
