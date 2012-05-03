#ifndef _BATTERY_COMPAL_E88_H
#define _BATTERY_COMPAL_E88_H

#include <stdint.h>
#include <abb/twl3025.h>

/* initialize the charger control loop on C123 */

extern void
battery_compal_e88_init();

extern uint16_t
compal_e88_madc[MADC_NUM_CHANNELS];

#endif
