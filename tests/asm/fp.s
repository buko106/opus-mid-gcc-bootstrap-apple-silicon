	.text
_f:
	fadd	d0, d1, d2
	fsub	s0, s1, s2
	fmul	d0, d1, d2
	fdiv	s0, s1, s2
	fmax	d0, d1, d2
	fminnm	s0, s1, s2
	fnmul	d0, d1, d2
	fabs	d0, d1
	fneg	s0, s1
	fsqrt	d0, d1
	frintz	d0, d1
	frinta	s0, s1
	frintm	d0, d1
	frintx	d0, d1
	fmadd	d0, d1, d2, d3
	fmsub	s0, s1, s2, s3
	fnmadd	d0, d1, d2, d3
	fnmsub	d0, d1, d2, d3
	fcmp	d0, d1
	fcmp	s0, #0.0
	fcmpe	d0, d1
	fcmpe	d0, #0.0
	fccmp	d0, d1, #0, eq
	fcsel	d0, d1, d2, gt
	fmov	d0, d1
	fmov	s0, w1
	fmov	w0, s1
	fmov	x0, d1
	fmov	d0, x1
	fmov	d0, #1.0
	fmov	s0, #-2.5
	fmov	d0, #0.5
	fmov	d0, #31.0
	fmov	x0, v1.d[1]
	fmov	v1.d[1], x0
	fcvt	d0, s1
	fcvt	s0, d1
	fcvt	h0, s1
	fcvt	s0, h1
	fcvtzs	w0, d1
	fcvtzs	x0, s1
	fcvtzu	x0, d1
	fcvtzu	w0, s1
	scvtf	d0, w1
	scvtf	s0, x1
	ucvtf	d0, x1
	ucvtf	s0, w1
	fcvtas	x0, d1
	fcvtms	w0, d0
	fcvtps	x0, d0
	fcvtns	w0, s0
	fcvtzs	w0, d1, #4
	scvtf	d0, x1, #10
	movi	v0.2d, #0
	movi	v0.16b, #0xff
	movi	v0.4s, #1
	movi	d0, #0
	movi	v0.8h, #2, lsl #8
	dup	v0.4s, w1
	dup	v0.2d, x1
	dup	v0.16b, w0
	mov	w0, v1.s[1]
	mov	x0, v1.d[0]
	umov	w0, v1.b[3]
	umov	w0, v1.h[2]
	mov	v0.s[1], w0
	mov	v0.d[1], x0
	ins	v0.b[2], w1
	mov	v0.16b, v1.16b
	mov	v0.8b, v1.8b
	cnt	v0.8b, v0.8b
	addv	b0, v0.8b
	uaddlv	h0, v0.8b
	add	v0.4s, v1.4s, v2.4s
	add	v0.2d, v1.2d, v2.2d
	and	v0.16b, v1.16b, v2.16b
	orr	v0.16b, v1.16b, v2.16b
	eor	v0.8b, v1.8b, v2.8b
	bic	v0.16b, v1.16b, v2.16b
	fadd	v0.2d, v1.2d, v2.2d
	fmul	v0.4s, v1.4s, v2.4s
	ld1	{v0.16b}, [x0]
	st1	{v0.16b, v1.16b}, [x1]
	ld1	{v0.4s}, [x0], #16
	addp	d0, v0.2d
	umov	w0, v0.b[0]
	smov	x0, v1.h[1]
	ext	v0.16b, v1.16b, v2.16b, #8
	zip1	v0.4s, v1.4s, v2.4s
	xtn	v0.8b, v1.8h
	uxtl	v0.8h, v1.8b
	cmeq	v0.16b, v1.16b, v2.16b
	umaxv	b0, v1.16b
