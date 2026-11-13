	.text
_f:
	bif	v18.8b, v21.8b, v19.8b
	bit	v0.16b, v1.16b, v2.16b
	bsl	v3.8b, v4.8b, v5.8b
	bif	v31.16b, v30.16b, v29.16b
	ret
