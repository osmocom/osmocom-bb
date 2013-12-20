/* GPRS Networks Service (NS) messages on the Gb interface
 * 3GPP TS 08.16 version 8.0.1 Release 1999 / ETSI TS 101 299 V8.0.1 (2002-05) */

/* NS-over-FR-over-GRE implementation */

/* (C) 2009-2010 by Harald Welte <laforge@gnumonks.org>
 *
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
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include <osmocom/core/select.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/socket.h>
#include <osmocom/gprs/gprs_ns.h>

#include "common_vty.h"

#define GRE_PTYPE_FR	0x6559
#define GRE_PTYPE_IPv4	0x0800
#define GRE_PTYPE_KAR	0x0000	/* keepalive response */

struct gre_hdr {
	uint16_t flags;
	uint16_t ptype;
} __attribute__ ((packed));

#if defined(__FreeBSD__) || defined(__APPLE__)
/**
 * On BSD the IPv4 struct is called struct ip and instead of iXX
 * the members are called ip_XX. One could change this code to use
 * struct ip but that would require to define _BSD_SOURCE and that
 * might have other complications. Instead make sure struct iphdr
 * is present on FreeBSD. The below is taken from GLIBC.
 *
 * The GNU C Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */
struct iphdr
  {
#if BYTE_ORDER == LITTLE_ENDIAN
    unsigned int ihl:4;
    unsigned int version:4;
#elif BYTE_ORDER == BIG_ENDIAN
    unsigned int version:4;
    unsigned int ihl:4;
#endif
    u_int8_t tos;
    u_int16_t tot_len;
    u_int16_t id;
    u_int16_t frag_off;
    u_int8_t ttl;
    u_int8_t protocol;
    u_int16_t check;
    u_int32_t saddr;
    u_int32_t daddr;
    /*The options start here. */
  };
#endif


/* IPv4 messages inside the GRE tunnel might be GRE keepalives */
static int handle_rx_gre_ipv4(struct osmo_fd *bfd, struct msgb *msg,
				struct iphdr *iph, struct gre_hdr *greh)
{
	struct gprs_ns_inst *nsi = bfd->data;
	int gre_payload_len;
	struct iphdr *inner_iph;
	struct gre_hdr *inner_greh;
	struct sockaddr_in daddr;
	struct in_addr ia;

	gre_payload_len = msg->len - (iph->ihl*4 + sizeof(*greh));

	inner_iph = (struct iphdr *) ((uint8_t *)greh + sizeof(*greh));

	if (gre_payload_len < inner_iph->ihl*4 + sizeof(*inner_greh)) {
		LOGP(DNS, LOGL_ERROR, "GRE keepalive too short\n");
		return -EIO;
	}

	if (inner_iph->saddr != iph->daddr ||
	    inner_iph->daddr != iph->saddr) {
		LOGP(DNS, LOGL_ERROR,
			"GRE keepalive with wrong tunnel addresses\n");
		return -EIO;
	}

	if (inner_iph->protocol != IPPROTO_GRE) {
		LOGP(DNS, LOGL_ERROR, "GRE keepalive with wrong protocol\n");
		return -EIO;
	}

	inner_greh = (struct gre_hdr *) ((uint8_t *)inner_iph + iph->ihl*4);
	if (inner_greh->ptype != htons(GRE_PTYPE_KAR)) {
		LOGP(DNS, LOGL_ERROR, "GRE keepalive inner GRE type != 0\n");
		return -EIO;
	}

	/* Actually send the response back */

	daddr.sin_family = AF_INET;
	daddr.sin_addr.s_addr = inner_iph->daddr;
	daddr.sin_port = IPPROTO_GRE;

	ia.s_addr = iph->saddr;
	LOGP(DNS, LOGL_DEBUG, "GRE keepalive from %s, responding\n",
		inet_ntoa(ia));

	return sendto(nsi->frgre.fd.fd, inner_greh,
		      gre_payload_len - inner_iph->ihl*4, 0,
		      (struct sockaddr *)&daddr, sizeof(daddr));
}

static struct msgb *read_nsfrgre_msg(struct osmo_fd *bfd, int *error,
					struct sockaddr_in *saddr)
{
	struct msgb *msg = msgb_alloc(NS_ALLOC_SIZE, "Gb/NS/FR/GRE Rx");
	int ret = 0;
	socklen_t saddr_len = sizeof(*saddr);
	struct iphdr *iph;
	struct gre_hdr *greh;
	uint8_t *frh;
	uint16_t dlci;

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
		goto out_err;
	} else if (ret == 0) {
		*error = ret;
		goto out_err;
	}

	msgb_put(msg, ret);

	if (msg->len < sizeof(*iph) + sizeof(*greh) + 2) {
		LOGP(DNS, LOGL_ERROR, "Short IP packet: %u bytes\n", msg->len);
		*error = -EIO;
		goto out_err;
	}

	iph = (struct iphdr *) msg->data;
	if (msg->len < (iph->ihl*4 + sizeof(*greh) + 2)) {
		LOGP(DNS, LOGL_ERROR, "Short IP packet: %u bytes\n", msg->len);
		*error = -EIO;
		goto out_err;
	}

	greh = (struct gre_hdr *) (msg->data + iph->ihl*4);
	if (greh->flags) {
		LOGP(DNS, LOGL_NOTICE, "Unknown GRE flags 0x%04x\n",
			ntohs(greh->flags));
	}

	switch (ntohs(greh->ptype)) {
	case GRE_PTYPE_IPv4:
		/* IPv4 messages might be GRE keepalives */
		*error = handle_rx_gre_ipv4(bfd, msg, iph, greh);
		goto out_err;
		break;
	case GRE_PTYPE_FR:
		/* continue as usual */
		break;
	default:
		LOGP(DNS, LOGL_NOTICE, "Unknown GRE protocol 0x%04x != FR\n",
			ntohs(greh->ptype));
		*error = -EIO;
		goto out_err;
		break;
	}

	if (msg->len < sizeof(*greh) + 2) {
		LOGP(DNS, LOGL_ERROR, "Short FR header: %u bytes\n", msg->len);
		*error = -EIO;
		goto out_err;
	}

	frh = (uint8_t *)greh + sizeof(*greh);
	if (frh[0] & 0x01) {
		LOGP(DNS, LOGL_NOTICE, "Unsupported single-byte FR address\n");
		*error = -EIO;
		goto out_err;
	}
	dlci = ((frh[0] & 0xfc) << 2);
	if ((frh[1] & 0x0f) != 0x01) {
		LOGP(DNS, LOGL_NOTICE, "Unknown second FR octet 0x%02x\n",
			frh[1]);
		*error = -EIO;
		goto out_err;
	}
	dlci |= (frh[1] >> 4);

	msg->l2h = frh+2;

	/* Store DLCI in NETWORK BYTEORDER in sockaddr port member */
	saddr->sin_port = htons(dlci);

	return msg;

out_err:
	msgb_free(msg);
	return NULL;
}

int gprs_ns_rcvmsg(struct gprs_ns_inst *nsi, struct msgb *msg,
		   struct sockaddr_in *saddr, enum gprs_ns_ll ll);

static int handle_nsfrgre_read(struct osmo_fd *bfd)
{
	int rc;
	struct sockaddr_in saddr;
	struct gprs_ns_inst *nsi = bfd->data;
	struct msgb *msg;
	uint16_t dlci;

	msg = read_nsfrgre_msg(bfd, &rc, &saddr);
	if (!msg)
		return rc;

	dlci = ntohs(saddr.sin_port);
	if (dlci == 0 || dlci == 1023) {
		LOGP(DNS, LOGL_INFO, "Received FR on LMI DLCI %u - ignoring\n",
			dlci);
		rc = 0;
		goto out;
	}

	rc = gprs_ns_rcvmsg(nsi, msg, &saddr, GPRS_NS_LL_FR_GRE);
out:
	msgb_free(msg);

	return rc;
}

static int handle_nsfrgre_write(struct osmo_fd *bfd)
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
	daddr.sin_family = AF_INET;
	daddr.sin_addr = nsvc->frgre.bts_addr.sin_addr;
	daddr.sin_port = IPPROTO_GRE;

	/* Prepend the FR header */
	frh = msgb_push(msg, 2);
	frh[0] = (dlci >> 2) & 0xfc;
	frh[1] = ((dlci & 0xf)<<4) | 0x01;

	/* Prepend the GRE header */
	greh = (struct gre_hdr *) msgb_push(msg, sizeof(*greh));
	greh->flags = 0;
	greh->ptype = htons(GRE_PTYPE_FR);

	rc = sendto(nsi->frgre.fd.fd, msg->data, msg->len, 0,
		  (struct sockaddr *)&daddr, sizeof(daddr));

	msgb_free(msg);

	return rc;
}

static int nsfrgre_fd_cb(struct osmo_fd *bfd, unsigned int what)
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
	struct in_addr in;
	int rc;

	in.s_addr = htonl(nsi->frgre.local_ip);

	/* Make sure we close any existing socket before changing it */
	if (nsi->frgre.fd.fd)
		close(nsi->frgre.fd.fd);

	if (!nsi->frgre.enabled)
		return 0;

	nsi->frgre.fd.cb = nsfrgre_fd_cb;
	nsi->frgre.fd.data = nsi;
	rc = osmo_sock_init_ofd(&nsi->frgre.fd, AF_INET, SOCK_RAW,
				IPPROTO_GRE, inet_ntoa(in), 0,
				OSMO_SOCK_F_BIND);
	if (rc < 0) {
		LOGP(DNS, LOGL_ERROR, "Error creating GRE socket (%s)\n",
			strerror(errno));
		return rc;
	}
	nsi->frgre.fd.data = nsi;

	return rc;
}
