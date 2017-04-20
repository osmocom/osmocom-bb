/*
 * (C) 2017 by Vadim Yanitskiy <axilirator@gmail.com>
 *
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

#include <stdio.h>
#include <errno.h>
#include <talloc.h>
#include <string.h>
#include <sqlite3.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/mobile/db.h>

extern void *l23_ctx;

static char *db_compose_path(const char *db_name)
{
	const char *db_home_path = ".osmocom/bb";
	const char *db_tmp_path = "/tmp";
	char const *home = getenv("HOME");
	char *db_full_path;
	size_t len;

	if (home != NULL) {
		len = strlen(home) + strlen(db_home_path) + strlen(db_name) + 3;
		db_full_path = talloc_size(l23_ctx, len);
		if (db_full_path != NULL)
			snprintf(db_full_path, len, "%s/%s/%s",
				home, db_home_path, db_name);
	} else {
		len = strlen(db_tmp_path) + strlen(db_name) + 2;
		db_full_path = talloc_size(l23_ctx, len);
		if (db_full_path != NULL)
			snprintf(db_full_path, len, "%s/%s",
				db_tmp_path, db_name);
	}

	return db_full_path;
}

int db_open(sqlite3 **db, const char *db_name)
{
	char *db_full_path;
	int rc;

	// Compose full database path
	db_full_path = db_compose_path(db_name);
	if (db_full_path == NULL)
		return -ENOMEM;

	// Connect to database
	rc = sqlite3_open(db_full_path, db);
	if (rc) {
		fprintf(stderr, "[!] Couldn't open database: %s\n",
			sqlite3_errmsg(*db));
		goto final;
	}

	fprintf(stderr, "[i] Successfully connected to database\n");

final:
	talloc_free(db_full_path);

	return rc;
}

void db_close(sqlite3 *db)
{
	if (!db)
		sqlite3_close(db);
}
