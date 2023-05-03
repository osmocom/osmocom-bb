/* APN Context
 * (C) 2023 by sysmocom - s.m.f.c. GmbH <info@sysmocom.de>
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
 */

#pragma once

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/select.h>
#include <osmocom/core/tun.h>
#include <osmocom/bb/common/apn_fsm.h>

struct osmocom_ms;

#define APN_TYPE_IPv4	0x01	/* v4-only */
#define APN_TYPE_IPv6	0x02	/* v6-only */
#define APN_TYPE_IPv4v6	0x04	/* v4v6 dual-stack */

struct osmobb_apn {
	/* list of APNs inside MS */
	struct llist_head list;
	/* back-pointer to MS */
	struct osmocom_ms *ms;

	bool started;

	struct {
		/* Primary name */
		char *name;
		/* name of the network device */
		char *dev_name;
		/* netns name of the network device, NULL = default netns */
		char *dev_netns_name;
		/* types supported address types on this APN */
		uint32_t apn_type_mask;
		/* administratively shutdown (true) or not (false) */
		bool shutdown;
		/* transmit G-PDU sequence numbers (true) or not (false) */
		bool tx_gpdu_seq;
	} cfg;
	struct osmo_tundev *tun;
	struct apn_fsm_ctx fsm;
};

struct osmobb_apn *apn_alloc(struct osmocom_ms *ms, const char *name);
void apn_free(struct osmobb_apn *apn);
int apn_start(struct osmobb_apn *apn);
int apn_stop(struct osmobb_apn *apn);

#define LOGPAPN(level, apn, fmt, args...)			\
	LOGP(DTUN, level, "APN(%s): " fmt, (apn)->cfg.name, ## args)

#define LOGTUN(level, tun, fmt, args...) \
	LOGP(DTUN, level, "TUN(%s): " fmt, osmo_tundev_get_name(tun), ## args)
