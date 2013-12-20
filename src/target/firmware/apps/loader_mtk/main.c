/*
 * boot loader for MTK phones (based on the calypso-version)
 *
 * (C) 2010 by Ingo Albrecht <prom@berlin.ccc.de>
 * (C) 2011 by Wolfram Sang <wolfram@the-dreams.de>
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
#include <keypad.h>
#include <board.h>
#include <console.h>
#include <defines.h>
#include <manifest.h>

#include <osmocom/core/crc16.h>

#include <comm/sercomm.h>

#include <uart.h>

#include <flash/cfi_flash.h>

#include <mtk/emi.h>
#include <mtk/mt6235.h>
#include <mtk/system.h>

#include "../loader/protocol.h"

/* Main Program */
const char *hr =
    "======================================================================\n";

static void cmd_handler(uint8_t dlci, struct msgb *msg);

int flag = 0;
static int sercomm_uart;

static void flush_uart(void)
{
	unsigned i;
	for (i = 0; i < 500; i++) {
		uart_poll(sercomm_uart);
		delay_ms(1);
	}
}

static void device_poweroff(void)
{
	flush_uart();
	writew(BBPU_MAGIC | RTC_BBPU_WRITE_EN,
	       MTK_RTC_BBPU);
	writew(1, MTK_RTC_WRTGR);
}

static void device_reset(void)
{
	flush_uart();
}

static void device_enter_loader(__unused unsigned char bootrom)
{
	flush_uart();
	delay_ms(2000);
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

flash_t the_flash;

extern void putchar_asm(uint32_t c);

static const uint8_t phone_ack[] = { 0x1b, 0xf6, 0x02, 0x00, 0x41, 0x03, 0x42 };

int main(void)
{
	board_init(0);
	sercomm_uart = sercomm_get_uart();

	/* Initialize HDLC subsystem */
	sercomm_init();

	/* Say hi */
	puts("\n\nOsmocomBB Loader (revision " GIT_REVISION ")\n");
	puts(hr);

	/* Identify environment */
	printf("\nRunning on %s in environment %s\n", manifest_board,
	       manifest_environment);

	printf("\nHW_CODE = 0x%04x", readw(MTK_CONFG_HW_CODE));

	/* Set up loader communications */
	sercomm_register_rx_cb(SC_DLCI_LOADER, &cmd_handler);

	/* Wait for events */

	while (1) {
		uart_poll(sercomm_uart);
	}

}

static void cmd_handler(uint8_t dlci, struct msgb *msg)
{
	if (msg->data_len < 1) {
		return;
	}

	uint8_t command = msgb_pull_u8(msg);

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

		nbytes = msgb_pull_u8(msg);
		address = msgb_pull_u32(msg);

		crc = osmo_crc16(0, (void *)address, nbytes);

		msgb_put_u8(reply, LOADER_MEM_READ);
		msgb_put_u8(reply, nbytes);
		msgb_put_u16(reply, crc);
		msgb_put_u32(reply, address);

		memcpy(msgb_put(reply, nbytes), (void *)address, nbytes);

		sercomm_sendmsg(dlci, reply);

		break;

	case LOADER_MEM_WRITE:

		nbytes = msgb_pull_u8(msg);
		crc = msgb_pull_u16(msg);
		address = msgb_pull_u32(msg);

		data = msgb_pull(msg, nbytes) - nbytes;

		mycrc = osmo_crc16(0, data, nbytes);

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

		address = msgb_pull_u32(msg);

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

		unsigned i;
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

		chip = msgb_pull_u8(msg);
		address = msgb_pull_u32(msg);

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

		chip = msgb_pull_u8(msg);
		address = msgb_pull_u32(msg);

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

		nbytes = msgb_pull_u8(msg);
		crc = msgb_pull_u16(msg);
		msgb_pull_u8(msg);	// XXX align
		chip = msgb_pull_u8(msg);
		address = msgb_pull_u32(msg);

		data = msgb_pull(msg, nbytes) - nbytes;

		mycrc = osmo_crc16(0, data, nbytes);

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
