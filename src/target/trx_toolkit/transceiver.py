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

import logging as log
import threading

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

	== Child transceivers

	A BTS can (optionally) have more than one transceiver. In this case
	additional (let's say child) transceivers basically share the same
	clock source of the first transceiver, so UDP port mapping is a bit
	different, for example:

	  (trx_0) clck=5700, ctrl=5701, data=5702,
	  (trx_1)            ctrl=5703, data=5704,
	  (trx_2)            ctrl=5705, data=5706.
	  ...

	By default, powering on/off a parent transceiver (child_idx=0) will
	automatically power on/off its child transceivers (if any).  This
	behavior can be disabled by setting "child_mgt" param to False.

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

	== The transmit burst queue

	According to 3GPP 45.002, the time difference between Uplink and
	Downlink corresponds to three TDMA timeslot periods.  However,
	in general the L1 implementations (such as osmo-bts-trx and trxcon)
	never schedule to be transmitted bursts for the current TDMA frame
	immediately.  Instead, they are being scheduled prematurely.

	The rationale is that both transceiver and the L1 implementation
	are separete processes that are not perfectly synchronized in time.
	Moreover, the transceiver needs some time to prepare a burst for
	transmission.  This is why the time difference between Uplink and
	Downlink is actually much higher on practice (20 TDMA frame periods
	by default, at the moment of writing this patch).

	In order to reflect that delay in a virtual environment, this
	implementation, just like a normal transceiver (e.g. osmo-trx),
	queues all to be transmitted (L12TRX) bursts, so hey remain in
	the transmit queue until the appropriate time of transmission.

	The API user is supposed to call recv_data_msg() in order to obtain
	a L12TRX message on the TRXD (data) inteface, so it gets queued by
	this function.  Then, to ensure the timeous transmission, the user
	of this implementation needs to call clck_tick() on each TDMA
	frame.  Both functions are thread-safe (queue mutex).

	In a multi-trx configuration, the use of queue additionally ensures
	proper burst aggregation on multiple TRXD connections, so all L12TRX
	messages are guaranteed to be sent in the right order, i.e. with
	monolithically-increasing TDMA frame numbers.

	"""

	def __init__(self, bind_addr, remote_addr, base_port, **kwargs):
		# Connection info
		self.remote_addr = remote_addr
		self.bind_addr = bind_addr
		self.base_port = base_port
		self.child_idx = kwargs.get("child_idx", 0)
		self.child_mgt = kwargs.get("child_mgt", True)

		# Meta info
		self.name = kwargs.get("name", None)

		log.info("Init transceiver '%s'" % self)

		# Child transceiver cannot have its own clock
		self.clck_gen = kwargs.get("clck_gen", None)
		if self.clck_gen is not None and self.child_idx > 0:
			raise TypeError("Child transceiver cannot have its own clock")

		# Init DATA interface
		self.data_if = DATAInterface(
			remote_addr, base_port + self.child_idx * 2 + 102,
			bind_addr, base_port + self.child_idx * 2 + 2)

		# Init CTRL interface
		self.ctrl_if = CTRLInterfaceTRX(self,
			remote_addr, base_port + self.child_idx * 2 + 101,
			bind_addr, base_port + self.child_idx * 2 + 1)

		# Init optional CLCK interface
		if self.clck_gen is not None:
			self.clck_if = UDPLink(
				remote_addr, base_port + 100,
				bind_addr, base_port)

		# Optional Power Measurement interface
		self.pwr_meas = kwargs.get("pwr_meas", None)

		# Internal state
		self.running = False

		# Actual RX / TX frequencies
		self._rx_freq = None
		self._tx_freq = None

		# Frequency hopping parameters (set by CTRL)
		self.fh = None

		# List of child transceivers
		self.child_trx_list = TRXList()

		# Tx (L12TRX) burst queue and mutex
		self._tx_queue_lock = threading.Lock()
		self._tx_queue = []

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

	def power_event_handler(self, poweron: bool) -> None:
		# If self.child_mgt is True, automatically power on/off children
		if self.child_mgt and self.child_idx == 0:
			trx_list = [self, *self.child_trx_list.trx_list]
		else:
			trx_list = [self]
		# Update self and optionally child transceivers
		for trx in trx_list:
			trx.running = poweron
			if not poweron:
				trx.tx_queue_clear()
				trx.disable_fh()

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
		msg = self.data_if.recv_tx_msg()
		if not msg:
			return None

		# Make sure that transceiver is configured and running
		if not self.running:
			log.warning("(%s) RX TRXD message (%s), but transceiver "
				"is not running => dropping..." % (self, msg.desc_hdr()))
			return None

		# Enque the message, it will be sent later
		self.tx_queue_append(msg)
		return msg

	def handle_data_msg(self, msg):
		# TODO: make legacy mode configurable (via argv?)
		self.data_if.send_msg(msg, legacy = True)

	def tx_queue_append(self, msg):
		with self._tx_queue_lock:
			self._tx_queue.append(msg)

	def tx_queue_clear(self):
		with self._tx_queue_lock:
			self._tx_queue.clear()

	def clck_tick(self, fwd, fn):
		if not self.running:
			return

		drop = []
		emit = []
		wait = []

		self._tx_queue_lock.acquire()

		for msg in self._tx_queue:
			if msg.fn < fn:
				drop.append(msg)
			elif msg.fn == fn:
				emit.append(msg)
			else:
				wait.append(msg)

		self._tx_queue = wait
		self._tx_queue_lock.release()

		for msg in emit:
			fwd.forward_msg(self, msg)

		for msg in drop:
			log.warning("(%s) Stale TRXD message (fn=%u): %s"
				% (self, fn, msg.desc_hdr()))
