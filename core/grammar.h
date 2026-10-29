/*
 * grammar.h - WP8: English grammar and style checks on a paragraph's text, as Word's (and WordCraft's)
 * grammar checker marks them: a word repeated, "a" before a vowel sound or "an" before a consonant
 * sound, a sentence starting with a small letter, a space before punctuation or none after it, two
 * spaces between words, commonly confused words ("could of", "their is", "more then"), and two
 * optional style checks, passive voice and wordy phrases.
 *
 * Written from the documented behaviour; the word lists are Quill's own.
 */
#ifndef QUILL_GRAMMAR_H
#define QUILL_GRAMMAR_H

#include "doc.h"

enum { GR_REPEAT, GR_ARTICLE, GR_CAPITAL, GR_SPACEBEFORE, GR_SPACEAFTER, GR_DOUBLESPACE, GR_CONFUSED,
       GR_PASSIVE, GR_WORDY, GR_N };
/* what is checked: GRO_GRAMMAR the grammar rules, GRO_STYLE passive voice and wordy phrases too */
enum { GRO_GRAMMAR = 1, GRO_STYLE = 2 };
typedef struct {
  u32 a, b;                 /* the bytes flagged, a..b in the paragraph's text */
  u8 kind;                  /* GR_ */
  char sug[48];             /* what to put in their place (UTF-8; may be ""), when there is one */
  u8 has_sug;               /* (0: no suggestion, for the style checks) */
} GramIssue;

/* the issues in t (len bytes of a paragraph's UTF-8 text), in order; up to max; how many */
int grammar_check(const char *t, u32 len, unsigned opts, GramIssue *out, int max);
/* the rule's explanation, in English (the interface translates it) */
const char *grammar_message(int kind);
/* the next issue from p onwards in d's body (paragraphs in English, or with no language, and not
   marked "do not check"), going round once; 0 if none. Issues the user ignored (grammar_ignore) are
   passed over */
int grammar_next(Doc *d, Pos p, unsigned opts, Pos *a, Pos *b, GramIssue *gi);
/* is text in paragraph q at off checked for English grammar? (its language and "do not check") */
int grammar_applies(Para *q, u32 off);
/* "Ignore": this issue (its kind and text) not flagged again this session; grammar_ignored asks */
void grammar_ignore(const GramIssue *gi, const char *t);
int  grammar_ignored(const GramIssue *gi, const char *t);
void grammar_forget(void);

#endif
