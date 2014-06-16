#pragma once

#include <stdint.h>
#include <osmocom/core/write_queue.h>
#include <osmocom/core/select.h>

/*! \defgroup gsmtap GSMTAP
 *  @{
 */
/*! \file gsmtap_util.h */

uint8_t chantype_rsl2gsmtap(uint8_t rsl_chantype, uint8_t rsl_link_id);

struct msgb *gsmtap_makemsg_ex(uint8_t type, uint16_t arfcn, uint8_t ts, uint8_t chan_type,
			    uint8_t ss, uint32_t fn, int8_t signal_dbm,
			    uint8_t snr, const uint8_t *data, unsigned int len);

struct msgb *gsmtap_makemsg(uint16_t arfcn, uint8_t ts, uint8_t chan_type,
			    uint8_t ss, uint32_t fn, int8_t signal_dbm,
			    uint8_t snr, const uint8_t *data, unsigned int len);

/*! \brief one gsmtap instance */
struct gsmtap_inst {
	int ofd_wq_mode;	/*!< \brief wait queue mode? */
	struct osmo_wqueue wq;	/*!< \brief the wait queue */
	struct osmo_fd sink_ofd;/*!< \brief file descriptor */
};

/*! \brief obtain the file descriptor associated with a gsmtap instance */
static inline int gsmtap_inst_fd(struct gsmtap_inst *gti)
{
	return gti->wq.bfd.fd;
}

int gsmtap_source_init_fd(const char *host, uint16_t port);

int gsmtap_source_add_sink_fd(int gsmtap_fd);

struct gsmtap_inst *gsmtap_source_init(const char *host, uint16_t port,
					int ofd_wq_mode);

int gsmtap_source_add_sink(struct gsmtap_inst *gti);

int gsmtap_sendmsg(struct gsmtap_inst *gti, struct msgb *msg);

int gsmtap_send_ex(struct gsmtap_inst *gti, uint8_t type, uint16_t arfcn, uint8_t ts,
		uint8_t chan_type, uint8_t ss, uint32_t fn,
		int8_t signal_dbm, uint8_t snr, const uint8_t *data,
		unsigned int len);

int gsmtap_send(struct gsmtap_inst *gti, uint16_t arfcn, uint8_t ts,
		uint8_t chan_type, uint8_t ss, uint32_t fn,
		int8_t signal_dbm, uint8_t snr, const uint8_t *data,
		unsigned int len);

/*! @} */
