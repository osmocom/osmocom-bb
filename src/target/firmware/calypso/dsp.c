#define DEBUG
/* Driver for the Calypso integrated DSP */

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

#include <debug.h>
#include <delay.h>
#include <memory.h>
#include <calypso/clock.h>
#include <calypso/dsp.h>
#include <calypso/dsp_api.h>
#include <calypso/tpu.h>

#define REG_API_CONTROL		0xfffe0000
#define APIC_R_SMODE_HOM	(1 << 1)	/* API is configured in HOM mode */
#define APIC_R_HINT		(1 << 3)	/* Host processor interrupt (DSP->MCU) */
#define APIC_W_DSPINT		(1 << 2)	/* ARM issues interrupt to DSP */

#define REG_API_WS		0xfffff902	/* Number of wait states for ARM access to API memory */
#define REG_ARM_RHEA_CTL	0xfffff904	/* Write buffer bypassing */
#define REG_EXT_RHEA_CTL	0xfffff906	/* Some timeout */

#define API_SIZE		0x2000U		/* in words */

#define BASE_API_RAM		0xffd00000	/* Base address of API RAM form ARM point of view */

#define DSP_BASE_API		0x0800		/* Base address of API RAM for DSP */
#define DSP_BASE_API_MIRROR	0xe000		/* Base address of API RAM for DSP (API boot mirrot */
#define DSP_START		0x7000		/* DSP Start address */

/* Boot loader */
#define BL_CMD_STATUS		(BASE_API_RAM + 0x0ffe)	/* Status / Command var    */
#define BL_ADDR_LO		(BASE_API_RAM + 0x0ffc)	/* Address (16 lsbs)       */
#define BL_ADDR_HI		(BASE_API_RAM + 0x0ff8)	/* Address (ext page bits) */
#define BL_SIZE			(BASE_API_RAM + 0x0ffa) /* Size                    */

#define BL_MAX_BLOCK_SIZE	0x7F0			/* Maximum size of copied block */

	/* Possible values for the download status */
#define BL_STATUS_NA		0
#define BL_STATUS_IDLE		1
#define BL_CMD_COPY_BLOCK	2
#define BL_CMD_COPY_MODE	4

#define BL_MODE_PROG_WRITE	0
#define BL_MODE_DATA_WRITE	1
#define BL_MODE_PROG_READ	2
#define BL_MODE_DATA_READ	3
#define BL_MODE_PROM_READ	4
#define BL_MODE_DROM_READ	5


struct dsp_section {
	uint32_t addr;		/* addr for DSP  */
	uint32_t size;		/* size in words */
	const uint16_t *data;
};

#include "dsp_params.c"
#include "dsp_bootcode.c"
#include "dsp_dumpcode.c"

struct dsp_api dsp_api = {
	.ndb	= (T_NDB_MCU_DSP *) BASE_API_NDB,
	.db_r	= (T_DB_DSP_TO_MCU *) BASE_API_R_PAGE_0,
	.db_w	= (T_DB_MCU_TO_DSP *) BASE_API_W_PAGE_0,
	.param	= (T_PARAM_MCU_DSP *) BASE_API_PARAM,
	.r_page	= 0,
	.w_page = 0,
};


void dsp_dump_version(void)
{
	printf("DSP Download Status: 0x%04x\n", readw(BL_CMD_STATUS));
	printf("DSP API Version: 0x%04x 0x%04x\n",
		dsp_api.ndb->d_version_number1, dsp_api.ndb->d_version_number2);
}

static void dsp_bl_wait_ready(void)
{
	while (readw(BL_CMD_STATUS) != BL_STATUS_IDLE);
}

static int dsp_upload_sections_api(const struct dsp_section *sec, uint16_t dsp_base_api)
{
	for (; sec->data; sec++) {
		unsigned int i;
		volatile uint16_t *dptr;

		if (sec->addr & ~((1<<16)-1))	/* 64k max addr */
			return -1;
		if (sec->addr < dsp_base_api)
			return -1;
		if ((sec->addr + sec->size) > (dsp_base_api + API_SIZE))
			return -1;

		dptr = (volatile uint16_t *)(BASE_API_RAM + ((sec->addr - dsp_base_api) * sizeof(uint16_t)));
		for (i=0; i<sec->size; i++)
			*dptr++ = sec->data[i];
	}

	/* FIXME need eioio or wb ? */

	return 0;
}

static void dsp_pre_boot(const struct dsp_section *bootcode)
{
	dputs("Assert DSP into Reset\n");
	calypso_reset_set(RESET_DSP, 1);

	if (bootcode) {
		dputs("Loading initial DSP bootcode (API boot mode)\n");
		dsp_upload_sections_api(dsp_bootcode, DSP_BASE_API_MIRROR);

		writew(BL_STATUS_NA, BL_CMD_STATUS);
	} else
		delay_ms(10);

	dputs("Releasing DSP from Reset\n");
	calypso_reset_set(RESET_DSP, 0);

	/* Wait 10 us */
	delay_ms(100);

	dsp_bl_wait_ready();
}

static void dsp_set_params(int16_t *param_tab, int param_size)
{
	int i;
	int16_t *param_ptr = (int16_t *) BASE_API_PARAM;

	/* Start DSP up to bootloader */
	dsp_pre_boot(dsp_bootcode);

	/* FIXME: Implement Patch download, if any */

	dputs("Setting some dsp_api.ndb values\n");
	dsp_api.ndb->d_background_enable = 0;
	dsp_api.ndb->d_background_abort = 0;
	dsp_api.ndb->d_background_state = 0;
	dsp_api.ndb->d_debug_ptr = 0x0074;
	dsp_api.ndb->d_debug_bk = 0x0001;
	dsp_api.ndb->d_pll_config = 0x154; //C_PLL_CONFIG;
	dsp_api.ndb->p_debug_buffer = 0x17ff; //C_DEBUG_BUFFER_ADD;
	dsp_api.ndb->d_debug_buffer_size = 7; //C_DEBUG_BUFFER_SIZE;
	dsp_api.ndb->d_debug_trace_type = 0; //C_DEBUG_TRACE_TYPE;
	dsp_api.ndb->d_dsp_state = 3; //C_DSP_IDLE3;
	dsp_api.ndb->d_audio_gain_ul = 0;
	dsp_api.ndb->d_audio_gain_dl = 0;
	dsp_api.ndb->d_es_level_api = 0x5213;
	dsp_api.ndb->d_mu_api = 0x5000;

	dputs("Setting API NDB parameters\n");
	for (i = 0; i < param_size; i ++)
		*param_ptr++ = param_tab[i];
	
	dsp_dump_version();

	dputs("Finishing download phase\n");
	writew(0, BL_SIZE);
	writew(DSP_START, BL_ADDR_LO);
	writew(BL_CMD_COPY_BLOCK, BL_CMD_STATUS);

	dsp_dump_version();
}

void dsp_api_memset(uint16_t *ptr, int octets)
{
	uint16_t i;
	for (i = 0; i < octets / sizeof(uint16_t); i++)
		*ptr++ = 0;
}

static void dsp_ndb_init(void)
{
	T_NDB_MCU_DSP *ndb = dsp_api.ndb;

	ndb->d_fb_mode	= 1;		/* mode 1 FCCH burst detection */
	ndb->d_fb_det	= 0;		/* we have not yet detected a FB */
	ndb->a_cd[0]	= (1<<B_FIRE1);	/* CCCH/SACCH downlink */
	ndb->a_dd_0[0]	= 0;
	ndb->a_dd_0[2]	= 0xffff;
	ndb->a_dd_1[0]	= 0;
	ndb->a_dd_1[2]	= 0xffff;
	ndb->a_du_0[0]	= 0;
	ndb->a_du_0[2]	= 0xffff;
	ndb->a_du_1[0]	= 0;
	ndb->a_du_1[2]	= 0xffff;
	ndb->a_fd[0]	= (1<<B_FIRE1);
	ndb->a_fd[2]	= 0xffff;
	ndb->d_a5mode	= 0;
	ndb->d_tch_mode	= 0x0800;
	/* FIXME: set guard bits */
	ndb->a_sch26[0]	= (1<<B_SCH_CRC);
	/* Interrupt RIF transmit if FIFO <= threshold with threshold == 0 */
	/* MCM = 1, XRST = 0, CLKX_AUTO=1, TXM=1, NCLK_EN=1, NCLK13_EN=1,
	 * THRESHOLD = 0, DIV_CLK = 0 (13MHz) */
	ndb->d_spcx_rif	= 0x179;
}

static void dsp_db_init(void)
{
	dsp_api_memset((void *)BASE_API_W_PAGE_0, sizeof(T_DB_MCU_TO_DSP));
	dsp_api_memset((void *)BASE_API_W_PAGE_1, sizeof(T_DB_MCU_TO_DSP));
	dsp_api_memset((void *)BASE_API_R_PAGE_0, sizeof(T_DB_DSP_TO_MCU));
	dsp_api_memset((void *)BASE_API_R_PAGE_1, sizeof(T_DB_DSP_TO_MCU));
}

void dsp_power_on(void)
{
	dsp_set_params((int16_t *)&dsp_params, sizeof(dsp_params)/2);
	dsp_ndb_init();
	dsp_db_init();
	dsp_api.frame_ctr = 0;
	dsp_api.r_page = dsp_api.w_page = dsp_api.r_page_used = 0;
}

/* test for frequency burst detection */
#define REG_INT_STAT 0xffff1004
static void wait_for_frame_irq(void)
{
	//puts("Waiting for Frame Interrupt");
	//while (readb(REG_INT_STAT) & 1)
	while (readb((void *)0xffff1000) & (1<<4))
	;//	putchar('.');
	//puts("Done!\n");
}

void dsp_end_scenario(void)
{
	/* FIXME: we don't yet deal with the MISC_TASK */

	/* End the DSP Scenario */
	dsp_api.ndb->d_dsp_page = B_GSM_TASK | dsp_api.w_page;
	dsp_api.w_page ^= 1;

	/* Tell TPU to generate a FRAME interrupt to the DSP */
	tpu_dsp_frameirq_enable();
	tpu_frame_irq_en(1, 1);
}

void dsp_load_rx_task(uint16_t task, uint8_t burst_id, uint8_t tsc)
{
	dsp_api.db_w->d_task_d = task;
	dsp_api.db_w->d_burst_d = burst_id;
	dsp_api.db_w->d_ctrl_system |= tsc & 0x7;
}

void dsp_load_tx_task(uint16_t task, uint8_t burst_id, uint8_t tsc)
{
	dsp_api.db_w->d_task_u = task;
	dsp_api.db_w->d_burst_u = burst_id;
	dsp_api.db_w->d_ctrl_system |= tsc & 0x7;
}

#define SC_CHKSUM_VER     (BASE_API_W_PAGE_0 + (2 * (0x08DB - 0x800)))
static void dsp_dump_csum(void)
{
	printf("dsp page          : %u\n", dsp_api.ndb->d_dsp_page);
	printf("dsp code version  : 0x%04x\n", dsp_api.db_r->a_pm[0]);
	printf("dsp checksum      : 0x%04x\n", dsp_api.db_r->a_pm[1]);
	printf("dsp patch version : 0x%04x\n", readw(SC_CHKSUM_VER));
}

void dsp_checksum_task(void)
{
	dsp_dump_csum();
	dsp_api.db_w->d_task_md = CHECKSUM_DSP_TASK;
	dsp_api.ndb->d_fb_mode = 1;

	dsp_end_scenario();

	wait_for_frame_irq();

	dsp_dump_csum();
}

#define L1D_AUXAPC              0x0012
#define L1D_APCRAM              0x0014

void dsp_load_apc_dac(uint16_t apc)
{
	dsp_api.db_w->d_power_ctl = (apc << 6) | L1D_AUXAPC;
}


static void _dsp_dump_range(uint32_t addr, uint32_t size, int mode)
{
	uint32_t bs;

	/* Mode selection */
	writew(mode, BASE_API_RAM);
	writew(BL_CMD_COPY_MODE, BL_CMD_STATUS);
	dsp_bl_wait_ready();

	/* Block by block dump */
	while (size) {
		volatile uint16_t *api = (volatile uint16_t *)BASE_API_RAM;

		bs = (size > BL_MAX_BLOCK_SIZE) ? BL_MAX_BLOCK_SIZE : size;
		size -= bs;

		writew(addr >> 16, BL_ADDR_HI);
		writew(addr & 0xffff, BL_ADDR_LO);
		writew(bs, BL_SIZE);
		writew(BL_CMD_COPY_BLOCK, BL_CMD_STATUS);

		dsp_bl_wait_ready();

		while (bs--) {
			if ((addr&15)==0)
				printf("%05lx : ", addr);
			printf("%04hx%c", *api++, ((addr&15)==15)?'\n':' ');
			addr++;
		}
	};
	puts("\n");
}

void dsp_dump(void)
{
	static const struct {
		const char *name;
		uint32_t addr;
		uint32_t size;
		int mode;
	} dr[] = {
		{ "Registers",	0x00000, 0x0060, BL_MODE_DATA_READ },
		{ "DROM",	0x09000, 0x5000, BL_MODE_DROM_READ },
		{ "PDROM",	0x0e000, 0x2000, BL_MODE_DROM_READ },
		{ "PROM0",	0x07000, 0x7000, BL_MODE_PROM_READ },
		{ "PROM1",	0x18000, 0x8000, BL_MODE_PROM_READ },
		{ "PROM2",	0x28000, 0x8000, BL_MODE_PROM_READ },
		{ "PROM3",	0x38000, 0x2000, BL_MODE_PROM_READ },
		{ NULL, 0, 0, -1 }
	};

	int i;

	/* Start DSP up to bootloader */
	dsp_pre_boot(dsp_bootcode);

	/* Load and execute our dump code in the DSP */
	dsp_upload_sections_api(dsp_dumpcode, DSP_BASE_API);

	writew(0, BL_ADDR_HI);
	writew(DSP_DUMPCODE_START, BL_ADDR_LO);
	writew(0, BL_SIZE);
	writew(BL_CMD_COPY_BLOCK, BL_CMD_STATUS);

		/* our dump code actually simulates the boot loaded
		 * but with added read commands */
	dsp_bl_wait_ready();

	/* Test the 'version' command */
	writew(0xffff, BL_CMD_STATUS);
	dsp_bl_wait_ready();
	printf("DSP bootloader version 0x%04x\n", readw(BASE_API_RAM));

	/* Dump each range */
	for (i=0; dr[i].name; i++) {
		printf("DSP dump: %s [%05lx-%05lx]\n", dr[i].name,
			dr[i].addr, dr[i].addr+dr[i].size-1);
		_dsp_dump_range(dr[i].addr, dr[i].size, dr[i].mode);
	}
}

