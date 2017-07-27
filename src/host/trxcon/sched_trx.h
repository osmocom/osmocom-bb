#pragma once

#include <stdint.h>

#include <osmocom/core/bits.h>
#include <osmocom/core/utils.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/core/linuxlist.h>

#include "logging.h"
#include "scheduler.h"

#define GSM_BURST_LEN		148
#define GSM_BURST_PL_LEN	116

#define GPRS_BURST_LEN		GSM_BURST_LEN
#define EDGE_BURST_LEN		444

#define TRX_CH_FLAG_PDCH	(1 << 0)
#define TRX_CH_FLAG_AUTO	(1 << 1)
#define TRX_TS_COUNT		8

#define MAX_A5_KEY_LEN		(128 / 8)

/* Forward declaration to avoid mutual include */
struct trx_instance;
struct trx_ts;

enum trx_burst_type {
	TRX_BURST_GMSK,
	TRX_BURST_8PSK,
};

/**
 * These types define the different channels on a multiframe.
 * Each channel has queues and can be activated individually.
 */
enum trx_lchan_type {
	TRXC_IDLE = 0,
	TRXC_FCCH,
	TRXC_SCH,
	TRXC_BCCH,
	TRXC_RACH,
	TRXC_CCCH,
	TRXC_TCHF,
	TRXC_TCHH_0,
	TRXC_TCHH_1,
	TRXC_SDCCH4_0,
	TRXC_SDCCH4_1,
	TRXC_SDCCH4_2,
	TRXC_SDCCH4_3,
	TRXC_SDCCH8_0,
	TRXC_SDCCH8_1,
	TRXC_SDCCH8_2,
	TRXC_SDCCH8_3,
	TRXC_SDCCH8_4,
	TRXC_SDCCH8_5,
	TRXC_SDCCH8_6,
	TRXC_SDCCH8_7,
	TRXC_SACCHTF,
	TRXC_SACCHTH_0,
	TRXC_SACCHTH_1,
	TRXC_SACCH4_0,
	TRXC_SACCH4_1,
	TRXC_SACCH4_2,
	TRXC_SACCH4_3,
	TRXC_SACCH8_0,
	TRXC_SACCH8_1,
	TRXC_SACCH8_2,
	TRXC_SACCH8_3,
	TRXC_SACCH8_4,
	TRXC_SACCH8_5,
	TRXC_SACCH8_6,
	TRXC_SACCH8_7,
	TRXC_PDTCH,
	TRXC_PTCCH,
	_TRX_CHAN_MAX
};

typedef int trx_lchan_rx_func(struct trx_instance *trx,
	struct trx_ts *ts,
	uint32_t fn, enum trx_lchan_type chan,
	uint8_t bid, sbit_t *bits, uint16_t nbits,
	int8_t rssi, float toa);

typedef int trx_lchan_tx_func(struct trx_instance *trx,
	struct trx_ts *ts,
	uint32_t fn, enum trx_lchan_type chan,
	uint8_t bid, uint16_t *nbits);

struct trx_lchan_desc {
	/*! \brief TRX Channel Type */
	enum trx_lchan_type chan;
	/*! \brief Human-readable name */
	const char *name;
	/*! \brief Channel Number (like in RSL) */
	uint8_t chan_nr;
	/*! \brief Link ID (like in RSL) */
	uint8_t link_id;

	/*! \brief How much memory do we need to store bursts */
	size_t burst_buf_size;
	/*! \brief Channel specific flags */
	uint8_t flags;

	/*! \brief Function to call when burst received from PHY */
	trx_lchan_rx_func *rx_fn;
	/*! \brief Function to call when data received from L2 */
	trx_lchan_tx_func *tx_fn;
};

struct trx_frame {
	/*! \brief Downlink TRX channel type */
	enum trx_lchan_type dl_chan;
	/*! \brief Downlink block ID */
	uint8_t dl_bid;
	/*! \brief Uplink TRX channel type */
	enum trx_lchan_type ul_chan;
	/*! \brief Uplink block ID */
	uint8_t ul_bid;
};

struct trx_multiframe {
	/*! \brief Channel combination */
	enum gsm_phys_chan_config chan_config;
	/*! \brief Human-readable name */
	const char *name;
	/*! \brief Repeats how many frames */
	uint8_t period;
	/*! \brief Applies to which timeslots */
	uint8_t slotmask;
	/*! \brief Contains which lchans */
	uint64_t lchan_mask;
	/*! \brief Pointer to scheduling structure */
	const struct trx_frame *frames;
};

/* States each channel on a multiframe */
struct trx_lchan_state {
	/*! \brief Channel type */
	enum trx_lchan_type type;
	/*! \brief Channel status */
	uint8_t active;

	/*! \brief Burst type: GMSK or 8PSK */
	enum trx_burst_type burst_type;
	/*! \brief Frame number of first burst */
	uint32_t rx_first_fn;
	/*! \brief Mask of received bursts */
	uint8_t rx_burst_mask;
	/*! \brief Mask of transmitted bursts */
	uint8_t tx_burst_mask;
	/*! \brief Burst buffer for RX */
	sbit_t *rx_bursts;
	/*! \brief Burst buffer for TX */
	ubit_t *tx_bursts;

	/*! \brief Number of RSSI values */
	uint8_t rssi_num;
	/*! \brief Sum of RSSI values */
	float rssi_sum;
	/*! \brief Number of TOA values */
	uint8_t toa_num;
	/*! \brief Sum of TOA values */
	float toa_sum;
	/*! \brief (SACCH) loss detection */
	uint8_t lost;
	/*! \brief Mode for TCH channels */
	uint8_t	rsl_cmode, tch_mode;

	/* AMR specific */
	/*! \brief 4 possible codecs for AMR */
	uint8_t codec[4];
	/*! \brief Number of possible codecs */
	int codecs;
	/*! \brief Sum of bit error rates */
	float ber_sum;
	/*! \brief Number of bit error rates */
	int ber_num;
	/*! \brief Current uplink FT index */
	uint8_t ul_ft;
	/*! \brief Current downlink FT index */
	uint8_t dl_ft;
	/*! \brief Current uplink CMR index */
	uint8_t ul_cmr;
	/*! \brief Current downlink CMR index */
	uint8_t dl_cmr;
	/*! \brief If AMR loop is enabled */
	uint8_t amr_loop;

	/* TCH/H */
	uint8_t dl_ongoing_facch; /*! \brief FACCH/H on downlink */
	uint8_t ul_ongoing_facch; /*! \brief FACCH/H on uplink */

	/*! \brief A5/x encryption algorithm */
	int encr_algo;
	int encr_key_len;
	uint8_t encr_key[MAX_A5_KEY_LEN];

	/*! \brief Measurements */
	struct {
		/*! \brief Cyclic clock counter */
		uint8_t clock;
		/*! \brief Last RSSI values */
		int8_t rssi[32];
		/*! \brief Received RSSI values */
		int rssi_count;
		/*! \brief Number of stored value */
		int rssi_valid_count;
		/*! \brief Any burst received so far */
		int rssi_got_burst;
		/*! \brief Sum of TOA values */
		float toa_sum;
		/*! \brief Number of TOA value */
		int toa_num;
	} meas;
};

struct trx_ts {
	/*! \brief Timeslot index within a frame (0..7) */
	uint8_t index;
	/*! \brief Last received frame number */
	uint32_t mf_last_fn;

	/*! \brief Pointer to multiframe layout */
	const struct trx_multiframe *mf_layout;
	/*! \brief Channel states for logical channels */
	struct trx_lchan_state *lchans;
	/*! \brief Queue primitives for TX */
	struct llist_head tx_prims;
	/*! \brief Link to parent list */
	struct llist_head list;
};

/* Represents one TX primitive in the queue of trx_ts */
struct trx_ts_prim {
	/*! \brief Link to queue of TS */
	struct llist_head list;
	/*! \brief Logical channel type */
	enum trx_lchan_type chan;
	/*! \brief Payload */
	uint8_t payload[0];
};

extern const struct trx_lchan_desc trx_lchan_desc[_TRX_CHAN_MAX];
const struct trx_multiframe *sched_mframe_layout(
	enum gsm_phys_chan_config config, int ts_num);

/* Scheduler management functions */
int sched_trx_init(struct trx_instance *trx);
int sched_trx_reset(struct trx_instance *trx, int reset_clock);
int sched_trx_shutdown(struct trx_instance *trx);

/* Timeslot management functions */
struct trx_ts *sched_trx_add_ts(struct trx_instance *trx, int ts_num);
struct trx_ts *sched_trx_find_ts(struct trx_instance *trx, int ts_num);
void sched_trx_del_ts(struct trx_instance *trx, int ts_num);
int sched_trx_reset_ts(struct trx_instance *trx, int ts_num);
int sched_trx_configure_ts(struct trx_instance *trx, int ts_num,
	enum gsm_phys_chan_config config);

/* Logical channel management functions */
enum gsm_phys_chan_config sched_trx_chan_nr2pchan_config(uint8_t chan_nr);
enum trx_lchan_type sched_trx_chan_nr2lchan_type(uint8_t chan_nr);
void sched_trx_deactivate_all_lchans(struct trx_ts *ts);
int sched_trx_activate_lchan(struct trx_ts *ts, enum trx_lchan_type chan);
int sched_trx_deactivate_lchan(struct trx_ts *ts, enum trx_lchan_type chan);
struct trx_lchan_state *sched_trx_find_lchan(struct trx_ts *ts,
	enum trx_lchan_type chan);

int sched_trx_handle_rx_burst(struct trx_instance *trx, uint8_t ts_num,
	uint32_t burst_fn, sbit_t *bits, uint16_t nbits, int8_t rssi, float toa);
