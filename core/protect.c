/*
 * protect.c - Restrict Editing and the encryption password (see protect.h). The documentProtection hash
 * follows ECMA-376 Part 1 17.15.1.29 and Part 4 14.8.1 (Word's legacy key), [MS-OI29500] and
 * [MS-OFFCRYPTO] 2.4.2.4: the way the legacy key is hashed (as an upper-case hex string of its bytes
 * reversed, the iteration number after the hash) is as WordCraft's crates/docx/src/crypt.rs does it
 * (MIT OR Apache-2.0, (c) its authors), written again here in C.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "protect.h"
#include "crypt.h"

char prot_user[64];
void (*prot_on_denied)(Doc *d);

DocProtect *prot_peek(const Doc *d) { return d ? d->prot : NULL; }
DocProtect *prot_get(Doc *d)
{
  if (!d->prot && (d->prot = (DocProtect *)calloc(1, sizeof(DocProtect)))) d->prot->sid = 14;
  return d->prot;
}
void prot_free(Doc *d)
{
  DocProtect *p = d->prot;
  if (!p) return;
  free(p->perms);
  if (p->password) { crypt_wipe(p->password, strlen(p->password)); free(p->password); }
  crypt_wipe(p, sizeof *p);
  free(p);
  d->prot = NULL;
}

/* ------------------------------------------------------------------ what is enforced */
int prot_enforced(const Doc *d)
{
  const DocProtect *p = prot_peek(d);
  return p && p->enforced && !p->bypass ? p->kind : PR_NONE;
}
int prot_forces_tracking(const Doc *d) { return prot_enforced(d) == PR_TRACKED; }
int prot_forms_only(const Doc *d) { return prot_enforced(d) == PR_FORMS; }
int (*prot_form_at)(const Doc *d, Pos p);
static int ieq(const char *a, const char *b)
{
  for (; *a && *b; a++, b++) {
    char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
    if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
    if (x != y) return 0;
  }
  return *a == *b;
}
int prot_perm_mine(const Doc *d, int k)
{
  const DocProtect *p = prot_peek(d);
  const Perm *e;
  if (!p || k < 1 || k > p->nperms) return 0;
  e = &p->perms[k - 1];
  if (e->grp[0] && (ieq(e->grp, "everyone") || ieq(e->grp, "current"))) return 1;
  if (e->ed[0] && prot_user[0]) {
    const char *bs = strrchr(e->ed, '\\');
    if (ieq(e->ed, prot_user) || (bs && ieq(bs + 1, prot_user))) return 1;
  }
  return 0;
}
/* the run covering off (the last at the end) is free */
static int free_run(const Doc *d, Para *p, u32 off)
{
  if (prot_perm_mine(d, para_fmt_at(p, off)->perm)) return 1;
  if (prot_form_at && prot_forms_only(d)) {
    u32 pi;
    for (pi = 0; pi < d->np; pi++) if (d->p[pi] == p) { Pos q; q.para = pi; q.off = off; return prot_form_at(d, q); }
  }
  return 0;
}
int prot_free_at(const Doc *d, Pos p)
{
  if (p.para >= d->np) return 0;
  return free_run(d, d->p[p.para], p.off);
}
static int any_free(const Doc *d, const Para *p)
{
  u32 i;
  for (i = 0; i < p->nr; i++) if (prot_perm_mine(d, p->r[i].f.perm) || free_run(d, (Para *)p, i ? p->r[i - 1].end : 0)) return 1;
  return 0;
}
static int deny(Doc *d)
{
  DocProtect *p = d->prot;
  if (p) p->pass = 0;
  if (prot_on_denied) prot_on_denied(d);
  return 0;
}
static int allow(Doc *d)
{
  DocProtect *p = d->prot;
  if (p) { p->pass = 1; p->passver = d->ver; }
  return 1;
}
/* nothing to check: not enforced, or tracked changes (anything may change, tracked), or a comment's own
   text while comments are allowed */
static int unchecked(const Doc *d)
{
  int k = prot_enforced(d);
  return k == PR_NONE || k == PR_TRACKED || (k == PR_COMMENTS && d->edit.kind == SK_COMMENT);
}
static int only_marks(const char *s, u32 n)
{
  u32 i;
  if (!n || n % 4) return 0;
  for (i = 0; i < n; i += 4) if (!IS_MARK(s + i)) return 0;
  return 1;
}
int prot_insert_ok(Doc *d, Pos at, const char *s, u32 n)
{
  Para *p;
  if (unchecked(d)) return allow(d);
  if (prot_enforced(d) == PR_COMMENTS && only_marks(s, n)) return allow(d);     /* a comment's marks */
  if (at.para >= d->np) return deny(d);
  p = d->p[at.para];
  if (at.off > p->len) at.off = p->len;
  /* typing at the edge of a free stretch continues it: the character before, or the one after */
  if ((at.off > 0 && free_run(d, p, at.off - 1)) || (at.off < p->len && free_run(d, p, at.off)) || (!p->len && free_run(d, p, 0)))
    return allow(d);
  return deny(d);
}
int prot_range_ok(Doc *d, Pos a, Pos b, int format)
{
  u32 pi;
  if (unchecked(d)) return allow(d);
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  if (prot_enforced(d) == PR_COMMENTS && a.para == b.para && b.off == a.off + 4 && a.para < d->np && a.off + 4 <= d->p[a.para]->len &&
      IS_MARK(d->p[a.para]->t + a.off))
    return allow(d);                          /* a comment's mark taken out */
  for (pi = a.para; pi <= b.para && pi < d->np; pi++) {
    Para *p = d->p[pi];
    u32 s = pi == a.para ? a.off : 0, e = pi == b.para ? b.off : p->len, i, start = 0;
    if (!p->len || s == e) { if (!free_run(d, p, s)) return deny(d); continue; }
    for (i = 0; i < p->nr; start = p->r[i].end, i++)
      if (p->r[i].end > s && start < e && !prot_perm_mine(d, p->r[i].f.perm) && !free_run(d, p, start > s ? start : s)) return deny(d);
  }
  return allow(d);
}
int prot_resolve_ok(Doc *d)
{
  if (prot_enforced(d) == PR_TRACKED) return deny(d);     /* (Word greys Accept and Reject out) */
  return 1;
}
int prot_gate(Doc *d, u32 p0, u32 n)
{
  DocProtect *p = d->prot;
  u32 i;
  if (unchecked(d)) return 1;
  if (p->pass && p->passver == d->ver) return 1;
  if (!n) return (p0 < d->np && any_free(d, d->p[p0])) || (p0 > 0 && p0 - 1 < d->np && any_free(d, d->p[p0 - 1])) ? 1 : deny(d);
  for (i = p0; i < p0 + n && i < d->np; i++) if (!any_free(d, d->p[i])) return deny(d);
  return 1;
}
int prot_gate_styles(Doc *d)
{
  int k = prot_enforced(d);
  if (k == PR_NONE || k == PR_TRACKED) return 1;
  return deny(d);
}

/* ------------------------------------------------------------------ setting it up */
void prot_set_kind(Doc *d, int kind, int formatting)
{
  DocProtect *p = prot_get(d);
  if (!p || p->enforced) return;
  p->kind = (u8)kind; p->formatting = (u8)(formatting != 0);
  d->dirty = 1;
}
/* Word's legacy key (ECMA-376 Part 4 14.8.1): the high word from the initial code array and the
   encryption matrix, the low word from the characters rotated and XOR-ed backwards */
static const u16 initcode[15] = { 0xE1F0, 0x1D0F, 0xCC9C, 0x84C0, 0x110C, 0x0E10, 0xF1CE, 0x313E, 0x1872, 0xE139, 0xD40F, 0x84F9, 0x280C, 0xA96A, 0x4EC3 };
static const u16 matrix[15][7] = {
  { 0xAEFC, 0x4DD9, 0x9BB2, 0x2745, 0x4E8A, 0x9D14, 0x2A09 }, { 0x7B61, 0xF6C2, 0xFDA5, 0xEB6B, 0xC6F7, 0x9DCF, 0x2BBF },
  { 0x4563, 0x8AC6, 0x05AD, 0x0B5A, 0x16B4, 0x2D68, 0x5AD0 }, { 0x0375, 0x06EA, 0x0DD4, 0x1BA8, 0x3750, 0x6EA0, 0xDD40 },
  { 0xD849, 0xA0B3, 0x5147, 0xA28E, 0x553D, 0xAA7A, 0x44D5 }, { 0x6F45, 0xDE8A, 0xAD35, 0x4A4B, 0x9496, 0x390D, 0x721A },
  { 0xEB23, 0xC667, 0x9CEF, 0x29FF, 0x53FE, 0xA7FC, 0x5FD9 }, { 0x47D3, 0x8FA6, 0x0F6D, 0x1EDA, 0x3DB4, 0x7B68, 0xF6D0 },
  { 0xB861, 0x60E3, 0xC1C6, 0x93AD, 0x377B, 0x6EF6, 0xDDEC }, { 0x45A0, 0x8B40, 0x06A1, 0x0D42, 0x1A84, 0x3508, 0x6A10 },
  { 0xAA51, 0x4483, 0x8906, 0x022D, 0x045A, 0x08B4, 0x1168 }, { 0x76B4, 0xED68, 0xCAF1, 0x85C3, 0x1BA7, 0x374E, 0x6E9C },
  { 0x3730, 0x6E60, 0xDCC0, 0xA9A1, 0x4363, 0x86C6, 0x1DAD }, { 0x3331, 0x6662, 0xCCC4, 0x89A9, 0x0373, 0x06E6, 0x0DCC },
  { 0x1021, 0x2042, 0x4084, 0x8108, 0x1231, 0x2462, 0x48C4 } };
u32 prot_legacy_key(const char *pw)
{
  u8 b[15];
  int n = 0, i, bit;
  size_t k = 0, len = strlen(pw);
  u16 hi, lo = 0;
  while (k < len && n < 15) {
    int used;
    u32 ch = utf8_get(pw + k, len - k, &used);
    k += (size_t)used;
    if (ch >= 0x10000) {                     /* (two UTF-16 units) */
      u32 v = ch - 0x10000, h = 0xD800 + (v >> 10), l = 0xDC00 + (v & 0x3FF);
      b[n++] = (u8)((h & 0xFF) ? h & 0xFF : h >> 8);
      if (n < 15) b[n++] = (u8)((l & 0xFF) ? l & 0xFF : l >> 8);
    } else if (ch != 0xFEFF || n) b[n++] = (u8)((ch & 0xFF) ? ch & 0xFF : ch >> 8);
  }
  if (!n) return 0;
  hi = initcode[n - 1];
  for (i = 0; i < n; i++)
    for (bit = 0; bit < 7; bit++) if (b[i] & (1 << bit)) hi ^= matrix[15 - n + i][bit];
  for (i = n - 1; i >= 0; i--) lo = (u16)((((lo >> 14) & 1) | ((lo << 1) & 0x7FFF)) ^ b[i]);
  lo = (u16)((((lo >> 14) & 1) | ((lo << 1) & 0x7FFF)) ^ (u16)n ^ 0xCE4B);
  crypt_wipe(b, sizeof b);
  return (u32)hi << 16 | lo;
}
/* H0 = H(salt + text as UTF-16LE), Hn = H(Hn-1 + iterator) */
static void prot_hash(int alg, const u8 *salt, size_t sl, const char *text, u32 spin, u8 *out)
{
  u8 t16[512], buf[68];
  size_t tl = enc_utf16(text, t16, sizeof t16);
  int hl = hash_len(alg);
  u32 i;
  hash2(alg, salt, sl, t16, tl, out);
  if (alg == CH_SHA512) { sha512_spin(out, spin, 1); spin = 0; }
  for (i = 0; i < spin; i++) {
    memcpy(buf, out, (size_t)hl);
    buf[hl] = (u8)i; buf[hl + 1] = (u8)(i >> 8); buf[hl + 2] = (u8)(i >> 16); buf[hl + 3] = (u8)(i >> 24);
    hash2(alg, buf, (size_t)hl + 4, NULL, 0, out);
  }
  crypt_wipe(t16, sizeof t16);
}
/* what Word's form hashes: the legacy key's bytes, lowest first, as upper-case hex */
static void legacy_text(const char *pw, char *out)
{
  u32 k = prot_legacy_key(pw);
  sprintf(out, "%02X%02X%02X%02X", (unsigned)(k & 255), (unsigned)((k >> 8) & 255), (unsigned)((k >> 16) & 255), (unsigned)(k >> 24));
}
int prot_start(Doc *d, const char *pw)
{
  DocProtect *p = prot_get(d);
  if (!p) return 0;
  p->hash[0] = p->salt[0] = 0;
  if (pw && pw[0]) {
    u8 salt[16], h[64];
    char t[12];
    crypt_random(salt, 16);
    legacy_text(pw, t);
    p->iso = 0; p->sid = 14; p->spin = PROT_SPIN;
    strcpy(p->provider, "rsaAES");
    prot_hash(CH_SHA512, salt, 16, t, p->spin, h);
    b64_put(h, 64, p->hash, sizeof p->hash);
    b64_put(salt, 16, p->salt, sizeof p->salt);
    crypt_wipe(t, sizeof t); crypt_wipe(h, sizeof h);
  }
  if (p->kind == PR_NONE) p->kind = PR_READONLY;
  p->enforced = 1;
  d->dirty = 1; d->ver++;
  doc_clear_undo(d);                          /* (what was done before can't be undone past the lock) */
  return 1;
}
int prot_check(const Doc *d, const char *pw)
{
  const DocProtect *p = prot_peek(d);
  u8 want[64], salt[64], h[64];
  long wl, sl;
  int alg;
  if (!p || !p->hash[0]) return PC_NOPASSWORD;
  if (p->iso) alg = p->sid;                    /* (the ISO form keeps its CH_ in sid) */
  else alg = p->sid == 4 ? CH_SHA1 : p->sid == 14 ? CH_SHA512 : -1;
  if (alg < 0 || p->spin > 10000000UL) return PC_UNSUPPORTED;
  wl = b64_get(p->hash, strlen(p->hash), want, sizeof want);
  sl = p->salt[0] ? b64_get(p->salt, strlen(p->salt), salt, sizeof salt) : 0;
  if (wl != hash_len(alg) || sl < 0) return PC_UNSUPPORTED;
  if (p->iso) prot_hash(alg, salt, (size_t)sl, pw ? pw : "", p->spin, h);
  else { char t[12]; legacy_text(pw ? pw : "", t); prot_hash(alg, salt, (size_t)sl, t, p->spin, h); }
  return memcmp(h, want, (size_t)wl) ? PC_INCORRECT : PC_CORRECT;
}
int prot_stop(Doc *d, const char *pw)
{
  int r = prot_check(d, pw);
  DocProtect *p = prot_peek(d);
  if (!p) return PC_NOPASSWORD;
  if (r == PC_NOPASSWORD || r == PC_CORRECT) {
    p->enforced = 0; p->hash[0] = p->salt[0] = 0;
    d->dirty = 1; d->ver++;
  }
  return r;
}
static int perm_add(Doc *d, const char *ed, const char *grp, i32 cf, i32 cl)
{
  DocProtect *p = prot_get(d);
  Perm *n;
  int i;
  if (!p) return 0;
  ed = ed ? ed : ""; grp = grp ? grp : "";
  for (i = 0; i < p->nperms; i++)
    if (!strcmp(p->perms[i].ed, ed) && ieq(p->perms[i].grp, grp) && p->perms[i].colfirst == cf && p->perms[i].collast == cl) return i + 1;
  if (p->nperms >= 60000 || !(n = (Perm *)realloc(p->perms, sizeof(Perm) * (size_t)(p->nperms + 1)))) return 0;
  p->perms = n;
  memset(&n[p->nperms], 0, sizeof(Perm));
  strncpy(n[p->nperms].ed, ed, sizeof n->ed - 1);
  strncpy(n[p->nperms].grp, grp, sizeof n->grp - 1);
  n[p->nperms].colfirst = cf; n[p->nperms].collast = cl;
  return ++p->nperms;
}
int prot_perm(Doc *d, const char *ed, const char *grp) { return perm_add(d, ed, grp, -1, -1); }
int prot_mark(Doc *d, Pos a, Pos b, int perm)
{
  DocProtect *p = prot_get(d);
  CharFmt v;
  int r;
  if (!p) return 0;
  memset(&v, 0, sizeof v);
  v.perm = (u16)perm;
  p->bypass = 1;
  r = doc_format(d, a, b, CM_PERM, &v);
  p->bypass = 0;
  return r;
}
int prot_perms_in(Doc *d, Pos a, Pos b, int *out, int max)
{
  u32 pi;
  int n = 0, j;
  if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
  for (pi = a.para; pi <= b.para && pi < d->np; pi++) {
    Para *p = d->p[pi];
    u32 s = pi == a.para ? a.off : 0, e = pi == b.para ? b.off : p->len, i, start = 0;
    for (i = 0; i < p->nr; start = p->r[i].end, i++) {
      int k = p->r[i].f.perm;
      if (!k || !(p->r[i].end > s && start <= e)) continue;
      for (j = 0; j < n && out[j] != k; j++) ;
      if (j == n && n < max) out[n++] = k;
    }
  }
  return n;
}
int prot_next_free(const Doc *d, Pos p, Pos *a, Pos *b)
{
  u32 k, pi;
  if (!d->np) return 0;
  if (p.para >= d->np) p.para = 0, p.off = 0;
  for (k = 0; k <= d->np; k++) {
    Para *q;
    u32 i, start = 0, from;
    pi = (p.para + k) % d->np;
    q = d->p[pi];
    from = k == 0 ? p.off : 0;
    for (i = 0; i < q->nr; start = q->r[i].end, i++) {
      if (q->r[i].end <= from && !(k == 0 && from == 0 && !q->len)) continue;
      if (!prot_perm_mine(d, q->r[i].f.perm)) continue;
      if (k == 0 && start < from && from > 0) continue;  /* (the stretch p is in: the next one) */
      a->para = b->para = pi; a->off = start;
      while (i + 1 < q->nr && prot_perm_mine(d, q->r[i + 1].f.perm)) i++;
      b->off = q->r[i].end;
      return 1;
    }
  }
  return 0;
}

/* ------------------------------------------------------------------ encryption */
void prot_set_password(Doc *d, const char *pw)
{
  DocProtect *p = prot_get(d);
  if (!p) return;
  if (p->password) { crypt_wipe(p->password, strlen(p->password)); free(p->password); p->password = NULL; }
  memset(&p->key, 0, sizeof p->key);
  if (pw && pw[0]) p->password = str_dup(pw);
  d->dirty = 1;
}
const char *prot_password(const Doc *d) { const DocProtect *p = prot_peek(d); return p ? p->password : NULL; }
int prot_open_encrypted(const u8 *data, size_t len, const char *(*ask)(int), u8 **out, size_t *outlen, char **pw, EncKey *key)
{
  int attempt, rc = ENC_BADPW;
  *out = NULL; *pw = NULL;
  if (!enc_is_encrypted(data, len)) return ENC_NOTENC;
  for (attempt = 0; attempt < 5; attempt++) {
    const char *p = ask ? ask(attempt) : NULL;
    if (!p) return PROT_CANCELLED;
    rc = enc_decrypt(data, len, p, out, outlen, key);
    if (rc == ENC_OK) { *pw = str_dup(p); return ENC_OK; }
    if (rc != ENC_BADPW) return rc;
  }
  return rc;
}

/* ------------------------------------------------------------------ .docx */
static int on(const char *v) { return v && (!strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "on")); }
static void cpy(char *dst, size_t cap, const char *v) { if (v) { strncpy(dst, v, cap - 1); dst[cap - 1] = 0; } else dst[0] = 0; }
void prot_read_setting(Doc *d, Xml *x)
{
  DocProtect *p;
  const char *v;
  if (strcmp(x->name, "documentProtection") || !(p = prot_get(d))) return;
  v = xml_attr(x, "edit");
  p->kind = !v ? PR_NONE : !strcmp(v, "readOnly") ? PR_READONLY : !strcmp(v, "comments") ? PR_COMMENTS :
            !strcmp(v, "trackedChanges") ? PR_TRACKED : !strcmp(v, "forms") ? PR_FORMS : PR_NONE;
  p->formatting = (u8)on(xml_attr(x, "formatting"));
  p->enforced = (u8)on(xml_attr(x, "enforcement"));
  if ((v = xml_attr(x, "hashValue"))) {
    p->iso = 1;
    p->sid = hash_by_name(xml_attr(x, "algorithmName"));
    cpy(p->hash, sizeof p->hash, v);
    cpy(p->salt, sizeof p->salt, xml_attr(x, "saltValue"));
    p->spin = (v = xml_attr(x, "spinCount")) ? (u32)strtoul(v, NULL, 10) : 0;
  } else {
    p->iso = 0;
    p->sid = (v = xml_attr(x, "cryptAlgorithmSid")) ? atoi(v) : 4;
    if ((v = xml_attr(x, "cryptAlgorithmClass")) && strcmp(v, "hash")) p->sid = -1;
    if (xml_attr(x, "algIdExt")) p->sid = -1;
    cpy(p->hash, sizeof p->hash, xml_attr(x, "hash"));
    cpy(p->salt, sizeof p->salt, xml_attr(x, "salt"));
    cpy(p->provider, sizeof p->provider, xml_attr(x, "cryptProviderType"));
    p->spin = (v = xml_attr(x, "cryptSpinCount")) ? (u32)strtoul(v, NULL, 10) : 0;
  }
}
void prot_write_setting(Doc *d, Buf *b)
{
  static const char *const kinds[] = { "none", "readOnly", "comments", "trackedChanges", "forms" };
  const DocProtect *p = prot_peek(d);
  if (!p || (p->kind == PR_NONE && !p->enforced && !p->hash[0])) return;
  buf_printf(b, "<w:documentProtection w:edit=\"%s\"", kinds[p->kind <= PR_FORMS ? p->kind : 0]);
  if (p->formatting) buf_adds(b, " w:formatting=\"1\"");
  buf_printf(b, " w:enforcement=\"%d\"", p->enforced ? 1 : 0);
  if (p->hash[0]) {
    if (p->iso) {
      buf_printf(b, " w:algorithmName=\"%s\" w:hashValue=\"", p->sid == CH_SHA1 ? "SHA-1" : "SHA-512");
      xml_escape(b, p->hash, strlen(p->hash));
      buf_adds(b, "\" w:saltValue=\""); xml_escape(b, p->salt, strlen(p->salt));
      buf_printf(b, "\" w:spinCount=\"%lu\"", (unsigned long)p->spin);
    } else {
      buf_adds(b, " w:cryptProviderType=\"");
      xml_escape(b, p->provider[0] ? p->provider : "rsaAES", strlen(p->provider[0] ? p->provider : "rsaAES"));
      buf_printf(b, "\" w:cryptAlgorithmClass=\"hash\" w:cryptAlgorithmType=\"typeAny\" w:cryptAlgorithmSid=\"%d\" w:cryptSpinCount=\"%lu\" w:hash=\"",
                 p->sid, (unsigned long)p->spin);
      xml_escape(b, p->hash, strlen(p->hash));
      buf_adds(b, "\" w:salt=\""); xml_escape(b, p->salt, strlen(p->salt));
      buf_adds(b, "\"");
    }
  }
  buf_adds(b, "/>");
}
/* runs [a, b) of p get perm (an empty paragraph's one run too) */
static void para_perm(Para *p, u32 a, u32 b, int perm)
{
  u32 i, start = 0;
  if (!p->len) { p->r[0].f.perm = (u16)perm; return; }
  if (b > p->len) b = p->len;
  if (a >= b) return;
  /* room for two more runs (the splits at a and b) */
  if (p->nr + 2 > p->rcap) {
    Run *nr = (Run *)realloc(p->r, sizeof(Run) * (p->nr + 2));
    if (!nr) return;
    p->r = nr; p->rcap = p->nr + 2;
  }
  for (i = 0; i < p->nr; start = p->r[i].end, i++) {
    u32 e = p->r[i].end;
    if (e <= a || start >= b) continue;
    if (start < a) {                           /* split at a */
      memmove(p->r + i + 1, p->r + i, sizeof(Run) * (p->nr - i)); p->nr++;
      p->r[i].end = a;
      continue;
    }
    if (e > b) {                               /* split at b */
      memmove(p->r + i + 1, p->r + i, sizeof(Run) * (p->nr - i)); p->nr++;
      p->r[i].end = b;
    }
    p->r[i].f.perm = (u16)perm;
  }
  para_normalise(p);
}
int prot_read_perm(Doc *d, Xml *x, const void *pc, Para **out, u32 nout, Para *cur)
{
  DocProtect *p;
  const char *id = xml_attr(x, "id");
  int i;
  if (strcmp(x->name, "permStart") && strcmp(x->name, "permEnd")) return 0;
  if (!(p = prot_get(d))) return 1;
  if (x->name[4] == 'S') {
    const char *cf = xml_attr(x, "colFirst"), *cl = xml_attr(x, "colLast");
    int k = perm_add(d, xml_attr(x, "ed"), xml_attr(x, "edGrp"), cf ? atoi(cf) : -1, cl ? atoi(cl) : -1);
    if (k && p->nopen < 8) {
      p->open[p->nopen].pc = pc; p->open[p->nopen].para = nout; p->open[p->nopen].off = cur ? cur->len : 0;
      p->open[p->nopen].perm = k;
      cpy(p->open[p->nopen].id, sizeof p->open[0].id, id ? id : "");
      p->nopen++;
    }
    return 1;
  }
  for (i = 0; i < p->nopen; i++)
    if (p->open[i].pc == pc && !strcmp(p->open[i].id, id ? id : "")) {
      u32 k;
      for (k = p->open[i].para; k < nout; k++) para_perm(out[k], k == p->open[i].para ? p->open[i].off : 0, out[k]->len, p->open[i].perm);
      if (cur && p->open[i].para <= nout) para_perm(cur, p->open[i].para == nout ? p->open[i].off : 0, cur->len, p->open[i].perm);
      p->open[i] = p->open[--p->nopen];
      break;
    }
  return 1;
}
void prot_write_perm(Buf *o, const Doc *d, u16 *open, u16 perm)
{
  const DocProtect *p = prot_peek(d);
  if (!p || perm > p->nperms) perm = 0;
  if (perm == *open) return;
  if (*open) buf_printf(o, "<w:permEnd w:id=\"%u\"/>", (unsigned)*open);
  if (perm) {
    const Perm *e = &p->perms[perm - 1];
    buf_printf(o, "<w:permStart w:id=\"%u\"", (unsigned)perm);
    if (e->ed[0]) { buf_adds(o, " w:ed=\""); xml_escape(o, e->ed, strlen(e->ed)); buf_adds(o, "\""); }
    if (e->grp[0]) { buf_adds(o, " w:edGrp=\""); xml_escape(o, e->grp, strlen(e->grp)); buf_adds(o, "\""); }
    if (e->colfirst >= 0) buf_printf(o, " w:colFirst=\"%ld\"", (long)e->colfirst);
    if (e->collast >= 0) buf_printf(o, " w:colLast=\"%ld\"", (long)e->collast);
    buf_adds(o, "/>");
  }
  *open = perm;
}

/* ------------------------------------------------------------------ reading and writing whole files */
const char *(*prot_ask)(int attempt);
static char *load_pw;                    /* the password the file being read was decrypted with */
static EncKey load_key;
u8 *prot_load_decrypt(u8 *data, size_t *n, char *err, size_t errcap)
{
  u8 *plain;
  size_t pn;
  int rc;
  if (load_pw) { crypt_wipe(load_pw, strlen(load_pw)); free(load_pw); load_pw = NULL; }
  if (!enc_is_encrypted(data, *n)) return data;
  memset(&load_key, 0, sizeof load_key);
  rc = prot_open_encrypted(data, *n, prot_ask, &plain, &pn, &load_pw, &load_key);
  free(data);
  if (rc != ENC_OK) {
    snprintf(err, errcap, "%s", rc == PROT_CANCELLED ? "The document is encrypted: it can only be opened with its password." : enc_error(rc));
    return NULL;
  }
  *n = pn;
  return plain;
}
void prot_load_done(Doc *d)
{
  if (!load_pw) return;
  if (d) {
    prot_set_password(d, load_pw);
    if (d->prot) d->prot->key = load_key;
    d->dirty = 0;
  }
  crypt_wipe(load_pw, strlen(load_pw)); free(load_pw); load_pw = NULL;
  crypt_wipe(&load_key, sizeof load_key);
}
u8 *prot_save_encrypt(Doc *d, u8 *data, size_t *n)
{
  DocProtect *p = prot_peek(d);
  u8 *enc;
  size_t en;
  if (!p || !p->password) return data;
  enc = enc_encrypt(data, *n, p->password, &p->key, &en);
  crypt_wipe(data, *n);
  free(data);
  if (enc) *n = en;
  return enc;
}
