/*
 * (C) 2013-2014 by Harald Welte <laforge@gnumonks.org>
 * (C) 2014 by Holger Hans Peter Freyther
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

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>


int osmo_macaddr_parse(uint8_t *out, const char *in)
{
	/* 00:00:00:00:00:00 */
	char tmp[18];
	char *tok;
	unsigned int i = 0;

	if (strlen(in) < 17)
		return -1;

	strncpy(tmp, in, sizeof(tmp)-1);
	tmp[sizeof(tmp)-1] = '\0';

	for (tok = strtok(tmp, ":"); tok && (i < 6); tok = strtok(NULL, ":")) {
		unsigned long ul = strtoul(tok, NULL, 16);
		out[i++] = ul & 0xff;
	}

	return 0;
}

#if defined(__FreeBSD__) || defined(__APPLE__)
#include <sys/socket.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <net/if_dl.h>
#include <net/if_types.h>


int osmo_get_macaddr(uint8_t *mac_out, const char *dev_name)
{
	int rc = -1;
	struct ifaddrs *ifa, *ifaddr;

	if (getifaddrs(&ifaddr) != 0)
		return -1;

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		struct sockaddr_dl *sdl;

		sdl = (struct sockaddr_dl *) ifa->ifa_addr;
		if (!sdl)
			continue;
		if (sdl->sdl_family != AF_LINK)
			continue;
		if (sdl->sdl_type != IFT_ETHER)
			continue;
		if (strcmp(ifa->ifa_name, dev_name) != 0)
			continue;

		memcpy(mac_out, LLADDR(sdl), 6);
		rc = 0;
		break;
	}

	freeifaddrs(ifaddr);
	return 0;
}

#else

#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>

int osmo_get_macaddr(uint8_t *mac_out, const char *dev_name)
{
	int fd, rc;
	struct ifreq ifr;

	fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (fd < 0)
		return fd;

	memset(&ifr, 0, sizeof(ifr));
	memcpy(&ifr.ifr_name, dev_name, sizeof(ifr.ifr_name));
	rc = ioctl(fd, SIOCGIFHWADDR, &ifr);
	close(fd);

	if (rc < 0)
		return rc;

	memcpy(mac_out, ifr.ifr_hwaddr.sa_data, 6);

	return 0;
}
#endif
