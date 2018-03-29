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
SController controller[1];  // 1 controller

/* static data definitions */
static void (*l_DebugCallback)(void *, int, const char *) = NULL;
static void *l_DebugCallContext = NULL;
static int l_PluginInit = 0;

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
	if (l_PluginInit)
		return M64ERR_ALREADY_INIT;

	/* first thing is to set the callback function for debug info */
	l_DebugCallback = DebugCallback;
	l_DebugCallContext = Context;

	L = luaL_newstate();

	luaL_openlibs(L);

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
	// reset controllers
	memset(controller, 0, sizeof(SController));

	// set our CONTROL struct pointers to the array that was passed in to this function from the core
	// this small struct tells the core whether each controller is plugged in, and what type of pak is connected
	controller[0].control = ControlInfo.Controls;

	// init controller
	controller[0].control->Present = 1;
	controller[0].control->RawData = 1;
	//controller[0].control->Plugin = PLUGIN_RAW;

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
	if (Command == NULL || Control != 0)
		return;

	unsigned char tx_len = Command[0] & 0x3F;
	unsigned char rx_len = Command[1] & 0x3F;

	unsigned char *tx_data = Command + 2;
	unsigned char *rx_data = Command + 2 + tx_len;

	lua_rawgeti(L, LUA_REGISTRYINDEX, lua_input_ref);
	lua_getfield(L, -1, "ControllerCommand");

	lua_pushinteger(L, tx_len);
	lua_pushinteger(L, rx_len);
	lua_pushlstring(L, (char*) tx_data, tx_len);
	lua_pushlstring(L, (char*) rx_data, rx_len);

	int ret = lua_pcall(L, 4, 0, 0);
	if (ret != 0) {
		fprintf(stderr, "lua runtime error: %s\n", lua_tostring(L, -1));
	}

	lua_pop(L, 1);
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

void print_hex_memory(const char *mem, size_t len) {
  int i;
  unsigned char *p = (unsigned char *)mem;
  for (i=0;i<len;i++) {
    printf("0x%02x ", p[i]);
  }
  printf("\n");
}

EXPORT void CALL ReadController(int Control, unsigned char *Command)
{
	if (Command == NULL || Control != 0)
		return;

	unsigned char tx_len = Command[0] & 0x3F;
	unsigned char rx_len = Command[1] & 0x3F;

	unsigned char *tx_data = Command + 2;
	unsigned char *rx_data = Command + 2 + tx_len;

	lua_rawgeti(L, LUA_REGISTRYINDEX, lua_input_ref);
	lua_getfield(L, -1, "ReadController");

	lua_pushinteger(L, tx_len);
	lua_pushinteger(L, rx_len);
	lua_pushlstring(L, (char*) tx_data, tx_len);
	lua_pushlstring(L, (char*) rx_data, rx_len);

	int ret = lua_pcall(L, 4, 1, 0);
	if (ret != 0) {
		fprintf(stderr, "lua runtime error: %s\n", lua_tostring(L, -1));
	}

	const char* data = lua_tostring(L, -1);
	memcpy(rx_data, data, rx_len);

	lua_pop(L, 2);
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
	int status = luaL_loadfile(L, "/home/jake/Developer/n64lua/mupen.lua");

	if (status != 0) {
		fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
		return 0;
	}

	int ret = lua_pcall(L, 0, 1, 0);
	if (ret != 0) {
		fprintf(stderr, "lua runtime error: %s\n", lua_tostring(L, -1));
		return 0;
	}

	lua_input_ref = luaL_ref(L, LUA_REGISTRYINDEX);

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
	if (Control != 0)
		return;

	lua_rawgeti(L, LUA_REGISTRYINDEX, lua_input_ref);
	lua_getfield(L, -1, "GetKeys");

	int ret = lua_pcall(L, 0, 1, 0);
	if (ret != 0) {
		fprintf(stderr, "lua runtime error: %s\n", lua_tostring(L, -1));
	}

	int keys = lua_tointeger(L, -1);

	lua_pop(L, 2);

	memcpy(&controller[0].buttons, &keys, sizeof keys);
	*Keys = controller[0].buttons;
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
}
