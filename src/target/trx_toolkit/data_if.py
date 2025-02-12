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
		if not ver in Msg.KNOWN_VERSIONS:
			return False

		self._hdr_ver = ver
		return True

	def pick_hdr_ver(self, ver_req):
		# Pick a version that is lower or equal to ver_req
		for ver in Msg.KNOWN_VERSIONS[::-1]:
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

	def recv_tx_msg(self):
		# Read raw data from socket
		data = self.recv_raw_data()

		# Attempt to parse a TRXD Tx message
		try:
			msg = TxMsg()
			msg.parse_msg(data)
		except:
			log.error("Failed to parse a TRXD Tx message "
				"from R:%s:%u" % (self.remote_addr, self.remote_port))
			return None

		# Make sure the header version matches
		# the configured one (self._hdr_ver)
		if not self.match_hdr_ver(msg):
			return None

		return msg

	def recv_rx_msg(self):
		# Read raw data from socket
		data = self.recv_raw_data()

		# Attempt to parse a TRXD Rx message
		try:
			msg = RxMsg()
			msg.parse_msg(bytearray(data))
		except:
			log.error("Failed to parse a TRXD Rx message "
				"from R:%s:%u" % (self.remote_addr, self.remote_port))
			return None

		# Make sure the header version matches
		# the configured one (self._hdr_ver)
		if not self.match_hdr_ver(msg):
			return None

		return msg

	def send_msg(self, msg, legacy = False):
		try:
			# Validate and encode a TRXD message
			payload = msg.gen_msg(legacy)
		except ValueError as e:
			log.error("Failed to encode a TRXD message ('%s') "
				"due to error: %s" % (msg.desc_hdr(), e))
			# TODO: we may want to send a NOPE.ind here
			return

		# Send message
		self.send(payload)
