/*
 * mac/qm_measure.c - Octavo's own figures, for the log (AURORA68=1 builds send it to the testbed's
 * kernel log): the memory it uses, how busy it is while nothing happens, and what a keystroke and a
 * redraw cost. docs/mac.md, "The experimental ribbon tree", has the figures taken with it.
 * GPL-2.0-only, as Quill.
 *
 *  memory   qm_mem_log(when): the application heap's size and what is free in it (the Memory
 *           Manager's FreeMem), what mac/libc.c's malloc holds (its 64 KB chunks and its own big
 *           blocks, and the most the big ones ever held)
 *  idle     every WaitNextEvent goes through __wrap_WaitNextEvent (the link's --wrap): the time
 *           spent inside it is time Octavo gave away; every ten seconds the log says how much of
 *           the time Octavo kept (busy, per cent) and how often it woke (with Microseconds)
 *  timing   qm_us(): Microseconds' low 32 bits, for the view's keystroke and redraw lines
 */
#include <stdio.h>
#include <string.h>
#include "qm_mac.h"

unsigned long long mac_us(void);
long qm_freemem(void);                  /* mac/qm_thunks.S: _FreeMem */
void mm_log(const char *fmt, ...);
void libc_stats(unsigned long *chunks, unsigned long *big, unsigned long *bigpeak);

unsigned long qm_us(void)
{
	return (unsigned long)mac_us();
}

void qm_mem_log(const char *when)
{
	char *zone = *(char **)0x2AA;                    /* ApplZone: the application heap */
	char *lim = zone ? *(char **)zone : NULL;        /* its bkLim */
	unsigned long chunks = 0, big = 0, peak = 0;
	libc_stats(&chunks, &big, &peak);
	mm_log("memory (%s): heap %ld KB, free %ld KB (used %ld KB); malloc: %lu KB in chunks, %lu KB in big blocks (most %lu KB)",
	       when, zone && lim ? (long)(lim - zone) / 1024 : -1L, qm_freemem() / 1024,
	       zone && lim ? ((long)(lim - zone) - qm_freemem()) / 1024 : -1L, chunks / 1024, big / 1024, peak / 1024);
}

/* ---- idle ---- */
short __real_WaitNextEvent(int mask, void *ev, long sleep, void *rgn);
static unsigned long win_start, slept, wakes, longest;
short __wrap_WaitNextEvent(int mask, void *ev, long sleep, void *rgn)
{
	unsigned long t0 = qm_us(), t;
	short r = __real_WaitNextEvent(mask, ev, sleep, rgn);
	t = qm_us() - t0;
	slept += t;
	wakes++;
	if (!win_start)
		win_start = t0;
	return r;
}

/* from the main loop: every ten seconds, the share of the time Octavo kept */
void qm_measure_loop(void)
{
	unsigned long now = qm_us(), span;
	static unsigned long busiest;
	if (!win_start)
		return;
	span = now - win_start;
	if (span < 10000000UL)
		return;
	{
		unsigned long busy = span > slept ? span - slept : 0;
		unsigned long permille = (unsigned long)((unsigned long long)busy * 1000 / span);
		unsigned long rate10 = (unsigned long)((unsigned long long)wakes * 10000000ULL / span);
		mm_log("idle: busy %lu.%lu%% of %lu s, %lu wakes (%lu.%lu a second)", permille / 10, permille % 10, span / 1000000UL, wakes,
		       rate10 / 10, rate10 % 10);
		(void)busiest;
		(void)longest;
	}
	win_start = now;
	slept = wakes = 0;
}
