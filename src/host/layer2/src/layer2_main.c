/* Main method of the layer2 stack */
/* (C) 2010 by Holger Hans Peter Freyther
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
#include <osmocom/msgb.h>
#include <osmocom/talloc.h>

#include <arpa/inet.h>

#include <sys/socket.h>
#include <sys/un.h>

#define _GNU_SOURCE
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "gsmtap_util.h"

#define GSM_L2_LENGTH 256

static void *l2_ctx = NULL;
static char *socket_path = "/tmp/osmocom_l2";
static struct osmocom_ms *ms = NULL;

static int layer2_read(struct bsc_fd *fd, unsigned int flags)
{
	struct msgb *msg;
	u_int16_t len;
	int rc;

	msg = msgb_alloc(GSM_L2_LENGTH, "Layer2");
	if (!msg) {
		fprintf(stderr, "Failed to allocate msg.\n");
		return -1;
	}

	rc = read(fd->fd, &len, sizeof(len));
	if (rc < sizeof(len)) {
		fprintf(stderr, "Short read. Error.\n");
		exit(2);
	}

	if (ntohs(len) > GSM_L2_LENGTH) {
		fprintf(stderr, "Length is too big: %u\n", ntohs(len));
		msgb_free(msg);
		return -1;
	}


	/* blocking read for the poor... we can starve in here... */
	msg->l2h = msgb_put(msg, ntohs(len));
	rc = read(fd->fd, msg->l2h, msgb_l2len(msg));
	if (rc != msgb_l2len(msg)) {
		fprintf(stderr, "Can not read data: rc: %d errno: %d\n", rc, errno);
		msgb_free(msg);
		return -1;
	}

	osmo_recv((struct osmocom_ms *) fd->data, msg);
	msgb_free(msg);
	return 0;
}

int osmo_send_l1(struct osmocom_ms *ms, struct msgb *msg)
{
	int rc;
	uint16_t *len;

	LOGP(DMUX, LOGL_INFO, "Sending: '%s'\n", hexdump(msg->data, msg->len));

	
	len = (uint16_t *) msgb_push(msg, sizeof(*len));
	*len = htons(msg->len - sizeof(*len));

	/* TODO: just enqueue the message and wait for ready write.. */
	rc = write(ms->bfd.fd, msg->data, msg->len);
	if (rc != msg->len) {
		fprintf(stderr, "Failed to write data: rc: %d\n", rc);
		msgb_free(msg);
		return -1;
	}

	msgb_free(msg);
	return 0;
}

static void print_usage()
{
	printf("Usage: ./layer2\n");
}

static void print_help()
{
	printf(" Some help...\n");
	printf("  -h --help this text\n");
	printf("  -s --socket /tmp/osmocom_l2. Path to the unix domain socket\n");
	printf("  -a --arfcn NR. The ARFCN to be used for layer2.\n");
}

static void handle_options(int argc, char **argv)
{
	while (1) {
		int option_index = 0, c;
		static struct option long_options[] = {
			{"help", 0, 0, 'h'},
			{"socket", 1, 0, 's'},
			{"arfcn", 1, 0, 'a'},
			{0, 0, 0, 0},
		};

		c = getopt_long(argc, argv, "hs:a:",
				long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			print_usage();
			print_help();
			exit(0);
			break;
		case 's':
			socket_path = talloc_strdup(l2_ctx, optarg);
			break;
		case 'a':
			ms->arfcn = atoi(optarg);
			break;
		default:
			break;
		}
	}
}

int main(int argc, char **argv)
{
	int rc;
	struct sockaddr_un local;
	struct sockaddr_in gsmtap;

	l2_ctx = talloc_named_const(NULL, 1, "layer2 context");

	ms = talloc_zero(l2_ctx, struct osmocom_ms);
	if (!ms) {
		fprintf(stderr, "Failed to allocate MS\n");
		exit(1);
	}

	ms->arfcn = 871;

	handle_options(argc, argv);

	ms->bfd.fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (ms->bfd.fd < 0) {
		fprintf(stderr, "Failed to create unix domain socket.\n");
		exit(1);
	}

	local.sun_family = AF_UNIX;
	strncpy(local.sun_path, socket_path, sizeof(local.sun_path));
	local.sun_path[sizeof(local.sun_path) - 1] = '\0';

	rc = connect(ms->bfd.fd, (struct sockaddr *) &local,
		     sizeof(local.sun_family) + strlen(local.sun_path));
	if (rc < 0) {
		fprintf(stderr, "Failed to connect to '%s'.\n", local.sun_path);
		exit(1);
	}

	ms->bfd.when = BSC_FD_READ;
	ms->bfd.cb = layer2_read;
	ms->bfd.data = ms;

	if (bsc_register_fd(&ms->bfd) != 0) {
		fprintf(stderr, "Failed to register fd.\n");
		exit(1);
	}

	rc = gsmtap_init();
	if (rc < 0) {
		fprintf(stderr, "Failed during gsmtap_init()\n");
		exit(1);
	}

	while (1) {
		bsc_select_main(0);
	}


	return 0;
}
