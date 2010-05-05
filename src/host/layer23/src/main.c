/* Main method of the layer2/3 stack */

/* (C) 2010 by Holger Hans Peter Freyther
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

#include <osmocom/osmocom_data.h>
#include <osmocom/l1ctl.h>
#include <osmocom/layer3.h>
#include <osmocom/lapdm.h>
#include <osmocom/gsmtap_util.h>
#include <osmocom/logging.h>
#include <osmocom/l23_app.h>

#include <osmocore/msgb.h>
#include <osmocore/talloc.h>
#include <osmocore/select.h>

#include <arpa/inet.h>

#include <sys/socket.h>
#include <sys/un.h>

#define _GNU_SOURCE
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

struct log_target *stderr_target;

#define GSM_L2_LENGTH 256

void *l23_ctx = NULL;
static char *socket_path = "/tmp/osmocom_l2";
static struct osmocom_ms *ms = NULL;
static uint32_t gsmtap_ip = 0;
int (*l23_app_work) (struct osmocom_ms *ms) = NULL;
int (*l23_app_exit) (struct osmocom_ms *ms) = NULL;

static int layer2_read(struct bsc_fd *fd)
{
	struct msgb *msg;
	u_int16_t len;
	int rc;

	msg = msgb_alloc(GSM_L2_LENGTH, "Layer2");
	if (!msg) {
		LOGP(DL1C, LOGL_ERROR, "Failed to allocate msg.\n");
		return -1;
	}

	rc = read(fd->fd, &len, sizeof(len));
	if (rc < sizeof(len)) {
		LOGP(DL1C, LOGL_ERROR, "Short read. Error.\n");
		exit(2);
	}

	len = ntohs(len);
	if (len > GSM_L2_LENGTH) {
		LOGP(DL1C, LOGL_ERROR, "Length is too big: %u\n", len);
		msgb_free(msg);
		return -1;
	}


	msg->l1h = msgb_put(msg, len);
	rc = read(fd->fd, msg->l1h, msgb_l1len(msg));
	if (rc != msgb_l1len(msg)) {
		LOGP(DL1C, LOGL_ERROR, "Can not read data: len=%d rc=%d "
		     "errno=%d\n", len, rc, errno);
		msgb_free(msg);
		return -1;
	}

	l1ctl_recv((struct osmocom_ms *) fd->data, msg);
	msgb_free(msg);
	return 0;
}

static int layer2_write(struct bsc_fd *fd, struct msgb *msg)
{
	int rc;

	rc = write(fd->fd, msg->data, msg->len);
	if (rc != msg->len) {
		LOGP(DL1C, LOGL_ERROR, "Failed to write data: rc: %d\n", rc);
		return -1;
	}

	return 0;
}


int osmo_send_l1(struct osmocom_ms *ms, struct msgb *msg)
{
	uint16_t *len;

	DEBUGP(DL1C, "Sending: '%s'\n", hexdump(msg->data, msg->len));

	if (msg->l1h != msg->data)
		LOGP(DL1C, LOGL_ERROR, "Message L1 header != Message Data\n");
	
	/* prepend 16bit length before sending */
	len = (uint16_t *) msgb_push(msg, sizeof(*len));
	*len = htons(msg->len - sizeof(*len));

	if (write_queue_enqueue(&ms->wq, msg) != 0) {
		LOGP(DL1C, LOGL_ERROR, "Failed to enqueue msg.\n");
		msgb_free(msg);
		return -1;
	}

	return 0;
}

static void print_usage(const char *app)
{
	printf("Usage: %s\n", app);
}

static void print_help()
{
	printf(" Some help...\n");
	printf("  -h --help		this text\n");
	printf("  -s --socket		/tmp/osmocom_l2. Path to the unix domain socket\n");
	printf("  -a --arfcn NR		The ARFCN to be used for layer2.\n");
	printf("  -i --gsmtap-ip	The destination IP used for GSMTAP.\n");
}

static void handle_options(int argc, char **argv)
{
	struct sockaddr_in gsmtap;
	while (1) {
		int option_index = 0, c;
		static struct option long_options[] = {
			{"help", 0, 0, 'h'},
			{"socket", 1, 0, 's'},
			{"arfcn", 1, 0, 'a'},
			{"gsmtap-ip", 1, 0, 'i'},
			{"debug", 1, 0, 'd'},
			{0, 0, 0, 0},
		};

		c = getopt_long(argc, argv, "hs:a:i:d:",
				long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			print_usage(argv[0]);
			print_help();
			exit(0);
			break;
		case 's':
			socket_path = talloc_strdup(l23_ctx, optarg);
			break;
		case 'a':
			ms->test_arfcn = atoi(optarg);
			break;
		case 'i':
			if (!inet_aton(optarg, &gsmtap.sin_addr)) {
				perror("inet_aton");
				exit(2);
			}
			gsmtap_ip = ntohl(gsmtap.sin_addr.s_addr);
			break;
		case 'd':
			log_parse_category_mask(stderr_target, optarg);
			break;
		default:
			break;
		}
	}
}

void sighandler(int sigset)
{
	int rc = 0;

	if (sigset == SIGHUP || sigset == SIGPIPE)
		return;

	fprintf(stderr, "Signal %d recevied.\n", sigset);
	if (l23_app_exit)
		rc = l23_app_exit(ms);

	if (rc != -EBUSY)
		exit (0);
}

int main(int argc, char **argv)
{
	int rc;
	struct sockaddr_un local;

	log_init(&log_info);
	stderr_target = log_target_create_stderr();
	log_add_target(stderr_target);
	log_set_all_filter(stderr_target, 1);

	l23_ctx = talloc_named_const(NULL, 1, "layer2 context");

	ms = talloc_zero(l23_ctx, struct osmocom_ms);
	if (!ms) {
		fprintf(stderr, "Failed to allocate MS\n");
		exit(1);
	}

	sprintf(ms->name, "1");

	ms->test_arfcn = 871;

	handle_options(argc, argv);

	ms->wq.bfd.fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (ms->wq.bfd.fd < 0) {
		fprintf(stderr, "Failed to create unix domain socket.\n");
		exit(1);
	}

	local.sun_family = AF_UNIX;
	strncpy(local.sun_path, socket_path, sizeof(local.sun_path));
	local.sun_path[sizeof(local.sun_path) - 1] = '\0';

	rc = connect(ms->wq.bfd.fd, (struct sockaddr *) &local,
		     sizeof(local.sun_family) + strlen(local.sun_path));
	if (rc < 0) {
		fprintf(stderr, "Failed to connect to '%s'.\n", local.sun_path);
		exit(1);
	}

	write_queue_init(&ms->wq, 100);
	ms->wq.bfd.data = ms;
	ms->wq.bfd.when = BSC_FD_READ;
	ms->wq.read_cb = layer2_read;
	ms->wq.write_cb = layer2_write;

	lapdm_init(&ms->l2_entity.lapdm_dcch, ms);
	lapdm_init(&ms->l2_entity.lapdm_acch, ms);

	l23_app_init(ms);

	if (bsc_register_fd(&ms->wq.bfd) != 0) {
		fprintf(stderr, "Failed to register fd.\n");
		exit(1);
	}

	if (gsmtap_ip) {
		rc = gsmtap_init(gsmtap_ip);
		if (rc < 0) {
			fprintf(stderr, "Failed during gsmtap_init()\n");
			exit(1);
		}
	}

	signal(SIGINT, sighandler);
	signal(SIGHUP, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGPIPE, sighandler);

	while (1) {
		if (l23_app_work)
			l23_app_work(ms);
		bsc_select_main(0);
	}

	return 0;
}
