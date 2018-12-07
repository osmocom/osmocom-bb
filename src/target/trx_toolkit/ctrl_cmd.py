#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# TRX Toolkit
# Auxiliary tool to send custom commands via TRX CTRL interface,
# which may be useful for testing and fuzzing
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
from udp_link import UDPLink

class Application(ApplicationBase):
	def __init__(self):
		print_copyright(CR_HOLDERS)
		self.argv = self.parse_argv()

		# Set up signal handlers
		signal.signal(signal.SIGINT, self.sig_handler)

		# Configure logging
		self.app_init_logging(self.argv)

		# Init UDP connection
		self.ctrl_link = UDPLink(
			self.argv.remote_addr, self.argv.base_port + 1,
			self.argv.bind_addr, self.argv.bind_port)

		# Debug print
		log.info("Init CTRL interface (%s)" \
			% self.ctrl_link.desc_link())

	def parse_argv(self):
		parser = argparse.ArgumentParser(prog = "ctrl_cmd",
			description = "Auxiliary tool to send control commands")

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
		trx_group.add_argument("-P", "--bind-port",
			dest = "bind_port", type = int, default = 0,
			help = "Set bind port number (default random)")
		trx_group.add_argument("-f", "--fuzzing",
			dest = "fuzzing", action = "store_true",
			help = "Send raw payloads (without CMD)")

		return parser.parse_args()

	def run(self):
		while True:
			self.print_prompt()

			# Wait until we get any data on any socket
			socks = [sys.stdin, self.ctrl_link.sock]
			r_event, w_event, x_event = select.select(socks, [], [])

			# Check for incoming CTRL commands
			if sys.stdin in r_event:
				cmd = sys.stdin.readline()
				self.handle_cmd(cmd)

			if self.ctrl_link.sock in r_event:
				data, addr = self.ctrl_link.sock.recvfrom(128)
				sys.stdout.write("\r%s\n" % data.decode())
				sys.stdout.flush()

	def handle_cmd(self, cmd):
		# Strip spaces, tabs, etc.
		cmd = cmd.strip().strip("\0")

		# Send a command
		if self.argv.fuzzing:
			self.ctrl_link.send("%s" % cmd)
		else:
			self.ctrl_link.send("CMD %s\0" % cmd)

	def print_prompt(self):
		sys.stdout.write("CTRL# ")
		sys.stdout.flush()

	def sig_handler(self, signum, frame):
		log.info("Signal %d received" % signum)
		if signum is signal.SIGINT:
			sys.exit(0)

if __name__ == '__main__':
	app = Application()
	app.run()
