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

void gsm_support_init(struct osmocom_ms *ms)
{
	struct gsm_support *s = &ms->support;
	int i;

	memset(s, 0, sizeof(*s));

	/* rf power capability */
	s->pwr_lev = 3; /* handheld */
	/* controlled early classmark sending */
	s->es_ind = 0; /* no */
	/* revision level */
	s->rev_lev = 1; /* phase 2 mobile station */
	/* support of VGCS */
	s->vgcs = 0; /* no */
	/* support of VBS */
	s->vbs = 0; /* no */
	/* support of SMS */
	s->sm = 1; /* yes */
	/* screening indicator */
	s->ss_ind = 1; /* phase 2 error handling */
	/* pseudo synchronised capability */
	s->ps_cap = 0; /* no */
	/* CM service prompt */
	s->cmsp = 0; /* no */
	/* solsa support */
	s->solsa = 0; /* no */
	/* location service support */
	s->lcsva = 0; /* no */
	s->loc_serv = 0; /* no */
	/* codec supprot */
	s->a5_1 = 0; /* currently not */
	s->a5_2 = 0;
	s->a5_3 = 0;
	s->a5_4 = 0;
	s->a5_5 = 0;
	s->a5_6 = 0;
	s->a5_7 = 0;
	/* radio support */
	s->p_gsm = 1; /* gsm 900 */
	for(i = 1, i <= 124, i++)
		s->freq_map[i >> 3] |= (1 << (i & 7));
	s->frq_map[1] = 0xff;
	s->frq_map[1] = 0xff;
	s->e_gsm = 0;
	s->r_gsm = 0;
	s->r_capa = 0;
	s->low_capa = 4; /* p,e,r power class */
	s->dcs_1800 = 1;
	for(i = 512, i <= 885, i++)
		s->freq_map[i >> 3] |= (1 << (i & 7));
	s->dcs_capa = 1; /* dcs power class */
	/* multi slot support */
	s->ms_sup = 0; /* no */
	/* ucs2 treatment */
	s->ucs2_treat = 0; /* default */
	/* support extended measurements */
	s->ext_meas = 0; /* no */
	/* support switched measurement capability */
	s->meas_cap = 0; /* no */
	//s->sms_val = ;
	//s->sm_val = ;

	/* IMEI */
	sprintf(s->imei, "000000000000000");
	sprintf(s->imeisv, "0000000000000000");
}


