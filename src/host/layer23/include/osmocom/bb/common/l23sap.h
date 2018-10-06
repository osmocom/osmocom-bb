#pragma once

#include <osmocom/core/msgb.h>
#include <osmocom/bb/common/osmocom_data.h>

/* Logical channel link ID */
#define LID_SACCH 0x40
#define LID_DEDIC 0x00

#define CHAN_IS_SACCH(link_id) \
	((link_id & 0xc0) == LID_SACCH)

int l23sap_gsmtap_data_ind(struct osmocom_ms *ms, struct msgb *msg);
int l23sap_gsmtap_data_req(struct osmocom_ms *ms, struct msgb *msg);

int l23sap_data_ind(struct osmocom_ms *ms, struct msgb *msg);
int l23sap_data_conf(struct osmocom_ms *ms, struct msgb *msg);
int l23sap_rach_conf(struct osmocom_ms *ms, struct msgb *msg);
