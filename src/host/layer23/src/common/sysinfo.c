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
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/bitvec.h>
#include <osmocom/gsm/gsm48.h>
#include <osmocom/gsm/gsm48_rest_octets.h>

#include <osmocom/gprs/rlcmac/csn1_defs.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/networks.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/sysinfo.h>

/*
 * dumping
 */

// FIXME: move to libosmocore
char *gsm_print_arfcn(uint16_t arfcn)
{
	static char text[10];

	sprintf(text, "%d", arfcn & 1023);
	if ((arfcn & ARFCN_PCS))
		strcat(text, "(PCS)");
	else if (arfcn >= 512 && arfcn <= 885)
		strcat(text, "(DCS)");

	return text;
}

/* Check if the cell 'talks' about DCS (false) or PCS (true) */
bool gsm_refer_pcs(uint16_t cell_arfcn, const struct gsm48_sysinfo *cell_s)
{
	/* If ARFCN is PCS band, the cell refers to PCS */
	if ((cell_arfcn & ARFCN_PCS))
		return true;

	/* If no SI1 is available, we assume DCS. Be sure to call this
	 * function only if SI 1 is available. */
	if (!cell_s->si1)
		return 0;

	/* If band indicator indicates PCS band, the cell refers to PCSThe  */
	return cell_s->band_ind;
}

/* Change the given ARFCN to PCS ARFCN, if it is in the PCS channel range and the cell refers to PCS band. */
uint16_t gsm_arfcn_refer_pcs(uint16_t cell_arfcn, const struct gsm48_sysinfo *cell_s, uint16_t arfcn)
{
	/* If ARFCN is not one of the overlapping channel of PCS and DCS. */
	if (arfcn < 512 && arfcn > 810)
		return arfcn;

	/* If the 'cell' does not refer to PCS. */
	if (!gsm_refer_pcs(cell_arfcn, cell_s))
		return arfcn;

	/* The ARFCN is PCS, because the ARFCN is in the PCS range and the cell refers to it. */
	return arfcn | ARFCN_PCS;
}

int gsm48_sysinfo_dump(const struct gsm48_sysinfo *s, uint16_t arfcn,
		       void (*print)(void *, const char *, ...),
		       void *priv, uint8_t *freq_map)
{
	char buffer[82];
	int i, j, k, index;
	int refer_pcs = gsm_refer_pcs(arfcn, s);
	int rc;

	/* available sysinfos */
	print(priv, "ARFCN = %s  channels 512+ refer to %s\n",
		gsm_print_arfcn(arfcn),
		(refer_pcs) ? "PCS (1900)" : "DCS (1800)");
	print(priv, "Available SYSTEM INFORMATION =");
	if (s->si1)
		print(priv, " 1");
	if (s->si2)
		print(priv, " 2");
	if (s->si2bis)
		print(priv, " 2bis");
	if (s->si2ter)
		print(priv, " 2ter");
	if (s->si3)
		print(priv, " 3");
	if (s->si4)
		print(priv, " 4");
	if (s->si5)
		print(priv, " 5");
	if (s->si5bis)
		print(priv, " 5bis");
	if (s->si5ter)
		print(priv, " 5ter");
	if (s->si6)
		print(priv, " 6");
	print(priv, "\n");
	print(priv, "\n");

	/* frequency list */
	j = 0; k = 0;
	for (i = 0; i < 1024; i++) {
		if ((s->freq[i].mask & FREQ_TYPE_SERV)) {
			if (!k) {
				sprintf(buffer, "serv. cell  : ");
				j = strlen(buffer);
			}
			if (j >= 75) {
				buffer[j - 1] = '\0';
				print(priv, "%s\n", buffer);
				sprintf(buffer, "              ");
				j = strlen(buffer);
			}
			sprintf(buffer + j, "%d,", i);
			j = strlen(buffer);
			k++;
		}
	}
	if (j) {
		buffer[j - 1] = '\0';
		print(priv, "%s\n", buffer);
	}
	j = 0; k = 0;
	for (i = 0; i < 1024; i++) {
		if ((s->freq[i].mask & FREQ_TYPE_NCELL)) {
			if (!k) {
				sprintf(buffer, "SI2 (neigh.) BA=%d: ",
					s->nb_ba_ind_si2);
				j = strlen(buffer);
			}
			if (j >= 70) {
				buffer[j - 1] = '\0';
				print(priv, "%s\n", buffer);
				sprintf(buffer, "                   ");
				j = strlen(buffer);
			}
			sprintf(buffer + j, "%d,", i);
			j = strlen(buffer);
			k++;
		}
	}
	if (j) {
		buffer[j - 1] = '\0';
		print(priv, "%s\n", buffer);
	}
	j = 0; k = 0;
	for (i = 0; i < 1024; i++) {
		if ((s->freq[i].mask & FREQ_TYPE_REP)) {
			if (!k) {
				sprintf(buffer, "SI5 (report) BA=%d: ",
					s->nb_ba_ind_si5);
				j = strlen(buffer);
			}
			if (j >= 70) {
				buffer[j - 1] = '\0';
				print(priv, "%s\n", buffer);
				sprintf(buffer, "                   ");
				j = strlen(buffer);
			}
			sprintf(buffer + j, "%d,", i);
			j = strlen(buffer);
			k++;
		}
	}
	if (j) {
		buffer[j - 1] = '\0';
		print(priv, "%s\n", buffer);
	}
	print(priv, "\n");

	/* frequency map */
	for (i = 0; i < 1024; i += 64) {
		snprintf(buffer, sizeof(buffer), " %3d ", i);
		for (j = 0; j < 64; j++) {
			index = i+j;
			if (refer_pcs && index >= 512 && index <= 885)
				index = index-512+1024;
			if ((s->freq[i+j].mask & FREQ_TYPE_SERV))
				buffer[j + 5] = 'S';
			else if ((s->freq[i+j].mask & FREQ_TYPE_NCELL)
			      && (s->freq[i+j].mask & FREQ_TYPE_REP))
				buffer[j + 5] = 'b';
			else if ((s->freq[i+j].mask & FREQ_TYPE_NCELL))
				buffer[j + 5] = 'n';
			else if ((s->freq[i+j].mask & FREQ_TYPE_REP))
				buffer[j + 5] = 'r';
			else if (!freq_map || (freq_map[index >> 3]
						& (1 << (index & 7))))
				buffer[j + 5] = '.';
			else
				buffer[j + 5] = ' ';
		}
		for (; j < 64; j++)
			buffer[j + 5] = ' ';
		snprintf(buffer + 69, sizeof(buffer) - 69, " %d", i + 63);
		print(priv, "%s\n", buffer);
	}
	print(priv, " 'S' = serv. cell  'n' = SI2 (neigh.)  'r' = SI5 (rep.)  "
		"'b' = SI2+SI5\n\n");

	/* serving cell */
	print(priv, "Serving Cell:\n");
	print(priv, " BSIC = %d,%d  LAI = %s Cell ID = 0x%04x\n",
		s->bsic >> 3, s->bsic & 0x7, osmo_lai_name(&s->lai), s->cell_id);
	print(priv, " Country = %s  Network Name = %s\n",
		gsm_get_mcc(s->lai.plmn.mcc), gsm_get_mnc(&s->lai.plmn));
	print(priv, " MAX_RETRANS = %d  TX_INTEGER = %d  re-establish = %s\n",
		s->max_retrans, s->tx_integer,
		(s->reest_denied) ? "denied" : "allowed");
	print(priv, " Cell barred = %s  barred classes =",
		(s->cell_barr ? "yes" : "no"));
	for (i = 0; i < 16; i++) {
		if ((s->class_barr & (1 << i)))
			print(priv, " C%d", i);
	}
	print(priv, "\n");
	if (s->sp)
		print(priv, " CBQ = %d  CRO = %d  TEMP_OFFSET = %d  "
			"PENALTY_TIME = %d\n", s->sp_cbq, s->sp_cro, s->sp_to,
			s->sp_pt);
	if (s->nb_ncc_permitted_si2) {
		print(priv, "NCC Permitted BCCH =");
		for (i = 0; i < 8; i++)
			if ((s->nb_ncc_permitted_si2 & (1 << i)))
				print(priv, " %d", i);
		print(priv, "\n");
	}
	if (s->nb_ncc_permitted_si6) {
		print(priv, "NCC Permitted SACCH/TCH =");
		for (i = 0; i < 8; i++)
			if ((s->nb_ncc_permitted_si6 & (1 << i)))
				print(priv, " %d", i);
		print(priv, "\n");
	}
	print(priv, "\n");

	/* neighbor cell */
	print(priv, "Neighbor Cell:\n");
	print(priv, " MAX_RETRANS = %d  TX_INTEGER = %d  re-establish = %s\n",
		s->nb_max_retrans, s->nb_tx_integer,
		(s->nb_reest_denied) ? "denied" : "allowed");
	print(priv, " Cell barred = %s  barred classes =",
		(s->nb_cell_barr ? "yes" : "no"));
	for (i = 0; i < 16; i++) {
		if ((s->nb_class_barr & (1 << i)))
			print(priv, " C%d", i);
	}
	print(priv, "\n");
	print(priv, "\n");

	/* cell selection */
	print(priv, "MS_TXPWR_MAX_CCCH = %d  CRH = %d  RXLEV_MIN = %d  "
		"NECI = %d  ACS = %d\n", s->ms_txpwr_max_cch,
		s->cell_resel_hyst_db, s->rxlev_acc_min_db, s->neci, s->acs);

	/* bcch options */
	print(priv, "BCCH link timeout = %d  DTX = %d  PWRC = %d\n",
		s->bcch_radio_link_timeout, s->bcch_dtx, s->bcch_pwrc);

	/* sacch options */
	print(priv, "SACCH link timeout = %d  DTX = %d  PWRC = %d\n",
		s->sacch_radio_link_timeout, s->sacch_dtx, s->sacch_pwrc);

	/* control channel */
	switch(s->ccch_conf) {
	case 0:
	case 2:
	case 4:
	case 6:
		print(priv, "CCCH Config = %d CCCH", (s->ccch_conf >> 1) + 1);
		break;
	case 1:
		print(priv, "CCCH Config = 1 CCCH + SDCCH");
		break;
	default:
		print(priv, "CCCH Config = reserved");
	}
	print(priv, "  BS-PA-MFMS = %d  Attachment = %s\n",
		s->pag_mf_periods, (s->att_allowed) ? "allowed" : "denied");
	print(priv, "BS-AG_BLKS_RES = %d  ", s->bs_ag_blks_res);
	if (!s->nch)
		print(priv, "NCH not available  ");
	else {
		uint8_t num_blocks, first_block;
		rc = osmo_gsm48_si1ro_nch_pos_decode(s->nch_position, &num_blocks, &first_block);
		if (rc < 0)
			print(priv, "NCH Position invalid  ");
		else
			print(priv, "NCH Position %u / %u blocks  ", first_block, num_blocks);
	}
	if (s->t3212)
		print(priv, "T3212 = %d sec.\n", s->t3212);
	else
		print(priv, "T3212 = disabled\n", s->t3212);

	/* channel description */
	if (s->h)
		print(priv, "chan_nr = 0x%02x TSC = %d  MAIO = %d  HSN = %d\n",
			s->chan_nr, s->tsc, s->maio, s->hsn);
	else
		print(priv, "chan_nr = 0x%02x TSC = %d  ARFCN = %d\n",
			s->chan_nr, s->tsc, s->arfcn);
	print(priv, "\n");

	return 0;
}

int gsm48_si10_dump(const struct gsm48_sysinfo *s, void (*print)(void *, const char *, ...), void *priv)
{
	const struct si10_cell_info *c;
	int i;

	if (!s || !s->si10) {
		print(priv, "No group channel neighbor information available.\n");
		return 0;
	}

	if (!s->si10_cell_num) {
		print(priv, "No group channel neighbors exist.\n");
		return 0;
	}

	/* Group call neighbor cells. */
	print(priv, "Group channel neighbor cells (current or last call):\n");
	for (i = 0; i < s->si10_cell_num; i++) {
		c = &s->si10_cell[i];
		print(priv, " index = %d", c->index);
		if (c->arfcn >= 0)
			print(priv, " ARFCN = %d", c->arfcn);
		else
			print(priv, " ARFCN = not in SI5*");
		print(priv, "  BSIC = %d,%d", c->bsic >> 3, c->bsic & 0x7);
		if (c->barred) {
			print(priv, "  barred");
			continue;
		}
		if (c->la_different)
			print(priv, "  CRH = %d", c->cell_resel_hyst_db);
		print(priv, "  MS_TXPWR_MAX_CCCH = %d\n", c->ms_txpwr_max_cch);
		print(priv, "  RXLEV_MIN = %d", c->rxlev_acc_min_db);
		print(priv, "  CRO = %d", c->cell_resel_offset);
		print(priv, "  TEMP_OFFSET = %d", c->temp_offset);
		print(priv, "  PENALTY_TIME = %d", c->penalty_time);
	}
	print(priv, "\n");

	return 0;
}

/*
 * decoding
 */

int gsm48_decode_chan_h0(const struct gsm48_chan_desc *cd,
			 uint8_t *tsc, uint16_t *arfcn)
{
	*tsc = cd->h0.tsc;
	*arfcn = cd->h0.arfcn_low | (cd->h0.arfcn_high << 8);

	return 0;
}

int gsm48_decode_chan_h1(const struct gsm48_chan_desc *cd,
			 uint8_t *tsc, uint8_t *maio, uint8_t *hsn)
{
	*tsc = cd->h1.tsc;
	*maio = cd->h1.maio_low | (cd->h1.maio_high << 2);
	*hsn = cd->h1.hsn;

	return 0;
}

/* decode "Cell Channel Description" (10.5.2.1b) and other frequency lists */
static int decode_freq_list(struct gsm_sysinfo_freq *f,
			    const uint8_t *cd, uint8_t len,
			    uint8_t mask, uint8_t frqt)
{
#if 0
	/* only Bit map 0 format for P-GSM */
	if ((cd[0] & 0xc0 & mask) != 0x00 &&
	    (set->p_gsm && !set->e_gsm && !set->r_gsm && !set->dcs))
		return 0;
#endif

	return gsm48_decode_freq_list(f, cd, len, mask, frqt);
}

/* decode "Cell Selection Parameters" (10.5.2.4) */
static int gsm48_decode_cell_sel_param(struct gsm48_sysinfo *s,
				       const struct gsm48_cell_sel_par *cs)
{
	s->ms_txpwr_max_cch = cs->ms_txpwr_max_ccch;
	s->cell_resel_hyst_db = cs->cell_resel_hyst * 2;
	s->rxlev_acc_min_db = cs->rxlev_acc_min - 110;
	s->neci = cs->neci;
	s->acs = cs->acs;

	return 0;
}

/* decode "Cell Options (BCCH)" (10.5.2.3) */
static int gsm48_decode_cellopt_bcch(struct gsm48_sysinfo *s,
				     const struct gsm48_cell_options *co)
{
	s->bcch_radio_link_timeout = (co->radio_link_timeout + 1) * 4;
	s->bcch_dtx = co->dtx;
	s->bcch_pwrc = co->pwrc;

	return 0;
}

/* decode "Cell Options (SACCH)" (10.5.2.3a) */
static int gsm48_decode_cellopt_sacch(struct gsm48_sysinfo *s,
				      const struct gsm48_cell_options *co)
{
	s->sacch_radio_link_timeout = (co->radio_link_timeout + 1) * 4;
	s->sacch_dtx = co->dtx;
	s->sacch_pwrc = co->pwrc;

	return 0;
}

/* decode "Control Channel Description" (10.5.2.11) */
static int gsm48_decode_ccd(struct gsm48_sysinfo *s,
			    const struct gsm48_control_channel_descr *cc)
{
	s->ccch_conf = cc->ccch_conf;
	s->bs_ag_blks_res = cc->bs_ag_blks_res;
	s->att_allowed = cc->att;
	s->pag_mf_periods = cc->bs_pa_mfrms + 2;
	s->t3212 = cc->t3212 * 360; /* convert deci-hours to seconds */

	return 0;
}

/* decode "Mobile Allocation" (10.5.2.21) */
int gsm48_decode_mobile_alloc(struct gsm_sysinfo_freq *freq,
			      const uint8_t *ma, uint8_t len,
			      uint16_t *hopping, uint8_t *hopp_len, int si4)
{
	int i, j = 0;
	uint16_t f[len << 3];

	/* not more than 64 hopping indexes allowed in IE */
	if (len > 8)
		return -EINVAL;

	/* tabula rasa */
	*hopp_len = 0;
	if (si4) {
		for (i = 0; i < 1024; i++)
			freq[i].mask &= ~FREQ_TYPE_HOPP;
	}

	/* generating list of all frequencies (1..1023,0) */
	for (i = 1; i <= 1024; i++) {
		if ((freq[i & 1023].mask & FREQ_TYPE_SERV)) {
			LOGP(DRR, LOGL_INFO, "Serving cell ARFCN #%d: %d\n",
				j, i & 1023);
			f[j++] = i & 1023;
			if (j == (len << 3))
				break;
		}
	}

	/* fill hopping table with frequency index given by IE
	 * and set hopping type bits
	 */
	for (i = 0; i < (len << 3); i++) {
		/* if bit is set, this frequency index is used for hopping */
		if ((ma[len - 1 - (i >> 3)] & (1 << (i & 7)))) {
			LOGP(DRR, LOGL_INFO, "Hopping ARFCN: %d (bit %d)\n",
				i, f[i]);
			/* index higher than entries in list ? */
			if (i >= j) {
				LOGP(DRR, LOGL_NOTICE, "Mobile Allocation "
					"hopping index %d exceeds maximum "
					"number of cell frequencies. (%d)\n",
					i + 1, j);
				break;
			}
			hopping[(*hopp_len)++] = f[i];
			if (si4)
				freq[f[i]].mask |= FREQ_TYPE_HOPP;
		}
	}

	return 0;
}

/* Rach Control decode tables */
static const uint8_t gsm48_max_retrans[4] = {
	1, 2, 4, 7
};
static const uint8_t gsm48_tx_integer[16] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 16, 20, 25, 32, 50
};

/* decode "RACH Control Parameter" (10.5.2.29) */
static int gsm48_decode_rach_ctl_param(struct gsm48_sysinfo *s,
				       const struct gsm48_rach_control *rc)
{
	s->reest_denied = rc->re;
	s->cell_barr = rc->cell_bar;
	s->tx_integer = gsm48_tx_integer[rc->tx_integer];
	s->max_retrans = gsm48_max_retrans[rc->max_trans];
	s->class_barr = (rc->t2 << 8) | rc->t3;

	return 0;
}
static int gsm48_decode_rach_ctl_neigh(struct gsm48_sysinfo *s,
				       const struct gsm48_rach_control *rc)
{
	s->nb_reest_denied = rc->re;
	s->nb_cell_barr = rc->cell_bar;
	s->nb_tx_integer = gsm48_tx_integer[rc->tx_integer];
	s->nb_max_retrans = gsm48_max_retrans[rc->max_trans];
	s->nb_class_barr = (rc->t2 << 8) | rc->t3;

	return 0;
}

/* decode "SI 1 Rest Octets" (10.5.2.32) */
static int gsm48_decode_si1_rest(struct gsm48_sysinfo *s,
				 const uint8_t *si, uint8_t len)
{
	struct bitvec bv = {
		.data_len = len,
		.data = (uint8_t *)si,
	};

	/* Optional Selection Parameters */
	if (bitvec_get_bit_high(&bv) == H) {
		s->nch = 1;
		s->nch_position = bitvec_get_uint(&bv, 5);
	} else
		s->nch = 0;
	s->band_ind = (bitvec_get_bit_high(&bv) == H);

	return 0;
}

/* decode "SI 3 Rest Octets" (10.5.2.34) */
static int gsm48_decode_si3_rest(struct gsm48_sysinfo *s,
				 const uint8_t *si, uint8_t len)
{
	struct bitvec bv = {
		.data_len = len,
		.data = (uint8_t *)si,
	};

	/* Optional Selection Parameters */
	if (bitvec_get_bit_high(&bv) == H) {
		s->sp = 1;
		s->sp_cbq = bitvec_get_uint(&bv, 1);
		s->sp_cro = bitvec_get_uint(&bv, 6);
		s->sp_to = bitvec_get_uint(&bv, 3);
		s->sp_pt = bitvec_get_uint(&bv, 5);
	} else
		s->sp = 0;
	/* Optional Power Offset */
	if (bitvec_get_bit_high(&bv) == H) {
		s->po = 1;
		s->po_value = bitvec_get_uint(&bv, 2);
	} else
		s->po = 0;
	/* System Onformation 2ter Indicator */
	if (bitvec_get_bit_high(&bv) == H)
		s->si2ter_ind = 1;
	else
		s->si2ter_ind = 0;
	/* Early Classark Sending Control */
	if (bitvec_get_bit_high(&bv) == H)
		s->ecsm = 1;
	else
		s->ecsm = 0;
	/* Scheduling if and where */
	if (bitvec_get_bit_high(&bv) == H) {
		s->sched = 1;
		s->sched_where = bitvec_get_uint(&bv, 3);
	} else
		s->sched = 0;
	/* GPRS Indicator */
	if (bitvec_get_bit_high(&bv) == H) {
		s->gprs.supported = 1;
		s->gprs.ra_colour = bitvec_get_uint(&bv, 3);
		s->gprs.si13_pos = bitvec_get_uint(&bv, 1);
	} else
		s->gprs.supported = 0;

	return 0;
}

/* decode "SI 4 Rest Octets" (10.5.2.35) */
static int gsm48_decode_si4_rest(struct gsm48_sysinfo *s,
				 const uint8_t *si, uint8_t len)
{
	struct bitvec bv = {
		.data_len = len,
		.data = (uint8_t *)si,
	};

	/* Optional Selection Parameters */
	if (bitvec_get_bit_high(&bv) == H) {
		s->sp = 1;
		s->sp_cbq = bitvec_get_uint(&bv, 1);
		s->sp_cro = bitvec_get_uint(&bv, 6);
		s->sp_to = bitvec_get_uint(&bv, 3);
		s->sp_pt = bitvec_get_uint(&bv, 5);
	} else
		s->sp = 0;
	/* Optional Power Offset */
	if (bitvec_get_bit_high(&bv) == H) {
		s->po = 1;
		s->po_value = bitvec_get_uint(&bv, 3);
	} else
		s->po = 0;
	/* GPRS Indicator */
	if (bitvec_get_bit_high(&bv) == H) {
		s->gprs.supported = 1;
		s->gprs.ra_colour = bitvec_get_uint(&bv, 3);
		s->gprs.si13_pos = bitvec_get_uint(&bv, 1);
	} else
		s->gprs.supported = 0;
	// todo: more rest octet bits

	return 0;
}

/* TODO: decode "SI 6 Rest Octets" (10.5.2.35a) */
static int gsm48_decode_si6_rest(struct gsm48_sysinfo *s,
				 const uint8_t *si, uint8_t len)
{
	return 0;
}

/* Decode "SI 10 Rest Octets" (10.5.2.44) */
static int gsm48_decode_si10_rest_first(struct gsm48_sysinfo *s, struct bitvec *bv,
					struct si10_cell_info *c)
{
	uint8_t ba_ind;

	/* <BA ind : bit(1)> */
	ba_ind = bitvec_get_uint(bv, 1);
	if (ba_ind != s->nb_ba_ind_si5) {
		LOGP(DRR, LOGL_NOTICE, "SI10: BA_IND %u != BA_IND %u of SI5!\n", ba_ind, s->nb_ba_ind_si5);
		return EOF;
	}

	/* { L <spare padding> | H <neighbour information> } */
	if (bitvec_get_bit_high(bv) != H) {
		LOGP(DRR, LOGL_INFO, "SI10: No neighbor cell defined.\n");
		return EOF;
	}

	/* <first frequency: bit(5)> */
	c->index = bitvec_get_uint(bv, 5);

	/* <bsic : bit(6)> */
	c->bsic = bitvec_get_uint(bv, 6);

	/* { H <cell parameters> | L } */
	if (bitvec_get_bit_high(bv) != H) {
		LOGP(DRR, LOGL_NOTICE, "SI10: No cell parameters for first cell, cannot continue to decode!\n");
		return EOF;
	}

	/* <cell barred (H)> | L <further cell info> */
	if (bitvec_get_bit_high(bv) == H) {
		c->barred = true;
		return 0;
	}

	/* { H <cell reselect hysteresis : bit(3)> | L } */
	if (bitvec_get_bit_high(bv) == H) {
		c->la_different = true;
		c->cell_resel_hyst_db = bitvec_get_uint(bv, 3) * 2;
	}

	/* <ms txpwr max cch : bit(5)> */
	c->ms_txpwr_max_cch = bitvec_get_uint(bv, 5);
	/* <rxlev access min : bit(6)> */
	c->rxlev_acc_min_db = rxlev2dbm(bitvec_get_uint(bv, 6));
	/* <cell reselect offset : bit(6)> */
	c->cell_resel_offset = bitvec_get_uint(bv, 6);
	/* <temporary offset : bit(3)> */
	c->temp_offset = bitvec_get_uint(bv, 3);
	/* <penalty time : bit(5)> */
	c->penalty_time = bitvec_get_uint(bv, 5);

	return 0;
}

static int gsm48_decode_si10_rest_other(struct gsm48_sysinfo *s, struct bitvec *bv,
					struct si10_cell_info *c)
{
	int rc;

	/* { H <info field> }** L <spare padding> */
	if (bitvec_get_bit_high(bv) != H)
		return EOF;

	c->index = (c->index + 1) & 0x1f;
	/* <next frequency (H)>** L <differential cell info> */
	/* Increment frequency number for every <info field> and every <next frequency> occurrence. */
	while ((rc = bitvec_get_bit_high(bv)) == H)
		c->index = (c->index + 1) & 0x1f;
	if (rc < 0)
		goto short_read;

	/* { H <BCC : bit(3)> | L <bsic : bit(6)> } */
	rc = bitvec_get_bit_high(bv);
	if (rc < 0)
		goto short_read;
	if (rc == H) {
		rc = bitvec_get_uint(bv, 3);
		if (rc < 0)
			goto short_read;
		c->bsic = (c->bsic & 0x07) | rc;
	} else {
		rc = bitvec_get_uint(bv, 6);
		if (rc < 0)
			goto short_read;
		c->bsic = rc;
	}

	/* { H <diff cell pars> | L } */
	rc = bitvec_get_bit_high(bv);
	if (rc < 0)
		goto short_read;
	if (rc != H)
		return 0;

	/* <cell barred (H)> | L <further cell info> */
	rc = bitvec_get_bit_high(bv);
	if (rc < 0)
		goto short_read;
	if (rc == H) {
		c->barred = true;
		return 0;
	}

	/* { H <cell reselect hysteresis : bit(3)> | L } */
	rc = bitvec_get_bit_high(bv);
	if (rc < 0)
		goto short_read;
	if (rc == H) {
		c->la_different = true;
		rc = bitvec_get_uint(bv, 3);
		if (rc < 0)
			goto short_read;
		c->cell_resel_hyst_db = bitvec_get_uint(bv, 3) * 2;
	}

	/* { H <ms txpwr max cch : bit(5)> | L } */
	rc = bitvec_get_bit_high(bv);
	if (rc < 0)
		goto short_read;
	if (rc == H) {
		rc = bitvec_get_uint(bv, 5);
		if (rc < 0)
			goto short_read;
		c->ms_txpwr_max_cch = rc;
	}

	/* { H <rxlev access min : bit(6)> | L } */
	rc = bitvec_get_bit_high(bv);
	if (rc < 0)
		goto short_read;
	if (rc == H) {
		rc = bitvec_get_uint(bv, 6);
		if (rc < 0)
			goto short_read;
		c->rxlev_acc_min_db = rxlev2dbm(rc);
	} else
		c->rxlev_acc_min_db = -110;

	/* { H <cell reselect offset : bit(6)> | L } */
	rc = bitvec_get_bit_high(bv);
	if (rc < 0)
		goto short_read;
	if (rc == H) {
		rc = bitvec_get_uint(bv, 6);
		if (rc < 0)
			goto short_read;
		c->cell_resel_offset = rc;
	}

	/* { H <temporary offset : bit(3)> | L } */
	rc = bitvec_get_bit_high(bv);
	if (rc < 0)
		goto short_read;
	if (rc == H) {
		rc = bitvec_get_uint(bv, 3);
		if (rc < 0)
			goto short_read;
		c->temp_offset = rc;
	}

	/* { H <penalty time : bit(5)> | L } */
	rc = bitvec_get_bit_high(bv);
	if (rc < 0)
		goto short_read;
	if (rc == H) {
		rc = bitvec_get_uint(bv, 5);
		if (rc < 0)
			goto short_read;
		c->penalty_time = rc;
	}

	return 0;

short_read:
	LOGP(DRR, LOGL_NOTICE, "SI10: Short read of differential cell info.\n");
	return -EINVAL;
}

int gsm48_decode_sysinfo1(struct gsm48_sysinfo *s,
			  const struct gsm48_system_information_type_1 *si, int len)
{
	int payload_len = len - sizeof(*si);

	memcpy(s->si1_msg, si, OSMO_MIN(len, sizeof(s->si1_msg)));

	/* Cell Channel Description */
	decode_freq_list(s->freq, si->cell_channel_description,
			 sizeof(si->cell_channel_description),
			 0xce, FREQ_TYPE_SERV);
	/* RACH Control Parameter */
	gsm48_decode_rach_ctl_param(s, &si->rach_control);
	/* SI 1 Rest Octets */
	if (payload_len)
		gsm48_decode_si1_rest(s, si->rest_octets, payload_len);

	s->si1 = 1;

	if (s->si4) {
		const struct gsm48_system_information_type_4 *si4 = (void *)s->si4_msg;
		LOGP(DRR, LOGL_NOTICE,
		     "Now updating previously received SYSTEM INFORMATION 4\n");
		gsm48_decode_sysinfo4(s, si4, sizeof(s->si4_msg));
	}

	return 0;
}

int gsm48_decode_sysinfo2(struct gsm48_sysinfo *s,
			  const struct gsm48_system_information_type_2 *si, int len)
{
	memcpy(s->si2_msg, si, OSMO_MIN(len, sizeof(s->si2_msg)));

	/* Neighbor Cell Description */
	s->nb_ext_ind_si2 = (si->bcch_frequency_list[0] >> 5) & 1;
	s->nb_ba_ind_si2 = (si->bcch_frequency_list[0] >> 4) & 1;
	decode_freq_list(s->freq, si->bcch_frequency_list,
			 sizeof(si->bcch_frequency_list),
			 0xce, FREQ_TYPE_NCELL_2);
	/* NCC Permitted */
	s->nb_ncc_permitted_si2 = si->ncc_permitted;
	/* RACH Control Parameter */
	gsm48_decode_rach_ctl_neigh(s, &si->rach_control);

	s->si2 = 1;

	return 0;
}

int gsm48_decode_sysinfo2bis(struct gsm48_sysinfo *s,
			     const struct gsm48_system_information_type_2bis *si, int len)
{
	memcpy(s->si2b_msg, si, OSMO_MIN(len, sizeof(s->si2b_msg)));

	/* Neighbor Cell Description */
	s->nb_ext_ind_si2bis = (si->bcch_frequency_list[0] >> 5) & 1;
	s->nb_ba_ind_si2bis = (si->bcch_frequency_list[0] >> 4) & 1;
	decode_freq_list(s->freq, si->bcch_frequency_list,
		sizeof(si->bcch_frequency_list), 0xce, FREQ_TYPE_NCELL_2bis);
	/* RACH Control Parameter */
	gsm48_decode_rach_ctl_neigh(s, &si->rach_control);

	s->si2bis = 1;

	return 0;
}

int gsm48_decode_sysinfo2ter(struct gsm48_sysinfo *s,
			     const struct gsm48_system_information_type_2ter *si, int len)
{
	memcpy(s->si2t_msg, si, OSMO_MIN(len, sizeof(s->si2t_msg)));

	/* Neighbor Cell Description 2 */
	s->nb_multi_rep_si2ter = (si->ext_bcch_frequency_list[0] >> 5) & 3;
	s->nb_ba_ind_si2ter = (si->ext_bcch_frequency_list[0] >> 4) & 1;
	decode_freq_list(s->freq, si->ext_bcch_frequency_list,
		sizeof(si->ext_bcch_frequency_list), 0x8e,
			FREQ_TYPE_NCELL_2ter);

	s->si2ter = 1;

	return 0;
}

int gsm48_decode_sysinfo3(struct gsm48_sysinfo *s,
			  const struct gsm48_system_information_type_3 *si, int len)
{
	int payload_len = len - sizeof(*si);

	memcpy(s->si3_msg, si, OSMO_MIN(len, sizeof(s->si3_msg)));

	/* Cell Identity */
	s->cell_id = ntohs(si->cell_identity);
	/* LAI */
	gsm48_decode_lai2(&si->lai, &s->lai);
	/* Control Channel Description */
	gsm48_decode_ccd(s, &si->control_channel_desc);
	/* Cell Options (BCCH) */
	gsm48_decode_cellopt_bcch(s, &si->cell_options);
	/* Cell Selection Parameters */
	gsm48_decode_cell_sel_param(s, &si->cell_sel_par);
	/* RACH Control Parameter */
	gsm48_decode_rach_ctl_param(s, &si->rach_control);
	/* SI 3 Rest Octets */
	if (payload_len >= 4)
		gsm48_decode_si3_rest(s, si->rest_octets, payload_len);

	LOGP(DRR, LOGL_INFO,
	     "New SYSTEM INFORMATION 3 (lai=%s)\n", osmo_lai_name(&s->lai));

	s->si3 = 1;

	return 0;
}

int gsm48_decode_sysinfo4(struct gsm48_sysinfo *s,
			  const struct gsm48_system_information_type_4 *si, int len)
{
	int payload_len = len - sizeof(*si);

	const uint8_t *data = si->data;
	const struct gsm48_chan_desc *cd;

	memcpy(s->si4_msg, si, OSMO_MIN(len, sizeof(s->si4_msg)));

	/* LAI */
	gsm48_decode_lai2(&si->lai, &s->lai);
	/* Cell Selection Parameters */
	gsm48_decode_cell_sel_param(s, &si->cell_sel_par);
	/* RACH Control Parameter */
	gsm48_decode_rach_ctl_param(s, &si->rach_control);

	/* CBCH Channel Description */
	if (payload_len >= 1 && data[0] == GSM48_IE_CBCH_CHAN_DESC) {
		if (payload_len < 4) {
short_read:
			LOGP(DRR, LOGL_NOTICE, "Short read!\n");
			return -EIO;
		}
		cd = (const struct gsm48_chan_desc *)(data + 1);
		s->chan_nr = cd->chan_nr;
		s->h = cd->h0.h;
		if (s->h)
			gsm48_decode_chan_h1(cd, &s->tsc, &s->maio, &s->hsn);
		else
			gsm48_decode_chan_h0(cd, &s->tsc, &s->arfcn);
		payload_len -= 4;
		data += 4;
	}
	/* CBCH Mobile Allocation */
	if (payload_len >= 1 && data[0] == GSM48_IE_CBCH_MOB_AL) {
		if (payload_len < 1 || payload_len < 2 + data[1])
			goto short_read;
		if (!s->si1) {
			LOGP(DRR, LOGL_NOTICE, "Ignoring CBCH allocation of "
			     "SYSTEM INFORMATION 4 until SI 1 is received.\n");
		} else {
			gsm48_decode_mobile_alloc(s->freq, data + 2, data[1],
						  s->hopping, &s->hopp_len, 1);
		}
		payload_len -= 2 + data[1];
		data += 2 + data[1];
	}
	/* SI 4 Rest Octets */
	if (payload_len > 0)
		gsm48_decode_si4_rest(s, data, payload_len);

	s->si4 = 1;

	return 0;
}

int gsm48_decode_sysinfo5(struct gsm48_sysinfo *s,
			  const struct gsm48_system_information_type_5 *si, int len)
{
	memcpy(s->si5_msg, si, OSMO_MIN(len, sizeof(s->si5_msg)));

	/* Neighbor Cell Description */
	s->nb_ext_ind_si5 = (si->bcch_frequency_list[0] >> 5) & 1;
	s->nb_ba_ind_si5 = (si->bcch_frequency_list[0] >> 4) & 1;
	decode_freq_list(s->freq, si->bcch_frequency_list,
			 sizeof(si->bcch_frequency_list),
			 0xce, FREQ_TYPE_REP_5);

	s->si5 = 1;
	s->si10 = false;

	return 0;
}

int gsm48_decode_sysinfo5bis(struct gsm48_sysinfo *s,
			     const struct gsm48_system_information_type_5bis *si, int len)
{
	memcpy(s->si5b_msg, si, OSMO_MIN(len, sizeof(s->si5b_msg)));

	/* Neighbor Cell Description */
	s->nb_ext_ind_si5bis = (si->bcch_frequency_list[0] >> 5) & 1;
	s->nb_ba_ind_si5bis = (si->bcch_frequency_list[0] >> 4) & 1;
	decode_freq_list(s->freq, si->bcch_frequency_list,
			 sizeof(si->bcch_frequency_list),
			 0xce, FREQ_TYPE_REP_5bis);

	s->si5bis = 1;
	s->si10 = false;

	return 0;
}

int gsm48_decode_sysinfo5ter(struct gsm48_sysinfo *s,
			     const struct gsm48_system_information_type_5ter *si, int len)
{
	memcpy(s->si5t_msg, si, OSMO_MIN(len, sizeof(s->si5t_msg)));

	/* Neighbor Cell Description */
	s->nb_multi_rep_si5ter = (si->bcch_frequency_list[0] >> 5) & 3;
	s->nb_ba_ind_si5ter = (si->bcch_frequency_list[0] >> 4) & 1;
	decode_freq_list(s->freq, si->bcch_frequency_list,
			 sizeof(si->bcch_frequency_list),
			 0x8e, FREQ_TYPE_REP_5ter);

	s->si5ter = 1;
	s->si10 = false;

	return 0;
}

int gsm48_decode_sysinfo6(struct gsm48_sysinfo *s,
			  const struct gsm48_system_information_type_6 *si, int len)
{
	int payload_len = len - sizeof(*si);

	memcpy(s->si6_msg, si, OSMO_MIN(len, sizeof(s->si6_msg)));

	/* Cell Identity */
	if (s->si6 && s->cell_id != ntohs(si->cell_identity))
		LOGP(DRR, LOGL_INFO, "Cell ID on SI 6 differs from previous "
			"read.\n");
	s->cell_id = ntohs(si->cell_identity);
	/* LAI */
	gsm48_decode_lai2(&si->lai, &s->lai);
	/* Cell Options (SACCH) */
	gsm48_decode_cellopt_sacch(s, &si->cell_options);
	/* NCC Permitted */
	s->nb_ncc_permitted_si6 = si->ncc_permitted;
	/* SI 6 Rest Octets */
	if (payload_len >= 4)
		gsm48_decode_si6_rest(s, si->rest_octets, payload_len);

	s->si6 = 1;

	return 0;
}

/* Get ARFCN from BCCH allocation found in SI5/SI5bis an SI5ter. See TS 44.018 §10.5.2.20. */
int16_t arfcn_from_freq_index(const struct gsm48_sysinfo *s, uint16_t index)
{
	uint16_t arfcn, i = 0;

	/* Search for ARFCN found in SI5 or SI5bis. (first sub list) */
	for (arfcn = 1; arfcn <= 1024; arfcn++) {
		if (!(s->freq[arfcn & 1023].mask & (FREQ_TYPE_REP_5 | FREQ_TYPE_REP_5bis)))
			continue;
		if (index == i++)
			return arfcn & 1023;
	}

	/* Search for ARFCN found in SI5ter. (second sub list) */
	for (arfcn = 1; arfcn <= 1024; arfcn++) {
		if (!(s->freq[arfcn & 1023].mask & FREQ_TYPE_REP_5ter))
			continue;
		if (index == i++)
			return arfcn & 1023;
	}

	/* If not found, return EOF (-1) as idicator. */
	return EOF;
}

int gsm48_decode_sysinfo10(struct gsm48_sysinfo *s,
			   const struct gsm48_system_information_type_10 *si, int len)
{
	int payload_len = len - sizeof(*si);
	struct bitvec bv;
	int i;
	int rc;

	bv = (struct bitvec) {
		.data_len = payload_len,
		.data = (uint8_t *)si->rest_octets,
	};

	memcpy(s->si10_msg, si, OSMO_MIN(len, sizeof(s->si10_msg)));

	/* Clear cell list. */
	s->si10_cell_num = 0;
	memset(s->si10_cell, 0, sizeof(s->si10_cell));

	/* SI 10 Rest Octets of first neighbor cell, if included. */
	rc = gsm48_decode_si10_rest_first(s, &bv, &s->si10_cell[0]);
	if (rc == EOF) {
		s->si10 = true;
		return 0;
	}
	if (rc < 0)
		return rc;
	s->si10_cell[0].arfcn = arfcn_from_freq_index(s, s->si10_cell[0].index);
	s->si10_cell_num++;

	for (i = 1; i < ARRAY_SIZE(s->si10_cell); i++) {
		/* Clone last cell info and then store differential elements. */
		memcpy(&s->si10_cell[i], &s->si10_cell[i - 1], sizeof(s->si10_cell[i]));
		/* SI 10 Rest Octets of other neighbor cell, if included. */
		rc = gsm48_decode_si10_rest_other(s, &bv, &s->si10_cell[i]);
		if (rc == EOF)
			break;
		if (rc < 0)
			return rc;
		s->si10_cell[i].arfcn = arfcn_from_freq_index(s, s->si10_cell[i].index);
		s->si10_cell_num++;
	}

	s->si10 = true;
	return 0;
}

int gsm48_decode_sysinfo13(struct gsm48_sysinfo *s,
			   const struct gsm48_system_information_type_13 *si, int len)
{
	SI13_RestOctets_t si13ro;
	int rc;

	memcpy(s->si13_msg, si, OSMO_MIN(len, sizeof(s->si13_msg)));

	rc = osmo_gprs_rlcmac_decode_si13ro(&si13ro, si->rest_octets, sizeof(s->si13_msg));
	if (rc != 0) {
		LOGP(DRR, LOGL_ERROR, "Failed to parse SI13 Rest Octets\n");
		return rc;
	}

	if (si13ro.UnionType != 0) {
		LOGP(DRR, LOGL_NOTICE, "PBCCH is deprecated and not supported\n");
		return -ENOTSUP;
	}

	s->gprs.hopping = si13ro.Exist_MA;
	if (s->gprs.hopping) {
		const GPRS_Mobile_Allocation_t *gma = &si13ro.GPRS_Mobile_Allocation;

		s->gprs.hsn = gma->HSN;
		s->gprs.rfl_num_len = gma->ElementsOf_RFL_NUMBER;
		memcpy(&s->gprs.rfl_num[0], &gma->RFL_NUMBER[0], sizeof(gma->RFL_NUMBER));

		if (gma->UnionType == 0) { /* MA Bitmap */
			const MobileAllocation_t *ma = &gma->u.MA;
			s->gprs.ma_bitlen = ma->MA_BitLength;
			memcpy(&s->gprs.ma_bitmap[0], &ma->MA_BITMAP[0], sizeof(ma->MA_BITMAP));
		} else { /* ARFCN Index List */
			const ARFCN_index_list_t *ai = &gma->u.ARFCN_index_list;
			s->gprs.arfcn_idx_len = ai->ElementsOf_ARFCN_INDEX;
			memcpy(&s->gprs.arfcn_idx[0], &ai->ARFCN_INDEX[0], sizeof(ai->ARFCN_INDEX));
		}
	}

	const PBCCH_Not_present_t *np = &si13ro.u.PBCCH_Not_present;

	s->gprs.rac = np->RAC;
	s->gprs.prio_acc_thresh = np->PRIORITY_ACCESS_THR;
	s->gprs.nco = np->NETWORK_CONTROL_ORDER;

	const GPRS_Cell_Options_t *gco = &np->GPRS_Cell_Options;

	s->gprs.nmo = gco->NMO;
	s->gprs.T3168 = gco->T3168;
	s->gprs.T3192 = gco->T3192;
	s->gprs.ab_type = gco->ACCESS_BURST_TYPE;
	s->gprs.ctrl_ack_type_use_block = !gco->CONTROL_ACK_TYPE; /* inverted */
	s->gprs.bs_cv_max = gco->BS_CV_MAX;

	s->gprs.pan_params_present = gco->Exist_PAN;
	if (s->gprs.pan_params_present) {
		s->gprs.pan_dec = gco->PAN_DEC;
		s->gprs.pan_inc = gco->PAN_INC;
		s->gprs.pan_max = gco->PAN_MAX;
	}

	s->gprs.egprs_supported = 0;
	if (gco->Exist_Extension_Bits) {
		/* CSN.1 codec is not powerful enough (yet?) to decode this part :( */
		unsigned int ext_len = gco->Extension_Bits.extension_length;
		const uint8_t *ext = &gco->Extension_Bits.Extension_Info[0];

		s->gprs.egprs_supported = (ext[0] >> 7);
		if (s->gprs.egprs_supported) {
			if (ext_len < 6)
				return -EINVAL;
			s->gprs.egprs_pkt_chan_req = ~ext[0] & (1 << 6); /* inverted */
			s->gprs.egprs_bep_period = (ext[0] >> 2) & 0x0f;
		}
	}

	/* TODO: GPRS_Power_Control_Parameters */

	s->si13 = 1;

	return 0;
}
