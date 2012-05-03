/* Driver for Calypso RTC controller */

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

#include <defines.h>
#include <debug.h>
#include <memory.h>
#include <calypso/irq.h>

#define BASE_ADDR_RTC	0xfffe1800
#define RTC_REG(x)	((void *)BASE_ADDR_RTC + (x))

enum rtc_reg {
	SECOND_REG		= 0x00,
	MINUTES_REG		= 0x01,
	HOURS_REG		= 0x02,
	DAYS_REG		= 0x03,
	MONTHS_REG		= 0x04,
	YEARS_REG		= 0x05,
	WEEK_REG		= 0x06,
	/* reserved */
	ALARM_SECOND_REG	= 0x08,
	ALARM_MINUTES_REG	= 0x09,
	ALARM_HOURS_REG		= 0x0a,
	ALARM_DAYS_REG		= 0x0b,
	ALARM_MONTHS_REG	= 0x0c,
	ALARM_YEARS_REG		= 0x0d,
	/* reserved */
	/* reserved */
	CTRL_REG		= 0x10,
	STATUS_REG		= 0x11,
	INT_REG			= 0x12,
	COMP_LSB_REG		= 0x13,
	COMP_MSB_REG		= 0x14,
	RES_PROG_REG		= 0x15,
};

static int tick_ctr;

static void rtc_irq_tick(__unused enum irq_nr nr)
{
	tick_ctr++;
}

void rtc_init(void)
{
	irq_register_handler(IRQ_RTC_TIMER, &rtc_irq_tick);
	irq_config(IRQ_RTC_TIMER, 0, 1, 0);
	irq_enable(IRQ_RTC_TIMER);

	/* clear power-up reset */
	writeb(0x80, RTC_REG(STATUS_REG));
	/* enable RTC running */
	writeb(0x01, RTC_REG(CTRL_REG));
	/* enable periodic interrupts every second */
	writeb(0x04, RTC_REG(INT_REG));
}
