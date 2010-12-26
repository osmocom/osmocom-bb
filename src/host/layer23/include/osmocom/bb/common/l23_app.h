#ifndef _L23_APP_H
#define _L23_APP_H

/* Options supported by the l23 app */
enum {
	L23_OPT_SAP	= 1,
	L23_OPT_ARFCN	= 2,
	L23_OPT_TAP	= 4,
	L23_OPT_VTY	= 8,
	L23_OPT_DBG	= 16,
};

/* initialization, called once when starting the app, before entering
 * select loop */
extern int l23_app_init(struct osmocom_ms *ms);
extern int (*l23_app_work) (struct osmocom_ms *ms);
extern int (*l23_app_exit) (struct osmocom_ms *ms);

/* configuration options */
struct l23_app_info {
	const char *copyright;
	const char *contribution;

	int (*cfg_supported)();
};

extern struct l23_app_info *l23_app_info();

#endif /* _L23_APP_H */
