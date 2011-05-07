/*
 * Parse a simple file with messages, e.g used for USSD messages
 *
 * (C) 2010 by Holger Hans Peter Freyther
 * (C) 2010 by On-Waves
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

#include <osmocom/core/msgfile.h>
#include <osmocom/core/talloc.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

static struct osmo_config_entry *
alloc_entry(struct osmo_config_list *entries,
	    const char *mcc, const char *mnc,
	    const char *option, const char *text)
{
	struct osmo_config_entry *entry =
		talloc_zero(entries, struct osmo_config_entry);
	if (!entry)
		return NULL;

	entry->mcc = talloc_strdup(entry, mcc);
	entry->mnc = talloc_strdup(entry, mnc);
	entry->option = talloc_strdup(entry, option);
	entry->text = talloc_strdup(entry, text);

	llist_add_tail(&entry->list, &entries->entry);
	return entry;
}

static struct osmo_config_list *alloc_entries(void *ctx)
{
	struct osmo_config_list *entries;

	entries = talloc_zero(ctx, struct osmo_config_list);
	if (!entries)
		return NULL;

	INIT_LLIST_HEAD(&entries->entry);
	return entries;
}

/*
 * split a line like 'foo:Text'.
 */
static void handle_line(struct osmo_config_list *entries, char *line)
{
	int i;
	const int len = strlen(line);

	char *items[3];
	int last_item = 0;

	/* Skip comments from the file */
	if (line[0] == '#')
		return;

	for (i = 0; i < len; ++i) {
		if (line[i] == '\n' || line[i] == '\r')
			line[i] = '\0';
		else if (line[i] == ':' && last_item < 3) {
			line[i] = '\0';

			items[last_item++] = &line[i + 1];
		}
	}

	if (last_item == 3) {
		alloc_entry(entries, &line[0] , items[0], items[1], items[2]);
		return;
	}

	/* nothing found */
}

struct osmo_config_list *osmo_config_list_parse(void *ctx, const char *filename)
{
	struct osmo_config_list *entries;
	size_t n;
	char *line;
	FILE *file;

	file = fopen(filename, "r");
	if (!file)
		return NULL;

	entries = alloc_entries(ctx);
	if (!entries) {
		fclose(file);
		return NULL;
	}

	n = 2342;
	line = NULL;
        while (getline(&line, &n, file) != -1) {
		handle_line(entries, line);
		free(line);
		line = NULL;
	}

	fclose(file);
	return entries;
}
