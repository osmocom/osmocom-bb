/*
 * (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */


/* 9.2 commands */
#define GSM1111_CLASS_GSM		0xa0
#define	GSM1111_INST_SELECT		0xa4
#define	GSM1111_INST_STATUS		0xf2
#define	GSM1111_INST_READ_BINARY	0xb0
#define	GSM1111_INST_UPDATE_BINARY	0xd6
#define	GSM1111_INST_READ_RECORD	0xb2
#define	GSM1111_INST_UPDATE_RECORD	0xdc
#define	GSM1111_INST_SEEK		0xa2
#define	GSM1111_INST_INCREASE		0x32
#define	GSM1111_INST_VERIFY_CHV		0x20
#define	GSM1111_INST_CHANGE_CHV		0x24
#define	GSM1111_INST_DISABLE_CHV	0x26
#define	GSM1111_INST_ENABLE_CHV		0x28
#define	GSM1111_INST_UNBLOCK_CHV	0x2c
#define	GSM1111_INST_INVALIDATE		0x04
#define	GSM1111_INST_REHABLILITATE	0x44
#define	GSM1111_INST_RUN_GSM_ALGO	0x88
#define	GSM1111_INST_SLEEP		0xfa
#define	GSM1111_INST_GET_RESPONSE	0xc0
#define	GSM1111_INST_TERMINAL_PROFILE	0x10
#define	GSM1111_INST_ENVELOPE		0xc2
#define	GSM1111_INST_FETCH		0x12
#define	GSM1111_INST_TERMINAL_RESPONSE	0x14

/* 9.3 access conditions */
#define GSM1111_ACC_ALWAYS		0x0
#define GSM1111_ACC_CHV1		0x1
#define GSM1111_ACC_CHV2		0x2
#define GSM1111_ACC_RFU			0x3
#define GSM1111_ACC_NEW			0xf
/* others are ADM */

/* 9.3 type of file */
#define GSM1111_TOF_RFU			0x00
#define GSM1111_TOF_MF			0x01
#define GSM1111_TOF_DF			0x02
#define GSM1111_TOF_EF			0x04

/* 9.3 struct of file */
#define GSM1111_SOF_TRANSPARENT		0x00
#define GSM1111_SOF_LINEAR		0x01
#define GSM1111_SOF_CYCLIC		0x03

/* 9.4 status */
#define GSM1111_STAT_NORMAL		0x90
#define GSM1111_STAT_PROACTIVE		0x91
#define GSM1111_STAT_DL_ERROR		0x9e
#define GSM1111_STAT_RESPONSE		0x9f
#define GSM1111_STAT_RESPONSE_TOO	0x61
#define GSM1111_STAT_APP_TK_BUSY	0x93
#define GSM1111_STAT_MEM_PROBLEM	0x92
#define GSM1111_STAT_REFERENCING	0x94
#define GSM1111_STAT_SECURITY		0x98
#define GSM1111_STAT_INCORR_P3		0x67
#define GSM1111_STAT_INCORR_P1_P2	0x6b
#define GSM1111_STAT_UKN_INST		0x6d
#define GSM1111_STAT_WRONG_CLASS	0x6e
#define GSM1111_STAT_TECH_PROBLEM	0x6f

/* 9.4.4 Referencing management SW2 */
#define GSM1111_REF_NO_EF		0x00
#define GSM1111_REF_OUT_OF_RANGE	0x02
#define GSM1111_REF_FILE_NOT_FOUND	0x04
#define GSM1111_REF_FILE_INCONSI	0x08

/* 9.4.5 Security management SW2 */
#define GSM1111_SEC_NO_CHV		0x02
#define GSM1111_SEC_NO_ACCESS		0x04
#define GSM1111_SEC_CONTRA_CHV		0x08
#define GSM1111_SEC_CONTRA_INVAL	0x10
#define GSM1111_SEC_BLOCKED		0x40
#define GSM1111_SEC_MAX_VALUE		0x50

/* messages from application to sim client */
enum {
	/* requests */
	SIM_JOB_READ_BINARY,
	SIM_JOB_UPDATE_BINARY,
	SIM_JOB_READ_RECORD,
	SIM_JOB_UPDATE_RECORD,
	SIM_JOB_SEEK_RECORD,
	SIM_JOB_INCREASE,
	SIM_JOB_INVALIDATE,
	SIM_JOB_REHABILITATE,
	SIM_JOB_RUN_GSM_ALGO,
	SIM_JOB_PIN1_UNLOCK,
	SIM_JOB_PIN1_CHANGE,
	SIM_JOB_PIN1_DISABLE,
	SIM_JOB_PIN1_ENABLE,
	SIM_JOB_PIN1_UNBLOCK,
	SIM_JOB_PIN2_UNLOCK,
	SIM_JOB_PIN2_CHANGE,
	SIM_JOB_PIN2_UNBLOCK,

	/* results */
	SIM_JOB_OK,
	SIM_JOB_ERROR,
};

/* messages from sim client to application */
#define SIM_JOB_OK		0
#define SIM_JOB_ERROR		1

/* error causes */
#define SIM_CAUSE_NO_SIM	0	/* no SIM present, if detectable */
#define SIM_CAUSE_SIM_ERROR	1	/* any error while reading SIM */
#define SIM_CAUSE_REQUEST_ERROR	2	/* error in request */
#define SIM_CAUSE_PIN1_REQUIRED	3	/* CHV1 is required for access */
#define SIM_CAUSE_PIN2_REQUIRED	4	/* CHV2 is required for access */
#define SIM_CAUSE_PIN1_BLOCKED	5	/* CHV1 was entered too many times */
#define SIM_CAUSE_PIN2_BLOCKED	6	/* CHV2 was entered too many times */
#define SIM_CAUSE_PUC_BLOCKED	7	/* unblock entered too many times */

/* job states */
enum {
	SIM_JST_IDLE = 0,
	SIM_JST_SELECT_MFDF,		/* SELECT sent */
	SIM_JST_SELECT_MFDF_RESP,	/* GET RESPONSE sent */
	SIM_JST_SELECT_EF,		/* SELECT sent */
	SIM_JST_SELECT_EF_RESP,		/* GET RESPONSE sent */
	SIM_JST_WAIT_FILE,		/* file command sent */
	SIM_JST_RUN_GSM_ALGO,		/* wait for algorithm to process */
	SIM_JST_RUN_GSM_ALGO_RESP,	/* wait for response */
	SIM_JST_PIN1_UNLOCK,
	SIM_JST_PIN1_CHANGE,
	SIM_JST_PIN1_DISABLE,
	SIM_JST_PIN1_ENABLE,
	SIM_JST_PIN1_UNBLOCK,
	SIM_JST_PIN2_UNLOCK,
	SIM_JST_PIN2_CHANGE,
	SIM_JST_PIN2_UNBLOCK,
};

#define MAX_SIM_PATH_LENGTH	6 + 1 /* one for the termination */

struct gsm_sim_handler {
	struct llist_head	entry;

	uint32_t		handle;
	void			(*cb)(struct osmocom_ms *ms, struct msgb *msg);
};

struct gsm_sim {
	struct llist_head	handlers; /* gsm_sim_handler */
	struct llist_head	jobs; /* messages */
	uint16_t path[MAX_SIM_PATH_LENGTH];
	uint16_t file;

	struct msgb		*job_msg;
	uint32_t		job_handle;
	int			job_state;

	uint8_t			reset;
	uint8_t			chv1_remain, chv2_remain;
	uint8_t			unblk1_remain, unblk2_remain;
};

struct sim_hdr {
	int handle;
	uint8_t job_type;
	uint16_t path[MAX_SIM_PATH_LENGTH];
	uint16_t file;
	uint8_t rec_no, rec_mode; /* in case of record */
	uint8_t seek_type_mode; /* in case of seek command */
};

#define SIM_ALLOC_SIZE		512
#define SIM_ALLOC_HEADROOM	64

struct msgb *gsm_sim_msgb_alloc(uint32_t handle, uint8_t job_type);
uint32_t sim_open(struct osmocom_ms *ms,
	void (*cb)(struct osmocom_ms *ms, struct msgb *msg));
void sim_close(struct osmocom_ms *ms, uint32_t handle);
void sim_job(struct osmocom_ms *ms, struct msgb *msg);

/* Section 9.2.1 (response to selecting DF or MF) */
struct gsm1111_response_mfdf {
	uint16_t rfu1;
	uint16_t free_mem;
	uint16_t file_id;
	uint8_t tof;
	uint8_t	rfu2[5];
	uint8_t length;
	uint8_t gsm_data[0];
} __attribute__ ((packed));

struct gsm1111_response_mfdf_gsm {
	uint8_t file_char;
	uint8_t num_df;
	uint8_t num_ef;
	uint8_t num_codes;
	uint8_t rfu1;
	uint8_t chv1_remain:4,
		 rfu2:3,
		 chv1_init:1;
	uint8_t unblk1_remain:4,
		 rfu3:3,
		 unblk1_init:1;
	uint8_t chv2_remain:4,
		 rfu4:3,
		 chv2_init:1;
	uint8_t unblk2_remain:4,
		 rfu5:3,
		 unblk2_init:1;
	uint8_t more_data[0];
} __attribute__ ((packed));

/* Section 9.2.1 (response to selecting EF) */
struct gsm1111_response_ef {
	uint16_t rfu1;
	uint16_t file_size;
	uint16_t file_id;
	uint8_t tof;
	uint8_t inc_allowed;
	uint8_t	acc_update:4,
		 acc_read:4;
	uint8_t	rfu2:4,
		 acc_inc:4;
	uint8_t	acc_inval:4,
		 acc_reha:4;
	uint8_t not_inval:1,
		 rfu3:1,
		 ru_inval:1,
		 rfu4:5;
	uint8_t length;
	uint8_t structure;
} __attribute__ ((packed));

/* Section 10.3.17 */
struct gsm1111_ef_loci {
	uint32_t tmsi;
	struct gsm48_loc_area_id lai;
	uint8_t tmsi_time;
	uint8_t lupd_status;
} __attribute__ ((packed));

/* Section 10.5.1 */
struct gsm1111_ef_adn {
	uint8_t len_bcd;
	uint8_t ton_npi;
	uint8_t number[10];
	uint8_t capa_conf;
	uint8_t ext_id;
} __attribute__ ((packed));

/* Section 10.5.6 */
struct gsm1111_ef_smsp {
	uint8_t par_ind;
	uint8_t tp_da[12];
	uint8_t ts_sca[12];
	uint8_t tp_proto;
	uint8_t tp_dcs;
	uint8_t tp_vp;
} __attribute__ ((packed));

int sim_apdu_resp(struct osmocom_ms *ms, struct msgb *msg);
int gsm_sim_init(struct osmocom_ms *ms);
int gsm_sim_exit(struct osmocom_ms *ms);
int gsm_sim_job_dequeue(struct osmocom_ms *ms);


