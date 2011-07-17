/* Driver for Simcard Controller inside TI Calypso/Iota */

/* (C) 2010 by Philipp Fabian Benedikt Maier <philipp-maier@runningserver.com>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef _CALYPSO_SIM_H
#define _CALYPSO_SIM_H

/* == REGISTERS IN THE IOTA BASEBAND == */

/* SimCard Control Register */
#define VRPCSIM_SIMLEN (1 << 3)		/* Enable level shifter */
#define VRPCSIM_SIMRSU (1 << 2)		/* voltage regulator output status */
#define VRPCSIM_RSIMEN (1 << 1)		/* Voltage regulator enable */
#define VRPCSIM_SIMSEL 1		/* Select the VRSIM output voltage 1=2.9V, 0=1.8V */



/* == REGISTERS IN THE CALYPSO CPU == */

/* Reg_sim_cmd register (R/W) - FFFE:0000 */
#define REG_SIM_CMD			0xFFFE0000	/* register address */
#define REG_SIM_CMD_CMDCARDRST		1		/* SIM card reset sequence */
#define REG_SIM_CMD_CMDIFRST 		(1 << 1)	/* SIM interface software reset */
#define REG_SIM_CMD_CMDSTOP		(1 << 2)	/* SIM card stop procedure */
#define REG_SIM_CMD_CMDSTART		(1 << 3)	/* SIM card start procedure */
#define REG_SIM_CMD_MODULE_CLK_EN	(1 << 4)	/* Clock of the module */

/* Reg_sim_stat register (R) - FFFE:0002 */
#define REG_SIM_STAT			0xFFFE0002	/* register address */
#define REG_SIM_STAT_STATNOCARD		1		/* card presence, 0 = no card, 1 = card detected */
#define REG_SIM_STAT_STATTXPAR		(1 << 1)	/* parity check for transmit byte, 0 = parity error, 1 = parity OK */
#define REG_SIM_STAT_STATFIFOFULL	(1 << 2)	/* FIFO content, 1 = FIFO full */ 
#define REG_SIM_STAT_STATFIFOEMPTY	(1 << 3)	/* FIFO content, 1 = FIFO empty */

/* Reg_sim_conf1 register (R/W) - FFFE:0004 */
#define REG_SIM_CONF1			0xFFFE0004	/* register address */
#define REG_SIM_CONF1_CONFCHKPAR	1		/* enable parity check on reception */
#define REG_SIM_CONF1_CONFCODCONV	(1 << 1)	/* coding convention: (TS character) */
#define REG_SIM_CONF1_CONFTXRX		(1 << 2)	/* SIO line direction */
#define REG_SIM_CONF1_CONFSCLKEN	(1 << 3)	/* SIM clock */
#define REG_SIM_CONF1_reserved		(1 << 4)	/* ETU period */
#define REG_SIM_CONF1_CONFSCLKDIV	(1 << 5)	/* SIM clock frequency */
#define REG_SIM_CONF1_CONFSCLKLEV	(1 << 6)	/* SIM clock idle level */
#define REG_SIM_CONF1_CONFETUPERIOD	(1 << 7)	/* ETU period */
#define REG_SIM_CONF1_CONFBYPASS	(1 << 8)	/* bypass hardware timers and start and stop sequences */
#define REG_SIM_CONF1_CONFSVCCLEV	(1 << 9)	/* logic level on SVCC (used if CONFBYPASS = 1) */
#define REG_SIM_CONF1_CONFSRSTLEV	(1 << 10)	/* logic level on SRST (used if CONFBYPASS = 1) */
#define REG_SIM_CONF1_CONFTRIG		11		/* FIFO trigger level */
#define REG_SIM_CONF1_CONFTRIG_0	(1 << 11)	
#define REG_SIM_CONF1_CONFTRIG_1	(1 << 12)	
#define REG_SIM_CONF1_CONFTRIG_2	(1 << 13)
#define REG_SIM_CONF1_CONFTRIG_3	(1 << 14)
#define REG_SIM_CONF1_CONFTRIG_MASK	0xF
#define REG_SIM_CONF1_CONFSIOLOW	(1 << 15)	/* SIO - 0 = no effect, 1 = force low */

/* Reg_sim_conf2 register (R/W) - FFFE:0006 */
#define REG_SIM_CONF2			0xFFFE0006	/* register address */
#define REG_SIM_CONF2_CONFTFSIM		0		/* time delay for filtering of SIM_CD */
#define REG_SIM_CONF2_CONFTFSIM_0	1		/* time-unit = 1024 * TCK13M (card extraction) */
#define REG_SIM_CONF2_CONFTFSIM_1	(1 << 1)	/* or */
#define REG_SIM_CONF2_CONFTFSIM_2	(1 << 2)	/* time-unit = 8192 * TCK13M (card insertion) */
#define REG_SIM_CONF2_CONFTFSIM_3	(1 << 3)
#define REG_SIM_CONF2_CONFTFSIM_MASK	0xF
#define REG_SIM_CONF2_CONFTDSIM		4		/* time delay for contact activation/deactivation */
#define REG_SIM_CONF2_CONFTDSIM_0	(1 << 4)	/* time unit = 8 * TCKETU */
#define REG_SIM_CONF2_CONFTDSIM_1	(1 << 5)
#define REG_SIM_CONF2_CONFTDSIM_2	(1 << 6)
#define REG_SIM_CONF2_CONFTDSIM_3	(1 << 7)
#define REG_SIM_CONF2_CONFTDSIM_MASK 	0xF
#define REG_SIM_CONF2_CONFWAITI		8		/* CONFWAITI overflow wait time between two received */
#define REG_SIM_CONF2_CONFWAITI_0	(1 << 8)	/* character time unit = 960 *D * TCKETU */
#define REG_SIM_CONF2_CONFWAITI_1	(1 << 9)	/* with D parameter = 1 or 8 (TA1 character) */
#define REG_SIM_CONF2_CONFWAITI_2	(1 << 10)
#define REG_SIM_CONF2_CONFWAITI_3	(1 << 11)
#define REG_SIM_CONF2_CONFWAITI_4	(1 << 12)
#define REG_SIM_CONF2_CONFWAITI_5	(1 << 13)
#define REG_SIM_CONF2_CONFWAITI_6	(1 << 14)
#define REG_SIM_CONF2_CONFWAITI_7	(1 << 15) 
#define REG_SIM_CONF2_CONFWAITI_MASK 	0xFF

/* Reg_sim_it register (R) - FFFE:0008 */
#define REG_SIM_IT			0xFFFE0008	/* register address */
#define REG_SIM_IT_SIM_NATR		1		/* 0 = on read access to REG_SIM_IT, 1 = no answer to reset */
#define REG_SIM_IT_SIM_WT		(1 << 1)	/* 0 = on read access to REG_SIM_IT, 1 = character underflow */
#define REG_SIM_IT_SIM_OV		(1 << 2)	/* 0 = on read access to REG_SIM_IT, 1 = receive overflow */
#define REG_SIM_IT_SIM_TX		(1 << 3)	/* 0 = on write access to REG_SIM_DTX or */
							/* on switching from transmit to receive, mode (CONFTXRX bit) */
							/* 1 = waiting for character to transmit */
#define REG_SIM_IT_SIM_RX		(1 << 4)	/* 0 = on read access to REG_SIM_DRX */
							/* 1 = waiting characters to be read */

/* Reg_sim_drx register (R) - FFFE:000A */
#define REG_SIM_DRX			0xFFFE000A 	/* register address */
#define REG_SIM_DRX_SIM_DRX		0		/* next data byte in FIFO available for reading */
#define REG_SIM_DRX_SIM_DRX_0		1
#define REG_SIM_DRX_SIM_DRX_1		(1 << 1)	
#define REG_SIM_DRX_SIM_DRX_2		(1 << 2)
#define REG_SIM_DRX_SIM_DRX_3		(1 << 3)
#define REG_SIM_DRX_SIM_DRX_4		(1 << 4)
#define REG_SIM_DRX_SIM_DRX_5		(1 << 5)
#define REG_SIM_DRX_SIM_DRX_6		(1 << 6)
#define REG_SIM_DRX_SIM_DRX_7		(1 << 7)
#define REG_SIM_DRX_SIM_DRX_MASK	0xFF
#define REG_SIM_DRX_STATRXPAR		(1 << 8)	/* parity-check for received byte */

/* Reg_sim_dtx register (R/W) - FFFE:000C */
#define REG_SIM_DTX 			0xFFFE000C	/* register address */
#define REG_SIM_DTX_SIM_DTX_0				/* next data byte to be transmitted */
#define REG_SIM_DTX_SIM_DTX_1
#define REG_SIM_DTX_SIM_DTX_2
#define REG_SIM_DTX_SIM_DTX_3
#define REG_SIM_DTX_SIM_DTX_4
#define REG_SIM_DTX_SIM_DTX_5
#define REG_SIM_DTX_SIM_DTX_6
#define REG_SIM_DTX_SIM_DTX_7

/* Reg_sim_maskit register (R/W) - FFFE:000E */
#define REG_SIM_MASKIT			0xFFFE000E 	/* register address */
#define REG_SIM_MASKIT_MASK_SIM_NATR	1		/* No-answer-to-reset interrupt */
#define REG_SIM_MASKIT_MASK_SIM_WT 	(1 << 1)	/* Character wait-time overflow interrupt */
#define REG_SIM_MASKIT_MASK_SIM_OV 	(1 << 2)	/* Receive overflow interrupt */
#define REG_SIM_MASKIT_MASK_SIM_TX 	(1 << 3)	/* Waiting character to transmit interrupt */
#define REG_SIM_MASKIT_MASK_SIM_RX 	(1 << 4)	/* Waiting characters to be read interrupt */
#define REG_SIM_MASKIT_MASK_SIM_CD 	(1 << 5)	/* SIM card insertion/extraction interrupt */

/* Reg_sim_it_cd register (R) - FFFE:0010 */
#define REG_SIM_IT_CD			0xFFFE0010	/* register address */
#define REG_SIM_IT_CD_IT_CD		1		/* 0 = on read access to REG_SIM_IT_CD, */ 
							/* 1 = SIM card insertion/extraction */


#define SIM_OPERATION_DELAY 100				/* Time between operations like reset, vcc apply ect... */ 


void calypso_sim_regdump(void);				/* Display Register dump */

int calypso_sim_powerup(uint8_t *atr);			/* Apply power to the simcard (see note 1) */
int calypso_sim_reset(uint8_t *atr);			/* reset the simcard (see note 1) */


void calypso_sim_powerdown(void);			/* Powerdown simcard */

/* APDU transmission modes */
#define SIM_APDU_PUT 0		/* Transmit a data body to the card */
#define SIM_APDU_GET 1		/* Fetch data from the card eg. GET RESOPNSE */


void calypso_sim_init(void);				/* Initialize simcard interface */

/* handling sim events */
void sim_handler(void);

/* simm command from layer 23 */
void sim_apdu(uint16_t len, uint8_t *data);


/* Known Bugs:
   1.) After powering down the simcard communication stops working
*/

#endif /* _CALYPSO_SIM_H */
