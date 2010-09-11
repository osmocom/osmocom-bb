#ifndef _SAP_INTERFACE_H
#define _SAP_INTERFACE_H

typedef int (*osmosap_cb_t)(struct msgb *msg, struct osmocom_ms *ms);

int sap_open(struct osmocom_ms *ms, const char *socket_path);
int sap_close(struct osmocom_ms *ms);
int osmosap_send(struct osmocom_ms *ms, struct msgb *msg);
int osmosap_register_handler(struct osmocom_ms *ms, osmosap_cb_t cb);

#endif /* _SAP_INTERFACE_H */
