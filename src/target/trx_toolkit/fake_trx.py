#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# TRX Toolkit
# Virtual Um-interface (fake transceiver)
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

APP_CR_HOLDERS = [("2017-2018", "Vadim Yanitskiy <axilirator@gmail.com>")]

import logging as log
import signal
import argparse
import random
import select
import sys
import re

from app_common import ApplicationBase
from burst_fwd import BurstForwarder
from transceiver import Transceiver
from clck_gen import CLCKGen
from trx_list import TRXList
from fake_pm import FakePM

class FakeTRX(Transceiver):
	""" Fake transceiver with RF path (burst loss, RSSI, TA, ToA) simulation.

	== ToA / RSSI measurement simulation

	Since this is a virtual environment, we can simulate different
	parameters of the physical RF interface:

	  - ToA (Timing of Arrival) - measured difference between expected
	    and actual time of burst arrival in units of 1/256 of GSM symbol
	    periods. A pair of both base and threshold values defines a range
	    of ToA value randomization:

	      from (toa256_base - toa256_rand_threshold)
	        to (toa256_base + toa256_rand_threshold).

	  - RSSI (Received Signal Strength Indication) - measured "power" of
	    the signal (per burst) in dBm. A pair of both base and threshold
	    values defines a range of RSSI value randomization:

	      from (rssi_base - rssi_rand_threshold)
	        to (rssi_base + rssi_rand_threshold).

	Please note that randomization of both RSSI and ToA is optional,
	and can be enabled from the control interface.

	== Timing Advance handling

	The BTS is using ToA measurements for UL bursts in order to calculate
	Timing Advance value, that is then indicated to a MS, which in its turn
	shall apply this value to the transmitted signal in order to compensate
	the delay. Basically, every burst is transmitted in advance defined by
	the indicated Timing Advance value. The valid range is 0..63, where
	each unit means one GSM symbol advance. The actual Timing Advance value
	is set using SETTA control command from MS. By default, it's set to 0.

	== Path loss simulation

	=== Burst dropping

	In some cases, e.g. due to a weak signal or high interference, a burst
	can be lost, i.e. not detected by the receiver. This can also be
	simulated using FAKE_DROP command on the control interface:

	  - burst_drop_amount - the amount of DL/UL bursts
	    to be dropped (i.e. not forwarded towards the MS/BTS),

	  - burst_drop_period - drop a DL/UL burst if its (fn % period) == 0.

	== Configuration

	All simulation parameters mentioned above can be changed at runtime
	using the commands with prefix 'FAKE_' on the control interface.
	All of them are handled by our custom CTRL command handler.

	"""

	TOA256_BASE_DEFAULT = 0
	RSSI_BASE_DEFAULT = -60

	def __init__(self, *trx_args, **trx_kwargs):
		Transceiver.__init__(self, *trx_args, **trx_kwargs)

		# Actual ToA / RSSI / TA values
		self.toa256_base = self.TOA256_BASE_DEFAULT
		self.rssi_base = self.RSSI_BASE_DEFAULT
		self.ta = 0

		# ToA / RSSI randomization threshold
		self.toa256_rand_threshold = 0
		self.rssi_rand_threshold = 0

		# Path loss simulation (burst dropping)
		self.burst_drop_amount = 0
		self.burst_drop_period = 1

	@property
	def toa256(self):
		# Check if randomization is required
		if self.toa256_rand_threshold is 0:
			return self.toa256_base

		# Generate a random ToA value in required range
		toa256_min = self.toa256_base - self.toa256_rand_threshold
		toa256_max = self.toa256_base + self.toa256_rand_threshold
		return random.randint(toa256_min, toa256_max)

	@property
	def rssi(self):
		# Check if randomization is required
		if self.rssi_rand_threshold is 0:
			return self.rssi_base

		# Generate a random RSSI value in required range
		rssi_min = self.rssi_base - self.rssi_rand_threshold
		rssi_max = self.rssi_base + self.rssi_rand_threshold
		return random.randint(rssi_min, rssi_max)

	# Path loss simulation: burst dropping
	# Returns: True - drop, False - keep
	def sim_burst_drop(self, msg):
		# Check if dropping is required
		if self.burst_drop_amount is 0:
			return False

		if msg.fn % self.burst_drop_period == 0:
			log.info("(%s) Simulation: dropping burst (fn=%u %% %u == 0)"
				% (self, msg.fn, self.burst_drop_period))
			self.burst_drop_amount -= 1
			return True

		return False

	# Takes (partially initialized) TRX2L1 message,
	# simulates RF path parameters (such as RSSI),
	# and sends towards the L1
	def send_data_msg(self, src_trx, msg):
		# Complete message header
		msg.toa256 = self.toa256
		msg.rssi = self.rssi

		# Apply optional Timing Advance
		if src_trx.ta is not 0:
			msg.toa256 -= src_trx.ta * 256

		# Path loss simulation
		if self.sim_burst_drop(msg):
			return

		# TODO: make legacy mode configurable (via argv?)
		self.data_if.send_msg(msg, legacy = True)

	# Simulation specific CTRL command handler
	def ctrl_cmd_handler(self, request):
		# Timing Advance
		# Syntax: CMD SETTA <TA>
		if self.ctrl_if.verify_cmd(request, "SETTA", 1):
			log.debug("(%s) Recv SETTA cmd" % self)

			# Store indicated value
			self.ta = int(request[1])
			return 0

		# Timing of Arrival simulation
		# Absolute form: CMD FAKE_TOA <BASE> <THRESH>
		elif self.ctrl_if.verify_cmd(request, "FAKE_TOA", 2):
			log.debug("(%s) Recv FAKE_TOA cmd" % self)

			# Parse and apply both base and threshold
			self.toa256_base = int(request[1])
			self.toa256_rand_threshold = int(request[2])
			return 0

		# Timing of Arrival simulation
		# Relative form: CMD FAKE_TOA <+-BASE_DELTA>
		elif self.ctrl_if.verify_cmd(request, "FAKE_TOA", 1):
			log.debug("(%s) Recv FAKE_TOA cmd" % self)

			# Parse and apply delta
			self.toa256_base += int(request[1])
			return 0

		# RSSI simulation
		# Absolute form: CMD FAKE_RSSI <BASE> <THRESH>
		elif self.ctrl_if.verify_cmd(request, "FAKE_RSSI", 2):
			log.debug("(%s) Recv FAKE_RSSI cmd" % self)

			# Parse and apply both base and threshold
			self.rssi_base = int(request[1])
			self.rssi_rand_threshold = int(request[2])
			return 0

		# RSSI simulation
		# Relative form: CMD FAKE_RSSI <+-BASE_DELTA>
		elif self.ctrl_if.verify_cmd(request, "FAKE_RSSI", 1):
			log.debug("(%s) Recv FAKE_RSSI cmd" % self)

			# Parse and apply delta
			self.rssi_base += int(request[1])
			return 0

		# Path loss simulation: burst dropping
		# Syntax: CMD FAKE_DROP <AMOUNT>
		# Dropping pattern: fn % 1 == 0
		elif self.ctrl_if.verify_cmd(request, "FAKE_DROP", 1):
			log.debug("(%s) Recv FAKE_DROP cmd" % self)

			# Parse / validate amount of bursts
			num = int(request[1])
			if num < 0:
				log.error("(%s) FAKE_DROP amount shall not "
					"be negative" % self)
				return -1

			self.burst_drop_amount = num
			self.burst_drop_period = 1
			return 0

		# Path loss simulation: burst dropping
		# Syntax: CMD FAKE_DROP <AMOUNT> <FN_PERIOD>
		# Dropping pattern: fn % period == 0
		elif self.ctrl_if.verify_cmd(request, "FAKE_DROP", 2):
			log.debug("(%s) Recv FAKE_DROP cmd" % self)

			# Parse / validate amount of bursts
			num = int(request[1])
			if num < 0:
				log.error("(%s) FAKE_DROP amount shall not "
					"be negative" % self)
				return -1

			# Parse / validate period
			period = int(request[2])
			if period <= 0:
				log.error("(%s) FAKE_DROP period shall "
					"be greater than zero" % self)
				return -1

			self.burst_drop_amount = num
			self.burst_drop_period = period
			return 0

		# Unhandled command
		return None

class Application(ApplicationBase):
	def __init__(self):
		self.app_print_copyright(APP_CR_HOLDERS)
		self.argv = self.parse_argv()

		# Set up signal handlers
		signal.signal(signal.SIGINT, self.sig_handler)

		# Configure logging
		self.app_init_logging(self.argv)

		# List of all transceivers
		self.trx_list = TRXList()

		# Init shared clock generator
		self.clck_gen = CLCKGen([])

		# Power measurement emulation
		# Noise: -120 .. -105
		# BTS: -75 .. -50
		self.fake_pm = FakePM(-120, -105, -75, -50)
		self.fake_pm.trx_list = self.trx_list

		# Init TRX instance for BTS
		self.append_trx(self.argv.bts_addr, self.argv.bts_base_port)

		# Init TRX instance for BB
		self.append_trx(self.argv.bb_addr, self.argv.bb_base_port)

		# Additional transceivers (optional)
		if self.argv.trx_list is not None:
			for trx_def in self.argv.trx_list:
				(addr, port, idx) = trx_def
				self.append_child_trx(addr, port, idx)

		# Burst forwarding between transceivers
		self.burst_fwd = BurstForwarder(self.trx_list)

		log.info("Init complete")

	def append_trx(self, remote_addr, base_port):
		trx = FakeTRX(self.argv.trx_bind_addr, remote_addr, base_port,
			clck_gen = self.clck_gen, pwr_meas = self.fake_pm)
		self.trx_list.add_trx(trx)

	def append_child_trx(self, remote_addr, base_port, child_idx):
		# Index 0 corresponds to the first transceiver
		if child_idx is 0:
			self.append_trx(remote_addr, base_port)
			return

		# Find 'parent' transceiver for a new child
		trx_parent = self.trx_list.find_trx(remote_addr, base_port)
		if trx_parent is None:
			raise IndexError("Couldn't find parent transceiver "
				"for '%s:%d/%d'" % (remote_addr, base_port, child_idx))

		# Allocate a new child
		trx_child = FakeTRX(self.argv.trx_bind_addr, remote_addr, base_port,
			child_idx = child_idx, pwr_meas = self.fake_pm)
		self.trx_list.add_trx(trx_child)

		# Link a new 'child' with its 'parent'
		trx_parent.child_trx_list.add_trx(trx_child)

	def run(self):
		# Compose list of to be monitored sockets
		sock_list = []
		for trx in self.trx_list:
			sock_list.append(trx.ctrl_if.sock)
			sock_list.append(trx.data_if.sock)

		# Enter main loop
		while True:
			# Wait until we get any data on any socket
			r_event, _, _ = select.select(sock_list, [], [])

			# Iterate over all transceivers
			for trx in self.trx_list:
				# DATA interface
				if trx.data_if.sock in r_event:
					msg = trx.recv_data_msg()
					if msg is not None:
						self.burst_fwd.forward_msg(trx, msg)

				# CTRL interface
				if trx.ctrl_if.sock in r_event:
					trx.ctrl_if.handle_rx()

	def shutdown(self):
		log.info("Shutting down...")

		# Stop clock generator
		self.clck_gen.stop()

	# Parses a TRX definition of the following
	# format: REMOTE_ADDR:BIND_PORT[/TRX_NUM]
	# e.g. [2001:0db8:85a3:0000:0000:8a2e:0370:7334]:5700/5
	# e.g. 127.0.0.1:5700 or 127.0.0.1:5700/1
	@staticmethod
	def trx_def(val):
		try:
			result = re.match("(.+):([0-9]+)(\/[0-9]+)?", val)
			(addr, port, idx) = result.groups()
		except:
			raise argparse.ArgumentTypeError("Invalid TRX definition: %s" % val)

		if idx is not None:
			idx = int(idx[1:])
		else:
			idx = 0

		return (addr, int(port), idx)

	def parse_argv(self):
		parser = argparse.ArgumentParser(prog = "fake_trx",
			description = "Virtual Um-interface (fake transceiver)")

		# Register common logging options
		self.app_reg_logging_options(parser)

		trx_group = parser.add_argument_group("TRX interface")
		trx_group.add_argument("-b", "--trx-bind-addr",
			dest = "trx_bind_addr", type = str, default = "0.0.0.0",
			help = "Set FakeTRX bind address (default %(default)s)")
		trx_group.add_argument("-R", "--bts-addr",
			dest = "bts_addr", type = str, default = "127.0.0.1",
			help = "Set BTS remote address (default %(default)s)")
		trx_group.add_argument("-r", "--bb-addr",
			dest = "bb_addr", type = str, default = "127.0.0.1",
			help = "Set BB remote address (default %(default)s)")
		trx_group.add_argument("-P", "--bts-base-port",
			dest = "bts_base_port", type = int, default = 5700,
			help = "Set BTS base port number (default %(default)s)")
		trx_group.add_argument("-p", "--bb-base-port",
			dest = "bb_base_port", type = int, default = 6700,
			help = "Set BB base port number (default %(default)s)")

		mtrx_group = parser.add_argument_group("Additional transceivers")
		mtrx_group.add_argument("--trx",
			metavar = "REMOTE_ADDR:BASE_PORT[/TRX_NUM]",
			dest = "trx_list", type = self.trx_def, action = "append",
			help = "Add a transceiver for BTS or MS (e.g. 127.0.0.1:5703)")

		argv = parser.parse_args()

		# Make sure there is no overlap between ports
		if argv.bts_base_port == argv.bb_base_port:
			parser.error("BTS and BB base ports shall be different")

		return argv

	def sig_handler(self, signum, frame):
		log.info("Signal %d received" % signum)
		if signum is signal.SIGINT:
			self.shutdown()
			sys.exit(0)

if __name__ == '__main__':
	app = Application()
	app.run()
