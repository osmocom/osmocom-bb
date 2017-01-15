/*
 * (C) 2012 by Holger Hans Peter Freyther <zecke@selfish.org>
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define _GNU_SOURCE
#include <osmocom/core/application.h>

#include <osmocom/gprs/gprs_ns.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <stdio.h>
#include <stdlib.h>

#include <dlfcn.h>

int (*real_socket)(int, int, int);

static int GR_SOCKET = -1;

static void resolve_real(void)
{
	if (real_socket)
		return;
	real_socket = dlsym(RTLD_NEXT, "socket");
}

int socket(int domain, int type, int protocol)
{
	int fd;

	resolve_real();
	if (domain != AF_INET || type != SOCK_RAW || protocol != IPPROTO_GRE)
		return (*real_socket)(domain, type, protocol);

	/* Now call socket with a normal UDP/IP socket and assign to GR_SOCKET */
	fd = (*real_socket)(domain, SOCK_DGRAM, IPPROTO_UDP);
	GR_SOCKET = fd;
	return fd;
}

void bssgp_prim_cb()
{
}

static const struct log_info log_info = {};

int main(int argc, char **argv)
{
	int rc;
	struct gprs_ns_inst *nsi;

	log_init(&log_info, NULL);

	nsi = gprs_ns_instantiate(NULL, NULL);
	nsi->frgre.enabled = 1;

	rc = gprs_ns_frgre_listen(nsi);
	printf("Result: %s\n", rc == GR_SOCKET ? "PASSED" : "FAILED");
	return rc == GR_SOCKET ? EXIT_SUCCESS : EXIT_FAILURE;
}


