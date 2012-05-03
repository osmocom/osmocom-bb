#ifndef _FB_FONT_H
#define _FB_FONT_H

#include <stdint.h>
#include <unistd.h>

/*
	Example:
		Font Helvetica 14
		

	Character W ('X' and '.' is the character font data)

	    X.....X......&...
	    X.....X......X...
	    X....X.X.....X...
	    .X...X.X....X....
	    .X...X.X....X....
	    .X...X.X....X....
	    ..X.X....X.X.....
	    ..X.X....X.X.....
	    ..X.X....X.X.....
	    ...X......X......
	   @%..X......X...$..
	   <---dwidth---->

	   @ is the cursor position (origin) for this character
	   $ is the cursor position (origin) for the next character
	   % is the character boundingbox origin,
	   & is the character boundingbox top right corner
	     
 */

/* data for char c is found by getting the index into the
   chardata array from the charoffs array.
   
   if charoffs[c] == FB_FONT_NOCHAR, then this glyph does
   not exist! Better use the convenience function fb_font_get_char below! */

#define FB_FONT_NOCHAR 0xffff

struct fb_font {
	int8_t height;  /* total height of font */
	int8_t ascent;  /* topmost pixel is "ascend" above
			   current cursor position y */
	uint8_t firstchar,lastchar; /* range of characters in font (iso8859-1) */
	uint8_t const *chardata;
	uint16_t const *charoffs;		/* byte offsets relative to chardata */
	uint8_t  const *widths;		/* widths for characters */
};

struct fb_char {
	int8_t width;
	int8_t bbox_w,bbox_h,bbox_x,bbox_y;
	uint8_t data[0];
};

/* there are currently 6 fonts available, Helvetica 8, 14, 24 point
   in bold and regular shapes. The following enum has to match the
   order of the array fb_fonts in framebuffer.c!
*/

enum fb_font_id {
//	FB_FONT_4X6,
//	FB_FONT_5X8,
	FB_FONT_HELVR08,
//	FB_FONT_HELVR14
//	FB_FONT_HELVR24,
//	FB_FONT_HELVB08,
	FB_FONT_HELVB14,
//	FB_FONT_HELVB24,
	FB_FONT_C64,
	FB_FONT_SYMBOLS,
};

extern const struct fb_font *fb_fonts[]; // note: has to match fb_font_id enum!

extern const struct fb_char *
fb_font_get_char(const struct fb_font *fnt,unsigned char c);

#endif

