/* LBUS USB bus master host library: Lua wrapper
 *
 * Copyright (c) 2016 Hans-Werner Hilse <hwhilse@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdlib.h>

#include <lua.h>
#include <lauxlib.h>

#include "lbuscomm.h"

static int test_error(lua_State* L, const int r, const char* func) {
	const char* error = lbus_strerror(r);
	if(error != NULL)
		luaL_error(L, "lbuscomm error in %s: %s", func, error);
	return r;
}

static lbus_ctx* checklbusctx(lua_State* L, int narg) {
	if(lua_type(L, narg) != LUA_TUSERDATA)
		luaL_error(L, "argument %d is not a lbuscomm context", narg);
	return (lbus_ctx*) lua_touserdata(L, narg);
}

static int checkdst(lua_State* L, int narg) {
	if(!lua_isnumber(L, narg))
		return luaL_error(L, "argument %d is not a LBUS address", narg);
	int dst = lua_tointeger(L, narg);
	if(dst < 0 || dst > 0xFF)
		return luaL_error(L, "argument %d is not a valid LBUS address", narg);
	return dst;
}

static int llbus_free(lua_State* L) {
	lbus_ctx *C = checklbusctx(L, 1);
	lbus_free(C);
	return 0;
}

static int llbus_busmaster_echo(lua_State* L) {
	lbus_ctx *C = checklbusctx(L, 1);
	test_error(L, lbus_busmaster_echo(C), "lbus_busmaster_echo()");
	lua_pushboolean(L, 1);
	return 1;
}

static int llbus_led_set_16bit(lua_State* L) {
	lbus_ctx *C = checklbusctx(L, 1);
	int dst = checkdst(L, 2);
	int offset = luaL_checkinteger(L, 3);
	if(offset < 0 || offset > 0xFFFF)
		return luaL_error(L, "invalid offset given");
	if(!lua_istable(L, 4))
		return luaL_error(L, "no value table given");
	int len = 0;
	uint16_t values[1024];
	lua_pushnil(L);
	while(len < 1024 && lua_next(L, 4) != 0) {
		values[len++] = lua_tointeger(L, -1);
		lua_pop(L, 1);
	}
	test_error(L, lbus_led_set_16bit(C, dst, offset, len, values), "lbus_led_set_16bit()");
	return 0;
}

static int llbus_led_commit(lua_State* L) {
	lbus_ctx *C = checklbusctx(L, 1);
	int dst = checkdst(L, 2);
	test_error(L, lbus_led_commit(C, dst), "lbus_led_commit()");
	return 0;
}

static int llbus_ping(lua_State* L) {
	lbus_ctx *C = checklbusctx(L, 1);
	int dst = checkdst(L, 2);
	int res = test_error(L, lbus_ping(C, dst), "lbus_ping()");
	lua_pushinteger(L, res);
	return 1;
}

static int llbus_reset_to_bootloader(lua_State* L) {
	lbus_ctx *C = checklbusctx(L, 1);
	int dst = checkdst(L, 2);
	test_error(L, lbus_reset_to_bootloader(C, dst), "lbus_reset_to_bootloader()");
	return 0;
}

static int llbus_reset_to_firmware(lua_State* L) {
	lbus_ctx *C = checklbusctx(L, 1);
	int dst = checkdst(L, 2);
	test_error(L, lbus_reset_to_firmware(C, dst), "lbus_reset_to_firmware()");
	return 0;
}

static int llbus_erase_config(lua_State* L) {
	lbus_ctx *C = checklbusctx(L, 1);
	int dst = checkdst(L, 2);
	test_error(L, lbus_erase_config(C, dst), "lbus_erase_config()");
	return 0;
}

static int llbus_get_config(lua_State* L) {
	lbus_ctx *C = checklbusctx(L, 1);
	int dst = checkdst(L, 2);
	int type = luaL_checkinteger(L, 3);
	if(type < 0 || type > 0xFFFF)
		return luaL_error(L, "bad type ID");
	luaL_checktype(L, 4, LUA_TBOOLEAN);
	int length = luaL_checkinteger(L, 5);
	if(lua_toboolean(L, 4)) {
		if(length == 1 || length == 2) {
			lua_pushinteger(L, test_error(L, lbus_get_config(C, dst, type, true, length, NULL), "lbus_get_config()"));
			return 1;
		} else if(length == 4) {
			uint32_t r;
			test_error(L, lbus_get_config(C, dst, type, true, length, &r), "lbus_get_config()");
			lua_pushinteger(L, (lua_Integer)r);
			return 1;
		} else {
			return luaL_error(L, "bad length given");
		}
	} else {
		if(length <= 0 || length > 4096)
			return luaL_error(L, "unsupported length given");
		uint8_t *buf = malloc(length);
		if(buf == NULL)
			return luaL_error(L, "cannot get memory");
		int size = test_error(L, lbus_get_config(C, dst, type, false, length, buf), "lbus_get_config()");
		lua_pushlstring(L, buf, size);
		free(buf);
		return 1;
	}
}

static int llbus_set_address(lua_State* L) {
	lbus_ctx *C = checklbusctx(L, 1);
	int dst = checkdst(L, 2);
	int address = luaL_checkinteger(L, 3);
	if(address < 1 || address > 127)
		return luaL_error(L, "invalid address given");
	lua_pushinteger(L, test_error(L, lbus_set_address(C, dst, address), "lbus_set_address()"));
	return 1;
}

static int llbus_read_memory(lua_State* L) {
	lbus_ctx *C = checklbusctx(L, 1);
	int dst = checkdst(L, 2);
	const uint32_t address = (uint32_t)luaL_checkinteger(L, 3);
	const int size = luaL_checkinteger(L, 4);
	if(size <= 0 || size > 4096)
		return luaL_error(L, "invalid size given");
	uint8_t *buf = malloc(size);
	if(buf == NULL)
		return luaL_error(L, "cannot claim memory");
	int r = test_error(L, lbus_read_memory(C, dst, address, size, buf), "lbus_read_memory()");
	lua_pushlstring(L, buf, r);
	lua_pushinteger(L, r);
	return 2;
}

static int llbus_flash_firmware(lua_State* L) {
	lbus_ctx *C = checklbusctx(L, 1);
	int dst = checkdst(L, 2);
	const char* path = luaL_checkstring(L, 3);
	lua_pushinteger(L, test_error(L, lbus_flash_firmware(C, dst, path), "lbus_flash_firmware()"));
	return 1;
}

static int llbus_tx(lua_State* L) {
	lbus_ctx *C = checklbusctx(L, 1);
	size_t size;
	const char* buf = luaL_checklstring(L, 2, &size);
	lua_pushinteger(L, test_error(L, lbus_tx(C, buf, (int)size), "lbus_tx()"));
	return 1;
}

static int llbus_rx(lua_State* L) {
	lbus_ctx *C = checklbusctx(L, 1);
	const int size = luaL_checkinteger(L, 2);
	if(size <= 0 || size > 4096)
		return luaL_error(L, "invalid size given");
	uint8_t *buf = malloc(size);
	if(buf == NULL)
		return luaL_error(L, "cannot claim memory");
	int r = test_error(L, lbus_rx(C, buf, size), "lbus_rx()");
	lua_pushlstring(L, buf, r);
	lua_pushinteger(L, r);
	return 2;
}

static const struct luaL_Reg lbus_methods[] = {
	{ "free",			llbus_free },
	{ "__gc",			llbus_free },
	{ "busmaster_echo",		llbus_busmaster_echo },
	{ "led_set_16bit",		llbus_led_set_16bit },
	{ "led_commit",			llbus_led_commit },
	{ "ping",			llbus_ping },
	{ "reset_to_bootloader",	llbus_reset_to_bootloader },
	{ "reset_to_firmware",		llbus_reset_to_firmware },
	{ "erase_config",		llbus_erase_config },
	{ "get_config",			llbus_get_config },
	{ "set_address",		llbus_set_address },
	{ "read_memory",		llbus_read_memory },
	{ "flash_firmware",		llbus_flash_firmware },
	{ "tx",				llbus_tx },
	{ "rx",				llbus_rx },
	{ NULL,				NULL }
};

static int llbus_open(lua_State* L) {
	lbus_ctx* C;
	test_error(L, lbus_open(&C), "lbus_open()");
	lbus_ctx* u = (lbus_ctx*) lua_newuserdata(L, sizeof(lbus_ctx*));
	u = C;	
	/* set up metatable */
	lua_newtable(L);
	lua_pushvalue(L, -1); // dup
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, lbus_methods);
	lua_setmetatable(L, -2);
	return 1;
}

static const struct luaL_Reg lbus_functions[] = {
	{ "open",			llbus_open },
	{ NULL,				NULL }
};

int luaopen_lualbuscomm(lua_State* L) {
	lua_newtable(L);
	luaL_register(L, NULL, lbus_methods);
	return 1;
}
