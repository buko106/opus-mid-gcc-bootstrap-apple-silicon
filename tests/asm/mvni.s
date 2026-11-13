	.text
_f:
	mvni	v0.4h, 0x84, lsl 8
	mvni	v1.8h, #3
	mvni	v2.2s, #255, lsl #24
	mvni	v3.4s, #1, msl #8
	mvni	v4.4s, #7, msl #16
	mvni	v5.2s, #9, lsl #8
	ret
