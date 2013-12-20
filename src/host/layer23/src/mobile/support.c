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

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <osmocom/bb/common/osmocom_data.h>

void gsm_support_init(struct osmocom_ms *ms)
{
	struct gsm_support *sup = &ms->support;

	memset(sup, 0, sizeof(*sup));
	sup->ms = ms;

	/* controlled early classmark sending */
	sup->es_ind = 0; /* no */
	/* revision level */
	sup->rev_lev = 1; /* phase 2 mobile station */
	/* support of VGCS */
	sup->vgcs = 0; /* no */
	/* support of VBS */
	sup->vbs = 0; /* no */
	/* support of SMS */
	sup->sms_ptp = 1; /* no */
	/* screening indicator */
	sup->ss_ind = 1; /* phase 2 error handling */
	/* pseudo synchronised capability */
	sup->ps_cap = 0; /* no */
	/* CM service prompt */
	sup->cmsp = 0; /* no */
	/* solsa support */
	sup->solsa = 0; /* no */
	/* location service support */
	sup->lcsva = 0; /* no */
	sup->loc_serv = 0; /* no */
	/* cipher support */
	sup->a5_1 = 1;
	sup->a5_2 = 1;
	sup->a5_3 = 0;
	sup->a5_4 = 0;
	sup->a5_5 = 0;
	sup->a5_6 = 0;
	sup->a5_7 = 0;
	/* radio support */
	sup->p_gsm = 1; /* P-GSM */
	sup->e_gsm = 1; /* E-GSM */
	sup->r_gsm = 1; /* R-GSM */
	sup->dcs = 1;
	sup->gsm_850 = 1;
	sup->pcs = 1;
	sup->gsm_480 = 0;
	sup->gsm_450 = 0;
	/* rf power capability */
	sup->class_900 = 4; /* CLASS 4: Handheld 2W */
	sup->class_850 = 4;
	sup->class_400 = 4;
	sup->class_dcs = 1; /* CLASS 1: Handheld 1W */
	sup->class_pcs = 1;
	/* multi slot support */
	sup->ms_sup = 0; /* no */
	/* ucs2 treatment */
	sup->ucs2_treat = 0; /* default */
	/* support extended measurements */
	sup->ext_meas = 0; /* no */
	/* support switched measurement capability */
	sup->meas_cap = 0; /* no */
	//sup->sms_val = ;
	//sup->sm_val = ;

	/* radio */
	sup->ch_cap = GSM_CAP_SDCCH_TCHF_TCHH;
	sup->min_rxlev_dbm = -106; // TODO
	sup->sync_to = 6; /* how long to wait sync (0.9 s) */
	sup->scan_to = 4; /* how long to wait for all sysinfos (>=4 s) */
	sup->dsc_max = 90; /* the specs defines 90 */

	/* codec */
	sup->full_v1 = 1;
	sup->full_v2 = 1;
	sup->full_v3 = 0;
	sup->half_v1 = 1;
	sup->half_v3 = 0;
}

/* (3.2.1) maximum channels to scan within each band */
struct gsm_support_scan_max gsm_sup_smax[] = {
	{ 259, 293, 15, 0 }, /* GSM 450 */
	{ 306, 340, 15, 0 }, /* GSM 480 */
	{ 438, 511, 25, 0 },
	{ 128, 251, 30, 0 }, /* GSM 850 */
	{ 955, 124, 30, 0 }, /* P,E,R GSM */
	{ 512, 885, 40, 0 }, /* DCS 1800 */
	{ 1024, 1322, 40, 0 }, /* PCS 1900 */
	{ 0, 0, 0, 0 }
};

#define SUP_SET(item) \
	((sup->item) ? ((set->item) ? "yes" : "disabled") : "no")
/* dump support */
void gsm_support_dump(struct osmocom_ms *ms,
			void (*print)(void *, const char *, ...), void *priv)
{
	struct gsm_support *sup = &ms->support;
	struct gsm_settings *set = &ms->settings;

	print(priv, "Supported features of MS '%s':\n", sup->ms->name);
	print(priv, " Phase %d mobile station\n", sup->rev_lev + 1);
	print(priv, " R-GSM        : %s\n", SUP_SET(r_gsm));
	print(priv, " E-GSM        : %s\n", SUP_SET(e_gsm));
	print(priv, " P-GSM        : %s\n", SUP_SET(p_gsm));
	if (set->r_gsm || set->e_gsm || set->p_gsm)
		print(priv, " GSM900 Class : %d\n", set->class_900);
	print(priv, " DCS 1800     : %s\n", SUP_SET(dcs));
	if (set->dcs)
		print(priv, " DCS Class    : %d\n", set->class_dcs);
	print(priv, " GSM 850      : %s\n", SUP_SET(gsm_850));
	if (set->gsm_850)
		print(priv, " GSM 850 Class: %d\n", set->class_850);
	print(priv, " PCS 1900     : %s\n", SUP_SET(pcs));
	if (set->pcs)
		print(priv, " PCS Class    : %d\n", set->class_pcs);
	print(priv, " GSM 480      : %s\n", SUP_SET(gsm_480));
	print(priv, " GSM 450      : %s\n", SUP_SET(gsm_450));
	if (set->gsm_480 | set->gsm_450)
		print(priv, " GSM 400 Class: %d\n", set->class_400);
	print(priv, " CECS         : %s\n", (sup->es_ind) ? "yes" : "no");
	print(priv, " VGCS         : %s\n", (sup->vgcs) ? "yes" : "no");
	print(priv, " VBS          : %s\n", (sup->vbs) ? "yes" : "no");
	print(priv, " SMS          : %s\n", SUP_SET(sms_ptp));
	print(priv, " SS_IND       : %s\n", (sup->ss_ind) ? "yes" : "no");
	print(priv, " PS_CAP       : %s\n", (sup->ps_cap) ? "yes" : "no");
	print(priv, " CMSP         : %s\n", (sup->cmsp) ? "yes" : "no");
	print(priv, " SoLSA        : %s\n", (sup->solsa) ? "yes" : "no");
	print(priv, " LCSVA        : %s\n", (sup->lcsva) ? "yes" : "no");
	print(priv, " LOC_SERV     : %s\n", (sup->loc_serv) ? "yes" : "no");
	print(priv, " A5/1         : %s\n", SUP_SET(a5_1));
	print(priv, " A5/2         : %s\n", SUP_SET(a5_2));
	print(priv, " A5/3         : %s\n", SUP_SET(a5_3));
	print(priv, " A5/4         : %s\n", SUP_SET(a5_4));
	print(priv, " A5/5         : %s\n", SUP_SET(a5_5));
	print(priv, " A5/6         : %s\n", SUP_SET(a5_6));
	print(priv, " A5/7         : %s\n", SUP_SET(a5_7));
	switch (set->ch_cap) {
		case GSM_CAP_SDCCH:
		print(priv, " Channels     : SDCCH only\n");
		break;
		case GSM_CAP_SDCCH_TCHF:
		print(priv, " Channels     : SDCCH + TCH/F\n");
		break;
		case GSM_CAP_SDCCH_TCHF_TCHH:
		print(priv, " Channels     : SDCCH + TCH/F + TCH/H\n");
		break;
	}
	print(priv, " Full-Rate V1 : %s\n", SUP_SET(full_v1));
	print(priv, " Full-Rate V2 : %s\n", SUP_SET(full_v2));
	print(priv, " Full-Rate V3 : %s\n", SUP_SET(full_v3));
	print(priv, " Half-Rate V1 : %s\n", SUP_SET(half_v1));
	print(priv, " Half-Rate V3 : %s\n", SUP_SET(half_v3));
	print(priv, " Min RXLEV    : %d\n", set->min_rxlev_dbm);
}

