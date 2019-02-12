	.text
	.code	32
	.globl	memcmp
memcmp:
	cmp	r2, #0
	beq	zero_len_out
	mov	r3, r0
1:	ldrb	r0, [r3], #1
	ldrb	r12, [r1], #1
	subs	r0, r0, r12
	bxne	lr
	subs	r2, r2, #1
	bne	1b
	bx	lr

zero_len_out:
	mov	r0, #0
	bx	lr
