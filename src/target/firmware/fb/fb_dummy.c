/*
    "hardware" driver for a dummy framebuffer. Used when no
    display hardware is supported
 */

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
#include <defines.h>

static void
fb_dummy_init(){
}

static void
fb_dummy_clear(){
}

static void
fb_dummy_boxto(uint16_t x,uint16_t y){
	framebuffer->cursor_x = x;
	framebuffer->cursor_y = y;
}

static void
fb_dummy_lineto(uint16_t x,uint16_t y){
	framebuffer->cursor_x = x;
	framebuffer->cursor_y = y;
}

static int
fb_dummy_putstr(__unused char *c, __unused int maxwidth){
	return 0;
}

static void
fb_dummy_flush(){
}

struct framebuffer fb_dummy_framebuffer = {
	.name = "dummyfb",
	.init = fb_dummy_init,
	.clear = fb_dummy_clear,
	.boxto = fb_dummy_boxto,
	.lineto = fb_dummy_lineto,
	.putstr = fb_dummy_putstr,
	.flush = fb_dummy_flush,
	.width = 128,
	.height = 64
};

struct framebuffer *framebuffer = & fb_dummy_framebuffer;
