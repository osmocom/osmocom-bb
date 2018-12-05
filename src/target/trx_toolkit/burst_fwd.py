#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# TRX Toolkit
# BTS <-> BB burst forwarding
#
# (C) 2017-2018 by Vadim Yanitskiy <axilirator@gmail.com>
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
	""" Performs burst forwarding and preprocessing between MS and BTS.

	== Pass-filtering parameters

	BurstForwarder may drop or pass an UL/DL burst depending
	on the following parameters:

	  - bts_freq / bb_freq - the current BTS / MS frequency
	    that was set using RXTUNE control command. By default,
	    both freq. values are set to None, so nothing is being
	    forwarded (i.e. bursts are getting dropped).

	    FIXME: currently, we don't care about TXTUNE command
	    and transmit frequencies. It would be great to distinguish
	    between RX and TX frequencies for both BTS and MS.

	  - ts_pass_list - the list of active (i.e. configured)
	    timeslot numbers for the MS. A timeslot can be activated
	    or deactivated using SETSLOT control command from the MS.

	    FIXME: there is no such list for the BTS side.

	== Preprocessing and measurement simulation

	Since this is a virtual environment, we can simulate different
	parameters of a virtual RF interface:

	  - ToA (Timing of Arrival) - measured difference between expected
	    and actual time of burst arrival in units of 1/256 of GSM symbol
	    periods. A pair of both base and threshold values defines a range
	    of ToA value randomization:

	      DL: from (toa256_dl_base - toa256_dl_threshold)
	            to (toa256_dl_base + toa256_dl_threshold),
	      UL: from (toa256_ul_base - toa256_ul_threshold)
	            to (toa256_ul_base + toa256_ul_threshold).

	  - RSSI (Received Signal Strength Indication) - measured "power" of
	    the signal (per burst) in dBm. A pair of both base and threshold
	    values defines a range of RSSI value randomization:

	      DL: from (rssi_dl_base - rssi_dl_threshold)
	            to (rssi_dl_base + rssi_dl_threshold),
	      UL: from (rssi_ul_base - rssi_ul_threshold)
	            to (rssi_ul_base + rssi_ul_threshold).

	Please note that the randomization of both RSSI and ToA
	is optional, and can be enabled from the control interface.

	=== Timing Advance handling

	The BTS is using ToA measurements for UL bursts in order to calculate
	Timing Advance value, that is then indicated to a MS, which in its turn
	shall apply this value to the transmitted signal in order to compensate
	the delay. Basically, every burst is transmitted in advance defined by
	the indicated Timing Advance value. The valid range is 0..63, where
	each unit means one GSM symbol advance. The actual Timing Advance value
	is set using SETTA control command from MS. By default, it's set to 0.

	=== Path loss simulation - burst dropping

	In some cases, e.g. due to a weak signal or high interference, a burst
	can be lost, i.e. not detected by the receiver. This can also be
	simulated using FAKE_DROP command on both control interfaces:

	  - burst_{dl|ul}_drop_amount - the amount of DL/UL bursts
	      to be dropped (i.e. not forwarded towards the MS/BTS),

	  - burst_{dl|ul}_drop_period - drop every X DL/UL burst, e.g.
	    1 - drop every consequent burst, 2 - drop every second burst, etc.

	"""

	def __init__(self, bts_link, bb_link):
		self.bts_link = bts_link
		self.bb_link = bb_link

		# Init default parameters
		self.reset_dl()
		self.reset_ul()

	# Initialize (or reset to) default parameters for Downlink
	def reset_dl(self):
		# Unset current DL freq.
		self.bts_freq = None

		# Indicated RSSI / ToA values
		self.toa256_dl_base = 0
		self.rssi_dl_base = -60

		# RSSI / ToA randomization threshold
		self.toa256_dl_threshold = 0
		self.rssi_dl_threshold = 0

		# Path loss simulation (burst dropping)
		self.burst_dl_drop_amount = 0
		self.burst_dl_drop_period = 1

	# Initialize (or reset to) default parameters for Uplink
	def reset_ul(self):
		# Unset current DL freq.
		self.bb_freq = None

		# Indicated RSSI / ToA values
		self.rssi_ul_base = -70
		self.toa256_ul_base = 0

		# RSSI / ToA randomization threshold
		self.toa256_ul_threshold = 0
		self.rssi_ul_threshold = 0

		# Path loss simulation (burst dropping)
		self.burst_ul_drop_amount = 0
		self.burst_ul_drop_period = 1

		# Init timeslot filter (drop everything by default)
		self.ts_pass_list = []

		# Reset Timing Advance value
		self.ta = 0

	# Converts TA value from symbols to
	# units of 1/256 of GSM symbol periods
	def calc_ta256(self):
		return self.ta * 256

	# Calculates a random ToA value for Downlink bursts
	def calc_dl_toa256(self):
		# Check if randomization is required
		if self.toa256_dl_threshold is 0:
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
		if self.toa256_ul_threshold is 0:
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
		if self.rssi_dl_threshold is 0:
			return self.rssi_dl_base

		# Calculate a range for randomization
		rssi_min = self.rssi_dl_base - self.rssi_dl_threshold
		rssi_max = self.rssi_dl_base + self.rssi_dl_threshold

		# Generate a random RSSI value
		return random.randint(rssi_min, rssi_max)

	# Calculates a random RSSI value for Uplink bursts
	def calc_ul_rssi(self):
		# Check if randomization is required
		if self.rssi_ul_threshold is 0:
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
		if msg.tn not in self.ts_pass_list:
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

		# Timeslot filter
		if msg.tn not in self.ts_pass_list:
			print("[!] TS %u is not configured, dropping UL burst..." % msg.tn)
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
