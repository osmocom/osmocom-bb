#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# Virtual Um-interface (fake transceiver)
# DATA interface message definitions and helpers
#
# (C) 2018 by Vadim Yanitskiy <axilirator@gmail.com>
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

from gsm_shared import *

class DATAMSG:
	# Common message fields
	burst = None
	fn = None
	tn = None

	# HACK: Abstract class definition
	def __init__(self):
		raise NotImplementedError

	# Generates message specific header
	def gen_hdr(self):
		raise NotImplementedError

	# Parses message specific header
	def parse_hdr(self, hdr):
		raise NotImplementedError

	# Generates message specific burst
	def gen_burst(self):
		raise NotImplementedError

	# Parses message specific burst
	def parse_burst(self, burst):
		raise NotImplementedError

	# Generates a random frame number
	def rand_fn(self):
		return random.randint(0, GSM_HYPERFRAME)

	# Generates a random timeslot number
	def rand_tn(self):
		return random.randint(0, 7)

	# Randomizes the message header
	def rand_hdr(self):
		self.fn = self.rand_fn()
		self.tn = self.rand_tn()

	# Converts unsigned soft-bits {254..0} to soft-bits {-127..127}
	def usbit2sbit(self, bits):
		buf = []

		for bit in bits:
			if bit == 0xff:
				buf.append(-127)
			else:
				buf.append(127 - bit)

		return buf

	# Converts soft-bits {-127..127} to unsigned soft-bits {254..0}
	def sbit2usbit(self, bits):
		buf = []

		for bit in bits:
			buf.append(127 - bit)

		return buf

	# Converts soft-bits {-127..127} to bits {1..0}
	def sbit2ubit(self, bits):
		buf = []

		for bit in bits:
			buf.append(1 if bit < 0 else 0)

		return buf

	# Converts bits {1..0} to soft-bits {-127..127}
	def ubit2sbit(self, bits):
		buf = []

		for bit in bits:
			buf.append(-127 if bit else 127)

		return buf

	# Validates the message fields
	def validate(self):
		if self.burst is None:
			return False

		if len(self.burst) not in (GSM_BURST_LEN, EDGE_BURST_LEN):
			return False

		if self.fn is None:
			return False

		if self.fn < 0 or self.fn > GSM_HYPERFRAME:
			return False

		if self.tn is None:
			return False

		if self.tn < 0 or self.tn > 7:
			return False

		return True

	# Generates frame number to bytes
	def gen_fn(self, fn):
		# Allocate an empty byte-array
		buf = bytearray()

		# Big endian, 4 bytes
		buf.append((fn >> 24) & 0xff)
		buf.append((fn >> 16) & 0xff)
		buf.append((fn >>  8) & 0xff)
		buf.append((fn >>  0) & 0xff)

		return buf

	# Parses frame number from bytes
	def parse_fn(self, buf):
		# Big endian, 4 bytes
		return (buf[0] << 24) \
			 | (buf[1] << 16) \
			 | (buf[2] << 8)  \
			 | (buf[3] << 0)

	# Generates a TRX DATA message
	def gen_msg(self):
		# Validate all the fields
		if not self.validate():
			raise ValueError("Message incomplete or incorrect")

		# Allocate an empty byte-array
		buf = bytearray()

		# Put timeslot index
		buf.append(self.tn)

		# Put frame number
		fn = self.gen_fn(self.fn)
		buf += fn

		# Generate message specific header part
		hdr = self.gen_hdr()
		buf += hdr

		# Generate burst
		buf += self.gen_burst()

		return buf

	# Parses a TRX DATA message
	def parse_msg(self, msg):
		# Calculate message length
		length = len(msg)

		# Check length
		if length < (self.HDR_LEN + GSM_BURST_LEN):
			raise ValueError("Message is to short")

		# Parse both fn and tn
		self.fn = self.parse_fn(msg[1:])
		self.tn = msg[0]

		# Specific message part
		self.parse_hdr(msg)

		# Copy burst, skipping header
		msg_burst = msg[self.HDR_LEN:]
		self.parse_burst(msg_burst)

class DATAMSG_L12TRX(DATAMSG):
	# Constants
	HDR_LEN = 6
	PWR_MIN = 0x00
	PWR_MAX = 0xff

	# Specific message fields
	pwr = None

	def __init__(self, fn = None, tn = None, pwr = None, burst = None):
		# Init local variables
		self.burst = burst
		self.pwr = pwr
		self.fn = fn
		self.tn = tn

	# Validates the message fields
	def validate(self):
		# Validate common fields
		if not DATAMSG.validate(self):
			return False

		if self.pwr is None:
			return False

		if self.pwr < self.PWR_MIN or self.pwr > self.PWR_MAX:
			return False

		return True

	# Generates a random power level
	def rand_pwr(self, min = None, max = None):
		if min is None:
			min = self.PWR_MIN

		if max is None:
			max = self.PWR_MAX

		return random.randint(min, max)

	# Randomizes message specific header
	def rand_hdr(self):
		DATAMSG.rand_hdr(self)
		self.pwr = self.rand_pwr()

	# Generates message specific header part
	def gen_hdr(self):
		# Allocate an empty byte-array
		buf = bytearray()

		# Put power
		buf.append(self.pwr)

		return buf

	# Parses message specific header part
	def parse_hdr(self, hdr):
		# Parse power level
		self.pwr = hdr[5]

	# Generates message specific burst
	def gen_burst(self):
		# Copy burst 'as is'
		return bytearray(self.burst)

	# Parses message specific burst
	def parse_burst(self, burst):
		length = len(burst)

		# Distinguish between GSM and EDGE
		if length >= EDGE_BURST_LEN:
			self.burst = list(burst[:EDGE_BURST_LEN])
		else:
			self.burst = list(burst[:GSM_BURST_LEN])

class DATAMSG_TRX2L1(DATAMSG):
	# Constants
	HDR_LEN = 8
	RSSI_MIN = -120
	RSSI_MAX = -50

	# TODO: verify this range
	TOA_MIN = -10.0
	TOA_MAX = 10.0

	# Specific message fields
	rssi = None
	toa = None

	def __init__(self, fn = None, tn = None, rssi = None, toa = None, burst = None):
		# Init local variables
		self.burst = burst
		self.rssi = rssi
		self.toa = toa
		self.fn = fn
		self.tn = tn

	# Validates the message fields
	def validate(self):
		# Validate common fields
		if not DATAMSG.validate(self):
			return False

		if self.rssi is None:
			return False

		if self.rssi < self.RSSI_MIN or self.rssi > self.RSSI_MAX:
			return False

		if self.toa is None:
			return False

		if self.toa < self.TOA_MIN or self.toa > self.TOA_MAX:
			return False

		return True

	# Generates a random RSSI value
	def rand_rssi(self, min = None, max = None):
		if min is None:
			min = self.RSSI_MIN

		if max is None:
			max = self.RSSI_MAX

		return random.randint(min, max)

	# Generates a ToA (Time of Arrival) value
	def rand_toa(self, min = None, max = None):
		if min is None:
			min = self.TOA_MIN

		if max is None:
			max = self.TOA_MAX

		return random.uniform(min, max)

	# Randomizes message specific header
	def rand_hdr(self):
		DATAMSG.rand_hdr(self)
		self.rssi = self.rand_rssi()
		self.toa = self.rand_toa()

	# Generates message specific header part
	def gen_hdr(self):
		# Allocate an empty byte-array
		buf = bytearray()

		# Put RSSI
		buf.append(-self.rssi)

		# Round ToA (Time of Arrival) to closest integer
		toa = int(self.toa * 256.0 + 0.5)

		# Encode ToA
		buf.append((toa >> 8) & 0xff)
		buf.append(toa & 0xff)

		return buf

	# Parses message specific header part
	def parse_hdr(self, hdr):
		# Parse RSSI
		self.rssi = -(hdr[5])

		# Parse ToA (Time of Arrival)
		# FIXME: parsing unsupported
		self.toa = None

	# Generates message specific burst
	def gen_burst(self):
		# Convert soft-bits to unsigned soft-bits
		burst_usbits = self.sbit2usbit(self.burst)

		# Encode to bytes
		return bytearray(burst_usbits)

	# Parses message specific burst
	def parse_burst(self, burst):
		length = len(burst)

		# Distinguish between GSM and EDGE
		if length >= EDGE_BURST_LEN:
			burst_usbits = list(burst[:EDGE_BURST_LEN])
		else:
			burst_usbits = list(burst[:GSM_BURST_LEN])

		# Convert unsigned soft-bits to soft-bits
		burst_sbits = self.usbit2sbit(burst_usbits)

		# Save
		self.burst = burst_sbits

# Regression test
if __name__ == '__main__':
	# Common reference data
	fn = 1024
	tn = 0

	# Generate two random bursts
	burst_l12trx_ref = []
	burst_trx2l1_ref = []

	for i in range(0, GSM_BURST_LEN):
		ubit = random.randint(0, 1)
		burst_l12trx_ref.append(ubit)

		sbit = random.randint(-127, 127)
		burst_trx2l1_ref.append(sbit)

	print("[i] Generating the reference messages")

	# Create messages of both types
	msg_l12trx_ref = DATAMSG_L12TRX(fn = fn, tn = tn)
	msg_trx2l1_ref = DATAMSG_TRX2L1(fn = fn, tn = tn)

	# Fill in message specific fields
	msg_trx2l1_ref.rssi = -88
	msg_l12trx_ref.pwr = 0x33
	msg_trx2l1_ref.toa = -0.6

	# Specify the reference bursts
	msg_l12trx_ref.burst = burst_l12trx_ref
	msg_trx2l1_ref.burst = burst_trx2l1_ref

	print("[i] Encoding the reference messages")

	# Encode DATA messages
	l12trx_raw = msg_l12trx_ref.gen_msg()
	trx2l1_raw = msg_trx2l1_ref.gen_msg()

	print("[i] Parsing generated messages back")

	# Parse generated DATA messages
	msg_l12trx_dec = DATAMSG_L12TRX()
	msg_trx2l1_dec = DATAMSG_TRX2L1()
	msg_l12trx_dec.parse_msg(l12trx_raw)
	msg_trx2l1_dec.parse_msg(trx2l1_raw)

	print("[i] Comparing decoded messages with the reference")

	# Compare bursts
	assert(msg_l12trx_dec.burst == burst_l12trx_ref)
	assert(msg_trx2l1_dec.burst == burst_trx2l1_ref)

	print("[?] Compare bursts: OK")

	# Compare both parsed messages with the reference data
	assert(msg_l12trx_dec.fn == fn)
	assert(msg_trx2l1_dec.fn == fn)
	assert(msg_l12trx_dec.tn == tn)
	assert(msg_trx2l1_dec.tn == tn)

	print("[?] Compare FN / TN: OK")

	# Compare message specific parts
	assert(msg_trx2l1_dec.rssi == msg_trx2l1_ref.rssi)
	assert(msg_l12trx_dec.pwr == msg_l12trx_ref.pwr)

	# FIXME: ToA check disabled until the parsing is implemented
	# assert(msg_trx2l1_dec.toa == msg_trx2l1_ref.toa)

	print("[?] Compare message specific data: OK")

	# Validate header randomization
	for i in range(0, 100):
		msg_l12trx_ref.rand_hdr()
		msg_trx2l1_ref.rand_hdr()

		assert(msg_l12trx_ref.validate())
		assert(msg_trx2l1_ref.validate())

	print("[?] Validate header randomization: OK")

	# Bit conversation test
	usbits_ref = range(0, 256)
	sbits_ref = range(-127, 128)

	# Test both usbit2sbit() and sbit2usbit()
	sbits = msg_trx2l1_ref.usbit2sbit(usbits_ref)
	usbits = msg_trx2l1_ref.sbit2usbit(sbits)
	assert(usbits[:255] == usbits_ref[:255])
	assert(usbits[255] == 254)

	print("[?] Check both usbit2sbit() and sbit2usbit(): OK")

	# Test both sbit2ubit() and ubit2sbit()
	ubits = msg_trx2l1_ref.sbit2ubit(sbits_ref)
	assert(ubits == ([1] * 127 + [0] * 128))

	sbits = msg_trx2l1_ref.ubit2sbit(ubits)
	assert(sbits == ([-127] * 127 + [127] * 128))

	print("[?] Check both sbit2ubit() and ubit2sbit(): OK")
