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

from data_dump import DATADumpFile
from data_if import DATAInterface
from gsm_shared import *
from data_msg import *

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
	conn_mode = "TRX"

	# Burst source
	capture_file = None

	# Count limitations
	msg_skip = None
	msg_count = None

	# Pass filtering
	pf_fn_lt = None
	pf_fn_gt = None
	pf_tn = None

	def __init__(self):
		self.print_copyright()
		self.parse_argv()

		# Set up signal handlers
		signal.signal(signal.SIGINT, self.sig_handler)

		# Open requested capture file
		self.ddf = DATADumpFile(self.capture_file)

	def run(self):
		# Init DATA interface with TRX or L1
		if self.conn_mode == "TRX":
			self.data_if = DATAInterface(self.remote_addr,
				self.base_port + 2, self.base_port + 102)
			l12trx = True
		elif self.conn_mode == "L1":
			self.data_if = DATAInterface(self.remote_addr,
				self.base_port + 102, self.base_port + 2)
			l12trx = False
		else:
			self.print_help("[!] Unknown connection type")
			sys.exit(2)

		# Read messages from the capture
		messages = self.ddf.parse_all(
			skip = self.msg_skip, count = self.msg_count)
		if messages is False:
			pass # FIXME!!!

		for msg in messages:
			# Pass filter
			if not self.msg_pass_filter(l12trx, msg):
				continue

			print("[i] Sending a burst %s to %s..."
				% (msg.desc_hdr(), self.conn_mode))

			# Send message
			self.data_if.send_msg(msg)

	def msg_pass_filter(self, l12trx, msg):
		# Direction filter
		if isinstance(msg, DATAMSG_L12TRX) and not l12trx:
			return False
		elif isinstance(msg, DATAMSG_TRX2L1) and l12trx:
			return False

		# Timeslot filter
		if self.pf_tn is not None:
			if msg.tn != self.pf_tn:
				return False

		# Frame number filter
		if self.pf_fn_lt is not None:
			if msg.fn > self.pf_fn_lt:
				return False
		if self.pf_fn_gt is not None:
			if msg.fn < self.pf_fn_gt:
				return False

		# Burst passed ;)
		return True

	def print_copyright(self):
		print(COPYRIGHT)

	def print_help(self, msg = None):
		s  = " Usage: " + sys.argv[0] + " [options]\n\n" \
			 " Some help...\n" \
			 "  -h --help              this text\n\n"

		s += " TRX interface specific\n" \
			 "  -m --conn-mode         Send bursts to: TRX (default) / L1\n"    \
			 "  -r --remote-addr       Set remote address (default %s)\n"       \
			 "  -p --base-port         Set base port number (default %d)\n\n"

		s += " Burst source\n" \
			 "  -i --capture-file      Read bursts from capture file\n\n" \

		s += " Count limitations (disabled by default)\n" \
			 "  --msg-skip      NUM    Skip NUM messages before sending\n"   \
			 "  --msg-count     NUM    Stop after sending NUM messages\n\n"  \

		s += " Filtering (disabled by default)\n" \
			 "  --timeslot      NUM    TDMA timeslot number [0..7]\n"        \
			 "  --frame-num-lt  NUM    TDMA frame number lower than NUM\n"   \
			 "  --frame-num-gt  NUM    TDMA frame number greater than NUM\n"

		print(s % (self.remote_addr, self.base_port))

		if msg is not None:
			print(msg)

	def parse_argv(self):
		try:
			opts, args = getopt.getopt(sys.argv[1:],
				"m:r:p:i:h",
				[
					"help",
					"conn-mode=",
					"remote-addr=",
					"base-port=",
					"capture-file=",
					"msg-skip=",
					"msg-count=",
					"timeslot=",
					"frame-num-lt=",
					"frame-num-gt=",
				])
		except getopt.GetoptError as err:
			self.print_help("[!] " + str(err))
			sys.exit(2)

		for o, v in opts:
			if o in ("-h", "--help"):
				self.print_help()
				sys.exit(2)

			# Capture file
			elif o in ("-i", "--capture-file"):
				self.capture_file = v

			# TRX interface specific
			elif o in ("-m", "--conn-mode"):
				self.conn_mode = v
			elif o in ("-r", "--remote-addr"):
				self.remote_addr = v
			elif o in ("-p", "--base-port"):
				self.base_port = int(v)

			# Count limitations
			elif o == "--msg-skip":
				self.msg_skip = int(v)
			elif o == "--msg-count":
				self.msg_count = int(v)

			# Timeslot pass filter
			elif o == "--timeslot":
				self.pf_tn = int(v)
				if self.pf_tn < 0 or self.pf_tn > 7:
					self.print_help("[!] Wrong timeslot value")
					sys.exit(2)

			# Frame number pass filter
			elif o == "--frame-num-lt":
				self.pf_fn_lt = int(v)
			elif o == "--frame-num-gt":
				self.pf_fn_gt = int(v)

		if self.capture_file is None:
			self.print_help("[!] Please specify a capture file")
			sys.exit(2)

	def sig_handler(self, signum, frame):
		print("Signal %d received" % signum)
		if signum is signal.SIGINT:
			sys.exit(0)

if __name__ == '__main__':
	app = Application()
	app.run()
