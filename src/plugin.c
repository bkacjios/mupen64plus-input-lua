#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/socket.h>

#include "plugin.h"
#include "version.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

lua_State *L;

int lua_input_ref;

/* global data definitions */
SController controller[4];  // 4 controllers

/* static data definitions */
static void (*l_DebugCallback)(void *, int, const char *) = NULL;
static void *l_DebugCallContext = NULL;
static int l_PluginInit = 0;

static m64p_handle l_ConfigInput;

ptr_ConfigOpenSection      ConfigOpenSection = NULL;
ptr_ConfigSaveSection      ConfigSaveSection = NULL;
ptr_ConfigSetDefaultString ConfigSetDefaultString = NULL;
ptr_ConfigGetParamString   ConfigGetParamString = NULL;

static int check_err(int status)
{
	if (status != 0) {
		DebugMessage(M64MSG_ERROR, "lua error: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	return status;
}

static int traceback(lua_State *l)
{
	luaL_traceback(l, l, lua_tostring(l, 1), 1);
	return 1;
}

/* Global functions */
void DebugMessage(int level, const char *message, ...)
{
	char msgbuf[1024];
	va_list args;

	if (l_DebugCallback == NULL)
		return;

	va_start(args, message);
	vsprintf(msgbuf, message, args);

	(*l_DebugCallback)(l_DebugCallContext, level, msgbuf);

	va_end(args);
}

/* Mupen64Plus plugin functions */
EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context, void (*DebugCallback)(void *, int, const char *))
{
	ptr_CoreGetAPIVersions CoreAPIVersionFunc;

	if (l_PluginInit)
		return M64ERR_ALREADY_INIT;

	/* first thing is to set the callback function for debug info */
	l_DebugCallback = DebugCallback;
	l_DebugCallContext = Context;

	/* attach and call the CoreGetAPIVersions function, check Config API version for compatibility */
	CoreAPIVersionFunc = (ptr_CoreGetAPIVersions) DLSYM(CoreLibHandle, "CoreGetAPIVersions");
	if (CoreAPIVersionFunc == NULL)
	{
		DebugMessage(M64MSG_ERROR, "Core emulator broken; no CoreAPIVersionFunc() function found.");
		return M64ERR_INCOMPATIBLE;
	}

	int ConfigAPIVersion, DebugAPIVersion, VidextAPIVersion;
	
	(*CoreAPIVersionFunc)(&ConfigAPIVersion, &DebugAPIVersion, &VidextAPIVersion, NULL);
	if ((ConfigAPIVersion & 0xffff0000) != (CONFIG_API_VERSION & 0xffff0000) || ConfigAPIVersion < CONFIG_API_VERSION)
	{
		DebugMessage(M64MSG_ERROR, "Emulator core Config API (v%i.%i.%i) incompatible with plugin (v%i.%i.%i)",
				VERSION_PRINTF_SPLIT(ConfigAPIVersion), VERSION_PRINTF_SPLIT(CONFIG_API_VERSION));
		return M64ERR_INCOMPATIBLE;
	}

	ConfigOpenSection = (ptr_ConfigOpenSection) DLSYM(CoreLibHandle, "ConfigOpenSection");
	ConfigSaveSection = (ptr_ConfigSaveSection) DLSYM(CoreLibHandle, "ConfigSaveSection");
	ConfigSetDefaultString = (ptr_ConfigSetDefaultString) DLSYM(CoreLibHandle, "ConfigSetDefaultString");
	ConfigGetParamString = (ptr_ConfigGetParamString) DLSYM(CoreLibHandle, "ConfigGetParamString");

	if (!ConfigOpenSection || !ConfigSaveSection || !ConfigSetDefaultString || !ConfigGetParamString)
		return M64ERR_INCOMPATIBLE;

	if (ConfigOpenSection("Input-Lua", &l_ConfigInput) != M64ERR_SUCCESS)
	{
		DebugMessage(M64MSG_ERROR, "Couldn't open config section 'input-lua'");
		return M64ERR_INPUT_NOT_FOUND;
	}

	ConfigSetDefaultString(l_ConfigInput, "LuaScript", "~/mupen.lua", "Path for the Lua script to be ran");
	ConfigSaveSection("Input-Lua");

	L = luaL_newstate();

	luaL_openlibs(L);

	lua_pushinteger(L, PLUGIN_NONE);
	lua_setglobal(L, "PLUGIN_NONE");
	lua_pushinteger(L, PLUGIN_MEMPAK);
	lua_setglobal(L, "PLUGIN_MEMPAK");
	lua_pushinteger(L, PLUGIN_RUMBLE_PAK);
	lua_setglobal(L, "PLUGIN_RUMBLE_PAK");
	lua_pushinteger(L, PLUGIN_TRANSFER_PAK);
	lua_setglobal(L, "PLUGIN_TRANSFER_PAK");
	lua_pushinteger(L, PLUGIN_RAW);
	lua_setglobal(L, "PLUGIN_RAW");

	l_PluginInit = 1;
	return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
	if (!l_PluginInit)
		return M64ERR_NOT_INIT;

	lua_close(L);

	l_PluginInit = 0;
	return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
	/* set version info */
	if (PluginType != NULL)
		*PluginType = M64PLUGIN_INPUT;

	if (PluginVersion != NULL)
		*PluginVersion = PLUGIN_VERSION;

	if (APIVersion != NULL)
		*APIVersion = INPUT_API_VERSION;

	if (PluginNamePtr != NULL)
		*PluginNamePtr = PLUGIN_NAME;

	if (Capabilities != NULL)
		*Capabilities = 0;

	return M64ERR_SUCCESS;
}

/******************************************************************
	Function: InitiateControllers
	Purpose:  This function initialises how each of the controllers
						should be handled.
	input:    - The handle to the main window.
						- A controller structure that needs to be filled for
							the emulator to know how to handle each controller.
	output:   none
*******************************************************************/
EXPORT void CALL InitiateControllers(CONTROL_INFO ControlInfo)
{
	const char* lua_file = ConfigGetParamString(l_ConfigInput, "LuaScript");

	if (check_err(luaL_loadfile(L, lua_file)) != 0)
		return;

	int base = lua_gettop(L);
	lua_pushcfunction(L, traceback);
	lua_insert(L, base);

	if (check_err(lua_pcall(L, 0, 1, base)) == 0) {
		lua_input_ref = luaL_ref(L, LUA_REGISTRYINDEX);

		// reset controllers
		memset(controller, 0, sizeof(SController));

		for (int i=0; i<4; i++) {
			// set our CONTROL struct pointers to the array that was passed in to this function from the core
			// this small struct tells the core whether each controller is plugged in, and what type of pak is connected
			controller[i].control = ControlInfo.Controls;

			lua_rawgeti(L, LUA_REGISTRYINDEX, lua_input_ref);
				lua_getfield(L, -1, "InitiateController");
			lua_remove(L, -2); // Pop input table

			lua_pushinteger(L, i+1);

			if (check_err(lua_pcall(L, 1, 1, base)) == 0) {
				if (lua_istable(L, -1)) {
					// init controller
					lua_getfield(L, -1, "Present");
					controller[i].control->Present = lua_toboolean(L, -1);
					lua_pop(L, 1);
					lua_getfield(L, -1, "RawData");
					controller[i].control->RawData = lua_toboolean(L, -1);
					lua_pop(L, 1);
					lua_getfield(L, -1, "Plugin");
					controller[i].control->Plugin = lua_tointeger(L, -1);
					lua_pop(L, 1);
				}

				lua_pop(L, 1); // Pop return value
			}
		}		
	}

	lua_remove(L, base);

	DebugMessage(M64MSG_INFO, "%s version %i.%i.%i initialized.", PLUGIN_NAME, VERSION_PRINTF_SPLIT(PLUGIN_VERSION));
}

/******************************************************************
	Function: ControllerCommand
	Purpose:  To process the raw data that has just been sent to a
						specific controller.
	input:    - Controller Number (0 to 3) and -1 signalling end of
							processing the pif ram.
						- Pointer of data to be processed.
	output:   none

	note:     This function is only needed if the DLL is allowing raw
						data, or the plugin is set to raw

						the data that is being processed looks like this:
						initilize controller: 01 03 00 FF FF FF
						read controller:      01 04 01 FF FF FF FF
*******************************************************************/
EXPORT void CALL ControllerCommand(int Control, unsigned char *Command)
{
	if (Command == NULL)
		return;

	unsigned char tx_len = Command[0] & 0x3F;
	unsigned char rx_len = Command[1] & 0x3F;

	unsigned char *tx_data = Command + 2;
	unsigned char *rx_data = Command + 2 + tx_len;

	lua_pushcfunction(L, traceback);
	int base = lua_gettop(L);

	lua_rawgeti(L, LUA_REGISTRYINDEX, lua_input_ref);
		lua_getfield(L, -1, "ControllerCommand");
	lua_remove(L, -2); // Pop input table

	lua_pushinteger(L, Control + 1);
	lua_pushinteger(L, tx_len);
	lua_pushinteger(L, rx_len);
	lua_pushlstring(L, (char*) tx_data, tx_len);
	lua_pushlstring(L, (char*) rx_data, rx_len);

	check_err(lua_pcall(L, 5, 0, base));

	lua_remove(L, base);
}

/******************************************************************
	Function: ReadController
	Purpose:  To process the raw data in the pif ram that is about to
						be read.
	input:    - Controller Number (0 to 3) and -1 signalling end of
							processing the pif ram.
						- Pointer of data to be processed.
	output:   none
	note:     This function is only needed if the DLL is allowing raw
						data.
*******************************************************************/
EXPORT void CALL ReadController(int Control, unsigned char *Command)
{
	if (Command == NULL)
		return;

	unsigned char tx_len = Command[0] & 0x3F;
	unsigned char rx_len = Command[1] & 0x3F;

	unsigned char *tx_data = Command + 2;
	unsigned char *rx_data = Command + 2 + tx_len;

	lua_pushcfunction(L, traceback);
	int base = lua_gettop(L);

	lua_rawgeti(L, LUA_REGISTRYINDEX, lua_input_ref);
		lua_getfield(L, -1, "ReadController");
	lua_remove(L, -2); // Pop input table

	lua_pushinteger(L, Control + 1);
	lua_pushinteger(L, tx_len);
	lua_pushinteger(L, rx_len);
	lua_pushlstring(L, (char*) tx_data, tx_len);
	lua_pushlstring(L, (char*) rx_data, rx_len);

	if (check_err(lua_pcall(L, 5, 1, base)) == 0) {
		if (lua_isstring(L, -1)) {
			const char* data = lua_tostring(L, -1);
			memcpy(rx_data, data, rx_len);
		}
		lua_pop(L, 1); // Pop return value
	}

	lua_remove(L, base);
}

/******************************************************************
	Function: RomOpen
	Purpose:  This function is called when a rom is open. (from the
						emulation thread)
	input:    none
	output:   none
*******************************************************************/
EXPORT int CALL RomOpen(void)
{
	lua_pushcfunction(L, traceback);
	int base = lua_gettop(L);

	lua_rawgeti(L, LUA_REGISTRYINDEX, lua_input_ref);
		lua_getfield(L, -1, "RomOpen");
	lua_remove(L, -2); // Pop input table

	lua_pcall(L, 0, 0, base);
	lua_remove(L, base);
	return 1;
}

/******************************************************************
	Function: RomClosed
	Purpose:  This function is called when a rom is closed.
	input:    none
	output:   none
*******************************************************************/
EXPORT void CALL RomClosed(void)
{
	lua_pushcfunction(L, traceback);
	int base = lua_gettop(L);

	lua_rawgeti(L, LUA_REGISTRYINDEX, lua_input_ref);
		lua_getfield(L, -1, "RomClosed");
	lua_remove(L, -2); // Pop input table

	lua_pcall(L, 0, 0, base);
	lua_remove(L, base);

	luaL_unref(L, LUA_REGISTRYINDEX, lua_input_ref);
}

/******************************************************************
	Function: GetKeys
	Purpose:  To get the current state of the controllers buttons.
	input:    - Controller Number (0 to 3)
						- A pointer to a BUTTONS structure to be filled with
						the controller state.
	output:   none
*******************************************************************/
EXPORT void CALL GetKeys(int Control, BUTTONS *Keys )
{
	lua_pushcfunction(L, traceback);
	int base = lua_gettop(L);

	lua_rawgeti(L, LUA_REGISTRYINDEX, lua_input_ref);
		lua_getfield(L, -1, "GetKeys");
	lua_remove(L, -2); // Pop input table

	lua_pushinteger(L, Control + 1);

	if (check_err(lua_pcall(L, 1, 1, base)) == 0) {
		int keys = lua_tointeger(L, -1);
		lua_pop(L, 1); // Pop return value

		// Set pressed keys within emulator
		memcpy(&controller[Control].buttons, &keys, sizeof keys);
		*Keys = controller[Control].buttons;
	}

	lua_remove(L, base);
}

/******************************************************************
	Function: SDL_KeyDown
	Purpose:  To pass the SDL_KeyDown message from the emulator to the
						plugin.
	input:    keymod and keysym of the SDL_KEYDOWN message.
	output:   none
*******************************************************************/
EXPORT void CALL SDL_KeyDown(int keymod, int keysym)
{
	lua_pushcfunction(L, traceback);
	int base = lua_gettop(L);

	lua_rawgeti(L, LUA_REGISTRYINDEX, lua_input_ref);
		lua_getfield(L, -1, "SDLKeyDown");
	lua_remove(L, -2); // Pop input table

	lua_pushinteger(L, keymod);
	lua_pushinteger(L, keysym);

	check_err(lua_pcall(L, 2, 0, base));

	lua_remove(L, base);
}

/******************************************************************
	Function: SDL_KeyUp
	Purpose:  To pass the SDL_KeyUp message from the emulator to the
						plugin.
	input:    keymod and keysym of the SDL_KEYUP message.
	output:   none
*******************************************************************/
EXPORT void CALL SDL_KeyUp(int keymod, int keysym)
{
	lua_pushcfunction(L, traceback);
	int base = lua_gettop(L);

	lua_rawgeti(L, LUA_REGISTRYINDEX, lua_input_ref);
		lua_getfield(L, -1, "SDLKeyUp");
	lua_remove(L, -2); // Pop input table

	lua_pushinteger(L, keymod);
	lua_pushinteger(L, keysym);

	check_err(lua_pcall(L, 2, 0, base));

	lua_remove(L, base);
}
