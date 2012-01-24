#ifndef ASSIGNMENT_H
#define ASSIGNMENT_H

#include <stdint.h>
#include <osmocom/gsm/gsm48_ie.h>

struct gsm_assignment {
	int chan_nr;
	int tsc;
	int h;
	union {
		struct {
			int band_arfcn;
		} h0;
		struct {
			int maio;
			int hsn;
			uint16_t ma[64];
			int ma_len;
		} h1;
	};
	int chan_mode;
	int rate_conf;
};

void parse_assignment(uint8_t *msg, unsigned len, struct gsm_sysinfo_freq *cell_arfcns, struct gsm_assignment *ga);

#endif
