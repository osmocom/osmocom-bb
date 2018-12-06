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

from copyright import print_copyright
CR_HOLDERS = [("2018", "Vadim Yanitskiy <axilirator@gmail.com>")]

import logging as log
import signal
import getopt
import sys

import scapy.all

from data_dump import DATADumpFile
from data_msg import *

class Application:
	# Application variables
	sniff_interface = "lo"
	sniff_base_port = 5700
	print_bursts = False
	output_file = None

	# Counters
	cnt_burst_dropped_num = 0
	cnt_burst_break = None
	cnt_burst_num = 0

	cnt_frame_break = None
	cnt_frame_last = None
	cnt_frame_num = 0

	# Burst direction fliter
	bf_dir_l12trx = None

	# Timeslot number filter
	bf_tn_val = None

	# Frame number fliter
	bf_fn_lt = None
	bf_fn_gt = None

	# Internal variables
	lo_trigger = False

	def __init__(self):
		print_copyright(CR_HOLDERS)
		self.parse_argv()

		# Configure logging
		log.basicConfig(level = log.DEBUG,
			format = "[%(levelname)s] %(filename)s:%(lineno)d %(message)s")

		# Open requested capture file
		if self.output_file is not None:
			self.ddf = DATADumpFile(self.output_file)

	def run(self):
		# Compose a packet filter
		pkt_filter = "udp and (port %d or port %d)" \
			% (self.sniff_base_port + 2, self.sniff_base_port + 102)

		log.info("Listening on interface '%s'..." % self.sniff_interface)

		# Start sniffing...
		scapy.all.sniff(iface = self.sniff_interface, store = 0,
			filter = pkt_filter, prn = self.pkt_handler)

		# Scapy registers its own signal handler
		self.shutdown()

	def pkt_handler(self, ether):
		# Prevent loopback packet duplication
		if self.sniff_interface == "lo":
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
		if self.bf_dir_l12trx is not None:
			if l12trx != self.bf_dir_l12trx:
				return False

		# Timeslot filter
		if self.bf_tn_val is not None:
			if tn != self.bf_tn_val:
				return False

		# Frame number filter
		if self.bf_fn_lt is not None:
			if fn > self.bf_fn_lt:
				return False
		if self.bf_fn_gt is not None:
			if fn < self.bf_fn_gt:
				return False

		# Burst passed ;)
		return True

	def msg_handle(self, msg):
		if self.print_bursts:
			print(msg.burst)

		# Append a new message to the capture
		if self.output_file is not None:
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
		if self.cnt_burst_break is not None:
			if self.cnt_burst_num == self.cnt_burst_break:
				log.info("Collected required amount of bursts")
				return True

		# Stop sniffing after N frames
		if self.cnt_frame_break is not None:
			if self.cnt_frame_num == self.cnt_frame_break:
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

	def print_help(self, msg = None):
		s  = " Usage: " + sys.argv[0] + " [options]\n\n" \
			 " Some help...\n" \
			 "  -h --help              this text\n\n"

		s += " Sniffing options\n" \
			 "  -i --sniff-interface   Set network interface (default '%s')\n"  \
			 "  -p --sniff-base-port   Set base port number (default %d)\n\n"

		s += " Processing (no processing by default)\n" \
			 "  -o --output-file       Write bursts to file\n"          \
			 "  -v --print-bits        Print burst bits to stdout\n\n"  \

		s += " Count limitations (disabled by default)\n" \
			 "  --frame-count   NUM    Stop after sniffing NUM frames\n"  \
			 "  --burst-count   NUM    Stop after sniffing NUM bursts\n\n"

		s += " Filtering (disabled by default)\n" \
			 "  --direction     DIR    Burst direction: L12TRX or TRX2L1\n"  \
			 "  --timeslot      NUM    TDMA timeslot number [0..7]\n"        \
			 "  --frame-num-lt  NUM    TDMA frame number lower than NUM\n"   \
			 "  --burst-num-gt  NUM    TDMA frame number greater than NUM\n"

		print(s % (self.sniff_interface, self.sniff_base_port))

		if msg is not None:
			print(msg)

	def parse_argv(self):
		try:
			opts, args = getopt.getopt(sys.argv[1:],
				"i:p:o:v:h", ["help", "sniff-interface=", "sniff-base-port=",
					"frame-count=", "burst-count=", "direction=",
					"timeslot=", "frame-num-lt=", "frame-num-gt=",
					"output-file=", "print-bits"])
		except getopt.GetoptError as err:
			self.print_help("[!] " + str(err))
			sys.exit(2)

		for o, v in opts:
			if o in ("-h", "--help"):
				self.print_help()
				sys.exit(2)

			elif o in ("-i", "--sniff-interface"):
				self.sniff_interface = v
			elif o in ("-p", "--sniff-base-port"):
				self.sniff_base_port = int(v)

			elif o in ("-o", "--output-file"):
				self.output_file = v
			elif o in ("-v", "--print-bits"):
				self.print_bursts = True

			# Break counters
			elif o == "--frame-count":
				self.cnt_frame_break = int(v)
			elif o == "--burst-count":
				self.cnt_burst_break = int(v)

			# Direction filter
			elif o == "--direction":
				if v == "L12TRX":
					self.bf_dir_l12trx = True
				elif v == "TRX2L1":
					self.bf_dir_l12trx = False
				else:
					self.print_help("[!] Wrong direction argument")
					sys.exit(2)

			# Timeslot pass filter
			elif o == "--timeslot":
				self.bf_tn_val = int(v)
				if self.bf_tn_val < 0 or self.bf_tn_val > 7:
					self.print_help("[!] Wrong timeslot value")
					sys.exit(2)

			# Frame number pass filter
			elif o == "--frame-num-lt":
				self.bf_fn_lt = int(v)
			elif o == "--frame-num-gt":
				self.bf_fn_gt = int(v)

if __name__ == '__main__':
	app = Application()
	app.run()
