/* main program of Free Software for Calypso Phone */

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

#include <memory.h>
#include <delay.h>
#include <stdio.h>
#include <stdint.h>
#include <cfi_flash.h>
#include <abb/twl3025.h>
#include <calypso/clock.h>
#include <calypso/timer.h>
#include <calypso/misc.h>
#include <comm/timer.h>

#define KBIT 1024
#define	MBIT (1024*KBIT)

#define REG_DEV_ID_CODE 	0xfffef000
#define REG_DEV_VER_CODE	0xfffef002
#define REG_DEV_ARMVER_CODE	0xfffffe00
#define REG_cDSP_ID_CODE	0xfffffe02
#define REG_DIE_ID_CODE		0xfffef010

/* Main Program */
const char *hr = "======================================================================\n";

int main(void)
{
	puts("\n\nCompal device data dumper\n");
	puts(hr);

	/* Disable watchdog (for phones that have it enabled after boot) */
	wdog_enable(0);

	/* Initialize TWL3025 for power control */
	twl3025_init();

	/* Dump device identification */
	dump_dev_id();
	puts(hr);

	/* Initialize flash, dumping the protection area. */
	cfi_flash_t f;
	flash_init(&f, 0x00000000);
	flash_dump_info(&f);
	puts(hr);

	/* Dump flash contents */
	printf("Dump %lu kbytes of external flash\n", f.f_size/1024);
	memdump_range((void *)0x00000000, f.f_size);
	puts(hr);

	/* Power down */
	twl3025_power_off();

	while (1) {
		update_timers();
	}
}

