	.text
	.globl	_f
	.p2align	2
_f:
	add	x0, x1, x2
	add	w0, w1, w2, lsl #3
	add	x0, sp, x1
	add	sp, sp, #16
	add	x0, x1, #4095
	add	x0, x1, #1, lsl #12
	add	x0, x1, w2, sxtw #2
	add	x0, x1, w2, uxtw
	sub	x0, x1, #-8
	subs	w3, w4, w5
	adds	x3, x4, #12
	cmp	x0, #0
	cmp	w1, w2
	cmn	x3, #1
	cmp	x0, x1, lsl #2
	neg	x0, x1
	negs	w0, w1
	and	x0, x1, #0xff
	and	w0, w1, #0xfffffff0
	orr	x0, x1, x2, lsr #3
	eor	x0, x1, #0x5555555555555555
	ands	w0, w1, #1
	bic	x0, x1, x2
	orn	w0, w1, w2
	eon	x0, x1, x2, ror #5
	tst	x0, #8
	tst	w1, w2
	mvn	x0, x1
	mov	x0, x1
	mov	x0, sp
	mov	sp, x0
	mov	w0, #0
	mov	x0, #-1
	mov	w0, #-1
	mov	x0, #0x10000
	mov	x0, #0xffff0000ffff0000
	mov	w0, #0x7fffffff
	mov	x0, #0xffffffff
	movz	x0, #1, lsl #16
	movk	x0, #0x1234, lsl #48
	movn	w0, #5
	lsl	x0, x1, #3
	lsr	w0, w1, #31
	asr	x0, x1, #63
	ror	w0, w1, #7
	lsl	x0, x1, x2
	asr	w0, w1, w2
	ubfx	x0, x1, #4, #8
	sbfx	w0, w1, #0, #16
	bfi	x0, x1, #8, #4
	ubfiz	w0, w1, #2, #6
	sbfiz	x0, x1, #3, #10
	bfxil	w0, w1, #1, #3
	sxtb	w0, w1
	sxth	x0, w1
	sxtw	x0, w1
	uxtb	w0, w1
	uxth	w0, w1
	extr	x0, x1, x2, #13
	madd	x0, x1, x2, x3
	msub	w0, w1, w2, w3
	mul	x0, x1, x2
	mneg	w0, w1, w2
	smull	x0, w1, w2
	umull	x0, w1, w2
	smaddl	x0, w1, w2, x3
	umulh	x0, x1, x2
	smulh	x0, x1, x2
	sdiv	x0, x1, x2
	udiv	w0, w1, w2
	csel	x0, x1, x2, eq
	csinc	w0, w1, w2, ne
	csinv	x0, x1, x2, lt
	csneg	w0, w1, w2, ge
	cset	w0, hi
	csetm	x0, lo
	cinc	x0, x1, cc
	cneg	w0, w1, mi
	ccmp	x0, x1, #4, ne
	ccmp	w0, #3, #0, eq
	ccmn	x0, #1, #2, le
	clz	x0, x1
	cls	w0, w1
	rbit	x0, x1
	rev	w0, w1
	rev	x0, x1
	rev16	w0, w1
	rev32	x0, x1
	adc	x0, x1, x2
	sbcs	w0, w1, w2
	ngc	x0, x1
1:
	b	1b
	b	2f
	bl	_g
	b.eq	1b
	b.ne	2f
	cbz	x0, 1b
	cbnz	w0, 2f
	tbz	x0, #63, 1b
	tbnz	w1, #3, 2f
2:
	br	x16
	blr	x8
	ret
	ret	x1
	adr	x0, 1b
	adrp	x0, _g@PAGE
	add	x0, x0, _g@PAGEOFF
	adrp	x1, _v@GOTPAGE
	ldr	x1, [x1, _v@GOTPAGEOFF]
	ldr	w2, [x0, _g@PAGEOFF]
	nop
	dmb	ish
	dsb	sy
	isb
	brk	#0x3e8
	svc	#0x80
	mrs	x0, fpcr
	msr	fpcr, x1
	mrs	x0, tpidrro_el0
	yield
	hint	#34
