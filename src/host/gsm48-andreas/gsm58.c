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

todo: cell selection and reselection criteria
todo: path loss
todo: downlink signal failure

/*
 * initialization
 */

void gsm58_init(struct osmocom_ms *ms)
{
	struct gsm58_selproc *sp = &ms->selproc;

	memset(sp, 0, sizeof(*sp));
	sp->ms = ms;

	/* init list */
	INIT_LLIST_HEAD(&sp->event_queue);

	return 0;
}

/*
 * event messages
 */

/* allocate a 05.08 event message */
struct msgb *gsm58_msgb_alloc(int msg_type)
{
	struct msgb *msg;
	struct gsm58_msg *gm;

	msg = msgb_alloc_headroom(GSM58_ALLOC_SIZE, GSM58_ALLOC_HEADROOM,
		"GSM 05.08");
	if (!msg)
		return NULL;

	gm = (struct gsm58_msg *)msgb_put(msg, sizeof(*gm));
	gm->msg_type = msg_type;
	return msg;
}

/* queue received message */
int gsm58_sendmsg(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm58_selproc *sp = &ms->selproc;

	msgb_enqueue(&sp->event_queue, msg);
}

/*
 * timer
 */

/* search timer handling */
static void gsm58_timer_timeout(void *arg)
{
	struct gsm322_58_selproc *sp = arg;
	struct msgb *nmsg;

	/* indicate BCCH selection T timeout */
	nmsg = gsm58_msgb_alloc(GSM58_EVENT_TIMEOUT);
	if (!nmsg)
		return -ENOMEM;
	gsm58_sendmsg(ms, nmsg);
}

static void gsm58_timer_start(struct gsm58_selproc *sp, int secs, int micro)
{
	DEBUGP(DLC, "starting FREQUENCY search timer\n");
	sp->timer.cb = gsm58_timer_timeout;
	sp->timer.data = sp;
	bsc_schedule_timer(&sp->timer, secs, micro);
}

static void gsm58_timer_stop(struct gsm58_selproc *plmn)
{
	if (timer_pending(&sp->timer)) {
		DEBUGP(DLC, "stopping pending timer\n");
		bsc_del_timer(&sp->timer);
	}
}

/*
 * process handlers
 */

/* select first/next frequency */
static int gsm58_select_bcch(struct osmocom_ms *ms, int next)
{
	struct gsm_support *s = &ms->support;
	struct gsm58_selproc *sp = &ms->selproc;
	int i, j = 0;

	if (next)
		sp->cur_freq++;

next:
	for (i = 0, i < 1024, i++) {
		if ((sp->ba[i >> 3] & (1 << (i & 7)))) {
			if (j == sp->cur_freq)
				break;
			j++;
		}
	}
	if (i == 1024) {
		struct msgb *nmsg;

		DEBUGP(DLC, "Cycled through all %d frequencies in list.\n", j);
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_NO_CELL_F);
		if (!nmsg)
			return -ENOMEM;
		gsm322_sendmsg(ms, nmsg);
	}

	/* if frequency not supported */
	if (!(s->freq_map[i >> 3] & (1 << (i & 7)))) {
		DEBUGP(DLC, "Frequency %d in list, but not supported.\n", i);
		sp->cur_freq++;
		goto next;
	}

	/* set current BCCH frequency */
	sp->arfcn = i;
	DEBUGP(DLC, "Frequency %d selected, wait for sync.\n", sp->arfcn);
	tx_ph_bcch_req(ms, sp->arfcn);

	/* start timer for synchronizanation */
	gsm58_timer_start(sp, 0, 500000);

	sp->mode = GSM58_MODE_SYNC;

	return 0;
}

/* start normal cell selection: search any channel for given PLMN */
static int gsm58_start_normal_sel(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm58_selproc *sp = &ms->selproc;
	struct gsm_support *s = &ms->support;
	struct gsm58_msg *gm = msgb->data;

	/* reset process */
	memset(sp, 0, sizeof(*sp));

	/* use all frequencies from supported frequency map */
	memcpy(sp->ba, s->freq_map, sizeof(sp->ba));

	/* limit process to given PLMN */
	sp->mcc = gm->mcc;
	sp->mnc = gm->mnc;

	/* select first channel */
	gsm58_select_bcch(ms, 0);
}

/* start stored cell selection: search given channels for given PLMN */
static int gsm58_start_stored_sel(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm58_selproc *sp = &ms->selproc;
	struct gsm_support *s = &ms->support;
	struct gsm58_msg *gm = msgb->data;

	/* reset process */
	memset(sp, 0, sizeof(*sp));

	/* use all frequencies from given frequency map */
	memcpy(sp->ba, sp->ba, sizeof(sp->ba));

	/* limit process to given PLMN */
	sp->mcc = gm->mcc;
	sp->mnc = gm->mnc;

	/* select first channel */
	gsm58_select_bcch(ms, 0);
}

/* start any cell selection: search any channel for any PLMN */
static int gsm58_start_any_sel(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm58_selproc *sp = &ms->selproc;
	struct gsm_support *s = &ms->support;
	struct gsm58_msg *gm = msgb->data;

	/* reset process */
	memset(sp, 0, sizeof(*sp));

	/* allow any cell not barred */
	sp->any = 1;

	/* use all frequencies from supported frequency map */
	memcpy(sp->ba, s->freq_map, sizeof(sp->ba));

	/* select first channel */
	gsm58_select_bcch(ms, 0);
}

/* timeout while selecting BCCH */
static int gsm58_sel_timeout(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm58_selproc *sp = &ms->selproc;

	switch(sp->mode) {
	case GSM58_MODE_SYNC:
		/* if no sync is received from radio withing sync time */
		DEBUGP(DLC, "Syncronization failed, selecting next frq.\n");
		break;
	case GSM58_MODE_READ:
		/* timeout while reading BCCH */
		DEBUGP(DLC, "BCC reading failed, selecting next frq.\n");
		break;
	default:
		DEBUGP(DLC, "Timeout in wrong mode, please fix!\n");
	}

	gsm58_select_bcch(ms, 1);

	return 0;
}

/* we are synchronized to selecting BCCH */
static int gsm58_sel_sync(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm58_selproc *sp = &ms->selproc;
	struct gsm58_msg *gm = (struct gsm58_msg *)msgb->data;

	/* if we got a sync while already selecting a new frequency */
	if (gm->arfcn != sp->arfcn) {
		DEBUGP(DLC, "Requested frq %d, but synced to %d, ignoring.\n"
			sp->arfcn, gm->arfcn);
		return 0;
	}

	DEBUGP(DLC, "Synced to %d, waiting for relevant data.\n", sp->arfcn);

	/* set timer for reading BCCH */
	gsm58_timer_start(sp, 4, 0); // TODO: timer depends on BCCH configur.

	/* reset sysinfo and wait for relevant data */
	gsm_sysinfo_init(ms);
	sp->mode = GSM58_MODE_READ;

	return 0;
}

/* we are getting sysinfo from BCCH */
static int gsm58_sel_sysinfo(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm58_selproc *sp = &ms->selproc;
	struct gsm58_msg *gm = (struct gsm58_msg *)msgb->data;
	struct gsm322_msg *ngm;
	struct msgb *nmsg;
	int barred = 0, i;

	/* ignore system info, if not synced */
	if (sp->mode != GSM58_MODE_DATA && sp->mode != GSM58_MODE_CAMP)
		return 0;

	/* check if cell is barred for us */
	if (!subscr->barred_access && s->cell_barred)
		barred = 1;
	if (sp->any) {
		if (s->class_barr[10])
			barred = 1;
	} else {
		for (i = 0, i <= 15, i++)
			if (subscr->class_access[i] && !s->class_barr[i])
				break;
		if (i > 15)
			barred = 1;
	}


	if (sp->mode == GSM58_MODE_CAMP) {
		/* cell has become barred */
		if (barred) {
			DEBUGP(DLC, "Cell has become barred, starting "
				"reselection.\n");

			sp->mode = GSM58_MODE_IDLE;

			nmsg = gsm322_msgb_alloc(GSM322_EVENT_CELL_RESEL);
			if (!nmsg)
				return -ENOMEM;
			gsm322_sendmsg(ms, nmsg);

			return 0;
		}

		return 0;
	}

	/* can't use barred cell */
	if (barred) {
		DEBUGP(DLC, "Selected cell is barred, select next.\n");
		gsm58_timer_stop(sp);
		gsm58_select_bcch(ms, 1);

		return 0;
	}

	/* if PLMN not yet indicated, but required if not any cell selection */
	if (!sp->any && !s->mcc)
		return 0;

	// todo: what else do we need until we can camp?

	/* wrong PLMN */
	if (!sp->any && s->mcc == sp->mcc && s->mnc == sp->mnc) {
		DEBUGP(DLC, "Selected cell of differen PLMN, select next.\n");
		gsm58_timer_stop(sp);
		gsm58_select_bcch(ms, 1);

		return 0;
	}

	/* all relevant informations received */
	gsm58_timer_stop(sp);
	sp->mode = GSM58_MODE_CAMP;

	DEBUGP(DCS, "Cell with freq %d, mcc = %d, mnc = %d selected.\n",
		sp->arfcn, s->mcc, s->mnc);

	/* indicate cell */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_CELL_FOUND);
	if (!nmsg)
		return -ENOMEM;
	ngm = (struct gsm322_msg *)nmsg->data;
	ngm->mcc = s->mcc;
	ngm->mnc = s->mnc;
	ngm->lac = s->lac;
	gsm322_sendmsg(ms, nmsg);

	return 0;
}

/*
 * events
 */

/* receive events for GSM 05.08 processes */
static int gsm58_event(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm58_msg *gm = (struct gsm58_msg *)msgb->data;
	int msg_type = gm->msg_type;

	DEBUGP(DCS, "(ms %s) Message '%s' for link control "
		"%s\n", ms->name, gsm58_event_names[msg_type],
		plmn_a_state_names[plmn->state]);

	switch(msg_type) {
	case GSM58_EVENT_START_NORMAL:
		gsm58_start_normal_sel(ms, msg);
		break;
	case GSM58_EVENT_START_STORED:
		gsm58_start_stored_sel(ms, msg);
		break;
	case GSM58_EVENT_START_ANY:
		gsm58_start_any_sel(ms, msg);
		break;
	case GSM58_EVENT_TIMEOUT:
		gsm58_sel_timeout(ms, msg);
		break;
	case GSM58_EVENT_SYNC:
		gsm58_sel_sync(ms, msg);
		break;
	case GSM58_EVENT_SYSINFO:
		gsm58_sel_sysinfo(ms, msg);
		break;
	default:
		DEBUGP(DLC, "Message unhandled.\n");
	}

	return 0;
}

/* dequeue GSM 05.08 events */
int gsm58_event_queue(struct osmocom_ms *ms)
{
	struct gsm58_selproc *sp = &ms->selproc;
	struct msgb *msg;
	int work = 0;
	
	while ((msg = msgb_dequeue(&sp->event_queue))) {
		gsm58_event(ms, msg);
		free_msgb(msg);
		work = 1; /* work done */
	}
	
	return work;
}



