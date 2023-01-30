#ifndef _L23_APP_H
#define _L23_APP_H

#include <osmocom/core/tun.h>
#include <osmocom/core/gsmtap.h>

struct option;
struct vty_app_info;

/* Options supported by the l23 app */
enum {
	L23_OPT_SAP	= 1 << 0,
	L23_OPT_ARFCN	= 1 << 1,
	L23_OPT_TAP	= 1 << 2,
	L23_OPT_VTY	= 1 << 3,
	L23_OPT_DBG	= 1 << 4,
};

/* see (struct l23_global_config)->gsmtap.categ_gprs_mask */
enum l23_gsmtap_gprs_category {
	L23_GSMTAP_GPRS_C_DL_UNKNOWN	= 0,	/* unknown or undecodable downlink blocks */
	L23_GSMTAP_GPRS_C_DL_DUMMY	= 1,	/* downlink dummy blocks */
	L23_GSMTAP_GPRS_C_DL_CTRL	= 2,	/* downlink control blocks */
	L23_GSMTAP_GPRS_C_DL_DATA_GPRS	= 3,	/* downlink GPRS data blocks */
	L23_GSMTAP_GPRS_C_DL_DATA_EGPRS	= 4,	/* downlink EGPRS data blocks */

	L23_GSMTAP_GPRS_C_UL_UNKNOWN	= 5,	/* unknown or undecodable uplink blocks */
	L23_GSMTAP_GPRS_C_UL_DUMMY	= 6,	/* uplink dummy blocks */
	L23_GSMTAP_GPRS_C_UL_CTRL	= 7,	/* uplink control blocks */
	L23_GSMTAP_GPRS_C_UL_DATA_GPRS	= 8,	/* uplink GPRS data blocks */
	L23_GSMTAP_GPRS_C_UL_DATA_EGPRS	= 9,	/* uplink EGPRS data blocks */
};

struct l23_global_config {
	struct {
		char *remote_host;
		uint32_t lchan_mask; /* see l23_gsmtap_gprs_category */
		uint32_t lchan_acch_mask; /* see l23_gsmtap_gprs_category */
		bool lchan_acch;
		uint32_t categ_gprs_mask;
		struct gsmtap_inst *inst;
	} gsmtap;
};
extern struct l23_global_config l23_cfg;

extern void *l23_ctx;

/* initialization, called once when starting the app, before reading VTY config */
extern int l23_app_init(void);

/* Start work after reading VTY config and starting layer23 components,
 * immediately before entering main select loop */
extern int (*l23_app_start)(void);

extern int (*l23_app_work)(void);
extern int (*l23_app_exit)(void);

/* configuration options */
struct l23_app_info {
	const char *copyright;
	const char *contribution;
	struct vty_app_info *vty_info; /* L23_OPT_VTY */

	char *getopt_string;
	int (*cfg_supported)();
	int (*cfg_print_help)();
	int (*cfg_getopt_opt)(struct option **options);
	int (*cfg_handle_opt)(int c,const char *optarg);
	int (*vty_init)(void);
	osmo_tundev_data_ind_cb_t tun_data_ind_cb;
};

extern struct l23_app_info *l23_app_info();

#endif /* _L23_APP_H */
