#ifndef _L1_RFCH_H
#define _L1_RFCH_H

struct gsm_time;

void rfch_get_params(struct gsm_time *t,
                     uint16_t *arfcn_p, uint8_t *tsc_p, uint8_t *tn_p);

#endif /* _L1_RFCH_H */
