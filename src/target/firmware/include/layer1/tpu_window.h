#ifndef _L1_TPU_CTRL_H
#define _L1_TPU_CTRL_H

enum l1_rxwin_type {
	L1_RXWIN_PW,	/* power measurement */
	L1_RXWIN_FB,	/* FCCH burst detection */
	L1_RXWIN_SB,	/* SCH burst detection */
	L1_RXWIN_NB,	/* Normal burst decoding */
	_NUM_L1_RXWIN
};


void l1s_rx_win_ctrl(uint16_t arfcn, enum l1_rxwin_type wtype);

void tpu_end_scenario(void);

#endif /* _L1_TPU_CTRL_H */
