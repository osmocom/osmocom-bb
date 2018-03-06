#!/usr/bin/env python2
# -*- coding: utf-8 -*-

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

import signal
import getopt
import select
import sys

from udp_link import UDPLink

COPYRIGHT = \
	"Copyright (C) 2017-2018 by Vadim Yanitskiy <axilirator@gmail.com>\n" \
	"License GPLv2+: GNU GPL version 2 or later " \
	"<http://gnu.org/licenses/gpl.html>\n" \
	"This is free software: you are free to change and redistribute it.\n" \
	"There is NO WARRANTY, to the extent permitted by law.\n"

class Application:
	# Application variables
	remote_addr = "127.0.0.1"
	base_port = 5700
	bind_port = 0
	fuzzing = False

	def __init__(self):
		print(COPYRIGHT)
		self.parse_argv()

		# Set up signal handlers
		signal.signal(signal.SIGINT, self.sig_handler)

		# Init UDP connection
		self.ctrl_link = UDPLink(self.remote_addr,
			self.base_port + 1, self.bind_port)

		# Debug print
		print("[i] Init CTRL interface (%s)" \
			% self.ctrl_link.desc_link())

	def print_help(self, msg = None):
		s  = " Usage: " + sys.argv[0] + " [options]\n\n" \
			 " Some help...\n" \
			 "  -h --help           this text\n\n"

		s += " TRX interface specific\n" \
			 "  -r --remote-addr    Set remote address (default %s)\n"   \
			 "  -p --base-port      Set base port number (default %d)\n" \
			 "  -P --bind-port      Set local port number (default: random)\n" \
			 "  -f --fuzzing        Send raw payloads (without CMD)\n"   \

		print(s % (self.remote_addr, self.base_port))

		if msg is not None:
			print(msg)

	def parse_argv(self):
		try:
			opts, args = getopt.getopt(sys.argv[1:],
				"r:p:P:fh",
				[
					"help",
					"fuzzing",
					"base-port=",
					"bind-port=",
					"remote-addr=",
				])
		except getopt.GetoptError as err:
			self.print_help("[!] " + str(err))
			sys.exit(2)

		for o, v in opts:
			if o in ("-h", "--help"):
				self.print_help()
				sys.exit(2)

			elif o in ("-r", "--remote-addr"):
				self.remote_addr = v
			elif o in ("-p", "--base-port"):
				self.base_port = int(v)
			elif o in ("-P", "--bind-port"):
				self.bind_port = int(v)
			elif o in ("-f", "--fuzzing"):
				self.fuzzing = True

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
			sys.exit(0)

if __name__ == '__main__':
	app = Application()
	app.run()
