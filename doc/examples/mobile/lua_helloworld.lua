-- See https://www.lua.org/manual/5.3/ for Lua
-- See http://ftp.osmocom.org/docs/latest/osmocombb-usermanual.pdf -- Scripting with Lua

-- Standard print and log_* are forwarded to the Osmocom logging framework
print("Hellp from Lua");
log_notice("Notice from lua");
log_debug("Debug from Lua");
log_error("Error from Lua");
log_fatal("Fatal from Lua");
