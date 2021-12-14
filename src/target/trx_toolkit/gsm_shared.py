# -*- coding: utf-8 -*-

# TRX Toolkit
# Common GSM constants and helpers
#
# (C) 2018-2020 by Vadim Yanitskiy <axilirator@gmail.com>
# Contributions by sysmocom - s.f.m.c. GmbH
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

from enum import Enum

# TDMA definitions
GSM_SUPERFRAME = 26 * 51
GSM_HYPERFRAME = 2048 * GSM_SUPERFRAME

# Burst length
GMSK_BURST_LEN = 148
EDGE_BURST_LEN = GMSK_BURST_LEN * 3

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

class HoppingParams:
	""" Hopping sequence generation as per 3GPP TS 45.002, section 6.2.3.

	Based on firmware/layer1/rfch.c:rfch_hop_seq_gen() by Sylvain Munaut.

	"""

	# Magic numbers for pseudo-random hopping sequence generation
	RNTABLE = [
		 48,  98,  63,   1,  36,  95,  78, 102,  94,  73,
		  0,  64,  25,  81,  76,  59, 124,  23, 104, 100,
		101,  47, 118,  85,  18,  56,  96,  86,  54,   2,
		 80,  34, 127,  13,   6,  89,  57, 103,  12,  74,
		 55, 111,  75,  38, 109,  71, 112,  29,  11,  88,
		 87,  19,   3,  68, 110,  26,  33,  31,   8,  45,
		 82,  58,  40, 107,  32,   5, 106,  92,  62,  67,
		 77, 108, 122,  37,  60,  66, 121,  42,  51, 126,
		117, 114,   4,  90,  43,  52,  53, 113, 120,  72,
		 16,  49,   7,  79, 119,  61,  22,  84,   9,  97,
		 91,  15,  21,  24,  46,  39,  93, 105,  65,  70,
		125,  99,  17, 123,
	]

	def __init__(self, hsn, maio, ma):
		# Make sure MA is not empty
		ma_len = len(ma)
		if ma_len == 0: # TODO: or rather > 1?
			raise ValueError("Mobile Allocation is empty")

		self.hsn = hsn
		self.maio = maio
		self.ma = ma

		# Pre-calculate 2 ** NBIN in advance
		self._pnm = (ma_len >> 0) | (ma_len >> 1) \
			  | (ma_len >> 2) | (ma_len >> 3) \
			  | (ma_len >> 4) | (ma_len >> 5) \
			  | (ma_len >> 6)

	def __str__(self):
		fmt = "hsn=%u, maio=%u, ma_len=%u"
		return fmt % (self.hsn, self.maio, len(self.ma))

	@staticmethod
	def fn2gsm_time(fn):
		t1 = fn // (26 * 51)
		t2 = fn % 26
		t3 = fn % 51
		tc = (fn // 51) % 8
		return (t1, t2, t3, tc)

	# Resolve current ARFCN using the given TDMA frame number
	def resolve(self, fn):
		# Cyclic hopping
		if self.hsn == 0:
			mai = (fn + self.maio) % len(self.ma)
			return self.ma[mai]

		# Pseudo random hopping
		(t1, t2, t3, tc) = self.fn2gsm_time(fn)
		ma_len = len(self.ma)

		rn_idx = (self.hsn ^ (t1 & 63)) + t3
		m = t2 + self.RNTABLE[rn_idx]
		mp = m & self._pnm

		s = mp if mp < ma_len else (mp + t3 & self._pnm) % ma_len
		mai = (s + self.maio) % ma_len
		return self.ma[mai]
