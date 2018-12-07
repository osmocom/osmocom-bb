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

import logging as log
import signal
import argparse
import select
import sys

from app_common import ApplicationBase
from ctrl_if_bts import CTRLInterfaceBTS
from ctrl_if_bb import CTRLInterfaceBB
from burst_fwd import BurstForwarder
from fake_pm import FakePM

from udp_link import UDPLink
from clck_gen import CLCKGen

class Application(ApplicationBase):
	def __init__(self):
		print_copyright(CR_HOLDERS)
		self.argv = self.parse_argv()

		# Set up signal handlers
		signal.signal(signal.SIGINT, self.sig_handler)

		# Configure logging
		self.app_init_logging(self.argv)

	def run(self):
		# Init TRX CTRL interface for BTS
		self.bts_ctrl = CTRLInterfaceBTS(
			self.argv.bts_addr, self.argv.bts_base_port + 101,
			self.argv.trx_bind_addr, self.argv.bts_base_port + 1)

		# Init TRX CTRL interface for BB
		self.bb_ctrl = CTRLInterfaceBB(
			self.argv.bb_addr, self.argv.bb_base_port + 101,
			self.argv.trx_bind_addr, self.argv.bb_base_port + 1)

		# Power measurement emulation
		# Noise: -120 .. -105
		# BTS: -75 .. -50
		self.pm = FakePM(-120, -105, -75, -50)

		# Share a FakePM instance between both BTS and BB
		self.bts_ctrl.pm = self.pm
		self.bb_ctrl.pm = self.pm

		# Init DATA links
		self.bts_data = UDPLink(
			self.argv.bts_addr, self.argv.bts_base_port + 102,
			self.argv.trx_bind_addr, self.argv.bts_base_port + 2)
		self.bb_data = UDPLink(
			self.argv.bb_addr, self.argv.bb_base_port + 102,
			self.argv.trx_bind_addr, self.argv.bb_base_port + 2)

		# BTS <-> BB burst forwarding
		self.burst_fwd = BurstForwarder(self.bts_data, self.bb_data)

		# Share a BurstForwarder instance between BTS and BB
		self.bts_ctrl.burst_fwd = self.burst_fwd
		self.bb_ctrl.burst_fwd = self.burst_fwd

		# Provide clock to BTS
		self.bts_clck = UDPLink(
			self.argv.bts_addr, self.argv.bts_base_port + 100,
			self.argv.trx_bind_addr, self.argv.bts_base_port)
		self.clck_gen = CLCKGen([self.bts_clck])
		self.bts_ctrl.clck_gen = self.clck_gen

		log.info("Init complete")

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
		log.info("Shutting down...")

		# Stop clock generator
		self.clck_gen.stop()

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

		argv = parser.parse_args()

		# Make sure there is no overlap between ports
		if argv.bts_base_port == argv.bb_base_port:
			parser.error("BTS and BB base ports shall be different")

		return argv

	def sig_handler(self, signum, frame):
		log.info("Signal %d received" % signum)
		if signum is signal.SIGINT:
			self.shutdown()
			sys.exit(0)

if __name__ == '__main__':
	app = Application()
	app.run()
