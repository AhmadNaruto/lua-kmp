#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "lua_html.h"
#include "lua_pcre.h"
#include "lua_json.h"
#include "lua_crypto.h"
#include "lua_utils.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *script_path = (argc > 1) ? argv[1] : "native/tests/test_novela_runtime.lua";

    lua_State *L = luaL_newstate();
    if (!L) {
        fprintf(stderr, "Error: failed to create Lua state\n");
        return 1;
    }

    luaL_openlibs(L);

    /* Open NoveLA native modules */
    luaopen_html(L);
    luaopen_pcre(L);
    luaopen_json(L);
    luaopen_crypto(L);
    luaopen_utils(L);

    if (luaL_dofile(L, script_path) != LUA_OK) {
        fprintf(stderr, "\n❌ %s\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }

    lua_close(L);
    return 0;
}
