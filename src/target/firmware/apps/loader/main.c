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

#include <layer1/sync.h>
#include <layer1/tpu_window.h>

#include "protocol.h"

/* Main Program */
const char *hr = "======================================================================\n";

static void key_handler(enum key_codes code, enum key_states state);
static void cmd_handler(uint8_t dlci, struct msgb *msg);

int flag = 0;

static void flush_uart(void) {
	unsigned i;
	for(i = 0; i < 500; i++) {
		uart_poll(SERCOMM_UART_NR);
		delay_ms(1);
	}
}

static void device_poweroff(void) {
	flush_uart();
	twl3025_power_off();
}

static void device_reset(void) {
	flush_uart();
	wdog_reset();
}

static void device_enter_loader(unsigned char bootrom) {
	flush_uart();

	calypso_bootrom(bootrom);
	void (*entry)( void ) = (void (*)(void))0;
	entry();
}

static void device_jump(void *entry) {
	flush_uart();

	void (*f)( void ) = (void (*)(void))entry;
	f();
}

static void
loader_send_simple(uint8_t dlci, uint8_t command) {
	struct msgb *msg = sercomm_alloc_msgb(1);
	if(!msg) {
		puts("Failed to allocate message buffer!\n");
	}
	msgb_put_u8(msg, command);
	sercomm_sendmsg(dlci, msg);
}

extern unsigned char _start;

static void
loader_send_init(uint8_t dlci) {
	struct msgb *msg = sercomm_alloc_msgb(1);
	if(!msg) {
		puts("Failed to allocate message buffer!\n");
	}
	msgb_put_u8(msg, LOADER_INIT);
	msgb_put_u32(msg, 0);
	msgb_put_u32(msg, &_start);
	sercomm_sendmsg(dlci, msg);
}

int main(void)
{
	/* Always disable wdt (some platforms enable it on boot) */
	wdog_enable(0);

	/* Initialize TWL3025 for power control */
	twl3025_init();

	/* Initialize UART without interrupts */
	uart_init(SERCOMM_UART_NR, 0);
	uart_baudrate(SERCOMM_UART_NR, UART_115200);

	/* Initialize HDLC subsystem */
	sercomm_init();

	/* Say hi */
	puts("\n\nOSMOCOM Calypso loader (revision " GIT_REVISION ")\n");
	puts(hr);

	/* Identify environment */
	printf("Running on %s in environment %s\n", manifest_board, manifest_environment);

	/* Set up a key handler for powering off */
	keypad_set_handler(&key_handler);

	/* Set up loader communications */
	sercomm_register_rx_cb(SC_DLCI_LOADER, &cmd_handler);

	/* Notify any running osmoload about our startup */
	loader_send_init(SC_DLCI_LOADER);

	/* Wait for events */
	while (1) {
		keypad_poll();
		uart_poll(SERCOMM_UART_NR);
	}

	/* NOT REACHED */

	twl3025_power_off();
}

static void cmd_handler(uint8_t dlci, struct msgb *msg) {
	if(msg->data_len < 1) {
		return;
	}

	uint8_t command = msgb_get_u8(msg);

	printf("command %u: ", command);

	uint8_t  nbytes;
	uint16_t crc;
	uint32_t address;

	struct msgb *reply;

	switch(command) {

	case LOADER_PING:
		puts("ping\n");
		loader_send_simple(dlci, LOADER_PING);
		break;

	case LOADER_RESET:
		puts("reset\n");
		loader_send_simple(dlci, LOADER_RESET);
		device_reset();
		break;

	case LOADER_POWEROFF:
		puts("poweroff\n");
		loader_send_simple(dlci, LOADER_POWEROFF);
		device_poweroff();
		break;

	case LOADER_ENTER_ROM_LOADER:
		puts("jump to rom loader\n");
		loader_send_simple(dlci, LOADER_ENTER_ROM_LOADER);
		device_enter_loader(1);
		break;

	case LOADER_ENTER_FLASH_LOADER:
		puts("jump to flash loader\n");
		loader_send_simple(dlci, LOADER_ENTER_FLASH_LOADER);
		device_enter_loader(0);
		break;

	case LOADER_MEM_READ:

		nbytes = msgb_get_u8(msg);
		address = msgb_get_u32(msg);

		printf("mem read %u @ %p\n", nbytes, (void*)address);

		reply = sercomm_alloc_msgb(6 + nbytes);

		if(!reply) {
			printf("Failed to allocate reply buffer!\n");
		}

		msgb_put_u8(reply, LOADER_MEM_READ);
		msgb_put_u8(reply, nbytes);
		msgb_put_u32(reply, address);

		memcpy(msgb_put(reply, nbytes), (void*)address, nbytes);

		sercomm_sendmsg(dlci, reply);

		break;

	case LOADER_MEM_WRITE:

		nbytes = msgb_get_u8(msg);
		crc = msgb_get_u16(msg);
		address = msgb_get_u32(msg);

		printf("mem write %u @ %p\n", nbytes, (void*)address);

		void *data = msgb_get(msg, nbytes);

		uint16_t mycrc = crc16(0, data, nbytes);

#if 0
		printf("crc %x got %x\n", mycrc, crc);
		hexdump(data, nbytes);
#endif

		if(mycrc == crc) {
			memcpy((void*)address, data, nbytes);
		}

		reply = sercomm_alloc_msgb(8);

		if(!reply) {
			printf("Failed to allocate reply buffer!\n");
		}

		msgb_put_u8(reply, LOADER_MEM_WRITE);
		msgb_put_u8(reply, nbytes);
		msgb_put_u16(reply, mycrc);
		msgb_put_u32(reply, address);

		sercomm_sendmsg(dlci, reply);

		break;

	case LOADER_JUMP:

		address = msgb_get_u32(msg);

		printf("jump to 0x%x\n", address);

		reply = sercomm_alloc_msgb(5);

		if(!reply) {
			printf("Failed to allocate reply buffer!\n");
		}

		msgb_put_u8(reply, LOADER_JUMP);
		msgb_put_u32(reply, address);

		sercomm_sendmsg(dlci, reply);

		device_jump((void*)address);

		break;

	default:
		printf("unknown command %d\n", command);
		break;

	}

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
