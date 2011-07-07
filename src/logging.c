/* Debugging/Logging support code */

/* (C) 2008-2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2008 by Holger Hans Peter Freyther <zecke@selfish.org>
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

#include "../config.h"

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#include <time.h>
#include <errno.h>

#include <osmocom/core/talloc.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/logging.h>

#include <osmocom/vty/logging.h>	/* for LOGGING_STR. */

struct log_info *osmo_log_info;

static struct log_context log_context;
static void *tall_log_ctx = NULL;
LLIST_HEAD(osmo_log_target_list);

#define LOGLEVEL_DEFS	6	/* Number of loglevels.*/

static const struct value_string loglevel_strs[LOGLEVEL_DEFS+1] = {
	{ 0,		"EVERYTHING" },
	{ LOGL_DEBUG,	"DEBUG" },
	{ LOGL_INFO,	"INFO" },
	{ LOGL_NOTICE,	"NOTICE" },
	{ LOGL_ERROR,	"ERROR" },
	{ LOGL_FATAL,	"FATAL" },
	{ 0, NULL },
};

#define INT2IDX(x)	(-1*(x)-1)
static const struct log_info_cat internal_cat[OSMO_NUM_DLIB] = {
	[INT2IDX(DLGLOBAL)] = {	/* -1 becomes 0 */
		.name = "DLGLOBAL",
		.description = "Library-internal global log family",
		.loglevel = LOGL_NOTICE,
		.enabled = 1,
	},
	[INT2IDX(DLLAPDM)] = {	/* -2 becomes 1 */
		.name = "DLLAPDM",
		.description = "LAPDm in libosmogsm",
		.loglevel = LOGL_NOTICE,
		.enabled = 1,
	},
	[INT2IDX(DINP)] = {
		.name = "DINP",
		.description = "A-bis Intput Subsystem",
		.loglevel = LOGL_NOTICE,
		.enabled = 1,
	},
	[INT2IDX(DMUX)] = {
		.name = "DMUX",
		.description = "A-bis B-Subchannel TRAU Frame Multiplex",
		.loglevel = LOGL_NOTICE,
		.enabled = 1,
	},
	[INT2IDX(DMI)] = {
		.name = "DMI",
		.description = "A-bis Input Driver for Signalling",
		.enabled = 0, .loglevel = LOGL_NOTICE,
	},
	[INT2IDX(DMIB)] = {
		.name = "DMIB",
		.description = "A-bis Input Driver for B-Channels (voice)",
		.enabled = 0, .loglevel = LOGL_NOTICE,
	},
	[INT2IDX(DRSL)] = {
		.name = "DRSL",
		.description = "A-bis Radio Siganlling Link (RSL)",
		.color = "\033[1;35m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[INT2IDX(DNM)] = {
		.name = "DNM",
		.description = "A-bis Network Management / O&M (NM/OML)",
		.color = "\033[1;36m",
		.enabled = 1, .loglevel = LOGL_INFO,
	},
};

/* You have to keep this in sync with the structure loglevel_strs. */
const char *loglevel_descriptions[LOGLEVEL_DEFS+1] = {
	"Log simply everything",
	"Log debug messages and higher levels",
	"Log informational messages and higher levels",
	"Log noticable messages and higher levels",
	"Log error messages and higher levels",
	"Log only fatal messages",
	NULL,
};

/* special magic for negative (library-internal) log subsystem numbers */
static int subsys_lib2index(int subsys)
{
	return (subsys * -1) + (osmo_log_info->num_cat_user-1);
}

int log_parse_level(const char *lvl)
{
	return get_string_value(loglevel_strs, lvl);
}

const char *log_level_str(unsigned int lvl)
{
	return get_value_string(loglevel_strs, lvl);
}

int log_parse_category(const char *category)
{
	int i;

	for (i = 0; i < osmo_log_info->num_cat; ++i) {
		if (osmo_log_info->cat[i].name == NULL)
			continue;
		if (!strcasecmp(osmo_log_info->cat[i].name+1, category))
			return i;
	}

	return -EINVAL;
}

/*
 * Parse the category mask.
 * The format can be this: category1:category2:category3
 * or category1,2:category2,3:...
 */
void log_parse_category_mask(struct log_target* target, const char *_mask)
{
	int i = 0;
	char *mask = strdup(_mask);
	char *category_token = NULL;

	/* Disable everything to enable it afterwards */
	for (i = 0; i < osmo_log_info->num_cat; ++i)
		target->categories[i].enabled = 0;

	category_token = strtok(mask, ":");
	do {
		for (i = 0; i < osmo_log_info->num_cat; ++i) {
			char* colon = strstr(category_token, ",");
			int length = strlen(category_token);

			if (!osmo_log_info->cat[i].name)
				continue;

			if (colon)
			    length = colon - category_token;

			if (strncasecmp(osmo_log_info->cat[i].name,
					category_token, length) == 0) {
				int level = 0;

				if (colon)
					level = atoi(colon+1);

				target->categories[i].enabled = 1;
				target->categories[i].loglevel = level;
			}
		}
	} while ((category_token = strtok(NULL, ":")));

	free(mask);
}

static const char* color(int subsys)
{
	if (subsys < osmo_log_info->num_cat)
		return osmo_log_info->cat[subsys].color;

	return NULL;
}

static void _output(struct log_target *target, unsigned int subsys,
		    unsigned int level, char *file, int line, int cont,
		    const char *format, va_list ap)
{
	char buf[4096];
	int ret, len = 0, offset = 0, rem = sizeof(buf);

	/* are we using color */
	if (target->use_color) {
		const char *c = color(subsys);
		if (c) {
			ret = snprintf(buf + offset, rem, "%s", color(subsys));
			if (ret < 0)
				goto err;
			OSMO_SNPRINTF_RET(ret, rem, offset, len);
		}
	}
	if (!cont) {
		if (target->print_timestamp) {
			char *timestr;
			time_t tm;
			tm = time(NULL);
			timestr = ctime(&tm);
			timestr[strlen(timestr)-1] = '\0';
			ret = snprintf(buf + offset, rem, "%s ", timestr);
			if (ret < 0)
				goto err;
			OSMO_SNPRINTF_RET(ret, rem, offset, len);
		}
		ret = snprintf(buf + offset, rem, "<%4.4x> %s:%d ",
				subsys, file, line);
		if (ret < 0)
			goto err;
		OSMO_SNPRINTF_RET(ret, rem, offset, len);
	}
	ret = vsnprintf(buf + offset, rem, format, ap);
	if (ret < 0)
		goto err;
	OSMO_SNPRINTF_RET(ret, rem, offset, len);

	ret = snprintf(buf + offset, rem, "%s",
			target->use_color ? "\033[0;m" : "");
	if (ret < 0)
		goto err;
	OSMO_SNPRINTF_RET(ret, rem, offset, len);
err:
	buf[sizeof(buf)-1] = '\0';
	target->output(target, level, buf);
}

static void _logp(int subsys, int level, char *file, int line,
		  int cont, const char *format, va_list ap)
{
	struct log_target *tar;

	if (subsys < 0)
		subsys = subsys_lib2index(subsys);

	if (subsys > osmo_log_info->num_cat)
		subsys = DLGLOBAL;

	llist_for_each_entry(tar, &osmo_log_target_list, entry) {
		struct log_category *category;
		int output = 0;
		va_list bp;

		category = &tar->categories[subsys];
		/* subsystem is not supposed to be logged */
		if (!category->enabled)
			continue;

		/* Check the global log level */
		if (tar->loglevel != 0 && level < tar->loglevel)
			continue;

		/* Check the category log level */
		if (tar->loglevel == 0 && category->loglevel != 0 &&
		    level < category->loglevel)
			continue;

		/* Apply filters here... if that becomes messy we will
		 * need to put filters in a list and each filter will
		 * say stop, continue, output */
		if ((tar->filter_map & LOG_FILTER_ALL) != 0)
			output = 1;
		else if (osmo_log_info->filter_fn)
			output = osmo_log_info->filter_fn(&log_context,
						       tar);
		if (!output)
			continue;

		/* According to the manpage, vsnprintf leaves the value of ap
		 * in undefined state. Since _output uses vsnprintf and it may
		 * be called several times, we have to pass a copy of ap. */
		va_copy(bp, ap);
		_output(tar, subsys, level, file, line, cont, format, bp);
		va_end(bp);
	}
}

void logp(int subsys, char *file, int line, int cont,
	  const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	_logp(subsys, LOGL_DEBUG, file, line, cont, format, ap);
	va_end(ap);
}

void logp2(int subsys, unsigned int level, char *file, int line, int cont, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	_logp(subsys, level, file, line, cont, format, ap);
	va_end(ap);
}

void log_add_target(struct log_target *target)
{
	llist_add_tail(&target->entry, &osmo_log_target_list);
}

void log_del_target(struct log_target *target)
{
	llist_del(&target->entry);
}

void log_reset_context(void)
{
	memset(&log_context, 0, sizeof(log_context));
}

int log_set_context(uint8_t ctx_nr, void *value)
{
	if (ctx_nr > LOG_MAX_CTX)
		return -EINVAL;

	log_context.ctx[ctx_nr] = value;

	return 0;
}

void log_set_all_filter(struct log_target *target, int all)
{
	if (all)
		target->filter_map |= LOG_FILTER_ALL;
	else
		target->filter_map &= ~LOG_FILTER_ALL;
}

void log_set_use_color(struct log_target *target, int use_color)
{
	target->use_color = use_color;
}

void log_set_print_timestamp(struct log_target *target, int print_timestamp)
{
	target->print_timestamp = print_timestamp;
}

void log_set_log_level(struct log_target *target, int log_level)
{
	target->loglevel = log_level;
}

void log_set_category_filter(struct log_target *target, int category,
			       int enable, int level)
{
	if (category >= osmo_log_info->num_cat)
		return;
	target->categories[category].enabled = !!enable;
	target->categories[category].loglevel = level;
}

static void _file_output(struct log_target *target, unsigned int level,
			 const char *log)
{
	fprintf(target->tgt_file.out, "%s", log);
	fflush(target->tgt_file.out);
}

struct log_target *log_target_create(void)
{
	struct log_target *target;
	unsigned int i;

	target = talloc_zero(tall_log_ctx, struct log_target);
	if (!target)
		return NULL;

	target->categories = talloc_zero_array(target, 
						struct log_category,
						osmo_log_info->num_cat);
	if (!target->categories) {
		talloc_free(target);
		return NULL;
	}

	INIT_LLIST_HEAD(&target->entry);

	/* initialize the per-category enabled/loglevel from defaults */
	for (i = 0; i < osmo_log_info->num_cat; i++) {
		struct log_category *cat = &target->categories[i];
		cat->enabled = osmo_log_info->cat[i].enabled;
		cat->loglevel = osmo_log_info->cat[i].loglevel;
	}

	/* global settings */
	target->use_color = 1;
	target->print_timestamp = 0;

	/* global log level */
	target->loglevel = 0;
	return target;
}

struct log_target *log_target_create_stderr(void)
{
/* since C89/C99 says stderr is a macro, we can safely do this! */
#ifdef stderr
	struct log_target *target;

	target = log_target_create();
	if (!target)
		return NULL;

	target->type = LOG_TGT_TYPE_STDERR;
	target->tgt_file.out = stderr;
	target->output = _file_output;
	return target;
#else
	return NULL;
#endif /* stderr */
}

struct log_target *log_target_create_file(const char *fname)
{
	struct log_target *target;

	target = log_target_create();
	if (!target)
		return NULL;

	target->type = LOG_TGT_TYPE_FILE;
	target->tgt_file.out = fopen(fname, "a");
	if (!target->tgt_file.out)
		return NULL;

	target->output = _file_output;

	target->tgt_file.fname = talloc_strdup(target, fname);

	return target;
}

struct log_target *log_target_find(int type, const char *fname)
{
	struct log_target *tgt;

	llist_for_each_entry(tgt, &osmo_log_target_list, entry) {
		if (tgt->type != type)
			continue;
		if (tgt->type == LOG_TGT_TYPE_FILE) {
			if (!strcmp(fname, tgt->tgt_file.fname))
				return tgt;
		} else
			return tgt;
	}
	return NULL;
}

void log_target_destroy(struct log_target *target)
{

	/* just in case, to make sure we don't have any references */
	log_del_target(target);

	if (target->output == &_file_output) {
/* since C89/C99 says stderr is a macro, we can safely do this! */
#ifdef stderr
		/* don't close stderr */
		if (target->tgt_file.out != stderr)
#endif
		{
			fclose(target->tgt_file.out);
			target->tgt_file.out = NULL;
		}
	}

	talloc_free(target);
}

/* close and re-open a log file (for log file rotation) */
int log_target_file_reopen(struct log_target *target)
{
	fclose(target->tgt_file.out);

	target->tgt_file.out = fopen(target->tgt_file.fname, "a");
	if (!target->tgt_file.out)
		return -errno;

	/* we assume target->output already to be set */

	return 0;
}

/* This generates the logging command string for VTY. */
const char *log_vty_command_string(const struct log_info *unused_info)
{
	struct log_info *info = osmo_log_info;
	int len = 0, offset = 0, ret, i, rem;
	int size = strlen("logging level () ()") + 1;
	char *str;

	for (i = 0; i < info->num_cat; i++) {
		if (info->cat[i].name == NULL)
			continue;
		size += strlen(info->cat[i].name) + 1;
	}

	for (i = 0; i < LOGLEVEL_DEFS; i++)
		size += strlen(loglevel_strs[i].str) + 1;

	rem = size;
	str = talloc_zero_size(tall_log_ctx, size);
	if (!str)
		return NULL;

	ret = snprintf(str + offset, rem, "logging level (all|");
	if (ret < 0)
		goto err;
	OSMO_SNPRINTF_RET(ret, rem, offset, len);

	for (i = 0; i < info->num_cat; i++) {
		if (info->cat[i].name) {
			int j, name_len = strlen(info->cat[i].name)+1;
			char name[name_len];

			for (j = 0; j < name_len; j++)
				name[j] = tolower(info->cat[i].name[j]);

			name[name_len-1] = '\0';
			ret = snprintf(str + offset, rem, "%s|", name+1);
			if (ret < 0)
				goto err;
			OSMO_SNPRINTF_RET(ret, rem, offset, len);
		}
	}
	offset--;	/* to remove the trailing | */
	rem++;

	ret = snprintf(str + offset, rem, ") (");
	if (ret < 0)
		goto err;
	OSMO_SNPRINTF_RET(ret, rem, offset, len);

	for (i = 0; i < LOGLEVEL_DEFS; i++) {
		int j, loglevel_str_len = strlen(loglevel_strs[i].str)+1;
		char loglevel_str[loglevel_str_len];

		for (j = 0; j < loglevel_str_len; j++)
			loglevel_str[j] = tolower(loglevel_strs[i].str[j]);

		loglevel_str[loglevel_str_len-1] = '\0';
		ret = snprintf(str + offset, rem, "%s|", loglevel_str);
		if (ret < 0)
			goto err;
		OSMO_SNPRINTF_RET(ret, rem, offset, len);
	}
	offset--;	/* to remove the trailing | */
	rem++;

	ret = snprintf(str + offset, rem, ")");
	if (ret < 0)
		goto err;
	OSMO_SNPRINTF_RET(ret, rem, offset, len);
err:
	str[size-1] = '\0';
	return str;
}

/* This generates the logging command description for VTY. */
const char *log_vty_command_description(const struct log_info *unused_info)
{
	struct log_info *info = osmo_log_info;
	char *str;
	int i, ret, len = 0, offset = 0, rem;
	unsigned int size =
		strlen(LOGGING_STR
		       "Set the log level for a specified category\n") + 1;

	for (i = 0; i < info->num_cat; i++) {
		if (info->cat[i].name == NULL)
			continue;
		size += strlen(info->cat[i].description) + 1;
	}

	for (i = 0; i < LOGLEVEL_DEFS; i++)
		size += strlen(loglevel_descriptions[i]) + 1;

	size += strlen("Global setting for all subsystems") + 1;
	rem = size;
	str = talloc_zero_size(tall_log_ctx, size);
	if (!str)
		return NULL;

	ret = snprintf(str + offset, rem, LOGGING_STR
			"Set the log level for a specified category\n");
	if (ret < 0)
		goto err;
	OSMO_SNPRINTF_RET(ret, rem, offset, len);

	ret = snprintf(str + offset, rem,
			"Global setting for all subsystems\n");
	if (ret < 0)
		goto err;
	OSMO_SNPRINTF_RET(ret, rem, offset, len);

	for (i = 0; i < info->num_cat; i++) {
		if (info->cat[i].name == NULL)
			continue;
		ret = snprintf(str + offset, rem, "%s\n",
				info->cat[i].description);
		if (ret < 0)
			goto err;
		OSMO_SNPRINTF_RET(ret, rem, offset, len);
	}
	for (i = 0; i < LOGLEVEL_DEFS; i++) {
		ret = snprintf(str + offset, rem, "%s\n",
				loglevel_descriptions[i]);
		if (ret < 0)
			goto err;
		OSMO_SNPRINTF_RET(ret, rem, offset, len);
	}
err:
	str[size-1] = '\0';
	return str;
}

int log_init(const struct log_info *inf, void *ctx)
{
	int i;

	tall_log_ctx = talloc_named_const(ctx, 1, "logging");
	if (!tall_log_ctx)
		return -ENOMEM;

	osmo_log_info = talloc_zero(tall_log_ctx, struct log_info);
	if (!osmo_log_info)
		return -ENOMEM;

	osmo_log_info->num_cat_user = inf->num_cat;
	/* total number = number of user cat + library cat */
	osmo_log_info->num_cat = inf->num_cat + ARRAY_SIZE(internal_cat);

	osmo_log_info->cat = talloc_zero_array(osmo_log_info,
					struct log_info_cat,
					osmo_log_info->num_cat);
	if (!osmo_log_info->cat) {
		talloc_free(osmo_log_info);
		osmo_log_info = NULL;
		return -ENOMEM;
	}

	/* copy over the user part */
	for (i = 0; i < inf->num_cat; i++) {
		memcpy(&osmo_log_info->cat[i], &inf->cat[i],
			sizeof(struct log_info_cat));
	}

	/* copy over the library part */
	for (i = 0; i < ARRAY_SIZE(internal_cat); i++) {
		unsigned int cn = osmo_log_info->num_cat_user + i;
		memcpy(&osmo_log_info->cat[cn],
			&internal_cat[i], sizeof(struct log_info_cat));
	}

	return 0;
}
