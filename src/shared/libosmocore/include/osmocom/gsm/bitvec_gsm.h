#pragma once

#include <osmocom/core/bitvec.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>

/*! \defgroup bitvec helpers for GSM
 *  @{
 */
/*! \file bitvec_gsm.h */

void bitvec_add_range1024(struct bitvec *bv, const struct gsm48_range_1024 *r);

/*! @} */
