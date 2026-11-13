.macro M a
	.long \a
.endm
	M 1, 2
// EXPECT: too many positional arguments
