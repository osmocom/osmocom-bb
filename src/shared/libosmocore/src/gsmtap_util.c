/* GSMTAP output for Osmocom Layer2 (will only work on the host PC) */
/*
 * (C) 2010 by Harald Welte <laforge@gnumonks.org>
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

#include "../config.h"

#ifdef HAVE_SYS_SELECT_H

#include <osmocom/core/gsmtap_util.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/gsmtap.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/select.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/rsl.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

static struct osmo_fd gsmtap_bfd = { .fd = -1 };
static struct osmo_fd gsmtap_sink_bfd = { .fd = -1 };
static LLIST_HEAD(gsmtap_txqueue);

uint8_t chantype_rsl2gsmtap(uint8_t rsl_chantype, uint8_t link_id)
{
	uint8_t ret = GSMTAP_CHANNEL_UNKNOWN;

	switch (rsl_chantype) {
	case RSL_CHAN_Bm_ACCHs:
		ret = GSMTAP_CHANNEL_TCH_F;
		break;
	case RSL_CHAN_Lm_ACCHs:
		ret = GSMTAP_CHANNEL_TCH_H;
		break;
	case RSL_CHAN_SDCCH4_ACCH:
		ret = GSMTAP_CHANNEL_SDCCH4;
		break;
	case RSL_CHAN_SDCCH8_ACCH:
		ret = GSMTAP_CHANNEL_SDCCH8;
		break;
	case RSL_CHAN_BCCH:
		ret = GSMTAP_CHANNEL_BCCH;
		break;
	case RSL_CHAN_RACH:
		ret = GSMTAP_CHANNEL_RACH;
		break;
	case RSL_CHAN_PCH_AGCH:
		/* it could also be AGCH... */
		ret = GSMTAP_CHANNEL_PCH;
		break;
	}

	if (link_id & 0x40)
		ret |= GSMTAP_CHANNEL_ACCH;

	return ret;
}

/* receive a message from L1/L2 and put it in GSMTAP */
struct msgb *gsmtap_makemsg(uint16_t arfcn, uint8_t ts, uint8_t chan_type,
			    uint8_t ss, uint32_t fn, int8_t signal_dbm,
			    uint8_t snr, const uint8_t *data, unsigned int len)
{
	struct msgb *msg;
	struct gsmtap_hdr *gh;
	uint8_t *dst;

	msg = msgb_alloc(sizeof(*gh) + len, "gsmtap_tx");
	if (!msg)
		return NULL;

	gh = (struct gsmtap_hdr *) msgb_put(msg, sizeof(*gh));

	gh->version = GSMTAP_VERSION;
	gh->hdr_len = sizeof(*gh)/4;
	gh->type = GSMTAP_TYPE_UM;
	gh->timeslot = ts;
	gh->sub_slot = ss;
	gh->arfcn = htons(arfcn);
	gh->snr_db = snr;
	gh->signal_dbm = signal_dbm;
	gh->frame_number = htonl(fn);
	gh->sub_type = chan_type;
	gh->antenna_nr = 0;

	dst = msgb_put(msg, len);
	memcpy(dst, data, len);

	return msg;
}

/* receive a message from L1/L2 and put it in GSMTAP */
int gsmtap_sendmsg(uint16_t arfcn, uint8_t ts, uint8_t chan_type, uint8_t ss,
		   uint32_t fn, int8_t signal_dbm, uint8_t snr,
		   const uint8_t *data, unsigned int len)
{
	struct msgb *msg;

	/* gsmtap was never initialized, so don't try to send anything */
	if (gsmtap_bfd.fd == -1)
		return 0;

	msg = gsmtap_makemsg(arfcn, ts, chan_type, ss, fn, signal_dbm,
			     snr, data, len);
	if (!msg)
		return -ENOMEM;

	msgb_enqueue(&gsmtap_txqueue, msg);
	gsmtap_bfd.when |= BSC_FD_WRITE;

	return 0;
}

/* Callback from select layer if we can write to the socket */
static int gsmtap_fd_cb(struct osmo_fd *fd, unsigned int flags)
{
	struct msgb *msg;
	int rc;

	if (!(flags & BSC_FD_WRITE))
		return 0;

	msg = msgb_dequeue(&gsmtap_txqueue);
	if (!msg) {
		/* no more messages in the queue, disable READ cb */
		gsmtap_bfd.when = 0;
		return 0;
	}
	rc = write(gsmtap_bfd.fd, msg->data, msg->len);
	if (rc < 0) {
		perror("writing msgb to gsmtap fd");
		msgb_free(msg);
		return rc;
	}
	if (rc != msg->len) {
		perror("short write to gsmtap fd");
		msgb_free(msg);
		return -EIO;
	}

	msgb_free(msg);
	return 0;
}

int gsmtap_init(uint32_t dst_ip)
{
	int rc;
	struct sockaddr_in sin;

	sin.sin_family = AF_INET;
	sin.sin_port = htons(GSMTAP_UDP_PORT);
	sin.sin_addr.s_addr = htonl(dst_ip);

	/* create socket */
	rc = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (rc < 0) {
		perror("creating UDP socket");
		return rc;
	}
	gsmtap_bfd.fd = rc;
	rc = connect(rc, (struct sockaddr *)&sin, sizeof(sin));
	if (rc < 0) {
		perror("connecting UDP socket");
		close(gsmtap_bfd.fd);
		gsmtap_bfd.fd = -1;
		return rc;
	}

	gsmtap_bfd.when = BSC_FD_WRITE;
	gsmtap_bfd.cb = gsmtap_fd_cb;
	gsmtap_bfd.data = NULL;

	return osmo_fd_register(&gsmtap_bfd);
}

/* Callback from select layer if we can read from the sink socket */
static int gsmtap_sink_fd_cb(struct osmo_fd *fd, unsigned int flags)
{
	int rc;
	uint8_t buf[4096];

	if (!(flags & BSC_FD_READ))
		return 0;

	rc = read(fd->fd, buf, sizeof(buf));
	if (rc < 0) {
		perror("reading from gsmtap sink fd");
		return rc;
	}
	/* simply discard any data arriving on the socket */

	return 0;
}

/* Create a local 'gsmtap sink' avoiding the UDP packets being rejected
 * with ICMP reject messages */
int gsmtap_sink_init(uint32_t bind_ip)
{
	int rc;
	struct sockaddr_in sin;

	sin.sin_family = AF_INET;
	sin.sin_port = htons(GSMTAP_UDP_PORT);
	sin.sin_addr.s_addr = htonl(bind_ip);

	rc = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (rc < 0) {
		perror("creating UDP socket");
		return rc;
	}
	gsmtap_sink_bfd.fd = rc;
	rc = bind(rc, (struct sockaddr *)&sin, sizeof(sin));
	if (rc < 0) {
		perror("binding UDP socket");
		close(gsmtap_sink_bfd.fd);
		gsmtap_sink_bfd.fd = -1;
		return rc;
	}

	gsmtap_sink_bfd.when = BSC_FD_READ;
	gsmtap_sink_bfd.cb = gsmtap_sink_fd_cb;
	gsmtap_sink_bfd.data = NULL;

	return osmo_fd_register(&gsmtap_sink_bfd);

}

#endif /* HAVE_SYS_SELECT_H */
