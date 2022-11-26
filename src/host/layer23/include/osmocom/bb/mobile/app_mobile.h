#ifndef APP_MOBILE_H
#define APP_MOBILE_H

#include <stdbool.h>

extern char *config_dir;

struct osmocom_ms;
struct vty;

int l23_app_init(const char *config_file);
int l23_app_exit(void);
int l23_app_work(int *quit);
int mobile_delete(struct osmocom_ms *ms, int force);
struct osmocom_ms *mobile_new(char *name);
int mobile_work(struct osmocom_ms *ms);
int mobile_start(struct osmocom_ms *ms, char **other_name);
int mobile_stop(struct osmocom_ms *ms, int force);

void mobile_set_started(struct osmocom_ms *ms, bool state);
void mobile_set_shutdown(struct osmocom_ms *ms, int state);

int script_lua_load(struct vty *vty, struct osmocom_ms *ms, const char *filename);
int script_lua_close(struct osmocom_ms *ms);


/* Internal code. Don't call directly */
int mobile_exit(struct osmocom_ms *ms, int force);
#endif

