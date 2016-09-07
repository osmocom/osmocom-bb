/*
 * gsm0503.h
 *
 * Copyright (C) 2016 sysmocom s.f.m.c. GmbH
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#pragma once

#include <stdint.h>

#include <osmocom/core/conv.h>

/*! \file gsm0503.h
 * Osmocom convolutional encoder/decoder for xCCH channels, see 3GPP TS 05.03
 */

/*! \brief structure describing convolutional code xCCH
 *
 *  Non-recursive code, flushed, not punctured code.
 */
extern const struct osmo_conv_code gsm0503_xcch;

/*! \brief structures describing convolutional codes CS2/3
 */
extern const struct osmo_conv_code gsm0503_cs2;
extern const struct osmo_conv_code gsm0503_cs3;

/*! \brief structure describing convolutional code TCH/AFS 12.2
 */
extern const struct osmo_conv_code gsm0503_tch_afs_12_2;

/*! \brief structure describing convolutional code TCH/AFS 10.2
 */
extern const struct osmo_conv_code gsm0503_tch_afs_10_2;

/*! \brief structure describing convolutional code TCH/AFS 7.95
 */
extern const struct osmo_conv_code gsm0503_tch_afs_7_95;

/*! \brief structure describing convolutional code TCH/AFS 7.4
 */
extern const struct osmo_conv_code gsm0503_tch_afs_7_4;

/*! \brief structure describing convolutional code TCH/AFS 6.7
 */
extern const struct osmo_conv_code gsm0503_tch_afs_6_7;

/*! \brief structure describing convolutional code TCH/AFS 5.9
 */
extern const struct osmo_conv_code gsm0503_tch_afs_5_9;

/*! \brief structure describing convolutional code TCH/AFS 5.15
 */
extern const struct osmo_conv_code gsm0503_tch_afs_5_15;

/*! \brief structure describing convolutional code TCH/AFS 4.75
 */
extern const struct osmo_conv_code gsm0503_tch_afs_4_75;
