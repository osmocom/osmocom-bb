#pragma once

/*
 * TCP port numbers used for VTY interfaces in osmocom projects. See also the
 * osmocom wiki as well as the osmo-gsm-manuals, which should all be kept in
 * sync with this file:
 * https://osmocom.org/projects/cellular-infrastructure/wiki/PortNumbers
 * https://git.osmocom.org/osmo-gsm-manuals/tree/common/chapters/port_numbers.adoc
 */

/* 4238 used by osmo-bts control interface */
#define OSMO_VTY_PORT_PCU	4240	/* also: osmo_pcap_client */
#define OSMO_VTY_PORT_BTS	4241	/* also: osmo_pcap_server */
#define OSMO_VTY_PORT_NITB_BSC	4242
#define OSMO_VTY_PORT_BSC_MGCP	4243
#define OSMO_VTY_PORT_BSC_NAT	4244
#define OSMO_VTY_PORT_SGSN	4245
#define OSMO_VTY_PORT_GBPROXY	4246
#define OSMO_VTY_PORT_BB	4247
/* 4249-4251 used by control interface */
#define OSMO_VTY_PORT_BTSMGR	4252
#define OSMO_VTY_PORT_GTPHUB	4253
#define OSMO_VTY_PORT_CSCN	4254
/* 4255 used by control interface */
#define OSMO_VTY_PORT_MNCC_SIP	4256
/* 4257 used by control interface */
