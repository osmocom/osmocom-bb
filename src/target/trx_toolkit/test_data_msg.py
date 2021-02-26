#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# TRX Toolkit
# Unit test for TRXD message codec
#
# (C) 2019 by Vadim Yanitskiy <axilirator@gmail.com>
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

import unittest

from data_msg import DATAMSG, DATAMSG_L12TRX, DATAMSG_TRX2L1

class DATAMSG_Test(unittest.TestCase):
	# Compare message a with message b
	def _compare_msg(self, a, b):
		# Make sure we're comparing messages of the same type
		self.assertEqual(a.__class__, b.__class__)

		# Compare common header fields
		self.assertEqual(a.ver, b.ver)
		self.assertEqual(a.fn, b.fn)
		self.assertEqual(a.tn, b.tn)

		# Burst bits (if present)
		self.assertEqual(a.burst, b.burst)

		# TRX2L1 specific fields
		if isinstance(a, DATAMSG_L12TRX):
			self.assertEqual(a.pwr, b.pwr)

		# L12TRX specific fields
		if isinstance(a, DATAMSG_TRX2L1):
			# Version independent fields
			self.assertEqual(a.toa256, b.toa256)
			self.assertEqual(a.rssi, b.rssi)

			# Version specific fields
			if a.ver >= 1:
				self.assertEqual(a.nope_ind, b.nope_ind)
				self.assertEqual(a.mod_type, b.mod_type)
				self.assertEqual(a.tsc_set, b.tsc_set)
				self.assertEqual(a.tsc, b.tsc)
				self.assertEqual(a.ci, b.ci)

	# Make sure that message validation throws a ValueError
	def test_validate(self):
		# Unknown version
		with self.assertRaises(ValueError):
			msg = DATAMSG_TRX2L1(fn = 0, tn = 0, ver = 100)
			msg.validate()

		# Uninitialized field
		with self.assertRaises(ValueError):
			msg = DATAMSG_TRX2L1()
			msg.validate()
		with self.assertRaises(ValueError):
			msg = DATAMSG_TRX2L1(fn = None, tn = 0)
			msg.validate()

		# Out-of-range value(s)
		with self.assertRaises(ValueError):
			msg = DATAMSG_TRX2L1(fn = -1, tn = 0)
			msg.validate()
		with self.assertRaises(ValueError):
			msg = DATAMSG_TRX2L1(fn = 0, tn = 10)
			msg.validate()

	# Validate header and burst randomization
	def test_rand_hdr_burst(self):
		msg_l12trx = DATAMSG_L12TRX()
		msg_trx2l1 = DATAMSG_TRX2L1()

		for i in range(100):
			msg_l12trx.rand_burst()
			msg_trx2l1.rand_burst()
			msg_l12trx.rand_hdr()
			msg_trx2l1.rand_hdr()

			msg_l12trx.validate()
			msg_trx2l1.validate()

	def _test_enc_dec(self, msg, legacy = False, nope_ind = False):
		# Prepare a given message (randomize)
		msg.rand_hdr()

		# NOPE.ind contains no burst
		if not nope_ind:
			msg.rand_burst()
		else:
			msg.nope_ind = True
			msg.mod_type = None
			msg.tsc_set = None
			msg.tsc = None

		# Encode a given message to bytes
		msg_enc = msg.gen_msg(legacy)

		# Decode a new message from bytes
		msg_dec = msg.__class__()
		msg_dec.parse_msg(msg_enc)

		# Compare decoded vs the original
		self._compare_msg(msg, msg_dec)

	# Validate encoding and decoding
	def test_enc_dec(self):
		for ver in DATAMSG.KNOWN_VERSIONS:
			with self.subTest("L1 -> TRX message", ver = ver):
				msg = DATAMSG_L12TRX(ver = ver)
				self._test_enc_dec(msg)

			with self.subTest("TRX -> L1 message", ver = ver):
				msg = DATAMSG_TRX2L1(ver = ver)
				self._test_enc_dec(msg)

			if ver >= 1:
				with self.subTest("TRX -> L1 NOPE.ind", ver = ver):
					msg = DATAMSG_TRX2L1(ver = ver)
					self._test_enc_dec(msg, nope_ind = True)

		with self.subTest("TRX -> L1 message (legacy)"):
			msg = DATAMSG_TRX2L1(ver = 0)
			self._test_enc_dec(msg, legacy = True)

	# Validate bit conversations
	def test_bit_conv(self):
		usbits_ref = list(range(0, 256))
		sbits_ref = list(range(-127, 128))

		# Test both usbit2sbit() and sbit2usbit()
		sbits = DATAMSG.usbit2sbit(usbits_ref)
		usbits = DATAMSG.sbit2usbit(sbits)
		self.assertEqual(usbits[:255], usbits_ref[:255])
		self.assertEqual(usbits[255], 254)

		# Test both sbit2ubit() and ubit2sbit()
		ubits = DATAMSG.sbit2ubit(sbits_ref)
		self.assertEqual(ubits, ([1] * 127 + [0] * 128))

		sbits = DATAMSG.ubit2sbit(ubits)
		self.assertEqual(sbits, ([-127] * 127 + [127] * 128))

	def _test_transform(self, msg):
		# Prepare given messages
		msg.rand_hdr()
		msg.rand_burst()

		# Perform message transformation
		if isinstance(msg, DATAMSG_L12TRX):
			msg_trans = msg.gen_trx2l1()
		else:
			msg_trans = msg.gen_l12trx()

		self.assertEqual(msg_trans.ver, msg.ver)
		self.assertEqual(msg_trans.fn, msg.fn)
		self.assertEqual(msg_trans.tn, msg.tn)

		if isinstance(msg, DATAMSG_TRX2L1):
			burst = DATAMSG.sbit2ubit(msg.burst)
			self.assertEqual(msg_trans.burst, burst)
		else:
			burst = DATAMSG.ubit2sbit(msg.burst)
			self.assertEqual(msg_trans.burst, burst)

	# Validate message transformation
	def test_transform(self):
		for ver in DATAMSG.KNOWN_VERSIONS:
			with self.subTest("L1 -> TRX message", ver = ver):
				msg = DATAMSG_L12TRX(ver = ver)
				self._test_transform(msg)

			with self.subTest("TRX -> L1 message", ver = ver):
				msg = DATAMSG_TRX2L1(ver = ver)
				self._test_transform(msg)

if __name__ == '__main__':
	unittest.main()
