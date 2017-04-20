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
#include <stdint.h>
#include <sqlite3.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/mobile/db.h>
#include <osmocom/bb/mobile/db_sms.h>

extern void *l23_ctx;

int db_prepare(sqlite3 *db)
{
	char *err = 0;
	int rc = 0;

	const char *inbox_sql = \
		"CREATE TABLE IF NOT EXISTS inbox (" \
			"dst_imsi TEXT NOT NULL, " \
			"dst_tmsi TEXT NOT NULL, " \
			"src_number TEXT NOT NULL, " \
			"date DATETIME DEFAULT CURRENT_TIMESTAMP, " \
			"handled INT DEFAULT 0, " \
			"text TEXT NOT NULL" \
		");";

	const char *outbox_sql = \
		"CREATE TABLE IF NOT EXISTS outbox (" \
			"src_imsi text TEXT NOT NULL, " \
			"dst_number text TEXT NOT NULL, " \
			"date DATETIME DEFAULT CURRENT_TIMESTAMP, " \
			"handled INT DEFAULT 0, " \
			"text TEXT NOT NULL" \
		");";

	rc = sqlite3_exec(db, inbox_sql, NULL, 0, &err);
	if (rc != SQLITE_DONE && rc != SQLITE_OK) {
		fprintf(stderr, "[!] Couldn't init database: %s\n", err);
		return rc;
	}

	rc = sqlite3_exec(db, outbox_sql, NULL, 0, &err) == SQLITE_DONE;
	if (rc != SQLITE_DONE && rc != SQLITE_OK) {
		fprintf(stderr, "[!] Couldn't init database: %s\n", err);
		return rc;
	}

	fprintf(stderr, "[i] Database init complete\n");

	return 0;
}

int db_push_sms(sqlite3 *db, char *dst_imsi, uint32_t dst_tmsi,
	char *src_number, char *text)
{
	sqlite3_stmt *stmt;
	char *tmsi_str;
	int rc;

	const char *push_sql = \
		"INSERT INTO inbox " \
			"(dst_imsi, dst_tmsi, src_number, text) "
			"VALUES (?, ?, ?, ?);";

	rc = sqlite3_prepare(db, push_sql, strlen(push_sql), &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "[!] SQLite error: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	// Convert TMSI to string
	tmsi_str = talloc_asprintf(l23_ctx, "0x%08x", dst_tmsi);

	// Bind query params
	sqlite3_bind_text(stmt, 1, dst_imsi, strlen(dst_imsi), 0);
	sqlite3_bind_text(stmt, 2, tmsi_str, strlen(tmsi_str), 0);
	sqlite3_bind_text(stmt, 3, src_number, strlen(src_number), 0);
	sqlite3_bind_text(stmt, 4, text, strlen(text), 0);

	// Commit
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE && rc != SQLITE_OK) {
		fprintf(stderr, "[!] SQLite error: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	printf("[i] Message saved\n");
	
	sqlite3_finalize(stmt);
	talloc_free(tmsi_str);

	return 0;
}

int db_pop_sms(sqlite3 *db, struct db_sms_record **sms)
{
	struct db_sms_record *sms_new = NULL;
	sqlite3_stmt *stmt;
	int rc;

	const char *pop_select_sql = \
		"SELECT rowid, src_imsi, dst_number, text " \
			"FROM outbox WHERE handled = 0;";

	const char *pop_update_sql = \
		"UPDATE outbox SET handled = 1 " \
			"WHERE rowid = ?;";

	// Prepare query
	rc = sqlite3_prepare(db, pop_select_sql, strlen(pop_select_sql),
		&stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "[!] SQLite error: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	// Attempt to get a record
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		if (rc != SQLITE_DONE && rc != SQLITE_OK)
			fprintf(stderr, "[!] SQLite error: %s\n", sqlite3_errmsg(db));
		
		goto final;
	}

	// Allocate a new structure for SMS
	sms_new = talloc(l23_ctx, struct db_sms_record);
	if (sms_new == NULL) {
		rc = -ENOMEM;
		goto final;
	}

	// Fill structure
	sms_new->row_id = sqlite3_column_int(stmt, 0);
	sms_new->src_imsi = talloc_strdup(sms_new,
		(char *) sqlite3_column_text(stmt, 1));
	sms_new->dst_number = talloc_strdup(sms_new,
		(char *) sqlite3_column_text(stmt, 2));
	sms_new->text = talloc_strdup(sms_new,
		(char *) sqlite3_column_text(stmt, 3));

	// Set external pointer
	*sms = sms_new;

	// Prepare another query
	sqlite3_finalize(stmt);
	rc = sqlite3_prepare(db, pop_update_sql, strlen(pop_update_sql),
		&stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "[!] SQLite error: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	// Mark this record as 'handled'
	sqlite3_bind_int(stmt, 1, sms_new->row_id);
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE && rc != SQLITE_OK) {
		fprintf(stderr, "[!] SQLite error: %s\n", sqlite3_errmsg(db));
		goto final;
	}

	printf("[i] Got a new message from DB\n");

final:
	sqlite3_finalize(stmt);

	return (rc != SQLITE_DONE && rc != SQLITE_OK) ? rc : 0;
}
