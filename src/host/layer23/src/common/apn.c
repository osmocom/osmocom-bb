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
#include <arpa/inet.h>

#include <talloc.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/apn.h>
#include <osmocom/bb/common/ms.h>

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

	apn->ms = ms;
	llist_add_tail(&apn->list, &ms->gprs.apn_list);
	return apn;
}

void apn_free(struct osmobb_apn *apn)
{
	llist_del(&apn->list);
	talloc_free(apn);
}

int apn_start(struct osmobb_apn *apn)
{
	return 0;
}

int apn_stop(struct osmobb_apn *apn)
{
	return 0;
}
