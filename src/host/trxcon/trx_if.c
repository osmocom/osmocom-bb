/*
 * OsmocomBB <-> SDR connection bridge
 * Transceiver interface handlers
 *
 * Copyright (C) 2013 by Andreas Eversberg <jolly@eversberg.eu>
 * Copyright (C) 2016-2017 by Vadim Yanitskiy <axilirator@gmail.com>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <netinet/in.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/select.h>
#include <osmocom/core/socket.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/bits.h>
#include <osmocom/core/fsm.h>

#include <osmocom/gsm/gsm_utils.h>

#include "trxcon.h"
#include "trx_if.h"
#include "logging.h"
#include "scheduler.h"

extern void *tall_trx_ctx;
extern struct osmo_fsm_inst *trxcon_fsm;

static struct osmo_fsm_state trx_fsm_states[] = {
	[TRX_STATE_OFFLINE] = {
		.out_state_mask = (
			GEN_MASK(TRX_STATE_IDLE) |
			GEN_MASK(TRX_STATE_RSP_WAIT)),
		.name = "OFFLINE",
	},
	[TRX_STATE_IDLE] = {
		.out_state_mask = UINT32_MAX,
		.name = "IDLE",
	},
	[TRX_STATE_ACTIVE] = {
		.out_state_mask = (
			GEN_MASK(TRX_STATE_IDLE) |
			GEN_MASK(TRX_STATE_RSP_WAIT)),
		.name = "ACTIVE",
	},
	[TRX_STATE_RSP_WAIT] = {
		.out_state_mask = (
			GEN_MASK(TRX_STATE_IDLE) |
			GEN_MASK(TRX_STATE_ACTIVE) |
			GEN_MASK(TRX_STATE_OFFLINE)),
		.name = "RSP_WAIT",
	},
};

static struct osmo_fsm trx_fsm = {
	.name = "trx_interface_fsm",
	.states = trx_fsm_states,
	.num_states = ARRAY_SIZE(trx_fsm_states),
	.log_subsys = DTRX,
};

static int trx_udp_open(void *priv, struct osmo_fd *ofd, const char *host,
	uint16_t port_local, uint16_t port_remote,
	int (*cb)(struct osmo_fd *fd, unsigned int what))
{
	struct sockaddr_storage sas;
	struct sockaddr *sa = (struct sockaddr *) &sas;
	socklen_t sa_len;
	int rc;

	ofd->data = priv;
	ofd->fd = -1;
	ofd->cb = cb;

	/* Init RX side for UDP connection */
	rc = osmo_sock_init_ofd(ofd, AF_UNSPEC, SOCK_DGRAM,
		0, host, port_local, OSMO_SOCK_F_BIND);
	if (rc < 0)
		return rc;

	/* Init TX side for UDP connection */
	sa_len = sizeof(sas);
	rc = getsockname(ofd->fd, sa, &sa_len);
	if (rc)
		return rc;

	if (sa->sa_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *) sa;
		sin->sin_port = htons(port_remote);
	} else if (sa->sa_family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) sa;
		sin6->sin6_port = htons(port_remote);
	} else {
		return -EINVAL;
	}

	rc = connect(ofd->fd, sa, sa_len);
	return rc;
}

static void trx_udp_close(struct osmo_fd *ofd)
{
	if (ofd->fd > 0) {
		osmo_fd_unregister(ofd);
		close(ofd->fd);
		ofd->fd = -1;
	}
}

/* ------------------------------------------------------------------------ */
/* Clock (CLCK) interface handlers                                          */
/* ------------------------------------------------------------------------ */
/* Indications on the Master Clock Interface                                */
/*                                                                          */
/* The master clock interface is output only (from the radio).              */
/* Messages are "indications".                                              */
/*                                                                          */
/* CLOCK gives the current value of the transceiver clock to be used by the */
/* core. This message is sent whenever a transmission packet arrives that   */
/* is too late or too early. The clock value is NOT the current transceiver */
/* time. It is a time setting the core should use to give better packet     */
/* arrival times.                                                           */
/*                                                                          */
/* IND CLOCK <totalFrames>                                                  */
/* ------------------------------------------------------------------------ */

static int trx_clck_read_cb(struct osmo_fd *ofd, unsigned int what)
{
	struct trx_instance *trx = (struct trx_instance *) ofd->data;
	char buf[1500];
	uint32_t fn;
	int len;

	len = recv(ofd->fd, buf, sizeof(buf) - 1, 0);
	if (len <= 0)
		return len;

	/* Terminate received string */
	buf[len] = '\0';

	if (!!strncmp(buf, "IND CLOCK ", 10)) {
		LOGP(DTRX, LOGL_ERROR,
			"Unknown message on CLCK socket: %s\n", buf);
		return 0;
	}

	sscanf(buf, "IND CLOCK %u", &fn);

	LOGP(DTRX, LOGL_DEBUG, "Clock indication: fn=%u\n", fn);

	if (fn >= 2715648) {
		fn %= 2715648;
		LOGP(DTRX, LOGL_ERROR, "Indicated clock's FN is not wrapping "
			"correctly, correcting to fn=%u\n", fn);
	}

	/* Call the clck_ind callback */
	sched_clck_handle(&trx->sched, fn);

	return 0;
}

/* ------------------------------------------------------------------------ */
/* Control (CTRL) interface handlers                                        */
/* ------------------------------------------------------------------------ */
/* Commands on the Per-ARFCN Control Interface                              */
/*                                                                          */
/* The per-ARFCN control interface uses a command-response protocol.        */
/* Commands are NULL-terminated ASCII strings, one per UDP socket.          */
/* Each command has a corresponding response.                               */
/* Every command is of the form:                                            */
/*                                                                          */
/* CMD <cmdtype> [params]                                                   */
/*                                                                          */
/* The <cmdtype> is the actual command.                                     */
/* Parameters are optional depending on the commands type.                  */
/* Every response is of the form:                                           */
/*                                                                          */
/* RSP <cmdtype> <status> [result]                                          */
/*                                                                          */
/* The <status> is 0 for success and a non-zero error code for failure.     */
/* Successful responses may include results, depending on the command type. */
/* ------------------------------------------------------------------------ */

static void trx_ctrl_timer_cb(void *data);

/* Send first CTRL message and start timer */
static void trx_ctrl_send(struct trx_instance *trx)
{
	struct trx_ctrl_msg *tcm;

	if (llist_empty(&trx->trx_ctrl_list))
		return;
	tcm = llist_entry(trx->trx_ctrl_list.next, struct trx_ctrl_msg, list);

	/* Send command */
	LOGP(DTRX, LOGL_DEBUG, "Sending control '%s'\n", tcm->cmd);
	send(trx->trx_ofd_ctrl.fd, tcm->cmd, strlen(tcm->cmd) + 1, 0);

	/* Trigger state machine */
	if (trx->fsm->state != TRX_STATE_RSP_WAIT) {
		trx->prev_state = trx->fsm->state;
		osmo_fsm_inst_state_chg(trx->fsm, TRX_STATE_RSP_WAIT, 0, 0);
	}

	/* Start expire timer */
	trx->trx_ctrl_timer.data = trx;
	trx->trx_ctrl_timer.cb = trx_ctrl_timer_cb;
	osmo_timer_schedule(&trx->trx_ctrl_timer, 2, 0);
}

static void trx_ctrl_timer_cb(void *data)
{
	struct trx_instance *trx = (struct trx_instance *) data;
	struct trx_ctrl_msg *tcm;

	/* Queue may be cleaned at this moment */
	if (llist_empty(&trx->trx_ctrl_list))
		return;

	LOGP(DTRX, LOGL_NOTICE, "No response from transceiver...\n");

	tcm = llist_entry(trx->trx_ctrl_list.next, struct trx_ctrl_msg, list);
	if (++tcm->retry_cnt > 3) {
		LOGP(DTRX, LOGL_NOTICE, "Transceiver offline\n");
		osmo_fsm_inst_state_chg(trx->fsm, TRX_STATE_OFFLINE, 0, 0);
		osmo_fsm_inst_dispatch(trxcon_fsm, TRX_EVENT_OFFLINE, trx);
		return;
	}

	/* Attempt to send a command again */
	trx_ctrl_send(trx);
}

/* Add a new CTRL command to the trx_ctrl_list */
static int trx_ctrl_cmd(struct trx_instance *trx, int critical,
	const char *cmd, const char *fmt, ...)
{
	struct trx_ctrl_msg *tcm;
	int len, pending = 0;
	va_list ap;

	/* TODO: make sure that transceiver online */

	if (!llist_empty(&trx->trx_ctrl_list))
		pending = 1;

	/* Allocate a message */
	tcm = talloc_zero(trx, struct trx_ctrl_msg);
	if (!tcm)
		return -ENOMEM;

	/* Fill in command arguments */
	if (fmt && fmt[0]) {
		len = snprintf(tcm->cmd, sizeof(tcm->cmd) - 1, "CMD %s ", cmd);
		va_start(ap, fmt);
		vsnprintf(tcm->cmd + len, sizeof(tcm->cmd) - len - 1, fmt, ap);
		va_end(ap);
	} else {
		snprintf(tcm->cmd, sizeof(tcm->cmd) - 1, "CMD %s", cmd);
	}

	tcm->cmd_len = strlen(cmd);
	tcm->critical = critical;
	llist_add_tail(&tcm->list, &trx->trx_ctrl_list);
	LOGP(DTRX, LOGL_INFO, "Adding new control '%s'\n", tcm->cmd);

	/* Send message, if no pending messages */
	if (!pending)
		trx_ctrl_send(trx);

	return 0;
}

/*
 * Power Control
 *
 * ECHO is used to check transceiver availability.
 * CMD ECHO
 * RSP ECHO <status>
 *
 * POWEROFF shuts off transmitter power and stops the demodulator.
 * CMD POWEROFF
 * RSP POWEROFF <status>
 *
 * POWERON starts the transmitter and starts the demodulator.
 * Initial power level is very low.
 * This command fails if the transmitter and receiver are not yet tuned.
 * This command fails if the transmit or receive frequency creates a conflict
 * with another ARFCN that is already running.
 * If the transceiver is already on, it response with success to this command.
 * CMD POWERON
 * RSP POWERON <status>
 */

int trx_if_cmd_echo(struct trx_instance *trx)
{
	return trx_ctrl_cmd(trx, 1, "ECHO", "");
}

int trx_if_cmd_poweroff(struct trx_instance *trx)
{
	return trx_ctrl_cmd(trx, 1, "POWEROFF", "");
}

int trx_if_cmd_poweron(struct trx_instance *trx)
{
	return trx_ctrl_cmd(trx, 1, "POWERON", "");
}

/*
 * SETPOWER sets output power in dB wrt full scale.
 * This command fails if the transmitter and receiver are not running.
 * CMD SETPOWER <dB>
 * RSP SETPOWER <status> <dB>
 */

int trx_if_cmd_setpower(struct trx_instance *trx, int db)
{
	return trx_ctrl_cmd(trx, 0, "SETPOWER", "%d", db);
}

/*
 * ADJPOWER adjusts power by the given dB step.
 * Response returns resulting power level wrt full scale.
 * This command fails if the transmitter and receiver are not running.
 * CMD ADJPOWER <dBStep>
 * RSP ADJPOWER <status> <dBLevel>
*/

int trx_if_cmd_adjpower(struct trx_instance *trx, int db)
{
	return trx_ctrl_cmd(trx, 0, "ADJPOWER", "%d", db);
}

int trx_if_cmd_setrxgain(struct trx_instance *trx, int db)
{
	return trx_ctrl_cmd(trx, 0, "SETRXGAIN", "%d", db);
}

int trx_if_cmd_setmaxdly(struct trx_instance *trx, int dly)
{
	return trx_ctrl_cmd(trx, 0, "SETMAXDLY", "%d", dly);
}

/*
 * Timeslot Control
 *
 * SETSLOT sets the format of the uplink timeslots in the ARFCN.
 * The <timeslot> indicates the timeslot of interest.
 * The <chantype> indicates the type of channel that occupies the timeslot.
 * A chantype of zero indicates the timeslot is off.
 * CMD SETSLOT <timeslot> <chantype>
 * RSP SETSLOT <status> <timeslot> <chantype>
 */

int trx_if_cmd_setslot(struct trx_instance *trx, uint8_t tn, uint8_t type)
{
	return trx_ctrl_cmd(trx, 1, "SETSLOT", "%d %d", tn, type);
}

/*
 * Tuning Control
 *
 * (RX/TX)TUNE tunes the receiver to a given frequency in kHz.
 * This command fails if the receiver is already running.
 * (To re-tune you stop the radio, re-tune, and restart.)
 * This command fails if the transmit or receive frequency
 * creates a conflict with another ARFCN that is already running.
 * CMD (RX/TX)TUNE <kHz>
 * RSP (RX/TX)TUNE <status> <kHz>
 */

int trx_if_cmd_rxtune(struct trx_instance *trx, uint16_t arfcn)
{
	uint16_t freq10;

	/* RX is downlink on MS side */
	freq10 = gsm_arfcn2freq10(arfcn, 0);
	if (freq10 == 0xffff) {
		LOGP(DTRX, LOGL_ERROR, "ARFCN %d not defined\n", arfcn);
		return -ENOTSUP;
	}

	return trx_ctrl_cmd(trx, 1, "RXTUNE", "%d", freq10 * 100);
}

int trx_if_cmd_txtune(struct trx_instance *trx, uint16_t arfcn)
{
	uint16_t freq10;

	/* TX is uplink on MS side */
	freq10 = gsm_arfcn2freq10(arfcn, 1);
	if (freq10 == 0xffff) {
		LOGP(DTRX, LOGL_ERROR, "ARFCN %d not defined\n", arfcn);
		return -ENOTSUP;
	}

	return trx_ctrl_cmd(trx, 1, "TXTUNE", "%d", freq10 * 100);
}

/* Get response from CTRL socket */
static int trx_ctrl_read_cb(struct osmo_fd *ofd, unsigned int what)
{
	struct trx_instance *trx = ofd->data;
	struct trx_ctrl_msg *tcm;
	int len, resp, rsp_len;
	char buf[1500], *p;

	len = recv(ofd->fd, buf, sizeof(buf) - 1, 0);
	if (len <= 0)
		return len;
	buf[len] = '\0';

	if (!!strncmp(buf, "RSP ", 4)) {
		LOGP(DTRX, LOGL_NOTICE, "Unknown message on CTRL port: %s\n", buf);
		return 0;
	}

	/* Calculate the length of response item */
	p = strchr(buf + 4, ' ');
	rsp_len = p ? p - buf - 4 : strlen(buf) - 4;

	LOGP(DTRX, LOGL_INFO, "Response message: '%s'\n", buf);

	/* Abort expire timer */
	if (osmo_timer_pending(&trx->trx_ctrl_timer))
		osmo_timer_del(&trx->trx_ctrl_timer);

	/* Get command for response message */
	if (llist_empty(&trx->trx_ctrl_list)) {
		LOGP(DTRX, LOGL_NOTICE, "Response message without command\n");
		return -EINVAL;
	}

	tcm = llist_entry(trx->trx_ctrl_list.next,
		struct trx_ctrl_msg, list);

	/* Check if response matches command */
	if (!!strncmp(buf + 4, tcm->cmd + 4, rsp_len)) {
		LOGP(DTRX, (tcm->critical) ? LOGL_FATAL : LOGL_ERROR,
			"Response message '%s' does not match command "
			"message '%s'\n", buf, tcm->cmd);
		goto rsp_error;
	}

	/* Check for response code */
	sscanf(p + 1, "%d", &resp);
	if (resp) {
		LOGP(DTRX, (tcm->critical) ? LOGL_FATAL : LOGL_ERROR,
			"Transceiver rejected TRX command with "
			"response: '%s'\n", buf);

		if (tcm->critical)
			goto rsp_error;
	}

	/* Trigger state machine */
	if (!strncmp(tcm->cmd + 4, "POWERON", 7))
		osmo_fsm_inst_state_chg(trx->fsm, TRX_STATE_ACTIVE, 0, 0);
	else if (!strncmp(tcm->cmd + 4, "POWEROFF", 8))
		osmo_fsm_inst_state_chg(trx->fsm, TRX_STATE_IDLE, 0, 0);
	else if (!strncmp(tcm->cmd + 4, "ECHO", 4)) {
		osmo_fsm_inst_state_chg(trx->fsm, TRX_STATE_IDLE, 0, 0);
		osmo_fsm_inst_dispatch(trxcon_fsm,
			TRX_EVENT_RESET_IND, trx);
	} else
		osmo_fsm_inst_state_chg(trx->fsm, trx->prev_state, 0, 0);

	/* Remove command from list */
	llist_del(&tcm->list);
	talloc_free(tcm);

	/* Send next message, if any */
	trx_ctrl_send(trx);

	return 0;

rsp_error:
	/* Notify higher layers about the problem */
	osmo_fsm_inst_dispatch(trxcon_fsm, TRX_EVENT_RSP_ERROR, trx);
	return -EIO;
}

/* ------------------------------------------------------------------------ */
/* Data interface handlers                                                  */
/* ------------------------------------------------------------------------ */
/* DATA interface                                                           */
/*                                                                          */
/* Messages on the data interface carry one radio burst per UDP message.    */
/*                                                                          */
/* Received Data Burst:                                                     */
/* 1 byte timeslot index                                                    */
/* 4 bytes GSM frame number, BE                                             */
/* 1 byte RSSI in -dBm                                                      */
/* 2 bytes correlator timing offset in 1/256 symbol steps, 2's-comp, BE     */
/* 148 bytes soft symbol estimates, 0 -> definite "0", 255 -> definite "1"  */
/* 2 bytes are not used, but being sent by OsmoTRX                          */
/*                                                                          */
/* Transmit Data Burst:                                                     */
/* 1 byte timeslot index                                                    */
/* 4 bytes GSM frame number, BE                                             */
/* 1 byte transmit level wrt ARFCN max, -dB (attenuation)                   */
/* 148 bytes output symbol values, 0 & 1                                    */
/* ------------------------------------------------------------------------ */

static int trx_data_rx_cb(struct osmo_fd *ofd, unsigned int what)
{
	struct trx_instance *trx = ofd->data;
	uint8_t buf[256];
	sbit_t bits[148];
	int8_t rssi, tn;
	uint32_t fn;
	int len, i;
	float toa;

	len = recv(ofd->fd, buf, sizeof(buf), 0);
	if (len <= 0)
		return len;

	if (len != 158) {
		LOGP(DTRX, LOGL_ERROR, "Got data message with invalid length "
			"'%d'\n", len);
		return -EINVAL;
	}

	tn = buf[0];
	fn = (buf[1] << 24) | (buf[2] << 16) | (buf[3] << 8) | buf[4];
	rssi = -(int8_t) buf[5];
	toa = ((int16_t) (buf[6] << 8) | buf[7]) / 256.0F;

	/* Copy and convert bits {254..0} to sbits {-127..127} */
	for (i = 0; i < 148; i++) {
		if (buf[8 + i] == 255)
			bits[i] = -127;
		else
			bits[i] = 127 - buf[8 + i];
	}

	if (tn >= 8) {
		LOGP(DTRX, LOGL_ERROR, "Illegal TS %d\n", tn);
		return -EINVAL;
	}

	if (fn >= 2715648) {
		LOGP(DTRX, LOGL_ERROR, "Illegal FN %u\n", fn);
		return -EINVAL;
	}

	LOGP(DTRX, LOGL_DEBUG, "RX burst tn=%u fn=%u rssi=%d toa=%.2f\n",
		tn, fn, rssi, toa);

	/* Poke scheduler */
	sched_trx_handle_rx_burst(trx, tn, fn, bits, 148, rssi, toa);

	return 0;
}

int trx_if_tx_burst(struct trx_instance *trx, uint8_t tn, uint32_t fn,
	uint8_t pwr, const ubit_t *bits)
{
	uint8_t buf[256];

	/**
	 * We must be sure that we have clock,
	 * and we have sent all control data
	 *
	 * TODO: should we wait in TRX_STATE_RSP_WAIT state?
	 */
	if (trx->fsm->state != TRX_STATE_ACTIVE) {
		LOGP(DTRX, LOGL_DEBUG, "Ignoring TX data, "
			"transceiver isn't ready\n");
		return -EAGAIN;
	}

	LOGP(DTRX, LOGL_DEBUG, "TX burst tn=%u fn=%u pwr=%u\n", tn, fn, pwr);

	buf[0] = tn;
	buf[1] = (fn >> 24) & 0xff;
	buf[2] = (fn >> 16) & 0xff;
	buf[3] = (fn >>  8) & 0xff;
	buf[4] = (fn >>  0) & 0xff;
	buf[5] = pwr;

	/* Copy ubits {0,1} */
	memcpy(buf + 6, bits, 148);

	/* Send data to transceiver */
	send(trx->trx_ofd_data.fd, buf, 154, 0);

	return 0;
}

/*
 * Open/close OsmoTRX connection
 */

int trx_if_open(struct trx_instance **trx, const char *host, uint16_t port)
{
	struct trx_instance *trx_new;
	char *inst_name;
	int rc;

	LOGP(DTRX, LOGL_NOTICE, "Init transceiver interface\n");

	/* Try to allocate memory */
	trx_new = talloc_zero(tall_trx_ctx, struct trx_instance);
	if (!trx_new) {
		fprintf(stderr, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	/* Initialize CTRL queue */
	INIT_LLIST_HEAD(&trx_new->trx_ctrl_list);

	/* Open sockets */
	rc = trx_udp_open(trx_new, &trx_new->trx_ofd_clck, host,
		port + 100, port + 0, trx_clck_read_cb);
	if (rc < 0)
		goto error;

	rc = trx_udp_open(trx_new, &trx_new->trx_ofd_ctrl, host,
		port + 101, port + 1, trx_ctrl_read_cb);
	if (rc < 0)
		goto error;

	rc = trx_udp_open(trx_new, &trx_new->trx_ofd_data, host,
		port + 102, port + 2, trx_data_rx_cb);
	if (rc < 0)
		goto error;

	/* Allocate a new dedicated state machine */
	osmo_fsm_register(&trx_fsm);
	inst_name = talloc_asprintf(trx_new, "%s:%u", host, port);
	trx_new->fsm = osmo_fsm_inst_alloc(&trx_fsm, trx_new,
		NULL, LOGL_DEBUG, inst_name);
	talloc_free(inst_name);

	*trx = trx_new;

	return 0;

error:
	LOGP(DTRX, LOGL_NOTICE, "Couldn't establish UDP connection\n");
	talloc_free(trx_new);
	return rc;
}

/* Flush pending control messages */
void trx_if_flush_ctrl(struct trx_instance *trx)
{
	struct trx_ctrl_msg *tcm;

	/* Reset state machine */
	osmo_fsm_inst_state_chg(trx->fsm, TRX_STATE_IDLE, 0, 0);

	/* Clear command queue */
	while (!llist_empty(&trx->trx_ctrl_list)) {
		tcm = llist_entry(trx->trx_ctrl_list.next,
			struct trx_ctrl_msg, list);
		llist_del(&tcm->list);
		talloc_free(tcm);
	}
}

void trx_if_close(struct trx_instance *trx)
{
	/* May be unallocated due to init error */
	if (!trx)
		return;

	LOGP(DTRX, LOGL_NOTICE, "Shutdown transceiver interface\n");

	/* Flush CTRL message list */
	trx_if_flush_ctrl(trx);

	/* Close sockets */
	trx_udp_close(&trx->trx_ofd_clck);
	trx_udp_close(&trx->trx_ofd_ctrl);
	trx_udp_close(&trx->trx_ofd_data);

	/* Free memory */
	osmo_fsm_inst_free(trx->fsm);
	talloc_free(trx);
}
