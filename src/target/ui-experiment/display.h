
#ifndef _UI_DISPLAY_H
#define _UI_DISPLAY_H

#include <ui/pixel.h>
#include <ui/image.h>

/**
 * Displays - physical display devices
 *
 * This layer is introduced tentatively, expecting use
 * of OSMOCOM on multi-display phones, most likely
 * with a main screen and a cover screen.
 *
 */
struct display {
	const char *name;

	pxtype_t pixeltype;
	pxsize_t width;
	pxsize_t height;

	/* We always operate on an in-memory frame buffer that 
	 * can be put on display using damage functions provided
	 * by the image class.
	 */
	struct image *fbuf;

	/*
	 * We display a top-level widget.
	 */
	struct widget *widget;

	/*
	 * We hold a graphics context, configured for the target
	 * pixel format.
	 */
	struct graphics *graphics;


	void (*draw) (struct display *display);

	void *priv;
};

#endif
