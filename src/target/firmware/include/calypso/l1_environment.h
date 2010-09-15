#include <stdint.h>

typedef unsigned short API;
typedef signed short API_SIGNED;

#define FAR

#define CHIPSET		12
#define DSP		36
#define ANLG_FAM	2	/* Iota */

/* MFTAB */
#define L1_MAX_FCT	5	/* Max number of fctions in a frame */
#define MFTAB_SIZE	20

#define NBMAX_CARRIER	174+374	/* Number of carriers (GSM-Ext + DCS */

#define DPAGC_FIFO_LEN	4

#define SIZE_HIST	10

#if !L1_GPRS
# define NBR_DL_L1S_TASKS 32
#else
# define NBR_DL_L1S_TASKS 45
#endif

#define NBR_L1A_PROCESSES	46

#define W_A_DSP_IDLE3	1



// Identifier for all DSP tasks.
// ...RX & TX tasks identifiers.
#define NO_DSP_TASK        0  // No task.
#define NP_DSP_TASK       21  // Normal Paging reading task.
#define EP_DSP_TASK       22  // Extended Paging reading task.
#define NBS_DSP_TASK      19  // Normal BCCH serving reading task.
#define EBS_DSP_TASK      20  // Extended BCCH serving reading task.
#define NBN_DSP_TASK      17  // Normal BCCH neighbour reading task.
#define EBN_DSP_TASK      18  // Extended BCCH neighbour reading task.
#define ALLC_DSP_TASK     24  // CCCH reading task while performing FULL BCCH/CCCH reading task.
#define CB_DSP_TASK       25  // CBCH reading task.
#define DDL_DSP_TASK      26  // SDCCH/D (data) reading task.
#define ADL_DSP_TASK      27  // SDCCH/A (SACCH) reading task.
#define DUL_DSP_TASK      12  // SDCCH/D (data) transmit task.
#define AUL_DSP_TASK      11  // SDCCH/A (SACCH) transmit task.
#define RACH_DSP_TASK     10  // RACH transmit task.
#define TCHT_DSP_TASK     13  // TCH Traffic data DSP task id (RX or TX)
#define TCHA_DSP_TASK     14  // TCH SACCH   data DSP task id (RX or TX)
#define TCHD_DSP_TASK     28  // TCH Traffic data DSP task id (RX or TX)

#define TCH_DTX_UL        15  // Replace UL task in DSP->MCU com. to say "burst not transmitted".

#if (L1_GPRS)
  // Identifier for DSP tasks Packet dedicated.
  // ...RX & TX tasks identifiers.
  //------------------------------------------------------------------------
  // WARNING ... Need to aligned following macro with MCU/DSP GPRS Interface
  //------------------------------------------------------------------------
  #define PNP_DSP_TASK      30
  #define PEP_DSP_TASK      31
  #define PALLC_DSP_TASK    32
  #define PBS_DSP_TASK      33

  #define PTCCH_DSP_TASK    33

#endif

// Identifier for measurement, FB / SB search tasks.
// Values 1,2,3 reserved for "number of measurements".
#define FB_DSP_TASK        5  // Freq. Burst reading task in Idle mode.
#define SB_DSP_TASK        6  // Sync. Burst reading task in Idle mode.
#define TCH_FB_DSP_TASK    8  // Freq. Burst reading task in Dedicated mode.
#define TCH_SB_DSP_TASK    9  // Sync. Burst reading task in Dedicated mode.
#define IDLE1              1

// Debug tasks
#define CHECKSUM_DSP_TASK 33
#define TST_NDB           35  //  Checksum DSP->MCU
#define TST_DB            36  //  DB communication check
#define INIT_VEGA         37
#define DSP_LOOP_C        38

// Identifier for measurement, FB / SB search tasks.
// Values 1,2,3 reserved for "number of measurements".
#define TCH_LOOP_A        31
#define TCH_LOOP_B        32

// bits in d_gsm_bgd_mgt - background task management
#define B_DSPBGD_RECO           1       // start of reco in dsp background
#define B_DSPBGD_UPD            2       // start of alignement update in dsp background
#define B_DSPBGD_STOP_RECO      256     // stop of reco in dsp background
#define B_DSPBGD_STOP_UPD       512     // stop of alignement update in dsp background

// bit in d_pll_config
#define B_32KHZ_CALIB      (1 << 14) // force DSP in Idle1 during 32 khz calibration
// ****************************************************************
// NDB AREA (PARAM) MCU<->DSP COMMUNICATION DEFINITIONS
// ****************************************************************
// bits in d_tch_mode
#define B_EOTD            (1 << 0) // EOTD mode
#define B_PLAY_UL         (1 << 3) // Play UL
#define B_DCO_ON          (1 << 4) // DCO ON/OFF
#define B_AUDIO_ASYNC     (1 << 1) // WCP reserved

// ****************************************************************
// PARAMETER AREA (PARAM) MCU<->DSP COMMUNICATION DEFINITIONS
// ****************************************************************
#define C_POND_RED              1L
// below values are defined in the file l1_time.h
//#define D_NSUBB_IDLE            296L
//#define D_NSUBB_DEDIC           30L
#define D_FB_THR_DET_IACQ       0x3333L
#define D_FB_THR_DET_TRACK      0x28f6L
#define D_DC_OFF_THRES          0x7fffL
#define D_DUMMY_THRES           17408L
#define D_DEM_POND_GEWL         26624L
#define D_DEM_POND_RED          20152L
#define D_HOLE                  0L
#define D_TRANSFER_RATE         0x6666L

// Full Rate vocoder definitions.
#define D_MACCTHRESH1           7872L
#define D_MLDT                  -4L
#define D_MACCTHRESH            7872L
#define D_GU                    5772L
#define D_GO                    7872L
#define D_ATTMAX                53L
#define D_SM                    -892L
#define D_B                     208L
#define D_SD_MIN_THR_TCHFS      15L                   //(24L   *C_POND_RED)
#define D_MA_MIN_THR_TCHFS      738L                  //(1200L *C_POND_RED)
#define D_MD_MAX_THR_TCHFS      1700L                 //(2000L *C_POND_RED)
#define D_MD1_MAX_THR_TCHFS     99L                   //(160L  *C_POND_RED)

#if (DSP == 33) || (DSP == 34) || (DSP == 35) || (DSP == 36)
  // Frequency burst definitions
  #define D_FB_MARGIN_BEG         24
  #define D_FB_MARGIN_END         22

  // V42bis definitions
  #define D_V42B_SWITCH_HYST      16L
  #define D_V42B_SWITCH_MIN       64L
  #define D_V42B_SWITCH_MAX       250L
  #define D_V42B_RESET_DELAY      10L

  // Latencies definitions
  #if (DSP == 33) || (DSP == 34) || (DSP == 35) || (DSP == 36)
    // C.f. BUG1404
    #define D_LAT_MCU_BRIDGE        0x000FL
  #else
  #define D_LAT_MCU_BRIDGE        0x0009L
  #endif

  #define D_LAT_MCU_HOM2SAM       0x000CL

  #define D_LAT_MCU_BEF_FAST_ACCESS 0x0005L
  #define D_LAT_DSP_AFTER_SAM     0x0004L

  // Background Task in GSM mode: Initialization.
  #define D_GSM_BGD_MGT           0L

#if (CHIPSET == 4)
  #define D_MISC_CONFIG           0L
#elif (CHIPSET == 7)  || (CHIPSET == 8) || (CHIPSET == 10) || (CHIPSET == 11) || (CHIPSET == 12)
  #define D_MISC_CONFIG           1L
#else
  #define D_MISC_CONFIG           0L
#endif

#endif

// Hall Rate vocoder and ched definitions.

#define D_SD_MIN_THR_TCHHS      37L
#define D_MA_MIN_THR_TCHHS      344L
#define D_MD_MAX_THR_TCHHS      2175L
#define D_MD1_MAX_THR_TCHHS     138L
#define D_SD_AV_THR_TCHHS       1845L
#define D_WED_FIL_TC            0x7c00L
#define D_WED_FIL_INI           4650L
#define D_X_MIN                 15L
#define D_X_MAX                 23L
#define D_Y_MIN                 703L
#define D_Y_MAX                 2460L
#define D_SLOPE                 135L
#define D_WED_DIFF_THRESHOLD    406L
#define D_MABFI_MIN_THR_TCHHS   5320L
#define D_LDT_HR                -5
#define D_MACCTRESH_HR          6500
#define D_MACCTRESH1_HR         6500
#define D_GU_HR                 2620
#define D_GO_HR                 3700
#define D_B_HR                  182
#define D_SM_HR                 -1608
#define D_ATTMAX_HR             53

// Enhanced Full Rate vocoder and ched definitions.

#define C_MLDT_EFR              -4
#define C_MACCTHRESH_EFR        8000
#define C_MACCTHRESH1_EFR       8000
#define C_GU_EFR                4522
#define C_GO_EFR                6500
#define C_B_EFR                 174
#define C_SM_EFR                -878
#define C_ATTMAX_EFR            53
#define D_SD_MIN_THR_TCHEFS     15L                   //(24L   *C_POND_RED)
#define D_MA_MIN_THR_TCHEFS     738L                  //(1200L *C_POND_RED)
#define D_MD_MAX_THR_TCHEFS     1230L                 //(2000L *C_POND_RED)
#define D_MD1_MAX_THR_TCHEFS    99L                   //(160L  *C_POND_RED)


// Integrated Data Services definitions.
#define D_MAX_OVSPD_UL          8
// Detect frames containing 90% of 1s as synchro frames
#define D_SYNC_THRES            0x3f50
// IDLE frames are only frames with 100 % of 1s
#define D_IDLE_THRES            0x4000
#define D_M1_THRES              5
#define D_MAX_OVSP_DL           8

// d_ra_act: bit field definition
#define B_F48BLK                5

// Mask for b_itc information (d_ra_conf)
#define CE_MASK                 0x04

#define D_FACCH_THR             0
#define D_DSP_TEST              0
#define D_VERSION_NUMBER        0
#define D_TI_VERSION            0


/*------------------------------------------------------------------------------*/
/*                                                                              */
/*                 DEFINITIONS FOR DSP <-> MCU COMMUNICATION.                   */
/*                 ++++++++++++++++++++++++++++++++++++++++++                   */
/*                                                                              */
/*------------------------------------------------------------------------------*/
// COMMUNICATION Interrupt definition
//------------------------------------
#define ALL_16BIT          0xffffL
#define B_GSM_PAGE         (1 << 0)
#define B_GSM_TASK         (1 << 1)
#define B_MISC_PAGE        (1 << 2)
#define B_MISC_TASK        (1 << 3)

#define B_GSM_PAGE_MASK    (ALL_16BIT ^ B_GSM_PAGE)
#define B_GSM_TASK_MASK    (ALL_16BIT ^ B_GSM_TASK)
#define B_MISC_PAGE_MASK   (ALL_16BIT ^ B_MISC_PAGE)
#define B_MISC_TASK_MASK   (ALL_16BIT ^ B_MISC_TASK)

// Common definition
//----------------------------------
// Index to *_DEMOD* arrays.
#define D_TOA                    0  // Time Of Arrival.
#define D_PM                     1  // Power Measurement.
#define D_ANGLE                  2  // Angle (AFC correction)
#define D_SNR                    3  // Signal / Noise Ratio.

// Bit name/position definitions.
#define B_FIRE0                  5  // Fire result bit 0. (00 -> NO ERROR) (01 -> ERROR CORRECTED)
#define B_FIRE1                  6  // Fire result bit 1. (10 -> ERROR)    (11 -> unused)
#define B_SCH_CRC                8  // CRC result for SB decoding. (1 for ERROR).
#define B_BLUD                  15  // Uplink,Downlink data block Present. (1 for PRESENT).
#define B_AF                    14  // Activity bit: 1 if data block is valid.
#define B_BFI                    2  // Bad Frame Indicator
#define B_UFI                    0  // UNRELIABLE FRAME Indicator
#define B_ECRC                   9  // Enhanced full rate CRC bit
#define B_EMPTY_BLOCK           10  // for voice memo purpose, this bit is used to determine

#if (DEBUG_DEDIC_TCH_BLOCK_STAT == 1)
  #define FACCH_GOOD 10
  #define FACCH_BAD  11
#endif

#if (AMR == 1)
  // Place of the RX type in the AMR block header
  #define RX_TYPE_SHIFT           3
  #define RX_TYPE_MASK            0x0038

  // Place of the vocoder type in the AMR block header
  #define VOCODER_TYPE_SHIFT      0
  #define VOCODER_TYPE_MASK       0x0007

  // List of the possible RX types in a_dd block
  #define SPEECH_GOOD             0
  #define SPEECH_DEGRADED         1
  #define ONSET                   2
  #define SPEECH_BAD              3
  #define SID_FIRST               4
  #define SID_UPDATE              5
  #define SID_BAD                 6
  #define AMR_NO_DATA             7
  #define AMR_INHIBIT             8

  // List of possible RX types in RATSCCH block
  #define C_RATSCCH_GOOD          5

  // List of the possible AMR channel rate
  #define AMR_CHANNEL_4_75        0
  #define AMR_CHANNEL_5_15        1
  #define AMR_CHANNEL_5_9         2
  #define AMR_CHANNEL_6_7         3
  #define AMR_CHANNEL_7_4         4
  #define AMR_CHANNEL_7_95        5
  #define AMR_CHANNEL_10_2        6
  #define AMR_CHANNEL_12_2        7

  // Types of RATSCCH blocks
  #define C_RATSCCH_UNKNOWN                   0
  #define C_RATSCCH_CMI_PHASE_REQ             1
  #define C_RATSCCH_AMR_CONFIG_REQ_MAIN       2
  #define C_RATSCCH_AMR_CONFIG_REQ_ALT        3
  #define C_RATSCCH_AMR_CONFIG_REQ_ALT_IGNORE 4    // Alternative AMR_CONFIG_REQ with updates coming in the next THRES_REQ block
  #define C_RATSCCH_THRES_REQ                 5

  // These flags define a bitmap that indicates which AMR parameters are being modified by a RATSCCH
  #define C_AMR_CHANGE_CMIP  0
  #define C_AMR_CHANGE_ACS   1
  #define C_AMR_CHANGE_ICM   2
  #define C_AMR_CHANGE_THR1  3
  #define C_AMR_CHANGE_THR2  4
  #define C_AMR_CHANGE_THR3  5
  #define C_AMR_CHANGE_HYST1 6
  #define C_AMR_CHANGE_HYST2 7
  #define C_AMR_CHANGE_HYST3 8

  // CMIP default value
  #define C_AMR_CMIP_DEFAULT 1  // According to ETSI specification 05.09, cmip is always 1 by default (new channel, handover...)

#endif
// "d_ctrl_tch" bits positions for TCH configuration.
#define B_CHAN_MODE              0
#define B_CHAN_TYPE              4
#define B_RESET_SACCH            6
#define B_VOCODER_ON             7
#define B_SYNC_TCH_UL            8
#if (AMR == 1)
  #define B_SYNC_AMR               9
#else
#define B_SYNC_TCH_DL            9
#endif
#define B_STOP_TCH_UL           10
#define B_STOP_TCH_DL           11
#define B_TCH_LOOP              12
#define B_SUBCHANNEL            15

// "d_ctrl_abb" bits positions for conditionnal loading of abb registers.
#define B_RAMP                   0
#if ((ANLG_FAM == 1) || (ANLG_FAM == 2) || (ANLG_FAM == 3))
  #define B_BULRAMPDEL             3 // Note: this name is changed
  #define B_BULRAMPDEL2            2 // Note: this name is changed
  #define B_BULRAMPDEL_BIS         9
  #define B_BULRAMPDEL2_BIS       10
#endif
#define B_AFC                    4

// "d_ctrl_system" bits positions.
#define B_TSQ                    0
#define B_BCCH_FREQ_IND          3
#define B_TASK_ABORT            15  // Abort RF tasks for DSP.

/* Channel type definitions for DEDICATED mode */
#define INVALID_CHANNEL    0
#define TCH_F              1
#define TCH_H              2
#define SDCCH_4            3
#define SDCCH_8            4

/* Channel mode definitions for DEDICATED mode */
#define SIG_ONLY_MODE      0    // signalling only
#define TCH_FS_MODE        1    // speech full rate
#define TCH_HS_MODE        2    // speech half rate
#define TCH_96_MODE        3    // data 9,6 kb/s
#define TCH_48F_MODE       4    // data 4,8 kb/s full rate
#define TCH_48H_MODE       5    // data 4,8 kb/s half rate
#define TCH_24F_MODE       6    // data 2,4 kb/s full rate
#define TCH_24H_MODE       7    // data 2,4 kb/s half rate
#define TCH_EFR_MODE       8    // enhanced full rate
#define TCH_144_MODE       9    // data 14,4 kb/s half rate

