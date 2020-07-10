#!/usr/bin/env python
# -*- coding: utf-8 -*-

# TRX Toolkit
# Simple TDMA frame clock generator
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

APP_CR_HOLDERS = [("2017-2019", "Vadim Yanitskiy <axilirator@gmail.com>")]

import logging as log
import threading
import signal
import time

from app_common import ApplicationBase
from udp_link import UDPLink
from gsm_shared import *

class CLCKGen:
	# GSM TDMA definitions
	SEC_DELAY_US = 1000 * 1000
	GSM_FRAME_US = 4615.0

	# Average loop back delay
	LO_DELAY_US = 90.0

	def __init__(self, clck_links, clck_start = 0, ind_period = 102):
		# This event is needed to control the thread
		self._breaker = threading.Event()
		self._thread = None

		self.clck_links = clck_links
		self.ind_period = ind_period
		self.clck_start = clck_start

		# Calculate counter time
		self.ctr_interval  = self.GSM_FRAME_US - self.LO_DELAY_US
		self.ctr_interval /= self.SEC_DELAY_US

	@property
	def running(self):
		if self._thread is None:
			return False
		return self._thread.isAlive()

	def start(self):
		# Make sure we won't start two threads
		assert(self._thread is None)

		# (Re)set the clock counter
		self.clck_src = self.clck_start

		# Initialize and start a new thread
		self._thread = threading.Thread(target = self._worker)
		self._thread.setDaemon(True)
		self._thread.start()

	def stop(self):
		# No thread, no problem ;)
		if self._thread is None:
			return

		# Stop the thread first
		self._breaker.set()
		self._thread.join()

		# Free memory, reset breaker
		del self._thread
		self._thread = None
		self._breaker.clear()

	def _worker(self):
		while not self._breaker.wait(self.ctr_interval):
			self.send_clck_ind()

	def send_clck_ind(self):
		# Keep clock cycle
		if self.clck_src % GSM_HYPERFRAME >= 0:
			self.clck_src %= GSM_HYPERFRAME

		# We don't need to send so often
		if self.clck_src % self.ind_period == 0:
			# Create UDP payload
			payload = "IND CLOCK %u\0" % self.clck_src

			# Send indication to all UDP links
			for link in self.clck_links:
				link.send(payload)

			# Debug print
			log.debug(payload.rstrip("\0"))

		# Increase frame count
		self.clck_src += 1

# Just a wrapper for independent usage
class Application(ApplicationBase):
	def __init__(self):
		# Print copyright
		self.app_print_copyright(APP_CR_HOLDERS)

		# Set up signal handlers
		signal.signal(signal.SIGINT, self.sig_handler)

		# Configure logging
		log.basicConfig(level = log.DEBUG,
			format = "[%(levelname)s] TID#%(thread)s %(filename)s:%(lineno)d %(message)s")

	def run(self):
		self.link = UDPLink("127.0.0.1", 5800, "0.0.0.0", 5700)
		self.clck = CLCKGen([self.link], ind_period = 51)
		self.clck.start()

		# Block unless we receive a signal
		self.clck._thread.join()

	def sig_handler(self, signum, frame):
		log.info("Signal %d received" % signum)
		if signum == signal.SIGINT:
			self.clck.stop()

if __name__ == '__main__':
	app = Application()
	app.run()
