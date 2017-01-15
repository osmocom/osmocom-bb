/* Utility program for ARFCN / frequency calculations */
/*
 * (C) 2011 by Harald Welte <laforge@gnumonks.org>
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
#include <getopt.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>

#include <osmocom/gsm/gsm_utils.h>

enum program_mode {
	MODE_NONE,
	MODE_A2F,
	MODE_F2A,
};

static int arfcn2freq(int arfcn)
{
	uint16_t freq10u, freq10d;

	if (arfcn < 0 || arfcn > 0xffff) {
		fprintf(stderr, "Invalid ARFCN %d\n", arfcn);
		return -EINVAL;
	}

	freq10u = gsm_arfcn2freq10(arfcn, 1);
	freq10d = gsm_arfcn2freq10(arfcn, 0);
	if (freq10u == 0xffff || freq10d == 0xffff) {
		fprintf(stderr, "Error during conversion of ARFCN %d\n",
			arfcn);
		return -EINVAL;
	}

	printf("ARFCN %4d: Uplink %4u.%1u MHz / Downlink %4u.%1u MHz\n",
		arfcn & ~ARFCN_FLAG_MASK,
		freq10u/10, freq10u%10, freq10d/10, freq10d%10);

	return 0;
}

static int freq2arfcn(int freq10, int uplink)
{
	uint16_t arfcn;

	if (uplink != 0 && uplink != 1) {
		fprintf(stderr, "Need to specify uplink or downlink\n");
		return -EINVAL;
	}

	arfcn = gsm_freq102arfcn(freq10, uplink);

	if (arfcn == 0xffff) {
		fprintf(stderr, "Unable to find matching ARFCN\n");
		return -EINVAL;
	}

	printf("%s: ARFCN %4d\n",
		gsm_band_name(gsm_arfcn2band(arfcn)),
		arfcn & ~ARFCN_FLAG_MASK);
	return 0;
}

static void help(const char *progname)
{
	printf("Usage: %s [-h] [-p] [-a arfcn] [-f freq] [-u|-d]\n",
		progname);
}

int main(int argc, char **argv)
{
	int arfcn, freq, pcs = 0, uplink = -1;
	int opt;
	char *param;
	enum program_mode mode = MODE_NONE;

	while ((opt = getopt(argc, argv, "pa:f:ud")) != -1) {
		switch (opt) {
		case 'p':
			pcs = 1;
			break;
		case 'a':
			mode = MODE_A2F;
			param = optarg;
			break;
		case 'f':
			mode = MODE_F2A;
			param = optarg;
			break;
		case 'u':
			uplink = 1;
			break;
		case 'd':
			uplink = 0;
			break;
		case 'h':
			help(argv[0]);
			exit(0);
			break;
		default:
			break;
		}
	}

	switch (mode) {
	case MODE_NONE:
		help(argv[0]);
		exit(2);
		break;
	case MODE_A2F:
		arfcn = atoi(param);
		if (pcs)
			arfcn |= ARFCN_PCS;
		arfcn2freq(arfcn);
		break;
	case MODE_F2A:
		freq = (int)(atof(param) * 10.0f);
		freq2arfcn(freq, uplink);
		break;
	}

	exit(0);
}
