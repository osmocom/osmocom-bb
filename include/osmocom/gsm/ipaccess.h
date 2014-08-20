#pragma once

#include <stdint.h>

#include <osmocom/core/msgb.h>
#include <osmocom/gsm/tlv.h>

/* internal (host-only) data structure */
struct ipaccess_unit {
	uint16_t site_id;
	uint16_t bts_id;
	uint16_t trx_id;
	char *unit_name;
	char *equipvers;
	char *swversion;
	uint8_t mac_addr[6];
	char *location1;
	char *location2;
	char *serno;
};

/* obtain the human-readable name of an IPA CCM ID TAG */
const char *ipaccess_idtag_name(uint8_t tag);

/* parse a buffer of ID tags into a osmocom TLV style representation */
int ipaccess_idtag_parse(struct tlv_parsed *dec, unsigned char *buf, int len);

/* parse an Unit ID in string format into the 'ipaccess_unit' data structure */
int ipaccess_parse_unitid(const char *str, struct ipaccess_unit *unit_data);

/* fill a 'struct ipaccess_unit' based on a parsed IDTAG TLV */
int ipaccess_tlv_to_unitdata(struct ipaccess_unit *ud,
			     const struct tlv_parsed *tp);

/* Send an IPA message to the given FD */
int ipaccess_send(int fd, const void *msg, size_t msglen);

/* Send an IPA CCM PONG via the given FD */
int ipaccess_send_pong(int fd);

/* Send an IPA CCM ID_ACK via the given FD */
int ipaccess_send_id_ack(int fd);

/* Send an IPA CCM ID_REQ via the given FD */
int ipaccess_send_id_req(int fd);

/* Common handling of IPA CCM, BSC side */
int ipaccess_rcvmsg_base(struct msgb *msg, struct osmo_fd *bfd);

/* Common handling of IPA CCM, BTS side */
int ipaccess_rcvmsg_bts_base(struct msgb *msg, struct osmo_fd *bfd);

/* prepend (push) an ipaccess_head_ext to the msgb */
void ipaccess_prepend_header_ext(struct msgb *msg, int proto);

/* prepend (push) an ipaccess_head to the msgb */
void ipaccess_prepend_header(struct msgb *msg, int proto);

struct msgb *ipa_msg_alloc(int headroom);

int ipa_msg_recv(int fd, struct msgb **rmsg);
int ipa_msg_recv_buffered(int fd, struct msgb **rmsg, struct msgb **tmp_msg);
