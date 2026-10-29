/*
 * lang.h - the languages Quill's interface speaks. Its words are written in English in the code;
 * tr() gives them in the language chosen (File > Options), from a table built in. The custom parts
 * (ribbon, File page, status bar, pane) translate as they draw, so a new language shows at once;
 * menus and windows take theirs when made. Latin-1 in, Latin-1 out.
 */
#ifndef QUILL_LANG_H
#define QUILL_LANG_H

enum { LANG_EN, LANG_DE, LANG_FR, LANG_ES, LANG_IT, UI_NLANG };
extern const char *const ui_languages[UI_NLANG];   /* each language's name in itself */

const char *tr(const char *english);                /* the words in the language chosen (or as given) */
void lang_set(int lang);                            /* the interface in language lang (LANG_) */
int  lang_current(void);                            /* the language chosen */
int  lang_system(void);                             /* the language Locale prefs prefer, if Quill has it */
const char *lang_code(int lang);                    /* "en", "de" ... */
int  lang_by_code(const char *code);                /* -1: not known */

#endif
