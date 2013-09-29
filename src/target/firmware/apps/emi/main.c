/* EMI Test Transmitter of Free Software for Calypso Phone */

/* (C) 2013 by Andreas Eversberg <jolly@eversberg.eu>
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
#include <layer1/apc.h>
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
	MODE_ARFCN,
	MODE_TEMPLATE,
} mode = MODE_MAIN;

static uint8_t transmitter_on = 0;
static uint16_t arfcn = 0;
static uint8_t power = 0;
static uint16_t auxapc_abb;
static int pcs = 0;
static int uplink = 1;
static char input[5];
static int cursor;

static struct band {
	int min, max, prev, next, freq_ul, freq_dl;
} bands[] = {
        { 128, 251, 124, 512, 8242, 8692 }, /* GSM 850 */
        { 955, 124, 885, 128, 8762, 9212 }, /* P,E,R GSM */
        { 512, 885, 251, 955, 17102, 18052 }, /* DCS 1800 */
	{ 0, 0, 0, 0, 0, 0},
};

static struct band *band;

#define PCS_MIN 512
#define PCS_MAX 810
#define DCS_MIN 512
#define DCS_MAX 885
#define PCS_UL 18502
#define PCS_DL 19302

#define TONE_JIFFIES ((HZ < 25) ? 1 : HZ / 25)
static int tone_on = 0;

/* templates */

struct templates {
	char *name;		/* name of template */
	uint8_t slots;		/* number of slots used (multislot) */
	int burst_num;		/* number of bits in burstmap */
	uint32_t *burst_map;
};

/* two multiframes of SDCCH */
static uint32_t template_sdcch[] = {
	0x0001e000, 0x0001e000, 0x3c000000, 0x00000000,
};

/* one multiframe of TCH/F */
static uint32_t template_tchf[] = {
	0xffffff80,
};

/* one multiframe of TCH/H with SACCH/TF only */
static uint32_t template_tchf_h_dtx[] = {
	0x00080000,
};

/* one multiframe of TCH/H */
static uint32_t template_tchh[] = {
	0xaaad5500,
};

/* 32 multiframes of PDCH representing bursts during download (accessing google) */
static uint32_t template_pdch_ack[] = {
	0xffff803c, 0x3c000000, 0x07800001, 0xe00ff7ff, 0xbc01e1e0, 0x0000003c,
	0x00000f00, 0x7fbffde0, 0x0f0f0000, 0x0001e000, 0x00000000, 0x000f0000,
	0x03c001fe,
	0xffff803c, 0x01e00000, 0x00780000, 0x000f0000, 0x03c01fef, 0xff7803c3,
	0xc0000000, 0x78000000, 0x0f000003, 0xc01ffeff, 0xf7803fc0, 0x00000078,
	0x00001e00,
	0xfffff800, 0x01fe0000, 0x0003c000, 0x00f007ff, 0xbffde00f, 0xf0000000,
	0x1e000000, 0x07800000, 0x00f00000, 0x3c01feff, 0xf7803c3c, 0x00000007,
	0x800001e0,
	0x0fffffbc, 0x01e1e000, 0x00003c00, 0x00000780, 0x0001e00f, 0xff7f8000,
	0x1fe00000, 0x003c0000, 0x000f0000, 0x3c001eff, 0xf7f8003d, 0xe0000000,
	0x780001e0,

};

/* 8 multiframes of PDCH representing bursts during upload (large ping reply) */
static uint32_t template_pdch[] = {
	0xffffffbf, 0xfdffefff, 0x7ffbffdf, 0xfefff7ff, 0xbffdffef, 0xff7ffbff,
	0xdffefff7, 0xffbffdff, 0xefff7ffb, 0xffdffeff, 0xf7ffbffd, 0xffefff7f,
	0xfbffdffe,
};

static struct templates templates[] = {
	{ "SDCCH",		1,	102,	template_sdcch },
	{ "TCH/F",		1,	26,	template_tchf },
	{ "TCH/F (2 TS)",	2,	26,	template_tchf },
	{ "TCH/F (3 TS)",	3,	26,	template_tchf },
	{ "TCH/F (4 TS)",	4,	26,	template_tchf },
	{ "TCH/F (5 TS)",	5,	26,	template_tchf },
	{ "TCH/H",		1,	26,	template_tchh },
	{ "TCH/F(H) DTX",	1,	26,	template_tchf_h_dtx },
	{ "PDCH (ack)",		1,	1664,	template_pdch_ack },
	{ "PDCH",		1,	416,	template_pdch },
	{ "PDCH (2 TS)",	2,	416,	template_pdch },
	{ "PDCH (3 TS)",	3,	416,	template_pdch },
	{ "PDCH (4 TS)",	4,	416,	template_pdch },
	{ "PDCH (5 TS)",	5,	416,	template_pdch },
	{ "RACH",		1,	217,	NULL },
	{ 0,			0,	0,	NULL }
};

static int sel_template = 0, scroll_template = 0;

/* UI */

static void refresh_template(void);

static void refresh_display(void)
{
	char text[16];
	int bat = battery_info.battery_percent;

	fb_clear();

	/* header */
	fb_setbg(FB_COLOR_WHITE);
	if (1) {
		fb_setfg(FB_COLOR_BLUE);
		fb_setfont(FB_FONT_HELVR08);
		fb_gotoxy(0, 7);
		fb_putstr("Osmocom EMI", -1);
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
		if (transmitter_on) {
			sprintf(text, "%cDEF%c", (power >= 30) ? 'D':'G',
				(power >= 30) ? 'F':'G');
			fb_putstr(text, framebuffer->width);
		} else
			fb_putstr("GGEGG", framebuffer->width);
		fb_setfg(FB_COLOR_GREEN);
		fb_gotoxy(0, 10);
		fb_boxto(framebuffer->width - 1, 10);
	}
	fb_setfg(FB_COLOR_BLACK);
	fb_setfont(FB_FONT_C64);

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

	/* select template */
	if (mode == MODE_TEMPLATE) {
		int i;

		for (i = 0; i < 4; i++) {
			if (!templates[scroll_template + i].name)
				break;
			if (scroll_template + i == cursor) {
				fb_setfg(FB_COLOR_WHITE);
				fb_setbg(FB_COLOR_BLUE);
			}
			fb_gotoxy(0, 20 + i * 10);
			fb_putstr(templates[scroll_template + i].name,
				framebuffer->width);
			if (scroll_template + i == cursor) {
				fb_setfg(FB_COLOR_BLACK);
				fb_setbg(FB_COLOR_WHITE);
			}
		}
	}

	/* Frequency / power / template */
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
#ifndef CONFIG_TX_ENABLE
		fb_setfg(FB_COLOR_RED);
		fb_putstr("TX DISABLED!", framebuffer->width);
		fb_setfg(FB_COLOR_BLACK);
#else
		/* in case of RACH, burst_map is not set. we always display
		 * power. */
		if (transmitter_on || !templates[sel_template].burst_map) {
			sprintf(text, "Power %d dBm", power);
			fb_putstr(text, framebuffer->width);
		} else
			fb_putstr("Power -off-", framebuffer->width);
#endif

		fb_gotoxy(0, 50);
		fb_putstr(templates[sel_template].name, framebuffer->width);
		fb_setbg(FB_COLOR_WHITE);
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
	else if (mode == MODE_TEMPLATE)
		sprintf(text, "%s   %s", "back", "enter");
	else
		sprintf(text, "%s       %s", (pcs) ? "PCS" : "DCS",
			(uplink || !templates[sel_template].burst_map)
				? "UL" : "DL");
	fb_putstr(text, -1);
	fb_setfg(FB_COLOR_BLACK);
	fb_setfont(FB_FONT_HELVR08);
	fb_gotoxy(0, framebuffer->height - 2);
	sprintf(text, "%d", l1s.emi.tone / 25);
	fb_putstr(text, -1);

	fb_flush();
}

static void exit_arfcn(void)
{
	mode = MODE_MAIN;
	refresh_display();
}

static void enter_arfcn(enum key_codes code)
{
	/* enter mode */
	if (mode != MODE_ARFCN) {
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
		mode = MODE_MAIN;
		refresh_display();
		refresh_template();
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
	refresh_template();

	return 0;
}

static void toggle_dcs_pcs(void)
{
	pcs = !pcs;
	refresh_display();
	refresh_template();
}

static void toggle_up_down(void)
{
	uplink = !uplink;
	refresh_display();
	refresh_template();
}

static void exit_template(void)
{
	mode = MODE_MAIN;
	refresh_display();
}

static void enter_template(enum key_codes code)
{
	/* enter mode */
	if (mode != MODE_TEMPLATE) {
		mode = MODE_TEMPLATE;
		cursor = sel_template;
		if (cursor < scroll_template)
			scroll_template = cursor;
		else if (cursor >= scroll_template + 4)
			scroll_template = cursor - 3;
		refresh_display();
		return;
	}

	if (code == KEY_LEFT_SB) {
		/* back */
		exit_template();
		return;
	}

	if (code == KEY_RIGHT_SB) {
		sel_template = cursor;
		mode = MODE_MAIN;
		refresh_template();
		refresh_display();
		return;
	}

	if (code == KEY_UP) {
		if (cursor == 0)
			return;
		cursor--;
		if (cursor < scroll_template)
			scroll_template = cursor;
	}

	if (code == KEY_DOWN) {
		if (!templates[cursor + 1].name)
			return;
		cursor++;
		if (cursor >= scroll_template + 4)
			scroll_template = cursor - 3;
	}

	refresh_display();
}

static void tone_inc_dec(int inc)
{
	if (inc) {
		if (l1s.emi.tone + 25 <= 255)
			l1s.emi.tone += 25;
	} else {
		if (l1s.emi.tone - 25 >= 0)
			l1s.emi.tone -= 25;
	}

	refresh_display();
}

static void power_inc_dec(int inc)
{
	if (inc) {
		if (power < 30)
			power += 2;
	} else {
		if (power > 0)
			power -= 2;
	}

	refresh_template();
	/* refresh_template() might change tx power, so we refresh
	 * display afterwards: */
	refresh_display();
}

static void toggle_transmitter(void)
{
	transmitter_on = !transmitter_on;
	refresh_display();
	refresh_template();
}

static void send_rach(void);

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
			 || key_pressed_code == KEY_RIGHT
			 || key_pressed_code == KEY_UP
			 || key_pressed_code == KEY_DOWN
			 || key_pressed_code == KEY_STAR
			 || key_pressed_code == KEY_HASH)
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
		if (mode == MODE_MAIN || mode == MODE_ARFCN)
			enter_arfcn(key_code);
		break;
	case KEY_UP:
		if (mode == MODE_MAIN)
			power_inc_dec(1);
		else if (mode == MODE_TEMPLATE)
			enter_template(key_code);
		break;
	case KEY_DOWN:
		if (mode == MODE_MAIN)
			power_inc_dec(0);
		else if (mode == MODE_TEMPLATE)
			enter_template(key_code);
		break;
	case KEY_RIGHT:
		if (mode == MODE_MAIN)
			inc_dec_arfcn(1);
		break;
	case KEY_LEFT:
		if (mode == MODE_MAIN)
			inc_dec_arfcn(0);
		break;
	case KEY_LEFT_SB:
		if (mode == MODE_MAIN)
			toggle_dcs_pcs();
		else if (mode == MODE_ARFCN)
			enter_arfcn(key_code);
		else if (mode == MODE_TEMPLATE)
			exit_template();
		break;
	case KEY_RIGHT_SB:
		if (mode == MODE_MAIN)
			toggle_up_down();
		else if (mode == MODE_ARFCN)
			enter_arfcn(key_code);
		else if (mode == MODE_TEMPLATE)
			enter_template(key_code);
		break;
	case KEY_OK:
		if (mode == MODE_MAIN) {
			if (!templates[sel_template].burst_map) {
				send_rach();
				break;
			}
			toggle_transmitter();
		}
		break;
	case KEY_MENU:
		if (mode == MODE_MAIN)
			enter_template(key_code);
		break;
	case KEY_POWER:
		if (mode == MODE_MAIN) {
			transmitter_on = 1; /* is turned of by toggeling */
			toggle_transmitter();
		} else if (mode == MODE_ARFCN)
			exit_arfcn();
		else if (mode == MODE_TEMPLATE)
			exit_template();
		break;
	case KEY_STAR:
		if (mode == MODE_MAIN)
			tone_inc_dec(0);
		break;
	case KEY_HASH:
		if (mode == MODE_MAIN)
			tone_inc_dec(1);
		break;
	default:
		break;
	}

	key_code = KEY_INV;
}

/* Main Program */
const char *hr = "======================================================================\n";

static void set_tx_power(int dbm)
{
	enum gsm_band _band;

	_band = gsm_arfcn2band(arfcn);
	l1s.tx_power = ms_pwr_ctl_lvl(_band, dbm);
}

static void send_rach(void)
{
	struct msgb *msg = l1ctl_msgb_alloc(L1CTL_RACH_REQ);
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *)
			msgb_put(msg, sizeof(*ul));;
	struct l1ctl_rach_req *rach_req = (struct l1ctl_rach_req *)
			msgb_put(msg, sizeof(*rach_req));

	set_tx_power(power);
	l1s.serving_cell.arfcn = arfcn;

	rach_req->ra = 0x00;
	rach_req->offset = 0;
	rach_req->combined = 0;

	l1a_l23_rx(SC_DLCI_L1A_L23, msg);

	/* make one click */
	buzzer_volume(l1s.emi.tone);
	buzzer_note(NOTE(NOTE_C, OCTAVE_5));
	delay_us(300);
	buzzer_volume(0);
}

static void refresh_template(void)
{
	mframe_disable(MF_TASK_EMI);
	buzzer_volume(0);

	if (transmitter_on && templates[sel_template].burst_map) {
		/* hack: because we cannot control power of multiple burst,
		 * we just force power to 1w, as it is fixed in dsp_extcode
		 */
		if (templates[sel_template].slots > 1)
			power = 30;
		set_tx_power(power);
		l1s.emi.burst_curr = 0;
		l1s.emi.burst_map = templates[sel_template].burst_map;
		l1s.emi.burst_num = templates[sel_template].burst_num;
		l1s.emi.slots = templates[sel_template].slots;
		l1s.emi.arfcn = arfcn; /* prim_rach always uses uplink */
		if (uplink)
			l1s.emi.arfcn |= ARFCN_UPLINK;
		mframe_enable(MF_TASK_EMI);
	}
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

static void l1a_l23_tx(struct msgb *msg)
{
	msgb_free(msg);
}


#include <abb/twl3025.h>
#define ABB_VAL(reg, val) ( (((reg) & 0x1F) << 1) | (((val) & 0x3FF) << 6) )
static void calculate_power(void)
{
	int auxapc;

	auxapc = apc_tx_dbm2auxapc(GSM_BAND_900, power);
	auxapc_abb = ABB_VAL(AUXAPC, auxapc);
	printf("Power AUXAPC value for %ddBm = %d (0x%04x)\n", power, auxapc,
		auxapc_abb);
}

int main(void)
{
	int i;

	board_init(1);

	puts("\n\nOsmocomBB EMI Test Transmitter (revision " GIT_REVISION
		")\n");
	puts(hr);

	calculate_power();

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

	layer1_init(1);
	l1a_l23_tx_cb = l1a_l23_tx;

//	display_unset_attr(DISP_ATTR_INVERT);

	tpu_frame_irq_en(1, 1);

	buzzer_mode_pwt(1);
	buzzer_volume(0);

	/* inc 0 to 1 and refresh (display/template) */
	inc_dec_arfcn(1);

	while (1) {
		for (i = 0; i < 256; i++) {
			l1a_compl_execute();
			osmo_timers_update();
			handle_key_code();
			l1a_l23_handler();
		}
		refresh_display();
	}

	/* NOT REACHED */

	twl3025_power_off();
}

