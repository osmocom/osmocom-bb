#!/usr/bin/python
# -*- coding: utf-8 -*-

'''
This script converts a bdf-font to a c-source-file containing
selected glyphs in the format defined by the <fb/font.h> header.
'''

# (C) 2010 by Christian Vogel <vogelchr@vogel.cx>
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

from optparse import OptionParser
import sys
import os

def unique_name(thisname,existingnames) :
	# return first of thisname, thisname_1, thisname_2, ...
	# that does not yet exist in existingnames. This is used
	# because somethings glyphs with non-unique names exist
	# in fonts!
	retname=thisname
	N=1
	while retname in existingnames :
		N=N+1
		retname='%s_%d'%(thisname,N)
	return retname


# return number N (for a character), optionally including
# the ascii character
def ascii_charnum(n) :
	if n >= 32 and n < 127 :
		if n != 34 : # """ looks stupid
			return '(%d, ASCII "%s")'%(n,chr(n))
		else :
			return '(%d, ASCII \'%s\')'%(n,chr(n))
	return '(%d)'%(n)

def is_zeroes(s) :
	# check if list s consists entirely of "00" strings
	# (used to detect empty lines in fonts)
	for x in s :
		if s != '00' :
			return False
	return True

def byte_to_bits(x) :
	# convert byte x to a string representing the bits #=1, .=0
	# used for drawing pretty pictures in the comments of the
	# generated C-file
	ret = ''
	for i in range(8) :
		if x & 1<<(7-i) :
			ret = ret + '#'
		else :
			ret = ret + '.'
	return ret

class BDF_Font(object) :
	# this class stores a read-in bdf font
	def __init__(self,filename) :
		self.filename = filename
		self.glyphs = dict()
		self.enc = dict()
		self.height = None
		self.registry = None
		self.encoding = None
		self.ascent = 0
		self.descent = 0
		self.read_font(filename)

	def add_header(self,data) :
		#print 'Header data: ',data
		self.registry = data.get('charset_registry','none')
		self.encoding = data.get('charset_encoding','unknown')
		self.ascent = int(data['font_ascent'])
		self.descent = int(data['font_descent'])
		bbx = data['fontboundingbox'].split(None,3)
		self.height = int(bbx[1])

	def add_glyph(self,charname,data,bitmap) :
		chnum = int(data['encoding'])
#		print 'add_glyph(%s) -> %s'%(charname,ascii_charnum(chnum))
		self.enc[chnum] = charname
		self.glyphs[charname] = data
		self.glyphs[charname]['bitmap']=bitmap

	def read_font(self,filename) :
		f = file(filename)

		hdr_data = dict()
		# read in header
		for l in f :
			l = l.strip()
			if l == '' :
				continue
			arr = l.split(None,1)
			if len(arr) > 1 :
				hdr_data[ arr[0].lower() ] = arr[1]
			if arr[0].lower() == 'chars' :
				break

		self.add_header(hdr_data)

		# now read in characters
		inchar = None
		data = dict() # store glyph data
		bitmap = None
		for l in f :
			l = l.strip()
			if l == '' :
				continue

			# waiting for next glyph
			if inchar == None :
				if l.lower() == 'endfont' :
					break # end of font :-)
				arr = l.split(None,1)
				if len(arr) < 2 and \
				    arr[0].lower() != 'STARTCHAR' :
					print >>sys.stderr,'Not start of glyph: %s'%(l)
					continue
				inchar = unique_name(arr[1],self.glyphs)
				continue

			# ENDCHAR always ends the glyph
			if l.lower() == 'endchar' :
				self.add_glyph(inchar,data,bitmap)
				inchar = None
				bitmap = None
				data = dict()
				continue

			# in bitmap
			if bitmap != None :
				bitmap.append(l)
				continue

			# else: metadata for this glyph
			arr = l.split(None,1)

			if arr[0].lower() == 'bitmap' :
				bitmap = list() # start collecting pixels
				continue

			if len(arr) < 2 :
				print >>sys.stderr,'Bad line in font: %s'%(l)
				continue
			data[arr[0].lower()] = arr[1]

if __name__ == '__main__' :
	P = OptionParser(usage='%prog [options] bdf-file')
	P.add_option('-o','--out',action='store', dest='out', default=None,
		metavar='FILE',help='write .c-code representing font to FILE')
	P.add_option('-b','--base',action='store',dest='base',default=None,
		metavar='base_symbol',help='prefix for all generated symbols')
	P.add_option('-f','--firstchar',action='store',dest='firstchar',type="int",
		metavar='N',default=None,help='numeric value of first char')
	P.add_option('-l','--lastchar',action='store',dest='lastchar',type="int",
		metavar='N',default=None,help='numeric value of last char')

	opts,args = P.parse_args()

	if len(args) != 1 :
		P.error('Please specify (exactly one) bdf input file.')

	font = BDF_Font(args[0])

	if opts.firstchar == None :
		opts.firstchar = min(font.enc)
		print 'First character in font: %d, %s'%(opts.firstchar,
			font.enc[opts.firstchar])

	if opts.lastchar == None :
		opts.lastchar = max(font.enc)
		print 'Last character in font: %d, %s'%(opts.lastchar,
			font.enc[opts.lastchar])

	if opts.base == None :
		opts.base = 'font_'+os.path.basename(args[0])
		if opts.base[-4:] == '.bdf' :
			opts.base = opts.base[:-4]
		print >>sys.stderr,'Guessing symbol prefix to be %s.'%(opts.base)

	if opts.out == None :
		opts.out = os.path.basename(args[0])
		if opts.out[-4:] == '.bdf' :
			opts.out = opts.out[:-4]
		opts.out = opts.out + '.c'
		print >>sys.stderr,'Guessing output filename to be %s.'%(opts.out)

		if os.path.exists(opts.out) :
			print >>sys.stderr,'Will *NOT* overwrite existing file when guessing output!'
			sys.exit(1)

	of = file(opts.out,'w')
	
	print >>of,'#include <fb/font.h>'
	print >>of,'/* file autogenerated by %s */'%(sys.argv[0])

	offsets = list()
	glyphnames = list()

	print >>of,'static const uint8_t %s_data[] = {'%(opts.base)

	pos = 0

	# output font data, build up per-character information

	for i in range(opts.firstchar,opts.lastchar+1) :
		if not i in font.enc :
			offsets.append(0xffff)
			glyphnames.append('(no glyph)')
			continue

		charname = font.enc[i]
		glyphnames.append('%s %s'%(charname,ascii_charnum(i)))
		offsets.append(pos)
		glyph = font.glyphs[charname]
		bbx = map(int,glyph['bbx'].split(None,3))
		bitmap = glyph['bitmap']

		if bbx[1] != len(bitmap) :
			print >>sys.stderr,'ERROR: glyph',charname,'has wrong number of lines of data!'
			print >>sys.stderr,' want: ',bbx[1],'but have',len(bitmap)
			sys.exit(1)

		removedrows = 0

		while len(bitmap) > 1 and is_zeroes(bitmap[0]) :
			removedrows = removedrows + 1
			bbx[1] = bbx[1] - 1 # decrease height
			bitmap = bitmap[1:]

		while len(bitmap) > 1 and is_zeroes(bitmap[-1]) :
			removedrows = removedrows + 1
			bbx[1] = bbx[1] - 1  # decrease height
			bbx[3] = bbx[3] + 1  # increase y0
			bitmap = bitmap[:-1]

		if removedrows > 0 :
			print "Glyph %s: removed %d rows."%(charname,removedrows)

		w = int(glyph['dwidth'].split(None,1)[0])

		print >>of,'/* --- new character %s %s starting at offset 0x%04x --- */'%(
				charname,ascii_charnum(i),pos)
		print >>of,'\t/*%04x:*/\t%d, %d, %d, %d, %d, /* width and bbox (w,h,x,y) */'%(
			pos,w,bbx[0],bbx[1],bbx[2],bbx[3])

		pos += 5

		for k,l in enumerate(bitmap) :
			bytes = [ int(l[i:i+2],16) for i in range(0,len(l),2) ]
			if len(bytes) != (bbx[0]+7)/8 :
				print >>sys.stderr,'ERROR: glyph',charname,'has wrong # of bytes'
				print >>sys.stderr,' per line. Want',(bbx[0]+7)/8,'have',len(bytes)
				sys.exit(1)
			cdata = ','.join([ '0x%02x'%v for v in bytes ])
			comment = ''.join([ byte_to_bits(b) for b in bytes ])
			print >>of,'\t/*%04x:*/\t'%(pos)+cdata+',  /* '+comment+' */'
			pos += len(bytes)

	print >>of,"};"

	x = ',\n\t'.join(['0x%04x /* %s */'%(w,n) for w,n in zip(offsets,glyphnames)])
	print >>of,'static const uint16_t %s_offsets[] = {\n\t%s\n};'%(opts.base,x)

	height = font.ascent + font.descent

	print >>of,'const struct fb_font %s = {'%(opts.base)
	print >>of,'\t.height = %d,'%(height)
	print >>of,'\t.ascent = %d,'%(font.ascent)
	print >>of,'\t.firstchar = %d, /* %s */'%(opts.firstchar,font.enc.get(opts.firstchar,"?"))
	print >>of,'\t.lastchar = %d, /* %s */'%(opts.lastchar,font.enc.get(opts.lastchar,"?"))
	print >>of,'\t.chardata = %s_data,'%(opts.base)
	print >>of,'\t.charoffs = %s_offsets,'%(opts.base)
	print >>of,'};'
