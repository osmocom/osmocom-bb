#ifndef _MTK_BFE_H
#define _MTK_BFE_H

/* MTK Baseband Frontend */

/* MT6235 Chapter 10 */

enum mtk_bfe_reg {
	BFE_CON			= 0x0000,
	BFE_STA			= 0x0004,
	/* Rx Configuration Register */
	RX_CFG			= 0x0010,
	/* Rx Control Register */
	RX_CON			= 0x0014,
	/* RX Interference Detection Power Measurement Control Register */
	RX_PM_CON		= 0x0018,
	/* RX FIR Coefficient Set ID Control Register */
	RX_FIR_CSID_CON		= 0x001c,
	/* RX Ram0 Coefficient Set 0 Register */
	RX_RAM0_CS0		= 0x0070,
	/* RX Ram1 Coefficient Set 0 Register */
	RX_RAM1_CS0		= 0x0020,
	/* Rx Interference Detection HPF Power Register */
	RX_HPWR_STS		= 0x00b0,		
	/* Rx Interference Detection BPF Power Register */
	RX_BPWR_STS		= 0x00b4,

	TX_CFG			= 0x0060,
	TX_CON			= 0x0064,
	TX_OFF			= 0x0068,
};

#define RX_RAM0_CS(n)	(RX_RAM0_CS0 + (n)*4)
#define RX_RAM1_CS(n)	(RX_RAM0_CS1 + (n)*4)

/* SWAP I/Q before input to baesband frontend */
#define RX_CFG_SWAP_IQ		0x0001
/* Bypass RX FIR filter control */
#define RX_CFG_BYPFLTR		0x0002
/* Number of RX FIR filter taps */
#define RX_CFG_FIRTPNO(n)	(((n) & 0x3f) << 4)

#define RX_CON_BLPEN_NORMAL	(0 << 0)
#define RX_CON_BLPEN_LOOPB	(1 << 0)
#define RX_CON_BLPEN_LOOPB_FILT	(2 << 0)

/* Phase de-rotation in wide FIR data path */
#define RX_CON_PH_ROEN_W	(1 << 2)
/* Phase de-rotation in narrow FIR data path */
#define RX_CON_PH_ROEN_N	(1 << 3)
/* RX I-data gain compenstation select (+/- 1.5dB */
#define RX_CON_IGAINSEL_00dB	(0 << 4)
#define RX_CON_IGAINSEL_03dB	(1 << 4)
#define RX_CON_IGAINSEL_06dB	(2 << 4)
#define RX_CON_IGAINSEL_09dB	(3 << 4)
#define RX_CON_IGAINSEL_12dB	(4 << 4)
#define RX_CON_IGAINSEL_15dB	(5 << 4)
#define RX_CON_IGAINSEL_n03dB	(9 << 4)
#define RX_CON_IGAINSEL_n06dB	(10 << 4)
#define RX_CON_IGAINSEL_n09dB	(11 << 4)
#define RX_CON_IGAINSEL_n12dB	(12 << 4)
#define RX_CON_IGAINSEL_n15dB	(13 << 4)

/* TX_CFG */
/* Appending Bits enable */
#define TX_CFG_APNDEN		(1 << 0)
/* Ramp Profile Select for 8PSK */
#define TX_CFG_RPSEL_I		(0 << 1)	/* 50 kHz sine tone */
#define TX_CFG_RPSEL_II		(1 << 1)	/* null DC I/Q */
#define TX_CFG_RPSEL_III	(3 << 1)
#define TX_CFG_INTEN		(1 << 3)	/* Interpolate between bursts */
#define TX_CFG_MDBYP		(1 << 4)	/* Modulator Bypass */
#define TX_CFG_SGEN		(1 << 5)	/* 540 kHz sine tone */
#define TX_CFG_ALL_10GEN_ZERO	(1 << 6)
#define TX_CFG_ALL_10GEN_ONE	(2 << 6)
#define TX_CFG_SW_QBCNT(n)	(((n) & 0x1f) << 8)
#define TX_CFG_GMSK_DTAP_SYM_1	(0 << 13)
#define TX_CFG_GMSK_DTAP_SYM_0	(1 << 13)
#define TX_CFG_GMSK_DTAP_SYM_2	(2 << 13)

#define TX_CON_IQSWP		(1 << 0)	/* Swap I/Q */
/* GMSK or 8PSK modulation for 1st through 4th burst */
#define TX_CON_MDSEL1_8PSK	(1 << 2)
#define TX_CON_MDSEL2_8PSK	(1 << 3)
#define TX_CON_MDSEL3_8PSK	(1 << 4)
#define TX_CON_MDSEL4_8PSK	(1 << 5)
/* Quadratur phase compensation select */
#define TX_CON_PHSEL_0deg	(0 << 8)
#define TX_CON_PHSEL_1deg	(1 << 8)
#define TX_CON_PHSEL_2deg	(2 << 8)
#define TX_CON_PHSEL_3deg	(3 << 8)
#define TX_CON_PHSEL_4deg	(4 << 8)
#define TX_CON_PHSEL_5deg	(5 << 8)
#define TX_CON_PHSEL_n5deg	(10 << 8)
#define TX_CON_PHSEL_n4deg	(11 << 8)
#define TX_CON_PHSEL_n3deg	(12 << 8)
#define TX_CON_PHSEL_n2deg	(13 << 8)
#define TX_CON_PHSEL_n1deg	(14 << 8)
/* GMSK modulator output latenct */
#define TX_CON_GMSK_DTAP_QB(n)	(((n) & 3) << 12)

#define TX_OFF_I(n)		(((n) & 0x3f) << 0)
#define TX_OFF_Q(n)		(((n) & 0x3f) << 8)
/* Double Buffering */
#define TX_OFF_TYP_DB		0x8000

#endif /* _MTK_BFE_H */
