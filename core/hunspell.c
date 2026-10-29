/* hunspell.c - see hunspell.h (WP31). Written from hunspell(5), the file format's documentation */
#include <stdlib.h>
#include <string.h>
#include "hunspell.h"

#define MAXW 200                       /* bytes of a word checked (longer ones count as right) */
enum { FL_CHAR, FL_LONG, FL_NUM, FL_UTF8 };

typedef struct {
  u16 flag; u8 cross, pfx;
  u32 strip, add;                      /* offsets into the string pool (NUL-terminated) */
  u16 striplen, addlen;
  u32 cont;                            /* continuation flags: offset into the flag pool (0: none) */
  u32 cond;                            /* condition: offset into the condition pool (0: none) */
} Aff;
typedef struct { u32 word, flags; } Ent;    /* a stem: its offset in the dictionary text, its flags (0: none) */
typedef struct { u32 from, to; } Rep;

struct Hun {
  char *dic;                           /* the .dic text (UTF-8), its words NUL-terminated in place */
  Ent *tab; u32 tabmask, nwords;
  u16 *fp; u32 nfp, fpcap;             /* flag pool: at an offset, a count then the flags; fp[0] = 0 */
  u32 *cp; u32 ncp, cpcap;             /* condition pool: count of elements, then each: kind, n, code points */
  char *sp; u32 nsp, spcap;            /* string pool */
  Aff *aff; u32 naff, affcap;
  u32 *bucket[2][257]; u32 nbucket[2][257];   /* [suffix 0, prefix 1][last or first byte, 256 empty add]: affix indices */
  u32 *alias; u32 nalias;              /* AF: flag pool offsets */
  Rep *rep; u32 nrep;
  u32 try_[48]; int ntry;
  u32 ignore[16]; int nignore;
  int flagmode;
  u16 needaffix, forbidden, keepcase, onlyincompound, cflag, cbegin, cmiddle, cend;
  int cmin;
};

/* ---- small helpers */
static u32 lc(u32 c)
{
  if (c >= 'A' && c <= 'Z') return c + 32;
  if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
  if (c >= 0x100 && c <= 0x17F) {
    if (c == 0x130) return 'i';
    if (c == 0x178) return 0xFF;
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c + 1 : c;
    if (c == 0x131 || c == 0x138 || c == 0x149 || c == 0x17F) return c;
    return (c & 1) ? c : c + 1;
  }
  if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return c + 32;
  if (c >= 0x410 && c <= 0x42F) return c + 32;
  if (c >= 0x400 && c <= 0x40F) return c + 80;
  return c;
}
static u32 uc(u32 c)
{
  if (c >= 'a' && c <= 'z') return c - 32;
  if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
  if (c == 0xFF) return 0x178;
  if (c >= 0x100 && c <= 0x17F) {
    if (c == 0x131) return 'I';
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c : c - 1;
    if (c == 0x130 || c == 0x138 || c == 0x149 || c == 0x17F) return c;
    return (c & 1) ? c - 1 : c;
  }
  if (c >= 0x3B1 && c <= 0x3CB && c != 0x3C2) return c - 32;
  if (c >= 0x430 && c <= 0x44F) return c - 32;
  if (c >= 0x450 && c <= 0x45F) return c - 80;
  return c;
}
static int is_upper(u32 c) { return lc(c) != c; }
static int is_lower(u32 c) { return uc(c) != c; }
/* code points of s (n bytes) into out (at most max); how many */
static int decode(const char *s, u32 n, u32 *out, int max)
{
  u32 i = 0;
  int k = 0;
  while (i < n && k < max) { int used; out[k++] = utf8_get(s + i, n - i, &used); i += (u32)used; }
  return k;
}
static u32 encode(const u32 *c, int n, char *out, u32 cap)
{
  u32 o = 0;
  int i;
  for (i = 0; i < n; i++) { char t[4]; int k = utf8_put(t, c[i]); if (o + (u32)k >= cap) break; memcpy(out + o, t, (size_t)k); o += (u32)k; }
  out[o] = 0;
  return o;
}
static u32 hash(const char *s, u32 n)
{
  u32 h = 2166136261u, i;
  for (i = 0; i < n; i++) h = (h ^ (u8)s[i]) * 16777619u;
  return h;
}

/* ---- the character sets a dictionary may be in, made UTF-8 */
static const u16 iso2[96] = {
  0x00A0,0x0104,0x02D8,0x0141,0x00A4,0x013D,0x015A,0x00A7,0x00A8,0x0160,0x015E,0x0164,0x0179,0x00AD,0x017D,0x017B,
  0x00B0,0x0105,0x02DB,0x0142,0x00B4,0x013E,0x015B,0x02C7,0x00B8,0x0161,0x015F,0x0165,0x017A,0x02DD,0x017E,0x017C,
  0x0154,0x00C1,0x00C2,0x0102,0x00C4,0x0139,0x0106,0x00C7,0x010C,0x00C9,0x0118,0x00CB,0x011A,0x00CD,0x00CE,0x010E,
  0x0110,0x0143,0x0147,0x00D3,0x00D4,0x0150,0x00D6,0x00D7,0x0158,0x016E,0x00DA,0x0170,0x00DC,0x00DD,0x0162,0x00DF,
  0x0155,0x00E1,0x00E2,0x0103,0x00E4,0x013A,0x0107,0x00E7,0x010D,0x00E9,0x0119,0x00EB,0x011B,0x00ED,0x00EE,0x010F,
  0x0111,0x0144,0x0148,0x00F3,0x00F4,0x0151,0x00F6,0x00F7,0x0159,0x016F,0x00FA,0x0171,0x00FC,0x00FD,0x0163,0x02D9 };
enum { CS_UTF8, CS_LATIN1, CS_LATIN9, CS_LATIN2, CS_CP1250 };
static u32 cs_char(int cs, u8 b)
{
  if (b < 0x80) return b;
  if (cs == CS_LATIN9) {
    switch (b) { case 0xA4: return 0x20AC; case 0xA6: return 0x160; case 0xA8: return 0x161; case 0xB4: return 0x17D;
                 case 0xB8: return 0x17E; case 0xBC: return 0x152; case 0xBD: return 0x153; case 0xBE: return 0x178; }
    return b;
  }
  if (cs == CS_CP1250) {
    switch (b) { case 0x8A: return 0x160; case 0x8C: return 0x15A; case 0x8D: return 0x164; case 0x8E: return 0x17D; case 0x8F: return 0x179;
                 case 0x9A: return 0x161; case 0x9C: return 0x15B; case 0x9D: return 0x165; case 0x9E: return 0x17E; case 0x9F: return 0x17A;
                 case 0xA1: return 0x2C7; case 0xA5: return 0x104; case 0xA6: return 0xA6; case 0xA9: return 0xA9; case 0xAB: return 0xAB;
                 case 0xAC: return 0xAC; case 0xAE: return 0xAE; case 0xB1: return 0xB1; case 0xB5: return 0xB5; case 0xB6: return 0xB6;
                 case 0xB7: return 0xB7; case 0xB9: return 0x105; case 0xBB: return 0xBB; case 0xBC: return 0x13D; case 0xBE: return 0x13E; }
    if (b < 0xA0) return b;
    return iso2[b - 0xA0];
  }
  if (cs == CS_LATIN2) return b >= 0xA0 ? iso2[b - 0xA0] : b;
  return b;
}
/* the .aff's SET line (anywhere near its start) */
static int charset_of(const char *aff, size_t n)
{
  size_t i = 0;
  while (i < n) {
    size_t e = i;
    while (e < n && aff[e] != '\n') e++;
    if (e - i > 4 && !strncmp(aff + i, "SET ", 4)) {
      const char *v = aff + i + 4;
      while (*v == ' ' || *v == '\t') v++;
      if (!strncmp(v, "UTF-8", 5) || !strncmp(v, "utf-8", 5)) return CS_UTF8;
      if (!strncmp(v, "ISO8859-15", 10) || !strncmp(v, "ISO-8859-15", 11)) return CS_LATIN9;
      if (!strncmp(v, "ISO8859-2", 9) || !strncmp(v, "ISO-8859-2", 10)) return CS_LATIN2;
      if (!strncmp(v, "microsoft-cp1250", 16) || !strncmp(v, "cp1250", 6)) return CS_CP1250;
      return CS_LATIN1;
    }
    i = e + 1;
  }
  return CS_LATIN1;
}
/* text in charset cs made UTF-8 (malloc'd, NUL-terminated); NULL if out of memory */
static char *to_utf8(int cs, const char *s, size_t n, size_t *outn)
{
  char *o;
  size_t i, k = 0;
  if (cs == CS_UTF8) {
    if (!(o = (char *)malloc(n + 1))) return NULL;
    memcpy(o, s, n); o[n] = 0; *outn = n;
    return o;
  }
  if (!(o = (char *)malloc(n * 3 + 1))) return NULL;
  for (i = 0; i < n; i++) k += (size_t)utf8_put(o + k, cs_char(cs, (u8)s[i]));
  o[k] = 0; *outn = k;
  return o;
}

/* ---- pools */
static u32 sp_add(Hun *h, const char *s, u32 n)
{
  u32 at;
  if (h->nsp + n + 1 > h->spcap) {
    u32 nc = h->spcap ? h->spcap * 2 : 4096;
    char *ns;
    while (nc < h->nsp + n + 1) nc *= 2;
    if (!(ns = (char *)realloc(h->sp, nc))) return 0;
    h->sp = ns; h->spcap = nc;
  }
  at = h->nsp;
  memcpy(h->sp + at, s, n); h->sp[at + n] = 0;
  h->nsp += n + 1;
  return at;
}
static u32 fp_add(Hun *h, const u16 *f, u32 n)
{
  u32 at;
  if (!n) return 0;
  if (h->nfp + n + 1 > h->fpcap) {
    u32 nc = h->fpcap ? h->fpcap * 2 : 4096;
    u16 *nf;
    while (nc < h->nfp + n + 1) nc *= 2;
    if (!(nf = (u16 *)realloc(h->fp, nc * sizeof(u16)))) return 0;
    h->fp = nf; h->fpcap = nc;
  }
  at = h->nfp;
  h->fp[at] = (u16)n;
  memcpy(h->fp + at + 1, f, n * sizeof(u16));
  h->nfp += n + 1;
  return at;
}
static int cp_add(Hun *h, u32 v)
{
  if (h->ncp >= h->cpcap) {
    u32 nc = h->cpcap ? h->cpcap * 2 : 1024;
    u32 *n2 = (u32 *)realloc(h->cp, nc * sizeof(u32));
    if (!n2) return 0;
    h->cp = n2; h->cpcap = nc;
  }
  h->cp[h->ncp++] = v;
  return 1;
}
/* flags written in s (n bytes) as the FLAG mode says; how many into out */
static u32 parse_flags(const Hun *h, const char *s, u32 n, u16 *out, u32 max)
{
  u32 k = 0, i = 0;
  if (h->flagmode == FL_NUM) {
    while (i < n && k < max) {
      u32 v = 0;
      if (s[i] < '0' || s[i] > '9') { i++; continue; }
      while (i < n && s[i] >= '0' && s[i] <= '9') v = v * 10 + (u32)(s[i++] - '0');
      out[k++] = (u16)v;
    }
  } else if (h->flagmode == FL_LONG) {
    for (; i + 1 < n && k < max; i += 2) out[k++] = (u16)(((u8)s[i] << 8) | (u8)s[i + 1]);
  } else {
    while (i < n && k < max) { int used; out[k++] = (u16)utf8_get(s + i, n - i, &used); i += (u32)used; }
  }
  return k;
}
static int has(const Hun *h, u32 fo, u16 f)
{
  u32 i, n;
  if (!fo || !f) return 0;
  n = h->fp[fo];
  for (i = 1; i <= n; i++) if (h->fp[fo + i] == f) return 1;
  return 0;
}
/* the flags at the end of a .dic word or an affix (after its '/'): their pool offset; AF numbers resolved */
static u32 flags_of(Hun *h, const char *s, u32 n)
{
  u16 f[256];
  u32 k;
  if (!n) return 0;
  if (h->nalias) {
    u32 a = (u32)atoi(s);
    return a >= 1 && a <= h->nalias ? h->alias[a - 1] : 0;
  }
  k = parse_flags(h, s, n, f, 256);
  return fp_add(h, f, k);
}
static u16 one_flag(const Hun *h, const char *s)
{
  u16 f[2];
  u32 n = 0;
  while (s[n] && s[n] != ' ' && s[n] != '\t' && s[n] != '\r' && s[n] != '\n') n++;
  return parse_flags(h, s, n, f, 1) ? f[0] : 0;
}
/* a condition ("[^aeiou]y", "." for none) into the pool: its offset, 0 for none */
static u32 parse_cond(Hun *h, const char *s, u32 n)
{
  u32 at, cnt = 0, i = 0, cntat;
  if (!n || (n == 1 && s[0] == '.')) return 0;
  if (!h->ncp && !cp_add(h, 0)) return 0;          /* (offset 0 means none) */
  at = h->ncp;
  if (!cp_add(h, 0)) return 0;
  cntat = at;
  while (i < n) {
    if (s[i] == '[') {
      u32 kindat, nat, k = 0;
      int used;
      i++;
      kindat = h->ncp; cp_add(h, 1);
      if (i < n && s[i] == '^') { h->cp[kindat] = 2; i++; }
      nat = h->ncp; cp_add(h, 0);
      while (i < n && s[i] != ']') { cp_add(h, utf8_get(s + i, n - i, &used)); i += (u32)used; k++; }
      if (i < n) i++;
      h->cp[nat] = k;
    } else if (s[i] == '.') { cp_add(h, 0); cp_add(h, 0); i++; }
    else { int used; cp_add(h, 1); cp_add(h, 1); cp_add(h, utf8_get(s + i, n - i, &used)); i += (u32)used; }
    cnt++;
  }
  h->cp[cntat] = cnt;
  return at;
}
/* does code point c pass condition element at e (kind, n, points)? */
static int elem_ok(const Hun *h, u32 e, u32 c)
{
  u32 kind = h->cp[e], n = h->cp[e + 1], i;
  int in = 0;
  if (kind == 0) return 1;
  for (i = 0; i < n; i++) if (h->cp[e + 2 + i] == c) { in = 1; break; }
  return kind == 1 ? in : !in;
}
/* the condition at the end (suffix) or start (prefix) of root */
static int cond_ok(const Hun *h, const Aff *a, const char *root, u32 rn)
{
  u32 c[48], ne, e, i;
  int k;
  if (!a->cond) return 1;
  ne = h->cp[a->cond];
  k = decode(root, rn, c, 48);
  if ((u32)k < ne) return 0;
  e = a->cond + 1;
  for (i = 0; i < ne; i++) {
    u32 ch = a->pfx ? c[i] : c[(u32)k - ne + i];
    if (!elem_ok(h, e, ch)) return 0;
    e += 2 + h->cp[e + 1];
  }
  return 1;
}

/* ---- the .aff file */
/* the i-th space-separated field of line (its start and length in *len); NULL if there are fewer */
static const char *field(const char *line, int i, u32 *len)
{
  const char *p = line;
  for (;;) {
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return NULL;
    *len = 0;
    while (p[*len] && p[*len] != ' ' && p[*len] != '\t') (*len)++;
    if (!i--) return p;
    p += *len;
  }
}
static int add_aff(Hun *h, int pfx, const char *line, u16 flag, int cross)
{
  const char *f;
  u32 l, sl;
  Aff a;
  char tmp[MAXW];
  memset(&a, 0, sizeof a);
  a.flag = flag; a.cross = (u8)cross; a.pfx = (u8)pfx;
  if (!(f = field(line, 2, &l))) return 0;          /* strip */
  if (l == 1 && f[0] == '0') l = 0;
  if (l >= MAXW) return 0;
  a.strip = sp_add(h, f, l); a.striplen = (u16)l;
  if (!(f = field(line, 3, &l))) return 0;          /* add[/flags] */
  for (sl = 0; sl < l && f[sl] != '/'; sl++) ;
  if (sl < l) a.cont = flags_of(h, f + sl + 1, l - sl - 1);
  if (sl == 1 && f[0] == '0') sl = 0;
  if (sl >= MAXW) return 0;
  {
    /* IGNORE'd characters left out of the affix */
    u32 i = 0, o = 0;
    while (i < sl) {
      int used, k, ign = 0;
      u32 c = utf8_get(f + i, sl - i, &used);
      for (k = 0; k < h->nignore; k++) if (h->ignore[k] == c) ign = 1;
      if (!ign) { memcpy(tmp + o, f + i, (size_t)used); o += (u32)used; }
      i += (u32)used;
    }
    a.add = sp_add(h, tmp, o); a.addlen = (u16)o;
  }
  if ((f = field(line, 4, &l))) a.cond = parse_cond(h, f, l);
  if (h->naff >= h->affcap) {
    u32 nc = h->affcap ? h->affcap * 2 : 256;
    Aff *na = (Aff *)realloc(h->aff, nc * sizeof(Aff));
    if (!na) return 0;
    h->aff = na; h->affcap = nc;
  }
  h->aff[h->naff++] = a;
  return 1;
}
static void parse_aff(Hun *h, char *t)
{
  char *line = t, *next;
  u16 curflag = 0; int curpfx = 0, curcross = 0, curleft = 0;
  for (; line && *line; line = next) {
    u32 l, l2;
    const char *k, *v;
    next = strchr(line, '\n');
    if (next) *next++ = 0;
    { size_t n = strlen(line); while (n && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0; }
    if (!(k = field(line, 0, &l)) || *k == '#') continue;
    v = field(line, 1, &l2);
    if (l == 4 && !strncmp(k, "FLAG", 4) && v) {
      h->flagmode = !strncmp(v, "long", 4) ? FL_LONG : !strncmp(v, "num", 3) ? FL_NUM : !strncmp(v, "UTF-8", 5) ? FL_UTF8 : FL_CHAR;
    }
    else if (l == 2 && !strncmp(k, "AF", 2) && v) {
      /* the first AF line gives the count, each later one an alias's flags */
      if (v[0] >= '0' && v[0] <= '9' && !h->alias) { u32 n = (u32)atoi(v); h->alias = (u32 *)calloc(n ? n : 1, sizeof(u32)); h->nalias = 0; }
      else if (h->alias) {
        u16 f[256];
        u32 n = parse_flags(h, v, l2, f, 256);
        h->alias[h->nalias++] = fp_add(h, f, n);
      }
    }
    else if (l == 3 && (!strncmp(k, "PFX", 3) || !strncmp(k, "SFX", 3)) && v) {
      int pfx = k[0] == 'P';
      const char *third;
      u32 l3;
      u16 flag;
      char fl[16];
      if (l2 >= sizeof fl) continue;
      memcpy(fl, v, l2); fl[l2] = 0;
      { u32 sv = h->nalias; h->nalias = 0; flag = one_flag(h, fl); h->nalias = sv; }
      third = field(line, 2, &l3);
      if (curleft > 0 && flag == curflag && pfx == curpfx) { add_aff(h, pfx, line, flag, curcross); curleft--; }
      else if (third) {
        /* a header: PFX flag Y|N count */
        const char *cnt = field(line, 3, &l3);
        curflag = flag; curpfx = pfx; curcross = third[0] == 'Y';
        curleft = cnt ? atoi(cnt) : 0;
      }
    }
    else if (l == 3 && !strncmp(k, "TRY", 3) && v) h->ntry = decode(v, l2, h->try_, 48);
    else if (l == 6 && !strncmp(k, "IGNORE", 6) && v) h->nignore = decode(v, l2, h->ignore, 16);
    else if (l == 3 && !strncmp(k, "REP", 3) && v) {
      u32 l3;
      const char *to = field(line, 2, &l3);
      if (to) {
        Rep *nr = (Rep *)realloc(h->rep, (h->nrep + 1) * sizeof(Rep));
        if (nr) { h->rep = nr; h->rep[h->nrep].from = sp_add(h, v, l2); h->rep[h->nrep].to = sp_add(h, to, l3); h->nrep++; }
      }
    }
    else if (v && l == 9 && !strncmp(k, "NEEDAFFIX", 9)) h->needaffix = one_flag(h, v);
    else if (v && l == 8 && !strncmp(k, "PSEUDOROOT", 8)) h->needaffix = one_flag(h, v);
    else if (v && l == 13 && !strncmp(k, "FORBIDDENWORD", 13)) h->forbidden = one_flag(h, v);
    else if (v && l == 8 && !strncmp(k, "KEEPCASE", 8)) h->keepcase = one_flag(h, v);
    else if (v && l == 14 && !strncmp(k, "ONLYINCOMPOUND", 14)) h->onlyincompound = one_flag(h, v);
    else if (v && l == 12 && !strncmp(k, "COMPOUNDFLAG", 12)) h->cflag = one_flag(h, v);
    else if (v && l == 13 && !strncmp(k, "COMPOUNDBEGIN", 13)) h->cbegin = one_flag(h, v);
    else if (v && l == 14 && !strncmp(k, "COMPOUNDMIDDLE", 14)) h->cmiddle = one_flag(h, v);
    else if (v && l == 11 && !strncmp(k, "COMPOUNDEND", 11)) h->cend = one_flag(h, v);
    else if (v && l == 11 && !strncmp(k, "COMPOUNDMIN", 11)) { h->cmin = atoi(v); if (h->cmin < 1) h->cmin = 1; }
  }
}
/* the affixes by the byte their added part ends (suffixes) or starts (prefixes) with; 256 for none */
static int make_buckets(Hun *h)
{
  u32 i;
  int s, b;
  for (i = 0; i < h->naff; i++) {
    const Aff *a = &h->aff[i];
    b = !a->addlen ? 256 : (u8)h->sp[a->add + (a->pfx ? 0 : a->addlen - 1)];
    h->nbucket[a->pfx][b]++;
  }
  for (s = 0; s < 2; s++) for (b = 0; b < 257; b++) {
    if (h->nbucket[s][b] && !(h->bucket[s][b] = (u32 *)malloc(h->nbucket[s][b] * sizeof(u32)))) return 0;
    h->nbucket[s][b] = 0;
  }
  for (i = 0; i < h->naff; i++) {
    const Aff *a = &h->aff[i];
    b = !a->addlen ? 256 : (u8)h->sp[a->add + (a->pfx ? 0 : a->addlen - 1)];
    h->bucket[a->pfx][b][h->nbucket[a->pfx][b]++] = i;
  }
  return 1;
}

/* ---- the .dic file */
static int parse_dic(Hun *h)
{
  char *p = h->dic, *next;
  u32 cap, n = 0, size = 1;
  /* the first line is the count (a guide only) */
  next = strchr(p, '\n');
  cap = (u32)atoi(p);
  if (!next) return 0;
  p = next + 1;
  { char *q; for (q = p; *q; q++) if (*q == '\n') n++; }
  if (n + 1 > cap) cap = n + 1;
  while (size < cap * 2) size <<= 1;
  if (!(h->tab = (Ent *)calloc(size, sizeof(Ent)))) return 0;
  h->tabmask = size - 1;
  for (; p && *p; p = next) {
    char *e, *w = p, *o;
    u32 wl, fo = 0, hh;
    next = strchr(p, '\n');
    if (next) *next++ = 0;
    /* the word runs to an unescaped '/', a tab or a space */
    for (e = p, o = p; *e && *e != '\t' && *e != ' ' && *e != '\r'; e++) {
      if (*e == '\\' && e[1] == '/') { *o++ = '/'; e++; continue; }
      if (*e == '/') break;
      *o++ = *e;
    }
    wl = (u32)(o - w);
    if (*e == '/') {
      char *fs = e + 1, *fe = fs;
      while (*fe && *fe != '\t' && *fe != ' ' && *fe != '\r') fe++;
      fo = flags_of(h, fs, (u32)(fe - fs));
    }
    if (!wl) continue;
    if (h->nignore) {
      /* IGNORE'd characters left out */
      u32 i = 0, k = 0;
      char *t = w;
      while (i < wl) {
        int used, j, ign = 0;
        u32 c = utf8_get(t + i, wl - i, &used);
        for (j = 0; j < h->nignore; j++) if (h->ignore[j] == c) ign = 1;
        if (!ign) { memmove(w + k, t + i, (size_t)used); k += (u32)used; }
        i += (u32)used;
      }
      wl = k;
    }
    w[wl] = 0;
    for (hh = hash(w, wl) & h->tabmask; h->tab[hh].word; hh = (hh + 1) & h->tabmask) ;
    h->tab[hh].word = (u32)(w - h->dic) + 1;
    h->tab[hh].flags = fo;
    h->nwords++;
    if (h->nwords * 4 > size * 3) break;            /* (more lines than the count said: stop before the table fills) */
  }
  return h->nwords > 0;
}

Hun *hun_load(const char *aff, size_t an, const char *dic, size_t dn)
{
  Hun *h = (Hun *)calloc(1, sizeof(Hun));
  char *at;
  size_t n;
  int cs;
  if (!h) return NULL;
  h->cmin = 3;
  cs = charset_of(aff, an);
  h->fpcap = 4096; h->fp = (u16 *)malloc(h->fpcap * sizeof(u16)); h->nfp = 1;
  if (!h->fp) { free(h); return NULL; }
  h->fp[0] = 0;
  sp_add(h, "", 0);                                 /* (offset 0: the empty string) */
  if (!(at = to_utf8(cs, aff, an, &n))) { hun_free(h); return NULL; }
  parse_aff(h, at);
  free(at);
  if (!h->ntry) h->ntry = decode("esianrtolcdugmphbyfvkwzESIANRTOLCDUGMPHBYFVKWZ'", 47, h->try_, 48);
  if (!make_buckets(h) || !(h->dic = to_utf8(cs, dic, dn, &n)) || !parse_dic(h)) { hun_free(h); return NULL; }
  return h;
}
void hun_free(Hun *h)
{
  int s, b;
  if (!h) return;
  for (s = 0; s < 2; s++) for (b = 0; b < 257; b++) free(h->bucket[s][b]);
  free(h->dic); free(h->tab); free(h->fp); free(h->cp); free(h->sp); free(h->aff); free(h->alias); free(h->rep);
  free(h);
}
u32 hun_count(const Hun *h) { return h ? h->nwords : 0; }

/* ---- checking */
/* a stem w (n bytes) in the dictionary with flags f1 and f2 (0: any), reached through an affix or not,
   its case changed or not (KEEPCASE words then don't count), and for a compound's part one of the
   flags anyof (0-terminated; NULL: not a compound part, so ONLYINCOMPOUND stems don't count) */
static int root_ok(const Hun *h, const char *w, u32 n, u16 f1, u16 f2, int affixed, int low, const u16 *anyof)
{
  u32 k;
  for (k = hash(w, n) & h->tabmask; h->tab[k].word; k = (k + 1) & h->tabmask) {
    const char *e = h->dic + h->tab[k].word - 1;
    u32 fo = h->tab[k].flags;
    if (strncmp(e, w, n) || e[n]) continue;
    if (h->forbidden && has(h, fo, h->forbidden)) continue;
    if (f1 && !has(h, fo, f1)) continue;
    if (f2 && !has(h, fo, f2)) continue;
    if (!affixed && h->needaffix && has(h, fo, h->needaffix)) continue;
    if (low && h->keepcase && has(h, fo, h->keepcase)) continue;
    if (anyof) {
      int i, any = !anyof[0];                     /* (an empty list: its affix allowed it already) */
      for (i = 0; anyof[i]; i++) if (has(h, fo, anyof[i])) any = 1;
      if (!any) continue;
    } else if (h->onlyincompound && has(h, fo, h->onlyincompound)) continue;
    return 1;
  }
  return 0;
}
static int forbidden_word(const Hun *h, const char *w, u32 n)
{
  u32 k;
  if (!h->forbidden) return 0;
  for (k = hash(w, n) & h->tabmask; h->tab[k].word; k = (k + 1) & h->tabmask) {
    const char *e = h->dic + h->tab[k].word - 1;
    if (!strncmp(e, w, n) && !e[n] && has(h, h->tab[k].flags, h->forbidden)) return 1;
  }
  return 0;
}
static const u16 any_part[1] = { 0 };
/* what a stem reached through affix a (and b, may be NULL) must have to be this compound part: nothing
   more if the affix's continuation flags allow it (German dictionaries give the compound flags to
   empty affixes so), else anyof; -1: the affix may only be used in compounds and this isn't one */
static const u16 *part_need(const Hun *h, const Aff *a, const Aff *b, const u16 *anyof, int *bad)
{
  int i;
  *bad = 0;
  if (!anyof) {
    if (h->onlyincompound && (has(h, a->cont, h->onlyincompound) || (b && has(h, b->cont, h->onlyincompound)))) *bad = 1;
    return NULL;
  }
  for (i = 0; anyof[i]; i++) if (has(h, a->cont, anyof[i]) || (b && has(h, b->cont, anyof[i]))) return any_part;
  return anyof;
}
/* w as a stem, or a stem with a suffix, a prefix, both, or two suffixes */
static int forms_ok(const Hun *h, const char *w, u32 n, int low, const u16 *anyof)
{
  char r[MAXW + 8], r2[MAXW + 8];
  int pass, b, b2;
  u32 i, j;
  if (!n || n > MAXW) return 0;
  if (root_ok(h, w, n, 0, 0, 0, low, anyof)) return 1;
  /* suffixes (those whose added part ends as w does, then those that add nothing) */
  for (pass = 0; pass < 2; pass++) {
    b = pass ? 256 : (u8)w[n - 1];
    for (i = 0; i < h->nbucket[0][b]; i++) {
      const Aff *a = &h->aff[h->bucket[0][b][i]];
      u32 rl;
      if (a->addlen >= n || memcmp(w + n - a->addlen, h->sp + a->add, a->addlen)) continue;
      rl = n - a->addlen;
      if (rl + a->striplen > MAXW) continue;
      memcpy(r, w, rl); memcpy(r + rl, h->sp + a->strip, a->striplen); rl += a->striplen;
      if (!cond_ok(h, a, r, rl)) continue;
      { int bad; const u16 *nd = part_need(h, a, NULL, anyof, &bad);
        if (!bad && !(h->needaffix && has(h, a->cont, h->needaffix)) && root_ok(h, r, rl, a->flag, 0, 1, low, nd)) return 1; }
      if (a->cross) {
        /* and a prefix before it */
        int pp;
        for (pp = 0; pp < 2; pp++) {
          b2 = pp ? 256 : (u8)r[0];
          for (j = 0; j < h->nbucket[1][b2]; j++) {
            const Aff *p = &h->aff[h->bucket[1][b2][j]];
            u32 l2;
            if (!p->cross || p->addlen >= rl || memcmp(r, h->sp + p->add, p->addlen)) continue;
            l2 = rl - p->addlen + p->striplen;
            if (l2 > MAXW) continue;
            memcpy(r2, h->sp + p->strip, p->striplen); memcpy(r2 + p->striplen, r + p->addlen, rl - p->addlen);
            { int bad; const u16 *nd = part_need(h, a, p, anyof, &bad);
              if (!bad && cond_ok(h, p, r2, l2) && root_ok(h, r2, l2, a->flag, p->flag, 1, low, nd)) return 1; }
          }
        }
      }
      /* two suffixes: r is a stem with another suffix whose continuation allows this one */
      {
        int pp;
        for (pp = 0; pp < 2; pp++) {
          b2 = pp ? 256 : (u8)r[rl - 1];
          for (j = 0; j < h->nbucket[0][b2]; j++) {
            const Aff *a2 = &h->aff[h->bucket[0][b2][j]];
            u32 l2;
            if (!a2->cont || !has(h, a2->cont, a->flag) || a2->addlen >= rl || memcmp(r + rl - a2->addlen, h->sp + a2->add, a2->addlen)) continue;
            l2 = rl - a2->addlen;
            if (l2 + a2->striplen > MAXW) continue;
            memcpy(r2, r, l2); memcpy(r2 + l2, h->sp + a2->strip, a2->striplen); l2 += a2->striplen;
            { int bad; const u16 *nd = part_need(h, a, a2, anyof, &bad);
              if (!bad && cond_ok(h, a2, r2, l2) && root_ok(h, r2, l2, a2->flag, 0, 1, low, nd)) return 1; }
          }
        }
      }
    }
  }
  /* prefixes */
  for (pass = 0; pass < 2; pass++) {
    b = pass ? 256 : (u8)w[0];
    for (i = 0; i < h->nbucket[1][b]; i++) {
      const Aff *p = &h->aff[h->bucket[1][b][i]];
      u32 rl;
      if (p->addlen >= n || memcmp(w, h->sp + p->add, p->addlen)) continue;
      rl = n - p->addlen + p->striplen;
      if (rl > MAXW) continue;
      memcpy(r, h->sp + p->strip, p->striplen); memcpy(r + p->striplen, w + p->addlen, n - p->addlen);
      { int bad; const u16 *nd = part_need(h, p, NULL, anyof, &bad);
        if (!bad && cond_ok(h, p, r, rl) && !(h->needaffix && has(h, p->cont, h->needaffix)) && root_ok(h, r, rl, p->flag, 0, 1, low, nd)) return 1; }
    }
  }
  return 0;
}
/* w with its first letter made capital (cap 1) or small (0) into out; its length */
static u32 recase_first(const char *w, u32 n, char *out, int cap)
{
  int used;
  u32 c = utf8_get(w, n, &used);
  u32 k = (u32)utf8_put(out, cap ? uc(c) : lc(c));
  if (k + n - (u32)used > MAXW) return 0;
  memcpy(out + k, w + used, n - (u32)used);
  return k + n - (u32)used;
}
/* chars in w */
static u32 nchars(const char *w, u32 n) { u32 i, k = 0; for (i = 0; i < n; i++) if (((u8)w[i] & 0xC0) != 0x80) k++; return k; }
/* w made of two or more dictionary words joined (the compound flags allowing it) */
static int compound_ok(const Hun *h, const char *w, u32 n, int low, int depth)
{
  u16 first[3], last[3];
  u32 i;
  char t[MAXW + 8];
  if (!(h->cflag || h->cbegin) || depth > 2 || n > MAXW) return 0;
  first[0] = h->cflag ? h->cflag : (depth ? h->cmiddle : h->cbegin); first[1] = depth ? h->cmiddle : h->cbegin; first[2] = 0;
  if (!first[0]) { first[0] = first[1]; first[1] = 0; }
  last[0] = h->cflag ? h->cflag : h->cend; last[1] = h->cend; last[2] = 0;
  if (!last[0]) { last[0] = last[1]; last[1] = 0; }
  if (!first[0] || !last[0]) return 0;
  for (i = 1; i < n; i++) {
    u32 rn;
    if (((u8)w[i] & 0xC0) == 0x80) continue;
    if (nchars(w, i) < (u32)h->cmin || nchars(w + i, n - i) < (u32)h->cmin) continue;
    if (!forms_ok(h, w, i, low, first)) continue;
    if (forms_ok(h, w + i, n - i, low, last)) return 1;
    /* (German nouns: the part as the dictionary has it, with a capital) */
    if ((rn = recase_first(w + i, n - i, t, 1)) && forms_ok(h, t, rn, low, last)) return 1;
    if (compound_ok(h, w + i, n - i, low, depth + 1)) return 1;
  }
  return 0;
}
static int check(const Hun *h, const char *w, u32 n, int low)
{
  return forms_ok(h, w, n, low, NULL) || compound_ok(h, w, n, low, 0);
}
int hun_ok(const Hun *h, const char *word, u32 n)
{
  char w[MAXW + 8], t[MAXW + 8];
  u32 c[MAXW], i, wn = 0;
  int k, nc, ncap = 0, nlow = 0;
  if (!h || !n) return 1;
  if (n > MAXW) return 1;
  /* the typographic apostrophe as the straight one; IGNORE'd characters out */
  nc = decode(word, n, c, MAXW);
  for (k = 0; k < nc; k++) {
    int j, ign = 0;
    if (c[k] == 0x2019) c[k] = '\'';
    for (j = 0; j < h->nignore; j++) if (h->ignore[j] == c[k]) ign = 1;
    if (ign) continue;
    if (is_upper(c[k])) ncap++; else if (is_lower(c[k])) nlow++;
    c[wn++] = c[k];
  }
  n = encode(c, (int)wn, w, sizeof w);
  if (!n) return 1;
  if (forbidden_word(h, w, n)) return 0;
  if (check(h, w, n, 0)) return 1;
  if (wn && is_upper(c[0])) {
    u32 rn = recase_first(w, n, t, 0);
    if (rn && check(h, t, rn, 1)) return 1;
    if (!nlow && ncap > 1) {
      /* ALL CAPITALS: all small, or Capitalised */
      u32 l[MAXW];
      for (i = 0; i < wn; i++) l[i] = lc(c[i]);
      rn = encode(l, (int)wn, t, sizeof t);
      if (check(h, t, rn, 1)) return 1;
      l[0] = c[0];
      rn = encode(l, (int)wn, t, sizeof t);
      if (check(h, t, rn, 1)) return 1;
    }
  }
  return 0;
}

/* ---- suggestions: words one edit away (or a REP replacement), ranked by how likely the slip is */
#define HSUG_MAX 48
typedef struct { char *w; int score, seq; } HSug;
static int hsug_cmp(const void *x, const void *y)
{
  const HSug *a = (const HSug *)x, *b = (const HSug *)y;
  return a->score != b->score ? a->score - b->score : a->seq - b->seq;
}
typedef struct { const Hun *h; HSug *s; int n, cap1; } Cands;
static void cand(Cands *c, const u32 *cp, int n, int score)
{
  char buf[MAXW + 8];
  u32 len;
  int k;
  if (c->n >= HSUG_MAX || n <= 0) return;
  len = encode(cp, n, buf, sizeof buf);
  for (k = 0; k < c->n; k++) if (!strcmp(c->s[k].w, buf)) return;
  if (!hun_ok(c->h, buf, len)) return;
  if (c->cap1) { u32 cc[MAXW]; int m = decode(buf, len, cc, MAXW); if (m) { cc[0] = uc(cc[0]); len = encode(cc, m, buf, sizeof buf); } }
  if ((c->s[c->n].w = str_ndup(buf, len))) { c->s[c->n].score = score; c->s[c->n].seq = c->n; c->n++; }
}
int hun_suggest(const Hun *h, const char *w, u32 n, char **out, int max)
{
  u32 c[64], t[66];
  int nc, i, j, k, got = 0;
  Cands cs;
  if (!h || !n || max <= 0 || n > MAXW) return 0;
  nc = decode(w, n, c, 64);
  if (nc >= 63) return 0;
  memset(&cs, 0, sizeof cs);
  cs.h = h;
  if (!(cs.s = (HSug *)malloc(HSUG_MAX * sizeof(HSug)))) return 0;
  cs.cap1 = nc && is_upper(c[0]);
  for (i = 0; i < nc; i++) if (c[i] == 0x2019) c[i] = '\'';
  /* REP: a known misspelling of part of a word */
  for (k = 0; k < (int)h->nrep; k++) {
    u32 f[32], r[32];
    int fn = decode(h->sp + h->rep[k].from, (u32)strlen(h->sp + h->rep[k].from), f, 32);
    int rn = decode(h->sp + h->rep[k].to, (u32)strlen(h->sp + h->rep[k].to), r, 32);
    for (i = 0; fn && i + fn <= nc; i++) {
      int m = 0;
      if (memcmp(c + i, f, (size_t)fn * sizeof(u32))) continue;
      memcpy(t, c, (size_t)i * sizeof(u32)); m = i;
      for (j = 0; j < rn && m < 64; j++) t[m++] = r[j] == '_' ? ' ' : r[j];
      for (j = i + fn; j < nc && m < 64; j++) t[m++] = c[j];
      cand(&cs, t, m, 0);
    }
  }
  /* the word with a capital (a name typed small) */
  if (nc && is_lower(c[0])) {
    memcpy(t, c, (size_t)nc * sizeof(u32)); t[0] = uc(t[0]); cand(&cs, t, nc, 1);
    for (i = 1; i < nc; i++) t[i] = uc(t[i]);
    cand(&cs, t, nc, 1);                               /* (or an abbreviation: NASA) */
  }
  for (i = 0; i < nc; i++) {
    /* two swapped */
    if (i + 1 < nc) { memcpy(t, c, (size_t)nc * sizeof(u32)); t[i] = c[i + 1]; t[i + 1] = c[i]; cand(&cs, t, nc, 1); }
    /* one too many (one of a doubled pair is the likeliest) */
    memcpy(t, c, (size_t)i * sizeof(u32)); memcpy(t + i, c + i + 1, (size_t)(nc - i - 1) * sizeof(u32));
    cand(&cs, t, nc - 1, (i > 0 && c[i - 1] == c[i]) || (i + 1 < nc && c[i + 1] == c[i]) ? 0 : 3);
  }
  for (i = 0; i <= nc; i++) for (k = 0; k < h->ntry; k++) {
    /* one left out */
    u32 ch = h->try_[k];
    memcpy(t, c, (size_t)i * sizeof(u32)); t[i] = ch; memcpy(t + i + 1, c + i, (size_t)(nc - i) * sizeof(u32));
    cand(&cs, t, nc + 1, (i > 0 && c[i - 1] == ch) || (i < nc && c[i] == ch) ? 0 : 2);
  }
  for (i = 0; i < nc; i++) for (k = 0; k < h->ntry; k++) {
    /* one changed */
    if (h->try_[k] == c[i] || lc(h->try_[k]) == lc(c[i])) continue;
    memcpy(t, c, (size_t)nc * sizeof(u32)); t[i] = h->try_[k];
    cand(&cs, t, nc, i == 0 ? 5 : 3);
  }
  /* and a space put in (two words run together) */
  for (i = 2; i + 1 < nc; i++) {
    char a[MAXW + 8], b[MAXW + 8];
    u32 al = encode(c, i, a, sizeof a), bl = encode(c + i, nc - i, b, sizeof b);
    if (hun_ok(h, a, al) && hun_ok(h, b, bl)) { memcpy(t, c, (size_t)i * sizeof(u32)); t[i] = ' '; memcpy(t + i + 1, c + i, (size_t)(nc - i) * sizeof(u32));
      if (cs.n < HSUG_MAX) { char buf[MAXW + 8]; u32 l = encode(t, nc + 1, buf, sizeof buf); if ((cs.s[cs.n].w = str_ndup(buf, l))) { cs.s[cs.n].score = 6; cs.s[cs.n].seq = cs.n; cs.n++; } } }
  }
  qsort(cs.s, (size_t)cs.n, sizeof(HSug), hsug_cmp);
  for (k = 0; k < cs.n; k++) { if (got < max) out[got++] = cs.s[k].w; else free(cs.s[k].w); }
  free(cs.s);
  return got;
}
