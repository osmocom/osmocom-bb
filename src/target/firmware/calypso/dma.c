/* Driver for Calypso DMA controller */

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

#include <memory.h>

#define BASE_ADDR_DMA 0xfffffc00

enum dma_reg {
	CONTROLLER_CONF		= 0x00,
	ALLOC_CONFIG		= 0x02,
};
#define DMA_REG(m)		(BASE_ADDR_DMA + (m))

#define DMA_RAD(x)		DMA_REG((x)*0x10 + 0x0)
#define DMA_RDPATH(x)		DMA_REG((x)*0x10 + 0x2)
#define DMA_AAD(x)		DMA_REG((x)*0x10 + 0x4)
#define DMA_ALGTH(x)		DMA_REG((x)*0x10 + 0x6)
#define DMA_CTRL(x)		DMA_REG((x)*0x10 + 0x8)
#define DMA_CUR_OFF_API(x)	DMA_REG((x)*0x10 + 0xa)

void dma_init(void)
{
	/* DMA 1 (RIF Tx), 2 (RIF Rx) allocated to DSP, all others to ARM */
	writew(0x000c, DMA_REG(ALLOC_CONFIG));
}
