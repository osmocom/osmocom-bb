#!/usr/bin/env python

import re
import sys
import struct

DATA = 0
DATA = 1


class Section(object):

	DATA		= 0
	CODE		= 1

	STYP_NOLOAD	= 0x0002
	STYP_TEXT	= 0x0020
	STYP_DATA	= 0x0040
	STYP_BSS	= 0x0080

	def __init__(self, name, type, start, size, data=None):
		self.name  = name
		self.type  = type
		self.start = start
		self.size  = size
		self.data  = data

	@property
	def flags(self):
		if self.type == Section.DATA:
			return Section.STYP_DATA if self.data else Section.STYP_BSS
		else:
			return Section.STYP_TEXT if self.data else Section.STYP_NOLOAD


class CalypsoCOFF(object):

	F_RELFLG	= 0x0001 # Relocation information stripped from the file
	F_EXEC		= 0x0002 # File is executable (i.e., no unresolved external references)
	F_LNNO		= 0x0004 # Line numbers stripped from the file
	F_LSYMS		= 0x0010 # Local symbols stripped from the file
	F_LITTLE	= 0x0100 # Little endian

	def __init__(self, data_seg_base=0x80000000):
		self.sections = {}
		self.data_seg_base = data_seg_base
		self.ver_magic = 0x00c1
		self.tgt_magic = 0x0098
		self.flags = \
			CalypsoCOFF.F_RELFLG	|	\
			CalypsoCOFF.F_EXEC		|	\
			CalypsoCOFF.F_LNNO		|	\
			CalypsoCOFF.F_LSYMS		|	\
			CalypsoCOFF.F_LITTLE

	def _data_pack(self, d):
		return ''.join(struct.pack('<H', x) for x in d)

	def save(self, filename):
		# Formats
		HDR_FILE = '<HHlllHHH'
		HDR_SECTIONS = '<8sLLllllHHHcc'

		# Optional header
		oh = ''

		# File header
		fh = struct.pack(HDR_FILE,
			self.ver_magic,		# unsigned short  f_ver_magic; /* version magic number */
			len(self.sections),	# unsigned short  f_nscns;     /* number of section */
			0,					# long            f_timdat;    /* time and date stamp */
			0,					# long            f_symptr;    /* file ptr to symbol table */
			0,					# long            f_nsyms;     /* number entries in the sym table */
			len(oh),			# unsigned short  f_opthdr;    /* size of optional header */
			self.flags,			# unsigned short  f_flags;     /* flags */
			self.tgt_magic,		# unsigned short  f_tgt_magic; /* target magic number */
		)

		# File header size  + #sections * sizeof(section header)
		dptr = struct.calcsize(HDR_FILE) + len(oh) + len(self.sections) * struct.calcsize(HDR_SECTIONS)

		# Section headers
		sh = []
		sd = []

		sk = lambda x: self.data_seg_base + x.start if x.type==Section.DATA else x.start

		for s in sorted(self.sections.values(), key=sk):
			# Values
			if s.type == Section.DATA:
				mp = 0x80
				sa = s.start
			else:
				mp = 0
				sa = s.start
			sptr = dptr if s.data else 0

			# Header
			sh.append(struct.pack(HDR_SECTIONS,
				s.name,	# char[8]        s_name;   /* 8-character null padded section name */
				sa,		# long int       s_paddr;  /* Physical address of section */
				sa,		# long int       s_vaddr;  /* Virtual address of section */
				s.size,	# long int       s_size;   /* Section size in bytes */
				sptr,	# long int       s_scnptr; /* File pointer to raw data */
				0,		# long int       s_relptr; /* File pointer to relocation entries */
				0,		# long int       s_lnnoptr;/* File pointer to line number entries */
				0,		# unsigned short s_nreloc; /* Number of relocation entrie */
				0,		# unsigned short s_nlnno;  /* Number of line number entries */
				s.flags,# unsigned short s_flags;  /* Flags (see ``Section header flags'') */
				'\x00',	# /
				chr(mp),# char           s_mempage;/* Memory page number */
			))

			# Data
			if s.data:
				sd.append(self._data_pack(s.data))
				dptr += s.size * 2

		# Write the thing
		f = open(filename, 'wb')

		f.write(fh)
		f.write(oh)
		f.write(''.join(sh))
		f.write(''.join(sd))

		f.close()

	def add_section(self, name, type, addr, size, data=None):
		self.sections[name] = Section(name, type, addr, size, data=data)


# ----------------------------------------------------------------------------
# Dump loading
# ----------------------------------------------------------------------------

RE_DUMP_HDR = re.compile(
	r"^DSP dump: (\w*) \[([0-9a-fA-F]{5})-([0-9a-fA-F]{5})\]$"
)


def _file_strip_gen(f):
	while True:
		l = f.readline()
		if not l:
			return
		yield l.strip()


def dump_load_section(fg, sa, ea):
	data = []
	ca = sa
	for l in fg:
		if not l:
			break

		ra = int(l[0:5], 16)
		if ra != ca:
			raise ValueError('Invalid dump address %05x != %05x', ra, ca)

		v = l[8:].split()
		if len(v) != 16:
			raise ValueError('Invalid dump format')

		v = [int(x,16) for x in v]
		data.extend(v)

		ca += 0x10

	if ca != ea:
		raise ValueError('Missing dump data %05x != %05x', ra, ea)

	return data


def dump_load(filename):
	# Open file
	f = open(filename, 'r')
	fg = _file_strip_gen(f)

	# Scan line by line for a dump header line
	sections = []

	for l in fg:
		m = RE_DUMP_HDR.match(l)
		if not m:
			continue

		name = m.group(1)
		sa   = int(m.group(2), 16)
		ea   = int(m.group(3), 16) + 1

		sections.append((
			name, sa, ea,
			dump_load_section(fg, sa, ea),
		))

	# Done
	f.close()

	return sections


# ----------------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------------

def main(pname, dump_filename, out_filename):

	# Section to place in the COFF
	sections = [
		# name			type          start    size
		('.regs',		Section.DATA, 0x00000, 0x0060),
		('.scratch',	Section.DATA, 0x00060, 0x0020),
		('.drom',		Section.DATA, 0x09000, 0x5000),
		('.pdrom',		Section.CODE, 0x0e000, 0x2000),
		('.prom0',		Section.CODE, 0x07000, 0x7000),
		('.prom1',		Section.CODE, 0x18000, 0x8000),
		('.prom2',		Section.CODE, 0x28000, 0x8000),
		('.prom3',		Section.CODE, 0x38000, 0x2000),
		('.daram0',		Section.DATA, 0x00080, 0x0780),
		('.api',		Section.DATA, 0x00800, 0x2000),
		('.daram1',		Section.DATA, 0x02800, 0x4800),
	]

	# COFF name -> dump name
	dump_mapping = {
		# '.regs' :	'Registers',
		'.drom' :	'DROM',
		'.pdrom' :	'PDROM',
		'.prom0' :	'PROM0',
		'.prom1' :	'PROM1',
		'.prom2' :	'PROM2',
		'.prom3' :	'PROM3',
	}

	# Load the dump
	dump_sections = dict([(s[0], s) for s in dump_load(dump_filename)])

	# Create the COFF
	coff = CalypsoCOFF()

	# Add each section (with data if we have some)
	for name, type, start, size in sections:
		# Dumped data ?
		d_data = None
		if (name in dump_mapping) and (dump_mapping[name] in dump_sections):
			d_name, d_sa, d_ea, d_data = dump_sections[dump_mapping[name]]

		# Add sections
		coff.add_section(name, type, start, size, d_data)

	# Save result
	coff.save(out_filename)

	return 0


if __name__ == '__main__':
	sys.exit(main(*sys.argv))
