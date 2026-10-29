/*
 * recover.h - WP24: recovery copies and version history, in drawers the program names (on the
 * Amiga PROGDIR:Recovery and PROGDIR:Versions). Plain stdio only (no directory listing): each drawer
 * has index files saying what is in it.
 *
 * Recovery copies: while a document has unsaved changes, a copy is written every so often as
 * rec<id>.docx, listed in Recovery.index ("id<TAB>stamp<TAB>its own path<TAB>its name" a line). A
 * clean close or save drops it; copies still there at the next start are from a session that ended
 * without saving them, and are offered back (File > Info > Recover Unsaved Documents).
 *
 * Version history: each save of a document keeps a copy of the saved file as <key>-<n>.docx, key
 * made from the document's path, listed in <key>.idx (its path on the first line, then "n<TAB>stamp
 * <TAB>size<TAB>kind" a line, kind 's' a save, 'a' AutoSave: a run of AutoSaves keeps only its last).
 * The newest VER_KEEP are kept.
 */
#ifndef QUILL_RECOVER_H
#define QUILL_RECOVER_H

#include "doc.h"

#define VER_KEEP 25
/* the two drawers (they must exist; the program makes them) */
void rec_set_dirs(const char *recdir, const char *verdir);
/* dir and name joined as a path ("PROGDIR:Recovery" + "x" = "PROGDIR:Recovery/x"; "Work:" + "x" = "Work:x") */
void rec_join(const char *dir, const char *name, char *out, size_t cap);

typedef struct { int id; char stamp[24]; char path[256]; char name[108]; } RecEntry;
int  rec_list(RecEntry *out, int max);                 /* the copies there, oldest first; how many */
int  rec_new_id(void);                                 /* an id no copy has */
/* d's copy written (replacing the one with this id): path its own file ("" none), name what to call it */
int  rec_save(int id, Doc *d, const char *path, const char *name, const char *stamp);
void rec_drop(int id);                                 /* its file and line gone */
void rec_file(int id, char *out, size_t cap);          /* its file's path */

typedef struct { int n; char stamp[24]; long size; char kind; } VerEntry;
int  ver_list(const char *docpath, VerEntry *out, int max);     /* newest first; how many */
/* the file at docpath (just saved) kept as a version; autosave: replaces the last version if that was
   an AutoSave too. 0 if it couldn't be */
int  ver_add(const char *docpath, const char *stamp, int autosave);
void ver_file(const char *docpath, int n, char *out, size_t cap);   /* version n's file */
int  ver_delete(const char *docpath, int n);

#endif
