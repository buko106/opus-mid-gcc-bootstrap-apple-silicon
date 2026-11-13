	ldxp	x0, w1, [x2]
	stxp	x1, x2, x3, [x4]
	ldaxp	x0, x1, [x2, #8]
	stlxp	w1, x2, [x4]
// EXPECT: invalid operand for instruction
