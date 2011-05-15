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

#ifndef MSG_FILE_H
#define MSG_FILE_H

#include <osmocom/core/linuxlist.h>

/**
 * One message in the list.
 */
struct osmo_config_entry {
	struct llist_head list;

	/* number for everyone to use */
	int nr;

	/* data from the file */
	char *mcc;
	char *mnc;
	char *option;
	char *text;
};

struct osmo_config_list {
	struct llist_head entry;
};

struct osmo_config_list* osmo_config_list_parse(void *ctx, const char *filename);

#endif
