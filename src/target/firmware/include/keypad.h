#ifndef _KEYPAD_H
#define _KEYPAD_H

enum buttons {
	BTN_0		= 0x00002000,
	BTN_1		= 0x00008000,
	BTN_2		= 0x00000400,
	BTN_3		= 0x00000020,
	BTN_4		= 0x00010000,
	BTN_5		= 0x00000800,
	BTN_6		= 0x00000040,
	BTN_7		= 0x00020000,
	BTN_8		= 0x00001000,
	BTN_9		= 0x00000080,
	BTN_STAR	= 0x00040000,
	BTN_HASH	= 0x00000100,
	BTN_MENU	= 0x00004000,
	BTN_LEFT_SB	= 0x00080000,
	BTN_RIGHT_SB	= 0x00000200,
	BTN_UP		= 0x00000002,
	BTN_DOWN	= 0x00000004,
	BTN_LEFT	= 0x00000008,
	BTN_RIGHT	= 0x00000010,
	BTN_OK		= 0x00000001,
	BTN_POWER	= 0x01000000,
};

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
	KEY_INV = 0xFF
};

enum key_states {
	PRESSED,
	RELEASED,
};

void keypad_init();

void keypad_scan();

typedef void (*key_handler_t)(enum key_codes code, enum key_states state);

void keypad_set_handler(key_handler_t handler);

#endif /* KEYPAD_H */
