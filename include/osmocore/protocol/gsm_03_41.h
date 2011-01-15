#ifndef PROTO_GSM_03_41_H
#define PROTO_GSM_03_41_H

#include <stdint.h>

/* GSM TS 03.41 definitions also TS 23.041*/

/* Chapter 9.3.2 */
struct gsm341_ms_message {
	struct {
		uint8_t code_hi:6;
		uint8_t gs:2;
		uint8_t update:4;
		uint8_t code_lo:4;
	} serial;
	uint16_t msg_id;
	struct {
		uint8_t language:4;
		uint8_t group:4;
	} dcs;
	struct {
		uint8_t total:4;
		uint8_t current:4;
	} page;
	uint8_t data[0];
} __attribute__((packed));

/* Chapter 9.4.1.3 */
struct gsm341_etws_message {
	struct {
		uint8_t code_hi:4;
		uint8_t popup:1;
		uint8_t alert:1;
		uint8_t gs:2;
		uint8_t update:4;
		uint8_t code_lo:4;
	} serial;
	uint16_t msg_id;
	uint16_t warning_type;
	uint8_t data[0];
} __attribute__((packed));

#define GSM341_MSG_CODE(ms) ((ms)->serial.code_lo | ((ms)->serial.code_hi << 4))

/* Section 9.3.2.1 - Geographical Scope */
#define GSM341_GS_CELL_WIDE_IMMED	0
#define GSM341_GS_PLMN_WIDE		1
#define GSM341_GS_LA_WIDE		2
#define GSM341_GS_CELL_WIDE		3

#endif /* PROTO_GSM_03_41_H */
