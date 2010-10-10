/* boot loader for Calypso phones */

/* (C) 2010 by Ingo Albrecht <prom@berlin.ccc.de>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <debug.h>
#include <memory.h>
#include <delay.h>
#include <rffe.h>
#include <keypad.h>
#include <board.h>
#include <console.h>
#include <manifest.h>

#include <osmocore/crc16.h>

#include <abb/twl3025.h>
#include <rf/trf6151.h>

#include <comm/sercomm.h>

#include <calypso/clock.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/irq.h>
#include <calypso/misc.h>
#include <calypso/uart.h>
#include <calypso/timer.h>

#include <flash/cfi_flash.h>

#include "protocol.h"

/* MT6223 */
//TODO split to separate drivers and put the register definitions there
#define GPIO_BASE	0x80120000
#define GPIO_REG(n)	(GPIO_BASE + n)
enum gpio_reg {
	/* data direction registers */
	DIR1	= 0x00,
	DIR2	= 0x10,
	DIR3	= 0x20,
	DIR4	= 0x30,
	/* pull-up registers */
	PULLEN1	= 0x40,
	PULLEN2	= 0x50,
	PULLEN3	= 0x60,
	PULLEN4	= 0x70,
	/* data inversion registers */
	DINV1	= 0x80,
	DINV2	= 0x90,
	DINV3	= 0xa0,
	DINV4	= 0xb0,
	/* data output registers */
	DOUT1	= 0xc0,\
	DOUT2	= 0xd0,
	DOUT3	= 0xe0,
	DOUT4	= 0xf0,
	/* data input registers */
	DIN1	= 0x0100,
	DIN2	= 0x0110,
	DIN3	= 0x0120,
	DIN4	= 0x0130,
	/*[...]*/
	BANK	= 0x01c0,

	/* GPIO mode registers */
	MODE1	= 0x0150,
	MODE2	= 0x0160,
	MODE3	= 0x0170,
	MODE4	= 0x0180,
	MODE5	= 0x0190,
	MODE6	= 0x01a0,
	MODE7	= 0x01b0,

};

#define CONFIG_BASE	0x80000000
#define CONFIG_REG(n)	(CONFIG_BASE + n)

enum config_reg {
	HW_VERSION	= 0x00,
	SW_VERSION	= 0x04,
	HW_CODE		= 0x08,

	PDN_CON0 = 0x300,
	PDN_CON1 = 0x304,
	PDN_CON2 = 0x308,
	PDN_CON3 = 0x30c,
	PDN_CON4 = 0x330,
	PDN_SET0 = 0x310,
	PDN_SET1 = 0x314,
	PDN_SET2 = 0x318,
	PDN_SET3 = 0x31c,
	PDN_CLR0 = 0x320,
	PDN_CLR1 = 0x324,
	PDN_CLR2 = 0x328,
	PDN_CLR3 = 0x32c,
	PDN_CLR4 = 0x338,

	APB_CON		= 0x404,
	AHB_CON		= 0x500,
};

#define MAGIC_POWERKEY1		0xa357
#define MAGIC_POWERKEY2		0x67d2
#define RTC_BASE		0x80210000
#define RGU_BASE		0x80040000

#define RTC_REG(n)	(RTC_BASE + n)

enum rtc_reg {
	BBPU		= 0x00,
	POWERKEY1	= 0x50,
	POWERKEY2	= 0x54,
};

/* Main Program */
const char *hr =
    "======================================================================\n";

static void key_handler(enum key_codes code, enum key_states state);
static void cmd_handler(uint8_t dlci, struct msgb *msg);

int flag = 0;

static void flush_uart(void)
{
	unsigned i;
	for (i = 0; i < 500; i++) {
		uart_poll(SERCOMM_UART_NR);
		delay_ms(1);
	}
}

static void device_poweroff(void)
{
	flush_uart();
	writew(0x430a, RTC_REG(BBPU));
}

static void device_reset(void)
{
	flush_uart();
//	wdog_reset();
}

static void device_enter_loader(unsigned char bootrom)
{
	flush_uart();
	delay_ms(2000);
//	calypso_bootrom(bootrom);
	void (*entry)( void ) = (void (*)(void))0;
	entry();
}

static void device_jump(void *entry)
{
	flush_uart();

	void (*f) (void) = (void (*)(void))entry;
	f();
}

static void loader_send_simple(struct msgb *msg, uint8_t dlci, uint8_t command)
{
	msgb_put_u8(msg, command);
	sercomm_sendmsg(dlci, msg);
}

extern unsigned char _start;

static void loader_send_init(uint8_t dlci)
{
	struct msgb *msg = sercomm_alloc_msgb(9);
	msgb_put_u8(msg, LOADER_INIT);
	msgb_put_u32(msg, 0);
	msgb_put_u32(msg, &_start);
	sercomm_sendmsg(dlci, msg);
}

flash_t the_flash;

extern void putchar_asm(uint32_t c);

static const uint8_t phone_ack[] = { 0x1b, 0xf6, 0x02, 0x00, 0x41, 0x03, 0x42 };

int main(void)
{
	/* powerup the baseband */
	writew(MAGIC_POWERKEY1, RTC_REG(POWERKEY1));
	writew(MAGIC_POWERKEY2, RTC_REG(POWERKEY2));
	writew(0x430e, RTC_REG(BBPU));

	/* disable watchdog timer */
	writew(0x2200, RGU_BASE);

	/* power _everything_ on for now */
	writew(0xffff, CONFIG_REG(PDN_CLR0));
	writew(0xffff, CONFIG_REG(PDN_CLR1));
	writew(0xffff, CONFIG_REG(PDN_CLR2));
	writew(0xffff, CONFIG_REG(PDN_CLR3));
	writew(0xffff, CONFIG_REG(PDN_CLR4));

	/* Initialize UART without interrupts */
	uart_init(SERCOMM_UART_NR, 0);
	uart_baudrate(SERCOMM_UART_NR, UART_115200);

	/* Initialize HDLC subsystem */
	sercomm_init();

	/* Say hi */
	puts("\n\nOSMOCOM Loader (revision " GIT_REVISION ")\n");
	puts(hr);

	/* Identify environment */
	printf("Running on %s in environment %s\n", manifest_board,
	       manifest_environment);

	printf("HW_VERSION = 0x%04x\n", readw(CONFIG_REG(HW_VERSION)));
	printf("SW_VERSION = 0x%04x\n", readw(CONFIG_REG(SW_VERSION)));
	printf("HW_CODE = 0x%04x\n", readw(CONFIG_REG(HW_CODE)));

	/* Initialize flash driver */
/*	if (flash_init(&the_flash, 0)) {
		puts("Failed to initialize flash!\n");
	} else {
		printf("Found flash of %d bytes at 0x%x with %d regions\n",
		       the_flash.f_size, the_flash.f_base,
		       the_flash.f_nregions);

		int i;
		for (i = 0; i < the_flash.f_nregions; i++) {
			printf("  Region %d of %d pages with %d bytes each.\n",
			       i,
			       the_flash.f_regions[i].fr_bnum,
			       the_flash.f_regions[i].fr_bsize);
		}

	}
*/

	/* Set up a key handler for powering off */
//	keypad_set_handler(&key_handler);

	/* Set up loader communications */
	sercomm_register_rx_cb(SC_DLCI_LOADER, &cmd_handler);

	/* Notify any running osmoload about our startup */
//	loader_send_init(SC_DLCI_LOADER);

	/* Wait for events */
	while (1) {
		uart_poll(SERCOMM_UART_NR);
	}

	/* NOT REACHED */

//	twl3025_power_off();
}

static void cmd_handler(uint8_t dlci, struct msgb *msg)
{
	if (msg->data_len < 1) {
		return;
	}

	uint8_t command = msgb_get_u8(msg);

	int res;

	flash_lock_t lock;

	void *data;

	uint8_t chip;
	uint8_t nbytes;
	uint16_t crc, mycrc;
	uint32_t address;

	struct msgb *reply = sercomm_alloc_msgb(256);	// XXX

	if (!reply) {
		printf("Failed to allocate reply buffer!\n");
		goto out;
	}

	switch (command) {

	case LOADER_PING:
		loader_send_simple(reply, dlci, LOADER_PING);
		break;

	case LOADER_RESET:
		loader_send_simple(reply, dlci, LOADER_RESET);
		device_reset();
		break;

	case LOADER_POWEROFF:
		loader_send_simple(reply, dlci, LOADER_POWEROFF);
		device_poweroff();
		break;

	case LOADER_ENTER_ROM_LOADER:
		loader_send_simple(reply, dlci, LOADER_ENTER_ROM_LOADER);
		device_enter_loader(1);
		break;

	case LOADER_ENTER_FLASH_LOADER:
		loader_send_simple(reply, dlci, LOADER_ENTER_FLASH_LOADER);
		device_enter_loader(0);
		break;

	case LOADER_MEM_READ:

		nbytes = msgb_get_u8(msg);
		address = msgb_get_u32(msg);

		crc = crc16(0, (void *)address, nbytes);

		msgb_put_u8(reply, LOADER_MEM_READ);
		msgb_put_u8(reply, nbytes);
		msgb_put_u16(reply, crc);
		msgb_put_u32(reply, address);

		memcpy(msgb_put(reply, nbytes), (void *)address, nbytes);

		sercomm_sendmsg(dlci, reply);

		break;

	case LOADER_MEM_WRITE:

		nbytes = msgb_get_u8(msg);
		crc = msgb_get_u16(msg);
		address = msgb_get_u32(msg);

		data = msgb_get(msg, nbytes);

		mycrc = crc16(0, data, nbytes);

		if (mycrc == crc) {
			memcpy((void *)address, data, nbytes);
		}

		msgb_put_u8(reply, LOADER_MEM_WRITE);
		msgb_put_u8(reply, nbytes);
		msgb_put_u16(reply, mycrc);
		msgb_put_u32(reply, address);

		sercomm_sendmsg(dlci, reply);

		break;

	case LOADER_JUMP:

		address = msgb_get_u32(msg);

		msgb_put_u8(reply, LOADER_JUMP);
		msgb_put_u32(reply, address);

		sercomm_sendmsg(dlci, reply);

		device_jump((void *)address);

		break;

	case LOADER_FLASH_INFO:

		msgb_put_u8(reply, LOADER_FLASH_INFO);
		msgb_put_u8(reply, 1);	// nchips

		// chip 1
		msgb_put_u32(reply, the_flash.f_base);
		msgb_put_u32(reply, the_flash.f_size);
		msgb_put_u8(reply, the_flash.f_nregions);

		int i;
		for (i = 0; i < the_flash.f_nregions; i++) {
			msgb_put_u32(reply, the_flash.f_regions[i].fr_bnum);
			msgb_put_u32(reply, the_flash.f_regions[i].fr_bsize);
		}

		sercomm_sendmsg(dlci, reply);

		break;

	case LOADER_FLASH_ERASE:
	case LOADER_FLASH_UNLOCK:
	case LOADER_FLASH_LOCK:
	case LOADER_FLASH_LOCKDOWN:

		chip = msgb_get_u8(msg);
		address = msgb_get_u32(msg);

		if (command == LOADER_FLASH_ERASE) {
			res = flash_block_erase(&the_flash, address);
		}
		if (command == LOADER_FLASH_UNLOCK) {
			res = flash_block_unlock(&the_flash, address);
		}
		if (command == LOADER_FLASH_LOCK) {
			res = flash_block_lock(&the_flash, address);
		}
		if (command == LOADER_FLASH_LOCKDOWN) {
			res = flash_block_lockdown(&the_flash, address);
		}

		msgb_put_u8(reply, command);
		msgb_put_u8(reply, chip);
		msgb_put_u32(reply, address);
		msgb_put_u32(reply, (res != 0));

		sercomm_sendmsg(dlci, reply);

		break;

	case LOADER_FLASH_GETLOCK:

		chip = msgb_get_u8(msg);
		address = msgb_get_u32(msg);

		lock = flash_block_getlock(&the_flash, address);

		msgb_put_u8(reply, command);
		msgb_put_u8(reply, chip);
		msgb_put_u32(reply, address);

		switch (lock) {
		case FLASH_UNLOCKED:
			msgb_put_u32(reply, LOADER_FLASH_UNLOCKED);
			break;
		case FLASH_LOCKED:
			msgb_put_u32(reply, LOADER_FLASH_LOCKED);
			break;
		case FLASH_LOCKED_DOWN:
			msgb_put_u32(reply, LOADER_FLASH_LOCKED_DOWN);
			break;
		default:
			msgb_put_u32(reply, 0xFFFFFFFF);
			break;
		}

		sercomm_sendmsg(dlci, reply);

		break;

	case LOADER_FLASH_PROGRAM:

		nbytes = msgb_get_u8(msg);
		crc = msgb_get_u16(msg);
		msgb_get_u8(msg);	// XXX align
		chip = msgb_get_u8(msg);
		address = msgb_get_u32(msg);

		data = msgb_get(msg, nbytes);

		mycrc = crc16(0, data, nbytes);

		if (mycrc == crc) {
			res = flash_program(&the_flash, address, data, nbytes);
		}

		msgb_put_u8(reply, LOADER_FLASH_PROGRAM);
		msgb_put_u8(reply, nbytes);
		msgb_put_u16(reply, mycrc);
		msgb_put_u8(reply, 0);	// XXX align
		msgb_put_u8(reply, chip);
		msgb_put_u32(reply, address);

		msgb_put_u32(reply, (uint32_t) res);	// XXX

		sercomm_sendmsg(dlci, reply);

		break;

	default:
		printf("unknown command %d\n", command);

		msgb_free(reply);

		break;
	}

 out:

	msgb_free(msg);
}

static void key_handler(enum key_codes code, enum key_states state)
{
	if (state != PRESSED)
		return;

	switch (code) {
	case KEY_POWER:
		puts("Powering off due to keypress.\n");
		device_poweroff();
		break;
	case KEY_OK:
		puts("Resetting due to keypress.\n");
		device_reset();
		break;
	default:
		break;
	}
}
