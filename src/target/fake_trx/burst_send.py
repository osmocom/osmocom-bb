#!/usr/bin/env python2
# -*- coding: utf-8 -*-

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

import signal
import getopt
import sys

from data_if import DATAInterface
from gsm_shared import *
from data_msg import *

COPYRIGHT = \
	"Copyright (C) 2017 by Vadim Yanitskiy <axilirator@gmail.com>\n" \
	"License GPLv2+: GNU GPL version 2 or later " \
	"<http://gnu.org/licenses/gpl.html>\n" \
	"This is free software: you are free to change and redistribute it.\n" \
	"There is NO WARRANTY, to the extent permitted by law.\n"

class Application:
	# Application variables
	remote_addr = "127.0.0.1"
	base_port = 5700
	conn_mode = "TRX"

	burst_src = None
	
	# Common header fields
	fn = None
	tn = None

	# Message specific header fields
	rssi = None
	toa = None
	pwr = None

	def __init__(self):
		self.print_copyright()
		self.parse_argv()

		# Set up signal handlers
		signal.signal(signal.SIGINT, self.sig_handler)

	def run(self):
		# Init DATA interface with TRX or L1
		if self.conn_mode == "TRX":
			self.data_if = DATAInterface(self.remote_addr,
				self.base_port + 2, self.base_port + 102)
		elif self.conn_mode == "L1":
			self.data_if = DATAInterface(self.remote_addr,
				self.base_port + 102, self.base_port + 2)
		else:
			self.print_help("[!] Unknown connection type")
			sys.exit(2)

		# Open the burst source (file or stdin)
		if self.burst_src is not None:
			print("[i] Reading bursts from file '%s'..." % self.burst_src)
			src = open(self.burst_src, "r")
		else:
			print("[i] Reading bursts from stdin...")
			src = sys.stdin

		# Init an empty DATA message
		if self.conn_mode == "TRX":
			msg = DATAMSG_L12TRX()
		elif self.conn_mode == "L1":
			msg = DATAMSG_TRX2L1()

		# Generate a random frame number or use provided one
		fn = msg.rand_fn() if self.fn is None else self.fn

		# Read the burst source line-by-line
		for line in src:
			# Strip spaces
			burst_str = line.strip()
			burst = []

			# Check length
			if len(burst_str) not in (GSM_BURST_LEN, EDGE_BURST_LEN):
				print("[!] Dropping burst due to incorrect length")
				continue

			# Randomize the message header
			msg.rand_hdr()

			# Set frame number
			msg.fn = fn

			# Set timeslot number
			if self.tn is not None:
				msg.tn = self.tn

			# Set transmit power level
			if self.pwr is not None:
				msg.pwr = self.pwr

			# Set time of arrival
			if self.toa is not None:
				msg.toa = self.toa

			# Set RSSI
			if self.rssi is not None:
				msg.rssi = self.rssi

			# Parse a string
			for bit in burst_str:
				if bit == "1":
					burst.append(1)
				else:
					burst.append(0)

			# Convert to soft-bits in case of TRX -> L1 message
			if self.conn_mode == "L1":
				burst = msg.ubit2sbit(burst)

			# Set burst
			msg.burst = burst

			print("[i] Sending a burst %s to %s..."
				% (msg.desc_hdr(), self.conn_mode))

			# Send message
			self.data_if.send_msg(msg)

			# Increase frame number (for count > 1)
			fn = (fn + 1) % GSM_HYPERFRAME

		# Finish
		self.shutdown()

	def print_copyright(self):
		print(COPYRIGHT)

	def print_help(self, msg = None):
		s  = " Usage: " + sys.argv[0] + " [options]\n\n" \
			 " Some help...\n" \
			 "  -h --help           this text\n\n"

		s += " TRX interface specific\n" \
			 "  -m --conn-mode      Send bursts to: TRX (default) / L1\n"    \
			 "  -r --remote-addr    Set remote address (default %s)\n"       \
			 "  -p --base-port      Set base port number (default %d)\n\n"

		s += " Burst generation\n" \
			 "  -i --burst-file     Read bursts from file (default stdin)\n" \
			 "  -f --frame-number   Set frame number (default random)\n"     \
			 "  -t --timeslot       Set timeslot index (default random)\n"   \
			 "     --pwr            Set power level (default random)\n"      \
			 "     --rssi           Set RSSI (default random)\n"             \
			 "     --toa            Set TOA (default random)\n\n"

		print(s % (self.remote_addr, self.base_port))

		if msg is not None:
			print(msg)

	def parse_argv(self):
		try:
			opts, args = getopt.getopt(sys.argv[1:],
				"m:r:p:i:f:t:h",
				[
					"help",
					"conn-mode=",
					"remote-addr=",
					"base-port=",
					"burst-file=",
					"frame-number=",
					"timeslot=",
					"rssi=",
					"toa=",
					"pwr=",
				])
		except getopt.GetoptError as err:
			self.print_help("[!] " + str(err))
			sys.exit(2)

		for o, v in opts:
			if o in ("-h", "--help"):
				self.print_help()
				sys.exit(2)

			elif o in ("-m", "--conn-mode"):
				self.conn_mode = v
			elif o in ("-r", "--remote-addr"):
				self.remote_addr = v
			elif o in ("-p", "--base-port"):
				self.base_port = int(v)

			elif o in ("-i", "--burst-file"):
				self.burst_src = v
			elif o in ("-f", "--frame-number"):
				self.fn = int(v)
			elif o in ("-t", "--timeslot"):
				self.tn = int(v)

			# Message specific header fields
			elif o == "--pwr":
				self.pwr = int(v)
			elif o == "--rssi":
				self.rssi = int(v)
			elif o == "--toa":
				self.toa = float(v)

	def shutdown(self):
		self.data_if.shutdown()

	def sig_handler(self, signum, frame):
		print("Signal %d received" % signum)
		if signum is signal.SIGINT:
			self.shutdown()
			sys.exit(0)

if __name__ == '__main__':
	app = Application()
	app.run()
