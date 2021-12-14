#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# TRX Toolkit
# Random burst (NB, FB, SB, AB) generator
#
# (C) 2017 by Vadim Yanitskiy <axilirator@gmail.com>
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

import random

from gsm_shared import *

class RandBurstGen:

	# GSM 05.02 Chapter 5.2.6 Dummy Burst
	db_bits = [
		0, 0, 0,
		1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0,
		0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 0,
		0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0,
		0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0,
		0, 1, 0, 1, 1, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 0,
		0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0,
		0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1,
		1, 1, 1, 0, 1, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 1,
		0, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0,
		0, 0, 0,
	]

	# Pick a random TSC for a given burst type
	def get_rand_tsc(self, bt):
		tsc_list = filter(lambda seq: seq.bt == bt, list(TrainingSeqGMSK))
		tsc_list = list(tsc_list) # In Python 3 filter() returns an iterator
		return random.choice(tsc_list)

	# Generate a normal burst
	def gen_nb(self, tsc = None):
		buf = []

		# Tailing bits
		buf += [0] * 3

		# Random data 1 / 2
		buf += [random.randint(0, 1) for _ in range(57)]

		# Steal flag 1 / 2
		buf.append(random.randint(0, 1))

		# Training sequence
		if tsc is None:
			tsc = self.get_rand_tsc(BurstType.NORMAL)
		buf += tsc.seq

		# Steal flag 2 / 2
		buf.append(random.randint(0, 1))

		# Random data 2 / 2
		buf += [random.randint(0, 1) for _ in range(57)]

		# Tailing bits
		buf += [0] * 3

		return buf

	# Generate a frequency correction burst
	def gen_fb(self):
		return [0] * GMSK_BURST_LEN

	# Generate a synchronization burst
	def gen_sb(self, tsc = None):
		buf = []

		# Tailing bits
		buf += [0] * 3

		# Random data 1 / 2
		buf += [random.randint(0, 1) for _ in range(39)]

		# Training sequence
		if tsc is None:
			tsc = self.get_rand_tsc(BurstType.SYNC)
		buf += tsc.seq

		# Random data 2 / 2
		buf += [random.randint(0, 1) for _ in range(39)]

		# Tailing bits
		buf += [0] * 3

		return buf

	# Generate a dummy burst
	def gen_db(self):
		return self.db_bits

	# Generate an access burst
	def gen_ab(self, tsc = None):
		buf = []

		# Tailing bits
		buf += [0] * 8

		# Training sequence
		if tsc is None:
			tsc = self.get_rand_tsc(BurstType.ACCESS)
		buf += tsc.seq

		# Random data
		buf += [random.randint(0, 1) for _ in range(36)]

		# Tailing bits
		buf += [0] * 3

		# Guard period
		buf += [0] * 60

		return buf
