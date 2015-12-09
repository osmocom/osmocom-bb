#pragma once

#include <stdint.h>

#include <osmocom/core/endian.h>
#include <osmocom/gsm/protocol/gsm_04_12.h>

#ifndef OSMO_IS_LITTLE_ENDIAN
 #define OSMO_IS_LITTLE_ENDIAN 0
#endif

/* GSM TS 03.41 definitions also TS 23.041*/

#define GSM341_MAX_PAYLOAD	(GSM412_MSG_LEN-sizeof(struct gsm341_ms_message))
#define GSM341_MAX_CHARS	(GSM341_MAX_PAYLOAD*8/7)
#define GSM341_7BIT_PADDING	'\r'

/* Chapter 9.3.2 */
struct gsm341_ms_message {
	struct {
#if OSMO_IS_LITTLE_ENDIAN == 1
		uint8_t code_hi:6;
		uint8_t gs:2;
		uint8_t update:4;
		uint8_t code_lo:4;
#else
		uint8_t gs:2;
		uint8_t code_hi:6;
		uint8_t code_lo:4;
		uint8_t update:4;
#endif
	} serial;
	uint16_t msg_id;
	struct {
#if OSMO_IS_LITTLE_ENDIAN == 1
		uint8_t language:4;
		uint8_t group:4;
#else
		uint8_t group:4;
		uint8_t language:4;
#endif
	} dcs;
	struct {
#if OSMO_IS_LITTLE_ENDIAN == 1
		uint8_t total:4;
		uint8_t current:4;
#else
		uint8_t current:4;
		uint8_t total:4;
#endif
	} page;
	uint8_t data[0];
} __attribute__((packed));

/* Chapter 9.4.1.3 */
struct gsm341_etws_message {
	struct {
#if OSMO_IS_LITTLE_ENDIAN == 1
		uint8_t code_hi:4;
		uint8_t popup:1;
		uint8_t alert:1;
		uint8_t gs:2;
		uint8_t update:4;
		uint8_t code_lo:4;
#else
		uint8_t gs:2;
		uint8_t alert:1;
		uint8_t popup:1;
		uint8_t code_hi:4;
		uint8_t code_lo:4;
		uint8_t update:4;
#endif
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

/* Section 9.4.1.2.2 */
#define GSM341_MSGID_EOTD_ASSISTANCE			0x03E8
#define GSM341_MSGID_DGPS_CORRECTION			0x03E9
#define GSM341_MSGID_DGPS_EPH_CLOCK_COR			0x03EA
#define GSM341_MSGID_GPS_ALMANAC_OTHER			0x03EB
#define GSM341_MSGID_ETWS_EARTHQUAKE			0x1100
#define GSM341_MSGID_ETWS_TSUNAMI			0x1101
#define GSM341_MSGID_ETWS_QUAKE_AND_TSUNAMI		0x1102
#define GSM341_MSGID_ETWS_TEST				0x1103
#define GSM341_MSGID_ETWS_OTHER				0x1104
#define GSM341_MSGID_ETWS_CMAS_PRESIDENTIAL		0x1112
#define GSM341_MSGID_ETWS_CMAS_EXTREME_IMM_OBSERVED	0x1113
#define GSM341_MSGID_ETWS_CMAS_EXTREME_IMM_LIKELY	0x1114
#define GSM341_MSGID_ETWS_CMAS_EXTREME_EXP_OBSERVED	0x1115
#define GSM341_MSGID_ETWS_CMAS_EXTREME_EXP_LIKELY	0x1116
#define GSM341_MSGID_ETWS_CMAS_SEVERE_IMM_OBSERVED	0x1117
#define GSM341_MSGID_ETWS_CMAS_SEVERE_IMM_LIKELY	0x1118
#define GSM341_MSGID_ETWS_CMAS_SEVERE_EXP_OBSERVED	0x1119
#define GSM341_MSGID_ETWS_CMAS_SEVERE_EXP_LIKELY	0x111A
#define GSM341_MSGID_ETWS_CMAS_AMBER			0x111B
#define GSM341_MSGID_ETWS_CMAS_MONTHLY_TEST		0x111C
#define GSM341_MSGID_ETWS_CMAS_EXERCISE			0x111D
#define GSM341_MSGID_ETWS_CMAS_OPERATOR_DEFINED		0x111E
#define GSM341_MSGID_ETWS_CMAS_PRESIDENTIAL_AL		0x111F
#define GSM341_MSGID_ETWS_CMAS_EXTREME_IMM_OBSERVED_AL	0x1120
#define GSM341_MSGID_ETWS_CMAS_EXTREME_IMM_LIKELY_AL	0x1121
#define GSM341_MSGID_ETWS_CMAS_EXTREME_EXP_OBSERVED_AL	0x1122
#define GSM341_MSGID_ETWS_CMAS_EXTREME_EXP_LIKELY_AL	0x1123
#define GSM341_MSGID_ETWS_CMAS_SEVERE_IMM_OBSERVED_AL	0x1124
#define GSM341_MSGID_ETWS_CMAS_SEVERE_IMM_LIKELY_AL	0x1125
#define GSM341_MSGID_ETWS_CMAS_SEVERE_EXP_OBSERVED_AL	0x1126
#define GSM341_MSGID_ETWS_CMAS_SEVERE_EXP_LIKELY_AL	0x1127
#define GSM341_MSGID_ETWS_CMAS_AMBER_AL			0x1128
#define GSM341_MSGID_ETWS_CMAS_MONTHLY_TEST_AL		0x1129
#define GSM341_MSGID_ETWS_CMAS_EXERCISE_AL		0x112A
#define GSM341_MSGID_ETWS_CMAS_OPERATOR_DEFINED_AL	0x112B
#define GSM341_MSGID_ETWS_EU_INFO_LOCAL_LANGUAGE	0x1900
