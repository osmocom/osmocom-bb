/*
 * This code was written by Mychaela Falconia <falcon@freecalypso.org>
 * who refuses to claim copyright on it and has released it as public domain
 * instead. NO rights reserved, all rights relinquished.
 *
 * Tweaked (coding style changes) by Vadim Yanitskiy <axilirator@gmail.com>
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
#include <stdint.h>
#include <string.h>

#include <rf/readcal.h>
#include <rf/vcxocal.h>
#include <rf/txcal.h>
#include <tiffs.h>

static int16_t afcdac_shifted;

static void afcdac_postproc(void)
{
	afc_initial_dac_value = afcdac_shifted >> 3;
}

static const struct calmap {
	char	*pathname;
	size_t	record_len;
	void	*buffer;
	void	(*postproc)(void);
} rf_cal_list[] = {
	{ "/gsm/rf/afcdac",          2,   &afcdac_shifted,     afcdac_postproc },
	{ "/gsm/rf/tx/ramps.850",    512, rf_tx_ramps_850,     NULL },
	{ "/gsm/rf/tx/levels.850",   128, rf_tx_levels_850,    NULL },
	{ "/gsm/rf/tx/calchan.850",  128, rf_tx_chan_cal_850,  NULL },
	{ "/gsm/rf/tx/ramps.900",    512, rf_tx_ramps_900,     NULL },
	{ "/gsm/rf/tx/levels.900",   128, rf_tx_levels_900,    NULL },
	{ "/gsm/rf/tx/calchan.900",  128, rf_tx_chan_cal_900,  NULL },
	{ "/gsm/rf/tx/ramps.1800",   512, rf_tx_ramps_1800,    NULL },
	{ "/gsm/rf/tx/levels.1800",  128, rf_tx_levels_1800,   NULL },
	{ "/gsm/rf/tx/calchan.1800", 128, rf_tx_chan_cal_1800, NULL },
	{ "/gsm/rf/tx/ramps.1900",   512, rf_tx_ramps_1900,    NULL },
	{ "/gsm/rf/tx/levels.1900",  128, rf_tx_levels_1900,   NULL },
	{ "/gsm/rf/tx/calchan.1900", 128, rf_tx_chan_cal_1900, NULL },
	{ NULL,                      0,   NULL,                NULL }
};

void read_factory_rf_calibration(void)
{
	const struct calmap *tp;
	uint8_t buf[512];
	int rc;

	puts("Checking TIFFS for the RF calibration records\n");
	for (tp = rf_cal_list; tp->pathname; tp++) {
		rc = tiffs_read_file_fixedlen(tp->pathname, buf,
					      tp->record_len);
		if (rc <= 0)
			continue;
		printf("Found '%s', applying\n", tp->pathname);
		memcpy(tp->buffer, buf, tp->record_len);
		if (tp->postproc)
			tp->postproc();
	}
}
