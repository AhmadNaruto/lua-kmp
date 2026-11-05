#ifndef LUA_CRYPTO_H
#define LUA_CRYPTO_H

#include "lua.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers base64_encode, base64_decode, md5, sha256, and aes_decrypt into global L */
int luaopen_crypto(lua_State *L);

#ifdef __cplusplus
}
#endif

#endif /* LUA_CRYPTO_H */
