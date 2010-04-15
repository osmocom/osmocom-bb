/* (C) 2009,2010 by Holger Hans Peter Freyther <zecke@selfish.org>
 * (C) 2009,2010 by On-Waves
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

#include <osmocore/gsm0808.h>
#include <osmocore/protocol/gsm_08_08.h>
#include <osmocore/gsm48.h>

#include <arpa/inet.h>

#define BSSMAP_MSG_SIZE 512
#define BSSMAP_MSG_HEADROOM 128

struct msgb *gsm0808_create_layer3(struct msgb *msg_l3, uint16_t nc, uint16_t cc, int lac, int _ci)
{
	uint8_t *data;
	uint16_t *ci;
	struct msgb* msg;
	struct gsm48_loc_area_id *lai;

	msg  = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
				   "bssmap cmpl l3");
	if (!msg)
		return NULL;

	/* create the bssmap header */
	msg->l3h = msgb_put(msg, 2);
	msg->l3h[0] = 0x0;

	/* create layer 3 header */
	data = msgb_put(msg, 1);
	data[0] = BSS_MAP_MSG_COMPLETE_LAYER_3;

	/* create the cell header */
	data = msgb_put(msg, 3);
	data[0] = GSM0808_IE_CELL_IDENTIFIER;
	data[1] = 1 + sizeof(*lai) + 2;
	data[2] = CELL_IDENT_WHOLE_GLOBAL;

	lai = (struct gsm48_loc_area_id *) msgb_put(msg, sizeof(*lai));
	gsm48_generate_lai(lai, cc, nc, lac);

	ci = (uint16_t *) msgb_put(msg, 2);
	*ci = htons(_ci);

	/* copy the layer3 data */
	data = msgb_put(msg, msgb_l3len(msg_l3) + 2);
	data[0] = GSM0808_IE_LAYER_3_INFORMATION;
	data[1] = msgb_l3len(msg_l3);
	memcpy(&data[2], msg_l3->l3h, data[1]);

	/* update the size */
	msg->l3h[1] = msgb_l3len(msg) - 2;

	return msg;
}

struct msgb *gsm0808_create_reset(void)
{
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "bssmap: reset");
	if (!msg)
		return NULL;

	msg->l3h = msgb_put(msg, 6);
	msg->l3h[0] = BSSAP_MSG_BSS_MANAGEMENT;
	msg->l3h[1] = 0x04;
	msg->l3h[2] = 0x30;
	msg->l3h[3] = 0x04;
	msg->l3h[4] = 0x01;
	msg->l3h[5] = 0x20;
	return msg;
}

struct msgb *gsm0808_create_clear_complete(void)
{
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "bssmap: clear complete");
	if (!msg)
		return NULL;

	msg->l3h = msgb_put(msg, 3);
	msg->l3h[0] = BSSAP_MSG_BSS_MANAGEMENT;
	msg->l3h[1] = 1;
	msg->l3h[2] = BSS_MAP_MSG_CLEAR_COMPLETE;

	return msg;
}

struct msgb *gsm0808_create_cipher_reject(uint8_t cause)
{
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "bssmap: clear complete");
	if (!msg)
		return NULL;

	msg->l3h = msgb_put(msg, 3);
	msg->l3h[0] = BSSAP_MSG_BSS_MANAGEMENT;
	msg->l3h[1] = 2;
	msg->l3h[2] = BSS_MAP_MSG_CIPHER_MODE_REJECT;
	msg->l3h[3] = cause;

	return msg;
}

struct msgb *gsm0808_create_classmark_update(const uint8_t *classmark_data, u_int8_t length)
{
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "classmark-update");
	if (!msg)
		return NULL;

	msg->l3h = msgb_put(msg, 3);
	msg->l3h[0] = BSSAP_MSG_BSS_MANAGEMENT;
	msg->l3h[1] = 0xff;
	msg->l3h[2] = BSS_MAP_MSG_CLASSMARK_UPDATE;

	msg->l4h = msgb_put(msg, length);
	memcpy(msg->l4h, classmark_data, length);

	/* update the size */
	msg->l3h[1] = msgb_l3len(msg) - 2;
	return msg;
}

struct msgb *gsm0808_create_sapi_reject(uint8_t link_id)
{
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "bssmap: sapi 'n' reject");
	if (!msg)
		return NULL;

	msg->l3h = msgb_put(msg, 5);
	msg->l3h[0] = BSSAP_MSG_BSS_MANAGEMENT;
	msg->l3h[1] = 3;
	msg->l3h[2] = BSS_MAP_MSG_SAPI_N_REJECT;
	msg->l3h[3] = link_id;
	msg->l3h[4] = GSM0808_CAUSE_BSS_NOT_EQUIPPED;

	return msg;
}

struct msgb *gsm0808_create_assignment_failure(uint8_t cause, uint8_t *rr_cause)
{
	uint8_t *data;
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "bssmap: ass fail");
	if (!msg)
		return NULL;

	msg->l3h = msgb_put(msg, 6);
	msg->l3h[0] = BSSAP_MSG_BSS_MANAGEMENT;
	msg->l3h[1] = 0xff;
	msg->l3h[2] = BSS_MAP_MSG_ASSIGMENT_FAILURE;
	msg->l3h[3] = GSM0808_IE_CAUSE;
	msg->l3h[4] = 1;
	msg->l3h[5] = cause;

	/* RR cause 3.2.2.22 */
	if (rr_cause) {
		data = msgb_put(msg, 2);
		data[0] = GSM0808_IE_RR_CAUSE;
		data[1] = *rr_cause;
	}

	/* Circuit pool 3.22.45 */
	/* Circuit pool list 3.2.2.46 */

	/* update the size */
	msg->l3h[1] = msgb_l3len(msg) - 2;
	return msg;
}
