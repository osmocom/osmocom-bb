/* Driver for the keypad attached to the TI Calypso */

/* (C) 2010 by roh <roh@hyte.de>
 * (C) 2013 by Steve Markgraf <steve@steve-m.de>
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
#include <delay.h>
#include <memory.h>
#include <keypad.h>

#include <calypso/irq.h>
#include <abb/twl3025.h>
#include <comm/timer.h>

#define KBR_LATCH_REG	0xfffe480a
#define KBC_REG		0xfffe480c
#define KBD_GPIO_INT	0xfffe4816
#define KBD_GPIO_MASKIT	0xfffe4818

static key_handler_t key_handler = NULL;

void emit_key(uint8_t key, uint8_t state)
{
	printf("key=%u %s\n", key, state == PRESSED ? "pressed" : "released");

	if(key_handler) {
		key_handler(key, state);
	}
}

volatile uint32_t lastbuttons = 0;
static const uint8_t *btn_map;
unsigned long power_hold = 0;

void dispatch_buttons(uint32_t buttons)
{
	int i;
	uint8_t state;

	if ((buttons & (1 << btn_map[KEY_POWER]))) {
		/* hold button 500ms to shut down */
		if ((lastbuttons & (1 << btn_map[KEY_POWER]))) {
			unsigned long elapsed = jiffies - power_hold;
			if (elapsed > 50)
				twl3025_power_off();
			power_hold++;
		} else
		power_hold = jiffies;
	}

	if (buttons == lastbuttons)
		return;

	uint32_t diff = buttons ^ lastbuttons;
	for (i = 0; i < BUTTON_CNT; i++) {
		if (diff & (1 << btn_map[i])) {
			state = (buttons & (1 << btn_map[i])) ? PRESSED : RELEASED;
			emit_key(i, state);
		}
	}
	lastbuttons = buttons;
}

static uint8_t	polling = 0;
static uint8_t  with_interrupts = 0;

static void keypad_irq(__unused enum irq_nr nr)
{
	/* enable polling */
	polling = 1;
	irq_disable(IRQ_KEYPAD_GPIO);
}

void keypad_init(const uint8_t *keymap, uint8_t interrupts)
{
	btn_map = keymap;
	lastbuttons = 0;
	polling = 0;
	writew(0, KBD_GPIO_MASKIT);
	writew(0, KBC_REG);

	if(interrupts) {
		with_interrupts = 1;
		irq_register_handler(IRQ_KEYPAD_GPIO, &keypad_irq);
		irq_config(IRQ_KEYPAD_GPIO, 0, 0, 0);
		irq_enable(IRQ_KEYPAD_GPIO);
	}
}

void keypad_set_handler(key_handler_t handler)
{
	key_handler = handler;
}

void keypad_poll()
{
	static uint16_t reg;
	static uint16_t col;
	static uint32_t buttons = 0, debounce1 = 0, debounce2 = 0;

	if (with_interrupts && !polling)
		return;

	/* start polling */
	if (polling == 1) {
		writew(0x1f & ~0x1, KBC_REG); /* first col */
		col = 0;
		polling = 2;
		return;
	}

	/* enable keypad irq after the signal settles */
	if (polling == 3) {
		if(with_interrupts) {
			irq_enable(IRQ_KEYPAD_GPIO);
			polling = 0;
		} else {
			polling = 1;
		}
		return;
	}

	reg = readw(KBR_LATCH_REG);
	buttons = (buttons & ~(0x1f << (col * 5)))
		| ((~reg & 0x1f) << (col * 5 ));
	/* if key is released, stay in column for faster debounce */
	if ((debounce1 | debounce2) & ~buttons) {
		debounce2 = debounce1;
		debounce1 = buttons;
		return;
	}

	col++;
	if (col > 5) {
		uint32_t pwr_mask = (1 << btn_map[KEY_POWER]);
		col = 0;
		/* if power button, ignore other states */
		if (buttons & pwr_mask)
			buttons = lastbuttons | pwr_mask;
		else if (lastbuttons & pwr_mask)
			buttons = lastbuttons & ~pwr_mask;
		dispatch_buttons(buttons);
		if (buttons == 0) {
			writew(0x0, KBC_REG);
			polling = 3;
			return;
		}
	}
	if (col == 5)
		writew(0xff, KBC_REG);
	else
		writew(0x1f & ~(0x1 << col ), KBC_REG);

}
