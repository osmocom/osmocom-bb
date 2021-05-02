#!/usr/bin/env python3
# -*- coding: utf-8 -*-

'''
Unit tests for declarative message codec.
'''

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

import unittest
import struct
import codec

class TestField(codec.Field):
	DEF_PARAMS = { 'key' : 0xde }
	DEF_LEN = 4

	@staticmethod
	def xor(data: bytes, key: int = 0x00):
		return bytes([x ^ key for x in data])

	def _from_bytes(self, vals: dict, data: bytes) -> None:
		vals[self.name] = self.xor(data, self.p['key'])

	def _to_bytes(self, vals: dict) -> bytes:
		return self.xor(self.get_val(vals), self.p['key'])

class Field(unittest.TestCase):
	MAGIC = b'\xde\xad\xbe\xef'

	def test_to_bytes(self):
		vals = { 'magic' : self.MAGIC, 'other' : 'unrelated' }
		encoded_de = TestField.xor(self.MAGIC, 0xde)
		encoded_88 = TestField.xor(self.MAGIC, 0x88)

		with self.subTest('default length=4, default key=0xde'):
			field = TestField('magic')
			self.assertEqual(field.to_bytes(vals), encoded_de)

		with self.subTest('default length=4, different key=0x88'):
			field = TestField('magic', key=0x88)
			self.assertEqual(field.to_bytes(vals), encoded_88)

		with self.subTest('different length=2, default key=0xde'):
			field = TestField('magic', len=2)
			vals['magic'] = vals['magic'][:2]
			self.assertEqual(field.to_bytes(vals), encoded_de[:2])

		with self.subTest('EncodeError due to length mismatch'):
			field = TestField('magic', len=8)
			with self.assertRaises(codec.EncodeError):
				field.to_bytes(vals)

	def test_from_bytes(self):
		encoded_de = TestField.xor(self.MAGIC, 0xde) + b'\xff' * 60
		encoded_88 = TestField.xor(self.MAGIC, 0x88) + b'\xff' * 60
		vals = { 'magic' : 'overrien', 'other' : 'unchanged' }

		with self.subTest('default length=4, default key=0xde'):
			field = TestField('magic')
			offset = field.from_bytes(vals, encoded_de)
			self.assertEqual(vals['other'], 'unchanged')
			self.assertEqual(vals['magic'], self.MAGIC)
			self.assertEqual(offset, len(self.MAGIC))

		with self.subTest('default length=4, different key=0x88'):
			field = TestField('magic', key=0x88)
			offset = field.from_bytes(vals, encoded_88)
			self.assertEqual(vals['other'], 'unchanged')
			self.assertEqual(vals['magic'], self.MAGIC)
			self.assertEqual(offset, len(self.MAGIC))

		with self.subTest('different length=2, default key=0xde'):
			field = TestField('magic', len=2)
			offset = field.from_bytes(vals, encoded_de)
			self.assertEqual(vals['other'], 'unchanged')
			self.assertEqual(vals['magic'], self.MAGIC[:2])
			self.assertEqual(offset, 2)

		with self.subTest('full length, different key=0x88'):
			field = TestField('magic', len=0, key=0x88)
			offset = field.from_bytes(vals, encoded_88)
			self.assertEqual(vals['other'], 'unchanged')
			self.assertEqual(vals['magic'], self.MAGIC + b'\x77' * 60)
			self.assertEqual(offset, len(encoded_88))

		with self.subTest('DecodeError due to short read'):
			field = TestField('magic', len=4)
			with self.assertRaises(codec.DecodeError):
				field.from_bytes(vals, b'\x00')

	def test_get_pres(self):
		vals = { 'magic' : self.MAGIC }

		with self.subTest('to_bytes() for a non-existing field'):
			field = TestField('not-there')
			with self.assertRaises(KeyError):
				field.to_bytes(vals)

		with self.subTest('to_bytes() for a field with get_pres()'):
			field = TestField('magic', key=0x00)
			field.get_pres = lambda v: not v['omit']

			data = field.to_bytes({ **vals, 'omit' : False })
			self.assertEqual(data, self.MAGIC)

			data = field.to_bytes({ **vals, 'omit' : True })
			self.assertEqual(data, b'')

		with self.subTest('from_bytes() for a field with get_pres()'):
			field = TestField('magic', key=0x00)
			field.get_pres = lambda v: not v['omit']

			vals = { 'omit' : False }
			offset = field.from_bytes(vals, self.MAGIC)
			self.assertEqual(vals['magic'], self.MAGIC)
			self.assertEqual(offset, len(self.MAGIC))

			vals = { 'omit' : True }
			offset = field.from_bytes(vals, self.MAGIC)
			self.assertFalse('magic' in vals)
			self.assertEqual(offset, 0)

	def test_get_len(self):
		vals = { 'len' : 32, 'unrelated' : 'foo' }

		field = TestField('magic', key=0x00)
		field.get_len = lambda v, _: v['len']

		with self.subTest('not enough octets in the buffer: 16 < 32'):
			with self.assertRaises(codec.DecodeError):
				field.from_bytes(vals, b'\xff' * 16)

		with self.subTest('more than enough octets in the buffer'):
			offset = field.from_bytes(vals, b'\xff' * 64)
			self.assertEqual(vals['magic'], b'\xff' * 32)
			self.assertEqual(offset, 32)

		with self.subTest('length field does not exist'):
			with self.assertRaises(KeyError):
				field.from_bytes({ }, b'\xff' * 64)

	def test_get_val(self):
		field = TestField('magic', key=0x00, len=0)
		field.get_val = lambda v: v.get('val', self.MAGIC)

		with self.subTest('value is present in the dict'):
			data = field.to_bytes({ 'val' : b'\xd0\xde' })
			self.assertEqual(data, b'\xd0\xde')

		with self.subTest('value is not present in the dict'):
			data = field.to_bytes({ })
			self.assertEqual(data, self.MAGIC)

class Buf(unittest.TestCase):
	MAGIC = b'\xde\xad' * 4

	def test_to_bytes(self):
		vals = { 'buf' : self.MAGIC }

		with self.subTest('with no length constraints'):
			field = codec.Buf('buf') # default: len=0
			self.assertEqual(field.to_bytes(vals), self.MAGIC)

		with self.subTest('with length constraints'):
			field = codec.Buf('buf', len=len(self.MAGIC))
			self.assertEqual(field.to_bytes(vals), self.MAGIC)

		with self.subTest('EncodeError due to length mismatch'):
			field = codec.Buf('buf', len=4)
			with self.assertRaises(codec.EncodeError):
				field.to_bytes(vals)

	def test_from_bytes(self):
		vals = { }

		with self.subTest('with no length constraints'):
			field = codec.Buf('buf') # default: len=0
			offset = field.from_bytes(vals, self.MAGIC)
			self.assertEqual(vals['buf'], self.MAGIC)
			self.assertEqual(offset, len(self.MAGIC))

		with self.subTest('with length constraints'):
			field = codec.Buf('buf', len=2)
			offset = field.from_bytes(vals, self.MAGIC)
			self.assertEqual(vals['buf'], self.MAGIC[:2])
			self.assertEqual(offset, len(self.MAGIC[:2]))

		with self.subTest('DecodeError due to not enough bytes'):
			field = codec.Buf('buf', len=64)
			with self.assertRaises(codec.DecodeError):
				field.from_bytes(vals, self.MAGIC)

class Spare(unittest.TestCase):
	# Fixed length with custom filler
	SAA = codec.Spare('pad', len=4, filler=b'\xaa')
	# Auto-calculated length with custom filler
	SFF = codec.Spare('pad', filler=b'\xff')
	SFF.get_len = lambda v, _: v['len']
	# Fixed length with default filler
	S00 = codec.Spare('pad', len=2)

	def test_to_bytes(self):
		self.assertEqual(self.SFF.to_bytes({ 'len' : 8 }), b'\xff' * 8)
		self.assertEqual(self.SAA.to_bytes({ }), b'\xaa' * 4)
		self.assertEqual(self.S00.to_bytes({ }), b'\x00' * 2)

	def test_from_bytes(self):
		with self.assertRaises(codec.DecodeError):
			self.S00.from_bytes({ }, b'\x00') # Short read
		self.assertEqual(self.SFF.from_bytes({ 'len' : 8 }, b'\xff' * 8), 8)
		self.assertEqual(self.SAA.from_bytes({ }, b'\xaa' * 64), 4)
		self.assertEqual(self.S00.from_bytes({ }, b'\x00' * 64), 2)

class Uint(unittest.TestCase):
	def _test_uint(self, field, fmt, vals):
		for i in vals:
			with self.subTest('to_bytes()'):
				val = field.to_bytes({ field.name : i })
				self.assertEqual(val, struct.pack(fmt, i))

			with self.subTest('from_bytes()'):
				data, parsed = struct.pack(fmt, i), { }
				offset = field.from_bytes(parsed, data)
				self.assertEqual(offset, len(data))
				self.assertEqual(parsed[field.name], i)

	def test_uint8(self):
		self._test_uint(codec.Uint('foo'), 'B', range(2 ** 8))

	def test_int8(self):
		self._test_uint(codec.Int('foo'), 'b', range(-128, 128))

	def test_uint16(self):
		vals = (0, 65, 128, 255, 512, 1023, 2 ** 16 - 1)
		self._test_uint(codec.Uint16BE('foo'), '>H', vals)
		self._test_uint(codec.Uint16LE('foo'), '<H', vals)

	def test_int16(self):
		vals = (-32767, -16384, 0, 16384, 32767)
		self._test_uint(codec.Int16BE('foo'), '>h', vals)
		self._test_uint(codec.Int16LE('foo'), '<h', vals)

	def test_uint32(self):
		vals = (0, 33, 255, 1024, 1337, 4099, 2 ** 32 - 1)
		self._test_uint(codec.Uint32BE('foo'), '>I', vals)
		self._test_uint(codec.Uint32LE('foo'), '<I', vals)

	def test_int32(self):
		vals = (-2147483647, 0, 2147483647)
		self._test_uint(codec.Int32BE('foo'), '>i', vals)
		self._test_uint(codec.Int32LE('foo'), '<i', vals)

	def test_offset_mult(self):
		with self.subTest('encode / decode with offset=5'):
			field = codec.Uint('foo', offset=5)

			self.assertEqual(field.to_bytes({ 'foo' : 10 }), b'\x05')
			self.assertEqual(field.to_bytes({ 'foo' :  5 }), b'\x00')

			vals = { 'foo' : 'overriden' }
			field.from_bytes(vals, b'\xff')
			self.assertEqual(vals['foo'], 260)
			field.from_bytes(vals, b'\x00')
			self.assertEqual(vals['foo'], 5)

		with self.subTest('encode / decode with mult=2'):
			field = codec.Uint('foo', mult=2)

			self.assertEqual(field.to_bytes({ 'foo' : 0 }), b'\x00')
			self.assertEqual(field.to_bytes({ 'foo' : 3 }), b'\x01')
			self.assertEqual(field.to_bytes({ 'foo' : 32 }), b'\x10')
			self.assertEqual(field.to_bytes({ 'foo' : 64 }), b'\x20')

			vals = { 'foo' : 'overriden' }
			field.from_bytes(vals, b'\x00')
			self.assertEqual(vals['foo'], 0 * 2)
			field.from_bytes(vals, b'\x0f')
			self.assertEqual(vals['foo'], 15 * 2)
			field.from_bytes(vals, b'\xff')
			self.assertEqual(vals['foo'], 255 * 2)

class BitFieldSet(unittest.TestCase):
	S16 = codec.BitFieldSet(set=(
		codec.BitField('f4a', bl=4),
		codec.BitField('f8', bl=8),
		codec.BitField('f4b', bl=4),
	))

	S8M = codec.BitFieldSet(order='msb', set=(
		codec.BitField('f4', bl=4),
		codec.BitField('f1', bl=1),
		codec.BitField('f3', bl=3),
	))

	S8L = codec.BitFieldSet(order='lsb', set=(
		codec.BitField('f4', bl=4),
		codec.BitField('f1', bl=1),
		codec.BitField('f3', bl=3),
	))

	S8V = codec.BitFieldSet(set=(
		codec.BitField('f4', bl=4, val=2),
		codec.BitField('f1', bl=1, val=0),
		codec.BitField('f3', bl=3),
	))

	S8P = codec.BitFieldSet(set=(
		codec.BitField.Spare(bl=4),
		codec.BitField('f4', bl=4),
	))

	@staticmethod
	def from_bytes(s: codec.BitFieldSet, data: bytes) -> dict:
		vals = { }
		s.from_bytes(vals, data)
		return vals

	def test_len_auto(self):
		with self.subTest('1 + 2 = 3 bits => 1 octet (with padding)'):
			s = codec.BitFieldSet(set=(
				codec.BitField('f1', bl=1),
				codec.BitField('f2', bl=2),
			))
			self.assertEqual(s.len, 1)

		with self.subTest('4 + 2 + 2 = 8 bits => 1 octet'):
			s = codec.BitFieldSet(set=(
				codec.BitField('f4', bl=4),
				codec.BitField('f2a', bl=2),
				codec.BitField('f2b', bl=2),
			))
			self.assertEqual(s.len, 1)

		with self.subTest('12 + 4 + 2 = 18 bits => 3 octets (with padding)'):
			s = codec.BitFieldSet(set=(
				codec.BitField('f12', bl=12),
				codec.BitField('f4', bl=4),
				codec.BitField('f2', bl=2),
			))
			self.assertEqual(s.len, 3)

	def test_overflow(self):
		with self.assertRaises(codec.ProtocolError):
			s = codec.BitFieldSet(len=1, set=(
				codec.BitField('f6', bl=6),
				codec.BitField('f4', bl=4),
			))

	def test_offset_mask(self):
		calc = lambda s: [(f.name, f.offset, f.mask) for f in s._fields]

		with self.subTest('16 bit total (MSB): f4a + f8 + f4b'):
			om = [('f4a', 8 + 4, 0x0f), ('f8', 4, 0xff), ('f4b', 0, 0x0f)]
			self.assertEqual(len(self.S16._fields), 3)
			self.assertEqual(calc(self.S16), om)

		with self.subTest('8 bit total (MSB): f4 + f1 + f3'):
			om = [('f4', 1 + 3, 0x0f), ('f1', 3, 0x01), ('f3', 0, 0x07)]
			self.assertEqual(len(self.S8M._fields), 3)
			self.assertEqual(calc(self.S8M), om)

		with self.subTest('8 bit total (LSB): f4 + f1 + f3'):
			om = [('f3', 1 + 4, 0x07), ('f1', 4, 0x01), ('f4', 0, 0x0f)]
			self.assertEqual(len(self.S8L._fields), 3)
			self.assertEqual(calc(self.S8L), om)

		with self.subTest('8 bit total (LSB): s4 + f4'):
			om = [(None, 4, 0x0f), ('f4', 0, 0x0f)]
			self.assertEqual(len(self.S8P._fields), 2)
			self.assertEqual(calc(self.S8P), om)

	def test_to_bytes(self):
		with self.subTest('16 bit total (MSB): f4a + f8 + f4b'):
			vals = { 'f4a' : 0x0f, 'f8' : 0xff, 'f4b' : 0x0f }
			self.assertEqual(self.S16.to_bytes(vals), b'\xff\xff')
			vals = { 'f4a' : 0x00, 'f8' : 0x00, 'f4b' : 0x00 }
			self.assertEqual(self.S16.to_bytes(vals), b'\x00\x00')
			vals = { 'f4a' : 0x0f, 'f8' : 0x00, 'f4b' : 0x0f }
			self.assertEqual(self.S16.to_bytes(vals), b'\xf0\x0f')
			vals = { 'f4a' : 0x00, 'f8' : 0xff, 'f4b' : 0x00 }
			self.assertEqual(self.S16.to_bytes(vals), b'\x0f\xf0')

		with self.subTest('8 bit total (MSB): f4 + f1 + f3'):
			vals = { 'f4' : 0x0f, 'f1' : 0x01, 'f3' : 0x07 }
			self.assertEqual(self.S8M.to_bytes(vals), b'\xff')
			vals = { 'f4' : 0x00, 'f1' : 0x00, 'f3' : 0x00 }
			self.assertEqual(self.S8M.to_bytes(vals), b'\x00')
			vals = { 'f4' : 0x0f, 'f1' : 0x00, 'f3' : 0x00 }
			self.assertEqual(self.S8M.to_bytes(vals), b'\xf0')

		with self.subTest('8 bit total (LSB): f4 + f1 + f3'):
			vals = { 'f4' : 0x0f, 'f1' : 0x01, 'f3' : 0x07 }
			self.assertEqual(self.S8L.to_bytes(vals), b'\xff')
			vals = { 'f4' : 0x00, 'f1' : 0x00, 'f3' : 0x00 }
			self.assertEqual(self.S8L.to_bytes(vals), b'\x00')
			vals = { 'f4' : 0x0f, 'f1' : 0x00, 'f3' : 0x00 }
			self.assertEqual(self.S8L.to_bytes(vals), b'\x0f')

	def test_from_bytes(self):
		pad = b'\xff' * 64

		with self.subTest('16 bit total (MSB): f4a + f8 + f4b'):
			vals = { 'f4a' : 0x0f, 'f8' : 0xff, 'f4b' : 0x0f }
			self.assertEqual(self.from_bytes(self.S16, b'\xff\xff' + pad), vals)
			vals = { 'f4a' : 0x00, 'f8' : 0x00, 'f4b' : 0x00 }
			self.assertEqual(self.from_bytes(self.S16, b'\x00\x00' + pad), vals)
			vals = { 'f4a' : 0x0f, 'f8' : 0x00, 'f4b' : 0x0f }
			self.assertEqual(self.from_bytes(self.S16, b'\xf0\x0f' + pad), vals)
			vals = { 'f4a' : 0x00, 'f8' : 0xff, 'f4b' : 0x00 }
			self.assertEqual(self.from_bytes(self.S16, b'\x0f\xf0' + pad), vals)

		with self.subTest('8 bit total (MSB): f4 + f1 + f3'):
			vals = { 'f4' : 0x0f, 'f1' : 0x01, 'f3' : 0x07 }
			self.assertEqual(self.from_bytes(self.S8M, b'\xff' + pad), vals)
			vals = { 'f4' : 0x00, 'f1' : 0x00, 'f3' : 0x00 }
			self.assertEqual(self.from_bytes(self.S8M, b'\x00' + pad), vals)
			vals = { 'f4' : 0x0f, 'f1' : 0x00, 'f3' : 0x00 }
			self.assertEqual(self.from_bytes(self.S8M, b'\xf0' + pad), vals)

		with self.subTest('8 bit total (LSB): f4 + f1 + f3'):
			vals = { 'f4' : 0x0f, 'f1' : 0x01, 'f3' : 0x07 }
			self.assertEqual(self.from_bytes(self.S8L, b'\xff' + pad), vals)
			vals = { 'f4' : 0x00, 'f1' : 0x00, 'f3' : 0x00 }
			self.assertEqual(self.from_bytes(self.S8L, b'\x00' + pad), vals)
			vals = { 'f4' : 0x0f, 'f1' : 0x00, 'f3' : 0x00 }
			self.assertEqual(self.from_bytes(self.S8L, b'\x0f' + pad), vals)

	def test_to_bytes_val(self):
		with self.subTest('fixed values in absence of user-supplied values'):
			vals = { 'f3' : 0x00 } # | { 'f4' : 2, 'f1' : 0 }
			self.assertEqual(self.S8V.to_bytes(vals), b'\x20')

		with self.subTest('fixed values take precedence'):
			vals = { 'f4' : 1, 'f1' : 1, 'f3' : 0 }
			self.assertEqual(self.S8V.to_bytes(vals), b'\x20')

	def test_from_bytes_val(self):
		with self.assertRaises(codec.DecodeError):
			self.S8V.from_bytes({ }, b'\xf0') # 'f4': 15 vs 2

		with self.assertRaises(codec.DecodeError):
			self.S8V.from_bytes({ }, b'\x08') # 'f1': 1 vs 0

		# Field 'f3' takes any value, no exceptions shall be raised
		for i in range(8):
			data, vals = bytes([0x20 + i]), { 'f4' : 2, 'f1' : 0, 'f3' : i }
			self.assertEqual(self.from_bytes(self.S8V, data), vals)

	def test_to_bytes_spare(self):
		self.assertEqual(self.S8P.to_bytes({ 'f4' : 0x00 }), b'\x00')
		self.assertEqual(self.S8P.to_bytes({ 'f4' : 0x0f }), b'\x0f')
		self.assertEqual(self.S8P.to_bytes({ 'f4' : 0xff }), b'\x0f')

	def test_from_bytes_spare(self):
		self.assertEqual(self.from_bytes(self.S8P, b'\x00'), { 'f4' : 0x00 })
		self.assertEqual(self.from_bytes(self.S8P, b'\x0f'), { 'f4' : 0x0f })
		self.assertEqual(self.from_bytes(self.S8P, b'\xff'), { 'f4' : 0x0f })

class TestPDU(codec.Envelope):
	STRUCT = (
		codec.BitFieldSet(len=2, set=(
			codec.BitField('ver', bl=4),
			codec.BitField('flag', bl=1),
		)),
		codec.Uint16BE('len'),
		codec.Buf('data'),
		codec.Buf('tail', len=2),
	)

	def __init__(self, *args, **kw):
		codec.Envelope.__init__(self, *args, **kw)
		self.STRUCT[-3].get_val = lambda v: len(v['data'])
		self.STRUCT[-2].get_len = lambda v, _: v['len']
		self.STRUCT[-1].get_pres = lambda v: bool(v['flag'])

	def check(self, vals: dict) -> None:
		if not vals['ver'] in (0, 1, 2):
			raise ValueError('Unknown version %d' % vals['ver'])

class Envelope(unittest.TestCase):
	def test_rest_octets(self):
		pdu = TestPDU(check_len=False)
		pdu.from_bytes(b'\x00' * 64)

		with self.assertRaises(codec.DecodeError):
			pdu = TestPDU(check_len=True)
			pdu.from_bytes(b'\x00' * 64) # 'len' : 0

	def test_field_raises(self):
		pdu = TestPDU()
		with self.assertRaises(codec.EncodeError):
			pdu.c = { 'ver' : 0, 'flag' : 1, 'data' : b'\xff' * 16 }
			pdu.to_bytes() # KeyError: 'tail' not found

	def test_to_bytes(self):
		pdu = TestPDU()

		# No content in the new instances
		self.assertEqual(pdu.c, { })

		pdu.c = { 'ver' : 0, 'flag' : 1, 'data' : b'', 'tail' : b'\xde\xbe' }
		self.assertEqual(pdu.to_bytes(), b'\x08\x00\x00\x00' + b'\xde\xbe')

		pdu.c = { 'ver' : 1, 'flag' : 0, 'data' : b'\xff' * 15 }
		self.assertEqual(pdu.to_bytes(), b'\x10\x00\x00\x0f' + b'\xff' * 15)

		pdu.c = { 'ver' : 2, 'flag' : 1, 'data' : b'\xf0', 'tail' : b'\xbe\xed' }
		self.assertEqual(pdu.to_bytes(), b'\x28\x00\x00\x01\xf0\xbe\xed')

	def test_from_bytes(self):
		pdu = TestPDU()

		# No content in the new instances
		self.assertEqual(pdu.c, { })

		c = { 'ver' : 0, 'flag' : 1, 'len' : 0, 'data' : b'', 'tail' : b'\xde\xbe' }
		pdu.from_bytes(b'\x08\x00\x00\x00' + b'\xde\xbe')
		self.assertEqual(pdu.c, c)

		c = { 'ver' : 1, 'flag' : 0, 'len' : 15, 'data' : b'\xff' * 15 }
		pdu.from_bytes(b'\x10\x00\x00\x0f' + b'\xff' * 15)
		self.assertEqual(pdu.c, c)

		c = { 'ver' : 2, 'flag' : 1, 'len' : 1, 'data' : b'\xf0', 'tail' : b'\xbe\xed' }
		pdu.from_bytes(b'\x28\x00\x00\x01\xf0\xbe\xed')
		self.assertEqual(pdu.c, c)

	def test_to_bytes_check(self):
		pdu = TestPDU()

		pdu.c = { 'ver' : 8, 'flag' : 1, 'data' : b'', 'tail' : b'\xde\xbe' }
		with self.assertRaises(ValueError):
			pdu.to_bytes()

	def test_from_bytes_check(self):
		pdu = TestPDU()

		with self.assertRaises(ValueError):
			pdu.from_bytes(b'\xf0\x00\x00\x00')

class Sequence(unittest.TestCase):
	class TLV(codec.Envelope):
		STRUCT = (
			codec.Uint('T'),
			codec.Uint('L'),
			codec.Buf('V'),
		)

		def __init__(self, *args, **kw) -> None:
			codec.Envelope.__init__(self, *args, **kw)
			self.STRUCT[-2].get_val = lambda v: len(v['V'])
			self.STRUCT[-1].get_len = lambda v, _: v['L']

	# Sequence of TLVs
	SEQ = codec.Sequence(item=TLV())

	Vseq, Bseq = [
		{ 'T' : 0xde, 'L' : 4, 'V' : b'\xde\xad\xbe\xef' },
		{ 'T' : 0xbe, 'L' : 2, 'V' : b'\xbe\xef' },
		{ 'T' : 0xbe, 'L' : 2, 'V' : b'\xef\xbe' },
		{ 'T' : 0x00, 'L' : 0, 'V' : b'' },
	], b''.join([
		b'\xde\x04\xde\xad\xbe\xef',
		b'\xbe\x02\xbe\xef',
		b'\xbe\x02\xef\xbe',
		b'\x00\x00',
	])

	def test_to_bytes(self):
		res = self.SEQ.to_bytes(self.Vseq)
		self.assertEqual(res, self.Bseq)

	def test_from_bytes(self):
		res = self.SEQ.from_bytes(self.Bseq)
		self.assertEqual(res, self.Vseq)

if __name__ == '__main__':
	unittest.main()
