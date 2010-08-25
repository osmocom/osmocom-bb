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

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#include <time.h>
#include <errno.h>

#include <osmocore/talloc.h>
#include <osmocore/utils.h>
#include <osmocore/logging.h>

const struct log_info *osmo_log_info;

static struct log_context log_context;
static void *tall_log_ctx = NULL;
static LLIST_HEAD(target_list);

static const struct value_string loglevel_strs[] = {
	{ 0,		"EVERYTHING" },
	{ LOGL_DEBUG,	"DEBUG" },
	{ LOGL_INFO,	"INFO" },
	{ LOGL_NOTICE,	"NOTICE" },
	{ LOGL_ERROR,	"ERROR" },
	{ LOGL_FATAL,	"FATAL" },
	{ 0, NULL },
};

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
	for (i = 0; i < ARRAY_SIZE(target->categories); ++i)
		target->categories[i].enabled = 0;

	category_token = strtok(mask, ":");
	do {
		for (i = 0; i < osmo_log_info->num_cat; ++i) {
			char* colon = strstr(category_token, ",");
			int length = strlen(category_token);

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
		    char *file, int line, int cont, const char *format,
		    va_list ap)
{
	char col[30];
	char sub[30];
	char tim[30];
	char buf[4096];
	char final[4096];

	/* prepare the data */
	col[0] = '\0';
	sub[0] = '\0';
	tim[0] = '\0';
	buf[0] = '\0';

	/* are we using color */
	if (target->use_color) {
		const char *c = color(subsys);
		if (c) {
			snprintf(col, sizeof(col), "%s", color(subsys));
			col[sizeof(col)-1] = '\0';
		}
	}
	vsnprintf(buf, sizeof(buf), format, ap);
	buf[sizeof(buf)-1] = '\0';

	if (!cont) {
		if (target->print_timestamp) {
			char *timestr;
			time_t tm;
			tm = time(NULL);
			timestr = ctime(&tm);
			timestr[strlen(timestr)-1] = '\0';
			snprintf(tim, sizeof(tim), "%s ", timestr);
			tim[sizeof(tim)-1] = '\0';
		}
		snprintf(sub, sizeof(sub), "<%4.4x> %s:%d ", subsys, file, line);
		sub[sizeof(sub)-1] = '\0';
	}

	snprintf(final, sizeof(final), "%s%s%s%s%s", col, tim, sub, buf,
		 target->use_color ? "\033[0;m" : "");
	final[sizeof(final)-1] = '\0';
	target->output(target, final);
}


static void _logp(unsigned int subsys, int level, char *file, int line,
		  int cont, const char *format, va_list ap)
{
	struct log_target *tar;

	llist_for_each_entry(tar, &target_list, entry) {
		struct log_category *category;
		int output = 0;

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

		if (output) {
			/* FIXME: copying the va_list is an ugly
			 * workaround against a bug hidden somewhere in
			 * _output.  If we do not copy here, the first
			 * call to _output() will corrupt the va_list
			 * contents, and any further _output() calls
			 * with the same va_list will segfault */
			va_list bp;
			va_copy(bp, ap);
			_output(tar, subsys, file, line, cont, format, bp);
			va_end(bp);
		}
	}
}

void logp(unsigned int subsys, char *file, int line, int cont,
	  const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	_logp(subsys, LOGL_DEBUG, file, line, cont, format, ap);
	va_end(ap);
}

void logp2(unsigned int subsys, unsigned int level, char *file, int line, int cont, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	_logp(subsys, level, file, line, cont, format, ap);
	va_end(ap);
}

void log_add_target(struct log_target *target)
{
	llist_add_tail(&target->entry, &target_list);
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

/* since C89/C99 says stderr is a macro, we can safely do this! */
#ifdef stderr
static void _file_output(struct log_target *target, const char *log)
{
	fprintf(target->tgt_file.out, "%s", log);
	fflush(target->tgt_file.out);
}
#endif

struct log_target *log_target_create(void)
{
	struct log_target *target;
	unsigned int i;

	target = talloc_zero(tall_log_ctx, struct log_target);
	if (!target)
		return NULL;

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

	target->tgt_file.out = stderr;
	target->output = _file_output;
	return target;
#else
	return NULL;
#endif /* stderr */
}

const char *log_vty_level_string(struct log_info *info)
{
	const struct value_string *vs;
	unsigned int len = 3; /* ()\0 */
	char *str;

	for (vs = loglevel_strs; vs->value || vs->str; vs++)
		len += strlen(vs->str) + 1;

	str = talloc_zero_size(NULL, len);
	if (!str)
		return NULL;

	str[0] = '(';
	for (vs = loglevel_strs; vs->value || vs->str; vs++) {
		strcat(str, vs->str);
		strcat(str, "|");
	}
	str[strlen(str)-1] = ')';

	return str;
}

const char *log_vty_category_string(struct log_info *info)
{
	unsigned int len = 3;	/* "()\0" */
	unsigned int i;
	char *str;

	for (i = 0; i < info->num_cat; i++)
		len += strlen(info->cat[i].name) + 1;

	str = talloc_zero_size(NULL, len);
	if (!str)
		return NULL;

	str[0] = '(';
	for (i = 0; i < info->num_cat; i++) {
		strcat(str, info->cat[i].name+1);
		strcat(str, "|");
	}
	str[strlen(str)-1] = ')';

	return str;
}

void log_init(const struct log_info *cat)
{
	tall_log_ctx = talloc_named_const(NULL, 1, "logging");
	osmo_log_info = cat;
}
