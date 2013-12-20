#ifndef _KEYPAD_H
#define _KEYPAD_H

enum key_codes {
	KEY_0	= 0,
	KEY_1,
	KEY_2,
	KEY_3,
	KEY_4,
	KEY_5,
	KEY_6,
	KEY_7,
	KEY_8,
	KEY_9,
	KEY_STAR,	//*
	KEY_HASH,	//#
	KEY_MENU,	//center of directional keys
	KEY_LEFT_SB,	//softbutton
	KEY_RIGHT_SB,	//softbutton
	KEY_UP,
	KEY_DOWN,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_OK,		//green off-hook
	KEY_POWER,	//red on-hook
	KEY_MINUS,
	KEY_PLUS,
	KEY_INV = 0xFF
};

#define BUTTON_CNT	23

enum key_states {
	PRESSED,
	RELEASED,
};

void keypad_init(const uint8_t *keymap, uint8_t interrupts);

void keypad_poll();

typedef void (*key_handler_t)(enum key_codes code, enum key_states state);

void keypad_set_handler(key_handler_t handler);

#endif /* KEYPAD_H */
