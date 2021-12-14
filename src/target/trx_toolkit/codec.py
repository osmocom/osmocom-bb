# -*- coding: utf-8 -*-

'''
Very simple (performance oriented) declarative message codec.
Inspired by Pycrate and Scapy.
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

from typing import Optional, Callable, Tuple, Any
import abc

class ProtocolError(Exception):
	''' Error in a protocol definition. '''

class DecodeError(Exception):
	''' Error during decoding of a field/message. '''

class EncodeError(Exception):
	''' Error during encoding of a field/message. '''


class Codec(abc.ABC):
	''' Base class providing encoding and decoding API. '''

	@abc.abstractmethod
	def from_bytes(self, vals: dict, data: bytes) -> int:
		''' Decode value(s) from the given buffer of bytes. '''

	@abc.abstractmethod
	def to_bytes(self, vals: dict) -> bytes:
		''' Encode value(s) into bytes. '''


class Field(Codec):
	''' Base class representing one field in a Message. '''

	# Default length (0 means the whole buffer)
	DEF_LEN = 0 # type: int

	# Default parameters
	DEF_PARAMS = { } # type: dict

	# Presence of a field during decoding and encoding
	## get_pres: Callable[[dict], bool]
	# Length of a field for self.from_bytes()
	## get_len: Callable[[dict, bytes], int]
	# Value of a field for self.to_bytes()
	## get_val: Callable[[dict], Any]

	def __init__(self, name: str, **kw) -> None:
		self.name = name

		self.len = kw.get('len', self.DEF_LEN)
		if self.len == 0: # flexible field
			self.get_len = lambda _, data: len(data)
		else: # fixed length
			self.get_len = lambda vals, _: self.len

		# Field is unconditionally present by default
		self.get_pres = lambda vals: True
		# Field takes its value from the given dict by default
		self.get_val = lambda vals: vals[self.name]

		# Additional parameters for derived field types
		self.p = { key : kw.get(key, self.DEF_PARAMS[key])
				for key in self.DEF_PARAMS }

	def from_bytes(self, vals: dict, data: bytes) -> int:
		if self.get_pres(vals) is False:
			return 0
		length = self.get_len(vals, data)
		if len(data) < length:
			raise DecodeError('Short read')
		self._from_bytes(vals, data[:length])
		return length

	def to_bytes(self, vals: dict) -> bytes:
		if self.get_pres(vals) is False:
			return b''
		data = self._to_bytes(vals)
		if self.len > 0 and len(data) != self.len:
			raise EncodeError('Field length mismatch')
		return data

	@abc.abstractmethod
	def _from_bytes(self, vals: dict, data: bytes) -> None:
		''' Decode value(s) from the given buffer of bytes. '''
		raise NotImplementedError

	@abc.abstractmethod
	def _to_bytes(self, vals: dict) -> bytes:
		''' Encode value(s) into bytes. '''
		raise NotImplementedError


class Buf(Field):
	''' A sequence of octets. '''

	def _from_bytes(self, vals: dict, data: bytes) -> None:
		vals[self.name] = data

	def _to_bytes(self, vals: dict) -> bytes:
		# TODO: handle len(self.get_val()) < self.get_len()
		return self.get_val(vals)


class Spare(Field):
	''' Spare filling for RFU fields or padding. '''

	# Default parameters
	DEF_PARAMS = {
		'filler'	: b'\x00',
	}

	def _from_bytes(self, vals: dict, data: bytes) -> None:
		pass # Just ignore it

	def _to_bytes(self, vals: dict) -> bytes:
		return self.p['filler'] * self.get_len(vals, b'')


class Uint(Field):
	''' An integer field: unsigned, N bits, big endian. '''

	# Uint8 by default
	DEF_LEN = 1

	# Default parameters
	DEF_PARAMS = {
		'offset'	: 0,
		'mult'		: 1,
	}

	# Big endian, unsigned
	SIGN = False
	BO = 'big'

	def _from_bytes(self, vals: dict, data: bytes) -> None:
		val = int.from_bytes(data, self.BO, signed=self.SIGN)
		vals[self.name] = val * self.p['mult'] + self.p['offset']

	def _to_bytes(self, vals: dict) -> bytes:
		val = (self.get_val(vals) - self.p['offset']) // self.p['mult']
		return val.to_bytes(self.len, self.BO, signed=self.SIGN)

class Uint16BE(Uint):
	DEF_LEN = 16 // 8

class Uint16LE(Uint16BE):
	BO = 'little'

class Uint32BE(Uint):
	DEF_LEN = 32 // 8

class Uint32LE(Uint32BE):
	BO = 'little'

class Int(Uint):
	SIGN = True

class Int16BE(Int):
	DEF_LEN = 16 // 8

class Int16LE(Int16BE):
	BO = 'little'

class Int32BE(Int):
	DEF_LEN = 32 // 8

class Int32LE(Int32BE):
	BO = 'little'


class BitFieldSet(Field):
	''' A set of bit-fields. '''

	# Default parameters
	DEF_PARAMS = {
		# Default field order (MSB first)
		'order'		: 'big',
	}

	# To be defined by derived types
	STRUCT = () # type: Tuple['BitField', ...]

	def __init__(self, **kw) -> None:
		Field.__init__(self, self.__class__.__name__, **kw)

		self._fields = kw.get('set', self.STRUCT)
		if type(self._fields) is not tuple:
			raise ProtocolError('Expected a tuple')

		# LSB first is basically reversed order
		if self.p['order'] in ('little', 'lsb'):
			self._fields = self._fields[::-1]

		# Calculate the overall field length
		if self.len == 0:
			bl_sum = sum([f.bl for f in self._fields])
			self.len = bl_sum // 8
			if bl_sum % 8 > 0:
				self.len += 1

		# Re-define self.get_len() since we always know the length
		self.get_len = lambda vals, data: self.len

		# Pre-calculate offset and mask for each field
		offset = self.len * 8
		for f in self._fields:
			if f.bl > offset:
				raise ProtocolError(f, 'BitFieldSet overflow')
			f.offset = offset - f.bl
			f.mask = 2 ** f.bl - 1
			offset -= f.bl

	def _from_bytes(self, vals: dict, data: bytes) -> None:
		blob = int.from_bytes(data, byteorder='big') # intentionally using 'big' here
		for f in self._fields:
			f.dec_val(vals, blob)

	def _to_bytes(self, vals: dict) -> bytes:
		blob = 0x00
		for f in self._fields: # TODO: use functools.reduce()?
			blob |= f.enc_val(vals)
		return blob.to_bytes(self.len, byteorder='big')

class BitField:
	''' One field in a BitFieldSet. '''

	# Special fields for BitFieldSet
	offset = 0 # type: int
	mask = 0 # type: int

	class Spare:
		''' Spare filling in a BitFieldSet. '''

		def __init__(self, bl: int) -> None:
			self.name = None
			self.bl = bl

		def enc_val(self, vals: dict) -> int:
			return 0

		def dec_val(self, vals: dict, blob: int) -> None:
			pass # Just ignore it

	def __init__(self, name: str, bl: int, **kw) -> None:
		if bl < 1: # Ensure proper length
			raise ProtocolError('Incorrect bit-field length')

		self.name = name
		self.bl = bl

		# (Optional) fixed value for encoding and decoding
		self.val = kw.get('val', None) # type: Optional[int]

	def enc_val(self, vals: dict) -> int:
		if self.val is None:
			val = vals[self.name]
		else:
			val = self.val
		return (val & self.mask) << self.offset

	def dec_val(self, vals: dict, blob: int) -> None:
		vals[self.name] = (blob >> self.offset) & self.mask
		if (self.val is not None) and (vals[self.name] != self.val):
			raise DecodeError('Unexpected value %d, expected %d'
				% (vals[self.name], self.val))


class Envelope:
	''' A group of related fields. '''

	STRUCT = () # type: Tuple[Codec, ...]

	def __init__(self, check_len: bool = True):
		# TODO: ensure uniqueue field names in self.STRUCT
		self.c = { } # type: dict
		self.check_len = check_len

	def __getitem__(self, key: str) -> Any:
		return self.c[key]

	def __setitem__(self, key: str, val: Any) -> None:
		self.c[key] = val

	def __delitem__(self, key: str) -> None:
		del self.c[key]

	def check(self, vals: dict) -> None:
		''' Check the content before encoding and after decoding.
		    Raise exceptions (e.g. ValueError) if something is wrong.

		    Do not assert for every possible error (e.g. a negative value
		    for a Uint field) if an exception will be thrown by the field's
		    to_bytes() method anyway.  Only additional constraints here.
		'''

	def from_bytes(self, data: bytes) -> int:
		self.c.clear() # forget the old content
		return self._from_bytes(self.c, data)

	def to_bytes(self) -> bytes:
		return self._to_bytes(self.c)

	def _from_bytes(self, vals: dict, data: bytes, offset: int = 0) -> int:
		try: # Fields throw exceptions
			for f in self.STRUCT:
				offset += f.from_bytes(vals, data[offset:])
		except Exception as e:
			# Add contextual info
			raise DecodeError(self, f, offset) from e
		if self.check_len and len(data) != offset:
			raise DecodeError(self, 'Unhandled tail octets: %s'
						% data[offset:].hex())
		self.check(vals) # Check the content after decoding (raises exceptions)
		return offset

	def _to_bytes(self, vals: dict) -> bytes:
		def proc(f: Codec):
			try: # Fields throw exceptions
				return f.to_bytes(vals)
			except Exception as e:
				# Add contextual info
				raise EncodeError(self, f) from e
		self.check(vals) # Check the content before encoding (raises exceptions)
		return b''.join([proc(f) for f in self.STRUCT])

	class F(Field):
		''' Field wrapper. '''

		def __init__(self, e: 'Envelope', name: str, **kw) -> None:
			Field.__init__(self, name, **kw)
			self.e = e

		def _from_bytes(self, vals: dict, data: bytes) -> None:
			vals[self.name] = { }
			self.e._from_bytes(vals[self.name], data)

		def _to_bytes(self, vals: dict) -> bytes:
			return self.e._to_bytes(self.get_val(vals))

	def f(self, name: str, **kw) -> Field:
		return self.F(self, name, **kw)


class Sequence:
	''' A sequence of repeating elements (e.g. TLVs). '''

	# The item of sequence
	ITEM = None # type: Optional[Envelope]

	def __init__(self, **kw) -> None:
		if (self.ITEM is None) and ('item' not in kw):
			raise ProtocolError('Missing Sequence item')
		self._item = kw.get('item', self.ITEM) # type: Envelope
		self._item.check_len = False

	def from_bytes(self, data: bytes) -> list:
		proc = self._item._from_bytes
		vseq, offset = [], 0
		length = len(data)

		while offset < length:
			vseq.append({ }) # new item of sequence
			offset += proc(vseq[-1], data[offset:])

		return vseq

	def to_bytes(self, vseq: list) -> bytes:
		proc = self._item._to_bytes
		return b''.join([proc(v) for v in vseq])

	class F(Field):
		''' Field wrapper. '''

		def __init__(self, s: 'Sequence', name: str, **kw) -> None:
			Field.__init__(self, name, **kw)
			self.s = s

		def _from_bytes(self, vals: dict, data: bytes) -> None:
			vals[self.name] = self.s.from_bytes(data)

		def _to_bytes(self, vals: dict) -> bytes:
			return self.s.to_bytes(self.get_val(vals))

	def f(self, name: str, **kw) -> Field:
		return self.F(self, name, **kw)
