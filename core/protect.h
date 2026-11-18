/*
 * protect.h - WP7 (docs/wordcraft-gap.md): Restrict Editing and the password a document is encrypted with.
 *
 * Restrict Editing (Word's w:documentProtection in settings.xml): while it is enforced, the document can
 * only be read (PR_READONLY), commented on (PR_COMMENTS), changed with every change tracked (PR_TRACKED) or
 * filled in (PR_FORMS: Quill has no form fields, so like read only). Parts of it can be left free for
 * everyone or for named people (exceptions: w:permStart/w:permEnd ranges; CharFmt.perm numbers them in the
 * text, so they move with it). Stopping it takes the password whose hash (SHA-512 with a salt, 10000
 * spins) is kept in the file; Word 2007's legacy form of the hash is checked too.
 *
 * Enforcement is in core/doc.c's edit paths: doc_insert, doc_delete, doc_format and the other per-run
 * edits, doc_paste and doc_resolve check the exact range (prot_*_ok); every other edit is caught where it
 * saves its paragraphs for undo (prot_gate), so nothing changes a locked paragraph.
 *
 * Encryption (core/encrypt.h): a document read with a password keeps it, and is encrypted again when it is
 * saved as .docx; File > Info > Protect Document > Encrypt with Password sets or clears it.
 */
#ifndef QUILL_PROTECT_H
#define QUILL_PROTECT_H

#include "doc.h"
#include "encrypt.h"
#include "xml.h"

enum { PR_NONE, PR_READONLY, PR_COMMENTS, PR_TRACKED, PR_FORMS };
/* the spins Quill hashes a protection password with (Word uses 100000; any count is valid, and the legacy
   key's 32 bits are what limits the hash anyway: 10000 takes 16 s instead of 160 on a 68040) */
#define PROT_SPIN 10000UL
#define CM_PERM 0x08000000U             /* doc_format: CharFmt.perm (not in CM_ALL) */

/* an exception: who may edit the text whose CharFmt.perm is its number (index + 1) */
typedef struct Perm {
  char ed[64];              /* a person (w:ed: "name" or "domain\\name"), "" none */
  char grp[24];             /* a group (w:edGrp: "everyone", "current", "editors" ...), "" none */
  i32 colfirst, collast;    /* table columns (w:colFirst, w:colLast), -1 none */
} Perm;

typedef struct DocProtect {
  u8 kind;                  /* PR_ (w:edit) */
  u8 enforced;              /* w:enforcement */
  u8 formatting;            /* w:formatting: formatting limited to a selection of styles */
  /* the password's hash: Word's form (w:hash, w:salt, w:cryptAlgorithmSid, w:cryptSpinCount: the password
     made Word's legacy key first) or the ISO form (w:hashValue, w:saltValue, w:algorithmName, w:spinCount) */
  u8 iso;                   /* the ISO form */
  int sid;                  /* the hash: 4 SHA-1, 14 SHA-512 (others can't be checked) */
  char hash[100], salt[64]; /* base64, "" none */
  u32 spin;
  char provider[16];        /* w:cryptProviderType as read ("rsaAES"), written back */
  Perm *perms; int nperms;
  /* encryption */
  char *password;           /* UTF-8, NULL: not encrypted */
  EncKey key;               /* the password's derived key (saving again is quick) */
  /* (working state) */
  u32 passver; u8 pass;     /* an entry check let Doc.ver's edit through */
  u8 bypass;                /* the user is setting protection up: nothing is checked */
  struct { const void *pc; u32 para, off; int perm; char id[16]; } open[8]; int nopen;   /* ranges being read */
} DocProtect;

/* the document's (made on first use; NULL if out of memory), or NULL if it has none */
DocProtect *prot_get(Doc *d);
DocProtect *prot_peek(const Doc *d);
void prot_free(Doc *d);                          /* (doc_free does it) */

/* ---- who is editing (the name exceptions are matched against; "" nobody in particular) */
extern char prot_user[64];

/* ---- what is enforced */
int  prot_enforced(const Doc *d);                /* the kind enforced, PR_NONE if none */
int  prot_forces_tracking(const Doc *d);         /* changes must be tracked */
/* (WP16, content controls and form fields) "Filling in forms" is enforced: only form fields, content
   controls that aren't locked, Restrict Editing's exceptions and sections with w:formProt off may be
   changed. prot_form_at (WP16's sdt_form_at, core/sdt.h) says whether the text at p (the character
   after it) is such a form's fillable part; WP16's sdt_forms_protected is prot_forms_only */
int  prot_forms_only(const Doc *d);
extern int (*prot_form_at)(const Doc *d, Pos p);
/* the text at p (the character after it) may be edited by prot_user: inside an exception for them */
int  prot_free_at(const Doc *d, Pos p);
/* the checks doc.c makes: 1 if the edit may go ahead (and then the paragraphs it saves for undo pass too) */
int  prot_insert_ok(Doc *d, Pos at, const char *s, u32 n);
int  prot_range_ok(Doc *d, Pos a, Pos b, int format);
int  prot_resolve_ok(Doc *d);
int  prot_gate(Doc *d, u32 p0, u32 n);           /* paragraphs [p0, p0 + n) are about to change */
int  prot_gate_styles(Doc *d);                   /* the style table is about to change */
/* "Limit formatting to a selection of styles" (w:formatting) enforced, with or without an editing
   restriction: no direct formatting (character look, paragraph settings), and the styles marked locked
   (WP13's SXF_LOCKED, Manage Styles > Restrict, w:locked) can't be applied. doc.c's formatting edits ask */
int  prot_limits_formatting(const Doc *d);
int  prot_direct_ok(Doc *d);                     /* direct formatting allowed */
int  prot_style_ok(Doc *d, int style);           /* applying the style (Doc.styles index) allowed */
/* called whenever an edit is refused (the interface beeps and says why); NULL none */
extern void (*prot_on_denied)(Doc *d);

/* ---- setting it up */
/* kind (PR_) and formatting chosen; not yet enforced */
void prot_set_kind(Doc *d, int kind, int formatting);
/* enforcing with a password (UTF-8; "" or NULL: none): its hash made (SHA-512, Word's form). 0 if out of memory */
int  prot_start(Doc *d, const char *password);
/* the password checked: PC_ */
enum { PC_NOPASSWORD, PC_CORRECT, PC_INCORRECT, PC_UNSUPPORTED };
int  prot_check(const Doc *d, const char *password);
/* enforcement stopped if the password is right: PC_ (the hash forgotten on PC_NOPASSWORD or PC_CORRECT) */
int  prot_stop(Doc *d, const char *password);
/* an exception's number (added if new) for a person (ed) or a group (grp); 0 if out of memory */
int  prot_perm(Doc *d, const char *ed, const char *grp);
/* a..b made free to edit for perm (0: the exceptions there removed); one undo step. 0 if nothing done */
int  prot_mark(Doc *d, Pos a, Pos b, int perm);
/* the exceptions of a..b: groups and people in *out (at most max), how many */
int  prot_perms_in(Doc *d, Pos a, Pos b, int *out, int max);
/* the next stretch of text from p (wrapping round) that prot_user may edit; 0 if none */
int  prot_next_free(const Doc *d, Pos p, Pos *a, Pos *b);
/* whether anyone may edit perm k (everyone, or prot_user) */
int  prot_perm_mine(const Doc *d, int k);

/* ---- the password hash (for tests and the settings) */
u32  prot_legacy_key(const char *password);      /* Word's legacy 32-bit key of a password (ECMA-376 Part 4) */

/* ---- encryption */
void prot_set_password(Doc *d, const char *password);   /* NULL or "": not encrypted */
const char *prot_password(const Doc *d);         /* NULL if not encrypted */

/* ---- .docx (core/docx.c's hooks) */
void prot_read_setting(Doc *d, Xml *x);   /* w:documentProtection */
void prot_write_setting(Doc *d, Buf *b);         /* (where 1) */
/* w:permStart / w:permEnd read: pc identifies the story (its paragraph list), out/nout its finished
   paragraphs, cur the one being built (may be NULL) */
int  prot_read_perm(Doc *d, Xml *x, const void *pc, Para **out, u32 nout, Para *cur);
/* writing a paragraph's runs: the exception open moves from *open to perm (permEnd, permStart written) */
void prot_write_perm(Buf *o, const Doc *d, u16 *open, u16 perm);
/* the decrypted package, if data is an encrypted one: asks for the password with ask (attempt 0, 1, 2 ...;
   NULL: given up, PROT_CANCELLED returned); returns ENC_ and *out, the password kept in *pw (malloc'd) */
#define PROT_CANCELLED 100
/* doc_load_file's: data (taken) decrypted when it is an encrypted package, asking with prot_ask (NULL: not
   asked, refused); NULL on failure with err set. prot_load_done gives the next document read the password */
extern const char *(*prot_ask)(int attempt);
u8  *prot_load_decrypt(u8 *data, size_t *n, char *err, size_t errcap);
void prot_load_done(Doc *d);
/* doc_save_file's: a .docx (taken) encrypted when the document has a password; NULL if out of memory */
u8  *prot_save_encrypt(Doc *d, u8 *data, size_t *n);
int  prot_open_encrypted(const u8 *data, size_t len, const char *(*ask)(int attempt), u8 **out, size_t *outlen, char **pw, EncKey *key);

#endif
