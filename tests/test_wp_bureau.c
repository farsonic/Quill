/* test_wp_bureau.c - the native tests of core/respath.c, where Quill finds the fonts and dictionaries
   it shares with the Bureau suite. Checks with ok() (tests/testutil.h); coretest.c's main() calls
   test_wp_bureau() */
#include <string.h>
#include "testutil.h"
#include "../core/respath.h"

/* a pretend file system: the drawers in the space-separated list ud (e.g. "PROGDIR:Dict Bureau:Dict") */
static int asked;
static int fake_exists(const char *dir, void *ud)
{
  const char *have = ud, *p;
  size_t n = strlen(dir);
  asked++;
  for (p = have; (p = strstr(p, dir)); p += n)
    if ((p == have || p[-1] == ' ') && (p[n] == ' ' || !p[n])) return 1;
  return 0;
}
static int pick_is(int kind, const char *have, const char *want)
{
  const char *got = res_pick(kind, fake_exists, (void *)have);
  return want ? got && !strcmp(got, want) : !got;
}

void test_wp_bureau(void)
{
  char out[32];
  const char *all3 = "PROGDIR:Fonts PROGDIR:TrueType PROGDIR:Dict Bureau:Fonts Bureau:TrueType Bureau:Dict";
  const char *bureau = "Bureau:Fonts Bureau:TrueType Bureau:Dict";
  /* a single unpacked program: its own drawers first, whatever else there is */
  ok("respath: own Fonts first", pick_is(RES_FONTS, all3, "PROGDIR:Fonts"));
  ok("respath: own TrueType first", pick_is(RES_TRUETYPE, all3, "PROGDIR:TrueType"));
  ok("respath: own Dict first", pick_is(RES_DICT, all3, "PROGDIR:Dict"));
  /* installed as part of the suite: Bureau:'s */
  ok("respath: Bureau:Fonts second", pick_is(RES_FONTS, bureau, "Bureau:Fonts"));
  ok("respath: Bureau:TrueType second", pick_is(RES_TRUETYPE, bureau, "Bureau:TrueType"));
  ok("respath: Bureau:Dict second", pick_is(RES_DICT, bureau, "Bureau:Dict"));
  /* screen fonts in FONTS: (the suite's installer puts them there), taken without asking */
  asked = 0;
  ok("respath: FONTS: third", pick_is(RES_FONTS, "Bureau:TrueType Bureau:Dict", "FONTS:") && asked == 2);
  ok("respath: FONTS: even with nothing", pick_is(RES_FONTS, "", "FONTS:"));
  /* no Bureau: assign and no drawers: nothing for TrueType and Dict */
  ok("respath: no TrueType", pick_is(RES_TRUETYPE, "", NULL));
  ok("respath: no Dict", pick_is(RES_DICT, "PROGDIR:Fonts PROGDIR:TrueType", NULL));
  ok("respath: own Dict without Bureau:", pick_is(RES_DICT, "PROGDIR:Dict", "PROGDIR:Dict"));
  ok("respath: a bad kind", res_pick(7, fake_exists, (void *)all3) == NULL && res_pick(-1, fake_exists, (void *)all3) == NULL);
  /* joins */
  ok("respath: join after ':'", res_join(out, sizeof out, "FONTS:", "x.font") && !strcmp(out, "FONTS:x.font"));
  ok("respath: join after '/'", res_join(out, sizeof out, "Work:Bureau/", "Dict") && !strcmp(out, "Work:Bureau/Dict"));
  ok("respath: join a plain dir", res_join(out, sizeof out, "PROGDIR:Dict", "th_en.dat") && !strcmp(out, "PROGDIR:Dict/th_en.dat"));
  ok("respath: join an empty dir", res_join(out, sizeof out, "", "Quill.words") && !strcmp(out, "Quill.words"));
  ok("respath: join a size drawer", res_join(out, sizeof out, "Bureau:Fonts", "QuillSans/13") && !strcmp(out, "Bureau:Fonts/QuillSans/13"));
  /* truncation: exactly fitting, one short (out ""), no room at all */
  ok("respath: join just fits", res_join(out, 8, "AB:", "cdef") && !strcmp(out, "AB:cdef"));
  ok("respath: join just fits with '/'", res_join(out, 8, "A:b", "cde") && !strcmp(out, "A:b/cde"));
  strcpy(out, "junk");
  ok("respath: join too long", !res_join(out, 7, "AB:", "cdef") && out[0] == 0);
  strcpy(out, "junk");
  ok("respath: join too long by the '/'", !res_join(out, 7, "A:b", "cde") && out[0] == 0);
  strcpy(out, "junk");
  ok("respath: join into nothing", !res_join(out, 0, "A:", "b") && !strcmp(out, "junk"));
}
