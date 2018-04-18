#include <stdint.h>

// TODO refactor interface to be generic for mediatek/calypso
void calypso_sim_powerdown(void) {}
void calypso_sim_init(void) {}
int calypso_sim_powerup(uint8_t *atr) { return 0; }
void sim_handler(void) {}
void sim_apdu(uint16_t len, uint8_t *data) {}
