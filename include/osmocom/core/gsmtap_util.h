#ifndef _GSMTAP_UTIL_H
#define _GSMTAP_UTIL_H

#include <stdint.h>
#include <osmocom/core/write_queue.h>
#include <osmocom/core/select.h>

/* convert RSL channel number to GSMTAP channel type */
uint8_t chantype_rsl2gsmtap(uint8_t rsl_chantype, uint8_t rsl_link_id);

/* generate msgb from data + metadata */
struct msgb *gsmtap_makemsg(uint16_t arfcn, uint8_t ts, uint8_t chan_type,
			    uint8_t ss, uint32_t fn, int8_t signal_dbm,
			    uint8_t snr, const uint8_t *data, unsigned int len);

/* one gsmtap instance */
struct gsmtap_inst {
	int ofd_wq_mode;
	struct osmo_wqueue wq;
	struct osmo_fd sink_ofd;
};

static inline int gsmtap_inst_fd(struct gsmtap_inst *gti)
{
	return gti->wq.bfd.fd;
}

/* Open a GSMTAP source (sending) socket, conncet it to host/port and
 * return resulting fd */
int gsmtap_source_init_fd(const char *host, uint16_t port);

/* Add a local sink to an existing GSMTAP source and return fd */
int gsmtap_source_add_sink_fd(int gsmtap_fd);

/* Open GSMTAP source (sending) socket, connect it to host/port,
 * allocate 'struct gsmtap_inst' and optionally osmo_fd/osmo_wqueue
 * registration */
struct gsmtap_inst *gsmtap_source_init(const char *host, uint16_t port,
					int ofd_wq_mode);

/* Add a local sink to an existing GSMTAP source instance */
int gsmtap_source_add_sink(struct gsmtap_inst *gti);

/* Send a msgb through a GSMTAP source */
int gsmtap_sendmsg(struct gsmtap_inst *gti, struct msgb *msg);

/* generate a message and send it via GSMTAP */
int gsmtap_send(struct gsmtap_inst *gti, uint16_t arfcn, uint8_t ts,
		uint8_t chan_type, uint8_t ss, uint32_t fn,
		int8_t signal_dbm, uint8_t snr, const uint8_t *data,
		unsigned int len);

#endif /* _GSMTAP_UTIL_H */
