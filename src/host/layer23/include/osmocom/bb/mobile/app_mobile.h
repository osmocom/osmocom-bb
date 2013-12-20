#ifndef APP_MOBILE_H
#define APP_MOBILE_H

extern char *config_dir;

int l23_app_init(int (*mncc_recv)(struct osmocom_ms *ms, int, void *),
	const char *config_file, const char *vty_ip, uint16_t vty_port);
int l23_app_exit(void);
int l23_app_work(int *quit);
int mobile_delete(struct osmocom_ms *ms, int force);
struct osmocom_ms *mobile_new(char *name);
int mobile_init(struct osmocom_ms *ms);
int mobile_exit(struct osmocom_ms *ms, int force);
int mobile_work(struct osmocom_ms *ms);

#endif

