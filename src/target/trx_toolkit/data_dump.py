#!/usr/bin/env python
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

import logging as log
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
			log.info("Opening capture file '%s'..." % capture)
			self.f = open(capture, "a+b")
		else:
			self.f = capture

	def __del__(self):
		# FIXME: this causes an Exception in Python 2 (but not in Python 3)
		# AttributeError: 'NoneType' object has no attribute 'info'
		log.info("Closing the capture file")
		self.f.close()

	# Moves the file descriptor before a specified message
	# Return value:
	#   True in case of success,
	#   or False in case of EOF or header parsing error.
	def _seek2msg(self, idx):
		# Seek to the beginning of the capture
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
				log.error("Couldn't parse a message header")
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
			log.error("Couldn't parse a message header")
			return None

		# Expand the header
		(msg, msg_len) = rc

		# Attempt to read a message
		msg_raw = self.f.read(msg_len)
		if len(msg_raw) != msg_len:
			log.error("Message length mismatch")
			return None

		# Attempt to parse a message
		try:
			msg_raw = bytearray(msg_raw)
			msg.parse_msg(msg_raw)
		except:
			log.error("Couldn't parse a message, skipping...")
			return False

		# Success
		return msg

	# Parses a particular message defined by index idx
	# Return value:
	#   a parsed message in case of success,
	#   or None in case of EOF, out of range, or header parsing error,
	#   or False in case of message parsing error.
	def parse_msg(self, idx):
		# Move descriptor to the beginning of requested message
		rc = self._seek2msg(idx)
		if not rc:
			log.error("Couldn't find requested message")
			return None

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
			# Seek to the beginning of the capture
			self.f.seek(0)
		else:
			rc = self._seek2msg(skip)
			if not rc:
				log.error("Couldn't find requested message")
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
