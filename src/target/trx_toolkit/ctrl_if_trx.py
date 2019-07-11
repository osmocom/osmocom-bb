#!/usr/bin/env python
# -*- coding: utf-8 -*-

# TRX Toolkit
# CTRL interface implementation (common commands)
#
# (C) 2016-2019 by Vadim Yanitskiy <axilirator@gmail.com>
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
from data_msg import DATAMSG

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

	== TRXD header version negotiation

	Messages on DATA interface may have different header formats,
	defined by a version number, which can be negotiated on the
	control interface. By default, the Transceiver will use the
	legacy header version (0).

	The header format negotiation can be initiated by the L1
	using 'SETFORMAT' command. If the requested version is not
	supported by the transceiver, status code of the response
	message should indicate a preferred (basically, the latest)
	version. The format of this message is the following:

	  L1 -> TRX: CMD SETFORMAT VER_REQ
	  L1 <- TRX: RSP SETFORMAT VER_RSP VER_REQ

	where:

	  - VER_REQ is the requested version (suggested by the L1),
	  - VER_RSP is either the applied version if matches VER_REQ,
	    or a preferred version if VER_REQ is not supported.

	If the transceiver indicates VER_RSP different than VER_REQ,
	the L1 is supposed to reinitiate the version negotiation
	using the suggested VER_RSP. For example:

	  L1 -> TRX: CMD SETFORMAT 2
	  L1 <- TRX: RSP SETFORMAT 1 2

	  L1 -> TRX: CMD SETFORMAT 1
	  L1 <- TRX: RSP SETFORMAT 1 1

	If no suitable VER_RSP is found, or the VER_REQ is incorrect,
	the status code in the response shall be -1.

	As soon as VER_RSP matches VER_REQ in the response, the process
	of negotiation is complete. Changing the header version is
	supposed to be done before POWERON, but can be also done after.

	"""

	def __init__(self, trx, *udp_link_args):
		CTRLInterface.__init__(self, *udp_link_args)

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

		# TRXD header version negotiation
		if self.verify_cmd(request, "SETFORMAT", 1):
			log.debug("(%s) Recv SETFORMAT cmd" % self.trx)

			# Parse the requested version
			ver_req = int(request[1])
			# ... and store current for logging
			ver_cur = self.trx.data_if._hdr_ver

			if ver_req < 0 or ver_req > DATAMSG.CHDR_VERSION_MAX:
				log.error("(%s) Incorrect TRXD header version %u"
					% (self.trx, ver_req))
				return -1

			if not self.trx.data_if.set_hdr_ver(ver_req):
				ver_rsp = self.trx.data_if.pick_hdr_ver(ver_req)
				log.warn("(%s) Requested TRXD header version %u "
					  "is not supported, suggesting %u..."
					% (self.trx, ver_req, ver_rsp))
				return ver_rsp

			log.info("(%s) TRXD header version %u -> %u"
				% (self.trx, ver_cur, ver_req))
			return ver_req

		# Wrong / unknown command
		else:
			# We don't care about other commands,
			# so let's merely ignore them ;)
			log.debug("(%s) Ignore CMD %s" % (self.trx, request[0]))
			return 0
