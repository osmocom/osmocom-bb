#ifndef _TWL3025_H
#define _TWL3025_H

#define PAGE(n)		(n << 7)
enum twl3025_reg {
	VRPCCFG		= PAGE(1) | 30,
	VRPCDEV		= PAGE(0) | 30,
	VRPCMSK		= PAGE(1) | 31,
	VRPCMSKABB	= PAGE(1) | 29,
	VRPCSTS		= PAGE(0) | 31,
	/* Monitoring ADC Registers */
	MADCTRL		= PAGE(0) | 13,
	MADCSTAT	= PAGE(0) | 24,
	VBATREG		= PAGE(0) | 15,
	VCHGREG		= PAGE(0) | 16,
	ICHGREG		= PAGE(0) | 17,
	VBKPREG		= PAGE(0) | 18,
	ADIN1REG	= PAGE(0) | 19,
	ADIN2REG	= PAGE(0) | 20,
	ADIN3REG	= PAGE(0) | 21,
	ADIN4REG	= PAGE(0) | 22,
	/* Clock Generator Registers */
	TOGBR1		= PAGE(0) | 4,
	TOGBR2		= PAGE(0) | 5,
	PWDNRG		= PAGE(1) | 9,
	TAPCTRL		= PAGE(1) | 19,
	TAPREG		= PAGE(1) | 20,
	/* Automatic Frequency Control (AFC) Registers */
	AUXAFC1		= PAGE(0) | 7,
	AUXAFC2		= PAGE(0) | 8,
	AFCCTLADD	= PAGE(1) | 21,
	AFCOUT		= PAGE(1) | 22,
	/* Automatic Power Control (APC) Registers */
	APCDEL1		= PAGE(0) | 2,
	APCDEL2		= PAGE(1) | 26,
	AUXAPC		= PAGE(0) | 9,
	APCRAM		= PAGE(0) | 10,
	APCOFF		= PAGE(0) | 11,
	APCOUT		= PAGE(1) | 12,
	/* Auxiliary DAC Control Register */
	AUXDAC		= PAGE(0) | 12,
	/* SimCard Control Register */
	VRPCSIM		= PAGE(1) | 23,
	/* LED Driver Register */
	AUXLED		= PAGE(1) | 24,
	/* Battery Charger Interface (BCI) Registers */
	CHGREG		= PAGE(0) | 25,
	BCICTL1		= PAGE(0) | 28,
	BCICTL2		= PAGE(0) | 29,
	BCICONF		= PAGE(1) | 13,
	/* Interrupt and Bus Control (IBIC) Registers */
	ITMASK		= PAGE(0) | 28,
	ITSTATREG	= PAGE(0) | 27,	/* both pages! */
	PAGEREG		= PAGE(0) | 1, 	/* both pages! */
	/* Baseband Codec (BBC) Registers */
	BULIOFF		= PAGE(1) | 2,
	BULQOFF		= PAGE(1) | 3,
	BULIDAC		= PAGE(1) | 5,
	BULQDAC		= PAGE(1) | 4,
	BULGCAL		= PAGE(1) | 14,
	BULDATA1	= PAGE(0) | 3,	/* 16 words */
	BBCTRL		= PAGE(1) | 6,
	/* Voiceband Codec (VBC) Registers */
	VBCTRL1		= PAGE(1) | 8,
	VBCTRL2		= PAGE(1) | 11,
	VBPOP		= PAGE(1) | 10,
	VBUCTRL		= PAGE(1) | 7,
	VBDCTRL		= PAGE(0) | 6,
};
#define BULDATA2	BULDATA1

enum togbr2_bits {
	TOGBR2_KEEPR	= (1 << 0),	/* Clear KEEPON bit */
	TOGBR2_KEEPS	= (1 << 1),	/* Set KEEPON bit */
	TOGBR2_ACTR	= (1 << 2),	/* Dectivate MCLK */
	TOGBR2_ACTS	= (1 << 3),	/* Activate MCLK */
	TOGBR2_IBUFPTR1	= (1 << 4),	/* Initialize pointer of burst buffer 1 */
	TOGBR2_IBUFPTR2	= (1 << 5),	/* Initialize pointer of burst buffer 2 */
	TOGBR2_IAPCPTR	= (1 << 6),	/* Initialize pointer of APC RAM */
};

enum twl3025_unit {
	TWL3025_UNIT_AFC,
	TWL3025_UNIT_MAD,
	TWL3025_UNIT_ADA,
	TWL3025_UNIT_VDL,
	TWL3025_UNIT_VUL,
};

void twl3025_init(void);
void twl3025_reg_write(uint8_t reg, uint16_t data);
uint16_t twl3025_reg_read(uint8_t reg);

void twl3025_power_off(void);

void twl3025_clk13m(int enable);

void twl3025_unit_enable(enum twl3025_unit unit, int on);

enum twl3025_tsp_bits {
	BULON		= 0x80,
	BULCAL		= 0x40,
	BULENA		= 0x20,
	BDLON		= 0x10,
	BDLCAL		= 0x08,
	BDLENA		= 0x04,
	STARTADC	= 0x02,
};

/* Enqueue a TSP signal change via the TPU */
void twl3025_tsp_write(uint8_t data);

/* Enqueue a series of TSP commands in the TPU to (de)activate the downlink path */
void twl3025_downlink(int on, int16_t at);

/* Enqueue a series of TSP commands in the TPU to (de)activate the uplink path */
void twl3025_uplink(int on, int16_t at);

/* Update the AFC DAC value */
void twl3025_afc_set(int16_t val);

/* Get the AFC DAC value */
int16_t twl3025_afc_get(void);

/* Get the AFC DAC output value */
uint8_t twl3025_afcout_get(void);

/* Force a certain static AFC DAC output value */
void twl3025_afcout_set(uint8_t val);

#endif
