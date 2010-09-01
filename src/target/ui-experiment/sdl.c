
#include <ui/display.h>
#include <ui/image.h>
#include <ui/sdl.h>

#include <stdio.h>

#include <SDL.h>

#define SDL_PRIV(d) ((struct sdl_display*)(d)->priv)

#define REFRESH_INTERVAL_MSEC 50

struct sdl_display {
	SDL_Surface *display;
	SDL_TimerID refresh;
	unsigned width;
	unsigned height;
	unsigned scale;
};

static Uint32 sdl_redraw_callback(Uint32 interval, void *param) {
	struct display *display = (struct display*)param;

	display->draw(display);

	return interval;
}

void
sdl_init(struct display *display,
		 unsigned width, unsigned height, unsigned scale)
{
	if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)) {
		printf("Failed to initialize SDL: %s\n", SDL_GetError());
		exit(1);
	}

	atexit(&SDL_Quit);

	struct sdl_display *priv = SDL_PRIV(display);

	priv->width = width;
	priv->height = height;
	priv->scale = scale;

	priv->display = SDL_SetVideoMode(width * scale, height * scale, 32, 0);
	if(!priv->display) {
		printf("Failed to set SDL video mode: %s\n", SDL_GetError());
		exit(1);
	}

	priv->refresh = SDL_AddTimer(REFRESH_INTERVAL_MSEC,
								 &sdl_redraw_callback, display);
	if(!priv->refresh) {
		printf("Failed to add refresh timer: %s\n", SDL_GetError());
		exit(1);
	}
}

void
sdl_draw(struct display *display)
{
	struct sdl_display *priv = SDL_PRIV(display);

	struct image *img = display->fbuf;

	SDL_Rect r;

	r.w = priv->scale;
	r.h = priv->scale;

	if(img->type == PXTYPE_RGB444) {
		unsigned stride = img->size.w * 2;

		unsigned x, y;
		for(y = 0; y < img->size.h; y++) {
			for(x = 0; x < img->size.w; x++) {
				px_t color = image_get_pixel(img, x, y);

				r.x = x * priv->scale;
				r.y = y * priv->scale;

				SDL_FillRect(priv->display, &r, color);
			}
		}
	} else {
		puts("Unsupported framebuffer type for SDL emulator.");
		exit(1);
	}

	SDL_UpdateRect(priv->display, 0, 0, 0, 0);
}


static struct sdl_display display_sdl_priv;

uint8_t sdl_fbuf[96*64*2];

struct image display_sdl_fbuf = {
	.type = PXTYPE_RGB444,
	.size = {96, 64},
	.data = &sdl_fbuf
};

struct display display_sdl = {
	.name = "Main Display",
	.fbuf = &display_sdl_fbuf,
	.priv = &display_sdl_priv,
	.draw = &sdl_draw
};

uint16_t fnord_buf[] = {
	0x0F00,
	0x00F0,
	0x000F,
	0x00FF,
	0x0F00,
	0x00F0,
	0x000F,
	0x00FF,
	0x0F00,
	0x00F0,
	0x000F,
	0x00FF,
	0x0F00,
	0x00F0,
	0x000F,
	0x00FF,
	0x0F00,
	0x00F0,
	0x000F,
	0x00FF,
	0x0F00,
	0x00F0,
	0x000F,
	0x00FF,
	0x0F00,
	0x00F0,
	0x000F,
	0x00FF,
	0x0F00,
	0x00F0,
	0x000F,
	0x00FF

};

struct image fnord = {
	.type = PXTYPE_RGB444,
	.size = {8,4},
	.data = &fnord_buf
};

uint8_t fubar_img[] = {
	0x01, 0x02, 0x03, 0x04,
	0x05, 0x06, 0x07, 0x08,
	0x09, 0x0a, 0x0b, 0x0c,
	0x0d, 0x0e, 0x0f, 0x0f

};

struct image fubar = {
	.type = PXTYPE_MONO_V8,
	.size = {8,16},
	.data = &fubar_img
};

void
sdl_run(void)
{
	int r;
	SDL_Event e;

	while((r = SDL_WaitEvent(&e))) {

		if(e.type == SDL_KEYDOWN) {
			if(e.key.keysym.sym == SDLK_ESCAPE) {
				puts("Bloody quitter!");
				break;
			}
			if(e.key.keysym.sym == SDLK_SPACE) {
				pxposn_t dp = {0,0};
				pxposn_t sp = {0,0};
				pxdims_t d = {8,4};

				image_blit(&display_sdl_fbuf, dp,
						   &fnord, sp,
						   d);

				sp.x = 0;
				sp.y = 0;
				dp.x = 5;
				dp.y = 10;
				d.w = 8;
				d.h = 16;

				image_blit(&display_sdl_fbuf, dp,
						   &fubar, sp,
						   d);

				pxrect_t r = {{12,0},{40,20}};
				image_fill_rect(&display_sdl_fbuf,
								r,
								0xFF00FF);


#if 0
				dp.x = 0;
				dp.y = 0;

				image_draw_string(&display_sdl_fbuf, dp,
								"ABCDEFGHI");

				dp.y += 10;

				image_draw_string(&display_sdl_fbuf, dp,
								"abcdefghi");

#endif

				sdl_draw(&display_sdl);

			}
		}

		switch(e.type) {
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			printf("Key %d %d\n", e.key.keysym.sym, e.key.state);
			break;
		}
	}

	if(!r) {
		printf("Failed to wait for SDL event: %s\n", SDL_GetError());
		exit(1);
	}

}

int
main(void)
{
	sdl_init(&display_sdl, 96, 64, 4);

	sdl_run();

	return 0;
}
