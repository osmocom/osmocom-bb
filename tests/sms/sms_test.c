/*
 * (C) 2008 by Daniel Willmann <daniel@totalueberwachung.de>
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
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <osmocore/msgb.h>
#include <osmocore/gsm_utils.h>

int main(int argc, char** argv)
{
	printf("SMS testing\n");
	struct msgb *msg;
	uint8_t *sms;
	uint8_t i;

        /* test 7-bit coding/decoding */
	const char *input = "test text";
	uint8_t length;
	uint8_t coded[256];
	char result[256];

	length = gsm_7bit_encode(coded, input);
	gsm_7bit_decode(result, coded, length);
	if (strcmp(result, input) != 0) {
		printf("7 Bit coding failed... life sucks\n");
		printf("Wanted: '%s' got '%s'\n", input, result);
	}
}
