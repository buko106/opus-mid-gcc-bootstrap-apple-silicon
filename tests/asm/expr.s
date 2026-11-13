	.text
	.globl	"_quoted sym"
"_quoted sym":
	ret
_f:
	nop
	.data
_d:
	.byte	'\n', '\t', '\r', '\0', '\\', 'a', '\b', '\f', '\q', ';', 'b'
	.byte	1 + 2 * 3, (1 + 2) * 3, 10 - 2 - 3, 7 / 2, 7 % 4
	.byte	3 <> 4, 3 != 3, 3 <= 4, 4 >= 5, 2 < 3, 3 > 2, 3 == 3
	.byte	1 && 0, 1 || 0, 6 & 3, 6 | 1, 6 ^ 3, ~1, !0, !5, +4, -(-3)
	.byte	0b101, 0B11, 0x1f, 017, 1 << 3, 64 >> 2, -1 >> 60
	.byte	1 + 2 * 3 - 4, 1 | 2 & 3, 2 * 3 + 4 * 5
	.long	"_quoted sym" - _f
	.long	_f - "_quoted sym" + 4
	.quad	"_quoted sym"
	.long	_e - (_f - _f) - _d
_e:
	.ascii	"\b\f\x41\x4a\101\7"
	.asciz	"x\"y"
	.string	"str"
	.file	"foo.c"
	.ident	"GCC: test"
	.byte 9 /* another */
	.byte /* inline */ 10
	.p2align 2
	.quad	_f + 1 + 2 - 1
	.4byte	1
	.8byte	2
	.2byte	4
	.hword	5
	.value	6
	.int	7
	.word	8
	.xword	9
	.dword	10
	.single	1.25
	.skip	3
	.balign	8
	.sleb128 _e - _d, -64, 63, 64
	.uleb128 _e - _d, 127, 128
