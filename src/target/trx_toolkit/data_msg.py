#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# TRX Toolkit
# DATA interface message definitions and helpers
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

import random
import struct

from gsm_shared import *

class DATAMSG:
	""" TRXD (DATA) message codec (common part).

	The DATA messages are used to carry bursts in both directions
	between L1 and TRX. There exist two kinds of them:

	  - L12TRX (L1 -> TRX) - to be transmitted bursts,
	  - TRX2L1 (TRX -> L1) - received bursts.

	Both of them have quite similar structure, and start with
	the common fixed-size message header (no TLVs):

	  +---------------+-----------------+------------+
	  | common header | specific header | burst bits |
	  +---------------+-----------------+------------+

	while the message specific headers and bit types are different.

	The common header is represented by this class, which is the
	parent of both DATAMSG_L12TRX and DATAMSG_TRX2L2 (see below),
	and has the following fields:

	  +-----------------+----------------+-------------------+
	  | VER (1/2 octet) | TN (1/2 octet) | FN (4 octets, BE) |
	  +-----------------+----------------+-------------------+

	where:

	  - VER is the header version indicator (1/2 octet MSB),
	  - TN is TDMA time-slot number (1/2 octet LSB), and
	  - FN is TDMA frame number (4 octets, big endian).

	== Header version indication

	It may be necessary to extend the message specific header
	with more information. Since this is not a TLV-based
	protocol, we need to include the header format version.

	  +-----------------+------------------------+
	  | 7 6 5 4 3 2 1 0 | bit numbers            |
	  +-----------------+------------------------+
	  | X X X X . . . . | header version (0..15) |
	  +-----------------+------------------------+
	  | . . . . . X X X | TDMA TN (0..7)         |
	  +-----------------+------------------------+
	  | . . . . X . . . | RESERVED (0)           |
	  +-----------------+------------------------+

	Instead of prepending an additional byte, it was decided to use
	4 MSB bits of the first octet, which used to be zero-initialized
	due to the value range of TDMA TN. Therefore, the legacy header
	format has implicit version 0x00.

	Otherwise Wireshark (or trx_sniff.py) would need to guess the
	header version, or alternatively follow the control channel
	looking for the version setting command.

	The reserved bit number 3 can be used in the future to extend
	the TDMA TN range to (0..15), in case anybody would need
	to transfer UMTS bursts.

	"""

	# NOTE: up to 16 versions can be encoded
	CHDR_VERSION_MAX = 0b1111
	known_versions = [0x00]

	# Common constructor
	def __init__(self, fn = None, tn = None, burst = None, ver = 0):
		self.burst = burst
		self.ver = ver
		self.fn = fn
		self.tn = tn

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

	# Generate a random message specific burst
	def rand_burst(self):
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

	# Generates human-readable header description
	def desc_hdr(self):
		result = ""

		if self.ver > 0:
			result += ("ver=%u " % self.ver)

		if self.fn is not None:
			result += ("fn=%u " % self.fn)

		if self.tn is not None:
			result += ("tn=%u " % self.tn)

		return result

	# Converts unsigned soft-bits {254..0} to soft-bits {-127..127}
	@staticmethod
	def usbit2sbit(bits):
		buf = []

		for bit in bits:
			if bit == 0xff:
				buf.append(-127)
			else:
				buf.append(127 - bit)

		return buf

	# Converts soft-bits {-127..127} to unsigned soft-bits {254..0}
	@staticmethod
	def sbit2usbit(bits):
		buf = []

		for bit in bits:
			buf.append(127 - bit)

		return buf

	# Converts soft-bits {-127..127} to bits {1..0}
	@staticmethod
	def sbit2ubit(bits):
		buf = []

		for bit in bits:
			buf.append(1 if bit < 0 else 0)

		return buf

	# Converts bits {1..0} to soft-bits {-127..127}
	@staticmethod
	def ubit2sbit(bits):
		buf = []

		for bit in bits:
			buf.append(-127 if bit else 127)

		return buf

	# Validates the message fields
	def validate(self):
		if not self.ver in self.known_versions:
			return False

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

	# Generates a TRX DATA message
	def gen_msg(self, legacy = False):
		# Validate all the fields
		if not self.validate():
			raise ValueError("Message incomplete or incorrect")

		# Allocate an empty byte-array
		buf = bytearray()

		# Put version (4 bits) and TDMA TN (3 bits)
		buf.append((self.ver << 4) | (self.tn & 0x07))

		# Put TDMA FN (4 octets, BE)
		buf += struct.pack(">L", self.fn)

		# Generate message specific header part
		hdr = self.gen_hdr()
		buf += hdr

		# Generate burst
		buf += self.gen_burst()

		# This is a rudiment from (legacy) OpenBTS transceiver,
		# some L1 implementations still expect two dummy bytes.
		if legacy:
			buf += bytearray(2)

		return buf

	# Parses a TRX DATA message
	def parse_msg(self, msg):
		# Calculate message length
		length = len(msg)

		# Check length
		if length < (self.HDR_LEN + GSM_BURST_LEN):
			raise ValueError("Message is to short")

		# Parse version and TDMA TN
		self.ver = (msg[0] >> 4)
		self.tn = (msg[0] & 0x07)

		# Parse TDMA FN
		self.fn = struct.unpack(">L", msg[1:5])[0]

		# Specific message part
		self.parse_hdr(msg)

		# Copy burst, skipping header
		msg_burst = msg[self.HDR_LEN:]
		self.parse_burst(msg_burst)

class DATAMSG_L12TRX(DATAMSG):
	""" L12TRX (L1 -> TRX) message codec.

	This message represents a Downlink burst on the BTS side,
	or an Uplink burst on the MS side, and has the following
	message specific fixed-size header preceding the burst bits:

	  +-----+--------------------+
	  | PWR | hard-bits (1 or 0) |
	  +-----+--------------------+

	where PWR (1 octet) is relative (to the full-scale amplitude)
	transmit power level in dB. The absolute value is set on
	the control interface.

	Each hard-bit (1 or 0) of the burst is represented using one
	byte (0x01 or 0x00 respectively).

	"""

	# Constants
	HDR_LEN = 6
	PWR_MIN = 0x00
	PWR_MAX = 0xff

	# Specific message fields
	pwr = None

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

	# Generates human-readable header description
	def desc_hdr(self):
		# Describe the common part
		result = DATAMSG.desc_hdr(self)

		if self.pwr is not None:
			result += ("pwr=%u " % self.pwr)

		# Strip useless whitespace and return
		return result.strip()

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

	# Generate a random message specific burst
	def rand_burst(self, length = GSM_BURST_LEN):
		self.burst = []

		for i in range(length):
			ubit = random.randint(0, 1)
			self.burst.append(ubit)

	# Transforms this message to TRX2L1 message
	def gen_trx2l1(self, ver = None):
		# Allocate a new message
		msg = DATAMSG_TRX2L1(fn = self.fn, tn = self.tn,
			ver = self.ver if ver is None else ver)

		# Convert burst bits
		if self.burst is not None:
			msg.burst = self.ubit2sbit(self.burst)

		return msg

class DATAMSG_TRX2L1(DATAMSG):
	""" TRX2L1 (TRX -> L1) message codec.

	This message represents an Uplink burst on the BTS side,
	or a Downlink burst on the MS side, and has the following
	message specific fixed-size header preceding the burst bits:

	  +------+-----+--------------------+
	  | RSSI | ToA | soft-bits (254..0) |
	  +------+-----+--------------------+

	where:

	  - RSSI (1 octet) - Received Signal Strength Indication
			     encoded without the negative sign.
	  - ToA (2 octets) - Timing of Arrival in units of 1/256
			     of symbol (big endian).

	Unlike to be transmitted bursts, the received bursts are designated
	using the soft-bits notation, so the receiver can indicate its
	assurance from 0 to -127 that a given bit is 1, and from 0 to +127
	that a given bit is 0. The Viterbi algorithm allows to approximate
	the original sequence of hard-bits (1 or 0) using these values.

	Each soft-bit (-127..127) of the burst is encoded as an unsigned
	value in range (254..0) respectively using the constant shift.

	"""

	# Constants
	HDR_LEN = 8

	# rxlev2dbm(0..63) gives us [-110..-47], plus -10 dbm for noise
	RSSI_MIN = -120
	RSSI_MAX = -47

	# Min and max values of int16_t
	TOA256_MIN = -32768
	TOA256_MAX = 32767

	# Specific message fields
	rssi = None
	toa256 = None

	# Validates the message fields
	def validate(self):
		# Validate common fields
		if not DATAMSG.validate(self):
			return False

		if self.rssi is None:
			return False

		if self.rssi < self.RSSI_MIN or self.rssi > self.RSSI_MAX:
			return False

		if self.toa256 is None:
			return False

		if self.toa256 < self.TOA256_MIN or self.toa256 > self.TOA256_MAX:
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
	def rand_toa256(self, min = None, max = None):
		if min is None:
			min = self.TOA256_MIN

		if max is None:
			max = self.TOA256_MAX

		return random.randint(min, max)

	# Randomizes message specific header
	def rand_hdr(self):
		DATAMSG.rand_hdr(self)
		self.rssi = self.rand_rssi()
		self.toa256 = self.rand_toa256()

	# Generates human-readable header description
	def desc_hdr(self):
		# Describe the common part
		result = DATAMSG.desc_hdr(self)

		if self.rssi is not None:
			result += ("rssi=%d " % self.rssi)

		if self.toa256 is not None:
			result += ("toa256=%d " % self.toa256)

		# Strip useless whitespace and return
		return result.strip()

	# Generates message specific header part
	def gen_hdr(self):
		# Allocate an empty byte-array
		buf = bytearray()

		# Put RSSI
		buf.append(-self.rssi)

		# Encode ToA (Time of Arrival)
		# Big endian, 2 bytes (int32_t)
		buf += struct.pack(">h", self.toa256)

		return buf

	# Parses message specific header part
	def parse_hdr(self, hdr):
		# Parse RSSI
		self.rssi = -(hdr[5])

		# Parse ToA (Time of Arrival)
		self.toa256 = struct.unpack(">h", hdr[6:8])[0]

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

	# Generate a random message specific burst
	def rand_burst(self, length = GSM_BURST_LEN):
		self.burst = []

		for i in range(length):
			sbit = random.randint(-127, 127)
			self.burst.append(sbit)

	# Transforms this message to L12TRX message
	def gen_l12trx(self, ver = None):
		# Allocate a new message
		msg = DATAMSG_L12TRX(fn = self.fn, tn = self.tn,
			ver = self.ver if ver is None else ver)

		# Convert burst bits
		if self.burst is not None:
			msg.burst = self.sbit2ubit(self.burst)

		return msg

# Regression test
if __name__ == '__main__':
	import logging as log

	# Configure logging
	log.basicConfig(level = log.DEBUG,
		format = "[%(levelname)s] %(filename)s:%(lineno)d %(message)s")

	log.info("Generating the reference messages")

	# Create messages of both types
	msg_l12trx_ref = DATAMSG_L12TRX()
	msg_trx2l1_ref = DATAMSG_TRX2L1()

	# Validate header randomization
	for i in range(0, 100):
		msg_l12trx_ref.rand_hdr()
		msg_trx2l1_ref.rand_hdr()

		msg_l12trx_ref.rand_burst()
		msg_trx2l1_ref.rand_burst()

		assert(msg_l12trx_ref.validate())
		assert(msg_trx2l1_ref.validate())

	log.info("Validate header randomization: OK")

	log.info("Encoding the reference messages")

	# Encode DATA messages
	l12trx_raw = msg_l12trx_ref.gen_msg()
	trx2l1_raw = msg_trx2l1_ref.gen_msg()

	# Encode a TRX2L1 message in legacy mode
	trx2l1_raw_legacy = msg_trx2l1_ref.gen_msg(legacy = True)

	log.info("Parsing generated messages back")

	# Parse generated DATA messages
	msg_l12trx_dec = DATAMSG_L12TRX()
	msg_trx2l1_dec = DATAMSG_TRX2L1()
	msg_l12trx_dec.parse_msg(l12trx_raw)
	msg_trx2l1_dec.parse_msg(trx2l1_raw)

	# Parse generated TRX2L1 message in legacy mode
	msg_trx2l1_legacy_dec = DATAMSG_TRX2L1()
	msg_trx2l1_legacy_dec.parse_msg(trx2l1_raw_legacy)

	log.info("Comparing decoded messages with the reference")

	# Compare bursts
	assert(msg_l12trx_dec.burst == msg_l12trx_ref.burst)
	assert(msg_trx2l1_dec.burst == msg_trx2l1_ref.burst)
	assert(msg_trx2l1_legacy_dec.burst == msg_trx2l1_ref.burst)

	log.info("Compare bursts: OK")

	# Compare both parsed messages with the reference data
	assert(msg_l12trx_dec.fn == msg_l12trx_ref.fn)
	assert(msg_trx2l1_dec.fn == msg_trx2l1_ref.fn)
	assert(msg_l12trx_dec.tn == msg_l12trx_ref.tn)
	assert(msg_trx2l1_dec.tn == msg_trx2l1_ref.tn)

	log.info("Compare FN / TN: OK")

	# Compare message specific parts
	assert(msg_trx2l1_dec.rssi == msg_trx2l1_ref.rssi)
	assert(msg_l12trx_dec.pwr == msg_l12trx_ref.pwr)
	assert(msg_trx2l1_dec.toa256 == msg_trx2l1_ref.toa256)

	log.info("Compare message specific data: OK")

	# Bit conversation test
	usbits_ref = list(range(0, 256))
	sbits_ref = list(range(-127, 128))

	# Test both usbit2sbit() and sbit2usbit()
	sbits = DATAMSG.usbit2sbit(usbits_ref)
	usbits = DATAMSG.sbit2usbit(sbits)
	assert(usbits[:255] == usbits_ref[:255])
	assert(usbits[255] == 254)

	log.info("Check both usbit2sbit() and sbit2usbit(): OK")

	# Test both sbit2ubit() and ubit2sbit()
	ubits = DATAMSG.sbit2ubit(sbits_ref)
	assert(ubits == ([1] * 127 + [0] * 128))

	sbits = DATAMSG.ubit2sbit(ubits)
	assert(sbits == ([-127] * 127 + [127] * 128))

	log.info("Check both sbit2ubit() and ubit2sbit(): OK")

	# Test message transformation
	msg_l12trx_dec = msg_trx2l1_ref.gen_l12trx()
	msg_trx2l1_dec = msg_l12trx_ref.gen_trx2l1()

	assert(msg_l12trx_dec.fn == msg_trx2l1_ref.fn)
	assert(msg_l12trx_dec.tn == msg_trx2l1_ref.tn)

	assert(msg_trx2l1_dec.fn == msg_l12trx_ref.fn)
	assert(msg_trx2l1_dec.tn == msg_l12trx_ref.tn)

	assert(msg_l12trx_dec.burst == DATAMSG.sbit2ubit(msg_trx2l1_ref.burst))
	assert(msg_trx2l1_dec.burst == DATAMSG.ubit2sbit(msg_l12trx_ref.burst))

	log.info("Check L12TRX <-> TRX2L1 type transformations: OK")

	# Test header version coding
	for ver in DATAMSG.known_versions:
		# Create messages of both types
		msg_l12trx = DATAMSG_L12TRX(ver = ver)
		msg_trx2l1 = DATAMSG_TRX2L1(ver = ver)

		# Randomize message specific headers
		msg_l12trx.rand_hdr()
		msg_trx2l1.rand_hdr()

		# Randomize bursts
		msg_l12trx.rand_burst()
		msg_trx2l1.rand_burst()

		# Encode DATA messages
		msg_l12trx_enc = msg_l12trx.gen_msg()
		msg_trx2l1_enc = msg_trx2l1.gen_msg()

		# Parse generated DATA messages
		msg_l12trx_dec = DATAMSG_L12TRX()
		msg_trx2l1_dec = DATAMSG_TRX2L1()
		msg_l12trx_dec.parse_msg(msg_l12trx_enc)
		msg_trx2l1_dec.parse_msg(msg_trx2l1_enc)

		# Match the header version
		assert(msg_l12trx_dec.ver == ver)
		assert(msg_trx2l1_dec.ver == ver)

		# Match common TDMA fields
		assert(msg_l12trx_dec.tn == msg_l12trx.tn)
		assert(msg_trx2l1_dec.fn == msg_trx2l1.fn)

		# Compare bursts
		assert(msg_l12trx_dec.burst == msg_l12trx.burst)
		assert(msg_trx2l1_dec.burst == msg_trx2l1.burst)

		log.info("Check header version %u coding: OK" % ver)

		msg_trx2l1_gen = msg_l12trx.gen_trx2l1()
		msg_l12trx_gen = msg_trx2l1.gen_l12trx()

		assert(msg_trx2l1_gen is not None)
		assert(msg_l12trx_gen is not None)

		# Match the header version
		assert(msg_trx2l1_gen.ver == ver)
		assert(msg_l12trx_gen.ver == ver)

		# Match common TDMA fields
		assert(msg_trx2l1_gen.tn == msg_l12trx.tn)
		assert(msg_l12trx_gen.fn == msg_trx2l1.fn)

		log.info("Verify direct transformation: OK")
