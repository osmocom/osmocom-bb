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

int gsm_subscr_init(struct osmocom_ms *ms)
{
	struct gsm_subscriber *subcr = &ms->subscr;

	memset(subscr, 0, sizeof(*subscr));

	/* set key invalid */
	subscr->key_seq = 7;

	INIT_LLIST_HEAD(&subscr->plmn_na);
}

static const char *subscr_ustate_names[] = {
	"U0_NULL",
	"U1_UPDATED",
	"U2_NOT_UPDATED",
	"U3_ROAMING_NA"
};

/* change to new U state */
void new_sim_ustate(struct gsm_subscriber *subscr, int state)
{
	DEBUGP(DMM, "(ms %s) new state %s -> %s\n", subscr->ms,
		subscr_ustate_names[subscr->ustate], subscr_ustate_names[state]);

	subscr->ustate = state;
}

