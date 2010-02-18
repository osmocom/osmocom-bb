#ifndef _STRING_H
#define _STRING_H

#include <sys/types.h>

size_t strnlen(const char *s, size_t count);
size_t strlen(const char *s);

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);

#endif
