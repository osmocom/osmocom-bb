#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# TRX Toolkit
# Helpers for DATA capture management
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

import struct

from data_msg import *

class DATADump:
	# Constants
	TAG_L12TRX = b'\x01'
	TAG_TRX2L1 = b'\x02'
	HDR_LENGTH = 3

	# Generates raw bytes from a DATA message
	# Return value: raw message bytes
	def dump_msg(self, msg):
		# Determine a message type
		if isinstance(msg, DATAMSG_L12TRX):
			tag = self.TAG_L12TRX
		elif isinstance(msg, DATAMSG_TRX2L1):
			tag = self.TAG_TRX2L1
		else:
			raise ValueError("Unknown message type")

		# Generate a message payload
		msg_raw = msg.gen_msg()

		# Calculate and pack the message length
		msg_len = len(msg_raw)

		# Pack to unsigned short (2 bytes, BE)
		msg_len = struct.pack(">H", msg_len)

		# Concatenate a message with header
		return bytearray(tag + msg_len) + msg_raw

	def parse_hdr(self, hdr):
		# Extract the header info
		msg_len = struct.unpack(">H", hdr[1:3])[0]
		tag = hdr[:1]

		# Check if tag is known
		if tag == self.TAG_L12TRX:
			# L1 -> TRX
			msg = DATAMSG_L12TRX()
		elif tag == self.TAG_TRX2L1:
			# TRX -> L1
			msg = DATAMSG_TRX2L1()
		else:
			# Unknown tag
			return False

		return (msg, msg_len)

class DATADumpFile(DATADump):
	def __init__(self, capture):
		# Check if capture file is already opened
		if isinstance(capture, str):
			print("[i] Opening capture file '%s'..." % capture)
			self.f = open(capture, "a+b")
		else:
			self.f = capture

	def __del__(self):
		print("[i] Closing the capture file")
		self.f.close()

	# Moves the file descriptor before a specified message
	# Return value:
	#   True in case of success,
	#   or False in case of EOF or header parsing error.
	def _seek2msg(self, idx):
		# Seek to the begining of the capture
		self.f.seek(0)

		# Read the capture in loop...
		for i in range(idx):
			# Attempt to read a message header
			hdr_raw = self.f.read(self.HDR_LENGTH)
			if len(hdr_raw) != self.HDR_LENGTH:
				return False

			# Attempt to parse it
			rc = self.parse_hdr(hdr_raw)
			if rc is False:
				print("[!] Couldn't parse a message header")
				return False

			# Expand the header
			(_, msg_len) = rc

			# Skip a message
			self.f.seek(msg_len, 1)

		return True

	# Parses a single message at the current descriptor position
	# Return value:
	#   a parsed message in case of success,
	#   or None in case of EOF or header parsing error,
	#   or False in case of message parsing error.
	def _parse_msg(self):
		# Attempt to read a message header
		hdr_raw = self.f.read(self.HDR_LENGTH)
		if len(hdr_raw) != self.HDR_LENGTH:
			return None

		# Attempt to parse it
		rc = self.parse_hdr(hdr_raw)
		if rc is False:
			print("[!] Couldn't parse a message header")
			return None

		# Expand the header
		(msg, msg_len) = rc

		# Attempt to read a message
		msg_raw = self.f.read(msg_len)
		if len(msg_raw) != msg_len:
			print("[!] Message length mismatch")
			return None

		# Attempt to parse a message
		try:
			msg_raw = bytearray(msg_raw)
			msg.parse_msg(msg_raw)
		except:
			print("[!] Couldn't parse a message, skipping...")
			return False

		# Success
		return msg

	# Parses a particular message defined by index idx
	# Return value:
	#   a parsed message in case of success,
	#   or None in case of EOF or header parsing error,
	#   or False in case of message parsing error or out of range.
	def parse_msg(self, idx):
		# Move descriptor to the begining of requested message
		rc = self._seek2msg(idx)
		if not rc:
			print("[!] Couldn't find requested message")
			return False

		# Attempt to parse a message
		return self._parse_msg()

	# Parses all messages from a given file
	# Return value:
	#   list of parsed messages,
	#   or False in case of range error.
	def parse_all(self, skip = None, count = None):
		result = []

		# Should we skip some messages?
		if skip is None:
			# Seek to the begining of the capture
			self.f.seek(0)
		else:
			rc = self._seek2msg(skip)
			if not rc:
				print("[!] Couldn't find requested message")
				return False

		# Read the capture in loop...
		while True:
			# Attempt to parse a message
			msg = self._parse_msg()

			# EOF or broken header
			if msg is None:
				break

			# Skip unparsed messages
			if msg is False:
				continue

			# Success, append a message
			result.append(msg)

			# Count limitation
			if count is not None:
				if len(result) == count:
					break

		return result

	# Writes a new message at the end of the capture
	def append_msg(self, msg):
		# Generate raw bytes and write
		msg_raw = self.dump_msg(msg)
		self.f.write(msg_raw)

	# Writes a list of messages at the end of the capture
	def append_all(self, msgs):
		for msg in msgs:
			self.append_msg(msg)

# Regression tests
if __name__ == '__main__':
	from tempfile import TemporaryFile
	from gsm_shared import *
	import random

	# Create a temporary file
	tf = TemporaryFile()

	# Create an instance of DATA dump manager
	ddf = DATADumpFile(tf)

	# Generate two random bursts
	burst_l12trx = []
	burst_trx2l1 = []

	for i in range(0, GSM_BURST_LEN):
		ubit = random.randint(0, 1)
		burst_l12trx.append(ubit)

		sbit = random.randint(-127, 127)
		burst_trx2l1.append(sbit)

	# Generate a basic list of random messages
	print("[i] Generating the reference messages")
	messages_ref = []

	for i in range(100):
		# Create a message
		if i % 2:
			msg = DATAMSG_L12TRX()
			msg.burst = burst_l12trx
		else:
			msg = DATAMSG_TRX2L1()
			msg.burst = burst_trx2l1

		# Randomize the header
		msg.rand_hdr()

		# HACK: as ToA parsing is not implemented yet,
		# we have to use a fixed 0.00 value for now...
		if isinstance(msg, DATAMSG_TRX2L1):
			msg.toa = 0.00

		# Append
		messages_ref.append(msg)

	print("[i] Adding the following messages to the capture:")
	for msg in messages_ref[:3]:
		print("    %s: burst_len=%d"
			% (msg.desc_hdr(), len(msg.burst)))

	# Check single message appending
	ddf.append_msg(messages_ref[0])
	ddf.append_msg(messages_ref[1])
	ddf.append_msg(messages_ref[2])

	# Read the written messages back
	messages_check = ddf.parse_all()

	print("[i] Read the following messages back:")
	for msg in messages_check:
		print("    %s: burst_len=%d"
			% (msg.desc_hdr(), len(msg.burst)))

	# Expecting three messages
	assert(len(messages_check) == 3)

	# Check the messages
	for i in range(3):
		# Compare common header parts and bursts
		assert(messages_check[i].burst == messages_ref[i].burst)
		assert(messages_check[i].fn == messages_ref[i].fn)
		assert(messages_check[i].tn == messages_ref[i].tn)

		# HACK: as ToA parsing is not implemented yet,
		# we have to use a fixed 0.00 value for now...
		messages_check[i].toa = 0.00

		# Validate a message
		assert(messages_check[i].validate())

	print("[?] Check append_msg(): OK")


	# Append the pending reference messages
	ddf.append_all(messages_ref[3:])

	# Read the written messages back
	messages_check = ddf.parse_all()

	# Check the final amount
	assert(len(messages_check) == len(messages_ref))

	# Check the messages
	for i in range(len(messages_check)):
		# Compare common header parts and bursts
		assert(messages_check[i].burst == messages_ref[i].burst)
		assert(messages_check[i].fn == messages_ref[i].fn)
		assert(messages_check[i].tn == messages_ref[i].tn)

		# HACK: as ToA parsing is not implemented yet,
		# we have to use a fixed 0.00 value for now...
		messages_check[i].toa = 0.00

		# Validate a message
		assert(messages_check[i].validate())

	print("[?] Check append_all(): OK")


	# Check parse_msg()
	msg0 = ddf.parse_msg(0)
	msg10 = ddf.parse_msg(10)

	# Make sure parsing was successful
	assert(msg0 and msg10)

	# Compare common header parts and bursts
	assert(msg0.burst == messages_ref[0].burst)
	assert(msg0.fn == messages_ref[0].fn)
	assert(msg0.tn == messages_ref[0].tn)

	assert(msg10.burst == messages_ref[10].burst)
	assert(msg10.fn == messages_ref[10].fn)
	assert(msg10.tn == messages_ref[10].tn)

	# HACK: as ToA parsing is not implemented yet,
	# we have to use a fixed 0.00 value for now...
	msg0.toa = 0.00
	msg10.toa = 0.00

	# Validate both messages
	assert(msg0.validate())
	assert(msg10.validate())

	print("[?] Check parse_msg(): OK")


	# Check parse_all() with range
	messages_check = ddf.parse_all(skip = 10, count = 20)

	# Make sure parsing was successful
	assert(messages_check)

	# Check the amount
	assert(len(messages_check) == 20)

	for i in range(20):
		# Compare common header parts and bursts
		assert(messages_check[i].burst == messages_ref[i + 10].burst)
		assert(messages_check[i].fn == messages_ref[i + 10].fn)
		assert(messages_check[i].tn == messages_ref[i + 10].tn)

		# HACK: as ToA parsing is not implemented yet,
		# we have to use a fixed 0.00 value for now...
		messages_check[i].toa = 0.00

		# Validate a message
		assert(messages_check[i].validate())

	print("[?] Check parse_all(): OK")
