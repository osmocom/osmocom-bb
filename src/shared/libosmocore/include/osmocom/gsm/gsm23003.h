#pragma once
#include <stdint.h>

/* 23.003 Chapter 12.1 */
struct osmo_plmn_id {
	uint16_t mcc;
	uint16_t mnc;
};

/* 4.1 */
struct osmo_location_area_id {
	struct osmo_plmn_id plmn;
	uint16_t lac;
};

/* 4.2 */
struct osmo_routing_area_id {
	struct osmo_location_area_id lac;
	uint8_t rac;
};

/* 4.3.1 */
struct osmo_cell_global_id {
	struct osmo_location_area_id lai;
	uint16_t cell_identity;
};

/* 12.5 */
struct osmo_service_area_id {
	struct osmo_location_area_id lai;
	uint16_t sac;
};

/* 12.6 */
struct osmo_shared_network_area_id {
	struct osmo_plmn_id plmn;
	uint32_t snac;
};

/* 5.1 */
enum osmo_gsn_addr_type {
	GSN_ADDR_TYPE_IPV4	= 0,
	GSN_ADDR_TYPE_IPV6	= 1,
};

/* 5.1 */
struct osmo_gsn_address {
	enum osmo_gsn_addr_type type;
	uint8_t length;
	uint8_t addr[16];
};

/* 19.4.2.3 */
struct osmo_tracking_area_id {
	struct osmo_plmn_id plmn;
	uint16_t tac;
};

struct osmo_eutran_cell_global_id {
	struct osmo_plmn_id plmn;
	uint32_t eci; /* FIXME */
};

/* 2.8.1 */
struct osmo_mme_id {
	uint16_t group_id;
	uint8_t code;
};

/* 2.8.1 */
struct osmo_gummei {
	struct osmo_plmn_id plmn;
	struct osmo_mme_id mme;
};

/* 2.8.1 */
struct osmo_guti {
	struct osmo_gummei gummei;
	uint32_t mtmsi;
};
