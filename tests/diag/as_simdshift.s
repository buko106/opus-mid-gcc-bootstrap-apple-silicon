	shl	d0, d1, #64
	ushr	v0.4s, v1.4s, #0
	sshr	s0, s1, #3
	shl	v0.4s, v1.2s, #1
	sli	v0.1d, v1.1d, #1
// EXPECT: invalid operand for instruction
