#ifndef _FB_FRAMEBUFFER_H
#define _FB_FRAMEBUFFER_H

#include <fb/font.h>
#include <stdint.h>

/* color is encoded as <special><red><green><blue> */
/* if a color is "special", then the RGB components most likely
   don't make sense. Use "special" colours when you have to
   mask out bits with transparency or you have to encode
   colours in a fixed color palette... */

#define FB_COLOR_WHITE		0x00ffffffU
#define FB_COLOR_BLACK		0x00000000U
#define FB_COLOR_TRANSP		0x01ffffffU

#define FB_COLOR_RGB(r,g,b) ((((r) & 0xff)<<16)|(((g)&0xff)<<8)|((b)&0xff))
#define FB_COLOR_RED		FB_COLOR_RGB(0xff,0x00,0x00)
#define FB_COLOR_GREEN		FB_COLOR_RGB(0x00,0xff,0x00)
#define FB_COLOR_BLUE		FB_COLOR_RGB(0x00,0x00,0xff)

/* encode */

/* decode */
#define FB_COLOR_IS_SPECIAL(v)     (!!((v) & 0xff000000U))
#define FB_COLOR_TO_R(v)		(((v)>>16) & 0xff)
#define FB_COLOR_TO_G(v)		(((v)>> 8) & 0xff)
#define FB_COLOR_TO_B(v)		( (v)      & 0xff)

struct framebuffer {
	char name[8];				// keep it short!
	void (*init)();				// (re)initialize
	void (*clear)();			// clear display
	void (*boxto)(uint16_t x,uint16_t y);	// draw box to xy
	void (*lineto)(uint16_t x,uint16_t y);	// draw line to xy
	int (*putstr)(char *c,int maxwidth);	// put text in current font to fb
	void (*flush)();			// flush changes

	uint16_t width,height;			// width/height of fb
	uint16_t cursor_x,cursor_y;		// current cursor
	uint32_t fg_color,bg_color;		// current fg/bg color
	enum fb_font_id font;			// current font
};

/* there is a single framebuffer, the specific driver defines
   the "framebuffer" symbol */
extern struct framebuffer *framebuffer;

static inline void
fb_init(){
	framebuffer->init();
}

static inline void
fb_clear(){
	framebuffer->clear();
}

static inline void
fb_boxto(uint16_t x,uint16_t y){
	framebuffer->boxto(x,y);
}

static inline void
fb_lineto(uint16_t x,uint16_t y){
	framebuffer->lineto(x,y);
}

static inline int
fb_putstr(char *str,int maxwidth){
	return framebuffer->putstr(str,maxwidth);
}

static inline void
fb_flush(){
	framebuffer->flush();
}

static inline void
fb_gotoxy(uint16_t x,uint16_t y){
	framebuffer->cursor_x = x;
	framebuffer->cursor_y = y;
}

static inline void
fb_setfg(uint32_t color){
	framebuffer->fg_color = color;
}

static inline void
fb_setbg(uint32_t color){
	framebuffer->bg_color = color;
}

static inline void
fb_setfont(enum fb_font_id fid){
	framebuffer->font = fid;
}

/* utility function: limit coordinates to area of framebuffer */
static inline void
fb_limit_fb_range(uint16_t *x,uint16_t *y){
	if(*x >= framebuffer->width)
		*x = framebuffer->width - 1;
	if(*y >= framebuffer->height)
		*y = framebuffer->height - 1;
}

/* utility function: limit box coordinates to area of framebuffer
   and make sure that x1y1 is left upper edge, x2y2 is right lower */
static inline void
fb_sanitize_box(uint16_t *x1,uint16_t *y1,uint16_t *x2,uint16_t *y2){
	fb_limit_fb_range(x1,y1);
	fb_limit_fb_range(x2,y2);
	if(*x1 > *x2){
		uint16_t tmp = *x1;
		*x1 = *x2;
		*x2 = tmp;
	}
	if(*y1 > *y2){
		uint16_t tmp = *y1;
		*y1 = *y2;
		*y2 = tmp;
	}
}

#endif

