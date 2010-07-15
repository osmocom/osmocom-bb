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

#include <stdint.h>
#include <stdio.h>

#include <debug.h>
#include <memory.h>
#include <abb/twl3025.h>
#include <calypso/sim.h>
#include <calypso/irq.h>

static int sim_rx_character_count = 0;	/* How many bytes have been received by calypso_sim_receive() */
static int sim_tx_character_count = 0;	/* How many bytes have been transmitted by calypso_sim_transmit() */
static int sim_tx_character_length = 0;	/* How many bytes have to be transmitted by calypso_sim_transmit() */
static uint8_t *rx_buffer = 0;		/* RX-Buffer that is issued by calypso_sim_receive() */
static uint8_t *tx_buffer = 0;		/* TX-Buffer that is issued by calypso_sim_transmit() */
volatile static int rxDoneFlag = 0;	/* Used for rx syncronization instead of a semaphore in calypso_sim_receive() */
volatile static int txDoneFlag = 0;	/* Used for rx syncronization instead of a semaphore in calypso_sim_transmit() */

/* Display Register dump */
void calypso_sim_regdump(void)
{
#if (SIM_DEBUG == 1)
	unsigned int regVal;


	  puts("\n\n\n");
	  puts("====================== CALYPSO SIM REGISTER DUMP =====================\n");
  	  puts("Reg_sim_cmd register (R/W) - FFFE:0000\n");


	regVal = readw(REG_SIM_CMD);
	printf("  |-REG_SIM_CMD = %04x\n", readw(REG_SIM_CMD));

	if(regVal & REG_SIM_CMD_CMDCARDRST)
		puts("  |  |-REG_SIM_CMD_CMDCARDRST = 1 ==> SIM card reset sequence enabled.\n");
	else
		puts("  |  |-REG_SIM_CMD_CMDCARDRST = 0 ==> SIM card reset sequence disabled.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_CMD_CMDIFRST)
		puts("  |  |-REG_SIM_CMD_CMDIFRST = 1\n");
	else
		puts("  |  |-REG_SIM_CMD_CMDIFRST = 0\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_CMD_CMDSTOP)
		puts("  |  |-REG_SIM_CMD_CMDSTOP = 1\n");
	else
		puts("  |  |-REG_SIM_CMD_CMDSTOP = 0\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_CMD_CMDSTART)
		puts("  |  |-REG_SIM_CMD_CMDSTART = 1 ==> SIM card start procedure active.\n");
	else
		puts("  |  |-REG_SIM_CMD_CMDSTART = 0\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_CMD_CMDSTART)
		puts("  |  |-REG_SIM_CMD_MODULE_CLK_EN = 1 ==> Clock of the module enabled.\n");
	else
		puts("  |  |-REG_SIM_CMD_MODULE_CLK_EN = 0 ==> Clock of the module disabled.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	regVal = readw(REG_SIM_STAT);
	printf("  |-REG_SIM_STAT = %04x\n", regVal);
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_STAT_STATNOCARD)
		puts("  |  |-REG_SIM_STAT_STATNOCARD = 1 ==> No card!\n");
	else
		puts("  |  |-REG_SIM_STAT_STATNOCARD = 0 ==> Card detected!\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_STAT_STATTXPAR)
		puts("  |  |-REG_SIM_STAT_STATTXPAR = 1 ==> Parity ok!\n");
	else
		puts("  |  |-REG_SIM_STAT_STATTXPAR = 0 ==> Parity error!\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_STAT_STATFIFOFULL)
		puts("  |  |-REG_SIM_STAT_STATFIFOFULL = 1 ==> Fifo full!\n");
	else
		puts("  |  |-REG_SIM_STAT_STATFIFOFULL = 0\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_STAT_STATFIFOEMPTY)
		puts("  |  |-REG_SIM_STAT_STATFIFOEMPTY = 1 ==> Fifo empty!\n");
	else
		puts("  |  |-REG_SIM_STAT_STATFIFOEMPTY = 0\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	regVal = readw(REG_SIM_CONF1);
	printf("  |-REG_SIM_CONF1 = %04x\n", regVal);
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_CONF1_CONFCHKPAR)
		puts("  |  |-REG_SIM_CONF1_CONFCHKPAR = 1 ==> Parity check on reception enabled.\n");
	else
		puts("  |  |-REG_SIM_CONF1_CONFCHKPAR = 0 ==> Parity check on reception disabled.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_CONF1_CONFCODCONV)
		puts("  |  |-REG_SIM_CONF1_CONFCODCONV = 1 ==> Coding convention is inverse.\n");
	else
		puts("  |  |-REG_SIM_CONF1_CONFCODCONV = 0 ==> Coding convention is direct (normal).\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_CONF1_CONFTXRX)
		puts("  |  |-REG_SIM_CONF1_CONFTXRX = 1 ==> SIO line direction is in transmit mode.\n");
	else
		puts("  |  |-REG_SIM_CONF1_CONFTXRX = 0 ==> SIO line direction is in receive mode.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_CONF1_CONFSCLKEN)
		puts("  |  |-REG_SIM_CONF1_CONFSCLKEN = 1 ==> SIM clock in normal mode.\n");
	else
		puts("  |  |-REG_SIM_CONF1_CONFSCLKEN = 0 ==> SIM clock in standby mode.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_CONF1_reserved)
		puts("  |  |-REG_SIM_CONF1_reserved = 1 ==> ETU period is 4*1/Fsclk.\n");
	else
		puts("  |  |-REG_SIM_CONF1_reserved = 0 ==> ETU period is CONFETUPERIOD.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_CONF1_CONFSCLKDIV)
		puts("  |  |-REG_SIM_CONF1_CONFSCLKDIV = 1 ==> SIM clock frequency is 13/8 Mhz.\n");
	else
		puts("  |  |-REG_SIM_CONF1_CONFSCLKDIV = 0 ==> SIM clock frequency is 13/4 Mhz.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);
    
	if(regVal & REG_SIM_CONF1_CONFSCLKLEV)
		puts("  |  |-REG_SIM_CONF1_CONFSCLKLEV = 1 ==> SIM clock idle level is high.\n");
	else
		puts("  |  |-REG_SIM_CONF1_CONFSCLKLEV = 0 ==> SIM clock idle level is low.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);
              
	if(regVal & REG_SIM_CONF1_CONFETUPERIOD)
		puts("  |  |-REG_SIM_CONF1_CONFETUPERIOD = 1 ==> ETU period is 512/8*1/Fsclk.\n");
	else
		puts("  |  |-REG_SIM_CONF1_CONFETUPERIOD = 0 ==> ETU period is 372/8*1/Fsclk.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);
	
	if(regVal & REG_SIM_CONF1_CONFBYPASS)
		puts("  |  |-REG_SIM_CONF1_CONFBYPASS = 1 ==> Hardware timers and start and stop sequences are bypassed.\n");
	else
		puts("  |  |-REG_SIM_CONF1_CONFBYPASS = 0 ==> Hardware timers and start and stop sequences are normal.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);
          
	if(regVal & REG_SIM_CONF1_CONFSVCCLEV)
		puts("  |  |-REG_SIM_CONF1_CONFSVCCLEV = 1 ==> SVCC Level is high (Only valid when CONFBYPASS = 1).\n");
	else
		puts("  |  |-REG_SIM_CONF1_CONFSVCCLEV = 0 ==> SVCC Level is low (Only valid when CONFBYPASS = 1).\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_CONF1_CONFSRSTLEV)
		puts("  |  |-REG_SIM_CONF1_CONFSRSTLEV = 1 ==> SRST Level is high (Only valid when CONFBYPASS = 1).\n");
	else
		puts("  |  |-REG_SIM_CONF1_CONFSRSTLEV = 0 ==> SRST Level is low (Only valid when CONFBYPASS = 1).\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

 	printf("  |  |-REG_SIM_CONF1_CONFTRIG = 0x%x (FIFO trigger level)\n",(regVal >> REG_SIM_CONF1_CONFTRIG) & REG_SIM_CONF1_CONFTRIG_MASK);
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_CONF1_CONFSIOLOW)
		puts("  |  |-REG_SIM_CONF1_CONFSIOLOW = 1 ==> I/O is forced to low.\n");
	else
		puts("  |  |-REG_SIM_CONF1_CONFSIOLOW = 0\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	regVal = readw(REG_SIM_CONF2);
	printf("  |-REG_SIM_CONF2 = %04x\n", regVal);
 	printf("  |  |-REG_SIM_CONF2_CONFTFSIM = 0x%x (time delay for filtering of SIM_CD)\n",(regVal >> REG_SIM_CONF2_CONFTFSIM) & REG_SIM_CONF2_CONFTFSIM_MASK);
 	printf("  |  |-REG_SIM_CONF2_CONFTDSIM = 0x%x (time delay for contact activation/deactivation)\n",(regVal >> REG_SIM_CONF2_CONFTDSIM) & REG_SIM_CONF2_CONFTDSIM_MASK);
 	printf("  |  |-REG_SIM_CONF2_CONFWAITI = 0x%x (CONFWAITI overflow wait time between two received chars)\n",(regVal >> REG_SIM_CONF2_CONFWAITI) & REG_SIM_CONF2_CONFWAITI_MASK);
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	regVal = readw(REG_SIM_IT);
	printf("  |-REG_SIM_IT = %04x\n", regVal);
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_IT_SIM_NATR)
		puts("  |  |-REG_SIM_IT_SIM_NATR = 1 ==> No answer to reset!\n");
	else
		puts("  |  |-REG_SIM_IT_SIM_NATR = 0 ==> On read access to REG_SIM_IT.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_IT_SIM_WT)
		puts("  |  |-REG_SIM_IT_SIM_WT = 1 ==> Character underflow!\n");
	else
		puts("  |  |-REG_SIM_IT_SIM_WT = 0 ==> On read access to REG_SIM_IT.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_IT_SIM_OV)
		puts("  |  |-REG_SIM_IT_SIM_OV = 1 ==> Receive overflow!\n");
	else
		puts("  |  |-REG_SIM_IT_SIM_OV = 0 ==> On read access to REG_SIM_IT.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_IT_SIM_TX)
		puts("  |  |-REG_SIM_IT_SIM_TX = 1 ==> Waiting for character to transmit...\n");
	else
	{
		puts("  |  |-REG_SIM_IT_SIM_TX = 0 ==> On write access to REG_SIM_DTX or on switching\n");
		puts("  |  |                           from transmit to receive mode (CONFTXRX bit)\n");
	}
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_IT_SIM_RX)
		puts("  |  |-REG_SIM_IT_SIM_RX = 1 ==> Waiting characters to be read...\n");
	else
		puts("  |  |-REG_SIM_IT_SIM_RX = 0 ==> On read access to REG_SIM_DRX.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	regVal = readw(REG_SIM_DRX);
	printf("  |-REG_SIM_DRX = %04x\n", regVal);
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

 	printf("  |  |-REG_SIM_DRX_SIM_DRX = 0x%x (next data byte in FIFO available for reading)\n",(regVal >> REG_SIM_DRX_SIM_DRX) & REG_SIM_DRX_SIM_DRX_MASK);
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_DRX_STATRXPAR)
		puts("  |  |-REG_SIM_DRX_STATRXPAR = 1 ==> Parity Ok.\n");
	else
		puts("  |  |-REG_SIM_DRX_STATRXPAR = 0 ==> Parity error!\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	regVal = readw(REG_SIM_DTX);
	printf("  |-REG_SIM_DTX = %02x (next data byte to be transmitted)\n", regVal);
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	regVal = readw(REG_SIM_MASKIT);
	printf("  |-REG_SIM_MASKIT = %04x\n", regVal);
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_MASKIT_MASK_SIM_NATR)
		puts("  |  |-REG_SIM_MASKIT_MASK_SIM_NATR = 1 ==> No-answer-to-reset interrupt is masked.\n");
	else
		puts("  |  |-REG_SIM_MASKIT_MASK_SIM_NATR = 0 ==> No-answer-to-reset interrupt is unmasked.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_MASKIT_MASK_SIM_WT)
		puts("  |  |-REG_SIM_MASKIT_MASK_SIM_WT = 1 ==> Character wait-time overflow interrupt is masked.\n");
	else
		puts("  |  |-REG_SIM_MASKIT_MASK_SIM_WT = 0 ==> Character wait-time overflow interrupt is unmasked.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_MASKIT_MASK_SIM_OV)
		puts("  |  |-REG_SIM_MASKIT_MASK_SIM_OV = 1 ==> Receive overflow interrupt is masked.\n");
	else
		puts("  |  |-REG_SIM_MASKIT_MASK_SIM_OV = 0 ==> Receive overflow interrupt is unmasked.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_MASKIT_MASK_SIM_TX)
		puts("  |  |-REG_SIM_MASKIT_MASK_SIM_TX = 1 ==> Waiting characters to be transmit interrupt is masked.\n");
	else
		puts("  |  |-REG_SIM_MASKIT_MASK_SIM_TX = 0 ==> Waiting characters to be transmit interrupt is unmasked.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_MASKIT_MASK_SIM_RX)
		puts("  |  |-REG_SIM_MASKIT_MASK_SIM_RX = 1 ==> Waiting characters to be read interrupt is masked.\n");
	else
		puts("  |  |-REG_SIM_MASKIT_MASK_SIM_RX = 0 ==> Waiting characters to be read interrupt is unmasked.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	if(regVal & REG_SIM_MASKIT_MASK_SIM_CD)
		puts("  |  |-REG_SIM_MASKIT_MASK_SIM_CD = 1 ==> SIM card insertion/extraction interrupt is masked.\n");
	else
		puts("  |  |-REG_SIM_MASKIT_MASK_SIM_CD = 0 ==> SIM card insertion/extraction interrupt is unmasked.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);

	regVal = REG_SIM_IT_CD;
	printf("  |-REG_SIM_IT_CD = %04x\n", regVal);
	if(regVal & REG_SIM_IT_CD_IT_CD)
		puts("     |-REG_SIM_IT_CD_IT_CD = 1 ==> SIM card insertion/extraction interrupt is masked.\n");
	else
		puts("     |-REG_SIM_IT_CD_IT_CD = 0 ==> SIM card insertion/extraction interrupt is unmasked.\n");
	delay_ms(SIM_DEBUG_OUTPUTDELAY);
#endif
	return;
}

/* Apply power to the simcard (use nullpointer to ignore atr) */
int calypso_sim_powerup(uint8_t *atr)
{
	/* Enable level shifters and voltage regulator */
	twl3025_reg_write(VRPCSIM, VRPCSIM_SIMLEN | VRPCSIM_RSIMEN | VRPCSIM_SIMSEL);
#if (SIM_DEBUG == 1)
	puts(" * Power enabled!\n");
#endif
	delay_ms(SIM_OPERATION_DELAY);

	/* Enable clock */
	writew(REG_SIM_CMD_MODULE_CLK_EN | REG_SIM_CMD_CMDSTART, REG_SIM_CMD);
#if (SIM_DEBUG == 1)
	puts(" * Clock enabled!\n");
#endif
	delay_ms(SIM_OPERATION_DELAY);

	/* Release reset */
	writew(readw(REG_SIM_CONF1) | REG_SIM_CONF1_CONFBYPASS | REG_SIM_CONF1_CONFSRSTLEV | REG_SIM_CONF1_CONFSVCCLEV, REG_SIM_CONF1);
#if (SIM_DEBUG == 1)
	puts(" * Reset released!\n");
#endif

	/* Catch ATR */
	if(atr != 0)
		return calypso_sim_receive(atr);
	else
		return 0;
}


/* Powerdown simcard */
void calypso_sim_powerdown(void)
{
	writew(readw(REG_SIM_CONF1) & ~REG_SIM_CONF1_CONFBYPASS, REG_SIM_CONF1);
#if (SIM_DEBUG == 1)
	puts(" * Reset pulled down!\n");
#endif
	delay_ms(SIM_OPERATION_DELAY);

	writew(REG_SIM_CMD_MODULE_CLK_EN | REG_SIM_CMD_CMDSTOP, REG_SIM_CMD);
#if (SIM_DEBUG == 1)
	puts(" * Clock disabled!\n");
#endif
	delay_ms(SIM_OPERATION_DELAY);

	writew(0, REG_SIM_CMD);
#if (SIM_DEBUG == 1)
	puts(" * Module disabled!\n");
#endif
	delay_ms(SIM_OPERATION_DELAY);

	/* Disable level shifters and voltage regulator */
	twl3025_reg_write(VRPCSIM, 0);
#if (SIM_DEBUG == 1)
	puts(" * Power disabled!\n");
#endif
	delay_ms(SIM_OPERATION_DELAY);

	return;
}

/* reset the simcard (see note 1) */
int calypso_sim_reset(uint8_t *atr)
{

	/* Pull reset down */
	writew(readw(REG_SIM_CONF1) & ~REG_SIM_CONF1_CONFSRSTLEV , REG_SIM_CONF1);
#if (SIM_DEBUG == 1)
	puts(" * Reset pulled down!\n");
#endif

	delay_ms(SIM_OPERATION_DELAY);

	/* Pull reset down */
	writew(readw(REG_SIM_CONF1) | REG_SIM_CONF1_CONFSRSTLEV , REG_SIM_CONF1);
#if (SIM_DEBUG == 1)
	puts(" * Reset released!\n");
#endif

	/* Catch ATR */
	if(atr != 0)
		return calypso_sim_receive(atr);
	else
		return 0;
}

/* Receive raw data through the sim interface */
int calypso_sim_receive(uint8_t *data)
{
	/* Prepare buffers and flags */
	rx_buffer = data;
	sim_rx_character_count = 0;
	rxDoneFlag = 0;

	/* Switch I/O direction to input */
	writew(readw(REG_SIM_CONF1) & ~REG_SIM_CONF1_CONFTXRX, REG_SIM_CONF1);

	/* Unmask the interrupts that are needed to perform this action */
	writew(~(REG_SIM_MASKIT_MASK_SIM_RX | REG_SIM_MASKIT_MASK_SIM_WT), REG_SIM_MASKIT);

	/* Wait till rxDoneFlag is set */
	while(rxDoneFlag == 0);

	/* Disable all interrupt driven functions by masking all interrupts */
	writew(0xFF, REG_SIM_MASKIT);

	/* Hand back the number of bytes received */
	return sim_rx_character_count;
	
	return;
}

/* Transmit raw data through the sim interface */
int calypso_sim_transmit(uint8_t *data, int length)
{
	/* Prepare buffers and flags */
	tx_buffer = data;
	sim_tx_character_count = 0;
	txDoneFlag = 0;
	sim_tx_character_length = length;

	/* Switch I/O direction to output */
	writew(readw(REG_SIM_CONF1) | REG_SIM_CONF1_CONFTXRX, REG_SIM_CONF1);

	/* Unmask the interrupts that are needed to perform this action */
	writew(~(REG_SIM_MASKIT_MASK_SIM_TX), REG_SIM_MASKIT);

	/* Transmit the first byte manually to start the interrupt cascade */
	writew(*tx_buffer,REG_SIM_DTX);
	tx_buffer++;
	sim_tx_character_count++;

	/* Wait till rxDoneFlag is set */
	while(txDoneFlag == 0);

	/* Disable all interrupt driven functions by masking all interrupts */
	writew(0xFF, REG_SIM_MASKIT);

	return 0;
}


/* IRQ-Handler for simcard interface */
void sim_irq_handler(enum irq_nr irq)
{
	int regVal = readw(REG_SIM_IT);


	/* Display interrupt information */
#if (SIM_DEBUG == 1)
	puts("SIM-ISR: Interrupt caught: ");
#endif
	if(regVal & REG_SIM_IT_SIM_NATR)
	{
#if (SIM_DEBUG == 1)
		puts(" No answer to reset!\n");
#endif
	}

	/* Used by: calypso_sim_receive() to determine when the transmission is over */
	if(regVal & REG_SIM_IT_SIM_WT)
	{
#if (SIM_DEBUG == 1)
		puts(" Character underflow!\n");
#endif
		rxDoneFlag = 1;
	}

	if(regVal & REG_SIM_IT_SIM_OV)
	{
#if (SIM_DEBUG == 1)
		puts(" Receive overflow!\n");
#endif
	}

	/* Used by: calypso_sim_transmit() to transmit the data */
	if(regVal & REG_SIM_IT_SIM_TX)
	{
#if (SIM_DEBUG == 1)
		puts(" Waiting for character to transmit...\n");
#endif
		if(sim_tx_character_count >= sim_tx_character_length)
			txDoneFlag = 1;
		else
		{
			writew(*tx_buffer,REG_SIM_DTX);
			tx_buffer++;
			sim_tx_character_count++;
		}
	}

	/* Used by: calypso_sim_receive() to receive the incoming data */
	if(regVal & REG_SIM_IT_SIM_RX)
	{
#if (SIM_DEBUG == 1)
		puts(" Waiting characters to be read...\n");
#endif
		/* Increment character count - this is what calypso_sim_receive() hands back */
		sim_rx_character_count++;

		/* Read byte from rx-fifo and write it to the issued buffer */
		*rx_buffer = (uint8_t) (readw(REG_SIM_DRX) & 0xFF);
		rx_buffer++;
	}
}

/* Transceive T0 Apdu to sim acording to GSM 11.11 Page 34 */
int calypso_sim_transceive(uint8_t cla, 		/* Class (in GSM context mostly 0xA0 */
				uint8_t ins,		/* Instruction */
				uint8_t p1,		/* First parameter */
				uint8_t p2,		/* Second parameter */
				uint8_t p3le,		/* Length of the data that should be transceived */
				uint8_t *data,		/* Data payload */
				uint8_t *status,	/* Status word (2 byte array, see note 1) */
				uint8_t mode)		/* Mode of operation: 1=GET, 0=PUT */

				/* Note 1: You can use a null-pointer (0) if you are not interested in 
					   the status word */
{
	uint8_t transmissionBuffer[256];
	uint8_t numberOfReceivedBytes;

#if (SIM_DEBUG == 1)
	printf("SIM-T0: Transceiving APDU-Header: (%02x %02x %02x %02x %02x)\n",cla,ins,p1,p2,p3le);
#endif

	/* Transmit APDU header */
	memset(transmissionBuffer,0,sizeof(transmissionBuffer));
	transmissionBuffer[0] = cla;
	transmissionBuffer[1] = ins;
	transmissionBuffer[2] = p1;
	transmissionBuffer[3] = p2;
	transmissionBuffer[4] = p3le;
	calypso_sim_transmit(transmissionBuffer,5);

	/* Case 1: No input, No Output */
	if(p3le == 0)
	{
#if (SIM_DEBUG == 1)
		puts("SIM-T0: Case 1: No input, No Output (See also GSM 11.11 Page 34)\n");
#endif
		numberOfReceivedBytes = calypso_sim_receive(transmissionBuffer);
		
		if(numberOfReceivedBytes == 2)
		{
#if (SIM_DEBUG == 1)
			printf("SIM-T0: Status-word received: %02x %02x\n", transmissionBuffer[0], transmissionBuffer[1]);
#endif
			/* Hand back status word */
			if(status != 0)
			{
				status[0] = transmissionBuffer[0];
				status[1] = transmissionBuffer[1];
			}	

			return 0;
		}
		else
		{
#if (SIM_DEBUG == 1)
			puts("SIM-T0: T0 Protocol error -- aborting!\n");
#endif
			return -1;
		}
	}

	/* Case 2: No input / Output of known length */
	else if(mode == SIM_APDU_PUT)
	{
#if (SIM_DEBUG == 1)
		puts("SIM-T0: Case 2: No input / Output of known length (See also GSM 11.11 Page 34)\n");
#endif

		numberOfReceivedBytes = calypso_sim_receive(transmissionBuffer);

		/* Error situation: The card has aborted, sends no data but a status word */
		if(numberOfReceivedBytes == 2)
		{
#if (SIM_DEBUG == 1)
			printf("SIM-T0: Status-word received (ERROR): %02x %02x\n", transmissionBuffer[0], transmissionBuffer[1]);
#endif
			/* Hand back status word */
			if(status != 0)
			{
				status[0] = transmissionBuffer[0];
				status[1] = transmissionBuffer[1];
			}				

			return 0;
		}
		/* Acknoledge byte received */
		else if(numberOfReceivedBytes == 1)
		{
#if (SIM_DEBUG == 1)
			printf("SIM-T0: ACK received: %02x\n", transmissionBuffer[0]);
#endif
			/* Check if ACK is valid */
			if(transmissionBuffer[0] != ins)
			{
#if (SIM_DEBUG == 1)
				puts("SIM-T0: T0 Protocol error: Invalid ACK byte -- aborting!\n");
#endif				
				return -1;
			}

			/* Transmit body */
			calypso_sim_transmit(data,p3le);
			
			/* Receive status word */
			numberOfReceivedBytes = calypso_sim_receive(transmissionBuffer);

			/* Check status word */
			if(numberOfReceivedBytes == 2)
			{
#if (SIM_DEBUG == 1)
				printf("SIM-T0: Status-word received: %02x %02x\n", transmissionBuffer[0], transmissionBuffer[1]);
#endif

				/* Hand back status word */
				if(status != 0)
				{
					status[0] = transmissionBuffer[0];
					status[1] = transmissionBuffer[1];
				}

				return 0;
			}
			else
			{
#if (SIM_DEBUG == 1)
				puts("SIM-T0: T0 Protocol error: Missing or invalid status word -- aborting!\n");
#endif				
				return -1;
			}
		}
		else
		{
#if (SIM_DEBUG == 1)
			puts("SIM-T0: T0 Protocol error: Missing ACK byte -- aborting!\n");
#endif
			return -1;
		}
	}

	/* Case 4: Input / No output */
	else if(mode == SIM_APDU_GET)
	{
#if (SIM_DEBUG == 1)
		puts("SIM-T0: Case 4: Input / No output (See also GSM 11.11 Page 34)\n");
#endif

		numberOfReceivedBytes = calypso_sim_receive(data);

		/* Error situation: The card has aborted, sends no data but a status word */
		if(numberOfReceivedBytes == 2)
		{

#if (SIM_DEBUG == 1)
			printf("SIM-T0: Status-word received (ERROR): %02x %02x\n", data[0], data[1]);
#endif
			/* Hand back status word */
			if(status != 0)
			{
				status[0] = data[0];
				status[1] = data[1];
			}				

			return 0;
		}

		/* Data correctly received */
		else if(numberOfReceivedBytes == p3le + 1 + 2)
		{
#if (SIM_DEBUG == 1)
			printf("SIM-T0: ACK received: %02x\n", data[0]);
#endif
			/* Check if ACK is valid */
			if(data[0] != ins)
			{
#if (SIM_DEBUG == 1)
				puts("SIM-T0: T0 Protocol error: Invalid ACK byte -- aborting!\n");
#endif				
				return -1;
			}

#if (SIM_DEBUG == 1)
			printf("SIM-T0: Status-word received: %02x %02x\n", data[p3le + 1], data[p3le + 2]);
#endif
			/* Hand back status word */
			if(status != 0)
			{
				status[0] = data[p3le + 1];
				status[1] = data[p3le + 2];
			}				
	
			/* Move data one position left to cut away the ACK-Byte */
			memcpy(data,data+1,p3le);

			return 0;
		}
		else
		{
#if (SIM_DEBUG == 1)
			puts("SIM-T0: T0 Protocol error: Incorrect or missing answer -- aborting!\n");
#endif
			return -1;
		}
	}

	/* Should not happen, if it happens then the programmer has submitted invalid parameters! */
	else
	{
#if (SIM_DEBUG == 1)
		puts("SIM-T0: T0 Protocol error: Invalid case (program bug!) -- aborting!\n");
#endif
	}

	/* Note: The other cases are not implemented because they are already covered
                 by the CASE 1,2 and 4. */

	return 0;
}


/* Initialize simcard interface */
void calypso_sim_init(void)
{
	/* Register IRQ handler and turn interrupts on */
#if (SIM_DEBUG == 1)
	puts("SIM: Registering interrupt handler for simcard-interface\n");
#endif
	irq_register_handler(IRQ_SIMCARD, &sim_irq_handler);
	irq_config(IRQ_SIMCARD, 0, 0, 0xff);
	irq_enable(IRQ_SIMCARD);
}

