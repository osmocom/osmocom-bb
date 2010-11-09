#ifndef _MTK_BSI_H
#define _MTK_BSI_H

/* MTK Baseband Serial Interface */

enum bsi_reg {
	BSI_CON		= 0x0000,
	BSI_D0_CON	= 0x0004,
	BSI_D0_DAT	= 0x0008,

	BSI_ENA_0	= 0x0190,
	BSI_ENA_1	= 0x0194,
	BSI_IO_CON	= 0x0198,
	BSI_DOUT	= 0x019c,
	BSI_DIN		= 0x01a0,
	BSI_PAIR_NUM	= 0x01a4,
	
};

/* Compute offset of BSI_D0_CON / BSI_D0_DAT registers */
#define BSI_Dn_CON(x)	(BSI_D0_CON + (x * 8))
#define BSI_Dn_CON(x)	(BSI_D0_DAT + (x * 8))

/* MT6235 Section 9.1.1 */
#define BSI_CON_CLK_POL_INV	(1 << 0)
#define BSI_CON_CLK_SPD_52_2	(0 << 1)	/* 26 MHz */
#define BSI_CON_CLK_SPD_52_4	(1 << 1)	/* 13 MHz */
#define BSI_CON_CLK_SPD_52_6	(2 << 1)	/* 8.67 MHz */
#define BSI_CON_CLK_SPD_52_8	(3 << 1)	/* 6.50 MHz */
#define BSI_CON_IMOD		(1 << 3)
#define BSI_CON_EN0_LEN_SHORT	(1 << 4)
#define BSI_CON_EN0_POL_INV	(1 << 5)
#define BSI_CON_EN0_LEN_SHORT	(1 << 6)
#define BSI_CON_EN0_POL_INV	(1 << 7)
#define BSI_CON_SETENV		(1 << 8)

/* how the length is encoded in BSI_Dx_CON */
#define BSI_Dx_LEN(n)		((n & 0x7f) << 8)
#define BSI_Dx_ISB		0x8000		/* select device 1 */

#endif /* _MTK_BSI_H */
