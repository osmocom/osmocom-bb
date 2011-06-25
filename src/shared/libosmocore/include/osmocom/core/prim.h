#ifndef OSMO_PRIMITIVE_H
#define OSMO_PRIMITIVE_H

#include <stdint.h>
#include <osmocom/core/msgb.h>

enum osmo_prim_operation {
	PRIM_OP_REQUEST,
	PRIM_OP_RESPONSE,
	PRIM_OP_INDICATION,
	PRIM_OP_CONFIRM,
};

#define _SAP_GSM_SHIFT	24

#define _SAP_GSM_BASE	(0x01 << _SAP_GSM_SHIFT)
#define _SAP_TETRA_BASE	(0x02 << _SAP_GSM_SHIFT)

struct osmo_prim_hdr {
	unsigned int sap;
	unsigned int primitive;
	enum osmo_prim_operation operation;
	struct msgb *msg;	/* message containing associated data */
};

static inline void
osmo_prim_init(struct osmo_prim_hdr *oph, unsigned int sap,
		unsigned int primitive, enum osmo_prim_operation operation,
		struct msgb *msg)
{
	oph->sap = sap;
	oph->primitive = primitive;
	oph->operation = operation;
	oph->msg = msg;
}

typedef int (*osmo_prim_cb)(struct osmo_prim_hdr *oph, void *ctx);
#endif
