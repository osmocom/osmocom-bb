# -*- coding: utf-8 -*-

# TRX Toolkit
# Transceiver implementation
#
# (C) 2018-2020 by Vadim Yanitskiy <axilirator@gmail.com>
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

from ctrl_if_trx import CTRLInterfaceTRX
from data_if import DATAInterface
from udp_link import UDPLink
from trx_list import TRXList

from gsm_shared import HoppingParams

class Transceiver:
	""" Base transceiver implementation.

	Represents a single transceiver, that can be used as for the BTS side,
	as for the MS side. Each individual instance of Transceiver unifies
	three basic interfaces built on three independent UDP connections:

	  - CLCK (base port + 100/0) - clock indications from TRX to L1,
	  - CTRL (base port + 101/1) - control interface for L1,
	  - DATA (base port + 102/2) - bidirectional data interface for bursts.

	A transceiver can be either in active (i.e. working), or in idle mode.
	The active mode should ensure that both RX/TX frequencies are set.

	NOTE: CLCK is not required for some L1 implementations, so it is optional.

	== Timeslot configuration

	Transceiver has a list of active (i.e. configured) TDMA timeslots.
	The L1 should configure a timeslot before sending or expecting any
	data on it. This is done by SETSLOT control command, which also
	indicates an associated channel combination (see GSM TS 05.02).

	NOTE: we don't store the associated channel combinations,
	      as they are only useful for burst detection and demodulation.

	== Child transceivers

	A BTS can (optionally) have more than one transceiver. In this case
	additional (let's say child) transceivers basically share the same
	clock source of the first transceiver, so UDP port mapping is a bit
	different, for example:

	  (trx_0) clck=5700, ctrl=5701, data=5702,
	  (trx_1)            ctrl=5703, data=5704,
	  (trx_2)            ctrl=5705, data=5706.
	  ...

	As soon as the first transceiver is powered on / off,
	all child transceivers are also powered on / off.

	== Clock distribution (optional)

	The clock indications are not expected by L1 when transceiver
	is not running, so we monitor both POWERON / POWEROFF events
	from the control interface, and keep the list of CLCK links
	in a given CLCKGen instance updated. The clock generator is
	started and stopped automatically.

	NOTE: a single instance of CLCKGen can be shared between multiple
	      transceivers, as well as multiple transceivers may use
	      individual CLCKGen instances.

	== Power Measurement (optional)

	Transceiver may have an optional power measurement interface,
	that shall provide at least one method: measure(freq). This
	is required for the MS side (i.e. OsmocomBB).

	== Frequency hopping (optional)

	There are two ways to implement frequency hopping:

	  a) The Transceiver is configured with the hopping parameters, in
	     particular HSN, MAIO, and the list of ARFCNs (channels), so the
	     actual Rx/Tx frequencies are changed by the Transceiver itself
	     depending on the current TDMA frame number.

	  b) The L1 maintains several Transceivers (two or more), so each
	     instance is assigned one dedicated RF carrier frequency, and
	     hence the number of available hopping frequencies is equal to
	     the number of Transceivers. In this case, it's the task of
	     the L1 to commutate bursts between Transceivers (frequencies).

	Variant a) is commonly known as "synthesizer frequency hopping"
	whereas b) is known as "baseband frequency hopping".

	For the MS side, a) is preferred, because a phone usually has only
	one Transceiver (per RAT). On the other hand, b) is more suitable
	for the BTS side, because it's relatively easy to implement and
	there is no technical limitation on the amount of Transceivers.

	FakeTRX obviously does support b) since multi-TRX feature has been
	implemented, as well as a) by resolving UL/DL frequencies using a
	preconfigured (by the L1) set of the hopping parameters. The later
	can be enabled using the SETFH control command.

	NOTE: in the current implementation, mode a) applies to the whole
	Transceiver and all its timeslots, so using in for the BTS side
	does not make any sense (imagine BCCH hopping together with DCCH).

	"""

	def __init__(self, bind_addr, remote_addr, base_port, name = None,
			child_idx = 0, clck_gen = None, pwr_meas = None):
		# Connection info
		self.remote_addr = remote_addr
		self.bind_addr = bind_addr
		self.base_port = base_port
		self.child_idx = child_idx

		# Meta info
		self.name = name

		log.info("Init transceiver '%s'" % self)

		# Child transceiver cannot have its own clock
		if clck_gen is not None and child_idx > 0:
			raise TypeError("Child transceiver cannot have its own clock")

		# Init DATA interface
		self.data_if = DATAInterface(
			remote_addr, base_port + child_idx * 2 + 102,
			bind_addr, base_port + child_idx * 2 + 2)

		# Init CTRL interface
		self.ctrl_if = CTRLInterfaceTRX(self,
			remote_addr, base_port + child_idx * 2 + 101,
			bind_addr, base_port + child_idx * 2 + 1)

		# Init optional CLCK interface
		self.clck_gen = clck_gen
		if clck_gen is not None:
			self.clck_if = UDPLink(
				remote_addr, base_port + 100,
				bind_addr, base_port)

		# Optional Power Measurement interface
		self.pwr_meas = pwr_meas

		# Internal state
		self.running = False

		# Actual RX / TX frequencies
		self._rx_freq = None
		self._tx_freq = None

		# Frequency hopping parameters (set by CTRL)
		self.fh = None

		# List of active (configured) timeslots
		self.ts_list = []

		# List of child transceivers
		self.child_trx_list = TRXList()

	def __str__(self):
		desc = "%s:%d" % (self.remote_addr, self.base_port)
		if self.child_idx > 0:
			desc += "/%d" % self.child_idx
		if self.name is not None:
			desc = "%s@%s" % (self.name, desc)

		return desc

	@property
	def ready(self):
		# Make sure that either both Rx/Tx frequencies are set
		if self._rx_freq is None or self._tx_freq is None:
			# ... or frequency hopping is in use
			if self.fh is None:
				return False

		return True

	def get_rx_freq(self, fn):
		if self.fh is None:
			return self._rx_freq

		# Frequency hopping in use, resolve by TDMA fn
		(rx_freq, _) = self.fh.resolve(fn)
		return rx_freq

	def get_tx_freq(self, fn):
		if self.fh is None:
			return self._tx_freq

		# Frequency hopping in use, resolve by TDMA fn
		(_, tx_freq) = self.fh.resolve(fn)
		return tx_freq

	def enable_fh(self, *args):
		self.fh = HoppingParams(*args)
		log.info("(%s) Frequency hopping configured: %s" % (self, self.fh))

	def disable_fh(self):
		if self.fh is not None:
			log.info("(%s) Frequency hopping disabled" % self)
			self.fh = None

	# To be overwritten if required,
	# no custom command handlers by default
	def ctrl_cmd_handler(self, request):
		return None

	def power_event_handler(self, event):
		# Update child transceivers
		for trx in self.child_trx_list.trx_list:
			if event == "POWERON":
				trx.running = True
			elif event == "POWEROFF":
				trx.running = False
				trx.disable_fh()

		# Reset frequency hopping parameters
		if event == "POWEROFF":
			self.disable_fh()

		# Trigger clock generator if required
		if self.clck_gen is not None:
			clck_links = self.clck_gen.clck_links
			if not self.running and (self.clck_if in clck_links):
				# Transceiver was stopped
				clck_links.remove(self.clck_if)
			elif self.running and (self.clck_if not in clck_links):
				# Transceiver was started
				clck_links.append(self.clck_if)

			if not self.clck_gen.running and len(clck_links) > 0:
				log.info("Starting clock generator")
				self.clck_gen.start()
			elif self.clck_gen.running and not clck_links:
				log.info("Stopping clock generator")
				self.clck_gen.stop()

	def recv_data_msg(self):
		# Read and parse data from socket
		msg = self.data_if.recv_l12trx_msg()
		if not msg:
			return None

		# Make sure that transceiver is configured and running
		if not self.running:
			log.warning("(%s) RX TRXD message (%s), but transceiver "
				"is not running => dropping..." % (self, msg.desc_hdr()))
			return None

		# Make sure that indicated timeslot is configured
		if msg.tn not in self.ts_list:
			log.warning("(%s) RX TRXD message (%s), but timeslot is not "
				"configured => dropping..." % (self, msg.desc_hdr()))
			return None

		return msg

	def handle_data_msg(self, msg):
		# TODO: make legacy mode configurable (via argv?)
		self.data_if.send_msg(msg, legacy = True)
