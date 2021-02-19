# -*- coding: utf-8 -*-

'''
TRXD PDU definitions based on declarative codec.
'''

# TRX Toolkit
#
# (C) 2021 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
# Author: Vadim Yanitskiy <vyanitskiy@sysmocom.de>
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

import codec

class Header(codec.BitFieldSet):
	''' Header constructor for TRXD PDUs. '''

	def __init__(self, ver: int):
		codec.BitFieldSet.__init__(self, len=1, set=(
			codec.BitField('ver', bl=4, val=ver),
			codec.BitField.Pad(bl=1), # RFU
			codec.BitField('tn', bl=3),
		))

class MTS(codec.BitFieldSet):
	''' Modulation and Training Sequence. '''

	DEF_LEN = 1
	STRUCT = (
		codec.BitField('nope', bl=1),
		codec.BitField('mod', bl=4),
		codec.BitField('tsc', bl=3),
	)

	@staticmethod
	def get_burst_len(mod: int) -> int:
		''' Get burst length by modulation type. '''

		GMSK_BURST_LEN = 148

		if (mod >> 2) in (0b00, 0b11): # GMSK or AQPSK
			return 1 * GMSK_BURST_LEN
		elif (mod >> 1) == 0b010: # 8-PSK
			return 3 * GMSK_BURST_LEN
		elif (mod >> 1) == 0b100: # 16QAM
			return 4 * GMSK_BURST_LEN
		elif (mod >> 1) == 0b101: # 32QAM
			return 5 * GMSK_BURST_LEN
		elif mod == 0b0110: # GMSK (Access Burst)
			return 1 * GMSK_BURST_LEN
		raise ValueError('Unknown modulation type')

class BurstBits(codec.Buf):
	''' Soft-/hard-bits with variable length. '''

	def __init__(self, name: str, *args, **kw):
		codec.Buf.__init__(self, name, *args, **kw)

		# This field has a variable length that depends on moduletion type
		self.get_len = lambda v, _: MTS.get_burst_len(v['mod'])
		# This field is not present in NOPE / IDLE indications
		self.get_pres = lambda v: not v['nope']


class PDUv0Rx(codec.Envelope):
	STRUCT = (
		Header(ver=0),
		codec.Uint32BE('fn'),
		codec.Uint('rssi', mult=-1),
		codec.Int16BE('toa256'),
		codec.Buf('soft-bits'),
		codec.Buf('pad'), # Optional
	)

	def __init__(self, *args, **kw):
		codec.Envelope.__init__(self, *args, **kw)

		# Field 'soft-bits' is either 148 (GMSK) or 444 (8-PSK) octets long
		self.STRUCT[-2].get_len = lambda _, data: 444 if len(data) > 148 else 148

class PDUv0Tx(codec.Envelope):
	STRUCT = (
		Header(ver=0),
		codec.Uint32BE('fn'),
		codec.Uint('pwr'),
		codec.Buf('hard-bits'),
	)


class PDUv1Rx(codec.Envelope):
	STRUCT = (
		Header(ver=1),
		codec.Uint32BE('fn'),
		codec.Uint('rssi', mult=-1),
		codec.Int16BE('toa256'),
		MTS(),
		codec.Int16BE('cir'),
		BurstBits('soft-bits'),
	)

class PDUv1Tx(PDUv0Tx):
	# Same structure as PDUv0Tx, only the version is different
	def __init__(self, *args, **kw):
		PDUv0Tx.__init__(self, *args, **kw)
		self.STRUCT[0]._fields[0].val = 1
