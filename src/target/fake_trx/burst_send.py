#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# Simple tool to send bursts via TRX DATA interface
#
# (C) 2017 by Vadim Yanitskiy <axilirator@gmail.com>
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

import random
import signal
import getopt
import sys

from data_if import DATAInterface

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
	pwr = None
	fn = None
	tn = None

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
			src = open(self.burst_src, "r")
		else:
			src = sys.stdin

		# Generate a random frame number or use provided one
		if self.fn is None:
			fn = random.randint(0, DATAInterface.GSM_HYPERFRAME)
		else:
			fn = self.fn

		# Read the burst source line-by-line
		for line in src:
			# Strip spaces
			burst = line.strip()
			buf = []

			# Check length
			if len(burst) != 148:
				print("[!] Dropping burst due to length != 148")
				continue

			print("[i] Sending a burst (fn=%u) to %s..."
				% (fn, self.conn_mode))

			# Parse a string
			for bit in burst:
				if bit == "1":
					buf.append(1)
				else:
					buf.append(0)

			# Send to TRX or L1
			if self.conn_mode == "TRX":
				self.data_if.send_trx_msg(buf,
					self.tn, fn, self.pwr)
			elif self.conn_mode == "L1":
				self.data_if.send_l1_msg(buf,
					self.tn, fn, self.pwr)

			# Increase frame number (for count > 1)
			fn = (fn + 1) % DATAInterface.GSM_HYPERFRAME

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
			 "  -l --power-level    Set transmit level (default random)\n"   \

		print(s % (self.remote_addr, self.base_port))

		if msg is not None:
			print(msg)

	def parse_argv(self):
		try:
			opts, args = getopt.getopt(sys.argv[1:],
				"m:r:p:i:f:t:l:h",
				[
					"help",
					"conn-mode=",
					"remote-addr=",
					"base-port=",
					"burst-file=",
					"frame-number=",
					"timeslot=",
					"power-level=",
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
			elif o in ("-l", "--power-level"):
				self.pwr = abs(int(v))

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
