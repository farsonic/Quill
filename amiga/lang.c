/* lang.c - see lang.h */
#include <string.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/locale.h>
#include "lang.h"

const char *const ui_languages[UI_NLANG] = { "English", "Deutsch", "Fran\347ais", "Espa\361ol", "Italiano" };
static const char *const codes[UI_NLANG] = { "en", "de", "fr", "es", "it" };
static const char *const names[UI_NLANG] = { "english", "deutsch", "fran\347ais", "espa\361ol", "italiano" };   /* Locale's */

/* the table: English, then each other language in order (NULL: as English); generated from
   lang/strings.txt by lang/mktable.py */
typedef struct { const char *s[UI_NLANG]; } Words;
static const Words table[] = {
#include "lang_table.h"
  { { NULL } }
};
#define NWORDS ((int)(sizeof table / sizeof table[0]))
static const Words *byen[NWORDS + 1];   /* sorted by the English, for bsearch */
static int nby, cur;

/* qsort's and bsearch's comparisons, both by the English (bsearch's key is the plain string) */
static int cmp_words(const void *a, const void *b) { return strcmp((*(const Words *const *)a)->s[0], (*(const Words *const *)b)->s[0]); }
static int cmp_key(const void *k, const void *b) { return strcmp((const char *)k, (*(const Words *const *)b)->s[0]); }
/* the interface in language lang (a LANG_; anything else means English). The first call also
   builds byen, the table sorted by its English, so tr() gives the English back until then */
void lang_set(int lang)
{
  int i;
  cur = lang >= 0 && lang < UI_NLANG ? lang : LANG_EN;
  if (!nby) {
    for (i = 0; i < NWORDS; i++) if (table[i].s[0]) byen[nby++] = &table[i];
    qsort(byen, (size_t)nby, sizeof byen[0], cmp_words);
  }
}
/* the LANG_ lang_set chose */
int lang_current(void) { return cur; }
/* the words in the language chosen: a pointer into the built-in table (not to be freed or
   changed), or english itself when the table hasn't got it or has no translation for it */
const char *tr(const char *english)
{
  const Words *const *w;
  if (cur == LANG_EN || !english || !*english || !nby) return english;
  w = (const Words *const *)bsearch(english, byen, (size_t)nby, sizeof byen[0], cmp_key);
  return w && (*w)->s[cur] ? (*w)->s[cur] : english;
}
/* the two-letter code Quill.prefs keeps (language=de); English's for a number out of range */
const char *lang_code(int lang) { return codes[lang >= 0 && lang < UI_NLANG ? lang : 0]; }
/* the LANG_ for a code from lang_code (as read back from Quill.prefs), -1 if it isn't one */
int lang_by_code(const char *code)
{
  int i;
  for (i = 0; i < UI_NLANG; i++) if (!strcmp(code, codes[i])) return i;
  return -1;
}
/* locale.library, open only while lang_system looks (the proto/ calls go through this global) */
struct Library *LocaleBase;
/* the first of the user's preferred languages in Locale prefs that Quill speaks, else English.
   Locale names them in their own words, lower case ("deutsch"), so they are matched by their
   start against names[]. The library is opened just for this and closed again */
int lang_system(void)
{
  int found = LANG_EN, i, k;
  struct Locale *loc;
  if (!(LocaleBase = OpenLibrary((STRPTR)"locale.library", 38))) return LANG_EN;
  if ((loc = OpenLocale(NULL))) {
    for (k = 0; k < 10 && loc->loc_PrefLanguages[k] && found == LANG_EN; k++) {
      const char *p = (const char *)loc->loc_PrefLanguages[k];
      for (i = 1; i < UI_NLANG; i++) if (!strncmp(p, names[i], strlen(names[i]))) { found = i; break; }
    }
    CloseLocale(loc);
  }
  CloseLibrary(LocaleBase); LocaleBase = NULL;
  return found;
}
