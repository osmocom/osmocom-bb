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

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <errno.h>

#include <osmocore/talloc.h>
#include <osmocore/utils.h>
#include <osmocore/debug.h>

static const struct debug_info *debug_info;

static struct debug_context debug_context;
static void *tall_dbg_ctx = NULL;
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

int debug_parse_level(const char *lvl)
{
	return get_string_value(loglevel_strs, lvl);
}

int debug_parse_category(const char *category)
{
	int i;

	for (i = 0; i < debug_info->num_cat; ++i) {
		if (!strcasecmp(debug_info->cat[i].name+1, category))
			return debug_info->cat[i].number;
	}

	return -EINVAL;
}

/*
 * Parse the category mask.
 * The format can be this: category1:category2:category3
 * or category1,2:category2,3:...
 */
void debug_parse_category_mask(struct debug_target* target, const char *_mask)
{
	int i = 0;
	char *mask = strdup(_mask);
	char *category_token = NULL;

	/* Disable everything to enable it afterwards */
	for (i = 0; i < ARRAY_SIZE(target->categories); ++i)
		target->categories[i].enabled = 0;

	category_token = strtok(mask, ":");
	do {
		for (i = 0; i < debug_info->num_cat; ++i) {
			char* colon = strstr(category_token, ",");
			int length = strlen(category_token);

			if (colon)
			    length = colon - category_token;

			if (strncasecmp(debug_info->cat[i].name, category_token,
					length) == 0) {
				int number = debug_info->cat[i].number;
				int level = 0;

				if (colon)
					level = atoi(colon+1);

				target->categories[number].enabled = 1;
				target->categories[number].loglevel = level;
			}
		}
	} while ((category_token = strtok(NULL, ":")));

	free(mask);
}

static const char* color(int subsys)
{
	int i = 0;

	for (i = 0; i < debug_info->num_cat; ++i) {
		if (debug_info->cat[i].number == subsys)
			return debug_info->cat[i].color;
	}

	return NULL;
}

static void _output(struct debug_target *target, unsigned int subsys,
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

	snprintf(final, sizeof(final), "%s%s%s%s\033[0;m", col, tim, sub, buf);
	final[sizeof(final)-1] = '\0';
	target->output(target, final);
}


static void _debugp(unsigned int subsys, int level, char *file, int line,
		    int cont, const char *format, va_list ap)
{
	struct debug_target *tar;

	llist_for_each_entry(tar, &target_list, entry) {
		struct debug_category *category;
		int output = 0;

		category = &tar->categories[subsys];
		/* subsystem is not supposed to be debugged */
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
		if ((tar->filter_map & DEBUG_FILTER_ALL) != 0)
			output = 1;
		else if (debug_info->filter_fn)
			output = debug_info->filter_fn(&debug_context,
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

void debugp(unsigned int subsys, char *file, int line, int cont,
	    const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	_debugp(subsys, LOGL_DEBUG, file, line, cont, format, ap);
	va_end(ap);
}

void debugp2(unsigned int subsys, unsigned int level, char *file, int line, int cont, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	_debugp(subsys, level, file, line, cont, format, ap);
	va_end(ap);
}

static char hexd_buff[4096];

char *hexdump(const unsigned char *buf, int len)
{
	int i;
	char *cur = hexd_buff;

	hexd_buff[0] = 0;
	for (i = 0; i < len; i++) {
		int len_remain = sizeof(hexd_buff) - (cur - hexd_buff);
		int rc = snprintf(cur, len_remain, "%02x ", buf[i]);
		if (rc <= 0)
			break;
		cur += rc;
	}
	hexd_buff[sizeof(hexd_buff)-1] = 0;
	return hexd_buff;
}

void debug_add_target(struct debug_target *target)
{
	llist_add_tail(&target->entry, &target_list);
}

void debug_del_target(struct debug_target *target)
{
	llist_del(&target->entry);
}

void debug_reset_context(void)
{
	memset(&debug_context, 0, sizeof(debug_context));
}

int debug_set_context(uint8_t ctx_nr, void *value)
{
	if (ctx_nr > DEBUG_MAX_CTX)
		return -EINVAL;

	debug_context.ctx[ctx_nr] = value;

	return 0;
}

void debug_set_all_filter(struct debug_target *target, int all)
{
	if (all)
		target->filter_map |= DEBUG_FILTER_ALL;
	else
		target->filter_map &= ~DEBUG_FILTER_ALL;
}

void debug_set_use_color(struct debug_target *target, int use_color)
{
	target->use_color = use_color;
}

void debug_set_print_timestamp(struct debug_target *target, int print_timestamp)
{
	target->print_timestamp = print_timestamp;
}

void debug_set_log_level(struct debug_target *target, int log_level)
{
	target->loglevel = log_level;
}

void debug_set_category_filter(struct debug_target *target, int category,
			       int enable, int level)
{
	if (category >= debug_info->num_cat)
		return;
	target->categories[category].enabled = !!enable;
	target->categories[category].loglevel = level;
}

static void _stderr_output(struct debug_target *target, const char *log)
{
	fprintf(target->tgt_stdout.out, "%s", log);
	fflush(target->tgt_stdout.out);
}

struct debug_target *debug_target_create(void)
{
	struct debug_target *target;

	target = talloc_zero(tall_dbg_ctx, struct debug_target);
	if (!target)
		return NULL;

	INIT_LLIST_HEAD(&target->entry);
	memcpy(target->categories, debug_info->cat,
		sizeof(struct debug_category)*debug_info->num_cat);
	target->use_color = 1;
	target->print_timestamp = 0;
	target->loglevel = 0;
	return target;
}

struct debug_target *debug_target_create_stderr(void)
{
	struct debug_target *target;

	target = debug_target_create();
	if (!target)
		return NULL;

	target->tgt_stdout.out = stderr;
	target->output = _stderr_output;
	return target;
}

void debug_init(const struct debug_info *cat)
{
	tall_dbg_ctx = talloc_named_const(NULL, 1, "debug");
	debug_info = cat;
}
