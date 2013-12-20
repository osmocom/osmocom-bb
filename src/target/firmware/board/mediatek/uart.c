/* MediaTek MT62xx internal UART Driver
 *
 * based on the Calypso driver, so there might be some cruft from it left...
 *
 * (C) 2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2010 by Ingo Albrecht <prom@berlin.ccc.de>
 * (C) 2010 by Steve Markgraf <steve@steve-m.de>
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

#include <debug.h>
#include <memory.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <defines.h>
#include <uart.h>
#include <console.h>

#include <comm/sercomm.h>

/* MT622x */
#if 0
#define BASE_ADDR_UART1	0x80130000
#define BASE_ADDR_UART2	0x80180000
#define BASE_ADDR_UART3	0x801b0000
#endif

/* MT 6235 */
#define BASE_ADDR_UART1	0x81030000

//TODO make UART2 and 3 work
#define UART_REG(n,m)	(BASE_ADDR_UART1 + (m))

#define LCR7BIT		0x80
#define LCRBFBIT	0x40
#define MCR6BIT		0x20
#define REG_OFFS(m)	((m) & ~(LCR7BIT|LCRBFBIT|MCR6BIT))
/* read access LCR[7] = 0 */
enum uart_reg {
	RBR		= 0x00,
	IER		= 0x04,
	IIR		= 0x08,
	LCR		= 0x0c,
	MCR		= 0x10,
	LSR		= 0x14,
	MSR		= 0x18,
	SCR		= 0x1c,
	AUTOBAUD_EN	= 0x20,
	HIGHSPEED	= 0x24,
	SAMPLE_COUNT	= 0x28,
	SAMPLE_POINT	= 0x2c,
	AUTOBAUD_REG	= 0x30,
	RATE_FIX_REG	= 0x34, /* undocumented */
	AUTOBAUDSAMPLE	= 0x38,
	GUARD		= 0x3c,
	ESCAPE_DAT	= 0x40,
	ESCAPE_EN	= 0x44,
	SLEEP_EN	= 0x48,
	VFIFO_EN	= 0x4c,
/* read access LCR[7] = 1 */
	DLL	= RBR,
	DLH	= IER,
/* read/write access LCR[7:0] = 0xbf */
	EFR	= IIR | LCRBFBIT,
	XON1	= MCR | LCRBFBIT,
	XON2	= LSR | LCRBFBIT,
	XOFF1	= MSR | LCRBFBIT,
	XOFF2 	= SCR | LCRBFBIT,
};

enum fcr_bits {
	FIFO_EN		= (1 << 0),
	RX_FIFO_CLEAR	= (1 << 1),
	TX_FIFO_CLEAR	= (1 << 2),
	DMA_MODE	= (1 << 3),
};
#define TX_FIFO_TRIG_SHIFT	4
#define RX_FIFO_TRIG_SHIFT	6

enum iir_bits {
	IIR_INT_PENDING			= 0x01,
	IIR_INT_TYPE			= 0x3E,
	IIR_INT_TYPE_RX_STATUS_ERROR 	= 0x06,
	IIR_INT_TYPE_RX_TIMEOUT		= 0x0C,
	IIR_INT_TYPE_RBR		= 0x04,
	IIR_INT_TYPE_THR		= 0x02,
	IIR_INT_TYPE_MSR		= 0x00,
	IIR_INT_TYPE_XOFF		= 0x10,
	IIR_INT_TYPE_FLOW		= 0x20,
	IIR_FCR0_MIRROR			= 0xC0,
};


/* enable or disable the divisor latch for access to DLL, DLH */
static void uart_set_lcr7bit(int uart, int on)
{
	uint8_t reg;

	reg = readb(UART_REG(uart, LCR));
	if (on)
		reg |= (1 << 7);
	else
		reg &= ~(1 << 7);
	writeb(reg, UART_REG(uart, LCR));
}

static uint8_t old_lcr;
static void uart_set_lcr_bf(int uart, int on)
{
	if (on) {
		old_lcr = readb(UART_REG(uart, LCR));
		writeb(0xBF, UART_REG(uart, LCR));
	} else {
		writeb(old_lcr, UART_REG(uart, LCR));
	}
}

/* Enable or disable the TCR_TLR latch bit in MCR[6] */
static void uart_set_mcr6bit(int uart, int on)
{
	uint8_t mcr;
	/* we assume EFR[4] is always set to 1 */
	mcr = readb(UART_REG(uart, MCR));
	if (on)
		mcr |= (1 << 6);
	else
		mcr &= ~(1 << 6);
	writeb(mcr, UART_REG(uart, MCR));
}

static void uart_reg_write(int uart, enum uart_reg reg, uint8_t val)
{
	if (reg & LCRBFBIT)
		uart_set_lcr_bf(uart, 1);
	else if (reg & LCR7BIT)
		uart_set_lcr7bit(uart, 1);
	else if (reg & MCR6BIT)
		uart_set_mcr6bit(uart, 1);

	writeb(val, UART_REG(uart, REG_OFFS(reg)));

	if (reg & LCRBFBIT)
		uart_set_lcr_bf(uart, 0);
	else if (reg & LCR7BIT)
		uart_set_lcr7bit(uart, 0);
	else if (reg & MCR6BIT)
		uart_set_mcr6bit(uart, 0);
}

/* read from a UART register, applying any required latch bits */
static uint8_t uart_reg_read(int uart, enum uart_reg reg)
{
	uint8_t ret;

	if (reg & LCRBFBIT)
		uart_set_lcr_bf(uart, 1);
	else if (reg & LCR7BIT)
		uart_set_lcr7bit(uart, 1);
	else if (reg & MCR6BIT)
		uart_set_mcr6bit(uart, 1);

	ret = readb(UART_REG(uart, REG_OFFS(reg)));

	if (reg & LCRBFBIT)
		uart_set_lcr_bf(uart, 0);
	else if (reg & LCR7BIT)
		uart_set_lcr7bit(uart, 0);
	else if (reg & MCR6BIT)
		uart_set_mcr6bit(uart, 0);

	return ret;
}

static void uart_irq_handler_cons(__unused int irqnr)
{
	const uint8_t uart = cons_get_uart();
	uint8_t iir;

	//uart_putchar_nb(uart, 'U');

	iir = uart_reg_read(uart, IIR);
	if (iir & IIR_INT_PENDING)
		return;

	switch (iir & IIR_INT_TYPE) {
	case IIR_INT_TYPE_RBR:
		break;
	case IIR_INT_TYPE_THR:
		if (cons_rb_flush() == 1) {
			/* everything was flushed, disable RBR IRQ */
			uint8_t ier = uart_reg_read(uart, IER);
			ier &= ~(1 << 1);
			uart_reg_write(uart, IER, ier);
		}
		break;
	case IIR_INT_TYPE_MSR:
		break;
	case IIR_INT_TYPE_RX_STATUS_ERROR:
		break;
	case IIR_INT_TYPE_RX_TIMEOUT:
		break;
	case IIR_INT_TYPE_XOFF:
		break;
	}
}

static void uart_irq_handler_sercomm(__unused int irqnr)
{
	const uint8_t uart = sercomm_get_uart();
	uint8_t iir, ch;

	//uart_putchar_nb(uart, 'U');

	iir = uart_reg_read(uart, IIR);
	if (iir & IIR_INT_PENDING)
		return;

	switch (iir & IIR_INT_TYPE) {
	case IIR_INT_TYPE_RX_TIMEOUT:
	case IIR_INT_TYPE_RBR:
		/* as long as we have rx data available */
		while (uart_getchar_nb(uart, &ch)) {
			if (sercomm_drv_rx_char(ch) < 0) {
				/* sercomm cannot receive more data right now */
				uart_irq_enable(uart, UART_IRQ_RX_CHAR, 0);
			}
		}
		break;
	case IIR_INT_TYPE_THR:
		/* as long as we have space in the FIFO */
		while (!uart_tx_busy(uart)) {
			/* get a byte from sercomm */
			if (!sercomm_drv_pull(&ch)) {
				/* no more bytes in sercomm, stop TX interrupts */
				uart_irq_enable(uart, UART_IRQ_TX_EMPTY, 0);
				break;
			}
			/* write the byte into the TX FIFO */
			uart_putchar_nb(uart, ch);
		}
		break;
	case IIR_INT_TYPE_MSR:
		printf("UART IRQ MSR\n");
		break;
	case IIR_INT_TYPE_RX_STATUS_ERROR:
		printf("UART IRQ RX_SE\n");
		break;
	case IIR_INT_TYPE_XOFF:
		printf("UART IRQXOFF\n");
		break;
	}
}

void uart_init(uint8_t uart, __unused uint8_t interrupts)
{
	/* no interrupts, only polling so far */

	uart_reg_write(uart, IER, 0x00);
	if (uart == cons_get_uart()) {
		cons_init();
	} else if (uart == sercomm_get_uart()) {
		sercomm_init();
		uart_irq_enable(uart, UART_IRQ_RX_CHAR, 1);
	} else {
		return;
	}

	uart_reg_write(uart, AUTOBAUD_EN, 0x00); /* disable AUTOBAUD */
	uart_reg_write(uart,   EFR, 0x10); /* Enhanced Features Register */

	/* no XON/XOFF flow control, ENHANCED_EN, no auto-RTS/CTS */
	uart_reg_write(uart, EFR, (1 << 4));
	/* enable Tx/Rx FIFO, Tx trigger at 56 spaces, Rx trigger at 60 chars */
	//FIXME check those FIFO settings
	uart_reg_write(uart, IIR, FIFO_EN | RX_FIFO_CLEAR | TX_FIFO_CLEAR |
			(3 << TX_FIFO_TRIG_SHIFT) | (1 << RX_FIFO_TRIG_SHIFT));

	/* RBR interrupt only when TX FIFO and TX shift register are empty */
	uart_reg_write(uart, SCR, (1 << 0));// | (1 << 3));

	/* 8 bit, 1 stop bit, no parity, no break */
	uart_reg_write(uart, LCR, 0x03);

	uart_set_lcr7bit(uart, 0);
}

void uart_poll(uint8_t uart) {
	if(uart == cons_get_uart()) {
		uart_irq_handler_cons(0);
	} else {
		uart_irq_handler_sercomm(0);
	}
}

void uart_irq_enable(uint8_t uart, enum uart_irq irq, int on)
{
	uint8_t ier = uart_reg_read(uart, IER);
	uint8_t mask = 0;

	switch (irq) {
	case UART_IRQ_TX_EMPTY:
		mask = (1 << 1);
		break;
	case UART_IRQ_RX_CHAR:
		mask = (1 << 0);
		break;
	}

	if (on)
		ier |= mask;
	else
		ier &= ~mask;

	uart_reg_write(uart, IER, ier);
}


void uart_putchar_wait(uint8_t uart, int c)
{
	/* wait while TX FIFO indicates full */
	while (~readb(UART_REG(uart, LSR)) & 0x20) { }

	/* put character in TX FIFO */
	writeb(c, UART_REG(uart, RBR));
}

int uart_putchar_nb(uint8_t uart, int c)
{
	/* if TX FIFO indicates full, abort */
	if (~readb(UART_REG(uart, LSR)) & 0x20)
		return 0;

	writeb(c, UART_REG(uart, RBR));
	return 1;
}

int uart_getchar_nb(uint8_t uart, uint8_t *ch)
{
	uint8_t lsr;

	lsr = readb(UART_REG(uart, LSR));

	/* something strange happened */
	if (lsr & 0x02)
		printf("LSR RX_OE\n");
	if (lsr & 0x04)
		printf("LSR RX_PE\n");
	if (lsr & 0x08)
		printf("LSR RX_FE\n");
	if (lsr & 0x10)
		printf("LSR RX_BI\n");
	if (lsr & 0x80)
		printf("LSR RX_FIFO_STS\n");

	/* is the Rx FIFO empty? */
	if (!(lsr & 0x01))
		return 0;

	*ch = readb(UART_REG(uart, RBR));
	//printf("getchar_nb(%u) = %02x\n", uart, *ch);
	return 1;
}

int uart_tx_busy(uint8_t uart)
{
	/* Check THRE bit (LSR[5]) to see if FIFO is full */
	if (~readb(UART_REG(uart, LSR)) & 0x20)
		return 1;
	return 0;
}

#if 0
/* 26MHz clock input (used when no PLL initialized directly after poweron) */
static const uint16_t divider[] = {
	[UART_38400]	= 42,
	[UART_57600]	= 28,
	[UART_115200]	= 14,
	[UART_230400]	= 7,
	[UART_460800]	= 14,	/* would need UART_REG(HIGHSPEED) = 1 or 2 */
	[UART_921600]	= 7,	/* would need UART_REG(HIGHSPEED) = 2 */
};
#endif

/* 52MHz clock input (after PLL init) */
static const uint16_t divider[] = {
	[UART_38400]	= 85,
	[UART_57600]	= 56,
	[UART_115200]	= 28,
	[UART_230400]	= 14,
	[UART_460800]	= 7,
	[UART_921600]	= 7,	/* would need UART_REG(HIGHSPEED) = 1 */
};

int uart_baudrate(uint8_t uart, enum uart_baudrate bdrt)
{
	uint16_t div;

	if (bdrt > ARRAY_SIZE(divider))
		return -1;

	div = divider[bdrt];
	uart_set_lcr7bit(uart, 1);
	writeb(div & 0xff, UART_REG(uart, DLL));
	writeb(div >> 8, UART_REG(uart, DLH));
	uart_set_lcr7bit(uart, 0);

	return 0;
}
