#pragma once

#define NUM_RXLEVS 32
#define NUM_ARFCNS 1024

struct rxlev_stats {
	/* the maximum number of ARFCN's is 1024, and there are 32 RxLevels,
	 * so in we keep one 1024bit-bitvec for each RxLev */
	uint8_t rxlev_buckets[NUM_RXLEVS][NUM_ARFCNS/8];
};

void rxlev_stat_input(struct rxlev_stats *st, uint16_t arfcn, uint8_t rxlev);

/* get the next ARFCN that has the specified Rxlev */
int16_t rxlev_stat_get_next(const struct rxlev_stats *st, uint8_t rxlev, int16_t arfcn);

void rxlev_stat_reset(struct rxlev_stats *st);

void rxlev_stat_dump(const struct rxlev_stats *st);
