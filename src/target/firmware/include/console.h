#ifndef _CONSOLE_H
#define _CONSOLE_H

/* This is the direct (IRQ driven) UART console, bypassing the HDLC layer.
 * You should not need to call those functions unless you've decided to
 * not use the HLDC layer or have a device with two UARTs */

int cons_rb_append(const char *data, int len);
int cons_puts(const char *s);
int cons_putchar(char c);
int cons_rb_flush(void);
void cons_init(void);
void cons_bind_uart(int uart);
int cons_get_uart(void);

/* Size of the static ring-buffer that we keep for console print messages */
#define CONS_RB_SIZE	4096

#endif /* _CONSOLE_H */
