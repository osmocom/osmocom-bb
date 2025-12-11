# -*- coding: utf-8 -*-

# TRX Toolkit
# Common helpers for applications
#
# (C) 2018-2020 by Vadim Yanitskiy <axilirator@gmail.com>
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

import logging as log

class ApplicationBase:
	# Osmocom-style logging message format
	# Example: [DEBUG] ctrl_if_bts.py:71 Recv POWEROFF cmd
	LOG_FMT_DEFAULT = "[%(levelname)s] %(filename)s:%(lineno)d %(message)s"

	# Default time / date format (e.g. 2003-01-23 00:29:50)
	LOG_TIME_FMT_DEFAULT = "%Y-%m-%d %H:%M:%S"

	def app_print_copyright(self, holders = []):
		# Print copyright holders if any
		for date, author in holders:
			print("Copyright (C) %s by %s" % (date, author))

		# Print the license header itself
		print("License GPLv2+: GNU GPL version 2 or later " \
			"<http://gnu.org/licenses/gpl.html>\n" \
			"This is free software: you are free to change and redistribute it.\n" \
			"There is NO WARRANTY, to the extent permitted by law.\n")

	def add_log_handler(self, lh, log_level, log_fmt, time_fmt, log_time = False):
		log_fmt = "%(asctime)s.%(msecs)03d " + log_fmt if log_time else log_fmt
		lf = log.Formatter(log_fmt, time_fmt)
		ll = log.getLevelName(log_level)

		log.root.addHandler(lh)
		lh.setFormatter(lf)
		lh.setLevel(ll)

	def app_init_logging(self, argv):
		# Default logging handler (stderr)
		lo = (argv.log_level, argv.log_fmt, argv.log_time_fmt, argv.log_time)
		lh = log.StreamHandler()
		self.add_log_handler(lh, *lo)

		# Optional file handler
		if argv.log_file_name is not None:
			lo = (argv.log_file_level, argv.log_file_fmt,
			      argv.log_file_time_fmt, argv.log_file_time)
			lh = log.FileHandler(argv.log_file_name)
			self.add_log_handler(lh, *lo)

		# Set DEBUG for the root logger
		log.root.setLevel(log.DEBUG)

	def app_reg_logging_options(self, parser):
		parser.add_argument("--log-level", metavar = "LVL",
			dest = "log_level", type = str, default = "DEBUG",
			choices = ["DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"],
			help = "Set logging level (default %(default)s)")
		parser.add_argument("--log-time",
			dest = "log_time", action = "store_true",
			help = "Prefix each log message with the current time")
		parser.add_argument("--log-time-format", metavar = "FMT",
			dest = "log_time_fmt", type = str,
			default = self.LOG_TIME_FMT_DEFAULT,
			help = "Set time format (default %(default)s)")
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
		parser.add_argument("--log-file-time",
			dest = "log_file_time", action = "store_true",
			help = "Prefix each log message with the current time")
		parser.add_argument("--log-file-time-format", metavar = "FMT",
			dest = "log_file_time_fmt", type = str,
			default = self.LOG_TIME_FMT_DEFAULT,
			help = "Set time format for file (default %(default)s)")
		parser.add_argument("--log-file-format", metavar = "FMT",
			dest = "log_file_fmt", type = str, default = self.LOG_FMT_DEFAULT,
			help = "Set logging message format for file")
