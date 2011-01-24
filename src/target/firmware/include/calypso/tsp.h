#ifndef _CALYPSO_TSP_H
#define _CALYPSO_TSP_H

#define TSPACT(x)	(1 << x)
#define TSPEN(x)	(x)

/* initiate a TSP write through the TPU */
void tsp_write(uint8_t dev_idx, uint8_t bitlen, uint32_t dout);

/* Configure clock edge and chip enable polarity for a device */
void tsp_setup(uint8_t dev_idx, int clk_rising, int en_positive, int en_edge);

/* Obtain the current tspact state */
uint16_t tsp_act_state(void);

/* Update the TSPACT state, including enable and disable */
void tsp_act_update(uint16_t new_act);

/* Enable one or multiple TSPACT signals */
void tsp_act_enable(uint16_t bitmask);

/* Disable one or multiple TSPACT signals */
void tsp_act_disable(uint16_t bitmask);

/* Toggle one or multiple TSPACT signals */
void tsp_act_toggle(uint16_t bitmask);

/* Initialize TSP driver */
void tsp_init(void);

#endif /* _CALYPSO_TSP_H */
