#ifndef _STRING_H
#define _STRING_H

#include <sys/types.h>

size_t strnlen(const char *s, size_t count);
size_t strlen(const char *s);

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);

int memcmp(const void *s1, const void *s2, size_t n);
int strcmp(const char *s1, const char *s2);

#endif
