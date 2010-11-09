#ifndef _MTK_TDMA_H
#define _MTK_TDMA_H

/* MTK TDMA Timer */

/* MT6235 Section 11 */

enum mtk_tdma_reg {
	/* Read current quarter bit count */
	TDMA_TQCNT		= 0x0000,
	/* Latched Qbit counter reset position */
	TDMA_WRAP		= 0x0004,
	/* Direct Qbit counter reset position */
	TDMA_WRAPIMD		= 0x0008,
	/* Event latch position */
	TDMA_EVTVAL		= 0x000c,
	/* DSP software control */
	TDMA_DTIRQ		= 0x0010,
	/* MCU software control */
	TDMA_CTIRQ1		= 0x0014,
	TDMA_CTIRQ2	 	= 0x0018,
	/* AFC control */
	TDMA_AFC0		= 0x0020,
	TDMA_AFC1		= 0x0024,
	TDMA_AFC2		= 0x0028,
	TDMA_AFC3		= 0x002c,

	/* BSI event */
	TDMA_BSI0		= 0x00b0,
	/* BPI event */
	TDMA_BPI0		= 0x0100,
	/* Auxiliary ADC event */
	TDMA_AUXEV0		= 0x0400,
	TDMA_AUXEV1		= 0x0404,
	/* Event Control */
	TDMA_EVTENA0		= 0x0150,
	TDMA_EVTENA1		= 0x0154,
	TDMA_EVTENA2		= 0x0158,
	TDMA_EVTENA3		= 0x015c,
	TDMA_EVTENA4		= 0x0160,
	TDMA_EVTENA5		= 0x0164,
	TDMA_EVTENA6		= 0x0168,
	TDMA_EVTENA6		= 0x016c,
	TDMA_WRAPOFS		= 0x0170,
	TDMA_REGBIAS		= 0x0174,
	TDMA_DTXCON		= 0x0180,
	TDMA_RXCON		= 0x0184,
	TDMA_BDLCON		= 0x0188,
	TDMA_BULCON1		= 0x018c,
	TDMA_BULCON2		= 0x0190,
	TDMA_FB_FLAG		= 0x0194,
	TDMA_FB_CLRI		= 0x0198,
};

#define TDMA_BSI(n)	(TDMA_BSI0 + (n)*4)
#define TDMA_BPI(n)	(TDMA_BPI0 + (n)*4)
	
	

#endif /* _MTK_TDMA_H */
