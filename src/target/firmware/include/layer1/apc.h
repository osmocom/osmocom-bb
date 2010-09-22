#ifndef _L1_APC_H
#define _L1_APC_H

/* determine the AUXAPC value by the Tx Power Level */
int16_t apc_tx_dbm2auxapc(enum gsm_band band, int8_t dbm);

/* determine the AUXAPC value by the Tx Power Level */
int16_t apc_tx_pwrlvl2auxapc(enum gsm_band band, uint8_t lvl);

#endif
