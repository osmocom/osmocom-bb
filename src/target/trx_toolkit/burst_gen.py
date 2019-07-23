#!/usr/bin/env python
# -*- coding: utf-8 -*-

# TRX Toolkit
# Auxiliary tool to generate and send random bursts via TRX DATA
# interface, which may be useful for fuzzing and testing
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
import signal
import argparse
import sys

from app_common import ApplicationBase
from rand_burst_gen import RandBurstGen
from data_dump import DATADumpFile
from data_if import DATAInterface
from gsm_shared import *
from data_msg import *

class Application(ApplicationBase):
	def __init__(self):
		self.app_print_copyright(APP_CR_HOLDERS)
		self.argv = self.parse_argv()

		# Set up signal handlers
		signal.signal(signal.SIGINT, self.sig_handler)

		# Configure logging
		self.app_init_logging(self.argv)

		# Open requested capture file
		if self.argv.output_file is not None:
			self.ddf = DATADumpFile(self.argv.output_file)

	def run(self):
		# Init DATA interface with TRX or L1
		if self.argv.conn_mode == "TRX":
			self.data_if = DATAInterface(
				self.argv.remote_addr, self.argv.base_port + 2,
				self.argv.bind_addr, self.argv.base_port + 102)
		elif self.argv.conn_mode == "L1":
			self.data_if = DATAInterface(
				self.argv.remote_addr, self.argv.base_port + 102,
				self.argv.bind_addr, self.argv.base_port + 2)

		# Init random burst generator
		burst_gen = RandBurstGen()

		# Init an empty DATA message
		if self.argv.conn_mode == "TRX":
			msg = DATAMSG_L12TRX(ver = self.argv.hdr_ver)
		elif self.argv.conn_mode == "L1":
			msg = DATAMSG_TRX2L1(ver = self.argv.hdr_ver)

		# Generate a random frame number or use provided one
		fn_init = msg.rand_fn() if self.argv.tdma_fn is None \
			else self.argv.tdma_fn

		# Send as much bursts as required
		for i in range(self.argv.burst_count):
			# Randomize the message header
			msg.rand_hdr()

			# Increase and set frame number
			msg.fn = (fn_init + i) % GSM_HYPERFRAME

			# Set timeslot number
			if self.argv.tdma_tn is not None:
				msg.tn = self.argv.tdma_tn

			# Set transmit power level
			if self.argv.pwr is not None:
				msg.pwr = self.argv.pwr

			# Set time of arrival
			if self.argv.toa is not None:
				msg.toa256 = int(float(self.argv.toa) * 256.0 + 0.5)
			elif self.argv.toa256 is not None:
				msg.toa256 = self.argv.toa256

			# Set RSSI
			if self.argv.rssi is not None:
				msg.rssi = self.argv.rssi

			if msg.ver >= 0x01:
				# TODO: Only GMSK and TSC set 0 for now
				msg.mod_type = Modulation.ModGMSK
				self.tsc_set = 0

				if self.argv.tsc is not None:
					msg.tsc = self.argv.tsc

				if self.argv.ci is not None:
					msg.ci = self.argv.ci

			# Generate a random burst
			if self.argv.burst_type == "NB":
				burst = burst_gen.gen_nb()
			elif self.argv.burst_type == "FB":
				burst = burst_gen.gen_fb()
			elif self.argv.burst_type == "SB":
				burst = burst_gen.gen_sb()
			elif self.argv.burst_type == "AB":
				burst = burst_gen.gen_ab()

			# Convert to soft-bits in case of TRX -> L1 message
			if self.argv.conn_mode == "L1":
				burst = msg.ubit2sbit(burst)

			# Set burst
			msg.burst = burst

			log.info("Sending %d/%d %s burst %s to %s..."
				% (i + 1, self.argv.burst_count, self.argv.burst_type,
					msg.desc_hdr(), self.argv.conn_mode))

			# Send message
			self.data_if.send_msg(msg)

			# Append a new message to the capture
			if self.argv.output_file is not None:
				self.ddf.append_msg(msg)

	def parse_argv(self):
		parser = argparse.ArgumentParser(prog = "burst_gen",
			description = "Auxiliary tool to generate and send random bursts")

		# Register common logging options
		self.app_reg_logging_options(parser)

		trx_group = parser.add_argument_group("TRX interface")
		trx_group.add_argument("-r", "--remote-addr",
			dest = "remote_addr", type = str, default = "127.0.0.1",
			help = "Set remote address (default %(default)s)")
		trx_group.add_argument("-b", "--bind-addr",
			dest = "bind_addr", type = str, default = "0.0.0.0",
			help = "Set bind address (default %(default)s)")
		trx_group.add_argument("-p", "--base-port",
			dest = "base_port", type = int, default = 6700,
			help = "Set base port number (default %(default)s)")
		trx_group.add_argument("-m", "--conn-mode",
			dest = "conn_mode", type = str,
			choices = ["TRX", "L1"], default = "TRX",
			help = "Where to send bursts (default %(default)s)")
		trx_group.add_argument("-o", "--output-file",
			dest = "output_file", type = str,
			help = "Write bursts to a capture file")

		bg_group = parser.add_argument_group("Burst generation")
		bg_group.add_argument("-B", "--burst-type",
			dest = "burst_type", type = str,
			choices = ["NB", "FB", "SB", "AB"], default = "NB",
			help = "Random burst type (default %(default)s)")
		bg_group.add_argument("-c", "--burst-count", metavar = "N",
			dest = "burst_count", type = int, default = 1,
			help = "How many bursts to send (default %(default)s)")
		bg_group.add_argument("-v", "--hdr-version", metavar = "VER",
			dest = "hdr_ver", type = int,
			default = 0, choices = DATAMSG.known_versions,
			help = "TRXD header version (default %(default)s)")
		bg_group.add_argument("-f", "--frame-number", metavar = "FN",
			dest = "tdma_fn", type = int,
			help = "Set TDMA frame number (default random)")
		bg_group.add_argument("-t", "--timeslot", metavar = "TN",
			dest = "tdma_tn", type = int, choices = range(0, 8),
			help = "Set TDMA timeslot (default random)")

		bg_pwr_group = bg_group.add_mutually_exclusive_group()
		bg_pwr_group.add_argument("--pwr", metavar = "dBm",
			dest = "pwr", type = int,
			help = "Set power level (default random)")
		bg_pwr_group.add_argument("--rssi", metavar = "dBm",
			dest = "rssi", type = int,
			help = "Set RSSI (default random)")

		bg_toa_group = bg_group.add_mutually_exclusive_group()
		bg_toa_group.add_argument("--toa",
			dest = "toa", type = int,
			help = "Set Timing of Arrival in symbols (default random)")
		bg_toa_group.add_argument("--toa256",
			dest = "toa256", type = int,
			help = "Set Timing of Arrival in 1/256 symbol periods")

		bg_group.add_argument("--tsc", metavar = "TSC",
			dest = "tsc", type = int, choices = range(0, 8),
			help = "Set Training Sequence Code (default random)")
		bg_group.add_argument("--ci", metavar = "CI",
			dest = "ci", type = int,
			help = "C/I: Carrier-to-Interference ratio "
			       "in centiBels (default random)")

		return parser.parse_args()

	def sig_handler(self, signum, frame):
		log.info("Signal %d received" % signum)
		if signum == signal.SIGINT:
			sys.exit(0)

if __name__ == '__main__':
	app = Application()
	app.run()
