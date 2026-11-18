/*
 * mac/qm_text.c - Quill on the classic Mac OS: the Mac's character set beside Quill's.
 * GPL-2.0-only, as Quill.
 *
 * Quill's core keeps text as UTF-8 and its interface words (amiga/lang_table.h) are Latin-1, the
 * Amiga's character set. The Mac's keyboard, menus and window titles speak Mac Roman, Apple's
 * own 8-bit set, whose upper half differs from Latin-1 almost everywhere (é is 0x8E, not 0xE9).
 * So every character that crosses between the two goes through the tables here: typing (Mac
 * Roman to UTF-8), a document's name in its window title (UTF-8 to Mac Roman) and the interface
 * words (Latin-1 to Mac Roman, once, when the language is chosen: mac/qm_lang.c).
 *
 * The Mac Roman table is Apple's as of Mac OS 8.5, where 0xDB is the euro sign; on System 7 that
 * byte was the general currency sign, and fonts of the time draw it so.
 */
#include <string.h>
#include "quill_mac.h"

/* Mac Roman 0x80-0xFF as Unicode */
static const unsigned short roman_hi[128] = {
	0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
	0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
	0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
	0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
	0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
	0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
	0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
	0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
	0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
	0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
	0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
	0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
	0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
	0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
	0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
	0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
};

/* the Latin-1 characters Mac Roman has not got, as the nearest it has: currency sign, broken
   bar, soft hyphen, superscripts 2 3 1, the three fractions, Eth, times, Y acute, Thorn, eth,
   y acute, thorn */
static const struct { unsigned char latin1; char mac; } missing[] = {
	{ 0xA4, (char)0xDB }, { 0xA6, '|' }, { 0xAD, '-' }, { 0xB2, '2' }, { 0xB3, '3' }, { 0xB9, '1' },
	{ 0xBC, '?' }, { 0xBD, '?' }, { 0xBE, '?' }, { 0xD0, 'D' }, { 0xD7, 'x' }, { 0xDD, 'Y' },
	{ 0xDE, 'T' }, { 0xF0, 'd' }, { 0xFD, 'y' }, { 0xFE, 't' },
};

/* a Unicode character as Mac Roman, 0 when it has none */
static unsigned char roman_of(unsigned long u)
{
	int i;
	if (u < 0x80)
		return (unsigned char)u;
	for (i = 0; i < 128; i++)
		if (roman_hi[i] == u)
			return (unsigned char)(0x80 + i);
	return 0;
}

int macroman_to_utf8(unsigned char c, char *out)
{
	unsigned u = c < 0x80 ? c : roman_hi[c - 0x80];
	if (u < 0x80) {
		out[0] = (char)u;
		return 1;
	}
	if (u < 0x800) {
		out[0] = (char)(0xC0 | (u >> 6));
		out[1] = (char)(0x80 | (u & 0x3F));
		return 2;
	}
	out[0] = (char)(0xE0 | (u >> 12));
	out[1] = (char)(0x80 | ((u >> 6) & 0x3F));
	out[2] = (char)(0x80 | (u & 0x3F));
	return 3;
}

void utf8_to_macroman(const char *s, char *out, int cap)
{
	const unsigned char *p = (const unsigned char *)s;
	int o = 0;
	while (*p && o < cap - 1) {
		unsigned long u = *p++;
		unsigned char m;
		int more = u >= 0xF0 ? 3 : u >= 0xE0 ? 2 : u >= 0xC0 ? 1 : 0;
		if (more)
			u &= 0x3F >> more;
		while (more-- && (*p & 0xC0) == 0x80)
			u = (u << 6) | (*p++ & 0x3F);
		m = roman_of(u);
		out[o++] = m ? (char)m : '?';
	}
	out[o] = 0;
}

void latin1_to_macroman(const char *s, char *out, int cap)
{
	const unsigned char *p = (const unsigned char *)s;
	int o = 0, i;
	while (*p && o < cap - 1) {
		unsigned char c = *p++, m = roman_of(c);
		if (!m && c >= 0x80) {
			m = '?';
			for (i = 0; i < (int)(sizeof missing / sizeof missing[0]); i++)
				if (missing[i].latin1 == c)
					m = (unsigned char)missing[i].mac;
		}
		out[o++] = (char)m;
	}
	out[o] = 0;
}
