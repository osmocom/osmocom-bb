/*
 * (C) 2010 by Holger Hans Peter Freyther
 * (C) 2010 by On-Waves
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

#include <osmocom/core/application.h>
#include <osmocom/core/logging.h>
#include <osmocom/gsm/gsm0480.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t ussd_request[] = {
	0x0b, 0x7b, 0x1c, 0x15, 0xa1, 0x13, 0x02, 0x01,
	0x03, 0x02, 0x01, 0x3b, 0x30, 0x0b, 0x04, 0x01,
	0x0f, 0x04, 0x06, 0x2a, 0xd5, 0x4c, 0x16, 0x1b,
	0x01, 0x7f, 0x01, 0x00
};

static int parse_ussd(const uint8_t *_data, int len)
{
	uint8_t *data;
	int rc;
	struct ussd_request req;
	struct gsm48_hdr *hdr;

	data = malloc(len);
	memcpy(data, _data, len);
	hdr = (struct gsm48_hdr *) &data[0];
	rc = gsm0480_decode_ussd_request(hdr, len, &req);
	free(data);

	return rc;
}

static int parse_mangle_ussd(const uint8_t *_data, int len)
{
	uint8_t *data;
	int rc;
	struct ussd_request req;
	struct gsm48_hdr *hdr;

	data = malloc(len);
	memcpy(data, _data, len);
	hdr = (struct gsm48_hdr *) &data[0];
	hdr->data[1] = len - sizeof(*hdr) - 2;
	rc = gsm0480_decode_ussd_request(hdr, len, &req);
	free(data);

	return rc;
}

struct log_info info = {};

int main(int argc, char **argv)
{
	struct ussd_request req;
	const int size = sizeof(ussd_request);
	int i;

	osmo_init_logging(&info);

	gsm0480_decode_ussd_request((struct gsm48_hdr *) ussd_request, size, &req);
	printf("Tested if it still works. Text was: %s\n", req.text);


	printf("Testing parsing a USSD request and truncated versions\n");

	for (i = size; i > sizeof(struct gsm48_hdr); --i) {
		int rc = parse_ussd(&ussd_request[0], i);
		printf("Result for %d is %d\n", rc, i);
	}

	printf("Mangling the container now\n");
	for (i = size; i > sizeof(struct gsm48_hdr) + 2; --i) {
		int rc = parse_mangle_ussd(&ussd_request[0], i);
		printf("Result for %d is %d\n", rc, i);
	}

	return 0;
}
