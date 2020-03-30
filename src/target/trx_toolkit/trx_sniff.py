#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# TRX Toolkit
# Scapy-based TRX interface sniffer
#
# (C) 2018-2020 by Vadim Yanitskiy <axilirator@gmail.com>
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

APP_CR_HOLDERS = [("2018-2020", "Vadim Yanitskiy <axilirator@gmail.com>")]

import logging as log
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
		# Compose a list of permitted UDP ports
		rx_port_list = ["port %d" % (port + 102) for port in self.argv.base_ports]
		tx_port_list = ["port %d" % (port +   2) for port in self.argv.base_ports]

		# Arguments to be passed to scapy.all.sniff()
		sniff_args = {
			"filter" : "udp and (%s)" % " or ".join(rx_port_list + tx_port_list),
			"prn" : self.pkt_handler,
			"store" : 0,
		}

		if self.argv.cap_file is not None:
			log.info("Reading packets from '%s'..." % self.argv.cap_file)
			sniff_args["offline"] = self.argv.cap_file
		else:
			log.info("Listening on interface '%s'..." % self.argv.sniff_if)
			sniff_args["iface"] = self.argv.sniff_if

		if self.argv.cap_filter is not None:
			log.info("Using additional capture filter '%s'" % self.argv.cap_filter)
			sniff_args["filter"] += " and (%s)" % self.argv.cap_filter

		# Start sniffing...
		scapy.all.sniff(**sniff_args)

		# Scapy registers its own signal handler
		self.shutdown()

	def pkt_handler(self, ether):
		# Prevent loopback packet duplication
		if self.argv.sniff_if == "lo" and self.argv.cap_file is None:
			self.lo_trigger = not self.lo_trigger
			if not self.lo_trigger:
				return

		# Extract a TRX payload
		ip = ether.payload
		udp = ip.payload
		trx = udp.payload

		# Convert to bytearray
		msg_raw = bytearray(trx.load)

		# Determine a burst direction (L1 <-> TRX)
		l12trx = udp.sport > udp.dport

		# Create an empty DATA message
		msg = DATAMSG_L12TRX() if l12trx else DATAMSG_TRX2L1()

		# Attempt to parse the payload as a DATA message
		try:
			msg.parse_msg(msg_raw)
			msg.validate()
		except ValueError as e:
			desc = msg.desc_hdr()
			if desc == "":
				desc = "parsing error"
			log.warning("Ignoring an incorrect message (%s): %s" % (desc, e))
			self.cnt_burst_dropped_num += 1
			return

		# Poke burst pass filter
		if not self.burst_pass_filter(msg):
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

	def burst_pass_filter(self, msg):
		# Direction filter
		if self.argv.direction is not None:
			if self.argv.direction == "TRX": # L1 -> TRX
				if not isinstance(msg, DATAMSG_L12TRX):
					return False
			elif self.argv.direction == "L1": # TRX -> L1
				if not isinstance(msg, DATAMSG_TRX2L1):
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

		# Message type specific filtering
		if isinstance(msg, DATAMSG_TRX2L1):
			# NOPE.ind filter
			if not self.argv.pf_nope_ind and msg.nope_ind:
				return False

			# RSSI filter
			if self.argv.pf_rssi_min is not None:
				if msg.rssi < self.argv.pf_rssi_min:
					return False
			if self.argv.pf_rssi_max is not None:
				if msg.rssi > self.argv.pf_rssi_max:
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
		trx_group.add_argument("-p", "--base-port", "--base-ports",
			dest = "base_ports", type = int, metavar = "PORT",
			default = [5700, 6700], nargs = "*",
			help = "Set base port number (default %(default)s)")
		trx_group.add_argument("-o", "--output-file", metavar = "FILE",
			dest = "output_file", type = str,
			help = "Write bursts to a capture file")

		input_group = trx_group.add_mutually_exclusive_group()
		input_group.add_argument("-i", "--sniff-interface",
			dest = "sniff_if", type = str, default = "lo", metavar = "IF",
			help = "Set network interface (default '%(default)s')")
		input_group.add_argument("-r", "--capture-file",
			dest = "cap_file", type = str, metavar = "FILE",
			help = "Read packets from a PCAP file")

		trx_group.add_argument("-f", "--capture-filter",
			dest = "cap_filter", type = str, metavar = "FILTER",
			help = "Set additional capture filter (e.g. 'host 192.168.1.2')")

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
		pf_group.add_argument("--no-nope-ind",
			dest = "pf_nope_ind", action = "store_false",
			help = "Ignore NOPE.ind (NOPE / IDLE indications)")
		pf_group.add_argument("--rssi-min", metavar = "RSSI",
			dest = "pf_rssi_min", type = int,
			help = "Minimum RSSI value (e.g. -75)")
		pf_group.add_argument("--rssi-max", metavar = "RSSI",
			dest = "pf_rssi_max", type = int,
			help = "Maximum RSSI value (e.g. -50)")

		return parser.parse_args()

if __name__ == '__main__':
	app = Application()
	app.run()
