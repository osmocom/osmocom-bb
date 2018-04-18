#include <stdint.h>
#include <rf/trf6151.h>

// was in target/firmware/rf/trf6151.c but... for mediatek/fernvale?
uint16_t rf_arfcn = 871; // TODO fixme, correct me
void trf6151_rx_window(int16_t start_qbits, uint16_t arfcn) {}
void trf6151_set_mode(enum trf6151_mode mode) {}
