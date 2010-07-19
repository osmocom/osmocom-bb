/* Compal ramloader -> Calypso romloader Chainloading application */

/* (C) 2010 by Steve Markgraf <steve@steve-m.de>
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

#include <calypso/clock.h>

/* Main Program */

static void device_enter_loader(unsigned char bootrom) {
	calypso_bootrom(bootrom);
	void (*entry)( void ) = (void (*)(void))0;
	entry();
}

int main(void)
{
	/* Always disable wdt (some platforms enable it on boot) */
	wdog_enable(0);

	/* enable Calypso romloader mapping and jump there */
	delay_ms(200);
	device_enter_loader(1);

	/* Not reached */
	while(1) {
	}
}
