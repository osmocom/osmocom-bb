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

from copyright import print_copyright
CR_HOLDERS = [("2017-2018", "Vadim Yanitskiy <axilirator@gmail.com>")]

import signal
import getopt
import select
import sys

from ctrl_if_bts import CTRLInterfaceBTS
from ctrl_if_bb import CTRLInterfaceBB
from burst_fwd import BurstForwarder
from fake_pm import FakePM

from udp_link import UDPLink
from clck_gen import CLCKGen

class Application:
	# Application variables
	bts_addr = "127.0.0.1"
	bb_addr = "127.0.0.1"
	trx_bind_addr = "0.0.0.0"
	bts_base_port = 5700
	bb_base_port = 6700

	def __init__(self):
		print_copyright(CR_HOLDERS)
		self.parse_argv()

		# Set up signal handlers
		signal.signal(signal.SIGINT, self.sig_handler)

	def run(self):
		# Init TRX CTRL interface for BTS
		self.bts_ctrl = CTRLInterfaceBTS(self.bts_addr, self.bts_base_port + 101,
			self.trx_bind_addr, self.bts_base_port + 1)

		# Init TRX CTRL interface for BB
		self.bb_ctrl = CTRLInterfaceBB(self.bb_addr, self.bb_base_port + 101,
			self.trx_bind_addr, self.bb_base_port + 1)

		# Power measurement emulation
		# Noise: -120 .. -105
		# BTS: -75 .. -50
		self.pm = FakePM(-120, -105, -75, -50)

		# Share a FakePM instance between both BTS and BB
		self.bts_ctrl.pm = self.pm
		self.bb_ctrl.pm = self.pm

		# Init DATA links
		self.bts_data = UDPLink(self.bts_addr, self.bts_base_port + 102,
			self.trx_bind_addr, self.bts_base_port + 2)
		self.bb_data = UDPLink(self.bb_addr, self.bb_base_port + 102,
			self.trx_bind_addr, self.bb_base_port + 2)

		# BTS <-> BB burst forwarding
		self.burst_fwd = BurstForwarder(self.bts_data, self.bb_data)

		# Share a BurstForwarder instance between BTS and BB
		self.bts_ctrl.burst_fwd = self.burst_fwd
		self.bb_ctrl.burst_fwd = self.burst_fwd

		# Provide clock to BTS
		self.bts_clck = UDPLink(self.bts_addr, self.bts_base_port + 100,
			self.trx_bind_addr, self.bts_base_port)
		self.clck_gen = CLCKGen([self.bts_clck])
		self.bts_ctrl.clck_gen = self.clck_gen

		print("[i] Init complete")

		# Enter main loop
		while True:
			socks = [self.bts_ctrl.sock, self.bb_ctrl.sock,
				self.bts_data.sock, self.bb_data.sock]

			# Wait until we get any data on any socket
			r_event, w_event, x_event = select.select(socks, [], [])

			# Downlink: BTS -> BB
			if self.bts_data.sock in r_event:
				self.burst_fwd.bts2bb()

			# Uplink: BB -> BTS
			if self.bb_data.sock in r_event:
				self.burst_fwd.bb2bts()

			# CTRL commands from BTS
			if self.bts_ctrl.sock in r_event:
				data, addr = self.bts_ctrl.sock.recvfrom(128)
				self.bts_ctrl.handle_rx(data.decode(), addr)

			# CTRL commands from BB
			if self.bb_ctrl.sock in r_event:
				data, addr = self.bb_ctrl.sock.recvfrom(128)
				self.bb_ctrl.handle_rx(data.decode(), addr)

	def shutdown(self):
		print("[i] Shutting down...")

		# Stop clock generator
		self.clck_gen.stop()

	def print_help(self, msg = None):
		s  = " Usage: " + sys.argv[0] + " [options]\n\n" \
			 " Some help...\n" \
			 "  -h --help           this text\n\n"

		s += " TRX interface specific\n" \
			 "  -R --bts-addr       Set BTS remote address (default %s)\n"   \
			 "  -r --bb-addr        Set BB remote address (default %s)\n"    \
			 "  -P --bts-base-port  Set BTS base port number (default %d)\n" \
			 "  -p --bb-base-port   Set BB base port number (default %d)\n" \
			 "  -b --trx-bind-addr  Set TRX bind address (default %s)\n"

		print(s % (self.bts_addr, self.bb_addr,
			self.bts_base_port, self.bb_base_port,
			self.trx_bind_addr))

		if msg is not None:
			print(msg)

	def parse_argv(self):
		try:
			opts, args = getopt.getopt(sys.argv[1:],
				"R:r:P:p:b:h",
				[
					"help",
					"bts-addr=", "bb-addr=",
					"bts-base-port=", "bb-base-port=",
					"trx-bind-addr=",
				])
		except getopt.GetoptError as err:
			self.print_help("[!] " + str(err))
			sys.exit(2)

		for o, v in opts:
			if o in ("-h", "--help"):
				self.print_help()
				sys.exit(2)

			elif o in ("-R", "--bts-addr"):
				self.bts_addr = v
			elif o in ("-r", "--bb-addr"):
				self.bb_addr = v

			elif o in ("-P", "--bts-base-port"):
				self.bts_base_port = int(v)
			elif o in ("-p", "--bb-base-port"):
				self.bb_base_port = int(v)

			elif o in ("-b", "--trx-bind-addr"):
				self.trx_bind_addr = v

		# Ensure there is no overlap between ports
		if self.bts_base_port == self.bb_base_port:
			self.print_help("[!] BTS and BB base ports should be different")
			sys.exit(2)

		bts_ports = [
			self.bts_base_port + 0, self.bts_base_port + 100,
			self.bts_base_port + 1, self.bts_base_port + 101,
			self.bts_base_port + 2, self.bts_base_port + 102,
		]

		bb_ports = [
			self.bb_base_port + 0, self.bb_base_port + 100,
			self.bb_base_port + 1, self.bb_base_port + 101,
			self.bb_base_port + 2, self.bb_base_port + 102,
		]

		for p in bb_ports:
			if p in bts_ports:
				self.print_help("[!] BTS and BB ports overlap detected")
				sys.exit(2)

	def sig_handler(self, signum, frame):
		print("Signal %d received" % signum)
		if signum is signal.SIGINT:
			self.shutdown()
			sys.exit(0)

if __name__ == '__main__':
	app = Application()
	app.run()
