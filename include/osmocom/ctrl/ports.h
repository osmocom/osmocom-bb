#pragma once

/*
 * TCP port numbers used for CTRL interfaces in osmocom projects. See also the
 * osmocom wiki as well as the osmo-gsm-manuals, which should all be kept in
 * sync with this file:
 * https://osmocom.org/projects/cellular-infrastructure/wiki/PortNumbers
 * https://git.osmocom.org/osmo-gsm-manuals/tree/common/chapters/port_numbers.adoc
 */

#define OSMO_CTRL_PORT_BTS	4238
#define OSMO_CTRL_PORT_NITB_BSC	4249
#define OSMO_CTRL_PORT_BSC_NAT	4250
#define OSMO_CTRL_PORT_SGSN	4251
#define OSMO_CTRL_PORT_GGSN	4252
/* 4252-4254 used by VTY interface */
#define OSMO_CTRL_PORT_CSCN	4255
/* 4256 used by VTY interface */
