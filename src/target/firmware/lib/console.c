/* Ringbuffer based serial console layer, imported from OpenPCD */

/* (C) 2006-2010 by Harald Welte <laforge@gnumonks.org>
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
#include <string.h>
#include <console.h>
#include <uart.h>

#include <asm/system.h>

struct cons {
	char buf[CONS_RB_SIZE];
	char *next_inbyte;
	char *next_outbyte;
	int initialized;
	int uart_id;
};
static struct cons cons;

void cons_bind_uart(int uart)
{
	cons.uart_id = uart;
}

int cons_get_uart(void)
{
	return cons.uart_id;
}

void cons_init(void)
{
	memset(cons.buf, 0, sizeof(cons.buf));
	cons.next_inbyte = &cons.buf[0];
	cons.next_outbyte = &cons.buf[0];
	cons.initialized = 1;
}

/* determine how many bytes are left in the ringbuffer without overwriting
   bytes that haven't been written to the console yet */
static int __cons_rb_space(void)
{
	if (cons.next_inbyte == cons.next_outbyte)
		return sizeof(cons.buf)-1;
	else if (cons.next_outbyte > cons.next_inbyte)
		return (cons.next_outbyte - cons.next_inbyte) -1;
	else
		return sizeof(cons.buf) - 1 - (cons.next_inbyte - cons.next_outbyte);
}

/* pull one char out of debug ring buffer */
static int cons_rb_pull(char *ret)
{
	unsigned long flags;

	local_irq_save(flags);

	if (cons.next_outbyte == cons.next_inbyte) {
		local_irq_restore(flags);
		return -1;
	}

	*ret = *cons.next_outbyte;

	cons.next_outbyte++;
	if (cons.next_outbyte >= &cons.buf[0]+sizeof(cons.buf)) {
		cons.next_outbyte = &cons.buf[0];
	}
#if 0
	 else if (cons.next_outbyte > &cons.buf[0]+sizeof(cons.buf)) {
		cons.next_outbyte -= sizeof(cons.buf);
	}
#endif

	local_irq_restore(flags);

	return 0;
}

/* returns if everything was flushed (1) or if there's more to flush (0) */
static void __rb_flush_wait(void)
{
	char ch;
	while (cons_rb_pull(&ch) >= 0)
		uart_putchar_wait(cons.uart_id, ch);
}

/* returns if everything was flushed (1) or if there's more to flush (0) */
static int __rb_flush(void)
{
	while (!uart_tx_busy(cons.uart_id)) {
		char ch;
		if (cons_rb_pull(&ch) < 0) {
			/* no more data to write, disable interest in Tx FIFO interrupts */
			return 1;
		}
		uart_putchar_nb(cons.uart_id, ch);
	}

	/* if we reach here, UART Tx FIFO is busy again */
	return 0;
}

/* flush pending data from debug ring buffer to serial port */
int cons_rb_flush(void)
{
	return __rb_flush();
}

/* Append bytes to ring buffer, not more than we have left! */
static void __cons_rb_append(const char *data, int len)
{
	if (cons.next_inbyte + len >= &cons.buf[0]+sizeof(cons.buf)) {
		int before_tail = (&cons.buf[0]+sizeof(cons.buf)) - cons.next_inbyte;
		/* copy the first part before we wrap */
		memcpy(cons.next_inbyte, data, before_tail);
		data += before_tail;
		len -= before_tail;
		/* reset the buffer */
		cons.next_inbyte = &cons.buf[0];
	}
	memcpy(cons.next_inbyte, data, len);
	cons.next_inbyte += len;
}

/* append bytes to the ringbuffer, do one wrap */
int cons_rb_append(const char *data, int len)
{
	unsigned long flags;
	int bytes_left;
	const char *data_cur;

	/* we will never be able to write more than the console buffer */
	if (len > (int) sizeof(cons.buf))
		len = sizeof(cons.buf);

	local_irq_save(flags);

	bytes_left = __cons_rb_space();
	data_cur = data;

	if (len > bytes_left) {
		/* append what we can */
		__cons_rb_append(data_cur, bytes_left);
		/* busy-wait for all characters to be transmitted */
		__rb_flush_wait();
		/* fill it with the remaining bytes */
		len -= bytes_left;
		data_cur += bytes_left;
	}
	__cons_rb_append(data_cur, len);

	/* we want to get Tx FIFO interrupts */
	uart_irq_enable(cons.uart_id, UART_IRQ_TX_EMPTY, 1);

	local_irq_restore(flags);

	return len;
}

int cons_puts(const char *s)
{
	if (cons.initialized) {
		return cons_rb_append(s, strlen(s));
	} else {
		/* if the console is not active yet, we need to fall back */
		int i = strlen(s);
		while (i--)
			uart_putchar_wait(cons.uart_id, *s++);
		return i;
	}
}

int cons_putchar(char c)
{
	if (cons.initialized)
		return cons_rb_append(&c, 1);
	else {
		/* if the console is not active yet, we need to fall back */
		uart_putchar_wait(cons.uart_id, c);
		return 0;
	}
}
