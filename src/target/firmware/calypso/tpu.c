/* Calypso DBB internal TPU (Time Processing Unit) Driver */

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
#include <calypso/tpu.h>
#include <calypso/tsp.h>

/* Using TPU_DEBUG you will send special HLDC messages to the host PC
 * containing the full TPU RAM content at the time you call tpu_enable() */
//#define TPU_DEBUG

#define BASE_ADDR_TPU	0xffff1000
#define TPU_REG(x)	(BASE_ADDR_TPU+(x))

#define BASE_ADDR_TPU_RAM	0xffff9000
#define TPU_RAM_END		0xffff97ff

enum tpu_reg_arm {
	TPU_CTRL	= 0x0,	/* Control & Status Register */
	INT_CTRL	= 0x2,	/* Interrupt Control Register */
	INT_STAT	= 0x4,	/* Interrupt Status Register */
	TPU_OFFSET	= 0xC,	/* Offset operand value register */
	TPU_SYNCHRO	= 0xE,	/* synchro operand value register */
	IT_DSP_PG	= 0x20,
};

enum tpu_ctrl_bits {
	TPU_CTRL_RESET		= (1 << 0),
	TPU_CTRL_PAGE		= (1 << 1),
	TPU_CTRL_EN		= (1 << 2),
	/* unused */
	TPU_CTRL_DSP_EN		= (1 << 4),
	/* unused */
	TPU_CTRL_MCU_RAM_ACC	= (1 << 6),
	TPU_CTRL_TSP_RESET	= (1 << 7),
	TPU_CTRL_IDLE		= (1 << 8),
	TPU_CTRL_WAIT		= (1 << 9),
	TPU_CTRL_CK_ENABLE	= (1 << 10),
	TPU_CTRL_FULL_WRITE	= (1 << 11),
};

enum tpu_int_ctrl_bits {
	ICTRL_MCU_FRAME		= (1 << 0),
	ICTRL_MCU_PAGE		= (1 << 1),
	ICTRL_DSP_FRAME		= (1 << 2),
	ICTRL_DSP_FRAME_FORCE	= (1 << 3),
};

static uint16_t *tpu_ptr = (uint16_t *)BASE_ADDR_TPU_RAM;

#ifdef TPU_DEBUG
#include <comm/sercomm.h>
#include <layer1/sync.h>
static void tpu_ram_read_en(int enable)
{
	uint16_t reg;

	reg = readw(TPU_REG(TPU_CTRL));
	if (enable)
		reg |= TPU_CTRL_MCU_RAM_ACC;
	else
		reg &= ~TPU_CTRL_MCU_RAM_ACC;
	writew(reg, TPU_REG(TPU_CTRL));
}

static void tpu_debug(void)
{
	uint16_t *tpu_base = (uint16_t *)BASE_ADDR_TPU_RAM;
	unsigned int tpu_size = tpu_ptr - tpu_base;
	struct msgb *msg = sercomm_alloc_msgb(tpu_size*2);
	uint16_t *data;
	uint32_t *fn;
	uint16_t reg;
	int i;

	/* prepend tpu memory dump with frame number */
	fn = (uint32_t *) msgb_put(msg, sizeof(fn));
	*fn = l1s.current_time.fn;

	tpu_ram_read_en(1);

	data = (uint16_t *) msgb_put(msg, tpu_size*2);
	for (i = 0; i < tpu_size; i ++)
		data[i] = tpu_base[i];

	tpu_ram_read_en(0);

	sercomm_sendmsg(SC_DLCI_DEBUG, msg);
}
#else
static void tpu_debug(void) { }
#endif

#define BIT_SET	1
#define BIT_CLEAR 0

/* wait for a certain control bit to be set */
static int tpu_wait_ctrl_bit(uint16_t bit, int set)
{
	int timeout = 10*1000;

	while (1) {
		uint16_t reg = readw(TPU_REG(TPU_CTRL));
		if (set) {
			if (reg & bit)
				break;
		} else {
			if (!(reg & bit))
				break;
		}
		timeout--;
		if (timeout <= 0) {
			puts("Timeout while waiting for TPU ctrl bit!\n");
			return -1;
		}
	}

	return 0;
}

/* assert or de-assert TPU reset */
void tpu_reset(int active)
{
	uint16_t reg;

	printd("tpu_reset(%u)\n", active);
	reg = readw(TPU_REG(TPU_CTRL));
	if (active) {
		reg |= (TPU_CTRL_RESET|TPU_CTRL_TSP_RESET);
		writew(reg, TPU_REG(TPU_CTRL));
		tpu_wait_ctrl_bit(TPU_CTRL_RESET, BIT_SET);
	} else {
		reg &= ~(TPU_CTRL_RESET|TPU_CTRL_TSP_RESET);
		writew(reg, TPU_REG(TPU_CTRL));
		tpu_wait_ctrl_bit(TPU_CTRL_RESET, BIT_CLEAR);
	}
}

/* Enable or Disable a new scenario loaded into the TPU */
void tpu_enable(int active)
{
	uint16_t reg = readw(TPU_REG(TPU_CTRL));

	printd("tpu_enable(%u)\n", active);

	tpu_debug();

	if (active)
		reg |= TPU_CTRL_EN;
	else
		reg &= ~TPU_CTRL_EN;
	writew(reg, TPU_REG(TPU_CTRL));

	/* After the new scenario is loaded, TPU switches the MCU-visible memory
	 * page, i.e. we can write without any danger */
	tpu_rewind();
#if 0
	{
		int i;
		uint16_t oldreg = 0;

		for (i = 0; i < 100000; i++) {
			reg = readw(TPU_REG(TPU_CTRL));
			if (i == 0 || oldreg != reg) {
				printd("%d TPU state: 0x%04x\n", i, reg);
			}
			oldreg = reg;
		}
	}
#endif
}

/* Enable or Disable the clock of the TPU Module */
void tpu_clk_enable(int active)
{
	uint16_t reg = readw(TPU_REG(TPU_CTRL));

	printd("tpu_clk_enable(%u)\n", active);
	if (active) {
		reg |= TPU_CTRL_CK_ENABLE;
		writew(reg, TPU_REG(TPU_CTRL));
		tpu_wait_ctrl_bit(TPU_CTRL_CK_ENABLE, BIT_SET);
	} else {
		reg &= ~TPU_CTRL_CK_ENABLE;
		writew(reg, TPU_REG(TPU_CTRL));
		tpu_wait_ctrl_bit(TPU_CTRL_CK_ENABLE, BIT_CLEAR);
	}
}

/* Enable Frame Interrupt generation on next frame.  DSP will reset it */
void tpu_dsp_frameirq_enable(void)
{
	uint16_t reg = readw(TPU_REG(TPU_CTRL));
	reg |= TPU_CTRL_DSP_EN;
	writew(reg, TPU_REG(TPU_CTRL));

	tpu_wait_ctrl_bit(TPU_CTRL_DSP_EN, BIT_SET);
}

/* Is a Frame interrupt still pending for the DSP ? */
int tpu_dsp_fameirq_pending(void)
{
	uint16_t reg = readw(TPU_REG(TPU_CTRL));

	if (reg & TPU_CTRL_DSP_EN)
		return 1;

	return 0;
}

void tpu_rewind(void)
{
	dputs("tpu_rewind()\n");
	tpu_ptr = (uint16_t *) BASE_ADDR_TPU_RAM;
}

void tpu_enqueue(uint16_t instr)
{
	printd("tpu_enqueue(tpu_ptr=%p, instr=0x%04x)\n", tpu_ptr, instr);
	*tpu_ptr++ = instr;
	if (tpu_ptr > (uint16_t *) TPU_RAM_END)
		puts("TPU enqueue beyond end of TPU memory\n");
}

void tpu_init(void)
{
	uint16_t *ptr;

	/* Put TPU into Reset and enable clock */
	tpu_reset(1);
	tpu_clk_enable(1);

	/* set all TPU RAM to zero */
	for (ptr = (uint16_t *) BASE_ADDR_TPU_RAM; ptr < (uint16_t *) TPU_RAM_END; ptr++)
		*ptr = 0x0000;

	/* Get TPU out of reset */
	tpu_reset(0);
	/* Disable all interrupts */
	writeb(0x7, TPU_REG(INT_CTRL));

	tpu_rewind();
	tpu_enq_offset(0);
	tpu_enq_sync(0);
}

void tpu_test(void)
{
	int i;

	/* program a sequence of TSPACT events into the TPU */
	for (i = 0; i < 10; i++) {
		puts("TSP ACT enable: ");
		tsp_act_enable(0x0001);
		tpu_enq_wait(10);
		puts("TSP ACT disable: ");
		tsp_act_disable(0x0001);
		tpu_enq_wait(10);
	}
	tpu_enq_sleep();

	/* tell the chip to execute the scenario */
	tpu_enable(1);
}

void tpu_wait_idle(void)
{
	dputs("Waiting for TPU Idle ");
	/* Wait until TPU is doing something */
	delay_us(3);
	/* Wait until TPU is idle */
	while (readw(TPU_REG(TPU_CTRL)) & TPU_CTRL_IDLE)
		dputchar('.');
	dputs("Done!\n");
}

void tpu_frame_irq_en(int mcu, int dsp)
{
	uint8_t reg = readb(TPU_REG(INT_CTRL));
	if (mcu)
		reg &= ~ICTRL_MCU_FRAME;
	else
		reg |= ICTRL_MCU_FRAME;

	if (dsp)
		reg &= ~ICTRL_DSP_FRAME;
	else
		reg |= ICTRL_DSP_FRAME;

	writeb(reg, TPU_REG(INT_CTRL));
}

void tpu_force_dsp_frame_irq(void)
{
	uint8_t reg = readb(TPU_REG(INT_CTRL));
	reg |= ICTRL_DSP_FRAME_FORCE;
	writeb(reg, TPU_REG(INT_CTRL));
}

uint16_t tpu_get_offset(void)
{
	return readw(TPU_REG(TPU_OFFSET));
}

uint16_t tpu_get_synchro(void)
{
	return readw(TPU_REG(TPU_SYNCHRO));
}

/* add two numbers, modulo 5000, and ensure the result is positive */
uint16_t add_mod5000(int16_t a, int16_t b)
{
	int32_t sum = (int32_t)a + (int32_t)b;

	sum %= 5000;

	/* wrap around zero */
	if (sum < 0)
		sum += 5000;

	return sum;
}
