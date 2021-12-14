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
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <rf/readcal.h>
#include <rf/txcal.h>
#include <rf/vcxocal.h>

static int16_t afcdac_shifted;

static void afcdac_postproc(void)
{
	afc_initial_dac_value = afcdac_shifted >> 3;
}

static int verify_checksum(const uint8_t *start, size_t len)
{
	const uint8_t *p, *endp;
	uint8_t accum;

	p = start;
	endp = start + len;
	accum = 0;
	while (p < endp)
		accum += *p++;

	if (accum == *p)
		return 0;	/* good */
	else
		return -1;	/* bad */
}

static const struct calmap {
	char		*desc;
	unsigned	offset;
	size_t		record_len;
	void		*buffer;
	void		(*postproc)(void);
} rf_cal_list[] = {
	{ "afcdac",          0x528, 2,   &afcdac_shifted,     afcdac_postproc },
	{ "Tx ramps 900",    0x72B, 512, rf_tx_ramps_900,     NULL },
	{ "Tx levels 900",   0x92C, 128, rf_tx_levels_900,    NULL },
	{ "Tx calchan 900",  0x9AD, 128, rf_tx_chan_cal_900,  NULL },
	{ "Tx ramps 1800",   0xA2E, 512, rf_tx_ramps_1800,    NULL },
	{ "Tx levels 1800",  0xC2F, 128, rf_tx_levels_1800,   NULL },
	{ "Tx calchan 1800", 0xCB0, 128, rf_tx_chan_cal_1800, NULL },
	{ "Tx ramps 1900",   0xD31, 512, rf_tx_ramps_1900,    NULL },
	{ "Tx levels 1900",  0xF32, 128, rf_tx_levels_1900,   NULL },
	{ "Tx calchan 1900", 0xFB3, 128, rf_tx_chan_cal_1900, NULL },
	{ NULL,              0,     0,   NULL,                NULL }
};

void read_factory_rf_calibration(void)
{
	const struct calmap *tp;
	const uint8_t *record;

	puts("Checking factory data block for the RF calibration records\n");
	for (tp = rf_cal_list; tp->desc; tp++) {
		record = (const uint8_t *)0x027F0000 + tp->offset;
		if (verify_checksum(record, tp->record_len) < 0)
			continue;
		printf("Found '%s' record, applying\n", tp->desc);
		memcpy(tp->buffer, record, tp->record_len);
		if (tp->postproc)
			tp->postproc();
	}
}
