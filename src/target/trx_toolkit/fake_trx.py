#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# TRX Toolkit
# Virtual Um-interface (fake transceiver)
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

APP_CR_HOLDERS = [("2017-2019", "Vadim Yanitskiy <axilirator@gmail.com>")]

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
from data_msg import Modulation
from clck_gen import CLCKGen
from trx_list import TRXList
from fake_pm import FakePM
from gsm_shared import *

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

	  - C/I (Carrier-to-Interference ratio) - value in cB (centiBels),
	    computed from the training sequence of each received burst, by
	    comparing the "ideal" training sequence with the actual one.
	    A pair of both base and threshold values defines a range of
	    C/I randomization:

	      from (ci_base - ci_rand_threshold)
	        to (ci_base + ci_rand_threshold).

	Please note that the randomization is optional and disabled by default.

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

	NOMINAL_TX_POWER_DEFAULT = 50 # dBm
	TX_ATT_DEFAULT = 0 # dB
	PATH_LOSS_DEFAULT = 110 # dB

	TOA256_BASE_DEFAULT = 0
	CI_BASE_DEFAULT = 90

	# Default values for NOPE / IDLE indications
	TOA256_NOISE_DEFAULT = 0
	RSSI_NOISE_DEFAULT = -110
	CI_NOISE_DEFAULT = -30

	def __init__(self, *trx_args, **trx_kwargs):
		Transceiver.__init__(self, *trx_args, **trx_kwargs)

		# fake RSSI is disabled by default, only enabled through TRXC FAKE_RSSI.
		# When disabled, RSSI is calculated based on Tx power and Rx path loss
		self.fake_rssi_enabled = False

		self.rf_muted = False

		# Actual ToA, RSSI, C/I, TA values
		self.tx_power_base = self.NOMINAL_TX_POWER_DEFAULT
		self.tx_att_base = self.TX_ATT_DEFAULT
		self.toa256_base = self.TOA256_BASE_DEFAULT
		self.rssi_base = self.NOMINAL_TX_POWER_DEFAULT - self.TX_ATT_DEFAULT - self.PATH_LOSS_DEFAULT
		self.ci_base = self.CI_BASE_DEFAULT
		self.ta = 0

		# ToA, RSSI, C/I randomization thresholds
		self.toa256_rand_threshold = 0
		self.rssi_rand_threshold = 0
		self.ci_rand_threshold = 0

		# Path loss simulation (burst dropping)
		self.burst_drop_amount = 0
		self.burst_drop_period = 1

	@property
	def toa256(self):
		# Check if randomization is required
		if self.toa256_rand_threshold == 0:
			return self.toa256_base

		# Generate a random ToA value in required range
		toa256_min = self.toa256_base - self.toa256_rand_threshold
		toa256_max = self.toa256_base + self.toa256_rand_threshold
		return random.randint(toa256_min, toa256_max)

	@property
	def rssi(self):
		# Check if randomization is required
		if self.rssi_rand_threshold == 0:
			return self.rssi_base

		# Generate a random RSSI value in required range
		rssi_min = self.rssi_base - self.rssi_rand_threshold
		rssi_max = self.rssi_base + self.rssi_rand_threshold
		return random.randint(rssi_min, rssi_max)

	@property
	def tx_power(self):
		return self.tx_power_base - self.tx_att_base

	@property
	def ci(self):
		# Check if randomization is required
		if self.ci_rand_threshold == 0:
			return self.ci_base

		# Generate a random C/I value in required range
		ci_min = self.ci_base - self.ci_rand_threshold
		ci_max = self.ci_base + self.ci_rand_threshold
		return random.randint(ci_min, ci_max)

	# Path loss simulation: burst dropping
	# Returns: True - drop, False - keep
	def sim_burst_drop(self, msg):
		# Check if dropping is required
		if self.burst_drop_amount == 0:
			return False

		if msg.fn % self.burst_drop_period == 0:
			log.info("(%s) Simulation: dropping burst (fn=%u %% %u == 0)"
				% (self, msg.fn, self.burst_drop_period))
			self.burst_drop_amount -= 1
			return True

		return False

	def _handle_data_msg_v1(self, src_msg, msg):
		# C/I (Carrier-to-Interference ratio)
		msg.ci = self.ci

		# Pick modulation type by burst length
		bl = len(src_msg.burst)
		msg.mod_type = Modulation.pick_by_bl(bl)

		# Pick TSC (Training Sequence Code) and TSC set
		if msg.mod_type is Modulation.ModGMSK:
			ss = TrainingSeqGMSK.pick(src_msg.burst)
			msg.tsc = ss.tsc if ss is not None else 0
			msg.tsc_set = ss.tsc_set if ss is not None else 0
		else: # TODO: other modulation types (at least 8-PSK)
			msg.tsc_set = 0
			msg.tsc = 0

	# Takes (partially initialized) TRXD Rx message,
	# simulates RF path parameters (such as RSSI),
	# and sends towards the L1
	def handle_data_msg(self, src_trx, src_msg, msg):
		if self.rf_muted:
			msg.nope_ind = True
		elif not msg.nope_ind:
			# Path loss simulation
			msg.nope_ind = self.sim_burst_drop(msg)
		if msg.nope_ind:
			# Before TRXDv1, we simply drop the message
			if msg.ver < 0x01:
				del msg
				return

			# Since TRXDv1, we should send a NOPE.ind
			del msg.burst # burst bits are omited
			msg.burst = None

			# TODO: shoud we make these values configurable?
			msg.toa256 = self.TOA256_NOISE_DEFAULT
			msg.rssi = self.RSSI_NOISE_DEFAULT
			msg.ci = self.CI_NOISE_DEFAULT

			self.data_if.send_msg(msg)
			return

		# Complete message header
		msg.toa256 = self.toa256

		# Apply RSSI based on transmitter:
		if not self.fake_rssi_enabled:
			msg.rssi = src_trx.tx_power - src_msg.pwr - self.PATH_LOSS_DEFAULT
		else: # Apply fake RSSI
			msg.rssi = self.rssi

		# Version specific fields
		if msg.ver >= 0x01:
			self._handle_data_msg_v1(src_msg, msg)

		# Apply optional Timing Advance
		if src_trx.ta != 0:
			msg.toa256 -= src_trx.ta * 256

		Transceiver.handle_data_msg(self, msg)

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

			# Use negative threshold to disable fake_rssi if previously enabled:
			if int(request[2]) < 0:
				self.fake_rssi_enabled = False
				return 0

			# Parse and apply both base and threshold
			self.rssi_base = int(request[1])
			self.rssi_rand_threshold = int(request[2])
			self.fake_rssi_enabled = True
			return 0

		# RSSI simulation
		# Relative form: CMD FAKE_RSSI <+-BASE_DELTA>
		elif self.ctrl_if.verify_cmd(request, "FAKE_RSSI", 1):
			log.debug("(%s) Recv FAKE_RSSI cmd" % self)

			# Parse and apply delta
			self.rssi_base += int(request[1])
			return 0

		# C/I simulation
		# Absolute form: CMD FAKE_CI <BASE> <THRESH>
		elif self.ctrl_if.verify_cmd(request, "FAKE_CI", 2):
			log.debug("(%s) Recv FAKE_CI cmd" % self)

			# Parse and apply both base and threshold
			self.ci_base = int(request[1])
			self.ci_rand_threshold = int(request[2])
			return 0

		# C/I simulation
		# Relative form: CMD FAKE_CI <+-BASE_DELTA>
		elif self.ctrl_if.verify_cmd(request, "FAKE_CI", 1):
			log.debug("(%s) Recv FAKE_CI cmd" % self)

			# Parse and apply delta
			self.ci_base += int(request[1])
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

		# Artificial delay for the TRXC interface
		# Syntax: CMD FAKE_TRXC_DELAY <DELAY_MS>
		elif self.ctrl_if.verify_cmd(request, "FAKE_TRXC_DELAY", 1):
			log.debug("(%s) Recv FAKE_TRXC_DELAY cmd", self)

			self.ctrl_if.rsp_delay_ms = int(request[1])
			log.info("(%s) Artificial TRXC delay set to %d",
				 self, self.ctrl_if.rsp_delay_ms)

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
		self.clck_gen = CLCKGen([], sched_rr_prio = self.argv.sched_rr_prio)
		# This method will be called on each TDMA frame
		self.clck_gen.clck_handler = self.clck_handler

		# Power measurement emulation
		# Noise: -120 .. -105
		# BTS: -75 .. -50
		self.fake_pm = FakePM(-120, -105, -75, -50)
		self.fake_pm.trx_list = self.trx_list

		# Init TRX instance for BTS
		self.append_trx(self.argv.bts_addr, self.argv.bts_base_port, name = "BTS")

		# Init TRX instance for BB
		self.append_trx(self.argv.bb_addr, self.argv.bb_base_port, name = "MS", child_mgt = False)

		# Additional transceivers (optional)
		if self.argv.trx_list is not None:
			for trx_def in self.argv.trx_list:
				(name, addr, port, idx) = trx_def
				self.append_child_trx(addr, port, name = name, child_idx = idx)

		# Burst forwarding between transceivers
		self.burst_fwd = BurstForwarder(self.trx_list.trx_list)

		log.info("Init complete")

	def append_trx(self, remote_addr, base_port, **kwargs):
		trx = FakeTRX(self.argv.trx_bind_addr, remote_addr, base_port,
			clck_gen = self.clck_gen, pwr_meas = self.fake_pm, **kwargs)
		self.trx_list.add_trx(trx)

	def append_child_trx(self, remote_addr, base_port, **kwargs):
		child_idx = kwargs.get("child_idx", 0)
		if child_idx == 0:  # Index 0 indicates parent transceiver
			self.append_trx(remote_addr, base_port, **kwargs)
			return

		# Find 'parent' transceiver for a new child
		trx_parent = self.trx_list.find_trx(remote_addr, base_port)
		if trx_parent is None:
			raise IndexError("Couldn't find parent transceiver "
				"for '%s:%d/%d'" % (remote_addr, base_port, child_idx))

		# Allocate a new child
		trx_child = FakeTRX(self.argv.trx_bind_addr, remote_addr, base_port,
			pwr_meas = self.fake_pm, **kwargs)
		self.trx_list.add_trx(trx_child)

		# Link a new 'child' with its 'parent'
		trx_parent.child_trx_list.add_trx(trx_child)

	def run(self):
		# Compose list of to be monitored sockets
		sock_list = []
		for trx in self.trx_list.trx_list:
			sock_list.append(trx.ctrl_if.sock)
			sock_list.append(trx.data_if.sock)

		# Enter main loop
		while True:
			# Wait until we get any data on any socket
			r_event, _, _ = select.select(sock_list, [], [])

			# Iterate over all transceivers
			for trx in self.trx_list.trx_list:
				# DATA interface
				if trx.data_if.sock in r_event:
					trx.recv_data_msg()

				# CTRL interface
				if trx.ctrl_if.sock in r_event:
					trx.ctrl_if.handle_rx()

	# This method will be called by the clock thread
	def clck_handler(self, fn):
		# We assume that this list is immutable at run-time
		for trx in self.trx_list.trx_list:
			trx.clck_tick(self.burst_fwd, fn)

	def shutdown(self):
		log.info("Shutting down...")

		# Stop clock generator
		self.clck_gen.stop()

	# Parses a TRX definition of the following
	# format: REMOTE_ADDR:BIND_PORT[/TRX_NUM]
	# e.g. [2001:0db8:85a3:0000:0000:8a2e:0370:7334]:5700/5
	# e.g. 127.0.0.1:5700 or 127.0.0.1:5700/1
	# e.g. foo@127.0.0.1:5700 or bar@127.0.0.1:5700/1
	@staticmethod
	def trx_def(val):
		try:
			result = re.match(r"(.+@)?(.+):([0-9]+)(/[0-9]+)?", val)
			(name, addr, port, idx) = result.groups()
		except:
			raise argparse.ArgumentTypeError("Invalid TRX definition: %s" % val)

		if idx is not None:
			idx = int(idx[1:])
		else:
			idx = 0

		# Cut '@' from TRX name
		if name is not None:
			name = name[:-1]

		return (name, addr, int(port), idx)

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
		trx_group.add_argument("-s", "--sched-rr-prio",
			dest = "sched_rr_prio", type = int, default = None,
			help = "Set Scheduler RR Priority (default None)")

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
		if signum == signal.SIGINT:
			self.shutdown()
			sys.exit(0)

if __name__ == '__main__':
	app = Application()
	app.run()
