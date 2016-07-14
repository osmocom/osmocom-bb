#pragma once

#include <stdint.h>

/*! \brief Structure for CPS coding and puncturing scheme (TS 04.60 10.4.8a) */
struct egprs_cps {
	uint8_t bits;
	uint8_t mcs;
	uint8_t p[2];
};

/*! \brief CPS puncturing table selection (TS 04.60 10.4.8a) */
enum egprs_cps_punc {
	EGPRS_CPS_P1,
	EGPRS_CPS_P2,
	EGPRS_CPS_P3,
	EGPRS_CPS_NONE = -1,
};

/*! \brief EGPRS header types (TS 04.60 10.0a.2) */
enum egprs_hdr_type {
        EGPRS_HDR_TYPE1,
        EGPRS_HDR_TYPE2,
        EGPRS_HDR_TYPE3,
};

int egprs_get_cps(struct egprs_cps *cps, uint8_t type, uint8_t bits);
