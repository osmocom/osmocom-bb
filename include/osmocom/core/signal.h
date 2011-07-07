#ifndef OSMO_SIGNAL_H
#define OSMO_SIGNAL_H

#include <stdint.h>

/* subsystem signaling numbers: we split the numberspace for applications and
 * libraries: from 0 to UINT_MAX/2 for applications, from UINT_MAX/2 to
 * UINT_MAX for libraries. */
#define OSMO_SIGNAL_SS_APPS		0
#define OSMO_SIGNAL_SS_RESERVED		2147483648

/* signal subsystems. */
enum {
	SS_GLOBAL		= OSMO_SIGNAL_SS_RESERVED,
	SS_INPUT,
};

/* application-defined signal types. */
#define OSMO_SIGNAL_T_APPS		0
#define OSMO_SIGNAL_T_RESERVED		2147483648

/* signal types. */
enum {
	S_GLOBAL_SHUTDOWN	= OSMO_SIGNAL_T_RESERVED,
};

typedef int osmo_signal_cbfn(unsigned int subsys, unsigned int signal, void *handler_data, void *signal_data);


/* Management */
int osmo_signal_register_handler(unsigned int subsys, osmo_signal_cbfn *cbfn, void *data);
void osmo_signal_unregister_handler(unsigned int subsys, osmo_signal_cbfn *cbfn, void *data);

/* Dispatch */
void osmo_signal_dispatch(unsigned int subsys, unsigned int signal, void *signal_data);

#endif /* OSMO_SIGNAL_H */
