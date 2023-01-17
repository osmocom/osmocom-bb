/*
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

#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>

#include <talloc.h>
#include <osmocom/core/socket.h>
#include <osmocom/core/sockaddr_str.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/apn.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/common/l23_app.h>

struct osmobb_apn *apn_alloc(struct osmocom_ms *ms, const char *name)
{
	struct osmobb_apn *apn;
	apn = talloc_zero(ms, struct osmobb_apn);
	if (!apn)
		return NULL;

	talloc_set_name(apn, "apn_%s", name);
	apn->cfg.name = talloc_strdup(apn, name);
	apn->cfg.shutdown = true;
	apn->cfg.tx_gpdu_seq = true;

	apn->tun = osmo_tundev_alloc(apn, name);
	if (!apn->tun) {
		talloc_free(apn);
		return NULL;
	}
	osmo_tundev_set_priv_data(apn->tun, apn);

	apn->ms = ms;
	llist_add_tail(&apn->list, &ms->gprs.apn_list);
	return apn;
}

void apn_free(struct osmobb_apn *apn)
{
	llist_del(&apn->list);
	osmo_tundev_free(apn->tun);
	talloc_free(apn);
}

int apn_start(struct osmobb_apn *apn)
{
	struct l23_app_info *app_info = l23_app_info();
	int rc;

	if (apn->started)
		return 0;

	LOGPAPN(LOGL_INFO, apn, "Opening TUN device %s\n", apn->cfg.dev_name);
	/* Set TUN library callback. Must have been configured by the app: */
	OSMO_ASSERT(app_info && app_info->tun_data_ind_cb);
	osmo_tundev_set_data_ind_cb(apn->tun, app_info->tun_data_ind_cb);
	osmo_tundev_set_dev_name(apn->tun, apn->cfg.dev_name);
	osmo_tundev_set_netns_name(apn->tun, apn->cfg.dev_netns_name);

	rc = osmo_tundev_open(apn->tun);
	if (rc < 0) {
		LOGPAPN(LOGL_ERROR, apn, "Failed to configure tun device\n");
		return -1;
	}

	LOGPAPN(LOGL_INFO, apn, "Opened TUN device %s\n", osmo_tundev_get_dev_name(apn->tun));

	struct osmo_netdev *netdev = osmo_tundev_get_netdev(apn->tun);
	struct osmo_sockaddr_str osa_str = {};
	struct osmo_sockaddr osa = {};
	rc = osmo_sockaddr_str_from_str2(&osa_str, "192.168.200.1");
	if (rc < 0)
		return -1;
	rc = osmo_sockaddr_str_to_sockaddr(&osa_str, &osa.u.sas);
	if (rc < 0)
		return -1;
	rc = osmo_netdev_add_addr(netdev, &osa, 24);
	if (rc < 0)
		return -1;

	rc = osmo_netdev_ifupdown(netdev, true);
	if (rc < 0)
		return -1;

	/* Add default route (0.0.0.0/0): */
	rc = osmo_sockaddr_str_from_str2(&osa_str, "0.0.0.0");
	if (rc < 0)
		return -1;
	rc = osmo_sockaddr_str_to_sockaddr(&osa_str, &osa.u.sas);
	if (rc < 0)
		return -1;
	rc = osmo_netdev_add_route(netdev, &osa, 0, NULL);
	if (rc < 0)
		return -1;
	/* TODO: set IP addresses on the tun device once we receive them from GGSN. See
	   osmo-ggsn.git's apn_start() */

	LOGPAPN(LOGL_NOTICE, apn, "Successfully started\n");
	apn->started = true;
	return 0;
}

int apn_stop(struct osmobb_apn *apn)
{
	LOGPAPN(LOGL_NOTICE, apn, "Stopping\n");

	/* shutdown whatever old state might be left */
	if (apn->tun) {
		/* release tun device */
		LOGPAPN(LOGL_INFO, apn, "Closing TUN device %s\n",
			osmo_tundev_get_dev_name(apn->tun));
		osmo_tundev_close(apn->tun);
	}

	apn->started = false;
	return 0;
}
