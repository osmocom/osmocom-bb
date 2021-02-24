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
import abc

from enum import Enum
from gsm_shared import *

class Modulation(Enum):
	""" Modulation types defined in 3GPP TS 45.002 """
	ModGMSK  = (0b0000, 148)
	Mod8PSK  = (0b0100, 444)
	ModAQPSK = (0b0110, 296)
	Mod16QAM = (0b1000, 592)
	Mod32QAM = (0b1010, 740)

	def __init__(self, coding, bl):
		# Coding in TRXD header
		self.coding = coding
		# Burst length
		self.bl = bl

	@classmethod
	def pick(self, coding):
		for mod in list(self):
			if mod.coding == coding:
				return mod
		return None

	@classmethod
	def pick_by_bl(self, bl):
		for mod in list(self):
			if mod.bl == bl:
				return mod
		return None

class DATAMSG(abc.ABC):
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
	known_versions = [0x00, 0x01]

	def __init__(self, fn = None, tn = None, burst = None, ver = 0):
		self.burst = burst
		self.ver = ver
		self.fn = fn
		self.tn = tn

	@property
	def CHDR_LEN(self):
		''' The common header length. '''
		return 1 + 4 # (VER + TN) + FN

	@abc.abstractmethod
	def gen_hdr(self):
		''' Generate message specific header. '''

	@abc.abstractmethod
	def parse_hdr(self, hdr):
		''' Parse message specific header. '''

	@abc.abstractmethod
	def gen_burst(self):
		''' Generate message specific burst. '''

	@abc.abstractmethod
	def parse_burst(self, burst):
		''' Parse message specific burst. '''

	@abc.abstractmethod
	def rand_burst(self):
		''' Generate a random message specific burst. '''

	def rand_fn(self):
		''' Generate a random frame number. '''
		return random.randint(0, GSM_HYPERFRAME)

	def rand_tn(self):
		''' Generate a random timeslot number. '''
		return random.randint(0, 7)

	def rand_hdr(self):
		''' Randomize the message header. '''
		self.fn = self.rand_fn()
		self.tn = self.rand_tn()

	def desc_hdr(self):
		''' Generate human-readable header description. '''

		result = ""

		if self.ver > 0:
			result += ("ver=%u " % self.ver)

		if self.fn is not None:
			result += ("fn=%u " % self.fn)

		if self.tn is not None:
			result += ("tn=%u " % self.tn)

		if self.burst is not None and len(self.burst) > 0:
			result += ("bl=%u " % len(self.burst))

		return result

	@staticmethod
	def usbit2sbit(bits):
		''' Convert unsigned soft-bits {254..0} to soft-bits {-127..127}. '''
		return [-127 if (b == 0xff) else 127 - b for b in bits]

	@staticmethod
	def sbit2usbit(bits):
		''' Convert soft-bits {-127..127} to unsigned soft-bits {254..0}. '''
		return [127 - b for b in bits]

	@staticmethod
	def sbit2ubit(bits):
		''' Convert soft-bits {-127..127} to bits {1..0}. '''
		return [int(b < 0) for b in bits]

	@staticmethod
	def ubit2sbit(bits):
		''' Convert bits {1..0} to soft-bits {-127..127}. '''
		return [-127 if b else 127 for b in bits]

	def validate(self):
		''' Validate the message fields (throws ValueError). '''

		if not self.ver in self.known_versions:
			raise ValueError("Unknown TRXD header version %d" % self.ver)

		if self.fn is None:
			raise ValueError("TDMA frame-number is not set")

		if self.fn < 0 or self.fn > GSM_HYPERFRAME:
			raise ValueError("TDMA frame-number %d is out of range" % self.fn)

		if self.tn is None:
			raise ValueError("TDMA time-slot is not set")

		if self.tn < 0 or self.tn > 7:
			raise ValueError("TDMA time-slot %d is out of range" % self.tn)

	def gen_msg(self, legacy = False):
		''' Generate a TRX DATA message. '''

		# Validate all the fields
		self.validate()

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
		if self.burst is not None:
			buf += self.gen_burst()

		# This is a rudiment from (legacy) OpenBTS transceiver,
		# some L1 implementations still expect two dummy bytes.
		if legacy and self.ver == 0x00:
			buf += bytearray(2)

		return buf

	def parse_msg(self, msg):
		''' Parse a TRX DATA message. '''

		# Make sure we have at least common header
		if len(msg) < self.CHDR_LEN:
			raise ValueError("Message is to short: missing common header")

		# Parse the header version first
		self.ver = (msg[0] >> 4)
		if not self.ver in self.known_versions:
			raise ValueError("Unknown TRXD header version %d" % self.ver)

		# Parse TDMA TN and FN
		self.tn = (msg[0] & 0x07)
		self.fn = struct.unpack(">L", msg[1:5])[0]

		# Make sure we have the whole header,
		# including the version specific fields
		if len(msg) < self.HDR_LEN:
			raise ValueError("Message is to short: missing version specific header")

		# Specific message part
		self.parse_hdr(msg)

		# Copy burst, skipping header
		msg_burst = msg[self.HDR_LEN:]
		if len(msg_burst) > 0:
			self.parse_burst(msg_burst)
		else:
			self.burst = None

class DATAMSG_L12TRX(DATAMSG):
	""" L12TRX (L1 -> TRX) message codec.

	This message represents a Downlink burst on the BTS side,
	or an Uplink burst on the MS side, and has the following
	message specific fixed-size header preceding the burst bits:

	== Versions 0x00, 0x01

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
	PWR_MIN = 0x00
	PWR_MAX = 0xff

	# Specific message fields
	pwr = None

	@property
	def HDR_LEN(self):
		''' Calculate header length depending on its version. '''

		# Common header length
		length = self.CHDR_LEN

		# Message specific header length
		if self.ver in (0x00, 0x01):
			length += 1 # PWR
		else:
			raise IndexError("Unhandled version %u" % self.ver)

		return length

	def validate(self):
		''' Validate the message fields (throws ValueError). '''

		# Validate common fields
		DATAMSG.validate(self)

		if self.pwr is None:
			raise ValueError("Tx Attenuation level is not set")

		if self.pwr < self.PWR_MIN or self.pwr > self.PWR_MAX:
			raise ValueError("Tx Attenuation %d is out of range" % self.pwr)

		# FIXME: properly handle IDLE / NOPE indications
		if self.burst is None:
			raise ValueError("Tx burst bits are not set")

		# FIXME: properly handle IDLE / NOPE indications
		if len(self.burst) not in (GSM_BURST_LEN, EDGE_BURST_LEN):
			raise ValueError("Tx burst has odd length %u" % len(self.burst))

	def rand_pwr(self, min = None, max = None):
		''' Generate a random power level. '''

		if min is None:
			min = self.PWR_MIN

		if max is None:
			max = self.PWR_MAX

		return random.randint(min, max)

	def rand_hdr(self):
		''' Randomize message specific header. '''

		DATAMSG.rand_hdr(self)
		self.pwr = self.rand_pwr()

	def desc_hdr(self):
		''' Generate human-readable header description. '''

		# Describe the common part
		result = DATAMSG.desc_hdr(self)

		if self.pwr is not None:
			result += ("pwr=%u " % self.pwr)

		# Strip useless whitespace and return
		return result.strip()

	def gen_hdr(self):
		''' Generate message specific header part. '''

		# Allocate an empty byte-array
		buf = bytearray()

		# Put power
		buf.append(self.pwr)

		return buf

	def parse_hdr(self, hdr):
		''' Parse message specific header part. '''

		# Parse power level
		self.pwr = hdr[5]

	def gen_burst(self):
		''' Generate message specific burst. '''

		# Copy burst 'as is'
		return bytearray(self.burst)

	def parse_burst(self, burst):
		''' Parse message specific burst. '''

		length = len(burst)

		# Distinguish between GSM and EDGE
		if length >= EDGE_BURST_LEN:
			self.burst = list(burst[:EDGE_BURST_LEN])
		else:
			self.burst = list(burst[:GSM_BURST_LEN])

	def rand_burst(self, length = GSM_BURST_LEN):
		''' Generate a random message specific burst. '''
		self.burst = [random.randint(0, 1) for _ in range(length)]

	def gen_trx2l1(self, ver = None):
		''' Transform this message to TRX2L1 message. '''

		# Allocate a new message
		msg = DATAMSG_TRX2L1(fn = self.fn, tn = self.tn,
			ver = self.ver if ver is None else ver)

		# Convert burst bits
		if self.burst is not None:
			msg.burst = self.ubit2sbit(self.burst)
		else:
			msg.nope_ind = True

		return msg

class DATAMSG_TRX2L1(DATAMSG):
	""" TRX2L1 (TRX -> L1) message codec.

	This message represents an Uplink burst on the BTS side,
	or a Downlink burst on the MS side, and has the following
	message specific fixed-size header preceding the burst bits:

	== Version 0x00

	  +------+-----+--------------------+
	  | RSSI | ToA | soft-bits (254..0) |
	  +------+-----+--------------------+

	== Version 0x01

	  +------+-----+-----+-----+--------------------+
	  | RSSI | ToA | MTS | C/I | soft-bits (254..0) |
	  +------+-----+-----+-----+--------------------+

	where:

	  - RSSI (1 octet) - Received Signal Strength Indication
			     encoded without the negative sign.
	  - ToA (2 octets) - Timing of Arrival in units of 1/256
			     of symbol (big endian).
	  - MTS (1 octet)  - Modulation and Training Sequence info.
	  - C/I (2 octets) - Carrier-to-Interference ratio (big endian).

	== Coding of MTS: Modulation and Training Sequence info

	3GPP TS 45.002 version 15.1.0 defines several modulation types,
	and a few sets of training sequences for each type. The most
	common are GMSK and 8-PSK (which is used in EDGE).

	  +-----------------+---------------------------------------+
	  | 7 6 5 4 3 2 1 0 | bit numbers (value range)             |
	  +-----------------+---------------------------------------+
	  | X . . . . . . . | IDLE / nope frame indication (0 or 1) |
	  +-----------------+---------------------------------------+
	  | . X X X X . . . | Modulation, TS set number (see below) |
	  +-----------------+---------------------------------------+
	  | . . . . . X X X | Training Sequence Code (0..7)         |
	  +-----------------+---------------------------------------+

	The bit number 7 (MSB) is set to high when either nothing has been
	detected, or during IDLE frames, so we can deliver noise levels,
	and avoid clock gaps on the L1 side. Other bits are ignored,
	and should be set to low (0) in this case.

	== Coding of modulation and TS set number

	GMSK has 4 sets of training sequences (see tables 5.2.3a-d),
	while 8-PSK (see tables 5.2.3f-g) and the others have 2 sets.
	Access and Synchronization bursts also have several synch.
	sequences.

	  +-----------------+---------------------------------------+
	  | 7 6 5 4 3 2 1 0 | bit numbers (value range)             |
	  +-----------------+---------------------------------------+
	  | . 0 0 X X . . . | GMSK, 4 TS sets (0..3)                |
	  +-----------------+---------------------------------------+
	  | . 0 1 0 X . . . | 8-PSK, 2 TS sets (0..1)               |
	  +-----------------+---------------------------------------+
	  | . 0 1 1 X . . . | AQPSK, 2 TS sets (0..1)               |
	  +-----------------+---------------------------------------+
	  | . 1 0 0 X . . . | 16QAM, 2 TS sets (0..1)               |
	  +-----------------+---------------------------------------+
	  | . 1 0 1 X . . . | 32QAM, 2 TS sets (0..1)               |
	  +-----------------+---------------------------------------+
	  | . 1 1 1 X . . . | RESERVED (0)                          |
	  +-----------------+---------------------------------------+

	== C/I: Carrier-to-Interference ratio

	The C/I value can be computed from the training sequence of each
	burst, where we can compare the "ideal" training sequence with
	the actual training sequence and then express that in centiBels.

	== Coding of the burst bits

	Unlike the transmitted bursts, the received bursts are designated
	using the soft-bits notation, so the receiver can indicate its
	assurance from 0 to -127 that a given bit is 1, and from 0 to +127
	that a given bit is 0. The Viterbi algorithm allows to approximate
	the original sequence of hard-bits (1 or 0) using these values.

	Each soft-bit (-127..127) of the burst is encoded as an unsigned
	value in range (0..255) respectively using the constant shift.

	"""

	# rxlev2dbm(0..63) gives us [-110..-47], plus -10 dbm for noise
	RSSI_MIN = -120
	RSSI_MAX = -47

	# Min and max values of int16_t
	TOA256_MIN = -32768
	TOA256_MAX = 32767

	# TSC (Training Sequence Code) range
	TSC_RANGE = range(0, 8)

	# C/I range (in centiBels)
	CI_MIN = -1280
	CI_MAX = 1280

	# IDLE frame / nope detection indicator
	NOPE_IND = (1 << 7)

	# Specific message fields
	rssi = None
	toa256 = None

	# Version 0x01 specific (default values)
	mod_type = Modulation.ModGMSK
	nope_ind = False

	tsc_set = None
	tsc = None
	ci = None

	@property
	def HDR_LEN(self):
		''' Calculate header length depending on its version. '''

		# Common header length
		length = self.CHDR_LEN

		# Message specific header length
		if self.ver == 0x00:
			# RSSI + ToA
			length += 1 + 2
		elif self.ver == 0x01:
			# RSSI + ToA + TS + C/I
			length += 1 + 2 + 1 + 2
		else:
			raise IndexError("Unhandled version %u" % self.ver)

		return length

	def _validate_burst_v0(self):
		# Burst is mandatory
		if self.burst is None:
			raise ValueError("Rx burst bits are not set")

		# ... and can be either of GSM (GMSK) or EDGE (8-PSK)
		if len(self.burst) not in (GSM_BURST_LEN, EDGE_BURST_LEN):
			raise ValueError("Rx burst has odd length %u" % len(self.burst))

	def _validate_burst_v1(self):
		# Burst is omitted in case of an IDLE / NOPE indication
		if self.nope_ind and self.burst is None:
			return

		if self.nope_ind and self.burst is not None:
			raise ValueError("NOPE.ind comes with burst?!?")
		if self.burst is None:
			raise ValueError("Rx burst bits are not set")

		# Burst length depends on modulation type
		if len(self.burst) != self.mod_type.bl:
			raise ValueError("Rx burst has odd length %u" % len(self.burst))

	def validate_burst(self):
		''' Validate the burst (throws ValueError). '''

		if self.ver == 0x00:
			self._validate_burst_v0()
		elif self.ver >= 0x01:
			self._validate_burst_v1()

	def validate(self):
		''' Validate the message header fields (throws ValueError). '''

		# Validate common fields
		DATAMSG.validate(self)

		if self.rssi is None:
			raise ValueError("RSSI is not set")

		if self.rssi < self.RSSI_MIN or self.rssi > self.RSSI_MAX:
			raise ValueError("RSSI %d is out of range" % self.rssi)

		if self.toa256 is None:
			raise ValueError("ToA256 is not set")

		if self.toa256 < self.TOA256_MIN or self.toa256 > self.TOA256_MAX:
			raise ValueError("ToA256 %d is out of range" % self.toa256)

		# Version specific parameters (omited for NOPE.ind)
		if self.ver >= 0x01 and not self.nope_ind:
			if type(self.mod_type) is not Modulation:
				raise ValueError("Unknown Rx modulation type")

			if self.tsc_set is None:
				raise ValueError("TSC set is not set")

			if self.mod_type is Modulation.ModGMSK:
				if self.tsc_set not in range(0, 4):
					raise ValueError("TSC set %d is out of range" % self.tsc_set)
			else:
				if self.tsc_set not in range(0, 2):
					raise ValueError("TSC set %d is out of range" % self.tsc_set)

			if self.tsc is None:
				raise ValueError("TSC is not set")

			if self.tsc not in self.TSC_RANGE:
				raise ValueError("TSC %d is out of range" % self.tsc)

		# Version specific parameters (also present in NOPE.ind)
		if self.ver >= 0x01:
			if self.ci is None:
				raise ValueError("C/I is not set")

			if self.ci < self.CI_MIN or self.ci > self.CI_MAX:
				raise ValueError("C/I %d is out of range" % self.ci)

		self.validate_burst()

	def rand_rssi(self, min = None, max = None):
		''' Generate a random RSSI value. '''

		if min is None:
			min = self.RSSI_MIN

		if max is None:
			max = self.RSSI_MAX

		return random.randint(min, max)

	def rand_toa256(self, min = None, max = None):
		''' Generate a random ToA (Time of Arrival) value. '''

		if min is None:
			min = self.TOA256_MIN

		if max is None:
			max = self.TOA256_MAX

		return random.randint(min, max)

	def rand_hdr(self):
		''' Randomize message specific header. '''

		DATAMSG.rand_hdr(self)
		self.rssi = self.rand_rssi()
		self.toa256 = self.rand_toa256()

		if self.ver >= 0x01:
			self.mod_type = random.choice(list(Modulation))
			if self.mod_type is Modulation.ModGMSK:
				self.tsc_set = random.randint(0, 3)
			else:
				self.tsc_set = random.randint(0, 1)
			self.tsc = random.choice(self.TSC_RANGE)

			# C/I: Carrier-to-Interference ratio
			self.ci = random.randint(self.CI_MIN, self.CI_MAX)

	def desc_hdr(self):
		''' Generate human-readable header description. '''

		# Describe the common part
		result = DATAMSG.desc_hdr(self)

		if self.rssi is not None:
			result += ("rssi=%d " % self.rssi)

		if self.toa256 is not None:
			result += ("toa256=%d " % self.toa256)

		if self.ver >= 0x01:
			if not self.nope_ind:
				if self.mod_type is not None:
					result += ("%s " % self.mod_type)
				if self.tsc_set is not None:
					result += ("set=%u " % self.tsc_set)
				if self.tsc is not None:
					result += ("tsc=%u " % self.tsc)
				if self.ci is not None:
					result += ("C/I=%d cB " % self.ci)
			else:
				result += "(IDLE / NOPE IND) "

		# Strip useless whitespace and return
		return result.strip()

	def gen_mts(self):
		''' Encode Modulation and Training Sequence info. '''

		# IDLE / nope indication has no MTS info
		if self.nope_ind:
			return self.NOPE_IND

		# TSC: . . . . . X X X
		mts = self.tsc & 0b111

		# MTS: . X X X X . . .
		mts |= self.mod_type.coding << 3
		mts |= self.tsc_set << 3

		return mts

	def parse_mts(self, mts):
		''' Parse Modulation and Training Sequence info. '''

		# IDLE / nope indication has no MTS info
		self.nope_ind = (mts & self.NOPE_IND) > 0
		if self.nope_ind:
			self.mod_type = None
			self.tsc_set = None
			self.tsc = None
			return

		# TSC: . . . . . X X X
		self.tsc = mts & 0b111

		# MTS: . X X X X . . .
		mts = (mts >> 3) & 0b1111
		if (mts & 0b1100) > 0:
			# Mask: . . . . M M M S
			self.mod_type = Modulation.pick(mts & 0b1110)
			self.tsc_set = mts & 0b1
		else:
			# GMSK: . . . . 0 0 S S
			self.mod_type = Modulation.ModGMSK
			self.tsc_set = mts & 0b11

	def gen_hdr(self):
		''' Generate message specific header part. '''

		# Allocate an empty byte-array
		buf = bytearray()

		# Put RSSI
		buf.append(-self.rssi)

		# Encode ToA (Time of Arrival)
		# Big endian, 2 bytes (int32_t)
		buf += struct.pack(">h", self.toa256)

		if self.ver >= 0x01:
			# Modulation and Training Sequence info
			mts = self.gen_mts()
			buf.append(mts)

			# C/I: Carrier-to-Interference ratio (in centiBels)
			buf += struct.pack(">h", self.ci)

		return buf

	def parse_hdr(self, hdr):
		''' Parse message specific header part. '''

		# Parse RSSI
		self.rssi = -(hdr[5])

		# Parse ToA (Time of Arrival)
		self.toa256 = struct.unpack(">h", hdr[6:8])[0]

		if self.ver >= 0x01:
			# Modulation and Training Sequence info
			self.parse_mts(hdr[8])

			# C/I: Carrier-to-Interference ratio (in centiBels)
			self.ci = struct.unpack(">h", hdr[9:11])[0]

	def gen_burst(self):
		''' Generate message specific burst. '''

		# Convert soft-bits to unsigned soft-bits
		burst_usbits = self.sbit2usbit(self.burst)

		# Encode to bytes
		return bytearray(burst_usbits)

	def _parse_burst_v0(self, burst):
		''' Parse message specific burst for header version 0. '''

		bl = len(burst)

		# We need to guess modulation by the length of burst
		self.mod_type = Modulation.pick_by_bl(bl)
		if self.mod_type is None:
			# Some old transceivers append two dummy bytes
			self.mod_type = Modulation.pick_by_bl(bl - 2)

		if self.mod_type is None:
			raise ValueError("Odd burst length %u" % bl)

		return burst[:self.mod_type.bl]

	def parse_burst(self, burst):
		''' Parse message specific burst. '''

		burst = list(burst)

		if self.ver == 0x00:
			burst = self._parse_burst_v0(burst)

		# Convert unsigned soft-bits to soft-bits
		self.burst = self.usbit2sbit(burst)

	def rand_burst(self, length = None):
		''' Generate a random message specific burst. '''

		if length is None:
			length = self.mod_type.bl

		self.burst = [random.randint(-127, 127) for _ in range(length)]

	def gen_l12trx(self, ver = None):
		''' Transform this message to L12TRX message. '''

		# Allocate a new message
		msg = DATAMSG_L12TRX(fn = self.fn, tn = self.tn,
			ver = self.ver if ver is None else ver)

		# Convert burst bits
		if self.burst is not None:
			msg.burst = self.sbit2ubit(self.burst)

		return msg
