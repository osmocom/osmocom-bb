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
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include <arpa/inet.h>

#include <sys/stat.h>

#include <sys/socket.h>
#include <sys/un.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/select.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/crc16.h>

#include <loader/protocol.h>

#define MSGB_MAX 256

#define MEM_MSG_MAX (MSGB_MAX - 16)

#define DEFAULT_SOCKET "/tmp/osmocom_loader"

static struct osmo_fd connection;

enum {
	STATE_INIT,
	STATE_QUERY_PENDING,
	STATE_DUMP_IN_PROGRESS,
	STATE_LOAD_IN_PROGRESS,
	STATE_FLASHRANGE_GET_INFO,
	STATE_FLASHRANGE_IN_PROGRESS,
	STATE_PROGRAM_GET_INFO,
	STATE_PROGRAM_IN_PROGRESS,
	STATE_DUMPING,
};

struct flashblock {
	uint8_t fb_chip;
	uint32_t fb_offset;
	uint32_t fb_addr;
	uint32_t fb_size;
};

static struct {
	/* debug flags */
	unsigned char print_requests;
	unsigned char print_replies;

	/* quit flag for main loop */
	unsigned char quit;

	/* main state machine */
	int state;

	/* pending query command */
	uint8_t command;

	/* general timeout */
	struct osmo_timer_list timeout;

	/* binary i/o for firmware images */
	FILE *binfile;
	/* buffer containing binfile data */
	char *binbuf;

	/* memory operation state */
	uint8_t  memchip; /* target chip (for flashes) */
	uint32_t membase; /* target base address of operation */
	uint32_t memlen;  /* length of entire operation */
	uint32_t memoff;  /* offset for next request */
	uint16_t memcrc;  /* crc for current request */
	uint16_t memreq;  /* length of current request */

	/* array of all flash blocks */
	uint8_t flashcommand;
	uint32_t numblocks;
	struct flashblock blocks[512];
} osmoload;

static int usage(const char *name)
{
	printf("Usage: %s [ -v | -h ] [ -d tr ] [ -m {c123,c155} ] [ -l /tmp/osmocom_loader ] COMMAND ...\n", name);

	puts("\n  Memory commands:");
	puts("    memget <hex-address> <hex-length>        - Peek at memory");
	puts("    memput <hex-address> <hex-bytes>         - Poke at memory");
	puts("    memdump <hex-address> <hex-length> <file>- Dump memory to file");
	puts("    memload <hex-address> <file>             - Load file into memory");

	puts("\n  Flash commands:");
	puts("    finfo                             - Information about flash chips");
	puts("    funlock <address> <length>        - Unlock flash block");
	puts("    flock <address> <length>          - Lock flash block");
	puts("    flockdown <address> <length>      - Lock down flash block");
	puts("    fgetlock <address> <length>       - Get locking state of block");
	puts("    ferase <address> <length>         - Erase flash range");
	puts("    fprogram <chip> <address> <file>  - Program file into flash");

	puts("\n  Execution commands:");
	puts("    jump <hex-address>                 - Jump to address");
	puts("    jumpflash                          - Jump to flash loader");
	puts("    jumprom                            - Jump to rom loader");

	puts("\n  Device lifecycle:");
	puts("    ping                               - Ping the loader");
	puts("    reset                              - Reset device");
	puts("    off                                - Power off device");

	puts("\n  Debug:");
	puts("    dump                               - Dump loader traffic to console");

	exit(2);
}

static int version(const char *name)
{
	//printf("\n%s version %s\n", name, VERSION);
	exit(2);
}

static void osmoload_osmo_hexdump(const uint8_t *data, unsigned int len)
{
	const uint8_t *bufptr = data;
	const uint8_t const *endptr = bufptr + len;
	int n, m, i, hexchr;

	for (n=0; n < len; n+=32, bufptr += 32) {
		hexchr = 0;
		for(m = 0; m < 32 && bufptr < endptr; m++, bufptr++) {
			if((m) && !(m%4)) {
				putchar(' ');
				hexchr++;
			}
			printf("%02x", *bufptr);
			hexchr+=2;
		}
		bufptr -= m;
		int n = 71 - hexchr;
		for(i = 0; i < n; i++) {
			putchar(' ');
		}

		putchar(' ');

		for(m = 0; m < 32 && bufptr < endptr; m++, bufptr++) {
			if(isgraph(*bufptr)) {
				putchar(*bufptr);
			} else {
				putchar('.');
			}
		}
		bufptr -= m;

		putchar('\n');
	}
}

static void
loader_send_request(struct msgb *msg) {
	int rc;
	uint16_t len = htons(msg->len);

	if(osmoload.print_requests) {
		printf("Sending %d bytes:\n", msg->len);
		osmoload_osmo_hexdump(msg->data, msg->len);
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

static void loader_do_memdump(uint16_t crc, void *address, size_t length);
static void loader_do_memload();
static void loader_do_fprogram();
static void loader_do_flashrange(uint8_t cmd, struct msgb *msg, uint8_t chip, uint32_t address, uint32_t status);

static void memop_timeout(void *dummy) {
	switch(osmoload.state) {
	case STATE_LOAD_IN_PROGRESS:
		printf("Timeout. Repeating.");
		osmoload.memoff -= osmoload.memreq;
		loader_do_memload();
		break;
	default:
		break;
	}
	return;
}

static void
loader_parse_flash_info(struct msgb *msg) {
	uint8_t nchips;

	nchips = msgb_pull_u8(msg);

	osmoload.numblocks = 0;

	int chip;
	for(chip = 0; chip < nchips; chip++) {

		uint32_t address;
		address = msgb_pull_u32(msg);

		uint32_t chipsize;
		chipsize = msgb_pull_u32(msg);

		uint8_t nregions;
		nregions = msgb_pull_u8(msg);

		printf("    chip %d at 0x%8.8x of %d bytes in %d regions\n", chip, address, chipsize, nregions);

		uint32_t curoffset = 0;
		int region;
		for(region = 0; region < nregions; region++) {
			uint16_t blockcount = msgb_pull_u32(msg);
			uint32_t blocksize = msgb_pull_u32(msg);

			printf("      region %d with %d blocks of %d bytes each\n", region, blockcount, blocksize);

			int block;
			for(block = 0; block < blockcount; block++) {
				osmoload.blocks[osmoload.numblocks].fb_chip   = chip;
				osmoload.blocks[osmoload.numblocks].fb_offset = curoffset;
				osmoload.blocks[osmoload.numblocks].fb_addr   = address + curoffset;
				osmoload.blocks[osmoload.numblocks].fb_size   = blocksize;

				printf("        block %d with %d bytes at 0x%8.8x on chip %d\n",
					   osmoload.numblocks, blocksize, address + curoffset, chip);

				curoffset += blocksize;

				osmoload.numblocks++;
			}
		}
	}
}


static void
loader_handle_reply(struct msgb *msg) {
	if(osmoload.print_replies) {
		printf("Received %d bytes:\n", msg->len);
		osmoload_osmo_hexdump(msg->data, msg->len);
	}

	uint8_t cmd = msgb_pull_u8(msg);

	uint8_t chip;
	uint8_t length;
	uint16_t crc;
	uint32_t address;
	uint32_t entrypoint;
	uint32_t status;

	void *data;

	switch(cmd) {
	case LOADER_INIT:
		address = msgb_pull_u32(msg);
		entrypoint = msgb_pull_u32(msg);
		printf("Loader at entry %x has been started, requesting load to %x\n", entrypoint, address);
		break;
	case LOADER_PING:
	case LOADER_RESET:
	case LOADER_POWEROFF:
	case LOADER_ENTER_ROM_LOADER:
	case LOADER_ENTER_FLASH_LOADER:
		break;
	case LOADER_MEM_READ:
		length = msgb_pull_u8(msg);
		crc = msgb_pull_u16(msg);
		address = msgb_pull_u32(msg);
		data = msgb_pull(msg, length) - length;
		break;
	case LOADER_MEM_WRITE:
		length = msgb_pull_u8(msg);
		crc = msgb_pull_u16(msg);
		address = msgb_pull_u32(msg);
		break;
	case LOADER_JUMP:
		address = msgb_pull_u32(msg);
		break;
	case LOADER_FLASH_INFO:
		break;
	case LOADER_FLASH_GETLOCK:
	case LOADER_FLASH_ERASE:
	case LOADER_FLASH_UNLOCK:
	case LOADER_FLASH_LOCK:
	case LOADER_FLASH_LOCKDOWN:
		chip = msgb_pull_u8(msg);
		address = msgb_pull_u32(msg);
		status = msgb_pull_u32(msg);
		break;
	case LOADER_FLASH_PROGRAM:
		length = msgb_pull_u8(msg);
		crc = msgb_pull_u16(msg);
		msgb_pull_u8(msg); // XXX align
		chip = msgb_pull_u8(msg);
		address = msgb_pull_u32(msg);
		status = msgb_pull_u32(msg);
		break;
	default:
		printf("Received unknown reply %d:\n", cmd);
		osmoload_osmo_hexdump(msg->data, msg->len);
		osmoload.quit = 1;
		return;
	}

	switch(osmoload.state) {
	case STATE_QUERY_PENDING:
	case STATE_DUMPING:
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
			osmoload_osmo_hexdump(data, length);
			break;
		case LOADER_MEM_WRITE:
			printf("Confirmed memory write of %d bytes at 0x%x.\n", length, address);
			break;
		case LOADER_JUMP:
			printf("Confirmed jump to 0x%x.\n", address);
			break;
		case LOADER_FLASH_ERASE:
			printf("Confirmed flash erase of chip %d address 0x%8.8x, status %s\n",
				   chip, address, status ? "FAILED" : "ok");
			break;
		case LOADER_FLASH_GETLOCK:
			printf("Lock state of chip %d address 0x%8.8x is %s\n",
				   chip, address, (status == LOADER_FLASH_LOCKED ? "locked"
								   : (status == LOADER_FLASH_LOCKED_DOWN ? "locked down"
									  : (status == LOADER_FLASH_UNLOCKED ? "unlocked"
										 : "UNKNOWN"))));
			break;
		case LOADER_FLASH_UNLOCK:
			printf("Confirmed flash unlock of chip %d address 0x%8.8x, status %s\n",
				   chip, address, status ? "FAILED" : "ok");
			break;
		case LOADER_FLASH_LOCK:
			printf("Confirmed flash lock of chip %d address 0x%8.8x, status %s\n",
				   chip, address, status ? "FAILED" : "ok");
			break;
		case LOADER_FLASH_LOCKDOWN:
			printf("Confirmed flash lockdown of chip %d address 0x%8.8x, status %s\n",
				   chip, address, status ? "FAILED" : "ok");
			break;
		case LOADER_FLASH_INFO:
			loader_parse_flash_info(msg);
			break;
		default:
			break;
		}
		if(osmoload.state == STATE_QUERY_PENDING) {
			if(osmoload.command == cmd) {
				osmoload.quit = 1;
			}
		}
		break;
	case STATE_DUMP_IN_PROGRESS:
		if(cmd == LOADER_MEM_READ) {
			loader_do_memdump(crc, data, length);
		}
		break;
	case STATE_LOAD_IN_PROGRESS:
		if(cmd == LOADER_MEM_WRITE) {
			if(osmoload.memcrc != crc) {
				osmoload.memoff -= osmoload.memreq;
				printf("\nbad crc %4.4x (not %4.4x) at offset 0x%8.8x", crc, osmoload.memcrc, osmoload.memoff);
			} else {
				putchar('.');
			}
			loader_do_memload();
		}
		break;
	case STATE_PROGRAM_GET_INFO:
	case STATE_PROGRAM_IN_PROGRESS:
		if(cmd == LOADER_FLASH_PROGRAM) {
			if(osmoload.memcrc != crc) {
				osmoload.memoff -= osmoload.memreq;
				printf("\nbad crc %4.4x (not %4.4x) at offset 0x%8.8x", crc, osmoload.memcrc, osmoload.memoff);
			} else {
				putchar('.');
			}
			if(((int)status) != 0) {
				printf("\nstatus %d, aborting\n", status);
				exit(1);
			}
			loader_do_fprogram();
		}
		break;
	case STATE_FLASHRANGE_GET_INFO:
	case STATE_FLASHRANGE_IN_PROGRESS:
		loader_do_flashrange(cmd, msg, chip, address, status);
		break;
	default:
		break;
	}

	fflush(stdout);
}

static int
loader_read_cb(struct osmo_fd *fd, unsigned int flags) {
	struct msgb *msg;
	uint16_t len;
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
	struct osmo_fd *conn = &connection;

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

	if (osmo_fd_register(conn) != 0) {
		fprintf(stderr, "Failed to register fd.\n");
		exit(1);
	}
}

static void
loader_send_simple(uint8_t command) {
	struct msgb *msg = msgb_alloc(MSGB_MAX, "loader");
	msgb_put_u8(msg, command);
	loader_send_request(msg);
	msgb_free(msg);

	osmoload.command = command;
}

static void
loader_start_query(uint8_t command) {
	loader_send_simple(command);
	osmoload.state = STATE_QUERY_PENDING;
}

static void
loader_send_flash_query(uint8_t command, uint8_t chip, uint32_t address) {
	struct msgb *msg = msgb_alloc(MSGB_MAX, "loader");
	msgb_put_u8(msg, command);
	msgb_put_u8(msg, chip);
	msgb_put_u32(msg, address);
	loader_send_request(msg);
	msgb_free(msg);

	osmoload.command = command;
}

static void
loader_start_flash_query(uint8_t command, uint8_t chip, uint32_t address) {
	loader_send_flash_query(command, chip, address);
	osmoload.state = STATE_QUERY_PENDING;
}

static void
loader_start_memget(uint8_t length, uint32_t address) {
	struct msgb *msg = msgb_alloc(MSGB_MAX, "loader");
	msgb_put_u8(msg, LOADER_MEM_READ);
	msgb_put_u8(msg, length);
	msgb_put_u32(msg, address);
	loader_send_request(msg);
	msgb_free(msg);

	osmoload.state = STATE_QUERY_PENDING;
	osmoload.command = LOADER_MEM_READ;
}

static void
loader_start_memput(uint8_t length, uint32_t address, void *data) {
	struct msgb *msg = msgb_alloc(MSGB_MAX, "loader");
	msgb_put_u8(msg, LOADER_MEM_WRITE);
	msgb_put_u8(msg, length);
	msgb_put_u16(msg, osmo_crc16(0, data, length));
	msgb_put_u32(msg, address);
	memcpy(msgb_put(msg, length), data, length);
	loader_send_request(msg);
	msgb_free(msg);

	osmoload.state = STATE_QUERY_PENDING;
	osmoload.command = LOADER_MEM_WRITE;
}

static void
loader_start_jump(uint32_t address) {
	struct msgb *msg = msgb_alloc(MSGB_MAX, "loader");
	msgb_put_u8(msg, LOADER_JUMP);
	msgb_put_u32(msg, address);
	loader_send_request(msg);
	msgb_free(msg);

	osmoload.state = STATE_QUERY_PENDING;
	osmoload.command = LOADER_JUMP;
}


static void
loader_do_memdump(uint16_t crc, void *data, size_t length) {
	int rc;

	if(data && length) {
		osmoload.memcrc = osmo_crc16(0, data, length);
		if(osmoload.memcrc != crc) {
			osmoload.memoff -= osmoload.memreq;
			printf("\nbad crc %4.4x (not %4.4x) at offset 0x%8.8x", crc, osmoload.memcrc, osmoload.memoff);
		} else {
			putchar('.');
		}

		memcpy(osmoload.binbuf + osmoload.memoff, data, length);
		osmoload.memoff += length;
	}

	uint32_t rembytes = osmoload.memlen - osmoload.memoff;

	if(!rembytes) {
		puts("done.");
		osmoload.quit = 1;

		unsigned c = osmoload.memlen;
		char *p = osmoload.binbuf;
		while(c) {
			rc = fwrite(p, 1, c, osmoload.binfile);
			if(ferror(osmoload.binfile)) {
				printf("Could not read from file: %s\n", strerror(errno));
				exit(1);
			}
			c -= rc;
			p += rc;
		}
		fclose(osmoload.binfile);
		osmoload.binfile = NULL;

		free(osmoload.binbuf);

		return;
	}

	uint8_t reqbytes = (rembytes < MEM_MSG_MAX) ? rembytes : MEM_MSG_MAX;

	struct msgb *msg = msgb_alloc(MSGB_MAX, "loader");

	msgb_put_u8(msg, LOADER_MEM_READ);
	msgb_put_u8(msg, reqbytes);
	msgb_put_u32(msg, osmoload.membase + osmoload.memoff);
	loader_send_request(msg);
	msgb_free(msg);
}

static void
loader_do_memload() {
	uint32_t rembytes = osmoload.memlen - osmoload.memoff;

	if(!rembytes) {
		puts("done.");
		osmoload.quit = 1;
		return;
	}

	osmo_timer_schedule(&osmoload.timeout, 0, 500000);

	uint8_t reqbytes = (rembytes < MEM_MSG_MAX) ? rembytes : MEM_MSG_MAX;

	osmoload.memcrc = osmo_crc16(0, (uint8_t *) osmoload.binbuf + osmoload.memoff, reqbytes);
	osmoload.memreq = reqbytes;

	struct msgb *msg = msgb_alloc(MSGB_MAX, "loader");

	msgb_put_u8(msg, LOADER_MEM_WRITE);
	msgb_put_u8(msg, reqbytes);
	msgb_put_u16(msg, osmoload.memcrc);
	msgb_put_u32(msg, osmoload.membase + osmoload.memoff);

	unsigned char *p = msgb_put(msg, reqbytes);
	memcpy(p, osmoload.binbuf + osmoload.memoff, reqbytes);

#if 0
	printf("Sending %u bytes at offset %u to address %x with crc %x\n",
		   reqbytes, osmoload.memoff, osmoload.membase + osmoload.memoff,
		   osmoload.memcrc);
#endif

	loader_send_request(msg);

	msgb_free(msg);

	osmoload.memoff += reqbytes;
}

static void
loader_do_fprogram() {
	uint32_t rembytes = osmoload.memlen - osmoload.memoff;

	if(!rembytes) {
		puts("done.");
		osmoload.quit = 1;
		return;
	}

	osmo_timer_schedule(&osmoload.timeout, 0, 10000000);

	uint8_t reqbytes = (rembytes < MEM_MSG_MAX) ? rembytes : MEM_MSG_MAX;

	osmoload.memcrc = osmo_crc16(0, (uint8_t *) osmoload.binbuf + osmoload.memoff, reqbytes);
	osmoload.memreq = reqbytes;

	struct msgb *msg = msgb_alloc(MSGB_MAX, "loader");

	msgb_put_u8(msg, LOADER_FLASH_PROGRAM);
	msgb_put_u8(msg, reqbytes);
	msgb_put_u16(msg, osmoload.memcrc);
	msgb_put_u8(msg, 0); // XXX: align data to 16bit
	msgb_put_u8(msg, osmoload.memchip);
	msgb_put_u32(msg, osmoload.membase + osmoload.memoff);

	unsigned char *p = msgb_put(msg, reqbytes);
	memcpy(p, osmoload.binbuf + osmoload.memoff, reqbytes);

#if 0
	printf("Sending %u bytes at offset %u to address %x with crc %x\n",
		   reqbytes, osmoload.memoff, osmoload.membase + osmoload.memoff,
		   osmoload.memcrc);
#endif

	loader_send_request(msg);

	msgb_free(msg);

	osmoload.memoff += reqbytes;
}

static void
loader_start_memdump(uint32_t length, uint32_t address, char *file) {
	printf("Dumping %u bytes of memory at 0x%x to file %s\n", length, address, file);

	osmoload.binbuf = malloc(length);
	if(!osmoload.binbuf) {
		printf("Could not allocate %u bytes for %s.\n", length, file);
		exit(1);
	}

	osmoload.binfile = fopen(file, "wb");
	if(!osmoload.binfile) {
		printf("Could not open %s: %s\n", file, strerror(errno));
		exit(1);
	}

	osmoload.membase = address;
	osmoload.memlen = length;
	osmoload.memoff = 0;

	osmoload.state = STATE_DUMP_IN_PROGRESS;
	loader_do_memdump(0, NULL, 0);
}

static void
loader_start_memload(uint32_t address, char *file) {
	int rc;
	struct stat st;

	rc = stat(file, &st);
	if(rc < 0) {
		printf("Could not stat %s: %s\n", file, strerror(errno));
		exit(1);
	}

	uint32_t length = st.st_size;

	printf("Loading %u bytes of memory to address 0x%x from file %s\n", length, address, file);

	osmoload.binbuf = malloc(length);
	if(!osmoload.binbuf) {
		printf("Could not allocate %u bytes for %s.\n", length, file);
		exit(1);
	}

	osmoload.binfile = fopen(file, "rb");
	if(!osmoload.binfile) {
		printf("Could not open %s: %s\n", file, strerror(errno));
		exit(1);
	}

	unsigned c = length;
	char *p = osmoload.binbuf;
	while(c) {
		rc = fread(p, 1, c, osmoload.binfile);
		if(ferror(osmoload.binfile)) {
			printf("Could not read from file: %s\n", strerror(errno));
			exit(1);
		}
		c -= rc;
		p += rc;
	}
	fclose(osmoload.binfile);
	osmoload.binfile = NULL;

	osmoload.membase = address;
	osmoload.memlen = length;
	osmoload.memoff = 0;

	osmoload.state = STATE_LOAD_IN_PROGRESS;
	loader_do_memload();
}

static void
loader_start_flashrange(uint8_t command, uint32_t address, uint32_t length) {
	switch(command) {
	case LOADER_FLASH_ERASE:
		printf("Erasing %u bytes of flash at 0x%x\n", length, address);
		break;
	case LOADER_FLASH_LOCK:
		printf("Locking %u bytes of flash at 0x%x\n", length, address);
		break;
	case LOADER_FLASH_LOCKDOWN:
		printf("Locking down %u bytes of flash at 0x%x\n", length, address);
		break;
	case LOADER_FLASH_UNLOCK:
		printf("Unlocking %u bytes of flash at 0x%x\n", length, address);
		break;
	case LOADER_FLASH_GETLOCK:
		printf("Getlocking %u bytes of flash at 0x%x\n", length, address);
		break;
	default:
		puts("Unknown range command");
		abort();
		break;
	}

	osmoload.flashcommand = command;

	osmoload.membase = address;
	osmoload.memlen = length;
	osmoload.memoff = 0;

	printf("  requesting flash info to determine block layout\n");

	osmoload.state = STATE_FLASHRANGE_GET_INFO;

	loader_send_simple(LOADER_FLASH_INFO);
}

static void
loader_do_flashrange(uint8_t cmd, struct msgb *msg, uint8_t chip, uint32_t address, uint32_t status) {
	switch(osmoload.state) {
	case STATE_FLASHRANGE_GET_INFO:
		if(cmd == LOADER_FLASH_INFO) {
			loader_parse_flash_info(msg);
			osmoload.state = STATE_FLASHRANGE_IN_PROGRESS;
			loader_do_flashrange(0, NULL, 0, 0, 0);
		}
		break;
	case STATE_FLASHRANGE_IN_PROGRESS:
		{
			if(msg) {
				if(cmd == osmoload.flashcommand) {
					if(cmd == LOADER_FLASH_GETLOCK) {
						printf("  lock state of chip %d address 0x%8.8x is %s\n",
							   chip, address, (status == LOADER_FLASH_LOCKED ? "locked"
											   : (status == LOADER_FLASH_LOCKED_DOWN ? "locked down"
												  : (status == LOADER_FLASH_UNLOCKED ? "unlocked"
													 : "UNKNOWN"))));
					} else {
						printf("  confirmed operation on chip %d address 0x%8.8x, status %s\n",
							   chip, address, status ? "FAILED" : "ok");
					}
				} else {
					break;
				}
			}

			uint32_t addr = osmoload.membase + osmoload.memoff;

			if(osmoload.memoff >= osmoload.memlen) {
				puts("  operation done");
				osmoload.quit = 1;
				break;
			}

			uint8_t found = 0;
			int i;
			for(i = 0; i < osmoload.numblocks; i++) {
				struct flashblock *b = &osmoload.blocks[i];
				if(b->fb_addr == addr) {
					loader_send_flash_query(osmoload.flashcommand, b->fb_chip, b->fb_offset);
					osmoload.memoff += b->fb_size;
					found = 1;
					break;
				}
			}
			if(!found) {
				puts("Oops!? Block not found?"); // XXX message
				abort();
			}
		}
		break;
	}
}

static void
loader_start_fprogram(uint8_t chip, uint32_t address, char *file) {
	int rc;
	struct stat st;

	rc = stat(file, &st);
	if(rc < 0) {
		printf("Could not stat %s: %s\n", file, strerror(errno));
		exit(1);
	}

	uint32_t length = st.st_size;

	printf("Loading %u bytes of memory at 0x%x in chip %d from file %s\n", length, address, chip, file);

	osmoload.binbuf = malloc(length);
	if(!osmoload.binbuf) {
		printf("Could not allocate %u bytes for %s.\n", length, file);
		exit(1);
	}

	osmoload.binfile = fopen(file, "rb");
	if(!osmoload.binfile) {
		printf("Could not open %s: %s\n", file, strerror(errno));
		exit(1);
	}

	unsigned c = length;
	char *p = osmoload.binbuf;
	while(c) {
		rc = fread(p, 1, c, osmoload.binfile);
		if(ferror(osmoload.binfile)) {
			printf("Could not read from file: %s\n", strerror(errno));
			exit(1);
		}
		c -= rc;
		p += rc;
	}
	fclose(osmoload.binfile);
	osmoload.binfile = NULL;

	osmoload.memchip = chip;
	osmoload.membase = address;
	osmoload.memlen = length;
	osmoload.memoff = 0;

	osmoload.state = STATE_PROGRAM_IN_PROGRESS;

	loader_do_fprogram();
}

static void
query_timeout(void *dummy) {
	puts("Query timed out.");
	exit(2);
}

static void
loader_command(char *name, int cmdc, char **cmdv) {
	if(!cmdc) {
		usage(name);
	}

	char *cmd = cmdv[0];

	char buf[MEM_MSG_MAX];
	memset(buf, 23, sizeof(buf));

	if(!strcmp(cmd, "dump")) {
		osmoload.state = STATE_DUMPING;
	} else if(!strcmp(cmd, "ping")) {
		loader_start_query(LOADER_PING);
	} else if(!strcmp(cmd, "off")) {
		loader_start_query(LOADER_POWEROFF);
	} else if(!strcmp(cmd, "reset")) {
		loader_start_query(LOADER_RESET);
	} else if(!strcmp(cmd, "jumprom")) {
		loader_start_query(LOADER_ENTER_ROM_LOADER);
	} else if(!strcmp(cmd, "jumpflash")) {
		loader_start_query(LOADER_ENTER_FLASH_LOADER);
	} else if(!strcmp(cmd, "finfo")) {
		puts("Requesting flash layout info");
		loader_start_query(LOADER_FLASH_INFO);
	} else if(!strcmp(cmd, "memput")) {
		uint32_t address;

		if(cmdc < 3) {
			usage(name);
		}

		address = strtoul(cmdv[1], NULL, 16);

		unsigned int i;
		char *hex = cmdv[2];
		if(strlen(hex)&1) {
			puts("Invalid hex string.");
			exit(2);
		}
		for(i = 0; i <= sizeof(buf) && i < strlen(hex)/2; i++) {
			if(i >= sizeof(buf)) {
				puts("Value too long for single message");
				exit(2);
			}
			unsigned int byte;
			int count = sscanf(hex + i * 2, "%02x", &byte);
			if(count != 1) {
				puts("Invalid hex string.");
				exit(2);
			}
			buf[i] = byte & 0xFF;
		}

		loader_start_memput(i & 0xFF, address, buf);
	} else if(!strcmp(cmd, "memget")) {
		uint32_t address;
		uint8_t length;

		if(cmdc < 3) {
			usage(name);
		}

		address = strtoul(cmdv[1], NULL, 16);
		length = strtoul(cmdv[2], NULL, 16);

		if(length > MEM_MSG_MAX) {
			puts("Too many bytes");
			exit(2);
		}

		loader_start_memget(length, address);
	} else if(!strcmp(cmd, "jump")) {
		uint32_t address;

		if(cmdc < 2) {
			usage(name);
		}

		address = strtoul(cmdv[1], NULL, 16);

		loader_start_jump(address);
	} else if(!strcmp(cmd, "memdump")) {
		uint32_t address;
		uint32_t length;

		if(cmdc < 4) {
			usage(name);
		}

		address = strtoul(cmdv[1], NULL, 16);
		length = strtoul(cmdv[2], NULL, 16);

		loader_start_memdump(length, address, cmdv[3]);
	} else if(!strcmp(cmd, "memload")) {
		uint32_t address;

		if(cmdc < 3) {
			usage(name);
		}

		address = strtoul(cmdv[1], NULL, 16);

		loader_start_memload(address, cmdv[2]);
	} else if(!strcmp(cmd, "fprogram")) {
		uint8_t chip;
		uint32_t address;

		if(cmdc < 4) {
			usage(name);
		}

		chip = strtoul(cmdv[1], NULL, 10);
		address = strtoul(cmdv[2], NULL, 16);

		loader_start_fprogram(chip, address, cmdv[3]);
	} else if(!strcmp(cmd, "ferase")) {
		uint32_t address;
		uint32_t length;

		if(cmdc < 3) {
			usage(name);
		}

		address = strtoul(cmdv[1], NULL, 16);
		length = strtoul(cmdv[2], NULL, 16);

		loader_start_flashrange(LOADER_FLASH_ERASE, address, length);
	} else if(!strcmp(cmd, "flock")) {
		uint32_t address;
		uint32_t length;

		if(cmdc < 3) {
			usage(name);
		}

		address = strtoul(cmdv[1], NULL, 16);
		length = strtoul(cmdv[2], NULL, 16);

		loader_start_flashrange(LOADER_FLASH_LOCK, address, length);
	} else if(!strcmp(cmd, "flockdown")) {
		uint32_t address;
		uint32_t length;

		if(cmdc < 3) {
			usage(name);
		}

		address = strtoul(cmdv[1], NULL, 16);
		length = strtoul(cmdv[2], NULL, 16);

		loader_start_flashrange(LOADER_FLASH_LOCKDOWN, address, length);
	} else if(!strcmp(cmd, "funlock")) {
		uint32_t address;
		uint32_t length;

		if(cmdc < 3) {
			usage(name);
		}

		address = strtoul(cmdv[1], NULL, 16);
		length = strtoul(cmdv[2], NULL, 16);

		loader_start_flashrange(LOADER_FLASH_UNLOCK, address, length);
	} else if(!strcmp(cmd, "fgetlock")) {
		uint32_t address;
		uint32_t length;

		if(cmdc < 3) {
			usage(name);
		}

		address = strtoul(cmdv[1], NULL, 16);
		length = strtoul(cmdv[2], NULL, 16);

		loader_start_flashrange(LOADER_FLASH_GETLOCK, address, length);
	} else if(!strcmp(cmd, "help")) {
		usage(name);
	} else {
		printf("Unknown command '%s'\n", cmd);
		usage(name);
	}

	if(osmoload.state == STATE_QUERY_PENDING) {
		osmoload.timeout.cb = &query_timeout;
		osmo_timer_schedule(&osmoload.timeout, 0, 5000000);
	}
	if(osmoload.state == STATE_LOAD_IN_PROGRESS) {
		osmoload.timeout.cb = &memop_timeout;
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
		osmo_select_main(0);
	}

	if(osmoload.binfile) {
		fclose(osmoload.binfile);
	}

	return 0;
}
