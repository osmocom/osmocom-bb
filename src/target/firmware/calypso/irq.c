/* Driver for Calypso IRQ controller */

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
#include <stdio.h>

#include <debug.h>
#include <memory.h>
#include <arm.h>
#include <calypso/irq.h>

#define BASE_ADDR_IRQ	0xfffffa00

enum irq_reg {
	IT_REG1		= 0x00,
	IT_REG2		= 0x02,
	MASK_IT_REG1	= 0x08,
	MASK_IT_REG2	= 0x0a,
	IRQ_NUM		= 0x10,
	FIQ_NUM		= 0x12,
	IRQ_CTRL	= 0x14,
};

#define ILR_IRQ(x)	(0x20 + (x*2))
#define IRQ_REG(x)	((void *)BASE_ADDR_IRQ + (x))

#define NR_IRQS	32

static uint8_t default_irq_prio[] = {
	[IRQ_WATCHDOG]		= 0xff,
	[IRQ_TIMER1]		= 0xff,
	[IRQ_TIMER2]		= 0xff,
	[IRQ_TSP_RX]		= 0,
	[IRQ_TPU_FRAME]		= 3,
	[IRQ_TPU_PAGE]		= 0xff,
	[IRQ_SIMCARD]		= 0xff,
	[IRQ_UART_MODEM]	= 8,
	[IRQ_KEYPAD_GPIO]	= 4,
	[IRQ_RTC_TIMER]		= 9,
	[IRQ_RTC_ALARM_I2C]	= 10,
	[IRQ_ULPD_GAUGING]	= 2,
	[IRQ_EXTERNAL]		= 12,
	[IRQ_SPI]		= 0xff,
	[IRQ_DMA]		= 0xff,
	[IRQ_API]		= 0xff,
	[IRQ_SIM_DETECT]	= 0,
	[IRQ_EXTERNAL_FIQ]	= 7,
	[IRQ_UART_IRDA]		= 2,
	[IRQ_ULPD_GSM_TIMER]	= 1,
	[IRQ_GEA]		= 0xff,
};

static irq_handler *irq_handlers[NR_IRQS];

static void _irq_enable(enum irq_nr nr, int enable)
{
	uint16_t *reg = IRQ_REG(MASK_IT_REG1);
	uint16_t val;

	if (nr > 15) {
		reg = IRQ_REG(MASK_IT_REG2);
		nr -= 16;
	}

	val = readw(reg);
	if (enable)
		val &= ~(1 << nr);
	else
		val |= (1 << nr);
	writew(val, reg);
}

void irq_enable(enum irq_nr nr)
{
	_irq_enable(nr, 1);
}

void irq_disable(enum irq_nr nr)
{
	_irq_enable(nr, 0);
}

void irq_config(enum irq_nr nr, int fiq, int edge, int8_t prio)
{
	uint16_t val;

	if (prio == -1)
		prio = default_irq_prio[nr];

	if (prio > 31)
		prio = 31;

	val = prio << 2;
	if (edge)
		val |= 0x02;
	if (fiq)
		val |= 0x01;

	writew(val, IRQ_REG(ILR_IRQ(nr)));
}

/* Entry point for interrupts */
void irq(void)
{
	uint8_t num, tmp;
	irq_handler *handler;

#if 1
	/* Hardware interrupt detection mode */
	num = readb(IRQ_REG(IRQ_NUM)) & 0x1f;

	printd("i%02x\n", num);

	handler = irq_handlers[num];

	if (handler)
		handler(num);
#else
	/* Software interrupt detection mode */
	{
		uint16_t it_reg, mask_reg;
		uint32_t irqs;

		it_reg = readw(IRQ_REG(IT_REG1));
		mask_reg = readw(IRQ_REG(MASK_IT_REG1));
		irqs = it_reg & ~mask_reg;

		it_reg = readw(IRQ_REG(IT_REG2));
		mask_reg = readw(IRQ_REG(MASK_IT_REG2));
		irqs |= (it_reg & ~mask_reg) << 16;

		for (num = 0; num < 32; num++) {
			if (irqs & (1 << num)) {
				printd("i%d\n", num);
				handler = irq_handlers[num];
				if (handler)
					handler(num);
				/* clear this interrupt */
				if (num < 16)
					writew(~(1 << num), IRQ_REG(IT_REG1));
				else
					writew(~(1 << (num-16)), IRQ_REG(IT_REG2));
			}
		}
		dputchar('\n');
	}
#endif
	/* Start new IRQ agreement */
	tmp = readb(IRQ_REG(IRQ_CTRL));
	tmp |= 0x01;
	writeb(tmp, IRQ_REG(IRQ_CTRL));
}

/* Entry point for FIQs */
void fiq(void)
{
	uint8_t num, tmp;
	irq_handler *handler;

	num = readb(IRQ_REG(FIQ_NUM)) & 0x1f;
	if (num) {
		printd("f%02x\n", num);
	}

	handler = irq_handlers[num];

	if (handler)
		handler(num);

	/* Start new FIQ agreement */
	tmp = readb(IRQ_REG(IRQ_CTRL));
	tmp |= 0x02;
	writeb(tmp, IRQ_REG(IRQ_CTRL));
}

void irq_register_handler(enum irq_nr nr, irq_handler *handler)
{
	if (nr >= NR_IRQS)
		return;

	irq_handlers[nr] = handler;
}

#define BASE_ADDR_IBOOT_EXC	0x0080001C
extern uint32_t _exceptions;

/* Install the exception handlers to where the ROM loader jumps */
void calypso_exceptions_install(void)
{
	uint32_t *exceptions_dst = (uint32_t *) BASE_ADDR_IBOOT_EXC;
	uint32_t *exceptions_src = &_exceptions;
	int i;

	for (i = 0; i < 7; i++)
		*exceptions_dst++ = *exceptions_src++;

}

static void set_default_priorities(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(default_irq_prio); i++) {
		uint16_t val;
		uint8_t prio = default_irq_prio[i];
		if (prio > 31)
			prio = 31;

		val = readw(IRQ_REG(ILR_IRQ(i)));
		val &= ~(0x1f << 2);
		val |= prio << 2;
		writew(val, IRQ_REG(ILR_IRQ(i)));
	}
}

static uint32_t irq_nest_mask;
/* mask off all interrupts that have a lower priority than irq_nr */
static void mask_all_lower_prio_irqs(enum irq_nr irqnr)
{
	uint8_t our_prio = readb(IRQ_REG(ILR_IRQ(irqnr))) >> 2;
	int i;

	for (i = 0; i < _NR_IRQ; i++) {
		uint8_t prio;

		if (i == irqnr)
			continue;

		prio = readb(IRQ_REG(ILR_IRQ(i))) >> 2;
		if (prio >= our_prio)
			irq_nest_mask |= (1 << i);
	}
}

void irq_init(void)
{
	/* set default priorities */
	set_default_priorities();
	/* mask all interrupts off */
	writew(0xffff, IRQ_REG(MASK_IT_REG1));
	writew(0xffff, IRQ_REG(MASK_IT_REG2));
	/* clear all pending interrupts */
	writew(0, IRQ_REG(IT_REG1));
	writew(0, IRQ_REG(IT_REG2));
	/* enable interrupts globally to the ARM core */
	arm_enable_interrupts();
}
