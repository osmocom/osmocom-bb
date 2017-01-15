/* GSMTAP network logging support code */

/* (C) 2016 by Harald Welte <laforge@gnumonks.org>
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

/*! \addtogroup logging
 *  @{
 */

/*! \file logging_gsmtap.c */

#include "../config.h"

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <arpa/inet.h>

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#include <osmocom/core/talloc.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/gsmtap.h>
#include <osmocom/core/gsmtap_util.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/timer.h>

#define	GSMTAP_LOG_MAX_SIZE 4096

static void _gsmtap_raw_output(struct log_target *target, int subsys,
			       unsigned int level, const char *file,
			       int line, int cont, const char *format,
			       va_list ap)
{
	struct msgb *msg;
	struct gsmtap_hdr *gh;
	struct gsmtap_osmocore_log_hdr *golh;
	const char *subsys_name = log_category_name(subsys);
	struct timeval tv;
	int rc;

	/* get timestamp ASAP */
	osmo_gettimeofday(&tv, NULL);

	msg = msgb_alloc(sizeof(*gh)+sizeof(*golh)+GSMTAP_LOG_MAX_SIZE,
			 "GSMTAP logging");

	/* GSMTAP header */
	gh = (struct gsmtap_hdr *) msgb_put(msg, sizeof(*gh));
	memset(gh, 0, sizeof(*gh));
	gh->version = GSMTAP_VERSION;
	gh->hdr_len = sizeof(*gh)/4;
	gh->type = GSMTAP_TYPE_OSMOCORE_LOG;

	/* Logging header */
	golh = (struct gsmtap_osmocore_log_hdr *) msgb_put(msg, sizeof(*golh));
	osmo_strlcpy(golh->proc_name, target->tgt_gsmtap.ident,
		     sizeof(golh->proc_name));
	if (subsys_name)
		osmo_strlcpy(golh->subsys, subsys_name+1, sizeof(golh->subsys));
	else
		golh->subsys[0] = '\0';
	osmo_strlcpy(golh->src_file.name, file, sizeof(golh->src_file.name));
	golh->src_file.line_nr = htonl(line);
	golh->level = level;
	/* we always store the timestamp in the message, irrespective
	 * of hat prrint_[ext_]timestamp say */
	golh->ts.sec = htonl(tv.tv_sec);
	golh->ts.usec = htonl(tv.tv_usec);

	rc = vsnprintf((char *) msg->tail, msgb_tailroom(msg), format, ap);
	if (rc < 0)
		return;
	msgb_put(msg, rc);

	gsmtap_sendmsg(target->tgt_gsmtap.gsmtap_inst, msg);
}

/*! \brief Create a new logging target for GSMTAP logging
 *  \param[in] ident string identifier
 *  \returns Log target in case of success, NULL in case of error
 */
struct log_target *log_target_create_gsmtap(const char *host, uint16_t port,
					    const char *ident,
					    bool ofd_wq_mode,
					    bool add_sink)
{
	struct log_target *target;
	struct gsmtap_inst *gti;

	target = log_target_create();
	if (!target)
		return NULL;

	gti = gsmtap_source_init(host, port, ofd_wq_mode);
	if (!gti) {
		log_target_destroy(target);
		return NULL;
	}

	if (add_sink)
		gsmtap_source_add_sink(gti);

	target->tgt_gsmtap.gsmtap_inst = gti;
	target->tgt_gsmtap.ident = talloc_strdup(target, ident);
	target->tgt_gsmtap.hostname = talloc_strdup(target, host);

	target->type = LOG_TGT_TYPE_GSMTAP;
	target->raw_output = _gsmtap_raw_output;

	return target;
}

/* @} */
