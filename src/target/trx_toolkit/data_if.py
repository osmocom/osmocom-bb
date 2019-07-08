#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# TRX Toolkit
# DATA interface implementation
#
# (C) 2017-2019 by Vadim Yanitskiy <axilirator@gmail.com>
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

from udp_link import UDPLink
from data_msg import *

class DATAInterface(UDPLink):
	def __init__(self, *udp_link_args):
		# Default header version (legacy)
		self._hdr_ver = 0x00

		UDPLink.__init__(self, *udp_link_args)
		log.debug("Init TRXD interface (%s)" % self.desc_link())

	def set_hdr_ver(self, ver):
		if not ver in DATAMSG.known_versions:
			return False

		self._hdr_ver = ver
		return True

	def pick_hdr_ver(self, ver_req):
		# Pick a version that is lower or equal to ver_req
		for ver in DATAMSG.known_versions[::-1]:
			if ver <= ver_req:
				return ver

		# No suitable version found
		return -1

	def match_hdr_ver(self, msg):
		if msg.ver == self._hdr_ver:
			return True

		log.error("(%s) Rx DATA message (%s) with unexpected header "
			  "version %u (!= expected %u), ignoring..."
			% (self.desc_link(), msg.desc_hdr(),
			   msg.ver, self._hdr_ver))
		return False

	def recv_raw_data(self):
		data, _ = self.sock.recvfrom(512)
		return data

	def recv_l12trx_msg(self):
		# Read raw data from socket
		data = self.recv_raw_data()

		# Attempt to parse as a L12TRX message
		try:
			msg = DATAMSG_L12TRX()
			msg.parse_msg(bytearray(data))
		except:
			log.error("Failed to parse a L12TRX message "
				"from R:%s:%u" % (self.remote_addr, self.remote_port))
			return None

		# Make sure the header version matches
		# the configured one (self._hdr_ver)
		if not self.match_hdr_ver(msg):
			return None

		return msg

	def recv_trx2l1_msg(self):
		# Read raw data from socket
		data = self.recv_raw_data()

		# Attempt to parse as a L12TRX message
		try:
			msg = DATAMSG_TRX2L1()
			msg.parse_msg(bytearray(data))
		except:
			log.error("Failed to parse a TRX2L1 message "
				"from R:%s:%u" % (self.remote_addr, self.remote_port))
			return None

		# Make sure the header version matches
		# the configured one (self._hdr_ver)
		if not self.match_hdr_ver(msg):
			return None

		return msg

	def send_msg(self, msg, legacy = False):
		# Validate a message
		if not msg.validate():
			raise ValueError("Message incomplete or incorrect")

		# Generate TRX message
		payload = msg.gen_msg(legacy)

		# Send message
		self.send(payload)
