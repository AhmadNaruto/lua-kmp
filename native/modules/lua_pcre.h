#ifndef LUA_PCRE_H
#define LUA_PCRE_H

#include "lua.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers regex_match and regex_replace into the global environment of L */
int luaopen_pcre(lua_State *L);

#ifdef __cplusplus
}
#endif

#endif /* LUA_PCRE_H */
