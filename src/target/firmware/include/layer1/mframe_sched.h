
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

	/* Test task: send Normal Burst in all timeslots */
	MF_TASK_UL_ALL_NB,
};

enum mf_sched_item_flag {
	MF_F_SACCH	= (1 << 0),
};

uint8_t mframe_task2chan_nr(enum mframe_task mft, uint8_t ts);

/* Schedule mframe_sched_items according to current MF TASK list */
void mframe_schedule(uint32_t task_bitmask);
