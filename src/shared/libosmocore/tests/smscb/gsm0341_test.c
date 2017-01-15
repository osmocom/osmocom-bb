/*
 * (C) 2014 by Harald Welte <laforge@gnumonks.org>
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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <osmocom/gsm/protocol/gsm_03_41.h>
#include <osmocom/gsm/gsm0341.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/msgb.h>

struct gsm341_ms_message *gen_msg_from_text(uint16_t msg_id, const char *text)
{
	struct gsm341_ms_message *cbmsg;
	int text_len = strlen(text);
	/* assuming default GSM alphabet, the encoded payload cannot be
	 * longer than the input text */
	uint8_t payload[text_len];
	int payload_octets;

	srand(time(NULL));

	gsm_7bit_encode_n(payload, sizeof(payload), text, &payload_octets);
	//cbmsg = gsm0341_build_msg(NULL, 0, rand(), 0, msg_id, 0x0f, 1, 1, payload, payload_octets);
	cbmsg = gsm0341_build_msg(NULL, 0, rand(), 0, msg_id, 0x00, 1, 1, payload, payload_octets);

	printf("%s\n", osmo_hexdump_nospc((uint8_t *)cbmsg, sizeof(*cbmsg)+payload_octets));

	return cbmsg;
}

int main(int argc, char **argv)
{
	uint16_t msg_id = GSM341_MSGID_ETWS_CMAS_MONTHLY_TEST;
	char *text = "Mahlzeit!";
	char tbuf[GSM341_MAX_CHARS+1];

	if (argc > 1)
		msg_id = atoi(argv[1]);

	if (argc > 2)
		text = argv[2];

	strncpy(tbuf, text, GSM341_MAX_CHARS);
	if (strlen(text) < GSM341_MAX_CHARS)
		memset(tbuf+strlen(text), GSM341_7BIT_PADDING,
			sizeof(tbuf)-strlen(text));
	tbuf[GSM341_MAX_CHARS] = 0;

	gen_msg_from_text(msg_id, tbuf);

	return EXIT_SUCCESS;
}
