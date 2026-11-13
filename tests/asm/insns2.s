	.text
_g:
	sbfm	x0, x1, #3, #7
	bfm	w0, w1, #4, #9
	ubfm	x2, x3, #60, #63
	rev16	v0.16b, v1.16b
	rev32	v0.8b, v1.8b
	rbit	v2.16b, v3.16b
	clz	v4.4s, v5.4s
	mvn	v0.16b, v1.16b
	ld1	{v0.16b, v1.16b, v2.16b}, [x0]
	ld1	{v0.4s, v1.4s, v2.4s, v3.4s}, [x1]
	ld1	{v0.2d}, [x0], x2
	st1	{v0.8b}, [x3], #8
	prfm	pldl1keep, L2
	prfum	pldl2strm, [x0, #-8]
	prfm	#5, [x1]
	udf	#1
	fabs	v0.4s, v1.4s
	fneg	v0.2d, v1.2d
	fsqrt	v0.4s, v1.4s
	scvtf	d0, d1
	ucvtf	s0, s1
	fcvtzs	d0, d1
	fcvtzu	s2, s3
	mov	v0.s[1], v1.s[3]
	mov	v2.b[0], v3.b[15]
	dup	v0.4s, v1.s[2]
	dup	v0.8h, v1.h[7]
	movi	v1.4s, #255, msl #16
	movi	v2.2d, #0xff00ff00ff00ff00
	movi	d3, #0xffffffff00000000
	movi	v4.4h, #3
	stp	s0, s1, [sp, #-8]!
	ldp	d0, d1, [x0], #16
	ldnp	d0, d1, [x0]
	stnp	s0, s1, [x0, #8]
	fmov	s0, #1.0e1
	fmov	d1, #-0.125
	fmov	h0, w1
	fcmp	h0, h1
	fadd	h0, h1, h2
	ldr	x0, [x1, w2, sxtw #3]
	ldr	w0, [x1, x2, sxtx]
	ldrb	w0, [x1, w2, uxtw]
	ldrh	w0, [x1, x2, lsl #1]
	str	q0, [x0, x1, lsl #4]
	ldr	b0, [x0, x1]
	ldr	q1, L2
	ldr	s1, L2
	dmb	#11
	dmb	oshld
	dsb	nsh
	hlt	#5
	msr	nzcv, x3
	mrs	x4, cntvct_el0
	mrs	x5, ctr_el0
	mrs	x6, dczid_el0
	mrs	x7, tpidr_el0
	xtn	v0.4h, v1.4s
	sxtl	v0.2d, v1.2s
	ushll	v0.8h, v1.8b, #2
	sshll	v0.4s, v1.4h, #3
	uzp1	v0.8h, v1.8h, v2.8h
	trn2	v0.2d, v1.2d, v2.2d
	cmhi	v0.4s, v1.4s, v2.4s
	cmge	v0.8b, v1.8b, v2.8b
	cmhs	v0.2d, v1.2d, v2.2d
	cmgt	v0.16b, v1.16b, v2.16b
	uminv	h0, v1.8h
	orn	v0.16b, v1.16b, v2.16b
	sub	v0.4s, v1.4s, v2.4s
	fsub	v0.4s, v1.4s, v2.4s
	fdiv	v0.2d, v1.2d, v2.2d
	mov	x0, #0xffff
	mov	x1, #0x12340000
	mov	w2, #0xffff0000
	mov	x3, #-65536
	mov	x4, #0x5555555555555555
	mov	w5, wzr
	mov	xzr, x1
	add	w0, w1, w2, uxtb #1
	add	x0, x1, w2, uxth #2
	sub	sp, sp, x1, lsl #3
	add	x0, sp, #0
	adds	w0, w1, #4095, lsl #12
	cmp	x0, w1, sxtw
	cmn	w0, #4096
	sub	x0, x1, #4096
	tst	x0, x1, lsl #2
	ands	x0, x1, x2, asr #3
	bics	w0, w1, w2
	eor	w0, w1, #0x80000001
	orr	x0, xzr, #0x1000000010000000
	lslv	w0, w1, w2
	lsrv	x0, x1, x2
	rorv	w0, w1, w2
	asrv	x0, x1, x2
	ror	x0, x1, x2
	cinv	w0, w1, ge
	smsubl	x0, w1, w2, x3
	umsubl	x0, w1, w2, x3
	smnegl	x0, w1, w2
	umnegl	x0, w1, w2
	umaddl	x0, w1, w2, x3
	adcs	w0, w1, w2
	sbc	x0, x1, x2
	ngcs	w0, w1
	ccmn	w0, w1, #15, vs
	csetm	w0, vc
	fccmpe	s0, s1, #8, pl
	fnmul	s0, s1, s2
	fmaxnm	d0, d1, d2
	fmin	s0, s1, s2
	frintn	d0, d1
	frintp	s0, s1
	frinti	d0, d1
	fcvtnu	w0, d1
	fcvtau	x0, s1
	fcvtpu	w0, d1
	fcvtmu	x0, s1
	fcvtzu	x0, d1, #3
	ucvtf	s0, w1, #5
	ldaxrh	w0, [x1]
	ldxrb	w0, [x1]
	stxrb	w2, w0, [x1]
	stlxrb	w2, w0, [x1]
	stxrh	w2, w0, [x1]
	ldarb	w0, [x1]
	ldarh	w0, [x1]
	stlrb	w0, [x1]
	stlrh	w0, [x1]
	ldxr	w0, [x1, #0]
	ret	x2
L2:
	.quad	0, 0
