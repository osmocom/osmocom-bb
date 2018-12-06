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

import logging as log

from ctrl_if import CTRLInterface

class CTRLInterfaceBTS(CTRLInterface):
	# Internal state variables
	trx_started = False
	burst_fwd = None
	clck_gen = None
	rx_freq = None
	tx_freq = None
	pm = None

	def __init__(self, remote_addr, remote_port, bind_addr, bind_port):
		CTRLInterface.__init__(self, remote_addr, remote_port, bind_addr, bind_port)
		log.info("Init CTRL interface for BTS (%s)" % self.desc_link())

	def parse_cmd(self, request):
		# Power control
		if self.verify_cmd(request, "POWERON", 0):
			log.debug("Recv POWERON CMD")

			# Ensure transceiver isn't working
			if self.trx_started:
				log.error("Transceiver already started")
				return -1

			# Ensure RX / TX freq. are set
			if (self.rx_freq is None) or (self.tx_freq is None):
				log.error("RX / TX freq. are not set")
				return -1

			log.info("Starting transceiver...")
			self.trx_started = True

			# Power emulation
			if self.pm is not None:
				self.pm.add_bts_list([self.tx_freq])

			# Start clock indications
			if self.clck_gen is not None:
				self.clck_gen.start()

			return 0

		elif self.verify_cmd(request, "POWEROFF", 0):
			log.debug("Recv POWEROFF cmd")

			log.info("Stopping transceiver...")
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
			log.debug("Recv RXTUNE cmd")

			# TODO: check freq range
			self.rx_freq = int(request[1]) * 1000
			return 0

		elif self.verify_cmd(request, "TXTUNE", 1):
			log.debug("Recv TXTUNE cmd")

			# TODO: check freq range
			self.tx_freq = int(request[1]) * 1000
			self.burst_fwd.bts_freq = self.tx_freq
			return 0

		# Timing of Arrival simulation for Downlink
		# Absolute form: CMD FAKE_TOA <BASE> <THRESH>
		elif self.verify_cmd(request, "FAKE_TOA", 2):
			log.debug("Recv FAKE_TOA cmd")

			# Parse and apply both base and threshold
			self.burst_fwd.toa256_dl_base = int(request[1])
			self.burst_fwd.toa256_dl_threshold = int(request[2])

			return 0

		# Timing of Arrival simulation for Downlink
		# Relative form: CMD FAKE_TOA <+-BASE_DELTA>
		elif self.verify_cmd(request, "FAKE_TOA", 1):
			log.debug("Recv FAKE_TOA cmd")

			# Parse and apply delta
			self.burst_fwd.toa256_dl_base += int(request[1])

			return 0

		# RSSI simulation for Downlink
		# Absolute form: CMD FAKE_RSSI <BASE> <THRESH>
		elif self.verify_cmd(request, "FAKE_RSSI", 2):
			log.debug("Recv FAKE_RSSI cmd")

			# Parse and apply both base and threshold
			self.burst_fwd.rssi_dl_base = int(request[1])
			self.burst_fwd.rssi_dl_threshold = int(request[2])

			return 0

		# RSSI simulation for Downlink
		# Relative form: CMD FAKE_RSSI <+-BASE_DELTA>
		elif self.verify_cmd(request, "FAKE_RSSI", 1):
			log.debug("Recv FAKE_RSSI cmd")

			# Parse and apply delta
			self.burst_fwd.rssi_dl_base += int(request[1])

			return 0

		# Path loss simulation for DL: burst dropping
		# Syntax: CMD FAKE_DROP <AMOUNT>
		# Dropping pattern: fn % 1 == 0
		elif self.verify_cmd(request, "FAKE_DROP", 1):
			log.debug("Recv FAKE_DROP cmd")

			# Parse / validate amount of bursts
			num = int(request[1])
			if num < 0:
				log.error("FAKE_DROP amount shall not be negative")
				return -1

			self.burst_fwd.burst_dl_drop_amount = num
			self.burst_fwd.burst_dl_drop_period = 1

			return 0

		# Path loss simulation for DL: burst dropping
		# Syntax: CMD FAKE_DROP <AMOUNT> <FN_PERIOD>
		# Dropping pattern: fn % period == 0
		elif self.verify_cmd(request, "FAKE_DROP", 2):
			log.debug("Recv FAKE_DROP cmd")

			# Parse / validate amount of bursts
			num = int(request[1])
			if num < 0:
				log.error("FAKE_DROP amount shall not be negative")
				return -1

			# Parse / validate period
			period = int(request[2])
			if period <= 0:
				log.error("FAKE_DROP period shall be greater than zero")
				return -1

			self.burst_fwd.burst_dl_drop_amount = num
			self.burst_fwd.burst_dl_drop_period = period

			return 0

		# Wrong / unknown command
		else:
			# We don't care about other commands,
			# so let's merely ignore them ;)
			log.debug("Ignore CMD %s" % request[0])
			return 0
