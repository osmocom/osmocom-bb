#pragma once

/**
 * GNU and FreeBSD have various ways to express the
 * endianess but none of them is similiar enough. This
 * will create two defines that allows to decide on the
 * endian. The following will be defined to either 0 or
 * 1 at the end of the file.
 *
 *      OSMO_IS_LITTLE_ENDIAN
 *      OSMO_IS_BIG_ENDIAN
 *
 */

#if defined(__FreeBSD__)
#include <sys/endian.h>
        #if BYTE_ORDER == LITTLE_ENDIAN
                #define OSMO_IS_LITTLE_ENDIAN           1
                #define OSMO_IS_BIG_ENDIAN              0
        #elif BYTE_ORDER == BIG_ENDIAN
                #define OSMO_IS_LITTLE_ENDIAN           0
                #define OSMO_IS_BIG_ENDIAN              1
        #else
                #error "Unknown endian"
        #endif
#else
#include <endian.h>
        #if __BYTE_ORDER == __LITTLE_ENDIAN
                #define OSMO_IS_LITTLE_ENDIAN           1
                #define OSMO_IS_BIG_ENDIAN              0
        #elif __BYTE_ORDER == __BIG_ENDIAN
                #define OSMO_IS_LITTLE_ENDIAN           0
                #define OSMO_IS_BIG_ENDIAN              1
        #else
                #error "Unknown endian"
        #endif
#endif

