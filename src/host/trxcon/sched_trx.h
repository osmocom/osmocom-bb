#pragma once

#include <stdint.h>
#include <stdbool.h>

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

#define GPRS_L2_MAX_LEN		54
#define EDGE_L2_MAX_LEN		155

#define TRX_CH_LID_DEDIC	0x00
#define TRX_CH_LID_SACCH	0x40

/* Is a channel related to PDCH (GPRS) */
#define TRX_CH_FLAG_PDCH	(1 << 0)
/* Should a channel be activated automatically */
#define TRX_CH_FLAG_AUTO	(1 << 1)
/* Is continuous burst transmission assumed */
#define TRX_CH_FLAG_CBTX	(1 << 2)

#define MAX_A5_KEY_LEN		(128 / 8)
#define TRX_TS_COUNT		8

/* Forward declaration to avoid mutual include */
struct trx_lchan_state;
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
	TRXC_SDCCH4_CBCH,
	TRXC_SDCCH8_CBCH,
	_TRX_CHAN_MAX
};

typedef int trx_lchan_rx_func(struct trx_instance *trx,
	struct trx_ts *ts, struct trx_lchan_state *lchan,
	uint32_t fn, uint8_t bid, sbit_t *bits,
	int8_t rssi, int16_t toa256);

typedef int trx_lchan_tx_func(struct trx_instance *trx,
	struct trx_ts *ts, struct trx_lchan_state *lchan,
	uint32_t fn, uint8_t bid);

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
	/*! \brief Link to a list of channels */
	struct llist_head list;

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

	/*! \brief A primitive being sent */
	struct trx_ts_prim *prim;

	/*! \brief Mode for TCH channels (see GSM48_CMODE_*) */
	uint8_t	tch_mode;

	/*! \brief FACCH/H on downlink */
	bool dl_ongoing_facch;
	/*! \brief pending FACCH/H blocks on Uplink */
	uint8_t ul_facch_blocks;

	struct {
		/*! \brief Number of measurements */
		unsigned int num;
		/*! \brief Sum of RSSI values */
		float rssi_sum;
		/*! \brief Sum of TOA values */
		int32_t toa256_sum;
	} meas;

	/*! \brief SACCH state */
	struct {
		/*! \brief Cached measurement report (last received) */
		uint8_t mr_cache[GSM_MACBLOCK_LEN];
		/*! \brief Cache usage counter */
		uint8_t mr_cache_usage;
		/*! \brief Was a MR transmitted last time? */
		bool mr_tx_last;
	} sacch;

	/* AMR specific */
	struct {
		/*! \brief 4 possible codecs for AMR */
		uint8_t codec[4];
		/*! \brief Number of possible codecs */
		uint8_t codecs;
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
		/*! \brief Number of bit error rates */
		uint8_t ber_num;
		/*! \brief Sum of bit error rates */
		float ber_sum;
	} amr;

	/*! \brief A5/X encryption state */
	struct {
		uint8_t key[MAX_A5_KEY_LEN];
		uint8_t key_len;
		uint8_t algo;
	} a5;
};

struct trx_ts {
	/*! \brief Timeslot index within a frame (0..7) */
	uint8_t index;
	/*! \brief Last received frame number */
	uint32_t mf_last_fn;

	/*! \brief Pointer to multiframe layout */
	const struct trx_multiframe *mf_layout;
	/*! \brief Channel states for logical channels */
	struct llist_head lchans;
	/*! \brief Queue primitives for TX */
	struct llist_head tx_prims;
};

/* Represents one TX primitive in the queue of trx_ts */
struct trx_ts_prim {
	/*! \brief Link to queue of TS */
	struct llist_head list;
	/*! \brief Logical channel type */
	enum trx_lchan_type chan;
	/*! \brief Payload length */
	size_t payload_len;
	/*! \brief Payload */
	uint8_t payload[0];
};

extern const struct trx_lchan_desc trx_lchan_desc[_TRX_CHAN_MAX];
const struct trx_multiframe *sched_mframe_layout(
	enum gsm_phys_chan_config config, int tn);

/* Scheduler management functions */
int sched_trx_init(struct trx_instance *trx, uint32_t fn_advance);
int sched_trx_reset(struct trx_instance *trx, bool reset_clock);
int sched_trx_shutdown(struct trx_instance *trx);

/* Timeslot management functions */
struct trx_ts *sched_trx_add_ts(struct trx_instance *trx, int tn);
void sched_trx_del_ts(struct trx_instance *trx, int tn);
int sched_trx_reset_ts(struct trx_instance *trx, int tn);
int sched_trx_configure_ts(struct trx_instance *trx, int tn,
	enum gsm_phys_chan_config config);
int sched_trx_start_ciphering(struct trx_ts *ts, uint8_t algo,
	uint8_t *key, uint8_t key_len);

/* Logical channel management functions */
enum gsm_phys_chan_config sched_trx_chan_nr2pchan_config(uint8_t chan_nr);
enum trx_lchan_type sched_trx_chan_nr2lchan_type(uint8_t chan_nr,
	uint8_t link_id);

void sched_trx_deactivate_all_lchans(struct trx_ts *ts);
int sched_trx_set_lchans(struct trx_ts *ts, uint8_t chan_nr, int active, uint8_t tch_mode);
int sched_trx_activate_lchan(struct trx_ts *ts, enum trx_lchan_type chan);
int sched_trx_deactivate_lchan(struct trx_ts *ts, enum trx_lchan_type chan);
struct trx_lchan_state *sched_trx_find_lchan(struct trx_ts *ts,
	enum trx_lchan_type chan);

/* Primitive management functions */
int sched_prim_init(void *ctx, struct trx_ts_prim **prim,
	size_t pl_len, uint8_t chan_nr, uint8_t link_id);
int sched_prim_push(struct trx_instance *trx,
	struct trx_ts_prim *prim, uint8_t chan_nr);

#define TCH_MODE_IS_SPEECH(mode)       \
	  (mode == GSM48_CMODE_SPEECH_V1   \
	|| mode == GSM48_CMODE_SPEECH_EFR  \
	|| mode == GSM48_CMODE_SPEECH_AMR)

#define TCH_MODE_IS_DATA(mode)        \
	  (mode == GSM48_CMODE_DATA_14k5  \
	|| mode == GSM48_CMODE_DATA_12k0  \
	|| mode == GSM48_CMODE_DATA_6k0   \
	|| mode == GSM48_CMODE_DATA_3k6)

#define CHAN_IS_TCH(chan) \
	(chan == TRXC_TCHF || chan == TRXC_TCHH_0 || chan == TRXC_TCHH_1)

#define CHAN_IS_SACCH(chan) \
	(trx_lchan_desc[chan].link_id & TRX_CH_LID_SACCH)

#define PRIM_IS_TCH(prim) \
	(CHAN_IS_TCH(prim->chan) && prim->payload_len != GSM_MACBLOCK_LEN)

#define PRIM_IS_FACCH(prim) \
	(CHAN_IS_TCH(prim->chan) && prim->payload_len == GSM_MACBLOCK_LEN)

struct trx_ts_prim *sched_prim_dequeue(struct llist_head *queue,
	uint32_t fn, struct trx_lchan_state *lchan);
int sched_prim_dummy(struct trx_lchan_state *lchan);
void sched_prim_drop(struct trx_lchan_state *lchan);
void sched_prim_flush_queue(struct llist_head *list);

int sched_trx_handle_rx_burst(struct trx_instance *trx, uint8_t tn,
	uint32_t burst_fn, sbit_t *bits, uint16_t nbits,
	int8_t rssi, int16_t toa256);
int sched_trx_handle_tx_burst(struct trx_instance *trx,
	struct trx_ts *ts, struct trx_lchan_state *lchan,
	uint32_t fn, ubit_t *bits);

/* Shared declarations for lchan handlers */
extern const uint8_t sched_nb_training_bits[8][26];

size_t sched_bad_frame_ind(uint8_t *l2, struct trx_lchan_state *lchan);
int sched_send_dt_ind(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint8_t *l2, size_t l2_len,
	int bit_error_count, bool dec_failed, bool traffic);
int sched_send_dt_conf(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint32_t fn, bool traffic);

/* Interleaved TCH/H block TDMA frame mapping */
uint32_t sched_tchh_block_dl_first_fn(enum trx_lchan_type chan,
	uint32_t last_fn, bool facch);
bool sched_tchh_block_map_fn(enum trx_lchan_type chan,
	uint32_t fn, bool ul, bool facch, bool start);

#define sched_tchh_traffic_start(chan, fn, ul) \
	sched_tchh_block_map_fn(chan, fn, ul, 0, 1)
#define sched_tchh_traffic_end(chan, fn, ul) \
	sched_tchh_block_map_fn(chan, fn, ul, 0, 0)

#define sched_tchh_facch_start(chan, fn, ul) \
	sched_tchh_block_map_fn(chan, fn, ul, 1, 1)
#define sched_tchh_facch_end(chan, fn, ul) \
	sched_tchh_block_map_fn(chan, fn, ul, 1, 0)
