/*
 * encrypt.h - WP7: password-encrypted Office documents ([MS-OFFCRYPTO], ECMA-376 Part 2 encryption).
 * Such a file is an OLE compound file (core/cfb.h) holding an EncryptionInfo stream (how it is encrypted)
 * and an EncryptedPackage stream (the .docx zip, encrypted), with the \x06DataSpaces storage that names
 * the transform.
 *
 * Read: agile encryption (Office 2010 and later, LibreOffice: AES-128/192/256 in CBC with SHA-1 or SHA-512
 * key derivation, the data integrity HMAC checked) and standard encryption (Office 2007: AES-128/192/256 in
 * ECB with SHA-1). Written: agile, AES-256-CBC with SHA-512, 100000 spins, and the HMAC, as Word writes it.
 */
#ifndef QUILL_ENCRYPT_H
#define QUILL_ENCRYPT_H

#include "util.h"

/* the slow part of a password (spinCount hashes of it with a salt), kept so the document can be saved
   again without spending the time once more */
typedef struct EncKey {
  u8 valid;
  u8 salt[16];
  u8 hn[64];                /* SHA-512 of salt and password, rehashed spin times */
  u32 spin;
} EncKey;

enum { ENC_OK, ENC_NOTENC, ENC_BADPW, ENC_UNSUPPORTED, ENC_DAMAGED, ENC_NOMEM };
#define ENC_SPIN 100000UL                /* Word's spin count */

/* 1 if data is a compound file holding an encrypted package (an encrypted .docx, .xlsx ...) */
int  enc_is_encrypted(const u8 *data, size_t len);
/* the package decrypted with a password (UTF-8): ENC_OK and *out (malloc'd), *outlen; or why not. key (may be
   NULL) gets the password's derived key when the file used agile SHA-512 with a 16-byte salt */
int  enc_decrypt(const u8 *data, size_t len, const char *password, u8 **out, size_t *outlen, EncKey *key);
/* pkg encrypted with password (agile, AES-256, SHA-512) as a compound file (malloc'd), *outlen; NULL if out
   of memory. key (may be NULL): reused when valid (and filled when not), so a second save is quick */
u8  *enc_encrypt(const u8 *pkg, size_t len, const char *password, EncKey *key, size_t *outlen);
/* a message for people, in English, for an ENC_ code */
const char *enc_error(int code);

/* the password as Office hashes it: UTF-16LE (into out, at most cap bytes); its length in bytes */
size_t enc_utf16(const char *password, u8 *out, size_t cap);

#endif
