/*
 * OsmocomBB <-> SDR connection bridge
 * Transceiver interface handlers
 *
 * Copyright (C) 2013 by Andreas Eversberg <jolly@eversberg.eu>
 * Copyright (C) 2016-2017 by Vadim Yanitskiy <axilirator@gmail.com>
 * Copyright (C) 2022 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
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
#include <osmocom/gsm/gsm0502.h>

#include <osmocom/bb/trxcon/trx_if.h>
#include <osmocom/bb/trxcon/logging.h>

#define TRXDv0_HDR_LEN		8

#define S(x)	(1 << (x))

static void trx_fsm_cleanup_cb(struct osmo_fsm_inst *fi,
			       enum osmo_fsm_term_cause cause);

static struct value_string trx_evt_names[] = {
	{ 0, NULL } /* no events? */
};

static struct osmo_fsm_state trx_fsm_states[] = {
	[TRX_STATE_OFFLINE] = {
		.out_state_mask = (
			S(TRX_STATE_IDLE) |
			S(TRX_STATE_RSP_WAIT)),
		.name = "OFFLINE",
	},
	[TRX_STATE_IDLE] = {
		.out_state_mask = UINT32_MAX,
		.name = "IDLE",
	},
	[TRX_STATE_ACTIVE] = {
		.out_state_mask = (
			S(TRX_STATE_IDLE) |
			S(TRX_STATE_RSP_WAIT)),
		.name = "ACTIVE",
	},
	[TRX_STATE_RSP_WAIT] = {
		.out_state_mask = (
			S(TRX_STATE_IDLE) |
			S(TRX_STATE_ACTIVE) |
			S(TRX_STATE_OFFLINE)),
		.name = "RSP_WAIT",
	},
};

static struct osmo_fsm trx_fsm = {
	.name = "trx_interface",
	.states = trx_fsm_states,
	.num_states = ARRAY_SIZE(trx_fsm_states),
	.log_subsys = DTRXC,
	.event_names = trx_evt_names,
	.cleanup = &trx_fsm_cleanup_cb,
};

static int trx_udp_open(void *priv, struct osmo_fd *ofd, const char *host_local,
	uint16_t port_local, const char *host_remote, uint16_t port_remote,
	int (*cb)(struct osmo_fd *fd, unsigned int what))
{
	int rc;

	ofd->data = priv;
	ofd->fd = -1;
	ofd->cb = cb;

	/* Init UDP Connection */
	rc = osmo_sock_init2_ofd(ofd, AF_UNSPEC, SOCK_DGRAM, 0, host_local, port_local,
				 host_remote, port_remote,
				 OSMO_SOCK_F_BIND | OSMO_SOCK_F_CONNECT);
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
	LOGPFSML(trx->fi, LOGL_DEBUG, "Sending control '%s'\n", tcm->cmd);
	send(trx->trx_ofd_ctrl.fd, tcm->cmd, strlen(tcm->cmd) + 1, 0);

	/* Trigger state machine */
	if (trx->fi->state != TRX_STATE_RSP_WAIT) {
		trx->prev_state = trx->fi->state;
		osmo_fsm_inst_state_chg(trx->fi, TRX_STATE_RSP_WAIT, 0, 0);
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

	LOGPFSML(trx->fi, LOGL_NOTICE, "No response from transceiver...\n");

	tcm = llist_entry(trx->trx_ctrl_list.next, struct trx_ctrl_msg, list);
	if (++tcm->retry_cnt > 3) {
		LOGPFSML(trx->fi, LOGL_NOTICE, "Transceiver offline\n");
		osmo_fsm_inst_state_chg(trx->fi, TRX_STATE_OFFLINE, 0, 0);
		osmo_fsm_inst_term(trx->fi, OSMO_FSM_TERM_TIMEOUT, NULL);
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
	LOGPFSML(trx->fi, LOGL_INFO, "Adding new control '%s'\n", tcm->cmd);

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

static int trx_if_cmd_echo(struct trx_instance *trx)
{
	return trx_ctrl_cmd(trx, 1, "ECHO", "");
}

static int trx_if_cmd_poweroff(struct trx_instance *trx)
{
	return trx_ctrl_cmd(trx, 1, "POWEROFF", "");
}

static int trx_if_cmd_poweron(struct trx_instance *trx)
{
#if 0
	if (trx->powered_up)
		return -EAGAIN;
#endif
	return trx_ctrl_cmd(trx, 1, "POWERON", "");
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

static int trx_if_cmd_setslot(struct trx_instance *trx,
			      const struct trxcon_phyif_cmdp_setslot *cmdp)
{
	/* Values correspond to 'enum ChannelCombination' in osmo-trx.git */
	static const uint8_t chan_types[_GSM_PCHAN_MAX] = {
		[GSM_PCHAN_UNKNOWN]             = 0,
		[GSM_PCHAN_NONE]                = 0,
		[GSM_PCHAN_CCCH]                = 4,
		[GSM_PCHAN_CCCH_SDCCH4]         = 5,
		[GSM_PCHAN_CCCH_SDCCH4_CBCH]    = 5,
		[GSM_PCHAN_TCH_F]               = 1,
		[GSM_PCHAN_TCH_H]               = 3,
		[GSM_PCHAN_SDCCH8_SACCH8C]      = 7,
		[GSM_PCHAN_SDCCH8_SACCH8C_CBCH] = 7,
		[GSM_PCHAN_PDCH]                = 13,
	};

	return trx_ctrl_cmd(trx, 1, "SETSLOT", "%u %u",
			    cmdp->tn, chan_types[cmdp->pchan]);
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

static int trx_if_cmd_rxtune(struct trx_instance *trx,
			     const struct trxcon_phyif_cmdp_setfreq_h0 *cmdp)
{
	uint16_t freq10;

	/* RX is downlink on MS side */
	freq10 = gsm_arfcn2freq10(cmdp->band_arfcn, 0);
	if (freq10 == 0xffff) {
		LOGPFSML(trx->fi, LOGL_ERROR, "ARFCN %d not defined\n", cmdp->band_arfcn);
		return -ENOTSUP;
	}

	return trx_ctrl_cmd(trx, 1, "RXTUNE", "%u", freq10 * 100);
}

static int trx_if_cmd_txtune(struct trx_instance *trx,
			     const struct trxcon_phyif_cmdp_setfreq_h0 *cmdp)
{
	uint16_t freq10;

	/* TX is uplink on MS side */
	freq10 = gsm_arfcn2freq10(cmdp->band_arfcn, 1);
	if (freq10 == 0xffff) {
		LOGPFSML(trx->fi, LOGL_ERROR, "ARFCN %d not defined\n", cmdp->band_arfcn);
		return -ENOTSUP;
	}

	return trx_ctrl_cmd(trx, 1, "TXTUNE", "%u", freq10 * 100);
}

/*
 * Power measurement
 *
 * MEASURE instructs the transceiver to perform a power
 * measurement on specified frequency. After receiving this
 * request, transceiver should quickly re-tune to requested
 * frequency, measure power level and re-tune back to the
 * previous frequency.
 * CMD MEASURE <kHz>
 * RSP MEASURE <status> <kHz> <dB>
 */

static int trx_if_cmd_measure(struct trx_instance *trx,
			      const struct trxcon_phyif_cmdp_measure *cmdp)
{
	uint16_t freq10;

	/* Calculate a frequency for current ARFCN (DL) */
	freq10 = gsm_arfcn2freq10(cmdp->band_arfcn, 0);
	if (freq10 == 0xffff) {
		LOGPFSML(trx->fi, LOGL_ERROR,
			 "ARFCN %d not defined\n", cmdp->band_arfcn);
		return -ENOTSUP;
	}

	return trx_ctrl_cmd(trx, 1, "MEASURE", "%u", freq10 * 100);
}

static void trx_if_measure_rsp_cb(struct trx_instance *trx, char *resp)
{
	unsigned int freq10;
	uint16_t band_arfcn;
	int dbm;

	/* Parse freq. and power level */
	sscanf(resp, "%u %d", &freq10, &dbm);
	freq10 /= 100;

	band_arfcn = gsm_freq102arfcn((uint16_t) freq10, 0);
	if (band_arfcn == 0xffff) {
		LOGPFSML(trx->fi, LOGL_ERROR,
			 "Failed to parse ARFCN from RSP MEASURE: %s\n", resp);
		return;
	}

	const struct trxcon_phyif_rsp rsp = {
		.type = TRXCON_PHYIF_CMDT_MEASURE,
		.param.measure = {
			.band_arfcn = band_arfcn,
			.dbm = dbm,
		},
	};

	trxcon_phyif_handle_rsp(trx->priv, &rsp);
}

/*
 * Timing Advance control
 *
 * SETTA instructs the transceiver to transmit bursts in
 * advance calculated from requested TA value. This value is
 * normally between 0 and 63, with each step representing
 * an advance of one bit period (about 3.69 microseconds).
 * Since OsmocomBB has a special feature, which allows one
 * to spoof the distance from BTS, the range is extended.
 * CMD SETTA <-128..127>
 * RSP SETTA <status> <TA>
 */

static int trx_if_cmd_setta(struct trx_instance *trx,
			    const struct trxcon_phyif_cmdp_setta *cmdp)
{
	return trx_ctrl_cmd(trx, 0, "SETTA", "%d", cmdp->ta);
}

/*
 * Frequency Hopping parameters indication.
 *
 * SETFH instructs transceiver to enable frequency hopping mode
 * using the given HSN, MAIO, and Mobile Allocation parameters.
 *
 * CMD SETFH <HSN> <MAIO> <RXF1> <TXF1> [... <RXFN> <TXFN>]
 *
 * where <RXFN> and <TXFN> is a pair of Rx/Tx frequencies (in kHz)
 * corresponding to one ARFCN the Mobile Allocation. Note that the
 * channel list is expected to be sorted in ascending order.
 */

static int trx_if_cmd_setfh(struct trx_instance *trx,
			    const struct trxcon_phyif_cmdp_setfreq_h1 *cmdp)
{
	/* Reserve some room for CMD SETFH <HSN> <MAIO> */
	char ma_buf[TRXC_BUF_SIZE - 24];
	size_t ma_buf_len = sizeof(ma_buf) - 1;
	uint16_t rx_freq, tx_freq;
	char *ptr;
	int i, rc;

	/* Make sure that Mobile Allocation has at least one ARFCN */
	if (!cmdp->ma_len || cmdp->ma == NULL) {
		LOGPFSML(trx->fi, LOGL_ERROR, "Mobile Allocation is empty?!?\n");
		return -EINVAL;
	}

	/* Compose a sequence of Rx/Tx frequencies (mobile allocation) */
	for (i = 0, ptr = ma_buf; i < cmdp->ma_len; i++) {
		/* Convert ARFCN to a pair of Rx/Tx frequencies (Hz * 10) */
		rx_freq = gsm_arfcn2freq10(cmdp->ma[i], 0); /* Rx: Downlink */
		tx_freq = gsm_arfcn2freq10(cmdp->ma[i], 1); /* Tx: Uplink */
		if (rx_freq == 0xffff || tx_freq == 0xffff) {
			LOGPFSML(trx->fi, LOGL_ERROR, "Failed to convert ARFCN %u "
			     "to a pair of Rx/Tx frequencies\n",
			     cmdp->ma[i] & ~ARFCN_FLAG_MASK);
			return -EINVAL;
		}

		/* Append a pair of Rx/Tx frequencies (in kHz) to the buffer */
		rc = snprintf(ptr, ma_buf_len, "%u %u ", rx_freq * 100, tx_freq * 100);
		if (rc < 0 || rc > ma_buf_len) { /* Prevent buffer overflow */
			LOGPFSML(trx->fi, LOGL_ERROR, "Not enough room to encode "
			     "Mobile Allocation (N=%u)\n", cmdp->ma_len);
			return -ENOSPC;
		}

		/* Move pointer */
		ma_buf_len -= rc;
		ptr += rc;
	}

	/* Overwrite the last space */
	*(ptr - 1) = '\0';

	return trx_ctrl_cmd(trx, 1, "SETFH", "%u %u %s", cmdp->hsn, cmdp->maio, ma_buf);
}

/* Get response from CTRL socket */
static int trx_ctrl_read_cb(struct osmo_fd *ofd, unsigned int what)
{
	struct trx_instance *trx = ofd->data;
	struct trx_ctrl_msg *tcm;
	int resp, rsp_len;
	char buf[TRXC_BUF_SIZE], *p;
	ssize_t read_len;

	read_len = read(ofd->fd, buf, sizeof(buf) - 1);
	if (read_len <= 0) {
		LOGPFSML(trx->fi, LOGL_ERROR, "read() failed with rc=%zd\n", read_len);
		return read_len;
	}
	buf[read_len] = '\0';

	if (!!strncmp(buf, "RSP ", 4)) {
		LOGPFSML(trx->fi, LOGL_NOTICE, "Unknown message on CTRL port: %s\n", buf);
		return 0;
	}

	/* Calculate the length of response item */
	p = strchr(buf + 4, ' ');
	rsp_len = p ? p - buf - 4 : strlen(buf) - 4;

	LOGPFSML(trx->fi, LOGL_INFO, "Response message: '%s'\n", buf);

	/* Abort expire timer */
	osmo_timer_del(&trx->trx_ctrl_timer);

	/* Get command for response message */
	if (llist_empty(&trx->trx_ctrl_list)) {
		LOGPFSML(trx->fi, LOGL_NOTICE, "Response message without command\n");
		return -EINVAL;
	}

	tcm = llist_entry(trx->trx_ctrl_list.next,
		struct trx_ctrl_msg, list);

	/* Check if response matches command */
	if (!!strncmp(buf + 4, tcm->cmd + 4, rsp_len)) {
		LOGPFSML(trx->fi, (tcm->critical) ? LOGL_FATAL : LOGL_ERROR,
			"Response message '%s' does not match command "
			"message '%s'\n", buf, tcm->cmd);
		goto rsp_error;
	}

	/* Check for response code */
	sscanf(p + 1, "%d", &resp);
	if (resp) {
		LOGPFSML(trx->fi, (tcm->critical) ? LOGL_FATAL : LOGL_ERROR,
			"Transceiver rejected TRX command with "
			"response: '%s'\n", buf);

		if (tcm->critical)
			goto rsp_error;
	}

	/* Trigger state machine */
	if (!strncmp(tcm->cmd + 4, "POWERON", 7)) {
		trx->powered_up = true;
		osmo_fsm_inst_state_chg(trx->fi, TRX_STATE_ACTIVE, 0, 0);
	}
	else if (!strncmp(tcm->cmd + 4, "POWEROFF", 8)) {
		trx->powered_up = false;
		osmo_fsm_inst_state_chg(trx->fi, TRX_STATE_IDLE, 0, 0);
	}
	else if (!strncmp(tcm->cmd + 4, "MEASURE", 7))
		trx_if_measure_rsp_cb(trx, buf + 14);
	else if (!strncmp(tcm->cmd + 4, "ECHO", 4))
		osmo_fsm_inst_state_chg(trx->fi, TRX_STATE_IDLE, 0, 0);
	else
		osmo_fsm_inst_state_chg(trx->fi, trx->prev_state, 0, 0);

	/* Remove command from list */
	llist_del(&tcm->list);
	talloc_free(tcm);

	/* Send next message, if any */
	trx_ctrl_send(trx);

	return 0;

rsp_error:
	osmo_fsm_inst_term(trx->fi, OSMO_FSM_TERM_ERROR, NULL);
	return -EIO;
}

int trx_if_handle_phyif_cmd(struct trx_instance *trx, const struct trxcon_phyif_cmd *cmd)
{
	int rc;

	switch (cmd->type) {
	case TRXCON_PHYIF_CMDT_RESET:
		if ((rc = trx_if_cmd_poweroff(trx)) != 0)
			return rc;
		rc = trx_if_cmd_echo(trx);
		break;
	case TRXCON_PHYIF_CMDT_POWERON:
		rc = trx_if_cmd_poweron(trx);
		break;
	case TRXCON_PHYIF_CMDT_POWEROFF:
		rc = trx_if_cmd_poweroff(trx);
		break;
	case TRXCON_PHYIF_CMDT_MEASURE:
		rc = trx_if_cmd_measure(trx, &cmd->param.measure);
		break;
	case TRXCON_PHYIF_CMDT_SETFREQ_H0:
		if ((rc = trx_if_cmd_rxtune(trx, &cmd->param.setfreq_h0)) != 0)
			return rc;
		if ((rc = trx_if_cmd_txtune(trx, &cmd->param.setfreq_h0)) != 0)
			return rc;
		break;
	case TRXCON_PHYIF_CMDT_SETFREQ_H1:
		rc = trx_if_cmd_setfh(trx, &cmd->param.setfreq_h1);
		break;
	case TRXCON_PHYIF_CMDT_SETSLOT:
		rc = trx_if_cmd_setslot(trx, &cmd->param.setslot);
		break;
	case TRXCON_PHYIF_CMDT_SETTA:
		rc = trx_if_cmd_setta(trx, &cmd->param.setta);
		break;
	default:
		LOGPFSML(trx->fi, LOGL_ERROR,
			 "Unhandled PHYIF command type=0x%02x\n", cmd->type);
		rc = -ENODEV;
	}

	return rc;
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
	struct trxcon_phyif_burst_ind bi;
	uint8_t buf[TRXD_BUF_SIZE];
	ssize_t read_len;
	sbit_t *burst;

	read_len = read(ofd->fd, buf, sizeof(buf));
	if (read_len <= 0) {
		strerror_r(errno, (char *)buf, sizeof(buf));
		LOGPFSMSL(trx->fi, DTRXD, LOGL_ERROR,
			  "read() failed on TRXD with rc=%zd (%s)\n",
			  read_len, (const char *)buf);
		return read_len;
	}

	if (read_len < TRXDv0_HDR_LEN) {
		LOGPFSMSL(trx->fi, DTRXD, LOGL_ERROR,
			  "Got malformed TRXD PDU (short length=%zd)\n", read_len);
		return -EINVAL;
	}

	if ((buf[0] >> 4) != 0) {
		LOGPFSMSL(trx->fi, DTRXD, LOGL_ERROR,
			  "Got TRXD PDU with unexpected version\n");
		return -ENOTSUP;
	}

	read_len -= TRXDv0_HDR_LEN;
	switch (read_len) {
	/* TRXDv0 PDUs may have 2 dummy bytes at the end */
	case GSM_NBITS_NB_GMSK_BURST + 2:
	case GSM_NBITS_NB_8PSK_BURST + 2:
		read_len -= 2;
		break;
	case GSM_NBITS_NB_GMSK_BURST:
	case GSM_NBITS_NB_8PSK_BURST:
		break;
	default:
		LOGPFSMSL(trx->fi, DTRXD, LOGL_ERROR,
			  "Got TRXD PDU unexpected burst length=%zd\n", read_len);
		return -EINVAL;
	}

	burst = (sbit_t *)&buf[8];

	bi = (struct trxcon_phyif_burst_ind) {
		.tn = buf[0] & 0x07,
		.fn = osmo_load32be(buf + 1),
		.rssi = -(int8_t) buf[5],
		.toa256 = (int16_t) (buf[6] << 8) | buf[7],
		.burst = burst, /* at least GSM_NBITS_NB_GMSK_BURST */
		.burst_len = read_len,
	};

	/* Convert ubits {254..0} to sbits {-127..127} in-place */
	for (unsigned int i = 0; i < bi.burst_len; i++) {
		if (buf[8 + i] == 255)
			burst[i] = -127;
		else
			burst[i] = 127 - buf[8 + i];
	}

	if (bi.fn >= GSM_TDMA_HYPERFRAME) {
		LOGPFSMSL(trx->fi, DTRXD, LOGL_ERROR, "Illegal FN %u\n", bi.fn);
		return -EINVAL;
	}

	LOGPFSMSL(trx->fi, DTRXD, LOGL_DEBUG,
		  "RX burst tn=%u fn=%u rssi=%d toa=%d\n",
		  bi.tn, bi.fn, bi.rssi, bi.toa256);

	trxcon_phyif_handle_burst_ind(trx->priv, &bi);

	struct trxcon_phyif_rts_ind rts = {
		.fn = GSM_TDMA_FN_SUM(bi.fn, trx->fn_advance),
		.tn = bi.tn,
	};

	trxcon_phyif_handle_rts_ind(trx->priv, &rts);

	return 0;
}

int trx_if_handle_phyif_burst_req(struct trx_instance *trx,
				  const struct trxcon_phyif_burst_req *br)
{
	uint8_t buf[TRXD_BUF_SIZE];
	size_t length;

	/**
	 * We must be sure that we have clock,
	 * and we have sent all control data
	 *
	 * TODO: introduce proper state machines for both
	 *       transceiver and its TRXC interface.
	 */
#if 0
	if (trx->fi->state != TRX_STATE_ACTIVE) {
		LOGPFSMSL(trx->fi, DTRXD, LOGL_ERROR,
			  "Ignoring TX data, transceiver isn't ready\n");
		return -EAGAIN;
	}
#endif

	LOGPFSMSL(trx->fi, DTRXD, LOGL_DEBUG,
		  "TX burst tn=%u fn=%u pwr=%u\n",
		  br->tn, br->fn, br->pwr);

	buf[0] = br->tn;
	osmo_store32be(br->fn, buf + 1);
	buf[5] = br->pwr;
	length = 6;

	/* Copy ubits {0,1} */
	if (br->burst_len != 0) {
		memcpy(buf + 6, br->burst, br->burst_len);
		length += br->burst_len;
	}

	/* Send data to transceiver */
	send(trx->trx_ofd_data.fd, buf, length, 0);

	return 0;
}

/* Init TRX interface (TRXC, TRXD sockets and FSM) */
struct trx_instance *trx_if_open(const struct trx_if_params *params)
{
	const unsigned int offset = params->instance * 2;
	struct trx_instance *trx;
	struct osmo_fsm_inst *fi;
	int rc;

	LOGPFSML(params->parent_fi, LOGL_NOTICE,
		 "Init transceiver interface (%s:%u/%u)\n",
		 params->remote_host, params->base_port,
		 params->instance);

	/* Allocate a new dedicated state machine */
	fi = osmo_fsm_inst_alloc_child(&trx_fsm, params->parent_fi,
				       params->parent_term_event);
	if (fi == NULL) {
		LOGPFSML(params->parent_fi, LOGL_ERROR,
			 "Failed to allocate an instance of FSM '%s'\n",
			 trx_fsm.name);
		return NULL;
	}

	trx = talloc_zero(fi, struct trx_instance);
	if (!trx) {
		LOGPFSML(params->parent_fi, LOGL_ERROR, "Failed to allocate memory\n");
		osmo_fsm_inst_free(fi);
		return NULL;
	}

	/* Initialize CTRL queue */
	INIT_LLIST_HEAD(&trx->trx_ctrl_list);

	/* Open sockets */
	rc = trx_udp_open(trx, &trx->trx_ofd_ctrl, /* TRXC */
			  params->local_host, params->base_port + 101 + offset,
			  params->remote_host, params->base_port + 1 + offset,
			  trx_ctrl_read_cb);
	if (rc < 0)
		goto udp_error;

	rc = trx_udp_open(trx, &trx->trx_ofd_data, /* TRXD */
			  params->local_host, params->base_port + 102 + offset,
			  params->remote_host, params->base_port + 2 + offset,
			  trx_data_rx_cb);
	if (rc < 0)
		goto udp_error;

	trx->fn_advance = params->fn_advance;
	trx->priv = params->priv;
	fi->priv = trx;
	trx->fi = fi;

	return trx;

udp_error:
	LOGPFSML(params->parent_fi, LOGL_ERROR, "Couldn't establish UDP connection\n");
	osmo_fsm_inst_free(trx->fi);
	return NULL;
}

/* Flush pending control messages */
static void trx_if_flush_ctrl(struct trx_instance *trx)
{
	struct trx_ctrl_msg *tcm;

	/* Reset state machine */
	osmo_fsm_inst_state_chg(trx->fi, TRX_STATE_IDLE, 0, 0);

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
	if (trx == NULL || trx->fi == NULL)
		return;
	osmo_fsm_inst_term(trx->fi, OSMO_FSM_TERM_REQUEST, NULL);
}

static void trx_fsm_cleanup_cb(struct osmo_fsm_inst *fi,
			       enum osmo_fsm_term_cause cause)
{
	static const char cmd_poweroff[] = "CMD POWEROFF";
	struct trx_instance *trx = fi->priv;

	/* May be unallocated due to init error */
	if (!trx)
		return;

	LOGPFSML(fi, LOGL_NOTICE, "Shutdown transceiver interface\n");

	/* Abort TRXC response timer (if pending) */
	osmo_timer_del(&trx->trx_ctrl_timer);

	/* Flush CTRL message list */
	trx_if_flush_ctrl(trx);

	/* Power off if the transceiver is up */
	if (trx->powered_up && trx->trx_ofd_ctrl.fd >= 0)
		send(trx->trx_ofd_ctrl.fd, &cmd_poweroff[0], sizeof(cmd_poweroff), 0);

	/* Close sockets */
	trx_udp_close(&trx->trx_ofd_ctrl);
	trx_udp_close(&trx->trx_ofd_data);

	/* Free memory */
	trx->fi->priv = NULL;
	talloc_free(trx);
}

static __attribute__((constructor)) void on_dso_load(void)
{
	OSMO_ASSERT(osmo_fsm_register(&trx_fsm) == 0);
}
