#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# TRX Toolkit
# Burst forwarding between transceivers
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

import logging as log

class BurstForwarder:
	""" Performs burst forwarding between transceivers.

	BurstForwarder distributes bursts between the list of given
	FakeTRX (Transceiver) instances depending on the following
	parameters of each transceiver:

	  - execution state (running or idle),
	  - actual RX / TX frequencies,
	  - list of active timeslots.

	Each to be distributed L12TRX message is being transformed
	into a TRX2L1 message, and then forwarded to transceivers
	with partially initialized header. All uninitialized header
	fields (such as rssi and toa256) shall be set by each
	transceiver individually before sending towards the L1.

	"""

	def __init__(self, trx_list = None):
		# List of Transceiver instances
		if trx_list is not None:
			self.trx_list = trx_list
		else:
			self.trx_list = []

	def add_trx(self, trx):
		if trx in self.trx_list:
			log.error("TRX is already in the list")
			return

		self.trx_list.append(trx)

	def del_trx(self, trx):
		if trx not in self.trx_list:
			log.error("TRX is not in the list")
			return

		self.trx_list.remove(trx)

	def forward_msg(self, src_trx, rx_msg):
		# Transform from L12TRX to TRX2L1
		tx_msg = rx_msg.gen_trx2l1()
		if tx_msg is None:
			log.error("Forwarding failed, could not transform "
				"message (%s) => dropping..." % rx_msg.desc_hdr())

		# Iterate over all known transceivers
		for trx in self.trx_list:
			if trx == src_trx:
				continue

			# Check transceiver state
			if not trx.running:
				continue
			if trx.rx_freq != src_trx.tx_freq:
				continue
			if tx_msg.tn not in trx.ts_list:
				continue

			trx.send_data_msg(src_trx, tx_msg)
