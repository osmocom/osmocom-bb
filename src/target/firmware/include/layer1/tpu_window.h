#ifndef _L1_TPU_CTRL_H
#define _L1_TPU_CTRL_H

enum l1_rxwin_type {
	L1_RXWIN_PW,	/* power measurement */
	L1_RXWIN_FB,	/* FCCH burst detection */
	L1_RXWIN_SB,	/* SCH burst detection */
	L1_RXWIN_NB,	/* Normal burst decoding */
	_NUM_L1_RXWIN
};

enum l1_txwin_type {
	L1_TXWIN_NB,	/* Normal burst sending */
	L1_TXWIN_AB,	/* RACH burst sending */
	_NUM_L1_TXWIN
};

void l1s_win_init(void);
void l1s_rx_win_ctrl(uint16_t arfcn, enum l1_rxwin_type wtype, uint8_t tn_ofs);
void l1s_tx_win_ctrl(uint16_t arfcn, enum l1_txwin_type wtype, uint8_t pwr, uint8_t tn_ofs);

void tpu_end_scenario(void);

#endif /* _L1_TPU_CTRL_H */
