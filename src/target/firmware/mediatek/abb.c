#include <stdint.h>
#include <abb/twl3025.h>

//TODO obviously refactor the interface to be generic and support both calypso and mediatek scenarios.

void twl3025_afc_set(int16_t val) {}
uint8_t twl3025_afcout_get(void) { return 0; }
void twl3025_unit_enable(enum twl3025_unit unit, int on) {}

/* values encountered on a GTA-02 for GSM900 (the same for GSM1800!?) */
const uint16_t twl3025_default_ramp[16] = {
	ABB_RAMP_VAL( 0,  0),
	ABB_RAMP_VAL( 0, 11),
	ABB_RAMP_VAL( 0, 31),
	ABB_RAMP_VAL( 0, 31),
	ABB_RAMP_VAL( 0, 31),
	ABB_RAMP_VAL( 0, 24),
	ABB_RAMP_VAL( 0,  0),
	ABB_RAMP_VAL( 0,  0),
	ABB_RAMP_VAL( 9,  0),
	ABB_RAMP_VAL(18,  0),
	ABB_RAMP_VAL(25,  0),
	ABB_RAMP_VAL(31,  0),
	ABB_RAMP_VAL(30,  0),
	ABB_RAMP_VAL(15,  0),
	ABB_RAMP_VAL( 0,  0),
	ABB_RAMP_VAL( 0,  0),
};

void twl3025_downlink(int on, int16_t at) {}
void twl3025_uplink(int on, int16_t at) {}



