	uaddlp	v0.8h, v1.8h
	saddlp	v0.2d, v1.2d
	ushl	v0.4s, v1.4s, v2.2s
	sshl	s0, s1, s2
// EXPECT: invalid operand for instruction
