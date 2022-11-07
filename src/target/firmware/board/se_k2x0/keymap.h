/* Keymap for SE K200i/K220i */
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
	[KEY_MENU]	= 21,	/* not existent */
	[KEY_LEFT_SB]	= 0,
	[KEY_RIGHT_SB]	= 5,
	[KEY_UP]	= 16,
	[KEY_DOWN]	= 15,
	[KEY_LEFT]	= 17,
	[KEY_RIGHT]	= 18,
	[KEY_OK]	= 10,
/* power button is not connected to keypad scan matrix but to TWL3025 */
	[KEY_POWER]	= 31,
	[KEY_MINUS]	= 22,	/* not existent */
	[KEY_PLUS]	= 23,	/* not existent */
	[KEY_CAMERA]	= 24,	/* not existent */
};
