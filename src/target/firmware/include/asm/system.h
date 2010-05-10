#ifndef __ASM_ARM_SYSTEM_H
#define __ASM_ARM_SYSTEM_H

/* Generic ARM7TDMI (ARMv4T) synchronisation primitives, mostly
 * taken from Linux kernel source, licensed under GPL */

#define local_irq_save(x)					\
	({							\
		unsigned long temp;				\
		(void) (&temp == &x);				\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ local_irq_save\n"	\
"	orr	%1, %0, #128\n"					\
"	msr	cpsr_c, %1"					\
	: "=r" (x), "=r" (temp)					\
	:							\
	: "memory", "cc");					\
	})
	
/* Save IRQ flags and disable FIQ + IRQ */
#define local_firq_save(x)					\
	({							\
		unsigned long temp;				\
		(void) (&temp == &x);				\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ local_firq_save\n"	\
"	orr	%1, %0, #0xC0\n"				\
"	msr	cpsr_c, %1"					\
	: "=r" (x), "=r" (temp)					\
	:							\
	: "memory", "cc");					\
	})

/*
 * Enable IRQs
 */
#define local_irq_enable()					\
	({							\
		unsigned long temp;				\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ local_irq_enable\n"	\
"	bic	%0, %0, #128\n"					\
"	msr	cpsr_c, %0"					\
	: "=r" (temp)						\
	:							\
	: "memory", "cc");					\
	})

/*
 * Disable IRQs
 */
#define local_irq_disable()					\
	({							\
		unsigned long temp;				\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ local_irq_disable\n"	\
"	orr	%0, %0, #128\n"					\
"	msr	cpsr_c, %0"					\
	: "=r" (temp)						\
	:							\
	: "memory", "cc");					\
	})

/*
 * Enable FIQs
 */
#define local_fiq_enable()					\
	({							\
		unsigned long temp;				\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ stf\n"		\
"	bic	%0, %0, #64\n"					\
"	msr	cpsr_c, %0"					\
	: "=r" (temp)						\
	:							\
	: "memory", "cc");					\
	})

/*
 * Disable FIQs
 */
#define local_fiq_disable()					\
	({							\
		unsigned long temp;				\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ clf\n"		\
"	orr	%0, %0, #64\n"					\
"	msr	cpsr_c, %0"					\
	: "=r" (temp)						\
	:							\
	: "memory", "cc");					\
	})

/*
 * Save the current interrupt enable state.
 */
#define local_save_flags(x)					\
	({							\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ local_save_flags"	\
	: "=r" (x) : : "memory", "cc");				\
	})

/*
 * restore saved IRQ & FIQ state
 */
#define local_irq_restore(x)					\
	__asm__ __volatile__(					\
	"msr	cpsr_c, %0		@ local_irq_restore\n"	\
	:							\
	: "r" (x)						\
	: "memory", "cc")

#define irqs_disabled()			\
({					\
	unsigned long flags;		\
	local_save_flags(flags);	\
	(int)(flags & PSR_I_BIT);	\
})

#define __asmeq(x, y)  ".ifnc " x "," y " ; .err ; .endif\n\t"

#endif
