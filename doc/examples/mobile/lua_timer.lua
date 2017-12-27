-- See https://www.lua.org/manual/5.3/ for Lua
-- See http://ftp.osmocom.org/docs/latest/osmocombb-usermanual.pdf -- Scripting with Lua

-- Start and stop timer with callback. Schedule a timeout and
-- resume execution then.

-- Timeout in 10 seconds
local timer = osmo.timeout(10, function()
	print("Timeout occurred");
end)
-- We can cancel it. The callback will not be called
timer:cancel()
