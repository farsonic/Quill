/*
 * mac/qm_mac.c - see qm_mac.h. The File Manager is called with parameter blocks through the app
 * kit's mac_pb (Riptide's src/mac/app/ostraps.S), paths passed as Pascal strings with volume 0 and
 * directory 0: a full path ("Disk:Folder:file") or one from the default folder (":Fonts:x"), which
 * at launch is the folder the application is in. GPL-2.0-only, as Quill.
 */
#include <string.h>
#include "qm_mac.h"

void *mac_newptr(long n);
void  mac_disposeptr(void *p);
long  mac_pb(void *pb, long trap);
void  mm_log_line(const char *s);

void *qm_newptr(long n) { return mac_newptr(n); }
void  qm_disposeptr(void *p) { mac_disposeptr(p); }

/* File Manager traps (Inside Macintosh: Files); HFS variants have bit 9 set */
#define T_HOPEN    0xA200
#define T_CLOSE    0xA001
#define T_READ     0xA002
#define T_WRITE    0xA003
#define T_HCREATE  0xA208
#define T_HDELETE  0xA209
#define T_HRENAME  0xA20B
#define T_GETEOF   0xA011
#define T_SETEOF   0xA012
#define T_GETFPOS  0xA018
#define T_SETFPOS  0xA044
#define T_HGETFINFO 0xA20C
#define T_HSETFINFO 0xA20D
/* the fields of an HParamBlockRec this file uses */
#define PB_RESULT  16
#define PB_NAME    18
#define PB_VREF    22
#define PB_REF     24
#define PB_PERM    27
#define PB_MISC    28
#define PB_BUF     32
#define PB_COUNT   36
#define PB_ACTUAL  40
#define PB_POSMODE 44
#define PB_POSOFF  46
#define PB_FINFO   32                       /* HGetFInfo: ioFlFndrInfo (type, creator, ...) */
#define PB_DIRID   48
#define PB_SIZE    108
#define W(pb, o)   (*(short *)((char *)(pb) + (o)))
#define L(pb, o)   (*(long *)((char *)(pb) + (o)))

static void pb_path(unsigned char *pb, unsigned char *pname, const char *path)
{
	size_t n = strlen(path);
	if (n > 255)
		n = 255;
	pname[0] = (unsigned char)n;
	memcpy(pname + 1, path, n);
	memset(pb, 0, PB_SIZE);
	*(unsigned char **)(pb + PB_NAME) = pname;
}

void qm_type_for(const char *path, unsigned long *type, unsigned long *creator)
{
	const char *dot = strrchr(path, '.');
	*creator = QM_CREATOR;
	*type = 0x54455854UL;                   /* 'TEXT': .txt .md .html .rtf .xml */
	if (!dot) {
		size_t n = strlen(path);
		if (n >= 12 && !strcmp(path + n - 12, " Preferences"))
			*type = 0x70726566UL;   /* 'pref': the Finder shows it as a preferences file */
		return;
	}
	if (!strcmp(dot, ".docx") || !strcmp(dot, ".docm") || !strcmp(dot, ".dotx"))
		*type = 0x444F4358UL;           /* 'DOCX' */
	else if (!strcmp(dot, ".odt"))
		*type = 0x4F445420UL;           /* 'ODT ' */
	else if (!strcmp(dot, ".pdf"))
		*type = 0x50444620UL, *creator = 0x4341524FUL;   /* 'PDF ', Acrobat's 'CARO' */
	else if (!strcmp(dot, ".png"))
		*type = 0x504E4766UL, *creator = 0x3842494DUL;   /* 'PNGf', '8BIM' */
	else if (!strcmp(dot, ".doc"))
		*type = 0x5744424EUL, *creator = 0x4D535744UL;   /* Word's 'WDBN', 'MSWD' */
}

int qm_fopen(const char *path, int write, int append, short *ref)
{
	unsigned char pb[PB_SIZE], pname[256];
	long r;
	pb_path(pb, pname, path);
	pb[PB_PERM] = (unsigned char)(write ? 3 : 1);      /* fsRdWrPerm : fsRdPerm */
	r = mac_pb(pb, T_HOPEN);
	if (r == -43 && write) {                           /* fnfErr: create it, with its type */
		unsigned long type, creator;
		pb_path(pb, pname, path);
		if ((r = mac_pb(pb, T_HCREATE)) != 0)
			return (int)r;
		qm_type_for(path, &type, &creator);
		pb_path(pb, pname, path);
		if (mac_pb(pb, T_HGETFINFO) == 0) {
			L(pb, PB_FINFO) = (long)type;          /* the rest of the Finder info kept */
			L(pb, PB_FINFO + 4) = (long)creator;
			W(pb, PB_DIRID) = 0, L(pb, PB_DIRID) = 0;
			mac_pb(pb, T_HSETFINFO);
		}
		pb_path(pb, pname, path);
		pb[PB_PERM] = 3;
		r = mac_pb(pb, T_HOPEN);
	}
	if (r)
		return (int)r;
	*ref = W(pb, PB_REF);
	if (write && !append) {
		memset(pb, 0, PB_SIZE);
		W(pb, PB_REF) = *ref;
		L(pb, PB_MISC) = 0;
		mac_pb(pb, T_SETEOF);
	} else if (append) {
		qm_fseek(*ref, qm_feof_pos(*ref));
	}
	return 0;
}

static long rw(short ref, const void *p, long n, long trap)
{
	unsigned char pb[PB_SIZE];
	long r;
	memset(pb, 0, PB_SIZE);
	W(pb, PB_REF) = ref;
	*(const void **)(pb + PB_BUF) = p;
	L(pb, PB_COUNT) = n;
	W(pb, PB_POSMODE) = 0;                  /* fsAtMark */
	r = mac_pb(pb, trap);
	if (r && r != -39)                      /* eofErr: what there was */
		return L(pb, PB_ACTUAL) ? L(pb, PB_ACTUAL) : -1;
	return L(pb, PB_ACTUAL);
}
long qm_fread(short ref, void *p, long n) { return rw(ref, p, n, T_READ); }
long qm_fwrite(short ref, const void *p, long n) { return rw(ref, p, n, T_WRITE); }

void qm_fclose(short ref)
{
	unsigned char pb[PB_SIZE];
	memset(pb, 0, PB_SIZE);
	W(pb, PB_REF) = ref;
	mac_pb(pb, T_CLOSE);
}
long qm_ftell(short ref)
{
	unsigned char pb[PB_SIZE];
	memset(pb, 0, PB_SIZE);
	W(pb, PB_REF) = ref;
	return mac_pb(pb, T_GETFPOS) ? -1 : L(pb, PB_POSOFF);
}
long qm_feof_pos(short ref)
{
	unsigned char pb[PB_SIZE];
	memset(pb, 0, PB_SIZE);
	W(pb, PB_REF) = ref;
	return mac_pb(pb, T_GETEOF) ? -1 : L(pb, PB_MISC);
}
int qm_fseek(short ref, long pos)
{
	unsigned char pb[PB_SIZE];
	memset(pb, 0, PB_SIZE);
	W(pb, PB_REF) = ref;
	W(pb, PB_POSMODE) = 1;                  /* fsFromStart */
	L(pb, PB_POSOFF) = pos;
	return mac_pb(pb, T_SETFPOS) ? -1 : 0;
}
int qm_fdelete(const char *path)
{
	unsigned char pb[PB_SIZE], pname[256];
	pb_path(pb, pname, path);
	return mac_pb(pb, T_HDELETE) ? -1 : 0;
}
int qm_frename(const char *from, const char *to)
{
	unsigned char pb[PB_SIZE], pname[256], nname[256];
	const char *leaf = strrchr(to, ':');
	size_t n;
	leaf = leaf ? leaf + 1 : to;               /* HRename takes the new name only, in the same folder */
	n = strlen(leaf) > 255 ? 255 : strlen(leaf);
	nname[0] = (unsigned char)n;
	memcpy(nname + 1, leaf, n);
	pb_path(pb, pname, from);
	*(unsigned char **)(pb + PB_MISC) = nname;
	return mac_pb(pb, T_HRENAME) ? -1 : 0;
}

unsigned long qm_seconds(void) { return *(volatile unsigned long *)0x20C; }   /* Time */
unsigned long qm_ticks(void) { return *(volatile unsigned long *)0x16A; }     /* Ticks */

void qm_log_bytes(const void *p, long n)
{
	char line[256];
	if (n > 255)
		n = 255;
	memcpy(line, p, (size_t)n);
	line[n] = 0;
	if (n && line[n - 1] == '\n')
		line[n - 1] = 0;
	if (line[0])
		mm_log_line(line);
}

void qm_exit(void)
{
	__asm__ volatile(".short 0xA9F4");      /* _ExitToShell */
	for (;;)
		;
}

/* The System's region code: the word after the version bytes of its 'vers' resource 1 (Inside
   Macintosh: Toolbox Essentials, "The Version Resource"). Read from the System file itself
   (reference 0): Quill's own resources come first in the chain. */
short CurResFile(void);
void  UseResFile(short ref);
void *Get1Resource(long type, short id);
short qm_system_region(void)
{
	short save = CurResFile(), region = -1;
	char **h;
	UseResFile(0);
	h = (char **)Get1Resource(0x76657273L /* 'vers' */, 1);
	if (h && *h)
		region = *(short *)(*h + 4);
	UseResFile(save);
	return region;
}

/* Everything the File Manager still holds for the volume a path is on written to the disk
   (FlushVol): after a save, so that a crash or a switched-off Mac keeps the document. The
   volume is named by the path's first part, "Disk:" */
#define T_FLUSHVOL 0xA013
void qm_flush(const char *path)
{
	unsigned char pb[PB_SIZE], name[64];
	const char *c = strchr(path, ':');
	size_t n = c && c != path ? (size_t)(c - path) + 1 : 0;
	memset(pb, 0, sizeof pb);
	if (n && n < sizeof name) {
		name[0] = (unsigned char)n;
		memcpy(name + 1, path, n);
		*(unsigned char **)(pb + PB_NAME) = name;
	}
	mac_pb(pb, T_FLUSHVOL);
}
