/* uart routines for early assembly code */

#define BA_UART_MODEM	0xFFFF5800

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

.section .text

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

.globl puts_asm
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

.globl memdump_asm
memdump_asm:	mov	r12, r0
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
