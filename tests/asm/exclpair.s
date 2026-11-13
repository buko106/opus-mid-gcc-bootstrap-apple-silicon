	.text
_f:
	ldxp	x0, x1, [x4]
	ldaxp	x0, x1, [x4]
	ldxp	w2, w3, [sp]
	ldaxp	w5, w6, [x7, #0]
	stxp	w15, x2, x3, [x4]
	stlxp	w15, x2, x3, [x4]
	stxp	w1, w2, w3, [x9]
	stlxp	wzr, w2, w3, [sp]
	ldxp	x0, xzr, [x30]
	ret
