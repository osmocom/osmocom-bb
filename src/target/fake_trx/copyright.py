#!/usr/bin/env python2
# -*- coding: utf-8 -*-

def print_copyright(holders = []):
	# Print copyright holders if any
	for date, author in holders:
		print("Copyright (C) %s by %s" % (date, author))

	# Print the license header itself
	print("License GPLv2+: GNU GPL version 2 or later " \
		"<http://gnu.org/licenses/gpl.html>\n" \
		"This is free software: you are free to change and redistribute it.\n" \
		"There is NO WARRANTY, to the extent permitted by law.\n")
