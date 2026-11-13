	.text
	.globl	_main
	.p2align 2
_main:
LFB0:
	stp	x29, x30, [sp, -16]!
	mov	x29, sp
	adrp	x0, lC0@PAGE
	add	x0, x0, lC0@PAGEOFF;momo
	adrp	x1, LJT@PAGE
	add	x1, x1, LJT@PAGEOFF
	adrp	x2, _tab+16@PAGE
	ldr	x2, [x2, _tab+16@PAGEOFF]
	bl	_helper
	bl	_puts
	ldp	x29, x30, [sp], 16
	ret
LJT:
	.long	LFB0-LJT
	.long	LFE0-LJT
LFE0:
	.p2align 2
_helper:
	b	_main
	.p2align 3,,7
	ret
	.private_extern _hidden
_hidden:
	ret
	.globl _weakf
	.weak_definition _weakf
_weakf:
	ret
	.cstring
lC0:
	.ascii "hello\0"
lC1:
	.asciz "a\tb\"c\\\101"
	.const
	.p2align 3
_tab:
	.quad	_main
	.quad	_helper+8
	.quad	LFE0
	.quad	_ext+4
	.long	_main - _helper
	.long	LFE0 - LFB0
	.quad	lC1
	.byte	1, 2, 255
	.short	0x1234
	.uleb128 LFE0-LFB0
	.sleb128 -129
	.uleb128 300
	.p2align 2
	.float	1.5
	.double	-2.25
	.data
	.globl	_d
_d:
	.long	42
	.space	12
	.fill	3, 2, 7
	.zero	5
	.set	L$set$0,LFE0-LFB0
	.long	L$set$0
	.set	L$set$1,_helper-_main
	.long	L$set$1
	.literal8
	.p2align 3
LC9:
	.double	3.0
	.literal4
LC10:
	.long	7
	.literal16
LC11:
	.quad 1, 2
	.zerofill __DATA,__bss,_bssv,16,4
	.zerofill __DATA,__bss,_bssw,4,2
	.comm	_common1,8,3
	.comm	_common2,100
	.lcomm	_lc,24,3
	.globl	_bssv
	.mod_init_func
	.quad	_helper
	.section __TEXT,__gcc_except_tab
GCC_except_table0:
	.byte	0xff
	.uleb128 LLSDACSE-LLSDACSB
LLSDACSB:
	.uleb128 LFB0-_main
	.uleb128 LFE0-LFB0
LLSDACSE:
	.section __TEXT,__eh_frame,coalesced,no_toc+strip_static_syms+live_support
EH_frame1:
	.set L$set$2,LECIE1-LSCIE1
	.long L$set$2
LSCIE1:
	.long	0
	.byte	0x1
	.ascii "zPR\0"
	.uleb128 0x1
	.sleb128 -8
	.byte	0x1e
	.uleb128 0x6
	.byte	0x9b
	.long	___gxx_personality_v0@GOT-.
	.byte	0x10
	.byte	0xc
	.uleb128 0x1f
	.uleb128 0
	.p2align 3
LECIE1:
LSFDE1:
	.set L$set$3,LEFDE1-LASFDE1
	.long L$set$3
LASFDE1:
	.long	LASFDE1-EH_frame1
	.quad	LFB0-.
	.set L$set$4,LFE0-LFB0
	.quad L$set$4
	.uleb128 0x8
	.quad	GCC_except_table0-.
	.byte	0x4
	.set L$set$5,LJT-LFB0
	.long L$set$5
	.p2align 3
LEFDE1:
	.section __DWARF,__debug_info,regular,debug
Ldebug_info0:
	.long	Ldebug_end - Ldebug_info0
	.quad	LFB0
	.quad	LFE0
	.quad	_main
	.quad	_ext
	.long	Ldebug_abbrev0
	.set L$set$6,Ldebug_abbrev0-Lsection__debug_abbrev
	.long L$set$6
Ldebug_end:
	.section __DWARF,__debug_abbrev,regular,debug
Lsection__debug_abbrev:
Ldebug_abbrev0:
	.byte 1,0
	.section __DWARF,__debug_line,regular,debug
	.long	Ldebug_info0
	.section __DWARF,__debug_aranges,regular,debug
	.quad	LFB0
	.subsections_via_symbols
