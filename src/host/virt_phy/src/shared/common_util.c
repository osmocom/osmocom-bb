/*
 * Utility function used both in osmo bts virt and osmocom bb virt.
 */

#include <osmocom/core/gsmtap.h>
#include <osmocom/gsm/rsl.h>
#include <virtphy/common_util.h>

void chantype_gsmtap2rsl(uint8_t gsmtap_chantype, uint8_t *rsl_chantype,
                         uint8_t *link_id)
{
	// switch case with removed acch flag
	switch (gsmtap_chantype & ~GSMTAP_CHANNEL_ACCH & 0xff) {
	case GSMTAP_CHANNEL_TCH_F: // TCH/F, FACCH/F
		*rsl_chantype = RSL_CHAN_Bm_ACCHs;
		break;
	case GSMTAP_CHANNEL_TCH_H: // TCH/H, FACCH/H
		*rsl_chantype = RSL_CHAN_Lm_ACCHs;
		break;
	case GSMTAP_CHANNEL_SDCCH4: // SDCCH/4
		*rsl_chantype = RSL_CHAN_SDCCH4_ACCH;
		break;
	case GSMTAP_CHANNEL_SDCCH8: // SDCCH/8
		*rsl_chantype = RSL_CHAN_SDCCH8_ACCH;
		break;
	case GSMTAP_CHANNEL_BCCH: // BCCH
		*rsl_chantype = RSL_CHAN_BCCH;
		break;
	case GSMTAP_CHANNEL_RACH: // RACH
		*rsl_chantype = RSL_CHAN_RACH;
		break;
	case GSMTAP_CHANNEL_PCH: // PCH
	case GSMTAP_CHANNEL_AGCH: // AGCH
		*rsl_chantype = RSL_CHAN_PCH_AGCH;
		break;
	case GSMTAP_CHANNEL_PDCH:
		*rsl_chantype = GSMTAP_CHANNEL_PDCH;
		break;
	}

	*link_id = gsmtap_chantype & GSMTAP_CHANNEL_ACCH ? LID_SACCH :
	                                                   LID_DEDIC;

}
