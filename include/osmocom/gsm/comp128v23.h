/*
 * COMP128v23 header
 *
 * See comp128v23.c for details
 */

#ifndef __COMP128V23_H__
#define __COMP128V23_H__

#include <stdint.h>

/*
 * Performs the COMP128 version 2 and 3 algorithm (used as A3/A8)
 * ki    : uint8_t [16]
 * srand : uint8_t [16]
 * version : uint8_t (2 or 3)
 * sres  : uint8_t [4]
 * kc    : uint8_t [8]
 * returns 1 if not version 2 or 3 specified
 */
int comp128v23(const uint8_t *ki, const uint8_t *rand, uint8_t version, uint8_t *sres, uint8_t *kc);

#endif /* __COMP128V23_H__ */
