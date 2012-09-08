/* Syslog logging support code */

/* (C) 2011 by Harald Welte <laforge@gnumonks.org>
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

/*! \file logging_syslog.c */

#include "../config.h"

#ifdef HAVE_SYSLOG_H

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#include <osmocom/core/talloc.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/logging.h>

static int logp2syslog_level(unsigned int level)
{
	if (level >= LOGL_FATAL)
		return LOG_CRIT;
	else if (level >= LOGL_ERROR)
		return LOG_ERR;
	else if (level >= LOGL_NOTICE)
		return LOG_NOTICE;
	else if (level >= LOGL_INFO)
		return LOG_INFO;
	else
		return LOG_DEBUG;
}

static void _syslog_output(struct log_target *target,
			   unsigned int level, const char *log)
{
	syslog(logp2syslog_level(level), "%s", log);
}

/*! \brief Create a new logging target for syslog logging
 *  \param[in] ident syslog string identifier
 *  \param[in] option syslog options
 *  \param[in] facility syslog facility
 *  \returns Log target in case of success, NULL in case of error
 */
struct log_target *log_target_create_syslog(const char *ident, int option,
					    int facility)
{
	struct log_target *target;

	target = log_target_create();
	if (!target)
		return NULL;

	target->tgt_syslog.facility = facility;
	target->type = LOG_TGT_TYPE_SYSLOG;
	target->output = _syslog_output;

	openlog(ident, option, facility);

	return target;
}

#endif /* HAVE_SYSLOG_H */

/* @} */
