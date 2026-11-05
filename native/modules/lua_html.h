#ifndef LUA_HTML_H
#define LUA_HTML_H

#include "lua.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers html_parse and DOM metatables into the Lua state L */
int luaopen_html(lua_State *L);

#ifdef __cplusplus
}
#endif

#endif /* LUA_HTML_H */
