/* control utility for the Calypso bootloader */

/* (C) 2010 by Ingo Albrecht <prom@berlin.ccc.de>
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

enum {
	OPERATION_QUERY,
	OPERATION_MEMDUMP,
	OPERATION_MEMLOAD,
};

static struct {
	unsigned char print_requests;
	unsigned char print_replies;

	unsigned char quit;
	int operation;
	uint8_t command;

	FILE *binfile;

	uint32_t req_length;
	uint32_t req_address;

	uint32_t cur_length;
	uint32_t cur_address;
} osmoload;

static int usage(const char *name)
{
	printf("\nUsage: %s [ -v | -h ] [ -d tr ] [ -m {c123,c155} ] [ -l /tmp/osmocom_loader ] COMMAND\n", name);
	puts("  memread <hex-length> <hex-address>");
	puts("  memdump <hex-length> <hex-address> <file>");
	puts("  flashloader");
	puts("  romloader");
	puts("  ping");
	puts("  reset");
	puts("  off");

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

	for (n=0; n < len; n++, bufptr++)
		printf("%02x ", *bufptr);
	printf("\n");
}

static void
loader_send_request(struct msgb *msg) {
	int rc;
	u_int16_t len = htons(msg->len);

	if(osmoload.print_requests) {
		printf("Sending %d bytes:\n", msg->len);
		hexdump(msg->data, msg->len);
	}

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

static void loader_do_memdump();

static void
loader_handle_reply(struct msgb *msg) {
	int rc;

	if(osmoload.print_replies) {
		printf("Received %d bytes:\n", msg->len);
		hexdump(msg->data, msg->len);
	}

	uint8_t cmd = msgb_get_u8(msg);

	uint8_t length;
	uint32_t address;

	void *data;

	switch(cmd) {
	case LOADER_PING:
	case LOADER_RESET:
	case LOADER_POWEROFF:
	case LOADER_ENTER_ROM_LOADER:
	case LOADER_ENTER_FLASH_LOADER:
		break;
	case LOADER_MEM_READ:
		length = msgb_get_u8(msg);
		address = msgb_get_u32(msg);
		data = msgb_get(msg, length);
		break;
	case LOADER_MEM_WRITE:
		length = msgb_get_u8(msg);
		address = msgb_get_u32(msg);
		break;
	default:
		printf("Received unknown reply %d:\n", cmd);
		hexdump(msg->data, msg->len);
		osmoload.quit = 1;
		break;
	}

	switch(osmoload.operation) {
	case OPERATION_QUERY:
		switch(cmd) {
		case LOADER_PING:
			printf("Received pong.\n");
			break;
		case LOADER_RESET:
			printf("Reset confirmed.\n");
			break;
		case LOADER_POWEROFF:
			printf("Poweroff confirmed.\n");
			break;
		case LOADER_ENTER_ROM_LOADER:
			printf("Jump to ROM loader confirmed.\n");
			break;
		case LOADER_ENTER_FLASH_LOADER:
			printf("Jump to flash loader confirmed.\n");
			break;
		case LOADER_MEM_READ:
			printf("Received memory dump of %d bytes at 0x%x:\n", length, address);
			hexdump(data, length);
			break;
		case LOADER_MEM_WRITE:
			printf("Confirmed memory write of %d bytes at 0x%x.\n", length, address);
			break;
		default:
			break;
		}
		if(osmoload.command == cmd) {
			osmoload.quit = 1;
		}
		break;
	case OPERATION_MEMDUMP:
		if(cmd == LOADER_MEM_READ) {
			rc = fwrite(data, 1, length, osmoload.binfile);
			if(ferror(osmoload.binfile)) {
				printf("Error writing to dump file: %s\n", strerror(errno));
			}
			loader_do_memdump();
		}
		break;
	default:
		break;
	}
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
loader_send_query(uint8_t command) {
	struct msgb *msg = msgb_alloc(MSGB_MAX, "loader");
	msgb_put_u8(msg, command);
	loader_send_request(msg);
	msgb_free(msg);

	osmoload.operation = OPERATION_QUERY;
	osmoload.command = command;
}

static void
loader_send_memread(uint8_t length, uint32_t address) {
	struct msgb *msg = msgb_alloc(MSGB_MAX, "loader");
	msgb_put_u8(msg, LOADER_MEM_READ);
	msgb_put_u8(msg, length);
	msgb_put_u32(msg, address);
	loader_send_request(msg);
	msgb_free(msg);

	osmoload.operation = OPERATION_QUERY;
	osmoload.command = LOADER_MEM_READ;
}

static void
loader_send_memwrite(uint8_t length, uint32_t address, void *data) {
	struct msgb *msg = msgb_alloc(MSGB_MAX, "loader");
	msgb_put_u8(msg, LOADER_MEM_WRITE);
	msgb_put_u8(msg, length);
	msgb_put_u32(msg, address);
	memcpy(msgb_put(msg, length), data, length);
	loader_send_request(msg);
	msgb_free(msg);

	osmoload.operation = OPERATION_QUERY;
	osmoload.command = LOADER_MEM_WRITE;
}

static void
loader_do_memdump() {
	uint32_t rembytes = osmoload.req_length - osmoload.cur_length;

	if(!rembytes) {
		osmoload.quit = 1;
		return;
	}

#define MEM_READ_MAX (MSGB_MAX - 16)

	uint8_t reqbytes = (rembytes < MEM_READ_MAX) ? rembytes : MEM_READ_MAX;

	struct msgb *msg = msgb_alloc(MSGB_MAX, "loader");

	msgb_put_u8(msg, LOADER_MEM_READ);
	msgb_put_u8(msg, reqbytes);
	msgb_put_u32(msg, osmoload.cur_address);
	loader_send_request(msg);
	msgb_free(msg);

	osmoload.cur_address += reqbytes;
	osmoload.cur_length  += reqbytes;
}

static void
loader_start_memdump(uint32_t length, uint32_t address, char *file) {
	osmoload.operation = OPERATION_MEMDUMP;
	osmoload.binfile = fopen(file, "wb");
	if(!osmoload.binfile) {
		printf("Could not open %s: %s\n", file, strerror(errno));
		exit(1);
	}
	osmoload.req_length = length;
	osmoload.req_address = address;

	osmoload.cur_length = 0;
	osmoload.cur_address = address;

	loader_do_memdump();
}

static void
loader_command(char *name, int cmdc, char **cmdv) {
	if(!cmdc) {
		usage(name);
	}

	char *cmd = cmdv[0];

	char buf[256];
	memset(buf, 23, sizeof(buf));

	if(!strcmp(cmd, "ping")) {
		loader_send_query(LOADER_PING);
	} else if(!strcmp(cmd, "memread")) {
		uint8_t length;
		uint32_t address;

		if(cmdc < 3) {
			usage(name);
		}

		length = strtoul(cmdv[1], NULL, 16);
		address = strtoul(cmdv[2], NULL, 16);

		loader_send_memread(length, address);
	} else if(!strcmp(cmd, "memdump")) {
		uint32_t length;
		uint32_t address;

		if(cmdc < 4) {
			usage(name);
		}

		length = strtoul(cmdv[1], NULL, 16);
		address = strtoul(cmdv[2], NULL, 16);

		loader_start_memdump(length, address, cmdv[3]);
	} else if(!strcmp(cmd, "memwrite")) {
		loader_send_memwrite(128, 0x810000, buf);
	} else if(!strcmp(cmd, "off")) {
		loader_send_query(LOADER_POWEROFF);
	} else if(!strcmp(cmd, "reset")) {
		loader_send_query(LOADER_RESET);
	} else if(!strcmp(cmd, "romload")) {
		loader_send_query(LOADER_ENTER_ROM_LOADER);
	} else if(!strcmp(cmd, "flashload")) {
		loader_send_query(LOADER_ENTER_FLASH_LOADER);
	} else if(!strcmp(cmd, "help")) {
		usage(name);
	} else {
		printf("Unknown command '%s'\n", cmd);
		usage(name);
	}
}

void
setdebug(const char *name, char c) {
	switch(c) {
	case 't':
		osmoload.print_requests = 1;
		break;
	case 'r':
		osmoload.print_replies = 1;
		break;
	default:
		usage(name);
		break;
	}
}

int
main(int argc, char **argv) {
	int opt;
	char *loader_un_path = "/tmp/osmocom_loader";
	const char *debugopt;

	while((opt = getopt(argc, argv, "d:hl:m:v")) != -1) {
		switch(opt) {
		case 'd':
			debugopt = optarg;
			while(*debugopt) {
				setdebug(argv[0], *debugopt);
				debugopt++;
			}
			break;
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

	osmoload.quit = 0;

	loader_connect(loader_un_path);

	loader_command(argv[0], argc - optind, argv + optind);

	while(!osmoload.quit) {
		bsc_select_main(0);
	}

	if(osmoload.binfile) {
		fclose(osmoload.binfile);
	}

	return 0;
}
