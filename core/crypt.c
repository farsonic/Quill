/*
 * crypt.c - SHA-1, SHA-512, HMAC, AES, random bytes and base64 for WP7 (see crypt.h). Written from
 * FIPS 180-4, FIPS 197 and RFC 2104; checked against their test vectors in tests/test_wp7.c.
 */
#include <string.h>
#include <time.h>
#include "crypt.h"

static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

void crypt_wipe(void *p, size_t n)
{
  volatile u8 *v = (volatile u8 *)p;
  while (n--) *v++ = 0;
}

/* ------------------------------------------------------------------ SHA-1 */
static void sha1_block(Sha1 *c, const u8 *p)
{
  u32 w[80], a, b, cc, d, e, t;
  int i;
  for (i = 0; i < 16; i++) w[i] = be32(p + 4 * i);
  for (; i < 80; i++) { t = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]; w[i] = ROL(t, 1); }
  a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4];
  for (i = 0; i < 80; i++) {
    u32 f, k;
    if (i < 20) { f = (b & cc) | (~b & d); k = 0x5A827999UL; }
    else if (i < 40) { f = b ^ cc ^ d; k = 0x6ED9EBA1UL; }
    else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDCUL; }
    else { f = b ^ cc ^ d; k = 0xCA62C1D6UL; }
    t = ROL(a, 5) + f + e + k + w[i];
    e = d; d = cc; cc = ROL(b, 30); b = a; a = t;
  }
  c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}
void sha1_init(Sha1 *c)
{
  c->h[0] = 0x67452301UL; c->h[1] = 0xEFCDAB89UL; c->h[2] = 0x98BADCFEUL; c->h[3] = 0x10325476UL; c->h[4] = 0xC3D2E1F0UL;
  c->nlo = c->nhi = 0; c->n = 0;
}
void sha1_update(Sha1 *c, const void *data, size_t len)
{
  const u8 *p = (const u8 *)data;
  u32 old = c->nlo;
  c->nlo += (u32)len << 3;
  if (c->nlo < old) c->nhi++;
  c->nhi += (u32)((len >> 16) >> 13);
  while (len) {
    size_t k = 64 - c->n;
    if (k > len) k = len;
    memcpy(c->buf + c->n, p, k);
    c->n += (u32)k; p += k; len -= k;
    if (c->n == 64) { sha1_block(c, c->buf); c->n = 0; }
  }
}
void sha1_final(Sha1 *c, u8 out[20])
{
  u8 len[8];
  int i;
  put32(len, c->nhi); put32(len + 4, c->nlo);
  c->buf[c->n++] = 0x80;
  if (c->n > 56) { memset(c->buf + c->n, 0, 64 - c->n); sha1_block(c, c->buf); c->n = 0; }
  memset(c->buf + c->n, 0, 56 - c->n);
  memcpy(c->buf + 56, len, 8);
  sha1_block(c, c->buf);
  for (i = 0; i < 5; i++) put32(out + 4 * i, c->h[i]);
  crypt_wipe(c, sizeof *c);
}
void sha1(const void *data, size_t len, u8 out[20]) { Sha1 c; sha1_init(&c); sha1_update(&c, data, len); sha1_final(&c, out); }

/* ------------------------------------------------------------------ SHA-512, in 32-bit halves */
static const u32 K512[160] = {
  0x428a2f98UL,0xd728ae22UL, 0x71374491UL,0x23ef65cdUL, 0xb5c0fbcfUL,0xec4d3b2fUL, 0xe9b5dba5UL,0x8189dbbcUL,
  0x3956c25bUL,0xf348b538UL, 0x59f111f1UL,0xb605d019UL, 0x923f82a4UL,0xaf194f9bUL, 0xab1c5ed5UL,0xda6d8118UL,
  0xd807aa98UL,0xa3030242UL, 0x12835b01UL,0x45706fbeUL, 0x243185beUL,0x4ee4b28cUL, 0x550c7dc3UL,0xd5ffb4e2UL,
  0x72be5d74UL,0xf27b896fUL, 0x80deb1feUL,0x3b1696b1UL, 0x9bdc06a7UL,0x25c71235UL, 0xc19bf174UL,0xcf692694UL,
  0xe49b69c1UL,0x9ef14ad2UL, 0xefbe4786UL,0x384f25e3UL, 0x0fc19dc6UL,0x8b8cd5b5UL, 0x240ca1ccUL,0x77ac9c65UL,
  0x2de92c6fUL,0x592b0275UL, 0x4a7484aaUL,0x6ea6e483UL, 0x5cb0a9dcUL,0xbd41fbd4UL, 0x76f988daUL,0x831153b5UL,
  0x983e5152UL,0xee66dfabUL, 0xa831c66dUL,0x2db43210UL, 0xb00327c8UL,0x98fb213fUL, 0xbf597fc7UL,0xbeef0ee4UL,
  0xc6e00bf3UL,0x3da88fc2UL, 0xd5a79147UL,0x930aa725UL, 0x06ca6351UL,0xe003826fUL, 0x14292967UL,0x0a0e6e70UL,
  0x27b70a85UL,0x46d22ffcUL, 0x2e1b2138UL,0x5c26c926UL, 0x4d2c6dfcUL,0x5ac42aedUL, 0x53380d13UL,0x9d95b3dfUL,
  0x650a7354UL,0x8baf63deUL, 0x766a0abbUL,0x3c77b2a8UL, 0x81c2c92eUL,0x47edaee6UL, 0x92722c85UL,0x1482353bUL,
  0xa2bfe8a1UL,0x4cf10364UL, 0xa81a664bUL,0xbc423001UL, 0xc24b8b70UL,0xd0f89791UL, 0xc76c51a3UL,0x0654be30UL,
  0xd192e819UL,0xd6ef5218UL, 0xd6990624UL,0x5565a910UL, 0xf40e3585UL,0x5771202aUL, 0x106aa070UL,0x32bbd1b8UL,
  0x19a4c116UL,0xb8d2d0c8UL, 0x1e376c08UL,0x5141ab53UL, 0x2748774cUL,0xdf8eeb99UL, 0x34b0bcb5UL,0xe19b48a8UL,
  0x391c0cb3UL,0xc5c95a63UL, 0x4ed8aa4aUL,0xe3418acbUL, 0x5b9cca4fUL,0x7763e373UL, 0x682e6ff3UL,0xd6b2b8a3UL,
  0x748f82eeUL,0x5defb2fcUL, 0x78a5636fUL,0x43172f60UL, 0x84c87814UL,0xa1f0ab72UL, 0x8cc70208UL,0x1a6439ecUL,
  0x90befffaUL,0x23631e28UL, 0xa4506cebUL,0xde82bde9UL, 0xbef9a3f7UL,0xb2c67915UL, 0xc67178f2UL,0xe372532bUL,
  0xca273eceUL,0xea26619cUL, 0xd186b8c7UL,0x21c0c207UL, 0xeada7dd6UL,0xcde0eb1eUL, 0xf57d4f7fUL,0xee6ed178UL,
  0x06f067aaUL,0x72176fbaUL, 0x0a637dc5UL,0xa2c898a6UL, 0x113f9804UL,0xbef90daeUL, 0x1b710b35UL,0x131c471bUL,
  0x28db77f5UL,0x23047d84UL, 0x32caab7bUL,0x40c72493UL, 0x3c9ebe0aUL,0x15c9bebcUL, 0x431d67c4UL,0x9c100d4cUL,
  0x4cc5d4beUL,0xcb3e42b6UL, 0x597f299cUL,0xfc657e2aUL, 0x5fcb6fabUL,0x3ad6faecUL, 0x6c44198cUL,0x4a475817UL
};
/* rotations and shifts of (hi, lo) right by n: the results' halves */
#define RH(h, l, n) (((h) >> (n)) | ((l) << (32 - (n))))          /* (0 < n < 32) */
#define RL(h, l, n) (((l) >> (n)) | ((h) << (32 - (n))))
#define RH2(h, l, n) RL(h, l, (n) - 32)                            /* (32 < n < 64) */
#define RL2(h, l, n) RH(h, l, (n) - 32)
#define ADD(ah, al, bh, bl) do { u32 _l = (al) + (bl); (ah) += (bh) + (_l < (al)); (al) = _l; } while (0)
static void sha512_block(Sha512 *c, const u8 *p)
{
  u32 w[160], v[16];
  int i;
  for (i = 0; i < 32; i++) w[i] = be32(p + 4 * i);
  for (i = 16; i < 80; i++) {
    u32 h15 = w[2 * (i - 15)], l15 = w[2 * (i - 15) + 1], h2 = w[2 * (i - 2)], l2 = w[2 * (i - 2) + 1];
    /* s0 = rotr1 ^ rotr8 ^ shr7; s1 = rotr19 ^ rotr61 ^ shr6 */
    u32 s0h = RH(h15, l15, 1) ^ RH(h15, l15, 8) ^ (h15 >> 7);
    u32 s0l = RL(h15, l15, 1) ^ RL(h15, l15, 8) ^ ((l15 >> 7) | (h15 << 25));
    u32 s1h = RH(h2, l2, 19) ^ RH2(h2, l2, 61) ^ (h2 >> 6);
    u32 s1l = RL(h2, l2, 19) ^ RL2(h2, l2, 61) ^ ((l2 >> 6) | (h2 << 26));
    u32 th = w[2 * (i - 16)], tl = w[2 * (i - 16) + 1];
    ADD(th, tl, s0h, s0l);
    ADD(th, tl, w[2 * (i - 7)], w[2 * (i - 7) + 1]);
    ADD(th, tl, s1h, s1l);
    w[2 * i] = th; w[2 * i + 1] = tl;
  }
  memcpy(v, c->h, sizeof v);
  for (i = 0; i < 80; i++) {
    /* a = v0,1  b = 2,3  c = 4,5  d = 6,7  e = 8,9  f = 10,11  g = 12,13  h = 14,15 */
    u32 eh = v[8], el = v[9], ah = v[0], al = v[1];
    u32 S1h = RH(eh, el, 14) ^ RH(eh, el, 18) ^ RH2(eh, el, 41);
    u32 S1l = RL(eh, el, 14) ^ RL(eh, el, 18) ^ RL2(eh, el, 41);
    u32 chh = (eh & v[10]) ^ (~eh & v[12]), chl = (el & v[11]) ^ (~el & v[13]);
    u32 S0h = RH(ah, al, 28) ^ RH2(ah, al, 34) ^ RH2(ah, al, 39);
    u32 S0l = RL(ah, al, 28) ^ RL2(ah, al, 34) ^ RL2(ah, al, 39);
    u32 mjh = (ah & v[2]) ^ (ah & v[4]) ^ (v[2] & v[4]), mjl = (al & v[3]) ^ (al & v[5]) ^ (v[3] & v[5]);
    u32 t1h = v[14], t1l = v[15], t2h = S0h, t2l = S0l;
    ADD(t1h, t1l, S1h, S1l);
    ADD(t1h, t1l, chh, chl);
    ADD(t1h, t1l, K512[2 * i], K512[2 * i + 1]);
    ADD(t1h, t1l, w[2 * i], w[2 * i + 1]);
    ADD(t2h, t2l, mjh, mjl);
    memmove(v + 2, v, 14 * sizeof(u32));
    ADD(v[8], v[9], t1h, t1l);              /* e = d + t1 (d moved into 8, 9) */
    v[0] = t1h; v[1] = t1l;
    ADD(v[0], v[1], t2h, t2l);              /* a = t1 + t2 */
  }
  for (i = 0; i < 16; i += 2) ADD(c->h[i], c->h[i + 1], v[i], v[i + 1]);
}
void sha512_init(Sha512 *c)
{
  static const u32 iv[16] = {
    0x6a09e667UL,0xf3bcc908UL, 0xbb67ae85UL,0x84caa73bUL, 0x3c6ef372UL,0xfe94f82bUL, 0xa54ff53aUL,0x5f1d36f1UL,
    0x510e527fUL,0xade682d1UL, 0x9b05688cUL,0x2b3e6c1fUL, 0x1f83d9abUL,0xfb41bd6bUL, 0x5be0cd19UL,0x137e2179UL };
  memcpy(c->h, iv, sizeof iv);
  c->n[0] = c->n[1] = 0; c->bn = 0;
}
void sha512_update(Sha512 *c, const void *data, size_t len)
{
  const u8 *p = (const u8 *)data;
  u32 old = c->n[1];
  c->n[1] += (u32)len << 3;
  if (c->n[1] < old) c->n[0]++;
  c->n[0] += (u32)((len >> 16) >> 13);
  while (len) {
    size_t k = 128 - c->bn;
    if (k > len) k = len;
    memcpy(c->buf + c->bn, p, k);
    c->bn += (u32)k; p += k; len -= k;
    if (c->bn == 128) { sha512_block(c, c->buf); c->bn = 0; }
  }
}
void sha512_final(Sha512 *c, u8 out[64])
{
  int i;
  c->buf[c->bn++] = 0x80;
  if (c->bn > 112) { memset(c->buf + c->bn, 0, 128 - c->bn); sha512_block(c, c->buf); c->bn = 0; }
  memset(c->buf + c->bn, 0, 120 - c->bn);
  put32(c->buf + 120, c->n[0]); put32(c->buf + 124, c->n[1]);
  sha512_block(c, c->buf);
  for (i = 0; i < 16; i++) put32(out + 4 * i, c->h[i]);
  crypt_wipe(c, sizeof *c);
}
void sha512(const void *data, size_t len, u8 out[64]) { Sha512 c; sha512_init(&c); sha512_update(&c, data, len); sha512_final(&c, out); }

/* ------------------------------------------------------------------ by algorithm */
int hash_len(int alg) { return alg == CH_SHA1 ? 20 : 64; }
void hash_init(Hash *h, int alg) { h->alg = alg; if (alg == CH_SHA1) sha1_init(&h->u.s1); else sha512_init(&h->u.s512); }
void hash_update(Hash *h, const void *data, size_t len) { if (h->alg == CH_SHA1) sha1_update(&h->u.s1, data, len); else sha512_update(&h->u.s512, data, len); }
void hash_final(Hash *h, u8 *out) { if (h->alg == CH_SHA1) sha1_final(&h->u.s1, out); else sha512_final(&h->u.s512, out); }
void hash2(int alg, const void *a, size_t an, const void *b, size_t bn, u8 *out)
{
  Hash h;
  hash_init(&h, alg);
  if (an) hash_update(&h, a, an);
  if (bn) hash_update(&h, b, bn);
  hash_final(&h, out);
}
int hash_by_name(const char *name)
{
  char s[16];
  size_t i, k = 0;
  if (!name) return -1;
  for (i = 0; name[i] && k + 1 < sizeof s; i++) if (name[i] != '-') s[k++] = (char)(name[i] | 0x20);
  s[k] = 0;
  if (!strcmp(s, "sha512")) return CH_SHA512;
  if (!strcmp(s, "sha1")) return CH_SHA1;
  return -1;
}
void hmac(int alg, const u8 *key, size_t keylen, const void *data, size_t len, u8 *out)
{
  u8 k[128], pad[128], inner[64];
  size_t bs = alg == CH_SHA1 ? 64 : 128, i;
  int hl = hash_len(alg);
  Hash h;
  memset(k, 0, sizeof k);
  if (keylen > bs) hash2(alg, key, keylen, NULL, 0, k); else memcpy(k, key, keylen);
  for (i = 0; i < bs; i++) pad[i] = (u8)(k[i] ^ 0x36);
  hash_init(&h, alg); hash_update(&h, pad, bs); hash_update(&h, data, len); hash_final(&h, inner);
  for (i = 0; i < bs; i++) pad[i] = (u8)(k[i] ^ 0x5C);
  hash_init(&h, alg); hash_update(&h, pad, bs); hash_update(&h, inner, (size_t)hl); hash_final(&h, out);
  crypt_wipe(k, sizeof k); crypt_wipe(pad, sizeof pad); crypt_wipe(inner, sizeof inner);
}

/* ------------------------------------------------------------------ AES */
static u8 sbox[256], isbox[256];
static u32 Te[4][256], Td[4][256];
static int tables;
static u8 xt(u8 x) { return (u8)((x << 1) ^ ((x & 0x80) ? 0x1B : 0)); }
static u8 gmul(u8 a, u8 b)
{
  u8 r = 0;
  while (b) { if (b & 1) r ^= a; a = xt(a); b >>= 1; }
  return r;
}
#define ROR8(x) (((x) >> 8) | ((x) << 24))
/* the S-box from its definition (the inverse in GF(2^8) and the affine map), and the round tables */
static void aes_tables(void)
{
  int i;
  u8 p = 1, q = 1;
  if (tables) return;
  /* walk the multiplicative group with generator 3: p runs through all non-zero elements, q = 1/p */
  do {
    u8 x;
    p = (u8)(p ^ xt(p));                    /* p * 3 */
    q ^= (u8)(q << 1); q ^= (u8)(q << 2); q ^= (u8)(q << 4);   /* q / 3 */
    if (q & 0x80) q ^= 0x09;
    x = (u8)(q ^ (u8)((q << 1) | (q >> 7)) ^ (u8)((q << 2) | (q >> 6)) ^ (u8)((q << 3) | (q >> 5)) ^ (u8)((q << 4) | (q >> 4)));
    sbox[p] = (u8)(x ^ 0x63);
  } while (p != 1);
  sbox[0] = 0x63;
  for (i = 0; i < 256; i++) isbox[sbox[i]] = (u8)i;
  for (i = 0; i < 256; i++) {
    u8 s = sbox[i], si = isbox[i];
    u32 e = (u32)xt(s) << 24 | (u32)s << 16 | (u32)s << 8 | (u32)(xt(s) ^ s);
    u32 d = (u32)gmul(si, 14) << 24 | (u32)gmul(si, 9) << 16 | (u32)gmul(si, 13) << 8 | gmul(si, 11);
    Te[0][i] = e; Te[1][i] = ROR8(e); Te[2][i] = ROR8(Te[1][i]); Te[3][i] = ROR8(Te[2][i]);
    Td[0][i] = d; Td[1][i] = ROR8(d); Td[2][i] = ROR8(Td[1][i]); Td[3][i] = ROR8(Td[2][i]);
  }
  tables = 1;
}
static u32 subw(u32 w) { return (u32)sbox[w >> 24] << 24 | (u32)sbox[(w >> 16) & 255] << 16 | (u32)sbox[(w >> 8) & 255] << 8 | sbox[w & 255]; }
int aes_init(Aes *a, const u8 *key, int keybits)
{
  int nk = keybits / 32, n, i, r;
  u32 rcon = 1;
  if (keybits != 128 && keybits != 192 && keybits != 256) return 0;
  aes_tables();
  a->rounds = nk + 6;
  n = 4 * (a->rounds + 1);
  for (i = 0; i < nk; i++) a->ek[i] = be32(key + 4 * i);
  for (; i < n; i++) {
    u32 t = a->ek[i - 1];
    if (i % nk == 0) { t = subw((t << 8) | (t >> 24)) ^ (rcon << 24); rcon = xt((u8)rcon); }
    else if (nk > 6 && i % nk == 4) t = subw(t);
    a->ek[i] = a->ek[i - nk] ^ t;
  }
  /* the equivalent inverse cipher's keys: in reverse order, InvMixColumns on all but the first and last */
  for (r = 0; r <= a->rounds; r++)
    for (i = 0; i < 4; i++) {
      u32 w = a->ek[4 * (a->rounds - r) + i];
      if (r > 0 && r < a->rounds)
        w = Td[0][sbox[w >> 24]] ^ Td[1][sbox[(w >> 16) & 255]] ^ Td[2][sbox[(w >> 8) & 255]] ^ Td[3][sbox[w & 255]];
      a->dk[4 * r + i] = w;
    }
  return 1;
}
void aes_encrypt(const Aes *a, const u8 in[16], u8 out[16])
{
  const u32 *rk = a->ek;
  u32 s0 = be32(in) ^ rk[0], s1 = be32(in + 4) ^ rk[1], s2 = be32(in + 8) ^ rk[2], s3 = be32(in + 12) ^ rk[3], t0, t1, t2, t3;
  int r;
  for (r = 1; r < a->rounds; r++) {
    rk += 4;
    t0 = Te[0][s0 >> 24] ^ Te[1][(s1 >> 16) & 255] ^ Te[2][(s2 >> 8) & 255] ^ Te[3][s3 & 255] ^ rk[0];
    t1 = Te[0][s1 >> 24] ^ Te[1][(s2 >> 16) & 255] ^ Te[2][(s3 >> 8) & 255] ^ Te[3][s0 & 255] ^ rk[1];
    t2 = Te[0][s2 >> 24] ^ Te[1][(s3 >> 16) & 255] ^ Te[2][(s0 >> 8) & 255] ^ Te[3][s1 & 255] ^ rk[2];
    t3 = Te[0][s3 >> 24] ^ Te[1][(s0 >> 16) & 255] ^ Te[2][(s1 >> 8) & 255] ^ Te[3][s2 & 255] ^ rk[3];
    s0 = t0; s1 = t1; s2 = t2; s3 = t3;
  }
  rk += 4;
  put32(out,      ((u32)sbox[s0 >> 24] << 24 | (u32)sbox[(s1 >> 16) & 255] << 16 | (u32)sbox[(s2 >> 8) & 255] << 8 | sbox[s3 & 255]) ^ rk[0]);
  put32(out + 4,  ((u32)sbox[s1 >> 24] << 24 | (u32)sbox[(s2 >> 16) & 255] << 16 | (u32)sbox[(s3 >> 8) & 255] << 8 | sbox[s0 & 255]) ^ rk[1]);
  put32(out + 8,  ((u32)sbox[s2 >> 24] << 24 | (u32)sbox[(s3 >> 16) & 255] << 16 | (u32)sbox[(s0 >> 8) & 255] << 8 | sbox[s1 & 255]) ^ rk[2]);
  put32(out + 12, ((u32)sbox[s3 >> 24] << 24 | (u32)sbox[(s0 >> 16) & 255] << 16 | (u32)sbox[(s1 >> 8) & 255] << 8 | sbox[s2 & 255]) ^ rk[3]);
}
void aes_decrypt(const Aes *a, const u8 in[16], u8 out[16])
{
  const u32 *rk = a->dk;
  u32 s0 = be32(in) ^ rk[0], s1 = be32(in + 4) ^ rk[1], s2 = be32(in + 8) ^ rk[2], s3 = be32(in + 12) ^ rk[3], t0, t1, t2, t3;
  int r;
  for (r = 1; r < a->rounds; r++) {
    rk += 4;
    t0 = Td[0][s0 >> 24] ^ Td[1][(s3 >> 16) & 255] ^ Td[2][(s2 >> 8) & 255] ^ Td[3][s1 & 255] ^ rk[0];
    t1 = Td[0][s1 >> 24] ^ Td[1][(s0 >> 16) & 255] ^ Td[2][(s3 >> 8) & 255] ^ Td[3][s2 & 255] ^ rk[1];
    t2 = Td[0][s2 >> 24] ^ Td[1][(s1 >> 16) & 255] ^ Td[2][(s0 >> 8) & 255] ^ Td[3][s3 & 255] ^ rk[2];
    t3 = Td[0][s3 >> 24] ^ Td[1][(s2 >> 16) & 255] ^ Td[2][(s1 >> 8) & 255] ^ Td[3][s0 & 255] ^ rk[3];
    s0 = t0; s1 = t1; s2 = t2; s3 = t3;
  }
  rk += 4;
  put32(out,      ((u32)isbox[s0 >> 24] << 24 | (u32)isbox[(s3 >> 16) & 255] << 16 | (u32)isbox[(s2 >> 8) & 255] << 8 | isbox[s1 & 255]) ^ rk[0]);
  put32(out + 4,  ((u32)isbox[s1 >> 24] << 24 | (u32)isbox[(s0 >> 16) & 255] << 16 | (u32)isbox[(s3 >> 8) & 255] << 8 | isbox[s2 & 255]) ^ rk[1]);
  put32(out + 8,  ((u32)isbox[s2 >> 24] << 24 | (u32)isbox[(s1 >> 16) & 255] << 16 | (u32)isbox[(s0 >> 8) & 255] << 8 | isbox[s3 & 255]) ^ rk[2]);
  put32(out + 12, ((u32)isbox[s3 >> 24] << 24 | (u32)isbox[(s2 >> 16) & 255] << 16 | (u32)isbox[(s1 >> 8) & 255] << 8 | isbox[s0 & 255]) ^ rk[3]);
}
void aes_cbc_encrypt(const Aes *a, const u8 *iv, u8 *data, size_t n)
{
  u8 chain[16];
  size_t i, k;
  if (iv) memcpy(chain, iv, 16); else memset(chain, 0, 16);
  for (i = 0; i + 16 <= n; i += 16) {
    if (iv) for (k = 0; k < 16; k++) data[i + k] ^= chain[k];
    aes_encrypt(a, data + i, data + i);
    memcpy(chain, data + i, 16);
  }
}
void aes_cbc_decrypt(const Aes *a, const u8 *iv, u8 *data, size_t n)
{
  u8 chain[16], next[16];
  size_t i, k;
  if (iv) memcpy(chain, iv, 16); else memset(chain, 0, 16);
  for (i = 0; i + 16 <= n; i += 16) {
    memcpy(next, data + i, 16);
    aes_decrypt(a, data + i, data + i);
    if (iv) for (k = 0; k < 16; k++) data[i + k] ^= chain[k];
    memcpy(chain, next, 16);
  }
}

/* ------------------------------------------------------------------ random bytes */
static u8 pool[64];
static u32 pool_count;
static int pool_seeded;
void crypt_stir(const void *data, size_t len)
{
  Sha512 c;
  sha512_init(&c);
  sha512_update(&c, pool, sizeof pool);
  sha512_update(&c, data, len);
  sha512_final(&c, pool);
}
void crypt_random(u8 *out, size_t n)
{
  if (!pool_seeded) {
    struct { time_t t; clock_t c; const void *a, *b; } s;
    s.t = time(NULL); s.c = clock(); s.a = &s; s.b = (const void *)out;
    crypt_stir(&s, sizeof s);
    pool_seeded = 1;
  }
  while (n) {
    u8 blk[64], in[8];
    clock_t c = clock();
    size_t k = n < 32 ? n : 32;
    put32(in, (u32)c);
    put32(in + 4, ++pool_count);
    hash2(CH_SHA512, pool, sizeof pool, in, sizeof in, blk);
    memcpy(out, blk, k);                     /* half of each block goes out ... */
    memcpy(pool, blk + 32, 32);              /* ... the other half moves the pool on */
    out += k; n -= k;
    crypt_wipe(blk, sizeof blk);
  }
}

/* ------------------------------------------------------------------ base64 */
static const char b64c[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
size_t b64_put(const u8 *d, size_t n, char *out, size_t cap)
{
  size_t i, o = 0;
  if (cap < 4 * ((n + 2) / 3) + 1) { if (cap) out[0] = 0; return 0; }
  for (i = 0; i < n; i += 3) {
    u32 v = (u32)d[i] << 16 | (i + 1 < n ? (u32)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
    out[o++] = b64c[v >> 18]; out[o++] = b64c[(v >> 12) & 63];
    out[o++] = i + 1 < n ? b64c[(v >> 6) & 63] : '=';
    out[o++] = i + 2 < n ? b64c[v & 63] : '=';
  }
  out[o] = 0;
  return o;
}
long b64_get(const char *s, size_t n, u8 *out, size_t cap)
{
  u32 v = 0;
  int bits = 0;
  size_t i, o = 0;
  for (i = 0; i < n; i++) {
    const char *p;
    char ch = s[i];
    if (ch == '=' ) break;
    if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') continue;
    if (!ch || !(p = strchr(b64c, ch))) return -1;
    v = (v << 6) | (u32)(p - b64c); bits += 6;
    if (bits >= 8) { bits -= 8; if (o >= cap) return -1; out[o++] = (u8)(v >> bits); }
  }
  return (long)o;
}
