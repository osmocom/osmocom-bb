#ifndef _cellid_c_
#define _cellid_c_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>

struct gsm_cell_id {
	uint16_t mcc;
	uint16_t mnc;
	uint16_t lac;
	uint16_t cid;
};

static struct gsm_cell_id *_gcid = 0;

static char _gcid_str[32] = "\0";

const char *get_cid_str()
{
	if (_gcid)
		return _gcid_str;

	return "unknown";
}

void reset_cid()
{
	if (_gcid)
		free(_gcid);
	_gcid = 0;
}

void set_cid(uint8_t *digits, uint16_t nlac, uint16_t ncid)
{
	if (_gcid)
		return;

	_gcid = malloc(sizeof(struct gsm_cell_id));

	_gcid->mcc = (digits[0] & 0xf) * 100;
	_gcid->mcc += (digits[0] >> 4) * 10;
	_gcid->mcc += (digits[1] & 0xf) * 1;

	if ((digits[1] >> 4) == 0xf) {
		_gcid->mnc = (digits[2] & 0xf) * 10;
		_gcid->mnc += (digits[2] >> 4) * 1;
	} else {
		_gcid->mnc = (digits[2] & 0xf) * 100;
		_gcid->mnc += (digits[2] >> 4) * 10;
		_gcid->mnc += (digits[1] >> 4) * 1;
	}

	_gcid->lac = ntohs(nlac);
	_gcid->cid = ntohs(ncid);

	sprintf(_gcid_str, "%d_%d_%.04X_%.04X",
		_gcid->mcc, _gcid->mnc,
		_gcid->lac, _gcid->cid);

	printf("Cell ID: %s\n", _gcid_str);
	fflush(stdout);
}

void set_cid_from_si6(uint8_t *msg)
{
	struct gsm48_system_information_type_6 *si;

	if (_gcid)
		return;

	si = (struct gsm48_system_information_type_6 *) &msg[5];

	if (((msg[2] & 0x1c) == 0) &&
	    ((msg[4] & 0xfc) != 0) &&
	    (si->rr_protocol_discriminator == 6) &&
	    (si->system_information == 0x1e)) {
		_gcid = malloc(sizeof(struct gsm_cell_id));

		memset(_gcid, 0, sizeof(struct gsm_cell_id));

		_gcid->cid = ntohs(si->cell_identity);

		_gcid->lac = ntohs(si->lai.lac);

		_gcid->mcc = (si->lai.digits[0] & 0xf) * 100;
		_gcid->mcc += (si->lai.digits[0] >> 4) * 10;
		_gcid->mcc += (si->lai.digits[1] & 0xf) * 1;

		if ((si->lai.digits[1] >> 4) == 0xf) {
			_gcid->mnc = (si->lai.digits[2] & 0xf) * 10;
			_gcid->mnc += (si->lai.digits[2] >> 4) * 1;
		} else {
			_gcid->mnc = (si->lai.digits[2] & 0xf) * 100;
			_gcid->mnc += (si->lai.digits[2] >> 4) * 10;
			_gcid->mnc += (si->lai.digits[1] >> 4) * 1;
		}

		sprintf(_gcid_str, "%d_%d_%.04X_%.04X",
			_gcid->mcc, _gcid->mnc,
			_gcid->lac, _gcid->cid);

		printf("Cell ID: %s\n", _gcid_str);
		fflush(stdout);
	}
}

#endif
