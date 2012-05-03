/* utility functions for a color framebuffer organized
   as one pixel per byte, with bits mapped as RRRGGGBB.
   This matches the SSD1783 LC Display Controller used
   on the Motorola C155 */

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
#include <fb/fb_rgb332.h>
#include <stdio.h>
#include <stdlib.h>

void
fb_rgb332_clear(){
	int i,n;
	
	/* bytes to clear */
	n = framebuffer->height * framebuffer->width;
	for(i=0;i<n;i++)
		fb_rgb332->mem[i]=0xff; /* white */

	/* mark everything as dirty */
	fb_rgb332->damage_x1 = 0;
	fb_rgb332->damage_x2 = framebuffer->width;
	fb_rgb332->damage_y1 = 0;
	fb_rgb332->damage_y2 = framebuffer->height;
}

/* update damage rectangle to include the area
   x1,y1 (upper left) to x2,y2 (lower right)
   Note that all pixels *including* x1y2 and x2y2 are
   marked as dirty */
static void
fb_rgb332_update_damage(
	uint16_t x1,uint16_t y1, /* left upper corner (inclusive) */
	uint16_t x2,uint16_t y2  /* right lower corner (inclusive) */
){
	fb_sanitize_box(&x1,&y1,&x2,&y2);
	
	x2++; /* see definition of fb_rgb332->damage_x2/y2 */
	y2++;

	/* maybe currently everything is clean? */
	if(fb_rgb332->damage_x1 == fb_rgb332->damage_x2 ||
	   fb_rgb332->damage_y1 == fb_rgb332->damage_y2
	){
			fb_rgb332->damage_x1 = x1;
			fb_rgb332->damage_y1 = y1;
			fb_rgb332->damage_x2 = x2;
			fb_rgb332->damage_y2 = y2;
			return;
	}

	/* grow damage box */
	if(x1 < fb_rgb332->damage_x1)
		fb_rgb332->damage_x1 = x1;
	if(y1 < fb_rgb332->damage_y1)
		fb_rgb332->damage_y1 = y1;
	if(x2 > fb_rgb332->damage_x2)
		fb_rgb332->damage_x2 = x2;
	if(y2 > fb_rgb332->damage_y2)
		fb_rgb332->damage_y2 = y2;
#if 0
	printf("%s: damage now %d %d %d %d\n",
	       __FUNCTION__,fb_rgb332->damage_x1,fb_rgb332->damage_y1,
	       fb_rgb332->damage_x2,fb_rgb332->damage_y2);
#endif
}

/* we trust gcc to move this expensive bitshifting out of
   the loops in the drawing funtcions */
static uint8_t rgb_to_pixel(uint32_t color){
	uint8_t ret;
	ret  = (FB_COLOR_TO_R(color) & 0xe0);      /* 765 = RRR */
	ret |= (FB_COLOR_TO_G(color) & 0xe0) >> 3; /* 432 = GGG */
	ret |= (FB_COLOR_TO_B(color) & 0xc0) >> 6; /*  10 =  BB */
	return ret;
}

static void set_pix(uint8_t *pixel,uint32_t color){
	if(color == FB_COLOR_TRANSP)
		return;
	*pixel = rgb_to_pixel(color);
}

static void set_fg(uint8_t *pixel){
	set_pix(pixel,framebuffer->fg_color);
}

static void set_bg(uint8_t *pixel){
	set_pix(pixel,framebuffer->bg_color);
}

void fb_rgb332_boxto(uint16_t x2,uint16_t y2)
{
	uint16_t x1 = framebuffer->cursor_x;
	uint16_t y1 = framebuffer->cursor_y;
	int x,y;
	uint8_t *p;

	framebuffer->cursor_x = x2;
	framebuffer->cursor_y = y2;
	
	fb_sanitize_box(&x1,&y1,&x2,&y2);
	fb_rgb332_update_damage(x1,y1,x2,y2);

	for(y=y1; y<=y2; y++){
		p = & fb_rgb332->mem[x1 + framebuffer->width * y];
		for(x=x1;x<=x2;x++){
			set_bg(p);
			if(y==y1 || y==y2 || x==x1 || x==x2) /* border */
				set_fg(p);
			p++;
		}
	}
}

/* draw a line like Brensenham did... (roughly) */
void fb_rgb332_lineto(uint16_t x2,uint16_t y2){
	uint8_t *p,pixel;	/* framebuffer pointer */
	int delta_regular;	/* framebuffer offset per step */
	int delta_step;		/* " */

	uint16_t x1 = framebuffer->cursor_x; /* start */
	uint16_t y1 = framebuffer->cursor_y;

	int t,tmax;		/* counter for steps */
	int err_inc,err_accu=0;	/* error delta and accumulator for */
				/* Brensenham's algorhithm */

	fb_limit_fb_range(&x1,&y1);
	fb_limit_fb_range(&x2,&y2);
	fb_rgb332_update_damage(x1,y1,x2,y2);

	framebuffer->cursor_x = x2; /* end pixel */
	framebuffer->cursor_y = y2;

	/* pointer to first pixel, pixel value in FB memory */
	p = fb_rgb332->mem + framebuffer->width * y1 + x1;
	pixel = rgb_to_pixel(framebuffer->fg_color);

	if(abs(x2-x1) >= abs(y2-y1)){ /* shallow line */
		/* set pointer deltas for directions */
		delta_regular = 1;		    /* X */
		if(x2 < x1)
			delta_regular = -delta_regular;
		delta_step = framebuffer->width;    /* Y */
		if(y2 < y1)
			delta_step = -delta_step;
		tmax = abs(x2-x1);
		err_inc = abs(y2-y1);
	} else { /* steep line */
		delta_regular = framebuffer->width; /* Y */
		if(y2 < y1)
			delta_regular = -delta_regular;
		delta_step = 1; 		    /* X */
		if(x2 < x1)
			delta_step = -1;
		tmax = abs(y2-y1);
		err_inc = abs(x2-y1);
	}

#if 0
	printf("%s: (%d,%d) -> (%d,%d) step=%d regular=%d err_inc=%d tmax=%d\n",
	       __FUNCTION__,x1,y1,x2,y2,delta_step,delta_regular,err_inc,tmax);
#endif

	for(t=0;t<=tmax;t++){
		*p = pixel;
		err_accu += err_inc;
		if(err_accu >= tmax){
			p += delta_step;
			err_accu -= tmax;
		}
		p += delta_regular;
	}
}

int fb_rgb332_putstr(char *str,int maxwidth){
	const struct fb_font *font = fb_fonts[framebuffer->font];
	const struct fb_char *fchr;

	int x1,y1,x2,y2; 		// will become bounding box
	int y;				// coordinates in display
	int char_x=0,char_y;		// coordinates in font character
	int bitmap_x,bitmap_y;		// coordinates in character's bitmap
	int byte_per_line;		// depending on character width in font
	int bitmap_offs,bitmap_bit;	// offset inside bitmap, bit number of pixel
	uint8_t *p,fgpixel,bgpixel,trans; // pointer into framebuffer memory
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
		else
			framebuffer->cursor_x = 1;
		maxwidth = framebuffer->width;
	}

	x1 = framebuffer->cursor_x;	// first col (incl!)
	x2 = x1 + maxwidth - 1;		// last col (incl!)
	if(x2 >= framebuffer->width)
		x2 = framebuffer->width - 1;

	y1 = framebuffer->cursor_y - font->ascent + 1; // first row
	y2 = y1 + font->height - 1;	// last row

	fgpixel = rgb_to_pixel(framebuffer->fg_color);
	bgpixel = rgb_to_pixel(framebuffer->bg_color);
	trans = (framebuffer->bg_color == FB_COLOR_TRANSP);

	if(y1 < 0)			// sanitize in case of overflow
		y1 = 0;
	if(y2 >= framebuffer->height)
		y2 = framebuffer->height - 1;

	/* iterate over all characters */
	for(;*str && framebuffer->cursor_x <= x2;str++){
		fchr = fb_font_get_char(font,*str);
		if(!fchr)  /* FIXME: Does '?' exist in every font? */
			fchr = fb_font_get_char(font,'?');
		if(!fchr)
			return 0;
		byte_per_line = (fchr->bbox_w+7)/8;

		for(y=y1;y<=y2;y++){
			p=fb_rgb332->mem+y*framebuffer->width;
			p+=framebuffer->cursor_x;

			for(char_x=0;
			    char_x<fchr->width &&
			    char_x+framebuffer->cursor_x <= x2;
			    char_x++
			){
				/* bitmap coordinates, X= left to right */
				bitmap_x = char_x - fchr->bbox_x;
				/* character coords. Y increases from
				   cursor upwards */
				char_y = framebuffer->cursor_y-y;
				/* bitmap index = height-(bitmap coords)-1 */
				bitmap_y = fchr->bbox_h -
					(char_y - fchr->bbox_y) - 1;

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
				bitmap_offs=bitmap_x/8+bitmap_y*byte_per_line;
				bitmap_bit=7-(bitmap_x%8);

				/* bit is set  */
				if(fchr->data[bitmap_offs]&(1<<bitmap_bit)){
					*p = fgpixel;
				} else { // unset, or outside bitmap
outside_char_bitmap:
					if (!trans)
						*p = bgpixel;
				}
				p++;
			} // for(x...)
		} // for(char_x...)
		framebuffer->cursor_x += char_x;
	} // str

	x2 = framebuffer->cursor_x;
	fb_rgb332_update_damage(x1,y1,x2,y2);
	return x2-x1;
}

