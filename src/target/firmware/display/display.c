
#include <stdint.h>

#include <display.h>

struct display_driver *display;

int display_puts(const char *str)
{
	char c;

	if (display->puts)
		display->puts(str);
	else {
		while ((c = *str++))
			display_putchar(c);
	}

	return 0;
}

int display_goto_xy(int x, int y)
{
	char c;

	if (display->goto_xy)
		display->goto_xy(x, y);

	return 0;
}


