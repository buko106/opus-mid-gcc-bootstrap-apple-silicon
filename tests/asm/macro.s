.macro	STARTFN name
	.text
	.balign	16
	.private_extern	_\name
_\name:
	.cfi_startproc
	hint	34
.endm
.macro	ENDFN name
	.cfi_endproc
.endm
.macro	JUMP_IF_NOT_LSE label
	adrp	x16, ___aarch64_have_lse_atomics@PAGE
	ldrb	w16, [x16, ___aarch64_have_lse_atomics@PAGEOFF]
	cbz	w16, \label
.endm
.macro	PAIR a, b=7
	.long	\a, \b
	.long	\@
L\a\()_x:
.endm
	STARTFN	__aarch64_cas4_acq
	JUMP_IF_NOT_LSE	8f
	.inst	0x08a07c41 + 0x80000000 + 0x400000
	ret
8:	mov	w16, w0
	ret
	ENDFN	__aarch64_cas4_acq
	STARTFN	other
	ret
	ENDFN	other
	.data
	PAIR	1
	PAIR	2, 3
	PAIR	4 5
