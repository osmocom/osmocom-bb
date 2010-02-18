#ifndef _GSM_H
#define _GSM_H

#include <l1a_l23_interface.h>

enum gsm_band {
	GSM_850		= 1,
	GSM_900		= 2,
	GSM_1800	= 4,
	GSM_1900	= 8,
	GSM_450		= 0x10,
	GSM_480		= 0x20,
	GSM_750		= 0x40,
	GSM_810		= 0x80,
};

#define	ARFCN_PCS	0x8000

enum gsm_band gsm_arfcn2band(uint16_t arfcn);

/* Convert an ARFCN to the frequency in MHz * 10 */
uint16_t gsm_arfcn2freq10(uint16_t arfcn, int uplink);

/* Convert from frame number to GSM time */
void gsm_fn2gsmtime(struct gsm_time *time, uint32_t fn);

/* Convert from GSM time to frame number */
uint32_t gsm_gsmtime2fn(struct gsm_time *time);
#endif
