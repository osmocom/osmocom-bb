#include <stdint.h>
#include <rffe.h>

void rffe_init(void) {}
void rffe_compute_gain(int16_t exp_inp, int16_t target_bb) {}
uint8_t rffe_get_gain(void) { return 0; }
void rffe_set_gain(uint8_t dbm) {}
int rffe_iq_swapped(uint16_t band_arfcn, int tx) { 
    // TODO mediatek equiv of trf6151_iq_swapped(band_arfcn, tx);
    return 0; 
}
/* This is a value that has been measured on the C123 by Harald: 71dBm,
   it is the difference between the input level at the antenna and what
   the DSP reports, subtracted by the total gain of the TRF6151 */
// TODO measure this inherent gain for mediatek/fernvale
#define SYSTEM_INHERENT_GAIN 71

const uint8_t system_inherent_gain = SYSTEM_INHERENT_GAIN;

void rffe_mode(enum gsm_band band, int tx) {}
