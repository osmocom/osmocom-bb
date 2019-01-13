#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# TRX Toolkit
# Transceiver list implementation
#
# (C) 2019 by Vadim Yanitskiy <axilirator@gmail.com>
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

class TRXList:
	""" Transceiver list implementation.

	This class is a simple wrapper around generic Python's list.
	The aim is to simplify management of multiple Transceiver
	instances, e.g. appending, removing and finding them.

	"""

	def __init__(self):
		self.trx_list = []

	def __getitem__(self, i):
		return self.trx_list[i]

	def find_trx(self, remote_addr, base_port, child_idx = 0):
		for trx in self.trx_list:
			if trx.remote_addr != remote_addr:
				continue
			if trx.base_port != base_port:
				continue
			if trx.child_idx != child_idx:
				continue
			return trx

		return None

	def add_trx(self, trx):
		if trx in self.trx_list:
			raise IndexError("TRX '%s' is already in the list" % trx)
		if self.find_trx(trx.remote_addr, trx.base_port, trx.child_idx):
			raise IndexError("TRX '%s' has duplicate in the list" % trx)

		self.trx_list.append(trx)

	def del_trx(self, trx):
		if trx not in self.trx_list:
			raise IndexError("TRX '%s' is not in the list" % trx)
		self.trx_list.remove(trx)
