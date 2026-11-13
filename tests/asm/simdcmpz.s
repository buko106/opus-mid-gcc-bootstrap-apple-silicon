	.text
_f:
	cmge	d0, d0, #0
	cmgt	d1, d2, #0
	cmeq	d3, d4, #0
	cmle	d5, d6, #0
	cmlt	d7, d8, #0
	cmge	v0.4s, v1.4s, #0
	cmgt	v0.16b, v1.16b, #0
	cmeq	v0.2d, v1.2d, #0
	cmle	v0.4h, v1.4h, #0
	cmlt	v0.8h, v1.8h, #0
	cmeq	v0.4s, v1.4s, v2.4s
	neg	d0, d0
	neg	v1.4s, v2.4s
	neg	v1.2d, v2.2d
	abs	d3, d4
	abs	v5.8b, v6.8b
	neg	x0, x1
	neg	w2, w3, lsl #3
	ret
