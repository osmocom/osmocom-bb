#pragma once
#include <osmocom/core/msgb.h>
#include <virtphy/virt_l1_model.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/gsm/gsm_utils.h>
#include <virtphy/virt_l1_sched.h>

typedef void virt_l1_sched_cb(struct msgb * msg);

/* bucket containing items to be executed for a specific mframe number */
struct virt_l1_sched_mframe_item {
	struct llist_head mframe_item_entry;
	struct llist_head tdma_item_list; /* list of tdma sched items */
	uint32_t fn; /* frame number of execution */
};

/* item to be be executed for a specific tdma timeslot of a framenumber */
struct virt_l1_sched_tdma_item {
	struct llist_head tdma_item_entry;
	struct msgb * msg; /* the msg to be handled */
	uint8_t ts; /* tdma timeslot of execution */
	virt_l1_sched_cb * handler_cb; /* handler callback */
};

void virt_l1_sched_init(struct l1_model_ms * model);
int virt_l1_sched_start(struct gsm_time time);
int virt_l1_sched_restart(struct gsm_time time);
void virt_l1_sched_sync_time(struct gsm_time time, uint8_t hard_reset);
void virt_l1_sched_stop();
void virt_l1_sched_execute(uint32_t fn);
void virt_l1_sched_schedule(struct msgb * msg, uint32_t fn, uint8_t ts,
                            virt_l1_sched_cb * handler_cb);
