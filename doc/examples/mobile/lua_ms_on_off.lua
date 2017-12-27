-- See https://www.lua.org/manual/5.3/ for Lua
-- See http://ftp.osmocom.org/docs/latest/osmocombb-usermanual.pdf -- Scripting with Lua


-- Switch the MS on/off but this can only be done if the MS
-- is in the right state. This assumes that the MS is fully
-- connected and doesn't stall.

local start = false
osmo.ms().start()
function toggle_ms_state()
	timer = osmo.timeout(20, function()
		if start then
			print("STARTING", osmo.ms().start())
			start = false
		else
			print("STOPPING", osmo.ms().stop(true))
			start = true
		end
		toggle_ms_state()
	end)
end
toggle_ms_state()
