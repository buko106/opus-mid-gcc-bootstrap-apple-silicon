	.text
_f:
	uaddlp v0.8h,v0.16b
	uaddlp	v1.4h, v2.8b
	uaddlp	v1.2s, v2.4h
	uaddlp	v1.4s, v2.8h
	uaddlp	v1.1d, v2.2s
	uaddlp	v1.2d, v2.4s
	saddlp	v3.8h, v4.16b
	uadalp	v5.4s, v6.8h
	sadalp	v7.2d, v8.4s
	ushl	v0.8h, v0.8h, v16.8h
	ushl	v1.16b, v2.16b, v3.16b
	ushl	v1.2d, v2.2d, v3.2d
	ushl	d0, d1, d2
	sshl	v4.4s, v5.4s, v6.4s
	sshl	d7, d8, d9
	ret
