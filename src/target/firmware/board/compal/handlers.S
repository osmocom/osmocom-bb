
		.EQU	I_BIT,		0x80
		.EQU	F_BIT,		0x40
		
.section .text

/* handler for all kinds of aborts */
.global handle_abort
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
.global irq_entry
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
.global fiq_entry
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
