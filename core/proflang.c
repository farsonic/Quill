/* proflang.c - see proflang.h (WP31) */
#include <stdlib.h>
#include <string.h>
#include "proflang.h"

/* Word's usual proofing languages, with Windows' numbers for them (the order of the Language window) */
static const struct { const char *tag, *name; u16 lcid; } langs[] = {
  { "en-GB", "English (United Kingdom)", 0x0809 }, { "en-US", "English (United States)", 0x0409 },
  { "en-AU", "English (Australia)", 0x0C09 }, { "en-CA", "English (Canada)", 0x1009 },
  { "en-IE", "English (Ireland)", 0x1809 }, { "en-NZ", "English (New Zealand)", 0x1409 },
  { "en-ZA", "English (South Africa)", 0x1C09 },
  { "de-DE", "German (Germany)", 0x0407 }, { "de-AT", "German (Austria)", 0x0C07 }, { "de-CH", "German (Switzerland)", 0x0807 },
  { "fr-FR", "French (France)", 0x040C }, { "fr-BE", "French (Belgium)", 0x080C }, { "fr-CA", "French (Canada)", 0x0C0C },
  { "fr-CH", "French (Switzerland)", 0x100C },
  { "es-ES", "Spanish (Spain)", 0x0C0A }, { "es-MX", "Spanish (Mexico)", 0x080A },
  { "it-IT", "Italian (Italy)", 0x0410 }, { "it-CH", "Italian (Switzerland)", 0x0810 },
  { "pt-PT", "Portuguese (Portugal)", 0x0816 }, { "pt-BR", "Portuguese (Brazil)", 0x0416 },
  { "nl-NL", "Dutch (Netherlands)", 0x0413 }, { "nl-BE", "Dutch (Belgium)", 0x0813 },
  { "da-DK", "Danish", 0x0406 }, { "sv-SE", "Swedish", 0x041D }, { "nb-NO", "Norwegian (Bokmal)", 0x0414 },
  { "nn-NO", "Norwegian (Nynorsk)", 0x0814 }, { "fi-FI", "Finnish", 0x040B }, { "is-IS", "Icelandic", 0x040F },
  { "pl-PL", "Polish", 0x0415 }, { "cs-CZ", "Czech", 0x0405 }, { "sk-SK", "Slovak", 0x041B }, { "hu-HU", "Hungarian", 0x040E },
  { "ro-RO", "Romanian", 0x0418 }, { "hr-HR", "Croatian", 0x041A }, { "sl-SI", "Slovenian", 0x0424 },
  { "ca-ES", "Catalan", 0x0403 }, { "eu-ES", "Basque", 0x042D }, { "gl-ES", "Galician", 0x0456 },
  { "ga-IE", "Irish", 0x083C }, { "cy-GB", "Welsh", 0x0452 },
  { "et-EE", "Estonian", 0x0425 }, { "lv-LV", "Latvian", 0x0426 }, { "lt-LT", "Lithuanian", 0x0427 },
  { "el-GR", "Greek", 0x0408 }, { "tr-TR", "Turkish", 0x041F }, { "ru-RU", "Russian", 0x0419 }, { "uk-UA", "Ukrainian", 0x0422 },
  { "id-ID", "Indonesian", 0x0421 }, { "la", "Latin", 0x0476 },
};
#define NLANGS ((int)(sizeof langs / sizeof langs[0]))
/* tags read from files that aren't in the list (kept for the session) */
static char **extra; static int nextra, extracap;

u16 plang_default = 1;                    /* en-GB */
int proof_live_spelling = 1, proof_live_grammar = 1;

/* tags alike: ASCII case and '-' or '_' ignored */
static int tag_eq(const char *a, const char *b)
{
  for (; *a && *b; a++, b++) {
    int x = *a == '_' ? '-' : *a, y = *b == '_' ? '-' : *b;
    if (x >= 'A' && x <= 'Z') x += 32;
    if (y >= 'A' && y <= 'Z') y += 32;
    if (x != y) return 0;
  }
  return !*a && !*b;
}
u16 plang_known(const char *tag)
{
  int i;
  if (!tag || !*tag) return 0;
  for (i = 0; i < NLANGS; i++) if (tag_eq(tag, langs[i].tag)) return (u16)(i + 1);
  for (i = 0; i < nextra; i++) if (tag_eq(tag, extra[i])) return (u16)(NLANGS + i + 1);
  return 0;
}
u16 plang_find(const char *tag)
{
  u16 k = plang_known(tag);
  char *c;
  if (k || !tag || !*tag || strlen(tag) > 40) return k;
  if (nextra >= extracap) {
    int nc = extracap ? extracap * 2 : 8;
    char **ne = (char **)realloc(extra, (size_t)nc * sizeof(char *));
    if (!ne) return 0;
    extra = ne; extracap = nc;
  }
  if (!(c = str_dup(tag))) return 0;
  extra[nextra++] = c;
  return (u16)(NLANGS + nextra);
}
const char *plang_tag(u16 id)
{
  if (id >= 1 && id <= NLANGS) return langs[id - 1].tag;
  if (id > NLANGS && id <= NLANGS + nextra) return extra[id - NLANGS - 1];
  return "";
}
const char *plang_name(u16 id)
{
  if (id >= 1 && id <= NLANGS) return langs[id - 1].name;
  return plang_tag(id);
}
int plang_nlisted(void) { return NLANGS; }
u16 plang_lcid(u16 id) { return id >= 1 && id <= NLANGS ? langs[id - 1].lcid : 0; }
int plang_primary(u16 id, const char *two)
{
  const char *t = plang_tag(id);
  size_t n = strlen(two);
  return t[0] && !strncmp(t, two, n) && (t[n] == 0 || t[n] == '-' || t[n] == '_');
}
u16 plang_of(const CharFmt *f) { return f && f->lang ? f->lang : plang_default; }

/* ---- the dictionaries, by language: 0 not asked yet, 1 asked and none, 2 the loader's, 3 given */
typedef struct { u16 id; u8 state; Spell *s; } Dict;
static Dict *dicts; static int ndicts, dictcap;
static PlangLoader loader;
void plang_set_loader(PlangLoader fn) { loader = fn; }
static Dict *dict_for(u16 id)
{
  int i;
  for (i = 0; i < ndicts; i++) if (dicts[i].id == id) return &dicts[i];
  if (ndicts >= dictcap) {
    int nc = dictcap ? dictcap * 2 : 8;
    Dict *nd = (Dict *)realloc(dicts, (size_t)nc * sizeof(Dict));
    if (!nd) return NULL;
    dicts = nd; dictcap = nc;
  }
  memset(&dicts[ndicts], 0, sizeof(Dict));
  dicts[ndicts].id = id;
  return &dicts[ndicts++];
}
void plang_set_spell(u16 id, Spell *s)
{
  Dict *d = dict_for(id);
  if (!d) return;
  if (d->state == 2 && d->s) { spell_free(d->s); free(d->s); }
  d->s = s; d->state = s ? 3 : 1;
}
Spell *plang_spell(u16 id)
{
  Dict *d;
  if (!id || !(d = dict_for(id))) return NULL;
  if (d->state == 0) {
    Spell *s = NULL;
    d->state = 1;
    if (loader && (s = (Spell *)calloc(1, sizeof(Spell)))) {
      if (loader(id, s) && s->n) { d->s = s; d->state = 2; }
      else { spell_free(s); free(s); }
    }
  }
  return d->state >= 2 ? d->s : NULL;
}
void plang_forget(void)
{
  int i;
  for (i = 0; i < ndicts; i++) if (dicts[i].state == 2 && dicts[i].s) { spell_free(dicts[i].s); free(dicts[i].s); }
  for (i = 0; i < ndicts; ) {
    if (dicts[i].state == 3) { i++; continue; }
    dicts[i] = dicts[--ndicts];
  }
}
Spell *proof_spell(Spell *deflt, const CharFmt *f)
{
  u16 id;
  Spell *s;
  if (f && f->noproof) return NULL;
  id = plang_of(f);
  if ((s = plang_spell(id))) return s;
  if (!id || plang_primary(id, "en")) return deflt;
  return NULL;
}
Spell *proof_spell_at(Spell *deflt, Para *p, u32 off)
{
  return proof_spell(deflt, p ? para_fmt_at(p, off) : NULL);
}

int doc_set_lang(Doc *d, Pos a, Pos b, u16 lang, int noproof)
{
  CharFmt v;
  memset(&v, 0, sizeof v);
  v.lang = lang; v.noproof = (u8)(noproof != 0);
  if (lang && pos_cmp(a, b)) {
    /* w:lang's other two slots kept from the text's start */
    Pos s = pos_cmp(a, b) < 0 ? a : b;
    if (s.para < d->np) { CharFmt *f = para_fmt_at(d->p[s.para], s.off); v.langea = f->langea; v.langbidi = f->langbidi; }
  }
  return doc_format(d, a, b, CM_LANG | CM_NOPROOF, &v);
}
