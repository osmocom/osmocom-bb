#ifndef _L1_TOA_H
#define _L1_TOA_H

/* Input a qbits error sample into the TOA averaging */
void toa_input(int32_t offset, uint32_t snr);

/* Reset the TOA counters */
void toa_reset(void);

#endif
