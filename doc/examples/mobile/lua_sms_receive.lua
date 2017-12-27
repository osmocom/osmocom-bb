-- See https://www.lua.org/manual/5.3/ for Lua
-- See http://ftp.osmocom.org/docs/latest/osmocombb-usermanual.pdf -- Scripting with Lua

function sms_cb(sms, cause, valid)
	print("SMS data cb", sms, cause, valid)
	for i, v in pairs(sms) do
		print(i, v)
	end
end

local cbs = {
	Sms=sms_cb,
}

osmo.ms():register(cbs)
