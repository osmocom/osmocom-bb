#pragma once

#include <osmocom/core/prim.h>

/* enumeration of GSM related SAPs */
enum osmo_gsm_sap {
	SAP_GSM_PH	= _SAP_GSM_BASE,
	SAP_GSM_DL,
	SAP_GSM_MDL,

	SAP_BSSGP_GMM,
	SAP_BSSGP_LL,
	SAP_BSSGP_NM,
	SAP_BSSGP_PFM,
};
