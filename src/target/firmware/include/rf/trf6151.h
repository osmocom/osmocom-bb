#ifndef _TRF6151_H
#define _TRF6151_H

#include <osmocom/gsm/gsm_utils.h>

/* initialize (reset + power up) */
void trf6151_init(uint8_t tsp_uid, uint16_t tsp_reset_id);

/* switch power off or on */
void trf6151_power(int on);

/* set the VGA and RF gain */
int trf6151_set_gain(uint8_t dbm, int high);

/* obtain the current total gain of the TRF6151 */
uint8_t trf6151_get_gain(void);

/* Request the PLL to be tuned to the given frequency */
void trf6151_set_arfcn(uint16_t arfcn, int uplink);

enum trf6151_mode {
	TRF6151_IDLE,
	TRF6151_RX,
	TRF6151_TX,
};

/* Set the operational mode of the TRF6151 chip */
void trf6151_set_mode(enum trf6151_mode mode);

void trf6151_test(uint16_t arfcn);
void trf6151_tx_test(uint16_t arfcn);

/* prepare a Rx window with the TRF6151 finished at time 'start' (in qbits) */
void trf6151_rx_window(int16_t start_qbits, uint16_t arfcn);

/* prepare a Tx window with the TRF6151 finished at time 'start' (in qbits) */
void trf6151_tx_window(int16_t start_qbits, uint16_t arfcn);

/* Given the expected input level of exp_inp dBm and the target of target_bb
 * dBm, configure the RF Frontend with the respective gain */
void trf6151_compute_gain(int16_t exp_inp, int16_t target_bb);

#endif /* TRF6151_H */
