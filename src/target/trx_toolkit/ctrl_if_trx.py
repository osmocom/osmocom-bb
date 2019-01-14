#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# TRX Toolkit
# CTRL interface implementation (common commands)
#
# (C) 2016-2018 by Vadim Yanitskiy <axilirator@gmail.com>
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

class CTRLInterfaceTRX(CTRLInterface):
	""" CTRL interface handler for common transceiver management commands.

	The following set of commands is mandatory for every transceiver:

	  - POWERON / POWEROFF - state management (running / idle),
	  - RXTUNE / TXTUNE - RX / TX frequency management,
	  - SETSLOT - timeslot management.

	Additionally, there is an optional MEASURE command, which is used
	by OsmocomBB to perform power measurement on a given frequency.

	A given transceiver may also define its own command handler,
	that is prioritized, i.e. it can overwrite any commands mentioned
	above. If None is returned, a command is considered as unhandled.

	"""

	def __init__(self, trx, *udp_link_args):
		CTRLInterface.__init__(self, *udp_link_args)
		log.info("Init CTRL interface (%s)" % self.desc_link())

		# Link with Transceiver instance we belong to
		self.trx = trx

	def parse_cmd(self, request):
		# Custom command handlers (prioritized)
		res = self.trx.ctrl_cmd_handler(request)
		if res is not None:
			return res

		# Power control
		if self.verify_cmd(request, "POWERON", 0):
			log.debug("(%s) Recv POWERON CMD" % self.trx)

			# Ensure transceiver isn't working
			if self.trx.running:
				log.error("(%s) Transceiver already started" % self.trx)
				return -1

			# Ensure RX / TX freq. are set
			if (self.trx.rx_freq is None) or (self.trx.tx_freq is None):
				log.error("(%s) RX / TX freq. are not set" % self.trx)
				return -1

			log.info("(%s) Starting transceiver..." % self.trx)
			self.trx.running = True

			# Notify transceiver about that
			self.trx.power_event_handler("POWERON")

			return 0

		elif self.verify_cmd(request, "POWEROFF", 0):
			log.debug("(%s) Recv POWEROFF cmd" % self.trx)

			log.info("(%s) Stopping transceiver..." % self.trx)
			self.trx.running = False

			# Notify transceiver about that
			self.trx.power_event_handler("POWEROFF")

			return 0

		# Tuning Control
		elif self.verify_cmd(request, "RXTUNE", 1):
			log.debug("(%s) Recv RXTUNE cmd" % self.trx)

			# TODO: check freq range
			self.trx.rx_freq = int(request[1]) * 1000
			return 0

		elif self.verify_cmd(request, "TXTUNE", 1):
			log.debug("(%s) Recv TXTUNE cmd" % self.trx)

			# TODO: check freq range
			self.trx.tx_freq = int(request[1]) * 1000
			return 0

		elif self.verify_cmd(request, "SETSLOT", 2):
			log.debug("(%s) Recv SETSLOT cmd" % self.trx)

			# Obtain TS index
			ts = int(request[1])
			if ts not in range(0, 8):
				log.error("(%s) TS index should be in "
					"range: 0..7" % self.trx)
				return -1

			# Parse TS type
			ts_type = int(request[2])

			# TS activation / deactivation
			# We don't care about ts_type
			if ts_type == 0:
				# Deactivate TS (remove from the list of active timeslots)
				if ts in self.trx.ts_list:
					self.trx.ts_list.remove(ts)
			else:
				# Activate TS (add to the list of active timeslots)
				if ts not in self.trx.ts_list:
					self.trx.ts_list.append(ts)

			return 0

		# Power measurement
		if self.verify_cmd(request, "MEASURE", 1):
			log.debug("(%s) Recv MEASURE cmd" % self.trx)

			# Power Measurement interface is optional
			# for Transceiver, thus may be uninitialized
			if self.trx.pwr_meas is None:
				log.error("(%s) Power Measurement interface is not "
					"initialized => rejecting command" % self.trx)
				return -1

			# TODO: check freq range
			meas_freq = int(request[1]) * 1000
			meas_dbm = self.trx.pwr_meas.measure(meas_freq)

			return (0, [str(meas_dbm)])

		# Wrong / unknown command
		else:
			# We don't care about other commands,
			# so let's merely ignore them ;)
			log.debug("(%s) Ignore CMD %s" % (self.trx, request[0]))
			return 0
