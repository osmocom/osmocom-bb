#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# TRX Toolkit
# Auxiliary tool to send existing bursts via TRX DATA interface
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
import sys

from app_common import ApplicationBase
from data_dump import DATADumpFile
from data_if import DATAInterface
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
		self.ddf = DATADumpFile(self.argv.capture_file)

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

		# Read messages from the capture
		messages = self.ddf.parse_all(
			skip = self.argv.cnt_skip, count = self.argv.cnt_count)
		if messages is False:
			pass # FIXME!!!

		for msg in messages:
			# Pass filter
			if not self.msg_pass_filter(msg):
				continue

			log.info("Sending a burst %s to %s..."
				% (msg.desc_hdr(), self.argv.conn_mode))

			# Send message
			self.data_if.send_msg(msg)

	def msg_pass_filter(self, msg):
		# Direction filter
		l12trx = self.argv.conn_mode == "TRX"
		if isinstance(msg, DATAMSG_L12TRX) and not l12trx:
			return False
		elif isinstance(msg, DATAMSG_TRX2L1) and l12trx:
			return False

		# Timeslot filter
		if self.argv.pf_tn is not None:
			if msg.tn != self.argv.pf_tn:
				return False

		# Frame number filter
		if self.argv.pf_fn_lt is not None:
			if msg.fn > self.argv.pf_fn_lt:
				return False
		if self.argv.pf_fn_gt is not None:
			if msg.fn < self.argv.pf_fn_gt:
				return False

		# Burst passed ;)
		return True

	def parse_argv(self):
		parser = argparse.ArgumentParser(prog = "burst_send",
			description = "Auxiliary tool to send (reply) captured bursts")

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
		trx_group.add_argument("-i", "--capture-file", metavar = "FILE",
			dest = "capture_file", type = str, required = True,
			help = "Capture file to read bursts from")

		cnt_group = parser.add_argument_group("Count limitations (optional)")
		cnt_group.add_argument("--skip", metavar = "N",
			dest = "cnt_skip", type = int,
			help = "Skip N messages before sending")
		cnt_group.add_argument("--count", metavar = "N",
			dest = "cnt_count", type = int,
			help = "Stop after sending N messages")

		pf_group = parser.add_argument_group("Filtering (optional)")
		cnt_group.add_argument("--timeslot", metavar = "TN",
			dest = "pf_tn", type = int, choices = range(0, 8),
			help = "TDMA timeslot number (equal TN)")
		cnt_group.add_argument("--frame-num-lt", metavar = "FN",
			dest = "pf_fn_lt", type = int,
			help = "TDMA frame number (lower than FN)")
		cnt_group.add_argument("--frame-num-gt", metavar = "FN",
			dest = "pf_fn_gt", type = int,
			help = "TDMA frame number (greater than FN)")

		return parser.parse_args()

	def sig_handler(self, signum, frame):
		log.info("Signal %d received" % signum)
		if signum is signal.SIGINT:
			sys.exit(0)

if __name__ == '__main__':
	app = Application()
	app.run()
