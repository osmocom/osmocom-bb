#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# Virtual Um-interface (fake transceiver)
# Simple TDMA frame clock generator
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

import signal
import time
import sys

from threading import Timer
from udp_link import UDPLink

COPYRIGHT = \
	"Copyright (C) 2017 by Vadim Yanitskiy <axilirator@gmail.com>\n" \
	"License GPLv2+: GNU GPL version 2 or later " \
	"<http://gnu.org/licenses/gpl.html>\n" \
	"This is free software: you are free to change and redistribute it.\n" \
	"There is NO WARRANTY, to the extent permitted by law.\n"

class CLCKGen:
	# GSM TDMA definitions
	SEC_DELAY_US = 1000 * 1000
	GSM_SUPERFRAME = 2715648
	GSM_FRAME_US = 4615.0

	# Average loop back delay
	LO_DELAY_US = 90.0

	def __init__(self, clck_links, clck_start = 0, ind_period = 102):
		self.clck_links = clck_links
		self.ind_period = ind_period
		self.clck_src = clck_start

		# Calculate counter time
		self.ctr_interval  = self.GSM_FRAME_US - self.LO_DELAY_US
		self.ctr_interval /= self.SEC_DELAY_US
		self.ctr_interval *= self.ind_period

		# Create a timer manager
		self.timer = Timer(self.ctr_interval, self.send_clck_ind)

	def start(self):
		# Schedule the first indication
		self.timer.start()

	def stop(self):
		self.timer.cancel()

	def send_clck_ind(self):
		# Keep clock cycle
		if self.clck_src % self.GSM_SUPERFRAME >= 0:
			self.clck_src %= self.GSM_SUPERFRAME

		# We don't need to send so often
		if self.clck_src % self.ind_period == 0:
			# Create UDP payload
			payload = "IND CLOCK %u\0" % self.clck_src

			# Send indication to all UDP links
			for link in self.clck_links:
				link.send(payload)

			# Debug print
			print("[T] %s" % payload)

		# Increase frame count
		self.clck_src += self.ind_period

		# Schedule a new indication
		self.timer = Timer(self.ctr_interval, self.send_clck_ind)
		self.timer.start()

# Just a wrapper for independent usage
class Application:
	def __init__(self):
		# Set up signal handlers
		signal.signal(signal.SIGINT, self.sig_handler)

		# Print copyright
		print(COPYRIGHT)

	def run(self):
		self.link = UDPLink("127.0.0.1", 5800, 5700)
		self.clck = CLCKGen([self.link], ind_period = 51)
		self.clck.start()

	def sig_handler(self, signum, frame):
		print("Signal %d received" % signum)
		if signum is signal.SIGINT:
			self.clck.stop()
			self.link.shutdown()

if __name__ == '__main__':
	app = Application()
	app.run()
