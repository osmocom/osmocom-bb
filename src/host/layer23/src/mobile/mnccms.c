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

#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <osmocom/core/talloc.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/common/settings.h>
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/mncc_ms.h>
#include <osmocom/bb/mobile/vty.h>

static uint32_t new_callref = 1;

static const char * const gsm_call_type_str[] = {
	[GSM_CALL_T_UNKNOWN]	= "unknown",
	[GSM_CALL_T_VOICE]	= "voice",
	[GSM_CALL_T_DATA]	= "data",
	[GSM_CALL_T_DATA_FAX]	= "fax",
};

static int dtmf_statemachine(struct gsm_call *call,
			     const struct gsm_mncc *mncc);
static void timeout_dtmf(void *arg);
static void timeout_ringer(void *arg);

/*
 * support functions
 */

/* DTMF timer */
static void start_dtmf_timer(struct gsm_call *call, uint16_t ms)
{
	LOGP(DCC, LOGL_INFO, "starting DTMF timer %d ms\n", ms);
	call->dtmf_timer.cb = timeout_dtmf;
	call->dtmf_timer.data = call;
	osmo_timer_schedule(&call->dtmf_timer, 0, ms * 1000);
}

static void stop_dtmf_timer(struct gsm_call *call)
{
	if (osmo_timer_pending(&call->dtmf_timer)) {
		LOGP(DCC, LOGL_INFO, "stopping pending DTMF timer\n");
		osmo_timer_del(&call->dtmf_timer);
	}
}

/* Ringer */
static void update_ringer(struct gsm_call *call)
{
	struct osmocom_ms *ms = call->ms;

	if (call->call_state == CALL_ST_MT_RING
	 || call->call_state == CALL_ST_MT_KNOCK) {
		struct gsm_settings *set = &ms->settings;

		/* ringer on */
		if (set->ringtone == 0) {
			LOGP(DCC, LOGL_INFO, "Ringer disabled\n");
			return;
		}
		if (osmo_timer_pending(&call->ringer_timer))
			return;
		LOGP(DCC, LOGL_INFO, "starting Ringer\n");
		call->ringer_timer.cb = timeout_ringer;
		call->ringer_timer.data = call;
		osmo_timer_schedule(&call->ringer_timer, RINGER_MARK);
		l1ctl_tx_ringer_req(ms, set->ringtone);
		call->ringer_state = 1;
	} else {
		/* ringer off */
		if (!osmo_timer_pending(&call->ringer_timer))
			return;
		LOGP(DCC, LOGL_INFO, "stop Ringer\n");
		osmo_timer_del(&call->ringer_timer);
		if (call->ringer_state)
			l1ctl_tx_ringer_req(ms, 0);
	}
}

static void timeout_ringer(void *arg)
{
	struct gsm_call *call = arg;
	struct osmocom_ms *ms = call->ms;

	/* on <-> off */
	call->ringer_state = 1 - call->ringer_state;

	if (call->ringer_state) {
		struct gsm_settings *set = &ms->settings;

		osmo_timer_schedule(&call->ringer_timer, RINGER_MARK);
		l1ctl_tx_ringer_req(ms, set->ringtone);
	} else {
		osmo_timer_schedule(&call->ringer_timer, RINGER_SPACE);
		l1ctl_tx_ringer_req(ms, 0);
	}
}

/* free call instance */
static void free_call(struct gsm_call *call)
{
	stop_dtmf_timer(call);

	call->call_state = CALL_ST_IDLE;
	update_ringer(call);

	llist_del(&call->entry);
	DEBUGP(DMNCC, "(call %x) Call removed.\n", call->callref);
	talloc_free(call);
}

static struct gsm_call *get_call_ref(struct osmocom_ms *ms, uint32_t callref)
{
	struct gsm_call *callt;

	llist_for_each_entry(callt, &ms->mncc_entity.call_list, entry) {
		if (callt->callref == callref)
			return callt;
	}
	return NULL;
}

static int8_t mncc_get_bearer(const struct gsm_settings *set, uint8_t speech_ver)
{
	switch (speech_ver) {
	case GSM48_BCAP_SV_AMR_F:
		if (set->full_v3)
			LOGP(DMNCC, LOGL_INFO, " net suggests full rate v3\n");
		else {
			LOGP(DMNCC, LOGL_INFO, " full rate v3 not supported\n");
			return -1;
		}
		break;
	case GSM48_BCAP_SV_EFR:
		if (set->full_v2)
			LOGP(DMNCC, LOGL_INFO, " net suggests full rate v2\n");
		else {
			LOGP(DMNCC, LOGL_INFO, " full rate v2 not supported\n");
			return -1;
		}
		break;
	case GSM48_BCAP_SV_FR: /* mandatory */
		if (set->full_v1)
			LOGP(DMNCC, LOGL_INFO, " net suggests full rate v1\n");
		else {
			LOGP(DMNCC, LOGL_INFO, " full rate v1 not supported\n");
			return -1;
		}
		break;
	case GSM48_BCAP_SV_AMR_H:
		if (set->half_v3)
			LOGP(DMNCC, LOGL_INFO, " net suggests half rate v3\n");
		else {
			LOGP(DMNCC, LOGL_INFO, " half rate v3 not supported\n");
			return -1;
		}
		break;
	case GSM48_BCAP_SV_HR:
		if (set->half_v1)
			LOGP(DMNCC, LOGL_INFO, " net suggests half rate v1\n");
		else {
			LOGP(DMNCC, LOGL_INFO, " half rate v1 not supported\n");
			return -1;
		}
		break;
	default:
		LOGP(DMNCC, LOGL_INFO, " net suggests unknown speech version "
			"%d\n", speech_ver);
		return -1;
	}

	return speech_ver;
}

static void mncc_set_bcap_speech(struct gsm_mncc *mncc,
				 const struct gsm_settings *set,
				 int speech_ver)
{
	int i = 0;

	mncc->fields |= MNCC_F_BEARER_CAP;
	mncc->bearer_cap.coding = GSM48_BCAP_CODING_GSM_STD;
	if (set->ch_cap == GSM_CAP_SDCCH_TCHF_TCHH
	 && (set->half_v1 || set->half_v3)) {
		mncc->bearer_cap.radio = GSM48_BCAP_RRQ_DUAL_FR;
		LOGP(DMNCC, LOGL_INFO, " support TCH/H also\n");
	} else {
		mncc->bearer_cap.radio = GSM48_BCAP_RRQ_FR_ONLY;
		LOGP(DMNCC, LOGL_INFO, " support TCH/F only\n");
	}
	mncc->bearer_cap.speech_ctm = 0;
	/* if no specific speech_ver is given */
	if (speech_ver < 0) {
		/* if half rate is supported and preferred */
		if (set->half_v3 && set->half && set->half_prefer) {
			mncc->bearer_cap.speech_ver[i++] = GSM48_BCAP_SV_AMR_H;
			LOGP(DMNCC, LOGL_INFO, " support half rate v3\n");
		}
		if (set->half_v1 && set->half && set->half_prefer) {
			mncc->bearer_cap.speech_ver[i++] = GSM48_BCAP_SV_HR;
			LOGP(DMNCC, LOGL_INFO, " support half rate v1\n");
		}
		/* if full rate is supported */
		if (set->full_v3) {
			mncc->bearer_cap.speech_ver[i++] = GSM48_BCAP_SV_AMR_F;
			LOGP(DMNCC, LOGL_INFO, " support full rate v3\n");
		}
		if (set->full_v2) {
			mncc->bearer_cap.speech_ver[i++] = GSM48_BCAP_SV_EFR;
			LOGP(DMNCC, LOGL_INFO, " support full rate v2\n");
		}
		if (set->full_v1) { /* mandatory, so it's always true */
			mncc->bearer_cap.speech_ver[i++] = GSM48_BCAP_SV_FR;
			LOGP(DMNCC, LOGL_INFO, " support full rate v1\n");
		}
		/* if half rate is supported and not preferred */
		if (set->half_v3 && set->half && !set->half_prefer) {
			mncc->bearer_cap.speech_ver[i++] = GSM48_BCAP_SV_AMR_H;
			LOGP(DMNCC, LOGL_INFO, " support half rate v3\n");
		}
		if (set->half_v1 && set->half && !set->half_prefer) {
			mncc->bearer_cap.speech_ver[i++] = GSM48_BCAP_SV_HR;
			LOGP(DMNCC, LOGL_INFO, " support half rate v1\n");
		}
	/* if specific speech_ver is given (it must be supported) */
	} else
		mncc->bearer_cap.speech_ver[i++] = speech_ver;
	mncc->bearer_cap.speech_ver[i] = -1; /* end of list */
	mncc->bearer_cap.transfer = GSM48_BCAP_ITCAP_SPEECH;
	mncc->bearer_cap.mode = GSM48_BCAP_TMOD_CIRCUIT;
}

static const struct bcap_data_set {
	enum gsm48_bcap_ra		ra;
	enum gsm48_bcap_interm_rate	ir;
	enum gsm48_bcap_user_rate	ur;
	enum gsm48_bcap_modem_type	mt;
} bcap_data_set_map[] = {
	[DATA_CALL_TR_V21_300] = {
		.ir = GSM48_BCAP_IR_8k,
		.ur = GSM48_BCAP_UR_300,
		.mt = GSM48_BCAP_MT_V21,
	},
	[DATA_CALL_TR_V22_1200] = {
		.ir = GSM48_BCAP_IR_8k,
		.ur = GSM48_BCAP_UR_1200,
		.mt = GSM48_BCAP_MT_V22,
	},
	[DATA_CALL_TR_V23_1200_75] = {
		.ir = GSM48_BCAP_IR_8k,
		.ur = GSM48_BCAP_UR_1200_75,
		.mt = GSM48_BCAP_MT_V23,
	},
	[DATA_CALL_TR_V22bis_2400] = {
		.ir = GSM48_BCAP_IR_8k,
		.ur = GSM48_BCAP_UR_2400,
		.mt = GSM48_BCAP_MT_V22bis,
	},
	[DATA_CALL_TR_V26ter_2400] = {
		.ir = GSM48_BCAP_IR_8k,
		.ur = GSM48_BCAP_UR_2400,
		.mt = GSM48_BCAP_MT_V26ter,
	},
	[DATA_CALL_TR_V32_4800] = {
		.ir = GSM48_BCAP_IR_8k,
		.ur = GSM48_BCAP_UR_4800,
		.mt = GSM48_BCAP_MT_V32,
	},
	[DATA_CALL_TR_V32_9600] = {
		.ir = GSM48_BCAP_IR_16k,
		.ur = GSM48_BCAP_UR_9600,
		.mt = GSM48_BCAP_MT_V32,
	},
#if 0
	[DATA_CALL_TR_V34_9600] = {
		.ir = GSM48_BCAP_IR_16k,
		.ur = GSM48_BCAP_UR_9600,
		.mt = GSM48_BCAP_MT_V32,
		/* (Octet 6c) Modem type: According to ITU-T Rec. V.32
		 * (Octet 6d) Other modem type: According to ITU-T Rec. V.34 (2)
		 * TODO: gsm48_encode_bearer_cap() does not support octet 6d */
	},
#endif
	[DATA_CALL_TR_V110_300] = {
		.ra = GSM48_BCAP_RA_V110_X30,
		.ir = GSM48_BCAP_IR_8k,
		.ur = GSM48_BCAP_UR_300,
	},
	[DATA_CALL_TR_V110_1200] = {
		.ra = GSM48_BCAP_RA_V110_X30,
		.ir = GSM48_BCAP_IR_8k,
		.ur = GSM48_BCAP_UR_1200,
	},
	[DATA_CALL_TR_V110_2400] = {
		.ra = GSM48_BCAP_RA_V110_X30,
		.ir = GSM48_BCAP_IR_8k,
		.ur = GSM48_BCAP_UR_2400,
	},
	[DATA_CALL_TR_V110_4800] = {
		.ra = GSM48_BCAP_RA_V110_X30,
		.ir = GSM48_BCAP_IR_8k,
		.ur = GSM48_BCAP_UR_4800,
	},
	[DATA_CALL_TR_V110_9600] = {
		.ra = GSM48_BCAP_RA_V110_X30,
		.ir = GSM48_BCAP_IR_16k,
		.ur = GSM48_BCAP_UR_9600,
	},
};

static void mncc_set_bcap_data(struct gsm_mncc *mncc,
			       const struct gsm_settings *set,
			       enum gsm_call_type call_type)
{
	const struct data_call_params *cp = &set->call_params.data;
	struct gsm_mncc_bearer_cap *bcap = &mncc->bearer_cap;
	const struct bcap_data_set *ds;

	OSMO_ASSERT(cp->type_rate < ARRAY_SIZE(bcap_data_set_map));
	ds = &bcap_data_set_map[cp->type_rate];
	if (ds->ir == 0 && ds->ur == 0) {
		LOGP(DMNCC, LOGL_ERROR, "AT+CBST=%d is not supported\n", cp->type_rate);
		return;
	}

	mncc->fields |= MNCC_F_BEARER_CAP;

	*bcap = (struct gsm_mncc_bearer_cap) {
		/* .transfer is set below */
		.mode = GSM48_BCAP_TMOD_CIRCUIT,
		.coding = GSM48_BCAP_CODING_GSM_STD,
		/* .radio is set below */
		.data = {
			.sig_access = GSM48_BCAP_SA_I440_I450,
			.rate_adaption = ds->ra,
			.interm_rate = ds->ir,
			.user_rate = ds->ur,
			.modem_type = ds->mt,
			.transp = cp->transp,

			/* async call params */
			.async = cp->is_async,
			.nr_data_bits = cp->nr_data_bits,
			.nr_stop_bits = cp->nr_stop_bits,
			.parity = cp->parity,
		},
	};

	/* Radio channel requirement (octet 3) */
	if (set->ch_cap == GSM_CAP_SDCCH_TCHF_TCHH) {
		if (set->half_prefer)
			bcap->radio = GSM48_BCAP_RRQ_DUAL_HR;
		else
			bcap->radio = GSM48_BCAP_RRQ_DUAL_FR;
		LOGP(DMNCC, LOGL_INFO, " support TCH/H also\n");
	} else {
		bcap->radio = GSM48_BCAP_RRQ_FR_ONLY;
		LOGP(DMNCC, LOGL_INFO, " support TCH/F only\n");
	}

	/* Information transfer capability (octet 3) */
	switch (call_type) {
	case GSM_CALL_T_DATA:
		if (ds->mt == GSM48_BCAP_MT_NONE)
			bcap->transfer = GSM_MNCC_BCAP_UNR_DIG;
		else /* analog modem */
			bcap->transfer = GSM_MNCC_BCAP_AUDIO;
		break;
	case GSM_CALL_T_DATA_FAX:
		bcap->transfer = GSM_MNCC_BCAP_FAX_G3;
		break;
	case GSM_CALL_T_VOICE:
	default: /* shall not happen */
		OSMO_ASSERT(0);
	}

	/* FAX calls are special (see 3GPP TS 24.008, Annex D.3) */
	if (call_type == GSM_CALL_T_DATA_FAX) {
		bcap->data.rate_adaption = GSM48_BCAP_RA_NONE;
		bcap->data.async = 0; /* shall be sync */
		bcap->data.transp = GSM48_BCAP_TR_TRANSP;
		bcap->data.modem_type = GSM48_BCAP_MT_NONE;
	}
}

/* Check the given Bearer Capability, select first supported speech codec version.
 * The choice between half-rate and full-rate is made based on current settings.
 * Return a selected codec or -1 if no speech codec was selected. */
static int mncc_handle_bcap_speech(const struct gsm_mncc_bearer_cap *bcap,
				   const struct gsm_settings *set)
{
	int speech_ver_half = -1;
	int speech_ver = -1;

	for (unsigned int i = 0; bcap->speech_ver[i] >= 0; i++) {
		int temp = mncc_get_bearer(set, bcap->speech_ver[i]);
		switch (temp) {
		case GSM48_BCAP_SV_AMR_H:
		case GSM48_BCAP_SV_HR:
			/* only the first half rate */
			if (speech_ver_half < 0)
				speech_ver_half = temp;
			break;
		default:
			if (temp < 0)
				continue;
			/* only the first full rate */
			if (speech_ver < 0)
				speech_ver = temp;
			break;
		}
	}

	/* half and full given */
	if (speech_ver_half >= 0 && speech_ver >= 0) {
		if (set->half_prefer) {
			LOGP(DMNCC, LOGL_INFO, " both supported"
				" codec rates are given, using "
				"preferred half rate\n");
			speech_ver = speech_ver_half;
		} else {
			LOGP(DMNCC, LOGL_INFO, " both supported"
				" codec rates are given, using "
				"preferred full rate\n");
		}
	} else if (speech_ver_half < 0 && speech_ver < 0) {
		LOGP(DMNCC, LOGL_INFO, " no supported codec "
			"rate is given\n");
	/* only half rate is given, use it */
	} else if (speech_ver_half >= 0) {
		LOGP(DMNCC, LOGL_INFO, " only supported half "
			"rate codec is given, using it\n");
		speech_ver = speech_ver_half;
	/* only full rate is given, use it */
	} else {
		LOGP(DMNCC, LOGL_INFO, " only supported full "
			"rate codec is given, using it\n");
	}

	return speech_ver;
}

/* Check the given Bearer Capability for a data call (CSD).
 * Return 0 if the bearer is accepted, otherwise return -1. */
static int mncc_handle_bcap_data(const struct gsm_mncc_bearer_cap *bcap,
				 const struct gsm_settings *set)
{
	switch (bcap->transfer) {
	case GSM48_BCAP_ITCAP_UNR_DIG_INF:
		if (bcap->data.rate_adaption != GSM48_BCAP_RA_V110_X30) {
			LOGP(DMNCC, LOGL_ERROR,
			     "%s(): Rate adaption (octet 5) 0x%02x is not supported\n",
			     __func__, bcap->data.rate_adaption);
			return -ENOTSUP;
		}
		break;
	case GSM48_BCAP_ITCAP_3k1_AUDIO:
	case GSM48_BCAP_ITCAP_FAX_G3:
		if (bcap->data.rate_adaption != GSM48_BCAP_RA_NONE) {
			LOGP(DMNCC, LOGL_ERROR,
			     "%s(): Rate adaption (octet 5) 0x%02x was expected to be NONE\n",
			     __func__, bcap->data.rate_adaption);
			return -ENOTSUP;
		}
		break;
	default:
		break;
	}

	if (bcap->data.sig_access != GSM48_BCAP_SA_I440_I450) {
		LOGP(DMNCC, LOGL_ERROR,
		     "%s(): Signalling access protocol (octet 5) 0x%02x is not supported\n",
		     __func__, bcap->data.sig_access);
		return -ENOTSUP;
	}

#define BCAP_RATE(interm_rate, user_rate) \
	((interm_rate << 8) | (user_rate << 0))

	switch (BCAP_RATE(bcap->data.interm_rate, bcap->data.user_rate)) {
	case BCAP_RATE(GSM48_BCAP_IR_8k, GSM48_BCAP_UR_300):
	case BCAP_RATE(GSM48_BCAP_IR_8k, GSM48_BCAP_UR_1200):
	case BCAP_RATE(GSM48_BCAP_IR_8k, GSM48_BCAP_UR_2400):
		if (bcap->data.transp != GSM48_BCAP_TR_TRANSP) {
			LOGP(DMNCC, LOGL_ERROR,
			     "%s(): wrong user-rate 0x%02x for a non-transparent call\n",
			     __func__, bcap->data.user_rate);
			return -EINVAL;
		}
		/* fall-through */
	case BCAP_RATE(GSM48_BCAP_IR_8k, GSM48_BCAP_UR_4800):
	case BCAP_RATE(GSM48_BCAP_IR_16k, GSM48_BCAP_UR_9600):
		if (bcap->data.transp != GSM48_BCAP_TR_TRANSP) {
			LOGP(DMNCC, LOGL_ERROR,
			     "%s(): only transparent calls are supported so far\n",
			     __func__);
			return -ENOTSUP;
		}
		break;
	default:
		LOGP(DMNCC, LOGL_ERROR,
		     "%s(): IR 0x%02x / UR 0x%02x combination is not supported\n",
		     __func__, bcap->data.interm_rate, bcap->data.user_rate);
		return -ENOTSUP;
	}

#undef BCAP_RATE

	return 0;
}

static int mncc_handle_bcap(struct gsm_mncc *mncc_out,		/* CC Call Confirmed */
			    const struct gsm_mncc *mncc_in,	/* CC Setup */
			    const struct gsm_settings *set)
{
	const struct gsm_mncc_bearer_cap *bcap = &mncc_in->bearer_cap;

	/* 3GPP TS 24.008, section 9.3.2.2 defines several cases in which the
	 * Bearer Capability 1 IE is to be included, provided that at least
	 * one of these conditions is met. */

	/* if the Bearer Capability 1 IE is not present */
	if (~mncc_in->fields & MNCC_F_BEARER_CAP) {
		/* ... include our own Bearer Capability, assuming a speech call */
		mncc_set_bcap_speech(mncc_out, set, -1);
		return 0;
	}

	if (bcap->mode != GSM48_BCAP_TMOD_CIRCUIT) {
		LOGP(DMNCC, LOGL_ERROR,
		     "%s(): Transfer mode 0x%02x is not supported\n",
		     __func__, bcap->mode);
		return -ENOTSUP;
	}
	if (bcap->coding != GSM48_BCAP_CODING_GSM_STD) {
		LOGP(DMNCC, LOGL_ERROR,
		     "%s(): Coding standard 0x%02x is not supported\n",
		     __func__, bcap->coding);
		return -ENOTSUP;
	}

	switch (bcap->transfer) {
	case GSM48_BCAP_ITCAP_SPEECH:
	{
		int speech_ver = mncc_handle_bcap_speech(bcap, set);
		/* include bearer cap, if not given in setup (see above)
		 * or if multiple codecs are given
		 * or if not only full rate
		 * or if given codec is unimplemented
		 */
		if (speech_ver < 0)
			mncc_set_bcap_speech(mncc_out, set, -1);
		else if (bcap->speech_ver[1] >= 0 || speech_ver != 0)
			mncc_set_bcap_speech(mncc_out, set, speech_ver);
		break;
	}
	case GSM48_BCAP_ITCAP_UNR_DIG_INF:
	case GSM48_BCAP_ITCAP_3k1_AUDIO:
	case GSM48_BCAP_ITCAP_FAX_G3:
		return mncc_handle_bcap_data(bcap, set);
	default:
		LOGP(DMNCC, LOGL_ERROR,
		     "%s(): Information transfer capability 0x%02x is not supported\n",
		     __func__, bcap->transfer);
		return -ENOTSUP;
	}

	return 0;
}

/*
 * MNCCms dummy application
 */

/* this is a minimal implementation as required by GSM 04.08 */
int mncc_recv_dummy(struct osmocom_ms *ms, int msg_type, void *arg)
{
	struct gsm_mncc *data = arg;
	uint32_t callref = data->callref;
	struct gsm_mncc rel;

	if (msg_type == MNCC_REL_IND || msg_type == MNCC_REL_CNF)
		return 0;

	LOGP(DMNCC, LOGL_INFO, "Rejecting incoming call\n");

	/* reject, as we don't support Calls */
	memset(&rel, 0, sizeof(struct gsm_mncc));
	rel.callref = callref;
	mncc_set_cause(&rel, GSM48_CAUSE_LOC_USER,
		GSM48_CC_CAUSE_INCOMPAT_DEST);

	return mncc_tx_to_cc(ms, MNCC_REL_REQ, &rel);
}

/*
 * MNCCms call application via socket
 */
int mncc_recv_external(struct osmocom_ms *ms, int msg_type, void *arg)
{
	struct mncc_sock_state *state = ms->mncc_entity.sock_state;
	struct gsm_mncc *mncc = arg;
	struct msgb *msg;
	unsigned char *data;

	if (!state) {
		if (msg_type != MNCC_REL_IND && msg_type != MNCC_REL_CNF) {
			struct gsm_mncc rel;

			/* reject */
			memset(&rel, 0, sizeof(struct gsm_mncc));
			rel.callref = mncc->callref;
			mncc_set_cause(&rel, GSM48_CAUSE_LOC_USER,
				GSM48_CC_CAUSE_TEMP_FAILURE);
			return mncc_tx_to_cc(ms, MNCC_REL_REQ, &rel);
		}
		return 0;
	}

	mncc->msg_type = msg_type;

	msg = msgb_alloc(sizeof(struct gsm_mncc), "MNCC");
	if (!msg)
		return -ENOMEM;

	data = msgb_put(msg, sizeof(struct gsm_mncc));
	memcpy(data, mncc, sizeof(struct gsm_mncc));

	return mncc_sock_from_cc(state, msg);
}

/*
 * MNCCms basic call application
 */

int mncc_recv_internal(struct osmocom_ms *ms, int msg_type, void *arg)
{
	const struct gsm_settings *set = &ms->settings;
	const struct gsm_mncc *data = arg;
	struct gsm_call *call = get_call_ref(ms, data->callref);
	struct gsm_mncc mncc;
	uint8_t cause;
	int first_call = 0;

	/* call does not exist */
	if (!call && msg_type != MNCC_SETUP_IND) {
		LOGP(DMNCC, LOGL_INFO, "Rejecting incoming call "
			"(callref %x)\n", data->callref);
		if (msg_type == MNCC_REL_IND || msg_type == MNCC_REL_CNF)
			return 0;
		cause = GSM48_CC_CAUSE_INCOMPAT_DEST;
		release:
		memset(&mncc, 0, sizeof(struct gsm_mncc));
		mncc.callref = data->callref;
		mncc_set_cause(&mncc, GSM48_CAUSE_LOC_USER, cause);
		return mncc_tx_to_cc(ms, MNCC_REL_REQ, &mncc);
	}

	/* setup without call */
	if (!call) {
		if (llist_empty(&ms->mncc_entity.call_list))
			first_call = 1;
		call = talloc_zero(ms, struct gsm_call);
		if (!call)
			return -ENOMEM;
		call->ms = ms;
		call->callref = data->callref;
		call->type = GSM_CALL_T_UNKNOWN;
		llist_add_tail(&call->entry, &ms->mncc_entity.call_list);
	}

	/* not in initiated state anymore */
	call->init = false;

	switch (msg_type) {
	case MNCC_DISC_IND:
		l23_vty_ms_notify(ms, NULL);
		switch (data->cause.value) {
		case GSM48_CC_CAUSE_UNASSIGNED_NR:
			l23_vty_ms_notify(ms, "Call: Number not assigned\n");
			break;
		case GSM48_CC_CAUSE_NO_ROUTE:
			l23_vty_ms_notify(ms, "Call: Destination unreachable\n");
			break;
		case GSM48_CC_CAUSE_NORM_CALL_CLEAR:
			l23_vty_ms_notify(ms, "Call: Remote hangs up\n");
			break;
		case GSM48_CC_CAUSE_USER_BUSY:
			l23_vty_ms_notify(ms, "Call: Remote busy\n");
			break;
		case GSM48_CC_CAUSE_USER_NOTRESPOND:
			l23_vty_ms_notify(ms, "Call: Remote not responding\n");
			break;
		case GSM48_CC_CAUSE_USER_ALERTING_NA:
			l23_vty_ms_notify(ms, "Call: Remote not answering\n");
			break;
		case GSM48_CC_CAUSE_CALL_REJECTED:
			l23_vty_ms_notify(ms, "Call has been rejected\n");
			break;
		case GSM48_CC_CAUSE_NUMBER_CHANGED:
			l23_vty_ms_notify(ms, "Call: Number changed\n");
			break;
		case GSM48_CC_CAUSE_PRE_EMPTION:
			l23_vty_ms_notify(ms, "Call: Cleared due to pre-emption\n");
			break;
		case GSM48_CC_CAUSE_DEST_OOO:
			l23_vty_ms_notify(ms, "Call: Remote out of order\n");
			break;
		case GSM48_CC_CAUSE_INV_NR_FORMAT:
			l23_vty_ms_notify(ms, "Call: Number invalid or incomplete\n");
			break;
		case GSM48_CC_CAUSE_NO_CIRCUIT_CHAN:
			l23_vty_ms_notify(ms, "Call: No channel available\n");
			break;
		case GSM48_CC_CAUSE_NETWORK_OOO:
			l23_vty_ms_notify(ms, "Call: Network out of order\n");
			break;
		case GSM48_CC_CAUSE_TEMP_FAILURE:
			l23_vty_ms_notify(ms, "Call: Temporary failure\n");
			break;
		case GSM48_CC_CAUSE_SWITCH_CONG:
			l23_vty_ms_notify(ms, "Congestion\n");
			break;
		default:
			l23_vty_ms_notify(ms, "Call has been disconnected "
				"(clear cause %d)\n", data->cause.value);
		}
		LOGP(DMNCC, LOGL_INFO, "Call has been disconnected "
			"(cause %d)\n", data->cause.value);
		if ((data->fields & MNCC_F_PROGRESS)
		 && data->progress.descr == 8) {
			l23_vty_ms_notify(ms, "Please hang up!\n");
			call->call_state = CALL_ST_DISC_RX;
			gui_notify_call(ms);
		 	break;
		}
		free_call(call);
		gui_notify_call(ms);
		cause = GSM48_CC_CAUSE_NORM_CALL_CLEAR;
		goto release;
	case MNCC_REL_IND:
	case MNCC_REL_CNF:
		l23_vty_ms_notify(ms, NULL);
		if (data->cause.value == GSM48_CC_CAUSE_CALL_REJECTED)
			l23_vty_ms_notify(ms, "Call has been rejected\n");
		else
			l23_vty_ms_notify(ms, "Call has been released\n");
		LOGP(DMNCC, LOGL_INFO, "Call has been released (cause %d)\n",
			data->cause.value);
		free_call(call);
		gui_notify_call(ms);
		break;
	case MNCC_CALL_PROC_IND:
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Call is proceeding\n");
		call->call_state = CALL_ST_MO_PROC;
		gui_notify_call(ms);
		LOGP(DMNCC, LOGL_INFO, "Call is proceeding\n");
		if ((data->fields & MNCC_F_BEARER_CAP)
		 && data->bearer_cap.speech_ver[0] >= 0) {
			mncc_get_bearer(set, data->bearer_cap.speech_ver[0]);
		}
		break;
	case MNCC_ALERT_IND:
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Call is alerting\n");
		call->call_state = CALL_ST_MO_ALERT;
		gui_notify_call(ms);
		LOGP(DMNCC, LOGL_INFO, "Call is alerting\n");
		break;
	case MNCC_SETUP_CNF:
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Call is answered\n");
		call->call_state = CALL_ST_ACTIVE;
		gui_notify_call(ms);
		LOGP(DMNCC, LOGL_INFO, "Call is answered\n");
		break;
	case MNCC_SETUP_IND:
		l23_vty_ms_notify(ms, NULL);
		if (!first_call && !ms->settings.cw) {
			l23_vty_ms_notify(ms, "Incoming call rejected while busy\n");
			LOGP(DMNCC, LOGL_INFO, "Incoming call but busy\n");
			cause = GSM48_CC_CAUSE_USER_BUSY;
			goto release;
		}
		/* presentation allowed if present == 0 */
		if (data->calling.present || !data->calling.number[0])
			l23_vty_ms_notify(ms, "Incoming call (anonymous)\n");
		} else if (data->calling.type == 1) {
			l23_vty_ms_notify(ms, "Incoming call (from +%s)\n",
				data->calling.number);
			call->number[0] = '+';
			strncpy(call->number + 1, data->calling.number,
				sizeof(call->number) - 2);
		} else if (data->calling.type == 2) {
			l23_vty_ms_notify(ms, "Incoming call (from 0-%s)\n",
				data->calling.number);
			call->number[0] = '0';
			call->number[1] = '-';
			strncpy(call->number + 2, data->calling.number,
				sizeof(call->number) - 3);
		} else {
			l23_vty_ms_notify(ms, "Incoming call (from %s)\n",
				data->calling.number);
			strncpy(call->number, data->calling.number,
				sizeof(call->number) - 1);
		}
		call->number[sizeof(call->number) - 1] = '\0';
		LOGP(DMNCC, LOGL_INFO, "Incoming call (from %s callref %x)\n",
			call->number, call->callref);
		memset(&mncc, 0, sizeof(struct gsm_mncc));
		mncc.callref = call->callref;
		/* Bearer capability (optional) */
		if (mncc_handle_bcap(&mncc, data, &ms->settings) != 0) {
			cause = GSM48_CC_CAUSE_INCOMPAT_DEST;
			goto release;
		}
		switch (mncc.bearer_cap.transfer) {
		case GSM48_BCAP_ITCAP_SPEECH:
			call->type = GSM_CALL_T_VOICE;
			break;
		case GSM48_BCAP_ITCAP_UNR_DIG_INF:
		case GSM48_BCAP_ITCAP_3k1_AUDIO:
			call->type = GSM_CALL_T_DATA;
			break;
		case GSM48_BCAP_ITCAP_FAX_G3:
			call->type = GSM_CALL_T_DATA_FAX;
			break;
		default:
			call->type = GSM_CALL_T_UNKNOWN;
			break;
		}
		/* CC capabilities (optional) */
		if (ms->settings.cc_dtmf) {
			mncc.fields |= MNCC_F_CCCAP;
			mncc.cccap.dtmf = 1;
		}
		mncc_tx_to_cc(ms, MNCC_CALL_CONF_REQ, &mncc);
		if (first_call) {
			LOGP(DMNCC, LOGL_INFO, "Ring!\n");
			call->call_state = CALL_ST_MT_RING;
		} else {
			LOGP(DMNCC, LOGL_INFO, "Knock!\n");
			call->call_state = CALL_ST_MT_KNOCK;
		}
		update_ringer(call);
		gui_notify_call(ms);
		memset(&mncc, 0, sizeof(struct gsm_mncc));
		mncc.callref = call->callref;
		mncc_tx_to_cc(ms, MNCC_ALERT_REQ, &mncc);
		if (ms->settings.auto_answer) {
			LOGP(DMNCC, LOGL_INFO, "Auto-answering call\n");
			mncc_answer(ms, 0);
		}
		break;
	case MNCC_SETUP_COMPL_IND:
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Call is connected\n");
		LOGP(DMNCC, LOGL_INFO, "Call is connected\n");
		break;
	case MNCC_HOLD_CNF:
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Call is on hold\n");
		LOGP(DMNCC, LOGL_INFO, "Call is on hold\n");
		call->call_state = CALL_ST_HOLD;
		gui_notify_call(ms);
		break;
	case MNCC_HOLD_REJ:
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Call hold was rejected\n");
		LOGP(DMNCC, LOGL_INFO, "Call hold was rejected\n");
		call->call_state = CALL_ST_ACTIVE;
		gui_notify_call(ms);
		break;
	case MNCC_RETRIEVE_CNF:
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Call is retrieved\n");
		LOGP(DMNCC, LOGL_INFO, "Call is retrieved\n");
		call->call_state = CALL_ST_ACTIVE;
		gui_notify_call(ms);
		break;
	case MNCC_RETRIEVE_REJ:
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Call retrieve was rejected\n");
		LOGP(DMNCC, LOGL_INFO, "Call retrieve was rejected\n");
		call->call_state = CALL_ST_HOLD;
		gui_notify_call(ms);
		break;
	case MNCC_FACILITY_IND:
		LOGP(DMNCC, LOGL_INFO, "Facility info not displayed, "
			"unsupported\n");
		break;
	case MNCC_NOTIFY_IND:
		LOGP(DMNCC, LOGL_INFO, "Notify info not displayed, "
			"unsupported\n");
		break;
	case MNCC_START_DTMF_RSP:
	case MNCC_START_DTMF_REJ:
	case MNCC_STOP_DTMF_RSP:
		dtmf_statemachine(call, data);
		break;
	default:
		LOGP(DMNCC, LOGL_INFO, "Message 0x%02x unsupported\n",
			msg_type);
		return -EINVAL;
	}

	return 0;
}

int mncc_call(struct osmocom_ms *ms, const char *number,
	      enum gsm_call_type call_type)
{
	struct gsm_call *call;
	struct gsm_mncc setup;

	llist_for_each_entry(call, &ms->mncc_entity.call_list, entry) {
		if (call->call_state != CALL_ST_HOLD) {
			l23_vty_ms_notify(ms, NULL);
			l23_vty_ms_notify(ms, "Please put active call on hold "
				"first!\n");
			LOGP(DMNCC, LOGL_INFO, "Cannot make a call, busy!\n");
			return -EBUSY;
		}
	}

	call = talloc_zero(ms, struct gsm_call);
	if (!call)
		return -ENOMEM;
	call->ms = ms;
	call->callref = new_callref++;
	call->type = call_type;
	llist_add_tail(&call->entry, &ms->mncc_entity.call_list);

	memset(&setup, 0, sizeof(struct gsm_mncc));
	setup.callref = call->callref;

	if (!strncasecmp(number, "emerg", 5)) {
		LOGP(DMNCC, LOGL_INFO, "Make emergency call\n");
		strcpy(call->number, "emergency");
		/* emergency */
		setup.emergency = 1;
	} else {
		LOGP(DMNCC, LOGL_INFO, "Make %s call to %s\n",
		     gsm_call_type_str[call_type], number);
		strncpy(call->number, number, sizeof(call->number) - 1);
		call->number[sizeof(call->number) - 1] = '\0';
		/* called number */
		setup.fields |= MNCC_F_CALLED;
		if (number[0] == '+') {
			number++;
			setup.called.type = 1; /* international */
		} else
			setup.called.type = 0; /* auto/unknown - prefix must be
						  used */
		setup.called.plan = 1; /* ISDN */
		OSMO_STRLCPY_ARRAY(setup.called.number, number);

		/* bearer capability (mandatory) */
		if (call_type == GSM_CALL_T_VOICE)
			mncc_set_bcap_speech(&setup, &ms->settings, -1);
		else
			mncc_set_bcap_data(&setup, &ms->settings, call_type);

		/* CLIR */
		if (ms->settings.clir)
			setup.clir.inv = 1;
		else if (ms->settings.clip)
			setup.clir.sup = 1;

		/* CC capabilities (optional) */
		if (ms->settings.cc_dtmf) {
			setup.fields |= MNCC_F_CCCAP;
			setup.cccap.dtmf = 1;
		}
	}
	call->call_state = CALL_ST_MO_INIT;
	gui_notify_call(ms);

	return mncc_tx_to_cc(ms, MNCC_SETUP_REQ, &setup);
}

int mncc_hangup(struct osmocom_ms *ms, int index)
{
	struct gsm_call *search, *first = NULL, *call = NULL;
	int calls = 0;
	struct gsm_mncc disc;

	llist_for_each_entry(search, &ms->mncc_entity.call_list, entry) {
		calls++;
		if (calls == 1)
			first = search;
		if (calls == index)
			call = search;
	}
	if (calls == 0) {
		LOGP(DMNCC, LOGL_INFO, "No active call to hangup\n");
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "No active call\n");
		return -EINVAL;
	}
	if (calls == 1 && index == 0)
		call = first;
	if (calls > 1 && index == 0) {
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Select call 1..%d\n", calls);
		return -EINVAL;
	}
	if (!call) {
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Given number %d out of range!\n", index);
		l23_vty_ms_notify(ms, "Select call 1..%d\n", calls);
		return -EINVAL;
	}
		
	call->call_state = CALL_ST_DISC_TX;
	gui_notify_call(ms);

	memset(&disc, 0, sizeof(struct gsm_mncc));
	disc.callref = call->callref;
	mncc_set_cause(&disc, GSM48_CAUSE_LOC_USER,
		GSM48_CC_CAUSE_NORM_CALL_CLEAR);
	return mncc_tx_to_cc(ms, (call->call_state == CALL_ST_MO_INIT) ?
					MNCC_REL_REQ : MNCC_DISC_REQ, &disc);
}

int mncc_answer(struct osmocom_ms *ms, int index)
{
	struct gsm_call *search, *first = NULL, *call = NULL;
	int calls = 0, alerting = 0, active = 0;
	struct gsm_mncc rsp;

	llist_for_each_entry(search, &ms->mncc_entity.call_list, entry) {
		calls++;
		if (search->call_state == CALL_ST_MT_RING
		 || search->call_state == CALL_ST_MT_KNOCK) {
		 	alerting++;
			if (alerting == 1)
				first = search;
			if (calls == index)
				call = search;
		} else
		if (search->call_state != CALL_ST_HOLD)
			active = calls;
	}
	if (active) {
		LOGP(DMNCC, LOGL_INFO, "Answer but we have an active call\n");
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Please put active call %d on hold first!\n",
			active);
		return -EBUSY;
	}
	if (alerting == 0) {
		LOGP(DMNCC, LOGL_INFO, "No call alerting\n");
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "No alerting call\n");
		return -EBUSY;
	}
	if (alerting == 1 && index == 0)
		call = first;
	if (alerting > 1 && index == 0) {
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Select call 1..%d\n", calls);
		return -EINVAL;
	}
	if (!call) {
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Given number %d out of range!\n", index);
		l23_vty_ms_notify(ms, "Select call 1..%d\n", calls);
		return -EINVAL;
	}
	call->call_state = CALL_ST_ACTIVE;
	update_ringer(call);
	gui_notify_call(ms);

	memset(&rsp, 0, sizeof(struct gsm_mncc));
	rsp.callref = call->callref;
	return mncc_tx_to_cc(ms, MNCC_SETUP_RSP, &rsp);
}

int mncc_hold(struct osmocom_ms *ms, int index)
{
	struct gsm_call *search, *first = NULL, *call = NULL;
	int calls = 0, active = 0;
	struct gsm_mncc hold;

	/* normally the selection should not happen, because only one call can
	 * be active.
	 */
	llist_for_each_entry(search, &ms->mncc_entity.call_list, entry) {
		calls++;
		if (search->call_state == CALL_ST_ACTIVE) {
		 	active++;
			if (active == 1)
				first = search;
			if (calls == index)
				call = search;
		}
	}
	if (active == 0) {
		LOGP(DMNCC, LOGL_INFO, "No active call to hold\n");
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "No active call\n");
		return -EBUSY;
	}
	if (active == 1 && index == 0)
		call = first;
	if (active > 1 && index == 0) {
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Select call 1..%d\n", calls);
		return -EINVAL;
	}
	if (!call) {
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Given number %d out of range!\n", index);
		l23_vty_ms_notify(ms, "Select call 1..%d\n", calls);
		return -EINVAL;
	}

	memset(&hold, 0, sizeof(struct gsm_mncc));
	hold.callref = call->callref;
	return mncc_tx_to_cc(ms, MNCC_HOLD_REQ, &hold);
}

int mncc_retrieve(struct osmocom_ms *ms, int index)
{
	struct gsm_call *search, *first = NULL, *call = NULL;
	int calls = 0, hold = 0, active = 0;
	struct gsm_mncc retr;

	llist_for_each_entry(search, &ms->mncc_entity.call_list, entry) {
		calls++;
		if (search->call_state == CALL_ST_HOLD) {
		 	hold++;
			if (hold == 1)
				first = search;
			if (calls == index)
				call = search;
		} else
		if (search->call_state != CALL_ST_MT_KNOCK)
			active = calls;
	}
	if (active) {
		LOGP(DMNCC, LOGL_INFO, "Cannot retrieve during active call\n");
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Hold or release active call first!\n");
		return -EINVAL;
	}
	if (hold == 0) {
		LOGP(DMNCC, LOGL_INFO, "No call to hold\n");
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "No call on hold!\n");
		return -EINVAL;
	}
	if (hold == 1 && index == 0)
		call = first;
	if (hold > 1 && index == 0) {
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Select call 1..%d\n", calls);
		return -EINVAL;
	}
	if (!call) {
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Given number %d out of range!\n", index);
		l23_vty_ms_notify(ms, "Select call 1..%d\n", calls);
		return -EINVAL;
	}

	memset(&retr, 0, sizeof(struct gsm_mncc));
	retr.callref = call->callref;
	return mncc_tx_to_cc(ms, MNCC_RETRIEVE_REQ, &retr);
}

/*
 * DTMF
 */

static int dtmf_statemachine(struct gsm_call *call,
			     const struct gsm_mncc *mncc)
{
	struct osmocom_ms *ms = call->ms;
	struct gsm_mncc dtmf;

	switch (call->dtmf_state) {
	case DTMF_ST_SPACE:
	case DTMF_ST_IDLE:
		/* end of string */
		if (!call->dtmf[call->dtmf_index]) {
			LOGP(DMNCC, LOGL_INFO, "done with DTMF\n");
			call->dtmf_state = DTMF_ST_IDLE;
			return -EOF;
		}
		memset(&dtmf, 0, sizeof(struct gsm_mncc));
		dtmf.callref = call->callref;
		dtmf.keypad = call->dtmf[call->dtmf_index++];
		call->dtmf_state = DTMF_ST_START;
		LOGP(DMNCC, LOGL_INFO, "start DTMF (keypad %c)\n",
			dtmf.keypad);
		return mncc_tx_to_cc(ms, MNCC_START_DTMF_REQ, &dtmf);
	case DTMF_ST_START:
		if (mncc->msg_type != MNCC_START_DTMF_RSP) {
			LOGP(DMNCC, LOGL_INFO, "DTMF was rejected\n");
			return -ENOTSUP;
		}
		start_dtmf_timer(call, 70);
		call->dtmf_state = DTMF_ST_MARK;
		LOGP(DMNCC, LOGL_INFO, "DTMF is on\n");
		break;
	case DTMF_ST_MARK:
		memset(&dtmf, 0, sizeof(struct gsm_mncc));
		dtmf.callref = call->callref;
		call->dtmf_state = DTMF_ST_STOP;
		LOGP(DMNCC, LOGL_INFO, "stop DTMF\n");
		return mncc_tx_to_cc(ms, MNCC_STOP_DTMF_REQ, &dtmf);
	case DTMF_ST_STOP:
		start_dtmf_timer(call, 120);
		call->dtmf_state = DTMF_ST_SPACE;
		LOGP(DMNCC, LOGL_INFO, "DTMF is off\n");
		break;
	}

	return 0;
}

static void timeout_dtmf(void *arg)
{
	struct gsm_call *call = arg;

	LOGP(DCC, LOGL_INFO, "DTMF timer has fired\n");
	dtmf_statemachine(call, NULL);
}

int mncc_dtmf(struct osmocom_ms *ms, int index, char *dtmf)
{
	struct gsm_call *search, *first = NULL, *call = NULL;
	int calls = 0, active = 0;

	/* normally the selection should not happen, because only one call can
	 * be active.
	 */
	llist_for_each_entry(search, &ms->mncc_entity.call_list, entry) {
		calls++;
		if (search->call_state == CALL_ST_ACTIVE) {
		 	active++;
			if (active == 1)
				first = search;
			if (calls == index)
				call = search;
		}
	}
	if (active == 0) {
		LOGP(DMNCC, LOGL_INFO, "No active call to send DTMF\n");
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "No active call\n");
		return -EBUSY;
	}
	if (active == 1 && index == 0)
		call = first;
	if (active > 1 && index == 0) {
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Select call 1..%d\n", calls);
		return -EINVAL;
	}
	if (!call) {
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Given number %d out of range!\n", index);
		l23_vty_ms_notify(ms, "Select call 1..%d\n", calls);
		return -EINVAL;
	}

	if (call->dtmf_state != DTMF_ST_IDLE) {
		LOGP(DMNCC, LOGL_INFO, "sending DTMF already\n");
		return -EINVAL;
	}

	call->dtmf_index = 0;
	OSMO_STRLCPY_ARRAY(call->dtmf, dtmf);
	return dtmf_statemachine(call, NULL);
}

int mncc_list(struct osmocom_ms *ms)
{
	struct gsm_call *call;
	int calls = 0;
	const char *state;

	l23_vty_ms_notify(ms, NULL);
	llist_for_each_entry(call, &ms->mncc_entity.call_list, entry) {
		calls++;
		switch (call->call_state) {
		case CALL_ST_MO_INIT:
			state = "Dialing";
			break;
		case CALL_ST_MO_PROC:
			state = "Proceeding";
			break;
		case CALL_ST_MO_ALERT:
			state = "Ringing";
			break;
		case CALL_ST_MT_RING:
			state = "Incomming";
			break;
		case CALL_ST_MT_KNOCK:
			state = "Knocking";
			break;
		case CALL_ST_ACTIVE:
			state = "Connected";
			break;
		case CALL_ST_HOLD:
			state = "On Hold";
			break;
		case CALL_ST_DISC_TX:
			state = "Releasing";
			break;
		case CALL_ST_DISC_RX:
			state = "Hung Up";
			break;
		default:
			continue;
		}
		if (call->number[0])
			l23_vty_ms_notify(ms, "%s (%s)\n", state, call->number);
		else
			l23_vty_ms_notify(ms, "%s\n", state);

	}
	if (calls == 0)
		l23_vty_ms_notify(ms, "No call\n");

	return 0;
}

/*
 * init / exit
 */

int mnccms_init(struct osmocom_ms *ms)
{
	INIT_LLIST_HEAD(&ms->mncc_entity.call_list);

	return 0;
}

void mnccms_exit(struct osmocom_ms *ms)
{
	struct gsm_call *c, *c2;

	llist_for_each_entry_safe(c, c2, &ms->mncc_entity.call_list, entry)
		free_call(c);
}

