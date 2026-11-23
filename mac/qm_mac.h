/*
 * mac/qm_mac.h - the Toolbox services Quill's Mac build uses below the C library (mac/qm_mac.c):
 * memory, the File Manager by path, the clock, the log. GPL-2.0-only, as Quill.
 */
#ifndef QM_MAC_H
#define QM_MAC_H
void *qm_newptr(long n);                    /* a cleared Memory Manager pointer, NULL if none */
void  qm_disposeptr(void *p);
/* a data fork by its Mac path: 0, or an OSErr. write: created if missing (type and creator from
   the name's ending), emptied unless append */
int   qm_fopen(const char *path, int write, int append, short *ref);
long  qm_fread(short ref, void *p, long n);      /* bytes read (0 at the end), -1 on an error */
long  qm_fwrite(short ref, const void *p, long n);
void  qm_fclose(short ref);
long  qm_ftell(short ref);
long  qm_feof_pos(short ref);               /* the file's length */
int   qm_fseek(short ref, long pos);
int   qm_fdelete(const char *path);
int   qm_frename(const char *from, const char *to);
/* full Mac paths: a folder's ("Disk:Folder:Sub:", always ending in ':'), a file's from its volume,
   folder and name (a Pascal string), the folder Octavo is in; 0, or -1 if unknown or too long */
int   qm_dir_path(short vref, long dir, char *out, int cap);
int   qm_spec_path(short vref, long dir, const unsigned char *name, char *out, int cap);
int   qm_app_dir(char *out, int cap);
void  qm_flush(const char *path);           /* the path's volume written out (FlushVol): after a save */
unsigned long qm_seconds(void);             /* the Mac's clock: local seconds since 1904 */
unsigned long qm_ticks(void);               /* sixtieths of a second since the Mac started */
void  qm_log_bytes(const void *p, long n);  /* stdout and stderr: Riptide's log (Aurora68's with AURORA68_LOG) */
void  qm_exit(void);                        /* ExitToShell */
/* file type and creator for a name: 'TEXT' for text formats, 'DOCX' for Word documents... */
void  qm_type_for(const char *path, unsigned long *type, unsigned long *creator);
#define QM_CREATOR 0x4F435456UL            /* 'OCTV': Octavo's (the Amiga's Quill is not on the Mac) */
short qm_system_region(void);               /* the System's region code ('vers' 1), -1 if unknown */

/* 32-bit QuickDraw's offscreen worlds (mac/qm_thunks.S): NewGWorld (0 or a QDErr), its pixel map
   (a PixMapHandle), the pixels locked while drawn into, their address */
short qm_newgworld(void **gw, short depth, const void *bounds);
void  qm_disposegworld(void *gw);
void **qm_gworldpixmap(void *gw);
int   qm_lockpixels(void **pm);
void  qm_unlockpixels(void **pm);
void *qm_pixbaseaddr(void **pm);
#endif
