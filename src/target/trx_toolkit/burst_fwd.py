#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# TRX Toolkit
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

	# Randomization of RSSI
	randomize_dl_rssi = False
	randomize_ul_rssi = False

	# Randomization of ToA
	randomize_dl_toa256 = False
	randomize_ul_toa256 = False

	# Timing Advance value indicated by MS (0 by default)
	# Valid range: 0..63, where each unit means
	# one GSM symbol advance.
	ta = 0

	# Timing of Arrival values indicated by transceiver
	# in units of 1/256 of GSM symbol periods. A pair of
	# base and threshold values defines a range of ToA value
	# randomization: from (base - threshold) to (base + threshold).
	toa256_dl_base = 0
	toa256_ul_base = 0

	toa256_dl_threshold = 128
	toa256_ul_threshold = 128

	# RSSI values indicated by transceiver in dBm.
	# A pair of base and threshold values defines a range of RSSI
	# randomization: from (base - threshold) to (base + threshold).
	rssi_dl_base = -60
	rssi_ul_base = -70

	rssi_dl_threshold = 10
	rssi_ul_threshold = 5

	# Path loss simulation: DL/UL burst dropping
	# Indicates how many bursts should be dropped
	# and which dropping period is used. By default,
	# period is 1, i.e. every burst (fn % 1 is always 0)
	burst_dl_drop_amount = 0
	burst_ul_drop_amount = 0
	burst_dl_drop_period = 1
	burst_ul_drop_period = 1

	def __init__(self, bts_link, bb_link):
		self.bts_link = bts_link
		self.bb_link = bb_link

	# Converts TA value from symbols to
	# units of 1/256 of GSM symbol periods
	def calc_ta256(self):
		return self.ta * 256

	# Calculates a random ToA value for Downlink bursts
	def calc_dl_toa256(self):
		# Check if randomization is required
		if not self.randomize_dl_toa256:
			return self.toa256_dl_base

		# Calculate a range for randomization
		toa256_min = self.toa256_dl_base - self.toa256_dl_threshold
		toa256_max = self.toa256_dl_base + self.toa256_dl_threshold

		# Generate a random ToA value
		toa256 = random.randint(toa256_min, toa256_max)

		return toa256

	# Calculates a random ToA value for Uplink bursts
	def calc_ul_toa256(self):
		# Check if randomization is required
		if not self.randomize_ul_toa256:
			return self.toa256_ul_base

		# Calculate a range for randomization
		toa256_min = self.toa256_ul_base - self.toa256_ul_threshold
		toa256_max = self.toa256_ul_base + self.toa256_ul_threshold

		# Generate a random ToA value
		toa256 = random.randint(toa256_min, toa256_max)

		return toa256

	# Calculates a random RSSI value for Downlink bursts
	def calc_dl_rssi(self):
		# Check if randomization is required
		if not self.randomize_dl_rssi:
			return self.rssi_dl_base

		# Calculate a range for randomization
		rssi_min = self.rssi_dl_base - self.rssi_dl_threshold
		rssi_max = self.rssi_dl_base + self.rssi_dl_threshold

		# Generate a random RSSI value
		return random.randint(rssi_min, rssi_max)

	# Calculates a random RSSI value for Uplink bursts
	def calc_ul_rssi(self):
		# Check if randomization is required
		if not self.randomize_ul_rssi:
			return self.rssi_ul_base

		# Calculate a range for randomization
		rssi_min = self.rssi_ul_base - self.rssi_ul_threshold
		rssi_max = self.rssi_ul_base + self.rssi_ul_threshold

		# Generate a random RSSI value
		return random.randint(rssi_min, rssi_max)

	# DL path loss simulation
	def path_loss_sim_dl(self, msg):
		# Burst dropping
		if self.burst_dl_drop_amount > 0:
			if msg.fn % self.burst_dl_drop_period == 0:
				print("[~] Simulation: dropping DL burst (fn=%u %% %u == 0)"
					% (msg.fn, self.burst_dl_drop_period))
				self.burst_dl_drop_amount -= 1
				return None

		return msg

	# UL path loss simulation
	def path_loss_sim_ul(self, msg):
		# Burst dropping
		if self.burst_ul_drop_amount > 0:
			if msg.fn % self.burst_ul_drop_period == 0:
				print("[~] Simulation: dropping UL burst (fn=%u %% %u == 0)"
					% (msg.fn, self.burst_ul_drop_period))
				self.burst_ul_drop_amount -= 1
				return None

		return msg

	# DL burst preprocessing
	def preprocess_dl_burst(self, msg):
		# Calculate both RSSI and ToA values
		msg.toa256 = self.calc_dl_toa256()
		msg.rssi = self.calc_dl_rssi()

	# UL burst preprocessing
	def preprocess_ul_burst(self, msg):
		# Calculate both RSSI and ToA values,
		# also apply Timing Advance
		msg.toa256 = self.calc_ul_toa256()
		msg.toa256 -= self.calc_ta256()
		msg.rssi = self.calc_ul_rssi()

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
		return msg_l12trx.gen_trx2l1()

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

		# Path loss simulation
		msg = self.path_loss_sim_dl(msg)
		if msg is None:
			return None

		# Burst preprocessing
		self.preprocess_dl_burst(msg)

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

		# Path loss simulation
		msg = self.path_loss_sim_ul(msg)
		if msg is None:
			return None

		# Burst preprocessing
		self.preprocess_ul_burst(msg)

		# Validate and generate the payload
		payload = msg.gen_msg()

		# Append two unused bytes at the end
		# in order to keep the compatibility
		payload += bytearray(2)

		# Send burst to BTS
		self.bts_link.send(payload)
