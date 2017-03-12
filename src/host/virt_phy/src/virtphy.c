/* osmocom includes */

#include <osmocom/core/msgb.h>
#include <osmocom/core/select.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <virtphy/virtual_um.h>
#include <virtphy/l1ctl_sock.h>
#include <virtphy/virt_l1_model.h>
#include <virtphy/l1ctl_sap.h>
#include <virtphy/gsmtapl1_if.h>
#include <virtphy/logging.h>
#include <virtphy/virt_l1_sched.h>

#define DEFAULT_MCAST_PORT 4729 /* IANA-registered port for GSMTAP */
#define DEFAULT_LOG_MASK "DL1C,1:DVIRPHY,1"

static char* dl_rx_grp = DEFAULT_MS_MCAST_GROUP;
static char* ul_tx_grp = DEFAULT_BTS_MCAST_GROUP;
static int port = DEFAULT_MCAST_PORT;
static char* log_mask = DEFAULT_LOG_MASK;
static char * l1ctl_sock_path = L1CTL_SOCK_PATH;

static void handle_options(int argc, char **argv)
{
	while (1) {
		int option_index = 0, c;
		static struct option long_options[] = {
			{"dl-rx-grp", required_argument, 0, 'z'},
		        {"ul-tx-grp", required_argument, 0, 'y'},
		        {"port", required_argument, 0, 'x'},
		        {"log-mask", required_argument, 0, 'd'},
		        {"l1ctl-sock", required_argument, 0, 's'},
		        {0, 0, 0, 0},
		};
		c = getopt_long(argc, argv, "z:y:x:d:s:", long_options,
		                &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'z':
			dl_rx_grp = optarg;
			break;
		case 'y':
			ul_tx_grp = optarg;
			break;
		case 'x':
			port = atoi(optarg);
			break;
		case 'd':
			log_mask = optarg;
			break;
		case 's':
			l1ctl_sock_path = optarg;
			break;
		default:
			break;
		}
	}
}

int main(int argc, char *argv[])
{
	// init loginfo
	static struct l1_model_ms *model;

	handle_options(argc, argv);

	ms_log_init(log_mask);

	LOGP(DVIRPHY, LOGL_INFO, "Virtual physical layer starting up...\n");

	model = l1_model_ms_init(NULL);

	model->vui = virt_um_init(NULL, ul_tx_grp, port, dl_rx_grp, port,
	                          gsmtapl1_rx_from_virt_um_inst_cb);
	model->lsi = l1ctl_sock_init(NULL, l1ctl_sap_rx_from_l23_inst_cb,
	                             l1ctl_sock_path);

	gsmtapl1_init(model);
	l1ctl_sap_init(model);
	virt_l1_sched_init(model);

	LOGP(DVIRPHY, LOGL_INFO,
	     "Virtual physical layer ready...\n \
			Waiting for l23 app on %s",
	     l1ctl_sock_path);

	while (1) {
		// handle osmocom fd READ events (l1ctl-unix-socket, virtual-um-mcast-socket)
		osmo_select_main(0);
	}

	l1_model_ms_destroy(model);

	// not reached
	return EXIT_FAILURE;
}
