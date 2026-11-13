	.text
_a1: nop
	.p2align 3
_b1: nop
_a2: nop
	.space 4
_b2: nop
_a3: nop
	b _x
	adrp x0, _y@PAGE
_b3: nop
_a4: nop
	.p2align 3, 0
_b4: nop
_a5: nop
	.uleb128 7
_b5: nop
_a6: .long 0
	.space _b1 - _a1
_b6: nop
	.data
	.long _b1 - _a1
	.long _b2 - _a2
	.long _b3 - _a3
	.long _b4 - _a4
	.long _b5 - _a5
	.long _b6 - _a6
	.long _b7 - _a1
_c: .long 0
_d: .long _d - _c
	.long _c - _d
	.long . - _c
	.quad _c - .
	.text
_b7: nop
