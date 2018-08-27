
/* (C) 2016 by Sebastian Stumpf <sebastian.stumpf87@googlemail.com>
 * (C) 2017 by Harald Welte <laforge@gnumonks.org>
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <virtphy/virt_l1_model.h>
#include <virtphy/l1ctl_sap.h>
#include <virtphy/logging.h>
#include <talloc.h>

static uint32_t next_ms_nr;

struct l1_model_ms *l1_model_ms_init(void *ctx, struct l1ctl_sock_client *lsc, struct virt_um_inst *vui)
{
	struct l1_model_ms *model = talloc_zero(ctx, struct l1_model_ms);
	if (!model)
		return NULL;

	model->nr = next_ms_nr++;
	model->lsc = lsc;
	model->vui = vui;

	l1ctl_sap_init(model);

	LOGPMS(DMAIN, LOGL_INFO, model, "allocated\n");

	return model;
}

void l1_model_ms_destroy(struct l1_model_ms *model)
{
	LOGPMS(DMAIN, LOGL_INFO, model, "destroyed\n");
	l1ctl_sap_exit(model);
	talloc_free(model);
}
