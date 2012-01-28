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

enum key_codes key_code = KEY_INV;
int key_pressed = 0;
enum key_codes key_pressed_code;
unsigned long key_pressed_when;
unsigned int key_pressed_delay;

enum mode {
	MODE_MAIN,
	MODE_SPECTRUM,
	MODE_ARFCN,
} mode = MODE_MAIN;
enum mode last_mode; /* where to return after entering ARFCN */

static uint16_t arfcn = 0;
int pcs = 0;
int uplink = 0;
int max = 0;
uint8_t power, max_power;
char input[5];
int cursor;

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

#define TONE_JIFFIES 4
int tone = 0;
unsigned long tone_time;
int tone_on = 0;

/* UI */

static void refresh_display(void)
{
	char text[16];

	fb_clear();

	/* header */
	fb_setbg(FB_COLOR_WHITE);
	if (mode != MODE_SPECTRUM) {
		fb_setfg(FB_COLOR_BLUE);
		fb_setfont(FB_FONT_HELVR08);
		fb_gotoxy(0,6);
		fb_putstr("Osmocom Monitor Tool",-1);
		fb_gotoxy(0,10);
		fb_setfg(FB_COLOR_BLACK);
		fb_boxto(framebuffer->width-1,10);
	}
	fb_setfg(FB_COLOR_BLACK);
	fb_setfont(FB_FONT_C64);

	/* ARFCN */
	if (mode == MODE_MAIN || mode == MODE_ARFCN) {
		fb_gotoxy(0,20);
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

		fb_gotoxy(0,30);
		sprintf(text, "Freq. %d.%d", f / 10, f % 10);
		fb_putstr(text,framebuffer->width);

		fb_gotoxy(0,40);
		sprintf(text, "Power %d", ((max) ? max_power : power) - 110);
		fb_putstr(text,framebuffer->width);
		if (max) {
			fb_setfont(FB_FONT_HELVR08);
			fb_gotoxy(80,39);
			fb_putstr("max",framebuffer->width);
			fb_setfont(FB_FONT_C64);
		}
		fb_setbg(FB_COLOR_BLACK);
		fb_gotoxy(0,45);
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

		fb_gotoxy(0,8);
		if (pcs && arfcn >= PCS_MIN && arfcn <= PCS_MAX)
			sprintf(text, "%4dP", arfcn);
		else if (arfcn >= DCS_MIN && arfcn <= DCS_MAX)
			sprintf(text, "%4dD", arfcn);
		else
			sprintf(text, "%4d ", arfcn);
		sprintf(text + 5, "   %d", pm_spectrum[arfcn & 1023] - 110);
		fb_putstr(text,framebuffer->width);
		if (max) {
			fb_setfont(FB_FONT_HELVR08);
			fb_gotoxy(80,15);
			fb_putstr("max",framebuffer->width);
			fb_setfont(FB_FONT_C64);
		}
		if (pm_scale != 1) {
			fb_setfont(FB_FONT_HELVR08);
			fb_gotoxy(1,15);
			sprintf(text, "x%d", pm_scale);
			fb_putstr(text,framebuffer->width);
			fb_setfont(FB_FONT_C64);
		}
		if (pcs && arfcn >= PCS_MIN && arfcn <= PCS_MAX) {
			a = PCS_MIN;
			e = PCS_MAX;
		} else {
			a = band->min;
			e = band->max;
		}
		for (i = 0; i < framebuffer->width - 1; i++) {
			p = (arfcn + i - (framebuffer->width >> 1)) & 1023;
			if ((((p - a) & 1023) & 512))
				continue;
			if ((((e - p) & 1023) & 512))
				continue;
			p = (pm_spectrum[p] * pm_scale * 40 / 64);
			if (p > 40)
				p = 40;
			fb_gotoxy(i, 50 - p);
			fb_boxto(i, 50);
		}
		i = framebuffer->width >> 1;
		fb_gotoxy(i, 0);
		fb_boxto(i, 4);
		fb_gotoxy(i, 50);
		fb_boxto(i, 54);
	}

	/* footer */
	fb_gotoxy(0,55);
	fb_boxto(framebuffer->width-1,55);
	fb_gotoxy(0,64);
	if (mode == MODE_ARFCN)
		sprintf(text, "%s   %s", (cursor) ? "del " : "back",
			(cursor) ? "enter" : "     ");
	else
		sprintf(text, "%s       %s", (pcs) ? "PCS" : "DCS",
			(uplink) ? "UL" : "DL");
	fb_putstr(text,framebuffer->width);
	fb_setfont(FB_FONT_HELVR08);
	fb_gotoxy(0,63);
	sprintf(text, "%d", tone / 25);
	fb_putstr(text,-1);

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

static void toggle_dcs_pcs(void)
{
	pcs = !pcs;
	refresh_display();
}

static void toggle_up_down(void)
{
	uplink = !uplink;
	refresh_display();
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

static void handle_key_code()
{
	/* key repeat */
	if (key_pressed) {
		unsigned long elapsed = jiffies - key_pressed_when;
		if (elapsed > key_pressed_delay) {
			key_pressed_when = jiffies;
			key_pressed_delay = 10;
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
		else if (mode == MODE_SPECTRUM)
			inc_dec_spectrum(1);
		break;
	case KEY_DOWN:
		if (mode == MODE_MAIN)
			tone_inc_dec(0);
		else if (mode == MODE_SPECTRUM)
			inc_dec_spectrum(0);
		break;
	case KEY_RIGHT:
		if (mode == MODE_MAIN || mode == MODE_SPECTRUM)
			inc_dec_arfcn(1);
		break;
	case KEY_LEFT:
		if (mode == MODE_MAIN || mode == MODE_SPECTRUM)
			inc_dec_arfcn(0);
		break;
	case KEY_LEFT_SB:
		if (mode == MODE_MAIN || mode == MODE_SPECTRUM)
			toggle_dcs_pcs();
		else if (mode == MODE_ARFCN)
			enter_arfcn(key_code);
		break;
	case KEY_RIGHT_SB:
		if (mode == MODE_MAIN || mode == MODE_SPECTRUM)
			toggle_up_down();
		else if (mode == MODE_ARFCN)
			enter_arfcn(key_code);
		break;
	case KEY_MENU:
		hold_max();
		break;
	case KEY_POWER:
		if (mode == MODE_ARFCN)
			exit_arfcn();
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

/* Main Program */
const char *hr = "======================================================================\n";

/* note: called from IRQ context */
static void l1a_l23_tx(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->l1h;
	struct l1ctl_pm_conf *pmr;

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
		key_pressed_delay = 60;
	}

	key_code = code;
}

int main(void)
{
	board_init();

	puts("\n\nOSMOCOM Monitor Tool (revision " GIT_REVISION ")\n");
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

	/* inc 0 to 1 and refresh */
	inc_dec_arfcn(1);

	while (1) {
		l1a_compl_execute();
		osmo_timers_update();
		handle_key_code();
		l1a_l23_handler();
		handle_pm();
		handle_tone();
	}

	/* NOT REACHED */

	twl3025_power_off();
}

