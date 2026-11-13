	.zerofill __DATA,__bss
	.section __DATA,__bss,zerofill
	.long 1
// EXPECT: cannot have initialized data in a zerofill section
