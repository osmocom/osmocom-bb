/*
 * (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
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

#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <osmocore/talloc.h>

#include <osmocom/logging.h>
#include <osmocom/osmocom_data.h>
#include <osmocom/mncc.h>

/*
 * support functions
 */

void mncc_set_cause(struct gsm_mncc *data, int loc, int val);

/*
 * MNCCms dummy application
 */

/* this is a minimal implementation as required by GSM 04.08 */
int mncc_recv_dummy(struct osmocom_ms *ms, int msg_type, void *arg)
{
	struct gsm_mncc *data = arg;
	int callref = data->callref;
	struct gsm_mncc rel;

	/* reject, as we don't support Calls */
	memset(&rel, 0, sizeof(struct gsm_mncc));
       	rel.callref = callref;
	mncc_set_cause(&rel, GSM48_CAUSE_LOC_USER,
		GSM48_CC_CAUSE_INCOMPAT_DEST);

	return mncc_send(ms, MNCC_REL_REQ, &rel);
}


