/*
 * app.h
 *
 * Application state / defines
 *
 * Copyright (C) 2013  Sylvain Munaut <tnt@246tNt.com>
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

#ifndef __TRX_APP_H__
#define __TRX_APP_H__


#include <stdint.h>

#include "l1ctl_link.h"


struct log_target;
struct trx;
struct osmo_gmsk_state;
struct osmo_gmsk_trainseq;

struct app_state
{
	/* Logging */
	struct log_target *stderr_target;

	/* L1CTL link */
	struct l1ctl_link l1l;

	/* TRX link to OpenBTS */
	struct trx *trx;

	/* Signal processing */
	struct osmo_gmsk_state *gs;
	struct osmo_gmsk_trainseq *train_ab;

	/* Options */
	uint16_t arfcn_sync;
};


#endif /* __TRX_APP_H__ */
