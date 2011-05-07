#ifndef OSMO_SIGNAL_H
#define OSMO_SIGNAL_H

typedef int osmo_signal_cbfn(unsigned int subsys, unsigned int signal, void *handler_data, void *signal_data);


/* Management */
int osmo_signal_register_handler(unsigned int subsys, osmo_signal_cbfn *cbfn, void *data);
void osmo_signal_unregister_handler(unsigned int subsys, osmo_signal_cbfn *cbfn, void *data);

/* Dispatch */
void osmo_signal_dispatch(unsigned int subsys, unsigned int signal, void *signal_data);

#endif /* OSMO_SIGNAL_H */
