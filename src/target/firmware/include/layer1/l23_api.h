#ifndef _L1_L23_API_H
#define _L1_L23_API_H

#include <stdint.h>
#include <osmocore/msgb.h>
#include <l1a_l23_interface.h>

void l1a_l23api_init(void);
void l1_queue_for_l2(struct msgb *msg);
struct msgb *l1ctl_msgb_alloc(uint8_t msg_type);
struct msgb *l1_create_l2_msg(int msg_type, uint32_t fn, uint16_t snr, uint16_t arfcn);

#endif /* _L1_L23_API_H */
