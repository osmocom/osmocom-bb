#ifndef _L1_MFRAME_SCHED_H
#define _L1_MFRAME_SCHED_H

#include <stdint.h>

enum mframe_task {
	MF_TASK_BCCH_NORM,
	MF_TASK_BCCH_EXT,
	MF_TASK_CCCH,
	MF_TASK_CCCH_COMB,

	MF_TASK_SDCCH4_0,
	MF_TASK_SDCCH4_1,
	MF_TASK_SDCCH4_2,
	MF_TASK_SDCCH4_3,

	MF_TASK_SDCCH8_0,
	MF_TASK_SDCCH8_1,
	MF_TASK_SDCCH8_2,
	MF_TASK_SDCCH8_3,
	MF_TASK_SDCCH8_4,
	MF_TASK_SDCCH8_5,
	MF_TASK_SDCCH8_6,
	MF_TASK_SDCCH8_7,

	MF_TASK_TCH_F_EVEN,
	MF_TASK_TCH_F_ODD,
	MF_TASK_TCH_H_0,
	MF_TASK_TCH_H_1,

	MF_TASK_NEIGH_PM51_C0T0,
	MF_TASK_NEIGH_PM51,
	MF_TASK_NEIGH_PM26E,
	MF_TASK_NEIGH_PM26O,

	/* Test task: send Normal Burst in all timeslots */
	MF_TASK_UL_ALL_NB,
};

enum mf_sched_item_flag {
	MF_F_SACCH	= (1 << 0),
};

/* The scheduler itself */
struct mframe_scheduler {
	uint32_t tasks;
	uint32_t tasks_tgt;
	uint32_t safe_fn;
};

uint8_t mframe_task2chan_nr(enum mframe_task mft, uint8_t ts);

/* Enable a specific task */
void mframe_enable(enum mframe_task task_id);

/* Disable a specific task */
void mframe_disable(enum mframe_task task_id);

/* Replace the current active set by the new one */
void mframe_set(uint32_t tasks);

/* Schedule mframe_sched_items according to current MF TASK list */
void mframe_schedule(void);

/* reset the scheduler, disabling all tasks */
void mframe_reset(void);

#endif /* _MFRAME_SCHED_H */
