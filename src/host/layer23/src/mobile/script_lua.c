/* (C) 2017-2018 by Holger Hans Peter Freyther
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

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/mobile/app_mobile.h>
#include <osmocom/bb/common/logging.h>

#include <osmocom/bb/mobile/primitives.h>

#include <osmocom/core/select.h>
#include <osmocom/vty/misc.h>

#include <sys/types.h>
#include <sys/socket.h>

struct timer_userdata {
	int cb_ref;
};

struct fd_userdata {
	struct lua_State *state;
	struct osmo_fd fd;
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

	LOGPSRC(DLUA, loglevel, ar.source, ar.currentline, "%s", "");
	for (i = 1; i <= argc; ++i) {
		if (!lua_isstring(L, i))
			continue;
		LOGPSRCC(DLUA, loglevel, ar.source, ar.currentline, 1,
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

/* Push table and function.   Stack+=2 */
static bool load_cb(struct osmocom_ms *ms, const char *cb_name)
{
	struct lua_State *L = ms->lua_state;

	if (ms->lua_cb_ref == LUA_REFNIL)
		return false;

	lua_rawgeti(L, LUA_REGISTRYINDEX, ms->lua_cb_ref);
	lua_pushstring(L, cb_name);
	lua_gettable(L, -2);
	if (lua_isnil(L, -1)) {
		LOGP(DLUA, LOGL_DEBUG, "No handler for %s\n", cb_name);
		lua_pop(L, 2);
		return false;
	}
	return true;
}

/* Call callback. Stack-=func + args. func/args popped by lua_pcall */
static void call_cb(lua_State *L, int args)
{
	int err = lua_pcall(L, args, 0, 0);
	if (err) {
		LOGP(DLUA, LOGL_ERROR, "lua error: %s\n", lua_tostring(L, -1));
		lua_pop(L, 2);
	}
}

static void handle_timeout(struct mobile_prim_intf *intf, struct mobile_timer_param *param)
{
	struct timer_userdata *timer = (void *)(intptr_t) param->timer_id;
	lua_State *L = intf->ms->lua_state;
	int err;

	lua_rawgeti(L, LUA_REGISTRYINDEX, timer->cb_ref);
	luaL_unref(L, LUA_REGISTRYINDEX, timer->cb_ref);
	timer->cb_ref = LUA_NOREF;

	err = lua_pcall(L, 0, 0, 0);
	if (err) {
		LOGP(DLUA, LOGL_ERROR, "lua error: %s\n", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

static void handle_started(struct mobile_prim_intf *intf, struct mobile_started_param *param)
{
	struct lua_State *L = intf->ms->lua_state;

	if (!load_cb(intf->ms, "Started"))
		return;

	lua_pushinteger(L, param->started);

	call_cb(L, 1);
	lua_pop(L, 1);
}

static void handle_shutdown(struct mobile_prim_intf *intf, struct mobile_shutdown_param *param)
{
	struct lua_State *L = intf->ms->lua_state;

	if (!load_cb(intf->ms, "Shutdown"))
		return;

	lua_pushinteger(L, param->old_state);
	lua_pushinteger(L, param->new_state);

	call_cb(L, 2);
	lua_pop(L, 1);
}

static void handle_sms(struct mobile_prim_intf *intf, struct mobile_sms_param *param)
{
	struct lua_State *L = intf->ms->lua_state;

	if (!load_cb(intf->ms, "Sms"))
		return;

	lua_createtable(L, 0, 11);

	lua_pushinteger(L, param->sms.validity_minutes);
	lua_setfield(L, -2, "validity_minutes");

	lua_pushinteger(L, param->sms.reply_path_req);
	lua_setfield(L, -2, "reply_path_req");

	lua_pushinteger(L, param->sms.status_rep_req);
	lua_setfield(L, -2, "status_rep_req");

	lua_pushinteger(L, param->sms.ud_hdr_ind);
	lua_setfield(L, -2, "ud_hdr_ind");

	lua_pushinteger(L, param->sms.protocol_id);
	lua_setfield(L, -2, "protocol_id");

	lua_pushinteger(L, param->sms.data_coding_scheme);
	lua_setfield(L, -2, "data_coding_scheme");

	lua_pushinteger(L, param->sms.msg_ref);
	lua_setfield(L, -2, "msg_ref");

	lua_pushstring(L, param->sms.address);
	lua_setfield(L, -2, "address");

	lua_pushinteger(L, param->sms.time);
	lua_setfield(L, -2, "time");

	lua_pushlstring(L, (char *) param->sms.user_data, param->sms.user_data_len);
	lua_setfield(L, -2, "user_data");

	lua_pushstring(L, param->sms.text);
	lua_setfield(L, -2, "text");

	lua_pushinteger(L, param->cause_valid);
	lua_pushinteger(L, param->cause);

	call_cb(L, 3);
	lua_pop(L, 1);
}

static void handle_mm(struct mobile_prim_intf *intf, struct mobile_mm_param *param)
{
	lua_State *L = intf->ms->lua_state;

	if (!load_cb(intf->ms, "Mm"))
		return;

	lua_pushinteger(L, param->state);
	lua_pushinteger(L, param->substate);
	lua_pushinteger(L, param->prev_substate);

	call_cb(L, 3);
	lua_pop(L, 1);
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
	if (timer->cb_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, timer->cb_ref);
		timer->cb_ref = LUA_NOREF;
	}

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

static int lua_osmo_ms(lua_State *L)
{
	lua_pushlightuserdata(L, get_primitive(L)->ms);
	luaL_getmetatable(L, "MS");
	lua_setmetatable(L, -2);

	return 1;
}

static int lua_ms_imei(lua_State *L)
{
	struct osmocom_ms *ms = get_primitive(L)->ms;

	luaL_argcheck(L, lua_isuserdata(L, -1), 1, "No userdata");
	lua_pushstring(L, ms->settings.imei);
	return 1;
}
static int lua_ms_imsi(lua_State *L)
{
	struct osmocom_ms *ms = get_primitive(L)->ms;

	luaL_argcheck(L, lua_isuserdata(L, -1), 1, "No userdata");
	lua_pushstring(L, ms->subscr.imsi);
	return 1;
}

static int lua_ms_shutdown_state(lua_State *L)
{
	struct osmocom_ms *ms = get_primitive(L)->ms;

	lua_pushinteger(L, ms->shutdown);
	return 1;
}

static int lua_ms_started(lua_State *L)
{
	struct osmocom_ms *ms = get_primitive(L)->ms;

	lua_pushinteger(L, ms->started);
	return 1;
}

static int lua_ms_register(lua_State *L)
{
	struct osmocom_ms *ms = get_primitive(L)->ms;

	/* callbacks must be a table */
	luaL_checktype(L, 2, LUA_TTABLE);

	if (ms->lua_cb_ref != LUA_REFNIL)
		luaL_unref(L, LUA_REGISTRYINDEX, ms->lua_cb_ref);
	ms->lua_cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

static int lua_ms_no_shutdown(lua_State *L)
{
	struct osmocom_ms *ms = get_primitive(L)->ms;
	char *name;
	int res;

	res = mobile_start(ms, &name);
	lua_pushinteger(L, res);
	return 1;
}

static int lua_ms_shutdown(lua_State *L)
{
	struct osmocom_ms *ms = get_primitive(L)->ms;
	int argc = lua_gettop(L);
	int force = 0;
	int res;

	if (argc >= 1) {
		luaL_argcheck(L, lua_isboolean(L, -1), 1, "Force");
		force = lua_toboolean(L, -1);
	}

	res = mobile_stop(ms, force);
	lua_pushinteger(L, res);
	return 1;
}

static int lua_ms_sms_send_simple(lua_State *L)
{
	const char *sms_sca, *number, *text;
	struct mobile_prim *prim;
	struct gsm_sms *sms;
	int msg_ref;

	luaL_argcheck(L, lua_isnumber(L, -1), 4, "msg_ref needs to be a number");
	luaL_argcheck(L, lua_isstring(L, -2), 3, "text must be a string");
	luaL_argcheck(L, lua_isstring(L, -3), 2, "number must be a string");
	luaL_argcheck(L, lua_isstring(L, -4), 1, "sms_sca must be a string");

	msg_ref = (int) lua_tonumber(L, -1);
	text = lua_tostring(L, -2);
	number = lua_tostring(L, -3);
	sms_sca = lua_tostring(L, -4);

	prim = mobile_prim_alloc(PRIM_MOB_SMS, PRIM_OP_REQUEST);

	sms = sms_from_text(number, 0, text);
	if (!sms) {
		lua_pushinteger(L, -ENOMEM);
		msgb_free(prim->hdr.msg);
		return 1;
	}

	prim->u.sms.sms = *sms;
	prim->u.sms.sms.msg_ref = msg_ref;
	osmo_strlcpy(prim->u.sms.sca, sms_sca, sizeof(prim->u.sms.sca));
	mobile_prim_intf_req(get_primitive(L), prim);

	lua_pushinteger(L, 0);
	return 1;
}

static int lua_ms_name(lua_State *L)
{
	lua_pushstring(L, get_primitive(L)->ms->name);
	return 1;
}

static int lua_reselect_network(lua_State *L)
{
	struct mobile_prim *prim;

	prim = mobile_prim_alloc(PRIM_MOB_NETWORK_RESELECT, PRIM_OP_REQUEST);
	mobile_prim_intf_req(get_primitive(L), prim);

	return 1;
}

/* Expect a fd on the stack and enable SO_PASSCRED */
static int lua_unix_passcred(lua_State *L)
{
	int one = 1;
	int fd, rc;

	luaL_argcheck(L, lua_isnumber(L, -1), 1, "needs to be a filedescriptor");
	fd = (int) lua_tonumber(L, -1);

	rc = setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &one, sizeof(one));
	if (rc != 0)
		LOGP(DLUA, LOGL_ERROR, "Failed to set SO_PASSCRED: %s\n",
			strerror(errno));
	lua_pushinteger(L, rc);
	return 1;
}

static void lua_fd_do_unregister(lua_State *L, struct fd_userdata *fdu) {
	/* Unregister the fd and forget about the callback */
	osmo_fd_unregister(&fdu->fd);
	if (fdu->cb_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, fdu->cb_ref);
		fdu->cb_ref = LUA_NOREF;
	}
}

static int lua_fd_cb(struct osmo_fd *fd, unsigned int what) {
	struct fd_userdata *fdu;
	lua_State *L;
	int cb_ref;
	int err;

	if (!fd->data) {
		LOGP(DLUA, LOGL_ERROR,
			"fd callback for fd(%d) but no lua callback\n", fd->fd);
		return 0;
	}

	fdu = fd->data;
	L = fdu->state;
	cb_ref = fdu->cb_ref;
	lua_rawgeti(L, LUA_REGISTRYINDEX, cb_ref);

	err = lua_pcall(L, 0, 1, 0);
	if (err) {
		LOGP(DLUA, LOGL_ERROR, "lua error: %s\n", lua_tostring(L, -1));
		lua_pop(L, 1);
	}

	/* Check if we should continue */
	luaL_argcheck(L, lua_isnumber(L, -1), 1, "needs to be a number");
	if (lua_tonumber(L, -1) == 1)
		return 0;

	lua_fd_do_unregister(L, fdu);
	return 0;
}

/* Register the fd */
static int lua_register_fd(lua_State *L)
{
	struct fd_userdata *fdu;
	int fd;

	/* fd, cb */
	luaL_argcheck(L, lua_isnumber(L, -2), 1, "needs to be a filedescriptor");
	luaL_argcheck(L, lua_isfunction(L, -1), 2, "Callback needs to be a function");

	/* Create a table so a user can unregister (and unregister on GC) */
	fdu = lua_newuserdata(L, sizeof(*fdu));
	fdu->state = L;
	fdu->fd.fd = -1;
	luaL_getmetatable(L, "Fd");
	lua_setmetatable(L, -2);

	/* Set the filedescriptor */
	fd = (int) lua_tonumber(L, -3);
	osmo_fd_setup(&fdu->fd, fd, OSMO_FD_READ, lua_fd_cb, fdu, 0);

	/* Assuming that an error here will lead to a GC */
	if (osmo_fd_register(&fdu->fd) != 0) {
		fdu->cb_ref = LUA_NOREF;
		lua_pushliteral(L, "Can't register the fd");
		lua_error(L);
		return 0;
	}

	/* Take the callback and keep a reference to it */
	lua_pushvalue(L, -2);
	fdu->cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	return 1;
}

static int lua_fd_unregister(lua_State *L) {
	struct fd_userdata *fdu;

	luaL_argcheck(L, lua_isuserdata(L, -1), 1, "No userdata");
	fdu = lua_touserdata(L, -1);

	lua_fd_do_unregister(L, fdu);
	return 0;
}


static const struct luaL_Reg fd_funcs[] = {
	{ "unregister", lua_fd_unregister },
	{ "__gc", lua_fd_unregister },
	{ NULL, NULL },
};

static const struct luaL_Reg ms_funcs[] = {
	{ "imsi", lua_ms_imsi },
	{ "imei", lua_ms_imei },
	{ "shutdown_state", lua_ms_shutdown_state },
	{ "started", lua_ms_started },
	{ "register", lua_ms_register },
	{ "start", lua_ms_no_shutdown },
	{ "stop", lua_ms_shutdown },
	{ "sms_send_simple", lua_ms_sms_send_simple },
	{ "number", lua_ms_name },
	{ "reselect_network", lua_reselect_network },
	{ NULL, NULL },
};


static const struct luaL_Reg osmo_funcs[] = {
	{ "timeout",	lua_osmo_timeout },
	{ "unix_passcred", lua_unix_passcred },
	{ "register_fd", lua_register_fd },
	{ "ms",	lua_osmo_ms },
	{ NULL, NULL },
};

static void lua_prim_ind(struct mobile_prim_intf *intf, struct mobile_prim *prim)
{
	switch (OSMO_PRIM_HDR(&prim->hdr)) {
	case OSMO_PRIM(PRIM_MOB_TIMER, PRIM_OP_INDICATION):
		handle_timeout(intf, (struct mobile_timer_param *) &prim->u.timer);
		break;
	case OSMO_PRIM(PRIM_MOB_STARTED, PRIM_OP_INDICATION):
		handle_started(intf, (struct mobile_started_param *) &prim->u.started);
		break;
	case OSMO_PRIM(PRIM_MOB_SHUTDOWN, PRIM_OP_INDICATION):
		handle_shutdown(intf, (struct mobile_shutdown_param *) &prim->u.shutdown);
		break;
	case OSMO_PRIM(PRIM_MOB_SMS, PRIM_OP_INDICATION):
		handle_sms(intf, (struct mobile_sms_param *) &prim->u.sms);
		break;
	case OSMO_PRIM(PRIM_MOB_MM, PRIM_OP_INDICATION):
		handle_mm(intf, (struct mobile_mm_param *) &prim->u.mm);
		break;
	default:
		LOGP(DLUA, LOGL_ERROR, "Unknown primitive: %d\n", OSMO_PRIM_HDR(&prim->hdr));
	};
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

static void create_meta_table(lua_State *L, const char *name, const luaL_Reg *regs)
{
	luaL_newmetatable(L, name);
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -2);
	lua_rawset(L, -3);
	luaL_setfuncs(L, regs, 0);
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
	create_meta_table(L, "Timer", timer_funcs);
	create_meta_table(L, "MS", ms_funcs);
	create_meta_table(L, "Fd", fd_funcs);

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
	struct mobile_prim_intf *intf;

	if (!ms->lua_state)
		return 0;

	intf = get_primitive(ms->lua_state);
	lua_close(ms->lua_state);
	mobile_prim_intf_free(intf);
	ms->lua_state = NULL;
	return 0;
}

int script_lua_load(struct vty *vty, struct osmocom_ms *ms, const char *filename)
{
	struct mobile_prim_intf *intf;
	int err;

	script_lua_close(ms);
	ms->lua_state = lua_newstate(talloc_lua_alloc, ms);
	if (!ms->lua_state)
		return -1;

	ms->lua_cb_ref = LUA_REFNIL;
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
		return -3;
	}

	return 0;
}
