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

#include <gsm.h>
#include <debug.h>
#include <memory.h>
#include <rffe.h>
#include <keypad.h>
#include <board.h>

#include <abb/twl3025.h>
#include <display/st7558.h>
#include <rf/trf6151.h>

#include <comm/sercomm.h>

#include <calypso/clock.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/irq.h>
#include <calypso/misc.h>

#include <layer1/sync.h>
#include <layer1/tpu_window.h>

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

const char *hr = "======================================================================\n";

/* MAIN program **************************************************************/

/* completion call-back for the L1 Sync Pwer Measurement */
static void l1s_signal_cb(struct l1_signal *sig)
{
	uint16_t i, next_arfcn;

	switch (sig->signum) {
	case L1_SIG_PM:
		break;
	case L1_SIG_NB:
		break;
	}
}

static void key_handler(enum key_codes code, enum key_states state);
static void la1_l23_rx_cb(uint8_t dlci, struct msgb *msg);

int main(void)
{
	board_init();
	puts("\n\nHello World from " __FILE__ " program code\n");

	puts(hr);
	/* Dump device identification */
	dump_dev_id();
	puts(hr);

	keypad_set_handler(&key_handler);

	/* Dump clock config aftee PLL set */
	calypso_clk_dump();
	puts(hr);

	st7558_set_attr(DISP_ATTR_INVERT);
	st7558_puts("layer1.bin");

	sercomm_register_rx_cb(SC_DLCI_L1A_L23, la1_l23_rx_cb);

	layer1_init();
	l1s_set_handler(&l1s_signal_cb);

	tpu_frame_irq_en(1, 1);

	while (1) {}

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

static void la1_l23_rx_cb(uint8_t dlci, struct msgb *msg)
{
	struct l1_info_ul *ul = msg->data;
	struct l1_sync_new_ccch_req *sync_req;

	if (sizeof(*ul) > msg->len) {
		printf("la1_l23_cb: Short message. %u\n", msg->len);
		goto exit;
	}

	switch (ul->msg_type) {
	case SYNC_NEW_CCCH_REQ:
		if (sizeof(*ul) + sizeof(*sync_req) > msg->len) {
			printf("Short sync msg. %u\n", msg->len);
			break;
		}

		sync_req = (struct l1_sync_new_ccch_req *) (&msg->data[0] + sizeof(*ul));
		printf("Asked to tune to frequency: %u\n", sync_req->band_arfcn);

		/* reset scheduler and hardware */
		tdma_sched_reset();
		l1s_dsp_abort();

		/* tune to specified frequency */
		trf6151_rx_window(0, sync_req->band_arfcn, 40, 0);
		tpu_end_scenario();

		puts("Starting FCCH Recognition\n");
		l1s_fb_test(1, 0);
		break;
	case DEDIC_MODE_EST_REQ:
		break;
	}

exit:
	msgb_free(msg);
}
