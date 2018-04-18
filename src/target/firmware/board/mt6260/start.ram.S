
.section .text.start

#include "macros.S"
		
.globl _start
_start:
	/* clear bss section */
	clear_bss

	/* initialize all stacks */
	init_stacks

	/* call constructors */
	call_ctors

	/* jump to main */
	ldr	pc, _jump_main

	/* endless loop at end of program */
_loop:
	b	_loop
	b	_start

_jump_main:
	.word main
