/* (C) 2015 by Sysmocom s.f.m.c. GmbH
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
#pragma once

#include <sys/socket.h>
#include <osmocom/core/linuxlist.h>

#include <stdint.h>

struct msgb;
struct osmo_stat_item_group;
struct osmo_stat_item_desc;
struct rate_ctr_group;
struct rate_ctr_desc;

enum osmo_stats_class {
	OSMO_STATS_CLASS_UNKNOWN,
	OSMO_STATS_CLASS_GLOBAL,
	OSMO_STATS_CLASS_PEER,
	OSMO_STATS_CLASS_SUBSCRIBER,
};

enum osmo_stats_reporter_type {
	OSMO_STATS_REPORTER_LOG,
	OSMO_STATS_REPORTER_STATSD,
};

struct osmo_stats_reporter {
	enum osmo_stats_reporter_type type;
	char *name;

	unsigned int have_net_config : 1;

	/* config */
	int enabled;
	char *name_prefix;
	char *dest_addr_str;
	char *bind_addr_str;
	int dest_port;
	int mtu;
	enum osmo_stats_class max_class;

	/* state */
	int running;
	struct sockaddr dest_addr;
	int dest_addr_len;
	struct sockaddr bind_addr;
	int bind_addr_len;
	int fd;
	struct msgb *buffer;
	int agg_enabled;
	int force_single_flush;

	struct llist_head list;
	int (*open)(struct osmo_stats_reporter *srep);
	int (*close)(struct osmo_stats_reporter *srep);
	int (*send_counter)(struct osmo_stats_reporter *srep,
		const struct rate_ctr_group *ctrg,
		const struct rate_ctr_desc *desc,
		int64_t value, int64_t delta);
	int (*send_item)(struct osmo_stats_reporter *srep,
		const struct osmo_stat_item_group *statg,
		const struct osmo_stat_item_desc *desc,
		int32_t value);
};

struct osmo_stats_config {
	int interval;
};

extern struct osmo_stats_config *osmo_stats_config;

void osmo_stats_init(void *ctx);
int osmo_stats_report();

int osmo_stats_set_interval(int interval);

struct osmo_stats_reporter *osmo_stats_reporter_alloc(enum osmo_stats_reporter_type type,
	const char *name);
void osmo_stats_reporter_free(struct osmo_stats_reporter *srep);

struct osmo_stats_reporter *osmo_stats_reporter_find(enum osmo_stats_reporter_type type,
	const char *name);

int osmo_stats_reporter_set_remote_addr(struct osmo_stats_reporter *srep, const char *addr);
int osmo_stats_reporter_set_remote_port(struct osmo_stats_reporter *srep, int port);
int osmo_stats_reporter_set_local_addr(struct osmo_stats_reporter *srep, const char *addr);
int osmo_stats_reporter_set_mtu(struct osmo_stats_reporter *srep, int mtu);
int osmo_stats_reporter_set_max_class(struct osmo_stats_reporter *srep,
	enum osmo_stats_class class_id);
int osmo_stats_reporter_set_name_prefix(struct osmo_stats_reporter *srep, const char *prefix);
int osmo_stats_reporter_enable(struct osmo_stats_reporter *srep);
int osmo_stats_reporter_disable(struct osmo_stats_reporter *srep);

/* reporter creation */
struct osmo_stats_reporter *osmo_stats_reporter_create_log(const char *name);
struct osmo_stats_reporter *osmo_stats_reporter_create_statsd(const char *name);

/* helper functions for reporter implementations */
int osmo_stats_reporter_send(struct osmo_stats_reporter *srep, const char *data,
	int data_len);
int osmo_stats_reporter_send_buffer(struct osmo_stats_reporter *srep);
int osmo_stats_reporter_udp_open(struct osmo_stats_reporter *srep);
int osmo_stats_reporter_udp_close(struct osmo_stats_reporter *srep);
