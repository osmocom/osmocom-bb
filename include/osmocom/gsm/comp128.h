/*
 * COMP128 header
 *
 * See comp128.c for details
 */

#ifndef __COMP128_H__
#define __COMP128_H__

#include <stdint.h>

/*
 * Performs the COMP128 algorithm (used as A3/A8)
 * ki    : uint8_t [16]
 * srand : uint8_t [16]
 * sres  : uint8_t [4]
 * kc    : uint8_t [8]
 */
void comp128(const uint8_t *ki, const uint8_t *srand, uint8_t *sres, uint8_t *kc);

#endif /* __COMP128_H__ */

