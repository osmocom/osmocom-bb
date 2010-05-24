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

#include <osmocom/osmocom_data.h>

void gsm_support_init(struct osmocom_ms *ms)
{
	struct gsm_support *sup = &ms->support;
	int i;

	memset(sup, 0, sizeof(*sup));
	sup->ms = ms;

	/* rf power capability */
	sup->pwr_lev_900 = 3; /* CLASS 4: Handheld 2W */
	sup->pwr_lev_1800 = 0; /* CLASS 1: Handheld 1W */
	/* controlled early classmark sending */
	sup->es_ind = 0; /* no */
	/* revision level */
	sup->rev_lev = 1; /* phase 2 mobile station */
	/* support of VGCS */
	sup->vgcs = 0; /* no */
	/* support of VBS */
	sup->vbs = 0; /* no */
	/* support of SMS */
	sup->sms_ptp = 1; /* yes */
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
	/* codec supprot */
	sup->a5_1 = 0; /* currently not */
	sup->a5_2 = 0;
	sup->a5_3 = 0;
	sup->a5_4 = 0;
	sup->a5_5 = 0;
	sup->a5_6 = 0;
	sup->a5_7 = 0;
	/* radio support */
	sup->p_gsm = 1; /* P-GSM only */
	sup->e_gsm = 0; /* E-GSM */
	sup->r_gsm = 0; /* R-GSM */
	sup->r_capa = 0;
	sup->low_capa = 4; /* p,e,r power class */
	sup->dcs_1800 = 0;
	/* set supported frequencies */
	if (sup->e_gsm || sup->r_gsm)
		sup->freq_map[0] |= 1;
	if (sup->p_gsm || sup->e_gsm || sup->r_gsm)
		for(i = 1; i <= 124; i++)
			sup->freq_map[i >> 3] |= (1 << (i & 7));
	if (sup->dcs_1800)
		for(i = 512; i <= 885; i++)
			sup->freq_map[i >> 3] |= (1 << (i & 7));
	if (sup->e_gsm)
		for(i = 975; i <= 1023; i++)
			sup->freq_map[i >> 3] |= (1 << (i & 7));
//		for(i = 978; i <= 978; i++)
//			sup->freq_map[i >> 3] |= (1 << (i & 7));
	if (sup->r_gsm)
		for(i = 955; i <= 1023; i++)
			sup->freq_map[i >> 3] |= (1 << (i & 7));
	sup->dcs_capa = 1; /* dcs power class */
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
	sup->min_rxlev_db = -100; // TODO
	sup->sync_to = 6; /* how long to wait sync (0.9 s) */
	sup->scan_to = 4; /* how long to wait for all sysinfos (>=4 s) */
}

/* (3.2.1) maximum channels to scan within each band */
struct gsm_support_scan_max gsm_sup_smax[] = {
	{ 259, 293, 15, 0 }, /* GSM 450 */
	{ 306, 340, 15, 0 }, /* GSM 480 */
	{ 438, 511, 25, 0 },
	{ 128, 251, 30, 0 }, 
	{ 955, 124, 30, 0 },
	{ 512, 885, 40, 0 }, /* DCS 1800 */
	{ 0, 0, 0, 0 }
};

/* dump support */
void gsm_support_dump(struct gsm_support *sup,
			void (*print)(void *, const char *, ...), void *priv)
{
	print(priv, "Supported features of MS '%s':\n", sup->ms->name);
	if (sup->r_gsm)
		print(priv, " R-GSM");
	if (sup->e_gsm || sup->r_gsm)
		print(priv, " E-GSM");
	if (sup->p_gsm || sup->p_gsm || sup->r_gsm)
		print(priv, " P-GSM");
	if (sup->dcs_1800)
		print(priv, " DCS1800");
	print(priv, "  (Phase %d mobile station)\n", sup->rev_lev + 1);
	print(priv, " CECS     : %s\n", (sup->es_ind) ? "yes" : "no");
	print(priv, " VGCS     : %s\n", (sup->vgcs) ? "yes" : "no");
	print(priv, " VBS      : %s\n", (sup->vbs) ? "yes" : "no");
	print(priv, " SMS      : %s\n", (sup->sms_ptp) ? "yes" : "no");
	print(priv, " SS_IND   : %s\n", (sup->ss_ind) ? "yes" : "no");
	print(priv, " PS_CAP   : %s\n", (sup->ps_cap) ? "yes" : "no");
	print(priv, " CMSP     : %s\n", (sup->cmsp) ? "yes" : "no");
	print(priv, " SoLSA    : %s\n", (sup->solsa) ? "yes" : "no");
	print(priv, " LCSVA    : %s\n", (sup->lcsva) ? "yes" : "no");
	print(priv, " LOC_SERV : %s\n", (sup->loc_serv) ? "yes" : "no");
	print(priv, " A5/1     : %s\n", (sup->a5_1) ? "yes" : "no");
	print(priv, " A5/2     : %s\n", (sup->a5_2) ? "yes" : "no");
	print(priv, " A5/3     : %s\n", (sup->a5_3) ? "yes" : "no");
	print(priv, " A5/4     : %s\n", (sup->a5_4) ? "yes" : "no");
	print(priv, " A5/5     : %s\n", (sup->a5_5) ? "yes" : "no");
	print(priv, " A5/6     : %s\n", (sup->a5_6) ? "yes" : "no");
	print(priv, " A5/7     : %s\n", (sup->a5_7) ? "yes" : "no");
	print(priv, " A5/1     : %s\n", (sup->a5_1) ? "yes" : "no");
	print(priv, " Min RXLEV: %d\n", sup->min_rxlev_db);
}

