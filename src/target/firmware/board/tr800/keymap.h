/*
 * This code was written by Mychaela Falconia <falcon@freecalypso.org>
 * who refuses to claim copyright on it and has released it as public domain
 * instead. NO rights reserved, all rights relinquished.
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
 */

/*
 * The TR-800 module itself does not prescribe any particular keypad
 * layout - instead all 5 KBC lines and all 5 KBR lines are simply
 * brought out, allowing user applications to implement any desired
 * keypad up to 5x5 buttons.  When designing keypads for development
 * boards (whether TR800-based or "raw" Calypso), Mother Mychaela's
 * preference is to follow TI's original D-Sample key layout:

	Main keypad (21 buttons):

	L. Soft		R. Soft
		5-way nav
	Green		Red
	1	2	3
	4	5	6
	7	8	9
	*	0	#

	Left side buttons: VOL+ / VOL-
	Right side button: generic

	Row/column matrix connections:

		KBC0	KBC1	KBC2	KBC3	KBC4
	KBR0	Green	VOL-	VOL+	L_Soft	Nav_left
	KBR1	1	2	3	R_Side	Nav_right
	KBR2	4	5	6	R_Soft	Nav_up
	KBR3	7	8	9	unused	Nav_down
	KBR4	*	0	#	unused	Nav_center

	The red button is out-of-matrix PWON.

 * If anyone has an original iWOW DSK board, the connection of
 * "CALL" and "1" buttons on that board also matches the present
 * D-Sample keymap.
 */

static const uint8_t keymap[] = {
	[KEY_0]		= 9,
	[KEY_1]		= 1,
	[KEY_2]		= 6,
	[KEY_3]		= 11,
	[KEY_4]		= 2,
	[KEY_5]		= 7,
	[KEY_6]		= 12,
	[KEY_7]		= 3,
	[KEY_8]		= 8,
	[KEY_9]		= 13,
	[KEY_STAR]	= 4,
	[KEY_HASH]	= 14,
	[KEY_MENU]	= 24,
	[KEY_LEFT_SB]	= 15,
	[KEY_RIGHT_SB]	= 17,
	[KEY_UP]	= 22,
	[KEY_DOWN]	= 23,
	[KEY_LEFT]	= 20,
	[KEY_RIGHT]	= 21,
	[KEY_OK]	= 0,
/* power button is not connected to keypad scan matrix but to TWL3025 */
	[KEY_POWER]	= 31,
/* D-Sample left side buttons for volume up/down control */
	[KEY_MINUS]	= 5,
	[KEY_PLUS]	= 10,
/*
 * D-Sample right side button can be seen as equivalent to
 * Pirelli DP-L10 camera button, except for reversed history:
 * D-Sample existed first and was used by the designers of the
 * Pirelli DP-L10 phone as their starting point.
 */
	[KEY_CAMERA]	= 16,
};
