#include <delay.h>

#define CALYPSO_CLK		52000000
#define CYCLES_PER_LOOP		4
#define COUNTS_PER_MS		(CALYPSO_CLK / CYCLES_PER_LOOP) / 1000
#define COUNTS_PER_US		COUNTS_PER_MS/1000

void delay_us(unsigned int us)
{
	unsigned int counts = COUNTS_PER_US * us;

	asm volatile
	(
		"mov r3, %[counts]\n\t"
		"usloop:\n\t"
			"subs r3, #1\n\t"
			"bne usloop\n\t"
		: /* we have no output, list empty */
		: [counts] "r" (counts)
		/* r3 and flags are clobbered */
		: "r3", "cc"
	);
}

void delay_ms(unsigned int ms)
{
	unsigned int counts = COUNTS_PER_MS;
	while (ms--) {
		asm volatile
		(
			"mov r3, %[counts]\n\t"
			"msloop:\n\t"
				"subs r3, #1\n\t"
				"bne msloop\n\t"
			: /* we have no output, list empty */
			: [counts] "r" (counts)
			/* r3 and flags are clobbered */
			: "r3", "cc"
		);
	}
}
