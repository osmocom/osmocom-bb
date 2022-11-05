#pragma once

#include <stdint.h>

#include <osmocom/core/bits.h>

/* PHYIF command type */
enum phyif_cmd_type {
	PHYIF_CMDT_RESET,
	PHYIF_CMDT_POWERON,
	PHYIF_CMDT_POWEROFF,
	PHYIF_CMDT_MEASURE,
	PHYIF_CMDT_SETFREQ_H0,
	PHYIF_CMDT_SETFREQ_H1,
	PHYIF_CMDT_SETSLOT,
	PHYIF_CMDT_SETTA,
};

/* param of PHYIF_CMDT_SETFREQ_H0 */
struct phyif_cmdp_setfreq_h0 {
	uint16_t band_arfcn;
};

/* param of PHYIF_CMDT_SETFREQ_H1 */
struct phyif_cmdp_setfreq_h1 {
	uint8_t hsn;
	uint8_t maio;
	const uint16_t *ma;
	unsigned int ma_len;
};

/* param of PHYIF_CMDT_SETSLOT */
struct phyif_cmdp_setslot {
	uint8_t tn;
	uint8_t pchan; /* enum gsm_phys_chan_config */
};

/* param of PHYIF_CMDT_SETTA */
struct phyif_cmdp_setta {
	int8_t ta; /* intentionally signed */
};

/* param of PHYIF_CMDT_MEASURE (command) */
struct phyif_cmdp_measure {
	uint16_t band_arfcn_start;
	uint16_t band_arfcn_stop;
};

/* param of PHYIF_CMDT_MEASURE (response) */
struct phyif_rspp_measure {
	bool last;
	uint16_t band_arfcn;
	int dbm;
};

struct phyif_cmd {
	enum phyif_cmd_type type;
	union {
		struct phyif_cmdp_setfreq_h0 setfreq_h0;
		struct phyif_cmdp_setfreq_h1 setfreq_h1;
		struct phyif_cmdp_setslot setslot;
		struct phyif_cmdp_setta setta;
		struct phyif_cmdp_measure measure;
	} param;
};

struct phyif_rsp {
	enum phyif_cmd_type type;
	union {
		struct phyif_rspp_measure measure;
	} param;
};

/* BURST.req - a burst to be transmitted */
struct phyif_burst_req {
	uint32_t fn;
	uint8_t tn;
	uint8_t pwr;
	const ubit_t *burst;
	unsigned int burst_len;
};

/* BURST.ind - a received burst */
struct phyif_burst_ind {
	uint32_t fn;
	uint8_t tn;
	int16_t toa256;
	int8_t rssi;
	sbit_t *burst;
	unsigned int burst_len;
};


int phyif_handle_burst_ind(void *phyif, const struct phyif_burst_ind *bi);
int phyif_handle_burst_req(void *phyif, const struct phyif_burst_req *br);
int phyif_handle_cmd(void *phyif, const struct phyif_cmd *cmd);
int phyif_handle_rsp(void *phyif, const struct phyif_rsp *rsp);
void phyif_close(void *phyif);
