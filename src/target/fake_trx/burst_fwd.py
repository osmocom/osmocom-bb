#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# Virtual Um-interface (fake transceiver)
# BTS <-> BB burst forwarding
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

from data_msg import *

class BurstForwarder:
	# Timeslot filter (drop everything by default)
	ts_pass = None

	# Freq. filter
	bts_freq = None
	bb_freq = None

	# Timing Advance value indicated by MS (0 by default)
	# Valid range: 0..63, where each unit means
	# one GSM symbol advance.
	ta = 0

	# Constants
	# TODO: add options to change this
	RSSI_RAND_TRESHOLD = 10
	RSSI_RAND_MIN = -90
	RSSI_RAND_MAX = -60

	# TODO: add options to change this
	TOA256_RAND_TRESHOLD = 128
	TOA256_RAND_BASE = 0

	def __init__(self, bts_link, bb_link):
		self.bts_link = bts_link
		self.bb_link = bb_link

		# Generate a random RSSI range
		rssi = random.randint(self.RSSI_RAND_MIN, self.RSSI_RAND_MAX)
		self.rssi_min = rssi - self.RSSI_RAND_TRESHOLD
		self.rssi_max = rssi + self.RSSI_RAND_TRESHOLD

		# Generate a random ToA range
		self.toa256_min = self.TOA256_RAND_BASE - self.TOA256_RAND_TRESHOLD
		self.toa256_max = self.TOA256_RAND_BASE + self.TOA256_RAND_TRESHOLD

	# Calculates ToA value for Uplink bursts (coming to a BTS)
	def calc_toa_ul(self):
		# Generate a random ToA value
		toa256 = random.randint(self.toa256_min, self.toa256_max)

		# Apply TA value
		ta256 = self.ta * 256
		toa256 -= ta256

		return toa256

	# Converts a L12TRX message to TRX2L1 message
	def transform_msg(self, msg_raw):
		# Attempt to parse a message
		try:
			msg_l12trx = DATAMSG_L12TRX()
			msg_l12trx.parse_msg(bytearray(msg_raw))
		except:
			print("[!] Dropping unhandled DL message...")
			return None

		# Compose a new message for L1
		msg_trx2l1 = msg_l12trx.gen_trx2l1()

		# Randomize both RSSI and ToA values
		msg_trx2l1.rssi = msg_trx2l1.rand_rssi(
			min = self.rssi_min, max = self.rssi_max)
		msg_trx2l1.toa256 = msg_trx2l1.rand_toa256(
			min = self.toa256_min, max = self.toa256_max)

		return msg_trx2l1

	# Downlink handler: BTS -> BB
	def bts2bb(self):
		# Read data from socket
		data, addr = self.bts_link.sock.recvfrom(512)

		# BB is not connected / tuned
		if self.bb_freq is None:
			return None

		# Freq. filter
		if self.bb_freq != self.bts_freq:
			return None

		# Process a message
		msg = self.transform_msg(data)
		if msg is None:
			return None

		# Timeslot filter
		if msg.tn != self.ts_pass:
			return None

		# Validate and generate the payload
		payload = msg.gen_msg()

		# Append two unused bytes at the end
		# in order to keep the compatibility
		payload += bytearray(2)

		# Send burst to BB
		self.bb_link.send(payload)

	# Uplink handler: BB -> BTS
	def bb2bts(self):
		# Read data from socket
		data, addr = self.bb_link.sock.recvfrom(512)

		# BTS is not connected / tuned
		if self.bts_freq is None:
			return None

		# Freq. filter
		if self.bb_freq != self.bts_freq:
			return None

		# Process a message
		msg = self.transform_msg(data)
		if msg is None:
			return None

		# Emulate ToA value for BTS
		msg.toa256 = self.calc_toa_ul()

		# Validate and generate the payload
		payload = msg.gen_msg()

		# Append two unused bytes at the end
		# in order to keep the compatibility
		payload += bytearray(2)

		# Send burst to BTS
		self.bts_link.send(payload)
