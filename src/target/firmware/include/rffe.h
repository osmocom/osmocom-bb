#ifndef _RFFE_H
#define _RFFE_H

#include <gsm.h>

/* initialize RF Frontend */
void rffe_init(void);

/* switch RF Frontend Mode */
void rffe_mode(enum gsm_band band, int tx);

/* get current gain of RF frontend (anything between antenna and baseband in dBm */
uint8_t rffe_get_gain(void);

#endif
