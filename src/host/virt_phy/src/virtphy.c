/* osmocom includes */

#include <osmocom/core/msgb.h>
#include <osmocom/core/select.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <virtphy/virtual_um.h>
#include <virtphy/l1ctl_sock.h>
#include <virtphy/virt_l1_model.h>
#include <virtphy/l1ctl_sap.h>
#include <virtphy/gsmtapl1_if.h>
#include <virtphy/logging.h>
#include <virtphy/virt_l1_sched.h>

int main( int argc, char *argv[] )
{
	// init loginfo
	static struct l1_model_ms *model;
	char * l1ctl_sock_path = NULL;

	// get path from commandline argument
	if( argc > 1 ) {
	      l1ctl_sock_path = argv[1];
	}


	//ms_log_init("DL1C,1:DVIRPHY,1");
	ms_log_init("DL1C,1");
	//ms_log_init("DL1C,8:DVIRPHY,8");

	LOGP(DVIRPHY, LOGL_INFO, "Virtual physical layer starting up...\n");

	model = l1_model_ms_init(NULL);

	// TODO: make this configurable
	model->vui = virt_um_init(NULL, DEFAULT_BTS_MCAST_GROUP,
	DEFAULT_BTS_MCAST_PORT, DEFAULT_MS_MCAST_GROUP,
	DEFAULT_MS_MCAST_PORT, gsmtapl1_rx_from_virt_um_inst_cb);
	model->lsi = l1ctl_sock_init(NULL, l1ctl_sap_rx_from_l23_inst_cb, l1ctl_sock_path);

	gsmtapl1_init(model);
	l1ctl_sap_init(model);
	virt_l1_sched_init(model);

	LOGP(DVIRPHY, LOGL_INFO, "Virtual physical layer ready...\n \
			Waiting for l23 app on %s", l1ctl_sock_path);

	while (1) {
		// handle osmocom fd READ events (l1ctl-unix-socket, virtual-um-mcast-socket)
		osmo_select_main(0);
		// handle outgoing l1ctl primitives to l2
		// TODO implement scheduler for uplink messages
	}

	l1_model_ms_destroy(model);

	// not reached
	return EXIT_FAILURE;
}
