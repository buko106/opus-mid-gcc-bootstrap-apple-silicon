	.text
_f:
	shl	d1, d0, 3
	shl	d2, d3, #63
	shl	v0.4s, v1.4s, #3
	shl	v0.8b, v1.8b, #7
	shl	v0.8h, v1.8h, #0
	shl	v0.2d, v1.2d, #33
	sshr	d0, d1, #64
	sshr	v0.4h, v1.4h, #1
	ushr	d0, d1, #1
	ushr	v0.16b, v1.16b, #8
	ushr	v0.2s, v1.2s, #32
	ssra	v0.4s, v1.4s, #5
	usra	d0, d1, #9
	srshr	v0.2d, v1.2d, #63
	urshr	v0.8h, v1.8h, #15
	sri	v0.8b, v1.8b, #2
	sli	d0, d1, #12
	sli	v0.4s, v1.4s, #31
	ret
