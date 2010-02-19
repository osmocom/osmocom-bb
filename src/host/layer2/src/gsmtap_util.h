#ifndef GSMTAP_ROUTINES_H
#define GSMTAP_ROUTINES_H

#include <stdint.h>
#include <netinet/in.h>

int gsmtap_init(void);

/* receive a message from L1/L2 and put it in GSMTAP */
int gsmtap_sendmsg(uint8_t ts, uint16_t arfcn, uint32_t fn,
		   const uint8_t *data, unsigned int len);

#endif
