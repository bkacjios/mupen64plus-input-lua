local Serial = require('periphery').Serial
local serial = Serial("/dev/ttyACM0", 115200)

local input = {}

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
		serial:write(string.char(tx_len, rx_len) .. tx_data)
		-- Read controller responce and pass to emulator
		return serial:read(rx_len)
	end
end

function input.GetKeys(controller)
end

return input