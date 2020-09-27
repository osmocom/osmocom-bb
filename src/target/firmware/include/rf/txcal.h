/*
 * RF Tx calibration structures matching those used by official
 * TI/FreeCalypso firmwares; these structures appear in the flash file
 * system (FFS) of FCDEV3B and GTA0x devices and in the factory data
 * block on the Pirelli DP-L10 phone.
 */

#define RF_TX_CHAN_CAL_TABLE_SIZE  4 /*!< channel calibration table size */
#define RF_TX_NUM_SUB_BANDS        8 /*!< number of sub-bands in channel calibration table */
#define RF_TX_LEVELS_TABLE_SIZE   32 /*!< level table size */
#define RF_TX_RAMP_SIZE           16 /*!< number of ramp definitions */

/* APC of Tx Power (pcm-file "rf/tx/level.gsm|dcs") */
struct txcal_tx_level {
	uint16_t apc;		/*!< 0..31 */
	uint8_t ramp_index;	/*!< 0..RF_TX_RAMP_SIZE */
	uint8_t chan_cal_index;	/*!< 0..RF_TX_CHAN_CAL_TABLE_SIZE */
};

/* Power ramp definition structure */
struct txcal_ramp_def {
	uint8_t ramp_up[16];	/*!< Ramp-up profile */
	uint8_t ramp_down[16];	/*!< Ramp-down profile */
};

/* Tx channel calibration structure */
struct txcal_chan_cal {
	uint16_t arfcn_limit;
	int16_t chan_cal;
};

extern struct txcal_tx_level rf_tx_levels_850[RF_TX_LEVELS_TABLE_SIZE];
extern struct txcal_tx_level rf_tx_levels_900[RF_TX_LEVELS_TABLE_SIZE];
extern struct txcal_tx_level rf_tx_levels_1800[RF_TX_LEVELS_TABLE_SIZE];
extern struct txcal_tx_level rf_tx_levels_1900[RF_TX_LEVELS_TABLE_SIZE];

extern struct txcal_ramp_def rf_tx_ramps_850[RF_TX_RAMP_SIZE];
extern struct txcal_ramp_def rf_tx_ramps_900[RF_TX_RAMP_SIZE];
extern struct txcal_ramp_def rf_tx_ramps_1800[RF_TX_RAMP_SIZE];
extern struct txcal_ramp_def rf_tx_ramps_1900[RF_TX_RAMP_SIZE];

extern struct txcal_chan_cal rf_tx_chan_cal_850[RF_TX_CHAN_CAL_TABLE_SIZE]
						[RF_TX_NUM_SUB_BANDS];
extern struct txcal_chan_cal rf_tx_chan_cal_900[RF_TX_CHAN_CAL_TABLE_SIZE]
						[RF_TX_NUM_SUB_BANDS];
extern struct txcal_chan_cal rf_tx_chan_cal_1800[RF_TX_CHAN_CAL_TABLE_SIZE]
						[RF_TX_NUM_SUB_BANDS];
extern struct txcal_chan_cal rf_tx_chan_cal_1900[RF_TX_CHAN_CAL_TABLE_SIZE]
						[RF_TX_NUM_SUB_BANDS];
