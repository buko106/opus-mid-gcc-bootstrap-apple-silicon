	cmge	d0, d1, #1
	cmeq	s0, s1, #0
	neg	s0, s1
	abs	v0.1d, v1.1d
	neg	v0.4s, v1.2s
// EXPECT: invalid operand for instruction
