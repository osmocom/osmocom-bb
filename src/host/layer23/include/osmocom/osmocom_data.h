#ifndef osmocom_data_h
#define osmocom_data_h

#include <osmocore/select.h>
#include <osmocore/gsm_utils.h>
#include <osmocore/write_queue.h>

#include <osmocom/lapdm.h>

/* One Mobilestation for osmocom */
struct osmocom_ms {
	struct write_queue wq;
	enum gsm_band band;
	int arfcn;

	struct lapdm_entity lapdm_dcch;
	struct lapdm_entity lapdm_acch;
};

#endif
