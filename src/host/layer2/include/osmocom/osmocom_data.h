#ifndef osmocom_data_h
#define osmocom_data_h

#include <osmocore/select.h>
#include <osmocore/gsm_utils.h>

/* One Mobilestation for osmocom */
struct osmocom_ms {
	struct bsc_fd bfd;
	enum gsm_band band;
	int arfcn;
};

#endif
