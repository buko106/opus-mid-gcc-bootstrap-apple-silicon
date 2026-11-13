// EXPECT: invalid operand for instruction
	.text
	add	x0, x1
	add	x0, w1, x2
	add	w0, w1, #4096, lsl #3
	add	x0, x1, #-5000000
	adds	x0, x1, v0
	sub	sp, w1, #1
	cmp	x0
	cmn	w0, x1
	and	x0, x1, #0
	and	w0, w1, #0x12345
	orr	x0, x1, w2
	eor	w0, w1, x2, lsl #2
	mov	x0, #0x123456789
	mov	w0, x1
	mov	x0, v1.s[9]
	mov	v0.4s, x1
	movz	x0, #1, lsl #7
	movk	w0, #1, lsl #32
	movn	x0
	lsl	x0, x1, #64
	lsr	w0, w1, #32
	asr	x0, w1, #3
	ror	w0, w1, x2
	ubfx	x0, x1, #60, #8
	sbfiz	w0, w1, #31, #4
	bfi	x0, x1
	extr	x0, x1, w2, #3
	mul	x0, x1, w2
	madd	x0, x1, x2
	smull	x0, x1, x2
	umulh	w0, w1, w2
	sdiv	x0, w1, x2
	csel	x0, x1, w2, eq
	csel	x0, x1, x2, zz
	cset	x0
	ccmp	x0, x1, #16, eq
	ccmp	x0, #32, #0, ne
	clz	x0, w1
	rev	w0, x1
	cls	v0, x1
	bl
	cbz	v0, 1f
	tbz	x0, #64, 1f
	tbnz	w0, #32, 1f
	b.eq
	br	w0
	blr	#1
	ret	w1
	ldr	x0, [w1]
	ldr	x0, [x1, x2, lsl #2]
	ldr	w0, [x1, w2, lsl #3]
	ldr	x0, [x1, w2, uxtx]
	ldrb	w0, [x1, #4096]
	ldrh	x0, [x1]
	ldrsw	w0, [x1]
	ldrsb	v0, [x1]
	str	x0, [x1], x2
	strb	x0, [x1]
	ldp	x0, w1, [x2]
	ldp	x0, x1, [x2, #3]
	ldp	x0, x1, [x2, #1024]
	stp	w0, w1, [x2, #256]
	ldnp	x0, x1, [x2], #8
	ldur	x0, [x1, #256]
	stur	w0, [x1, #-257]
	ldxr	x0, [x1, #8]
	stxr	x0, x1, [x2]
	ldaxr	w0, [w1]
	ldar	x0, [x1, #8]
	ldr	d0, [x1, w2, sxtw #2]
	fmov	s0, x1
	fmov	d0, #0.1
	fmov	x0, s1
	fmov	s0, d1
	fadd	s0, s1, d2
	fmul	d0, d1
	fmadd	s0, s1, s2
	fcmp	s0, d1
	fcmp	d0, #1.0
	fccmp	s0, d1, #0, eq
	fcsel	s0, s1, d2, eq
	fcvt	s0, s1
	fcvtzs	w0, v1.4s
	scvtf	s0, v1
	fabs	s0, d1
	fsqrt	d0, s1
	frintx	s0, d1
	st1	{v0.4s}, [w0]
	movi	v0.4s, #256
	movi	v0.2d, #0x123
	tbl	v0.16b, {v1.16b}, v2.4s
	shl	v0.4s, v1.4s, #32
	ushr	v0.2d, v1.2d, #0
	sshr	d0, d1, #65
	adr	x0
	adrp	w0, 1f
	prfm	nosuchop, [x0]
	dmb	nosuch
	dsb	#16
	svc	#0x10000
	brk	#-1
	mrs	x0, nosuch_el0
	msr	nosuch, x0
	msr	nzcv, w0
	sys	#0, c7, c5, #0, w0
	udf	#65536
1:
