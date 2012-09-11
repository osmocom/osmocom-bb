/* Core routines for SIM/UICC/USIM access */
/*
 * (C) 2012 by Harald Welte <laforge@gnumonks.org>
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


#include <stdlib.h>
#include <stdint.h>

#include <osmocom/core/talloc.h>
#include <osmocom/sim/sim.h>

struct osim_decoded_data *osim_file_decode(struct osim_file *file,
					   int len, uint8_t *data)
{
	struct osim_decoded_data *dd;

	if (!file->desc->ops.parse)
		return NULL;

	dd = talloc_zero(file, struct osim_decoded_data);
	dd->file = file;

	if (file->desc->ops.parse(dd, file->desc, len, data) < 0) {
		talloc_free(dd);
		return NULL;
	} else
		return dd;
}

struct msgb *osim_file_encode(const struct osim_file_desc *desc,
				const struct osim_decoded_data *data)
{
	if (!desc->ops.encode)
		return NULL;

	return desc->ops.encode(desc, data);
}

static struct osim_decoded_element *
__element_alloc(void *ctx, const char *name, enum osim_element_type type,
		enum osim_element_repr repr)
{
	struct osim_decoded_element *elem;

	elem = talloc_zero(ctx, struct osim_decoded_element);
	if (!elem)
		return NULL;
	elem->name = name;
	elem->type = type;
	elem->representation = repr;

	if (elem->type == ELEM_T_GROUP)
		INIT_LLIST_HEAD(&elem->u.siblings);

	return elem;
}


struct osim_decoded_element *
element_alloc(struct osim_decoded_data *dd, const char *name,
	      enum osim_element_type type, enum osim_element_repr repr)
{
	struct osim_decoded_element *elem;

	elem = __element_alloc(dd, name, type, repr);
	if (!elem)
		return NULL;

	llist_add_tail(&elem->list, &dd->decoded_elements);

	return elem;
}

struct osim_decoded_element *
element_alloc_sub(struct osim_decoded_element *ee, const char *name,
	      enum osim_element_type type, enum osim_element_repr repr)
{
	struct osim_decoded_element *elem;

	elem = __element_alloc(ee, name, type, repr);
	if (!elem)
		return NULL;

	llist_add(&elem->list, &ee->u.siblings);

	return elem;
}


void add_filedesc(struct osim_file_desc *root, const struct osim_file_desc *in, int num)
{
	int i;

	for (i = 0; i < num; i++) {
		struct osim_file_desc *ofd = talloc_memdup(root, &in[i], sizeof(*in));
		llist_add_tail(&ofd->list, &root->child_list);
	}
}

struct osim_file_desc *alloc_df(void *ctx, uint16_t fid, const char *name)
{
	struct osim_file_desc *mf;

	mf = talloc_zero(ctx, struct osim_file_desc);
	mf->type = TYPE_DF;
	mf->fid = fid;
	mf->short_name = name;
	INIT_LLIST_HEAD(&mf->child_list);

	return mf;
}

struct osim_file_desc *
add_df_with_ef(struct osim_file_desc *parent,
		uint16_t fid, const char *name,
		const struct osim_file_desc *in, int num)
{
	struct osim_file_desc *df;

	df = alloc_df(parent, fid, name);
	df->parent = parent;
	llist_add_tail(&df->list, &parent->child_list);
	add_filedesc(df, in, num);

	return df;
}

struct osim_file_desc *
add_adf_with_ef(struct osim_file_desc *parent,
		const uint8_t *adf_name, uint8_t adf_name_len,
		const char *name, const struct osim_file_desc *in,
		int num)
{
	struct osim_file_desc *df;

	df = alloc_df(parent, 0xffff, name);
	df->type = TYPE_ADF;
	df->df_name = adf_name;
	df->df_name_len = adf_name_len;
	df->parent = parent;
	llist_add_tail(&df->list, &parent->child_list);
	add_filedesc(df, in, num);

	return df;
}

struct osim_file_desc *
osim_file_find_name(struct osim_file_desc *parent, const char *name)
{
	struct osim_file_desc *ofd;
	llist_for_each_entry(ofd, &parent->child_list, list) {
		if (!strcmp(ofd->short_name, name)) {
			return ofd;
		}
	}
	return NULL;
}

/* create an APDU header
 * APDU format as defined in ISO/IEC 7816-4:2005(E) §5.1
 * - cla: CLASS byte
 * - ins: INSTRUCTION byte
 * - p1: Parameter 1 byte
 * - p2: Parameter 2 byte
 * - lc: number of bytes in the command data field Nc, which will encoded in 0, 1 or 3 bytes into Lc
 * - le: maximum number of bytes expected in the response data field,  which will encoded in 0, 1, 2 or 3 bytes into Le
 */
struct msgb *osim_new_apdumsg(uint8_t cla, uint8_t ins, uint8_t p1,
			      uint8_t p2, uint16_t lc, uint16_t le)
{
	struct osim_apdu_cmd_hdr *ch;
	struct msgb *msg = msgb_alloc(lc+le+sizeof(*ch)+2, "APDU");
	if (!msg)
		return NULL;

	ch = (struct osim_apdu_cmd_hdr *) msgb_put(msg, sizeof(*ch));
	msg->l2h = (char *) ch;

	ch->cla = cla;
	ch->ins = ins;
	ch->p1 = p1;
	ch->p2 = p2;

	msgb_apdu_lc(msg) = lc;
	msgb_apdu_le(msg) = le;

	if (lc == 0 && le == 0)
		msgb_apdu_case(msg) = APDU_CASE_1;
	else if (lc == 0 && le >= 1) {
		if (le <= 256)
			msgb_apdu_case(msg) = APDU_CASE_2;
		else
			msgb_apdu_case(msg) = APDU_CASE_2_EXT;
	} else if (le == 0 && lc >= 1) {
		if (lc <= 255)
			msgb_apdu_case(msg) = APDU_CASE_3;
		else
			msgb_apdu_case(msg) = APDU_CASE_3_EXT;
	} else if (lc >= 1 && le >= 1) {
		if (lc <= 255 & le <= 256)
			msgb_apdu_case(msg) = APDU_CASE_4;
		else
			msgb_apdu_case(msg) = APDU_CASE_4_EXT;
	}

	return msg;
}
