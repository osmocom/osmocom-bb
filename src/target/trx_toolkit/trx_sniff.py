#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# TRX Toolkit
# Scapy-based TRX interface sniffer
#
# (C) 2018 by Vadim Yanitskiy <axilirator@gmail.com>
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

APP_CR_HOLDERS = [("2018", "Vadim Yanitskiy <axilirator@gmail.com>")]

import logging as log
import signal
import argparse
import sys

import scapy.all

from app_common import ApplicationBase
from data_dump import DATADumpFile
from data_msg import *

class Application(ApplicationBase):
	# Counters
	cnt_burst_dropped_num = 0
	cnt_burst_num = 0

	cnt_frame_last = None
	cnt_frame_num = 0

	# Internal variables
	lo_trigger = False

	def __init__(self):
		self.app_print_copyright(APP_CR_HOLDERS)
		self.argv = self.parse_argv()

		# Configure logging
		self.app_init_logging(self.argv)

		# Open requested capture file
		if self.argv.output_file is not None:
			self.ddf = DATADumpFile(self.argv.output_file)

	def run(self):
		# Compose a packet filter
		pkt_filter = "udp and (port %d or port %d)" \
			% (self.argv.base_port + 2, self.argv.base_port + 102)

		log.info("Listening on interface '%s'..." % self.argv.sniff_if)

		# Start sniffing...
		scapy.all.sniff(iface = self.argv.sniff_if, store = 0,
			filter = pkt_filter, prn = self.pkt_handler)

		# Scapy registers its own signal handler
		self.shutdown()

	def pkt_handler(self, ether):
		# Prevent loopback packet duplication
		if self.argv.sniff_if == "lo":
			self.lo_trigger = not self.lo_trigger
			if not self.lo_trigger:
				return

		# Extract a TRX payload
		ip = ether.payload
		udp = ip.payload
		trx = udp.payload

		# Convert to bytearray
		msg_raw = bytearray(str(trx))

		# Determine a burst direction (L1 <-> TRX)
		l12trx = udp.sport > udp.dport

		# Create an empty DATA message
		msg = DATAMSG_L12TRX() if l12trx else DATAMSG_TRX2L1()

		# Attempt to parse the payload as a DATA message
		try:
			msg.parse_msg(msg_raw)
		except:
			log.warning("Failed to parse message, dropping...")
			self.cnt_burst_dropped_num += 1
			return

		# Poke burst pass filter
		rc = self.burst_pass_filter(l12trx, msg.fn, msg.tn)
		if rc is False:
			self.cnt_burst_dropped_num += 1
			return

		# Debug print
		log.debug("%s burst: %s" \
			% ("L1 -> TRX" if l12trx else "TRX -> L1", msg.desc_hdr()))

		# Poke message handler
		self.msg_handle(msg)

		# Poke burst counter
		rc = self.burst_count(msg.fn, msg.tn)
		if rc is True:
			self.shutdown()

	def burst_pass_filter(self, l12trx, fn, tn):
		# Direction filter
		if self.argv.direction is not None:
			if self.argv.direction == "TRX" and not l12trx:
				return False
			elif self.argv.direction == "L1" and l12trx:
				return False

		# Timeslot filter
		if self.argv.pf_tn is not None:
			if tn != self.argv.pf_tn:
				return False

		# Frame number filter
		if self.argv.pf_fn_lt is not None:
			if fn > self.argv.pf_fn_lt:
				return False
		if self.argv.pf_fn_gt is not None:
			if fn < self.argv.pf_fn_gt:
				return False

		# Burst passed ;)
		return True

	def msg_handle(self, msg):
		if self.argv.verbose:
			print(msg.burst)

		# Append a new message to the capture
		if self.argv.output_file is not None:
			self.ddf.append_msg(msg)

	def burst_count(self, fn, tn):
		# Update frame counter
		if self.cnt_frame_last is None:
			self.cnt_frame_last = fn
			self.cnt_frame_num += 1
		else:
			if fn != self.cnt_frame_last:
				self.cnt_frame_num += 1

		# Update burst counter
		self.cnt_burst_num += 1

		# Stop sniffing after N bursts
		if self.argv.burst_count is not None:
			if self.cnt_burst_num == self.argv.burst_count:
				log.info("Collected required amount of bursts")
				return True

		# Stop sniffing after N frames
		if self.argv.frame_count is not None:
			if self.cnt_frame_num == self.argv.frame_count:
				log.info("Collected required amount of frames")
				return True

		return False

	def shutdown(self):
		log.info("Shutting down...")

		# Print statistics
		log.info("%u bursts handled, %u dropped" \
			% (self.cnt_burst_num, self.cnt_burst_dropped_num))

		# Exit
		sys.exit(0)

	def parse_argv(self):
		parser = argparse.ArgumentParser(prog = "trx_sniff",
			description = "Scapy-based TRX interface sniffer")

		parser.add_argument("-v", "--verbose",
			dest = "verbose", action = "store_true",
			help = "Print burst bits to stdout")

		# Register common logging options
		self.app_reg_logging_options(parser)

		trx_group = parser.add_argument_group("TRX interface")
		trx_group.add_argument("-i", "--sniff-interface",
			dest = "sniff_if", type = str, default = "lo", metavar = "IF",
			help = "Set network interface (default '%(default)s')")
		trx_group.add_argument("-p", "--base-port",
			dest = "base_port", type = int, default = 6700,
			help = "Set base port number (default %(default)s)")
		trx_group.add_argument("-o", "--output-file", metavar = "FILE",
			dest = "output_file", type = str,
			help = "Write bursts to a capture file")

		cnt_group = parser.add_argument_group("Count limitations (optional)")
		cnt_group.add_argument("--frame-count", metavar = "N",
			dest = "frame_count", type = int,
			help = "Stop after sniffing N frames")
		cnt_group.add_argument("--burst-count", metavar = "N",
			dest = "burst_count", type = int,
			help = "Stop after sniffing N bursts")

		pf_group = parser.add_argument_group("Filtering (optional)")
		pf_group.add_argument("--direction",
			dest = "direction", type = str, choices = ["TRX", "L1"],
			help = "Burst direction")
		pf_group.add_argument("--timeslot", metavar = "TN",
			dest = "pf_tn", type = int, choices = range(0, 8),
			help = "TDMA timeslot number (equal TN)")
		pf_group.add_argument("--frame-num-lt", metavar = "FN",
			dest = "pf_fn_lt", type = int,
			help = "TDMA frame number (lower than FN)")
		pf_group.add_argument("--frame-num-gt", metavar = "FN",
			dest = "pf_fn_gt", type = int,
			help = "TDMA frame number (greater than FN)")

		return parser.parse_args()

if __name__ == '__main__':
	app = Application()
	app.run()
