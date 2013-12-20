/* Cell Monitor of Free Software for Calypso Phone */

/* (C) 2012 by Andreas Eversberg <jolly@eversberg.eu>
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
#include <errno.h>

#include <debug.h>
#include <memory.h>
#include <delay.h>
#include <byteorder.h>
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
#include <calypso/buzzer.h>
#include <comm/sercomm.h>
#include <comm/timer.h>
#include <fb/framebuffer.h>
#include <layer1/sync.h>
#include <layer1/async.h>
#include <layer1/l23_api.h>
#include <osmocom/gsm/rsl.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/gsm48_ie.h>
#include <battery/battery.h>

enum key_codes key_code = KEY_INV;
int key_pressed = 0;
enum key_codes key_pressed_code;
unsigned long key_pressed_when;
unsigned int key_pressed_delay;

enum mode {
	MODE_MAIN,
	MODE_SPECTRUM,
	MODE_ARFCN,
	MODE_SYNC,
	MODE_RACH,
} mode = MODE_MAIN;
enum mode last_mode; /* where to return after entering ARFCN */

static uint16_t arfcn = 0, ul_arfcn;
int pcs = 0;
int uplink = 0;
int max = 0;
uint8_t power, max_power;
char input[5];
int cursor;

char *sync_result = NULL;
char *sync_msg = "";

static struct band {
	int min, max, prev, next, freq_ul, freq_dl;
} bands[] = {
        { 128, 251, 124, 512, 8242, 8692 }, /* GSM 850 */
        { 955, 124, 885, 128, 8762, 9212 }, /* P,E,R GSM */
        { 512, 885, 251, 955, 17102, 18052 }, /* DCS 1800 */
	{ 0, 0, 0, 0, 0, 0},
};

struct band *band;

#define PCS_MIN 512
#define PCS_MAX 810
#define DCS_MIN 512
#define DCS_MAX 885
#define PCS_UL 18502
#define PCS_DL 19302

enum pm_mode {
	PM_IDLE,
	PM_SENT,
	PM_RANGE_SENT,
	PM_RANGE_RESULT,
	PM_RESULT,
} pm_mode = PM_IDLE;

#define NUM_PM_DL 2
#define NUM_PM_UL 10
int pm_meas[NUM_PM_UL];
int pm_count = 0;
int pm_max = 2;
uint8_t pm_spectrum[1024];
int pm_scale = 1; /* scale measured power level */

#define TONE_JIFFIES ((HZ < 25) ? 1 : HZ / 25)
int tone = 0;
unsigned long tone_time;
int tone_on = 0;

uint8_t bsic;
uint8_t ul_levels[8], ul_max[8]; /* 8 uplink levels */
uint8_t si_1[23];
uint8_t si_2[23];
uint8_t si_2bis[23];
uint8_t si_2ter[23];
uint8_t si_3[23];
uint8_t si_4[23];
uint16_t si_new = 0, ul_new;
uint16_t mcc, mnc, lac, cell_id;
int ccch_conf;
int nb_num;
struct gsm_sysinfo_freq freq[1024];
#define NEIGH_LINES	((framebuffer->height - 25) / 8)

#define FREQ_TYPE_SERV		0x01 /* frequency of the serving cell */
#define FREQ_TYPE_NCELL		0x1c /* frequency of the neighbor cell */
#define FREQ_TYPE_NCELL_2	0x04 /* sub channel of SI 2 */
#define FREQ_TYPE_NCELL_2bis	0x08 /* sub channel of SI 2bis */
#define FREQ_TYPE_NCELL_2ter	0x10 /* sub channel of SI 2ter */

int rach = 0;
struct gsm48_req_ref rach_ref;
uint8_t rach_ra;
unsigned long rach_when;
uint8_t ta;

enum assign {
	ASSIGN_NONE,
	ASSIGN_NO_TX,
	ASSIGN_RESULT,
	ASSIGN_REJECT,
	ASSIGN_TIMEOUT,
} assign;

/* UI */

static void print_display(char *text, int *y, int c)
{
	/* skip lines, given by cursor */
	(*y)++;
	if (c >= (*y))
		return;
	/* skip, if end of display area is reached */
	if ((*y) - c > NEIGH_LINES)
		return;

	fb_gotoxy(0, 20 + (((*y) - c - 1) << 3));
	fb_putstr(text, framebuffer->width);
}

static void refresh_display(void)
{
	char text[16];
	int bat = battery_info.battery_percent;

	fb_clear();

	/* header */
	fb_setbg(FB_COLOR_WHITE);
	if (mode != MODE_SPECTRUM && !(mode == MODE_SYNC && cursor < 0)) {
		fb_setfg(FB_COLOR_BLUE);
		fb_setfont(FB_FONT_HELVR08);
		fb_gotoxy(0, 7);
		fb_putstr("Osmocom RSSI", -1);
		fb_setfg(FB_COLOR_RGB(0xc0, 0xc0, 0x00));
		fb_setfont(FB_FONT_SYMBOLS);
		fb_gotoxy(framebuffer->width - 15, 8);
		if (bat >= 100 && (battery_info.flags & BATTERY_CHG_ENABLED)
		 && !(battery_info.flags & BATTERY_CHARGING))
			fb_putstr("@HHBC", framebuffer->width);
		else {
			sprintf(text, "@%c%c%cC", (bat >= 30) ? 'B':'A',
				(bat >= 60) ? 'B':'A', (bat >= 90) ? 'B':'A');
			fb_putstr(text, framebuffer->width);
		}
		fb_gotoxy(0, 8);
		sprintf(text, "%c%cE%c%c", (power >= 40) ? 'D':'G',
			(power >= 10) ? 'D':'G', (power >= 10) ? 'F':'G',
			(power >= 40) ? 'F':'G');
		fb_putstr(text, framebuffer->width);
		fb_setfg(FB_COLOR_GREEN);
		fb_gotoxy(0, 10);
		fb_boxto(framebuffer->width - 1, 10);
	}
	fb_setfg(FB_COLOR_BLACK);
	fb_setfont(FB_FONT_C64);

	/* RACH */
	if (mode == MODE_RACH) {
		unsigned long elapsed = jiffies - rach_when;

		fb_gotoxy(0,28);
		switch (assign) {
		case ASSIGN_NONE:
			fb_putstr("Rach sent...", -1);
			break;
		case ASSIGN_RESULT:
			sprintf(text, "TA = %d", ta);
			fb_putstr(text, -1);
			fb_gotoxy(0,36);
			sprintf(text, "(%dm)", ta * 554);
			fb_putstr(text, -1);
			break;
		case ASSIGN_REJECT:
			fb_putstr("Rejected!", -1);
			break;
		case ASSIGN_NO_TX:
			fb_putstr("TX disabled", -1);
			break;
		case ASSIGN_TIMEOUT:
			fb_putstr("Timeout", -1);
			break;
		}
		switch (assign) {
		case ASSIGN_RESULT:
		case ASSIGN_REJECT:
			fb_gotoxy(0,44);
			sprintf(text, "Delay:%ldms", elapsed * 1000 / HZ);
			fb_putstr(text, -1);
			break;
		default:
			break;
		}
	}

	/* SYNC / UL levels */
	if (mode == MODE_SYNC && cursor < 0) {
		int i, tn, l;
		int offset = (framebuffer->width - 96) >> 1;
		int height = framebuffer->height - 25;

		fb_setfont(FB_FONT_HELVR08);
		for (i = 0; i < 8; i++) {
			if (uplink)
				tn = (i + 3) & 7; /* UL is shifted by 3 */
			else
				tn = i;
			fb_setbg(FB_COLOR_WHITE);
			fb_gotoxy(offset + 12 * i, 7);
			l = (max) ? ul_max[tn] : ul_levels[tn];
			l = 110 - l;
			if (l >= 100)
				l -= 100;
			sprintf(text, "%02d", l);
			fb_putstr(text, framebuffer->width);
			fb_setbg(FB_COLOR_BLACK);
			fb_gotoxy(offset + 3 + 12 * i, height + 10);
			fb_boxto(offset + 3 + 12 * i + 5, height + 10 - ul_levels[tn] * height / 64);
			if (max) {
				fb_gotoxy(offset + 3 + 12 * i, height + 10 - ul_max[tn] * height / 64);
				fb_boxto(offset + 3 + 12 * i + 5, height + 10 - ul_max[tn] * height / 64);
			}
		}
		fb_setbg(FB_COLOR_TRANSP);
		if (max) {
			fb_setfg(FB_COLOR_RED);
			fb_gotoxy(framebuffer->width - 16, 15);
			fb_putstr("max", framebuffer->width);
		}
		fb_setfont(FB_FONT_C64);
		fb_setfg(FB_COLOR_BLUE);
		fb_gotoxy(0, 16);
		if (pcs && ul_arfcn >= PCS_MIN && ul_arfcn <= PCS_MAX)
			sprintf(text, "%4dP", ul_arfcn);
		else if (ul_arfcn >= DCS_MIN && ul_arfcn <= DCS_MAX)
			sprintf(text, "%4dD", ul_arfcn);
		else
			sprintf(text, "%4d ", ul_arfcn);
		fb_putstr(text, framebuffer->width);
		fb_setbg(FB_COLOR_WHITE);
		fb_setfg(FB_COLOR_BLACK);
	}

	/* SYNC / SI */
	if (mode == MODE_SYNC && cursor == 0) {
		fb_gotoxy(0, 20);
		if (sync_msg[0] == 'o')
			sprintf(text, "BSIC%d/%d %4d", bsic >> 3, bsic & 7,
				power - 110);
		else
			sprintf(text, "Sync %s", sync_msg);
		fb_putstr(text, -1);

		fb_gotoxy(0,28);
		text[0] = si_1[2] ? '1' : '-';
		text[1] = ' ';
		text[2] = si_2[2] ? '2' : '-';
		text[3] = ' ';
		text[4] = si_2bis[2] ? '2' : '-';
		text[5] = si_2bis[2] ? 'b' : ' ';
		text[6] = si_2ter[2] ? '2' : '-';
		text[7] = si_2ter[2] ? 't' : ' ';
		text[8] = ' ';
		text[9] = si_3[2] ? '3' : '-';
		text[10] = ' ';
		text[11] = si_4[2] ? '4' : '-';
		text[12] = '\0';
		fb_putstr(text, -1);

		fb_gotoxy(0, 36);
		fb_putstr("MCC MNC LAC ", -1);
		fb_gotoxy(0, 44);
		if (mcc) {
			if ((mnc & 0x00f) == 0x00f)
				sprintf(text, "%3x %02x  %04x", mcc, mnc >> 4, lac);
			else
				sprintf(text, "%3x %03x %04x", mcc, mnc, lac);
			fb_putstr(text, -1);
		} else
			fb_putstr("--- --- ----", -1);
		fb_gotoxy(0, 52);
		if (si_3[2]) {
			sprintf(text, "cell id:%04x", cell_id);
			fb_putstr(text, -1);
		} else
			fb_putstr("cell id:----", -1);
	}

	/* SYNC / neighbour cells */
	if (mode == MODE_SYNC && cursor > 0) {
		int i, y = 0;

		text[0] = '\0';
		for (i = 0; i < 1024; i++) {
			if (freq[i].mask & FREQ_TYPE_SERV) {
				if (!text[0])
					sprintf(text, "S: %4d", i);
				else {
					sprintf(text + 7, " %4d", i);
					print_display(text, &y, cursor - 1);
					text[0] = '\0';
				}
			}
		}
		if (text[0])
			print_display(text, &y, cursor - 1);
		text[0] = '\0';
		for (i = 0; i < 1024; i++) {
			if (freq[i].mask & FREQ_TYPE_NCELL) {
				if (!text[0])
					sprintf(text, "N: %4d", i);
				else {
					sprintf(text + 7, " %4d", i);
					print_display(text, &y, cursor - 1);
					text[0] = '\0';
				}
			}
		}
		if (text[0])
			print_display(text, &y, cursor - 1);
		nb_num = y;
	}

	/* ARFCN */
	if (mode == MODE_MAIN || mode == MODE_ARFCN) {
		fb_gotoxy(0, 20);
		if (mode == MODE_ARFCN)
			sprintf(text, "ARFCN %s", input);
		else if (pcs && arfcn >= PCS_MIN && arfcn <= PCS_MAX)
			sprintf(text, "ARFCN %dPCS", arfcn);
		else if (arfcn >= DCS_MIN && arfcn <= DCS_MAX)
			sprintf(text, "ARFCN %dDCS", arfcn);
		else
			sprintf(text, "ARFCN %d", arfcn);
		fb_putstr(text,framebuffer->width);
	}

	/* cursor */
	if (mode == MODE_ARFCN) {
		fb_setfg(FB_COLOR_WHITE);
		fb_setbg(FB_COLOR_BLUE);
		fb_putstr(" ", framebuffer->width);
		fb_setfg(FB_COLOR_BLACK);
		fb_setbg(FB_COLOR_WHITE);
	}

	/* Frequency / power */
	if (mode == MODE_MAIN) {
		int f;

		if (pcs && arfcn >= PCS_MIN && arfcn <= PCS_MAX) {
			if (uplink)
				f = PCS_UL;
			else
				f = PCS_DL;
		} else if (uplink)
			f = band->freq_ul;
		else
			f = band->freq_dl;
		f += ((arfcn - band->min) & 1023) << 1;

		fb_gotoxy(0, 30);
		sprintf(text, "Freq. %d.%d", f / 10, f % 10);
		fb_putstr(text,framebuffer->width);

		fb_gotoxy(0, 40);
		sprintf(text, "Power %d", ((max) ? max_power : power) - 110);
		fb_putstr(text, framebuffer->width);
		if (max) {
			fb_setfont(FB_FONT_HELVR08);
			fb_setfg(FB_COLOR_RED);
			fb_gotoxy(framebuffer->width - 16, 39);
			fb_putstr("max", framebuffer->width);
			fb_setfont(FB_FONT_C64);
			fb_setfg(FB_COLOR_BLACK);
		}
		fb_setbg(FB_COLOR_BLACK);
		fb_gotoxy(0, 45);
		fb_boxto(framebuffer->width * power / 64, 50);
		if (max) {
			fb_gotoxy(framebuffer->width * max_power / 64 ,45);
			fb_boxto(framebuffer->width * max_power / 64, 50);
		}
		fb_setbg(FB_COLOR_WHITE);
	}

	/* spectrum */
	if (mode == MODE_SPECTRUM) {
		int i;
		uint16_t a, e, p;
		int height = framebuffer->height - 25;

		fb_gotoxy(0, 8);
		if (pcs && arfcn >= PCS_MIN && arfcn <= PCS_MAX)
			sprintf(text, "%4dP", arfcn);
		else if (arfcn >= DCS_MIN && arfcn <= DCS_MAX)
			sprintf(text, "%4dD", arfcn);
		else
			sprintf(text, "%4d ", arfcn);
		sprintf(text + 5, "   %4d", pm_spectrum[arfcn & 1023] - 110);
		fb_putstr(text, -1);
		fb_setfg(FB_COLOR_RED);
		if (max) {
			fb_setfont(FB_FONT_HELVR08);
			fb_gotoxy(framebuffer->width - 16,15);
			fb_putstr("max", framebuffer->width);
			fb_setfont(FB_FONT_C64);
		}
		if (pm_scale != 1) {
			fb_setfont(FB_FONT_HELVR08);
			fb_gotoxy(1, 15);
			sprintf(text, "x%d", pm_scale);
			fb_putstr(text, framebuffer->width);
			fb_setfont(FB_FONT_C64);
		}
		fb_setfg(FB_COLOR_BLACK);
		if (pcs && arfcn >= PCS_MIN && arfcn <= PCS_MAX) {
			a = PCS_MIN;
			e = PCS_MAX;
		} else {
			a = band->min;
			e = band->max;
		}
		for (i = 0; i < framebuffer->width; i++) {
			p = (arfcn + i - (framebuffer->width >> 1)) & 1023;
			if ((((p - a) & 1023) & 512))
				continue;
			if ((((e - p) & 1023) & 512))
				continue;
			p = (pm_spectrum[p] * pm_scale * height / 64);
			if (p > height)
				p = height;
			if (i == (framebuffer->width >> 1))
				fb_setfg(FB_COLOR_RED);
			fb_gotoxy(i, height + 10 - p);
			fb_boxto(i, height + 10);
			if (i == (framebuffer->width >> 1))
				fb_setfg(FB_COLOR_BLACK);
		}
		i = framebuffer->width >> 1;
		fb_gotoxy(i, 0);
		fb_boxto(i, 4);
		fb_gotoxy(i, height + 10);
		fb_boxto(i, height + 14);
	}

	/* footer */
	fb_setfg(FB_COLOR_GREEN);
	fb_gotoxy(0, framebuffer->height - 10);
	fb_boxto(framebuffer->width-1, framebuffer->height - 10);
	fb_gotoxy(0, framebuffer->height - 1);
	fb_setfg(FB_COLOR_RED);
	if (mode == MODE_ARFCN)
		sprintf(text, "%s   %s", (cursor) ? "del " : "back",
			(cursor) ? "enter" : "     ");
	else if (mode == MODE_SYNC && cursor < 0)
		sprintf(text, "%s      %s", "back",
			(uplink) ? "UL" : "DL");
	else if (mode == MODE_SYNC || mode == MODE_RACH)
		sprintf(text, "%s        ", "back");
	else
		sprintf(text, "%s       %s", (pcs) ? "PCS" : "DCS",
			(uplink) ? "UL" : "DL");
	fb_putstr(text, -1);
	fb_setfg(FB_COLOR_BLACK);
	fb_setfont(FB_FONT_HELVR08);
	fb_gotoxy(0, framebuffer->height - 2);
	sprintf(text, "%d", tone / 25);
	fb_putstr(text, -1);

	fb_flush();
}

static void exit_arfcn(void)
{
	mode = last_mode;
	refresh_display();
}

static void enter_arfcn(enum key_codes code)
{
	/* enter mode */
	if (mode != MODE_ARFCN) {
		last_mode = mode;
		mode = MODE_ARFCN;
		input[0] = code - KEY_0 + '0';
		input[1] = '\0';
		cursor = 1;
		refresh_display();
		return;
	}

	if (code == KEY_LEFT_SB) {
		/* back */
		if (cursor == 0) {
			exit_arfcn();
			return;
		}
		/* delete */
		cursor--;
		input[cursor] = '\0';
		refresh_display();
		return;
	}

	if (code == KEY_RIGHT_SB) {
		int check = 0;
		int i;
		struct band *temp = NULL;

		/* nothing entered */
		if (cursor == 0) {
			return;
		}
		for (i = 0; i < cursor; i++)
			check = (check << 3) + (check << 1) + input[i] - '0';

		/* check */
		for (i = 0; bands[i].max; i++) {
			temp = &bands[i];
			if (temp->min < temp->max) {
				if (check >= temp->min && check <= temp->max)
					break;
			} else {
				if (check >= temp->min || check <= temp->max)
					break;
			}
		}
		if (!bands[i].max)
			return;
		if (check > 1023)
			return;
		arfcn = check;
		band = temp;
		mode = last_mode;
		refresh_display();
		return;
	}

	if (cursor == 4)
		return;

	input[cursor] = code - KEY_0 + '0';
	cursor++;
	input[cursor] = '\0';
	refresh_display();
}

static int inc_dec_arfcn(int inc)
{
	int i;

	/* select current band */
	for (i = 0; bands[i].max; i++) {
		band = &bands[i];
		if (band->min < band->max) {
			if (arfcn >= band->min && arfcn <= band->max)
				break;
		} else {
			if (arfcn >= band->min || arfcn <= band->max)
				break;
		}
	}
	if (!bands[i].max)
		return -EINVAL;

	if (inc) {
		if (arfcn == band->max)
			arfcn = band->next;
		else if (arfcn == 1023)
			arfcn = 0;
		else
			arfcn++;
	} else {
		if (arfcn == band->min)
			arfcn = band->prev;
		else if (arfcn == 0)
			arfcn = 1023;
		else
			arfcn--;
	}
	/* select next band */
	for (i = 0; bands[i].max; i++) {
		band = &bands[i];
		if (band->min < band->max) {
			if (arfcn >= band->min && arfcn <= band->max)
				break;
		} else {
			if (arfcn >= band->min || arfcn <= band->max)
				break;
		}
	}
	if (!bands[i].max)
		return -EINVAL;

	refresh_display();

	return 0;
}

static void request_ul_levels(uint16_t a);

static int inc_dec_ul_arfcn(int inc)
{
	uint16_t a;

	/* loop until we hit a serving cell or our current bcch arfcn */
	if (inc) {
		for (a = (ul_arfcn + 1) & 1023; a != (arfcn & 1023);
					a = (a + 1) & 1023) {
			if ((freq[a].mask & FREQ_TYPE_SERV))
				break;
		}
	} else {
		for (a = (ul_arfcn - 1) & 1023; a != (arfcn & 1023);
					a = (a - 1) & 1023) {
			if ((freq[a].mask & FREQ_TYPE_SERV))
				break;
		}
	}
	ul_arfcn = a;

	refresh_display();

	request_ul_levels(a);

	return 0;
}

static void toggle_dcs_pcs(void)
{
	pcs = !pcs;
	refresh_display();
}

static void toggle_up_down(void)
{
	uplink = !uplink;
	refresh_display();

	if (mode == MODE_SYNC && cursor < 0)
		request_ul_levels(ul_arfcn);
}

static void toggle_spectrum(void)
{
	if (mode == MODE_MAIN) {
		mode = MODE_SPECTRUM;
		pm_mode = PM_IDLE;
	} else if (mode == MODE_SPECTRUM) {
		mode = MODE_MAIN;
		pm_mode = PM_IDLE;
	}
	l1s_reset();
	l1s_reset_hw();
	pm_count = 0;
	refresh_display();
}

static void tone_inc_dec(int inc)
{
	if (inc) {
		if (tone + 25 <= 255)
			tone += 25;
	} else {
		if (tone - 25 >= 0)
			tone -= 25;
	}

	refresh_display();
}

static void hold_max(void)
{
	max = !max;
	max_power = power;
	refresh_display();
}

static int inc_dec_neighbour(int inc)
{
	if (inc) {
		if (cursor > 0 && cursor - 1 >= (nb_num - NEIGH_LINES))
			return -EINVAL;
		cursor++;
	} else {
		if (cursor < 0)
			return -EINVAL;
		cursor--;
	}

	refresh_display();

	return 0;
}

static int inc_dec_spectrum(int inc)
{
	if (inc) {
		pm_scale <<= 1;
		if (pm_scale > 8)
			pm_scale = 8;
	} else {
		pm_scale >>= 1;
		if (pm_scale < 1)
			pm_scale = 1;
	}

	refresh_display();

	return 0;
}

static void enter_sync(void);
static void exit_sync(void);

static void enter_rach(void);
static void exit_rach(void);

static void handle_key_code()
{
	/* key repeat */
	if (key_pressed) {
		unsigned long elapsed = jiffies - key_pressed_when;
		if (elapsed > key_pressed_delay) {
			key_pressed_when = jiffies;
			key_pressed_delay = HZ / 10;
			/* only repeat these keys */
			if (key_pressed_code == KEY_LEFT
			 || key_pressed_code == KEY_RIGHT)
				key_code = key_pressed_code;
		}
	}

	if (key_code == KEY_INV)
		return;

	/* do later, do not disturb tone */
	if (tone_on)
		return;

	switch (key_code) {
	case KEY_0:
	case KEY_1:
	case KEY_2:
	case KEY_3:
	case KEY_4:
	case KEY_5:
	case KEY_6:
	case KEY_7:
	case KEY_8:
	case KEY_9:
		if (mode == MODE_MAIN || mode == MODE_SPECTRUM || mode == MODE_ARFCN)
			enter_arfcn(key_code);
		break;
	case KEY_UP:
		if (mode == MODE_MAIN)
			tone_inc_dec(1);
		else if (mode == MODE_SYNC)
			inc_dec_neighbour(0);
		else if (mode == MODE_SPECTRUM)
			inc_dec_spectrum(1);
		break;
	case KEY_DOWN:
		if (mode == MODE_MAIN)
			tone_inc_dec(0);
		else if (mode == MODE_SYNC)
			inc_dec_neighbour(1);
		else if (mode == MODE_SPECTRUM)
			inc_dec_spectrum(0);
		break;
	case KEY_RIGHT:
		if (mode == MODE_MAIN || mode == MODE_SPECTRUM)
			inc_dec_arfcn(1);
		else if (mode == MODE_SYNC && cursor < 0)
			inc_dec_ul_arfcn(1);
		break;
	case KEY_LEFT:
		if (mode == MODE_MAIN || mode == MODE_SPECTRUM)
			inc_dec_arfcn(0);
		else if (mode == MODE_SYNC && cursor < 0)
			inc_dec_ul_arfcn(0);
		break;
	case KEY_LEFT_SB:
		if (mode == MODE_MAIN || mode == MODE_SPECTRUM)
			toggle_dcs_pcs();
		else if (mode == MODE_ARFCN)
			enter_arfcn(key_code);
		else if (mode == MODE_SYNC)
			exit_sync();
		else if (mode == MODE_RACH)
			exit_rach();
		break;
	case KEY_RIGHT_SB:
		if (mode == MODE_MAIN || mode == MODE_SPECTRUM)
			toggle_up_down();
		else if (mode == MODE_ARFCN)
			enter_arfcn(key_code);
		else if (mode == MODE_SYNC && cursor < 0)
			toggle_up_down();
		break;
	case KEY_OK:
		if (mode == MODE_MAIN || mode == MODE_SPECTRUM)
			enter_sync();
		else if (mode == MODE_SYNC || mode == MODE_RACH)
			enter_rach();
		break;
	case KEY_MENU:
		hold_max();
		break;
	case KEY_POWER:
		if (mode == MODE_ARFCN)
			exit_arfcn();
		else if (mode == MODE_SYNC)
			exit_sync();
		else if (mode == MODE_RACH)
			exit_rach();
		else if (mode == MODE_SPECTRUM)
			toggle_spectrum();
		break;
	case KEY_STAR:
		if (mode == MODE_MAIN || mode == MODE_SPECTRUM)
			toggle_spectrum();
		break;
	default:
		break;
	}

	key_code = KEY_INV;
}

static void handle_tone(void)
{
	unsigned long elapsed = jiffies - tone_time;

	if (!tone_on) {
		if (!tone || mode != MODE_MAIN)
			return;
		/* wait depending on power level */
		if (elapsed < (uint8_t)(63-power))
			return;
		buzzer_volume(tone);
		buzzer_note(NOTE(NOTE_C, OCTAVE_5));
		tone_time = jiffies;
		tone_on = 1;
		return;
	}

	if (elapsed >= TONE_JIFFIES) {
		tone_on = 0;
		tone_time = jiffies;
		buzzer_volume(0);
	}
}

/* PM handling */

static void handle_pm(void)
{
	/* start power measurement */
	if (pm_mode == PM_IDLE && (mode == MODE_MAIN || mode == MODE_SPECTRUM)) {
		struct msgb *msg = l1ctl_msgb_alloc(L1CTL_PM_REQ);
		struct l1ctl_pm_req *pm;
		uint16_t a, e;

		pm = (struct l1ctl_pm_req *) msgb_put(msg, sizeof(*pm));
		pm->type = 1;
		if (mode == MODE_MAIN) {
			a = arfcn;
			if (pcs && arfcn >= PCS_MIN && arfcn <= PCS_MAX)
				a |= ARFCN_PCS;
			if (uplink)
				a |= ARFCN_UPLINK;
			e = a;
			pm_mode = PM_SENT;
		}
		if (mode == MODE_SPECTRUM) {
			if (pcs && arfcn >= PCS_MIN && arfcn <= PCS_MAX) {
				a = PCS_MIN | ARFCN_PCS;
				e = PCS_MAX | ARFCN_PCS;
			} else {
				a = band->min;
				e = band->max;
			}
			pm_mode = PM_RANGE_SENT;
		}
		if (uplink) {
			a |= ARFCN_UPLINK;
			e |= ARFCN_UPLINK;
		}
		pm->range.band_arfcn_from = htons(a);
		pm->range.band_arfcn_to = htons(e);

		l1a_l23_rx(SC_DLCI_L1A_L23, msg);

		return;
	}

	if (pm_mode == PM_RESULT) {
		pm_mode = PM_IDLE;
		if (pm_count == pm_max) {
			int i = 0;
			int sum = 0;

			if (uplink) {
				/* find max */
				for (i = 0; i < pm_count; i++) {
					if (pm_meas[i] > sum)
						sum = pm_meas[i];
				}
				power = sum;
			} else {
				for (i = 0; i < pm_count; i++)
					sum += pm_meas[i];
				power = sum / pm_count;
			}
			if (power > max_power)
				max_power = power;
			pm_count = 0;
			pm_max = (uplink) ? NUM_PM_UL : NUM_PM_DL;
			if (!tone_on)
				refresh_display();
		}
		return;
	}

	if (pm_mode == PM_RANGE_RESULT) {
		pm_mode = PM_IDLE;
		refresh_display();
		buzzer_volume(tone);
		buzzer_note(NOTE(NOTE_C, OCTAVE_5));
		tone_time = jiffies;
		tone_on = 1;
		return;
	}
}

/* sync / SI */

static void enter_sync(void)
{
	struct msgb *msg = l1ctl_msgb_alloc(L1CTL_FBSB_REQ);
	struct l1ctl_fbsb_req *req;
	uint16_t a = arfcn;

	l1s_reset();
	l1s_reset_hw();
	pm_count = 0;
	pm_mode = PM_IDLE;

	req = (struct l1ctl_fbsb_req *) msgb_put(msg, sizeof(*req));
	if (pcs && arfcn >= PCS_MIN && arfcn <= PCS_MAX)
		a |= ARFCN_PCS;
	req->band_arfcn = htons(a);
	req->timeout = htons(100);
	/* Threshold when to consider FB_MODE1: 4kHz - 1kHz */
	req->freq_err_thresh1 = htons(11000 - 1000);
	/* Threshold when to consider SCH: 1kHz - 200Hz */
	req->freq_err_thresh2 = htons(1000 - 200);
	/* not used yet! */
	req->num_freqerr_avg = 3;
	req->flags = L1CTL_FBSB_F_FB01SB;
	req->sync_info_idx = 0;
	req->ccch_mode = CCCH_MODE_NONE;
	l1a_l23_rx(SC_DLCI_L1A_L23, msg);

	mode = MODE_SYNC;
	memset(ul_levels, 0, sizeof(ul_levels));
	si_new = 0;
	ul_new = 0;
	ul_arfcn = arfcn;
	si_1[2] = 0;
	si_2[2] = 0;
	si_2bis[2] = 0;
	si_2ter[2] = 0;
	si_3[2] = 0;
	si_4[2] = 0;
	mcc = mnc = lac = 0;
	ccch_conf = -1;
	memset(freq, 0, sizeof(freq));
	cursor = 0;
	nb_num = 0;
	sync_msg = "trying";
	refresh_display();
}

static void exit_sync(void)
{
	l1s_reset();
	l1s_reset_hw();
	pm_count = 0;
	pm_mode = PM_IDLE;
	mode = MODE_MAIN;
}

int gsm48_decode_lai(struct gsm48_loc_area_id *lai, uint16_t *_mcc,
uint16_t *_mnc, uint16_t *_lac)
{
	*_mcc = ((lai->digits[0] & 0x0f) << 8)
	 | (lai->digits[0] & 0xf0)
	 | (lai->digits[1] & 0x0f);
	*_mnc = ((lai->digits[2] & 0x0f) << 8)
	 | (lai->digits[2] & 0xf0)
	 | ((lai->digits[1] & 0xf0) >> 4);
	*_lac = ntohs(lai->lac);

	return 0;
}

static void request_ul_levels(uint16_t a)
{
	struct msgb *msg = l1ctl_msgb_alloc(L1CTL_NEIGH_PM_REQ);
	struct l1ctl_neigh_pm_req *pm_req =
		(struct l1ctl_neigh_pm_req *) msgb_put(msg, sizeof(*pm_req));
	int i;

	if (pcs && a >= PCS_MIN && a <= PCS_MAX)
		a |= ARFCN_PCS;
	if (uplink)
		a |= ARFCN_UPLINK;
	pm_req->n = 8;
	for (i = 0; i < 8; i++) {
		pm_req->band_arfcn[i] = htons(a);
		pm_req->tn[i] = i;
	}
	l1a_l23_rx(SC_DLCI_L1A_L23, msg);
}

static void handle_sync(void)
{
	struct gsm48_system_information_type_1 *si1;
	struct gsm48_system_information_type_2 *si2;
	struct gsm48_system_information_type_2bis *si2bis;
	struct gsm48_system_information_type_2ter *si2ter;
	struct gsm48_system_information_type_3 *si3;
	struct gsm48_system_information_type_4 *si4;

	if (mode != MODE_SYNC)
		return;

	/* once we synced, we take the result and request UL measurement */
	if (sync_result) {
		uint16_t a = ul_arfcn;

		sync_msg = sync_result;
		sync_result = NULL;
		refresh_display();

		if (sync_msg[0] != 'o')
			return;

		request_ul_levels(a);

		return;
	}

	if (tone_on)
		return;

	/* no UL result, no SI result */
	if (!ul_new && !(si_new & 0x100))
		return;

	/* new UL result */
	if (ul_new) {
		ul_new = 0;
		if (cursor < 0)
			refresh_display();
		return;
	}

	/* decode si */
	switch (si_new & 0xff) {
	case GSM48_MT_RR_SYSINFO_1:
		si1 = (struct gsm48_system_information_type_1 *)si_1;
		gsm48_decode_freq_list(freq, si1->cell_channel_description,
	                sizeof(si1->cell_channel_description), 0xce,
					FREQ_TYPE_SERV);
		break;
	case GSM48_MT_RR_SYSINFO_2:
		si2 = (struct gsm48_system_information_type_2 *)si_2;
		gsm48_decode_freq_list(freq, si2->bcch_frequency_list,
	                sizeof(si2->bcch_frequency_list), 0xce,
					FREQ_TYPE_NCELL_2);
		break;
	case GSM48_MT_RR_SYSINFO_2bis:
		si2bis = (struct gsm48_system_information_type_2bis *)si_2bis;
		gsm48_decode_freq_list(freq, si2bis->bcch_frequency_list,
	                sizeof(si2bis->bcch_frequency_list), 0xce,
					FREQ_TYPE_NCELL_2bis);
		break;
	case GSM48_MT_RR_SYSINFO_2ter:
		si2ter = (struct gsm48_system_information_type_2ter *)si_2ter;
		gsm48_decode_freq_list(freq, si2ter->ext_bcch_frequency_list,
	                sizeof(si2ter->ext_bcch_frequency_list), 0x8e,
					FREQ_TYPE_NCELL_2ter);
		break;
	case GSM48_MT_RR_SYSINFO_3:
		si3 = (struct gsm48_system_information_type_3 *)si_3;
		gsm48_decode_lai(&si3->lai, &mcc, &mnc, &lac);
		cell_id = ntohs(si3->cell_identity);
		if (ccch_conf < 0) {
			struct msgb *msg =
				l1ctl_msgb_alloc(L1CTL_CCCH_MODE_REQ);
			struct l1ctl_ccch_mode_req *req =
				(struct l1ctl_ccch_mode_req *)
					msgb_put(msg, sizeof(*req));

			ccch_conf = si3->control_channel_desc.ccch_conf;
			req->ccch_mode = (ccch_conf == 1)
					? CCCH_MODE_COMBINED
					: CCCH_MODE_NON_COMBINED;
			printf("ccch_mode=%d\n", ccch_conf);

			l1a_l23_rx(SC_DLCI_L1A_L23, msg);
		}
		break;
	case GSM48_MT_RR_SYSINFO_4:
		si4 = (struct gsm48_system_information_type_4 *)si_4;
		gsm48_decode_lai(&si4->lai, &mcc, &mnc, &lac);
		break;
	}

	if (cursor >= 0)
		refresh_display();

	/* tone depends on successfully received BCCH */
	buzzer_volume(tone);
	tone_time = jiffies;
	tone_on = 1;
	if ((si_new & 0xff) == 0xff)
		buzzer_note(NOTE(NOTE_C, OCTAVE_2));
	else
		buzzer_note(NOTE(NOTE_C, OCTAVE_5));
	si_new = 0;
}

static void enter_rach(void)
{
	if (ccch_conf < 0)
		return;

	if (rach)
		return;

#ifndef CONFIG_TX_ENABLE
	assign = ASSIGN_NO_TX;
	mode = MODE_RACH;
	/* display refresh is done by rach handler */
#else
	struct msgb *msg1 = l1ctl_msgb_alloc(L1CTL_NEIGH_PM_REQ);
	struct msgb *msg2 = l1ctl_msgb_alloc(L1CTL_RACH_REQ);
	struct l1ctl_neigh_pm_req *pm_req = (struct l1ctl_neigh_pm_req *)
			msgb_put(msg1, sizeof(*pm_req));
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *)
			msgb_put(msg2, sizeof(*ul));;
	struct l1ctl_rach_req *rach_req = (struct l1ctl_rach_req *)
			msgb_put(msg2, sizeof(*rach_req));

	l1s.tx_power = 0;

	pm_req->n = 0; /* disable */

	rach_ra = 0x00;
	rach_req->ra = rach_ra;
	rach_req->offset = 0;
	rach_req->combined = (ccch_conf == 1);

	l1a_l23_rx(SC_DLCI_L1A_L23, msg1);
	l1a_l23_rx(SC_DLCI_L1A_L23, msg2);
	rach = 1;
	rach_when = jiffies;
	assign = ASSIGN_NONE;
	mode = MODE_RACH;
	refresh_display();
#endif

}

static void exit_rach(void)
{
	rach = 0;

	request_ul_levels(ul_arfcn);

	mode = MODE_SYNC;
	refresh_display();
}

static void handle_assign(void)
{
	if (mode != MODE_RACH)
		return;

	if (assign == ASSIGN_NONE) {
		unsigned long elapsed = jiffies - rach_when;

		if (!rach)
			return;
		if (elapsed < HZ * 2)
			return;
		assign = ASSIGN_TIMEOUT;
		rach = 0;
	}

	refresh_display();
	assign = ASSIGN_NONE;
}

/* Main Program */
const char *hr = "======================================================================\n";

/* match request reference agains request history */
static int gsm48_match_ra(struct gsm48_req_ref *ref)
{
	uint8_t ia_t1, ia_t2, ia_t3;
	uint8_t cr_t1, cr_t2, cr_t3;

	if (rach && ref->ra == rach_ra) {
		ia_t1 = ref->t1;
		ia_t2 = ref->t2;
		ia_t3 = (ref->t3_high << 3) | ref->t3_low;
		ref = &rach_ref;
		cr_t1 = ref->t1;
		cr_t2 = ref->t2;
		cr_t3 = (ref->t3_high << 3) | ref->t3_low;
		if (ia_t1 == cr_t1 && ia_t2 == cr_t2 && ia_t3 == cr_t3)
			return 1;
	}

	return 0;
}


/* note: called from IRQ context */
static void rx_imm_ass(struct msgb *msg)
{
	struct gsm48_imm_ass *ia = msgb_l3(msg);

	if (gsm48_match_ra(&ia->req_ref)) {
		assign = ASSIGN_RESULT;
		ta = ia->timing_advance;
		rach = 0;
	}
}

/* note: called from IRQ context */
static void rx_imm_ass_ext(struct msgb *msg)
{
	struct gsm48_imm_ass_ext *ia = msgb_l3(msg);

	if (gsm48_match_ra(&ia->req_ref1)) {
		assign = ASSIGN_RESULT;
		ta = ia->timing_advance1;
		rach = 0;
	}
	if (gsm48_match_ra(&ia->req_ref2)) {
		assign = ASSIGN_RESULT;
		ta = ia->timing_advance2;
		rach = 0;
	}
}

/* note: called from IRQ context */
static void rx_imm_ass_rej(struct msgb *msg)
{
	struct gsm48_imm_ass_rej *ia = msgb_l3(msg);
	struct gsm48_req_ref *req_ref;
	int i;

	for (i = 0; i < 4; i++) {
		/* request reference */
		req_ref = (struct gsm48_req_ref *)
			(((uint8_t *)&ia->req_ref1) + i * 4);
		if (gsm48_match_ra(req_ref)) {
			assign = ASSIGN_REJECT;
			rach = 0;
		}
	}
}

/* note: called from IRQ context */
static void rx_pch_agch(struct msgb *msg)
{
	struct gsm48_system_information_type_header *sih;

	/* store SI */
	sih = msgb_l3(msg);
	switch (sih->system_information) {
	case GSM48_MT_RR_IMM_ASS:
		rx_imm_ass(msg);
		break;
	case GSM48_MT_RR_IMM_ASS_EXT:
		rx_imm_ass_ext(msg);
		break;
	case GSM48_MT_RR_IMM_ASS_REJ:
		rx_imm_ass_rej(msg);
		break;
	}
}

/* note: called from IRQ context */
static void rx_bcch(struct msgb *msg)
{
	struct gsm48_system_information_type_header *sih;

	/* store SI */
	sih = msgb_l3(msg);
	switch (sih->system_information) {
	case GSM48_MT_RR_SYSINFO_1:
		memcpy(si_1, msgb_l3(msg), msgb_l3len(msg));
		break;
	case GSM48_MT_RR_SYSINFO_2:
		memcpy(si_2, msgb_l3(msg), msgb_l3len(msg));
		break;
	case GSM48_MT_RR_SYSINFO_2bis:
		memcpy(si_2bis, msgb_l3(msg), msgb_l3len(msg));
		break;
	case GSM48_MT_RR_SYSINFO_2ter:
		memcpy(si_2ter, msgb_l3(msg), msgb_l3len(msg));
		break;
	case GSM48_MT_RR_SYSINFO_3:
		memcpy(si_3, msgb_l3(msg), msgb_l3len(msg));
		break;
	case GSM48_MT_RR_SYSINFO_4:
		memcpy(si_4, msgb_l3(msg), msgb_l3len(msg));
		break;
	}
	si_new = sih->system_information | 0x100;
}

/* note: called from IRQ context */
static void l1a_l23_tx(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->l1h;
	struct l1ctl_pm_conf *pmr;
	struct l1ctl_info_dl *dl;
	struct l1ctl_fbsb_conf *sb;
	uint8_t chan_type, chan_ts, chan_ss;
	struct l1ctl_neigh_pm_ind *pm_ind;
	struct gsm_time tm;

	switch (l1h->msg_type) {
	case L1CTL_PM_CONF:
		if (pm_mode == PM_SENT) {
			pmr = (struct l1ctl_pm_conf *) l1h->data;
			pm_meas[pm_count] = pmr->pm[0];
			pm_count++;
			pm_mode = PM_RESULT;
		}
		if (pm_mode == PM_RANGE_SENT) {
			for (pmr = (struct l1ctl_pm_conf *) l1h->data;
				(uint8_t *) pmr < msg->tail; pmr++) {
				if (!max || pm_spectrum[ntohs(pmr->band_arfcn) & 1023] < pmr->pm[0])
					pm_spectrum[ntohs(pmr->band_arfcn) & 1023] = pmr->pm[0];
			}
			if ((l1h->flags & L1CTL_F_DONE))
				pm_mode = PM_RANGE_RESULT;
		}
		l1s.tpu_offset_correction += 5000 / NUM_PM_UL;
		break;
	case L1CTL_FBSB_CONF:
		dl = (struct l1ctl_info_dl *) l1h->data;
		sb = (struct l1ctl_fbsb_conf *) dl->payload;
		if (sb->result == 0)
			sync_result = "ok";
		else
			sync_result = "error";
		bsic = sb->bsic;
		break;
	case L1CTL_DATA_IND:
		dl = (struct l1ctl_info_dl *) l1h->data;
		msg->l2h = dl->payload;
		rsl_dec_chan_nr(dl->chan_nr, &chan_type, &chan_ss, &chan_ts);

		power = dl->rx_level;
		if (dl->fire_crc >= 2) {
			if (chan_type == RSL_CHAN_BCCH)
				si_new = 0x1ff; /* error frame indication */
			break; /* free, but don't send to sercom */
		}

		switch (chan_type) {
		case RSL_CHAN_BCCH:
			msg->l3h = msg->l2h;
			rx_bcch(msg);
			break;
		case RSL_CHAN_PCH_AGCH:
			msg->l3h = msg->l2h;
			rx_pch_agch(msg);
			break;
		}
		sercomm_sendmsg(SC_DLCI_L1A_L23, msg);
		return; /* msg is freed by sercom */
	case L1CTL_NEIGH_PM_IND:
		for (pm_ind = (struct l1ctl_neigh_pm_ind *) l1h->data;
			(uint8_t *) pm_ind < msg->tail; pm_ind++) {
			ul_levels[pm_ind->tn] = pm_ind->pm[0];
			/* hold max only, if max enabled and level is lower */
			if (!max || ul_levels[pm_ind->tn] > ul_max[pm_ind->tn])
				ul_max[pm_ind->tn] = ul_levels[pm_ind->tn];
			if (pm_ind->tn == 7)
				ul_new = 1;
		}
		break;
	case L1CTL_RACH_CONF:
		dl = (struct l1ctl_info_dl *) l1h->data;
		gsm_fn2gsmtime(&tm, ntohl(dl->frame_nr));
		rach_ref.t1 = tm.t1;
		rach_ref.t2 = tm.t2;
		rach_ref.t3_low = tm.t3 & 0x7;
		rach_ref.t3_high = tm.t3 >> 3;
		break;
	}

	msgb_free(msg);

}

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
	printf("l1a_l23_rx_cb (DLCI %d): ", dlci);
	for (i = 0; i < msg->len; i++)
		printf("%02x ", msg->data[i]);
	puts("\n");
}

static void key_handler(enum key_codes code, enum key_states state)
{
	if (state != PRESSED) {
		key_pressed = 0;
		return;
	}
	/* key repeat */
	if (!key_pressed) {
		key_pressed = 1;
		key_pressed_when = jiffies;
		key_pressed_code = code;
		key_pressed_delay = HZ * 6 / 10;
	}

	key_code = code;
}

int main(void)
{
	board_init(1);

	puts("\n\nOsmocomBB Monitor Tool (revision " GIT_REVISION ")\n");
	puts(hr);

	/* Dump device identification */
	dump_dev_id();
	puts(hr);

	/* Dump clock config before PLL set */
	calypso_clk_dump();
	puts(hr);

	keypad_set_handler(&key_handler);

	/* Dump clock config after PLL set */
	calypso_clk_dump();
	puts(hr);

	sercomm_register_rx_cb(SC_DLCI_CONSOLE, console_rx_cb);
	sercomm_register_rx_cb(SC_DLCI_L1A_L23, l1a_l23_rx_cb);

	layer1_init();
	l1a_l23_tx_cb = l1a_l23_tx;

//	display_unset_attr(DISP_ATTR_INVERT);

	tpu_frame_irq_en(1, 1);

	buzzer_mode_pwt(1);
	buzzer_volume(0);

	memset(pm_spectrum, 0, sizeof(pm_spectrum));
	memset(ul_max, 0, sizeof(ul_max));

	/* inc 0 to 1 and refresh */
	inc_dec_arfcn(1);

	while (1) {
		l1a_compl_execute();
		osmo_timers_update();
		handle_key_code();
		l1a_l23_handler();
		handle_pm();
		handle_sync();
		handle_assign();
		handle_tone();
	}

	/* NOT REACHED */

	twl3025_power_off();
}

