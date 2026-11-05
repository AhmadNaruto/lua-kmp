#ifndef LUA_JSON_H
#define LUA_JSON_H

#include "lua.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers json_parse and json_stringify into the global environment of L */
int luaopen_json(lua_State *L);

#ifdef __cplusplus
}
#endif

#endif /* LUA_JSON_H */
