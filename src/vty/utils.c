/* utility routines for printing common objects in the Osmocom world */

/* (C) 2009-2010 by Harald Welte <laforge@gnumonks.org>
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
#include <inttypes.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/rate_ctr.h>
#include <osmocom/core/stat_item.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/statistics.h>

#include <osmocom/vty/vty.h>

/* \file utils.c */

/*! \addtogroup rate_ctr
 *  @{
 */

struct vty_out_context {
	struct vty *vty;
	const char *prefix;
	int max_level;
};

static int rate_ctr_handler(
	struct rate_ctr_group *ctrg, struct rate_ctr *ctr,
	const struct rate_ctr_desc *desc, void *vctx_)
{
	struct vty_out_context *vctx = vctx_;
	struct vty *vty = vctx->vty;

	vty_out(vty, " %s%s: %8" PRIu64 " "
		"(%" PRIu64 "/s %" PRIu64 "/m %" PRIu64 "/h %" PRIu64 "/d)%s",
		vctx->prefix, desc->description, ctr->current,
		ctr->intv[RATE_CTR_INTV_SEC].rate,
		ctr->intv[RATE_CTR_INTV_MIN].rate,
		ctr->intv[RATE_CTR_INTV_HOUR].rate,
		ctr->intv[RATE_CTR_INTV_DAY].rate,
		VTY_NEWLINE);

	return 0;
}

/*! \brief print a rate counter group to given VTY
 *  \param[in] vty The VTY to which it should be printed
 *  \param[in] prefix Any additional log prefix ahead of each line
 *  \param[in] ctrg Rate counter group to be printed
 */
void vty_out_rate_ctr_group(struct vty *vty, const char *prefix,
			    struct rate_ctr_group *ctrg)
{
	struct vty_out_context vctx = {vty, prefix};

	vty_out(vty, "%s%s:%s", prefix, ctrg->desc->group_description, VTY_NEWLINE);

	rate_ctr_for_each_counter(ctrg, rate_ctr_handler, &vctx);
}

static int osmo_stat_item_handler(
	struct osmo_stat_item_group *statg, struct osmo_stat_item *item, void *vctx_)
{
	struct vty_out_context *vctx = vctx_;
	struct vty *vty = vctx->vty;
	const char *unit =
		item->desc->unit != OSMO_STAT_ITEM_NO_UNIT ?
		item->desc->unit : "";

	vty_out(vty, " %s%s: %8" PRIi32 " %s%s",
		vctx->prefix, item->desc->description,
		osmo_stat_item_get_last(item),
		unit, VTY_NEWLINE);

	return 0;
}

/*! \brief print a stat item group to given VTY
 *  \param[in] vty The VTY to which it should be printed
 *  \param[in] prefix Any additional log prefix ahead of each line
 *  \param[in] statg Stat item group to be printed
 */
void vty_out_stat_item_group(struct vty *vty, const char *prefix,
			     struct osmo_stat_item_group *statg)
{
	struct vty_out_context vctx = {vty, prefix};

	vty_out(vty, "%s%s:%s", prefix, statg->desc->group_description,
		VTY_NEWLINE);
	osmo_stat_item_for_each_item(statg, osmo_stat_item_handler, &vctx);
}

static int osmo_stat_item_group_handler(struct osmo_stat_item_group *statg, void *vctx_)
{
	struct vty_out_context *vctx = vctx_;
	struct vty *vty = vctx->vty;

	if (statg->desc->class_id > vctx->max_level)
		return 0;

	if (statg->idx)
		vty_out(vty, "%s%s (%d):%s", vctx->prefix,
			statg->desc->group_description, statg->idx,
			VTY_NEWLINE);
	else
		vty_out(vty, "%s%s:%s", vctx->prefix,
			statg->desc->group_description, VTY_NEWLINE);

	osmo_stat_item_for_each_item(statg, osmo_stat_item_handler, vctx);

	return 0;
}

static int rate_ctr_group_handler(struct rate_ctr_group *ctrg, void *vctx_)
{
	struct vty_out_context *vctx = vctx_;
	struct vty *vty = vctx->vty;

	if (ctrg->desc->class_id > vctx->max_level)
		return 0;

	if (ctrg->idx)
		vty_out(vty, "%s%s (%d):%s", vctx->prefix,
			ctrg->desc->group_description, ctrg->idx, VTY_NEWLINE);
	else
		vty_out(vty, "%s%s:%s", vctx->prefix,
			ctrg->desc->group_description, VTY_NEWLINE);

	rate_ctr_for_each_counter(ctrg, rate_ctr_handler, vctx);

	return 0;
}

static int handle_counter(struct osmo_counter *counter, void *vctx_)
{
	struct vty_out_context *vctx = vctx_;
	struct vty *vty = vctx->vty;

	vty_out(vty, " %s%s: %8lu%s",
		vctx->prefix, counter->description,
		osmo_counter_get(counter), VTY_NEWLINE);

	return 0;
}

void vty_out_statistics_partial(struct vty *vty, const char *prefix,
	int max_level)
{
	struct vty_out_context vctx = {vty, prefix, max_level};

	vty_out(vty, "%sUngrouped counters:%s", prefix, VTY_NEWLINE);
	osmo_counters_for_each(handle_counter, &vctx);
	rate_ctr_for_each_group(rate_ctr_group_handler, &vctx);
	osmo_stat_item_for_each_group(osmo_stat_item_group_handler, &vctx);
}

void vty_out_statistics_full(struct vty *vty, const char *prefix)
{
	vty_out_statistics_partial(vty, prefix, INT_MAX);
}

/*! \brief Generate a VTY command string from value_string */
char *vty_cmd_string_from_valstr(void *ctx, const struct value_string *vals,
				 const char *prefix, const char *sep,
				 const char *end, int do_lower)
{
	int len = 0, offset = 0, ret, rem;
	int size = strlen(prefix) + strlen(end);
	int sep_len = strlen(sep);
	const struct value_string *vs;
	char *str;

	for (vs = vals; vs->value || vs->str; vs++)
		size += strlen(vs->str) + sep_len;

	rem = size;
	str = talloc_zero_size(ctx, size);
	if (!str)
		return NULL;

	ret = snprintf(str + offset, rem, "%s", prefix);
	if (ret < 0)
		goto err;
	OSMO_SNPRINTF_RET(ret, rem, offset, len);

	for (vs = vals; vs->value || vs->str; vs++) {
		if (vs->str) {
			int j, name_len = strlen(vs->str)+1;
			char name[name_len];

			for (j = 0; j < name_len; j++)
				name[j] = do_lower ?
					tolower(vs->str[j]) : vs->str[j];

			name[name_len-1] = '\0';
			ret = snprintf(str + offset, rem, "%s%s", name, sep);
			if (ret < 0)
				goto err;
			OSMO_SNPRINTF_RET(ret, rem, offset, len);
		}
	}
	offset -= sep_len;	/* to remove the trailing sep */
	rem += sep_len;

	ret = snprintf(str + offset, rem, "%s", end);
	if (ret < 0)
		goto err;
	OSMO_SNPRINTF_RET(ret, rem, offset, len);
err:
	str[size-1] = '\0';
	return str;
}


/*! @} */
