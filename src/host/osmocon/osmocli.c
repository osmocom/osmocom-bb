/* osmocli */

/* (C) 2013 by Andreas Eversberg <jolly@eversberg.eu>
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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <sercomm.h>

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/select.h>
#include <osmocom/core/serial.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/timer.h>

#include <arpa/inet.h>

#define MODEM_BAUDRATE		B115200

struct osmo_fd serial_fd;
struct osmo_fd console_fd;

static void hdlc_send_to_phone(uint8_t dlci, uint8_t *data, int len)
{
	struct msgb *msg;
	uint8_t *dest;

	if (len > 512) {
		fprintf(stderr, "Too much data to send. %u\n", len);
		return;
	}

	/* push the message into the stack */
	msg = sercomm_alloc_msgb(512);
	if (!msg) {
		fprintf(stderr, "Failed to create data for the frame.\n");
		return;
	}

	/* copy the data */
	dest = msgb_put(msg, len);
	memcpy(dest, data, len);

	sercomm_sendmsg(dlci, msg);

	serial_fd.when |= BSC_FD_WRITE;
}

static int handle_sercomm_write(void)
{
	uint8_t buffer[256];
	int i, count = 0, end = 0;

	for (i = 0; i < sizeof(buffer); i++) {
		if (sercomm_drv_pull(&buffer[i]) == 0) {
			end = 1;
			break;
		}
		count++;
	}

	if (count) {
		if (write(serial_fd.fd, buffer, count) != count)
			perror("short write");
	}

	if (end)
		serial_fd.when &= ~BSC_FD_WRITE;

	return 0;
}

static uint8_t buffer[256];

static int handle_sercomm_read(void)
{
	int nbytes, i;

	nbytes = read(serial_fd.fd, buffer, sizeof(buffer));
	if (nbytes <= 0)
		return nbytes;

	for (i = 0; i < nbytes; ++i) {
		if (sercomm_drv_rx_char(buffer[i]) == 0)
			printf("Dropping sample '%c'\n", buffer[i]);
	}

	return nbytes;
}

static int serial_cb(struct osmo_fd *fd, unsigned int flags)
{
	int rc;
	if (flags & BSC_FD_READ) {
		while ((rc = handle_sercomm_read()) > 0);
	}

	if (flags & BSC_FD_WRITE) {
		rc = handle_sercomm_write();
	}
	return 0;
}

static char inputbuffer[256];
static char *inputptr = inputbuffer;

static int handle_console_read(void)
{
	int nbytes;

again:
	nbytes = read(console_fd.fd, inputptr, 1);
	if (nbytes <= 0)
		return nbytes;
	inputptr++;

	if ((inputptr - inputbuffer) == sizeof(inputbuffer)-1
	 || inputptr[-1] == '\n') {
		*inputptr = '\0';
		hdlc_send_to_phone(SC_DLCI_CONSOLE, (uint8_t *)inputbuffer,
			strlen(inputbuffer)-1);
		inputptr = inputbuffer;
	}

	goto again;
}

static int console_cb(struct osmo_fd *fd, unsigned int flags)
{
	int rc;
	if (flags & BSC_FD_READ) {
		while ((rc = handle_console_read()) > 0);
//		if (rc == 0)
//			exit(2);
	}

	return 0;
}


static void hdlc_console_cb(uint8_t dlci, struct msgb *msg)
{
	int rc;

	rc = write(1, msg->data, msg->len);
	fflush(stdout);
	msgb_free(msg);
}

#define HELP_TEXT \
	"[ -v | -h ] [ -p /dev/ttyXXXX ]\n"

static int usage(const char *name)
{
	printf("Usage: %s ", name);
	printf(HELP_TEXT);
	exit(2);
}

static int version(const char *name)
{
	printf("%s version %s\n", name, PACKAGE_VERSION);
	exit(2);
}

int main(int argc, char **argv)
{
	int opt, flags;
	const char *serial_dev = "/dev/ttyUSB1";

	while ((opt = getopt(argc, argv, "hp:v")) != -1) {
		switch (opt) {
		case 'p':
			serial_dev = optarg;
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

	serial_fd.fd = osmo_serial_init(serial_dev, MODEM_BAUDRATE);
	if (serial_fd.fd < 0) {
		fprintf(stderr, "Cannot open serial device %s\n", serial_dev);
		exit(1);
	}

	if (osmo_fd_register(&serial_fd) != 0) {
		fprintf(stderr, "Failed to register the serial.\n");
		exit(1);
	}

	/* Set serial socket to non-blocking mode of operation */
	flags = fcntl(serial_fd.fd, F_GETFL);
	flags |= O_NONBLOCK;
	fcntl(serial_fd.fd, F_SETFL, flags);

	serial_fd.when = BSC_FD_READ;
	serial_fd.cb = serial_cb;

	console_fd.fd = 0;

	if (osmo_fd_register(&console_fd) != 0) {
		fprintf(stderr, "Failed to register the serial.\n");
		exit(1);
	}

	console_fd.when = BSC_FD_READ;
	console_fd.cb = console_cb;

	/* initialize the HDLC layer */
	sercomm_init();
	sercomm_register_rx_cb(SC_DLCI_CONSOLE, hdlc_console_cb);

	while (1) {
		if (osmo_select_main(0) < 0)
			break;
	}

	close(serial_fd.fd);

	exit(0);
}
