/* Client for Bluetooth SIM Access Profile  */

/* (C) 2010 by Ingo Albrecht <prom@berlin.ccc.de>
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

#include <sim.h>
#include <btsap.h>
#include <errno.h>
#include <stdio.h>
#include <comm/sercomm.h>

/* XXX memdump_range */
#include <calypso/misc.h>

struct {
	int state;
} btsap;

static void handle_sap_response(uint8_t dlci, struct msgb *msg)
{
	puts("Received SAP response:");
	memdump_range(msg->data, msg->data_len);
}

int sim_init(void)
{
	btsap.state = SAP_State_NOT_CONNECTED;

	sercomm_register_rx_cb(SC_DLCI_SAP, &handle_sap_response);

	return 0;
}

int sim_connect(void)
{
	struct msgb *m;

	/* check state */
	if(btsap.state == SAP_State_NOT_CONNECTED) {
		btsap.state = SAP_State_NEGOTIATING;
	} else if (!btsap.state == SAP_State_NEGOTIATING) {
		return -EINVAL;
	}

	/* build and send connect request */
	m = sap_alloc_msg(SAP_CONNECT_REQ, 1);

	uint16_t maxmsgsize = htons(HACK_MAX_MSG);
	sap_put_param(m, SAP_Parameter_MaxMsgSize, 2, &maxmsgsize);

	return sap_send(m);
}

int sim_disconnect(void)
{
	struct msgb *m;

	if(btsap.state == SAP_State_NOT_CONNECTED
	   || btsap.state == SAP_State_NEGOTIATING) {
		return -EINVAL;
	}

	btsap.state = SAP_State_NOT_CONNECTED;

	m = sap_alloc_msg(SAP_DISCONNECT_REQ, 0);
	return sap_send(m);
}

int sim_power_on(void)
{
	struct msgb *m;

	if(btsap.state != SAP_State_IDLE) {
		return -EINVAL;
	}

	btsap.state = SAP_State_PROCESSING_SIM_ON_REQ;

	m = sap_alloc_msg(SAP_POWER_SIM_ON_REQ, 0);
	sap_send(m);
}

int sim_power_off(void)
{
	struct msgb *m;

	if(btsap.state == SAP_State_NOT_CONNECTED
	   || btsap.state == SAP_State_NEGOTIATING) {
		return -EINVAL;
	}

	btsap.state = SAP_State_PROCESSING_SIM_OFF_REQ;

	m = sap_alloc_msg(SAP_POWER_SIM_OFF_REQ, 0);
	sap_send(m);
}

int sim_reset(void)
{
	struct msgb *m;

	if(btsap.state == SAP_State_NOT_CONNECTED
	   || btsap.state == SAP_State_NEGOTIATING) {
		return -EINVAL;
	}

	btsap.state = SAP_State_PROCESSING_SIM_RESET_REQ;

	m = sap_alloc_msg(SAP_RESET_SIM_REQ, 0);
	sap_send(m);
}

int sim_transfer_atr(/* callback */);
int sim_transfer_apdu(/* callback, struct msgb *request */);
