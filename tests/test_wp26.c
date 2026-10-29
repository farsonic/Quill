/* test_wp26.c - the native tests of WP26 Customization and discoverability (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp26() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/custom.h"

static void prefs_store(void)
{
  PrefStore ps;
  FILE *f;
  char buf[256];
  size_t n;
  memset(&ps, 0, sizeof ps);
  ok("prefs: a line kept", pstore_line(&ps, "qat=3|Save,8|Undo\n") && !strcmp(pstore_get(&ps, "qat"), "3|Save,8|Undo"));
  ok("prefs: not a setting", !pstore_line(&ps, "no equals sign") && !pstore_line(&ps, "=x"));
  pstore_line(&ps, "key.Ctrl+D=4512|Font...");
  pstore_set(&ps, "qat", "3|Save");
  ok("prefs: replaced in place", ps.n == 2 && !strcmp(pstore_get(&ps, "qat"), "3|Save"));
  pstore_set(&ps, "ribbon.hide", "Insert/Symbols;Design");
  pstore_set(&ps, "key.Ctrl+D", NULL);
  ok("prefs: removed", ps.n == 2 && !pstore_get(&ps, "key.Ctrl+D"));
  f = tmpfile();
  pstore_write(&ps, f);
  rewind(f);
  n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0;
  fclose(f);
  ok("prefs: written in order", !strcmp(buf, "qat=3|Save\nribbon.hide=Insert/Symbols;Design\n"));
  pstore_free(&ps);
}

static void keys(void)
{
  u16 c = 0;
  u8 m = 0;
  char t[40];
  ok("key: Ctrl+Shift+D", key_parse("Ctrl+Shift+D", &c, &m) && c == 0x22 && m == (KM_CTRL | KM_SHIFT));
  ok("key: F10", key_parse("F10", &c, &m) && c == 0x59 && m == 0);
  ok("key: f1 lower case", key_parse("shift+f1", &c, &m) && c == 0x50 && m == KM_SHIFT);
  ok("key: Help", key_parse("Help", &c, &m) && c == 0x5F);
  ok("key: Ctrl+Alt+.", key_parse("Ctrl+Alt+.", &c, &m) && c == 0x39 && m == (KM_CTRL | KM_ALT));
  ok("key: Ctrl+Num+", key_parse("Ctrl+Num+", &c, &m) && c == 0x5E && m == KM_CTRL);
  ok("key: Ctrl+-", key_parse("Ctrl+-", &c, &m) && c == 0x0B && m == KM_CTRL);
  ok("key: nonsense", !key_parse("Hyper+Q", &c, &m) && !key_parse("Ctrl+", &c, &m) && !key_parse("", &c, &m));
  key_format(0x22, KM_CTRL | KM_SHIFT, t, sizeof t);
  ok("key: formatted", !strcmp(t, "Ctrl+Shift+D"));
  key_format(0x01, KM_CTRL | KM_ALT, t, sizeof t);
  ok("key: formatted with Alt", !strcmp(t, "Ctrl+Alt+1"));
  /* every named key goes there and back */
  {
    int k, bad = 0;
    for (k = 0; k < 0x80; k++) {
      int mm;
      if (!key_name((u16)k)) continue;
      for (mm = 0; mm < 8; mm++) {
        key_format((u16)k, (u8)mm, t, sizeof t);
        if (!key_parse(t, &c, &m) || c != k || m != mm) { if (!bad) printf("  key round trip: %s\n", t); bad++; }
      }
    }
    ok("key: every name round-trips", !bad);
  }
}

static void cmdlists(void)
{
  CmdRef c[8];
  char t[200];
  int n = cmdlist_parse("3|Save,8|Undo,4512,0|bad,x,4100|Insert Field", c, 8);
  ok("cmdlist: parsed", n == 4 && c[0].id == 3 && !strcmp(c[0].name, "Save") && c[2].id == 4512 && !c[2].name[0] && !strcmp(c[3].name, "Insert Field"));
  strcpy(c[1].name, "Odd, name");
  cmdlist_format(c, n, t, sizeof t);
  ok("cmdlist: formatted (a name with a comma left out)", !strcmp(t, "3|Save,8,4512,4100|Insert Field"));
  ok("cmdlist: empty", cmdlist_parse("", c, 8) == 0 && cmdlist_parse(NULL, c, 8) == 0);
  cmdlist_format(c, n, t, 12);
  ok("cmdlist: cut at whole entries", !strcmp(t, "3|Save,8"));
}

static void keytips(void)
{
  /* Word's own Home tab row, in English */
  static const char *const tabs[] = { "File", "Home", "Insert", "Draw", "Design", "Layout", "References", "Mailings", "Review", "View" };
  static const char *const de[] = { "Datei", "Start", "Einf\374gen", "Zeichnen", "Entwurf", "Layout", "Referenzen", "Sendungen", "\334berpr\374fen", "Ansicht" };
  char codes[16][3];
  int i, j, bad = 0;
  keytips_assign(tabs, 10, codes);
  ok("keytips: File F", !strcmp(codes[0], "F"));
  ok("keytips: Home H", !strcmp(codes[1], "H"));
  ok("keytips: Insert I", !strcmp(codes[2], "I"));
  ok("keytips: D shared by Draw and Design", strlen(codes[3]) == 2 && codes[3][0] == 'D' && strlen(codes[4]) == 2 && strcmp(codes[3], codes[4]));
  ok("keytips: R shared", codes[6][0] == 'R' && codes[8][0] == 'R' && strlen(codes[6]) == 2 && strcmp(codes[6], codes[8]));
  /* no code starts another (so typing never stops early) */
  for (i = 0; i < 10; i++) for (j = 0; j < 10; j++) if (i != j && !strncmp(codes[j], codes[i], strlen(codes[i]))) bad++;
  ok("keytips: none is another's start", !bad);
  keytips_assign(de, 10, codes);
  ok("keytips: German umlaut as its letter", !strcmp(codes[8], "U") && codes[2][0] == 'E');
  ok("keytips: match completes", keytips_match(codes, 10, "U") == 8);
  ok("keytips: match pending", keytips_match(codes, 10, "E") == -1);
  ok("keytips: match none", keytips_match(codes, 10, "Q") == -2);
  {
    static const char *const same[] = { "Bold", "Bold", "Bold", NULL, "" };
    keytips_assign(same, 5, codes);
    ok("keytips: identical names still differ", strcmp(codes[0], codes[1]) && strcmp(codes[1], codes[2]) && strcmp(codes[0], codes[2]) && codes[0][0] == 'B');
    ok("keytips: empty names get none", !codes[3][0] && !codes[4][0]);
  }
}

static void tellme(void)
{
  ok("tell me: prefix", cmd_match("Bold", "bo") > 0);
  ok("tell me: not there", cmd_match("Bold", "x") == 0);
  ok("tell me: every word", cmd_match("Insert Table", "tab ins") > 0 && cmd_match("Insert Table", "tab pic") == 0);
  ok("tell me: accents ignored", cmd_match("Einf\374gen", "einfug") > 0 && cmd_match("Copier", "COP") > 0);
  ok("tell me: a word's start beats its middle", cmd_match("Table of Contents", "con") > cmd_match("Icons", "con"));
  ok("tell me: shorter first", cmd_match("Bold", "bold") > cmd_match("Bold Italic", "bold"));
  ok("tell me: nothing typed", cmd_match("Bold", "  ") == 0);
  ok("tell me: fold", fold_latin1(0xC9) == 'e' && fold_latin1('Q') == 'q' && fold_latin1(0xDF) == 's');
}

void test_wp26(void)
{
  prefs_store();
  keys();
  cmdlists();
  keytips();
  tellme();
}
