#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# TRX Toolkit
# CTRL interface implementation (OsmoBTS specific)
#
# (C) 2016-2017 by Vadim Yanitskiy <axilirator@gmail.com>
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

from ctrl_if import CTRLInterface

class CTRLInterfaceBTS(CTRLInterface):
	# Internal state variables
	trx_started = False
	burst_fwd = None
	clck_gen = None
	rx_freq = None
	tx_freq = None
	pm = None

	def __init__(self, remote_addr, remote_port, bind_port):
		CTRLInterface.__init__(self, remote_addr, remote_port, bind_port)
		print("[i] Init CTRL interface for BTS (%s)" % self.desc_link())

	def parse_cmd(self, request):
		# Power control
		if self.verify_cmd(request, "POWERON", 0):
			print("[i] Recv POWERON CMD")

			# Ensure transceiver isn't working
			if self.trx_started:
				print("[!] Transceiver already started")
				return -1

			# Ensure RX / TX freq. are set
			if (self.rx_freq is None) or (self.tx_freq is None):
				print("[!] RX / TX freq. are not set")
				return -1

			print("[i] Starting transceiver...")
			self.trx_started = True

			# Power emulation
			if self.pm is not None:
				self.pm.add_bts_list([self.tx_freq])

			# Start clock indications
			if self.clck_gen is not None:
				self.clck_gen.start()

			return 0

		elif self.verify_cmd(request, "POWEROFF", 0):
			print("[i] Recv POWEROFF cmd")

			print("[i] Stopping transceiver...")
			self.trx_started = False

			# Power emulation
			if self.pm is not None:
				self.pm.del_bts_list([self.tx_freq])

			# Stop clock indications
			if self.clck_gen is not None:
				self.clck_gen.stop()

			return 0

		# Tuning Control
		elif self.verify_cmd(request, "RXTUNE", 1):
			print("[i] Recv RXTUNE cmd")

			# TODO: check freq range
			self.rx_freq = int(request[1]) * 1000
			return 0

		elif self.verify_cmd(request, "TXTUNE", 1):
			print("[i] Recv TXTUNE cmd")

			# TODO: check freq range
			self.tx_freq = int(request[1]) * 1000
			self.burst_fwd.bts_freq = self.tx_freq
			return 0

		# Timing of Arrival simulation for Downlink
		# Absolute form: CMD FAKE_TOA <BASE> <THRESH>
		elif self.verify_cmd(request, "FAKE_TOA", 2):
			print("[i] Recv FAKE_TOA cmd")

			# Parse and apply both base and threshold
			self.burst_fwd.toa256_dl_base = int(request[1])
			self.burst_fwd.toa256_dl_threshold = int(request[2])

			return 0

		# Timing of Arrival simulation for Downlink
		# Relative form: CMD FAKE_TOA <+-BASE_DELTA>
		elif self.verify_cmd(request, "FAKE_TOA", 1):
			print("[i] Recv FAKE_TOA cmd")

			# Parse and apply delta
			self.burst_fwd.toa256_dl_base += int(request[1])

			return 0

		# Wrong / unknown command
		else:
			# We don't care about other commands,
			# so let's merely ignore them ;)
			print("[i] Ignore CMD %s" % request[0])
			return 0
