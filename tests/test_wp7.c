/* test_wp7.c - the native tests of WP7 Protection and encryption (docs/wordcraft-gap.md); this file belongs
   to that package. Checks with ok() (tests/testutil.h); coretest.c's main() calls test_wp7() */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "testutil.h"
#include "../core/crypt.h"
#include "../core/cfb.h"
#include "../core/encrypt.h"
#include "../core/protect.h"
#include "../core/zip.h"
#include "../core/review.h"

static int hexis(const u8 *b, int n, const char *hex)
{
  char s[300];
  int i;
  for (i = 0; i < n; i++) sprintf(s + 2 * i, "%02x", b[i]);
  if (strcmp(s, hex)) { printf("  got %s\n", s); return 0; }
  return 1;
}

/* FIPS 180-4, FIPS 197 and RFC 2202/4231 test vectors */
static void vectors(void)
{
  u8 o[64], k[32], pt[16], back[16];
  Aes a;
  int i;
  sha1("abc", 3, o);
  ok("crypt: SHA-1 abc", hexis(o, 20, "a9993e364706816aba3e25717850c26c9cd0d89d"));
  sha512("abc", 3, o);
  ok("crypt: SHA-512 abc", hexis(o, 64, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"));
  sha512("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", 112, o);
  ok("crypt: SHA-512 two blocks", hexis(o, 64, "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909"));
  {
    Sha512 c;
    static u8 mil[1000];
    memset(mil, 'a', sizeof mil);
    sha512_init(&c);
    for (i = 0; i < 1000; i++) sha512_update(&c, mil, sizeof mil);
    sha512_final(&c, o);
    ok("crypt: SHA-512 a million a's", hexis(o, 64, "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973ebde0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b"));
  }
  hmac(CH_SHA512, (const u8 *)"Jefe", 4, "what do ya want for nothing?", 28, o);
  ok("crypt: HMAC-SHA-512 (RFC 4231)", hexis(o, 64, "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737"));
  memset(k, 0xAA, 20);
  hmac(CH_SHA1, k, 20, "Hi There", 8, o);
  ok("crypt: HMAC-SHA-1", hexis(o, 20, "660a6b083d0838ca50abc154890ec9bffd96d8f0"));
  for (i = 0; i < 32; i++) k[i] = (u8)i;
  for (i = 0; i < 16; i++) pt[i] = (u8)(i * 0x11);
  aes_init(&a, k, 128); aes_encrypt(&a, pt, o); aes_decrypt(&a, o, back);
  ok("crypt: AES-128", hexis(o, 16, "69c4e0d86a7b0430d8cdb78070b4c55a") && !memcmp(back, pt, 16));
  aes_init(&a, k, 192); aes_encrypt(&a, pt, o); aes_decrypt(&a, o, back);
  ok("crypt: AES-192", hexis(o, 16, "dda97ca4864cdfe06eaf70a0ec0d7191") && !memcmp(back, pt, 16));
  aes_init(&a, k, 256); aes_encrypt(&a, pt, o); aes_decrypt(&a, o, back);
  ok("crypt: AES-256", hexis(o, 16, "8ea2b7ca516745bfeafc49904b496089") && !memcmp(back, pt, 16));
  {
    u8 buf[64], iv[16];
    char t[100];
    long n;
    for (i = 0; i < 64; i++) buf[i] = (u8)(i * 7);
    memset(iv, 3, 16);
    aes_cbc_encrypt(&a, iv, buf, 64);
    aes_cbc_decrypt(&a, iv, buf, 64);
    for (i = 0; i < 64 && buf[i] == (u8)(i * 7); i++) ;
    ok("crypt: CBC there and back", i == 64);
    b64_put((const u8 *)"hello!!", 7, t, sizeof t);
    n = b64_get(t, strlen(t), buf, sizeof buf);
    ok("crypt: base64", !strcmp(t, "aGVsbG8hIQ==") && n == 7 && !memcmp(buf, "hello!!", 7));
  }
  crypt_random(o, 32); crypt_random(k, 32);
  ok("crypt: random bytes differ", memcmp(o, k, 32) != 0);
}

/* a compound file written and read back: small streams (mini stream), large ones, storages in storages,
   and enough sectors to need DIFAT sectors past the header's 109 */
static void compound(void)
{
  CfbWriter *w = cfb_writer();
  u8 *big = (u8 *)malloc(8000000), *small = (u8 *)malloc(3000), *file, *got;
  size_t n, gl;
  Cfb c;
  int i, okall = 1;
  if (!big || !small || !w) { ok("cfb: memory", 0); free(big); free(small); cfb_writer_free(w); return; }
  for (i = 0; i < 8000000; i++) big[i] = (u8)(i * 31 + (i >> 9));
  for (i = 0; i < 3000; i++) small[i] = (u8)(i ^ 0x5A);
  for (i = 0; i < 40 && okall; i++) {
    char name[40];
    sprintf(name, "Store%d/Sub/s%02d", i % 3, i);
    okall = cfb_add(w, name, small, (size_t)(i * 70));
  }
  okall = okall && cfb_add(w, "WordDocument", big, 8000000) && cfb_add(w, "\x05SummaryInformation", small, 3000) && cfb_add(w, "Empty", "", 0);
  ok("cfb: streams added", okall);
  ok("cfb: a name twice refused", !cfb_add(w, "WordDocument", small, 10));
  file = cfb_finish(w, &n);
  ok("cfb: written", file != NULL && cfb_is(file, n));
  if (!file) { free(big); free(small); return; }
  ok("cfb: read", cfb_open(&c, file, n));
  got = cfb_read_path(&c, "worddocument", &gl);
  ok("cfb: the large stream back (DIFAT)", got && gl == 8000000 && !memcmp(got, big, gl));
  free(got);
  got = cfb_read_path(&c, "\x05SummaryInformation", &gl);
  ok("cfb: a mini stream back", got && gl == 3000 && !memcmp(got, small, gl));
  free(got);
  for (i = 0, okall = 1; i < 40; i++) {
    char name[40];
    sprintf(name, "Store%d/Sub/s%02d", i % 3, i);
    got = cfb_read_path(&c, name, &gl);
    if (!got || gl != (size_t)(i * 70) || memcmp(got, small, gl)) okall = 0;
    free(got);
  }
  ok("cfb: 40 streams in storages back", okall);
  got = cfb_read_path(&c, "Empty", &gl);
  ok("cfb: an empty stream", got && gl == 0);
  free(got);
  ok("cfb: parents", cfb_find(&c, "Store1/Sub") > 0 && c.e[cfb_find(&c, "Store1/Sub/s01")].parent == cfb_find(&c, "Store1/Sub") &&
     cfb_child(&c, 0, "Store2") == cfb_find(&c, "Store2") && cfb_find(&c, "Store1/Nope") < 0);
  cfb_close(&c);
  /* damaged: cut short, or garbage in the FAT, never reads out of bounds */
  ok("cfb: a file cut short", !cfb_open(&c, file, 600) || (cfb_close(&c), 1));
  for (i = 0; i < 2000; i++) file[512 + (i * 7919) % 4096] ^= (u8)i;
  if (cfb_open(&c, file, n)) {
    got = cfb_read_path(&c, "WordDocument", &gl);
    free(got);
    cfb_close(&c);
  }
  ok("cfb: a damaged file read safely", 1);
  free(file); free(big); free(small);
}

static const char *pw_given;
static int asked;
static const char *ask7(int attempt) { asked++; return attempt < 2 ? pw_given : NULL; }

static int starts(Doc *d, const char *t) { return d && d->np && !strncmp(d->p[0]->t, t, strlen(t)); }

/* encrypted packages: ours read by ourselves; msoffcrypto-tool's agile and LibreOffice's standard files read */
static void encryption(void)
{
  size_t n, en, dn;
  u8 *pkg = file_read("tests/data/word.docx", &n), *enc, *dec = NULL;
  EncKey key;
  char err[200];
  unsigned lost;
  Doc *d;
  if (!pkg) { ok("encrypt: word.docx", 0); return; }
  memset(&key, 0, sizeof key);
  enc = enc_encrypt(pkg, n, "p\xC3\xA4ssw\xC3\xB6rd", &key, &en);
  ok("encrypt: written", enc && enc_is_encrypted(enc, en) && key.valid && key.spin == 100000);
  ok("encrypt: the right password", enc && enc_decrypt(enc, en, "p\xC3\xA4ssw\xC3\xB6rd", &dec, &dn, NULL) == ENC_OK && dn == n && !memcmp(dec, pkg, n));
  free(dec); dec = NULL;
  ok("encrypt: a wrong one", enc && enc_decrypt(enc, en, "passwort", &dec, &dn, NULL) == ENC_BADPW && !dec);
  free(enc);
  enc = enc_encrypt(pkg, n, "p\xC3\xA4ssw\xC3\xB6rd", &key, &en);    /* (the key kept: no spinning) */
  ok("encrypt: saved again with the kept key", enc && enc_decrypt(enc, en, "p\xC3\xA4ssw\xC3\xB6rd", &dec, &dn, NULL) == ENC_OK && dn == n);
  free(dec); dec = NULL;
  if (enc && en > 4000) { enc[en / 2] ^= 1; ok("encrypt: a changed byte fails the integrity check", enc_decrypt(enc, en, "p\xC3\xA4ssw\xC3\xB6rd", &dec, &dn, NULL) == ENC_DAMAGED); }
  free(dec); dec = NULL;
  free(enc); free(pkg);
  ok("encrypt: a plain zip isn't encrypted", !enc_is_encrypted((const u8 *)"PK\3\4", 4));
  /* other programs' files */
  pw_given = "Quill-7"; prot_ask = ask7; asked = 0;
  d = doc_load_file("tests/data/enc-agile.docx", &lost, err, sizeof err);
  ok("encrypt: msoffcrypto's agile file opens", starts(d, "The Quill Test") && asked == 1 && prot_password(d) && !strcmp(prot_password(d), "Quill-7"));
  if (d) {
    /* saved: still encrypted, with the same password */
    const char *tmp = "/tmp/quill_wp7_enc.docx";
    Doc *e;
    size_t fl;
    u8 *f;
    ok("encrypt: saved encrypted", doc_save_file(d, tmp) && (f = file_read(tmp, &fl)) && enc_is_encrypted(f, fl) && (free(f), 1));
    asked = 0;
    e = doc_load_file(tmp, &lost, err, sizeof err);
    ok("encrypt: and read back", starts(e, "The Quill Test") && asked == 1);
    if (e) { prot_set_password(e, NULL); ok("encrypt: password cleared, saved plain", doc_save_file(e, tmp) && (f = file_read(tmp, &fl)) && fl > 4 && f[0] == 'P' && (free(f), 1)); }
    doc_free(e);
    remove(tmp);
  }
  doc_free(d);
  asked = 0;
  d = doc_load_file("tests/data/enc-standard.docx", &lost, err, sizeof err);
  ok("encrypt: LibreOffice's standard (AES-128) file opens", starts(d, "The Quill Test"));
  doc_free(d);
  /* LibreOffice 24.2's agile file (AES-256, SHA-512), saved from word.docx with the password LO-Quill-8 */
  pw_given = "LO-Quill-8"; asked = 0;
  d = doc_load_file("tests/data/enc-lo.docx", &lost, err, sizeof err);
  ok("encrypt: LibreOffice's agile file opens", starts(d, "The Quill Test") && asked == 1 && d->np > 10);
  doc_free(d);
  pw_given = "wrong";
  asked = 0;
  d = doc_load_file("tests/data/enc-agile.docx", &lost, err, sizeof err);
  ok("encrypt: a wrong password asked again, then given up", !d && asked == 3 && strstr(err, "password"));
  prot_ask = NULL;
  d = doc_load_file("tests/data/enc-agile.docx", &lost, err, sizeof err);
  ok("encrypt: without asking, refused", !d);
}

/* Restrict Editing: enforced in the edit paths, its exceptions, its password, and .docx */
static int form7(const Doc *d, Pos p) { (void)d; return p.para == 1; }
static Doc *three(void)
{
  Doc *d = doc_new();
  Pos p = P(0, 0);
  doc_insert(d, &p, "Locked text here.\nFree text here.\nMore locked.", 46, NULL);
  doc_clear_undo(d);
  return d;
}
static void restrict_editing(void)
{
  Doc *d = three(), *e;
  Pos p;
  CharFmt bold;
  int k, perms[4];
  unsigned lost;
  memset(&bold, 0, sizeof bold);
  bold.flags = CF_BOLD;
  prot_user[0] = 0;
  k = prot_perm(d, NULL, "everyone");
  ok("restrict: an exception", k == 1 && prot_perm(d, NULL, "everyone") == 1 && prot_perm(d, "jane", NULL) == 2);
  ok("restrict: marked", prot_mark(d, P(1, 0), P(1, 9), k));
  prot_set_kind(d, PR_READONLY, 0);
  ok("restrict: not yet enforced, anything goes", (p = P(0, 0), doc_insert(d, &p, "A", 1, NULL)) && doc_delete(d, P(0, 0), P(0, 1)));
  ok("restrict: enforced with a password", prot_start(d, "secret") && prot_enforced(d) == PR_READONLY && d->prot->hash[0] && d->prot->sid == 14);
  p = P(0, 3);
  ok("restrict: typing in locked text refused", !doc_insert(d, &p, "x", 1, NULL) && text_is(d, "Locked text here.\nFree text here.\nMore locked."));
  p = P(1, 4);
  ok("restrict: typing in the exception", doc_insert(d, &p, "!", 1, NULL) && !strncmp(d->p[1]->t, "Free! text", 10));
  p = P(1, 10);
  ok("restrict: typing at its end continues it", doc_insert(d, &p, "X", 1, NULL) && prot_free_at(d, P(1, 10)));
  ok("restrict: deleting inside", doc_delete(d, P(1, 4), P(1, 5)));
  ok("restrict: deleting across its edge refused", !doc_delete(d, P(1, 8), P(1, 14)) && !doc_delete(d, P(0, 10), P(1, 2)));
  ok("restrict: formatting outside refused, inside done", !doc_format(d, P(0, 0), P(0, 6), CM_BOLD, &bold) && doc_format(d, P(1, 0), P(1, 4), CM_BOLD, &bold));
  ok("restrict: paragraph formatting of a locked paragraph refused", !doc_para_format(d, 2, 2, PM_ALIGN, &d->p[0]->pf) || 0);
  ok("restrict: styles can't change", doc_delete_style(d, doc_style(d, "Heading1")) == 0);
  p = P(0, 0);
  ok("restrict: a comment refused in read only", doc_comment(d, P(0, 0), P(0, 6), "Hm", "A", "A", "") < 0 || !strstr(d->p[0]->t, "\xF3"));
  ok("restrict: the exceptions found", prot_perms_in(d, P(0, 0), P(2, 3), perms, 4) == 1 && perms[0] == 1);
  {
    Pos a, b;
    ok("restrict: the next region I can edit", prot_next_free(d, P(0, 0), &a, &b) && a.para == 1 && a.off == 0 && b.off >= 9);
  }
  ok("restrict: wrong password", prot_stop(d, "Secret") == PC_INCORRECT && prot_enforced(d));
  /* .docx: protection and exceptions kept */
  e = docx_again(d, &lost);
  ok("restrict: documentProtection read back", e && prot_enforced(e) == PR_READONLY && !strcmp(e->prot->hash, d->prot->hash) && e->prot->spin == PROT_SPIN);
  ok("restrict: the exception read back where it was", e && e->prot->nperms == 1 && !strcmp(e->prot->perms[0].grp, "everyone") &&
     prot_free_at(e, P(1, 2)) && !prot_free_at(e, P(0, 2)) && !prot_free_at(e, P(2, 2)));
  ok("restrict: its password checks", e && prot_check(e, "secret") == PC_CORRECT && prot_check(e, "secreT") == PC_INCORRECT);
  doc_free(e);
  ok("restrict: right password stops it", prot_stop(d, "secret") == PC_CORRECT && !prot_enforced(d));
  p = P(0, 0);
  ok("restrict: then anything goes", doc_insert(d, &p, "Z", 1, NULL));
  doc_free(d);

  /* comments only: comments may be added (and their marks taken out), nothing else */
  d = three();
  prot_set_kind(d, PR_COMMENTS, 0);
  prot_start(d, NULL);
  ok("restrict: comments allowed", doc_comment(d, P(0, 0), P(0, 6), "Note", "A", "A", "") >= 0 && strstr(d->p[0]->t, "\xF3"));
  p = P(0, 2);
  ok("restrict: but no typing", !doc_insert(d, &p, "x", 1, NULL));
  ok("restrict: a comment deleted", doc_delete_comment(d, 0) && !strstr(d->p[0]->t, "\xF3"));
  ok("restrict: no password: stopped by anyone", prot_check(d, "") == PC_NOPASSWORD && prot_stop(d, NULL) == PC_NOPASSWORD && !prot_enforced(d));
  doc_free(d);

  /* tracked changes only */
  d = three();
  prot_set_kind(d, PR_TRACKED, 0);
  prot_start(d, "t");
  p = P(0, 0);
  ok("restrict: tracked changes: typing allowed, tracking forced", doc_insert(d, &p, "x", 1, NULL) && prot_forces_tracking(d));
  ok("restrict: tracked changes: accept and reject refused", !doc_resolve(d, P(0, 0), P(2, 3), 1));
  doc_free(d);

  /* filling in forms: only what a forms package says is a form field (WP16's hook) */
  d = three();
  prot_set_kind(d, PR_FORMS, 0);
  prot_start(d, NULL);
  prot_form_at = form7;
  p = P(1, 2);
  ok("restrict: forms only, a form field filled in", prot_forms_only(d) && doc_insert(d, &p, "x", 1, NULL));
  p = P(0, 2);
  ok("restrict: forms only, other text locked", !doc_insert(d, &p, "x", 1, NULL));
  prot_form_at = NULL;
  doc_free(d);

  /* exceptions for a named person */
  d = three();
  k = prot_perm(d, "DOMAIN\\Jane", NULL);
  prot_mark(d, P(2, 0), P(2, 4), k);
  prot_set_kind(d, PR_READONLY, 0);
  prot_start(d, NULL);
  p = P(2, 1);
  strcpy(prot_user, "Bob");
  ok("restrict: someone else's exception is locked", !doc_insert(d, &p, "x", 1, NULL));
  strcpy(prot_user, "jane");
  ok("restrict: the named person may edit it", doc_insert(d, &p, "x", 1, NULL));
  e = docx_again(d, &lost);
  ok("restrict: w:ed read back", e && e->prot->nperms == 1 && !strcmp(e->prot->perms[0].ed, "DOMAIN\\Jane") && prot_free_at(e, P(2, 2)));
  doc_free(e);
  prot_user[0] = 0;
  doc_free(d);

  /* Word's legacy key (ECMA-376 Part 4's example: "Example" is 0x64CEED7E) and its form of the hash */
  ok("restrict: Word's legacy key", prot_legacy_key("Example") == 0x64CEED7EUL);
  d = doc_new();
  prot_set_kind(d, PR_READONLY, 0);
  {
    DocProtect *pr = prot_get(d);
    /* a SHA-1 hash with no spins, as Word 2007 might have stored it: worked out here from the definition */
    u8 salt[16], h[20], t16[16];
    char key[12];
    int i;
    u32 kk = prot_legacy_key("Example");
    for (i = 0; i < 16; i++) salt[i] = (u8)(i + 1);
    sprintf(key, "%02X%02X%02X%02X", (unsigned)(kk & 255), (unsigned)((kk >> 8) & 255), (unsigned)((kk >> 16) & 255), (unsigned)(kk >> 24));
    for (i = 0; i < 8; i++) { t16[2 * i] = (u8)key[i]; t16[2 * i + 1] = 0; }
    hash2(CH_SHA1, salt, 16, t16, 16, h);
    pr->enforced = 1; pr->sid = 4; pr->spin = 0; pr->iso = 0;
    b64_put(h, 20, pr->hash, sizeof pr->hash);
    b64_put(salt, 16, pr->salt, sizeof pr->salt);
    ok("restrict: a SHA-1 legacy hash checks", prot_check(d, "Example") == PC_CORRECT && prot_check(d, "example") == PC_INCORRECT);
    pr->sid = 3;
    ok("restrict: MD5 can't be checked (stays on)", prot_check(d, "Example") == PC_UNSUPPORTED && prot_stop(d, "Example") == PC_UNSUPPORTED && prot_enforced(d));
  }
  doc_free(d);
}

/* settings.xml: the packages' elements in CT_Settings' order (NEXT.md's integration item 1): the attached
   template (WP23) before w:doNotTrackMoves (WP5) before w:documentProtection (WP7), all before w:defaultTabStop */
static void settings_order(void)
{
  Doc *d = three();
  size_t n;
  u8 *z;
  char *st = NULL;
  const char *a, *b, *c, *t;
  d->attached = str_dup("Normal.dotm");
  d->rvsettings |= RVS_NOMOVES;
  prot_set_kind(d, PR_COMMENTS, 0);
  prot_start(d, "x");
  z = docx_save(d, &n);
  if (z) {
    ZipReader zr;
    size_t l = 0;
    if (zip_open(&zr, z, n)) { st = (char *)zip_read(&zr, zip_find(&zr, "word/settings.xml"), &l); zip_close(&zr); }
  }
  a = st ? strstr(st, "<w:attachedTemplate") : NULL; b = st ? strstr(st, "<w:doNotTrackMoves") : NULL;
  c = st ? strstr(st, "<w:documentProtection") : NULL; t = st ? strstr(st, "<w:defaultTabStop") : NULL;
  ok("settings: in the schema's order", a && b && c && t && a < b && b < c && c < t);
  free(st); free(z);
  doc_free(d);
}

void test_wp7(void)
{
  vectors();
  compound();
  encryption();
  restrict_editing();
  settings_order();
}
