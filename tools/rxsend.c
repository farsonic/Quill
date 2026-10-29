/*
 * rxsend - send ARexx command messages to a port and print the replies (a test aid, for systems
 * without a REXX interpreter): rxsend PORT "COMMAND args" ["COMMAND" ...]
 */
#include <stdio.h>
#include <string.h>
#include <exec/ports.h>
#include <rexx/storage.h>
#include <rexx/rxslib.h>
#include <proto/exec.h>
#include <proto/rexxsyslib.h>

struct RxsLib *RexxSysBase;

int main(int argc, char **argv)
{
  struct MsgPort *reply, *port;
  int i, k;
  if (argc < 3) { printf("rxsend PORT COMMAND...\n"); return 5; }
  if (!(RexxSysBase = (struct RxsLib *)OpenLibrary((STRPTR)"rexxsyslib.library", 0))) { printf("no rexxsyslib.library\n"); return 20; }
  if (!(reply = CreateMsgPort())) return 20;
  for (i = 2; i < argc; i++) {
    struct RexxMsg *rm = CreateRexxMsg(reply, NULL, (STRPTR)argv[1]);
    if (!rm) break;
    rm->rm_Args[0] = (STRPTR)CreateArgstring((STRPTR)argv[i], strlen(argv[i]));
    rm->rm_Action = RXCOMM | RXFF_RESULT;
    /* the port may not be there yet: try for ten seconds */
    for (k = 0; k < 50; k++) {
      Forbid();
      if ((port = FindPort((STRPTR)argv[1]))) PutMsg(port, &rm->rm_Node);
      Permit();
      if (port) break;
      Delay(10);
    }
    if (!port) { printf("%s: no port %s\n", argv[i], argv[1]); DeleteArgstring((UBYTE *)rm->rm_Args[0]); DeleteRexxMsg(rm); break; }
    WaitPort(reply); GetMsg(reply);
    printf("%s: rc %ld", argv[i], (long)rm->rm_Result1);
    if (!rm->rm_Result1 && rm->rm_Result2) { printf(" result \"%s\"", (char *)rm->rm_Result2); DeleteArgstring((UBYTE *)rm->rm_Result2); }
    printf("\n");
    DeleteArgstring((UBYTE *)rm->rm_Args[0]);
    DeleteRexxMsg(rm);
  }
  DeleteMsgPort(reply);
  CloseLibrary((struct Library *)RexxSysBase);
  return 0;
}
