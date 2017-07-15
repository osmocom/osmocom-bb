#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# Simple tool to send custom commands via TRX CTRL interface,
# which may be also useful for fuzzing
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

import signal
import select
import sys

from udp_link import UDPLink

class Application:
	def __init__(self, remote_addr, remote_port, bind_port, fuz = False):
		# Init UDP connection
		self.ctrl_link = UDPLink(remote_addr, remote_port, bind_port)

		# Determine working mode
		self.fuzzing = fuz

		# Set up signal handlers
		signal.signal(signal.SIGINT, self.sig_handler)

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
		if self.fuzzing:
			self.ctrl_link.send("%s" % cmd)
		else:
			self.ctrl_link.send("CMD %s\0" % cmd)

	def print_prompt(self):
		sys.stdout.write("CTRL# ")
		sys.stdout.flush()

	def sig_handler(self, signum, frame):
		print("\n\nSignal %d received" % signum)
		if signum is signal.SIGINT:
			self.ctrl_link.shutdown()
			sys.exit(0)

if __name__ == '__main__':
	app = Application("127.0.0.1", 5701, 5801)
	app.run()
