
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

	/* jump to main */
	ldr	pc, _jump_main

	/* endless loop at end of program */
_end:	b	_end
	b	_start

_jump_main:	.word main

/* handler for all kinds of aborts */
handle_abort:
		@ print the PC we would jump back to...
		sub	lr, lr, #4		@ we assume to be ARM32

		mov	r0, lr
		mov	r1, #8
		bl phex

		@ print abort message
		mov	r0, #'A'
		bl	putchar_asm
		mov	r0, #'B'
		bl	putchar_asm
		mov	r0, #'O'
		bl	putchar_asm
		mov	r0, #'R'
		bl	putchar_asm
		mov	r0, #'T'
		bl	putchar_asm

		@ disable IRQ and FIQ
		msr	CPSR_c, #I_BIT | F_BIT

0:					@ dead
		b	0b

/* entry point for IRQs */
irq_entry:
		/* Adjust and save LR_irq in IRQ stack */
		sub	lr, lr, #4
		stmfd	sp!, {lr}

		/* Save SPSR for nested interrupt */
		mrs	r14, SPSR
		stmfd	sp!, {r14}

		/* Call the interrupt handler C function */
		stmfd	sp!, {r0-r4, r12}
		bl	irq
		ldmfd	sp!, {r0-r4, r12}

		/* Restore SPSR_irq from IRQ stack */
		ldmia	sp!, {r14}
		msr	SPSR_cxsf, r14

		/* Restore adjusted LR_irq from IRQ stack directly in the PC */
		ldmia	sp!, {pc}^

/* entry point for FIQs */
fiq_entry:
		/* Adjust and save LR_irq in IRQ stack */
		sub	lr, lr, #4
		stmfd	sp!, {lr}

		/* Save SPSR for nested interrupt */
		mrs	r14, SPSR
		stmfd	sp!, {r14}

		/* Call the interrupt handler C function */
		stmfd	sp!, {r0-r4, r12}
		bl	fiq
		ldmfd	sp!, {r0-r4, r12}

		/* Restore SPSR_irq from IRQ stack */
		ldmia	sp!, {r14}
		msr	SPSR_cxsf, r14

		/* Restore adjusted LR_irq from IRQ stack directly in the PC */
		ldmia	sp!, {pc}^

/* Exception Vectors like they are needed for the exception vector
   indirection of the internal boot ROM.  The following section must be liked
   to appear at 0x80'001c */
.section .text.exceptions
_undef_instr:
	b	handle_abort
_sw_interr:
	b	_sw_interr
_prefetch_abort:
	b	handle_abort
_data_abort:
	b	handle_abort
_reserved:
	b	_reserved
_irq:
	b	irq_entry
_fiq:
	b	fiq_entry
