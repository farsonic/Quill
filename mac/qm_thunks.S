| mac/qm_thunks.S - Quill on the classic Mac OS: the calls between C and the Toolbox that C cannot
| make by itself. GPL-2.0-only, as Quill.
|
|   rp_hook_call       BOOPSI's way into a class dispatcher (a0 the class hook, a2 the object, a1
|                      the message), which Riptide's src/amiga/prefs/mui_util.c uses for DoMethodA
|   qv_dispatch_thunk  the page view's dispatcher (mac/qm_view.c), called that way, into C
|   qm_newgworld ...   32-bit QuickDraw's offscreen calls: one trap, _QDExtensions ($AB1D), with a
|                      selector in D0 (Inside Macintosh: Imaging With QuickDraw, "Offscreen
|                      Graphics Worlds"); Pascal calls, so the arguments are pushed left to right
	.text
	.even

	.globl	rp_hook_call
rp_hook_call:				| ULONG rp_hook_call(struct Hook *h, Object *obj, Msg msg)
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	move.l	48(%sp),%a0
	move.l	52(%sp),%a2
	move.l	56(%sp),%a1
	move.l	8(%a0),%a3
	jsr	(%a3)
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

| long qm_freemem(void): _FreeMem, the free bytes in the current heap (mac/qm_measure.c)
	.globl	qm_freemem
qm_freemem:
	.short	0xA01C
	rts

	.globl	qv_dispatch_thunk
qv_dispatch_thunk:			| into ULONG qv_dispatch(struct IClass *cl, Object *obj, Msg msg)
	move.l	%a1,-(%sp)
	move.l	%a2,-(%sp)
	move.l	%a0,-(%sp)
	jsr	qv_dispatch
	lea	12(%sp),%sp
	rts

| short qm_newgworld(void **gw, short depth, const Rect *bounds): NewGWorld, no colour table or
| device of its own, no flags; the QDErr
	.globl	qm_newgworld
qm_newgworld:
	move.l	%d2,-(%sp)
	clr.w	-(%sp)				| the result
	move.l	10(%sp),-(%sp)			| VAR offscreenGWorld
	move.w	20(%sp),-(%sp)			| pixelDepth (an int: its low word)
	move.l	24(%sp),-(%sp)			| boundsRect (a record: by address)
	clr.l	-(%sp)				| cTable
	clr.l	-(%sp)				| aGDevice
	clr.l	-(%sp)				| flags
	move.l	#0x00160000,%d0
	.short	0xAB1D
	move.w	(%sp)+,%d0
	ext.l	%d0
	move.l	(%sp)+,%d2
	rts

| void qm_disposegworld(void *gw)
	.globl	qm_disposegworld
qm_disposegworld:
	move.l	4(%sp),-(%sp)
	move.l	#0x00040004,%d0
	.short	0xAB1D
	rts

| void **qm_gworldpixmap(void *gw): GetGWorldPixMap (System 7)
	.globl	qm_gworldpixmap
qm_gworldpixmap:
	clr.l	-(%sp)
	move.l	8(%sp),-(%sp)
	move.l	#0x00040017,%d0
	.short	0xAB1D
	move.l	(%sp)+,%d0
	move.l	%d0,%a0
	rts

| int qm_lockpixels(void **pm): LockPixels, 1 when the pixels can be used
	.globl	qm_lockpixels
qm_lockpixels:
	clr.w	-(%sp)
	move.l	6(%sp),-(%sp)
	move.l	#0x00040001,%d0
	.short	0xAB1D
	move.w	(%sp)+,%d0			| the BOOLEAN is the word's high byte
	lsr.w	#8,%d0
	and.l	#1,%d0
	rts

| void qm_unlockpixels(void **pm)
	.globl	qm_unlockpixels
qm_unlockpixels:
	move.l	4(%sp),-(%sp)
	move.l	#0x00040002,%d0
	.short	0xAB1D
	rts

| void *qm_pixbaseaddr(void **pm): GetPixBaseAddr (32-bit clean)
	.globl	qm_pixbaseaddr
qm_pixbaseaddr:
	clr.l	-(%sp)
	move.l	8(%sp),-(%sp)
	move.l	#0x0004000F,%d0
	.short	0xAB1D
	move.l	(%sp)+,%d0
	move.l	%d0,%a0
	rts

| The Apple Event Manager (Inside Macintosh: Interapplication Communication, chapter 4): one trap,
| _Pack8 ($A816), with a selector in D0 whose high byte is the arguments' size in words. Pascal
| calls, the arguments pushed left to right after room for the OSErr. With the result word pushed,
| argument k (from 0) of the C call is at 10 + 4k(sp), and 4 more for each one pushed before it:
| so the k-th push takes 10 + 8k(sp).
| short qm_ae_install(long eventClass, long eventID, void *handler, long refcon):
| AEInstallEventHandler, in the application's table
	.globl	qm_ae_install
qm_ae_install:
	move.l	%d2,-(%sp)
	clr.w	-(%sp)
	move.l	10(%sp),-(%sp)			| theAEEventClass
	move.l	18(%sp),-(%sp)			| theAEEventID
	move.l	26(%sp),-(%sp)			| handler (a procedure pointer is the 68k's UPP)
	move.l	34(%sp),-(%sp)			| handlerRefcon
	clr.w	-(%sp)				| isSysHandler: FALSE
	move.l	#0x091F,%d0
	.short	0xA816
	move.w	(%sp)+,%d0
	ext.l	%d0
	move.l	(%sp)+,%d2
	rts

| short qm_ae_process(const EventRecord *ev): AEProcessAppleEvent
	.globl	qm_ae_process
qm_ae_process:
	move.l	%d2,-(%sp)
	clr.w	-(%sp)
	move.l	10(%sp),-(%sp)
	move.l	#0x021B,%d0
	.short	0xA816
	move.w	(%sp)+,%d0
	ext.l	%d0
	move.l	(%sp)+,%d2
	rts

| short qm_ae_getparamdesc(const AppleEvent *ev, long keyword, long desiredType, AEDesc *result)
	.globl	qm_ae_getparamdesc
qm_ae_getparamdesc:
	move.l	%d2,-(%sp)
	clr.w	-(%sp)
	move.l	10(%sp),-(%sp)
	move.l	18(%sp),-(%sp)
	move.l	26(%sp),-(%sp)
	move.l	34(%sp),-(%sp)
	move.l	#0x0812,%d0
	.short	0xA816
	move.w	(%sp)+,%d0
	ext.l	%d0
	move.l	(%sp)+,%d2
	rts

| short qm_ae_countitems(const AEDescList *list, long *count)
	.globl	qm_ae_countitems
qm_ae_countitems:
	move.l	%d2,-(%sp)
	clr.w	-(%sp)
	move.l	10(%sp),-(%sp)
	move.l	18(%sp),-(%sp)
	move.l	#0x0407,%d0
	.short	0xA816
	move.w	(%sp)+,%d0
	ext.l	%d0
	move.l	(%sp)+,%d2
	rts

| short qm_ae_getnthptr(const AEDescList *list, long index, long desiredType, long *keyword,
|                       long *typeCode, void *data, long maxSize, long *actualSize)
	.globl	qm_ae_getnthptr
qm_ae_getnthptr:
	move.l	%d2,-(%sp)
	clr.w	-(%sp)
	move.l	10(%sp),-(%sp)
	move.l	18(%sp),-(%sp)
	move.l	26(%sp),-(%sp)
	move.l	34(%sp),-(%sp)
	move.l	42(%sp),-(%sp)
	move.l	50(%sp),-(%sp)
	move.l	58(%sp),-(%sp)
	move.l	66(%sp),-(%sp)
	move.l	#0x100A,%d0
	.short	0xA816
	move.w	(%sp)+,%d0
	ext.l	%d0
	move.l	(%sp)+,%d2
	rts

| short qm_ae_disposedesc(AEDesc *desc)
	.globl	qm_ae_disposedesc
qm_ae_disposedesc:
	move.l	%d2,-(%sp)
	clr.w	-(%sp)
	move.l	10(%sp),-(%sp)
	move.l	#0x0204,%d0
	.short	0xA816
	move.w	(%sp)+,%d0
	ext.l	%d0
	move.l	(%sp)+,%d2
	rts

| qm_ae_handler: the handler the Apple Event Manager calls, pascal OSErr (const AppleEvent *event,
| AppleEvent *reply, long refcon), into C's short qm_ae_event(const AppleEvent *event, long refcon).
| The arguments are popped here and the result left in the room above them.
	.globl	qm_ae_handler
qm_ae_handler:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)		| 44 bytes: refcon at 48(sp), reply 52, event 56
	move.l	48(%sp),-(%sp)			| refcon
	move.l	60(%sp),-(%sp)			| event
	jsr	qm_ae_event
	addq.l	#8,%sp
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	move.l	(%sp)+,%a0
	lea	12(%sp),%sp
	move.w	%d0,(%sp)
	jmp	(%a0)

| qm_cdispatch_thunk: BOOPSI's way into a class of Quill's Amiga files (amiga/ribbon.c, panes.c,
| backstage.c, compiled for the Mac by the experimental ribbon tree): a0 the class (its hook), a2 the
| object, a1 the message; the C dispatcher is in the hook's h_SubEntry (mac/qm_ashim.c qm_create_class)
	.globl	qm_cdispatch_thunk
qm_cdispatch_thunk:
	move.l	%a1,-(%sp)
	move.l	%a2,-(%sp)
	move.l	%a0,-(%sp)
	jsr	qm_cdispatch
	lea	12(%sp),%sp
	rts
