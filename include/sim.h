
#ifndef sim_h
#define sim_h

typedef void (*sim_ready_cb_t)(void);
typedef void (*sim_notready_cb_t)(void);
typedef void (*sim_completion_cb_t)(struct msgb *pdu, void *cookie);

int sim_init(void);
int sim_set_callbacks(sim_ready_cb_t rdy, sim_notready_cb_t nrdy);
int sim_connect(void);
int sim_disconnect(void);
int sim_power_on(void);
int sim_power_off(void);
int sim_reset(void);

int sim_get_atr(sim_completion_cb_t *callback, void *cookie);

int sim_put_apdu(struct msgb *apdu, sim_completion_cb_t *callback, void *cookie);
int sim_get_apdu(struct msgb *apdu, sim_completion_cb_t *callback, void *cookie);

#endif /* !sim_h */
