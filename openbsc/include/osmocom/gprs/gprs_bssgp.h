#ifndef _GPRS_BSSGP_H
#define _GPRS_BSSGP_H

#include <stdint.h>

#include <osmocom/gsm/gsm48.h>
#include <osmocom/gsm/prim.h>

#include <osmocom/gprs/protocol/gsm_08_18.h>

/* gprs_bssgp_util.c */
extern struct gprs_ns_inst *bssgp_nsi;
struct msgb *bssgp_msgb_alloc(void);
const char *bssgp_cause_str(enum gprs_bssgp_cause cause);
/* Transmit a simple response such as BLOCK/UNBLOCK/RESET ACK/NACK */
int bssgp_tx_simple_bvci(uint8_t pdu_type, uint16_t nsei,
			 uint16_t bvci, uint16_t ns_bvci);
/* Chapter 10.4.14: Status */
int bssgp_tx_status(uint8_t cause, uint16_t *bvci, struct msgb *orig_msg);

enum bssgp_prim {
	PRIM_BSSGP_DL_UD,
	PRIM_BSSGP_UL_UD,
	PRIM_BSSGP_PTM_UD,

	PRIM_BSSGP_GMM_SUSPEND,
	PRIM_BSSGP_GMM_RESUME,
	PRIM_BSSGP_GMM_PAGING,

	PRIM_NM_FLUSH_LL,
	PRIM_NM_LLC_DISCARDED,
	PRIM_NM_BVC_RESET,
	PRIM_NM_BVC_BLOCK,
	PRIM_NM_BVC_UNBLOCK,
};

struct osmo_bssgp_prim {
	struct osmo_prim_hdr oph;

	/* common fields */
	uint16_t nsei;
	uint16_t bvci;
	uint32_t tlli;
	struct tlv_parsed *tp;
	struct gprs_ra_id *ra_id;

	/* specific fields */
	union {
		struct {
			uint8_t *suspend_ref;
		} resume;
	} u;
};

/* gprs_bssgp.c */

#define BVC_S_BLOCKED	0x0001

/* The per-BTS context that we keep on the SGSN side of the BSSGP link */
struct bssgp_bvc_ctx {
	struct llist_head list;

	struct gprs_ra_id ra_id; /*!< parsed RA ID of the remote BTS */
	uint16_t cell_id; /*!< Cell ID of the remote BTS */

	/* NSEI and BVCI of underlying Gb link.  Together they
	 * uniquely identify a link to a BTS (5.4.4) */
	uint16_t bvci;
	uint16_t nsei;

	uint32_t state;

	struct rate_ctr_group *ctrg;

	/* we might want to add this as a shortcut later, avoiding the NSVC
	 * lookup for every packet, similar to a routing cache */
	//struct gprs_nsvc *nsvc;
};
extern struct llist_head bssgp_bvc_ctxts;
/* Find a BTS Context based on parsed RA ID and Cell ID */
struct bssgp_bvc_ctx *btsctx_by_raid_cid(const struct gprs_ra_id *raid, uint16_t cid);
/* Find a BTS context based on BVCI+NSEI tuple */
struct bssgp_bvc_ctx *btsctx_by_bvci_nsei(uint16_t bvci, uint16_t nsei);

#define BVC_F_BLOCKED	0x0001

enum bssgp_ctr {
	BSSGP_CTR_PKTS_IN,
	BSSGP_CTR_PKTS_OUT,
	BSSGP_CTR_BYTES_IN,
	BSSGP_CTR_BYTES_OUT,
	BSSGP_CTR_BLOCKED,
	BSSGP_CTR_DISCARDED,
};


#include <osmocom/gsm/tlv.h>
#include <osmocom/gprs/gprs_msgb.h>

/* BSSGP-UL-UNITDATA.ind */
int bssgp_rcvmsg(struct msgb *msg);

/* BSSGP-DL-UNITDATA.req */
struct bssgp_lv {
	uint16_t len;
	uint8_t *v;
};
/* parameters for BSSGP downlink userdata transmission */
struct bssgp_dl_ud_par {
	uint32_t *tlli;
	char *imsi;
	uint16_t drx_parms;
	/* FIXME: priority */
	struct bssgp_lv ms_ra_cap;
	uint8_t qos_profile[3];
};
int bssgp_tx_dl_ud(struct msgb *msg, uint16_t pdu_lifetime,
		   struct bssgp_dl_ud_par *dup);

uint16_t bssgp_parse_cell_id(struct gprs_ra_id *raid, const uint8_t *buf);
int bssgp_create_cell_id(uint8_t *buf, const struct gprs_ra_id *raid,
			 uint16_t cid);

/* Wrapper around TLV parser to parse BSSGP IEs */
static inline int bssgp_tlv_parse(struct tlv_parsed *tp, uint8_t *buf, int len)
{
	return tlv_parse(tp, &tvlv_att_def, buf, len, 0, 0);
}

/*! \brief BSSGP Paging mode */
enum bssgp_paging_mode {
	BSSGP_PAGING_PS,
	BSSGP_PAGING_CS,
};

/*! \brief BSSGP Paging scope */
enum bssgp_paging_scope {
	BSSGP_PAGING_BSS_AREA,		/*!< all cells in BSS */
	BSSGP_PAGING_LOCATION_AREA,	/*!< all cells in LA */
	BSSGP_PAGING_ROUTEING_AREA,	/*!< all cells in RA */
	BSSGP_PAGING_BVCI,		/*!< one cell */
};

/*! \brief BSSGP paging information */
struct bssgp_paging_info {
	enum bssgp_paging_mode mode;	/*!< CS or PS paging */
	enum bssgp_paging_scope scope;	/*!< bssgp_paging_scope */
	struct gprs_ra_id raid;		/*!< RA Identifier */
	uint16_t bvci;			/*!< BVCI */
	char *imsi;			/*!< IMSI, if any */
	uint32_t *ptmsi;		/*!< P-TMSI, if any */
	uint16_t drx_params;		/*!< DRX parameters */
	uint8_t qos[3];			/*!< QoS parameters */
};

/* Send a single GMM-PAGING.req to a given NSEI/NS-BVCI */
int bssgp_tx_paging(uint16_t nsei, uint16_t ns_bvci,
		    struct bssgp_paging_info *pinfo);

/* gprs_bssgp_vty.c */
int bssgp_vty_init(void);
void bssgp_set_log_ss(int ss);

int bssgp_prim_cb(struct osmo_prim_hdr *oph, void *ctx);

#endif /* _GPRS_BSSGP_H */
