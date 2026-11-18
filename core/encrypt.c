/*
 * encrypt.c - encrypted Office documents read and written (see encrypt.h), from [MS-OFFCRYPTO] 2.3.4:
 * standard encryption (2.3.4.5-2.3.4.9) and agile encryption (2.3.4.10-2.3.4.15), and the data spaces
 * (2.1, 2.2) Word looks for.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "encrypt.h"
#include "crypt.h"
#include "cfb.h"
#include "xml.h"

static u32 le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static void put32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

const char *enc_error(int code)
{
  switch (code) {
  case ENC_BADPW: return "The password is not correct.";
  case ENC_UNSUPPORTED: return "This document is encrypted in a way Quill can't read.";
  case ENC_DAMAGED: return "The encrypted document is damaged.";
  case ENC_NOMEM: return "Not enough memory to open the file.";
  case ENC_NOTENC: return "The document is not encrypted.";
  }
  return "";
}

size_t enc_utf16(const char *pw, u8 *out, size_t cap)
{
  size_t i = 0, n = strlen(pw), o = 0;
  while (i < n) {
    int used;
    u32 ch = utf8_get(pw + i, n - i, &used);
    i += (size_t)used;
    if (ch >= 0x10000) {
      u32 v = ch - 0x10000, hi = 0xD800 + (v >> 10), lo = 0xDC00 + (v & 0x3FF);
      if (o + 4 > cap) break;
      out[o++] = (u8)hi; out[o++] = (u8)(hi >> 8); out[o++] = (u8)lo; out[o++] = (u8)(lo >> 8);
    } else {
      if (o + 2 > cap) break;
      out[o++] = (u8)ch; out[o++] = (u8)(ch >> 8);
    }
  }
  return o;
}

int enc_is_encrypted(const u8 *data, size_t len)
{
  Cfb c;
  int r;
  if (!cfb_is(data, len) || !cfb_open(&c, data, len)) return 0;
  r = cfb_find(&c, "EncryptionInfo") > 0 && cfb_find(&c, "EncryptedPackage") > 0;
  cfb_close(&c);
  return r;
}

/* H0 = H(salt + password), Hn = H(iterator + Hn-1) spin times */
static void spin_hash(int alg, const u8 *salt, size_t sl, const char *pw, u32 spin, u8 *out)
{
  u8 pw16[512], buf[4 + 64];
  size_t pl = enc_utf16(pw, pw16, sizeof pw16);
  int hl = hash_len(alg);
  u32 i;
  hash2(alg, salt, sl, pw16, pl, out);
  if (alg == CH_SHA512) { sha512_spin(out, spin, 0); spin = 0; }
  for (i = 0; i < spin; i++) {
    put32(buf, i);
    memcpy(buf + 4, out, (size_t)hl);
    hash2(alg, buf, 4 + (size_t)hl, NULL, 0, out);
  }
  crypt_wipe(pw16, sizeof pw16); crypt_wipe(buf, sizeof buf);
}
/* a key of kb bytes from Hn and a block key: H(Hn + block), cut short or padded with 0x36 */
static void agile_key(int alg, const u8 *hn, const u8 *block, size_t kb, u8 *key)
{
  u8 h[64];
  int hl = hash_len(alg);
  hash2(alg, hn, (size_t)hl, block, 8, h);
  if (kb <= (size_t)hl) memcpy(key, h, kb);
  else { memcpy(key, h, (size_t)hl); memset(key + hl, 0x36, kb - (size_t)hl); }
  crypt_wipe(h, sizeof h);
}
/* an IV of 16 bytes: H(salt + block) cut to the block size, or the salt itself (block NULL), padded with 0x36 */
static void agile_iv(int alg, const u8 *salt, size_t sl, const u8 *block, size_t bl, u8 *iv)
{
  u8 h[64];
  size_t n;
  if (block) { hash2(alg, salt, sl, block, bl, h); n = (size_t)hash_len(alg); }
  else { memcpy(h, salt, sl > 64 ? 64 : sl); n = sl; }
  if (n >= 16) memcpy(iv, h, 16);
  else { memcpy(iv, h, n); memset(iv + n, 0x36, 16 - n); }
}
static const u8 BK_VIN[8] = { 0xfe, 0xa7, 0xd2, 0x76, 0x3b, 0x4b, 0x9e, 0x79 };
static const u8 BK_VVAL[8] = { 0xd7, 0xaa, 0x0f, 0x6d, 0x30, 0x61, 0x34, 0x4e };
static const u8 BK_KEY[8] = { 0x14, 0x6e, 0x0b, 0xe7, 0xab, 0xac, 0xd0, 0xd6 };
static const u8 BK_HMACKEY[8] = { 0x5f, 0xb2, 0xad, 0x01, 0x0c, 0xb9, 0xe1, 0xf6 };
static const u8 BK_HMACVAL[8] = { 0xa0, 0x67, 0x7f, 0x02, 0xb2, 0x2c, 0x84, 0x33 };

/* an agile cipher's settings from keyData or an encryptedKey element */
typedef struct {
  int alg, keybits, blocksize, hashsize;
  u8 salt[64]; long saltlen;
  u32 spin;
  u8 vin[64], vval[128], key[64]; long vinlen, vvallen, keylen;
  u8 hkey[128], hval[128]; long hkeylen, hvallen;
} Agile;
static long attr64(Xml *x, const char *name, u8 *out, size_t cap)
{
  const char *v = xml_attr(x, name);
  return v ? b64_get(v, strlen(v), out, cap) : -1;
}
static int read_cipher(Xml *x, Agile *a)
{
  const char *alg = xml_attr(x, "cipherAlgorithm"), *ch = xml_attr(x, "cipherChaining"), *v;
  a->alg = hash_by_name(xml_attr(x, "hashAlgorithm"));
  a->keybits = (v = xml_attr(x, "keyBits")) ? atoi(v) : 0;
  a->blocksize = (v = xml_attr(x, "blockSize")) ? atoi(v) : 16;
  a->hashsize = (v = xml_attr(x, "hashSize")) ? atoi(v) : 0;
  a->spin = (v = xml_attr(x, "spinCount")) ? (u32)strtoul(v, NULL, 10) : 0;
  a->saltlen = attr64(x, "saltValue", a->salt, sizeof a->salt);
  if (!alg || strcmp(alg, "AES") || !ch || strcmp(ch, "ChainingModeCBC") || a->alg < 0 || a->blocksize != 16 || a->saltlen <= 0 ||
      (a->keybits != 128 && a->keybits != 192 && a->keybits != 256))
    return 0;
  if (a->hashsize != hash_len(a->alg)) a->hashsize = hash_len(a->alg);
  return 1;
}
static int decrypt_agile(const char *xml, size_t xl, const u8 *pkg, size_t pl, const char *pw, u8 **out, size_t *outlen, EncKey *keep)
{
  Xml x;
  Agile kd, pk;
  int got_kd = 0, got_pk = 0, inpass = 0, rc = ENC_DAMAGED;
  u8 hn[64], k[32], iv[16], vin[64], vval[128], secret[64], hk[128], hv[128], mac[64];
  Aes aes;
  u32 size, seg;
  u8 *data;
  memset(&kd, 0, sizeof kd); memset(&pk, 0, sizeof pk);
  xml_init(&x, xml, xl);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_END && !strcmp(x.name, "keyEncryptor")) inpass = 0;
    if (x.type != XT_START) continue;
    if (!strcmp(x.name, "keyData")) { if (!read_cipher(&x, &kd)) { xml_free(&x); return ENC_UNSUPPORTED; } got_kd = 1; }
    else if (!strcmp(x.name, "dataIntegrity")) {
      kd.hkeylen = attr64(&x, "encryptedHmacKey", kd.hkey, sizeof kd.hkey);
      kd.hvallen = attr64(&x, "encryptedHmacValue", kd.hval, sizeof kd.hval);
    }
    else if (!strcmp(x.name, "keyEncryptor")) {
      const char *u = xml_attr(&x, "uri");
      inpass = u && strstr(u, "keyEncryptor/password") != NULL;
    }
    else if (!strcmp(x.name, "encryptedKey") && inpass && !got_pk) {
      if (!read_cipher(&x, &pk)) { xml_free(&x); return ENC_UNSUPPORTED; }
      pk.vinlen = attr64(&x, "encryptedVerifierHashInput", pk.vin, sizeof pk.vin);
      pk.vvallen = attr64(&x, "encryptedVerifierHashValue", pk.vval, sizeof pk.vval);
      pk.keylen = attr64(&x, "encryptedKeyValue", pk.key, sizeof pk.key);
      got_pk = 1;
    }
  }
  xml_free(&x);
  if (!got_kd || !got_pk) return got_kd ? ENC_UNSUPPORTED : ENC_DAMAGED;     /* (a certificate only) */
  if (pk.vinlen < 16 || pk.vinlen % 16 || pk.vvallen < pk.hashsize || pk.vvallen % 16 || pk.keylen < pk.keybits / 8 || pk.keylen % 16)
    return ENC_DAMAGED;
  if (pk.spin > 10000000UL) return ENC_UNSUPPORTED;
  /* the password: its hash, then the verifier */
  if (keep && keep->valid && keep->spin == pk.spin && pk.alg == CH_SHA512 && pk.saltlen == 16 && !memcmp(keep->salt, pk.salt, 16))
    memcpy(hn, keep->hn, 64);                 /* (asked twice for the same file: no need to spin again) */
  else spin_hash(pk.alg, pk.salt, (size_t)pk.saltlen, pw, pk.spin, hn);
  agile_iv(pk.alg, pk.salt, (size_t)pk.saltlen, NULL, 0, iv);
  agile_key(pk.alg, hn, BK_VIN, (size_t)pk.keybits / 8, k);
  aes_init(&aes, k, pk.keybits);
  memcpy(vin, pk.vin, (size_t)pk.vinlen);
  aes_cbc_decrypt(&aes, iv, vin, (size_t)pk.vinlen);
  agile_key(pk.alg, hn, BK_VVAL, (size_t)pk.keybits / 8, k);
  aes_init(&aes, k, pk.keybits);
  memcpy(vval, pk.vval, (size_t)pk.vvallen);
  aes_cbc_decrypt(&aes, iv, vval, (size_t)pk.vvallen);
  {
    u8 h[64];
    hash2(pk.alg, vin, (size_t)(pk.saltlen < pk.vinlen ? pk.saltlen : pk.vinlen), NULL, 0, h);
    if (memcmp(h, vval, (size_t)pk.hashsize)) { rc = ENC_BADPW; goto done; }
  }
  agile_key(pk.alg, hn, BK_KEY, (size_t)pk.keybits / 8, k);
  aes_init(&aes, k, pk.keybits);
  memcpy(secret, pk.key, (size_t)pk.keylen);
  aes_cbc_decrypt(&aes, iv, secret, (size_t)pk.keylen);
  if (keep) {
    keep->valid = pk.alg == CH_SHA512 && pk.saltlen == 16;
    if (keep->valid) { memcpy(keep->salt, pk.salt, 16); memcpy(keep->hn, hn, 64); keep->spin = pk.spin; }
  }
  /* the data: 4096-byte segments, each with its own IV */
  if (pl < 8 || le32(pkg + 4) != 0) goto done;
  size = le32(pkg);
  if (size > pl - 8 || (pl - 8) % 16) goto done;
  if (!aes_init(&aes, secret, kd.keybits) || kd.keybits > pk.keylen * 8) goto done;
  /* the integrity check (HMAC of the whole stream) when there is one */
  if (kd.hkeylen >= kd.hashsize && kd.hvallen >= kd.hashsize && kd.hkeylen % 16 == 0 && kd.hvallen % 16 == 0) {
    agile_iv(kd.alg, kd.salt, (size_t)kd.saltlen, BK_HMACKEY, 8, iv);
    memcpy(hk, kd.hkey, (size_t)kd.hkeylen);
    aes_cbc_decrypt(&aes, iv, hk, (size_t)kd.hkeylen);
    agile_iv(kd.alg, kd.salt, (size_t)kd.saltlen, BK_HMACVAL, 8, iv);
    memcpy(hv, kd.hval, (size_t)kd.hvallen);
    aes_cbc_decrypt(&aes, iv, hv, (size_t)kd.hvallen);
    hmac(kd.alg, hk, (size_t)kd.hashsize, pkg, pl, mac);
    if (memcmp(mac, hv, (size_t)kd.hashsize)) goto done;
  }
  if (!(data = (u8 *)malloc(pl - 8 + 1))) { rc = ENC_NOMEM; goto done; }
  memcpy(data, pkg + 8, pl - 8);
  for (seg = 0; (size_t)seg * 4096 < pl - 8; seg++) {
    u8 blk[4];
    size_t off = (size_t)seg * 4096, n = pl - 8 - off;
    if (n > 4096) n = 4096;
    put32(blk, seg);
    agile_iv(kd.alg, kd.salt, (size_t)kd.saltlen, blk, 4, iv);
    aes_cbc_decrypt(&aes, iv, data + off, n);
  }
  *out = data; *outlen = size;
  rc = ENC_OK;
done:
  crypt_wipe(hn, sizeof hn); crypt_wipe(k, sizeof k); crypt_wipe(secret, sizeof secret); crypt_wipe(&aes, sizeof aes);
  crypt_wipe(hk, sizeof hk);
  return rc;
}
/* standard encryption (Office 2007): AES in ECB, SHA-1, 50000 spins, the key from CryptDeriveKey */
static int decrypt_standard(const u8 *info, size_t il, const u8 *pkg, size_t pl, const char *pw, u8 **out, size_t *outlen)
{
  u32 flags, hsize, alg, halg, kbits, saltsz, vhsize, i;
  const u8 *h, *v;
  u8 hn[20], hf[20], buf[64], x1[20], x2[20], key[40], ver[16], vh[32], chk[20];
  Aes aes;
  u32 size;
  u8 *data;
  int rc = ENC_DAMAGED;
  if (il < 12) return ENC_DAMAGED;
  flags = le32(info + 4); hsize = le32(info + 8);
  if (!(flags & 0x20)) return ENC_UNSUPPORTED;            /* (RC4 CryptoAPI: not an OOXML package's) */
  if (hsize < 32 || 12 + (size_t)hsize + 4 + 16 + 16 + 4 + 32 > il) return ENC_DAMAGED;
  h = info + 12;
  alg = le32(h + 8); halg = le32(h + 12); kbits = le32(h + 16);
  if ((alg != 0x660E && alg != 0x660F && alg != 0x6610) || (halg != 0x8004 && halg != 0)) return ENC_UNSUPPORTED;
  if (kbits != 128 && kbits != 192 && kbits != 256) return ENC_UNSUPPORTED;
  v = h + hsize;
  saltsz = le32(v);
  if (saltsz != 16) return ENC_DAMAGED;
  vhsize = le32(v + 4 + 16 + 16);
  spin_hash(CH_SHA1, v + 4, 16, pw, 50000, hn);
  memset(buf, 0, 4);
  hash2(CH_SHA1, hn, 20, buf, 4, hf);                     /* block 0 */
  memset(buf, 0x36, 64); for (i = 0; i < 20; i++) buf[i] ^= hf[i];
  sha1(buf, 64, x1);
  memset(buf, 0x5C, 64); for (i = 0; i < 20; i++) buf[i] ^= hf[i];
  sha1(buf, 64, x2);
  memcpy(key, x1, 20); memcpy(key + 20, x2, 20);
  aes_init(&aes, key, (int)kbits);
  memcpy(ver, v + 4 + 16, 16);
  aes_cbc_decrypt(&aes, NULL, ver, 16);
  memcpy(vh, v + 4 + 16 + 16 + 4, 32);
  aes_cbc_decrypt(&aes, NULL, vh, 32);
  sha1(ver, 16, chk);
  if (vhsize > 20 || memcmp(chk, vh, vhsize ? vhsize : 20)) { rc = ENC_BADPW; goto done; }
  if (pl < 8 || le32(pkg + 4) != 0) goto done;
  size = le32(pkg);
  if (size > pl - 8) goto done;
  if (!(data = (u8 *)malloc(pl - 8 + 1))) { rc = ENC_NOMEM; goto done; }
  memcpy(data, pkg + 8, pl - 8);
  aes_cbc_decrypt(&aes, NULL, data, (pl - 8) & ~(size_t)15);
  *out = data; *outlen = size;
  rc = ENC_OK;
done:
  crypt_wipe(hn, sizeof hn); crypt_wipe(hf, sizeof hf); crypt_wipe(key, sizeof key); crypt_wipe(buf, sizeof buf);
  crypt_wipe(x1, sizeof x1); crypt_wipe(x2, sizeof x2); crypt_wipe(&aes, sizeof aes);
  return rc;
}
int enc_decrypt(const u8 *data, size_t len, const char *pw, u8 **out, size_t *outlen, EncKey *key)
{
  Cfb c;
  u8 *info, *pkg;
  size_t il, pl;
  int rc;
  u32 vmaj, vmin;
  *out = NULL; *outlen = 0;
  if (!cfb_is(data, len)) return ENC_NOTENC;
  if (!cfb_open(&c, data, len)) return ENC_DAMAGED;
  info = cfb_read_path(&c, "EncryptionInfo", &il);
  pkg = cfb_read_path(&c, "EncryptedPackage", &pl);
  cfb_close(&c);
  if (!info || !pkg) { free(info); free(pkg); return info || pkg ? ENC_DAMAGED : ENC_NOTENC; }
  if (il < 8) rc = ENC_DAMAGED;
  else {
    vmaj = info[0] | info[1] << 8; vmin = info[2] | info[3] << 8;
    if (vmaj == 4 && vmin == 4) rc = decrypt_agile((const char *)info + 8, il - 8, pkg, pl, pw, out, outlen, key);
    else if ((vmaj == 3 || vmaj == 4) && vmin == 2) rc = decrypt_standard(info, il, pkg, pl, pw, out, outlen);
    else rc = ENC_UNSUPPORTED;
  }
  free(info); free(pkg);
  return rc;
}

/* ------------------------------------------------------------------ writing */
/* a length-prefixed UTF-16 string padded to 4 bytes ([MS-OFFCRYPTO] UNICODE-LP-P4) */
static void lp4(Buf *b, const char *s)
{
  size_t n = strlen(s), i;
  u8 l[4];
  put32(l, (u32)(2 * n));
  buf_add(b, l, 4);
  for (i = 0; i < n; i++) { buf_addc(b, s[i]); buf_addc(b, 0); }
  if ((2 * n) % 4) { buf_addc(b, 0); buf_addc(b, 0); }
}
static void u32out(Buf *b, u32 v) { u8 l[4]; put32(l, v); buf_add(b, l, 4); }
/* the data spaces: the encrypted package is transformed by the strong encryption transform */
static int add_dataspaces(CfbWriter *w)
{
  Buf b;
  int ok = 1;
  size_t m;
  buf_init(&b);
  lp4(&b, "Microsoft.Container.DataSpaces");
  u32out(&b, 1); u32out(&b, 1); u32out(&b, 1);           /* reader, updater, writer: 1.0 */
  ok &= !b.oom && cfb_add(w, "\x06" "DataSpaces/Version", b.data, b.len);
  b.len = 0;
  u32out(&b, 8); u32out(&b, 1);                          /* header length, one entry */
  m = b.len;
  u32out(&b, 0);                                         /* (its length, below) */
  u32out(&b, 1); u32out(&b, 0); lp4(&b, "EncryptedPackage");
  lp4(&b, "StrongEncryptionDataSpace");
  if (!b.oom) put32((u8 *)b.data + m, (u32)(b.len - m));
  ok &= !b.oom && cfb_add(w, "\x06" "DataSpaces/DataSpaceMap", b.data, b.len);
  b.len = 0;
  u32out(&b, 8); u32out(&b, 1); lp4(&b, "StrongEncryptionTransform");
  ok &= !b.oom && cfb_add(w, "\x06" "DataSpaces/DataSpaceInfo/StrongEncryptionDataSpace", b.data, b.len);
  b.len = 0;
  u32out(&b, 0x58); u32out(&b, 1);                       /* the transform header's length up to its name; type 1 */
  lp4(&b, "{FF9A3F03-56EF-4613-BDD5-5A41C1D07246}");
  lp4(&b, "Microsoft.Container.EncryptionTransform");
  u32out(&b, 1); u32out(&b, 1); u32out(&b, 1);
  u32out(&b, 0); u32out(&b, 0); u32out(&b, 0); u32out(&b, 4);   /* no name, block size, cipher mode, reserved */
  ok &= !b.oom && cfb_add(w, "\x06" "DataSpaces/TransformInfo/StrongEncryptionTransform/\x06" "Primary", b.data, b.len);
  buf_free(&b);
  return ok;
}
static void attr_b64(Buf *b, const char *name, const u8 *d, size_t n)
{
  char t[200];
  b64_put(d, n, t, sizeof t);
  buf_printf(b, " %s=\"%s\"", name, t);
}
u8 *enc_encrypt(const u8 *pkg, size_t len, const char *pw, EncKey *keep, size_t *outlen)
{
  EncKey local;
  u8 secret[32], kdsalt[16], vin[16], vval[64], ekey[32], hkey[64], hval[64], iv[16], k[32];
  u8 *ep = NULL, *res = NULL;
  size_t epl, seg;
  Aes aes, sec;
  Buf x;
  CfbWriter *w = NULL;
  if (!keep) { memset(&local, 0, sizeof local); keep = &local; }
  if (!keep->valid) {
    crypt_random(keep->salt, 16);
    keep->spin = ENC_SPIN;
    spin_hash(CH_SHA512, keep->salt, 16, pw, keep->spin, keep->hn);
    keep->valid = 1;
  }
  crypt_random(secret, 32); crypt_random(kdsalt, 16); crypt_random(vin, 16); crypt_random(hkey, 64);
  /* the package: its size, then 4096-byte segments padded to the block size */
  epl = 8 + ((len + 15) & ~(size_t)15);
  if (!(ep = (u8 *)calloc(epl ? epl : 1, 1))) return NULL;
  put32(ep, (u32)len);
  memcpy(ep + 8, pkg, len);
  aes_init(&sec, secret, 256);
  for (seg = 0; seg * 4096 < epl - 8; seg++) {
    u8 blk[4];
    size_t n = epl - 8 - seg * 4096;
    if (n > 4096) n = 4096;
    put32(blk, (u32)seg);
    agile_iv(CH_SHA512, kdsalt, 16, blk, 4, iv);
    aes_cbc_encrypt(&sec, iv, ep + 8 + seg * 4096, n);
  }
  /* the integrity check */
  hmac(CH_SHA512, hkey, 64, ep, epl, hval);
  agile_iv(CH_SHA512, kdsalt, 16, BK_HMACKEY, 8, iv);
  aes_cbc_encrypt(&sec, iv, hkey, 64);
  agile_iv(CH_SHA512, kdsalt, 16, BK_HMACVAL, 8, iv);
  aes_cbc_encrypt(&sec, iv, hval, 64);
  /* the password's verifier and the secret key, under keys from the password */
  sha512(vin, 16, vval);
  agile_iv(CH_SHA512, keep->salt, 16, NULL, 0, iv);
  agile_key(CH_SHA512, keep->hn, BK_VIN, 32, k); aes_init(&aes, k, 256); aes_cbc_encrypt(&aes, iv, vin, 16);
  agile_key(CH_SHA512, keep->hn, BK_VVAL, 32, k); aes_init(&aes, k, 256); aes_cbc_encrypt(&aes, iv, vval, 64);
  memcpy(ekey, secret, 32);
  agile_key(CH_SHA512, keep->hn, BK_KEY, 32, k); aes_init(&aes, k, 256); aes_cbc_encrypt(&aes, iv, ekey, 32);
  /* EncryptionInfo: version 4.4, flags 0x40, the XML */
  buf_init(&x);
  { u8 hdr[8] = { 4, 0, 4, 0, 0x40, 0, 0, 0 }; buf_add(&x, hdr, 8); }
  buf_adds(&x, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
               "<encryption xmlns=\"http://schemas.microsoft.com/office/2006/encryption\""
               " xmlns:p=\"http://schemas.microsoft.com/office/2006/keyEncryptor/password\""
               " xmlns:c=\"http://schemas.microsoft.com/office/2006/keyEncryptor/certificate\">"
               "<keyData saltSize=\"16\" blockSize=\"16\" keyBits=\"256\" hashSize=\"64\" cipherAlgorithm=\"AES\""
               " cipherChaining=\"ChainingModeCBC\" hashAlgorithm=\"SHA512\"");
  attr_b64(&x, "saltValue", kdsalt, 16);
  buf_adds(&x, "/><dataIntegrity");
  attr_b64(&x, "encryptedHmacKey", hkey, 64);
  attr_b64(&x, "encryptedHmacValue", hval, 64);
  buf_adds(&x, "/><keyEncryptors><keyEncryptor uri=\"http://schemas.microsoft.com/office/2006/keyEncryptor/password\">");
  buf_printf(&x, "<p:encryptedKey spinCount=\"%lu\" saltSize=\"16\" blockSize=\"16\" keyBits=\"256\" hashSize=\"64\""
                 " cipherAlgorithm=\"AES\" cipherChaining=\"ChainingModeCBC\" hashAlgorithm=\"SHA512\"", (unsigned long)keep->spin);
  attr_b64(&x, "saltValue", keep->salt, 16);
  attr_b64(&x, "encryptedVerifierHashInput", vin, 16);
  attr_b64(&x, "encryptedVerifierHashValue", vval, 64);
  attr_b64(&x, "encryptedKeyValue", ekey, 32);
  buf_adds(&x, "/></keyEncryptor></keyEncryptors></encryption>");
  if (!x.oom && (w = cfb_writer())) {
    if (add_dataspaces(w) && cfb_add(w, "EncryptionInfo", x.data, x.len) && cfb_add(w, "EncryptedPackage", ep, epl))
      res = cfb_finish(w, outlen);
    else cfb_writer_free(w);
  }
  buf_free(&x);
  free(ep);
  crypt_wipe(secret, sizeof secret); crypt_wipe(k, sizeof k); crypt_wipe(&aes, sizeof aes); crypt_wipe(&sec, sizeof sec);
  crypt_wipe(&local, sizeof local);
  return res;
}
