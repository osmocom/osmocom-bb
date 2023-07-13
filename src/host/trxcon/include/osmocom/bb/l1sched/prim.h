#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/prim.h>
#include <osmocom/core/utils.h>

#define l1sched_prim_from_msgb(msg) \
	((struct l1sched_prim *)(msg)->l1h)

#define l1sched_prim_data_from_msgb(msg) \
	((uint8_t *)msgb_l2(msg))

#define l1sched_prim_type_from_msgb(msg) \
	l1sched_prim_from_msgb(msg)->oph.primitive

#define L1SCHED_PRIM_STR_FMT "%s.%s"
#define L1SCHED_PRIM_STR_ARGS(prim) \
	l1sched_prim_type_name((prim)->oph.primitive), \
	osmo_prim_operation_name((prim)->oph.operation)

enum l1sched_prim_type {
	L1SCHED_PRIM_T_DATA,			/* Req | Ind | Cnf */
	L1SCHED_PRIM_T_RACH,			/* Req | Cnf */
	L1SCHED_PRIM_T_SCH,			/* Ind */
	L1SCHED_PRIM_T_PCHAN_COMB,		/* Ind */
};

extern const struct value_string l1sched_prim_type_names[];
static inline const char *l1sched_prim_type_name(enum l1sched_prim_type val)
{
	return get_value_string(l1sched_prim_type_names, val);
}

/*! Common header for L1SCHED_PRIM_T_{DATA,RACH} */
struct l1sched_prim_chdr {
	/*! TDMA Frame Number */
	uint32_t frame_nr;
	/*! RSL Channel Number */
	uint8_t chan_nr;
	/*! RSL Link Identifier */
	uint8_t link_id;
	/*! Traffic or signalling */
	bool traffic;
};

/*! Payload of L1SCHED_PRIM_T_DATA | Ind */
struct l1sched_prim_data_ind {
	/*! Common sub-header */
	struct l1sched_prim_chdr chdr;
	int16_t toa256;
	int8_t rssi;
	int n_errors;
	int n_bits_total;
};

/*! Payload of L1SCHED_PRIM_T_RACH | {Req,Cnf} */
struct l1sched_prim_rach {
	/*! Common sub-header */
	struct l1sched_prim_chdr chdr;
	/*! Training Sequence (only for 11-bit RA) */
	uint8_t synch_seq;
	/*! Transmission offset (how many frames to skip) */
	uint8_t offset;
	/*! RA value is 11 bit */
	bool is_11bit;
	/*! RA value */
	uint16_t ra;
};

struct l1sched_prim {
	/*! Primitive header */
	struct osmo_prim_hdr oph;
	/*! Type specific header */
	union {
		/*! L1SCHED_PRIM_T_DATA | Req */
		struct l1sched_prim_chdr data_req;
		/*! L1SCHED_PRIM_T_DATA | Cnf */
		struct l1sched_prim_chdr data_cnf;
		/*! L1SCHED_PRIM_T_DATA | Ind */
		struct l1sched_prim_data_ind data_ind;

		/*! L1SCHED_PRIM_T_RACH | Req */
		struct l1sched_prim_rach rach_req;
		/*! L1SCHED_PRIM_T_RACH | Cnf */
		struct l1sched_prim_rach rach_cnf;

		/*! L1SCHED_PRIM_T_SCH | Ind */
		struct {
			/*! TDMA frame number */
			uint32_t frame_nr;
			/*! BSIC */
			uint8_t bsic;
		} sch_ind;

		/*! L1SCHED_PRIM_T_PCHAN_COMB | Ind */
		struct {
			/*! Timeslot number */
			uint8_t tn;
			/*! Channel combination for a timeslot */
			enum gsm_phys_chan_config pchan;
		} pchan_comb_ind;
	};
};


struct l1sched_state;
struct l1sched_lchan_state;

void l1sched_prim_init(struct msgb *msg,
		       enum l1sched_prim_type type,
		       enum osmo_prim_operation op);

struct msgb *l1sched_prim_alloc(enum l1sched_prim_type type,
				enum osmo_prim_operation op);

bool l1sched_lchan_amr_prim_is_valid(struct l1sched_lchan_state *lchan,
				     struct msgb *msg, bool is_cmr);
struct msgb *l1sched_lchan_prim_dequeue_sacch(struct l1sched_lchan_state *lchan);
struct msgb *l1sched_lchan_prim_dequeue_tch(struct l1sched_lchan_state *lchan, bool facch);
struct msgb *l1sched_lchan_prim_dummy_lapdm(const struct l1sched_lchan_state *lchan);

int l1sched_lchan_emit_data_ind(struct l1sched_lchan_state *lchan,
				const uint8_t *data, size_t data_len,
				int n_errors, int n_bits_total, bool traffic);
int l1sched_lchan_emit_data_cnf(struct l1sched_lchan_state *lchan,
				struct msgb *msg, uint32_t fn);

int l1sched_prim_from_user(struct l1sched_state *sched, struct msgb *msg);
int l1sched_prim_to_user(struct l1sched_state *sched, struct msgb *msg);
