
.macro clear_bss
	mov   r0, #0
	ldr   r1, =__bss_start
	ldr   r2, =__bss_end
loop_bss:
	cmp   r1, r2
	strlo r0, [r1], #4
	blo   loop_bss
.endm

.macro copy_data
    ldr   r0, =__data_start
    ldr   r1, =_data_start
    ldr   r2, =__data_end
	cmp r0, r2
	beq done_data
loop_data:
    ldrb  r4, [r0], #1
    strb  r4, [r1], #1
	cmp r0, r2
    bne   loop_data
done_data:
.endm

.macro copy_ramtext
    ldr   r0, =__ramtext_start
    ldr   r1, =_ramtext_start
    ldr   r2, =__ramtext_end
	cmp r0, r2
	beq done_ramtext
loop_ramtext:
    ldrb  r4, [r0], #1
    strb  r4, [r1], #1
	cmp r0, r2
    bne   loop_ramtext
done_ramtext:
.endm
		
	.EQU	ARM_MODE_FIQ,	0x11
	.EQU	ARM_MODE_IRQ,	0x12
	.EQU	ARM_MODE_SVC,	0x13

	.EQU	I_BIT,		0x80
	.EQU	F_BIT,		0x40

#define	TOP_OF_RAM	0x4000a000
#define FIQ_STACK_SIZE	1024
#define IRQ_STACK_SIZE	1024
		
.macro init_stacks
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
.endm

.macro call_ctors
	/* call constructor functions */
	ldr	r0, =_ctor_start
	ldr	r1, =_ctor_end
	bl	do_global_ctors
.endm

