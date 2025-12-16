# -*- coding: utf-8 -*-

# TRX Toolkit
# UDP link implementation
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

import logging as log
import socket

class UDPLink:
	def __init__(self, remote_addr, remote_port, bind_addr = '0.0.0.0', bind_port = 0):
		self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
		self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
		self.sock.bind((bind_addr, bind_port))
		self.sock.setblocking(False)

		# Save remote info
		self.remote_addr = remote_addr
		self.remote_port = remote_port

	def __del__(self):
		self.sock.close()

	def desc_link(self):
		(bind_addr, bind_port) = self.sock.getsockname()

		return "L:%s:%u <-> R:%s:%u" \
			% (bind_addr, bind_port, self.remote_addr, self.remote_port)

	def send(self, data):
		self.sendto(data, (self.remote_addr, self.remote_port))

	def sendto(self, data, remote):
		if type(data) not in [bytearray, bytes]:
			data = data.encode()
		try:
			self.sock.sendto(data, remote)
		except BlockingIOError:
			# When the message does not fit into the send buffer of the socket, send()
			# normally blocks, unless the socket has been placed in nonblocking I/O mode.
			# In nonblocking mode it would fail with the BlockingIOError in this case.
			log.error('(%s) BlockingIOError: dropping Tx data', self.desc_link())
