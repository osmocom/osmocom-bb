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

#include <stdint.h>
#include <errno.h>
#include <arpa/inet.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/utils.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/l1ctl.h>

extern void *l23_ctx;
static int sim_process_job(struct osmocom_ms *ms);

/*
 * support
 */

uint32_t new_handle = 1;

static struct gsm1111_df_name {
	uint16_t file;
	const char *name;
} gsm1111_df_name[] = {
	{ 0x3f00, "MF" },
	{ 0x7f20, "DFgsm" },
	{ 0x7f10, "DFtelecom" },
	{ 0x7f22, "DFis-41" },
	{ 0x7f23, "DFfp-cts" },
	{ 0x5f50, "DFgraphics" },
	{ 0x5f30, "DFiridium" },
	{ 0x5f31, "DFglobst" },
	{ 0x5f32, "DFico" },
	{ 0x5f33, "DFaces" },
	{ 0x5f40, "DFeia/tia-553" },
	{ 0x5f60, "DFcts" },
	{ 0x5f70, "DFsolsa" },
	{ 0x5f3c, "DFmexe" },
	{ 0, NULL }
};

static const char *get_df_name(uint16_t fid)
{
	int i;
	static char text[7];

	for (i = 0; gsm1111_df_name[i].file; i++)
		if (gsm1111_df_name[i].file == fid)
			break;
	if (gsm1111_df_name[i].file)
		return gsm1111_df_name[i].name;

	sprintf(text, "0x%04x", fid);
	return text;
}

static struct gsm_sim_handler *sim_get_handler(struct gsm_sim *sim,
	uint32_t handle)
{
	struct gsm_sim_handler *handler;

	llist_for_each_entry(handler, &sim->handlers, entry)
		if (handler->handle == handle)
			return handler;

	return NULL;
}

/*
 * messages
 */

static const struct value_string sim_job_names[] = {
	{ SIM_JOB_READ_BINARY,		"SIM_JOB_READ_BINARY" },
	{ SIM_JOB_UPDATE_BINARY,	"SIM_JOB_UPDATE_BINARY" },
	{ SIM_JOB_READ_RECORD,		"SIM_JOB_READ_RECORD" },
	{ SIM_JOB_UPDATE_RECORD,	"SIM_JOB_UPDATE_RECORD" },
	{ SIM_JOB_SEEK_RECORD,		"SIM_JOB_SEEK_RECORD" },
	{ SIM_JOB_INCREASE,		"SIM_JOB_INCREASE" },
	{ SIM_JOB_INVALIDATE,		"SIM_JOB_INVALIDATE" },
	{ SIM_JOB_REHABILITATE,		"SIM_JOB_REHABILITATE" },
	{ SIM_JOB_RUN_GSM_ALGO,		"SIM_JOB_RUN_GSM_ALGO" },
	{ SIM_JOB_PIN1_UNLOCK,		"SIM_JOB_PIN1_UNLOCK" },
	{ SIM_JOB_PIN1_CHANGE,		"SIM_JOB_PIN1_CHANGE" },
	{ SIM_JOB_PIN1_DISABLE,		"SIM_JOB_PIN1_DISABLE" },
	{ SIM_JOB_PIN1_ENABLE,		"SIM_JOB_PIN1_ENABLE" },
	{ SIM_JOB_PIN1_UNBLOCK,		"SIM_JOB_PIN1_UNBLOCK" },
	{ SIM_JOB_PIN2_UNLOCK,		"SIM_JOB_PIN2_UNLOCK" },
	{ SIM_JOB_PIN2_CHANGE,		"SIM_JOB_PIN2_CHANGE" },
	{ SIM_JOB_PIN2_UNBLOCK,		"SIM_JOB_PIN2_UNBLOCK" },
	{ SIM_JOB_OK,			"SIM_JOB_OK" },
	{ SIM_JOB_ERROR,		"SIM_JOB_ERROR" },
	{ 0,				NULL }
};

static const char *get_job_name(int value)
{
	return get_value_string(sim_job_names, value);
}

/* allocate sim client message (upper layer) */
struct msgb *gsm_sim_msgb_alloc(uint32_t handle, uint8_t job_type)
{
	struct msgb *msg;
	struct sim_hdr *nsh;

	msg = msgb_alloc_headroom(SIM_ALLOC_SIZE+SIM_ALLOC_HEADROOM,
		SIM_ALLOC_HEADROOM, "SIM");
	if (!msg)
		return NULL;

	nsh = (struct sim_hdr *) msgb_put(msg, sizeof(*nsh));
	nsh->handle = handle;
	nsh->job_type = job_type;

	return msg;
}

/* reply to job, after it is done. reuse the msgb in the job */
void gsm_sim_reply(struct osmocom_ms *ms, uint8_t result_type, uint8_t *result,
	uint16_t result_len)
{
	struct gsm_sim *sim = &ms->sim;
	struct msgb *msg = sim->job_msg;
	struct sim_hdr *sh;
	uint8_t *payload;
	uint16_t payload_len;
	struct gsm_sim_handler *handler;

	LOGP(DSIM, LOGL_INFO, "sending result to callback function "
		"(type=%d)\n", result_type);

	/* if no handler, or no callback, just free the job */
	sh = (struct sim_hdr *)msg->data;
	handler = sim_get_handler(sim, sh->handle);
	if (!handler || !handler->cb) {
		LOGP(DSIM, LOGL_INFO, "no callback or no handler, "
			"dropping result\n");
		msgb_free(sim->job_msg);
		sim->job_msg = NULL;
		sim->job_state = SIM_JST_IDLE;
		return;
	}

	payload = msg->data + sizeof(*sh);
	payload_len = msg->len - sizeof(*sh);

	/* remove data */
	msg->tail -= payload_len;
	msg->len -= payload_len;

	/* add reply data */
	sh->job_type = result_type;
	if (result_len)
		memcpy(msgb_put(msg, result_len), result, result_len);

	/* callback */
	sim->job_state = SIM_JST_IDLE;
	sim->job_msg = NULL;
	handler->cb(ms, msg);
}

/* send APDU to card reader */
static int sim_apdu_send(struct osmocom_ms *ms, uint8_t *data, uint16_t length)
{
	LOGP(DSIM, LOGL_INFO, "sending APDU (class 0x%02x, ins 0x%02x)\n",
		data[0], data[1]);

	/* adding SAP client support
	 * it makes more sense to do it here then in L1CTL */
	if (ms->subscr.sim_type == GSM_SIM_TYPE_SAP) {
		LOGP(DSIM, LOGL_INFO, "Using SAP backend\n");
		osmosap_send_apdu(ms, data, length);
	} else {
		LOGP(DSIM, LOGL_INFO, "Using built-in SIM reader\n");
		l1ctl_tx_sim_req(ms, data, length);
	}

	return 0;
}

/* dequeue messages (RSL-SAP) */
int gsm_sim_job_dequeue(struct osmocom_ms *ms)
{
	struct gsm_sim *sim = &ms->sim;
	struct sim_hdr *sh;
	struct msgb *msg;
	struct gsm_sim_handler *handler;

	/* already have a job */
	if (sim->job_msg)
		return 0;

	/* get next job */
	while ((msg = msgb_dequeue(&sim->jobs))) {
		/* resolve handler */
		sh = (struct sim_hdr *) msg->data;
		LOGP(DSIM, LOGL_INFO, "got new job: %s (handle=%08x)\n",
			get_job_name(sh->job_type), sh->handle);
		handler = sim_get_handler(sim, sh->handle);
		if (!handler) {
			LOGP(DSIM, LOGL_INFO, "no handler, ignoring job\n");
			/* does not exist anymore */
			msgb_free(msg);
			continue;
		}

		/* init job */
		sim->job_state = SIM_JST_IDLE;
		sim->job_msg = msg;
		sim->job_handle = sh->handle;

		/* process current job, message is freed there */
		sim_process_job(ms);
		return 1; /* work done */
	}
	
	return 0;
}


/*
 * SIM commands
 */

/* 9.2.1 */
static int gsm1111_tx_select(struct osmocom_ms *ms, uint16_t fid)
{
	uint8_t buffer[5 + 2];

	LOGP(DSIM, LOGL_INFO, "SELECT (file=0x%04x)\n", fid);
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_SELECT;
	buffer[2] = 0x00;
	buffer[3] = 0x00;
	buffer[4] = 2;
	buffer[5] = fid >> 8;
	buffer[6] = fid;

	return sim_apdu_send(ms, buffer, 5 + 2);
}

#if 0
/* 9.2.2 */
static int gsm1111_tx_status(struct osmocom_ms *ms)
{
	uint8_t buffer[5];

	LOGP(DSIM, LOGL_INFO, "STATUS\n");
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_STATUS;
	buffer[2] = 0x00;
	buffer[3] = 0x00;
	buffer[4] = 0;

	return sim_apdu_send(ms, buffer, 5);
}
#endif

/* 9.2.3 */
static int gsm1111_tx_read_binary(struct osmocom_ms *ms, uint16_t offset,
	uint8_t length)
{
	uint8_t buffer[5];

	LOGP(DSIM, LOGL_INFO, "READ BINARY (offset=%d len=%d)\n", offset,
		length);
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_READ_BINARY;
	buffer[2] = offset >> 8;
	buffer[3] = offset;
	buffer[4] = length;

	return sim_apdu_send(ms, buffer, 5);
}

/* 9.2.4 */
static int gsm1111_tx_update_binary(struct osmocom_ms *ms, uint16_t offset,
	uint8_t *data, uint8_t length)
{
	uint8_t buffer[5 + length];

	LOGP(DSIM, LOGL_INFO, "UPDATE BINARY (offset=%d len=%d)\n", offset,
		length);
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_UPDATE_BINARY;
	buffer[2] = offset >> 8;
	buffer[3] = offset;
	buffer[4] = length;
	memcpy(buffer + 5, data, length);

	return sim_apdu_send(ms, buffer, 5 + length);
}

/* 9.2.5 */
static int gsm1111_tx_read_record(struct osmocom_ms *ms, uint8_t rec_no,
	uint8_t mode, uint8_t length)
{
	uint8_t buffer[5];

	LOGP(DSIM, LOGL_INFO, "READ RECORD (rec_no=%d mode=%d len=%d)\n",
		rec_no, mode, length);
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_READ_RECORD;
	buffer[2] = rec_no;
	buffer[3] = mode;
	buffer[4] = length;

	return sim_apdu_send(ms, buffer, 5);
}

/* 9.2.6 */
static int gsm1111_tx_update_record(struct osmocom_ms *ms, uint8_t rec_no,
	uint8_t mode, uint8_t *data, uint8_t length)
{
	uint8_t buffer[5 + length];

	LOGP(DSIM, LOGL_INFO, "UPDATE RECORD (rec_no=%d mode=%d len=%d)\n",
		rec_no, mode, length);
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_UPDATE_RECORD;
	buffer[2] = rec_no;
	buffer[3] = mode;
	buffer[4] = length;
	memcpy(buffer + 5, data, length);

	return sim_apdu_send(ms, buffer, 5 + length);
}

/* 9.2.7 */
static int gsm1111_tx_seek(struct osmocom_ms *ms, uint8_t type_mode,
	uint8_t *pattern, uint8_t length)
{
	uint8_t buffer[5 + length];
	uint8_t type = type_mode >> 4;
	uint8_t mode = type_mode & 0x0f;

	LOGP(DSIM, LOGL_INFO, "SEEK (type=%d mode=%d len=%d)\n", type, mode,
		length);
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_SEEK;
	buffer[2] = 0x00;
	buffer[3] = type_mode;
	buffer[4] = length;
	memcpy(buffer + 5, pattern, length);

	return sim_apdu_send(ms, buffer, 5 + length);
}

/* 9.2.8 */
static int gsm1111_tx_increase(struct osmocom_ms *ms, uint32_t value)
{
	uint8_t buffer[5 + 3];

	LOGP(DSIM, LOGL_INFO, "INCREASE (value=%d)\n", value);
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_INCREASE;
	buffer[2] = 0x00;
	buffer[3] = 0x00;
	buffer[4] = 3;
	buffer[5] = value >> 16;
	buffer[6] = value >> 8;
	buffer[7] = value;

	return sim_apdu_send(ms, buffer, 5 + 3);
}

/* 9.2.9 */
static int gsm1111_tx_verify_chv(struct osmocom_ms *ms, uint8_t chv_no,
	uint8_t *chv, uint8_t length)
{
	uint8_t buffer[5 + 8];
	int i;

	LOGP(DSIM, LOGL_INFO, "VERIFY CHV (CHV%d)\n", chv_no);
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_VERIFY_CHV;
	buffer[2] = 0x00;
	buffer[3] = chv_no;
	buffer[4] = 8;
	for (i = 0; i < 8; i++) {
		if (i < length)
			buffer[5 + i] = chv[i];
		else
			buffer[5 + i] = 0xff;
	}

	return sim_apdu_send(ms, buffer, 5 + 8);
}

/* 9.2.10 */
static int gsm1111_tx_change_chv(struct osmocom_ms *ms, uint8_t chv_no,
	uint8_t *chv_old, uint8_t length_old, uint8_t *chv_new,
	uint8_t length_new)
{
	uint8_t buffer[5 + 16];
	int i;

	LOGP(DSIM, LOGL_INFO, "CHANGE CHV (CHV%d)\n", chv_no);
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_CHANGE_CHV;
	buffer[2] = 0x00;
	buffer[3] = chv_no;
	buffer[4] = 16;
	for (i = 0; i < 8; i++) {
		if (i < length_old)
			buffer[5 + i] = chv_old[i];
		else
			buffer[5 + i] = 0xff;
		if (i < length_new)
			buffer[13 + i] = chv_new[i];
		else
			buffer[13 + i] = 0xff;
	}

	return sim_apdu_send(ms, buffer, 5 + 16);
}

/* 9.2.11 */
static int gsm1111_tx_disable_chv(struct osmocom_ms *ms, uint8_t *chv,
	uint8_t length)
{
	uint8_t buffer[5 + 8];
	int i;

	LOGP(DSIM, LOGL_INFO, "DISABLE CHV (CHV1)\n");
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_DISABLE_CHV;
	buffer[2] = 0x00;
	buffer[3] = 0x01;
	buffer[4] = 8;
	for (i = 0; i < 8; i++) {
		if (i < length)
			buffer[5 + i] = chv[i];
		else
			buffer[5 + i] = 0xff;
	}

	return sim_apdu_send(ms, buffer, 5 + 8);
}

/* 9.2.12 */
static int gsm1111_tx_enable_chv(struct osmocom_ms *ms, uint8_t *chv,
	uint8_t length)
{
	uint8_t buffer[5 + 8];
	int i;

	LOGP(DSIM, LOGL_INFO, "ENABLE CHV (CHV1)\n");
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_ENABLE_CHV;
	buffer[2] = 0x00;
	buffer[3] = 0x01;
	buffer[4] = 8;
	for (i = 0; i < 8; i++) {
		if (i < length)
			buffer[5 + i] = chv[i];
		else
			buffer[5 + i] = 0xff;
	}

	return sim_apdu_send(ms, buffer, 5 + 8);
}

/* 9.2.13 */
static int gsm1111_tx_unblock_chv(struct osmocom_ms *ms, uint8_t chv_no,
	uint8_t *chv_unblk, uint8_t length_unblk, uint8_t *chv_new,
	uint8_t length_new)
{
	uint8_t buffer[5 + 16];
	int i;

	LOGP(DSIM, LOGL_INFO, "UNBLOCK CHV (CHV%d)\n", (chv_no == 2) ? 2 : 1);
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_UNBLOCK_CHV;
	buffer[2] = 0x00;
	buffer[3] = (chv_no == 1) ? 0 : chv_no;
	buffer[4] = 16;
	for (i = 0; i < 8; i++) {
		if (i < length_unblk)
			buffer[5 + i] = chv_unblk[i];
		else
			buffer[5 + i] = 0xff;
		if (i < length_new)
			buffer[13 + i] = chv_new[i];
		else
			buffer[13 + i] = 0xff;
	}

	return sim_apdu_send(ms, buffer, 5 + 16);
}

/* 9.2.14 */
static int gsm1111_tx_invalidate(struct osmocom_ms *ms)
{
	uint8_t buffer[5];

	LOGP(DSIM, LOGL_INFO, "INVALIDATE\n");
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_INVALIDATE;
	buffer[2] = 0x00;
	buffer[3] = 0x00;
	buffer[4] = 0;

	return sim_apdu_send(ms, buffer, 5);
}

/* 9.2.15 */
static int gsm1111_tx_rehabilitate(struct osmocom_ms *ms)
{
	uint8_t buffer[5];

	LOGP(DSIM, LOGL_INFO, "REHABILITATE\n");
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_REHABLILITATE;
	buffer[2] = 0x00;
	buffer[3] = 0x00;
	buffer[4] = 0;

	return sim_apdu_send(ms, buffer, 5);
}

/* 9.2.16 */
static int gsm1111_tx_run_gsm_algo(struct osmocom_ms *ms, uint8_t *rand)
{
	uint8_t buffer[5 + 16];

	LOGP(DSIM, LOGL_INFO, "RUN GSM ALGORITHM\n");
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_RUN_GSM_ALGO;
	buffer[2] = 0x00;
	buffer[3] = 0x00;
	buffer[4] = 16;
	memcpy(buffer + 5, rand, 16);

	return sim_apdu_send(ms, buffer, 5 + 16);
}

#if 0
/* 9.2.17 */
static int gsm1111_tx_sleep(struct osmocom_ms *ms)
{
	uint8_t buffer[5];

	LOGP(DSIM, LOGL_INFO, "\n");
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_SLEEP;
	buffer[2] = 0x00;
	buffer[3] = 0x00;
	buffer[4] = 0;

	return sim_apdu_send(ms, buffer, 5);
}
#endif

/* 9.2.18 */
static int gsm1111_tx_get_response(struct osmocom_ms *ms, uint8_t length)
{
	uint8_t buffer[5];

	LOGP(DSIM, LOGL_INFO, "GET RESPONSE (len=%d)\n", length);
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_GET_RESPONSE;
	buffer[2] = 0x00;
	buffer[3] = 0x00;
	buffer[4] = length;

	return sim_apdu_send(ms, buffer, 5);
}

#if 0
/* 9.2.19 */
static int gsm1111_tx_terminal_profile(struct osmocom_ms *ms, uint8_t *data,
	uint8_t length)
{
	uint8_t buffer[5 + length];

	LOGP(DSIM, LOGL_INFO, "TERMINAL PROFILE (len=%d)\n", length);
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_TERMINAL_PROFILE;
	buffer[2] = 0x00;
	buffer[3] = 0x00;
	buffer[4] = length;
	memcpy(buffer + 5, data, length);

	return sim_apdu_send(ms, buffer, 5 + length);
}

/* 9.2.20 */
static int gsm1111_tx_envelope(struct osmocom_ms *ms, uint8_t *data,
	uint8_t length)
{
	uint8_t buffer[5 + length];

	LOGP(DSIM, LOGL_INFO, "ENVELOPE (len=%d)\n", length);
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_ENVELOPE;
	buffer[2] = 0x00;
	buffer[3] = 0x00;
	buffer[4] = length;
	memcpy(buffer + 5, data, length);

	return sim_apdu_send(ms, buffer, 5 + length);
}

/* 9.2.21 */
static int gsm1111_tx_fetch(struct osmocom_ms *ms, uint8_t length)
{
	uint8_t buffer[5];

	LOGP(DSIM, LOGL_INFO, "FETCH (len=%d)\n", length);
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_FETCH;
	buffer[2] = 0x00;
	buffer[3] = 0x00;
	buffer[4] = length;

	return sim_apdu_send(ms, buffer, 5);
}

/* 9.2.22 */
static int gsm1111_tx_terminal_response(struct osmocom_ms *ms, uint8_t *data,
	uint8_t length)
{
	uint8_t buffer[5 + length];

	LOGP(DSIM, LOGL_INFO, "TERMINAL RESPONSE (len=%d)\n", length);
	buffer[0] = GSM1111_CLASS_GSM;
	buffer[1] = GSM1111_INST_TERMINAL_RESPONSE;
	buffer[2] = 0x00;
	buffer[3] = 0x00;
	buffer[4] = length;
	memcpy(buffer + 5, data, length);

	return sim_apdu_send(ms, buffer, 5 + length);
}
#endif

/*
 * SIM state machine
 */

/* process job */
static int sim_process_job(struct osmocom_ms *ms)
{
	struct gsm_sim *sim = &ms->sim;
	uint8_t *payload, *payload2;
	uint16_t payload_len, payload_len2;
	struct sim_hdr *sh;
	uint8_t cause;
	int i;

	/* no current */
	if (!sim->job_msg)
		return 0;

	sh = (struct sim_hdr *)sim->job_msg->data;
	payload = sim->job_msg->data + sizeof(*sh);
	payload_len = sim->job_msg->len - sizeof(*sh);

	/* do reset before sim reading */
	if (!sim->reset) {
		sim->reset = 1;
		// FIXME: send reset command to L1
	}

	/* navigate to right DF */
	switch (sh->job_type) {
	case SIM_JOB_READ_BINARY:
	case SIM_JOB_UPDATE_BINARY:
	case SIM_JOB_READ_RECORD:
	case SIM_JOB_UPDATE_RECORD:
	case SIM_JOB_SEEK_RECORD:
	case SIM_JOB_INCREASE:
	case SIM_JOB_INVALIDATE:
	case SIM_JOB_REHABILITATE:
	case SIM_JOB_RUN_GSM_ALGO:
		/* check MF / DF */
		i = 0;
		while (sh->path[i] && sim->path[i]) {
			if (sh->path[i] != sim->path[i])
				break;
			i++;
		}
		/* if path in message is shorter or if paths are different */
		if (sim->path[i]) {
			LOGP(DSIM, LOGL_INFO, "go MF\n");
			sim->job_state = SIM_JST_SELECT_MFDF;
			/* go MF */
			sim->path[0] = 0;
			return gsm1111_tx_select(ms, 0x3f00);
		}
		/* if path in message is longer */
		if (sh->path[i]) {
			LOGP(DSIM, LOGL_INFO, "requested path is longer, go "
				"child %s\n", get_df_name(sh->path[i]));
			sim->job_state = SIM_JST_SELECT_MFDF;
			/* select child */
			sim->path[i] = sh->path[i];
			sim->path[i + 1] = 0;
			return gsm1111_tx_select(ms, sh->path[i]);
		}
		/* if paths are equal, continue */
	}

	/* set state and trigger SIM process */
	switch (sh->job_type) {
	case SIM_JOB_READ_BINARY:
	case SIM_JOB_UPDATE_BINARY:
	case SIM_JOB_READ_RECORD:
	case SIM_JOB_UPDATE_RECORD:
	case SIM_JOB_SEEK_RECORD:
	case SIM_JOB_INCREASE:
	case SIM_JOB_INVALIDATE:
	case SIM_JOB_REHABILITATE:
		sim->job_state = SIM_JST_SELECT_EF;
		sim->file = sh->file;
		return gsm1111_tx_select(ms, sh->file);
	case SIM_JOB_RUN_GSM_ALGO:
		if (payload_len != 16) {
			LOGP(DSIM, LOGL_ERROR, "random not 16 bytes\n");
			break;
		}
		sim->job_state = SIM_JST_RUN_GSM_ALGO;
		return gsm1111_tx_run_gsm_algo(ms, payload);
	case SIM_JOB_PIN1_UNLOCK:
		payload_len = strlen((char *)payload);
		if (payload_len < 4 || payload_len > 8) {
			LOGP(DSIM, LOGL_ERROR, "key not in range 4..8\n");
			break;
		}
		sim->job_state = SIM_JST_PIN1_UNLOCK;
		return gsm1111_tx_verify_chv(ms, 0x01, payload, payload_len);
	case SIM_JOB_PIN2_UNLOCK:
		payload_len = strlen((char *)payload);
		if (payload_len < 4 || payload_len > 8) {
			LOGP(DSIM, LOGL_ERROR, "key not in range 4..8\n");
			break;
		}
		sim->job_state = SIM_JST_PIN2_UNLOCK;
		return gsm1111_tx_verify_chv(ms, 0x02, payload, payload_len);
	case SIM_JOB_PIN1_CHANGE:
		payload_len = strlen((char *)payload);
		payload2 = payload + payload_len + 1;
		payload_len2 = strlen((char *)payload2);
		if (payload_len < 4 || payload_len > 8) {
			LOGP(DSIM, LOGL_ERROR, "key1 not in range 4..8\n");
			break;
		}
		if (payload_len2 < 4 || payload_len2 > 8) {
			LOGP(DSIM, LOGL_ERROR, "key2 not in range 4..8\n");
			break;
		}
		sim->job_state = SIM_JST_PIN1_CHANGE;
		return gsm1111_tx_change_chv(ms, 0x01, payload, payload_len,
			payload2, payload_len2);
	case SIM_JOB_PIN2_CHANGE:
		payload_len = strlen((char *)payload);
		payload2 = payload + payload_len + 1;
		payload_len2 = strlen((char *)payload2);
		if (payload_len < 4 || payload_len > 8) {
			LOGP(DSIM, LOGL_ERROR, "key1 not in range 4..8\n");
			break;
		}
		if (payload_len2 < 4 || payload_len2 > 8) {
			LOGP(DSIM, LOGL_ERROR, "key2 not in range 4..8\n");
			break;
		}
		sim->job_state = SIM_JST_PIN2_CHANGE;
		return gsm1111_tx_change_chv(ms, 0x02, payload, payload_len,
			payload2, payload_len2);
	case SIM_JOB_PIN1_DISABLE:
		payload_len = strlen((char *)payload);
		if (payload_len < 4 || payload_len > 8) {
			LOGP(DSIM, LOGL_ERROR, "key not in range 4..8\n");
			break;
		}
		sim->job_state = SIM_JST_PIN1_DISABLE;
		return gsm1111_tx_disable_chv(ms, payload, payload_len);
	case SIM_JOB_PIN1_ENABLE:
		payload_len = strlen((char *)payload);
		if (payload_len < 4 || payload_len > 8) {
			LOGP(DSIM, LOGL_ERROR, "key not in range 4..8\n");
			break;
		}
		sim->job_state = SIM_JST_PIN1_ENABLE;
		return gsm1111_tx_enable_chv(ms, payload, payload_len);
	case SIM_JOB_PIN1_UNBLOCK:
		payload_len = strlen((char *)payload);
		payload2 = payload + payload_len + 1;
		payload_len2 = strlen((char *)payload2);
		if (payload_len !=  8) {
			LOGP(DSIM, LOGL_ERROR, "key1 not 8 digits\n");
			break;
		}
		if (payload_len2 < 4 || payload_len2 > 8) {
			LOGP(DSIM, LOGL_ERROR, "key2 not in range 4..8\n");
			break;
		}
		sim->job_state = SIM_JST_PIN1_UNBLOCK;
		/* NOTE: CHV1 is coded 0x00 here */
		return gsm1111_tx_unblock_chv(ms, 0x00, payload, payload_len,
			payload2, payload_len2);
	case SIM_JOB_PIN2_UNBLOCK:
		payload_len = strlen((char *)payload);
		payload2 = payload + payload_len + 1;
		payload_len2 = strlen((char *)payload2);
		if (payload_len !=  8) {
			LOGP(DSIM, LOGL_ERROR, "key1 not 8 digits\n");
			break;
		}
		if (payload_len2 < 4 || payload_len2 > 8) {
			LOGP(DSIM, LOGL_ERROR, "key2 not in range 4..8\n");
			break;
		}
		sim->job_state = SIM_JST_PIN2_UNBLOCK;
		return gsm1111_tx_unblock_chv(ms, 0x02, payload, payload_len,
			payload2, payload_len2);
	}

	LOGP(DSIM, LOGL_ERROR, "unknown job %x, please fix\n", sh->job_type);
	cause = SIM_CAUSE_REQUEST_ERROR;
	gsm_sim_reply(ms, SIM_JOB_ERROR, &cause, 1);

	return 0;
}

/* receive SIM response */
int sim_apdu_resp(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_sim *sim = &ms->sim;
	uint8_t *payload;
	uint16_t payload_len;
	uint8_t *data = msg->data;
	int length = msg->len, ef_len;
	uint8_t sw1, sw2;
	uint8_t cause;
	uint8_t pin_cause[2];
	struct sim_hdr *sh;
	struct gsm1111_response_ef *ef;
	struct gsm1111_response_mfdf *mfdf;
	struct gsm1111_response_mfdf_gsm *mfdf_gsm;
	int i;

	/* ignore, if current job already gone */
	if (!sim->job_msg) {
		LOGP(DSIM, LOGL_ERROR, "received APDU but no job, "
			"please fix!\n");
		msgb_free(msg);
		return 0;
	}

	sh = (struct sim_hdr *)sim->job_msg->data;
	payload = sim->job_msg->data + sizeof(*sh);
	payload_len = sim->job_msg->len - sizeof(*sh);

	/* process status */
	if (length < 2) {
		msgb_free(msg);
		return 0;
	}
	sw1 = data[length - 2];
	sw2 = data[length - 1];
	length -= 2;
	LOGP(DSIM, LOGL_INFO, "received APDU (len=%d sw1=0x%02x sw2=0x%02x)\n",
		length, sw1, sw2);

	switch (sw1) {
	case GSM1111_STAT_SECURITY:
		LOGP(DSIM, LOGL_NOTICE, "SIM Security\n");
		/* error */
		if (sw2 != GSM1111_SEC_NO_ACCESS && sw2 != GSM1111_SEC_BLOCKED)
			goto sim_error;

		/* select the right remaining counter an cause */
		// FIXME: read status to replace "*_remain"-counters
		switch (sim->job_state) {
		case SIM_JST_PIN1_UNBLOCK:
			if (sw2 == GSM1111_SEC_NO_ACCESS) {
				pin_cause[0] = SIM_CAUSE_PIN1_BLOCKED;
				pin_cause[1] = --sim->unblk1_remain;
			} else {
				pin_cause[0] = SIM_CAUSE_PUC_BLOCKED;
				pin_cause[1] = 0;
			}
			break;
		case SIM_JST_PIN2_UNLOCK:
		case SIM_JST_PIN2_CHANGE:
			if (sw2 == GSM1111_SEC_NO_ACCESS && sim->chv2_remain) {
				pin_cause[0] = SIM_CAUSE_PIN2_REQUIRED;
				pin_cause[1] = sim->chv2_remain--;
			} else {
				pin_cause[0] = SIM_CAUSE_PIN2_BLOCKED;
				pin_cause[1] = sim->unblk2_remain;
			}
			break;
		case SIM_JST_PIN2_UNBLOCK:
			if (sw2 == GSM1111_SEC_NO_ACCESS) {
				pin_cause[0] = SIM_CAUSE_PIN2_BLOCKED;
				pin_cause[1] = --sim->unblk2_remain;
			} else {
				pin_cause[0] = SIM_CAUSE_PUC_BLOCKED;
				pin_cause[1] = 0;
			}
		case SIM_JST_PIN1_UNLOCK:
		case SIM_JST_PIN1_CHANGE:
		case SIM_JST_PIN1_DISABLE:
		case SIM_JST_PIN1_ENABLE:
		default:
			if (sw2 == GSM1111_SEC_NO_ACCESS && sim->chv1_remain) {
				pin_cause[0] = SIM_CAUSE_PIN1_REQUIRED;
				pin_cause[1] = sim->chv1_remain--;
			} else {
				pin_cause[0] = SIM_CAUSE_PIN1_BLOCKED;
				pin_cause[1] = sim->unblk1_remain;
			}
			break;
		}
		gsm_sim_reply(ms, SIM_JOB_ERROR, pin_cause, 2);
		msgb_free(msg);
		return 0;
	case GSM1111_STAT_MEM_PROBLEM:
		if (sw2 >= 0x40) {
			LOGP(DSIM, LOGL_NOTICE, "memory of SIM failed\n");
			sim_error:
			cause = SIM_CAUSE_SIM_ERROR;
			gsm_sim_reply(ms, SIM_JOB_ERROR, &cause, 1);
			msgb_free(msg);
			return 0;
		}
		LOGP(DSIM, LOGL_NOTICE, "memory of SIM is bad (write took %d "
			"times to succeed)\n", sw2);
		/* fall through */
	case GSM1111_STAT_NORMAL:
	case GSM1111_STAT_PROACTIVE:
	case GSM1111_STAT_DL_ERROR:
	case GSM1111_STAT_RESPONSE:
	case GSM1111_STAT_RESPONSE_TOO:
		LOGP(DSIM, LOGL_INFO, "command successful\n");
		break;
	default:
		LOGP(DSIM, LOGL_INFO, "command failed\n");
		request_error:
		cause = SIM_CAUSE_REQUEST_ERROR;
		gsm_sim_reply(ms, SIM_JOB_ERROR, &cause, 1);
		msgb_free(msg);
		return 0;
	}


	switch (sim->job_state) {
	/* step 1: after selecting MF / DF, request the response */
	case SIM_JST_SELECT_MFDF:
		/* not enough data */
		if (sw2 < 22) {
			LOGP(DSIM, LOGL_NOTICE, "expecting minimum 22 bytes\n");
			goto sim_error;
		}
		/* request response */
		sim->job_state = SIM_JST_SELECT_MFDF_RESP;
		gsm1111_tx_get_response(ms, sw2);
		msgb_free(msg);
		return 0;
	/* step 2: after getting response of selecting MF / DF, continue
	 * to "process_job".
	 */
	case SIM_JST_SELECT_MFDF_RESP:
		if (length < 22) {
			LOGP(DSIM, LOGL_NOTICE, "expecting minimum 22 bytes\n");
			goto sim_error;
		}
		mfdf = (struct gsm1111_response_mfdf *)data;
		mfdf_gsm = (struct gsm1111_response_mfdf_gsm *)(data + 13);
		sim->chv1_remain = mfdf_gsm->chv1_remain;
		sim->chv2_remain = mfdf_gsm->chv2_remain;
		sim->unblk1_remain = mfdf_gsm->unblk1_remain;
		sim->unblk2_remain = mfdf_gsm->unblk2_remain;
		/* if MF was selected */
		if (sim->path[0] == 0) {
			/* if MF was selected, but MF is not indicated */
			if (ntohs(mfdf->file_id) != 0x3f00) {
				LOGP(DSIM, LOGL_NOTICE, "Not MF\n");
				goto sim_error;
			}
			/* if MF was selected, but type is not indicated */
			if (mfdf->tof != GSM1111_TOF_MF) {
				LOGP(DSIM, LOGL_NOTICE, "MF %02x != %02x "
					"%04x\n", mfdf->tof, GSM1111_TOF_MF,
					sim->path[0]);
				goto sim_error;
			}
			/* now continue */
			msgb_free(msg);
			return sim_process_job(ms);
		}
		/* if DF was selected, but this DF is not indicated */
		i = 0;
		while (sim->path[i + 1])
			i++;
		if (ntohs(mfdf->file_id) != sim->path[i]) {
			LOGP(DSIM, LOGL_NOTICE, "Path %04x != %04x\n",
				ntohs(mfdf->file_id), sim->path[i]);
			goto sim_error;
		}
		/* if DF was selected, but type is not indicated */
		if (mfdf->tof != GSM1111_TOF_DF) {
			LOGP(DSIM, LOGL_NOTICE, "TOF error\n");
			goto sim_error;
		}
		/* now continue */
		msgb_free(msg);
		return sim_process_job(ms);
	/* step 1: after selecting EF, request response of SELECT */
	case SIM_JST_SELECT_EF:
		/* not enough data */
		if (sw2 < 14) {
			LOGP(DSIM, LOGL_NOTICE, "expecting minimum 14 bytes\n");
			goto sim_error;
		}
		/* request response */
		sim->job_state = SIM_JST_SELECT_EF_RESP;
		gsm1111_tx_get_response(ms, sw2);
		msgb_free(msg);
		return 0;
	/* step 2: after getting response of selecting EF, do file command */
	case SIM_JST_SELECT_EF_RESP:
		if (length < 14) {
			LOGP(DSIM, LOGL_NOTICE, "expecting minimum 14 bytes\n");
			goto sim_error;
		}
		ef = (struct gsm1111_response_ef *)data;
		/* if EF was selected, but type is not indicated */
		if (ntohs(ef->file_id) != sim->file) {
			LOGP(DSIM, LOGL_NOTICE, "EF ID %04x != %04x\n",
				ntohs(ef->file_id), sim->file);
			goto sim_error;
		}
		/* check for record */
		if (length >= 15 && ef->length >= 2 && ef->structure != 0x00) {
			/* get length of record */
			ef_len = ntohs(ef->file_size);
			if (ef_len < data[14]) {
				LOGP(DSIM, LOGL_NOTICE, "total length is "
					"smaller (%d) than record size (%d)\n",
					ef_len, data[14]);
				goto request_error;
			}
			ef_len = data[14];
			LOGP(DSIM, LOGL_NOTICE, "selected record (len %d "
				"structure %d)\n", ef_len, ef->structure);
		} else {
			/* get length of file */
			ef_len = ntohs(ef->file_size);
			LOGP(DSIM, LOGL_NOTICE, "selected file (len %d)\n",
				ef_len);
		}
		/* do file command */
		sim->job_state = SIM_JST_WAIT_FILE;
		switch (sh->job_type) {
		case SIM_JOB_READ_BINARY:
			// FIXME: do chunks when greater or equal 256 bytes */
			gsm1111_tx_read_binary(ms, 0, ef_len);
			break;
		case SIM_JOB_UPDATE_BINARY:
			// FIXME: do chunks when greater or equal 256 bytes */
			if (ef_len < payload_len) {
				LOGP(DSIM, LOGL_NOTICE, "selected file is "
					"smaller (%d) than data to update "
					"(%d)\n", ef_len, payload_len);
				goto request_error;
			}
			gsm1111_tx_update_binary(ms, 0, payload, payload_len);
			break;
		case SIM_JOB_READ_RECORD:
			gsm1111_tx_read_record(ms, sh->rec_no, sh->rec_mode,
				ef_len);
			break;
		case SIM_JOB_UPDATE_RECORD:
			if (ef_len != payload_len) {
				LOGP(DSIM, LOGL_NOTICE, "selected file length "
					"(%d) does not equal record to update "
					"(%d)\n", ef_len, payload_len);
				goto request_error;
			}
			gsm1111_tx_update_record(ms, sh->rec_no, sh->rec_mode,
				payload, payload_len);
			break;
		case SIM_JOB_SEEK_RECORD:
			gsm1111_tx_seek(ms, sh->seek_type_mode, data, length);
			break;
		case SIM_JOB_INCREASE:
			if (length != 4) {
				LOGP(DSIM, LOGL_ERROR, "expecting uint32_t as "
					"value lenght, but got %d bytes\n",
					length);
				goto request_error;
			}
			gsm1111_tx_increase(ms, *((uint32_t *)data));
			break;
		case SIM_JOB_INVALIDATE:
			gsm1111_tx_invalidate(ms);
			break;
		case SIM_JOB_REHABILITATE:
			gsm1111_tx_rehabilitate(ms);
			break;
		}
		msgb_free(msg);
		return 0;
	/* step 3: after processing file command, job is done */
	case SIM_JST_WAIT_FILE:
		/* reply job with data */
		gsm_sim_reply(ms, SIM_JOB_OK, data, length);
		msgb_free(msg);
		return 0;
	/* step 1: after running GSM algorithm, request response */
	case SIM_JST_RUN_GSM_ALGO:
		/* not enough data */
		if (sw2 < 12) {
			LOGP(DSIM, LOGL_NOTICE, "expecting minimum 12 bytes\n");
			goto sim_error;
		}
		/* request response */
		sim->job_state = SIM_JST_RUN_GSM_ALGO_RESP;
		gsm1111_tx_get_response(ms, sw2);
		msgb_free(msg);
		return 0;
	/* step 2: after processing GSM command, job is done */
	case SIM_JST_RUN_GSM_ALGO_RESP:
		/* reply job with data */
		gsm_sim_reply(ms, SIM_JOB_OK, data, length);
		msgb_free(msg);
		return 0;
	case SIM_JST_PIN1_UNLOCK:
	case SIM_JST_PIN1_CHANGE:
	case SIM_JST_PIN1_DISABLE:
	case SIM_JST_PIN1_ENABLE:
	case SIM_JST_PIN1_UNBLOCK:
	case SIM_JST_PIN2_UNLOCK:
	case SIM_JST_PIN2_CHANGE:
	case SIM_JST_PIN2_UNBLOCK:
		/* reply job with data */
		gsm_sim_reply(ms, SIM_JOB_OK, data, length);
		msgb_free(msg);
		return 0;
	}

	LOGP(DSIM, LOGL_ERROR, "unknown state %u, please fix!\n",
		sim->job_state);
	goto request_error;
}

/*
 * API
 */

/* open access to sim */
uint32_t sim_open(struct osmocom_ms *ms,
	void (*cb)(struct osmocom_ms *ms, struct msgb *msg))
{
	struct gsm_sim *sim = &ms->sim;
	struct gsm_sim_handler *handler;

	/* create handler and attach */
	handler = talloc_zero(l23_ctx, struct gsm_sim_handler);
	if (!handler)
		return 0;
	handler->handle = new_handle++;
	handler->cb = cb;
	llist_add_tail(&handler->entry, &sim->handlers);

	return handler->handle;
}

/* close access to sim */
void sim_close(struct osmocom_ms *ms, uint32_t handle)
{
	struct gsm_sim *sim = &ms->sim;
	struct gsm_sim_handler *handler;

	handler = sim_get_handler(sim, handle);
	if (!handle)
		return;

	/* kill ourself */
	llist_del(&handler->entry);
	talloc_free(handler);
}

/* send job */
void sim_job(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_sim *sim = &ms->sim;

	msgb_enqueue(&sim->jobs, msg);
}

/*
 * init
 */

int gsm_sim_init(struct osmocom_ms *ms)
{
	struct gsm_sim *sim = &ms->sim;

	/* current path is undefined, forching MF */
	sim->path[0] = 0x0bad;
	sim->path[1] = 0;
	sim->file = 0;

	INIT_LLIST_HEAD(&sim->handlers);
	INIT_LLIST_HEAD(&sim->jobs);

	LOGP(DSIM, LOGL_INFO, "init SIM client\n");

	return 0;
}

int gsm_sim_exit(struct osmocom_ms *ms)
{
	struct gsm_sim *sim = &ms->sim;
	struct gsm_sim_handler *handler, *handler2;
	struct msgb *msg;

	LOGP(DSIM, LOGL_INFO, "exit SIM client\n");

	/* remove pending job msg */
	if (sim->job_msg) {
		msgb_free(sim->job_msg);
		sim->job_msg = NULL;
	}
	/* flush handlers */
	llist_for_each_entry_safe(handler, handler2, &sim->handlers, entry)
		sim_close(ms, handler->handle);
	/* flush jobs */
	while ((msg = msgb_dequeue(&sim->jobs)))
		msgb_free(msg);

	return 0;
}




