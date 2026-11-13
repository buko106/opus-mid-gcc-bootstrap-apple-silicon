	.text
	.globl _f
	.cfi_startproc
_f:
	ret
	.cfi_endproc
// EXPECT: non-private labels cannot appear between
