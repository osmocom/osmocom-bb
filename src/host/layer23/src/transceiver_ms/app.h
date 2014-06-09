/*
 * app.h
 *
 * Application state / defines
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

#ifndef __TRXMS_APP_H__
#define __TRXMS_APP_H__


#include <stdint.h>

#include "l1ctl_link.h"


struct log_target;
struct trx;

struct app_state
{
	/* Logging */
	struct log_target *stderr_target;

	/* L1CTL server & active link */
	struct l1ctl_server l1s;
	struct l1ctl_link *l1l;

	/* TRX link to Transceiver */
	struct trx *trx;
};


#endif /* __TRXMS_APP_H__ */
