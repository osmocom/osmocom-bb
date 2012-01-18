#ifndef CATCHER_H
#define CATCHER_H

#include <sys/time.h>

#define CATCH_IDLE 0
#define CATCH_TRY  1
#define CATCH_EST  2
#define CATCH_DATA 3
#define CATCH_SERV 4

struct catcher_status {
	unsigned flag;

	unsigned rach;
	unsigned paging;
	unsigned imm_ass;
	unsigned ass;
	unsigned ho;
	unsigned release;
	unsigned tune;
	unsigned failure;

	unsigned cipher_req;
	unsigned cipher_resp;
	unsigned cipher_no_sc;
	unsigned cipher_no_cr;
	uint8_t first_cipher;
	uint8_t last_cipher;

	unsigned camped;
	uint16_t mcc;
	uint16_t old_mcc;
	unsigned mcc_change;
	uint16_t mnc;
	uint16_t old_mnc;
	unsigned mnc_change;
	uint16_t lac;
	uint16_t old_lac;
	unsigned lac_change;
	uint16_t cid;
	uint16_t old_cid;
	unsigned cid_change;

	//neighbours cid

	unsigned reg_timer;
	unsigned imsi_req;
	unsigned imei_req;
	unsigned silent_sms;
	unsigned power_count;
	unsigned high_power_count;

	unsigned current;
	struct osmo_timer_list dcch_time;
	struct timeval start_time;
	struct timeval stop_time;
};

void start_tcatcher(struct osmocom_ms *ms);

void stop_tcatcher(struct osmocom_ms *ms);

#endif
