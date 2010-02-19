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

#include <osmocom/osmocom_layer2.h>
#include <osmocom/osmocom_data.h>
#include <osmocom/debug.h>
#include <osmocom/gsm_04_08.h>
#include <osmocom/gsmtap.h>
#include <osmocom/msgb.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

static struct bsc_fd gsmtap_bfd;
static LLIST_HEAD(gsmtap_txqueue);

/* receive a message from L1/L2 and put it in GSMTAP */
int gsmtap_sendmsg(uint8_t ts, uint16_t arfcn, uint32_t fn,
		   const uint8_t *data, unsigned int len)
{
	struct msgb *msg;
	struct gsmtap_hdr *gh;
	uint8_t *dst;

	msg = msgb_alloc(sizeof(*gh) + len, "gsmtap_tx");
	if (!msg)
		return -ENOMEM;

	gh = (struct gsmtap_hdr *) msgb_put(msg, sizeof(*gh));

	gh->version = GSMTAP_VERSION;
	gh->hdr_len = sizeof(*gh)/4;
	gh->type = GSMTAP_TYPE_UM;
	gh->timeslot = ts;
	gh->arfcn = arfcn;
	gh->noise_db = 0;
	gh->signal_db = 0;
	gh->frame_number = fn;
	gh->burst_type = 0;
	gh->antenna_nr = 0;

	dst = msgb_put(msg, len);
	memcpy(dst, data, len);

	msgb_enqueue(&gsmtap_txqueue, msg);
	gsmtap_bfd.when |= BSC_FD_WRITE;

	return 0;
}

/* Callback from select layer if we can write to the socket */
static int gsmtap_fd_cb(struct bsc_fd *fd, unsigned int flags)
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

int gsmtap_init(struct sockaddr_in *sin)
{
	/* FIXME: create socket */
	//gsmtap_bfd.fd = 

	gsmtap_bfd.when = BSC_FD_WRITE;
	gsmtap_bfd.cb = gsmtap_fd_cb;
	gsmtap_bfd.data = NULL;

	return bsc_register_fd(&gsmtap_bfd);
}
