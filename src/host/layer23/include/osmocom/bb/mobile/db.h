#pragma once

#include <sqlite3.h>

int db_open(sqlite3 **db, const char *db_name);
void db_close(sqlite3 *db);
