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

	# Constants
	# TODO: add options to change this
	RSSI_RAND_TRESHOLD = 10
	RSSI_RAND_MIN = -90
	RSSI_RAND_MAX = -60

	# TODO: add options to change this
	TOA_RAND_TRESHOLD = 0.3
	TOA_RAND_BASE = 0.00

	def __init__(self, bts_link, bb_link):
		self.bts_link = bts_link
		self.bb_link = bb_link

		# Generate a random RSSI range
		rssi = random.randint(self.RSSI_RAND_MIN, self.RSSI_RAND_MAX)
		self.rssi_min = rssi - self.RSSI_RAND_TRESHOLD
		self.rssi_max = rssi + self.RSSI_RAND_TRESHOLD

		# Generate a random ToA range
		self.toa_min = self.TOA_RAND_BASE - self.TOA_RAND_TRESHOLD
		self.toa_max = self.TOA_RAND_BASE + self.TOA_RAND_TRESHOLD

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
		msg_trx2l1.toa = msg_trx2l1.rand_toa(
			min = self.toa_min, max = self.toa_max)

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

		# Validate and generate the payload
		payload = msg.gen_msg()

		# Append two unused bytes at the end
		# in order to keep the compatibility
		payload += bytearray(2)

		# Send burst to BTS
		self.bts_link.send(payload)
