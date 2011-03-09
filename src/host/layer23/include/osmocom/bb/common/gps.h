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

enum {
	GPS_TYPE_UNDEF,
	GPS_TYPE_GPSD,
	GPS_TYPE_SERIAL
};

struct osmo_gps {
	/* GPS device */
	uint8_t		enable;
	uint8_t		gps_type;

#ifdef _HAVE_GPSD
	char		gpsd_host[32];
	char		gpsd_port[6];
#endif

	char		device[32];
	uint32_t	baud;

	/* current data */
	uint8_t		valid; /* we have a fix */
	time_t		gmt; /* GMT time when position was received */
	double		latitude, longitude;
};

extern struct osmo_gps g;

int osmo_gps_open(void);
void osmo_gps_close(void);
void osmo_gps_init(void);


