#ifndef OSMO_APPLICATION_H
#define OSMO_APPLICATION_H

/*!
 * \file application.h
 * \brief Routines for helping with the osmocom application setup.
 */

/*! \brief information containing the available logging subsystems */
struct log_info;

/*! \brief one instance of a logging target (file, stderr, ...) */
struct log_target;

/*! \brief the default logging target, logging to stderr */
extern struct log_target *osmo_stderr_target;

void osmo_init_ignore_signals(void);
int osmo_init_logging(const struct log_info *);

int osmo_daemonize(void);

#endif
