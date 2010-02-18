#ifndef _ST7558_H
#define _ST7558_H

enum display_attr {
	DISP_ATTR_INVERT	= 0x0001,
};

void st7558_init(void);
void st7558_set_attr(unsigned long attr);
void st7558_unset_attr(unsigned long attr);
void st7558_clrscr(void);
void st7558_putchar(unsigned char c);
void st7558_puts(const char *str);

#endif
