#ifndef _L1_AFC_H
#define _L1_AFC_H

/* Input a frequency error sample into the AFC averaging */
void afc_input(int32_t freq_error, uint16_t arfcn, int valid);

/* Update the AFC with a frequency error, bypassing averaging */
void afc_correct(int16_t freq_error, uint16_t arfcn);

/* Update DSP with new AFC DAC value to be used for next TDMA frame */
void afc_load_dsp(void);

#endif
