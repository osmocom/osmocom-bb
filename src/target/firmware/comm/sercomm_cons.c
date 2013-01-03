/* Serial console layer, layered on top of sercomm HDLC */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
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
#include <errno.h>
#include <string.h>

#include <asm/system.h>

#include <uart.h>

#include <console.h>
#include <osmocom/core/msgb.h>
#include <comm/sercomm.h>
#include <comm/sercomm_cons.h>

static struct {
	struct msgb *cur_msg;
} scons;

static void raw_puts(const char *s)
{
	int i = strlen(s);
	int uart_id = sercomm_get_uart();
	while (i--)
		uart_putchar_wait(uart_id, *s++);
}

#ifdef DEBUG
#define raw_putd(x)	raw_puts(x)
#else
#define raw_putd(x)
#endif

int sercomm_puts(const char *s)
{
	unsigned long flags;
	const int len = strlen(s);
	unsigned int bytes_left = len;

	if (!sercomm_initialized()) {
		raw_putd("sercomm not initialized: ");
		raw_puts(s);
		return len - 1;
	}

	/* This function is called from any context: Supervisor, IRQ, FIQ, ...
	 * as such, we need to ensure re-entrant calls are either supported or
	 * avoided. */
	local_irq_save(flags);
	local_fiq_disable();

	while (bytes_left > 0) {
		unsigned int write_num, space_left, flush;
		uint8_t *data;

		if (!scons.cur_msg)
			scons.cur_msg = sercomm_alloc_msgb(SERCOMM_CONS_ALLOC);

		if (!scons.cur_msg) {
			raw_putd("cannot allocate sercomm msgb: ");
			raw_puts(s);
			return -ENOMEM;
		}

		/* space left in the current msgb */
		space_left = msgb_tailroom(scons.cur_msg);

		if (space_left <= bytes_left) {
			write_num = space_left;
			/* flush buffer when it is full */
			flush = 1;
		} else {
			write_num = bytes_left;
			flush = 0;
		}

		/* obtain pointer where to copy the data */
		data = msgb_put(scons.cur_msg, write_num);

		/* copy data while looking for \n line termination */
		{
			unsigned int i;
			for (i = 0; i < write_num; i++) {
				/* flush buffer at end of line, but skip
				 * flushing if we have a backlog in order to
				 * increase efficiency of msgb filling */
				if (*s == '\n' &&
				    sercomm_tx_queue_depth(SC_DLCI_CONSOLE) < 4)
					flush = 1;
				*data++ = *s++;
			}
		}
		bytes_left -= write_num;

		if (flush) {
			sercomm_sendmsg(SC_DLCI_CONSOLE, scons.cur_msg);
			/* reset scons.cur_msg pointer to ensure we allocate
			 * a new one next round */
			scons.cur_msg = NULL;
		}
	}

	local_irq_restore(flags);

	return len - 1;
}

int sercomm_putchar(int c)
{
	char s[2];
	int rc;

	s[0] = c & 0xff;
	s[1] = '\0';

	rc = sercomm_puts(s);
	if (rc < 0)
		return rc;

	return c;
}
