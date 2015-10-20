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

enum stats_reporter_type {
	STATS_REPORTER_STATSD,
};

struct stats_reporter {
	enum stats_reporter_type type;
	char *name;

	/* config */
	int enabled;
	int interval;
	char *name_prefix;
	char *dest_addr_str;
	char *bind_addr_str;
	int dest_port;
	int mtu;

	/* state */
	int running;
	struct sockaddr dest_addr;
	int dest_addr_len;
	struct sockaddr bind_addr;
	int bind_addr_len;
	int fd;

	struct llist_head list;
};

void stats_init(void *ctx);
int stats_report();

struct stats_reporter *stats_reporter_alloc(enum stats_reporter_type type,
	const char *name);
void stats_reporter_free(struct stats_reporter *srep);
struct stats_reporter *stats_reporter_create_statsd(const char *name);

struct stats_reporter *stats_reporter_find(enum stats_reporter_type type,
	const char *name);

int stats_reporter_set_remote_addr(struct stats_reporter *srep, const char *addr);
int stats_reporter_set_remote_port(struct stats_reporter *srep, int port);
int stats_reporter_set_local_addr(struct stats_reporter *srep, const char *addr);
int stats_reporter_set_interval(struct stats_reporter *srep, int interval);
int stats_reporter_set_name_prefix(struct stats_reporter *srep, const char *prefix);
int stats_reporter_enable(struct stats_reporter *srep);
int stats_reporter_disable(struct stats_reporter *srep);
