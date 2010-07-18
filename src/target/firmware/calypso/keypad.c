/* Driver for the keypad attached to the TI Calypso */

/* (C) 2010 by roh <roh@hyte.de>
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


#define KBR_LATCH_REG	0xfffe480a
#define KBC_REG		0xfffe480c
#define KBD_GPIO_INT	0xfffe4816
#define KBD_GPIO_MASKIT	0xfffe4818

static key_handler_t key_handler = NULL;

void emit_key(uint8_t key, uint8_t state)
{
	printf("key=%u %s\n", key, state == PRESSED ? "pressed" : "released");

	if (state == RELEASED)
		if (key == KEY_POWER)
			twl3025_power_off();

	if(key_handler) {
		key_handler(key, state);
	}
}

volatile uint32_t lastbuttons = 0;

#define BTN_TO_KEY(name) \
	((diff & BTN_##name) == BTN_##name)	\
	{					\
		key = KEY_##name;		\
		diff = diff & ~BTN_##name;	\
		state = (buttons & BTN_##name) ? PRESSED : RELEASED;	\
	}

void dispatch_buttons(uint32_t buttons)
{
	uint8_t state;

	if (buttons == lastbuttons)
		return;

	uint32_t diff = buttons ^ lastbuttons;
	uint8_t key=KEY_INV;

	while (diff != 0)
	{
		if BTN_TO_KEY(POWER)
		else if BTN_TO_KEY(0)
		else if BTN_TO_KEY(1)
		else if BTN_TO_KEY(2)
		else if BTN_TO_KEY(3)
		else if BTN_TO_KEY(4)
		else if BTN_TO_KEY(5)
		else if BTN_TO_KEY(6)
		else if BTN_TO_KEY(7)
		else if BTN_TO_KEY(8)
		else if BTN_TO_KEY(9)
		else if BTN_TO_KEY(STAR)
		else if BTN_TO_KEY(HASH)
		else if BTN_TO_KEY(MENU)
		else if BTN_TO_KEY(LEFT_SB)
		else if BTN_TO_KEY(RIGHT_SB)
		else if BTN_TO_KEY(UP)
		else if BTN_TO_KEY(DOWN)
		else if BTN_TO_KEY(LEFT)
		else if BTN_TO_KEY(RIGHT)
		else if BTN_TO_KEY(OK)
		else
		{
			printf("\nunknown keycode: 0x%08x\n", diff);
			break;
		}
		emit_key(key, state);
	}
	lastbuttons = buttons;
}

static uint8_t	polling = 0;

static void keypad_irq(__unused enum irq_nr nr)
{
	/* enable polling */
	polling = 1;
	irq_disable(IRQ_KEYPAD_GPIO);
}

void keypad_init(uint8_t interrupts)
{
	lastbuttons = 0;
	polling = 0;
	writew(0, KBD_GPIO_MASKIT);
	writew(0, KBC_REG);

	if(interrupts) {
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

	if (!polling)
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
		irq_enable(IRQ_KEYPAD_GPIO);
		polling = 0;
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
	if (col > 4) {
		col = 0;
		/* if power button, ignore other states */
		if (buttons & BTN_POWER)
			buttons = lastbuttons | BTN_POWER;
		else if (lastbuttons & BTN_POWER)
			buttons = lastbuttons & ~BTN_POWER;
		dispatch_buttons(buttons);
		if (buttons == 0) {
			writew(0x0, KBC_REG);
			polling = 3;
			return;
		}
	}
	if (col == 4)
		writew(0xff, KBC_REG);
	else
		writew(0x1f & ~(0x1 << col ), KBC_REG);

}

