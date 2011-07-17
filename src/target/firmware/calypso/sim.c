/* Driver for Simcard Controller inside TI Calypso/Iota */

/* (C) 2010 by Philipp Fabian Benedikt Maier <philipp-maier@runningserver.com>
 * (C) 2011 by Andreas Eversberg <jolly@eversberg.eu>
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

/* Uncomment to debug sim */
/* #define DEBUG */

#include <stdint.h>
#include <stdio.h>

#include <debug.h>
#include <memory.h>
#include <string.h>
#include <delay.h>
#include <osmocom/core/msgb.h>
#include <layer1/l23_api.h>
#include <abb/twl3025.h>
#include <calypso/sim.h>
#include <calypso/irq.h>

#include <l1ctl_proto.h>

#define SIM_CLASS		0xA0
	/* Class that contains the following instructions */
#define SIM_GET_RESPONSE	0xC0
	/* Get the response of a command from the card */
#define SIM_READ_BINARY		0xB0	/* Read file in binary mode */
#define SIM_READ_RECORD		0xB2	/* Read record in binary mode */

enum {
	SIM_STATE_IDLE,
	SIM_STATE_TX_HEADER,
	SIM_STATE_RX_STATUS,
	SIM_STATE_RX_ACK,
	SIM_STATE_RX_ACK_DATA,
	SIM_STATE_TX_DATA,
};

#define L3_MSG_HEAD 4

static uint8_t sim_data[256]; /* buffer for SIM command */
static volatile uint16_t sim_len = 0; /* lenght of data in sim_data[] */
static volatile uint8_t sim_state = SIM_STATE_IDLE;
	/* current state of SIM process */
static volatile uint8_t sim_ignore_waiting_char = 0;
	/* signal ignoring of NULL procedure byte */
static volatile int sim_rx_character_count = 0;
	/* How many bytes have been received by calypso_sim_receive() */
static volatile int sim_rx_max_character_count = 0;
	/* How many bytes have been received by calypso_sim_receive() */
static volatile int sim_tx_character_count = 0;
	/* How many bytes have been transmitted by calypso_sim_transmit() */
static volatile int sim_tx_character_length = 0;
	/* How many bytes have to be transmitted by calypso_sim_transmit() */
static uint8_t *rx_buffer = 0;
	/* RX-Buffer that is issued by calypso_sim_receive() */
static uint8_t *tx_buffer = 0;
	/* TX-Buffer that is issued by calypso_sim_transmit() */
static volatile int rxDoneFlag = 0;
	/* Used for rx synchronization instead of a semaphore in calypso_sim_receive() */
static volatile int txDoneFlag = 0;
	/* Used for rx synchronization instead of a semaphore in calypso_sim_transmit() */

/* Display Register dump */
void calypso_sim_regdump(void)
{
#ifdef DEBUG
	unsigned int regVal;

#define SIM_DEBUG_OUTPUTDELAY 200

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

/* Receive raw data through the sim interface */
int calypso_sim_receive(uint8_t *data, uint8_t len)
{
	printd("Triggering SIM reception\n");

	/* Prepare buffers and flags */
	rx_buffer = data;
	sim_rx_character_count = 0;
	rxDoneFlag = 0;
	sim_rx_max_character_count = len;

	/* Switch I/O direction to input */
	writew(readw(REG_SIM_CONF1) & ~REG_SIM_CONF1_CONFTXRX, REG_SIM_CONF1);

	/* Unmask the interrupts that are needed to perform this action */
	writew(~(REG_SIM_MASKIT_MASK_SIM_RX | REG_SIM_MASKIT_MASK_SIM_WT),
		REG_SIM_MASKIT);

	return 0;
}

/* Transmit raw data through the sim interface */
int calypso_sim_transmit(uint8_t *data, int length)
{
	printd("Triggering SIM transmission\n");

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

	return 0;
}


/* IRQ-Handler for simcard interface */
void sim_irq_handler(enum irq_nr irq)
{
	int regVal = readw(REG_SIM_IT);


	/* Display interrupt information */
	printd("SIM-ISR: ");

	if(regVal & REG_SIM_IT_SIM_NATR) {
		printd(" No answer to reset!\n");
	}

	/* Used by: calypso_sim_receive() to determine when the transmission
	 * is over
	 */
	if(regVal & REG_SIM_IT_SIM_WT) {
		printd(" Character underflow!\n");
		rxDoneFlag = 1;

	}

	if(regVal & REG_SIM_IT_SIM_OV) {
		printd(" Receive overflow!\n");
	}

	/* Used by: calypso_sim_transmit() to transmit the data */
	if(regVal & REG_SIM_IT_SIM_TX) {
		printd(" Waiting for transmit...\n");
		if(sim_tx_character_count >= sim_tx_character_length) {
			txDoneFlag = 1;
		} else {
			writew(*tx_buffer,REG_SIM_DTX);
			tx_buffer++;
			sim_tx_character_count++;

			/* its essential to immediately switch to RX after TX
			 * is done
			 */
			if(sim_tx_character_count >= sim_tx_character_length) {
				/* TODO: set a proper delay here, 4 is to
				   long if not debugging and no delay is too
				   short */
//				delay_ms(1);
				/* Switch I/O direction to input */
				writew(readw(REG_SIM_CONF1) &
					~REG_SIM_CONF1_CONFTXRX, REG_SIM_CONF1);
			}
		}
	}

	/* Used by: calypso_sim_receive() to receive the incoming data */
	if(regVal & REG_SIM_IT_SIM_RX) {
		uint8_t ch = (uint8_t) (readw(REG_SIM_DRX) & 0xFF);

		/* ignore NULL procedure byte */
		if(ch == 0x60 && sim_ignore_waiting_char) {
			printd(" 0x60 received...\n");
			return;
		}

		printd(" Waiting for read (%02X)...\n", ch);

		/* Increment character count - this is what
		 * calypso_sim_receive() hands back
		 */
		sim_rx_character_count++;

		/* Read byte from rx-fifo and write it to the issued buffer */
		*rx_buffer = ch;
		rx_buffer++;

		/* to maximise SIM access speed, stop waiting after
		   all the expected characters have been received. */
		if (sim_rx_max_character_count
		 && sim_rx_character_count >= sim_rx_max_character_count) {
			printd(" Max characters received!\n");
			rxDoneFlag = 1;
		}
	}
}

/* simm command from layer 23 */
void sim_apdu(uint16_t len, uint8_t *data)
{
	if (sim_state != SIM_STATE_IDLE) {
		puts("Sim reader currently busy...\n");
		return;
	}
	memcpy(sim_data, data, len);
	sim_len = len;
}

/* handling sim events */
void sim_handler(void)
{
	static struct msgb *msg;
	struct l1ctl_hdr *l1h;
	static uint8_t mode;
	static uint8_t *response;
	static uint16_t length;

	switch (sim_state) {
	case SIM_STATE_IDLE:
		if (!sim_len)
			break; /* wait for SIM command */
		/* check if instructions expects a response */
		if (/* GET RESPONSE needs SIM_APDU_GET */
		    (sim_len == 5 && sim_data[0] == SIM_CLASS &&
		     sim_data[1] == SIM_GET_RESPONSE && sim_data[2] == 0x00 &&
		     sim_data[3] == 0x00) ||
		    /* READ BINARY/RECORD needs SIM_APDU_GET */
		     (sim_len >= 5 && sim_data[0] == SIM_CLASS &&
		      (sim_data[1] == SIM_READ_BINARY ||
		       sim_data[1] == SIM_READ_RECORD)))
			mode = SIM_APDU_GET;
		else
			mode = SIM_APDU_PUT;

		length = sim_data[4];

		/* allocate space for expected response */
		msg = msgb_alloc_headroom(256, L3_MSG_HEAD
					+ sizeof(struct l1ctl_hdr), "l1ctl1");
		response = msgb_put(msg, length + 2 + 1);

		sim_state = SIM_STATE_TX_HEADER;

		/* send APDU header */
		calypso_sim_transmit(sim_data, 5);
		break;
	case SIM_STATE_TX_HEADER:
		if (!txDoneFlag)
			break; /* wait until header is transmitted */
		/* Disable all interrupt driven functions */
		writew(0xFF, REG_SIM_MASKIT);
		/* Case 1: No input, No Output */
		if (length == 0) {
			sim_state = SIM_STATE_RX_STATUS;
			calypso_sim_receive(response + 1, 2);
			break;
		}
		/* Case 2: No input / Output of known length */
		if (mode == SIM_APDU_PUT) {
			sim_state = SIM_STATE_RX_ACK;
			calypso_sim_receive(response, 1);
			break;
		/* Case 4: Input / No output */
		} else {
			sim_state = SIM_STATE_RX_ACK_DATA;
			calypso_sim_receive(response, length + 1 + 2);
		}
		break;
	case SIM_STATE_RX_STATUS:
		if (!rxDoneFlag)
			break; /* wait until data is received */
		/* Disable all interrupt driven functions */
		writew(0xFF, REG_SIM_MASKIT);
		/* disable special ignore case */
		sim_ignore_waiting_char = 0;
		/* wrong number of bytes received */
		if (sim_rx_character_count != 2) {
			puts("SIM: Failed to read status\n");
			goto error;
		}
		msgb_pull(msg, length + 1); /* pull up to status info */
		goto queue;
	case SIM_STATE_RX_ACK:
		if (!rxDoneFlag)
			break; /* wait until data is received */
		/* Disable all interrupt driven functions */
		writew(0xFF, REG_SIM_MASKIT);
		/* error received */
		if (sim_rx_character_count == 2) {
			puts("SIM: command failed\n");
			msgb_pull(msg, msg->len - 2);
			msg->data[0] = response[0];
			msg->data[1] = response[1];
			goto queue;
		}
		/* wrong number of bytes received */
		if (sim_rx_character_count != 1) {
			puts("SIM: ACK read failed\n");
			goto error;
		}
		if (response[0] != sim_data[1]) {
			puts("SIM: ACK does not match request\n");
			goto error;
		}
		sim_state = SIM_STATE_TX_DATA;
		calypso_sim_transmit(sim_data + 5, length);
		break;
	case SIM_STATE_TX_DATA:
		if (!txDoneFlag)
			break; /* wait until data is transmitted */
		/* Disable all interrupt driven functions */
		writew(0xFF, REG_SIM_MASKIT);
		/* Ignore waiting char for RUN GSM ALGORITHM */
		/* TODO: implement proper handling of the "Procedure Bytes"
		   than this is no longer needed */
		if(sim_data[1] == 0x88)
			sim_ignore_waiting_char = 1;
		sim_state = SIM_STATE_RX_STATUS;
		calypso_sim_receive(response + length + 1, 2);
		break;
	case SIM_STATE_RX_ACK_DATA:
		if (!rxDoneFlag)
			break; /* wait until data is received */
		/* Disable all interrupt driven functions */
		writew(0xFF, REG_SIM_MASKIT);
		/* error received */
		if (sim_rx_character_count == 2) {
			puts("SIM: command failed\n");
			msgb_pull(msg, msg->len - 2);
			msg->data[0] = response[0];
			msg->data[1] = response[1];
			goto queue;
		}
		/* wrong number of bytes received */
		if (sim_rx_character_count != length + 1 + 2) {
			puts("SIM: Failed to read data\n");
			goto error;
		}
		msgb_pull(msg, 1); /* pull ACK byte */
		goto queue;
	}

	return;

error:
	msgb_pull(msg, msg->len - 2);
	msg->data[0] = 0;
	msg->data[1] = 0;
queue:
	printf("SIM Response (%d): %s\n", msg->len,
		osmo_hexdump(msg->data, msg->len));
	l1h = (struct l1ctl_hdr *) msgb_push(msg, sizeof(*l1h));
	l1h->msg_type = L1CTL_SIM_CONF;
	l1h->flags = 0;
	msg->l1h = (uint8_t *)l1h;
	l1_queue_for_l2(msg);
	/* go IDLE */
	sim_state = SIM_STATE_IDLE;
	sim_len = 0;

	return;
}

/* Initialize simcard interface */
void calypso_sim_init(void)
{
	/* Register IRQ handler and turn interrupts on */
	printd("SIM: Registering interrupt handler for simcard-interface\n");

	irq_register_handler(IRQ_SIMCARD, &sim_irq_handler);

#if 1
	irq_config(IRQ_SIMCARD, 0, 0, 0xff);
#else
	irq_config(IRQ_SIMCARD, 0, 0, 1);
#endif

	irq_enable(IRQ_SIMCARD);
}

/* Apply power to the simcard (use nullpointer to ignore atr) */
int calypso_sim_powerup(uint8_t *atr)
{
	/* Enable level shifters and voltage regulator */
#if 1  // 2.9V
	twl3025_reg_write(VRPCSIM, VRPCSIM_SIMLEN | VRPCSIM_RSIMEN
					| VRPCSIM_SIMSEL);
#else // 1.8V
	twl3025_reg_write(VRPCSIM, VRPCSIM_SIMLEN | VRPCSIM_RSIMEN);
#endif
	printd(" * Power enabled!\n");
	delay_ms(SIM_OPERATION_DELAY);

	/* Enable clock */
	writew(REG_SIM_CMD_MODULE_CLK_EN | REG_SIM_CMD_CMDSTART, REG_SIM_CMD);
	printd(" * Clock enabled!\n");
	delay_ms(SIM_OPERATION_DELAY);

	/* Release reset */
	writew(readw(REG_SIM_CONF1) | REG_SIM_CONF1_CONFBYPASS
				| REG_SIM_CONF1_CONFSRSTLEV
				| REG_SIM_CONF1_CONFSVCCLEV, REG_SIM_CONF1);
	printd(" * Reset released!\n");

	/* Catch ATR */
	if(atr != 0) {
		calypso_sim_receive(atr, 0);
		while (!rxDoneFlag)
			;
	}

	return 0;
}


/* Powerdown simcard */
void calypso_sim_powerdown(void)
{
	writew(readw(REG_SIM_CONF1) & ~REG_SIM_CONF1_CONFBYPASS, REG_SIM_CONF1);
	printd(" * Reset pulled down!\n");
	delay_ms(SIM_OPERATION_DELAY);

	writew(REG_SIM_CMD_MODULE_CLK_EN | REG_SIM_CMD_CMDSTOP, REG_SIM_CMD);
	printd(" * Clock disabled!\n");
	delay_ms(SIM_OPERATION_DELAY);

	writew(0, REG_SIM_CMD);
	printd(" * Module disabled!\n");
	delay_ms(SIM_OPERATION_DELAY);

	/* Disable level shifters and voltage regulator */
	twl3025_reg_write(VRPCSIM, 0);
	printd(" * Power disabled!\n");
	delay_ms(SIM_OPERATION_DELAY);

	return;
}

/* reset the simcard (see note 1) */
int calypso_sim_reset(uint8_t *atr)
{

	/* Pull reset down */
	writew(readw(REG_SIM_CONF1) & ~REG_SIM_CONF1_CONFSRSTLEV,
		REG_SIM_CONF1);
	printd(" * Reset pulled down!\n");

	delay_ms(SIM_OPERATION_DELAY);

	/* Pull reset down */
	writew(readw(REG_SIM_CONF1) | REG_SIM_CONF1_CONFSRSTLEV, REG_SIM_CONF1);
	printd(" * Reset released!\n");

	/* Catch ATR */
	if(atr != 0) {
		calypso_sim_receive(atr, 0);
		while (!rxDoneFlag)
			;
	}

	return 0;
}

