
	.EQU	ARM_MODE_FIQ,	0x11
	.EQU	ARM_MODE_IRQ,	0x12
	.EQU	ARM_MODE_SVC,	0x13

	.EQU	I_BIT,		0x80
	.EQU	F_BIT,		0x40

#define	TOP_OF_RAM	0x083fff0
#define FIQ_STACK_SIZE	1024
#define IRQ_STACK_SIZE	1024

.section .text.start

.globl _start
_start:
	/* clear bss section */
	.global __bss_start
	.global __bss_end
	mov   r0, #0
	ldr   r1, =__bss_start
	ldr   r2, =__bss_end
2:	cmp   r1, r2
	strlo r0, [r1], #4
	blo   2b

	/* initialize stacks, starting at TOP_OF_RAM */
	ldr	r0, =TOP_OF_RAM

	/* initialize FIQ stack */
	msr	CPSR_c, #ARM_MODE_FIQ | I_BIT | F_BIT
	mov	r13, r0
	sub	r0, r0, #FIQ_STACK_SIZE

	/* initialize IRQ stack */
	msr	CPSR_c, #ARM_MODE_IRQ | I_BIT | F_BIT
	mov	r13, r0
	sub	r0, r0, #IRQ_STACK_SIZE

	/* initialize supervisor stack */
	msr	CPSR_c, #ARM_MODE_SVC | I_BIT | F_BIT
	mov	r13, r0

	/* set backlight to moderate level */
	bl	pwl_init
	mov	r0, #50
	bl	pwl_set_level

	/* test uart output */
	@ldr	r0, =string
	@bl	puts_asm

	/* dump some memory */
	@ldr	r0, =0xfffef000
	@bl	memdump
	@ldr	r0, =0xfffffe00
	@bl	memdump

	/* call constructor functions */
	ldr	r0, _ctor_list
	ldr	r1, _ctor_end
	bl	do_global_ctors

	/* jump to main */
	ldr	pc, _jump_main

	/* endless loop at end of program */
_end:	b	_end
	b	_start

_jump_main:	.word main
_ctor_list:	.word __CTOR_LIST__
_ctor_end:	.word __CTOR_END__

