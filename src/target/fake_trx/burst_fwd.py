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

class BurstForwarder:
	# Timeslot filter
	ts_pass = 0

	def __init__(self, bts_link, bb_link):
		self.bts_link = bts_link
		self.bb_link = bb_link

	def set_slot(self, ts):
		if ts > 0 and ts < 8:
			self.ts_pass = ts
		else:
			raise ValueError("Incorrect index for timeslot filter")

	def process_payload(self, data):
		payload = bytearray(data)
		length = len(payload)

		# HACK: set fake RSSI value (-53)
		payload[5] = 0x35

		# HACK: add fake TOA value (6th and 7th bytes)
		payload[6:2] = [0x00, 0x00]

		# Convert ubits to {255..0}
		for i in range(8, length):
			payload[i] = 255 if payload[i] else 0

		return payload

	# Downlink handler: BTS -> BB
	def bts2bb(self):
		# Read data from socket
		data, addr = self.bts_link.sock.recvfrom(512)
		payload = self.process_payload(data)

		# Timeslot filter
		if payload[0] != self.ts_pass:
			return None

		# Send burst to BB
		self.bb_link.send(payload)

	# Uplink handler: BB -> BTS
	def bb2bts(self):
		# Read data from socket
		data, addr = self.bb_link.sock.recvfrom(512)
		payload = self.process_payload(data)

		# Send burst to BTS
		self.bts_link.send(payload)
