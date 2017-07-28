#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# Simple tool to send custom messages via TRX DATA interface,
# which may be also useful for fuzzing and testing
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
import select
import sys

from udp_link import UDPLink
from rand_burst_gen import RandBurstGen

COPYRIGHT = \
	"Copyright (C) 2017 by Vadim Yanitskiy <axilirator@gmail.com>\n" \
	"License GPLv2+: GNU GPL version 2 or later " \
	"<http://gnu.org/licenses/gpl.html>\n" \
	"This is free software: you are free to change and redistribute it.\n" \
	"There is NO WARRANTY, to the extent permitted by law.\n"

class DATAInterface(UDPLink):
	# GSM PHY definitions
	GSM_HYPERFRAME = 2048 * 26 * 51

	def send_l1_msg(self, burst,
		tn = None, fn = None, rssi = None):
		# Generate random timeslot index if not preset
		if tn is None:
			tn = random.randint(0, 7)

		# Generate random frame number if not preset
		if fn is None:
			fn = random.randint(0, self.GSM_HYPERFRAME)

		# Generate random RSSI if not preset
		if rssi is None:
			rssi = -random.randint(-75, -50)

		# Prepare a buffer for header and burst
		buf = []

		# Put timeslot index
		buf.append(tn)

		# Put frame number
		buf.append((fn >> 24) & 0xff)
		buf.append((fn >> 16) & 0xff)
		buf.append((fn >>  8) & 0xff)
		buf.append((fn >>  0) & 0xff)

		# Put RSSI
		buf.append(rssi)

		# HACK: put fake TOA value
		buf += [0x00] * 2

		# Put burst
		buf += burst

		# Put two unused bytes
		buf += [0x00] * 2

		# Send message
		self.send(bytearray(buf))

	def send_trx_msg(self, burst,
		tn = None, fn = None, pwr = None):
		# Generate random timeslot index if not preset
		if tn is None:
			tn = random.randint(0, 7)

		# Generate random frame number if not preset
		if fn is None:
			fn = random.randint(0, self.GSM_HYPERFRAME)

		# Generate random power level if not preset
		if pwr is None:
			pwr = random.randint(0, 34)

		# Prepare a buffer for header and burst
		buf = []

		# Put timeslot index
		buf.append(tn)

		# Put frame number
		buf.append((fn >> 24) & 0xff)
		buf.append((fn >> 16) & 0xff)
		buf.append((fn >>  8) & 0xff)
		buf.append((fn >>  0) & 0xff)

		# Put transmit power level
		buf.append(pwr)

		# Put burst
		buf += burst

		# Send message
		self.send(bytearray(buf))

class Application:
	# Application variables
	remote_addr = "127.0.0.1"
	base_port = 5700
	conn_mode = "TRX"

	burst_type = None
	burst_count = 1

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

		# Init random burst generator
		self.gen = RandBurstGen()

		# Send as much bursts as required
		for i in range(self.burst_count):
			# Generate a random burst
			if self.burst_type == "NB":
				buf = self.gen.gen_nb()
			elif self.burst_type == "FB":
				buf = self.gen.gen_fb()
			elif self.burst_type == "SB":
				buf = self.gen.gen_sb()
			elif self.burst_type == "AB":
				buf = self.gen.gen_ab()
			else:
				self.print_help("[!] Unknown burst type")
				self.shutdown()
				sys.exit(2)

			print("[i] Sending %d/%d %s burst to %s..."
				% (i + 1, self.burst_count, self.burst_type,
					self.conn_mode))

			# Send to TRX or L1
			if self.conn_mode == "TRX":
				self.data_if.send_trx_msg(buf)
			elif self.conn_mode == "L1":
				self.data_if.send_l1_msg(buf)

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
			 "  -b --burst-type     Random burst type (NB, FB, SB, AB)\n"    \
			 "  -c --burst-count    How much bursts to send (default 1)\n"   \

		print(s % (self.remote_addr, self.base_port))

		if msg is not None:
			print(msg)

	def parse_argv(self):
		try:
			opts, args = getopt.getopt(sys.argv[1:],
				"m:r:p:b:c:h",
				[
					"help",
					"conn-mode=",
					"remote-addr=",
					"base-port=",
					"burst-type=",
					"burst-count=",
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

			elif o in ("-b", "--burst-type"):
				self.burst_type = v
			elif o in ("-c", "--burst-count"):
				self.burst_count = int(v)

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