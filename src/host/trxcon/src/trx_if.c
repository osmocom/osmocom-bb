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

#include <osmocom/bb/trxcon/l1sched.h>
#include <osmocom/bb/trxcon/l1ctl.h>
#include <osmocom/bb/trxcon/trxcon.h>
#include <osmocom/bb/trxcon/trx_if.h>
#include <osmocom/bb/trxcon/logging.h>

static struct value_string trx_evt_names[] = {
	{ 0, NULL } /* no events? */
};

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
	.event_names = trx_evt_names,
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
	if (trx->powered_up) {
		/* FIXME: this should be handled by the FSM, not here! */
		LOGP(DTRX, LOGL_ERROR, "Suppressing POWERON as we're already powered up\n");
		return -EAGAIN;
	}
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

int trx_if_cmd_setslot(struct trx_instance *trx, uint8_t tn,
		       enum gsm_phys_chan_config pchan)
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

	return trx_ctrl_cmd(trx, 1, "SETSLOT", "%u %u", tn, chan_types[pchan]);
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

int trx_if_cmd_rxtune(struct trx_instance *trx, uint16_t band_arfcn)
{
	uint16_t freq10;

	/* RX is downlink on MS side */
	freq10 = gsm_arfcn2freq10(band_arfcn, 0);
	if (freq10 == 0xffff) {
		LOGP(DTRX, LOGL_ERROR, "ARFCN %d not defined\n", band_arfcn);
		return -ENOTSUP;
	}

	return trx_ctrl_cmd(trx, 1, "RXTUNE", "%u", freq10 * 100);
}

int trx_if_cmd_txtune(struct trx_instance *trx, uint16_t band_arfcn)
{
	uint16_t freq10;

	/* TX is uplink on MS side */
	freq10 = gsm_arfcn2freq10(band_arfcn, 1);
	if (freq10 == 0xffff) {
		LOGP(DTRX, LOGL_ERROR, "ARFCN %d not defined\n", band_arfcn);
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

int trx_if_cmd_measure(struct trx_instance *trx,
	uint16_t band_arfcn_start, uint16_t band_arfcn_stop)
{
	uint16_t freq10;

	/* Update ARFCN range for measurement */
	trx->pm_band_arfcn_start = band_arfcn_start;
	trx->pm_band_arfcn_stop = band_arfcn_stop;

	/* Calculate a frequency for current ARFCN (DL) */
	freq10 = gsm_arfcn2freq10(band_arfcn_start, 0);
	if (freq10 == 0xffff) {
		LOGP(DTRX, LOGL_ERROR, "ARFCN %d not defined\n", band_arfcn_start);
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

	/* Check received ARFCN against expected */
	band_arfcn = gsm_freq102arfcn((uint16_t) freq10, 0);
	if (band_arfcn != trx->pm_band_arfcn_start) {
		LOGP(DTRX, LOGL_ERROR, "Power measurement error: "
			"response ARFCN=%u doesn't match expected ARFCN=%u\n",
			band_arfcn & ~ARFCN_FLAG_MASK,
			trx->pm_band_arfcn_start & ~ARFCN_FLAG_MASK);
		return;
	}

	/* Send L1CTL_PM_CONF */
	l1ctl_tx_pm_conf(trx->l1l, band_arfcn, dbm,
		band_arfcn == trx->pm_band_arfcn_stop);

	/* Schedule a next measurement */
	if (band_arfcn != trx->pm_band_arfcn_stop)
		trx_if_cmd_measure(trx, ++band_arfcn, trx->pm_band_arfcn_stop);
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

int trx_if_cmd_setta(struct trx_instance *trx, int8_t ta)
{
	return trx_ctrl_cmd(trx, 0, "SETTA", "%d", ta);
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

int trx_if_cmd_setfh(struct trx_instance *trx, uint8_t hsn,
	uint8_t maio, uint16_t *ma, size_t ma_len)
{
	/* Reserve some room for CMD SETFH <HSN> <MAIO> */
	char ma_buf[TRXC_BUF_SIZE - 24];
	size_t ma_buf_len = sizeof(ma_buf) - 1;
	uint16_t rx_freq, tx_freq;
	char *ptr;
	int i, rc;

	/* Make sure that Mobile Allocation has at least one ARFCN */
	if (!ma_len || ma == NULL) {
		LOGP(DTRX, LOGL_ERROR, "Mobile Allocation is empty?!?\n");
		return -EINVAL;
	}

	/* Compose a sequence of Rx/Tx frequencies (mobile allocation) */
	for (i = 0, ptr = ma_buf; i < ma_len; i++) {
		/* Convert ARFCN to a pair of Rx/Tx frequencies (Hz * 10) */
		rx_freq = gsm_arfcn2freq10(ma[i], 0); /* Rx: Downlink */
		tx_freq = gsm_arfcn2freq10(ma[i], 1); /* Tx: Uplink */
		if (rx_freq == 0xffff || tx_freq == 0xffff) {
			LOGP(DTRX, LOGL_ERROR, "Failed to convert ARFCN %u "
			     "to a pair of Rx/Tx frequencies\n",
			     ma[i] & ~ARFCN_FLAG_MASK);
			return -EINVAL;
		}

		/* Append a pair of Rx/Tx frequencies (in kHz) to the buffer */
		rc = snprintf(ptr, ma_buf_len, "%u %u ", rx_freq * 100, tx_freq * 100);
		if (rc < 0 || rc > ma_buf_len) { /* Prevent buffer overflow */
			LOGP(DTRX, LOGL_ERROR, "Not enough room to encode "
			     "Mobile Allocation (N=%zu)\n", ma_len);
			return -ENOSPC;
		}

		/* Move pointer */
		ma_buf_len -= rc;
		ptr += rc;
	}

	/* Overwrite the last space */
	*(ptr - 1) = '\0';

	return trx_ctrl_cmd(trx, 1, "SETFH", "%u %u %s", hsn, maio, ma_buf);
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
		LOGP(DTRX, LOGL_ERROR, "read() failed with rc=%zd\n", read_len);
		return read_len;
	}
	buf[read_len] = '\0';

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
	if (!strncmp(tcm->cmd + 4, "POWERON", 7)) {
		trx->powered_up = true;
		osmo_fsm_inst_state_chg(trx->fsm, TRX_STATE_ACTIVE, 0, 0);
	}
	else if (!strncmp(tcm->cmd + 4, "POWEROFF", 8)) {
		trx->powered_up = false;
		osmo_fsm_inst_state_chg(trx->fsm, TRX_STATE_IDLE, 0, 0);
	}
	else if (!strncmp(tcm->cmd + 4, "MEASURE", 7))
		trx_if_measure_rsp_cb(trx, buf + 14);
	else if (!strncmp(tcm->cmd + 4, "ECHO", 4))
		osmo_fsm_inst_state_chg(trx->fsm, TRX_STATE_IDLE, 0, 0);
	else
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
	struct l1sched_meas_set meas;
	uint8_t buf[TRXD_BUF_SIZE];
	sbit_t bits[148];
	int8_t rssi, tn;
	int16_t toa256;
	uint32_t fn;
	ssize_t read_len;

	read_len = read(ofd->fd, buf, sizeof(buf));
	if (read_len <= 0) {
		LOGP(DTRXD, LOGL_ERROR, "read() failed with rc=%zd\n", read_len);
		return read_len;
	}

	if (read_len < (8 + 148)) { /* TRXDv0 header + GMSK burst */
		LOGP(DTRXD, LOGL_ERROR, "Got data message with invalid "
			"length '%zd'\n", read_len);
		return -EINVAL;
	}

	tn = buf[0];
	fn = osmo_load32be(buf + 1);
	rssi = -(int8_t) buf[5];
	toa256 = ((int16_t) (buf[6] << 8) | buf[7]);

	/* Copy and convert bits {254..0} to sbits {-127..127} */
	osmo_ubit2sbit(bits, buf + 8, 148);

	if (tn >= 8) {
		LOGP(DTRXD, LOGL_ERROR, "Illegal TS %d\n", tn);
		return -EINVAL;
	}

	if (fn >= 2715648) {
		LOGP(DTRXD, LOGL_ERROR, "Illegal FN %u\n", fn);
		return -EINVAL;
	}

	LOGP(DTRXD, LOGL_DEBUG, "RX burst tn=%u fn=%u rssi=%d toa=%d\n",
		tn, fn, rssi, toa256);

	/* Group the measurements together */
	meas = (struct l1sched_meas_set) {
		.toa256 = toa256,
		.rssi = rssi,
		.fn = fn,
	};

	/* Poke scheduler */
	l1sched_handle_rx_burst(trx->sched, tn, fn, bits, 148, &meas);

	/* Correct local clock counter */
	if (fn % 51 == 0)
		l1sched_clck_handle(trx->sched, fn);

	return 0;
}

int trx_if_tx_burst(struct trx_instance *trx,
		    const struct l1sched_burst_req *br)
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
	if (trx->fsm->state != TRX_STATE_ACTIVE) {
		LOGP(DTRXD, LOGL_ERROR, "Ignoring TX data, "
			"transceiver isn't ready\n");
		return -EAGAIN;
	}
#endif

	LOGP(DTRXD, LOGL_DEBUG, "TX burst tn=%u fn=%u pwr=%u\n",
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
struct trx_instance *trx_if_open(void *tall_ctx,
	const char *local_host, const char *remote_host,
	uint16_t base_port)
{
	struct trx_instance *trx;
	int rc;

	LOGP(DTRX, LOGL_NOTICE, "Init transceiver interface "
		"(%s:%u)\n", remote_host, base_port);

	/* Try to allocate memory */
	trx = talloc_zero(tall_ctx, struct trx_instance);
	if (!trx) {
		LOGP(DTRX, LOGL_ERROR, "Failed to allocate memory\n");
		return NULL;
	}

	/* Allocate a new dedicated state machine */
	trx->fsm = osmo_fsm_inst_alloc(&trx_fsm, trx,
		NULL, LOGL_DEBUG, "trx_interface");
	if (trx->fsm == NULL) {
		LOGP(DTRX, LOGL_ERROR, "Failed to allocate an instance "
			"of FSM '%s'\n", trx_fsm.name);
		talloc_free(trx);
		return NULL;
	}

	/* Initialize CTRL queue */
	INIT_LLIST_HEAD(&trx->trx_ctrl_list);

	/* Open sockets */
	rc = trx_udp_open(trx, &trx->trx_ofd_ctrl, local_host,
		base_port + 101, remote_host, base_port + 1, trx_ctrl_read_cb);
	if (rc < 0)
		goto udp_error;

	rc = trx_udp_open(trx, &trx->trx_ofd_data, local_host,
		base_port + 102, remote_host, base_port + 2, trx_data_rx_cb);
	if (rc < 0)
		goto udp_error;

	return trx;

udp_error:
	LOGP(DTRX, LOGL_ERROR, "Couldn't establish UDP connection\n");
	osmo_fsm_inst_free(trx->fsm);
	talloc_free(trx);
	return NULL;
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
	trx_udp_close(&trx->trx_ofd_ctrl);
	trx_udp_close(&trx->trx_ofd_data);

	/* Free memory */
	osmo_fsm_inst_free(trx->fsm);
	talloc_free(trx);
}

static __attribute__((constructor)) void on_dso_load(void)
{
	OSMO_ASSERT(osmo_fsm_register(&trx_fsm) == 0);
}
