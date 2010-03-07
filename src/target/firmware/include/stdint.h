#ifndef _STDINT_H
#define _STDINT_H

/* some older toolchains (like gnuarm-3.x) don't provide a C99
   compliant stdint.h yet, so we define our own here */

typedef signed char int8_t;
typedef unsigned char uint8_t;

typedef signed short int16_t;
typedef unsigned short uint16_t;

typedef signed int int32_t;
typedef unsigned int uint32_t;

#endif
