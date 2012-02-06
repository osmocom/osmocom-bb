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
#include <time.h>
#include <stdbool.h>

#ifdef _HAVE_GPSD
#include <gps.h>
#endif

#include <osmocom/core/utils.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/gps.h>

struct osmo_gps g = {
	0,
	GPS_TYPE_UNDEF,
#ifdef _HAVE_GPSD
    "localhost",
	"2947",
#endif
	"/dev/ttyACM0",
	0,
	0,
	0,
	0,0
};

static struct osmo_fd gps_bfd;

#ifdef _HAVE_GPSD

static struct gps_data_t* gdata = NULL;

#if GPSD_API_MAJOR_VERSION >= 5
static struct gps_data_t _gdata;
#define gps_poll gps_read
#endif

int osmo_gpsd_cb(struct osmo_fd *bfd, unsigned int what)
{
	struct tm *tm;
	unsigned diff = 0;

	g.valid = 0;

	/* gps is offline */
	if (gdata->online)
	    goto gps_not_ready;

#if GPSD_API_MAJOR_VERSION >= 5
	/* gps has no data */
	if (gps_waiting(gdata, 500))
	    goto gps_not_ready;
#else
	/* gps has no data */
	if (gps_waiting(gdata))
	    goto gps_not_ready;
#endif

	/* polling returned an error */
	if (gps_poll(gdata))
	    goto gps_not_ready;

	/* data are valid */
	if (gdata->set & LATLON_SET) {
		g.valid = 1;
		g.gmt = gdata->fix.time;
		tm = localtime(&g.gmt);
		diff = time(NULL) - g.gmt;
		g.latitude = gdata->fix.latitude;
		g.longitude = gdata->fix.longitude;

		LOGP(DGPS, LOGL_INFO, " time=%02d:%02d:%02d %04d-%02d-%02d, "
			"diff-to-host=%d, latitude=%do%.4f, longitude=%do%.4f\n",
			tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_year + 1900,
			tm->tm_mday, tm->tm_mon + 1, diff,
			(int)g.latitude,
			(g.latitude - ((int)g.latitude)) * 60.0,
			(int)g.longitude,
			(g.longitude - ((int)g.longitude)) * 60.0);
	}

	return 0;

gps_not_ready:
	LOGP(DGPS, LOGL_DEBUG, "gps is offline");
	return -1;
}

int osmo_gpsd_open(void)
{
	LOGP(DGPS, LOGL_INFO, "Connecting to gpsd at '%s:%s'\n", g.gpsd_host, g.gpsd_port);

	gps_bfd.data = NULL;
	gps_bfd.when = BSC_FD_READ;
	gps_bfd.cb = osmo_gpsd_cb;

#if GPSD_API_MAJOR_VERSION >= 5
	if (gps_open(g.gpsd_host, g.gpsd_port, &_gdata) == -1)
		gdata = NULL;
	else
		gdata = &_gdata;
#else
	gdata = gps_open(g.gpsd_host, g.gpsd_port);
#endif
	if (gdata == NULL) {
		LOGP(DGPS, LOGL_ERROR, "Can't connect to gpsd\n");
		return -1;
	}
	gps_bfd.fd = gdata->gps_fd;
	if (gps_bfd.fd < 0)
		return gps_bfd.fd;

	if (gps_stream(gdata, WATCH_ENABLE, NULL) == -1) {
		LOGP(DGPS, LOGL_ERROR, "Error in gps_stream()\n");
		return -1;
	}

	osmo_fd_register(&gps_bfd);

	return 0;
}

void osmo_gpsd_close(void)
{
	if (gps_bfd.fd <= 0)
		return;

	LOGP(DGPS, LOGL_INFO, "Disconnecting from gpsd\n");

	osmo_fd_unregister(&gps_bfd);

#if GPSD_API_MAJOR_VERSION >= 5
	gps_stream(gdata, WATCH_DISABLE, NULL);
#endif
	gps_close(gdata);
	gps_bfd.fd = -1; /* -1 or 0 indicates: 'close' */
}

#endif

static struct termios gps_termios, gps_old_termios;

static int osmo_serialgps_line(char *line)
{
	time_t gps_now, host_now;
	struct tm *tm;
	int32_t diff;
	double latitude, longitude;

	if (!!strncmp(line, "$GPGLL", 6))
		return 0;
	line += 7;
	if (strlen(line) < 37)
		return 0;
	line[37] = '\0';
	/* ddmm.mmmm,N,dddmm.mmmm,E,hhmmss.mmm,A */

	/* valid position */
	if (line[36] != 'A') {
		LOGP(DGPS, LOGL_INFO, "%s (invalid)\n", line);
		g.valid = 0;
		return 0;
	}
	g.valid = 1;

	/* time stamp */
	gps_now = line[30] - '0';
	gps_now += (line[29] - '0') * 10;
	gps_now += (line[28] - '0') * 60;
	gps_now += (line[27] - '0') * 600;
	gps_now += (line[26] - '0') * 3600;
	gps_now += (line[25] - '0') * 36000;
	time(&host_now);
	/* calculate the number of seconds the host differs from GPS */
	diff = host_now % 86400 - gps_now;
	if (diff < 0)
		diff += 86400;
	if (diff >= 43200)
		diff -= 86400;
	/* apply the "date" part to the GPS time */
	gps_now = host_now - diff;
	g.gmt = gps_now;
	tm = localtime(&gps_now);

	/* position */
	latitude = (double)(line[0] - '0') * 10.0;
	latitude += (double)(line[1] - '0');
	latitude += (double)(line[2] - '0') / 6.0;
	latitude += (double)(line[3] - '0') / 60.0;
	latitude += (double)(line[5] - '0') / 600.0;
	latitude += (double)(line[6] - '0') / 6000.0;
	latitude += (double)(line[7] - '0') / 60000.0;
	latitude += (double)(line[8] - '0') / 600000.0;
	if (line[10] == 'S')
		latitude = 0.0 - latitude;
	g.latitude = latitude;
	longitude = (double)(line[12] - '0') * 100.0;
	longitude += (double)(line[13] - '0') * 10.0;
	longitude += (double)(line[14] - '0');
	longitude += (double)(line[15] - '0') / 6.0;
	longitude += (double)(line[16] - '0') / 60.0;
	longitude += (double)(line[18] - '0') / 600.0;
	longitude += (double)(line[19] - '0') / 6000.0;
	longitude += (double)(line[20] - '0') / 60000.0;
	longitude += (double)(line[21] - '0') / 600000.0;
	if (line[23] == 'W')
		longitude = 360.0 - longitude;
	g.longitude = longitude;
	
	LOGP(DGPS, LOGL_DEBUG, "%s\n", line);
	LOGP(DGPS, LOGL_INFO, " time=%02d:%02d:%02d %04d-%02d-%02d, "
		"diff-to-host=%d, latitude=%do%.4f, longitude=%do%.4f\n",
		tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_year + 1900,
		tm->tm_mday, tm->tm_mon + 1, diff,
		(int)g.latitude,
		(g.latitude - ((int)g.latitude)) * 60.0,
		(int)g.longitude,
		(g.longitude - ((int)g.longitude)) * 60.0);
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

int osmo_serialgps_cb(struct osmo_fd *bfd, unsigned int what)
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
				osmo_serialgps_line(line);
			continue;
		}
		line[lpos++] = buff[i++];
		if (lpos == sizeof(line))
			lpos--;
	}

	return 0;
}

int osmo_serialgps_open(void)
{
	int baud = 0;

	if (gps_bfd.fd > 0)
		return 0;

	LOGP(DGPS, LOGL_INFO, "Open GPS device '%s'\n", g.device);

	gps_bfd.data = NULL;
	gps_bfd.when = BSC_FD_READ;
	gps_bfd.cb = osmo_serialgps_cb;
	gps_bfd.fd = open(g.device, O_RDONLY);
	if (gps_bfd.fd < 0)
		return gps_bfd.fd;

	switch (g.baud) {
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

	osmo_fd_register(&gps_bfd);

	return 0;
}

void osmo_serialgps_close(void)
{
	if (gps_bfd.fd <= 0)
		return;

	LOGP(DGPS, LOGL_INFO, "Close GPS device\n");

	osmo_fd_unregister(&gps_bfd);

	if (isatty(gps_bfd.fd))
		tcsetattr(gps_bfd.fd, TCSANOW, &gps_old_termios);

	close(gps_bfd.fd);
	gps_bfd.fd = -1; /* -1 or 0 indicates: 'close' */
}

void osmo_gps_init(void)
{
	memset(&gps_bfd, 0, sizeof(gps_bfd));
}

int osmo_gps_open(void)
{
	switch (g.gps_type) {
#ifdef _HAVE_GPSD
		case GPS_TYPE_GPSD:
			return osmo_gpsd_open();
#endif
		case GPS_TYPE_SERIAL:
			return osmo_serialgps_open();

		default:
			return 0;
	}
}

void osmo_gps_close(void)
{
	switch (g.gps_type) {
#ifdef _HAVE_GPSD
		case GPS_TYPE_GPSD:
			return osmo_gpsd_close();
#endif
		case GPS_TYPE_SERIAL:
			return osmo_serialgps_close();

		default:
			return;
	}
}

