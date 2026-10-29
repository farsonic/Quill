/*
 * codepage.h - single-byte code pages (Windows 874, 1250-1258, DOS 437, 850, 852, 866, Mac Roman
 * 10000) to Unicode and back, and RTF's \fcharset numbers. Part of WP28a (RTF).
 */
#ifndef QUILL_CODEPAGE_H
#define QUILL_CODEPAGE_H

#include "util.h"

/* the Unicode character byte c stands for in code page cp (an unknown code page: Windows 1252) */
u32 cp_to_unicode(int cp, u8 c);
/* the byte that stands for u in code page cp, -1 if it has none */
int cp_from_unicode(int cp, u32 u);
/* the code page an RTF \fcharset names: 0 the document's (ANSI), 2 for Symbol, else a code page */
int cp_of_charset(int charset);
/* a double-byte code page (932, 936, 949, 950): its lead bytes start two-byte characters */
int cp_dbcs(int cp);

#endif
