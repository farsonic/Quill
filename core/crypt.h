/*
 * crypt.h - WP7 (docs/wordcraft-gap.md): the cryptography Office documents need, in plain C99 with
 * 32-bit arithmetic only (a 68040 has no 64-bit integer unit, so SHA-512 works on pairs of 32-bit halves).
 *
 *  - SHA-1 (FIPS 180-4), for Office 2007's standard encryption and Word's legacy protection hashes;
 *  - SHA-512 (FIPS 180-4), for agile encryption and Word 2010's documentProtection hashes;
 *  - HMAC (RFC 2104) with either;
 *  - AES (FIPS 197) with 128, 192 and 256-bit keys, one block at a time, and CBC and ECB over a buffer;
 *  - random bytes for salts and keys: a SHA-512 based generator stirred with whatever the caller can
 *    give (crypt_stir: times, clocks, mouse positions) on top of the C library's clock.
 *
 * These are for interchange with Office's file formats, not a general cryptographic library: there is
 * no constant-time guarantee against someone timing the machine as it works.
 */
#ifndef QUILL_CRYPT_H
#define QUILL_CRYPT_H

#include "util.h"

/* ---- hashes */
typedef struct { u32 h[5]; u32 nlo, nhi; u8 buf[64]; u32 n; } Sha1;
void sha1_init(Sha1 *c);
void sha1_update(Sha1 *c, const void *data, size_t len);
void sha1_final(Sha1 *c, u8 out[20]);
void sha1(const void *data, size_t len, u8 out[20]);

typedef struct { u32 h[16]; u32 n[2]; u8 buf[128]; u32 bn; } Sha512;   /* h: 8 words as hi, lo pairs */
void sha512_init(Sha512 *c);
void sha512_update(Sha512 *c, const void *data, size_t len);
void sha512_final(Sha512 *c, u8 out[64]);
void sha512(const void *data, size_t len, u8 out[64]);

/* a hash by its algorithm (CH_), so the Office code can work with whichever a file names */
enum { CH_SHA1, CH_SHA512 };
typedef struct { int alg; union { Sha1 s1; Sha512 s512; } u; } Hash;
int  hash_len(int alg);                          /* 20 or 64 */
void hash_init(Hash *h, int alg);
void hash_update(Hash *h, const void *data, size_t len);
void hash_final(Hash *h, u8 *out);               /* hash_len bytes */
/* the hash of a then b (either may be empty) */
void hash2(int alg, const void *a, size_t an, const void *b, size_t bn, u8 *out);
/* "SHA-512" / "SHA512" / "SHA-1" / "SHA1" (case ignored): CH_, or -1 if Quill hasn't it */
int  hash_by_name(const char *name);

/* HMAC with a hash (key of any length); out gets hash_len bytes */
void hmac(int alg, const u8 *key, size_t keylen, const void *data, size_t len, u8 *out);

/* ---- AES */
typedef struct { u32 ek[60], dk[60]; int rounds; } Aes;
int  aes_init(Aes *a, const u8 *key, int keybits);   /* 128, 192 or 256; 0 for another size */
void aes_encrypt(const Aes *a, const u8 in[16], u8 out[16]);
void aes_decrypt(const Aes *a, const u8 in[16], u8 out[16]);
/* n bytes (a multiple of 16) in place, chaining from iv (16 bytes; ECB when NULL) */
void aes_cbc_encrypt(const Aes *a, const u8 *iv, u8 *data, size_t n);
void aes_cbc_decrypt(const Aes *a, const u8 *iv, u8 *data, size_t n);

/* ---- random bytes */
void crypt_stir(const void *data, size_t len);   /* anything unpredictable the caller has */
void crypt_random(u8 *out, size_t n);

/* ---- base64 (RFC 4648), for the keys and salts in Office's XML */
/* out gets the text and a NUL (cap at least 4 * ((n + 2) / 3) + 1); its length */
size_t b64_put(const u8 *data, size_t n, char *out, size_t cap);
/* decoded into out (cap bytes), whitespace skipped; the number of bytes, -1 if bad or too long */
long   b64_get(const char *s, size_t n, u8 *out, size_t cap);

/* wipes memory (secrets: keys, passwords) so the compiler can't leave it out */
void crypt_wipe(void *p, size_t n);

#endif
