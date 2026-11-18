/* custom.c - see custom.h (WP26) */
#include <stdlib.h>
#include <string.h>
#include "custom.h"

/* ------------------------------------------------------------------ */
/* the prefs store */
void pstore_free(PrefStore *ps)
{
  int i;
  for (i = 0; i < ps->n; i++) { free(ps->kv[i].k); free(ps->kv[i].v); }
  free(ps->kv);
  ps->kv = NULL; ps->n = ps->cap = 0;
}
static int pfind(const PrefStore *ps, const char *key)
{
  int i;
  for (i = 0; i < ps->n; i++) if (!strcmp(ps->kv[i].k, key)) return i;
  return -1;
}
const char *pstore_get(const PrefStore *ps, const char *key)
{
  int i = pfind(ps, key);
  return i >= 0 ? ps->kv[i].v : NULL;
}
int pstore_set(PrefStore *ps, const char *key, const char *val)
{
  int i = pfind(ps, key);
  char *v;
  if (!val) {
    if (i >= 0) {
      free(ps->kv[i].k); free(ps->kv[i].v);
      memmove(&ps->kv[i], &ps->kv[i + 1], (size_t)(ps->n - i - 1) * sizeof ps->kv[0]);
      ps->n--;
    }
    return 1;
  }
  if (!(v = str_dup(val))) return 0;
  if (i >= 0) { free(ps->kv[i].v); ps->kv[i].v = v; return 1; }
  if (ps->n == ps->cap) {
    int nc = ps->cap ? ps->cap * 2 : 16;
    PrefKV *nk = (PrefKV *)realloc(ps->kv, (size_t)nc * sizeof *nk);
    if (!nk) { free(v); return 0; }
    ps->kv = nk; ps->cap = nc;
  }
  if (!(ps->kv[ps->n].k = str_dup(key))) { free(v); return 0; }
  ps->kv[ps->n].v = v;
  ps->n++;
  return 1;
}
int pstore_line(PrefStore *ps, const char *line)
{
  const char *eq = strchr(line, '=');
  char key[128];
  size_t n, len = strlen(line);
  if (!eq || eq == line) return 0;
  n = (size_t)(eq - line);
  if (n >= sizeof key) return 0;
  memcpy(key, line, n); key[n] = 0;
  while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;
  {
    char *v = str_ndup(eq + 1, len - n - 1);
    int r;
    if (!v) return 0;
    r = pstore_set(ps, key, v);
    free(v);
    return r;
  }
}
void pstore_write(const PrefStore *ps, FILE *f)
{
  int i;
  for (i = 0; i < ps->n; i++) fprintf(f, "%s=%s\n", ps->kv[i].k, ps->kv[i].v);
}

/* ------------------------------------------------------------------ */
/* keys: the raw codes' names (an English keyboard's labels for the positional codes) */
static const char *const rawnames[0x80] = {
  /* 0x00 */ "`", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "\\", NULL, "Num0",
  /* 0x10 */ "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]", NULL, "Num1", "Num2", "Num3",
  /* 0x20 */ "A", "S", "D", "F", "G", "H", "J", "K", "L", ";", "'", "#", NULL, "Num4", "Num5", "Num6",
  /* 0x30 */ "<>", "Z", "X", "C", "V", "B", "N", "M", ",", ".", "/", NULL, "Num.", "Num7", "Num8", "Num9",
  /* 0x40 */ "Space", "Backspace", "Tab", "Enter", "Return", "Esc", "Del", "Insert", "PageUp", "PageDown", "Num-", NULL, "Up", "Down", "Right", "Left",
  /* 0x50 */ "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "Num(", "Num)", "Num/", "Num*", "Num+", "Help",
  /* 0x60 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
  /* 0x70 */ "Home", "End", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
const char *key_name(u16 code) { return code < 0x80 ? rawnames[code] : NULL; }
static int ieq_n(const char *a, const char *b, size_t n)
{
  size_t i;
  for (i = 0; i < n; i++) {
    int x = (unsigned char)a[i], y = (unsigned char)b[i];
    if (x >= 'a' && x <= 'z') x -= 32;
    if (y >= 'a' && y <= 'z') y -= 32;
    if (x != y) return 0;
  }
  return b[n] == 0;
}
int key_parse(const char *s, u16 *code, u8 *mods)
{
  u8 m = 0;
  const char *p = s;
  while (*p == ' ') p++;
  for (;;) {
    const char *plus = strchr(p, '+');
    size_t n;
    int c;
    /* the part up to the next '+' (a "+" key itself: "Ctrl++" or "Num+") */
    if (plus == p) plus = strchr(p + 1, '+');
    if (plus && plus[1] == 0) plus = NULL;                       /* "Ctrl+Num+" */
    n = plus ? (size_t)(plus - p) : strlen(p);
    while (n && p[n - 1] == ' ') n--;
    if (plus) {
      if (ieq_n(p, "Shift", n)) m |= KM_SHIFT;
      else if (ieq_n(p, "Ctrl", n) || ieq_n(p, "Control", n)) m |= KM_CTRL;
      else if (ieq_n(p, "Alt", n)) m |= KM_ALT;
      else return 0;
      p = plus + 1;
      while (*p == ' ') p++;
      continue;
    }
    if (!n) return 0;
    for (c = 0; c < 0x80; c++) if (rawnames[c] && ieq_n(p, rawnames[c], n)) { *code = (u16)c; *mods = m; return 1; }
    return 0;
  }
}
void key_format(u16 code, u8 mods, char *out, size_t cap)
{
  const char *nm = key_name(code);
  char t[40];
  t[0] = 0;
  if (mods & KM_CTRL) strcat(t, "Ctrl+");
  if (mods & KM_ALT) strcat(t, "Alt+");
  if (mods & KM_SHIFT) strcat(t, "Shift+");
  if (nm) strcat(t, nm); else sprintf(t + strlen(t), "Key%02X", (unsigned)code);
  if (cap) { strncpy(out, t, cap - 1); out[cap - 1] = 0; }
}

/* ------------------------------------------------------------------ */
/* command lists: "12|Bold,4500|Insert Field" */
int cmdlist_parse(const char *s, CmdRef *out, int max)
{
  int n = 0;
  while (s && *s && n < max) {
    const char *end = strchr(s, ','), *bar;
    size_t len = end ? (size_t)(end - s) : strlen(s);
    char *e;
    unsigned long id = strtoul(s, &e, 10);
    if (e != s && id) {
      out[n].id = (u32)id; out[n].name[0] = 0;
      bar = memchr(s, '|', len);
      if (bar) {
        size_t nl = len - (size_t)(bar + 1 - s);
        if (nl >= sizeof out[n].name) nl = sizeof out[n].name - 1;
        memcpy(out[n].name, bar + 1, nl); out[n].name[nl] = 0;
      }
      n++;
    }
    if (!end) break;
    s = end + 1;
  }
  return n;
}
void cmdlist_format(const CmdRef *c, int n, char *out, size_t cap)
{
  size_t o = 0;
  int i;
  if (!cap) return;
  out[0] = 0;
  for (i = 0; i < n; i++) {
    char t[80];
    int clean = !strpbrk(c[i].name, ",|;=\n");
    snprintf(t, sizeof t, "%s%lu%s%s", i ? "," : "", (unsigned long)c[i].id, clean && c[i].name[0] ? "|" : "", clean ? c[i].name : "");
    if (o + strlen(t) + 1 > cap) break;
    strcpy(out + o, t); o += strlen(t);
  }
}

/* ------------------------------------------------------------------ */
/* KeyTips */
int fold_latin1(int c)
{
  /* Latin-1 0xC0-0xFF without their accents (0 where there is no plain letter) */
  static const char acc[] = "aaaaaaaceeeeiiiidnooooo\0ouuuuyts" "aaaaaaaceeeeiiiidnooooo\0ouuuuyty";
  c &= 0xFF;
  if (c >= 'A' && c <= 'Z') return c + 32;
  if (c >= 0xC0 && acc[c - 0xC0]) return (unsigned char)acc[c - 0xC0];
  return c;
}
/* a name's character as a KeyTip letter: A-Z or 0-9 (accents dropped), else 0 */
static int tipch(int c)
{
  c = fold_latin1(c);
  if (c >= 'a' && c <= 'z') return c - 32;
  if (c >= '0' && c <= '9') return c;
  return 0;
}
void keytips_assign(const char *const *names, int n, char (*codes)[3])
{
  int i, k;
  for (i = 0; i < n; i++) codes[i][0] = 0;
  for (i = 0; i < n; i++) {
    const char *s = names[i];
    int first = 0, j, count = 0;
    if (codes[i][0] || !s) continue;
    for (; *s && !(first = tipch((unsigned char)*s)); s++) ;
    if (!first) continue;
    /* everyone else whose first letter is the same */
    for (j = i; j < n; j++) {
      const char *t = names[j];
      int f = 0;
      if (!t) continue;
      for (; *t && !(f = tipch((unsigned char)*t)); t++) ;
      if (f == first) count++;
    }
    if (count == 1) { codes[i][0] = (char)first; codes[i][1] = 0; continue; }
    /* two letters: the first, then a later one of the name's own no other in the group has taken
       (a digit when none is left) */
    for (j = i; j < n; j++) {
      const char *t = names[j], *q;
      int f = 0, got = 0;
      if (!t || codes[j][0]) continue;
      for (; *t && !(f = tipch((unsigned char)*t)); t++) ;
      if (f != first) continue;
      for (q = t + 1; *q && !got; q++) {
        int c2 = tipch((unsigned char)*q), dup = 0;
        if (!c2) continue;
        for (k = 0; k < n; k++) if (codes[k][0] == first && codes[k][1] == c2) dup = 1;
        if (!dup) { codes[j][0] = (char)first; codes[j][1] = (char)c2; codes[j][2] = 0; got = 1; }
      }
      for (q = "123456789"; !got && *q; q++) {
        int dup = 0;
        for (k = 0; k < n; k++) if (codes[k][0] == first && codes[k][1] == *q) dup = 1;
        if (!dup) { codes[j][0] = (char)first; codes[j][1] = *q; codes[j][2] = 0; got = 1; }
      }
    }
  }
}
int keytips_match(char (*codes)[3], int n, const char *typed)
{
  size_t tl = strlen(typed);
  int i, starts = 0;
  for (i = 0; i < n; i++) {
    if (!codes[i][0] || strncmp(codes[i], typed, tl)) continue;
    if (strlen(codes[i]) == tl) return i;
    starts++;
  }
  return starts ? -1 : -2;
}

/* ------------------------------------------------------------------ */
/* Tell Me */
static int is_wordch(int c) { c = fold_latin1(c); return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0xDF; }
/* where word w (length wl, folded) is in name: 3 the name's start, 2 a word's start, 1 inside a
   word, 0 nowhere */
static int find_word(const char *name, const char *w, size_t wl)
{
  size_t i, j, nl = strlen(name);
  int best = 0;
  for (i = 0; i + wl <= nl; i++) {
    for (j = 0; j < wl && fold_latin1((unsigned char)name[i + j]) == (unsigned char)w[j]; j++) ;
    if (j < wl) continue;
    {
      int sc = i == 0 ? 3 : !is_wordch((unsigned char)name[i - 1]) ? 2 : 1;
      if (sc > best) best = sc;
    }
  }
  return best;
}
int cmd_match(const char *name, const char *query)
{
  char w[64];
  int total = 0, words = 0;
  const char *q = query;
  if (!name || !query) return 0;
  for (;;) {
    size_t wl = 0;
    int sc;
    while (*q && !is_wordch((unsigned char)*q)) q++;
    if (!*q) break;
    while (*q && is_wordch((unsigned char)*q)) { if (wl < sizeof w - 1) w[wl++] = (char)fold_latin1((unsigned char)*q); q++; }
    w[wl] = 0;
    if (!(sc = find_word(name, w, wl))) return 0;
    total += sc * 10 + (int)wl; words++;
  }
  if (!words) return 0;
  /* shorter names first among equals: "Bold" before "Bold Italic Underline" */
  return total * 4 + (40 - (int)(strlen(name) > 40 ? 40 : strlen(name))) / 8 + 1;
}
int cmd_search(const char *const *names, const char *const *alt, int n, const char *query, int *out, int max)
{
  int score[64], i, j, got = 0;
  if (max > 64) max = 64;
  if (!query || max <= 0) return 0;
  for (i = 0; i < n; i++) {
    int s = names[i] ? cmd_match(names[i], query) : 0;
    if (!s && alt && alt[i]) s = cmd_match(alt[i], query) / 2;
    if (!s) continue;
    for (j = got; j > 0 && score[j - 1] < s; j--) if (j < max) { score[j] = score[j - 1]; out[j] = out[j - 1]; }
    if (j < max) { score[j] = s; out[j] = i; if (got < max) got++; }
  }
  return got;
}

/* ------------------------------------------------------------------ */
/* the key map */
int keymap_init(KeyMap *km, const KeyBind *def, int ndef)
{
  km->def = def; km->ndef = ndef; km->k = NULL; km->n = km->cap = 0;
  keymap_reset(km);
  return km->n == ndef;
}
void keymap_free(KeyMap *km) { free(km->k); km->k = NULL; km->n = km->cap = 0; }
static int kfind(const KeyMap *km, u16 code, u8 mods)
{
  int i;
  for (i = 0; i < km->n; i++) if (km->k[i].code == code && km->k[i].mods == mods) return i;
  return -1;
}
int keymap_set(KeyMap *km, u16 code, u8 mods, u32 id)
{
  int i = kfind(km, code, mods);
  if (i >= 0) { km->k[i].id = id; return 1; }
  if (km->n == km->cap) {
    int nc = km->cap ? km->cap * 2 : 64;
    KeyBind *nk = (KeyBind *)realloc(km->k, (size_t)nc * sizeof *nk);
    if (!nk) return 0;
    km->k = nk; km->cap = nc;
  }
  km->k[km->n].code = code; km->k[km->n].mods = mods; km->k[km->n].id = id;
  km->n++;
  return 1;
}
u32 keymap_get(const KeyMap *km, u16 code, u8 mods)
{
  int i = kfind(km, code, mods);
  return i >= 0 ? km->k[i].id : 0;
}
u32 keymap_default(const KeyMap *km, u16 code, u8 mods)
{
  int i;
  for (i = 0; i < km->ndef; i++) if (km->def[i].code == code && km->def[i].mods == mods) return km->def[i].id;
  return 0;
}
int keymap_changed(const KeyMap *km, u16 code, u8 mods) { return keymap_get(km, code, mods) != keymap_default(km, code, mods); }
int keymap_keys_of(const KeyMap *km, u32 id, KeyBind *out, int max)
{
  int i, n = 0;
  if (!id) return 0;
  for (i = 0; i < km->n; i++) if (km->k[i].id == id) { if (out && n < max) out[n] = km->k[i]; n++; }
  return out && n > max ? max : n;
}
void keymap_unbind(KeyMap *km, u32 id)
{
  int i;
  for (i = 0; i < km->n; i++) if (km->k[i].id == id) km->k[i].id = 0;
}
void keymap_reset(KeyMap *km)
{
  int i;
  km->n = 0;
  for (i = 0; i < km->ndef; i++) keymap_set(km, km->def[i].code, km->def[i].mods, km->def[i].id);
}
void keymap_load(KeyMap *km, const PrefStore *ps, u32 (*resolve)(const CmdRef *r, void *u), void *u)
{
  int i;
  for (i = 0; i < ps->n; i++) {
    u16 code; u8 mods;
    CmdRef r;
    if (strncmp(ps->kv[i].k, "key.", 4) || !key_parse(ps->kv[i].k + 4, &code, &mods)) continue;
    keymap_set(km, code, mods, cmdlist_parse(ps->kv[i].v, &r, 1) == 1 ? resolve(&r, u) : 0);
  }
}
void keymap_save(const KeyMap *km, PrefStore *ps, void (*name)(u32 id, CmdRef *r, void *u), void *u)
{
  int i;
  for (i = ps->n - 1; i >= 0; i--) if (!strncmp(ps->kv[i].k, "key.", 4)) pstore_set(ps, ps->kv[i].k, NULL);
  for (i = 0; i < km->n; i++) {
    char k[48], v[80];
    CmdRef r;
    if (!keymap_changed(km, km->k[i].code, km->k[i].mods)) continue;
    strcpy(k, "key.");
    key_format(km->k[i].code, km->k[i].mods, k + 4, sizeof k - 4);
    if (km->k[i].id) { memset(&r, 0, sizeof r); r.id = km->k[i].id; name(km->k[i].id, &r, u); cmdlist_format(&r, 1, v, sizeof v); }
    else strcpy(v, "0");
    pstore_set(ps, k, v);
  }
}

/* ------------------------------------------------------------------ */
/* the ribbon's customization */
static void copyn(char *d, const char *s, size_t n, size_t cap)
{
  if (n >= cap) n = cap - 1;
  memcpy(d, s, n); d[n] = 0;
}
void rcustom_clean_name(char *s) { for (; *s; s++) if (*s == ':' || *s == ';' || *s == ',' || *s == '|' || *s == '=' || *s == '\n' || *s == '\r') *s = ' '; }
void rcustom_read(RibbonCustom *rc, const PrefStore *ps)
{
  const char *s;
  int g;
  memset(rc, 0, sizeof *rc);
  if ((s = pstore_get(ps, "ribbon.hide")))
    while (*s && rc->nhide < RC_MAXHIDE) {
      size_t l = strcspn(s, ";");
      if (l && l < sizeof rc->hide[0]) { copyn(rc->hide[rc->nhide], s, l, sizeof rc->hide[0]); rc->nhide++; }
      s += l; if (*s) s++;
    }
  if ((s = pstore_get(ps, "ribbon.tab"))) copyn(rc->tabname, s, strlen(s), sizeof rc->tabname);
  for (g = 0; g < RC_MAXGROUP; g++) {
    char key[32];
    const char *colon;
    RcGroup *gr = &rc->group[rc->ngroup];
    sprintf(key, "ribbon.group%d", g + 1);
    if (!(s = pstore_get(ps, key)) || !(colon = strchr(s, ':'))) continue;
    copyn(gr->name, s, (size_t)(colon - s), sizeof gr->name);
    gr->n = cmdlist_parse(colon + 1, gr->cmd, RC_MAXCMD);
    rc->ngroup++;
  }
  if ((s = pstore_get(ps, "qat"))) { rc->qatset = 1; rc->nqat = cmdlist_parse(s, rc->qat, RC_MAXQAT); }
}
int rcustom_write(const RibbonCustom *rc, PrefStore *ps)
{
  char buf[RC_MAXHIDE * 64 + 16], key[32];
  int i, g, ok = 1;
  size_t o = 0;
  buf[0] = 0;
  for (i = 0; i < rc->nhide; i++) {
    size_t l = strlen(rc->hide[i]);
    if (o + l + 2 >= sizeof buf) break;
    if (o) buf[o++] = ';';
    memcpy(buf + o, rc->hide[i], l + 1); o += l;
  }
  ok &= pstore_set(ps, "ribbon.hide", rc->nhide ? buf : NULL);
  ok &= pstore_set(ps, "ribbon.tab", rc->tabname[0] ? rc->tabname : NULL);
  for (g = 0; g < RC_MAXGROUP; g++) {
    sprintf(key, "ribbon.group%d", g + 1);
    if (g >= rc->ngroup) { ok &= pstore_set(ps, key, NULL); continue; }
    snprintf(buf, sizeof buf, "%s:", rc->group[g].name);
    cmdlist_format(rc->group[g].cmd, rc->group[g].n, buf + strlen(buf), sizeof buf - strlen(buf));
    ok &= pstore_set(ps, key, buf);
  }
  if (rc->qatset) { cmdlist_format(rc->qat, rc->nqat, buf, sizeof buf); ok &= pstore_set(ps, "qat", buf); }
  else ok &= pstore_set(ps, "qat", NULL);
  return ok;
}
static void hide_key(const char *tab, const char *group, char *out, size_t cap)
{
  if (group) snprintf(out, cap, "%s/%s", tab, group); else snprintf(out, cap, "%s", tab);
}
int rcustom_hidden(const RibbonCustom *rc, const char *tab, const char *group)
{
  char t[100];
  int i;
  hide_key(tab, group, t, sizeof t);
  for (i = 0; i < rc->nhide; i++) if (!strcmp(rc->hide[i], t)) return 1;
  return 0;
}
void rcustom_set_hidden(RibbonCustom *rc, const char *tab, const char *group, int hide)
{
  char t[100];
  int i;
  hide_key(tab, group, t, sizeof t);
  for (i = 0; i < rc->nhide && strcmp(rc->hide[i], t); i++) ;
  if (hide && i == rc->nhide && rc->nhide < RC_MAXHIDE && strlen(t) < sizeof rc->hide[0]) strcpy(rc->hide[rc->nhide++], t);
  if (!hide && i < rc->nhide) { memmove(rc->hide[i], rc->hide[i + 1], (size_t)(rc->nhide - i - 1) * sizeof rc->hide[0]); rc->nhide--; }
}
