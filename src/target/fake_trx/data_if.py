#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# Simple tool to send custom messages via TRX DATA interface,
# which may be also useful for fuzzing and testing
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

from udp_link import UDPLink

class DATAInterface(UDPLink):
	# GSM PHY definitions
	GSM_HYPERFRAME = 2048 * 26 * 51

	def send_l1_msg(self, burst,
		tn = None, fn = None, rssi = None):
		# Generate random timeslot index if not preset
		if tn is None:
			tn = random.randint(0, 7)

		# Generate random frame number if not preset
		if fn is None:
			fn = random.randint(0, self.GSM_HYPERFRAME)

		# Generate random RSSI if not preset
		if rssi is None:
			rssi = -random.randint(-75, -50)

		# Prepare a buffer for header and burst
		buf = []

		# Put timeslot index
		buf.append(tn)

		# Put frame number
		buf.append((fn >> 24) & 0xff)
		buf.append((fn >> 16) & 0xff)
		buf.append((fn >>  8) & 0xff)
		buf.append((fn >>  0) & 0xff)

		# Put RSSI
		buf.append(rssi)

		# HACK: put fake TOA value
		buf += [0x00] * 2

		# Put burst
		buf += burst

		# Put two unused bytes
		buf += [0x00] * 2

		# Send message
		self.send(bytearray(buf))

	def send_trx_msg(self, burst,
		tn = None, fn = None, pwr = None):
		# Generate random timeslot index if not preset
		if tn is None:
			tn = random.randint(0, 7)

		# Generate random frame number if not preset
		if fn is None:
			fn = random.randint(0, self.GSM_HYPERFRAME)

		# Generate random power level if not preset
		if pwr is None:
			pwr = random.randint(0, 34)

		# Prepare a buffer for header and burst
		buf = []

		# Put timeslot index
		buf.append(tn)

		# Put frame number
		buf.append((fn >> 24) & 0xff)
		buf.append((fn >> 16) & 0xff)
		buf.append((fn >>  8) & 0xff)
		buf.append((fn >>  0) & 0xff)

		# Put transmit power level
		buf.append(pwr)

		# Put burst
		buf += burst

		# Send message
		self.send(bytearray(buf))
