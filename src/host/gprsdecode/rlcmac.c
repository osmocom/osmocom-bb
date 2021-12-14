/*
 * (C) 2017-2018 by sysmocom - s.f.m.c. GmbH, Author: Max <msuraev@sysmocom.de>
 * (C) 2018 by Vadim Yanitskiy <axilirator@gmail.com>
 * (C) 2011-2012 by Luca Melette <luca@srlabs.de>
 *
 * All Rights Reserved
 *
 * SPDX-License-Identifier: GPL-2.0+
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
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>

#include <osmocom/core/gsmtap.h>

#include "l1ctl_proto.h"
#include "rlcmac.h"
#include "gsmtap.h"

static struct gprs_tbf tbf_table[32 * 2];

static inline int too_old(uint32_t current_fn, uint32_t test_fn)
{
	uint32_t delta = (current_fn - test_fn) & 0xffffffff;

	/* More and less 30 seconds from now */
	return abs(delta) > OLD_TIME;
}

static inline int bsn_is_next(uint8_t first, uint8_t second)
{
	return ((first + 1) % 128) == second;
}

void print_pkt(uint8_t *msg, size_t len)
{
	size_t i;

	printf("MSG: ");
	for (i = 0; i < len; i++)
		printf("%.02x", msg[i]);
	printf("\n");
}

void process_blocks(struct gprs_tbf *t, bool ul)
{
	uint8_t llc_data[65536], llc_first_bsn, llc_last_bsn = 0;
	unsigned skip, llc_len = 0;
	uint8_t bsn, bsn2, li_off;
	uint32_t current_fn;
	struct gprs_frag *f;
	struct gprs_lime *l;

	/* Get current "time", oldest unreassembled frag */
	bsn = t->start_bsn;
	while (t->frags[bsn].len == 0) {
		bsn = (bsn + 1) % 128;
		if (bsn == t->start_bsn) {
			printf("no valid  blocks in current TBF!\n");
			fflush(stdout);
			return;
		}
	}
	current_fn = t->frags[bsn].fn;
	t->start_bsn = bsn;

	/* Walk through fragments, mark reassembled/used blocks */
	skip = 0;
	for (bsn = t->start_bsn; bsn != ((t->last_bsn + 1) % 128); bsn = (bsn + 1) % 128) {
		/* Get fragment descriptor */
		f = &t->frags[bsn];

		printf(" bsn %d ", bsn);
		fflush(stdout);

		/* Already processed or null */
		if (!f->len) {
			printf("null\n");
			fflush(stdout);
			llc_len = 0;
			skip = 1;
			continue;
		}

		/* Check fragment age */
		if (too_old(current_fn, f->fn)) {
			printf("old segment\n");
			fflush(stdout);
			llc_len = 0;
			skip = 1;
			continue;
		}

		/* Update "time" */
		current_fn = f->fn;

		if (llc_len && !bsn_is_next(llc_last_bsn, bsn)) {
			printf("missing bsn, previous %d\n", llc_last_bsn);
			fflush(stdout);
			llc_len = 0;
			skip = 1;
			continue;
		}

		/* Check for multiple blocks/parts */
		if (f->n_blocks == 0) {
			/* Check if first part of message */
			if (!llc_len)
				llc_first_bsn = bsn;

			/* Append data to buffer */
			memcpy(&llc_data[llc_len], f->data, f->len);

			llc_len += f->len;

			llc_last_bsn = bsn;

			/* Last TBF block? (very rare condition) */
			if (f->last) {
				printf("end of TBF\n");
				fflush(stdout);
				print_pkt(llc_data, llc_len);

				gsmtap_send_llc(llc_data, llc_len, ul);

				/* Reset all fragments */
				for (bsn2 = 0; bsn2 < 128; bsn2++) {
					f = &t->frags[bsn2];
					f->len = 0;
					f->n_blocks = 0;
				}

				/* Reset buffer state */
				llc_len = 0;
				t->start_bsn = 0;
			}
		} else {
			/* Multiple data parts */
			unsigned i;
			li_off = 0;
			for (i = 0; i < f->n_blocks; i++) {
				printf("\nlime %d\n", i);
				fflush(stdout);
				l = &f->blocks[i];
				if (l->used) {
					if (llc_len) {
						printf("\nlime error!\n");
						fflush(stdout);
						llc_len = 0;
					}
				} else {
					if (!llc_len)
						llc_first_bsn = bsn;

					/* Append data to buffer */
					memcpy(&llc_data[llc_len], &f->data[li_off], l->li);

					llc_len += l->li;

					llc_last_bsn = bsn;

					if (!l->e || !l->m || (l->e && l->m)) {
						/* Message ends here */
						printf("end of message reached\n");
						fflush(stdout);
						print_pkt(llc_data, llc_len);

						gsmtap_send_llc(llc_data, llc_len, ul);

						/* Mark frags as used */
						l->used = 1;
						if (llc_first_bsn != bsn) {
						}


						llc_len = 0;
						if (!skip)
							t->start_bsn = bsn;
					}
				}

				li_off += l->li;
			}

			/* Is spare data valid? */
			if (l->m) {
				if (llc_len) {
					printf("spare and buffer not empty!\n");
					print_pkt(llc_data, llc_len);
					fflush(stdout);
				}
				if ((f->len > li_off) && (f->len-li_off < 65536)) {
					memcpy(llc_data, &f->data[li_off], f->len-li_off);
					llc_len = f->len - li_off;
					llc_first_bsn = bsn;
					llc_last_bsn = bsn;
					t->start_bsn = bsn;
				}
			}

		}
	}

	/* Shift window if needed */
	if (((t->last_bsn - t->start_bsn) % 128) > 64) {
		t->start_bsn = (t->last_bsn - 64) % 128;
		printf("shifting window\n");
		fflush(stdout);
	}
}

void rlc_data_handler(struct gprs_message *gm)
{
	int ul, off, d_bsn;
	uint8_t tfi, bsn, cv = 1, fbi = 0;
	uint32_t d_same_bsn, d_last_bsn;
	struct gprs_tbf *t, *t_prev;
	struct gprs_frag *f;
	struct gprs_lime *l;

	tfi = (gm->msg[1] & 0x3e) >> 1;
	bsn = (gm->msg[2] & 0xfe) >> 1;

	/* Get "end of TBF" according to direction */
	ul = !!(gm->arfcn & GSMTAP_ARFCN_F_UPLINK);
	if (ul) {
		cv = (gm->msg[0] & 0x3c) >> 2;
		printf("TFI %d BSN %d CV %d ", tfi, bsn, cv);
	} else {
		fbi = (gm->msg[1] & 0x01);
		printf("TFI %d BSN %d FBI %d ", tfi, bsn, fbi);
	}

	/* Get TBF descriptor for TFI,UL couple */
	t = &tbf_table[2 * tfi + ul];

	d_same_bsn = (gm->fn - t->frags[bsn].fn) & 0xffffffff;
	d_last_bsn = (gm->fn - t->frags[t->last_bsn].fn) & 0xffffffff;
	d_bsn = (bsn - t->last_bsn) % 128;

	printf("\nfn_same_bsn %d fn_last_bsn %d delta_bsn %d old_len %d\n",
		d_same_bsn, d_last_bsn, d_bsn, t->frags[bsn].len);

	/* New / old fragment decision */
	if (d_same_bsn > OLD_TIME) {
		if (d_last_bsn > OLD_TIME) {
			/* New TBF is starting, close old one... */
			t_prev = &tbf_table[2 * ((tfi + 1) % 32) + ul];
			printf("clearing TBF %d, first %d last %d\n",
				(tfi + 1) % 32, t_prev->start_bsn, t_prev->last_bsn);
			f = &t_prev->frags[t_prev->last_bsn];

			/* ...only if data is present */
			if (f->len) {
				f->last = 1;
				process_blocks(t_prev, ul);
			}

			printf("new TBF, starting from %d\n", bsn);
			t->start_bsn = 0;
			t->last_bsn = bsn;
			memset(t->frags, 0, 128 * sizeof(struct gprs_frag));
		} else {
			/* Fresh frag, current TBF */
			if ((d_bsn >= 0) || (d_bsn < -64)) {
				/* New frag */
				t->last_bsn = bsn;
			} else {
				/* Out of sequence / duplicate */
				t->frags[bsn].fn = gm->fn;
				printf("duplicate\n");
				fflush(stdout);
				return;
			}
		}
	} else {
		if (d_last_bsn > OLD_TIME) {
			printf("fucking error last_bsn!\n");
			fflush(stdout);
			return;
		} else {
			/* Fresh frag, current TBF */
			if (d_bsn > 0) {
				printf("fucking error d_bsn!\n");
				fflush(stdout);
				return;
			} else {
				if (d_bsn < -64) {
					/* New frag */
					t->last_bsn = bsn;
				} else {
					/* Duplicate */
					t->frags[bsn].fn = gm->fn;
					printf("duplicate2\n");
					fflush(stdout);
					return;
				}
			}
		}
	}

	/* Get fragment struct for current BSN */
	f = &t->frags[bsn];

	/* Scan for LI_M_E entries */
	off = 2;
	f->n_blocks = 0;
	while (!(gm->msg[off++] & 0x01)) {
		l = &f->blocks[f->n_blocks++];
		l->li = (gm->msg[off] & 0xfc) >> 2;
		l->m = (gm->msg[off] & 0x02) >> 1;
		l->e = (gm->msg[off] & 0x01);
		l->used = 0;
	}

	/* End of TBF? */
	f->last = (!cv || fbi) ? 1 : 0;

	/* Optional fields for uplink, indicated in TI and PI */
	if (ul) {
		if (gm->msg[1] & 0x01) {
			printf("TLLI 0x%.02x%.02x%.02x%.02x ", gm->msg[off],
				gm->msg[off+1], gm->msg[off + 2], gm->msg[off + 3]);
			off += 4;
		}
		if (gm->msg[1] & 0x40) {
			printf("PFI %d ", gm->msg[off]);
			off += 1;
		}
	}

	/* Copy data part of message */
	f->len = gm->len - off;
	f->fn = gm->fn;
	memcpy(f->data, &gm->msg[off], f->len);

	process_blocks(t, ul);
}

int rlc_type_handler(struct gprs_message *gm)
{
	bool ul = !!(gm->arfcn & GSMTAP_ARFCN_F_UPLINK);
	uint8_t rlc_type = (gm->msg[0] & 0xc0) >> 6;
	int rc = 0;

	/* Determine the RLC type */
	switch (rlc_type) {
	case 0:
		printf("TS %d ", gm->tn);

		switch(gm->len) {
		case 23:
			printf("CS1 ");
			break;
		case 33:
			printf("CS2 ");
			break;
		case 39:
			printf("CS3 ");
			break;
		case 53:
			printf("CS4 ");
			break;
		default:
			printf("unknown (M)CS ");
		}

		printf(ul ? "UL " : "DL ");

		gsmtap_send_rlcmac(gm->msg, gm->len, gm->tn, ul);

		printf("DATA ");
		rlc_data_handler(gm);
		printf("\n");
		fflush(stdout);
		break;

	/* Control block */
	case 1:
	case 2:
		gsmtap_send_rlcmac(gm->msg, gm->len, gm->tn,
			!!(gm->arfcn & GSMTAP_ARFCN_F_UPLINK));
		rc = 0;
		break;

	/* Reserved */
	case 3:
		printf("RLC type: reserved\n");
		rc = 0;
		break;

	default:
		printf("Unrecognized RLC type: %d\n", rlc_type);
		return -EINVAL;
	}

	return rc;
}
