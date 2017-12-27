-- See https://www.lua.org/manual/5.3/ for Lua
-- See http://ftp.osmocom.org/docs/latest/osmocombb-usermanual.pdf -- Scripting with Lua


-- State change
local sent_sms = false
function mm_cb(new_state, new_substate, old_substate)
	-- The system has attached and returned to idle. Send a SMS the first time
	-- it happens.
	if new_state == 19 and new_substate == 1 then
		if not sent_sms then
			sent_sms = true
			osmo.ms():sms_send_simple("1234", "21321324", "fooooooo", 23)
		end
	end
end

-- Called when a new SMS arrives or status for delivery
-- is updated. Check the msg_ref field.
function sms_cb(sms, cause, valid)
	print("SMS data cb", sms, cause, valid)
	for i, v in pairs(sms) do
		print(i, v)
	end
end

-- We need to register a callback 
local cbs = {
	Sms=sms_cb,
	Mm=mm_cb
}
osmo.ms():register(cbs)
