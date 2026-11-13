	.arch armv8-a
	.arch armv8.5-a+crc
	.arch_extension crc
	.text
_f:
	add	w0, w0, 1
	ret
	.ident	"GCC: (GNU) 10.5.0"
	.subsections_via_symbols
