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

struct gsm_support {
	/* rf power capability */
	uint8_t pwr_lev;
	/* controlled early classmark sending */
	uint8_t es_ind;
	/* revision level */
	uint8_t rev_lev;
	/* support of VGCS */
	uint8_t vgcs;
	/* support of VBS */
	uint8_t vbs;
	/* support of SMS */
	uint8_t sm;
	/* screening indicator */
	uint8_t ss_ind;
	/* pseudo synchronised capability */
	uint8_t ps_cap;
	/* CM service prompt */
	uint8_t cmsp;
	/* solsa support */
	uint8_t solsa;
	/* location service support */
	uint8_t lcsva;
	/* codec supprot */
	uint8_t a5_1;
	uint8_t a5_2;
	uint8_t a5_3;
	uint8_t a5_4;
	uint8_t a5_5;
	uint8_t a5_6;
	uint8_t a5_7;
	/* radio support */
	uint8_t p_gsm;
	uint8_t e_gsm;
	uint8_t r_gsm;
	uint8_t r_capa;
	uint8_t low_capa;
	uint8_t dcs_1800;
	uint8_t dcs_capa;
	utnt8_t freq_map[128];
	/* multi slot support */
	uint8_t ms_sup;
	/* ucs2 treatment */
	uint8_t ucs2_treat;
	/* support extended measurements */
	uint8_t ext_meas;
	/* support switched measurement capability */
	uint8_t meas_cap;
	uint8_t sms_val;
	uint8_t sm_val;
	/* positioning method capability */
	uint8_t loc_serv;
	uint8_t e_otd_ass;
	uint8_t e_otd_based;
	uint8_t gps_ass;
	uint8_t gps_based;
	uint8_t gps_conv;
};


