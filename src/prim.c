#include <osmocom/core/utils.h>
#include <osmocom/core/prim.h>

const struct value_string osmo_prim_op_names[5] = {
	{ PRIM_OP_REQUEST,			"request" },
	{ PRIM_OP_RESPONSE,			"response" },
	{ PRIM_OP_INDICATION,			"indication" },
	{ PRIM_OP_CONFIRM,			"confirm" },
	{ 0, NULL }
};
