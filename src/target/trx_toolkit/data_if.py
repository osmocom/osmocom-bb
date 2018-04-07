#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# TRX Toolkit
# DATA interface implementation
#
# (C) 2017-2018 by Vadim Yanitskiy <axilirator@gmail.com>
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

from udp_link import UDPLink
from data_msg import *

class DATAInterface(UDPLink):

	def send_msg(self, msg):
		# Validate a message
		if not msg.validate():
			raise ValueError("Message incomplete or incorrect")

		# Generate TRX message
		payload = msg.gen_msg()

		# Send message
		self.send(payload)
