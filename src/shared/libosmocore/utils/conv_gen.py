#!/usr/bin/python2

mod_license = """
/*
 * Copyright (C) 2011-2016 Sylvain Munaut <tnt@246tNt.com>
 * Copyright (C) 2016 sysmocom s.f.m.c. GmbH
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
"""

import sys, os, math
from functools import reduce
import conv_codes_gsm

class ConvolutionalCode(object):

	def __init__(self, block_len, polys, name,
			description = None, puncture = [], term_type = None):
		# Save simple params
		self.block_len = block_len
		self.k = 1
		self.puncture = puncture
		self.rate_inv = len(polys)
		self.term_type = term_type

		# Infos
		self.name = name
		self.description = description

		# Handle polynomials (and check for recursion)
		self.polys = [(1, 1) if x[0] == x[1] else x for x in polys]

		# Determine the polynomial degree
		for (x, y) in polys:
			self.k = max(self.k, int(math.floor(math.log(max(x, y), 2))))
		self.k = self.k + 1

		self.poly_divider = 1
		rp = [x[1] for x in self.polys if x[1] != 1]
		if rp:
			if not all([x == rp[0] for x in rp]):
				raise ValueError("Bad polynomials: "
					"Can't have multiple different divider polynomials!")

			if not all([x[0] == 1 for x in polys if x[1] == 1]):
				raise ValueError("Bad polynomials: "
					"Can't have a '1' divider with a non '1' dividend "
					"in a recursive code")

			self.poly_divider = rp[0]

	@property
	def recursive(self):
		return self.poly_divider != 1

	@property
	def _state_mask(self):
		return (1 << (self.k - 1)) - 1

	def next_state(self, state, bit):
		nb = combine(
			(state << 1) | bit,
			self.poly_divider,
			self.k,
		)
		return ((state << 1) | nb) & self._state_mask

	def next_term_state(self, state):
		return (state << 1) & self._state_mask

	def next_output(self, state, bit, ns = None):
		# Next state bit
		if ns is None:
			ns = self.next_state(state, bit)

		src = (ns & 1) | (state << 1)

		# Scan polynomials
		rv = []
		for p_n, p_d in self.polys:
			if self.recursive and p_d == 1:
				# No choice ... (systematic output in recursive case)
				o = bit
			else:
				o = combine(src, p_n, self.k)
			rv.append(o)

		return rv

	def next_term_output(self, state, ns = None):
		# Next state bit
		if ns is None:
			ns = self.next_term_state(state)

		src = (ns & 1) | (state << 1)

		# Scan polynomials
		rv = []
		for p_n, p_d in self.polys:
			if self.recursive and p_d == 1:
				# Systematic output are replaced when in 'termination' mode
				o = combine(src, self.poly_divider, self.k)
			else:
				o = combine(src, p_n, self.k)
			rv.append(o)

		return rv

	def next(self, state, bit):
		ns = self.next_state(state, bit)
		nb = self.next_output(state, bit, ns = ns)
		return ns, nb

	def next_term(self, state):
		ns = self.next_term_state(state)
		nb = self.next_term_output(state, ns = ns)
		return ns, nb

	def _print_term(self, fi, num_states, pack = False):
		items = []

		for state in range(num_states):
			if pack:
				x = pack(self.next_term_output(state))
			else:
				x = self.next_term_state(state)

			items.append(x)

		# Up to 12 numbers should be placed per line
		print_formatted(items, "%3d, ", 12, fi)

	def _print_x(self, fi, num_states, pack = False):
		items = []

		for state in range(num_states):
			if pack:
				x0 = pack(self.next_output(state, 0))
				x1 = pack(self.next_output(state, 1))
			else:
				x0 = self.next_state(state, 0)
				x1 = self.next_state(state, 1)

			items.append((x0, x1))

		# Up to 4 blocks should be placed per line
		print_formatted(items, "{ %2d, %2d }, ", 4, fi)

	def _print_puncture(self, fi):
		# Up to 12 numbers should be placed per line
		print_formatted(self.puncture, "%3d, ", 12, fi)

	def print_state_and_output(self, fi):
		pack = lambda n: \
			sum([x << (self.rate_inv - i - 1) for i, x in enumerate(n)])
		num_states = 1 << (self.k - 1)

		fi.write("static const uint8_t %s_state[][2] = {\n" % self.name)
		self._print_x(fi, num_states)
		fi.write("};\n\n")

		fi.write("static const uint8_t %s_output[][2] = {\n" % self.name)
		self._print_x(fi, num_states, pack)
		fi.write("};\n\n")

		if self.recursive:
			fi.write("static const uint8_t %s_term_state[] = {\n" % self.name)
			self._print_term(fi, num_states)
			fi.write("};\n\n")

			fi.write("static const uint8_t %s_term_output[] = {\n" % self.name)
			self._print_term(fi, num_states, pack)
			fi.write("};\n\n")

	def gen_tables(self, pref, fi, shared_tables = None):
		# Do not print shared tables
		if shared_tables is None:
			self.print_state_and_output(fi)
			table_pref = self.name
		else:
			table_pref = shared_tables

		if len(self.puncture):
			fi.write("static const int %s_puncture[] = {\n" % self.name)
			self._print_puncture(fi)
			fi.write("};\n\n")

		# Write description as a multi-line comment
		if self.description is not None:
			fi.write("/**\n")
			for line in self.description:
				fi.write(" * %s\n" % line)
			fi.write(" */\n")

		# Print a final convolutional code definition
		fi.write("const struct osmo_conv_code %s_%s = {\n" % (pref, self.name))
		fi.write("\t.N = %d,\n" % self.rate_inv)
		fi.write("\t.K = %d,\n" % self.k)
		fi.write("\t.len = %d,\n" % self.block_len)
		fi.write("\t.next_output = %s_output,\n" % table_pref)
		fi.write("\t.next_state = %s_state,\n" % table_pref)

		if self.term_type is not None:
			fi.write("\t.term = %s,\n" % self.term_type)

		if self.recursive:
			fi.write("\t.next_term_output = %s_term_output,\n" % table_pref)
			fi.write("\t.next_term_state = %s_term_state,\n" % table_pref)

		if len(self.puncture):
			fi.write("\t.puncture = %s_puncture,\n" % self.name)
		fi.write("};\n\n")

poly = lambda *args: sum([(1 << x) for x in args])

def combine(src, sel, nb):
	x = src & sel
	fn_xor = lambda x, y: x ^ y
	return reduce(fn_xor, [(x >> n) & 1 for n in range(nb)])

def print_formatted(items, format, count, fi):
	counter = 0

	# Print initial indent
	fi.write("\t")

	for item in items:
		if counter > 0 and counter % count == 0:
			fi.write("\n\t")

		fi.write(format % item)
		counter += 1

	fi.write("\n")

def print_shared(fi, shared_polys):
	for (name, polys) in shared_polys.items():
		# HACK
		code = ConvolutionalCode(0, polys, name = name)
		code.print_state_and_output(fi)

def generate_codes(codes, path, prefix):
	# Open a new file for writing
	f = open(os.path.join(path, prefix + "_conv.c"), 'w')
	f.write(mod_license + "\n")
	f.write("#include <stdint.h>\n")
	f.write("#include <osmocom/core/conv.h>\n\n")

	# Print shared tables first
	if hasattr(codes, "shared_polys"):
		print_shared(f, codes.shared_polys)

	# Generate the tables one by one
	for code in codes.conv_codes:
		sys.stderr.write("Generate '%s' definition\n" % code.name)

		# Check whether shared polynomials are used
		shared = None
		if hasattr(codes, "shared_polys"):
			for (name, polys) in codes.shared_polys.items():
				if code.polys == polys:
					shared = name
					break

		code.gen_tables(prefix, f, shared_tables = shared)

if __name__ == '__main__':
	path = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()

	sys.stderr.write("Generating convolutional codes...\n")

	# Generate GSM specific codes
	generate_codes(conv_codes_gsm, path, "gsm0503")

	sys.stderr.write("Generation complete.\n")
