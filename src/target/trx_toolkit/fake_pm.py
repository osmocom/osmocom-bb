#!/usr/bin/env python
# -*- coding: utf-8 -*-

# TRX Toolkit
# Power measurement emulation
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

from random import randint

class FakePM:
	""" Power measurement emulation for fake transceivers.

	There is no such thing like RF signal level in fake Um-interface,
	so we need to emulate this. The main idea is to have a list of
	all running and idle transceivers. As soon as a measurement
	request is received, FakePM will attempt to find a running
	transceiver on a given frequency.

	The result of such "measurement" is a random RSSI value
	in one of the following ranges:

	  - trx_min ... trx_max - if at least one TRX was found,
	  - noise_min ... noise_max - no TRX instances were found.

	FIXME: it would be great to average the rate of bursts
	       and indicated power / attenuation values for all
	       matching transceivers, so "pure traffic" ARFCNs
	       would be handled properly.

	"""

	def __init__(self, noise_min, noise_max, trx_min, trx_max):
		# Init list of transceivers
		self.trx_list = []

		# RSSI randomization ranges
		self.noise_min = noise_min
		self.noise_max = noise_max
		self.trx_min = trx_min
		self.trx_max = trx_max

	@property
	def rssi_noise(self):
		return randint(self.noise_min, self.noise_max)

	@property
	def rssi_trx(self):
		return randint(self.trx_min, self.trx_max)

	def measure(self, freq):
		# Iterate over all known transceivers
		for trx in self.trx_list:
			if not trx.running:
				continue

			# Match by given frequency
			if trx.tx_freq == freq:
				return self.rssi_trx

		return self.rssi_noise
