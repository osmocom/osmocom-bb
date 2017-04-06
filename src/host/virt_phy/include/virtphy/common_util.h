/*
 * Utility function used both in osmo bts virt and osmocom bb virt.
 */

#pragma once

#include <osmocom/core/gsmtap.h>
#include <osmocom/gsm/rsl.h>

#define LID_SACCH 		0x40
#define LID_DEDIC 		0x00

/*! \brief convert GSMTAP channel type to RSL channel number
 *  \param[in] gsmtap_chantype GSMTAP channel type
 *  \param[out] rsl_chantype rsl channel type
 *  \param[out] rsl_chantype rsl link id
 *
 *  Mapping from gsmtap channel:
 *  GSMTAP_CHANNEL_UNKNOWN *  0x00
 *  GSMTAP_CHANNEL_BCCH *  0x01
 *  GSMTAP_CHANNEL_CCCH *  0x02
 *  GSMTAP_CHANNEL_RACH *  0x03
 *  GSMTAP_CHANNEL_AGCH *  0x04
 *  GSMTAP_CHANNEL_PCH *  0x05
 *  GSMTAP_CHANNEL_SDCCH *  0x06
 *  GSMTAP_CHANNEL_SDCCH4 *  0x07
 *  GSMTAP_CHANNEL_SDCCH8 *  0x08
 *  GSMTAP_CHANNEL_TCH_F *  0x09
 *  GSMTAP_CHANNEL_TCH_H *  0x0a
 *  GSMTAP_CHANNEL_PACCH *  0x0b
 *  GSMTAP_CHANNEL_CBCH52 *  0x0c
 *  GSMTAP_CHANNEL_PDCH *  0x0d
 *  GSMTAP_CHANNEL_PTCCH *  0x0e
 *  GSMTAP_CHANNEL_CBCH51 *  0x0f
 *  to rsl channel type:
 *  RSL_CHAN_NR_MASK *  0xf8
 *  RSL_CHAN_NR_1 *   *  0x08
 *  RSL_CHAN_Bm_ACCHs *  0x08
 *  RSL_CHAN_Lm_ACCHs *  0x10
 *  RSL_CHAN_SDCCH4_ACCH *  0x20
 *  RSL_CHAN_SDCCH8_ACCH *  0x40
 *  RSL_CHAN_BCCH *   *  0x80
 *  RSL_CHAN_RACH *   *  0x88
 *  RSL_CHAN_PCH_AGCH *  0x90
 *  RSL_CHAN_OSMO_PDCH *  0xc0
 *  and logical channel link id:
 *  LID_SACCH  *   *  0x40
 *  LID_DEDIC  *   *  0x00
 */
void chantype_gsmtap2rsl(uint8_t gsmtap_chantype, uint8_t *rsl_chantype,
                         uint8_t *link_id);
