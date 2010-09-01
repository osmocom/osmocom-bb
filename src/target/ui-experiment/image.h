
#ifndef _UI_IMAGE_H
#define _UI_IMAGE_H

/* for exit() */
#include <stdlib.h>

#include <ui/pixel.h>
#include <ui/font.h>

struct image {
	pxtype_t type;
	pxdims_t size;
	unsigned char *data;
};


struct image font_img_v = {
	.type = PXTYPE_MONO_V8,
	.size = {8, 2048},
	.data = &fontdata_r8x8,
};

struct image font_img_h = {
	.type = PXTYPE_MONO_H8,
	.size = {8, 2048},
	.data = &fontdata_r8x8_horiz,
};

px_t
image_get_pixel(struct image *img, pxoff_t x, pxoff_t y) {
	unsigned stride, base, offset;
	const uint8_t *p8;
	const uint16_t *p16;

	switch(img->type) {
	case PXTYPE_MONO_V8:
		stride = img->size.w;
		base = y / 8;
		offset = y % 8;
		p8 = (uint8_t*)(img->data + x + base * stride);
		return px_from_mono(((*p8) >> offset) & 1);
	case PXTYPE_MONO_H8:
		stride = img->size.w / 8;
		base = x / 8;
		offset = x % 8;
		p8 = (uint8_t*)(img->data + base + y * stride);
		return px_from_mono(((*p8) >> offset) & 1);
	case PXTYPE_RGB444:
		stride = img->size.w * 2;
		p16 = (uint16_t*)(img->data + x * 2 + y * stride);
		return px_from_rgb444(*p16);
	}

	return 0;
}

void
image_set_pixel(struct image *img, pxoff_t x, pxoff_t y, px_t v) {
	unsigned stride, base, offset;
	uint8_t *p8;
	uint16_t *p16;

	switch(img->type) {
	case PXTYPE_MONO_V8:
		stride = img->size.w;
		base = y / 8;
		offset = y % 8;
		p8 = (uint8_t*)(img->data + x + base * stride);
		*p8 |= (px_to_mono(v) << offset);
		break;
	case PXTYPE_MONO_H8:
		stride = img->size.w / 8;
		base = x / 8;
		offset = x % 8;
		p8 = (uint8_t*)(img->data + base + y * stride);
		*p8 |= (px_to_mono(v) << offset);
		break;
	case PXTYPE_RGB444:
		stride = img->size.w * 2;
		p16 = (uint16_t*)(img->data + x * 2 + y * stride);
		*p16 = px_to_rgb444(v);
		break;
	}
}

void
image_blit(struct image *dst, pxposn_t dstp,
		   struct image *src, pxposn_t srcp,
		   pxdims_t d)
{
	unsigned x, y, s;

	printf("blit %dx%d from %dx%d to %dx%d\n", d.w, d.h, srcp.x, srcp.y, dstp.x, dstp.y);

	// *cough* slow.
	for(y = 0; y < d.h; y++) {
		for(x = 0; x < d.w; x++) {
			px_t p = image_get_pixel(src, srcp.x + x, srcp.y + y);
			image_set_pixel(dst, dstp.x + x, dstp.y + y, p);
		}
	}
}

void
image_draw_char(struct image *dst, pxposn_t p, char chr) {
	unsigned char c = (unsigned char)chr;
	pxposn_t pf = {0,c*8};
	pxdims_t d = {8,8};
	image_blit(dst, p, &font_img_h, pf, d);
}

void
image_draw_string(struct image *dst, pxposn_t p, char *str) {
	while(*str) {
		image_draw_char(dst, p, *str);
		p.x += 8;
		str++;
	}
}

static void
image_fill_rect_rgb444(struct image *dst, pxrect_t rect, uint16_t color) {
	unsigned x, y, s;
	uint16_t *p;

	unsigned stride = dst->size.w * 2;

	for(y = rect.p.y; y < rect.p.y + rect.d.h; y++) {
		for(x = rect.p.x; x < rect.p.x + rect.d.w; x++) {
			p = (uint16_t*)&dst->data[x * 2 + y * stride];
			*p = color;
		}
	}
}

void
image_fill_rect(struct image *dst, pxrect_t rect, px_t color)
{
	switch(dst->type) {
	case PXTYPE_MONO_V8:
		break;
	case PXTYPE_MONO_H8:
		break;
	case PXTYPE_RGB444:
		image_fill_rect_rgb444(dst, rect, px_to_rgb444(color));
		break;
	}
}

void
image_draw_hline(struct image *dst, pxposn_t posn, pxoff_t len, px_t color)
{
}

void
image_draw_vline(struct image *dst, pxposn_t posn, pxoff_t len, px_t color)
{
}

void
image_draw_rect(struct image *dst, pxrect_t rect, px_t color)
{
}

#endif
