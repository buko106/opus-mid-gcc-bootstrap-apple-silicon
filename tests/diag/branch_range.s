	.text
_f:
	cbz x0, L1
	.space 2000000
L1:
	ret
// EXPECT: fixup value out of range
