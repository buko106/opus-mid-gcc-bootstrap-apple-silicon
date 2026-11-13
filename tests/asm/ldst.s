	.text
_f:
	ldr	x0, [x1]
	ldr	x0, [x1, #8]
	ldr	x0, [x1, #-8]
	ldr	x0, [x1, #32760]
	ldr	w0, [x1, #3]
	ldr	x0, [x1, #8]!
	ldr	x0, [x1], #8
	ldr	x0, [x1, x2]
	ldr	x0, [x1, x2, lsl #3]
	ldr	w0, [x1, w2, sxtw]
	ldr	w0, [x1, w2, uxtw #2]
	ldr	x0, [sp, #16]
	str	x0, [sp, #-16]!
	str	w0, [x1, #4]
	strb	w0, [x1, #1]
	strh	w0, [x1, #2]
	ldrb	w0, [x1, x2]
	ldrh	w0, [x1, #6]
	ldrsb	x0, [x1]
	ldrsb	w0, [x1, #1]
	ldrsh	x0, [x1, #2]
	ldrsh	w0, [x1, x2, lsl #1]
	ldrsw	x0, [x1, #4]
	ldrsw	x0, [x1, x2, lsl #2]
	ldur	x0, [x1, #-1]
	stur	w0, [x1, #3]
	ldurb	w0, [x1, #-1]
	ldursw	x0, [x1, #-4]
	ldrb	w0, [x1, x2, lsl #0]
	ldp	x29, x30, [sp], #16
	stp	x29, x30, [sp, #-32]!
	stp	x19, x20, [sp, #16]
	ldp	w0, w1, [x2, #8]
	ldpsw	x0, x1, [x2]
	stp	d8, d9, [sp, #-16]!
	ldp	q0, q1, [x0, #32]
	ldr	d0, [x1, #8]
	str	s0, [x1, #4]
	ldr	q0, [x1, #16]
	str	b0, [x1]
	ldr	h0, [x1, #2]
	ldr	d1, [x1, x2, lsl #3]
	str	q1, [sp, #-16]!
	ldur	d0, [x0, #-8]
	ldr	x0, L1
	ldr	w0, L1
	ldr	d0, L1
	ldrsw	x0, L1
	prfm	pldl1keep, [x0]
	prfm	pstl2strm, [x0, #64]
	ldxr	x0, [x1]
	ldaxr	w0, [x2]
	stxr	w3, x0, [x1]
	stlxr	w3, w0, [x1]
	ldar	x0, [x1]
	stlr	w0, [x1]
	ldaxrb	w0, [x1]
	stlxrh	w2, w0, [x1]
	ldnp	x0, x1, [x2, #16]
	stnp	q0, q1, [x2]
L1:
	.quad	0
