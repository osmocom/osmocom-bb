#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# TRX Toolkit
# Unit tests for DATA capture management
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
import tempfile
import random

from gsm_shared import *
from data_dump import *

class DATADump_Test(unittest.TestCase):
	def setUp(self):
		# Create a temporary file
		self._tf = tempfile.TemporaryFile(mode = 'w+b')

		# Create an instance of DATA dump manager
		self._ddf = DATADumpFile(self._tf)

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

	# Generate a random message of a given type / version
	def _gen_rand_message(self, cls, ver = 1):
		msg = cls(ver = ver)
		msg.rand_hdr()
		msg.rand_burst()
		return msg

	# Generate a list of random messages
	def _gen_rand_messages(self, cls, count, ver = 1):
		msg_list = []

		for i in range(count):
			msg = self._gen_rand_message(cls, ver)
			msg_list.append(msg)

		return msg_list

	# Generate a mixed list of random messages
	def _gen_rand_message_mix(self, count, ver = 1):
		msg_list = []
		msg_list += self._gen_rand_messages(DATAMSG_TRX2L1, count)
		msg_list += self._gen_rand_messages(DATAMSG_L12TRX, count)
		random.shuffle(msg_list)
		return msg_list

	def _test_store_and_parse(self, cls):
		msg_ref = self._gen_rand_message(cls)
		self._ddf.append_msg(msg_ref)

		msg = self._ddf.parse_msg(0)
		self._compare_msg(msg, msg_ref)

	# Store one TRX2L1 message in a file, read it back and compare
	def test_store_and_parse_trx2l1(self):
		self._test_store_and_parse(DATAMSG_TRX2L1)

	# Store one L12TRX message in a file, read it back and compare
	def test_store_and_parse_l12trx(self):
		self._test_store_and_parse(DATAMSG_L12TRX)

	# Store multiple TRX2L1/L12TRX messages in a file, read them back and compare
	def test_store_and_parse_all(self):
		# Store a mixed list of random messages (19 + 19)
		msg_list_ref = self._gen_rand_message_mix(19)
		self._ddf.append_all(msg_list_ref)

		# Retrieve and compare stored messages
		msg_list = self._ddf.parse_all()
		for i in range(len(msg_list_ref)):
			self._compare_msg(msg_list[i], msg_list_ref[i])

	# Verify random access to stored messages
	def test_parse_msg_idx(self):
		# Store a mixed list of random messages (19 + 19)
		msg_list_ref = self._gen_rand_message_mix(19)
		self._ddf.append_all(msg_list_ref)

		# Random access
		for _ in range(100):
			idx = random.randrange(len(msg_list_ref))
			msg = self._ddf.parse_msg(idx)
			self._compare_msg(msg, msg_list_ref[idx])

	def test_parse_empty(self):
		with self.assertLogs(level = 'ERROR'):
			idx = random.randrange(100)
			msg = self._ddf.parse_msg(idx)
			self.assertEqual(msg, False)

	def test_parse_all_empty(self):
		msg_list = self._ddf.parse_all()
		self.assertEqual(msg_list, [])

	def test_parse_len_overflow(self):
		# Write a malformed message directly
		self._tf.write(DATADump.TAG_L12TRX)
		self._tf.write(b'\x00\x63') # 99
		self._tf.write(b'\xff' * 90)

		with self.assertLogs(level = 'ERROR'):
			msg = self._ddf.parse_msg(0)
			self.assertEqual(msg, None)

	def test_parse_unknown_tag(self):
		# Write a malformed message directly
		self._tf.write(b'\x33')
		self._tf.write(b'\x00\x63') # 99
		self._tf.write(b'\xff' * 90)

		with self.assertLogs(level = 'ERROR'):
			msg = self._ddf.parse_msg(0)
			self.assertEqual(msg, None)

if __name__ == '__main__':
	unittest.main()
