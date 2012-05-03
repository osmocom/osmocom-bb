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

/* available ADC inputs on IOTA */
enum twl3025_dac_inputs {/* === Signal ============================= */
	MADC_VBAT=0,	/* battery voltage / 4                      */
	MADC_VCHG=1,	/* charger voltage / 5                      */
	MADC_ICHG=2,	/* I-sense amp or CHGREG DAC output         */
	MADC_VBKP=3,	/* backup battery voltage / 4               */
	MADC_ADIN1=4,	/* VADCID, sense battery type, not used     */
	MADC_ADIN2=5,	/* Temperature sensor in Battery            */
	MADC_ADIN3=6,	/* Mode_detect: sense 2.5mm jack insertion  */
	MADC_ADIN4=7,	/* RITA: TEMP_SEN                           */
	MADC_NUM_CHANNELS=8
};

enum madcstat_reg_bits { /* monitoring ADC status register */
	ADCBUSY = 0x01  /* if set, a conversion is currently going on */
};

/* BCICTL1 register bits */
enum bcictl1_reg_bits {
	MESBAT	= 1<<0,	/* connect resistive divider for bat voltage */
	DACNBUF	= 1<<1,	/* bypass DAC buffer */
	THSENS0	= 1<<3,	/* thermal sensor bias current (ADIN2), bit 0 */
	THSENS1	= 1<<4,	/* "" bit 1 */
	THSENS2	= 1<<5,	/* "" bit 2 */
	THEN	= 1<<6,	/* enable thermal sensor bias current (ADIN1) */ 
	TYPEN	= 1<<7	/* enable bias current for battery type reading */
};

/* BCICTL1 register bits */
enum bcictl2_reg_bits {
	CHEN	= 1<<0,	/* enable charger */
	CHIV	= 1<<1,	/* 1=constant current, 0=constant voltage */
	CHBPASSPA=1<<2,	/* full charging of the battery during pulse radio */
	CLIB	= 1<<3,	/* calibrate I-to-V amp (short input pins) */
	CHDISPA	= 1<<4,	/* disabel charging during pulse radio (???) */
	LEDC	= 1<<5,	/* enable LED during charge */
	CGAIN4	= 1<<6,	/* if set, I-to-V amp gain is reduced from 10 to 4 */
	PREOFF	= 1<<7	/* disable battery precharge */
};

enum vrpcsts_reg_bits {
	ONBSTS = 1<<0,	/* button push switched on the mobile */
	ONRSTS = 1<<1,	/* RPWON terminal switched on the mobile */
	ITWSTS = 1<<2,	/* ITWAKEUP terminal switched on the mobile */
	CHGSTS = 1<<3,	/* plugging in charger has switched on the mobile */
	ONREFLT= 1<<4,	/* state of PWON terminal after debouncing */
	ONMRFLT= 1<<5,	/* state of RPWON terminal after debouncing */
	CHGPRES= 1<<6	/* charger is connected */
};

enum togbr2_bits {
	TOGBR2_KEEPR	= (1 << 0),	/* Clear KEEPON bit */
	TOGBR2_KEEPS	= (1 << 1),	/* Set KEEPON bit */
	TOGBR2_ACTR	= (1 << 2),	/* Dectivate MCLK */
	TOGBR2_ACTS	= (1 << 3),	/* Activate MCLK */
	TOGBR2_IBUFPTR1	= (1 << 4),	/* Initialize pointer of burst buffer 1 */
	TOGBR2_IBUFPTR2	= (1 << 5),	/* Initialize pointer of burst buffer 2 */
	TOGBR2_IAPCPTR	= (1 << 6),	/* Initialize pointer of APC RAM */
};

/* How a RAMP value is encoded */
#define ABB_RAMP_VAL(up, down)	( ((down & 0x1F) << 5) | (up & 0x1F) )

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

extern const uint16_t twl3025_default_ramp[16];

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
