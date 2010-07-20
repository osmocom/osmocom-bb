
#include <btsap.h>

#include <comm/sercomm.h>

struct msgb *sap_alloc_msg(uint8_t id, uint8_t nparams)
{
	struct msgb *m = msgb_alloc(HACK_MAX_MSG, "btsap");

	if(!m) {
		return m;
	}

	msgb_put_u8(m, id);
	msgb_put_u8(m, nparams);
	msgb_put_u16(m, 0); // reserved

	return m;
}

void sap_parse_msg(struct msgb *m, uint8_t *id, uint8_t *nparams)
{
	*id = msgb_get_u8(m);
	*nparams = msgb_get_u8(m);
	msgb_get_u16(m);
}

void sap_put_param(struct msgb *m, uint8_t id, uint16_t length, const uint8_t *value)
{
	/* write header */
	msgb_put_u8(m, id);
	msgb_put_u8(m, 0); // reserved
	msgb_put_u16(m, length);

	/* need to align payload to 4 bytes */
	size_t align = length % 4;
	if(align) {
		align = 4 - align;
	}

	/* append value and pad */
	uint8_t *p = msgb_put(m, length + align);
	memcpy(p, value, length);
	memset(p + length, 0, align);
}

void sap_get_param(struct msgb *m, uint8_t *id, uint16_t *length, const uint8_t **value)
{
	/* consume header */
	*id = msgb_get_u8(m);
	msgb_get_u8(m); // reserved
	*length = msgb_get_u16(m);

	/* need to align payload to 4 bytes */
	size_t align = *length % 4;
	if(align) {
		align = 4 - align;
	}

	/* consume data and pad */
	*value = msgb_get(m, (*length) + align);
}

void sap_send(struct msgb *m)
{
	sercomm_sendmsg(SC_DLCI_SAP, m);
}
