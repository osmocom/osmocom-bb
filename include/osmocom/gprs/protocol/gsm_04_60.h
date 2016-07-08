#pragma once

/* General Packet Radio Service (GPRS)
 * Radio Link Control / Medium Access Control (RLC/MAC) protocol
 * 3GPP TS 04.60 version 8.27.0 Release 1999
 */

#include <stdint.h>

#if OSMO_IS_LITTLE_ENDIAN == 1
/* TS 04.60  10.3a.4.1.1 */
struct gprs_rlc_ul_header_egprs_1 {
	uint8_t r:1,
		 si:1,
		 cv:4,
		 tfi_hi:2;
	uint8_t tfi_lo:3,
		 bsn1_hi:5;
	uint8_t bsn1_lo:6,
		 bsn2_hi:2;
	uint8_t bsn2_lo:8;
	uint8_t cps:5,
		 rsb:1,
		 pi:1,
		 spare_hi:1;
	uint8_t spare_lo:6,
		 dummy:2;
} __attribute__ ((packed));

/* TS 04.60  10.3a.4.2.1 */
struct gprs_rlc_ul_header_egprs_2 {
	uint8_t r:1,
		 si:1,
		 cv:4,
		 tfi_hi:2;
	uint8_t tfi_lo:3,
		 bsn1_hi:5;
	uint8_t bsn1_lo:6,
		 cps_hi:2;
	uint8_t cps_lo:1,
		 rsb:1,
		 pi:1,
		 spare_hi:5;
	uint8_t spare_lo:5,
		 dummy:3;
} __attribute__ ((packed));

/* TS 04.60  10.3a.4.3.1 */
struct gprs_rlc_ul_header_egprs_3 {
	uint8_t r:1,
		 si:1,
		 cv:4,
		 tfi_hi:2;
	uint8_t tfi_lo:3,
		 bsn1_hi:5;
	uint8_t bsn1_lo:6,
		 cps_hi:2;
	uint8_t cps_lo:2,
		 spb:2,
		 rsb:1,
		 pi:1,
		 spare:1,
		 dummy:1;
} __attribute__ ((packed));

struct gprs_rlc_dl_header_egprs_1 {
	uint8_t usf:3,
		 es_p:2,
		 rrbp:2,
		 tfi_hi:1;
	uint8_t tfi_lo:4,
		 pr:2,
		 bsn1_hi:2;
	uint8_t bsn1_mid:8;
	uint8_t bsn1_lo:1,
		 bsn2_hi:7;
	uint8_t bsn2_lo:3,
		 cps:5;
} __attribute__ ((packed));

struct gprs_rlc_dl_header_egprs_2 {
	uint8_t usf:3,
		 es_p:2,
		 rrbp:2,
		 tfi_hi:1;
	uint8_t tfi_lo:4,
		 pr:2,
		 bsn1_hi:2;
	uint8_t bsn1_mid:8;
	uint8_t bsn1_lo:1,
		 cps:3,
		 dummy:4;
} __attribute__ ((packed));

struct gprs_rlc_dl_header_egprs_3 {
	uint8_t usf:3,
		 es_p:2,
		 rrbp:2,
		 tfi_hi:1;
	uint8_t tfi_lo:4,
		 pr:2,
		 bsn1_hi:2;
	uint8_t bsn1_mid:8;
	uint8_t bsn1_lo:1,
		 cps:4,
		 spb:2,
		 dummy:1;
} __attribute__ ((packed));
#else
/* TS 04.60  10.3a.4.1.1 */
struct gprs_rlc_ul_header_egprs_1 {
	uint8_t tfi_hi:2,
		 cv:4,
		 si:1,
		 r:1;
	uint8_t bsn1_hi:5,
		tfi_lo:3;
	uint8_t bsn2_hi:2,
		bsn1_lo:6;
	uint8_t bsn2_lo:8;
	uint8_t spare_hi:1,
		pi:1,
		rsb:1,
		cps:5;
	uint8_t dummy:2,
		spare_lo:6;
} __attribute__ ((packed));

/* TS 04.60  10.3a.4.2.1 */
struct gprs_rlc_ul_header_egprs_2 {
	uint8_t tfi_hi:2,
		 cv:4,
		 si:1,
		 r:1;
	uint8_t bsn1_hi:5,
		 tfi_lo:3;
	uint8_t cps_hi:2,
		 bsn1_lo:6;
	uint8_t spare_hi:5,
		 pi:1,
		 rsb:1,
		 cps_lo:1;
	uint8_t dummy:3,
		 spare_lo:5;
} __attribute__ ((packed));

/* TS 04.60  10.3a.4.3.1 */
struct gprs_rlc_ul_header_egprs_3 {
	uint8_t tfi_hi:2,
		 cv:4,
		 si:1,
		 r:1;
	uint8_t bsn1_hi:5,
		 tfi_lo:3;
	uint8_t cps_hi:2,
		 bsn1_lo:6;
	uint8_t dummy:1,
		 spare:1,
		 pi:1,
		 rsb:1,
		 spb:2,
		 cps_lo:2;
} __attribute__ ((packed));

struct gprs_rlc_dl_header_egprs_1 {
	uint8_t tfi_hi:1,
		 rrbp:2,
		 es_p:2,
		 usf:3;
	uint8_t bsn1_hi:2,
		 pr:2,
		 tfi_lo:4;
	uint8_t bsn1_mid:8;
	uint8_t bsn2_hi:7,
		 bsn1_lo:1;
	uint8_t cps:5,
		 bsn2_lo:3;
} __attribute__ ((packed));

struct gprs_rlc_dl_header_egprs_2 {
	uint8_t tfi_hi:1,
		 rrbp:2,
		 es_p:2,
		 usf:3;
	uint8_t bsn1_hi:2,
		 pr:2,
		 tfi_lo:4;
	uint8_t bsn1_mid:8;
	uint8_t dummy:4,
		 cps:3,
		 bsn1_lo:1;
} __attribute__ ((packed));

struct gprs_rlc_dl_header_egprs_3 {
	uint8_t tfi_hi:1,
		 rrbp:2,
		 es_p:2,
		 usf:3;
	uint8_t bsn1_hi:2,
		 pr:2,
		 tfi_lo:4;
	uint8_t bsn1_mid:8;
	uint8_t dummy:1,
		 spb:2,
		 cps:4,
		 bsn1_lo:1;
} __attribute__ ((packed));
#endif
