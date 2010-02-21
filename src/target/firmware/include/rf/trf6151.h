#ifndef _TRF6151_H
#define _TRF6151_H

#include <gsm.h>

/* initialize (reset + power up) */
void trf6151_init(void);

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
void trf6151_rx_window(int16_t start_qbits, uint16_t arfcn, uint8_t vga_dbm, int rf_gain_high);

#endif /* TRF6151_H */
