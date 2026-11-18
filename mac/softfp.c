/*
 * mac/softfp.c - converting between integers, floats and doubles without an FPU. GPL-2.0-only, as Quill.
 *
 * Quill's Mac build is compiled -msoft-float (docs/mac.md, "Floating point: soft-float"), so every
 * double sum and every conversion is a call into libgcc. Debian's and Ubuntu's m68k-linux-gnu
 * compiler ships one libgcc only, built for the 68020 with a 68881. Its add, subtract, multiply,
 * divide and compare (_double.o, from lb1sf68.S) are integer code and take and give doubles in
 * D0/D1 as soft-float code expects. The conversions in fpgnulib.o are not: they are C compiled for
 * the 68881, use FMOVE and return their result in FP0. A soft-float caller reads D0/D1 and gets
 * whatever was there, so (double)i was garbage. Charts showed it first (18 November 2026): every
 * value read from a chart came out near 1e+153 and the charts were drawn as empty boxes. On a Mac
 * without an FPU (LC II, LC III, Classic II, 68LC040 Macs) the FMOVE itself would have stopped Quill
 * with an F-line exception.
 *
 * This file defines all ten functions fpgnulib.o defines, in integer arithmetic, so the linker takes
 * these and never pulls fpgnulib.o in (Makefile.mac checks the map for it). Floats and doubles are
 * IEEE 754, big-endian: a double is a sign bit, 11 bits of exponent (bias 1023) and 52 of fraction;
 * a float a sign bit, 8 bits of exponent (bias 127) and 23 of fraction. Results are rounded to
 * nearest, ties to even, as the 68881 does by default. Nothing here uses a double operation, which
 * would call back into libgcc.
 */
#include <stdint.h>

/* a double's two words: HI holds the sign, the exponent and the top 20 bits of the fraction. The
   68k is big-endian (HI first); the other order lets this file be tested on a PC against its FPU. */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define HI 1
#define LO 0
#else
#define HI 0
#define LO 1
#endif
typedef union { double d; uint32_t w[2]; } dbits;
typedef union { float f; uint32_t w; } fbits;

/* how far the top set bit of u (not 0) is from bit 31 */
static int lead(uint32_t u)
{
	int n = 0;
	while (!(u & 0x80000000UL)) {
		u <<= 1;
		n++;
	}
	return n;
}

static int dnan(dbits x) { return (x.w[HI] & 0x7FF00000UL) == 0x7FF00000UL && ((x.w[HI] & 0xFFFFF) | x.w[LO]); }
static int fnan(fbits x) { return (x.w & 0x7F800000UL) == 0x7F800000UL && (x.w & 0x7FFFFF); }

/* m shifted right by s (1 or more), rounded to nearest, ties to even */
static uint64_t round_shift(uint64_t m, int s)
{
	uint64_t r, rem, half;
	if (s >= 64)                                  /* (m is under 2^54 here: below half the last unit) */
		return 0;
	r = m >> s;
	rem = m & ((((uint64_t)1) << s) - 1);
	half = ((uint64_t)1) << (s - 1);
	if (rem > half || (rem == half && (r & 1)))
		r++;
	return r;
}

/* ---- integers to doubles: always exact (32 bits fit the 53 of a double) ---- */
double __floatunsidf(unsigned int u)
{
	dbits x;
	uint32_t m;
	int n;
	if (!u) {
		x.w[HI] = x.w[LO] = 0;
		return x.d;
	}
	n = lead(u);
	m = u << n;                                   /* bit 31 is the leading 1, which is not stored */
	x.w[HI] = ((uint32_t)(1023 + 31 - n) << 20) | ((m >> 11) & 0xFFFFF);
	x.w[LO] = m << 21;
	return x.d;
}

double __floatsidf(int i)
{
	dbits x;
	x.d = __floatunsidf(i < 0 ? 0u - (unsigned int)i : (unsigned int)i);
	if (i < 0)
		x.w[HI] |= 0x80000000UL;
	return x.d;
}

/* ---- integers to floats: rounded past 24 bits ---- */
float __floatunsisf(unsigned int u)
{
	fbits x;
	uint32_t r;
	int n, e;
	if (!u) {
		x.w = 0;
		return x.f;
	}
	n = lead(u);
	e = 127 + 31 - n;
	r = (uint32_t)round_shift((uint64_t)(u << n), 8);   /* 24 bits, the leading 1 included */
	if (r == 0x1000000UL) {                              /* rounded up to the next power of two */
		r >>= 1;
		e++;
	}
	x.w = ((uint32_t)e << 23) | (r & 0x7FFFFF);
	return x.f;
}

float __floatsisf(int i)
{
	fbits x;
	x.f = __floatunsisf(i < 0 ? 0u - (unsigned int)i : (unsigned int)i);
	if (i < 0)
		x.w |= 0x80000000UL;
	return x.f;
}

/* ---- to integers: toward zero; out of range gives the nearest of INT_MIN and INT_MAX, NaN 0 ---- */
int __fixdfsi(double v)
{
	dbits x;
	int e, neg;
	uint64_t m;
	uint32_t r;
	x.d = v;
	if (dnan(x))
		return 0;
	neg = (x.w[HI] & 0x80000000UL) != 0;
	e = (int)((x.w[HI] >> 20) & 0x7FF) - 1023;
	if (e < 0)
		return 0;
	if (e >= 31)
		return neg ? (int)0x80000000UL : 0x7FFFFFFF;
	m = ((uint64_t)((x.w[HI] & 0xFFFFF) | 0x100000) << 32) | x.w[LO];
	r = (uint32_t)(m >> (52 - e));
	return neg ? -(int)r : (int)r;
}

int __fixsfsi(float v)
{
	fbits x;
	int e, neg;
	uint32_t m, r;
	x.f = v;
	if (fnan(x))
		return 0;
	neg = (x.w & 0x80000000UL) != 0;
	e = (int)((x.w >> 23) & 0xFF) - 127;
	if (e < 0)
		return 0;
	if (e >= 31)
		return neg ? (int)0x80000000UL : 0x7FFFFFFF;
	m = (x.w & 0x7FFFFF) | 0x800000;
	r = e >= 23 ? m << (e - 23) : m >> (23 - e);
	return neg ? -(int)r : (int)r;
}

/* ---- float to double: exact ---- */
double __extendsfdf2(float v)
{
	fbits f;
	dbits x;
	uint32_t s, m;
	int e;
	f.f = v;
	s = f.w & 0x80000000UL;
	e = (int)((f.w >> 23) & 0xFF);
	m = f.w & 0x7FFFFF;
	if (e == 0xFF) {                              /* infinity, NaN */
		x.w[HI] = s | 0x7FF00000UL | (m >> 3);
		x.w[LO] = m << 29;
		return x.d;
	}
	if (e == 0) {
		if (!m) {                                 /* zero */
			x.w[HI] = s;
			x.w[LO] = 0;
			return x.d;
		}
		e = 1;                                    /* a denormal float: a normal double */
		while (!(m & 0x800000)) {
			m <<= 1;
			e--;
		}
		m &= 0x7FFFFF;
	}
	x.w[HI] = s | ((uint32_t)(e - 127 + 1023) << 20) | (m >> 3);
	x.w[LO] = m << 29;
	return x.d;
}

/* ---- double to float: rounded ---- */
float __truncdfsf2(double v)
{
	dbits x;
	fbits f;
	uint32_t s, r;
	uint64_t m;
	int e;
	x.d = v;
	s = x.w[HI] & 0x80000000UL;
	e = (int)((x.w[HI] >> 20) & 0x7FF);
	m = ((uint64_t)(x.w[HI] & 0xFFFFF) << 32) | x.w[LO];
	if (e == 0x7FF) {                             /* infinity, NaN (kept a NaN) */
		f.w = s | 0x7F800000UL | (m ? 0x400000UL | (uint32_t)(m >> 29) : 0);
		return f.f;
	}
	if (e == 0) {                                 /* zero, or a double denormal: far below a float */
		f.w = s;
		return f.f;
	}
	m |= (uint64_t)1 << 52;
	e = e - 1023 + 127;
	if (e >= 0xFF) {                              /* too large: infinity */
		f.w = s | 0x7F800000UL;
		return f.f;
	}
	if (e <= 0) {                                 /* a float denormal (or zero); 0x800000 is the smallest normal */
		f.w = s | (uint32_t)round_shift(m, 29 + 1 - e);
		return f.f;
	}
	r = (uint32_t)round_shift(m, 29);
	if (r == 0x1000000UL) {
		r >>= 1;
		e++;
		if (e >= 0xFF) {
			f.w = s | 0x7F800000UL;
			return f.f;
		}
	}
	f.w = s | ((uint32_t)e << 23) | (r & 0x7FFFFF);
	return f.f;
}

/* ---- unordered: either is a NaN ---- */
int __unorddf2(double a, double b)
{
	dbits x, y;
	x.d = a;
	y.d = b;
	return dnan(x) || dnan(y);
}

int __unordsf2(float a, float b)
{
	fbits x, y;
	x.f = a;
	y.f = b;
	return fnan(x) || fnan(y);
}
