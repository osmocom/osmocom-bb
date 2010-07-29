#ifndef _L23_APP_H
#define _L23_APP_H

/* initialization, called once when starting the app, before entering
 * select loop */
extern int l23_app_init(struct osmocom_ms *ms);
extern int (*l23_app_work) (struct osmocom_ms *ms);
extern int (*l23_app_exit) (struct osmocom_ms *ms);

#endif /* _L23_APP_H */
