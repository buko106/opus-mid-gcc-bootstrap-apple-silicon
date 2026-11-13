	.data
_s:
	.ascii	"a\nb\tc\rd\be\ff\"j\\k\x41m\101n\x7"
	.asciz	"x", "yz"
	.string	"q"
	.p2align 3, 0x55
	.byte	1
	.balign	8, 0xaa
	.byte	2
	.byte	3
	.p2align 4,,3
	.space	5, 0x7
	.skip	2
	.zero	3
	.fill	2, 4, 0x01020304
	.fill	3, 1, 9
	.fill	1, 8, 5
	.2byte	0x1234
	.4byte	0x12345678
	.8byte	0x1122334455667788
	.int	-1
	.hword	7
	.value	8
	.xword	9
	.dword	10
	.single	1.5, -2.0
	.double	0.1
	.float	3
	.sleb128 -1, 63, -64, 1000
	.uleb128 0, 127, 128, 0xffffffff
Lb:
	.long	(Le - Lb) * 2 + 1 - 1
	.long	Le - Lb == 8
	.long	(Le - Lb) / 4 % 3
	.long	(1 << 4) | 1 & 3 ^ 2
	.quad	~0 >> 1
	.long	!0 + (5 > 3) + (5 >= 6) + (1 != 2) + (2 <= 2) + (3 < 1) + (0 && 1) + (0 || 1)
	.long	-(-5)
Le:
	.space	Le - Lb
	.long	'a' + 1
	.quad	0b1011
	.set	sym1, 5
	.equ	sym2, sym1 * 2
	sym3 = sym2 + 1
	.long	sym3
	.globl	_absval
	.set	_absval, 42
	.p2align 2
1:	.long	1b - _s
2:	.long	2f - 1b
2:	.long	0
	.section __DATA,__mydata,regular,no_dead_strip
	.long	3
	.section __TEXT,__stubs2,symbol_stubs,pure_instructions,12
	.section __TEXT,__mylit,4byte_literals
	.long	4
	.section __DATA,__myptr,literal_pointers
	.quad	_s
	.section __DATA,__thr,thread_local_regular
	.long	5
	.section __DATA,__init,mod_init_funcs
	.section __DATA,__interp,interposing
	.section __DATA,__coal,coalesced
	.section __DATA,__vars,thread_local_variables
	.section __DATA,__varp,thread_local_variable_pointers
	.section __DATA,__tinit,thread_local_init_function_pointers
	.section __DATA,__tz,thread_local_zerofill
	.section __TEXT,__self,regular,self_modifying_code+live_support+strip_static_syms+no_toc
	.text
	.globl	_t
_t:
	.weak_reference	_wr
	.lazy_reference	_lr
	.reference	_rf
	.no_dead_strip	_t
	.alt_entry	_t2
_t2:
	.cold	_t3
_t3:
	.weak_def_can_be_hidden _t4
	.globl	_t4
_t4:
	bl	_wr
	ret
	.previous
	.build_version macos, 12, 3 sdk_version 14, 0
	.subsections_via_symbols
