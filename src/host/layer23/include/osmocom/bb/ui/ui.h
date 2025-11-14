#ifndef _libui_h
#define _libui_h

#include <osmocom/core/select.h>

#define UI_ROWS		8
#define UI_COLS		12
#define UI_TARGET	0

enum ui_key {
	UI_KEY_0 = '0',
	UI_KEY_1 = '1',
	UI_KEY_2 = '2',
	UI_KEY_3 = '3',
	UI_KEY_4 = '4',
	UI_KEY_5 = '5',
	UI_KEY_6 = '6',
	UI_KEY_7 = '7',
	UI_KEY_8 = '8',
	UI_KEY_9 = '9',
	UI_KEY_STAR = '*',
	UI_KEY_HASH = '#',
	UI_KEY_F1 = 1,
	UI_KEY_F2 = 2,
	UI_KEY_PICKUP = 26,
	UI_KEY_HANGUP = 27,
	UI_KEY_UP = 28,
	UI_KEY_DOWN = 29,
	UI_KEY_LEFT = 30,
	UI_KEY_RIGHT = 31,
};

union ui_view_data {
	struct {
		int lines;
		const char **text;
		int vpos;
	} listview;
	struct {
		int lines;
		const char **text;
		int vpos;
		int cursor;
	} selectview;
	struct {
		char *number;
		int num_len;
		int pos;
		int options;
		int options_pos;
	} stringview;
	struct {
		unsigned int value;
		int sign;
		int min, max;
	} intview;
};

struct ui_inst;

struct ui_view {
	const char *name;
	int (*init)(struct ui_view *uv, union ui_view_data *ud);
	int (*keypad)(struct ui_inst *ui, struct ui_view *uv,
		union ui_view_data *ud, enum ui_key kp);
	int (*display)(struct ui_inst *ui, union ui_view_data *ud);
};

struct ui_inst {
	struct ui_view *uv;
	const char *title;
	const char *bottom_line;
	union ui_view_data ud;
	int (*key_cb)(struct ui_inst *ui, enum ui_key kp);
	int (*beep_cb)(struct ui_inst *ui);
	/* display */
	char buffer[(UI_COLS + 1) * UI_ROWS];
	int cursor_on, cursor_x, cursor_y;
	/* telnet */
	void *tall_telnet_ctx;
	struct osmo_fd server_socket;
	struct llist_head active_connections;
	int (*telnet_cb)(struct ui_inst *ui);
};

extern struct ui_view ui_listview;
extern struct ui_view ui_selectview;
extern struct ui_view ui_stringview;
extern struct ui_view ui_intview;

int ui_inst_init(struct ui_inst *ui, struct ui_view *uv,
	int (*key_cb)(struct ui_inst *ui, enum ui_key kp),
	int (*beep_cb)(struct ui_inst *ui),
	int (*telnet_cb)(struct ui_inst *ui));
int ui_inst_keypad(struct ui_inst *ui, enum ui_key kp);
int ui_inst_refresh(struct ui_inst *ui);

#endif /* _libui_h */
