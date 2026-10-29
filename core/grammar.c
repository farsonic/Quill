/* grammar.c - see grammar.h (WP8) */
#include <stdlib.h>
#include <string.h>
#include "grammar.h"
#include "proflang.h"

#define MAXWORDS 400                  /* words of a paragraph looked at (the rest is not checked) */
typedef struct { u32 s, e; char low[32]; u8 cap, allcap, letters; } Word;

static int is_alpha(u8 c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_up(u8 c) { return c >= 'A' && c <= 'Z'; }
static int is_low(u8 c) { return c >= 'a' && c <= 'z'; }
/* a letter at t[i]: ASCII, or Latin-1 as UTF-8 (C3 80-BF but the signs) */
static int letter_at(const char *t, u32 len, u32 i)
{
  const u8 *u = (const u8 *)t;
  if (is_alpha(u[i])) return 1;
  return u[i] == 0xC3 && i + 1 < len && u[i + 1] >= 0x80 && u[i + 1] <= 0xBF && u[i + 1] != 0x97 && u[i + 1] != 0xB7;
}
/* the words of t: letters with inner apostrophes (straight or U+2019) and hyphens */
static int words_of(const char *t, u32 len, Word *w, int max)
{
  u32 i = 0;
  int n = 0;
  while (i < len && n < max) {
    u32 s, k = 0;
    if (!letter_at(t, len, i)) { i++; continue; }
    s = i;
    while (i < len) {
      if (letter_at(t, len, i)) { i += (u8)t[i] == 0xC3 ? 2 : 1; continue; }
      if ((t[i] == '\'' || t[i] == '-') && i + 1 < len && letter_at(t, len, i + 1)) { i++; continue; }
      if ((u8)t[i] == 0xE2 && i + 3 < len && (u8)t[i + 1] == 0x80 && (u8)t[i + 2] == 0x99 && letter_at(t, len, i + 3)) { i += 3; continue; }
      break;
    }
    /* digits stuck to it ("3rd", "MP3"): not a word for these rules */
    if ((s > 0 && t[s - 1] >= '0' && t[s - 1] <= '9') || (i < len && t[i] >= '0' && t[i] <= '9')) continue;
    w[n].s = s; w[n].e = i;
    w[n].cap = (u8)is_up((u8)t[s]); w[n].allcap = 1; w[n].letters = 0;
    for (k = s; k < i; k++) {
      u8 c = (u8)t[k];
      if (is_low(c) || (c == 0xC3 && k + 1 < i && (u8)t[k + 1] >= 0xA0)) w[n].allcap = 0;
      if (is_alpha(c) || c == 0xC3) w[n].letters++;
    }
    for (k = 0; k < i - s && k < sizeof w[n].low - 1; k++) {
      u8 c = (u8)t[s + k];
      w[n].low[k] = (char)(is_up(c) ? c + 32 : c);
    }
    w[n].low[k] = 0;
    if ((u8)t[s] == 0xE2) w[n].low[0] = 0;
    n++;
  }
  return n;
}
/* only spaces (at least one) between bytes a and b */
static int spaces(const char *t, u32 a, u32 b)
{
  u32 i;
  if (b <= a) return 0;
  for (i = a; i < b; i++) if (t[i] != ' ') return 0;
  return 1;
}
static int in_list(const char *w, const char *const *list)
{
  int i;
  for (i = 0; list[i]; i++) if (!strcmp(w, list[i])) return 1;
  return 0;
}
static int starts(const char *w, const char *p) { return !strncmp(w, p, strlen(p)); }
static int add(GramIssue *out, int *n, int max, u32 a, u32 b, int kind, const char *sug)
{
  int i;
  if (*n >= max) return 0;
  for (i = 0; i < *n; i++) if (out[i].a < b && a < out[i].b) return 0;     /* (one issue a place) */
  out[*n].a = a; out[*n].b = b; out[*n].kind = (u8)kind;
  out[*n].has_sug = sug != NULL;
  strncpy(out[*n].sug, sug ? sug : "", sizeof out[*n].sug - 1); out[*n].sug[sizeof out[*n].sug - 1] = 0;
  (*n)++;
  return 1;
}
/* s with its first letter made capital if like (a word of the text) starts with one */
static void cased(char *out, size_t cap, const char *s, const char *t, const Word *like)
{
  strncpy(out, s, cap - 1); out[cap - 1] = 0;
  if (like && is_up((u8)t[like->s]) && is_low((u8)out[0])) out[0] = (char)(out[0] - 32);
}

/* ---- the word lists */
static const char *const repeat_ok[] = { "had", "that", "bye", "no", "very", "so", "is", "do", "really", "far", "knock", "ha", "la", "yada", "blah", "tut", "chop", NULL };
/* words starting with a vowel letter that sound as a consonant, and the other way round */
static const char *const you_sound[] = { "uni", "unan", "use", "usa", "usu", "ute", "uti", "uto", "ura", "ure", "uri", "uro", "uku", "eu", "ewe", "one", "once", "ouija", "ufo", "ubiq", NULL };
static const char *const silent_h[] = { "hour", "honest", "honour", "honor", "heir", "honorar", NULL };
static int vowel_sound(const Word *w)
{
  int i;
  char c = w->low[0];
  if (w->allcap && w->e - w->s >= 2 && w->e - w->s <= 5) return strchr("aefhilmnorsx", c) != NULL;     /* an FBI, a NATO? (letters read out) */
  for (i = 0; you_sound[i]; i++) if (starts(w->low, you_sound[i])) return 0;
  for (i = 0; silent_h[i]; i++) if (starts(w->low, silent_h[i])) return 1;
  return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}
static const char *const abbrevs[] = { "e", "g", "i", "etc", "vs", "mr", "mrs", "ms", "dr", "prof", "st", "no", "fig", "cf", "approx", "dept", "inc", "ltd",
                                        "jr", "sr", "co", "corp", "vol", "pp", "p", "ch", "al", "viz", "ca", "est", "jan", "feb", "mar", "apr", "jun", "jul",
                                        "aug", "sep", "sept", "oct", "nov", "dec", "mon", "tue", "wed", "thu", "fri", "sat", "sun", "rev", "gen", "col", "lt", NULL };
/* commonly confused words: the words as typed (small) and what is meant */
static const struct { const char *w[4]; const char *with; } confused[] = {
  { { "could", "of" }, "could have" }, { { "should", "of" }, "should have" }, { { "would", "of" }, "would have" },
  { { "must", "of" }, "must have" }, { { "might", "of" }, "might have" }, { { "may", "of" }, "may have" },
  { { "alot" }, "a lot" }, { { "your", "welcome" }, "you're welcome" }, { { "their", "is" }, "there is" },
  { { "their", "are" }, "there are" }, { { "their", "was" }, "there was" }, { { "their", "were" }, "there were" },
  { { "more", "then" }, "more than" }, { { "less", "then" }, "less than" }, { { "better", "then" }, "better than" },
  { { "rather", "then" }, "rather than" }, { { "other", "then" }, "other than" }, { { "worse", "then" }, "worse than" },
  { { "greater", "then" }, "greater than" }, { { "fewer", "then" }, "fewer than" }, { { "larger", "then" }, "larger than" },
  { { "smaller", "then" }, "smaller than" }, { { "higher", "then" }, "higher than" }, { { "lower", "then" }, "lower than" },
  { { "it's", "own" }, "its own" }, { { "to", "much" }, "too much" }, { { "to", "many" }, "too many" },
  { { "irregardless" }, "regardless" }, { { "per", "say" }, "per se" }, { { "suppose", "to" }, "supposed to" },
  { { "for", "all", "intensive", "purposes" }, "for all intents and purposes" }, { { "loose", "weight" }, "lose weight" },
  { { "an", "affect", "on" }, "an effect on" }, { { "the", "affect", "of" }, "the effect of" }, { { "could", "care", "less" }, "couldn't care less" },
  { { "who's", "car" }, "whose car" }, { { "should", "of", "been" }, "should have been" }, { { "baited", "breath" }, "bated breath" },
  { { "sneak", "peak" }, "sneak peek" }, { { "free", "reign" }, "free rein" }, { { "peaked", "my", "interest" }, "piqued my interest" },
  { { "wreck", "havoc" }, "wreak havoc" }, { { "one", "in", "the", "same" }, "one and the same" }, { { "nip", "it", "in", "the" }, "nip it in the" },
};
static const struct { const char *w[6]; const char *with; } wordy[] = {
  { { "in", "order", "to" }, "to" }, { { "due", "to", "the", "fact", "that" }, "because" }, { { "at", "this", "point", "in", "time" }, "now" },
  { { "in", "the", "event", "that" }, "if" }, { { "for", "the", "purpose", "of" }, "for" }, { { "a", "large", "number", "of" }, "many" },
  { { "has", "the", "ability", "to" }, "can" }, { { "in", "spite", "of", "the", "fact", "that" }, "although" }, { { "with", "regard", "to" }, "about" },
  { { "in", "the", "near", "future" }, "soon" }, { { "at", "the", "present", "time" }, "now" }, { { "the", "majority", "of" }, "most" },
  { { "each", "and", "every" }, "every" }, { { "first", "and", "foremost" }, "first" }, { { "very", "unique" }, "unique" },
  { { "absolutely", "essential" }, "essential" }, { { "end", "result" }, "result" }, { { "past", "history" }, "history" },
  { { "close", "proximity" }, "proximity" }, { { "free", "gift" }, "gift" }, { { "advance", "planning" }, "planning" },
  { { "completely", "finished" }, "finished" }, { { "in", "regards", "to" }, "regarding" }, { { "a", "number", "of" }, "several" },
  { { "is", "able", "to" }, "can" }, { { "are", "able", "to" }, "can" }, { { "in", "close", "proximity", "to" }, "near" },
  { { "on", "a", "daily", "basis" }, "daily" }, { { "until", "such", "time", "as" }, "until" }, { { "prior", "to" }, "before" },
};
static const char *const be_forms[] = { "am", "is", "are", "was", "were", "be", "been", "being", NULL };
static const char *const irregular[] = { "written", "done", "made", "taken", "given", "seen", "known", "shown", "built", "found", "held", "told",
  "sent", "kept", "left", "paid", "brought", "bought", "thought", "caught", "taught", "chosen", "driven", "eaten", "broken", "spoken",
  "stolen", "forgotten", "gotten", "beaten", "hidden", "ridden", "begun", "sung", "drunk", "won", "lost", "sold", "torn", "worn", "drawn",
  "thrown", "grown", "blown", "flown", "frozen", "woken", "shaken", "forgiven", "hit", "cut", "put", "set", "shut", "spent", "lent", "meant",
  "read", "led", "fed", "bred", "bound", "ground", "wound", "struck", "hung", "spun", "dug", "felt", "dealt", "heard", "laid", "said", NULL };
static const char *const not_participle[] = { "need", "bed", "red", "shed", "seed", "feed", "speed", "indeed", "embed", "breed", "deed",
  "greed", "weed", "bleed", "proceed", "succeed", "exceed", "hundred", "sacred", "naked", "wicked", "ragged", "rugged", "aged", "beloved",
  "learned", "crooked", "jagged", "wretched", "dogged", "sled", "fled", "wed", "led", "fed", NULL };
static int participle(const Word *w)
{
  size_t n = strlen(w->low);
  if (in_list(w->low, irregular)) return 1;
  if (in_list(w->low, not_participle)) return 0;
  return n >= 4 && w->low[n - 2] == 'e' && w->low[n - 1] == 'd';
}
/* the words from i on are the phrase pw (small words, NULL-ended, up to max), with only spaces between */
static int phrase_at(const char *t, const Word *w, int nw, int i, const char *const *pw, int max)
{
  int k;
  for (k = 0; k < max && pw[k]; k++) {
    if (i + k >= nw || strcmp(w[i + k].low, pw[k])) return 0;
    if (k && !spaces(t, w[i + k - 1].e, w[i + k].s)) return 0;
  }
  return k;
}
/* t[i] ends a sentence: . ! ? (not an ellipsis), after a word that isn't an abbreviation */
static int sentence_end(const char *t, u32 len, u32 i, const Word *w, int nw)
{
  int k;
  if (t[i] == '!' || t[i] == '?') return 1;
  if (t[i] != '.') return 0;
  if ((i > 0 && t[i - 1] == '.') || (i + 1 < len && t[i + 1] == '.')) return 0;
  for (k = nw - 1; k >= 0; k--) if (w[k].e == i) {
    if (in_list(w[k].low, abbrevs) || (w[k].e - w[k].s == 1)) return 0;
    if (w[k].s > 0 && t[w[k].s - 1] == '.') return 0;          /* "U.S." */
    break;
  }
  return 1;
}

int grammar_check(const char *t, u32 len, unsigned opts, GramIssue *out, int max)
{
  Word *w;
  int nw, n = 0, i, k;
  char s[64];
  if (!len || max <= 0 || !(w = (Word *)malloc(MAXWORDS * sizeof(Word)))) return 0;
  nw = words_of(t, len, w, MAXWORDS);
  if (opts & GRO_GRAMMAR) {
    for (i = 0; i < nw; i++) {
      /* commonly confused words */
      for (k = 0; k < (int)(sizeof confused / sizeof confused[0]); k++) {
        int m = phrase_at(t, w, nw, i, confused[k].w, 4);
        if (m) { cased(s, sizeof s, confused[k].with, t, &w[i]); add(out, &n, max, w[i].s, w[i + m - 1].e, GR_CONFUSED, s); break; }
      }
      /* a word twice */
      if (i > 0 && w[i].low[0] && !strcmp(w[i].low, w[i - 1].low) && spaces(t, w[i - 1].e, w[i].s) && !in_list(w[i].low, repeat_ok))
        add(out, &n, max, w[i - 1].e, w[i].e, GR_REPEAT, "");
      /* a or an */
      if (i + 1 < nw && (!strcmp(w[i].low, "a") || !strcmp(w[i].low, "an")) && spaces(t, w[i].e, w[i + 1].s) && w[i + 1].letters > 0) {
        int an = w[i].low[1] == 'n', v = vowel_sound(&w[i + 1]);
        /* (not "a" as a letter or a list label: "a." "a)") */
        if (!(w[i].e < len && (t[w[i].e] == '.' || t[w[i].e] == ')')) && an != v && !(i > 0 && !strcmp(w[i - 1].low, "letter"))) {
          strcpy(s, v ? "an" : "a");
          if (is_up((u8)t[w[i].s])) s[0] = (char)(s[0] - 32);
          add(out, &n, max, w[i].s, w[i].e, GR_ARTICLE, s);
        }
      }
    }
    /* a sentence's first word with a small letter */
    for (i = 0; i < nw; i++) {
      u32 b = w[i].s, j;
      int start;
      if (!is_low((u8)t[b])) continue;
      /* (iPhone, eBay: a capital inside) */
      for (j = b + 1; j < w[i].e; j++) if (is_up((u8)t[j])) break;
      if (j < w[i].e) continue;
      j = b;
      while (j > 0 && (t[j - 1] == '"' || t[j - 1] == '(' || t[j - 1] == '\'' || (u8)t[j - 1] == 0x9C || (u8)t[j - 1] == 0x98)) {
        if ((u8)t[j - 1] >= 0x80) { if (j >= 3 && (u8)t[j - 3] == 0xE2) j -= 3; else break; } else j--;
      }
      if (j == 0) start = i == 0;
      else {
        u32 e = j;
        while (e > 0 && t[e - 1] == ' ') e--;
        start = e < j && e > 0 && sentence_end(t, len, e - 1, w, i);
      }
      if (start) {
        char word[40];
        u32 l = w[i].e - b < sizeof word - 1 ? w[i].e - b : sizeof word - 1;
        memcpy(word, t + b, l); word[l] = 0;
        word[0] = (char)(word[0] - 32);
        add(out, &n, max, b, w[i].e, GR_CAPITAL, word);
      }
    }
    /* spaces round punctuation */
    for (i = 0; i < (int)len; i++) {
      u8 c = (u8)t[i];
      if (strchr(",.;:!?", c) && c && i > 0 && t[i - 1] == ' ') {
        u32 a = (u32)i;
        while (a > 0 && t[a - 1] == ' ') a--;
        if (a > 0 && t[a - 1] != '\t' && !(c == '.' && i + 1 < (int)len && (t[i + 1] == '.' || (t[i + 1] >= '0' && t[i + 1] <= '9')))) {
          s[0] = (char)c; s[1] = 0;
          add(out, &n, max, a, (u32)i + 1, GR_SPACEBEFORE, s);
        }
      }
      if ((c == ',' || c == ';') && i > 0 && i + 1 < (int)len && letter_at(t, len, (u32)i - 1) && is_alpha((u8)t[i + 1])) {
        s[0] = (char)c; s[1] = ' '; s[2] = 0;
        add(out, &n, max, (u32)i, (u32)i + 1, GR_SPACEAFTER, s);
      }
      if ((c == '.' || c == '!' || c == '?') && i > 1 && i + 2 < (int)len && is_low((u8)t[i - 1]) && is_low((u8)t[i - 2]) && is_up((u8)t[i + 1]) && is_low((u8)t[i + 2])) {
        /* "end.The" (not an address: no @, www, :// in the word) */
        int j0 = i, url = 0;
        while (j0 > 0 && t[j0 - 1] != ' ') j0--;
        for (k = j0; k < (int)len && t[k] != ' '; k++) if (t[k] == '@' || t[k] == '/' || !strncmp(t + k, "www", 3)) url = 1;
        if (!url) { s[0] = (char)c; s[1] = ' '; s[2] = 0; add(out, &n, max, (u32)i, (u32)i + 1, GR_SPACEAFTER, s); }
      }
      /* two spaces or more between words */
      if (c == ' ' && i > 0 && t[i - 1] != ' ' && t[i - 1] != '\t' && i + 1 < (int)len && t[i + 1] == ' ') {
        u32 e = (u32)i;
        while (e < len && t[e] == ' ') e++;
        if (e < len && t[e] != '\t') add(out, &n, max, (u32)i, e, GR_DOUBLESPACE, " ");
      }
    }
  }
  if (opts & GRO_STYLE) {
    for (i = 0; i < nw; i++) {
      for (k = 0; k < (int)(sizeof wordy / sizeof wordy[0]); k++) {
        int m = phrase_at(t, w, nw, i, wordy[k].w, 6);
        if (m) { cased(s, sizeof s, wordy[k].with, t, &w[i]); add(out, &n, max, w[i].s, w[i + m - 1].e, GR_WORDY, s); break; }
      }
      if (in_list(w[i].low, be_forms) && i + 1 < nw && spaces(t, w[i].e, w[i + 1].s)) {
        int j = i + 1;
        size_t l = strlen(w[j].low);
        if (l > 3 && !strcmp(w[j].low + l - 2, "ly") && j + 1 < nw && spaces(t, w[j].e, w[j + 1].s)) j++;
        if (participle(&w[j])) add(out, &n, max, w[i].s, w[j].e, GR_PASSIVE, NULL);
      }
    }
  }
  free(w);
  /* in order */
  for (i = 1; i < n; i++) {
    GramIssue x = out[i];
    for (k = i; k > 0 && out[k - 1].a > x.a; k--) out[k] = out[k - 1];
    out[k] = x;
  }
  return n;
}

const char *grammar_message(int kind)
{
  switch (kind) {
  case GR_REPEAT: return "Repeated word";
  case GR_ARTICLE: return "Use \"a\" before a consonant sound and \"an\" before a vowel sound";
  case GR_CAPITAL: return "A sentence starts with a capital letter";
  case GR_SPACEBEFORE: return "No space before punctuation";
  case GR_SPACEAFTER: return "A space after punctuation";
  case GR_DOUBLESPACE: return "One space between words";
  case GR_CONFUSED: return "Commonly confused words";
  case GR_PASSIVE: return "Passive voice (consider the active)";
  case GR_WORDY: return "Wordy phrase (consider a shorter one)";
  }
  return "";
}

int grammar_applies(Para *q, u32 off)
{
  const CharFmt *f = para_fmt_at(q, off);
  return !f->noproof && plang_primary(plang_of(f), "en");
}

/* ---- issues ignored this session: their kind and text */
static char **ign; static int nign, igncap;
static int ign_key(const GramIssue *gi, const char *t, char *out, size_t cap)
{
  u32 n = gi->b - gi->a;
  if (n + 3 >= cap) return 0;
  out[0] = (char)('A' + gi->kind); memcpy(out + 1, t + gi->a, n); out[n + 1] = 0;
  return 1;
}
void grammar_ignore(const GramIssue *gi, const char *t)
{
  char k[96], *c;
  if (!ign_key(gi, t, k, sizeof k) || grammar_ignored(gi, t)) return;
  if (nign >= igncap) {
    int nc = igncap ? igncap * 2 : 16;
    char **ni = (char **)realloc(ign, (size_t)nc * sizeof(char *));
    if (!ni) return;
    ign = ni; igncap = nc;
  }
  if ((c = str_dup(k))) ign[nign++] = c;
}
int grammar_ignored(const GramIssue *gi, const char *t)
{
  char k[96];
  int i;
  if (!ign_key(gi, t, k, sizeof k)) return 0;
  for (i = 0; i < nign; i++) if (!strcmp(ign[i], k)) return 1;
  return 0;
}
void grammar_forget(void)
{
  int i;
  for (i = 0; i < nign; i++) free(ign[i]);
  free(ign); ign = NULL; nign = igncap = 0;
}

int grammar_next(Doc *d, Pos p, unsigned opts, Pos *a, Pos *b, GramIssue *gi)
{
  GramIssue is[64];
  u32 pi = p.para, n, off = p.off;
  if (!d->np) return 0;
  if (pi >= d->np) { pi = 0; off = 0; }
  for (n = 0; n <= d->np; n++) {
    Para *q = d->p[pi];
    int k, m = grammar_check(q->t, q->len, opts, is, 64);
    for (k = 0; k < m; k++) {
      if (is[k].a < off || (n == d->np && is[k].a >= p.off)) continue;
      if (!grammar_applies(q, is[k].a) || grammar_ignored(&is[k], q->t)) continue;
      a->para = b->para = pi; a->off = is[k].a; b->off = is[k].b;
      *gi = is[k];
      return 1;
    }
    pi = (pi + 1) % d->np; off = 0;
  }
  return 0;
}
