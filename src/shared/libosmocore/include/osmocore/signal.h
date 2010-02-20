#ifndef OSMOCORE_SIGNAL_H
#define OSMOCORE_SIGNAL_H

typedef int signal_cbfn(unsigned int subsys, unsigned int signal,
			void *handler_data, void *signal_data);


/* Management */
int register_signal_handler(unsigned int subsys, signal_cbfn *cbfn, void *data);
void unregister_signal_handler(unsigned int subsys, signal_cbfn *cbfn, void *data);

/* Dispatch */
void dispatch_signal(unsigned int subsys, unsigned int signal, void *signal_data);

#endif /* OSMOCORE_SIGNAL_H */
