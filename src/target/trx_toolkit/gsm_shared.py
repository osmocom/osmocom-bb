#!/usr/bin/env python
# -*- coding: utf-8 -*-

# TRX Toolkit
# Common GSM constants
#
# (C) 2018-2019 by Vadim Yanitskiy <axilirator@gmail.com>
#
# All Rights Reserved
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

from enum import Enum

# TDMA definitions
GSM_SUPERFRAME = 26 * 51
GSM_HYPERFRAME = 2048 * GSM_SUPERFRAME

# Burst length
GSM_BURST_LEN = 148
EDGE_BURST_LEN = GSM_BURST_LEN * 3

class BurstType(Enum):
	""" Burst types defined in 3GPP TS 45.002 """
	DUMMY	= ("DB") # Dummy burst (5.2.6)
	SYNC	= ("SB") # Synchronization Burst (5.2.5)
	FREQ	= ("FB") # Frequency correction Burst (5.2.4)
	ACCESS	= ("AB") # Access Burst (5.2.7)
	NORMAL	= ("NB") # Normal Burst (5.2.3)
	# HSR	= ("HB") # Higher symbol rate burst (5.2.3a)

class TrainingSeqGMSK(Enum):
	""" Training Sequences defined in 3GPP TS 45.002 """

	# Training Sequences for Access Burst (table 5.2.7-3)
	AB_TS0 = (0, BurstType.ACCESS, "01001011011111111001100110101010001111000")
	AB_TS1 = (1, BurstType.ACCESS, "01010100111110001000011000101111001001101")
	AB_TS2 = (2, BurstType.ACCESS, "11101111001001110101011000001101101110111")
	AB_TS4 = (4, BurstType.ACCESS, "11001001110001001110000000001101010110010")

	# Training Sequences for Access Burst (table 5.2.7-4)
	AB_TS3 = (3, BurstType.ACCESS, "10001000111010111011010000010000101100010")
	AB_TS5 = (5, BurstType.ACCESS, "01010000111111110101110101101100110010100")
	AB_TS6 = (6, BurstType.ACCESS, "01011110011101011110110100010011000010111")
	AB_TS7 = (7, BurstType.ACCESS, "01000010110000011101001010111011100010000")

	# Training Sequences for Synchronization Burst (table 5.2.5-3)
	SB_TS0 = (0, BurstType.SYNC, "1011100101100010000001000000111100101101010001010111011000011011")
	SB_TS1 = (1, BurstType.SYNC, "1110111001101011001010000011111011110100011111101100101100010101")
	SB_TS2 = (2, BurstType.SYNC, "1110110000110111010100010101101001111000000100000010001101001110")
	SB_TS3 = (3, BurstType.SYNC, "1011101000111101110101101111010010001011010000001000111010011000")

	# Training Sequences for Normal Burst (table 5.2.3a, TSC set 1)
	NB_TS0 = (0, BurstType.NORMAL, "00100101110000100010010111")
	NB_TS1 = (1, BurstType.NORMAL, "00101101110111100010110111")
	NB_TS2 = (2, BurstType.NORMAL, "01000011101110100100001110")
	NB_TS3 = (3, BurstType.NORMAL, "01000111101101000100011110")
	NB_TS4 = (4, BurstType.NORMAL, "00011010111001000001101011")
	NB_TS5 = (5, BurstType.NORMAL, "01001110101100000100111010")
	NB_TS6 = (6, BurstType.NORMAL, "10100111110110001010011111")
	NB_TS7 = (7, BurstType.NORMAL, "11101111000100101110111100")

	# TODO: more TSC sets from tables 5.2.3b-d

	def __init__(self, tsc, bt, seq_str, tsc_set = 0):
		# Training Sequence Code
		self.tsc = tsc
		# Burst type
		self.bt = bt

		# Training Sequence Code set
		# NOTE: unlike the specs. we count from zero
		self.tsc_set = tsc_set

		# Generate Training Sequence bits
		self.seq = [int(x) for x in seq_str]

	@classmethod
	def pick(self, burst):
		# Normal burst TS (26 bits)
		nb_seq = burst[3 + 57 + 1:][:26]
		# Access burst TS (41 bits)
		ab_seq = burst[8:][:41]
		# Sync Burst TS (64 bits)
		sb_seq = burst[3 + 39:][:64]

		for ts in list(self):
			# Ugly Python way of writing 'switch' statement
			if ts.bt is BurstType.NORMAL and ts.seq == nb_seq:
				return ts
			elif ts.bt is BurstType.ACCESS and ts.seq == ab_seq:
				return ts
			elif ts.bt is BurstType.SYNC and ts.seq == sb_seq:
				return ts

		return None
