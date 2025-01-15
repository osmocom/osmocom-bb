#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# TRX Toolkit
# Unit test for CLCKGen
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

import threading
import time
import unittest

import clck_gen


class CLCKGen_Test(unittest.TestCase):

	# verify that timing error is not accumulated in the clock gen loop
	def test_no_timing_error_accumulated(self):
		# observe many ticks with spending some noticeable time in clock handler each tick
		# assert that t(last) - t(first) ≈ ntick·dt(tick)
		# this will break if the clock generator is not careful to prevent timing error from accumulating
		clck = clck_gen.CLCKGen([])
		ntick = 200  # ~ 1 s
		tick = 0
		tstart = tend = None
		done = threading.Event()

		def _(fn):
			nonlocal tick, tstart, tend
			if tick == 0:
				tstart = time.monotonic()
			if tick == ntick:
				tend = time.monotonic()
				done.set()
			tick += 1
			time.sleep(clck.ctr_interval / 2)
		clck.clck_handler = _

		clck.start()
		try:
			ok = done.wait(10)
			self.assertTrue(ok, "clck_gen stuck")

			self.assertIsNotNone(tstart)
			self.assertIsNotNone(tend)
			dT = tend - tstart
			dTok = ntick * clck.ctr_interval
			dTerr = dT - dTok
			self.assertTrue((ntick-1)*clck.ctr_interval < dT, "tick #%d: time underrun by %dus total" %
											(ntick, dTerr // 1e-6))
			self.assertTrue((ntick+1)*clck.ctr_interval > dT, "tick #%d: time overrun  by %dus total" %
											(ntick, dTerr // 1e-6))

		finally:
			clck.stop()


if __name__ == '__main__':
	unittest.main()
