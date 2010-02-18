
#include <stdint.h>
#include <stdio.h>
#include <memory.h>

/* dump a memory range */
void memdump_range(unsigned int *ptr, unsigned int len)
{
	unsigned int *end = ptr + (len/4);
	unsigned int *tmp;

	for (tmp = ptr; tmp < end; tmp += 8) {
		int i;
		printf("%08X: ", (unsigned int) tmp);

		for (i = 0; i < 8; i++)
			printf("%08X %s", *(tmp+i), i == 3 ? " " : "");

		putchar('\n');
	}
}

#define KBIT 1024
#define	MBIT (1024*KBIT)
void dump_mem(void)
{
	puts("Dump 64kBits of internal ROM\n");
	memdump_range((void *)0x03800000, 64*KBIT/8);

	puts("Dump 8Mbits of external flash\n");
	memdump_range((void *)0x00000000, 8*MBIT/8);

	puts("Dump 2Mbits of internal RAM\n");
	memdump_range((void *)0x00800000, 2*MBIT/8);

	puts("Dump 2Mbits of external RAM\n");
	memdump_range((void *)0x01000000, 2*MBIT/8);
}

#define REG_DEV_ID_CODE 	0xfffef000
#define REG_DEV_VER_CODE	0xfffef002
#define REG_DEV_ARMVER_CODE	0xfffffe00
#define REG_cDSP_ID_CODE	0xfffffe02
#define REG_DIE_ID_CODE		0xfffef010

void dump_dev_id(void)
{
	int i;

	printf("Device ID code: 0x%04x\n", readw(REG_DEV_ID_CODE));
	printf("Device Version code: 0x%04x\n", readw(REG_DEV_VER_CODE));
	printf("ARM ID code: 0x%04x\n", readw(REG_DEV_ARMVER_CODE));
	printf("cDSP ID code: 0x%04x\n", readw(REG_cDSP_ID_CODE));
	puts("Die ID code: ");
	for (i = 0; i < 64/8; i += 4)
		printf("%08x", readl(REG_DIE_ID_CODE+i));
	putchar('\n');
}


