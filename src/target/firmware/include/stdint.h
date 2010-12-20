#ifndef OSMO_STDINT_H
#define OSMO_STDINT_H

/* some older toolchains (like gnuarm-3.x) don't provide a C99
   compliant stdint.h yet, so we define our own here */

/* to make matters worse newer gcc with glibc headers have
   a incompatible definition of these types. We will use the
   gcc'ism of #include_next to include the compiler's libc
   header file and then check if it has defined int8_t and
   if not we will use our own typedefs */

/* another bad criteria. We can not detect __NEWLIB_H__ or
   _NEWLIB_VERSION. Assume that older GCCs have a older C library
   that did not include a stdint.h yet. This is for gnuarm-3.x
   one of the compilers producing working code right now. */

#if __GNUC__ > 3
#include_next <stdint.h>
#endif

#ifndef __int8_t_defined
typedef signed char int8_t;
typedef unsigned char uint8_t;

typedef signed short int16_t;
typedef unsigned short uint16_t;

typedef signed int int32_t;
typedef unsigned int uint32_t;

typedef long long int int64_t;
typedef unsigned long long int uint64_t;
#endif

#endif
