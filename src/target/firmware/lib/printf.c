
#include <stdio.h>
#include <stdarg.h>

static char printf_buffer[1024];

int printf(const char *fmt, ...)
{
	va_list args;
	int r;

	va_start(args, fmt);
	r = vsnprintf(printf_buffer, sizeof(printf_buffer), fmt, args);
	va_end(args);

	puts(printf_buffer);

	return r;
}
