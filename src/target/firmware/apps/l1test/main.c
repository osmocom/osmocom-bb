/* main program of Free Software for Calypso Phone */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
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

#include <debug.h>
#include <memory.h>
#include <rffe.h>
#include <keypad.h>
#include <board.h>

#include <abb/twl3025.h>
#include <display.h>
#include <rf/trf6151.h>

#include <comm/sercomm.h>
#include <comm/timer.h>

#include <calypso/clock.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/irq.h>
#include <calypso/misc.h>

#include <layer1/sync.h>
#include <layer1/tpu_window.h>

#define SCAN

#ifdef SCAN
/* if scanning is enabled, scan from 0 ... 124 */
#define BASE_ARFCN	0
#else
/* fixed ARFCN in GSM1800 at which Harald has his GSM test license */
#define BASE_ARFCN	871
#endif

/* FIXME: We need proper calibrated delay loops at some point! */
void delay_us(unsigned int us)
{
	volatile unsigned int i;

	for (i= 0; i < us*4; i++) { i; }
}

void delay_ms(unsigned int ms)
{
	volatile unsigned int i;

	for (i= 0; i < ms*1300; i++) { i; }
}

/* Main Program */
const char *hr = "======================================================================\n";

/* Best ARFCN MAP ************************************************************/

struct arfcn_map {
	uint16_t arfcn;
	int16_t dbm8;
};

static struct arfcn_map best_arfcn_map[10];
static void best_arfcn_update(uint16_t arfcn, int16_t dbm8)
{
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(best_arfcn_map); i++) {
		if (best_arfcn_map[i].dbm8 < dbm8 ||
		    best_arfcn_map[i].dbm8 == 0) {
			best_arfcn_map[i].dbm8 = dbm8;
			best_arfcn_map[i].arfcn = arfcn;
			return;
		}
	}
}

static void best_arfcn_dump(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(best_arfcn_map); i++) {
		if (best_arfcn_map[i].dbm8 == 0)
			continue;
		printf("ARFCN %3d: %d dBm\n",
			best_arfcn_map[i].arfcn,
			best_arfcn_map[i].dbm8/8);
	}
}


/* MAIN program **************************************************************/

enum l1test_state {
	STATE_NONE,
	STATE_PM,
	STATE_FB,
};

static void l1test_state_change(enum l1test_state new_state)
{
	switch (new_state) {
	case STATE_PM:
		puts("Performing power measurement over GSM900\n");
		l1s_pm_test(1, BASE_ARFCN);
		break;
	case STATE_FB:
		puts("Starting FCCH Recognition\n");
		l1s_fb_test(1, 0);
		break;
	case STATE_NONE:
		/* disable frame interrupts */
		tpu_frame_irq_en(0, 0);
		break;
	}
}

/* completion call-back for the L1 Sync Pwer Measurement */
static void l1s_signal_cb(struct l1_signal *sig)
{
	uint16_t i, next_arfcn;

	switch (sig->signum) {
	case L1_SIG_PM:
		best_arfcn_update(sig->arfcn, sig->pm.dbm8[0]);
		next_arfcn = sig->arfcn + 1;

		if (next_arfcn >= 124) {
			puts("ARFCN Top 10 Rx Level\n");
			best_arfcn_dump();

			trf6151_rx_window(0, best_arfcn_map[0].arfcn, 40, 0);
			tpu_end_scenario();

			/* PM phase completed, do FB det */
			l1test_state_change(STATE_FB);

			break;
		}

		/* restart Power Measurement */
		l1s_pm_test(1, next_arfcn);
		break;
	case L1_SIG_NB:
		puts("NB SNR ");
		for (i = 0; i < 4; i++) {
			uint16_t snr = sig->nb.meas[i].snr;
			printf("%d.%03u ", l1s_snr_int(snr), l1s_snr_fract(snr));
		}
		putchar('\n');
		printf("--> Frame %d %d 0x%04X ",  sig->nb.fire, sig->nb.crc, sig->nb.num_biterr);
		for (i = 0; i < ARRAY_SIZE(sig->nb.frame); i++)
			printf("%02X ", sig->nb.frame[i]);
		putchar('\n');
		break;
	}
}

static void key_handler(enum key_codes code, enum key_states state);

int main(void)
{
	puts("\n\nHello World from " __FILE__ " program code\n");

	puts(hr);
	/* Dump device identification */
	dump_dev_id();
	puts(hr);

	keypad_set_handler(&key_handler);

	/* Dump clock config aftee PLL set */
	calypso_clk_dump();
	puts(hr);

	display_set_attr(DISP_ATTR_INVERT);
	display_puts("l1test.bin");

	layer1_init();
	l1s_set_handler(&l1s_signal_cb);

	//dsp_checksum_task();
#ifdef SCAN
	l1test_state_change(STATE_PM);
#else
	l1test_state_change(STATE_FB);
#endif
	tpu_frame_irq_en(1, 1);

	while (1) {
		update_timers();
	}

	/* NOT REACHED */

	twl3025_power_off();
}

static int8_t vga_gain = 40;
static int high_gain = 0;
static int afcout = 0;

static void update_vga_gain(void)
{
	printf("VGA Gain: %u %s\n", vga_gain, high_gain ? "HIGH" : "LOW");
	trf6151_set_gain(vga_gain, high_gain);
	tpu_enq_sleep();
	tpu_enable(1);
	tpu_wait_idle();
}

static void tspact_toggle(uint8_t num)
{
	printf("TSPACT%u toggle\n", num);
	tsp_act_toggle((1 << num));
	tpu_enq_sleep();
	tpu_enable(1);
	tpu_wait_idle();
}

static void key_handler(enum key_codes code, enum key_states state)
{
	if (state != PRESSED)
		return;

	switch (code) {
	case KEY_1:	/* VGA gain decrement */
		vga_gain -= 2;
		if (vga_gain < 14)
			vga_gain = 14;
		update_vga_gain();
		break;
	case KEY_2: 	/* High/Low Rx gain */
		high_gain ^= 1;
		update_vga_gain();
		break;
	case KEY_3:	/* VGA gain increment */
		vga_gain += 2;
		if (vga_gain > 40)
			vga_gain = 40;
		update_vga_gain();
		break;
	case KEY_4:
		tspact_toggle(6);	/* TRENA (RFFE) */
		break;
	case KEY_5:
		tspact_toggle(8);	/* GSM_TXEN (RFFE) */
		break;
	case KEY_6:
		tspact_toggle(1);	/* PAENA (RFFE) */
		break;
	case KEY_7:			/* decrement AFC OUT */
		afcout -= 100;
		if (afcout < -4096)
			afcout = -4096;
		twl3025_afc_set(afcout);
		printf("AFC OUT: %u\n", twl3025_afcout_get());
		break;
	case KEY_9:			/* increase AFC OUT */
		afcout += 100;
		if (afcout > 4095)
			afcout = 4095;
		twl3025_afc_set(afcout);
		printf("AFC OUT: %u\n", twl3025_afcout_get());
		break;
	default:
		break;
	}
}
