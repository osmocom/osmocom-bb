#ifndef _SERCOMM_CONS_H
#define _SERCOMM_CONS_H

/* how large buffers do we allocate? */
#define SERCOMM_CONS_ALLOC	256

int sercomm_write(const char *s, const int len);
int sercomm_putchar(int c);
void sercomm_flush();

#endif /* _SERCOMM_CONS_H */
