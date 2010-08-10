/*
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

#include <stdio.h>
#include <sys/file.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <osmocore/utils.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/mobile/gps.h>

struct gps gps = {
	0,
	"/dev/ttyACM0",
	0
};

static struct bsc_fd gps_bfd;
static struct termios gps_termios, gps_old_termios;

static int gps_line(char *line)
{
	if (!!strncmp(line, "$GPGLL", 6))
		return 0;
	line += 7;
	if (strlen(line) < 37)
		return 0;
	line[37] = '\0';
	/* ddmm.mmmm,N,dddmm.mmmm,E,hhmmss.mmm,A */

	printf("'%s'\n", line);

	return 0;
}

static int nmea_checksum(char *line)
{
	uint8_t checksum = 0;

	while (*line) {
		if (*line == '$') {
			line++;
			continue;
		}
		if (*line == '*')
			break;
		checksum ^= *line++;
	}
	return (strtoul(line+1, NULL, 16) == checksum);
}

int gps_cb(struct bsc_fd *bfd, unsigned int what)
{
	char buff[128];
	static char line[128];
	static int lpos = 0;
	int i = 0, len;

	len = read(bfd->fd, buff, sizeof(buff));
	if (len <= 0) {
		fprintf(stderr, "error reading GPS device (errno=%d)\n", errno);
		return len;
	}
	while(i < len) {
		if (buff[i] == 13) {
			i++;
			continue;
		}
		if (buff[i] == 10) {
			line[lpos] = '\0';
			lpos = 0;
			i++;
			if (!nmea_checksum(line))
				fprintf(stderr, "NMEA checksum error\n");
			else
				gps_line(line);
			continue;
		}
		line[lpos++] = buff[i++];
		if (lpos == sizeof(line))
			lpos--;
	}

	return 0;
}

int gps_open(void)
{
	int baud = 0;

	if (gps_bfd.fd > 0)
		return 0;
	gps_bfd.data = NULL;
	gps_bfd.when = BSC_FD_READ;
	gps_bfd.cb = gps_cb;
	gps_bfd.fd = open(gps.device, O_RDONLY);
	if (gps_bfd.fd < 0)
		return gps_bfd.fd;

	switch (gps.baud) {
	case   4800:
		baud = B4800;      break;
	case   9600:
		baud = B9600;      break;
	case  19200:
		baud = B19200;     break;
	case  38400:
		baud = B38400;     break;
	case  57600:
		baud = B57600;     break;	
	case 115200: 
		baud = B115200;    break;
	}

	if (isatty(gps_bfd.fd))
	{
		/* get termios */
		tcgetattr(gps_bfd.fd, &gps_old_termios);
		tcgetattr(gps_bfd.fd, &gps_termios);
		/* set baud */
		if (baud) {
			gps_termios.c_cflag |= baud;
			cfsetispeed(&gps_termios, baud);
			cfsetospeed(&gps_termios, baud);
		}
		if (tcsetattr(gps_bfd.fd, TCSANOW, &gps_termios))
			printf("Failed to set termios for GPS\n");
	}

	bsc_register_fd(&gps_bfd);

	return 0;
}

void gps_close(void)
{
	if (gps_bfd.fd <= 0)
		return;

	bsc_unregister_fd(&gps_bfd);

	if (isatty(gps_bfd.fd))
		tcsetattr(gps_bfd.fd, TCSANOW, &gps_old_termios);

	close(gps_bfd.fd);
	gps_bfd.fd = -1; /* -1 or 0 indicates: 'close' */
}


