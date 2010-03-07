#ifndef _STDIO_H
#define _STDIO_H

#ifndef NULL
#define NULL	0
#endif /* NULL */

#include <sys/types.h>

int printf(const char *format, ...);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);

#include <stdarg.h>

int vprintf(const char *format, va_list ap);
int vsprintf(char *str, const char *format, va_list ap);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int puts(const char *s);

#if 0
/* start.S based uart console */
#include <calypso/uart.h>
#define putchar(c)	uart_putchar_wait(1, c)
int puts(const char *s);
#endif

#if 0
/* regular UART console */
#include <console.h>
#define putchar(c)	cons_putchar(c)
#define _puts(s)	cons_puts(s)
#define ARCH_HAS_CONSOLE
#endif

#if 1
/* sercomm based console */
#include <comm/sercomm_cons.h>
#define putchar(c)	sercomm_putchar(c)
#define _puts(s)	sercomm_puts(s)
#define ARCH_HAS_CONSOLE
#endif

struct __file {
};

typedef struct __file FILE;

/* non-standard */
extern void phex(unsigned int c, unsigned int len);

#endif /* _STDIO_H */
