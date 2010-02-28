
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include <arpa/inet.h>

#include <sys/socket.h>
#include <sys/un.h>

#include <osmocore/msgb.h>
#include <osmocore/select.h>

#include <loader/protocol.h>

#define MSGB_MAX 256

#define DEFAULT_SOCKET "/tmp/osmocom_loader"

static struct bsc_fd connection;

static int usage(const char *name)
{
	printf("\nUsage: %s [ -v | -h ] [ -m {c123,c155} ] [ -l /tmp/osmocom_loader ] COMMAND\n", name);
	exit(2);
}

static int version(const char *name)
{
	//printf("\n%s version %s\n", name, VERSION);
	exit(2);
}

static void hexdump(const uint8_t *data, unsigned int len)
{
	const uint8_t *bufptr = data;
	int n;

	for (n=0; bufptr, n < len; n++, bufptr++)
		printf("%02x ", *bufptr);
	printf("\n");
}

static void
loader_send_request(struct msgb *msg) {
	int rc;
	u_int16_t len = htons(msg->len);

	printf("Sending %d bytes ", msg->len);
	hexdump(msg->data, msg->len);

	rc = write(connection.fd, &len, sizeof(len));
	if(rc != sizeof(len)) {
		fprintf(stderr, "Error writing.\n");
		exit(2);
	}

	rc = write(connection.fd, msg->data, msg->len);
	if(rc != msg->len) {
		fprintf(stderr, "Error writing.\n");
		exit(2);
	}
}

static void
loader_handle_reply(struct msgb *msg) {
	printf("Received ");
	hexdump(msg->data, msg->len);
}

static int
loader_read_cb(struct bsc_fd *fd, unsigned int flags) {
	struct msgb *msg;
	u_int16_t len;
	int rc;

	msg = msgb_alloc(MSGB_MAX, "loader");
	if (!msg) {
		fprintf(stderr, "Failed to allocate msg.\n");
		return -1;
	}

	rc = read(fd->fd, &len, sizeof(len));
	if (rc < sizeof(len)) {
		fprintf(stderr, "Short read. Error.\n");
		exit(2);
	}

	if (ntohs(len) > MSGB_MAX) {
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

	loader_handle_reply(msg);

	msgb_free(msg);

	return 0;
}

static void
loader_connect(const char *socket_path) {
	int rc;
	struct sockaddr_un local;
	struct bsc_fd *conn = &connection;

	local.sun_family = AF_UNIX;
	strncpy(local.sun_path, socket_path, sizeof(local.sun_path));
	local.sun_path[sizeof(local.sun_path) - 1] = '\0';

	conn->fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (conn->fd < 0) {
		fprintf(stderr, "Failed to create unix domain socket.\n");
		exit(1);
	}

	rc = connect(conn->fd, (struct sockaddr *) &local,
				 sizeof(local.sun_family) + strlen(local.sun_path));
	if (rc < 0) {
		fprintf(stderr, "Failed to connect to '%s'.\n", local.sun_path);
		exit(1);
	}

	conn->when = BSC_FD_READ;
	conn->cb = loader_read_cb;
	conn->data = NULL;

	if (bsc_register_fd(conn) != 0) {
		fprintf(stderr, "Failed to register fd.\n");
		exit(1);
	}
}

static void
loader_command(char *name, int cmdc, char **cmdv) {
	if(!cmdc) {
		usage(name);
	}

	char *cmd = cmdv[0];

	printf("Command %s\n", cmd);

	if(!strcmp(cmd, "ping")) {
		struct msgb *msg = msgb_alloc(MSGB_MAX, "loader");
		msgb_put_u8(msg, LOADER_PING);
		msgb_put_u8(msg, 0);
		loader_send_request(msg);
		msgb_free(msg);
	} else {
		printf("Unknown command '%s'\n", cmd);
		usage(name);
	}
}

int
main(int argc, char **argv) {
	int opt;
	char *loader_un_path = "/tmp/osmocom_loader";

	while((opt = getopt(argc, argv, "hl:m:v")) != -1) {
		switch(opt) {
		case 'l':
			loader_un_path = optarg;
			break;
		case 'm':
			puts("model selection not implemented");
			exit(2);
			break;
		case 'v':
			version(argv[0]);
			break;
		case 'h':
		default:
			usage(argv[0]);
			break;
		}
	}

	loader_connect(loader_un_path);

	loader_command(argv[0], argc - optind, argv + optind);

	return 0;
}
