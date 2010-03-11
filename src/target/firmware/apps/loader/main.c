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
#include <rffe.h>
#include <keypad.h>
#include <board.h>
#include <console.h>

#include <abb/twl3025.h>
#include <display/st7558.h>
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

struct loader_mem_read {
	uint8_t cmd;
	uint8_t nbytes;
	uint32_t address;
	uint8_t  data[0];
} __attribute__((__packed__));

uint32_t htonl(uint32_t hostlong) {
#if BYTE_ORDER==LITTLE_ENDIAN
  return (hostlong>>24) | ((hostlong&0xff0000)>>8) |
          ((hostlong&0xff00)<<8) | (hostlong<<24);
#else
  return hostlong;
#endif
}

uint32_t ntohl(uint32_t hostlong) __attribute__((weak,alias("htonl")));

uint16_t htons(uint16_t hostshort) {
#if BYTE_ORDER==LITTLE_ENDIAN
  return ((hostshort>>8)&0xff) | (hostshort<<8);
#else
  return hostshort;
#endif
}

uint16_t ntohs(uint16_t hostshort) __attribute__((weak,alias("htons")));

#define SCAN

#ifdef SCAN
/* if scanning is enabled, scan from 0 ... 124 */
#define BASE_ARFCN	0
#else
/* fixed ARFCN in GSM1800 at which Harald has his GSM test license */
#define BASE_ARFCN	871
#endif

/* FIXME: We need proper calibrated delay loops at some point! */
void delay_us(unsigned int us)
{
	volatile unsigned int i;

	for (i= 0; i < us*4; i++) { i; }
}

void delay_ms(unsigned int ms)
{
	volatile unsigned int i;

	for (i= 0; i < ms*1300; i++) { i; }
}

/* Main Program */
const char *hr = "======================================================================\n";

static void key_handler(enum key_codes code, enum key_states state);
static void cmd_handler(uint8_t dlci, struct msgb *msg);

int flag = 0;

void poweroff(void) {
	unsigned i;
	for(i = 0; i < 10; i++) {
		uart_poll(SERCOMM_UART_NR);
		delay_ms(10);
	}
	twl3025_power_off();
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
	puts("\n\nOSMOCOM Calypso loader\n");
	puts(hr);

	/* Set up a key handler for powering off */
	keypad_set_handler(&key_handler);

	/* Set up loader communications */
	sercomm_register_rx_cb(SC_DLCI_LOADER, &cmd_handler);

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

	uint8_t command = 0; //= msgb_get_u8(msg);

	printf("command %u\n", command);

	msgb_free(msg);

	return;

	uint8_t  nbytes;
	uint32_t address;

	struct msgb *reply;

	switch(command) {

	case LOADER_PING:

		printf("ping\n");

		//sercomm_sendmsg(dlci, msg);
		//msg = NULL;

		break;

	case LOADER_MEM_READ:

		nbytes = msgb_get_u8(msg);
		address = msgb_get_u32(msg);

		printf("mem read %u @ %p\n", nbytes, (void*)address);

		reply = sercomm_alloc_msgb(6 + nbytes);

		msgb_put_u8(reply, LOADER_MEM_READ);
		msgb_put_u8(reply, nbytes);
		msgb_put_u32(reply, address);

		memcpy(msgb_put(reply, nbytes), (void*)address, nbytes);

		sercomm_sendmsg(dlci, reply);

		break;

	}

	if(msg) {
		msgb_free(msg);
	}
}

static void key_handler(enum key_codes code, enum key_states state)
{
	if (state != PRESSED)
		return;

	switch (code) {
	case KEY_POWER:
		poweroff();
		break;
	default:
		break;
	}
}
