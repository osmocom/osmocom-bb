/*
 * (C) 2010 Holger Hans Peter Freyther
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

#include <osmocom/gsm/protocol/gsm_03_41.h>

#include <stdio.h>
#include <arpa/inet.h>

static uint8_t smscb_msg[] = { 0x40, 0x10, 0x05, 0x0d, 0x01, 0x11 };

int main(int argc, char **argv)
{
	struct gsm341_ms_message *msg;

	msg = (struct gsm341_ms_message *) smscb_msg;
	printf("(srl) GS: %d MSG_CODE: %d UPDATE: %d\n",
		msg->serial.gs, GSM341_MSG_CODE(msg), msg->serial.update);
	printf("(msg) msg_id: %d\n", htons(msg->msg_id));
	printf("(dcs) group: %d language: %d\n",
		msg->dcs.language, msg->dcs.group);
	printf("(pge) page total: %d current: %d\n",
		msg->page.total, msg->page.current);

	return 0;
}
