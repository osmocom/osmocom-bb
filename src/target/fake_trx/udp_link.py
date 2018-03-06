#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# Virtual Um-interface (fake transceiver)
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
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

import socket

class UDPLink:
	def __init__(self, remote_addr, remote_port, bind_port = 0):
		self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
		self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
		self.sock.bind(('0.0.0.0', bind_port))
		self.sock.setblocking(0)

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
		if type(data) not in [bytearray, bytes]:
			data = data.encode()

		self.sock.sendto(data, (self.remote_addr, self.remote_port))

	def sendto(self, data, remote):
		if type(data) not in [bytearray, bytes]:
			data = data.encode()

		self.sock.sendto(data, remote)
