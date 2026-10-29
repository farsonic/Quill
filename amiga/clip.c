/* clip.c - the clipboard: plain text as an IFF FTXT form with one CHRS chunk (Latin-1) */
#include <stdlib.h>
#include <exec/memory.h>
#include <devices/clipboard.h>
#include <proto/exec.h>
#include "quill.h"

/* one clipboard.device command (CMD_READ, CMD_WRITE, CMD_UPDATE), done synchronously with DoIO:
   1 if it went well. The device moves io_Offset on by itself, so successive reads or writes walk
   through the clip */
static int clip_io(struct IOClipReq *io, UWORD cmd, APTR data, ULONG len)
{
  io->io_Command = cmd; io->io_Data = (STRPTR)data; io->io_Length = len;
  DoIO((struct IORequest *)io);
  return io->io_Error == 0;
}
/* s (len bytes of Latin-1) onto clipboard unit 0 as an FTXT form, for any program to paste: 1 if
   written. The 20-byte header is FORM, its length, FTXT, CHRS and the text's length; a chunk of odd
   length gets a pad byte, as IFF wants. The device takes the form written in order from offset 0,
   then CMD_UPDATE to say the clip is complete (sent whatever happened, so the clipboard isn't
   left waiting for the rest) */
int clip_write(const char *s, long len)
{
  struct MsgPort *mp;
  struct IOClipReq *io;
  int ok = 0;
  if (!(mp = CreateMsgPort())) return 0;
  if ((io = (struct IOClipReq *)CreateIORequest(mp, sizeof(struct IOClipReq)))) {
    if (!OpenDevice((STRPTR)"clipboard.device", 0, (struct IORequest *)io, 0)) {
      ULONG hdr[5];
      UBYTE pad = 0;
      hdr[0] = 0x464F524DUL; hdr[1] = 4 + 8 + len + (len & 1); hdr[2] = 0x46545854UL; hdr[3] = 0x43485253UL; hdr[4] = len;
      io->io_Offset = 0; io->io_Error = 0; io->io_ClipID = 0;
      ok = clip_io(io, CMD_WRITE, hdr, 20) && clip_io(io, CMD_WRITE, (APTR)s, len);
      if (ok && (len & 1)) clip_io(io, CMD_WRITE, &pad, 1);
      clip_io(io, CMD_UPDATE, 0, 0);
      CloseDevice((struct IORequest *)io);
    }
    DeleteIORequest((struct IORequest *)io);
  }
  DeleteMsgPort(mp);
  return ok;
}
/* the text on clipboard unit 0 if it holds an FTXT form: its first CHRS chunk, malloc()ed and
   0-terminated (the caller frees it), its length in *len; NULL and *len 0 if there's none. Other
   chunks (FONS, ...) are skipped; the bytes come as the writer gave them, Latin-1 by convention */
char *clip_read(long *len)
{
  struct MsgPort *mp;
  struct IOClipReq *io;
  char *out = NULL;
  ULONG hdr[3], ch[2];
  *len = 0;
  if (!(mp = CreateMsgPort())) return NULL;
  if ((io = (struct IOClipReq *)CreateIORequest(mp, sizeof(struct IOClipReq)))) {
    if (!OpenDevice((STRPTR)"clipboard.device", 0, (struct IORequest *)io, 0)) {
      io->io_Offset = 0; io->io_Error = 0; io->io_ClipID = 0;
      if (clip_io(io, CMD_READ, hdr, 12) && io->io_Actual == 12 && hdr[0] == 0x464F524DUL && hdr[2] == 0x46545854UL) {
        ULONG left = hdr[1] - 4;
        while (left >= 8 && clip_io(io, CMD_READ, ch, 8) && io->io_Actual == 8) {
          ULONG n = ch[1], pad = n & 1;
          left -= 8;
          if (ch[0] == 0x43485253UL && !out && n < 16UL * 1024 * 1024) {          /* CHRS */
            if ((out = (char *)malloc(n + 1))) { clip_io(io, CMD_READ, out, n); out[io->io_Actual] = 0; *len = io->io_Actual; }
            if (pad) { UBYTE b; clip_io(io, CMD_READ, &b, 1); }
          } else {                                      /* skip the chunk */
            UBYTE junk[256];
            ULONG k = n + pad;
            while (k) { ULONG m = k > 256 ? 256 : k; clip_io(io, CMD_READ, junk, m); if (!io->io_Actual) break; k -= io->io_Actual; }
          }
          left -= n + pad > left ? left : n + pad;
        }
      }
      /* read to the end, so the clipboard is free again */
      { UBYTE junk[64]; do { io->io_Command = CMD_READ; io->io_Data = (STRPTR)junk; io->io_Length = 64; DoIO((struct IORequest *)io); } while (io->io_Actual); }
      CloseDevice((struct IORequest *)io);
    }
    DeleteIORequest((struct IORequest *)io);
  }
  DeleteMsgPort(mp);
  return out;
}
