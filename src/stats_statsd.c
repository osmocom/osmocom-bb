/*
 * (C) 2015 by Sysmocom s.f.m.c. GmbH
 *
 * Author: Jacob Erlbeck <jerlbeck@sysmocom.de>
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

#include <osmocom/core/stats.h>

#include <string.h>
#include <stdint.h>
#include <errno.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/rate_ctr.h>
#include <osmocom/core/stat_item.h>
#include <osmocom/core/msgb.h>

static int osmo_stats_reporter_statsd_send_counter(struct osmo_stats_reporter *srep,
	const struct rate_ctr_group *ctrg,
	const struct rate_ctr_desc *desc,
	int64_t value, int64_t delta);
static int osmo_stats_reporter_statsd_send_item(struct osmo_stats_reporter *srep,
	const struct osmo_stat_item_group *statg,
	const struct osmo_stat_item_desc *desc, int value);

struct osmo_stats_reporter *osmo_stats_reporter_create_statsd(const char *name)
{
	struct osmo_stats_reporter *srep;
	srep = osmo_stats_reporter_alloc(OSMO_STATS_REPORTER_STATSD, name);

	srep->have_net_config = 1;

	srep->open = osmo_stats_reporter_udp_open;
	srep->close = osmo_stats_reporter_udp_close;
	srep->send_counter = osmo_stats_reporter_statsd_send_counter;
	srep->send_item = osmo_stats_reporter_statsd_send_item;

	return srep;
}

static int osmo_stats_reporter_statsd_send(struct osmo_stats_reporter *srep,
	const char *name1, unsigned int index1, const char *name2, int value,
	const char *unit)
{
	char *buf;
	int buf_size;
	int nchars, rc = 0;
	char *fmt = NULL;
	char *prefix = srep->name_prefix;
	int old_len = msgb_length(srep->buffer);

	if (prefix) {
		if (name1) {
			if (index1 != 0)
				fmt = "%1$s.%2$s.%6$u.%3$s:%4$d|%5$s";
			else
				fmt = "%1$s.%2$s.%3$s:%4$d|%5$s";
		} else {
			fmt = "%1$s.%2$0.0s%3$s:%4$d|%5$s";
		}
	} else {
		prefix = "";
		if (name1) {
			if (index1 != 0)
				fmt = "%1$s%2$s.%6$u.%3$s:%4$d|%5$s";
			else
				fmt = "%1$s%2$s.%3$s:%4$d|%5$s";
		} else {
			fmt = "%1$s%2$0.0s%3$s:%4$d|%5$s";
		}
	}

	if (srep->agg_enabled) {
		if (msgb_length(srep->buffer) > 0 &&
			msgb_tailroom(srep->buffer) > 0)
		{
			msgb_put_u8(srep->buffer, '\n');
		}
	}

	buf = (char *)msgb_put(srep->buffer, 0);
	buf_size = msgb_tailroom(srep->buffer);

	nchars = snprintf(buf, buf_size, fmt,
		prefix, name1, name2,
		value, unit, index1);

	if (nchars >= buf_size) {
		/* Truncated */
		/* Restore original buffer (without trailing LF) */
		msgb_trim(srep->buffer, old_len);
		/* Send it */
		rc = osmo_stats_reporter_send_buffer(srep);

		/* Try again */
		buf = (char *)msgb_put(srep->buffer, 0);
		buf_size = msgb_tailroom(srep->buffer);

		nchars = snprintf(buf, buf_size, fmt,
			prefix, name1, name2,
			value, unit, index1);

		if (nchars >= buf_size)
			return -EMSGSIZE;
	}

	if (nchars > 0)
		msgb_trim(srep->buffer, msgb_length(srep->buffer) + nchars);

	if (!srep->agg_enabled)
		rc = osmo_stats_reporter_send_buffer(srep);

	return rc;
}

static int osmo_stats_reporter_statsd_send_counter(struct osmo_stats_reporter *srep,
	const struct rate_ctr_group *ctrg,
	const struct rate_ctr_desc *desc,
	int64_t value, int64_t delta)
{
	if (ctrg)
		return osmo_stats_reporter_statsd_send(srep,
			ctrg->desc->group_name_prefix,
			ctrg->idx,
			desc->name, delta, "c");
	else
		return osmo_stats_reporter_statsd_send(srep,
			NULL, 0,
			desc->name, delta, "c");
}

static int osmo_stats_reporter_statsd_send_item(struct osmo_stats_reporter *srep,
	const struct osmo_stat_item_group *statg,
	const struct osmo_stat_item_desc *desc, int value)
{
	const char *unit = desc->unit;

	if (unit == OSMO_STAT_ITEM_NO_UNIT) {
		unit = "g";
		if (value < 0)
			osmo_stats_reporter_statsd_send(srep,
				statg->desc->group_name_prefix,
				statg->idx,
				desc->name, 0, unit);
	}
	return osmo_stats_reporter_statsd_send(srep,
		statg->desc->group_name_prefix,
		statg->idx,
		desc->name, value, unit);
}
