/*
 * xcch.h
 *
 * Copyright (c) 2011  Sylvain Munaut <tnt@246tNt.com>
 */

#ifndef __XCCH_H__
#define __XCCH_H__

#include <stdint.h>
#include <osmocom/core/bits.h>

int xcch_decode(uint8_t *l2_data, sbit_t *bursts);

#endif /* __XCCH_H__ */
