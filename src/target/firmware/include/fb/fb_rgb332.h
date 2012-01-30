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

/* this convenience function can be used if you choose to
 * back a RGB565 display with a RGB332 framebuffer to conserve
 * ARM memory. It converts a rgb332 value to rgb565 as indicated
 * in the comments. */

static inline uint16_t
rgb332_to_565(uint8_t rgb332){

	uint8_t red   =  (rgb332 & 0xe0) >> 5 ; // rrr. .... -> .... .rrr
	uint8_t green = ((rgb332 & 0x1c) >> 2); // ...g gg.. -> .... .ggg
	uint8_t blue  =   rgb332 & 0x03;        // .... ..bb -> .... ..bb

	red   = (red   << 2) | (red >> 1);                /* .....210 -> ...21021 */
	green = (green << 3) | (green);                   /* .....210 -> ..210210 */
	blue  = (blue  << 3) | (blue << 1) | (blue >> 1); /* ......10 -> ...10101 */

	/* rrrrrggg gggbbbbb */
	return (red << 11) | (green << 5) | blue;
}

#endif
