/* (C) 2017 by Holger Hans Peter Freyther
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
 */

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/logging.h>

#include <osmocom/vty/misc.h>


int script_lua_load(struct vty *vty, struct osmocom_ms *ms, const char *filename)
{
	vty_out(vty, "%% No LUA support compiled into mobile!%s", VTY_NEWLINE);
	return -1;
}

int script_lua_close(struct osmocom_ms *ms)
{
	return 0;
}
