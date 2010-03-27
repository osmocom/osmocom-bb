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

struct gsm_plmn_na {
	struct llist_head	entry;
	uint16_t		mcc;
	uint16_t		mnc;
}

struct gsm_subsriber {
	/* imsi */
	uint8_t			sim_valid;
	uint16_t		mcc;
	uint16_t		mnc;

	/* stored PLMN */
	uint8_t			plmn_valid;
	uint16_t		plmn_mcc;
	uint16_t		plmn_mnc;
	struct llist_head	plmn_na;

	/* access */
	uint8_t			barred_access;
	uint8_t			class_access[16];

}


