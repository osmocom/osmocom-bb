/* GPRS Networks Service (NS) messages on the Gb interface
 * 3GPP TS 08.16 version 8.0.1 Release 1999 / ETSI TS 101 299 V8.0.1 (2002-05) */

/* NS-over-FR-over-GRE implementation */

/* (C) 2009-2010 by Harald Welte <laforge@gnumonks.org>
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
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <osmocore/select.h>
#include <osmocore/msgb.h>
#include <osmocore/talloc.h>

#include <openbsc/socket.h>
#include <openbsc/debug.h>
#include <openbsc/gprs_ns.h>

#define GRE_PTYPE_FR	0x6559

struct gre_hdr {
	uint16_t flags;
	uint16_t ptype;
} __attribute__ ((packed));

static struct msgb *read_nsfrgre_msg(struct bsc_fd *bfd, int *error,
					struct sockaddr_in *saddr)
{
	struct msgb *msg = msgb_alloc(NS_ALLOC_SIZE, "Gb/NS/FR/GRE Rx");
	int ret = 0;
	socklen_t saddr_len = sizeof(*saddr);
	struct gre_hdr *greh;
	uint8_t *frh;
	uint32_t dlci;

	if (!msg) {
		*error = -ENOMEM;
		return NULL;
	}

	ret = recvfrom(bfd->fd, msg->data, NS_ALLOC_SIZE, 0,
			(struct sockaddr *)saddr, &saddr_len);
	if (ret < 0) {
		LOGP(DNS, LOGL_ERROR, "recv error %s during NS-FR-GRE recv\n",
			strerror(errno));
		*error = ret;
		return NULL;
	} else if (ret == 0) {
		msgb_free(msg);
		*error = ret;
		return NULL;
	}

	msgb_put(msg, ret);

	if (msg->len < sizeof(*greh)) {
		LOGP(DNS, LOGL_ERROR, "Short GRE packet: %u bytes\n", msg->len);
		*error = -EIO;
		goto out_err;
	}

	greh = (struct gre_hdr *) msg->data;
	if (greh->flags) {
		LOGP(DNS, LOGL_NOTICE, "Unknown GRE flags 0x%04x\n",
			ntohs(greh->flags));
	}
	if (greh->ptype != htons(GRE_PTYPE_FR)) {
		LOGP(DNS, LOGL_NOTICE, "Unknown GRE protocol 0x%04x != FR\n",
			ntohs(greh->ptype));
		*error = -EIO;
		goto out_err;
	}

	if (msg->len < sizeof(*greh) + 2) {
		LOGP(DNS, LOGL_ERROR, "Short FR header: %u bytes\n", msg->len);
		*error = -EIO;
		goto out_err;
	}

	frh = msg->data + sizeof(*greh);
	if (frh[0] & 0x01) {
		LOGP(DNS, LOGL_NOTICE, "Unsupported single-byte FR address\n");
		*error = -EIO;
		goto out_err;
	}
	dlci = (frh[0] & 0xfc << 2);
	if ((frh[1] & 0x0f) != 0x01) {
		LOGP(DNS, LOGL_NOTICE, "Unknown second FR octet 0x%02x\n",
			frh[1]);
		*error = -EIO;
		goto out_err;
	}
	dlci |= frh[1] >> 4;
	if (dlci > 0xffff) {
		LOGP(DNS, LOGL_ERROR, "We don't support DLCI > 65535 (%u)\n",
			dlci);
		*error = -EINVAL;
		goto out_err;
	}

	msg->l2h = msg->data + sizeof(*greh) + 2;

	/* Store DLCI in NETWORK BYTEORDER in sockaddr port member */
	saddr->sin_port = htons(dlci & 0xffff);

	return msg;

out_err:
	msgb_free(msg);
	return NULL;
}

int gprs_ns_rcvmsg(struct gprs_ns_inst *nsi, struct msgb *msg,
		   struct sockaddr_in *saddr, enum gprs_ns_ll ll);

static int handle_nsfrgre_read(struct bsc_fd *bfd)
{
	int error;
	struct sockaddr_in saddr;
	struct gprs_ns_inst *nsi = bfd->data;
	struct msgb *msg = read_nsfrgre_msg(bfd, &error, &saddr);

	if (!msg)
		return error;

	error = gprs_ns_rcvmsg(nsi, msg, &saddr, GPRS_NS_LL_FR_GRE);

	msgb_free(msg);

	return error;
}

static int handle_nsfrgre_write(struct bsc_fd *bfd)
{
	/* FIXME: actually send the data here instead of nsip_sendmsg() */
	return -EIO;
}

int gprs_ns_frgre_sendmsg(struct gprs_nsvc *nsvc, struct msgb *msg)
{
	int rc;
	struct gprs_ns_inst *nsi = nsvc->nsi;
	struct sockaddr_in daddr;
	uint16_t dlci = ntohs(nsvc->frgre.bts_addr.sin_port);
	uint8_t *frh;
	struct gre_hdr *greh;

	/* Build socket address for the packet destionation */
	daddr.sin_addr = nsvc->frgre.bts_addr.sin_addr;
	daddr.sin_port = IPPROTO_GRE;

	/* Prepend the FR header */
	frh = msgb_push(msg, sizeof(frh));
	frh[0] = (dlci >> 2) & 0xfc;
	frh[1] = (dlci & 0xf0) | 0x01;

	/* Prepend the GRE header */
	greh = (struct gre_hdr *) msgb_push(msg, sizeof(*greh));
	greh->flags = 0;
	greh->ptype = GRE_PTYPE_FR;

	rc = sendto(nsi->frgre.fd.fd, msg->data, msg->len, 0,
		  (struct sockaddr *)&daddr, sizeof(daddr));

	talloc_free(msg);

	return rc;
}

static int nsfrgre_fd_cb(struct bsc_fd *bfd, unsigned int what)
{
	int rc = 0;

	if (what & BSC_FD_READ)
		rc = handle_nsfrgre_read(bfd);
	if (what & BSC_FD_WRITE)
		rc = handle_nsfrgre_write(bfd);

	return rc;
}

int gprs_ns_frgre_listen(struct gprs_ns_inst *nsi)
{
	int rc;

	/* Make sure we close any existing socket before changing it */
	if (nsi->frgre.fd.fd)
		close(nsi->frgre.fd.fd);

	if (!nsi->frgre.enabled)
		return 0;

	rc = make_sock(&nsi->frgre.fd, IPPROTO_GRE, nsi->frgre.local_ip,
			0, nsfrgre_fd_cb);
	if (rc < 0) {
		LOGP(DNS, LOGL_ERROR, "Error creating GRE socket (%s)\n",
			strerror(errno));
		return rc;
	}
	nsi->frgre.fd.data = nsi;

	return rc;
}
