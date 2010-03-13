gsm_04_08.h:

/* 10.5.1.5 */
struct gsm48_classmark1 {
	uint8_t pwr_lev:3,
		 a5_1:1,
		 es_ind:1,
		 rev_level:2,
		 spare:1;
} __attribute__ ((packed));

/* 10.5.1.6 */
struct gsm48_classmark2 {
	uint8_t pwr_lev:3,
		 a5_1:1,
		 es_ind:1,
		 rev_level:2,
		 spare:1;
	uint8_t	fc:1,
		 vgcs:1,
		 vbs:1,
		 sm_cap:1,
		 ss_scr:2,
		 ps_cap:1,
		 spare2:1;
	uint8_t	a5_2:1,
		 a5_3:1,
		 cmsp:1,
		 solsa:1,
		 spare3:1,
		 lcsva_cap:1,
		 spare4:1,
		 cm3:1;
} __attribute__ ((packed));

/* 10.5.2.20 */
struct gsm48_meas_res {
	uint8_t	rxlev_full:6,
		 dtx_used:1,
		 ba_used:1;
	uint8_t	rxlev_sub:6,
		 meas_valid:1,
		 spare:1;
	uint8_t	no_nc_n_hi:1,
		 rxqual_sub:3,
		 rxqual_full:3,
		 spare2:1;
	uint8_t	rxlev_nc1:6,
		 no_nc_n_lo:2;
	uint8_t	bsic_nc1_hi:3,
		 bcch_f_nc1:5;
	uint8_t	rxlev_nc2_hi:5,
		 bsic_nc1_lo:3;
	uint8_t	bsic_nc2_hi:2,
		 bcch_f_nc2:5,
		 rxlev_nc2_lo:1;
	uint8_t	rxlev_nc3_hi:4,
		 bsic_nc2_lo:4;
	uint8_t	bsic_nc3_hi:1,
		 bcch_f_nc3:5,
		 rxlev_nc3_lo:2;
	uint8_t	rxlev_nc4_hi:3,
		 bsic_nc3_lo:5;
	uint8_t	bcch_f_nc4:5,
		 rxlev_nc4_lo:3;
	uint8_t	rxlev_nc5_hi:2,
		 bsic_nc4:6;
	uint8_t	bcch_f_nc5_hi:4,
		 rxlev_nc5_lo:4;
	uint8_t	rxlev_nc6_hi:1,
		 bsic_nc5:6,
		 bcch_f_nc5_lo:1;
	uint8_t	bcch_f_nc6_hi:3,
		 rxlev_nc6_lo:5;
	uint8_t	bsic_nc6:6,
		 bcch_f_nc6_lo:2;
} __attribute__((packed));

/* Chapter 9.1.1 */
struct gsm48_add_ass {
	/* Semantic is from 10.5.2.5 */
	struct gsm48_chan_desc chan_desc;
	uint8_t data[0];
} __attribute__((packed));

/* Chapter 9.1.3 */
struct gsm48_ass_cpl {
	uint8_t rr_cause;
} __attribute__((packed));

/* Chapter 9.1.4 */
struct gsm48_ass_fail {
	uint8_t rr_cause;
} __attribute__((packed));

/* Chapter 9.1.7 */
struct gsm48_chan_rel {
	uint8_t rr_cause;
	uint8_t data[0];
} __attribute__((packed));

/* Chapter 9.1.9 */
struct gsm48_cip_mode_cmd {
	uint8_t sc:1,
		 alg_id:3,
		 spare:3,
		 cr:1;
}

/* Chapter 9.1.11 */
struct gsm48_cm_change {
	uint8_t cm2_len;
	struct gsm48_classmark2 cm2;
	uint8_t data[0];
} __attribute__((packed));

/* Chapter 9.1.18 */
struct gsm48_imm_ass {
	uint8_t l2_plen;
	uint8_t proto_discr;
	uint8_t msg_type;
	uint8_t page_mode;
	struct gsm48_chan_desc chan_desc;
	struct gsm48_req_ref req_ref;
	uint8_t timing_advance;
	uint8_t data[0];
} __attribute__ ((packed));

/* Chapter 9.1.19 */
struct gsm48_imm_ass_ext {
	uint8_t l2_plen;
	uint8_t proto_discr;
	uint8_t msg_type;
	uint8_t page_mode;
	struct gsm48_chan_desc chan_desc1;
	struct gsm48_req_ref req_ref1;
	uint8_t timing_advance1;
	struct gsm48_chan_desc chan_desc2;
	struct gsm48_req_ref req_ref2;
	uint8_t timing_advance2;
	uint8_t data[0];
} __attribute__ ((packed));

/* Chapter 9.1.20 */
struct gsm48_imm_ass_rej {
	uint8_t l2_plen;
	uint8_t proto_discr;
	uint8_t msg_type;
	uint8_t page_mode;
	struct gsm48_req_ref req_ref1
	uint8_t wait_ind1
	struct gsm48_req_ref req_ref2
	uint8_t wait_ind2
	struct gsm48_req_ref req_ref3
	uint8_t wait_ind3
	struct gsm48_req_ref req_ref4
	uint8_t wait_ind4
	uint8_t rest[0];
} __attribute__ ((packed));

/* Chapter 9.1.22 */
struct gsm48_rr_paging1 {
	uint8_t l2_plen;
	uint8_t proto_discr;
	uint8_t msg_type;
	uint8_t pag_mode:2,
		 spare:2,
		 cneed1:2,
		 cneed2:2;
	uint8_t data[0];
} __attribute__((packed));

/* Chapter 9.1.23 */
struct gsm48_rr_paging2 {
	uint8_t l2_plen;
	uint8_t proto_discr;
	uint8_t msg_type;
	uint8_t pag_mode:2,
		 spare:2,
		 cneed1:2,
		 cneed2:2;
	uint32_t tmsi1;
	uint32_t tmsi2;
	uint8_t data[0];
} __attribute__((packed));

/* Chapter 9.1.24 */
struct gsm48_rr_paging3 {
	uint8_t l2_plen;
	uint8_t proto_discr;
	uint8_t msg_type;
	uint8_t pag_mode:2,
		 spare:2,
		 cneed1:2,
		 cneed2:2;
	uint32_t tmsi1;
	uint32_t tmsi2;
	uint32_t tmsi3;
	uint32_t tmsi4;
	uint8_t cneed3:2,
		 cneed4:2,
		 spare:4;
	uint8_t rest[0];
} __attribute__((packed));

/* Chapter 9.1.25 */
struct gsm48_rr_pag_rsp {
	uint8_t key_seq:3,
		 spare:5;
	uint8_t cm2_len;
	struct gsm48_classmark2 cm2;
	uint8_t data[0];
}

/* Chapter 9.1.29 */
struct gsm48_rr_status {
	uint8_t rr_cause;
} __attribute__((packed));

rsl.h:

/* encode channel number and frequency as per Section 9.3.1 */
uint8_t rsl_enc_chan_nr(uint8_t type, uint8_t subch, uint8_t timeslot);
int rsl_enc_chan_h0(struct gsm48_chan_desc *cd, uint8_t tsc, uint16_t arfcn);
int rsl_enc_chan_h1(struct gsm48_chan_desc *cd, uint8_t tsc, uint8_t maio, uint8_t hsn);
/* decode channel number and frequency as per Section 9.3.1 */
int rsl_dec_chan_nr(uint8_t chan_nr, uint8_t *type, uint8_t *subch, uint8_t *timeslot);
int rsl_dec_chan_h0(struct gsm48_chan_desc *cd, uint8_t *tsc, uint16_t *arfcn);
int rsl_dec_chan_h1(struct gsm48_chan_desc *cd, uint8_t *tsc, uint8_t *maio, uint8_t *hsn);


gsm_04_08.h:

#define GSM48_IE_CHDES_2_AFTER	0x64
#define GSM48_IE_MODE_SEC_CH	0x66
#define GSM48_IE_MOB_AL_AFTER	0x72
#define GSM48_IE_START_TIME	0x7c
#define GSM48_IE_FRQLIST_BEFORE	0x19
#define GSM48_IE_CHDES_1_BEFORE 0x1c
#define GSM48_IE_CHDES_2_BEFORE	0x1d
#define GSM48_IE_FRQSEQ_BEFORE	0x1e
#define GSM48_IE_MOB_AL_BEFORE	0x21
#define GSM48_IE_CIP_MODE_SET	0x90
#define GSM48_IE_CLASSMARK2	0x20
