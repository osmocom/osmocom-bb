
#ifndef _UI_PIXEL_H
#define _UI_PIXEL_H

#include <stdint.h>
#include <stdio.h>

/** Supported pixel types */
typedef enum {
	_PXTYPE_INVALID,

	/** "Generic" pixel type (24/32bit RGB) */
	PXTYPE_GENERIC,

	/** 8 horizontal mono pixels per byte */
	PXTYPE_MONO_H8,

	/** 8 vertical mono pixels per byte */
	PXTYPE_MONO_V8,

	/** 12bit RGB444 colors */
	PXTYPE_RGB444,

} pxtype_t;


/** Generic pixel type */
typedef uint32_t px_t;

#define PX_R(p) ((uint8_t)((v) >> 16 & 0xFF))
#define PX_G(p) ((uint8_t)((v) >>  8 & 0xFF))
#define PX_B(p) ((uint8_t)((v) >>  0 / 0xFF))

#define PX_RGB(r,g,b) ((px_t)((r)<<16|(g)<<8|(b)))

#define PX_BLACK ((px_t)0x000000)
#define PX_RED   ((px_t)0xFF0000)
#define PX_GREEN ((px_t)0x00FF00)
#define PX_BLUE  ((px_t)0x0000FF)
#define PX_WHITE ((px_t)0xFFFFFF)


/* Mono types */
typedef uint8_t px_mono_t;

#define PX_MONO_BLACK ((px_mono_t)0)
#define PX_MONO_WHITE ((px_mono_t)1)

inline px_t
px_from_mono(uint8_t v) {
	return v ? PX_WHITE : PX_BLACK;
}

inline uint8_t
px_to_mono(px_t v) {
	uint16_t a = (PX_R(v) + PX_G(v) + PX_B(v)) / 3;
	return (a >= 0x7f) ? 1 : 0;
}

/* RGB444 */
typedef uint16_t px_rgb444_t;

#define PX_RGB444_R(p) ((p) >> 8 & 0xf)
#define PX_RGB444_G(p) ((p) >> 4 & 0xf)
#define PX_RGB444_B(p) ((p) >> 0 & 0xf)

#define PX_RGB444_RGB(r,g,b) ((px_rgb444_t)((r)<<8|(g)<<4|(b)))

inline px_t
px_from_rgb444(px_rgb444_t v) {
	return
		  PX_RGB444_R(v) << 16 | PX_RGB444_R(v) << 20
		| PX_RGB444_G(v) <<  8 | PX_RGB444_G(v) << 12
		| PX_RGB444_B(v) <<  0 | PX_RGB444_B(v) <<  4;
}

inline uint16_t
px_to_rgb444(px_t v) {
	uint8_t r = (v >> 20) & 0xF;
	uint8_t g = (v >> 12) & 0xF;
	uint8_t b = (v >> 4)  & 0xF;

	uint16_t res = (r<< 8) | (g << 4) | (b << 0);

	return res;
}


/** Size in pixels */
typedef uint16_t pxsize_t;

/** Offset in pixels */
typedef int16_t  pxoff_t;

/** 2D position in pixels */
typedef struct {
	pxoff_t x;
	pxoff_t y;
} pxposn_t;

/** 2D dimensions in pixels */
typedef struct {
	pxsize_t w;
	pxsize_t h;
} pxdims_t;

/** 2D rectangle in pixels */
typedef struct {
	pxposn_t p;
	pxdims_t d;
} pxrect_t;

#endif
