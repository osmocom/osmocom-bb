
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
