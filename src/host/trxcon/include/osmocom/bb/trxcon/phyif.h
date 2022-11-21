#pragma once

#include <stdint.h>

#include <osmocom/core/bits.h>

/* PHYIF command type */
enum trxcon_phyif_cmd_type {
	TRXCON_PHYIF_CMDT_RESET,
	TRXCON_PHYIF_CMDT_POWERON,
	TRXCON_PHYIF_CMDT_POWEROFF,
	TRXCON_PHYIF_CMDT_MEASURE,
	TRXCON_PHYIF_CMDT_SETFREQ_H0,
	TRXCON_PHYIF_CMDT_SETFREQ_H1,
	TRXCON_PHYIF_CMDT_SETSLOT,
	TRXCON_PHYIF_CMDT_SETTA,
};

/* param of TRXCON_PHYIF_CMDT_SETFREQ_H0 */
struct trxcon_phyif_cmdp_setfreq_h0 {
	uint16_t band_arfcn;
};

/* param of TRXCON_PHYIF_CMDT_SETFREQ_H1 */
struct trxcon_phyif_cmdp_setfreq_h1 {
	uint8_t hsn;
	uint8_t maio;
	const uint16_t *ma;
	unsigned int ma_len;
};

/* param of TRXCON_PHYIF_CMDT_SETSLOT */
struct trxcon_phyif_cmdp_setslot {
	uint8_t tn;
	uint8_t pchan; /* enum gsm_phys_chan_config */
};

/* param of TRXCON_PHYIF_CMDT_SETTA */
struct trxcon_phyif_cmdp_setta {
	int8_t ta; /* intentionally signed */
};

/* param of TRXCON_PHYIF_CMDT_MEASURE (command) */
struct trxcon_phyif_cmdp_measure {
	uint16_t band_arfcn;
};

/* param of TRXCON_PHYIF_CMDT_MEASURE (response) */
struct trxcon_phyif_rspp_measure {
	uint16_t band_arfcn;
	int dbm;
};

struct trxcon_phyif_cmd {
	enum trxcon_phyif_cmd_type type;
	union {
		struct trxcon_phyif_cmdp_setfreq_h0 setfreq_h0;
		struct trxcon_phyif_cmdp_setfreq_h1 setfreq_h1;
		struct trxcon_phyif_cmdp_setslot setslot;
		struct trxcon_phyif_cmdp_setta setta;
		struct trxcon_phyif_cmdp_measure measure;
	} param;
};

struct trxcon_phyif_rsp {
	enum trxcon_phyif_cmd_type type;
	union {
		struct trxcon_phyif_rspp_measure measure;
	} param;
};

/* RTS.ind - Ready-to-Send indication */
struct trxcon_phyif_rts_ind {
	uint32_t fn;
	uint8_t tn;
};

/* RTR.ind - Ready-to-Receive indicaton */
struct trxcon_phyif_rtr_ind {
	uint32_t fn;
	uint8_t tn;
};

/* The probed lchan is active */
#define TRXCON_PHYIF_RTR_F_ACTIVE	(1 << 0)

/* RTR.rsp - Ready-to-Receive response */
struct trxcon_phyif_rtr_rsp {
	uint32_t flags; /* see TRXCON_PHYIF_RTR_F_* above */
};

/* BURST.req - a burst to be transmitted */
struct trxcon_phyif_burst_req {
	uint32_t fn;
	uint8_t tn;
	uint8_t pwr;
	const ubit_t *burst;
	unsigned int burst_len;
};

/* BURST.ind - a received burst */
struct trxcon_phyif_burst_ind {
	uint32_t fn;
	uint8_t tn;
	int16_t toa256;
	int8_t rssi;
	const sbit_t *burst;
	unsigned int burst_len;
};

int trxcon_phyif_handle_burst_req(void *phyif, const struct trxcon_phyif_burst_req *br);
int trxcon_phyif_handle_burst_ind(void *priv, const struct trxcon_phyif_burst_ind *bi);
int trxcon_phyif_handle_clock_ind(void *priv, uint32_t fn);

int trxcon_phyif_handle_rts_ind(void *priv, const struct trxcon_phyif_rts_ind *rts);
int trxcon_phyif_handle_rtr_ind(void *priv, const struct trxcon_phyif_rtr_ind *ind,
				struct trxcon_phyif_rtr_rsp *rsp);

int trxcon_phyif_handle_cmd(void *phyif, const struct trxcon_phyif_cmd *cmd);
int trxcon_phyif_handle_rsp(void *priv, const struct trxcon_phyif_rsp *rsp);
void trxcon_phyif_close(void *phyif);
