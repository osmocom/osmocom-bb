/* BTSAP socket interface of layer2/3 stack */

/* (C) 2010 by Holger Hans Peter Freyther
 * (C) 2010 by Harald Welte <laforge@gnumonks.org>
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

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/sap_interface.h>

#include <osmocom/core/utils.h>

#include <sys/socket.h>
#include <sys/un.h>

#include <arpa/inet.h>

#define _GNU_SOURCE
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define GSM_SAP_LENGTH 300
#define GSM_SAP_HEADROOM 32

static int sap_read(struct bsc_fd *fd)
{
	struct msgb *msg;
	u_int16_t len;
	int rc;
	struct osmocom_ms *ms = (struct osmocom_ms *) fd->data;

	msg = msgb_alloc_headroom(GSM_SAP_LENGTH+GSM_SAP_HEADROOM, GSM_SAP_HEADROOM, "Layer2");
	if (!msg) {
		LOGP(DSAP, LOGL_ERROR, "Failed to allocate msg.\n");
		return -ENOMEM;
	}

	rc = read(fd->fd, &len, sizeof(len));
	if (rc < sizeof(len)) {
		fprintf(stderr, "SAP socket failed\n");
		msgb_free(msg);
		if (rc >= 0)
			rc = -EIO;
		sap_close(ms);
		return rc;
	}

	len = ntohs(len);
	if (len > GSM_SAP_LENGTH) {
		LOGP(DSAP, LOGL_ERROR, "Length is too big: %u\n", len);
		msgb_free(msg);
		return -EINVAL;
	}


	msg->l1h = msgb_put(msg, len);
	rc = read(fd->fd, msg->l1h, msgb_l1len(msg));
	if (rc != msgb_l1len(msg)) {
		LOGP(DSAP, LOGL_ERROR, "Can not read data: len=%d rc=%d "
		     "errno=%d\n", len, rc, errno);
		msgb_free(msg);
		return rc;
	}

	if (ms->sap_entity.msg_handler)
		ms->sap_entity.msg_handler(msg, ms);

	return 0;
}

static int sap_write(struct bsc_fd *fd, struct msgb *msg)
{
	int rc;

	if (fd->fd <= 0)
		return -EINVAL;

	rc = write(fd->fd, msg->data, msg->len);
	if (rc != msg->len) {
		LOGP(DSAP, LOGL_ERROR, "Failed to write data: rc: %d\n", rc);
		return rc;
	}

	return 0;
}

int sap_open(struct osmocom_ms *ms, const char *socket_path)
{
	int rc;
	struct sockaddr_un local;

	ms->sap_wq.bfd.fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (ms->sap_wq.bfd.fd < 0) {
		fprintf(stderr, "Failed to create unix domain socket.\n");
		return ms->sap_wq.bfd.fd;
	}

	local.sun_family = AF_UNIX;
	strncpy(local.sun_path, socket_path, sizeof(local.sun_path));
	local.sun_path[sizeof(local.sun_path) - 1] = '\0';

	rc = connect(ms->sap_wq.bfd.fd, (struct sockaddr *) &local,
		     sizeof(local.sun_family) + strlen(local.sun_path));
	if (rc < 0) {
		fprintf(stderr, "Failed to connect to '%s'.\n", local.sun_path);
		close(ms->sap_wq.bfd.fd);
		return rc;
	}

	write_queue_init(&ms->sap_wq, 100);
	ms->sap_wq.bfd.data = ms;
	ms->sap_wq.bfd.when = BSC_FD_READ;
	ms->sap_wq.read_cb = sap_read;
	ms->sap_wq.write_cb = sap_write;

	rc = bsc_register_fd(&ms->sap_wq.bfd);
	if (rc != 0) {
		fprintf(stderr, "Failed to register fd.\n");
		return rc;
	}

	return 0;
}

int sap_close(struct osmocom_ms *ms)
{
	if (ms->sap_wq.bfd.fd <= 0)
		return -EINVAL;

	close(ms->sap_wq.bfd.fd);
	ms->sap_wq.bfd.fd = -1;
	bsc_unregister_fd(&ms->sap_wq.bfd);

	return 0;
}

int osmosap_send(struct osmocom_ms *ms, struct msgb *msg)
{
	uint16_t *len;

	if (ms->sap_wq.bfd.fd <= 0)
		return -EINVAL;

	DEBUGP(DSAP, "Sending: '%s'\n", hexdump(msg->data, msg->len));

	if (msg->l1h != msg->data)
		LOGP(DSAP, LOGL_ERROR, "Message SAP header != Message Data\n");
	
	/* prepend 16bit length before sending */
	len = (uint16_t *) msgb_push(msg, sizeof(*len));
	*len = htons(msg->len - sizeof(*len));

	if (write_queue_enqueue(&ms->sap_wq, msg) != 0) {
		LOGP(DSAP, LOGL_ERROR, "Failed to enqueue msg.\n");
		msgb_free(msg);
		return -1;
	}

	return 0;
}

/* register message handler for messages that are sent from L2->L3 */
int osmosap_register_handler(struct osmocom_ms *ms, osmosap_cb_t cb)
{
	ms->sap_entity.msg_handler = cb;

	return 0;
}

