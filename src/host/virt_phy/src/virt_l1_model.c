
/* (C) 2016 by Sebastian Stumpf <sebastian.stumpf87@googlemail.com>
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
#include <talloc.h>

struct l1_model_ms* l1_model_ms_init(void *ctx)
{
	struct l1_model_ms *model = talloc_zero(ctx, struct l1_model_ms);
	return model;
}

void l1_model_ms_destroy(struct l1_model_ms *model)
{
	virt_um_destroy(model->vui);
	l1ctl_sock_destroy(model->lsi);
	talloc_free(model);
}
