#ifndef PROTO_GSM_04_12_H
#define PROTO_GSM_04_12_H

#include <stdint.h>

/* GSM TS 04.12 definitions for Short Message Service Cell Broadcast */

#define GSM412_SEQ_FST_BLOCK		0x0
#define GSM412_SEQ_SND_BLOCK		0x1
#define GSM412_SEQ_TRD_BLOCK		0x2
#define GSM412_SEQ_FTH_BLOCK		0x3
#define GSM412_SEQ_FST_SCHED_BLOCK	0x8
#define GSM412_SEQ_NULL_MSG		0xf

struct gsm412_block_type {
	uint8_t	seq_nr : 4,
		lb : 1,
		lpd : 2,
		spare : 1;
} __attribute__((packed));

struct gsm412_sched_msg {
	uint8_t beg_slot_nr : 6,
		type : 2;
	uint8_t end_slot_nr : 6,
		spare1 : 1, spare2: 1;
	uint8_t cbsms_msg_map[6];
	uint8_t data[0];
} __attribute__((packed));

#endif
