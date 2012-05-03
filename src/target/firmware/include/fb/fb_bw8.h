#ifndef FB_BW8_H
#define FB_BW8_H

/* 8bit monochrome framebuffer, organized with 8 stacked pixels
   per byte, backed by local memory. fb_bw8.c lists functions that
   are common to simmilar organized displays. */

/*
	Sketch of Memory Layout 
	Left Upper Corner of Display
     
			col0  col2
			   col1
		      +-------------
	1st row:      | A0 B0 C0
	2nd row:      | A1 B1 C1
			...
	7th row:      | A6 B6 C6
	8th row:      | A7 B7 C7
	9th row:      | Q0 R0 S0
	10th row:     | Q1 R1 S1 ...
			...

	Backing store (and internal display memory?) looks like...
	
	uint8_t mem[] = { A, B, C, .... Q, R, S, ... }

   We work on a in-memory copy of the framebuffer and only
   update the physical display on demand. The damage window
   has two corners, left upper inclusive x1,y1 and right
   lower x2,y2 exclusive. So dirty pixels are defined to
   be  x1 <= x_pixel < x2 and y1 <= y_pixel < y2.
*/

/* data specific to a bw8-type framebuffer as described above */

struct fb_bw8 {
	uint8_t *mem;			/* set to backingstore memory */
	uint16_t damage_x1,damage_y1;	/* current damage window, ul */
	uint16_t damage_x2,damage_y2;	/* current damage window, lr */
};

extern struct fb_bw8 *fb_bw8; /* symbol defined by the specific LCD driver */

extern void fb_bw8_clear();
extern void fb_bw8_boxto(uint16_t x,uint16_t y); /* draw a box from cursor to x,y */
extern void fb_bw8_lineto(uint16_t x,uint16_t y); /* draw a line from cursor to x,y */

extern int fb_bw8_putstr(char *str,int maxwidth);

#endif
