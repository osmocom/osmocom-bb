#ifndef FB_RGB332_H
#define FB_RGB332_H

/* RGB framebuffer with 1 byte per pixel, bits mapped as RRRGGGBB */

struct fb_rgb332 {
	uint8_t *mem;			/* set to backingstore memory */
	uint16_t damage_x1,damage_y1;	/* current damage window, ul (incl) */
	uint16_t damage_x2,damage_y2;	/* current damage window, lr (excl) */
};

extern void fb_rgb332_clear();

/* draw a box from cursor to x,y */
extern void fb_rgb332_boxto(uint16_t x,uint16_t y);
/* draw a line from cursor to x,y */
extern void fb_rgb332_lineto(uint16_t x,uint16_t y);

/* put string str onto framebuffer with line (bottom
   left pixel of, e.g. "m") starting at cursor.
   Maximum width consumed is maxwidth, actual width
   needed is returned */
extern int fb_rgb332_putstr(char *str,int maxwidth);

extern struct fb_rgb332 *fb_rgb332;

#endif
