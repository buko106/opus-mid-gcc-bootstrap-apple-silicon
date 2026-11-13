	.text
	.globl	_f
_f:
	.cfi_startproc
	mov	x0, #1
	ret
	.cfi_endproc
	.private_extern _g
	.balign 16
_g:
	.cfi_startproc
	hint	34
	.inst	0x08a07c41 + 0x40000000
	ret
	.cfi_endproc
	.subsections_via_symbols
	.text
	.private_extern _h
_h:
	.cfi_startproc
_local:
	ret
	.cfi_endproc
