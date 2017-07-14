#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# Virtual Um-interface (fake transceiver)
# CTRL interface implementation (OsmocomBB specific)
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

class CTRLInterfaceBB(CTRLInterface):
	# Internal state variables
	trx_started = False
	burst_fwd = None
	rx_freq = None
	tx_freq = None
	pm = None

	def __init__(self, remote_addr, remote_port, bind_port):
		print("[i] Init CTRL interface for BB")
		CTRLInterface.__init__(self, remote_addr, remote_port, bind_port)

	def shutdown(self):
		print("[i] Shutdown CTRL interface for BB")
		CTRLInterface.shutdown(self)

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
			return 0

		elif self.verify_cmd(request, "POWEROFF", 0):
			print("[i] Recv POWEROFF cmd")

			print("[i] Stopping transceiver...")
			self.trx_started = False
			return 0

		# Tuning Control
		elif self.verify_cmd(request, "RXTUNE", 1):
			print("[i] Recv RXTUNE cmd")

			# TODO: check freq range
			self.rx_freq = int(request[1]) * 1000
			self.burst_fwd.bb_freq = self.rx_freq
			return 0

		elif self.verify_cmd(request, "TXTUNE", 1):
			print("[i] Recv TXTUNE cmd")

			# TODO: check freq range
			self.tx_freq = int(request[1]) * 1000
			return 0

		# Power measurement
		elif self.verify_cmd(request, "MEASURE", 1):
			print("[i] Recv MEASURE cmd")

			if self.pm is None:
				return -1

			# TODO: check freq range
			meas_freq = int(request[1]) * 1000
			meas_dbm = str(self.pm.measure(meas_freq))

			return (0, [meas_dbm])

		elif self.verify_cmd(request, "SETSLOT", 2):
			print("[i] Recv SETSLOT cmd")

			if self.burst_fwd is None:
				return -1

			# Obtain TS index
			ts = int(request[1])
			if ts not in range(0, 8):
				print("[!] TS index should be in range: 0..7")
				return -1

			# Parse TS type
			ts_type = int(request[2])

			# TS activation / deactivation
			# We don't care about ts_type
			if ts_type == 0:
				self.burst_fwd.ts_pass = None
			else:
				self.burst_fwd.ts_pass = ts

			return 0

		# Wrong / unknown command
		else:
			# We don't care about other commands,
			# so let's merely ignore them ;)
			print("[i] Ignore CMD %s" % request[0])
			return 0
