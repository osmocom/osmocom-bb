#pragma once

#include <sqlite3.h>
#include <stdint.h>

struct db_sms_record {
	char *dst_number;
	char *src_imsi;
	char *text;
	int row_id;
};

int db_prepare(sqlite3 *db);
int db_pop_sms(sqlite3 *db, struct db_sms_record **sms);
int db_push_sms(sqlite3 *db, char *dst_imsi, uint32_t dst_tmsi,
	char *src_number, char *text);
