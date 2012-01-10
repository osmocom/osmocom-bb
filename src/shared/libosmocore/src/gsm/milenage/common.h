
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MSG_DEBUG
#define wpa_hexdump(x, args...)
#define wpa_hexdump_key(x, args...)
#define wpa_printf(x, args...)

#define os_memcpy(x, y, z)  memcpy(x, y, z)
#define os_memcmp(x, y, z)  memcmp(x, y, z)
#define os_memset(x, y, z)  memset(x, y, z)
#define os_malloc(x)  malloc(x)
#define os_free(x)  free(x)

typedef uint8_t u8;
typedef uint32_t u32;

#define __must_check
