#ifndef _GSMTAP_UTIL_H
#define _GSMTAP_UTIL_H

#include <stdint.h>

/* convert RSL channel number to GSMTAP channel type */
uint8_t chantype_rsl2gsmtap(uint8_t rsl_chantype, uint8_t rsl_link_id);

/* receive a message from L1/L2 and put it in GSMTAP */
struct msgb *gsmtap_makemsg(uint16_t arfcn, uint8_t ts, uint8_t chan_type,
			    uint8_t ss, uint32_t fn, int8_t signal_dbm,
			    uint8_t snr, const uint8_t *data, unsigned int len);

/* receive a message from L1/L2 and put it in GSMTAP */
int gsmtap_sendmsg(uint16_t arfcn, uint8_t ts, uint8_t chan_type, uint8_t ss,
		   uint32_t fn, int8_t signal_dbm, uint8_t snr,
		   const uint8_t *data, unsigned int len);

int gsmtap_init(uint32_t dst_ip);

#endif /* _GSMTAP_UTIL_H */
