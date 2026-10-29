/* autocorrect.c - see autocorrect.h (WP8) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "autocorrect.h"
#include "proflang.h"

/* Quill's replacement list: symbols, then common slips (Word's kind of list; these entries are Quill's) */
static const char *const def_list[][2] = {
  { "(c)", "\xC2\xA9" }, { "(r)", "\xC2\xAE" }, { "(tm)", "\xE2\x84\xA2" }, { "(e)", "\xE2\x82\xAC" }, { "...", "\xE2\x80\xA6" },
  { "-->", "\xE2\x86\x92" }, { "<--", "\xE2\x86\x90" }, { "==>", "\xE2\x87\x92" }, { "<==", "\xE2\x87\x90" }, { "<=>", "\xE2\x87\x94" },
  { "i", "I" }, { "abbout", "about" }, { "abotu", "about" }, { "accomodate", "accommodate" }, { "accross", "across" },
  { "acheive", "achieve" }, { "acn", "can" }, { "adn", "and" }, { "agian", "again" }, { "ahve", "have" }, { "alot", "a lot" },
  { "alwasy", "always" }, { "aquire", "acquire" }, { "arguement", "argument" }, { "aslo", "also" }, { "basicly", "basically" },
  { "beacuse", "because" }, { "becasue", "because" }, { "becuase", "because" }, { "begining", "beginning" }, { "beleive", "believe" },
  { "calender", "calendar" }, { "cant", "can't" }, { "cemetary", "cemetery" }, { "collegue", "colleague" }, { "comming", "coming" },
  { "commitee", "committee" }, { "completly", "completely" }, { "concious", "conscious" }, { "couldnt", "couldn't" },
  { "decieve", "deceive" }, { "definately", "definitely" }, { "didnt", "didn't" }, { "dissapear", "disappear" },
  { "dissapoint", "disappoint" }, { "doesnt", "doesn't" }, { "doign", "doing" }, { "dont", "don't" }, { "embarass", "embarrass" },
  { "enviroment", "environment" }, { "existance", "existence" }, { "familar", "familiar" }, { "finaly", "finally" },
  { "foriegn", "foreign" }, { "freind", "friend" }, { "goverment", "government" }, { "happend", "happened" }, { "harrass", "harass" },
  { "hasnt", "hasn't" }, { "havent", "haven't" }, { "hsa", "has" }, { "hte", "the" }, { "htis", "this" }, { "immediatly", "immediately" },
  { "independant", "independent" }, { "isnt", "isn't" }, { "jsut", "just" }, { "knowlege", "knowledge" }, { "knwo", "know" },
  { "libary", "library" }, { "liek", "like" }, { "maintainance", "maintenance" }, { "mkae", "make" }, { "nad", "and" },
  { "neccessary", "necessary" }, { "noticable", "noticeable" }, { "nto", "not" }, { "occassion", "occasion" }, { "occured", "occurred" },
  { "occurence", "occurrence" }, { "peopel", "people" }, { "persistant", "persistent" }, { "posession", "possession" },
  { "prefered", "preferred" }, { "probelm", "problem" }, { "publically", "publicly" }, { "realy", "really" }, { "recieve", "receive" },
  { "reccomend", "recommend" }, { "refered", "referred" }, { "relevent", "relevant" }, { "religous", "religious" },
  { "remeber", "remember" }, { "seperate", "separate" }, { "shoudl", "should" }, { "shouldnt", "shouldn't" }, { "smae", "same" },
  { "somethign", "something" }, { "succesful", "successful" }, { "suprise", "surprise" }, { "taht", "that" }, { "teh", "the" },
  { "tehre", "there" }, { "tendancy", "tendency" }, { "thier", "their" }, { "thsi", "this" }, { "thta", "that" }, { "tihs", "this" },
  { "tommorow", "tomorrow" }, { "tomorow", "tomorrow" }, { "truely", "truly" }, { "unfortunatly", "unfortunately" }, { "untill", "until" },
  { "useing", "using" }, { "visable", "visible" }, { "waht", "what" }, { "wasnt", "wasn't" }, { "wether", "whether" }, { "wich", "which" },
  { "whcih", "which" }, { "whihc", "which" }, { "wierd", "weird" }, { "wiht", "with" }, { "wouldnt", "wouldn't" }, { "writting", "writing" },
  { "yeild", "yield" }, { "yoru", "your" }, { "youre", "you're" }, { "theyre", "they're" }, { "whats", "what's" },
};
static const char *const def_exc[] = { "a.m.", "apr.", "aug.", "ca.", "cf.", "co.", "corp.", "dec.", "dept.", "dr.", "e.g.", "etc.", "feb.",
  "fig.", "i.e.", "inc.", "jan.", "jr.", "jul.", "jun.", "ltd.", "mar.", "mr.", "mrs.", "ms.", "no.", "nov.", "oct.", "p.m.", "pp.", "prof.",
  "sep.", "sept.", "sr.", "st.", "vs.", "viz.", "vol.", "approx.", "min.", "max.", NULL };
static const char *const def_exc2[] = { "IDs", "PCs", "CDs", "DVDs", "TVs", "URLs", "OSes", "PDFs", "APIs", NULL };
static const char *const days[] = { "monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday", NULL };

/* ---- the lists */
static int lst_add(char ***v, int *n, int *cap, const char *s)
{
  char *c;
  if (*n >= *cap) {
    int nc = *cap ? *cap * 2 : 32;
    char **nv = (char **)realloc(*v, (size_t)nc * sizeof(char *));
    if (!nv) return 0;
    *v = nv; *cap = nc;
  }
  if (!(c = str_dup(s))) return 0;
  (*v)[(*n)++] = c;
  return 1;
}
static int lst_find(char **v, int n, const char *s, int nocase)
{
  int i;
  for (i = 0; i < n; i++) if (nocase ? str_ieq(v[i], s) : !strcmp(v[i], s)) return i;
  return -1;
}
static void lst_free(char ***v, int *n, int *cap)
{
  int i;
  for (i = 0; i < *n; i++) free((*v)[i]);
  free(*v); *v = NULL; *n = *cap = 0;
}
int ac_find(const AutoCorrect *ac, const char *from)
{
  int lo = 0, hi = ac->n;
  while (lo < hi) {
    int mid = (lo + hi) / 2, c = strcmp(from, ac->from[mid]);
    if (!c) return mid;
    if (c < 0) hi = mid; else lo = mid + 1;
  }
  return -1;
}
int ac_add(AutoCorrect *ac, const char *from, const char *to)
{
  int k = ac_find(ac, from), i;
  char *f, *t;
  if (!from[0]) return 0;
  if (k >= 0) {
    if (!(t = str_dup(to))) return 0;
    free(ac->to[k]); ac->to[k] = t;
    return 1;
  }
  if (ac->n >= ac->cap) {
    int nc = ac->cap ? ac->cap * 2 : 256;
    char **nf = (char **)realloc(ac->from, (size_t)nc * sizeof(char *)), **nt;
    if (!nf) return 0;
    ac->from = nf;
    if (!(nt = (char **)realloc(ac->to, (size_t)nc * sizeof(char *)))) return 0;
    ac->to = nt; ac->cap = nc;
  }
  if (!(f = str_dup(from)) || !(t = str_dup(to))) { free(f); return 0; }
  for (i = ac->n; i > 0 && strcmp(ac->from[i - 1], from) > 0; i--) { ac->from[i] = ac->from[i - 1]; ac->to[i] = ac->to[i - 1]; }
  ac->from[i] = f; ac->to[i] = t; ac->n++;
  return 1;
}
int ac_remove(AutoCorrect *ac, const char *from)
{
  int k = ac_find(ac, from);
  if (k < 0) return 0;
  free(ac->from[k]); free(ac->to[k]);
  memmove(ac->from + k, ac->from + k + 1, (size_t)(ac->n - k - 1) * sizeof(char *));
  memmove(ac->to + k, ac->to + k + 1, (size_t)(ac->n - k - 1) * sizeof(char *));
  ac->n--;
  return 1;
}
int ac_exc_add(AutoCorrect *ac, int which, const char *word)
{
  if (!word[0]) return 0;
  if (which) return lst_find(ac->exc2, ac->nexc2, word, 0) >= 0 || lst_add(&ac->exc2, &ac->nexc2, &ac->exc2cap, word);
  return lst_find(ac->exc, ac->nexc, word, 1) >= 0 || lst_add(&ac->exc, &ac->nexc, &ac->exccap, word);
}
int ac_exc_remove(AutoCorrect *ac, int which, const char *word)
{
  char ***v = which ? &ac->exc2 : &ac->exc;
  int *n = which ? &ac->nexc2 : &ac->nexc, k = lst_find(*v, *n, word, !which);
  if (k < 0) return 0;
  free((*v)[k]);
  memmove(*v + k, *v + k + 1, (size_t)(*n - k - 1) * sizeof(char *));
  (*n)--;
  return 1;
}
void ac_init(AutoCorrect *ac)
{
  size_t i;
  memset(ac, 0, sizeof *ac);
  ac->opts = AC_ALL;
  for (i = 0; i < sizeof def_list / sizeof def_list[0]; i++) ac_add(ac, def_list[i][0], def_list[i][1]);
  for (i = 0; def_exc[i]; i++) ac_exc_add(ac, 0, def_exc[i]);
  for (i = 0; def_exc2[i]; i++) ac_exc_add(ac, 1, def_exc2[i]);
}
void ac_free(AutoCorrect *ac)
{
  int i;
  for (i = 0; i < ac->n; i++) { free(ac->from[i]); free(ac->to[i]); }
  free(ac->from); free(ac->to);
  lst_free(&ac->exc, &ac->nexc, &ac->exccap);
  lst_free(&ac->exc2, &ac->nexc2, &ac->exc2cap);
  memset(ac, 0, sizeof *ac);
}
/* "Quill AutoCorrect 1", then "options <hex>", "replace\tfrom\tto", "first\tword", "caps\tword" lines */
void ac_save(const AutoCorrect *ac, Buf *out)
{
  int i;
  buf_printf(out, "Quill AutoCorrect 1\noptions %lx\n", (unsigned long)ac->opts);
  for (i = 0; i < ac->n; i++) { buf_adds(out, "replace\t"); buf_adds(out, ac->from[i]); buf_addc(out, '\t'); buf_adds(out, ac->to[i]); buf_addc(out, '\n'); }
  for (i = 0; i < ac->nexc; i++) { buf_adds(out, "first\t"); buf_adds(out, ac->exc[i]); buf_addc(out, '\n'); }
  for (i = 0; i < ac->nexc2; i++) { buf_adds(out, "caps\t"); buf_adds(out, ac->exc2[i]); buf_addc(out, '\n'); }
}
int ac_load(AutoCorrect *ac, const char *text, size_t n)
{
  char *t = str_ndup(text, n), *line, *next;
  if (!t) return 0;
  if (strncmp(t, "Quill AutoCorrect 1", 19)) { free(t); return 0; }
  ac_free(ac);
  ac->opts = AC_ALL;
  for (line = t; line && *line; line = next) {
    char *tab, *tab2;
    next = strchr(line, '\n');
    if (next) *next++ = 0;
    { size_t l = strlen(line); if (l && line[l - 1] == '\r') line[l - 1] = 0; }
    if (!strncmp(line, "options ", 8)) { ac->opts = (unsigned)strtoul(line + 8, NULL, 16); continue; }
    if (!(tab = strchr(line, '\t'))) continue;
    *tab++ = 0;
    if (!strcmp(line, "replace") && (tab2 = strchr(tab, '\t'))) { *tab2++ = 0; ac_add(ac, tab, tab2); }
    else if (!strcmp(line, "first")) ac_exc_add(ac, 0, tab);
    else if (!strcmp(line, "caps")) ac_exc_add(ac, 1, tab);
  }
  free(t);
  return 1;
}

/* ---- editing */
typedef struct { Doc *d; Pos *caret; int rid; int changes; } Ctx;
static int is_alpha(u8 c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_digit(u8 c) { return c >= '0' && c <= '9'; }
static int is_space(u8 c) { return c == ' ' || c == '\t' || c == '\v'; }
static int is_upper(u8 c) { return c >= 'A' && c <= 'Z'; }
static int is_lower(u8 c) { return c >= 'a' && c <= 'z'; }
/* a..b of paragraph pi replaced by s (in the formatting a had; tracked when c->rid >= 0); the caret
   (if later in the paragraph) kept on its text */
static void replace(Ctx *c, u32 pi, u32 a, u32 b, const char *s)
{
  Para *p = c->d->p[pi];
  CharFmt f = *para_fmt_at(p, a);
  u32 n = (u32)strlen(s), before = p->len;
  Pos at, da, db;
  at.para = da.para = db.para = pi; at.off = a;
  if (c->rid >= 0) { f.rev = REV_INS; f.rid = (u16)c->rid; }
  if (n) doc_insert(c->d, &at, s, n, &f);
  da.off = a + n; db.off = b + n;
  if (b > a) { if (c->rid >= 0) doc_track_delete(c->d, da, db, c->rid); else doc_delete(c->d, da, db); }
  if (c->caret && c->caret->para == pi && c->caret->off >= b) c->caret->off = c->caret->off + c->d->p[pi]->len - before;
  c->changes++;
}
static void del(Ctx *c, u32 pi, u32 a, u32 b) { replace(c, pi, a, b, ""); }
/* the run of non-space characters ending at e in paragraph pi: its start */
static u32 tok_start(const char *t, u32 e)
{
  while (e > 0 && !is_space((u8)t[e - 1])) e--;
  return e;
}
static void lower_ascii(char *s) { for (; *s; s++) if (is_upper((u8)*s)) *s = (char)(*s + 32); }

/* the replacement list on a..b (the token): as typed, or with its case carried over */
static int do_replace(AutoCorrect *ac, Ctx *c, u32 pi, u32 a, u32 b)
{
  const char *t = c->d->p[pi]->t;
  char w[80], low[80], out[160];
  int k, allcap = 1, i;
  if (b <= a || b - a >= sizeof w) return 0;
  memcpy(w, t + a, b - a); w[b - a] = 0;
  if ((k = ac_find(ac, w)) >= 0) { if (strcmp(ac->to[k], w)) { replace(c, pi, a, b, ac->to[k]); return 1; } return 0; }
  strcpy(low, w); lower_ascii(low);
  if (!strcmp(low, w) || (k = ac_find(ac, low)) < 0) return 0;
  /* Teh -> The, TEH -> THE (a list entry with capitals of its own is used only as typed) */
  for (i = 0; ac->from[k][i]; i++) if (is_upper((u8)ac->from[k][i])) return 0;
  for (i = 1; w[i]; i++) if (is_lower((u8)w[i])) allcap = 0;
  if (!is_upper((u8)w[0])) return 0;
  strncpy(out, ac->to[k], sizeof out - 1); out[sizeof out - 1] = 0;
  if (allcap && strlen(w) > 1) { for (i = 0; out[i]; i++) if (is_lower((u8)out[i])) out[i] = (char)(out[i] - 32); }
  else if (is_lower((u8)out[0])) out[0] = (char)(out[0] - 32);
  replace(c, pi, a, b, out);
  return 1;
}
/* a word of letters at a..b (ASCII) */
static int letters_only(const char *t, u32 a, u32 b)
{
  u32 i;
  for (i = a; i < b; i++) if (!is_alpha((u8)t[i]) && t[i] != '\'') return 0;
  return b > a;
}
/* is the word at s (paragraph pi) the first of a sentence? (the paragraph's start, or after . ! ?
   and a space, the word before not one of the exceptions) */
static int sentence_start(AutoCorrect *ac, Doc *d, u32 pi, u32 s)
{
  const char *t = d->p[pi]->t;
  u32 j = s, e, ts;
  char prev[48];
  while (j > 0 && (t[j - 1] == '"' || t[j - 1] == '(' || t[j - 1] == '\'')) j--;
  if (j >= 3 && (u8)t[j - 3] == 0xE2 && (u8)t[j - 2] == 0x80 && ((u8)t[j - 1] == 0x9C || (u8)t[j - 1] == 0x98)) j -= 3;
  if (j == 0) return !in_table(&d->p[pi]->pf) || (ac->opts & AC_CELLS);
  e = j;
  while (e > 0 && t[e - 1] == ' ') e--;
  if (e == j || e == 0) return 0;
  if (t[e - 1] == '"' || t[e - 1] == ')' || t[e - 1] == '\'') e--;
  if (e == 0 || !(t[e - 1] == '.' || t[e - 1] == '!' || t[e - 1] == '?')) return 0;
  if (t[e - 1] == '.' && e >= 2 && t[e - 2] == '.') return 0;          /* an ellipsis */
  ts = tok_start(t, e);
  if (e - ts >= sizeof prev) return 1;
  memcpy(prev, t + ts, e - ts); prev[e - ts] = 0;
  return lst_find(ac->exc, ac->nexc, prev, 1) < 0;
}
static int is_link_text(const char *w)
{
  const char *at;
  if (!strncmp(w, "http://", 7) || !strncmp(w, "https://", 8) || !strncmp(w, "ftp://", 6) || !strncmp(w, "mailto:", 7)) return strlen(w) > 8;
  if (!strncmp(w, "www.", 4)) return strchr(w + 4, '.') != NULL;
  if ((at = strchr(w, '@')) && at > w && strchr(at, '.') && at[1] != '.' && w[strlen(w) - 1] != '.') {
    const char *q;
    for (q = w; *q; q++) if (!(is_alpha((u8)*q) || is_digit((u8)*q) || strchr("@.-_+", *q))) return 0;
    return 1;
  }
  return 0;
}
/* the word finished at e (a terminator follows at e) in paragraph pi: AutoCorrect and the word-sized
   AutoFormat changes. Returns the AC_/AF_ bit of what was done */
static unsigned word_done(AutoCorrect *ac, Ctx *c, u32 pi, u32 e)
{
  Doc *d = c->d;
  const char *t = d->p[pi]->t;
  u32 ts = tok_start(t, e), ws, we, i;
  unsigned did = 0;
  int en;
  char tok[256];
  if (e <= ts) return 0;
  if (e - ts >= sizeof tok) return 0;
  memcpy(tok, t + ts, e - ts); tok[e - ts] = 0;
  /* addresses become links (not inside one already) */
  if ((ac->opts & AF_LINKS) && is_link_text(tok)) {
    Pos a, b;
    char url[300];
    a.para = b.para = pi; a.off = ts; b.off = e;
    if (!doc_link_at(d, a, NULL, NULL)) {
      if (!strncmp(tok, "www.", 4)) sprintf(url, "http://%s", tok);
      else if (strchr(tok, '@') && strncmp(tok, "mailto:", 7) && !strstr(tok, "://")) sprintf(url, "mailto:%s", tok);
      else strcpy(url, tok);
      if (doc_set_link(d, a, b, url, NULL, NULL)) { c->changes++; return AF_LINKS; }
    }
    return 0;
  }
  /* *bold* and _italic_: the closing mark just before e, the opening one at a word's start */
  if ((ac->opts & AF_BOLDITALIC) && e >= 3 && (t[e - 1] == '*' || t[e - 1] == '_') && !is_space((u8)t[e - 2]) && t[e - 2] != t[e - 1]) {
    char m = t[e - 1];
    u32 o;
    for (o = e - 2; o-- > 0; ) {
      if (t[o] != m) continue;
      if ((o == 0 || is_space((u8)t[o - 1]) || t[o - 1] == '(' || t[o - 1] == '"') && !is_space((u8)t[o + 1]) && o + 1 < e - 1) {
        CharFmt v;
        Pos a, b;
        memset(&v, 0, sizeof v);
        v.flags = m == '*' ? CF_BOLD : CF_ITALIC;
        a.para = b.para = pi; a.off = o + 1; b.off = e - 1;
        doc_format(d, a, b, m == '*' ? CM_BOLD : CM_ITALIC, &v);
        del(c, pi, e - 1, e);
        del(c, pi, o, o + 1);
        return AF_BOLDITALIC;
      }
      break;
    }
  }
  /* the replacement list: the whole token, or the word in it (without quotes and brackets) */
  ws = ts;
  while (ws < e && (t[ws] == '"' || t[ws] == '(' || t[ws] == '\'' || t[ws] == '[')) ws++;
  we = e;
  while (we > ws && (t[we - 1] == '"' || t[we - 1] == ')' || t[we - 1] == '\'' || t[we - 1] == ']' || t[we - 1] == '.' || t[we - 1] == ',' ||
                     t[we - 1] == ';' || t[we - 1] == ':' || t[we - 1] == '!' || t[we - 1] == '?')) we--;
  /* (the word list and the days are English: text in other languages keeps its words) */
  en = plang_primary(plang_of(para_fmt_at(d->p[pi], ts)), "en");
  if ((ac->opts & AC_REPLACE) && en) {
    u32 before = d->p[pi]->len;
    if (do_replace(ac, c, pi, ts, e) || ((ws != ts || we != e) && do_replace(ac, c, pi, ws, we))) {
      /* replaced: the word may still start a sentence ("teh" at the start: "The") */
      u32 after = d->p[pi]->len;
      t = d->p[pi]->t;
      e = e + after - before;
      ws = ts;
      while (ws < e && (t[ws] == '"' || t[ws] == '(' || t[ws] == '\'' || t[ws] == '[')) ws++;
      we = e;
      while (we > ws && strchr("\")'].,;:!?", t[we - 1])) we--;
      if ((ac->opts & AC_SENTENCE) && we > ws && is_lower((u8)t[ws]) && letters_only(t, ws, we) && sentence_start(ac, d, pi, ts)) {
        char l[2];
        l[0] = (char)(t[ws] - 32); l[1] = 0;
        replace(c, pi, ws, ws + 1, l);
      }
      return AC_REPLACE;
    }
  }
  t = d->p[pi]->t;
  /* fractions and ordinals */
  if ((ac->opts & AF_FRACTIONS) && ws == ts && we - ws == 3 && t[ws + 1] == '/') {
    const char *fr = !strncmp(t + ws, "1/2", 3) ? "\xC2\xBD" : !strncmp(t + ws, "1/4", 3) ? "\xC2\xBC" : !strncmp(t + ws, "3/4", 3) ? "\xC2\xBE" : NULL;
    if (fr) { replace(c, pi, ws, we, fr); return AF_FRACTIONS; }
  }
  if ((ac->opts & AF_ORDINALS) && we - ws >= 3 && is_digit((u8)t[ws])) {
    u32 k = ws;
    unsigned long num = 0;
    while (k < we && is_digit((u8)t[k])) num = num * 10 + (unsigned long)(t[k++] - '0');
    if (we - k == 2) {
      char sfx[3], want[3];
      unsigned long r100 = num % 100, r10 = num % 10;
      sfx[0] = t[k]; sfx[1] = t[k + 1]; sfx[2] = 0; lower_ascii(sfx);
      strcpy(want, r100 >= 11 && r100 <= 13 ? "th" : r10 == 1 ? "st" : r10 == 2 ? "nd" : r10 == 3 ? "rd" : "th");
      if (!strcmp(sfx, want) && !(para_fmt_at(d->p[pi], k)->flags & CF_SUPER)) {
        CharFmt v;
        Pos a, b;
        memset(&v, 0, sizeof v); v.flags = CF_SUPER;
        a.para = b.para = pi; a.off = k; b.off = we;
        doc_format(d, a, b, CM_SUPER | CM_SUB, &v);
        c->changes++;
        return AF_ORDINALS;
      }
    }
  }
  /* word--word: an em dash */
  if (ac->opts & AF_DASHES) {
    for (i = ts + 1; i + 2 < e; i++)
      if (t[i] == '-' && t[i + 1] == '-' && (is_alpha((u8)t[i - 1]) || is_digit((u8)t[i - 1])) && (is_alpha((u8)t[i + 2]) || is_digit((u8)t[i + 2]))) {
        replace(c, pi, i, i + 2, "\xE2\x80\x94"); return AF_DASHES;
      }
    /* word - word (or --): an en dash, once the word after it is finished */
    if (ts >= 4 && t[ts - 1] == ' ') {
      u32 h = ts - 1, h0;
      h0 = h;
      while (h0 > 0 && t[h0 - 1] == '-' && h - h0 < 2) h0--;
      if (h0 < h && h0 >= 2 && t[h0 - 1] == ' ' && !is_space((u8)t[h0 - 2])) { replace(c, pi, h0, h, "\xE2\x80\x93"); return AF_DASHES; }
    }
  }
  if (!letters_only(t, ws, we)) return 0;
  /* TWo INitial CApitals */
  if ((ac->opts & AC_TWOCAPS) && we - ws >= 3 && is_upper((u8)t[ws]) && is_upper((u8)t[ws + 1])) {
    int lowers = 0, uppers = 0;
    char w[64];
    for (i = ws + 2; i < we; i++) { if (is_lower((u8)t[i])) lowers++; else if (is_upper((u8)t[i])) uppers++; }
    if (lowers && !uppers && we - ws < sizeof w) {
      memcpy(w, t + ws, we - ws); w[we - ws] = 0;
      if (lst_find(ac->exc2, ac->nexc2, w, 0) < 0) {
        char l[2];
        l[0] = (char)(t[ws + 1] + 32); l[1] = 0;
        replace(c, pi, ws + 1, ws + 2, l);
        did = AC_TWOCAPS;
      }
    }
  }
  /* cAPS lOCK on by mistake */
  if (!did && (ac->opts & AC_CAPSLOCK) && we - ws >= 3 && is_lower((u8)t[ws])) {
    int ok = 1;
    for (i = ws + 1; i < we; i++) if (!is_upper((u8)t[i]) && t[i] != '\'') ok = 0;
    if (ok) {
      char w[64];
      if (we - ws < sizeof w) {
        for (i = ws; i < we; i++) w[i - ws] = (char)(is_upper((u8)t[i]) ? t[i] + 32 : is_lower((u8)t[i]) ? t[i] - 32 : t[i]);
        w[we - ws] = 0;
        replace(c, pi, ws, we, w);
        return AC_CAPSLOCK;
      }
    }
  }
  t = d->p[pi]->t;
  /* names of days */
  if ((ac->opts & AC_DAYS) && en && is_lower((u8)t[ws])) {
    char w[16];
    int k;
    if (we - ws < sizeof w) {
      memcpy(w, t + ws, we - ws); w[we - ws] = 0;
      for (k = 0; days[k]; k++) if (!strcmp(w, days[k])) { char l[2]; l[0] = (char)(t[ws] - 32); l[1] = 0; replace(c, pi, ws, ws + 1, l); return AC_DAYS; }
    }
  }
  /* the first letter of a sentence */
  if ((ac->opts & AC_SENTENCE) && is_lower((u8)t[ws]) && sentence_start(ac, d, pi, ts)) {
    char l[2];
    for (i = ws + 1; i < we; i++) if (is_upper((u8)t[i])) return did;     /* (iPhone) */
    l[0] = (char)(t[ws] - 32); l[1] = 0;
    replace(c, pi, ws, ws + 1, l);
    return did | AC_SENTENCE;
  }
  return did;
}
/* the paragraph that just ended (Return): its last word, then a list from "* " or "1. ", or a border
   from a line of --- */
static unsigned para_done(AutoCorrect *ac, Ctx *c, u32 pi, u32 next)
{
  Doc *d = c->d;
  Para *p = d->p[pi];
  unsigned did = 0;
  u32 i;
  if (p->len) did |= word_done(ac, c, pi, p->len);
  p = d->p[pi];
  /* a border: a paragraph of only ---, ===, ___, ***, ~~~ or ### (three or more) */
  if ((ac->opts & AF_BORDERS) && p->len >= 3 && strchr("-=_*~#", p->t[0])) {
    for (i = 1; i < p->len && p->t[i] == p->t[0]; i++) ;
    if (i == p->len && !in_table(&p->pf)) {
      ParaFmt pf = p->pf;
      char ch = p->t[0];
      pf.bstyle[PE_BOTTOM] = (u8)(ch == '=' ? PB_DOUBLE : ch == '_' || ch == '#' ? PB_THICK : ch == '*' ? PB_DOTTED : ch == '~' ? PB_DASHED : PB_SINGLE);
      pf.bwidth[PE_BOTTOM] = (u8)(ch == '_' || ch == '#' ? 12 : 6); pf.bspace[PE_BOTTOM] = 1; pf.bcolor[PE_BOTTOM] = COLOR_AUTO;
      del(c, pi, 0, p->len);
      doc_para_format(d, pi, pi, PM_BORDER, &pf);
      c->changes++;
      return did | AF_BORDERS;
    }
  }
  /* a list: "* ", "- ", "> " bullets; "1. ", "1) ", "a. ", "A) ", "i. " numbers (with text after) */
  if (!p->pf.list && !in_table(&p->pf) && p->len >= 3) {
    const char *t = p->t;
    int bullet = 0, fmt = -1;
    u32 mark = 0;
    unsigned long start = 1;
    if ((ac->opts & AF_BULLETS) && (t[0] == '*' || t[0] == '-' || t[0] == '>') && (t[1] == ' ' || t[1] == '\t')) { bullet = t[0]; mark = 1; }
    else if (ac->opts & AF_NUMBERS) {
      u32 k = 0;
      start = 0;
      while (k < p->len && is_digit((u8)t[k]) && k < 4) { start = start * 10 + (unsigned long)(t[k] - '0'); k++; }
      if (k && k < p->len && (t[k] == '.' || t[k] == ')')) { fmt = t[k] == '.' ? 0 : 1; mark = k + 1; }
      else if (is_alpha((u8)t[0]) && (t[1] == '.' || t[1] == ')') && (t[0] == 'a' || t[0] == 'A' || t[0] == 'i' || t[0] == 'I')) {
        fmt = 2 + (t[0] == 'A' ? 1 : t[0] == 'i' ? 2 : t[0] == 'I' ? 3 : 0) * 2 + (t[1] == ')'); mark = 2; start = 1;
      }
      if (mark && !(mark < p->len && (t[mark] == ' ' || t[mark] == '\t'))) mark = 0;
    }
    if (mark) {
      u32 sp = mark;
      while (sp < p->len && (t[sp] == ' ' || t[sp] == '\t')) sp++;
      if (sp < p->len) {
        int list = doc_list_preset(d, bullet ? LP_BULLETS : (fmt == 1 || (fmt >= 2 && (fmt & 1))) ? LP_PARENS : LP_NUMBERS);
        if (list) {
          ListLevel *lv = &d->lists[list - 1].lvl[0];
          if (bullet == '-') strcpy(lv->text, "\xE2\x80\x93");
          else if (bullet == '>') strcpy(lv->text, "\xE2\x9E\xA2");
          if (!bullet) {
            static const u8 nf[4] = { NF_LOWER_LETTER, NF_UPPER_LETTER, NF_LOWER_ROMAN, NF_UPPER_ROMAN };
            if (fmt >= 2) lv->fmt = nf[(fmt - 2) / 2];
            lv->start = (u16)(start ? start : 1);
          }
          del(c, pi, 0, sp);
          doc_list_apply(d, pi, next < d->np && next > pi && !d->p[next]->len ? next : pi, list);
          c->changes++;
          return did | (bullet ? AF_BULLETS : AF_NUMBERS);
        }
      }
    }
  }
  return did;
}
/* the quote just typed at q in paragraph pi made smart: opening after a space, an opening bracket or
   the paragraph's start, else closing (an apostrophe) */
static unsigned smart_quote(Ctx *c, u32 pi, u32 q)
{
  const char *t = c->d->p[pi]->t;
  int dbl = t[q] == '"', open;
  u8 pc = q ? (u8)t[q - 1] : ' ';
  open = q == 0 || is_space(pc) || pc == '(' || pc == '[' || pc == '{' || pc == '-' ||
         (q >= 3 && (u8)t[q - 3] == 0xE2 && (u8)t[q - 2] == 0x80 && ((u8)t[q - 1] == 0x93 || (u8)t[q - 1] == 0x94 || (u8)t[q - 1] == 0x9C || (u8)t[q - 1] == 0x98));
  replace(c, pi, q, q + 1, dbl ? (open ? "\xE2\x80\x9C" : "\xE2\x80\x9D") : (open ? "\xE2\x80\x98" : "\xE2\x80\x99"));
  return AF_QUOTES;
}
unsigned ac_typed(AutoCorrect *ac, Doc *d, Pos *caret, const char *typed, u32 n, int rid)
{
  Ctx c;
  unsigned did = 0;
  u32 pi = caret->para, k;
  const char *t;
  u8 ch;
  if (!ac || !n || pi >= d->np) return 0;
  c.d = d; c.caret = caret; c.rid = rid; c.changes = 0;
  doc_begin(d, *caret);
  if (n == 1 && typed[0] == '\n') {
    if (pi > 0) did = para_done(ac, &c, pi - 1, pi);
    doc_end(d, *caret);
    return did;
  }
  t = d->p[pi]->t;
  if (caret->off < n || caret->off > d->p[pi]->len) { doc_end(d, *caret); return 0; }
  ch = (u8)typed[n - 1];
  k = caret->off - 1;                               /* the last character typed */
  if (ch == '"' || ch == '\'') {
    /* AutoCorrect on what the quote closes first ("teh" then a closing quote), then the quote */
    if ((ac->opts & AF_QUOTES)) did = smart_quote(&c, pi, k);
  }
  else if (is_space(ch) || strchr(".,;:!?)]", ch)) {
    did = word_done(ac, &c, pi, k);
  }
  /* symbols replaced at once, as soon as their last character is typed: (c), ..., --> */
  if (!did && (ac->opts & AC_REPLACE) && caret->para == pi) {
    int i;
    t = d->p[pi]->t;
    for (i = 0; i < ac->n; i++) {
      const char *f = ac->from[i];
      size_t l = strlen(f);
      if (l < 2 || is_alpha((u8)f[0]) || is_digit((u8)f[0]) || is_alpha((u8)f[l - 1]) || is_digit((u8)f[l - 1])) continue;
      if (caret->off >= l && !memcmp(t + caret->off - l, f, l)) {
        replace(&c, pi, caret->off - (u32)l, caret->off, ac->to[i]);
        did = AC_REPLACE;
        break;
      }
    }
  }
  doc_end(d, *caret);
  return c.changes ? (did ? did : AC_REPLACE) : 0;
}

int ac_format_now(AutoCorrect *ac, Doc *d, u32 pa, u32 pb)
{
  Ctx c;
  AutoCorrect fmt = *ac;
  u32 pi;
  Pos at;
  if (pa > pb) { u32 x = pa; pa = pb; pb = x; }
  if (pb >= d->np) pb = d->np ? d->np - 1 : 0;
  if (!d->np) return 0;
  /* only the AutoFormat options (Word's AutoFormat Now leaves the spelling corrections alone) */
  fmt.opts = ac->opts & (AF_QUOTES | AF_DASHES | AF_FRACTIONS | AF_ORDINALS | AF_BULLETS | AF_NUMBERS | AF_BORDERS | AF_LINKS | AF_BOLDITALIC);
  c.d = d; c.caret = NULL; c.rid = -1; c.changes = 0;
  at.para = pa; at.off = 0;
  doc_begin(d, at);
  for (pi = pa; pi <= pb && pi < d->np; pi++) {
    u32 i;
    /* quotes, then each word's end as if typed */
    for (i = 0; i < d->p[pi]->len; i++) {
      char q = d->p[pi]->t[i];
      if ((q == '"' || q == '\'') && (fmt.opts & AF_QUOTES)) smart_quote(&c, pi, i);
    }
    for (i = 0; i <= d->p[pi]->len; i++) {
      const char *t = d->p[pi]->t;
      u32 len = d->p[pi]->len;
      if (i == len || is_space((u8)t[i]) || strchr(".,;:!?)]", t[i])) {
        u32 before = len;
        if (i > 0 && !is_space((u8)t[i - 1])) word_done(&fmt, &c, pi, i);
        if (d->p[pi]->len < before) i -= before - d->p[pi]->len;
        else i += d->p[pi]->len - before;
      }
    }
    {
      u32 before = d->np;
      para_done(&fmt, &c, pi, pi);
      (void)before;
    }
  }
  doc_end(d, at);
  return c.changes;
}
