/* (C) 2011 by Andreas Eversberg <jolly@eversberg.eu>
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

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <osmocom/core/select.h>
#include <osmocom/bb/ui/ui.h>
#include <osmocom/bb/ui/telnet_interface.h>
#include <osmocom/bb/common/l1ctl.h>

static char *ui_center(const char *text)
{
	static char line[UI_COLS + 1];
	int len, shift;

	strncpy(line, text, UI_COLS);
	line[UI_COLS] = '\0';
	len = strlen(line);
	if (len + 1 < UI_COLS) {
		shift = (UI_COLS - len) / 2;
		memcpy(line + shift, line, len + 1);
		memset(line, ' ', shift);
	}

	return line;
}

/*
 * io functions
 */

int ui_clearhome(struct ui_inst *ui)
{
	int i;

	/* initialize with spaces */
	memset(ui->buffer, ' ', sizeof(ui->buffer));
	/* terminate with EOL */
	for (i = 0; i < UI_ROWS; i++)
		ui->buffer[(UI_COLS + 1) * (i + 1) - 1] = '\0';

	ui->cursor_x = ui->cursor_y = 0;
	ui->cursor_on = 0;

	return 0;
}

int ui_puts(struct ui_inst *ui, int ln, const char *text)
{
	int len = strlen(text);

	/* out of range */
	if (ln < 0 || ln >= UI_ROWS)
		return -EINVAL;

	/* clip */
	if (len > UI_COLS)
		len = UI_COLS;

	/* copy */
	if (len)
		memcpy(ui->buffer + (UI_COLS + 1) * ln, text, len);

	return 0;
}

int ui_flush(struct ui_inst *ui)
{
	int i;
	char frame[UI_COLS + 5];
	char line[UI_COLS + 5];
	char cursor[16];

	/* clear */
	ui_telnet_puts(ui, "\033c");

	/* display */
	memset(frame + 1, '-', UI_COLS);
	frame[0] = frame[UI_COLS + 1] = '+';
	frame[UI_COLS + 2] = '\r';
	frame[UI_COLS + 3] = '\n';
	frame[UI_COLS + 4] = '\0';
	ui_telnet_puts(ui, frame);
	for (i = 0; i < UI_ROWS; i++) {
		sprintf(line, "|%s|\r\n", ui->buffer + (UI_COLS + 1) * i);
		{
			// HACK
			struct gsm_ui *gui = container_of(ui, struct gsm_ui, ui);
			struct osmocom_ms *ms = container_of(gui, struct osmocom_ms, gui);
			l1ctl_tx_display_req(ms, 1, 8 + i * 8, 0, 1, 0, i == 0, i == UI_ROWS - 1 && !ui->cursor_on, ui->buffer + (UI_COLS + 1) * i);
		}
		ui_telnet_puts(ui, line);
	}
	ui_telnet_puts(ui, frame);

	ui_telnet_puts(ui, "\r\n"
		"1 2 3 4 5 6 7 8 9 * 0 # = digits\r\n"
		"Pos1 = pickup, End = hangup\r\n"
		"F1 = left button, F2 = right button\r\n"
		"arrow keys = navigation buttons\r\n"); 

	/* set cursor */
	if (ui->cursor_on) {
		sprintf(cursor, "\033[%d;%dH", ui->cursor_y + 2,
			ui->cursor_x + 2);
		{
			char c[] = "x";
			// HACK
			struct gsm_ui *gui = container_of(ui, struct gsm_ui, ui);
			struct osmocom_ms *ms = container_of(gui, struct osmocom_ms, gui);
			c[0] = ui->buffer[(UI_COLS + 1) * ui->cursor_y + ui->cursor_x];
			l1ctl_tx_display_req(ms, 1 + 8 * ui->cursor_x, 8 + 8 * ui->cursor_y, 1, 0, 0, 0, 1, c);
		}
		ui_telnet_puts(ui, cursor);
	}

	return 0;
}

static int bottom_puts(struct ui_inst *ui, const char *text)
{
	char bottom_line[UI_COLS + 1], *p;
	int space;

	strncpy(bottom_line, text, UI_COLS);
	bottom_line[UI_COLS] = '\0';
	if ((p = strchr(bottom_line, ' '))
	 && (space = UI_COLS - strlen(bottom_line))) {
	 	p++;
	 	memcpy(p + space, p, strlen(p));
		memset(p, ' ', space);
	}

	return ui_puts(ui, UI_ROWS - 1, bottom_line);
}

/*
 * listview
 */

static int init_listview(struct ui_view *uv, union ui_view_data *ud)
{
	ud->listview.vpos = 0;
	ud->listview.lines = 0;

	return 0;
}

static int keypad_listview(struct ui_inst *ui, struct ui_view *uv,
	union ui_view_data *ud, enum ui_key kp)
{
	int rows = UI_ROWS;

	if (ui->title)
		rows--;
	if (ui->bottom_line)
		rows--;

	switch (kp) {
	case UI_KEY_UP:
		if (ud->listview.vpos == 0)
			return -1;
		ud->listview.vpos--;
		break;
	case UI_KEY_DOWN:
		if (rows + ud->listview.vpos >= ud->listview.lines)
			return -1;
		ud->listview.vpos++;
		break;
	default:
		return 0;
	}
	/* refresh display */
	uv->display(ui, ud);

	return 0;
}

static int display_listview(struct ui_inst *ui, union ui_view_data *ud)
{
	const char **text = ud->listview.text;
	int lines = ud->listview.lines;
	int i, j = 0;
	int rows = UI_ROWS;
	
	if (ui->bottom_line)
		rows--;

	/* vpos will skip lines */
	for (i = 0; i < ud->listview.vpos; i++) {
		/* if we reached end of test, we leave the pointer there */
		if (*text == NULL)
			break;
		text++;
		j++;
	}

	ui_clearhome(ui);
	/* title */
	i = 0;
	if (ui->title)
		ui_puts(ui, i++, ui_center(ui->title));
	for (; i < rows; i++) {
		if (*text && j < lines) {
			ui_puts(ui, i, *text);
			text++;
			j++;
		} else
			break;
//			ui_puts(ui, i, "~");
	}
	if (ui->bottom_line)
		bottom_puts(ui, ui->bottom_line);
	ui_flush(ui);

	return 0;
}

/*
 * selectview
 */

static int init_selectview(struct ui_view *uv, union ui_view_data *ud)
{
	ud->selectview.vpos = 0;
	ud->selectview.cursor = 0;
	ud->selectview.lines = 0;

	return 0;
}

static int keypad_selectview(struct ui_inst *ui, struct ui_view *uv,
	union ui_view_data *ud, enum ui_key kp)
{
	int rows = UI_ROWS;
	
	if (ui->title)
		rows--;
	if (ui->bottom_line)
		rows--;

	switch (kp) {
	case UI_KEY_UP:
		if (ud->selectview.cursor == 0)
			return -1;
		ud->selectview.cursor--;
		/* follow cursor */
		if (ud->selectview.cursor < ud->selectview.vpos)
			ud->selectview.vpos = ud->selectview.cursor;
		break;
	case UI_KEY_DOWN:
		if (ud->selectview.cursor >= ud->selectview.lines - 1)
			return -1;
		ud->selectview.cursor++;
		/* follow cursor */
		if (ud->selectview.cursor > ud->selectview.vpos + rows - 1)
			ud->selectview.vpos = ud->selectview.cursor -
								(rows - 1);
		break;
	default:
		return 0;
	}
	/* refresh display */
	uv->display(ui, ud);

	return 0;
}

static int display_selectview(struct ui_inst *ui, union ui_view_data *ud)
{
	const char **text = ud->selectview.text;
	int lines = ud->selectview.lines;
	int i, j = 0, y = 0;
	int rows = UI_ROWS;
	char line[UI_COLS + 1];
	
	if (ui->bottom_line)
		rows--;

	/* vpos will skip lines */
	for (i = 0; i < ud->selectview.vpos; i++) {
		/* if we reached end of test, we leave the pointer there */
		if (*text == NULL)
			break;
		text++;
		j++;
	}

	ui_clearhome(ui);
	/* title */
	i = 0;
	if (ui->title)
		ui_puts(ui, i++, ui_center(ui->title));
	for (; i < rows; i++) {
		if (*text && j < lines) {
			if (ud->selectview.cursor == j)
				y = i;
			strncpy(line, *text, UI_COLS);
			line[UI_COLS] = '\0';
			ui_puts(ui, i, line);
			text++;
			j++;
		} else
			break;
//			ui_puts(ui, i, "~");
	}
	if (ui->bottom_line)
		bottom_puts(ui, ui->bottom_line);
	ui->cursor_on = 1;
	ui->cursor_x = 0;
	ui->cursor_y = y;
	ui_flush(ui);

	return 0;
}

/*
 * stringview
 */

static int init_stringview(struct ui_view *uv, union ui_view_data *ud)
{
	ud->stringview.options = 0;
	ud->stringview.pos = 0;

	return 0;
}

static int keypad_stringview(struct ui_inst *ui, struct ui_view *uv,
	union ui_view_data *ud, enum ui_key kp);

static int keypad_stringview_options(struct ui_inst *ui, struct ui_view *uv,
	union ui_view_data *ud, enum ui_key kp)
{
	switch (kp) {
	case UI_KEY_F1: /* back */
		ud->stringview.options = 0;
		break;
	case UI_KEY_1:
		ud->stringview.options = 0;
		return keypad_stringview(ui, uv, ud, 'a');
	case UI_KEY_2:
		ud->stringview.options = 0;
		return keypad_stringview(ui, uv, ud, 'b');
	case UI_KEY_3:
		ud->stringview.options = 0;
		return keypad_stringview(ui, uv, ud, 'c');
	case UI_KEY_0:
		ud->stringview.options = 0;
		return keypad_stringview(ui, uv, ud, '+');
	default:
		return 0;
	}
	/* refresh display */
	uv->display(ui, ud);

	return 0;
}

static int keypad_stringview(struct ui_inst *ui, struct ui_view *uv,
	union ui_view_data *ud, enum ui_key kp)
{
	if (ud->stringview.options)
		return keypad_stringview_options(ui, uv, ud, kp);

	switch (kp) {
	case UI_KEY_STAR:
	case UI_KEY_HASH:
	case UI_KEY_1:
	case UI_KEY_2:
	case UI_KEY_3:
	case UI_KEY_4:
	case UI_KEY_5:
	case UI_KEY_6:
	case UI_KEY_7:
	case UI_KEY_8:
	case UI_KEY_9:
	case UI_KEY_0:
	case 'a':
	case 'b':
	case 'c':
	case '+':
		/* check if number is full */
		if (strlen(ud->stringview.number) + 1 == ud->stringview.num_len)
			return -1;
		/* add digit */
		if (ud->stringview.number[ud->stringview.pos] == '\0') {
			/* add to the end */
			ud->stringview.number[ud->stringview.pos] = kp;
			ud->stringview.pos++;
			ud->stringview.number[ud->stringview.pos] = '\0';
		} else {
			/* insert digit */
			memcpy(ud->stringview.number + ud->stringview.pos + 1,
				ud->stringview.number + ud->stringview.pos,
				strlen(ud->stringview.number +
							ud->stringview.pos)
					+ 1);
			ud->stringview.number[ud->stringview.pos] = kp;
			ud->stringview.pos++;
		}
		break;
	case UI_KEY_LEFT:
		if (ud->stringview.pos == 0)
			return -1;
		ud->stringview.pos--;
		break;
	case UI_KEY_RIGHT:
		if (ud->stringview.pos == strlen(ud->stringview.number))
			return -1;
		ud->stringview.pos++;
		break;
	case UI_KEY_UP: /* select options */
		ud->stringview.options = 1;
		ud->stringview.options_pos = 0;
		break;
	case UI_KEY_F1: /* clear */
		ud->stringview.pos = 0;
		ud->stringview.number[0] = '\0';
		break;
	case UI_KEY_F2: /* delete */
		if (ud->stringview.pos == 0)
			return -1;
		/* del digit */
		if (ud->stringview.number[ud->stringview.pos] == '\0') {
			/* del digit from the end */
			ud->stringview.pos--;
			ud->stringview.number[ud->stringview.pos] = '\0';
		} else {
			/* remove digit */
			memcpy(ud->stringview.number + ud->stringview.pos - 1,
				ud->stringview.number + ud->stringview.pos,
				strlen(ud->stringview.number +
							ud->stringview.pos)
					+ 1);
			ud->stringview.pos--;
		}
		break;
	default:
		return 0;
	}
	/* refresh display */
	uv->display(ui, ud);

	return 0;
}

static int display_stringview(struct ui_inst *ui, union ui_view_data *ud)
{
	char line[UI_COLS + 1];
	char *p = ud->stringview.number;
	int len = strlen(p);
	int i = 1, y;

	/* options screen */
	if (ud->stringview.options) {
		ui_clearhome(ui);
		ui_puts(ui, 0, ui_center("Extra Keys"));
		ui_puts(ui, 2, "1:a 2:b 3:c");
		ui_puts(ui, 3, "4:  5:  6: ");
		ui_puts(ui, 4, "7:  8:  9: ");
		ui_puts(ui, 5, "*:  0:+ #: ");
		bottom_puts(ui, "back ");
		ui_flush(ui);
		return 0;
	}

	/* if number shrunk */
	if (ud->stringview.pos > len)
		ud->stringview.pos = len;

	ui_clearhome(ui);
	/* title */
	if (ui->title) {
		ui_puts(ui, i++, ui_center(ui->title));
		i++;
	}
	y = i;
	/* if line exceeds display width */
	while (len > UI_COLS) {
		memcpy(line, p, UI_COLS);
		line[UI_COLS] = '\0';
		ui_puts(ui, i++, line);
		p += UI_COLS;
		len -= UI_COLS;
	}
	/* last line */
	if (len)
		ui_puts(ui, i, p);
	/* cursor */
	ui->cursor_on = 1;
	ui->cursor_x = ud->stringview.pos % UI_COLS;
	ui->cursor_y = y + (ud->stringview.pos / UI_COLS);
	/* F-keys info */
	bottom_puts(ui, "clear del");
	ui_flush(ui);

	return 0;
}

/*
 * integer view
 */

static int init_intview(struct ui_view *uv, union ui_view_data *ud)
{
	ud->intview.min = -128;
	ud->intview.max = 127;
	ud->intview.value = 0;

	return 0;
}

static int keypad_intview(struct ui_inst *ui, struct ui_view *uv,
	union ui_view_data *ud, enum ui_key kp)
{
	int value;

	switch (kp) {
	case UI_KEY_1:
	case UI_KEY_2:
	case UI_KEY_3:
	case UI_KEY_4:
	case UI_KEY_5:
	case UI_KEY_6:
	case UI_KEY_7:
	case UI_KEY_8:
	case UI_KEY_9:
	case UI_KEY_0:
		value = ud->intview.value;
		value = value * 10 + kp - UI_KEY_0;
		/* if additional digit would cause overflow (or no change) */
		if (value <= ud->intview.value)
			return - 1;
		if (value > 0x7fffffff)
			return - 1;
		ud->intview.value = value;
		break;
	case UI_KEY_STAR:
		ud->intview.sign = 1 - ud->intview.sign;
		break;
	case UI_KEY_UP:
	case UI_KEY_DOWN:
		if (ud->intview.sign)
			value = 0 - ud->intview.value;
		else
			value = ud->intview.value;
		/* check if limit is already reached */
		if (kp == UI_KEY_UP && value == ud->intview.max)
			return -1;
		if (kp == UI_KEY_DOWN && value == ud->intview.min)
			return -1;
		/* if value out of range, put it in range */
		if (value > ud->intview.max) {
			value = ud->intview.max;
			goto store_value;
		}
		if (value < ud->intview.min) {
			value = ud->intview.min;
			goto store_value;
		}
		if (kp == UI_KEY_UP)
			value++;
		else
			value--;
		goto store_value;
	case UI_KEY_LEFT: /* delete */
		/* already 0 */
		if (ud->intview.value == 0)
			return -1;
		ud->intview.value /= 10;
		break;
	default:
		/* if other key is pressed, make value fit in range */
		if (ud->intview.sign)
			value = 0 - ud->intview.value;
		else
			value = ud->intview.value;
		if (value < ud->intview.min) {
			value = ud->intview.min;
			goto store_value;
		}
		if (value > ud->intview.max) {
			value = ud->intview.max;
			goto store_value;
		}
		return 0;
	}
	/* refresh display */
	uv->display(ui, ud);

	return 0;

store_value:
	/* store new value */
	if (value < 0) {
		ud->intview.value = 0 - value;
		ud->intview.sign = 1;
	} else {
		ud->intview.value = value;
		ud->intview.sign = 0;
	}
	/* refresh display */
	uv->display(ui, ud);

	return 0;
}

static int display_intview(struct ui_inst *ui, union ui_view_data *ud)
{
	char line[UI_COLS + 2];
	int i = 1, y, x = 1;
	int value;

	ui_clearhome(ui);
	/* title */
	if (ui->title) {
		ui_puts(ui, i++, ui_center(ui->title));
		i++;
	}
	y = i;
	/* value */
	if (ud->intview.sign)
		line[0] = '-';
	else
		line[0] = ' ';
	sprintf(line + 1, "%d", ud->intview.value);
	ui_puts(ui, i++, line);
	/* range */
	i++;
	snprintf(line, UI_COLS + 1, "(%d..%d)", ud->intview.min,
		ud->intview.max);
	line[UI_COLS] = '\0';
	ui_puts(ui, i++, line);
	/* cursor */
	value = ud->intview.value;
	while (value) {
		x++;
		value /= 10;
	}
	ui->cursor_on = 1;
	ui->cursor_x = x;
	ui->cursor_y = y;
	/* F-keys info */
	if (ui->bottom_line)
		bottom_puts(ui, ui->bottom_line);
	ui_flush(ui);

	return 0;
}

/*
 * structure of all views
 */

struct ui_view ui_listview = {
	.init = init_listview,
	.keypad = keypad_listview,
	.display = display_listview,
};

struct ui_view ui_selectview = {
	.init = init_selectview,
	.keypad = keypad_selectview,
	.display = display_selectview,
};

struct ui_view ui_stringview = {
	.init = init_stringview,
	.keypad = keypad_stringview,
	.display = display_stringview,
};

struct ui_view ui_intview = {
	.init = init_intview,
	.keypad = keypad_intview,
	.display = display_intview,
};

/*
 * instance handling
 */

int ui_inst_init(struct ui_inst *ui, struct ui_view *uv,
	int (*key_cb)(struct ui_inst *ui, enum ui_key kp),
	int (*beep_cb)(struct ui_inst *ui),
	int (*telnet_cb)(struct ui_inst *ui))
{
	ui->uv = uv;
	ui->key_cb = key_cb;
	ui->beep_cb = beep_cb;
	ui->telnet_cb = telnet_cb;

	ui_clearhome(ui);

	/* initialize view */
	uv->init(uv, &ui->ud);

	return 0;
}

int ui_inst_refresh(struct ui_inst *ui)
{
	/* refresh display */
	return ui->uv->display(ui, &ui->ud);
}

/* process keypress at user interface */
int ui_inst_keypad(struct ui_inst *ui, enum ui_key kp)
{
	int rc;

	/* first check if key is handled by callback */
	rc = ui->key_cb(ui, kp);
	if (rc)
		return rc; /* must exit, since key_cb() may reconfigure UI */

	rc = ui->uv->keypad(ui, ui->uv, &ui->ud, kp);
	if (rc < 0)
		ui->beep_cb(ui);

	return rc;
}

