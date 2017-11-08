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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/logging.h>

#include <osmocom/vty/misc.h>

static int lua_osmo_do_log(lua_State *L, int loglevel)
{
	int argc = lua_gettop(L);
	lua_Debug ar = { 0, };
	int i;

	lua_getstack(L, 1, &ar);
	lua_getinfo(L, "nSl", &ar);

	for (i = 1; i <= argc; ++i) {
		if (!lua_isstring(L, i))
			continue;
		LOGPSRC(DLUA, loglevel, ar.source, ar.currentline,
				"%s%s", i > 1 ? "\t" : "", lua_tostring(L, i));
	}
	LOGPC(DLUA, loglevel, "\n");
	return 0;
}

static int lua_osmo_print(lua_State *L)
{
	return lua_osmo_do_log(L, LOGL_NOTICE);
}

static int lua_osmo_debug(lua_State *L)
{
	return lua_osmo_do_log(L, LOGL_DEBUG);
}

static int lua_osmo_error(lua_State *L)
{
	return lua_osmo_do_log(L, LOGL_ERROR);
}

static int lua_osmo_fatal(lua_State *L)
{
	return lua_osmo_do_log(L, LOGL_FATAL);
}

static const struct luaL_Reg global_runtime[] = {
	{ "print", 	lua_osmo_print  },
	{ "log_notice",	lua_osmo_print },
	{ "log_debug",	lua_osmo_debug  },
	{ "log_error",	lua_osmo_error  },
	{ "log_fatal",	lua_osmo_fatal  },
	{ NULL, NULL },
};

/*
 *  Add functions to the global lua scope. Technically these are
 *  included in the _G table. The following lua code can be used
 *  to inspect it.
 *
 *  > for n in pairs(_G) do print(n) end
 */
static void add_globals(lua_State *L)
{
	lua_getglobal(L, "_G");
	luaL_setfuncs(L, global_runtime, 0);
	lua_pop(L, 1);
}

static void add_runtime(lua_State *L, struct osmocom_ms *ms)
{
	add_globals(L);
}

static void *talloc_lua_alloc(void *ctx, void *ptr, size_t osize, size_t nsize)
{
	if (nsize == 0) {
		talloc_free(ptr);
		return NULL;
	}
	return talloc_realloc_size(ctx, ptr, nsize);
}

int script_lua_close(struct osmocom_ms *ms)
{
	if (!ms->lua_state)
		return 0;

	lua_close(ms->lua_state);
	ms->lua_state = NULL;
	return 0;
}

int script_lua_load(struct vty *vty, struct osmocom_ms *ms, const char *filename)
{
	int err;

	if (ms->lua_state)
		lua_close(ms->lua_state);
	ms->lua_state = lua_newstate(talloc_lua_alloc, ms);
	if (!ms->lua_state)
		return -1;

	luaL_openlibs(ms->lua_state);
	err = luaL_loadfilex(ms->lua_state, filename, NULL);
	if (err) {
		vty_out(vty, "%% LUA load error: %s%s",
				lua_tostring(ms->lua_state, -1), VTY_NEWLINE);
		lua_pop(ms->lua_state, 1);
		return -2;
	}

	add_runtime(ms->lua_state, ms);

	err = lua_pcall(ms->lua_state, 0, 0, 0);
	if (err) {
		vty_out(vty, "%% LUA execute error: %s%s",
				lua_tostring(ms->lua_state, -1), VTY_NEWLINE);
		lua_pop(ms->lua_state, 1);
		return 3;
	}

	return 0;
}
