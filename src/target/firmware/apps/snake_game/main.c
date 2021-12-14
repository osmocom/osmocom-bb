/* The game Snake as Free Software for Calypso Phone */

/* (C) 2013 by Marcel `sdrfnord` McKinnon <sdrfnord@gmx.de>
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
 */

#include <stdio.h>
#include <string.h>

#include <debug.h>
#define DEBUG 1
#define KNRM  "\x1B[0m"
#define UNDERLINE  "\x1B[4m"

#include <memory.h>
#include <delay.h>
#include <rffe.h>
#include <keypad.h>
#include <board.h>
#include <abb/twl3025.h>
#include <rf/trf6151.h>
#include <calypso/clock.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/dsp.h>
#include <calypso/irq.h>
#include <calypso/misc.h>
#include <calypso/backlight.h>
#include <comm/sercomm.h>
#include <comm/timer.h>
#include <fb/framebuffer.h>
#include <battery/battery.h>

unsigned long next = 1;
/* This is not a good random number generator ... */
int rand(void)
{
	next = next * 110351 + 12;
	return (unsigned int)(next & 0x7fff);
}

void srand(unsigned int seed)
{
	next = seed;
}

#define BLANK 0
#define HEAD 1
#define TAIL 2
#define HEAD_FOOD 3
#define FOOD 9
#define SBODY 20
/* The numbers above 20 are the distance to the head.
 * 21 is direly behind the head.
 */
#define STDLEN 3
#define HEIGHT 7
#define WIDTH 16

/* Time in ms to wait to the next auto move of the snake. */
#define WAIT_TIME_AUTOMOVE 300

struct position {
	int x;
	int y;
} pos;

uint8_t field[WIDTH][HEIGHT];
int16_t score = 0, lenght = 0;
enum errors { ALLRIGHT, SNAKE_COL } err;

void printField();
void setItem(int, int, int);
void movepos(char);
void increaseBodyAge();
void setFood()
{
	int x, y, c;
	for (c = 0; c < 10; c++) {
		x = rand() % (WIDTH - 1);
		y = rand() % (HEIGHT - 1);
#if DEBUG > 0
		printf("Next %u\n", next);
		printf("Rand (%d|%d)\n", x, y);
#endif
		if (field[x][y] == BLANK) {
			field[x][y] = FOOD;
			return;
		}
	}
	for (x = 0; x < WIDTH; x++) {
		for (y = 0; y < HEIGHT; y++) {
			if (field[x][y] == BLANK) {
				field[x][y] = FOOD;
#if DEBUG > 0
				printf("Set without rand (%d|%d) %d\n", x, y,
				       c);
#endif
				return;
			}
		}
	}
}

static void print_snake_str(char *text, int16_t x, int16_t y)
{
	x = 6 * x;
	y = 8 * (y + 1) - 3;
#if DEBUG > 1
	printf("Put string %s to (%d|%d)\n", text, x, y);
#endif
	fb_gotoxy(x, y);
	fb_putstr(text, framebuffer->width);
}

char Move;
void movepos(char move)
{
	Move = move;
	setItem(pos.x, pos.y, SBODY);
	switch (move) {
	case 'h': pos.x--; break;
	case 'j': pos.y++; break;
	case 'k': pos.y--; break;
	case 'l': pos.x++; break;
	}
	switch (move) {
	case 'j':
	case 'k':
		if (pos.y == -1)
			pos.y = HEIGHT - 1;
		else if (pos.y == HEIGHT)
			pos.y = 0;
		increaseBodyAge();
		break;
	case 'l':
	case 'h':
		if (pos.x == -1)
			pos.x = WIDTH - 1;
		else if (pos.x == WIDTH)
			pos.x = 0;
		increaseBodyAge();
		break;
	}
	setItem(pos.x, pos.y, HEAD);
	printField();
}

void movepos_timer_cb(void *p)
{
	struct osmo_timer_list *tmr = (struct osmo_timer_list *)p;
#if DEBUG > 0
	printf("Auto move %c\n", Move);
#endif
	movepos(Move);

	osmo_timer_schedule(tmr, WAIT_TIME_AUTOMOVE);
}

static struct osmo_timer_list move_snake_timer = {
	.cb = &movepos_timer_cb,
	.data = &move_snake_timer
};

void movepos_keypress(char keypress)
{
	Move = keypress;
	osmo_timer_schedule(&move_snake_timer, 0);
}

void increaseBodyAge()
{
	int y, x;
	lenght = SBODY + STDLEN + score;
	for (x = 0; x < WIDTH; x++) {
		for (y = 0; y < HEIGHT; y++) {
			if (field[x][y] >= lenght)
				field[x][y] = BLANK;
			else if (field[x][y] >= SBODY)
				field[x][y]++;
		}
	}
}

void setItem(int x, int y, int item)
{
	if (item == HEAD) {
		switch (field[x][y]) {
		case FOOD:
			score++;
			setFood();
			item = HEAD_FOOD;
			break;
		case BLANK:
			break;
		default:
			err = SNAKE_COL;
			score--;
		}
	}
	field[x][y] = item;
}

void resetField()
{
	/* system("clear"); */
	printf("\033[H\033[2J");
}

void printField()
{
	fb_clear();
	int x, y;
	for (y = 0; y < HEIGHT; y++) {
		for (x = 0; x < WIDTH; x++) {
			switch (field[x][y]) {
			case BLANK:
				break;
			case HEAD:
				print_snake_str("O", x, y);
				break;
			case HEAD_FOOD:
				print_snake_str("P", x, y);
				break;
			case FOOD:
				print_snake_str("#", x, y);
				break;
			default:
				if (field[x][y] == lenght)
					print_snake_str(";", x, y);
				else
					print_snake_str("o", x, y);
			}
		}

	}
	printf("Score: %d\n", score);
	fb_gotoxy(0, framebuffer->height - 9);
	fb_lineto(framebuffer->width - 1, framebuffer->cursor_y);
	fb_gotoxy(0, framebuffer->height - 1);
	char text[16];
	switch (err) {
	case SNAKE_COL:
		fb_putstr("The snake ate itself!!!", framebuffer->width);
		err = ALLRIGHT;
		break;
	default:
		sprintf(text, "Score: %d", score);
		fb_putstr(text, framebuffer->width);
		framebuffer->cursor_x = 45;
		fb_putstr("OsmocomBB", framebuffer->width);
	}
	fb_flush();

#if DEBUG > 0
	printf("Pos X: %d, Y: %d\n", pos.x, pos.y);
	printf("\n\n");
	for (y = -1; y < HEIGHT; y++) {
		for (x = -1; x < WIDTH; x++) {
			if (y == -1 || x == -1) {
				if (x == -1)
					printf(" %2d: ", y);
				else if (y == -1)
					printf(UNDERLINE " %2d" KNRM,
							x);
			} else
				printf(" %2d", field[x][y]);
		}
		puts("\n");
	}
#endif
}

int cursor = 0;
#define NEIGH_LINES	((framebuffer->height +8) / 8)
static void print_display(char *text, int *y, int c)
{
	/* skip lines, given by cursor */
	(*y)++;
	if (c >= (*y)) {
		printf("Line %d: Return c >= y\n", *y);
		return;
	}
	/* skip, if end of display area is reached */
	if ((*y) - c > NEIGH_LINES) {
		printf("Line %d: Return\n", *y);
		return;
	}

	fb_gotoxy(0, -3 + (((*y) - c - 1) << 3));
	fb_putstr(text, framebuffer->width);
}

void fb_clear_fancy(uint8_t delay)
{
	int16_t x, y;
	fb_setfg(FB_COLOR_BLACK);
	fb_setbg(FB_COLOR_WHITE);
	for (x = 0; x < framebuffer->width; x++) {
		for (y = 0; y < framebuffer->height; y++) {
			fb_set_p(x, y);
		}
		fb_flush();
		delay_ms(delay);
	}
	fb_setfg(FB_COLOR_WHITE);
	fb_setbg(FB_COLOR_BLACK);
	/* for (x = 0; x < framebuffer->width; x++) { */
	for (; x >= 0; x--) {
		for (y = 0; y < framebuffer->height; y++) {
			fb_set_p(x, y);
		}
		fb_flush();
		delay_ms(delay);
	}
	fb_setfg(FB_COLOR_BLACK);
	fb_setbg(FB_COLOR_WHITE);
}

void intro()
{
	fb_setfg(FB_COLOR_BLACK);
	fb_setbg(FB_COLOR_WHITE);
	fb_setfont(FB_FONT_HELVB14);

	fb_gotoxy(framebuffer->width / 2 - 7 * 3, 15);
	fb_putstr("Snake", framebuffer->width - 4);

	fb_gotoxy(14, framebuffer->height - 5);
	fb_setfont(FB_FONT_HELVR08);
	fb_putstr("Version: " GIT_SHORTHASH, framebuffer->width - 4);
	fb_gotoxy(0, 0);
	fb_boxto(framebuffer->width - 1, 1);
	fb_boxto(framebuffer->width - 2, framebuffer->height - 1);
	fb_boxto(0, framebuffer->height - 2);
	fb_boxto(1, 1);

	printf("(%u, %u)\n", framebuffer->width, framebuffer->height);
	fb_gotoxy(2, 2);
	fb_lineto(framebuffer->width - 3, framebuffer->height - 3);
	fb_gotoxy(2, framebuffer->height - 3);
	fb_lineto(framebuffer->width - 3, 2);
	fb_flush();
}

/* Main Program */
const char *hr =
    "======================================================================\n";

void key_handler(enum key_codes code, enum key_states state);

static void console_rx_cb(uint8_t dlci, struct msgb *msg)
{
	if (dlci != SC_DLCI_CONSOLE) {
		printf("Message for unknown DLCI %u\n", dlci);
		return;
	}

	printf("Message on console DLCI: '%s'\n", msg->data);
	msgb_free(msg);
}

static void l1a_l23_rx_cb(uint8_t dlci, struct msgb *msg)
{
	int i;
	puts("l1a_l23_rx_cb: ");
	for (i = 0; i < msg->len; i++)
		printf("%02x ", msg->data[i]);
	puts("\n");
}

int main(void)
{
	board_init(1);

	puts("\n\nOsmocomBB Test sdrfnord (revision " GIT_REVISION ")\n");
	puts(hr);

	/* Dump device identification */
	dump_dev_id();
	puts(hr);

	/* Dump clock config before PLL set */
	calypso_clk_dump();
	puts(hr);

	/* Dump clock config after PLL set */
	calypso_clk_dump();
	puts(hr);

	fb_clear();
	bl_level(255);
	osmo_timers_update();

	intro();
	delay_ms(5000);
	fb_clear_fancy(20);

	fb_setfg(FB_COLOR_BLACK);
	fb_setbg(FB_COLOR_WHITE);
	fb_setfont(FB_FONT_HELVR08);
	fb_flush();

	pos.x = framebuffer->width / (6 * 2);
	pos.y = framebuffer->height / (8 * 2);
	setItem(pos.x, pos.y, HEAD);

	while (battery_info.bat_volt_mV == 0)
		osmo_timers_update();
	srand(battery_info.bat_volt_mV);
#if DEBUG > 0
	printf("Initialize random number generator with %d\n",
			battery_info.bat_volt_mV);
#endif
	setFood();
	printField();

	sercomm_register_rx_cb(SC_DLCI_CONSOLE, console_rx_cb);
	sercomm_register_rx_cb(SC_DLCI_L1A_L23, l1a_l23_rx_cb);
	keypad_set_handler(&key_handler);

	/* beyond this point we only react to interrupts */
	puts("entering interrupt loop\n");
	while (1) {
		osmo_timers_update();
	}

	twl3025_power_off();

	while (1) {
	}
}

void key_handler(enum key_codes code, enum key_states state)
{
	if (!osmo_timer_pending(&move_snake_timer)) {
		osmo_timer_schedule(&move_snake_timer, WAIT_TIME_AUTOMOVE);
	}
	if (state != PRESSED)
		return;

	switch (code) {
	case KEY_0:
		bl_level(0);
		break;
	case KEY_1:
		bl_level(10);
		break;
	case KEY_2:
		movepos_keypress('k');
		break;
	case KEY_3:
		bl_level(30);
		break;
	case KEY_4:
		movepos_keypress('h');
		break;
	case KEY_5:
		bl_level(50);
		break;
	case KEY_6:
		movepos_keypress('l');
		break;
	case KEY_7:
		bl_level(150);
		break;
	case KEY_8:
		movepos_keypress('j');
		break;
	case KEY_9:
		bl_level(255);
		break;
		// used to be display_puts...
		break;
	case KEY_STAR:
		// used to be display puts...
		break;
	case KEY_HASH:
		// used to be display puts...
		break;
	case KEY_LEFT_SB:
		bl_mode_pwl(1);
		break;
	case KEY_RIGHT_SB:
		bl_mode_pwl(0);
		break;
	case KEY_POWER:
		twl3025_power_off_now();
		break;
	case KEY_RIGHT:
		movepos_keypress('l');
		break;
	case KEY_LEFT:
		movepos_keypress('h');
		break;
	case KEY_UP:
		movepos_keypress('k');
		break;
	case KEY_DOWN:
		movepos_keypress('j');
		break;
	default:
		break;
	}
}
