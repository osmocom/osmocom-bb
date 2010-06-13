/* minimalistic telnet/network interface it might turn into a wire interface */
/* (C) 2009 by Holger Hans Peter Freyther <zecke@selfish.org>
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

#ifndef TELNET_INTERFACE_H
#define TELNET_INTERFACE_H

#include <osmocom/osmocom_data.h>
#include <osmocore/select.h>

#include <vty/vty.h>

struct telnet_connection {
	struct llist_head entry;
	struct osmocom_ms *ms;
	struct bsc_fd fd;
	struct vty *vty;
	struct debug_target *dbg;
};


extern int telnet_init(struct osmocom_ms *ms, int port);

extern int ms_vty_init(struct osmocom_ms *ms);

extern void vty_notify(struct osmocom_ms *ms, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));

#endif
