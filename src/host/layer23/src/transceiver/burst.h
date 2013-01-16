/*
 * burst.h
 *
 * Burst format
 *
 * Copyright (C) 2013  Sylvain Munaut <tnt@246tNt.com>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __TRX_BURST_H__
#define __TRX_BURST_H__


#include <stdint.h>


#define BURST_FB	0
#define BURST_SB	1
#define BURST_DUMMY	2
#define BURST_NB	3

struct burst_data
{
	uint8_t type;		/* BURST_??? */
	uint8_t data[15];	/* Only for NB */
} __attribute__((packed));


#endif /* __TRX_BURST_H__ */
