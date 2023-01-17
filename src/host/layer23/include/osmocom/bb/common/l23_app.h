#ifndef _L23_APP_H
#define _L23_APP_H

#include <osmocom/core/tun.h>

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
