#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# Virtual Um-interface (fake transceiver)
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
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

import random

class RandBurstGen:
	# GSM L1 definitions
	GSM_BURST_LEN = 148

	# GSM 05.02 Chapter 5.2.3 Normal Burst
	nb_tsc_list = [
		[
			0, 0, 1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0,
			0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1, 1, 1,
		],
		[
			0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1,
			1, 1, 0, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1,
		],
		[
			0, 1, 0, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1,
			0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0,
		],
		[
			0, 1, 0, 0, 0, 1, 1, 1, 1, 0, 1, 1, 0,
			1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1, 0,
		],
		[
			0, 0, 0, 1, 1, 0, 1, 0, 1, 1, 1, 0, 0,
			1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 1, 1,
		],
		[
			0, 1, 0, 0, 1, 1, 1, 0, 1, 0, 1, 1, 0,
			0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 1, 0,
		],
		[
			1, 0, 1, 0, 0, 1, 1, 1, 1, 1, 0, 1, 1,
			0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, 1,
		],
		[
			1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 1, 0,
			0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0,
		],
	]

	# GSM 05.02 Chapter 5.2.5 SCH training sequence
	sb_tsc = [
		1, 0, 1, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 0, 1, 0,
		0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1,
		0, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1,
		0, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 1, 1,
	]

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

	# GSM 05.02 Chapter 5.2.7 Access burst
	ab_tsc = [
		0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1,
		1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0,
		1, 0, 1, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0,
	]

	# Generate a normal burst
	def gen_nb(self, seq_idx = 0):
		buf = []

		# Tailing bits
		buf += [0] * 3

		# Random data 1 / 2
		for i in range(0, 57):
			buf.append(random.randint(0, 1))

		# Steal flag 1 / 2
		buf.append(random.randint(0, 1))

		# Training sequence
		buf += self.nb_tsc_list[seq_idx]

		# Steal flag 2 / 2
		buf.append(random.randint(0, 1))

		# Random data 2 / 2
		for i in range(0, 57):
			buf.append(random.randint(0, 1))

		# Tailing bits
		buf += [0] * 3

		return buf

	# Generate a frequency correction burst
	def gen_fb(self):
		return [0] * self.GSM_BURST_LEN

	# Generate a synchronization burst
	def gen_sb(self):
		buf = []

		# Tailing bits
		buf += [0] * 3

		# Random data 1 / 2
		for i in range(0, 39):
			buf.append(random.randint(0, 1))

		# Training sequence
		buf += self.sb_tsc

		# Random data 2 / 2
		for i in range(0, 39):
			buf.append(random.randint(0, 1))

		# Tailing bits
		buf += [0] * 3

		return buf

	# Generate a dummy burst
	def gen_db(self):
		return self.db_bits

	# Generate an access burst
	def gen_ab(self):
		buf = []

		# Tailing bits
		buf += [0] * 8

		# Training sequence
		buf += self.ab_tsc

		# Random data
		for i in range(0, 36):
			buf.append(random.randint(0, 1))

		# Tailing bits
		buf += [0] * 3

		# Guard period
		buf += [0] * 60

		return buf
