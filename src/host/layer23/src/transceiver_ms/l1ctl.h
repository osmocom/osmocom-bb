/*
 * l1ctl.h
 *
 * L1CTL interface
 *
 * Copyright (C) 2014  Sylvain Munaut <tnt@246tNt.com>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __TRXMS_L1CTL_H__
#define __TRXMS_L1CTL_H__

struct l1ctl_link;

int l1ctl_new_cb(void *data, struct l1ctl_link *l1l);

#endif /* __TRXMS_L1CTL_H__ */
