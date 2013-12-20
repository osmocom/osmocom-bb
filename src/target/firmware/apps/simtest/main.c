/* SIM test application */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
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
#include <string.h>

#include <debug.h>
#include <memory.h>
#include <delay.h>
#include <rffe.h>
#include <keypad.h>
#include <board.h>
#include <abb/twl3025.h>
#include <rf/trf6151.h>
#include <calypso/clock.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/dsp.h>
#include <calypso/irq.h>
#include <calypso/misc.h>
#include <comm/sercomm.h>
#include <comm/timer.h>

#include <calypso/sim.h>

#define DEBUG

/* Dump bytes in hex on the console */
static void myHexdump(uint8_t *data, int len)
{
	int i;

	for(i=0;i<len;i++)
		printf("%02x ",data[i]);

	printf("(%i bytes)\n", len);

	return;
}

/* SIM instructions
   All instructions a standard sim card must feature: */
#define SIM_CLASS 0xA0			/* Class that contains the following instructions */
#define SIM_SELECT 0xA4			/* Select a file on the card */
#define SIM_STATUS 0xF2			/* Get the status of the currently selected file */
#define SIM_READ_BINARY 0xB0		/* Read file in binary mode */
#define SIM_UPDATE_BINARY 0xD6		/* Write file in binary mode */
#define SIM_READ_RECORD 0xB2		/* Read record of a record based file */
#define SIM_UPDATE_RECORD 0xDC		/* Write record of a record based file */
#define SIM_SEEK 0xA2			/* Seek in a record based file */
#define SIM_INCREASE 0x32		/* Increase a record in a record based file */
#define SIM_VERIFY_CHV 0x20		/* Authenticate with card (enter pin) */
#define SIM_CHANGE_CHV 0x24		/* Change pin */
#define SIM_DISABLE_CHV 0x26		/* Disable pin so that no authentication is needed anymore */
#define SIM_ENABLE_CHV 0x28		/* Enable pin, authentication is now needed again */
#define SIM_UNBLOCK_CHV 0x2C		/* Unblock pin when it is blocked by entering a wrong pin three times */
#define SIM_INVALIDATE 0x04		/* Invalidate the current elementry file (file in a subdirectory) */
#define SIM_REHABILITATE 0x44		/* Rehabilitate the current elementry file (file in a subdirectory) */
#define SIM_RUN_GSM_ALGORITHM 0x88	/* Run the GSM A3 authentication algorithm in the card */
#define SIM_SLEEP 0xFA			/* Sleep command (only used in Phase 1 GSM) */
#define SIM_GET_RESPONSE 0xC0		/* Get the response of a command from the card */

/* File identifiers (filenames)
   The file identifiers are the standardized file identifiers mentioned in the
   GSM-11-11 specification. */
#define SIM_MF		0x3F00
#define SIM_EF_ICCID	0x2FE2
#define SIM_DF_TELECOM	0x7F10
#define SIM_EF_ADN	0x6F3A
#define SIM_EF_FDN	0x6F3B
#define SIM_EF_SMS	0x6F3C
#define SIM_EF_CCP	0x6F3D
#define SIM_EF_MSISDN	0x6F40
#define SIM_EF_SMSP	0x6F42
#define SIM_EF_SMSS	0x6F43
#define SIM_EF_LND	0x6F44
#define SIM_EF_EXT1	0x6F4A
#define SIM_EF_EXT2	0x6F4B
#define SIM_DF_GSM	0x7F20
#define SIM_EF_LP	0x6F05
#define SIM_EF_IMSI	0x6F07
#define SIM_EF_KC	0x6F20
#define SIM_EF_PLMNsel	0x6F30
#define SIM_EF_HPLMN	0x6F31
#define SIM_EF_ACMmax	0x6F37
#define SIM_EF_SST	0x6F38
#define SIM_EF_ACM	0x6F39
#define SIM_EF_GID1	0x6F3E
#define SIM_EF_GID2	0x6F3F
#define SIM_EF_PUCT	0x6F41
#define SIM_EF_CBMI	0x6F45
#define SIM_EF_SPN	0x6F46
#define SIM_EF_BCCH	0x6F74
#define SIM_EF_ACC	0x6F78
#define SIM_EF_FPLMN	0x6F7B
#define SIM_EF_LOCI	0x6F7E
#define SIM_EF_AD	0x6FAD
#define SIM_EF_PHASE	0x6FAE

/* Select a file on the card */
uint16_t sim_select(uint16_t fid)
{
	uint8_t txBuffer[2];
	uint8_t status_word[2];

	txBuffer[1] = (uint8_t) fid;
	txBuffer[0] = (uint8_t) (fid >> 8);

	if(calypso_sim_transceive(SIM_CLASS, SIM_SELECT, 0x00, 0x00, 0x02,
				  txBuffer, status_word, SIM_APDU_PUT) != 0)
		return 0xFFFF;

	return (status_word[0] << 8) | status_word[1];
}

/* Get the status of the currently selected file */
uint16_t sim_status(void)
{
	uint8_t status_word[2];

	if(calypso_sim_transceive(SIM_CLASS, SIM_STATUS, 0x00, 0x00, 0x00, 0,
				  status_word, SIM_APDU_PUT) != 0)
		return 0xFFFF;

	return (status_word[0] << 8) | status_word[1];
}

/* Read file in binary mode */
uint16_t sim_readbinary(uint8_t offset_high, uint8_t offset_low, uint8_t length, uint8_t *data)
{
	uint8_t status_word[2];
	if(calypso_sim_transceive(SIM_CLASS, SIM_READ_BINARY, offset_high,
				  offset_low, length, data ,status_word,
				  SIM_APDU_GET) != 0)
		return 0xFFFF;

	return (status_word[0] << 8) | status_word[1];
}

uint16_t sim_verify(char *pin)
{
	uint8_t txBuffer[8];
	uint8_t status_word[2];

	memset(txBuffer, 0xFF, 8);
	memcpy(txBuffer, pin, strlen(pin));

	if(calypso_sim_transceive(SIM_CLASS, SIM_VERIFY_CHV, 0x00, 0x01, 0x08, txBuffer,status_word, SIM_APDU_PUT) != 0)
		return 0xFFFF;

	return (status_word[0] << 8) | status_word[1];
}

uint16_t sim_run_gsm_algorith(uint8_t *data)
{
	uint8_t status_word[2];

	if(calypso_sim_transceive(SIM_CLASS, SIM_RUN_GSM_ALGORITHM, 0x00, 0x00, 0x10, data, status_word, SIM_APDU_PUT) != 0)
		return 0xFFFF;

	printf("   ==> Status word: %x\n", (status_word[0] << 8) | status_word[1]);

	if(status_word[0] != 0x9F || status_word[1] != 0x0C)
		return (status_word[0] << 8) | status_word[1];

	/* GET RESPONSE */

	if(calypso_sim_transceive(SIM_CLASS, SIM_GET_RESPONSE, 0, 0, 0x0C, data ,status_word, SIM_APDU_GET) != 0)
		return 0xFFFF;

	return (status_word[0] << 8) | status_word[1];
}


/* FIXME: We need proper calibrated delay loops at some point! */
void delay_us(unsigned int us)
{
	volatile unsigned int i;

	for (i= 0; i < us*4; i++) { i; }
}

void delay_ms(unsigned int ms)
{
	volatile unsigned int i;
	for (i= 0; i < ms*1300; i++) { i; }
}

/* Execute my (dexter's) personal test */
void do_sim_test(void)
{
	uint8_t testBuffer[20];
	uint8_t testtxBuffer[20];

	uint8_t testDataBody[257];
	uint8_t testStatusWord[2];
	int recivedChars;
	int i;

	uint8_t atr[20];
	uint8_t atrLength = 0;

	memset(atr,0,sizeof(atr));



	uint8_t buffer[20];


	memset(testtxBuffer,0,sizeof(testtxBuffer));

	puts("----------------SIMTEST----8<-----------------\n");

	/* Initialize Sim-Controller driver */
	puts("Initializing driver:\n");
	calypso_sim_init(NULL);

	/* Power up sim and display ATR */
	puts("Power up simcard:\n");
	memset(atr,0,sizeof(atr));
	atrLength = calypso_sim_powerup(atr);
	myHexdump(atr,atrLength);

	/* Reset sim and display ATR */
	puts("Reset simcard:\n");
	memset(atr,0,sizeof(atr));
	atrLength = calypso_sim_reset(atr);
	myHexdump(atr,atrLength);



	testDataBody[0] = 0x3F;
	testDataBody[1] = 0x00;
	calypso_sim_transceive(0xA0, 0xA4, 0x00, 0x00, 0x02, testDataBody,0, SIM_APDU_PUT);
	calypso_sim_transceive(0xA0, 0xC0, 0x00, 0x00, 0x0f, testDataBody,0, SIM_APDU_GET);
	myHexdump(testDataBody,0x0F);

	puts("Test Phase 1: Testing bare sim commands...\n");

	puts(" * Testing SELECT: Selecting MF\n");
	printf("   ==> Status word: %x\n", sim_select(SIM_MF));

	puts(" * Testing SELECT: Selecting DF_GSM\n");
	printf("   ==> Status word: %x\n", sim_select(SIM_DF_GSM));

	puts(" * Testing PIN VERIFY\n");
	printf("   ==> Status word: %x\n", sim_verify("1234"));

	puts(" * Testing SELECT: Selecting EF_IMSI\n");
	printf("   ==> Status word: %x\n", sim_select(SIM_EF_IMSI));

	puts(" * Testing STATUS:\n");
	printf("   ==> Status word: %x\n", sim_status());

	memset(buffer,0,sizeof(buffer));
	puts(" * Testing READ BINARY:\n");
	printf("   ==> Status word: %x\n", sim_readbinary(0,0,9,buffer));
	printf("       Data: ");
	myHexdump(buffer,9);

	memset(buffer,0,sizeof(buffer));
	memcpy(buffer,"\x00\x11\x22\x33\x44\x55\x66\x77\x88\x99\xaa\xbb\xcc\xdd\xee\xff",16);
	puts(" * Testing RUN GSM ALGORITHM\n");
	printf("   ==> Status word: %x\n", sim_run_gsm_algorith(buffer));
	printf("       Result: ");
	myHexdump(buffer,12);

	delay_ms(5000);

	calypso_sim_powerdown();

	puts("------------END SIMTEST----8<-----------------\n");
}

/* Main Program */
const char *hr = "======================================================================\n";

void key_handler(enum key_codes code, enum key_states state);

static void *console_rx_cb(uint8_t dlci, struct msgb *msg)
{
	if (dlci != SC_DLCI_CONSOLE) {
		printf("Message for unknown DLCI %u\n", dlci);
		return;
	}

	printf("Message on console DLCI: '%s'\n", msg->data);
	msgb_free(msg);
}

int main(void)
{
	board_init(1);

	puts("\n\nOsmocomBB SIM Test (revision " GIT_REVISION ")\n");
	puts(hr);

	/* Dump device identification */
	dump_dev_id();
	puts(hr);

	/* Dump clock config before PLL set */
	calypso_clk_dump();
	puts(hr);

	keypad_set_handler(&key_handler);

	/* Dump clock config after PLL set */
	calypso_clk_dump();
	puts(hr);

	/* Dump all memory */
	//dump_mem();
#if 0
	/* Dump Bootloader */
	memdump_range((void *)0x00000000, 0x2000);
	puts(hr);
#endif

	sercomm_register_rx_cb(SC_DLCI_CONSOLE, console_rx_cb);

	do_sim_test();

	/* beyond this point we only react to interrupts */
	puts("entering interrupt loop\n");
	while (1) {
	}

	twl3025_power_off();
	while (1) {}
}

void key_handler(enum key_codes code, enum key_states state)
{
	if (state != PRESSED)
		return;

	switch (code) {
	default:
		break;
	}
}
