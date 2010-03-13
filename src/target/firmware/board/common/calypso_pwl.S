
/* Calypso PWL driver */

#define ASIC_CONF_REG	0xfffef008
#define BA_PWL		0xfffe8000

.globl pwl_init
pwl_init:	ldr	r1, =ASIC_CONF_REG
		ldr	r2, [r1]
		orr	r2, r2, #0x10		@ set light output to PWL
		str	r2, [r1]
		ldr	r1, =BA_PWL
		mov	r0, #1
		strb	r0, [r1, #1]		@ enable clock of PWL unut
		mov	pc, lr

.globl pwl_set_level
pwl_set_level:	ldr	r1, =BA_PWL
		strb	r0, [r1]
		mov	pc, lr

