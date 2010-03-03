#ifndef osmocom_data_h
#define osmocom_data_h

#include <osmocore/select.h>
#include <osmocore/gsm_utils.h>

#include <osmocom/lapdm.h>

/* One Mobilestation for osmocom */
struct osmocom_ms {
	struct bsc_fd bfd;
	enum gsm_band band;
	int arfcn;

	struct lapdm_entity lapdm_dcch;
	struct lapdm_entity lapdm_acch;
};

#endif
