
#define BA_UART_MODEM	0xFFFF5800

#

.macro	senduart, rd, rx
	strb	\rd, [\rx, #0]
.endm

.macro	busyuart, rd, rx
1001:
	@busy waiting until THR is empty
	ldrb	\rd, [\rx, #5]		@ read LSR register
	mov	\rd, \rd, lsr #6
	tst	\rd, #1
	beq	1001b
.endm

.macro	loadsp, rd
	ldr	\rd, =BA_UART_MODEM
.endm


	.EQU	ARM_MODE_FIQ,	0x11
	.EQU	ARM_MODE_IRQ,	0x12
	.EQU	ARM_MODE_SVC,	0x13

	.EQU	I_BIT,		0x80
	.EQU	F_BIT,		0x40

#define	TOP_OF_RAM	0x083fff0
#define FIQ_STACK_SIZE	1024
#define IRQ_STACK_SIZE	1024

.section .text._start
.globl _start
_start:
	@ clear bss section
	.global __bss_start
	.global __bss_end
	mov   r0, #0
	ldr   r1, =__bss_start
	ldr   r2, =__bss_end
2:	cmp   r1, r2
	strlo r0, [r1], #4
	blo   2b

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

	@ set backlight to moderate level
	bl	pwl_init
	mov	r0, #50
	bl	pwl_set_level

	@ldr	r0, =string
	@bl	puts_asm

	@ some memory dumps
	@ldr	r0, =0xfffef000
	@bl	memdump
	@ldr	r0, =0xfffffe00
	@bl	memdump

	ldr	pc, _jump_main

	@ endless loop at end of program
_end:	b	_end
	b	_start

_jump_main:	.word main

string:	.word	0x6c6c6548
	.word	0x6f57206f
	.word	0x00646c72

foo:	.word	0xee4c9f63
bar:	.word	0x639f4cee

		.align	2
		.type	phexbuf, #object
phexbuf:	.space	12
		.size	phexubf, . - phexbuf

.globl phex
phex:		adr	r3, phexbuf
		mov	r2, #0
		strb	r2, [r3, r1]
1:		subs	r1, r1, #1
		movmi	r0, r3
		bmi	puts_asm
		and	r2, r0, #15
		mov	r0, r0, lsr #4
		cmp	r2, #10
		addge	r2, r2, #7
		add	r2, r2, #'0'
		strb	r2, [r3, r1]
		b	1b

puts_asm:	loadsp	r3
1:		ldrb	r2, [r0], #1
		teq	r2, #0
		moveq	pc, lr
2:		senduart r2, r3
		busyuart r1, r3
		teq	r2, #'\n'
		moveq	r2, #'\r'
		beq	2b
		teq	r0, #0
		bne	1b
		mov	pc, lr
.globl putchar_asm
putchar_asm:
		mov	r2, r0
		mov	r0, #0
		loadsp	r3
		b	2b

memdump:	mov	r12, r0
		mov	r10, lr
		mov	r11, #0
2:		mov 	r0, r11, lsl #2
		add	r0, r0, r12
		mov	r1, #8
		bl	phex
		mov	r0, #':'
		bl	putchar_asm
1:		mov	r0, #' '
		bl	putchar_asm
		ldr	r0, [r12, r11, lsl #2]
		mov	r1, #8
		bl	phex
		and	r0, r11, #7
		teq	r0, #3
		moveq	r0, #' '
		bleq	putchar_asm
		and	r0, r11, #7
		add	r11, r11, #1
		teq	r0, #7
		bne	1b
		mov	r0, #'\n'
		bl	putchar_asm
		cmp	r11, #64
		blt	2b
		mov	pc, r10


#define ASIC_CONF_REG	0xfffef008
#define BA_PWL		0xfffe8000

pwl_init:	ldr	r1, =ASIC_CONF_REG
		ldr	r2, [r1]
		orr	r2, r2, #0x10		@ set light output to PWL
		str	r2, [r1]
		ldr	r1, =BA_PWL
		mov	r0, #1
		strb	r0, [r1, #1]		@ enable clock of PWL unut
		mov	pc, lr

pwl_set_level:	ldr	r1, =BA_PWL
		strb	r0, [r1]
		mov	pc, lr

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
