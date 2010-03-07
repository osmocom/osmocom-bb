#ifndef _LINUX_BYTEORDER_LITTLE_ENDIAN_H
#define _LINUX_BYTEORDER_LITTLE_ENDIAN_H

#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN 1234
#endif
#ifndef __LITTLE_ENDIAN_BITFIELD
#define __LITTLE_ENDIAN_BITFIELD
#endif

#include <stdint.h>
#include <swab.h>

#define __constant_htonl(x) ___constant_swab32(x)
#define __constant_ntohl(x) ___constant_swab32(x)
#define __constant_htons(x) ___constant_swab16(x)
#define __constant_ntohs(x) ___constant_swab16(x)
#define __constant_cpu_to_le64(x) (x)
#define __constant_le64_to_cpu(x) (x)
#define __constant_cpu_to_le32(x) (x)
#define __constant_le32_to_cpu(x) (x)
#define __constant_cpu_to_le16(x) (x)
#define __constant_le16_to_cpu(x) (x)
#define __constant_cpu_to_be64(x) ___constant_swab64(x)
#define __constant_be64_to_cpu(x) ___constant_swab64(x)
#define __constant_cpu_to_be32(x) ___constant_swab32(x)
#define __constant_be32_to_cpu(x) ___constant_swab32(x)
#define __constant_cpu_to_be16(x) ___constant_swab16(x)
#define __constant_be16_to_cpu(x) ___constant_swab16(x)
#define __cpu_to_le64(x) (x)
#define __le64_to_cpu(x) (x)
#define __cpu_to_le32(x) (x)
#define __le32_to_cpu(x) (x)
#define __cpu_to_le16(x) (x)
#define __le16_to_cpu(x) (x)
#define __cpu_to_be64(x) __swab64(x)
#define __be64_to_cpu(x) __swab64(x)
#define __cpu_to_be32(x) __swab32(x)
#define __be32_to_cpu(x) __swab32(x)
#define __cpu_to_be16(x) __swab16(x)
#define __be16_to_cpu(x) __swab16(x)

/* from include/linux/byteorder/generic.h */
#define cpu_to_le64 __cpu_to_le64
#define le64_to_cpu __le64_to_cpu
#define cpu_to_le32 __cpu_to_le32
#define le32_to_cpu __le32_to_cpu
#define cpu_to_le16 __cpu_to_le16
#define le16_to_cpu __le16_to_cpu
#define cpu_to_be64 __cpu_to_be64
#define be64_to_cpu __be64_to_cpu
#define cpu_to_be32 __cpu_to_be32
#define be32_to_cpu __be32_to_cpu
#define cpu_to_be16 __cpu_to_be16
#define be16_to_cpu __be16_to_cpu

/*
 * They have to be macros in order to do the constant folding
 * correctly - if the argument passed into a inline function
 * it is no longer constant according to gcc..
 */

#undef ntohl
#undef ntohs
#undef htonl
#undef htons

#define ___htonl(x) __cpu_to_be32(x)
#define ___htons(x) __cpu_to_be16(x)
#define ___ntohl(x) __be32_to_cpu(x)
#define ___ntohs(x) __be16_to_cpu(x)

#define htonl(x) ___htonl(x)
#define ntohl(x) ___ntohl(x)
#define htons(x) ___htons(x)
#define ntohs(x) ___ntohs(x)


#endif /* _LINUX_BYTEORDER_LITTLE_ENDIAN_H */
