void f(void) { asm("nop" : : ); }
// EXPECT: inline assembly with operands is not supported
