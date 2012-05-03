/* Font Handling - Utility Functions */

/* (C) 2010 by Christian Vogel <vogelchr@vogel.cx>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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
 *
 */

#include <fb/font.h>

/* what fonts are linked in? */
extern const struct fb_font font_4x6;
extern const struct fb_font font_5x8;
extern const struct fb_font font_helvR08;
extern const struct fb_font font_helvR14;
//extern const struct fb_font font_helvR24;
//extern const struct fb_font font_helvB08;
extern const struct fb_font font_helvB14;
// extern const struct fb_font font_helvB24;
extern const struct fb_font font_c64;
extern const struct fb_font font_symbols;

const struct fb_font *fb_fonts[]={
//	&font_4x6,
//	&font_5x8,
	&font_helvR08,
//	&font_helvR14,
//	&font_helvR24,
//	&font_helvB08,
	&font_helvB14,
//	&font_helvB24,
	&font_c64,
	&font_symbols,
};

const struct fb_char *
fb_font_get_char(const struct fb_font *fnt,unsigned char c){
	if(c < fnt->firstchar || c > fnt->lastchar)
		return NULL;
	uint16_t offs = fnt->charoffs[c-fnt->firstchar];
	if(offs == FB_FONT_NOCHAR)
		return NULL;
	return (struct fb_char *)(fnt->chardata + offs);
}

