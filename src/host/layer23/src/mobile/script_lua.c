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

#include <osmocom/bb/mobile/primitives.h>

#include <osmocom/vty/misc.h>

struct timer_userdata {
	int cb_ref;
};

static char lua_prim_key[] = "osmocom.org-mobile-prim";

static struct mobile_prim_intf *get_primitive(lua_State *L)
{
	struct mobile_prim_intf *intf;

	lua_pushlightuserdata(L, lua_prim_key);
	lua_gettable(L, LUA_REGISTRYINDEX);
	intf = (void *) lua_topointer(L, -1);
	lua_pop(L, 1);
	return intf;
}

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

static void handle_timeout(struct mobile_prim_intf *intf, struct mobile_timer_param *param)
{
	struct timer_userdata *timer = (void *)(intptr_t) param->timer_id;
	lua_State *L = intf->ms->lua_state;
	int err;

	lua_rawgeti(L, LUA_REGISTRYINDEX, timer->cb_ref);
	luaL_unref(L, LUA_REGISTRYINDEX, timer->cb_ref);

	err = lua_pcall(L, 0, 0, 0);
	if (err) {
		LOGP(DLUA, LOGL_ERROR, "lua error: %s\n", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

static int lua_osmo_timeout(lua_State *L)
{
	struct mobile_prim *prim;
	struct timer_userdata *timer;

	if(lua_gettop(L) != 2) {
		lua_pushliteral(L, "Need two arguments");
		lua_error(L);
		return 0;
	}

	luaL_argcheck(L, lua_isnumber(L, -2), 1, "Timeout needs to be a number");
	luaL_argcheck(L, lua_isfunction(L, -1), 2, "Callback needs to be a function");

	/*
	 * Create a handle to prevent the function to be GCed while we run the
	 * timer. Add a metatable to the object so itself will be GCed properly.
	 */
	timer = lua_newuserdata(L, sizeof(*timer));
	luaL_getmetatable(L, "Timer");
	lua_setmetatable(L, -2);

	lua_pushvalue(L, -2);
	timer->cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	/* Take the address of the user data... */
	prim = mobile_prim_alloc(PRIM_MOB_TIMER, PRIM_OP_REQUEST);
	prim->u.timer.timer_id = (intptr_t) timer;
	prim->u.timer.seconds = lua_tonumber(L, -3);
	mobile_prim_intf_req(get_primitive(L), prim);

	return 1;
}

static int lua_timer_cancel(lua_State *L)
{
	struct mobile_prim *prim;
	struct timer_userdata *timer;

	luaL_argcheck(L, lua_isuserdata(L, -1), 1, "No userdata");
	timer = lua_touserdata(L, -1);

	prim = mobile_prim_alloc(PRIM_MOB_TIMER_CANCEL, PRIM_OP_REQUEST);
	prim->u.timer.timer_id = (intptr_t) timer;
	mobile_prim_intf_req(get_primitive(L), prim);
	return 0;
}

static const struct luaL_Reg timer_funcs[] = {
	{ "cancel", lua_timer_cancel },
	{ "__gc", lua_timer_cancel },
	{ NULL, NULL },
};

static const struct luaL_Reg osmo_funcs[] = {
	{ "timeout",	lua_osmo_timeout },
	{ NULL, NULL },
};

static void lua_prim_ind(struct mobile_prim_intf *intf, struct mobile_prim *prim)
{
	switch (OSMO_PRIM_HDR(&prim->hdr)) {
	case OSMO_PRIM(PRIM_MOB_TIMER, PRIM_OP_INDICATION):
		handle_timeout(intf, (struct mobile_timer_param *) &prim->u.timer);
		break;
	default:
		LOGP(DLUA, LOGL_ERROR, "Unknown primitive: %d\n", OSMO_PRIM_HDR(&prim->hdr));
	};

	msgb_free(prim->hdr.msg);
}

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

static void add_runtime(lua_State *L, struct mobile_prim_intf *intf)
{
	add_globals(L);

	/* Add a osmo module/table. Seems an oldschool module? */
	lua_newtable(L);
	luaL_setfuncs(L, osmo_funcs, 0);
	lua_setglobal(L, "osmo");

	/* Create metatables so we can GC objects... */
	luaL_newmetatable(L, "Timer");
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -2);
	lua_rawset(L, -3);
	luaL_setfuncs(L, timer_funcs, 0);
	lua_pop(L, 1);


	/* Remember the primitive pointer... store it in the registry */
	lua_pushlightuserdata(L, lua_prim_key);
	lua_pushlightuserdata(L, intf);
	lua_settable(L, LUA_REGISTRYINDEX);

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
	struct mobile_prim_intf *intf;
	int err;

	if (ms->lua_state)
		lua_close(ms->lua_state);
	ms->lua_state = lua_newstate(talloc_lua_alloc, ms);
	if (!ms->lua_state)
		return -1;

	luaL_openlibs(ms->lua_state);

	intf = mobile_prim_intf_alloc(ms);
	intf->indication = lua_prim_ind;
	add_runtime(ms->lua_state, intf);

	err = luaL_loadfilex(ms->lua_state, filename, NULL);
	if (err) {
		vty_out(vty, "%% LUA load error: %s%s",
				lua_tostring(ms->lua_state, -1), VTY_NEWLINE);
		lua_pop(ms->lua_state, 1);
		return -2;
	}


	err = lua_pcall(ms->lua_state, 0, 0, 0);
	if (err) {
		vty_out(vty, "%% LUA execute error: %s%s",
				lua_tostring(ms->lua_state, -1), VTY_NEWLINE);
		lua_pop(ms->lua_state, 1);
		return 3;
	}

	return 0;
}
