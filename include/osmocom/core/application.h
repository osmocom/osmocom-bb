#ifndef OSMO_APPLICATION_H
#define OSMO_APPLICATION_H

/**
 * Routines for helping with the application setup.
 */

struct log_info;
struct log_target;

extern struct log_target *osmo_stderr_target;

void osmo_init_ignore_signals(void);
int osmo_init_logging(const struct log_info *);

#endif
