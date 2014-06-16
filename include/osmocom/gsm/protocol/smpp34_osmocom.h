#pragma once
/* Osmocom SMPP extensions */

/* Osmocom specific new TLV definitions */

/* ARFCN in 16-bit encoding, highest bit: PCS(1) / DCS(0) */
#define TLVID_osmo_arfcn	0x2300
/* Timing advance as uint8_t */
#define TLVID_osmo_ta		0x2301
/* Receive signal level (uplink) as int16_t in dBm */
#define TLVID_osmo_rxlev_ul	0x2302
/* Receive signal quality (uplink) as uint8_t */
#define TLVID_osmo_rxqual_ul	0x2303
/* Receive signal level (downlink) as int16_t in dBm */
#define TLVID_osmo_rxlev_dl	0x2304
/* Receive signal quality (downlink) as uint8_t */
#define TLVID_osmo_rxqual_dl	0x2305
/* IMEI of the subscriber, if known */
#define TLVID_osmo_imei		0x2306
/* MS Layer 1 Transmit Power */
#define TLVID_osmo_ms_l1_txpwr	0x2307
/* BTS Layer 1 Transmit Power */
#define TLVID_osmo_bts_l1_txpwr	0x2308


/* DELIVER_SM can contain the following optional Osmocom TLVs:
 * 	TLVID_osmo_arfcn
 * 	TLVID_osmo_ta
 * 	TLVID_osmo_rxlev_ul
 * 	TLVID_osmo_rxqual_ul
 * 	TLVID_osmo_rxlev_dl
 * 	TLVID_osmo_rxqual_dl
 * 	TLVID_osmo_imei
 */

/* SUBMIT_SM_RESP (transaction mode) can contain the following optional
 * Osmocom TLVs:
 * 	TLVID_osmo_arfcn
 * 	TLVID_osmo_ta
 * 	TLVID_osmo_rxlev_ul
 * 	TLVID_osmo_rxqual_ul
 * 	TLVID_osmo_rxlev_dl
 * 	TLVID_osmo_rxqual_dl
 * 	TLVID_osmo_imei
 */
