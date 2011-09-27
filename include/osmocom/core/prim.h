#ifndef OSMO_PRIMITIVE_H
#define OSMO_PRIMITIVE_H

/*! \defgroup prim Osmocom primitives
 *  @{
 */

/*! \file prim.c */

#include <stdint.h>
#include <osmocom/core/msgb.h>

#define OSMO_PRIM(prim, op)	((prim << 8) | (op & 0xFF))
#define OSMO_PRIM_HDR(oph)	OSMO_PRIM((oph)->primitive, (oph)->operation)

/*! \brief primitive operation */
enum osmo_prim_operation {
	PRIM_OP_REQUEST,	/*!< \brief request */
	PRIM_OP_RESPONSE,	/*!< \brief response */
	PRIM_OP_INDICATION,	/*!< \brief indication */
	PRIM_OP_CONFIRM,	/*!< \brief cofirm */
};

#define _SAP_GSM_SHIFT	24

#define _SAP_GSM_BASE	(0x01 << _SAP_GSM_SHIFT)
#define _SAP_TETRA_BASE	(0x02 << _SAP_GSM_SHIFT)

/*! \brief primitive header */
struct osmo_prim_hdr {
	unsigned int sap;	/*!< \brief Service Access Point */
	unsigned int primitive;	/*!< \brief Primitive number */
	enum osmo_prim_operation operation; /*! \brief Primitive Operation */
	struct msgb *msg;	/*!< \brief \ref msgb containing associated data */
};

/*! \brief initialize a primitive header
 *  \param[in,out] oph primitive header
 *  \param[in] sap Service Access Point
 *  \param[in] primtive Primitive Number
 *  \param[in] operation Primitive Operation (REQ/RESP/IND/CONF)
 *  \param[in] msg Message
 */
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

/*! \brief primitive handler callback type */
typedef int (*osmo_prim_cb)(struct osmo_prim_hdr *oph, void *ctx);

#endif /* OSMO_PRIMITIVE_H */
