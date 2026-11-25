/*
 * mac/qm_lang.c - Quill on the classic Mac OS: the interface in English, German, French, Spanish
 * or Italian. GPL-2.0-only, as Quill.
 *
 * The words are the Amiga's, the same table (amiga/lang_table.h, made by tools/mklang.py from
 * tools/lang/), so a translation made for one is there on the other. The Amiga's amiga/lang.c
 * asks locale.library for the language and gives Latin-1; this file asks the System for its
 * region and gives Mac Roman, which is what the Mac's menus and windows draw. The table is
 * Latin-1, so when a language is chosen its column is converted once into one block of Mac Roman
 * strings, and tr() hands out pointers into that: they stay valid for as long as Quill runs, as
 * the Amiga's pointers into the table do (Riptide's Mac MUI keeps some, such as menu labels).
 *
 * tr() is also Riptide's translation hook: its Mac MUI passes every window title, menu label and
 * button through it, so the Edit menu's Undo, Cut and Copy come out in Quill's language too.
 */
#include <string.h>
#include <stdlib.h>
#include "quill_mac.h"
#include "../amiga/lang.h"
#include "qm_mac.h"

typedef struct { const char *s[UI_NLANG]; } Words;
static const Words table[] = {
#include "../amiga/lang_table.h"
	{ { NULL } }
};
#define NWORDS ((int)(sizeof table / sizeof table[0]) - 1)

/* the rows sorted by their English, and each one's words in the language chosen, Mac Roman */
static const Words **byen;
static const char **mac;
static char *block;
static int nby, cur;

static int cmp_words(const void *a, const void *b)
{
	return strcmp((*(const Words *const *)a)->s[0], (*(const Words *const *)b)->s[0]);
}

static int cmp_key(const void *k, const void *b)
{
	return strcmp((const char *)k, (*(const Words *const *)b)->s[0]);
}

void lang_set(int lang)
{
	long need = 0;
	int i;
	char *p;
	cur = lang > LANG_EN && lang < UI_NLANG ? lang : LANG_EN;
	if (!byen) {
		byen = malloc((size_t)NWORDS * sizeof *byen);
		mac = malloc((size_t)NWORDS * sizeof *mac);
		if (!byen || !mac)
			return;
		for (i = 0; i < NWORDS; i++)
			if (table[i].s[0])
				byen[nby++] = &table[i];
		qsort(byen, (size_t)nby, sizeof byen[0], cmp_words);
	}
	free(block);
	block = NULL;
	if (cur == LANG_EN)
		return;
	/* Latin-1 to Mac Roman is one byte for one, so the block is the column's length */
	for (i = 0; i < nby; i++)
		if (byen[i]->s[cur])
			need += (long)strlen(byen[i]->s[cur]) + 1;
	if (!(block = malloc((size_t)need))) {
		cur = LANG_EN;
		return;
	}
	for (i = 0, p = block; i < nby; i++) {
		mac[i] = NULL;
		if (byen[i]->s[cur]) {
			int n = (int)strlen(byen[i]->s[cur]) + 1;
			latin1_to_macroman(byen[i]->s[cur], p, n);
			mac[i] = p;
			p += n;
		}
	}
}

int lang_current(void)
{
	return cur;
}

const char *tr(const char *english)
{
	const Words *const *w;
	if (cur == LANG_EN || !english || !*english || !nby || !block)
		return english;
	w = (const Words *const *)bsearch(english, byen, (size_t)nby, sizeof byen[0], cmp_key);
	return w && mac[w - byen] ? mac[w - byen] : english;
}

const char *tr_ctx(const char *ctx, const char *english)
{
	char key[96];
	const Words *const *w;
	if (cur == LANG_EN || !english || !*english || !block || strlen(ctx) + strlen(english) + 2 > sizeof key)
		return tr(english);
	strcpy(key, ctx);
	strcat(key, ":");
	strcat(key, english);
	w = (const Words *const *)bsearch(key, byen, (size_t)nby, sizeof byen[0], cmp_key);
	return w && mac[w - byen] ? mac[w - byen] : tr(english);
}

/* The System's region, from its 'vers' resource 1 (Inside Macintosh: Macintosh Toolbox
   Essentials, "The Version Resource": two version bytes, the development stage, the
   pre-release number, then the region code as a word). Region codes from Script.h. */
int lang_mac_system(void)
{
	static const struct { short region; char lang; } regions[] = {
		{ 3, LANG_DE }, { 19, LANG_DE }, { 92, LANG_DE },              /* Germany, Swiss German, Austria */
		{ 1, LANG_FR }, { 6, LANG_FR }, { 11, LANG_FR }, { 18, LANG_FR }, /* France, Belgium, Canada, Swiss French */
		{ 8, LANG_ES }, { 86, LANG_ES },                                /* Spain, Latin America */
		{ 4, LANG_IT }, { 36, LANG_IT },                                /* Italy, Swiss Italian */
	};
	short region = qm_system_region();
	unsigned i;
	for (i = 0; i < sizeof regions / sizeof regions[0]; i++)
		if (regions[i].region == region)
			return regions[i].lang;
	return LANG_EN;
}

/* The same in Latin-1, as the Amiga's tr() gives them: for Quill's Amiga files compiled into the
   experimental ribbon tree (mac/ashim/qm_amiga.h), whose own strings are Latin-1 too; their text is
   turned into Mac Roman where it is drawn (mac/qm_ashim.c qm_Text) */
const char *qm_tr_l1(const char *english)
{
	const Words *const *w;
	if (cur == LANG_EN || !english || !*english || !nby)
		return english;
	w = (const Words *const *)bsearch(english, byen, (size_t)nby, sizeof byen[0], cmp_key);
	return w && (*w)->s[cur] ? (*w)->s[cur] : english;
}

const char *qm_tr_ctx_l1(const char *ctx, const char *english)
{
	char key[96];
	const Words *const *w;
	if (cur == LANG_EN || !english || !*english || !nby || strlen(ctx) + strlen(english) + 2 > sizeof key)
		return qm_tr_l1(english);
	strcpy(key, ctx);
	strcat(key, ":");
	strcat(key, english);
	w = (const Words *const *)bsearch(key, byen, (size_t)nby, sizeof byen[0], cmp_key);
	return w && (*w)->s[cur] ? (*w)->s[cur] : qm_tr_l1(english);
}
