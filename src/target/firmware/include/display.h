#ifndef _DISPLAY_DRIVER_H
#define _DISPLAY_DRIVER_H

enum display_attr {
	DISP_ATTR_INVERT	= 0x0001,
};

struct display_driver {
	char *name;
	void (*init)(void);
	void (*set_attr)(unsigned long attr);
	void (*unset_attr)(unsigned long attr);
	void (*clrscr)(void);
	void (*goto_xy)(int xpos, int ypos);
	void (*set_color)(int fgcolor, int bgcolor);
	int (*putchr)(unsigned char c);
	int (*puts)(const char *str);
};

extern struct display_driver *display;

static inline void display_init(void)
{
	display->init();
}
static inline void display_set_attr(unsigned long attr)
{
	display->set_attr(attr);
}
static inline void display_unset_attr(unsigned long attr)
{
	display->unset_attr(attr);
}
static inline void display_clrscr(void)
{
	display->clrscr();
}
static inline int display_putchar(unsigned char c)
{
	return display->putchr(c);
}
int display_puts(const char *s);

extern const struct display_driver st7558_display;
extern const struct display_driver ssd1783_display;
extern const struct display_driver td014_display;

#endif
