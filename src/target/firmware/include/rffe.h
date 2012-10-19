#ifndef _RFFE_H
#define _RFFE_H

#include <osmocom/gsm/gsm_utils.h>

extern const uint8_t system_inherent_gain;

/* initialize RF Frontend */
void rffe_init(void);

/* switch RF Frontend Mode */
void rffe_mode(enum gsm_band band, int tx);

/* query RF wiring */
enum rffe_port
{
	PORT_LO		= 0,	/* Combined 850/900 port */
	PORT_HI		= 1,	/* Combined 1800/1900 port */
	PORT_GSM850	= 2,
	PORT_GSM900	= 3,
	PORT_DCS1800	= 4,
	PORT_PCS1900	= 5,
};

uint32_t rffe_get_rx_ports(void);
uint32_t rffe_get_tx_ports(void);

/* IQ swap requirements */
int rffe_iq_swapped(uint16_t band_arfcn, int tx);

/* get current gain of RF frontend (anything between antenna and baseband in dBm */
uint8_t rffe_get_gain(void);

void rffe_set_gain(uint8_t dbm);

void rffe_compute_gain(int16_t exp_inp, int16_t target_bb);

#endif
