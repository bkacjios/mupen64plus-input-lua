local Serial = require('periphery').Serial
local serial = Serial("/dev/ttyACM0", 2000000)

local input = {}

function string.tohex(str)
	return (str:gsub('.', function (c)
		return string.format('%02X ', string.byte(c))
	end))
end

function input.RomOpen()
end

function input.RomClosed()
end

function input.InitiateController(controller)
	-- Initiate controller 1 with raw data
	if controller == 1 then
		return {
			Present = true,			-- Sets the controller as "Present" AKA plugged in
			RawData = true,			-- Passes all raw data to input.ReadController
			Plugin = PLUGIN_NONE,	-- This isn't necessary if RawData is true
		}
	end
end

function input.ControllerCommand(controller, tx_len, rx_len, tx_data, rx_data)
end

function input.ReadController(controller, tx_len, rx_len, tx_data, rx_data)
	if controller == 1 then
		-- Create the header, and append tx_data
		local write = string.char(tx_len, rx_len) .. tx_data

		if write ~= '\x01\x04\x01' then
			print("write", string.tohex(tx_data))
		end
		serial:write(write)
		-- Read controller responce and pass to emulator
		local read = serial:read(rx_len)
		if write ~= '\x01\x04\x01' then
			print("read", string.tohex(read))
		end
		return read
	end
end

function input.GetKeys(controller)
end

function input.SDLKeyDown(keymod, keysym)
end

function input.SDLKeyUp(keymod, keysym)
end

return input