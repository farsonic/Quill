; hook.s - an entry for hooks that realigns the stack before calling C.
;
; Zune (AROS's MUI) can call hooks with the stack pointer two bytes off a long-word boundary;
; vbcc's varargs (printf, sprintf, used throughout) assume an aligned stack and read garbage.
; rexx_entry takes the hook's registers (a0 hook, a2 object, a1 message), aligns a7 down to a
; multiple of 4 and calls rexx_dispatch(hook, object, message) with stack arguments.

	section	code,code
	xdef	_rexx_entry
	xref	_rexx_dispatch
_rexx_entry:
	move.l	a7,d0
	subq.l	#4,a7
	move.l	a7,d1
	and.b	#$fc,d1
	move.l	d1,a7
	move.l	d0,-(a7)
	move.l	a1,-(a7)
	move.l	a2,-(a7)
	move.l	a0,-(a7)
	jsr	_rexx_dispatch
	lea	12(a7),a7
	move.l	(a7),a7
	rts
