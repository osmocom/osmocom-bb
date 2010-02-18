#!/usr/bin/env python

import struct
import sys

def group_by_n(s, n, do_join=True):
	return ( ''.join(x) for x in zip(*[s[i::n] for i in range(n)]) )
	

def main(pn, filename):
	# Get all bytes
	f = open(filename, 'r')
	d = f.read()
	f.close()

	# Get the data
	ops = ''.join([
		'0x%04x,%c' % (
			struct.unpack('=H', x)[0],
			'\n' if (i&3==3) else ' '
		)
		for i, x
		in enumerate(group_by_n(d, 2))
	])[:-1]

	# Header / footer
	print """
#define _SA_DECL (const uint16_t *)&(const uint16_t [])

static const struct dsp_section dsp_xxx[] = {
	{
		.addr = 0x,
		.size = 0x%04x,
		.data = _SA_DECL {
%s
		},
	},
	{ /* Guard */
		.addr = 0,
		.size = 0,
		.data = NULL,
	},
};

#undef _SA_DECL
""" % (len(d)/2, ops)


if __name__ == "__main__":
	main(*sys.argv)
