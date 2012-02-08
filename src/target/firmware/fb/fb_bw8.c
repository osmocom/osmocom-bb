/* utility functions for a black-and-white framebuffer organized
   as 8-vertically-stacked-pixels per byte. This matches the
   ST7558 LC Display Controller used on the Motorola C123 */

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

#include <fb/framebuffer.h>
#include <fb/fb_bw8.h>

#include <stdio.h> // debugging

void fb_bw8_clear(){
	int i,n;

	/* bytes to clear */
	n = (framebuffer->height+7)/8 * framebuffer->width;
	for(i=0;i<n;i++)
		fb_bw8->mem[i]=0;

	/* mark everything as dirty */
	fb_bw8->damage_x1 = 0;
	fb_bw8->damage_x2 = framebuffer->width;
	fb_bw8->damage_y1 = 0;
	fb_bw8->damage_y2 = framebuffer->height;
}

/* update damage rectangle to include the area
   x1,y1 (upper left) to x2,y2 (lower right)
   Note that all pixels *including* x1y2 and x2y2 are
   marked as dirty */
static void fb_bw8_update_damage(
	uint16_t x1,uint16_t y1, /* left upper corner (inclusive) */
	uint16_t x2,uint16_t y2  /* right lower corner (inclusive) */
){
	fb_sanitize_box(&x1,&y1,&x2,&y2);
	
	x2++; /* see definition of fb_bw8->damage_x2/y2 */
	y2++;

	/* maybe currently everything is clean? */
	if(fb_bw8->damage_x1 == fb_bw8->damage_x2 ||
		fb_bw8->damage_y1 == fb_bw8->damage_y2){
			fb_bw8->damage_x1 = x1;
			fb_bw8->damage_y1 = y1;
			fb_bw8->damage_x2 = x2;
			fb_bw8->damage_y2 = y2;
/*
		printf("%s: was clean! damage now %d %d %d %d\n",
			__FUNCTION__,fb_bw8->damage_x1,fb_bw8->damage_y1,
			fb_bw8->damage_x2,fb_bw8->damage_y2);
*/
			return;
	}

	/* grow damage box */
	if(x1 < fb_bw8->damage_x1)
		fb_bw8->damage_x1 = x1;
	if(y1 < fb_bw8->damage_y1)
		fb_bw8->damage_y1 = y1;
	if(x2 > fb_bw8->damage_x2)
		fb_bw8->damage_x2 = x2;
	if(y2 > fb_bw8->damage_y2)
		fb_bw8->damage_y2 = y2;
#if 0
	printf("%s: damage now %d %d %d %d\n",
	       __FUNCTION__,fb_bw8->damage_x1,fb_bw8->damage_y1,
	       fb_bw8->damage_x2,fb_bw8->damage_y2);
#endif
}

static void fb_bw8_line(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2){
	fb_sanitize_box(&x1,&y1,&x2,&y2);
	/* FIXME : this is currently unimplemented! */
}

void fb_bw8_lineto(uint16_t x,uint16_t y){
	fb_bw8_line(framebuffer->cursor_x,framebuffer->cursor_y,x,y);
	framebuffer->cursor_x = x;
	framebuffer->cursor_y = y;	
}

/* depending on color set (add to or_mask) or clear
   (remove from and_mask) bit number bitnum */
static void set_pixel(uint8_t *and_mask,
		      uint8_t *or_mask,
		      int bitnum,
		      uint32_t color
){
	if(color == FB_COLOR_TRANSP)
		return;
	if(color == FB_COLOR_WHITE)
		*and_mask &= ~(1<<bitnum);
	else
		*or_mask  |=   1<<bitnum;
}

static void set_fg_pixel(uint8_t *and_mask,uint8_t *or_mask,int bitnum){
	set_pixel(and_mask,or_mask,bitnum,framebuffer->fg_color);
}

static void set_bg_pixel(uint8_t *and_mask,uint8_t *or_mask,int bitnum){
	set_pixel(and_mask,or_mask,bitnum,framebuffer->bg_color);
}

static void fb_bw8_box(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
{
	uint16_t y,w;
	uint8_t *p;

	uint8_t and_mask,or_mask;	// filling
	uint8_t and_mask_side,or_mask_side; // left and right side

	fb_sanitize_box(&x1,&y1,&x2,&y2);
	fb_bw8_update_damage(x1,y1,x2,y2);

	for(y=y1&0xfff8;y<=y2;y+=8){
		/* don't clear any pixels (white) */
		and_mask = and_mask_side = 0xff;
		or_mask = or_mask_side = 0;

		for(w=0;w<8;w++){ /* check which pixels are affected */
			if(y+w >= y1 && y+w <= y2){
				set_bg_pixel(&and_mask,&or_mask,w);
				set_fg_pixel(&and_mask_side,&or_mask_side,w);
			}

			if(y+w == y1 || y+w == y2){ /* top and bottom line */
				set_fg_pixel(&and_mask,&or_mask,w);
			}
		}

		p = fb_bw8->mem + (y/8)*framebuffer->width + x1;
		for(w=x1;w<=x2;w++){
			if(w == x1 || w == x2)
				*p = (*p & and_mask_side)|or_mask_side;
			else
				*p = (*p & and_mask)|or_mask;
			p++;
		}
	}
}

/* draw box from cursor to (x,y) */
void
fb_bw8_boxto(uint16_t x,uint16_t y){
	fb_bw8_box(framebuffer->cursor_x,framebuffer->cursor_y,x,y);
	framebuffer->cursor_x = x;
	framebuffer->cursor_y = y;
}

/* this is the most ridiculous function ever, because it has to
   fiddle with two braindead bitmaps at once, both being
   organized differently */

/* draw text at current position, with current font and colours up
   to a width of maxwidth pixels, return pixelwidth consumed */

int
fb_bw8_putstr(char *str,int maxwidth){
	const struct fb_font *font = fb_fonts[framebuffer->font];
	const struct fb_char *fchr;

	int x1,y1,x2,y2; 		// will become bounding box
	int w;				// 0..7 while building bits per byte
	int y;				// coordinates in display
	int char_x,char_y;		// coordinates in font character
	int bitmap_x,bitmap_y;		// coordinates in character's bitmap
	int byte_per_line;		// depending on character width in font
	int bitmap_offs,bitmap_bit;	// offset inside bitmap, bit number of pixel
	int fb8_offs;			// offset to current pixel in framebuffer
	uint8_t and_mask,or_mask;	// to draw on framebuffer
	uint8_t *p;			// pointer into framebuffer memorya
	int total_w;			// total width

	/* center, if maxwidth < 0 */
	if (maxwidth < 0) {
		total_w = 0;
		/* count width of string */
		for(p=(uint8_t *)str;*p;p++){
			fchr = fb_font_get_char(font,*p);
			if(!fchr)  /* FIXME: Does '?' exist in every font? */
				fchr = fb_font_get_char(font,'?');
			total_w += fchr->width;

		} // str
		if (total_w <= framebuffer->width)
			framebuffer->cursor_x =
				(framebuffer->width - total_w) >> 1;
		maxwidth = framebuffer->width;
	}

	x1 = framebuffer->cursor_x;	// first col (incl!)
	x2 = x1 + maxwidth - 1;		// last col (incl!)
	if(x2 >= framebuffer->width)
		x2 = framebuffer->width - 1;

	y1 = framebuffer->cursor_y - font->ascent + 1; // first row
	y2 = y1 + font->height - 1;	// last row

#if 0
	printf("%s: %d %d %d %d\n",__FUNCTION__,x1,y1,x2,y2);
#endif

	if(y1 < 0)			// sanitize in case of overflow
		y1 = 0;
	if(y2 >= framebuffer->height)
		y2 = framebuffer->height - 1;

	fb8_offs = x1 + (y1 & 0xfff8)/8;

	/* iterate over all characters */
	for(;*str && framebuffer->cursor_x <= x2;str++){
		fchr = fb_font_get_char(font,*str);
		if(!fchr)  /* FIXME: Does '?' exist in every font? */
			fchr = fb_font_get_char(font,'?');

		byte_per_line = (fchr->bbox_w+7)/8;;

		/* character pixels, left to right */
		for(char_x=0;
		    char_x<fchr->width && char_x + framebuffer->cursor_x <= x2;
		    char_x++
		){
			/* character pixels, top to bottom, in stripes
			   of 8 to match LCD RAM organisation */
			for(y=y1&0xfff8;y<=y2;y+=8){ // display lines
				/* bitmap coordinates, X= left to right */
				bitmap_x = char_x - fchr->bbox_x;
				/* character coords. Y increases from
				   cursor upwards */
				char_y = framebuffer->cursor_y-y;
				/* bitmap index = height-(bitmap coords)-1 */
				bitmap_y = fchr->bbox_h -
					(char_y - fchr->bbox_y) - 1;

				fb8_offs = framebuffer->cursor_x + 
					char_x + (y/8)*framebuffer->width;

				and_mask = 0xff;
				or_mask = 0x00;

				/* top to bottom inside of a 8bit column */
				for(w=0;w<8;w++,bitmap_y++){
					/* inside drawing area? */
					if(y+w < y1 || y+w > y2)
						continue;

					/* outside pixel data of this
					   character? */
					if(bitmap_x < 0 ||
					   bitmap_x >= fchr->bbox_w ||
					   bitmap_y < 0 ||
					   bitmap_y >= fchr->bbox_h
					)
						goto outside_char_bitmap;

					/* check bit in pixel data for
					   this character */
					bitmap_offs = bitmap_x/8+
						bitmap_y*byte_per_line;
					bitmap_bit = 7-(bitmap_x%8);

					/* bit is set  */
					if(fchr->data[bitmap_offs] &
					   (1<<bitmap_bit)){
						set_fg_pixel(&and_mask,
							     &or_mask,w);
					} else { // unset, or outside bitmap
outside_char_bitmap:
						set_bg_pixel(&and_mask,
							     &or_mask,w);
					}
				} // for(w...)
				/* adjust byte in framebuffer */
				p = fb_bw8->mem + fb8_offs;
				*p = ( *p & and_mask ) | or_mask;
			} // for(y...)
		} // for(char_x...)
		framebuffer->cursor_x += char_x;
	} // str

	x2 = framebuffer->cursor_x;
	fb_bw8_update_damage(x1,y1,x2,y2);
	return x2-x1;
}

int
fb_bw8_putchar(char c,int maxwidth){
	char tmp[2];
	tmp[0]=c;
	tmp[1]=c;
	return fb_bw8_putstr(tmp,maxwidth);
}
