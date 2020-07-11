#!/usr/bin/env python
# -*- coding: utf-8 -*-

# TRX Toolkit
# Burst forwarding between transceivers
#
# (C) 2017-2020 by Vadim Yanitskiy <axilirator@gmail.com>
# Contributions by sysmocom - s.f.m.c. GmbH
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

from trx_list import TRXList

class BurstForwarder(TRXList):
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

	def forward_msg(self, src_trx, rx_msg):
		# Originating Transceiver may use frequency hopping,
		# so let's precalculate its Tx frequency in advance
		tx_freq = src_trx.get_tx_freq(rx_msg.fn)

		# Iterate over all known transceivers
		for trx in self.trx_list:
			if trx == src_trx:
				continue

			# Check transceiver state
			if not trx.running:
				continue
			if rx_msg.tn not in trx.ts_list:
				continue

			# Match Tx/Rx frequencies of the both transceivers
			if trx.get_rx_freq(rx_msg.fn) != tx_freq:
				continue

			# Transform from L12TRX to TRX2L1 and forward
			tx_msg = rx_msg.gen_trx2l1(ver = trx.data_if._hdr_ver)
			trx.handle_data_msg(src_trx, rx_msg, tx_msg)
