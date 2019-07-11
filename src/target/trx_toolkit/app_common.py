#!/usr/bin/env python
# -*- coding: utf-8 -*-

# TRX Toolkit
# Common helpers for applications
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

import logging as log

class ApplicationBase:
	# Osmocom-style logging message format
	# Example: [DEBUG] ctrl_if_bts.py:71 Recv POWEROFF cmd
	LOG_FMT_DEFAULT = "[%(levelname)s] %(filename)s:%(lineno)d %(message)s"

	def app_print_copyright(self, holders = []):
		# Print copyright holders if any
		for date, author in holders:
			print("Copyright (C) %s by %s" % (date, author))

		# Print the license header itself
		print("License GPLv2+: GNU GPL version 2 or later " \
			"<http://gnu.org/licenses/gpl.html>\n" \
			"This is free software: you are free to change and redistribute it.\n" \
			"There is NO WARRANTY, to the extent permitted by law.\n")

	def app_init_logging(self, argv):
		# Default logging handler (stderr)
		sh = log.StreamHandler()
		sh.setLevel(log.getLevelName(argv.log_level))
		sh.setFormatter(log.Formatter(argv.log_fmt))
		log.root.addHandler(sh)

		# Optional file handler
		if argv.log_file_name is not None:
			fh = log.FileHandler(argv.log_file_name)
			fh.setLevel(log.getLevelName(argv.log_file_level))
			fh.setFormatter(log.Formatter(argv.log_file_fmt))
			log.root.addHandler(fh)

		# Set DEBUG for the root logger
		log.root.setLevel(log.DEBUG)

	def app_reg_logging_options(self, parser):
		parser.add_argument("--log-level", metavar = "LVL",
			dest = "log_level", type = str, default = "DEBUG",
			choices = ["DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"],
			help = "Set logging level (default %(default)s)")
		parser.add_argument("--log-format", metavar = "FMT",
			dest = "log_fmt", type = str, default = self.LOG_FMT_DEFAULT,
			help = "Set logging message format")

		parser.add_argument("--log-file-name", metavar = "FILE",
			dest = "log_file_name", type = str,
			help = "Set logging file name")
		parser.add_argument("--log-file-level", metavar = "LVL",
			dest = "log_file_level", type = str, default = "DEBUG",
			choices = ["DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"],
			help = "Set logging level for file (default %(default)s)")
		parser.add_argument("--log-file-format", metavar = "FMT",
			dest = "log_file_fmt", type = str, default = self.LOG_FMT_DEFAULT,
			help = "Set logging message format for file")
