#ifndef osmocom_data_h
#define osmocom_data_h

#include <osmocom/select.h>

/* taken from OpenBSC */
enum gsm_band {
	GSM_BAND_400,
	GSM_BAND_850,
	GSM_BAND_900,
	GSM_BAND_1800,
	GSM_BAND_1900,
};

/* One Mobilestation for osmocom */
struct osmocom_ms {
	struct bsc_fd bfd;
	enum gsm_band band;
	int arfcn;
};

#endif
