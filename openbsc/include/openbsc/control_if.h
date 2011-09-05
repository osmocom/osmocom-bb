#ifndef _CONTROL_IF_H
#define _CONTROL_IF_H

#include <osmocom/core/write_queue.h>
#include <openbsc/control_cmd.h>
#include <openbsc/gsm_data.h>

int ctrl_cmd_send(struct osmo_wqueue *queue, struct ctrl_cmd *cmd);
int ctrl_cmd_handle(struct ctrl_cmd *cmd, void *data);
int controlif_setup(struct gsm_network *gsmnet, uint16_t port);

#endif /* _CONTROL_IF_H */

